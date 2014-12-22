#include <stdio.h>
#include <stdlib.h>
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
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>

#include "StringStream/StringStream.h"
#include "bitfield.h"


#define BT_CONNECTED 1
// They connected to us; we should get a handshake from them
#define BT_AWAIT_INITIAL_HANDSHAKE 2  
// We sent them a handshake; they should send one back
#define BT_AWAIT_RESPONSE_HANDSHAKE 3


#define PEER_LISTEN_PORT 6881
#define BACKLOG 20

struct chunkInfo {

  int size;
  int have;
  char * data;
  char hash[20];

};

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

  // Hash of bencoded dictioanry in .torrent file
  unsigned char * infoHash;

  char * peerID;

  struct peerInfo * peerList;
  int peerListLen;

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



};



void initializePeer( struct peerInfo * thisPtr, struct torrentInfo * torrent );
void destroyPeer( struct peerInfo * peer ) ;
int connectToPeer( struct peerInfo * this, struct torrentInfo * torrent , char * handshake) ;

void * Malloc( size_t size ) {

  void * toRet = malloc( size );
  if ( ! toRet ) {
    perror( "malloc" );
  }
  return toRet;

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

  int i;
  unsigned char * toRet = Malloc( 20 ); // SHA_DIGEST_LENGTH
  SHA1( (unsigned char*) data, size, toRet );
  //print the hash
  for(i=0;i< 20;i++){
    printf("%02x",toRet[i]);
  }
  printf("\n");
  
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
	char * withoutHTTP = strdup( toRet->trackerDomain + 7 );
	free( toRet->trackerDomain );
	toRet->trackerDomain = withoutHTTP;
      }

      printf("Tracker Domain: %s\n", toRet->trackerDomain );

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
    toRet->chunks[i].data = NULL;
  }
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

  toRet->peerID = strdup( "BMaPeerID12345123456" );

  return toRet;

}

int trackerAnnounce( struct torrentInfo * torrent ) {
  int i;

  int sock = socket(AF_INET, SOCK_STREAM, 0);
  if ( sock < 0 ) {
    perror( "socket");
    exit(1);
  }

  struct sockaddr_in addr;
  addr.sin_family = AF_INET;
  addr.sin_port = htons( 6969 );
  addr.sin_addr.s_addr = inet_addr( torrent->trackerIP );

  int res = connect( sock, (struct sockaddr*) &addr, sizeof(addr) );
  if ( res < 0 ) {
    perror("connect");
    exit(1);
  }

  char * request = Malloc( 1024 * sizeof(char)) ;
  request[1023] = '\0';

  char * infoHash = percentEncode( torrent->infoHash, 20 );
  char * peerID = percentEncode( torrent->peerID, 20 );
  // TODO :: Dynamically generate these from the file blocks
  int uploaded = 0;
  int downloaded = 0;
  int left = torrent->totalSize;
  

  snprintf(request, 1023, "GET /announce?info_hash=%s&peer_id=%s&port=%d&uploaded=%d&downloaded=%d&left=%d&compact=0&no_peer_id=0&event=started&numwant=3 HTTP/1.1\r\nHost: %s:6969\r\n\r\n", infoHash, peerID, PEER_LISTEN_PORT , uploaded, downloaded, left, torrent-> trackerDomain);

  printf("REQUEST\n\n%s\n\n", request );

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
    printf("Received %d bytes. %d so far\n", recv_count, num_recv );
  }
  printf("BUFFER \n\n%s\n\n ", buf );



  printf(" Done \n" );

  // Find the number of bytes designated to peers
  char * peerListPtr = strstr( buf, "5:peers" );
  if ( ! peerListPtr ) {
    printf("Error: Response did not contain peers list\n");
    exit(1);
  }
  peerListPtr += strlen( "5:peers" );
  
  char * numEnd = strchr( peerListPtr, ':' );
  if ( ! numEnd ) {
    printf("Error: Invalid formatting of bencoded compressed peer list\n");
    exit(1);
  }
  *numEnd = '\0';
  int numBytes = atoi( peerListPtr );
  printf("Number of Bytes of Peers: %d\nNumber of Peers: %d\n", 
	 numBytes, numBytes / 6 );
  unsigned char ip[4];
  uint16_t portBytes;

  peerListPtr = numEnd + 1;
  torrent->peerList = Malloc( (numBytes/6) * sizeof( struct peerInfo ) );
  torrent->peerListLen = numBytes / 6;


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
    struct peerInfo * this = &torrent->peerList[i];

    // Get IP and port data in the right place
    memcpy( ip, peerListPtr, 4 );
    memcpy( &portBytes, peerListPtr + 4, 2 );

    snprintf( this->ipString, 16, "%u.%u.%u.%u", (int)ip[0], (int)ip[1], (int)ip[2], (int)ip[3] );
    this->portNum = ntohs( portBytes );

    printf("Initializing %s:%u - ", this->ipString, (int)this->portNum );
    fflush(stdout);

    if ( connectToPeer( this, torrent, handshake ) ) {
      printf("FAILED\n");
    }
    else {  
      printf("SUCCESS\n");
      SS_Push( this->outgoingData, handshake, 68 );
      this->status = BT_AWAIT_RESPONSE_HANDSHAKE;
    }

    peerListPtr += 6;

  }
  


  free( request );
  free( infoHash );
  free( peerID );
  

  return 0;

}

