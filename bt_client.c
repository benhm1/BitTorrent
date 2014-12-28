
/*
  bt_client.c - Function definitions for the main select loop and handling 
  I/O with connected peers and seeds.
*/

#include "common.h"

#include "utils/choke.h"
#include "bitfield/bitfield.h"
#include "StringStream/StringStream.h"
#include "utils/bencode.h"
#include "utils/percentEncode.h"
#include "timer/timer.h"
#include "messages/outgoingMessages.h"
#include "messages/incomingMessages.h"
#include "messages/tracker.h"
#include "utils/base.h"
#include "startup.h"
#include "managePeers.h"
#include "utils/algorithms.h"
#include "bt_client.h"

/*
  Force all tracker replies to include the peer
  at IP IP0.IP1.IP2.IP3:PORT
 */
#define TRACKER_RESP_HACK 
#ifdef  TRACKER_RESP_HACK
#define IP0  130
#define IP1  58
#define IP2  68
#define IP3  210
#define PORT 6800
#endif




void destroyTorrentInfo( ) { 
  int i; 
    
  struct torrentInfo * t = globalTorrentInfo;

  logToFile( t,  "SIGINT Received - Shutting down ... \n");
  printf("\n\nSIGINT Received - Shutting down ... \n");

  // Tell the tracker we're shutting down.
  doTrackerCommunication( t, TRACKER_STOPPED );

  logToFile( t, "SHUTDOWN Notified tracker we're stopping\n");
  printf("Notified tracker we're stopping\n");

  free( t->announceURL );
  free( t->trackerDomain );
  free( t->trackerIP );

  for ( i = 0; i < t->numChunks; i ++ ) {
    if (! t->chunks[i].have )  {
      free( t->chunks[i].data );
      free( t->chunks[i].subChunks );
    }
  }

  for ( i = 0; i < t->peerListLen; i ++ ) {
    if ( t->peerList[i].defined ) {
      destroyPeer( &t->peerList[i], t );
    }
  }

  printf("Freed data chunks and peer metadata structures\n");
  logToFile( t, "SHUTDOWN Freed data chunks and peer metadata structures\n");

  free( t->chunks );
  free( t->chunkOrdering );
  free( t->name );
  free( t->comment );
  free( t->infoHash );
  free( t->peerID );
  free( t->peerList );
  Bitfield_Destroy( t->ourBitfield );
  if ( timer_delete( t->timerTimeoutID ) ) {
    perror("timer_delete");
    logToFile( t, "SHUTDOWN Error deleting timer.\n");
  }
  if ( timer_delete( t->timerChokeID ) ) {
    perror("timer_delete");
    logToFile( t, "SHUTDOWN Error deleting timer.\n");
  }


  munmap( t->fileData, t->totalSize );

  printf("Unmapped file.\nClosing logfile.\n");
  logToFile( t, "SHUTDOWN Unmapped file.\n");
  logToFile( t, "SHUTDOWN Closing logfile.\n");

  if ( t->logFile ) {
    fclose( t->logFile );
  }

  free( t );

  printf("Have a nice day!\n");
  exit(1);
    
  return;

}


int setupReadWriteSets( fd_set * readPtr, 
			fd_set * writePtr, 
			struct torrentInfo * torrent ) {

  int i;

  int maxFD = 0;

  struct timeval tv;
  if ( gettimeofday( &tv, NULL ) ){
    perror("gettimeofday");
    exit(1);
  }

  for ( i = 0; i < torrent->peerListLen; i ++ ) {
    if ( torrent->peerList[i].defined ) {
      FD_SET( torrent->peerList[i].socket, readPtr );
      if ( maxFD < torrent->peerList[i].socket ) {
	maxFD = torrent->peerList[i].socket;
      }

      // We want to write to anybody who has data pending
      if ( torrent->peerList[i].outgoingData->size > 0 && 
	   tv.tv_sec - torrent->peerList[i].lastWrite > 2 ) {
	FD_SET( torrent->peerList[i].socket, writePtr ) ;
	if ( maxFD < torrent->peerList[i].socket ) {
	  maxFD = torrent->peerList[i].socket;
	}
      }
    } /* Peer is defined */
  }

  return maxFD ;


}

