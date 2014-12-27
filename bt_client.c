
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



void initializePeer( struct peerInfo * thisPtr, struct torrentInfo * torrent );
void destroyPeer( struct peerInfo * peer, struct torrentInfo * torrent ) ;
int connectToPeer( struct peerInfo * this, 
		   struct torrentInfo * torrent , 
		   char * handshake) ;
int getFreeSlot( struct torrentInfo * torrent ) ;
char * createTrackerMessage( struct torrentInfo * torrent, int msgType );
typedef void handler_t(int);
handler_t * setupSignals(int signum, handler_t * handler);
int doTrackerCommunication( struct torrentInfo * t, int type );
int nonBlockingConnect( char * ip, unsigned short port, int sock ) ;
int parseTrackerResponse( struct torrentInfo * torrent, 
			  char * response, int responseLen );
void sendUnchoke( struct peerInfo * this, struct torrentInfo * t );
void sendChoke( struct peerInfo * this, struct torrentInfo * t );
char * generateID();
unsigned char * computeSHA1( char * data, int size ) ;
void usage(FILE * file);






int min( int a, int b ) { return a > b ? b : a; }

void * Malloc( size_t size ) {

  void * toRet = malloc( size );
  if ( ! toRet ) {
    perror( "malloc" );
  }
  return toRet;

}

int sortChunksComparator( const void * a, const void * b ) {
  struct chunkInfo * ca = (struct chunkInfo *) a;
  struct chunkInfo * cb = (struct chunkInfo *) b;
  
  return ca->prevalence - cb->prevalence  ;
  
}

void sortChunks( struct torrentInfo * t ) {

  if ( t->numPrevalenceChanges > 5 ) {
    qsort( t->chunkOrdering, t->numChunks, 
	   sizeof( struct chunkInfo * ), 
	   sortChunksComparator );
    t->numPrevalenceChanges = 0;
  } 
}

handler_t * setupSignals(int signum, handler_t * handler) {  
  struct sigaction action, old_action; 
  action.sa_handler = handler;   
  sigemptyset(&action.sa_mask);  
  action.sa_flags = SA_RESTART; 
  if (sigaction(signum, &action, &old_action) < 0) {  
    perror("sigaction"); 
  }
  return (old_action.sa_handler);     
}  



void timeoutDetection( int sig, siginfo_t * si, void * uc ) {

  int i;

  struct torrentInfo* t = (struct torrentInfo *) si->si_value.sival_ptr ;

  struct timeval tv;
  if ( gettimeofday( &tv, NULL ) ) {
    perror("gettimeofday");
    exit(1);
  }


  for ( i = 0; i < t->peerListLen; i ++ ) {
    if ( t->peerList[i].defined &&
	 tv.tv_sec - t->peerList[i].lastWrite > 3 &&
	 tv.tv_sec - t->peerList[i].lastMessage > MAX_TIMEOUT_WAIT ) {
      /*
	Stop talking to people who we've sent stuff to a while ago and
	they haven't responded to us in a reasonable amount of time.
       */
      logToFile( t, "STATUS TIMEOUT %s:%d\n", 
		 t->peerList[i].ipString, t->peerList[i].portNum );
      destroyPeer( &t->peerList[i], t );
    }
  }

  return;

}




// Given a hostname, returns its IP address or exits if DNS lookup fails
void lookupIP( char * hostname, char * IP ) {

  struct addrinfo * result;
  if (getaddrinfo(hostname, 0, 0, &result)) {
    printf("\nError: Invalid hostname provided: %s\n", hostname);
    exit(1);
  }
  
  if (result != NULL) {
    // From sample code - converts the address to a char array and copies it
    strcpy(IP, inet_ntoa(((struct sockaddr_in*)result->ai_addr)->sin_addr));
  }
  else {
    printf("\nError: Could not resolve hostname to an IP address.\n");
    exit(1);
  }
  freeaddrinfo(result);
}



unsigned char * computeSHA1( char * data, int size ) {

  unsigned char * toRet = Malloc( 20 ); // SHA_DIGEST_LENGTH
  SHA1( (unsigned char*) data, size, toRet );
  return toRet;

}


