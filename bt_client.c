#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

#include <string.h>

#include "bencode.h"
#include <assert.h>
#include <openssl/sha.h> //hashing pieces
#include "percentEncode.h"

#include <netdb.h>
#include <time.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <sys/signal.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>

#include <fcntl.h>


#include "StringStream/StringStream.h"
#include "bitfield.h"

//#define EAGLE_HACK 1

/*
  Todos - 

  Handle keepalive messages and timeout messages.
  Handle interested messages by unchoking, up to a certain limit

  Fix log printing

  Parse command line arguments
  Allow for restarts

*/

#define BT_CONNECTED 1
// They connected to us; we should get a handshake from them
#define BT_AWAIT_INITIAL_HANDSHAKE 2  
// We sent them a handshake; they should send one back
#define BT_AWAIT_RESPONSE_HANDSHAKE 3
#define BT_AWAIT_BITFIELD 4
#define BT_RUNNING 5

#define BT_PEER 1
#define BT_SEED 2

#define TRACKER_STARTED 1
#define TRACKER_STOPPED 2
#define TRACKER_COMPLETED 3
#define TRACKER_STATUS 4

#define PEER_LISTEN_PORT 6881
#define BACKLOG 20

#define MAX_PENDING_SUBCHUNKS 10

typedef void handler_t(int);

struct subChunk {

  int start;
  int end;
  int len;
  int have;
  int requested;
  int requestTime;

};


struct chunkInfo {

  int size;
  int have;
  int requested; 
  char * data;
  char * shadow;
  struct timeval tv;
  char hash[20];
  int numSubChunks;
  struct subChunk * subChunks;

};

/*
struct trackerInfo {

  int socket;
  int lastRequestTime;
  int waitTime;
  StringStream * in;
  StringStream * out;

};
*/

struct torrentInfo {

  char * announceURL;   // Full tracker URL from .torrent file
  char * trackerDomain; // Truncated URL for DNS queries
  char * trackerIP;     // String IP of tracker server

  // Torrent Chunk Bookkeeping
  int totalSize;
  int chunkSize;
  int numChunks;  
  struct chunkInfo * chunks;
  
  // Torrent File Information
  char * name;
  char * comment; 
  int date;

  char * fileData;

  // Hash of bencoded dictioanry in .torrent file
  unsigned char * infoHash;

  char * peerID;

  struct peerInfo * peerList;
  int peerListLen;
 
  int numPeers;
  int numSeeds;

  long long numBytesDownloaded;
  long long numBytesUploaded  ;

  int lastPrint;

  // Startup time ms
  unsigned long long timer;
  
  FILE * logFile;

  Bitfield * ourBitfield;

};


struct peerInfo {

  int defined; // Is this slot in use or not

  char ipString[16];
  unsigned short portNum;
  int socket ;

  int status ;

  // Boolean state variables
  int peer_choking ;
  int am_choking ;
  int peer_interested ;
  int am_interested ;

  // Bitfield of their blocks
  Bitfield * haveBlocks ;

  int readingHeader;
  int incomingMessageRemaining ;
  char * incomingMessageData ;

  int incomingMessageOffset;

  StringStream * outgoingData ;

  int lastInterestedRequest;
  int lastWrite;

  int numPendingSubchunks;

  int type;

};

// Global variable for signal handling
struct torrentInfo * globalTorrentInfo;

void initializePeer( struct peerInfo * thisPtr, struct torrentInfo * torrent );
void destroyPeer( struct peerInfo * peer, struct torrentInfo * torrent ) ;
int connectToPeer( struct peerInfo * this, struct torrentInfo * torrent , char * handshake) ;
int getFreeSlot( struct torrentInfo * torrent ) ;
char * createTrackerMessage( struct torrentInfo * torrent, int msgType );
handler_t * setupSignals(int signum, handler_t * handler);
int doTrackerCommunication( struct torrentInfo * t, int type );
int nonBlockingConnect( char * ip, unsigned short port, int sock ) ;
int parseTrackerResponse( struct torrentInfo * torrent, char * response, int responseLen );
void sendUnchoke( struct peerInfo * this, struct torrentInfo * t );

int min( int a, int b ) { return a > b ? b : a; }

void * Malloc( size_t size ) {

  void * toRet = malloc( size );
  if ( ! toRet ) {
    perror( "malloc" );
  }
  return toRet;

}


handler_t * setupSignals(int signum, handler_t * handler) {                                struct sigaction action, old_action;                                                     action.sa_handler = handler;                                                             sigemptyset(&action.sa_mask);                                                            action.sa_flags = SA_RESTART;                                                            if (sigaction(signum, &action, &old_action) < 0) {                                         perror("sigaction");                                                                   }                                                                                        return (old_action.sa_handler);                                                        }                  

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

void trackerCheckin( int sig ) {

  printf("Communicating with tracker ... \n");
  int nextCheckin = doTrackerCommunication( globalTorrentInfo, TRACKER_STATUS );
  // TODO - use real value
  nextCheckin = 30;
  alarm( nextCheckin );
  printf("Done communicating with tracker ...\n");
  return;

}