void handleWrite( struct peerInfo * this, struct torrentInfo * torrent ) {


  int ret; 
  struct timeval tv;
  if ( gettimeofday( &tv, NULL ) ){
    perror("gettimeofday");
    exit(1);
  }
  this->lastWrite = tv.tv_sec;

  ret = write( this->socket, this->outgoingData->head, 
	       this->outgoingData->size );
  if ( ret < 0 ) {
    perror("write");
    exit(1);
  }
  SS_Pop( this->outgoingData, ret );

  return;

}

void handleRead( struct peerInfo * this, struct torrentInfo * torrent ) {

  int ret;
  ret = read( this->socket, 
	      &this->incomingMessageData[ this->incomingMessageOffset ], 
	      this->incomingMessageRemaining );
  if ( ret < 0 ) {

    if ( errno == ECONNRESET ) {
      logToFile( torrent, 
		 "STATUS Connection from %s:%u was forcibly reset.\n", 
		 this->ipString, this->portNum );
      destroyPeer(this, torrent);
      return;
    }
    perror( "read" );
    exit(1);
  }
  if ( 0 == ret ) {
    // Peer closed our connection
    logToFile( torrent, "STATUS Connection from %s:%u was closed by peer.\n", 
	       this->ipString, (unsigned int) this->portNum );
    destroyPeer( this, torrent );
    return ;
  }
  
  this->incomingMessageRemaining -= ret;
  this->incomingMessageOffset += ret;

  if ( this->incomingMessageRemaining == 0 ) {
    handleFullMessage( this, torrent );
  }
  
  return;

}

void handleActiveFDs( fd_set * readFDs, 
		      fd_set * writeFDs, 
		      struct torrentInfo * torrent, 
		      int listeningSock ) {
  int i;

  if ( FD_ISSET( listeningSock, readFDs ) ) {
    peerConnectedToUs( torrent, listeningSock );
  }
  
  // Iterate twice in case an invalid read message leads us to 
  // close the socket while we still wanted to write to it.
  for ( i = 0; i < torrent->peerListLen; i ++ ) {
    struct peerInfo * this = &torrent->peerList[i] ;
    if ( this->defined ) {
      if ( FD_ISSET( this->socket, readFDs ) ) {
	handleRead( this, torrent );
      }
    }
  }

  for ( i = 0; i < torrent->peerListLen; i ++ ) {
    struct peerInfo * this = &torrent->peerList[i] ;
    if ( this->defined ) {
      if ( FD_ISSET( this->socket, writeFDs ) ) {
	handleWrite( this, torrent );
      }
    }
  }

}



void generateMessages( struct torrentInfo * t ) {



  int i,j, k,val, ret;

  val = 0;

  struct timeval cur;
  ret = gettimeofday( &cur, NULL );
  if ( ret ) {
    perror("gettimeofday");
    exit(1);
  }


  // If we are choked but they have a piece that 
  // we don't have, then send them an interest 
  // message.
  for ( i = 0; i < t->peerListLen; i ++ ){
    if ( ! t->peerList[i].defined ) {
      continue ; // Unused slot
    }
    for ( j = 0; j < t->numChunks; j ++ ) {

      struct chunkInfo * curPtr = 
	t->chunkOrdering[j];

      if ( t->peerList[i].numPendingSubchunks >= MAX_PENDING_SUBCHUNKS ) {
	break; // This peer already has enough outstanding requests
	// so we don't want to waste time getting more.
      }
    
      if ( ! curPtr->have ) {
	struct peerInfo* peerPtr = &(t->peerList[i]);
	if ( (! Bitfield_Get( peerPtr->haveBlocks, j, &val )) && val ) {
	  // Our peer has this chunk, and we want it.
	  t->peerList[i].am_interested = 1;
	  
	  // This peer is choking us. Tell them we'll download from them if
	  // they unchoke us.
	  if ( t->peerList[i].peer_choking && 
	       (cur.tv_sec - t->peerList[i].lastInterestedRequest) > 5) {
	    sendInterested( &t->peerList[i], t );
	    t->peerList[i].lastInterestedRequest = cur.tv_sec;
	    break;
   	  }

	  // This peer is not choking us. Request up to 
	  // MAX_PENDING_SUBCHUNKS subchunks from them.
	  else if ( ! t->peerList[i].peer_choking ) {
	    for ( k = 0; k < curPtr->numSubChunks; k ++ ) {
	      if ( t->peerList[i].numPendingSubchunks >= MAX_PENDING_SUBCHUNKS ) {
		break;
	      }
	      if ( curPtr->subChunks[k].have == 0 &&
		   cur.tv_sec - curPtr->subChunks[k].requestTime > 20 ) {
		sendPieceRequest( &t->peerList[i], t, j, k );
		curPtr->subChunks[k].requestTime = cur.tv_sec;
		curPtr->requested = 1;
	      }
	    }
	  }

	  else { 
	    // We're choked, we can't ask again to be unchoked,
	    // and we can't request things while choked.
	    // So, do nothing. 
	    break;
	  }

	    
	} /* If they have this block */
      }   /* If we don't have this block */

    } /* For all blocks */
  }  /* For all peers */



}



