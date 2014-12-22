#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bencode.h"
#include <assert.h>
#include <openssl/sha.h> //hashing pieces
#include "percentEncode.h"

#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>
#include <errno.h>

#include "StringStream/StringStream.h"
#include "bitfield.h"


#define BT_CONNECTED 1


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

  int incomingMessageType ;
  int incomingMessageSize ;
  int incomingMessageRemaining ;
  char * incomingMessageData ;

  StringStream * outgoingData ;



};

struct peerInfo * peerList ;

int initializePeer( struct peerInfo * thisPtr, struct torrentInfo * torrent );
void destroyPeer( struct peerInfo * peer ) ;

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
  int port = addr.sin_port;
  // TODO :: Dynamically generate these from the file blocks
  int uploaded = 0;
  int downloaded = 0;
  int left = torrent->totalSize;
  

  snprintf(request, 1023, "GET /announce?info_hash=%s&peer_id=%s&port=%d&uploaded=%d&downloaded=%d&left=%d&compact=0&no_peer_id=0&event=started HTTP/1.1\r\nHost: %s:6969\r\n\r\n", infoHash, peerID, port, uploaded, downloaded, left, torrent-> trackerDomain);

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
  peerList = Malloc( (numBytes/6) * sizeof( struct peerInfo ) );

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


  //snprintf( handshake, 69, "%cBitTorrent protocol%d%d%s%s", 19, 0, 0, torrent->infoHash, torrent->peerID );

  for ( i = 0; i < numBytes/6; i ++ ) {
    struct peerInfo * this = &peerList[i];

    // Get IP and port data in the right place
    memcpy( ip, peerListPtr, 4 );
    memcpy( &portBytes, peerListPtr + 4, 2 );

    snprintf( this->ipString, 16, "%u.%u.%u.%u", (int)ip[0], (int)ip[1], (int)ip[2], (int)ip[3] );
    this->portNum = ntohs( portBytes );

    initializePeer( this, torrent );
    SS_Push( this->outgoingData, handshake, 68 );
    peerListPtr += 6;

    destroyPeer( this );

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
  return;

}

int initializePeer( struct peerInfo * this, struct torrentInfo * torrent ) {


  char * ip = this->ipString;
  unsigned short port = this->portNum;

  // Slot is taken
  this->defined = 1;

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
  
  int res = connect( sock, (struct sockaddr*) &addr, sizeof(addr) );
  if ( res < 0 ) {
    // Just get rid of this slot
    this->defined = 0;
    close( sock );
    return -1;
  }

  this->socket = sock;

  // Initialize our sending and receiving state and data structures
  this->peer_choking = 1;
  this->am_choking = 1;
  this->peer_interested = 0;
  this->am_interested = 0;
  
  this->haveBlocks = Bitfield_Init( torrent->numChunks );
  
  this->incomingMessageType = 0;
  this->incomingMessageSize = 0;
  this->incomingMessageRemaining = 0;
  
  this->incomingMessageData = NULL;
  
  this->outgoingData = SS_Init();

  this->status = BT_CONNECTED ;

  return 0;

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
  free( t );

  return;

}



int main(int argc, char ** argv) {

  be_node* data = load_be_node( argv[1] );
  struct torrentInfo * t = processBencodedTorrent( data );

  trackerAnnounce( t ) ;

  destroyTorrentInfo( t );
  
  free( peerList );

  be_dump( data );
  be_free( data );

  return 0;

}