struct torrentInfo* processBencodedTorrent( be_node * data, 
					    struct argsInfo * args ) {

  int i, j;
  int chunkHashesLen;
  char * chunkHashes;


  struct torrentInfo * toRet = Malloc( sizeof( struct torrentInfo ) );

  for ( i = 0; data->val.d[i].val != NULL; i ++ ) {

    // Keys can include: announce (string) and info (dict), among others
    if ( 0 == strcmp( data->val.d[i].key, "announce" ) ) {
      toRet->announceURL = strdup( data->val.d[i].val->val.s );
      printf("Announce URL: %s\n", toRet->announceURL);

      toRet->trackerDomain = strdup( toRet->announceURL );
      char * endPtr = strrchr( toRet->trackerDomain, ':' );
      if ( ! endPtr ) {
	printf("Error - Improperly formatted Announce URL - no ':'\n");
	exit(1);
      }
      *endPtr = '\0';

      char *portStart, *portEnd;
      portStart = strdup(endPtr + 1);
      portEnd = strchr( portStart, '/' );
      if ( ! portEnd ) {
	printf("Error - Improperly formatted Annoucen URL - no port after ':'\n");
	exit(1);
      }
      *portEnd = '\0';
      toRet->trackerPort = atoi( portStart );
      free( portStart );


      if ( ! strncmp( toRet->trackerDomain,  "http://", 7 ) ) {
	char * withoutHTTP = strdup( toRet->trackerDomain + 7 );
	free( toRet->trackerDomain );
	toRet->trackerDomain = withoutHTTP;
      }


      if ( toRet->trackerPort == 0 ) {
	printf("Error - Invalid announce port number. Assuming 6969.\n");
      }

      printf("Tracker Domain: %s\n", toRet->trackerDomain );
      printf("Tracker Port:   %d\n", toRet->trackerPort );
      toRet->trackerIP = Malloc( 25 * sizeof(char) );

      lookupIP( toRet->trackerDomain, toRet->trackerIP );
      printf("Tracker Domain IP: %s\n", toRet->trackerIP );

    }
    else if ( 0 == strcmp( data->val.d[i].key, "info" ) ) {

      be_node * infoIter = data->val.d[i].val;
      for ( j = 0; infoIter->val.d[j].val != NULL; j ++ ) {

	char * key = infoIter->val.d[j].key;
	if ( 0 == strcmp( key, "length" ) ) {
	  toRet->totalSize = infoIter->val.d[j].val->val.i;
	  printf("Torrent Size: %d\n", toRet->totalSize );
	}
	else if ( 0 == strcmp( key, "piece length" ) ) {
	  toRet->chunkSize = infoIter->val.d[j].val->val.i;
	  printf("Chunk Size: %d\n", toRet->chunkSize );
	}
	else if ( 0 == strcmp( key, "name" ) ) {
	  int nameLen = strlen(infoIter->val.d[j].val->val.s ) + 
	    strlen( args->saveFile ) + 3;
	  toRet->name = Malloc( nameLen * sizeof(char) );
	  snprintf( toRet->name, nameLen - 1, "%s/%s", 
		    args->saveFile, infoIter->val.d[j].val->val.s );
	  printf("Torrent Name: %s\n", toRet->name );
	}
	else if ( 0 == strcmp( key, "pieces" ) ) {
	  chunkHashesLen = be_str_len( infoIter->val.d[j].val );
	  chunkHashes = Malloc( chunkHashesLen );
	  memcpy( chunkHashes, infoIter->val.d[j].val->val.s, 
		  chunkHashesLen );
	  printf( "Hashes Length: %d\n", chunkHashesLen );
	}
	else {
	  printf("Ignoring Field: %s\n", key );
	}
      }
    }
    else if ( 0 == strcmp( data->val.d[i].key, "creation date" ) ) {
      toRet->date = data->val.d[i].val->val.i;
      printf("Creation Date: %d\n", toRet->date);
    }
    else if ( 0 == strcmp( data->val.d[i].key, "comment" ) ) {
      toRet->comment = strdup( data->val.d[i].val->val.s );
      printf("Comment: %s\n", toRet->comment);
    }
    else {
      printf("Ignoring Field : %s\n", data->val.d[i].key );
    }
  }

  toRet->completed = 0;

  // Now, set up the chunks for the torrent
  int numChunks = toRet->totalSize/toRet->chunkSize + 
    ( toRet->totalSize % toRet->chunkSize ? 1 : 0 );
  toRet->numChunks = numChunks;
  toRet->chunks = Malloc( numChunks * sizeof(struct chunkInfo) );
  toRet->chunkOrdering = Malloc( numChunks * sizeof( struct chunkInfo * ) );
  toRet->numPrevalenceChanges = 0;
  for ( i = 0; i < numChunks ; i ++ ) {
    toRet->chunkOrdering[i] = &toRet->chunks[i];
    memcpy( toRet->chunks[i].hash, chunkHashes + 20*i, 20 );
    toRet->chunks[i].size = ( i == numChunks - 1 ? 
			      toRet-> totalSize % toRet->chunkSize 
			      : toRet->chunkSize ) ;
    toRet->chunks[i].prevalence = 0;
    toRet->chunks[i].have = 0;
    toRet->chunks[i].requested = 0;
    toRet->chunks[i].data = Malloc( toRet->chunks[i].size ) ;


    int subChunkSize = 1 << 14;
    int numSubChunks = 
      ( toRet->chunks[i].size + subChunkSize - 1 ) / subChunkSize;
    toRet->chunks[i].numSubChunks = numSubChunks;
    toRet->chunks[i].subChunks = 
      Malloc( numSubChunks * sizeof( struct subChunk ) );
    for ( j = 0; j < numSubChunks; j ++ ) {
      toRet->chunks[i].subChunks[j].start = j * subChunkSize;
      toRet->chunks[i].subChunks[j].end   = 
	min( (j+1) * subChunkSize, toRet->chunks[i].size );
      toRet->chunks[i].subChunks[j].len   = 
	toRet->chunks[i].subChunks[j].end - toRet->chunks[i].subChunks[j].start ;
      toRet->chunks[i].subChunks[j].have       = 0;
      toRet->chunks[i].subChunks[j].requested  = 0; 
      toRet->chunks[i].subChunks[j].requestTime  = 0; 
    }

  }

  // Initialize our bitfield
  toRet->ourBitfield = Bitfield_Init( toRet->numChunks );
  free( chunkHashes );

  

  // Finally, read in the info dictionary so that we can compute the hash
  // for our connection to the tracker. 
  FILE * fd = fopen("infoDict.bencoding", "rb");
  fseek(fd, 0, SEEK_END);
  long fsize = ftell(fd);
  fseek(fd, 0, SEEK_SET);

  char *string = Malloc(fsize);
  long numRemaining = fsize;
  while ( numRemaining > 0 ) {
    long numRead = fread(string, 1, fsize, fd);
    numRemaining -= numRead;
  }  

  fclose(fd);

  toRet->infoHash = computeSHA1( string, fsize );
  free( string );

  // Remove the file infoDict.bencoding, since we don't need it anymore
  if ( remove( "infoDict.bencoding" ) ) {
    perror("remove");
    printf("Error removing 'infoDict.bencoding' file.\n"
	   "Please delete it manually at your convenience.\n");
  }

  // Initialize the peer ID;
  toRet->peerID = Malloc( 20 );
  memcpy( toRet->peerID, args->nodeID, 20 );
  
  // Initialize number of peers and seeds
  toRet->numPeers = 0;
  toRet->numSeeds = 0;
  toRet->numUnknown = 0;
  toRet->maxPeers = args->maxPeers;

  // Initialize upload / download stats
  toRet->numBytesUploaded = 0;
  toRet->numBytesDownloaded = 0;

  toRet->bindAddress = args->bindAddress;
  toRet->bindPort    = args->bindPort;

  toRet->lastPrint = 0;

  // Initialize the memory mapped file where we will store results
  mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
  int saveFile = open(toRet->name,
		      O_RDWR | O_CREAT , mode);
  if ( saveFile < 0 ) {
    perror("open");
    exit(1);
  }
  if ( ftruncate( saveFile, toRet->totalSize ) ) {
    perror("ftruncate");
  }

  toRet->fileData = mmap( NULL,             // Kernel chooses placement
			  toRet->totalSize, // Length of mapping
			  PROT_READ | PROT_WRITE, // Permissions,
			  MAP_SHARED, // Updates visible on system
			  saveFile, 
			  0 );
			  

  // Initialize our log file
  FILE * logFile = 
    fopen( args->logFile, "w+" ) ;
  if ( logFile < 0 ) {
    perror("open");
    exit(1);
  }
  toRet->logFile = logFile;

  // Get our startup time
  struct timeval tv;
  if ( gettimeofday( &tv, NULL ) ) {
    perror("gettimeofday");
    exit(1);
  }
  
  toRet->timer = tv.tv_sec * 1000000 + tv.tv_usec;

  toRet->timerTimeoutID = 0;
  toRet->timerChokeID = 0;


  toRet->peerList = Malloc( 30 * sizeof( struct peerInfo ) );
  toRet->peerListLen = 30;
  for ( i = 0; i < 30; i ++ ) {
    toRet->peerList[i].defined = 0;
  }
  toRet->optimisticUnchoke = NULL;
  toRet->chokingIter = 0;

  return toRet;

}