void destroyPeer( struct peerInfo * peer ) {

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

  return ;



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
  
  struct sockaddr_in addr;
  addr.sin_family = AF_INET;
  addr.sin_port = htons( port ); 
  addr.sin_addr.s_addr = inet_addr( ip ) ;
 
  // Set socket to nonblocking for connect, in case it fails
  int flags = fcntl( sock, F_GETFL );
  res = fcntl(sock, F_SETFL, O_NONBLOCK);  // set to non-blocking
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
      tv.tv_sec = 0; 
      tv.tv_usec = 500000; 
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


  if ( error ) {
    // Just get rid of this slot
    perror("connect");
    this->defined = 0;
    close( sock );
    return -1;
  }
  


  // And, make it blocking again
  res = fcntl( sock, F_SETFL, flags );
  if ( res < 0 ) {
    perror("fcntl set blocking");
    exit(1);
  }

  this->socket = sock;

  initializePeer( this, torrent );

  this->status = BT_CONNECTED;

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
  this->incomingMessageRemaining = 4; // Length
  this->incomingMessageOffset = 0;
  this->incomingMessageData = Malloc( 4 ); // 4 for length + 1 for header; will realloc this
  
  this->outgoingData = SS_Init();

  return;

}



void destroyTorrentInfo( struct torrentInfo * t ) {

  free( t->announceURL );
  free( t->trackerDomain );
  free( t->trackerIP );
  free( t->chunks );
  free( t->name );
  free( t->comment );
  free( t->infoHash );
  free( t->peerID );
  free( t->peerList );
  

  free( t );

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

  for ( i = 0; i < torrent->peerListLen; i ++ ) {
    if ( torrent->peerList[i].defined ) {

      // We want to read from anybody who meets any of the following criteria
      //   * We are waiting for a handshake response
      //   * We are not choking them
      if ( torrent->peerList[i].status == BT_AWAIT_INITIAL_HANDSHAKE ||
	   torrent->peerList[i].status == BT_AWAIT_RESPONSE_HANDSHAKE ||
	   ! torrent->peerList[i].am_choking ) {
	FD_SET( torrent->peerList[i].socket, readPtr );
	if ( maxFD < torrent->peerList[i].socket ) {
	  maxFD = torrent->peerList[i].socket;
	}

      }

      // We want to write to anybody who has data pending
      if ( torrent->peerList[i].outgoingData->size > 0 ) {
	FD_SET( torrent->peerList[i].socket, writePtr ) ;
	if ( maxFD < torrent->peerList[i].socket ) {
	  maxFD = torrent->peerList[i].socket;
	}
      }
    } /* Peer is defined */
  }

  return maxFD ;


}

void handleFullMessage( struct peerInfo * this, struct torrentInfo * torrent ) {
  
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
    }
    else {
      this->incomingMessageData = realloc( this->incomingMessageData, len + 4);
      this->incomingMessageRemaining = len;
      this->incomingMessageOffset = 4;
      this->readingHeader = 0;
    }
  }
  else {

    // Handle full message here ...

    // And prepare for the next request
    this->incomingMessageRemaining = 4;
    this->incomingMessageOffset = 0;
    this->readingHeader = 1 ;
  }


}

void handleWrite( struct peerInfo * this, struct torrentInfo * torrent ) {

  int ret;
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

  ret = read( this->socket, 
	      &this->incomingMessageData[ this->incomingMessageOffset ], 
	      this->incomingMessageRemaining );
  if ( ret < 0 ) {
    perror( "read" );
    exit(1);
  }
  if ( 0 == ret ) {
    // Peer closed our connection
    printf("Connection from %s:%u was closed by peer.\n", this->ipString, (unsigned int) this->portNum );
    destroyPeer( this );
    return ;
  }
  
  this->incomingMessageRemaining -= ret;
  this->incomingMessageOffset += ret;

  if ( this->incomingMessageRemaining == 0 ) {
    handleFullMessage( this, torrent );
  }
  
  return;

}

void handleActiveFDs( fd_set * readFDs, fd_set * writeFDs, struct torrentInfo * torrent, int listeningSock ) {

  int i;

  if ( FD_ISSET( listeningSock, readFDs ) ) {
    peerConnectedToUs( torrent, listeningSock );
  }

  for ( i = 0; i < torrent->peerListLen; i ++ ) {
    struct peerInfo * this = &torrent->peerList[i] ;
    if ( this->defined ) {

      if ( FD_ISSET( this->socket, readFDs ) ) {
	handleRead( this, torrent );
      }

      if ( FD_ISSET( this->socket, writeFDs ) ) {
	handleWrite( this, torrent );
      }

    }
  }
  

}

int main(int argc, char ** argv) {

  int ret;

  be_node* data = load_be_node( argv[1] );

  int listeningSocket = setupListeningSocket();

  struct torrentInfo * t = processBencodedTorrent( data );

  trackerAnnounce( t ) ;

  // By this point, we have a list of peers we are connected to.
  // We can now start our select loop
  fd_set readFDs, writeFDs;
  struct timeval tv;
  tv.tv_sec = 10;
  tv.tv_usec = 0;


  while ( 1 ) {
    FD_ZERO( &readFDs );
    FD_ZERO( &writeFDs );

    // We always want to accept new connections
    FD_SET( listeningSocket, &readFDs );

    int maxFD = setupReadWriteSets( &readFDs, &writeFDs, t );
    
    ret = select( maxFD + 1, &readFDs, &writeFDs, NULL, &tv );
    if ( ret < 0 ) {
      perror( "select" );
      exit(1);
    }

    handleActiveFDs( &readFDs, &writeFDs, t, listeningSocket );
    



  }




  close( listeningSocket );
  destroyTorrentInfo( t );
  be_dump( data );
  be_free( data );

  return 0;

}