void printStatus( struct torrentInfo * t ) {

  int i;
  struct timeval tv;
  if ( gettimeofday( &tv, NULL ) ){
    perror("gettimeofday");
    exit(1);
  }
  if ( tv.tv_sec - t->lastPrint < 1 ) {
    return;
  }
  t->lastPrint = tv.tv_sec;
  

  system("clear");

  for ( i = 0; i < t->numChunks; i ++ ) {

    if ( i % 80 == 0 ) {
      printf("\n");
    }

    if ( t->chunks[i].have ) {
      printf("X");
    }
    else if ( t->chunks[i].requested ) {
      printf("x");
    }
    else {
      printf(".");
    }
  }
  printf("\n\n");

  printf("  Number of Peers: %d\n", t->numPeers);
  printf("  Number of Seeds: %d\n", t->numSeeds);
  printf("Number of Unknown: %d\n", t->numUnknown);
  printf("  Download Amount: %.1f kB\n", 1.0*t->numBytesDownloaded / 1000 );
  printf("    Upload Amount: %.1f kB\n", 1.0*t->numBytesUploaded / 1000 );
  printf("\n===================================\n");
  logToFile( t, "STATUS UPDATE  Downloaded:%.1f, Uploaded %.1f\n",
	     1.0*t->numBytesDownloaded/1000, 
	     1.0*t->numBytesUploaded/1000 );
	     

}

int main(int argc, char ** argv) {

  int ret;

  struct argsInfo * args = parseArgs( argc, argv );

  be_node* data = load_be_node( args->fileName );
  
  int listeningSocket = setupListeningSocket( args );

  struct torrentInfo * t = processBencodedTorrent( data, args );
  be_free( data );
  freeArgs( args );

  loadPartialResults( t );

  globalTorrentInfo = t;

  doTrackerCommunication( t, TRACKER_STARTED );

  // Check in with the tracker server in 30 seconds,
  // and then parse the tracker response for subsequent
  // checkin delays
  setupSignals( SIGALRM, trackerCheckin );
  alarm( 30 );  

  // When the user hits Ctrl^C, exit
  setupSignals( SIGINT, destroyTorrentInfo );

  // Every 30 seconds, check for idle connections
  t->timerTimeoutID = setupSignal( SIGUSR1, timeoutDetection, 30, t );

  // Every 10 seconds, run the choking algorithm
  t->timerChokeID = setupSignal( SIGUSR2, chokingHandler, 10, t );
  


  // By this point, we have a list of peers we are connected to.
  // We can now start our select loop
  fd_set readFDs, writeFDs;
  struct timeval tv; 

  while ( 1 ) {

    FD_ZERO( &readFDs );
    FD_ZERO( &writeFDs );

    tv.tv_sec = 2;
    tv.tv_usec = 0;

    blockSignal( SIGUSR1 );
    blockSignal( SIGUSR2 );
    blockSignal( SIGALRM );

    sortChunks( t ) ;

    generateMessages( t );

    // We always want to accept new connections
    FD_SET( listeningSocket, &readFDs );    

    int maxFD = setupReadWriteSets( &readFDs, &writeFDs, t );
    
    maxFD = ( maxFD > listeningSocket ? maxFD : listeningSocket );


    ret = select( maxFD + 1, &readFDs, &writeFDs, NULL, &tv );
    if ( ret < 0 ) {
      if ( errno == EINTR ) {
	// We handled tracker communication while waiting here
	continue;
      }
      perror( "select" );
      exit(1);
    }

    handleActiveFDs( &readFDs, &writeFDs, t, listeningSocket );
    
    printStatus( t );

    // Handle any pending signals 

    // Idle connections timeout
    unblockSignal( SIGUSR1 );

    // Choking / Unchoking algorithm
    unblockSignal( SIGUSR2 );

    // Tracker checkins
    unblockSignal( SIGALRM );

  }

  // Never get here


  close( listeningSocket );


  return 0;

}