void logToFile( struct torrentInfo * torrent, const char * format, ... ) {

  va_list args;
  va_start( args, format );

  struct timeval tv;
  if ( gettimeofday( &tv, NULL ) ) {
    perror("gettimeofday");
    exit(1);
  }

  unsigned long long msDiff = 
    tv.tv_sec * 1000000 + tv.tv_usec - torrent->timer ;

  fprintf( torrent->logFile, "[%10.4f] ", 1.0 * msDiff / 1000000 );

  vfprintf( torrent->logFile, format, args );


}


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

int nonBlockingConnect( char * ip, unsigned short port, int sock ) {

  struct sockaddr_in addr;
  addr.sin_family = AF_INET;
  addr.sin_port = htons( port ); 
  addr.sin_addr.s_addr = inet_addr( ip ) ;
 
  // Set socket to nonblocking for connect, in case it fails
  int flags = fcntl( sock, F_GETFL );
  int res = fcntl(sock, F_SETFL, O_NONBLOCK);  // set to non-blocking
  if ( res < 0 ) {
    perror("fcntl");
    exit(1);
  }

  res = connect( sock, (struct sockaddr*) &addr, sizeof(addr) );
  // By default, since we are non blocking, we will return 
  // EINPROGRESS. The canonical way to check if connect() was 
  // successful is to call select() on the file descriptor
  // to see if we can write to it. This allows us to use a 
  // timeout and cancel the operation while remaining non-blocking
  // See: http://developerweb.net/viewtopic.php?id=3196
  int error = 0;

  if (res < 0) { 
    if (errno == EINPROGRESS) { 
      fd_set myset;
      struct timeval tv;
      tv.tv_sec = 3; 
      tv.tv_usec = 0;
      FD_ZERO(&myset); 
      FD_SET(sock, &myset); 
      res = select(sock+1, NULL, &myset, NULL, &tv); 
      if (res < 0 && errno != EINTR) { 
	perror("select");
	exit(1);
      } 
      else if (res > 0) { 
	// Socket selected for write 
	int valopt;
	unsigned int lenInt = sizeof(int);
	if (getsockopt(sock, SOL_SOCKET, SO_ERROR, (void*)(&valopt), &lenInt) < 0) { 
	  // Error in getsockopt() 
	  error = 1;
	} 
	// Check the value returned... 
	if (valopt) { 
	  // Error in delayed connection() 
	  error = 1;
	} 
      }
      else { 
	// Timeout in select()
	error = 1;
      }
    } 
    else { 
      // Error connecting address
      error = 1;
    } 
  }


  // And, make it blocking again
  res = fcntl( sock, F_SETFL, flags );
  if ( res < 0 ) {
    perror("fcntl set blocking");
    exit(1);
  }


  return error;

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

int setupListeningSocket( struct argsInfo * args ) {

  // Initialize the socket that our peers will connect to
  /* Create the socket that we'll listen on. */
  int serv_sock = socket(AF_INET, SOCK_STREAM, 0);
  
  /* Set SO_REUSEADDR so that we don't waste time in TIME_WAIT. */
  int val = 1;
  val = setsockopt(serv_sock, SOL_SOCKET, SO_REUSEADDR, &val,
                   sizeof(val));
  if (val < 0) {
    perror("Setting socket option failed");
    exit(1);
  }

  struct sockaddr_in addr;
  addr.sin_family = AF_INET;
  addr.sin_port = htons( args->bindPort );
  addr.sin_addr.s_addr = args->bindAddress ;
  
  /* Bind our socket and start listening for connections. */
  val = bind(serv_sock, (struct sockaddr*)&addr, sizeof(addr));
  if(val < 0) {
    perror("Error binding to port");
    exit(1);
  }
  
  val = listen(serv_sock, BACKLOG);
  if(val < 0) {
    perror("Error listening for connections");
    exit(1);
  }

  return serv_sock;

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

void freeArgs( struct argsInfo * args ) {

  free( args->fileName );
  free( args->saveFile );
  free( args->logFile );
  free( args->nodeID );
  free( args );

  return;
}

struct argsInfo * parseArgs( int argc, char ** argv ) {




  int ch;
  struct in_addr in;
  struct argsInfo * toRet = Malloc( sizeof( struct argsInfo ) );
  // Set default values
  toRet->saveFile = strdup("./");
  toRet->logFile = strdup("./bt-client.log");
  toRet->nodeID = generateID();

  toRet->fileName = NULL;

  toRet->maxPeers = 30;
  toRet->bindAddress = INADDR_ANY;
  toRet->bindPort = 6881;

  while ((ch = getopt(argc, argv, "ht:p:s:l:I:m:b:")) != -1) {
    switch (ch) {
    case 'h': //help                                                                     
      usage(stdout);
      exit(0);
      break;
    case 't' : // torrent file name
      toRet->fileName = strdup( optarg );
      break;
    case 's': //save file
      free( toRet->saveFile );
      toRet->saveFile = strdup( optarg );
      break;
    case 'l': //log file
      free( toRet->logFile );
      toRet->logFile = strdup( optarg );
      break;
    case 'I': 
      free( toRet->nodeID );
      toRet->nodeID = Malloc( 20 );
      strncpy( toRet->nodeID, optarg, 20 );
      break;
    case 'm' : // Max peers
      toRet->maxPeers = atoi(optarg);
      break;
    case 'b' :
      if ( inet_aton( optarg, &in ) == 0 ) {
	toRet->bindAddress = in.s_addr;
      }
      break;
    case 'p' :
      toRet->bindPort = atoi( optarg );
      break;
    default:
      fprintf(stderr,"ERROR: Unknown option '-%c'\n",ch);
      usage(stdout);
      exit(1);
    }                                                            
  }

  if ( toRet->fileName == NULL ) {
    usage(stdout);
    exit(1);
   
  }



  return toRet;


}

void usage(FILE * file){


  if(file == NULL){
    file = stdout;
  }

  fprintf(file,
          "bt-client [OPTIONS] file.torrent\n"
          "  -h          \t Print this help screen\n"
          "  -t torrent  \t Torrent file name (required)\n"
	  "  -b ip       \t Bind to this ip for incoming connections (dflt=INADDR_ANY)\n"
	  "  -p port     \t Bind to this port for incoming connections (dflt=6881)\n"
          "  -s save_file\t Save the torrent in directory save_dir (dflt: .)\n"
          "  -l log_file \t Save logs to log_file (dflt: bt-client.log)\n"
          "  -I id       \t Set the node identifier to id (dflt: random)\n"
          "  -m max_num  \t Max number of peers to connect to at once (dflt:25)\n"
	  );

}


char * generateID() {


 
 // Return a SHA1 hash of the concatenation of 
  // our IP address, startup time, and random
  char starter[128];
  memset( starter, 0, 128 );
  starter[127] = '\0';


  /* Get our startup time */
  struct timeval tv;
  if ( gettimeofday( &tv, NULL ) ) {
    perror("gettimeofday");
    exit(1);
  }
  snprintf( starter, 127, "%ld%lu", tv.tv_sec, tv.tv_usec );
  
  /* 
     Get our IP address

     Adapted from 
     http://man7.org/linux/man-pages/man3/getifaddrs.3.html 
  */
  struct ifaddrs *ifaddr, *ifa;
  int family, s, n;
  char host[NI_MAXHOST];

  if (getifaddrs(&ifaddr) == -1) {
    perror("getifaddrs");
    exit(1);
  }

  for (ifa = ifaddr, n = 0; ifa != NULL; ifa = ifa->ifa_next, n++) {
    if (ifa->ifa_addr == NULL) {
      continue;
    }

    family = ifa->ifa_addr->sa_family;
    /* For an AF_INET* interface address, display the address */
    if (family == AF_INET || family == AF_INET6) {
      s = getnameinfo(ifa->ifa_addr,
		      (family == AF_INET) ? sizeof(struct sockaddr_in) :
		      sizeof(struct sockaddr_in6),
		      host, NI_MAXHOST,
		      NULL, 0, NI_NUMERICHOST);
      if (s != 0) {
	printf("getnameinfo() failed: %s\n", gai_strerror(s));
	exit(1);
      }
      strncat( starter, host, 127 - strlen(host) );
    }
  }
  freeifaddrs(ifaddr);


  return (char*)computeSHA1( starter, 128 );
 
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

void loadPartialResults( struct torrentInfo * t ) {
  int i;
  int numExisting = 0;

  for( i = 0; i < t->numChunks; i ++ ) {
    unsigned char * hash = computeSHA1( &t->fileData[ i * t->chunkSize ],
					t->chunks[i].size );
    if ( ! memcmp( hash, t->chunks[i].hash, 20 ) ) {
      // Final file contents are valid for this block
      t->chunks[i].have = 1;
      Bitfield_Set( t->ourBitfield, i );
      
      free( t->chunks[i].subChunks );
      free( t->chunks[i].data );
      t->chunks[i].data = 
	&t->fileData[ i * t->chunkSize ];
      numExisting ++;
      t->numBytesDownloaded += t->chunks[i].size;
    }
    free( hash );
  }
  
  logToFile(t, "INIT Validated %d/%d blocks from the torrent.\n",
	    numExisting, t->numChunks );

  return;

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
