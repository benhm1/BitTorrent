/*

  managePeers.c - Function definitions for accepting new connections,
  destroying connections, and maintaining the peerList in the 
  torrentInfo struct.

*/

#include "managePeers.h"

void destroyPeer( struct peerInfo * peer, struct torrentInfo * torrent ) {

  if ( peer->type == BT_PEER ) {
    torrent->numPeers -= 1;
  }
  else if ( peer->type == BT_SEED ) {
    torrent->numSeeds -= 1;
  }
  else { 
    torrent->numUnknown -= 1;
  }
  logToFile( torrent, "STATUS Destroying connection: %s:%d\n", 
	     peer->ipString, peer->portNum);
    
  // Update chunk prevalence counts for the blocks this 
  // peer had.
  int i;
  int val;
  for ( i = 0; i < torrent->numChunks; i ++ ) {
    if ( !Bitfield_Get( peer->haveBlocks, i, &val ) &&
	 val ) {
      torrent->chunks[i].prevalence -- ;
      torrent->numPrevalenceChanges ++ ;
    }
  }


  close( peer->socket );
  Bitfield_Destroy( peer->haveBlocks );
  free( peer->incomingMessageData );
  SS_Destroy( peer->outgoingData );
  peer->defined = 0;

  return;

}


int getFreeSlot( struct torrentInfo * torrent ) {

  // Find a suitable slot for this peer
  int i;
  for ( i = 0; i < torrent->peerListLen; i ++ ) {
    if ( torrent->peerList[i].defined == 0 ) {
      return i ;
    }
  }
  torrent->peerList = realloc( torrent->peerList, 
			       2 * torrent->peerListLen * 
			       sizeof( struct peerInfo ) );
  if ( ! torrent->peerList ) {
    perror("realloc");
    exit(1);
  }
  for ( i = torrent->peerListLen; i < torrent->peerListLen*2; i ++ ) {
    torrent->peerList[i].defined = 0;
  }

  int retVal = torrent->peerListLen;
  torrent->peerListLen *= 2;

  return retVal;

}


void peerConnectedToUs( struct torrentInfo * torrent, int listenFD ) {

  struct sockaddr_in remote_addr;
  unsigned int socklen = sizeof(remote_addr);
  int newfd = accept(listenFD, (struct sockaddr*)& remote_addr, &socklen);
  if (newfd < 0 ) {
    perror("Error accepting connection");
    exit(1);
  }

  int slotIdx = getFreeSlot( torrent );

  struct peerInfo * this = & torrent->peerList[ slotIdx ] ;
  this->socket = newfd;
  strncpy( this->ipString, inet_ntoa( remote_addr.sin_addr ), 16 );
  this->portNum = ntohs(remote_addr.sin_port) ;

  initializePeer( this, torrent );

  this->status = BT_AWAIT_INITIAL_HANDSHAKE ;

  logToFile(torrent, 
	    "STATUS Accepted Connection from %s:%u\n", 
	    this->ipString, (unsigned int)this->portNum );

  torrent->numUnknown += 1;
  this->type = BT_UNKNOWN;

  return ;

}

int connectToPeer( struct peerInfo * this, 
		   struct torrentInfo * torrent, 
		   char * handshake) {
  int res;

  char * ip = this->ipString;
  unsigned short port = this->portNum;

  // Set up our socket
  int sock = socket(AF_INET, SOCK_STREAM, 0);
  if ( sock < 0 ) {
    perror( "socket");
    exit(1);
  }
  
  int error = nonBlockingConnect( ip, port, sock );

  if ( ! error ) {

    res = write( sock, handshake, 68 );
    if ( res != 68 ) {
      error = 1;
    }
    if ( res < 0 ) {
      perror("Handshake write: ");
      exit(1);
    }
  }
  else {
    // Just get rid of this slot
    this->defined = 0;
    close( sock );
    return -1;
  }
  
  

  this->socket = sock;

  initializePeer( this, torrent );

  logToFile( torrent, 
	     "HANDSHAKE INIT %s:%d\n", 
	     this->ipString, this->portNum );

  this->status = BT_AWAIT_RESPONSE_HANDSHAKE;
  this->type = BT_UNKNOWN;

  torrent->numUnknown += 1;

  return 0;

}

void initializePeer( struct peerInfo * this, struct torrentInfo * torrent ) {

  // Initialize our sending and receiving state and data structures
  this->peer_choking = 1;
  this->am_choking = 1;
  this->peer_interested = 0;
  this->am_interested = 0;
  
  this->haveBlocks = Bitfield_Init( torrent->numChunks );
  
  this->readingHeader = 1;
  this->incomingMessageRemaining = 68; // Length
  this->incomingMessageOffset = 0;
  // First message we expect is a handshake, which is 68 bytes
  this->incomingMessageData = Malloc( 68 ); 
  
  this->outgoingData = SS_Init();

  this->lastInterestedRequest = 0;
  this->lastWrite = 0;
  this->lastMessage = 0;

  this->numPendingSubchunks = 0;

  this->downloadAmt = 0;
  this->willUnchoke = 0;
  this->firstChokePass = 1;

  // Slot is taken
  this->defined = 1;

  return;

}