int doTrackerCommunication( struct torrentInfo * t, int type ) {

  // Connect to the tracker server
  int sock = socket(AF_INET, SOCK_STREAM, 0);
  if ( sock < 0 ) {
    perror( "socket");
    exit(1);
  }
  
  int error = nonBlockingConnect( t->trackerIP, 6969, sock );
  if ( error ) {
    perror("connect to tracker");
    return 60;
  }

  // Craft a message
  char * request = createTrackerMessage( t , type );

  // Send our message
  int offset, len, ret;
  offset = 0;
  len = strlen( request );
  while ( offset < len ) {
    ret = write( sock, &request[offset], len - offset );
    if ( ret < 0 ) {
      perror("write to tracker");
      return 60;
    }
    offset += ret;
  }

  free( request );

  // Receive until we get a disconnect
  char buf[2048];
  buf[2047] = '\0';
  offset = 0;
  ret = 1;
  while ( ret > 0 ) {
    ret = read( sock, &buf[offset], 2047 - offset );
    if ( ret < 0 ) {
      perror("read from tracker");
      return 60;
    }
    if ( ret == 0 ) {
      break;
    }
    offset += ret;
  }

  #ifdef EAGLE_HACK

  // Hack for eagle
  char fake[128];
  char * ptr = fake;
  strncpy( &fake[0], "8:intervali10e5:peers6:", 127);
  ptr += strlen( fake );
  char fake1 = 130;
  memcpy( ptr, &fake1, 1 );
  fake1 = 58;
  memcpy( ptr+1, &fake1, 1 );
  fake1 = 68;
  memcpy( ptr+2, &fake1, 1 );
  fake1 = 210;
  memcpy( ptr+3, &fake1, 1 );
  short fakePort = htons(6881);
  memcpy( ptr+4, &fakePort, 2 );

  int fakeLen = ptr + 6 - fake;
  int delay = parseTrackerResponse( t, fake, fakeLen );

  #else
  int delay = parseTrackerResponse( t, buf, offset );
  #endif
  
  


  return (delay == 0 ? 60 : delay ) ; // If something went wrong, try again in 1 min
  

}


unsigned char * computeSHA1( char * data, int size ) {

  unsigned char * toRet = Malloc( 20 ); // SHA_DIGEST_LENGTH
  SHA1( (unsigned char*) data, size, toRet );
  return toRet;

}


struct torrentInfo* processBencodedTorrent( be_node * data ) {

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

      if ( ! strncmp( toRet->trackerDomain,  "http://", 7 ) ) {
	printf("SAME");
	char * withoutHTTP = strdup( toRet->trackerDomain + 7 );
	free( toRet->trackerDomain );
	toRet->trackerDomain = withoutHTTP;
      }
      printf("PAST SAME");

      printf("Tracker Domain: %s\n", toRet->trackerDomain );
      fflush(stdout);

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
	  toRet->name = strdup( infoIter->val.d[j].val->val.s );
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

