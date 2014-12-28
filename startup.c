
/*
  startup.c - Function definitions for things we need to do on startup.
*/

#include "startup.h"

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

      lookupIP( toRet->trackerDomain, toRet->trackerIP, 25 );
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
  } /* Done iterating through .torrent file */

  /* Initialize our data strucures and data members */

  // Not done downloading yet!
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
  free( chunkHashes );

  // Initialize our bitfield
  toRet->ourBitfield = Bitfield_Init( toRet->numChunks );


  // Read in the info dictionary so that we can compute the hash
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

  // Copy over our arguments for the bind address and port
  toRet->bindAddress = args->bindAddress;
  toRet->bindPort    = args->bindPort;

  // Set our print timer
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

  // Initialize our timer ID's (we'll need to store them so
  // that we can free them at the end )
  toRet->timerTimeoutID = 0;
  toRet->timerChokeID = 0;

  // Initialize our peer list and peer data structures.
  toRet->peerList = Malloc( 30 * sizeof( struct peerInfo ) );
  toRet->peerListLen = 30;
  for ( i = 0; i < 30; i ++ ) {
    toRet->peerList[i].defined = 0;
  }
  toRet->optimisticUnchoke = NULL;
  toRet->chokingIter = 0;

  // Returns the initalized struct torrentInfo
  return toRet;

}

/*
  Bind to our listening address and port, and 
  begin listening for connections on that port.

  Parameters:
  => args - argsInfo struct containing the command line
  arguments, specifically the bind IP and port.

  Returns - the socket that we will use to listen
  for connections.
 */
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
