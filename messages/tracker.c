/*
  tracker.c - Contains function definitions for all functions
  related to communicating with the tracker server.
*/

#include "tracker.h"

void trackerCheckin( int sig ) {

  int nextCheckin = 
    doTrackerCommunication( globalTorrentInfo, TRACKER_STATUS );
  alarm( nextCheckin );
  return;

}


int doTrackerCommunication( struct torrentInfo * t, int type ) {

  // Connect to the tracker server
  int sock = socket(AF_INET, SOCK_STREAM, 0);
  if ( sock < 0 ) {
    perror( "socket");
    exit(1);
  }
  
  int error = nonBlockingConnect( t->trackerIP, t->trackerPort, sock );
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
    buf[offset] = '\0';
  }

  int delay = parseTrackerResponse( t, buf, offset );

  return (delay == 0 ? 60 : delay ) ; // If something went wrong, try again in 1 min

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
    strncpy( event, "event=started&", 31 );
  }
  else if ( msgType == TRACKER_STOPPED ) {
    strncpy( event, "event=stopped&", 31 );
  }
  else if ( msgType == TRACKER_COMPLETED ) {
    strncpy( event, "event=completed&", 31 );
  }
  else {
    event[0] = '\0';
  }

  char ip[25];
  ip[0] = '\0';
  if ( torrent->bindAddress != INADDR_ANY ) {
    struct in_addr in;
    in.s_addr = torrent->bindAddress;
    snprintf( ip, 24, "ip=%s&", inet_ntoa( in ) );
  }

  snprintf(request, 1023, 
	   "GET /announce?"
	   "info_hash=%s&"
	   "peer_id=%s&"
	   "port=%d&"
	   "uploaded=%d&"
	   "downloaded=%d&"
	   "left=%d&"
	   "compact=1&"
	   "no_peer_id=1&"
	   "%s"          // Event string, if present
	   "%s"          // IP string, if present
	   "numwant=50 "
	   "HTTP/1.1\r\nHost: %s:%d\r\n\r\n", 
	   infoHash, peerID, torrent->bindPort, uploaded, 
	   downloaded, left, event, ip, torrent-> trackerDomain,
	   torrent->trackerPort);
  free( infoHash );
  free( peerID );

  return request;

}



int parseTrackerResponse( struct torrentInfo * torrent, 
			  char * response, int responseLen ) {


  int i,j;

  // Find the number of bytes designated to peers
  char * peerListPtr = strstr( response, "5:peers" );
  if ( ! peerListPtr ) {
    return 0;
  }
  
  peerListPtr += strlen( "5:peers" );

  char * numEnd = peerListPtr;
  while ( (*numEnd) && (*(numEnd) != ':') ) {
    numEnd ++ ;
  }

  *numEnd = '\0';
  int numBytes = atoi( peerListPtr );
  logToFile( torrent, "TRACKER RESPONSE Number of Peers: %d\n", 
	     numBytes / 6 );
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

  #ifdef TRACKER_RESP_HACK
  // Overwrite the first peer with a particular IP and port
  if ( numBytes / 6 > 0 ) {
    char fake[6];
    char * ptr = fake;
    char fake1 = IP0;
    memcpy( ptr, &fake1, 1 );
    fake1 = IP1;
    memcpy( ptr+1, &fake1, 1 );
    fake1 = IP2;
    memcpy( ptr+2, &fake1, 1 );
    fake1 = IP3;
    memcpy( ptr+3, &fake1, 1 );
    short fakePort = htons( PORT );
    memcpy( ptr+4, &fakePort, 2 );
    memcpy( peerListPtr, ptr, 6 );
  } 
  #endif

  for ( i = 0; i < numBytes/6; i ++ ) {
    int newSlot = getFreeSlot( torrent );
    struct peerInfo * this = &torrent->peerList[ newSlot ];

    // Get IP and port data in the right place
    memcpy( ip, peerListPtr, 4 );
    memcpy( &portBytes, peerListPtr + 4, 2 );

    snprintf( this->ipString, 16, "%u.%u.%u.%u", 
	      (int)ip[0], (int)ip[1], (int)ip[2], (int)ip[3] );
    this->portNum = ntohs( portBytes );

    // Before continuing, see if we already have an existing 
    // connection with this host
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
      logToFile( torrent, "STATUS Initializing %s:%u - FAILED\n", 
		 this->ipString, (int)this->portNum );
    }
    else {  
      logToFile( torrent, 
		 "STATUS Initializing %s:%u - SUCCESS\n", 
		 this->ipString, (int)this->portNum );
    }

    peerListPtr += 6;

  }

  return interval;

}