  // Now, set up the chunks for the torrent
  int numChunks = toRet->totalSize/toRet->chunkSize + 
    ( toRet->totalSize % toRet->chunkSize ? 1 : 0 );
  toRet->numChunks = numChunks;
  toRet->chunks = Malloc( numChunks * sizeof(struct chunkInfo) );
  for ( i = 0; i < numChunks ; i ++ ) {
    memcpy( toRet->chunks[i].hash, chunkHashes + 20*i, 20 );
    toRet->chunks[i].size = ( i == numChunks - 1 ? 
			      toRet-> totalSize % toRet->chunkSize 
			      : toRet->chunkSize ) ;
    toRet->chunks[i].have = 0;
    toRet->chunks[i].requested = 0;
    toRet->chunks[i].data = Malloc( toRet->chunks[i].size ) ;

    int subChunkSize = 1 << 14;
    int numSubChunks = ( toRet->chunks[i].size + subChunkSize - 1 ) / subChunkSize;
    toRet->chunks[i].numSubChunks = numSubChunks;
    toRet->chunks[i].subChunks = Malloc( numSubChunks * sizeof( struct subChunk ) );
    for ( j = 0; j < numSubChunks; j ++ ) {
      toRet->chunks[i].subChunks[j].start = j * subChunkSize;
      toRet->chunks[i].subChunks[j].end   = min( (j+1) * subChunkSize, toRet->chunks[i].size );
      toRet->chunks[i].subChunks[j].len   = toRet->chunks[i].subChunks[j].end - toRet->chunks[i].subChunks[j].start ;
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
    //printf("Num Read: %d / %d\n", numRead, fsize);
    numRemaining -= numRead;
  }  

  fclose(fd);

  toRet->infoHash = computeSHA1( string, fsize );
  free( string );

  // Initialize the peer ID;
  toRet->peerID = strdup( "BMaPeerID12345123456" );
  
  // Initialize number of peers and seeds
  toRet->numPeers = 0;
  toRet->numSeeds = 0;

  // Initialize upload / download stats
  toRet->numBytesUploaded = 0;
  toRet->numBytesDownloaded = 0;

  toRet->lastPrint = 0;

  // Initialize the memory mapped file where we will store results
  mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
  int saveFile = open("/local/bmarks1/torrent.download", O_RDWR | O_CREAT | O_TRUNC, mode);
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
    fopen("/local/bmarks1/torrent.log", "w+" ) ;
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


  toRet->peerList = Malloc( 30 * sizeof( struct peerInfo ) );
  toRet->peerListLen = 30;
  for ( i = 0; i < 30; i ++ ) {
    toRet->peerList[i].defined = 0;
  }

  return toRet;

}



void logToFile( struct torrentInfo * torrent, const char * format, ... ) {
  
  return;

  va_list args;
  va_start( args, format );

  struct timeval tv;
  if ( gettimeofday( &tv, NULL ) ) {
    perror("gettimeofday");
    exit(1);
  }

  unsigned long long msDiff = 
    tv.tv_sec * 1000000 + tv.tv_usec - torrent->timer ;

  fprintf( torrent->logFile, "[%.2f] ", 1.0 * msDiff / 1000000 );

  fprintf( torrent->logFile, format, args );



}

char * createTrackerMessage( struct torrentInfo * torrent, int msgType ) {

  char * request = Malloc( 1024 * sizeof(char)) ;
  request[1023] = '\0';

  char * infoHash = percentEncode( torrent->infoHash, 20 );
  char * peerID = percentEncode( torrent->peerID, 20 );
  int uploaded = torrent->numBytesUploaded;
  int downloaded = torrent->numBytesDownloaded;
  int left = torrent->totalSize - torrent->numBytesDownloaded;
  
  char event[32];
  if ( msgType == TRACKER_STARTED ) {
    strncpy( event, "&event=started", 31 );
  }
  else if ( msgType == TRACKER_STOPPED ) {
    strncpy( event, "&event=stopped", 31 );
  }
  else if ( msgType == TRACKER_COMPLETED ) {
    strncpy( event, "&event=completed", 31 );
  }
  else {
    event[0] = '\0';
  }


  snprintf(request, 1023, 
	   "GET /announce?info_hash=%s&peer_id=%s&port=%d&uploaded=%d&downloaded=%d&left=%d&compact=1&no_peer_id=1%s&numwant=50 HTTP/1.1\r\nHost: %s:6969\r\n\r\n", 
	   infoHash, peerID, PEER_LISTEN_PORT, uploaded, 
	   downloaded, left, event, torrent-> trackerDomain);
  printf("\n%s\n", request );
  free( infoHash );
  free( peerID );

  return request;

}

int parseTrackerResponse( struct torrentInfo * torrent, char * response, int responseLen ) {

  int i,j;



  // Find the number of bytes designated to peers
  char * peerListPtr = strstr( response, "5:peers" );
  if ( ! peerListPtr ) {
    return 0;
  }
  peerListPtr += strlen( "5:peers" );
  
  char * numEnd = strchr( peerListPtr, ':' );
  if ( ! numEnd ) {
    return 0; 
  }

  *numEnd = '\0';
  int numBytes = atoi( peerListPtr );
  logToFile( torrent, "Number of Bytes of Peers: %d\nNumber of Peers: %d\n", 
	 numBytes, numBytes / 6 );
  *numEnd = ':';
  
  if ( response + responseLen < numEnd+1+numBytes ) {
    // We still need more data ...
    return 0;
  }


  char * intervalPtr = strstr( response, "8:intervali" );
  if ( ! intervalPtr ) {
    return 0;
  }
  char *  intervalPtrStart = intervalPtr + strlen( "8:intervali" );
  char * intervalPtrEnd = strchr( intervalPtrStart, 'e' );
  if ( ! intervalPtrEnd ) {
    return 0;
  }
  *intervalPtrEnd = '\0';
  int interval = atoi( intervalPtrStart );
  *intervalPtrEnd = 'e';

  unsigned char ip[4];
  uint16_t portBytes;

  peerListPtr = numEnd + 1;


  char handshake[68];
  char tmp = 19;
  memcpy( &handshake[0], &tmp, 1 );
  char protocol[20];
  strncpy( protocol, "BitTorrent protocol", 19 );
  memcpy( &handshake[1], protocol, 19 );
  int flags = 0;
  memcpy( &handshake[20], &flags, 4 );
  memcpy( &handshake[24], &flags, 4 );
  memcpy( &handshake[28], torrent->infoHash, 20 );
  memcpy( &handshake[48], torrent->peerID, 20 );


  for ( i = 0; i < numBytes/6; i ++ ) {
    int newSlot = getFreeSlot( torrent );
    struct peerInfo * this = &torrent->peerList[ newSlot ];

    // Get IP and port data in the right place
    memcpy( ip, peerListPtr, 4 );
    memcpy( &portBytes, peerListPtr + 4, 2 );

    snprintf( this->ipString, 16, "%u.%u.%u.%u", (int)ip[0], (int)ip[1], (int)ip[2], (int)ip[3] );
    this->portNum = ntohs( portBytes );

    // Before continuing, see if we already have an existing connection with this
    // host
    int exists = 0;
    for( j = 0; j < torrent->peerListLen; j ++ ) {
      if ( j == newSlot || torrent->peerList[j].defined == 0 ) {
	continue;
      }
      if ( !strcmp( torrent->peerList[j].ipString, this->ipString ) ) {
	exists = 1;
	break;
      }
    }
    if ( exists ) {
      this->defined = 0;
      break;
    }


    if ( connectToPeer( this, torrent, handshake ) ) {
      printf("Initializing %s:%u - FAILED\n", 
	     this->ipString, (int)this->portNum );
      
      logToFile( torrent, "Initializing %s:%u - FAILED\n", 
		 this->ipString, (int)this->portNum );
    }
    else {  
      printf("Initializing %s:%u - SUCCESS\n", 
		 this->ipString, (int)this->portNum );
      logToFile( torrent, 
		 "Initializing %s:%u - SUCCESS\n", this->ipString, (int)this->portNum );
    }

    peerListPtr += 6;

  }
  return interval;

}


int trackerAnnounce( struct torrentInfo * torrent ) {

  return 0;

  /*
  int send_length = strlen(request);
  int send_count = 0;
  while (send_count < send_length ) {
    int num_sent = send( sock, &request[send_count], send_length-send_count, 0);
    if ( num_sent < 0 ) {
      perror("send");
      exit(1);
    }
    send_count += num_sent;
  }

  int recv_count = 1;
  int num_recv = 0;
  char buf[10000];
  while ( recv_count > 0 ) {
    recv_count = recv( sock, &buf[num_recv] , 9999 - num_recv, 0 );
    num_recv += recv_count;
    buf[ num_recv ] = '\0';
    logToFile( torrent, "Received %d bytes. %d so far\n", recv_count, num_recv );
  }
  logToFile( torrent, "BUFFER \n\n%s\n\n ", buf );
  logToFile( torrent, " Done \n" );




  parseTrackerResponse( torrent, buf, num_recv );
  


  free( request );

  return 0;
  */
}

void destroyPeer( struct peerInfo * peer, struct torrentInfo * torrent ) {

  if ( peer->type == BT_PEER ) {
    torrent->numPeers -= 1;
  }
  else if ( peer->type == BT_SEED ) {
    printf("Destroying peer: %s:%d\n", peer->ipString, peer->portNum);
    torrent->numSeeds -= 1;
  }
  else { }

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
  torrent->peerList = realloc( torrent->peerList, 2 * torrent->peerListLen * sizeof( struct peerInfo ) );
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

  printf("Accepted Connection from %s:%u\n", this->ipString, (unsigned int)this->portNum );
  logToFile(torrent, "Accepted Connection from %s:%u\n", this->ipString, (unsigned int)this->portNum );

  torrent->numPeers += 1;
  this->type = BT_PEER;

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
	  fprintf(stderr, "Error in getsockopt() %d - %s\n", errno, strerror(errno)); 
	  error = 1;
	} 
	// Check the value returned... 
	if (valopt) { 
	  fprintf(stderr, "Error in delayed connection() %d - %s\n", valopt, strerror(valopt) 
		  ); 
	  error = 1;
	} 
      }
      else { 
	fprintf(stderr, "Timeout in select() - Cancelling!\n"); 
	error = 1;
      }
    } 
    else { 
      fprintf(stderr, "Error connecting %d - %s\n", errno, strerror(errno)); 
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


int connectToPeer( struct peerInfo * this, struct torrentInfo * torrent, char * handshake) {

  int res;

  char * ip = this->ipString;
  unsigned short port = this->portNum;

  // Set up our socket
  int sock = socket(AF_INET, SOCK_STREAM, 0);
  if ( sock < 0 ) {
    perror( "socket");
    exit(1);
  }
  
  printf("Connecting to peer - about to call nonblocking connect %s:%d\n", ip, port);

  int error = nonBlockingConnect( ip, port, sock );

  if ( ! error ) {

    res = write( sock, handshake, 68 );
    if ( res != 68 ) {
      printf("Error sending handshake.\n");
      error = 1;
    }
    if ( res < 0 ) {
      perror("Handshake write: ");
      exit(1);
    }
  }
  else {
    // Just get rid of this slot
    perror("connect");
    this->defined = 0;
    close( sock );
    return -1;
  }
  
  

  this->socket = sock;

  initializePeer( this, torrent );

  this->status = BT_AWAIT_RESPONSE_HANDSHAKE;
  this->type = BT_SEED;

  torrent->numSeeds += 1;

  return 0;

}



void initializePeer( struct peerInfo * this, struct torrentInfo * torrent ) {

  // Slot is taken
  this->defined = 1;

  // Initialize our sending and receiving state and data structures
  this->peer_choking = 1;
  this->am_choking = 1;
  this->peer_interested = 0;
  this->am_interested = 0;
  
  this->haveBlocks = Bitfield_Init( torrent->numChunks );
  
  this->readingHeader = 1;
  this->incomingMessageRemaining = 68; // Length
  this->incomingMessageOffset = 0;
  this->incomingMessageData = Malloc( 68 ); // First message we expect is a handshake
  
  this->outgoingData = SS_Init();

  this->lastInterestedRequest = 0;
  this->lastWrite = 0;
  this->numPendingSubchunks = 0;

  return;

}



void destroyTorrentInfo( ) { 
  
  struct torrentInfo * t = globalTorrentInfo;

  // Tell the tracker we're shutting down.
  doTrackerCommunication( t, TRACKER_STOPPED );

  free( t->announceURL );
  free( t->trackerDomain );
  free( t->trackerIP );

  int i;
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


  free( t->chunks );
  free( t->name );
  free( t->comment );
  free( t->infoHash );
  free( t->peerID );
  free( t->peerList );

  Bitfield_Destroy( t->ourBitfield );
  munmap( t->fileData, t->totalSize );

  if ( t->logFile ) {
    fclose( t->logFile );
  }

  free( t );

  printf("Have a nice day!\n");
  exit(1);
    
  return;

}

int setupListeningSocket() {

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
  addr.sin_port = htons( PEER_LISTEN_PORT );
  addr.sin_addr.s_addr = INADDR_ANY;
  
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

int setupReadWriteSets( fd_set * readPtr, fd_set * writePtr, struct torrentInfo * torrent ) {

  int i;

  int maxFD = 0;

  struct timeval tv;
  if ( gettimeofday( &tv, NULL ) ){
    perror("gettimeofday");
    exit(1);
  }

  for ( i = 0; i < torrent->peerListLen; i ++ ) {
    if ( torrent->peerList[i].defined ) {

      // We want to read from anybody who meets any of the following criteria
      //   * We are waiting for a handshake response
      //   * We are not choking them
      if ( torrent->peerList[i].status == BT_AWAIT_INITIAL_HANDSHAKE ||
	   torrent->peerList[i].status == BT_AWAIT_RESPONSE_HANDSHAKE ||
	   1 ) {
	if ( torrent->peerList[i].status == BT_AWAIT_INITIAL_HANDSHAKE ) {
	  printf("Waiting for Initial Handshake from %s:%d\n", torrent->peerList[i].ipString, torrent->peerList[i].portNum ); 
	}
	if ( torrent->peerList[i].status == BT_AWAIT_RESPONSE_HANDSHAKE ) {
	  printf("Waiting for Response Handshake from %s:%d\n", torrent->peerList[i].ipString, torrent->peerList[i].portNum ); 
	}

	FD_SET( torrent->peerList[i].socket, readPtr );
	//printf("Interestd in reading from %s:%d\n", torrent->peerList[i].ipString, torrent->peerList[i].portNum );

	if ( maxFD < torrent->peerList[i].socket ) {
	  maxFD = torrent->peerList[i].socket;
	}

      }

      // We want to write to anybody who has data pending
      if ( torrent->peerList[i].outgoingData->size > 0 && 
	   tv.tv_sec - torrent->peerList[i].lastWrite > 2 ) {
	printf("Interestd in writing to %s:%d\n", torrent->peerList[i].ipString, torrent->peerList[i].portNum );
	FD_SET( torrent->peerList[i].socket, writePtr ) ;
	if ( maxFD < torrent->peerList[i].socket ) {
	  maxFD = torrent->peerList[i].socket;
	}
      }
    } /* Peer is defined */
  }



  return maxFD ;


}

int handleHaveMessage( struct peerInfo * this, struct torrentInfo * torrent ) {
  uint32_t blockNum;
  memcpy( &blockNum, &this->incomingMessageData[5], 4 );
  blockNum = ntohl( blockNum );
  printf("%s:%d HAVE %u\n", this->ipString, this->portNum, blockNum );
  logToFile(torrent, "%s:%d HAVE %u\n", this->ipString, this->portNum, blockNum );
  return Bitfield_Set( this->haveBlocks, blockNum );
}

int handleBitfieldMessage( struct peerInfo * this, struct torrentInfo * torrent ) {
  printf("%s:%d BITFIELD\n", this->ipString, this->portNum );
  logToFile( torrent, "%s:%d BITFIELD\n", this->ipString, this->portNum );
  int len;
  memcpy( &len, &this->incomingMessageData[0], 4 );
  len = ntohl( len );
  return Bitfield_FromExisting( this->haveBlocks, 
				&this->incomingMessageData[5],
				len - 1 ); // Don't count message type byte

}

int handleRequestMessage( struct peerInfo * this, struct torrentInfo * torrent ) {

  // If we have the block, then break it into chunks and append those requests
  // to this socket's pending queue.
  
  logToFile(torrent, "Received request message!\n");

  int idx, begin, len;
  memcpy( &idx,   &this->incomingMessageData[5],  4 );
  memcpy( &begin, &this->incomingMessageData[9],  4 );
  memcpy( &len,   &this->incomingMessageData[13], 4 );
  idx   = ntohl(  idx  );
  begin = ntohl( begin );
  len   = ntohl(  len  );

  if ( ! torrent->chunks[idx].have ) {
    logToFile( torrent, "Warning: Received request for chunk %d, which I don't have.\n", 
	       idx );
    return -1;
  }

  // Check that the chunk is as large as the request says
  if ( begin + len > torrent->chunks[idx].size ) {
    logToFile(torrent, "Warning: Received request for chunk %d.%d-%d, which is out of bounds.\n",
	      idx, begin, begin + len );
    return -1;
  }

  SS_Push( this->outgoingData, torrent->chunks[idx].data + begin, len );
  torrent->numBytesUploaded += len;

  return 0;
}


void broadcastHaveMessage( struct torrentInfo * torrent, int blockIdx ) {

  int i;
  char msg[5];
  int len = htonl(5);
  char id = 4;
  memcpy( &msg[0], &len, 4 );
  memcpy( &msg[4], &id, 1 );

  for ( i = 0; i < torrent->peerListLen; i ++ ) {
    struct peerInfo * peerPtr = &torrent->peerList[i];
    if ( peerPtr->defined ) {
      int val;
      if ( Bitfield_Get( peerPtr->haveBlocks, blockIdx, &val ) ) {
	perror("bitfield_get");
	exit(1);
      }
      if ( ! val ) {
	// Only send them the have message if they don't have
	// this block already
	SS_Push( peerPtr->outgoingData, msg, 5 );
      }
    }
  }

  return;
}

int handlePieceMessage( struct peerInfo * this, struct torrentInfo * torrent ) {

  logToFile(torrent, "Received Piece Message: %s:%d \n", this->ipString, this->portNum );
  
  int idx;
  memcpy( &idx, &this->incomingMessageData[5], 4 );
  idx = ntohl( idx );

  int offset;
  memcpy( &offset, &this->incomingMessageData[9], 4 );
  offset = ntohl( offset );

  int messageLen;
  memcpy( &messageLen, &this->incomingMessageData[0], 4 );
  messageLen = ntohl( messageLen );

  int dataLen = messageLen - 9;

  this->numPendingSubchunks --;

  // Update our downloaded stats
  torrent->numBytesDownloaded += dataLen ;

  // This chunk is already finished. No need to continue.
  if ( torrent->chunks[ idx ].have ) {
    logToFile( torrent, "Warning: Received duplicate message!\n");
    return 0;
  }

  // Find the subchunk that we have just received
  struct subChunk sc = torrent->chunks[idx].subChunks[ offset / (1 << 14) ];

  // Check that we didn't get the chunk from somewhere else in the mean
  // time
  if ( ! torrent->chunks[idx].subChunks[ offset / (1 << 14) ].have ) {
    // Check that this is the right subchunk
    if ( sc.start == offset && sc.len == dataLen ) {
      logToFile( torrent, "Received chunk %d.%d / %d\n", idx, offset / ( 1 << 14 ), 
		 torrent->chunks[idx].numSubChunks);
      torrent->chunks[idx].subChunks[ offset / (1 << 14) ].have = 1;
    }
    
    memcpy( & torrent->chunks[idx].data[ offset ], 
	    & this->incomingMessageData[13], 
	    dataLen );
  } 
  else {
    logToFile( torrent, "Warning: Received duplicate message %d.%d / %d\n", idx, offset/(1<<14),
	   torrent->chunks[idx].numSubChunks);
  }

  // Are we done downloading this chunk?
  int i;
  for ( i = 0; i < torrent->chunks[idx].numSubChunks; i ++ ) {
    if ( torrent->chunks[idx].subChunks[i].have == 0 ) {
      return 0;
    }
  }

  // If we get here, then we have all of the subchunks
  // Check the SHA1 hash of the block and if its good, 
  // then broadcast a HAVE message to all our peers.
  unsigned char * hash = computeSHA1( torrent->chunks[idx].data, 
				      torrent->chunks[idx].size );
  if ( memcmp( hash, torrent->chunks[idx].hash, 20 ) ) {
    free( hash );
    logToFile( torrent, "Invalid SHA1 Hash for block %d.\n", idx);
    for ( i = 0; i < torrent->chunks[idx].numSubChunks; i ++ ) {
      torrent->chunks[idx].subChunks[i].have = 0;
    }
  } 
  else {
    free( hash );
    printf("Finished downloading block %d.\n", idx);
    logToFile( torrent, "Finished downloading block %d.\n", idx);
    torrent->chunks[idx].have = 1;
    broadcastHaveMessage( torrent, idx );
    Bitfield_Set( torrent->ourBitfield, idx );


    // Copy the data to a file, reset our data pointer, and clean up
    // any other state
    memmove( &torrent->fileData[ idx * torrent->chunkSize ], 
	     torrent->chunks[idx].data, torrent->chunks[idx].size );
    msync( &torrent->fileData[ idx * torrent->chunkSize ], 
	   torrent->chunks[idx].size, MS_SYNC );

    free( torrent->chunks[idx].subChunks );
    free( torrent->chunks[idx].data );
    torrent->chunks[idx].data = &torrent->fileData[ idx * torrent->chunkSize ];
  
    // Are we done downloading the entire torrent?
    int j;
    for ( j = 0; j < torrent->numChunks; j ++ ) {
      if ( ! torrent->chunks[j].have ) {
	return 0;
      }
    }
    // Yes, we are!
    doTrackerCommunication( torrent, TRACKER_COMPLETED );
  }
  return 0;
}

void sendBitfield( struct peerInfo * this, struct torrentInfo * torrent ) {

  int len =  torrent->ourBitfield->numBytes + 1;
  char id = 5;
  char message[ len + 4];
  int nlen = htonl( len );
  memcpy( message, &nlen, 4 );
  memcpy( &message[4], &id, 1 );
  memcpy( &message[5], torrent->ourBitfield->buffer, torrent->ourBitfield->numBytes ); 
  SS_Push( this->outgoingData, message, len + 4 );
  return;

}


void handleFullMessage( struct peerInfo * this, struct torrentInfo * torrent ) {
  
  // If we were expecting a handshake, see if we got it
  if ( this->status == BT_AWAIT_INITIAL_HANDSHAKE ||
       this->status == BT_AWAIT_RESPONSE_HANDSHAKE ) {

    // Validate that this is a valid handshake
    char correct[68];
    char tmp = 19;
    memcpy( &correct[0], &tmp, 1 );
    char protocol[20];
    strncpy( protocol, "BitTorrent protocol", 19 );
    memcpy( &correct[1], protocol, 19 );
    int flags = 0;
    memcpy( &correct[20], &flags, 4 );
    memcpy( &correct[24], &flags, 4 );
    memcpy( &correct[28], torrent->infoHash, 20 );
    memcpy( &correct[48], torrent->peerID, 20 );

    if ( memcmp( correct, &this->incomingMessageData[0], 20 ) ||
	 memcmp( torrent->infoHash, &this->incomingMessageData[28], 20 )  ) {
      int i;
      for ( i = 0; i < 20; i ++ ) {
	printf("%x   should be    %x    %d\n", this->incomingMessageData[i], correct[i], this->incomingMessageData[i] == correct[i] );
      }
      for ( i = 28; i < 48; i ++ ) {
	printf("%x   should be    %x    %d\n", this->incomingMessageData[i], correct[i], this->incomingMessageData[i] == correct[i] );
      }

      printf("Invalid handshake from %s:%d\n", this->ipString, this->portNum);
      logToFile( torrent, "Invalid handshake from %s:%d\n", this->ipString, this->portNum);
      destroyPeer( this, torrent);
      return;
    }
    printf("Got handshake from %s:%d\n", this->ipString, this->portNum );
    logToFile( torrent, "Got handshake from %s:%d\n", this->ipString, this->portNum );

    if ( this->status == BT_AWAIT_INITIAL_HANDSHAKE ) {
      // Send our handshake back
      printf("Sending response handshake to %s:%d\n", this->ipString, this->portNum );
      SS_Push( this->outgoingData, correct, 68 );
    } 

    sendBitfield( this, torrent );
    this->status = BT_AWAIT_BITFIELD;
    
    // Set us up to get the bitfield
    free( this->incomingMessageData );
    this->incomingMessageData = Malloc( 4 );
    this->incomingMessageOffset = 0;
    this->incomingMessageRemaining = 4;

    return;

  }

  // If we were reading the header, then realloc enough space for the whole message
  if ( this->readingHeader == 1 ) {
    int len;
    memcpy( &len, this->incomingMessageData, 4 );
    len = ntohl( len );
    
    // Handle keepalives
    if ( len == 0 ) {
      this->incomingMessageRemaining = 4;
      this->incomingMessageOffset = 0;
      this->readingHeader = 1 ;
      this->status = BT_RUNNING;
    }
    else {
      this->incomingMessageData = realloc( this->incomingMessageData, len + 4);
      this->incomingMessageRemaining = len;
      this->incomingMessageOffset = 4;
      this->readingHeader = 0;
    }
  }
  else {

    int error = 0;

    // Handle full message here ...
    switch( this->incomingMessageData[4] ) {

      

    case ( 0 ) :       // Choke
      this->peer_choking = 1;  
      logToFile( torrent, "%s:%d - CHOKE\n", this->ipString, this->portNum);
      break; 
    case ( 1 ) :       // Unchoke
      this->peer_choking = 0; 
      logToFile( torrent, "%s:%d - UN CHOKE\n", this->ipString, this->portNum);
      break; 
    case ( 2 ) :       // Interested
      this->peer_interested = 1; 
      sendUnchoke( this, torrent );
      printf("%s:%d - INTERESTED\n", this->ipString, this->portNum);
      logToFile( torrent, "%s:%d - INTERESTED\n", this->ipString, this->portNum);
      break; 
    case ( 3 ) :
      this->peer_interested = 0;
      logToFile( torrent, "%s:%d - NOT INTERESTED\n", this->ipString, this->portNum);
      break; 
    case ( 4 ) :
      error = handleHaveMessage( this, torrent );
      break;
    case ( 5 ) :
      printf("%s:%d - Bitfield\n", this->ipString, this->portNum);
      if ( this->status == BT_AWAIT_BITFIELD ) {
	error = handleBitfieldMessage( this, torrent );
      } else {
	error = 1;
      }
      break;
    case ( 6 ) :
      error = handleRequestMessage( this, torrent );
      break;
    case ( 7 ) :
      error = handlePieceMessage( this, torrent );
      break;
    case ( 8 ) : // Cancel
      break;
    case ( 9 ) : // DHT
      break;
    default :
      error = 1;
    };

    if ( error ) {
      printf("Error handling full message ... \n");
      destroyPeer( this, torrent );
    }
    else {
      // And prepare for the next request
      this->incomingMessageRemaining = 4;
      this->incomingMessageOffset = 0;
      this->readingHeader = 1 ;
    }
    this->status = BT_RUNNING ;
  }



}

void handleWrite( struct peerInfo * this, struct torrentInfo * torrent ) {

  int ret; 
  struct timeval tv;
  if ( gettimeofday( &tv, NULL ) ){
    perror("gettimeofday");
    exit(1);
  }
  this->lastWrite = tv.tv_sec;

  logToFile(torrent, "Writing to %s:%d\n", this->ipString, this->portNum );

  ret = write( this->socket, this->outgoingData->head, this->outgoingData->size );
  if ( ret < 0 ) {
    perror("write");
    exit(1);
  }
  SS_Pop( this->outgoingData, ret );
  return;

}

void handleRead( struct peerInfo * this, struct torrentInfo * torrent ) {

  int ret;
  //printf("Reading From %s:%d\n", this->ipString, this->portNum );
  ret = read( this->socket, 
	      &this->incomingMessageData[ this->incomingMessageOffset ], 
	      this->incomingMessageRemaining );
  if ( ret < 0 ) {

    if ( errno == ECONNRESET ) {
      printf("Connection from %s:%u was forcibly reset.\n", this->ipString, this->portNum );
      logToFile( torrent, "Connection from %s:%u was forcibly reset.\n", this->ipString, this->portNum );
      destroyPeer(this, torrent);
      return;
    }
    perror( "read" );
    exit(1);
  }
  if ( 0 == ret ) {
    // Peer closed our connection
    printf("Connection from %s:%u was closed by peer.\n", this->ipString, (unsigned int) this->portNum );
    logToFile( torrent, "Connection from %s:%u was closed by peer.\n", this->ipString, (unsigned int) this->portNum );
    destroyPeer( this, torrent );
    return ;
  }
  
  this->incomingMessageRemaining -= ret;
  this->incomingMessageOffset += ret;

  //printf("Have read %d/%d bytes.\n", this->incomingMessageOffset, this->incomingMessageRemaining + this->incomingMessageOffset);

  if ( this->incomingMessageRemaining == 0 ) {
    handleFullMessage( this, torrent );
  }
  
  return;

}
/*
void handleTrackerIO( fd_set * readFDs, fd_set * writeFDs, struct torrentInfo * torrent ) {


  int ret;
  if ( FD_ISSET( torrent->trackerConnection->socket, readFDs ) ) {
    // Get up to 1000 bytes from the tracker
    char * trackerResp = Malloc( 1000 );
    ret = read( torrent->trackerConnection->socket, trackerResp, 1000 );
    if ( ret < 0 ) {
      perror("read");
      printf("Error reading from tracker socket.\n");
    }
    else if ( ret == 0 ) { }
    else {
      printf("Received response from tracker server\n");

      SS_Push( torrent->trackerConnection->in, trackerResp, ret );
      char * donePtr;
      if ( ( donePtr = parseTrackerResponse( torrent, torrent->trackerConnection->in->head,
					     torrent->trackerConnection->in->size ) ) ) {
	int bytesToRemove = donePtr - torrent->trackerConnection->in->head ;
	SS_Pop( torrent->trackerConnection->in, bytesToRemove );
      }
      else {
	printf("Not enough bytes back from tracker ... %d\n%s\n", 
	       torrent->trackerConnection->in->size, 
	       torrent->trackerConnection->in->head ); 
      }

    }
  }
  if ( FD_ISSET( torrent->trackerConnection->socket, writeFDs ) ) {
    ret = write( torrent->trackerConnection->socket, 
		 torrent->trackerConnection->out->head ,
		 torrent->trackerConnection->out->size );
    if ( ret < 0 ) {
      perror("write");
      printf("Error writing to tracker socket.\n");
    }
    SS_Pop( torrent->trackerConnection->out, ret );
  }

  return;

}
*/

void handleActiveFDs( fd_set * readFDs, fd_set * writeFDs, struct torrentInfo * torrent, int listeningSock ) {

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

void sendInterested( struct peerInfo * p, struct torrentInfo * t ) {

  int len = htonl(1);
  char id = 2;
  char msg[5];
  memcpy( &msg[0], &len, 4 );
  memcpy( &msg[4], &id, 1 );
  logToFile( t, "Send Interested to %s\n", p->ipString);
  SS_Push( p->outgoingData, msg, 5 );
  return;

}

void sendUnchoke( struct peerInfo * this, struct torrentInfo * t ) {
  
  // Send them back an unchoke message.
  int nlenUnchoke = htonl(1);
  char unchokeID = 1;
  char msg[5];
  memmove( &msg[0], &nlenUnchoke, 4 );
  memmove( &msg[4], &unchokeID, 1 );
  SS_Push( this->outgoingData, msg, 5 );
  return;
}

void sendPieceRequest( struct peerInfo * p, struct torrentInfo * t , int pieceNum, int subChunkNum ) {

  char request[17];

  struct subChunk sc = t->chunks[pieceNum].subChunks[ subChunkNum ];
  int tmp = htonl(13);
  char tmpch = 6;
  memcpy( &request[0], &tmp, 4 );
  memcpy( &request[4], &tmpch, 1 );
  tmp = htonl( pieceNum );
  memcpy( &request[5], &tmp, 4 );
  tmp = htonl( sc.start );
  memcpy( &request[9], &tmp, 4 );
  tmp = htonl( sc.len );
  memcpy( &request[13], &tmp, 4 );

  logToFile( t, "Requesting %d.%d ( %d-%d ) from %s\n", pieceNum, subChunkNum, sc.start, sc.end, p->ipString);

  SS_Push( p->outgoingData, request, 17 );

  p->numPendingSubchunks ++;
  
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
      if ( t->peerList[i].numPendingSubchunks >= MAX_PENDING_SUBCHUNKS ) {
	break; // This peer already has enough outstanding requests
	// so we don't want to waste time getting more.
      }
    
      if ( ! t->chunks[j].have ) {
	struct peerInfo* peerPtr = &(t->peerList[i]);
	if ( ! Bitfield_Get( peerPtr->haveBlocks, j, &val ) && val ) {
	  // Our peer has this chunk, and we want it.
	  t->peerList[i].am_interested = 1;
	  
	  // This peer is choking us. Tell them we'll download from them if
	  // they unchoke us.
	  if ( t->peerList[i].peer_choking && (cur.tv_sec - t->peerList[i].lastInterestedRequest) > 5) {
	    sendInterested( &t->peerList[i], t );
	    t->peerList[i].lastInterestedRequest = cur.tv_sec;
	    break;
   	  }

	  // This peer is not choking us. Request up to MAX_PENDING_SUBCHUNKS subchunks
	  // from them.
	  else if ( ! t->peerList[i].peer_choking ) {
	    for ( k = 0; k < t->chunks[j].numSubChunks; k ++ ) {
	      if ( t->peerList[i].numPendingSubchunks >= MAX_PENDING_SUBCHUNKS ) {
		break;
	      }
	      if ( t->chunks[j].subChunks[k].have == 0 &&
		   cur.tv_sec - t->chunks[j].subChunks[k].requestTime > 20 ) {
		sendPieceRequest( &t->peerList[i], t, j, k );
		t->chunks[j].subChunks[k].requestTime = cur.tv_sec;
		t->chunks[j].requested = 1;
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

  printf("Number of Peers: %d\n", t->numPeers);
  printf("Number of Seeds: %d\n", t->numSeeds);
  printf("Download Amount: %.1f kB\n", 1.0*t->numBytesDownloaded / 1000 );
  printf("  Upload Amount: %.1f kB\n", 1.0*t->numBytesUploaded / 1000 );
  



}


int main(int argc, char ** argv) {

  int ret;

  be_node* data = load_be_node( argv[1] );
  

  int listeningSocket = setupListeningSocket();

  struct torrentInfo * t = processBencodedTorrent( data );
  be_free( data );

  globalTorrentInfo = t;

  doTrackerCommunication( t, TRACKER_STARTED );

  setupSignals( SIGALRM, trackerCheckin );
  setupSignals( SIGINT, destroyTorrentInfo );

  alarm( 30 );

  // trackerAnnounce( t ) ;

  // By this point, we have a list of peers we are connected to.
  // We can now start our select loop
  fd_set readFDs, writeFDs;
  struct timeval tv;


  while ( 1 ) {
    FD_ZERO( &readFDs );
    FD_ZERO( &writeFDs );

    tv.tv_sec = 2;
    tv.tv_usec = 0;


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

    //printf("Looping again after handling %d fds\n", ret );


  }

  // Never get here


  close( listeningSocket );


  return 0;

}
