
/* 
   incomingMessages.c - Function definitions for all 
   functions related to parsing received headers and
   messages. 
*/

#include "incomingMessages.h"



int handleHaveMessage( struct peerInfo * this, struct torrentInfo * torrent ) {


  uint32_t blockNum;
  memcpy( &blockNum, &this->incomingMessageData[5], 4 );
  blockNum = ntohl( blockNum );
  logToFile(torrent, "MESSAGE HAVE %u FROM %s:%d \n", 
	    blockNum, this->ipString, this->portNum );
  int ret = Bitfield_Set( this->haveBlocks, blockNum );

  torrent->chunks[ blockNum ].prevalence ++;
  torrent->numPrevalenceChanges ++;

  // If they are now finished, we should classify them as a seeder
  if ( Bitfield_AllSet( this->haveBlocks) ) {
    if ( this->type == BT_UNKNOWN ) {
      torrent->numUnknown --;
      torrent->numSeeds ++;
      this->type = BT_SEED;
    }
    if ( this->type == BT_PEER ) {
      torrent->numPeers --;
      torrent->numSeeds ++;
      this->type = BT_SEED;
    }
  }
  else {
    if ( this->type == BT_UNKNOWN ) {
      this->type = BT_PEER;
      torrent->numPeers ++ ;
      torrent->numUnknown --;

      if ( torrent->numPeers > torrent->maxPeers ) {
	ret = -1; // This will lead to destruction of the peer
      }


    }
  }


  return ret;
}

int handleBitfieldMessage( struct peerInfo * this, 
			   struct torrentInfo * torrent ) {


  logToFile( torrent, 
	     "MESSAGE BITFIELD FROM %s:%d\n", 
	     this->ipString, this->portNum );
  int len;
  memcpy( &len, &this->incomingMessageData[0], 4 );
  len = ntohl( len );

  int ret = Bitfield_FromExisting( this->haveBlocks, 
				   &this->incomingMessageData[5],
				   len - 1 ); // Don't count message type byte

 // If they are now finished, we should classify them as a seeder
  if ( Bitfield_AllSet( this->haveBlocks) ) {
    if ( this->type == BT_UNKNOWN ) {
      torrent->numUnknown --;
      torrent->numSeeds ++;
      this->type = BT_SEED;
    }
    if ( this->type == BT_PEER ) {
      torrent->numPeers --;
      torrent->numSeeds ++;
      this->type = BT_SEED;
    }
  }
  else {
    if ( this->type == BT_UNKNOWN ) {
      this->type = BT_PEER;
      torrent->numPeers ++ ;
      torrent->numUnknown --;

      if ( torrent->numPeers > torrent->maxPeers ) {
	ret = -1; // This will lead to destruction of the peer
      }
    }
  }

  // Update chunk prevalence counts for these blocks
  int i;
  int val;
  for ( i = 0; i < torrent->numChunks; i ++ ) {
    if ( !Bitfield_Get( this->haveBlocks, i, &val ) &&
	 val ) {
      torrent->chunks[i].prevalence ++;
      torrent->numPrevalenceChanges ++;
    }
  }



  return ret;

}


int handleRequestMessage( struct peerInfo * this, 
			  struct torrentInfo * torrent ) {


  int idx, begin, len;
  memcpy( &idx,   &this->incomingMessageData[5],  4 );
  memcpy( &begin, &this->incomingMessageData[9],  4 );
  memcpy( &len,   &this->incomingMessageData[13], 4 );
  idx   = ntohl(  idx  );
  begin = ntohl( begin );
  len   = ntohl(  len  );

  logToFile( torrent, "MESSAGE REQUEST BLOCK %d FROM %s:%d\n", 
	     idx, this->ipString, this->portNum );

  // If this is a peer and they are choked, then don't respond
  if( this->type == BT_PEER && this->am_choking ) {
    logToFile(torrent, 
	      "WARNING Request from %s:%d, who is choked.\n",
	      this->ipString, this->portNum );
    return -1;
  }


  if ( ! torrent->chunks[idx].have ) {
    logToFile( torrent, 
	       "WARNING Request ffrom %s:%d for chunk %d, which I don't have.\n", 
	       this->ipString, this->portNum, idx );
    return -1;
  }

  // Check that the chunk is as large as the request says
  if ( begin + len > torrent->chunks[idx].size ) {
    logToFile(torrent, 
	      "WARNING Request from %s:%d for chunk %d.%d-%d,"
	      "is out of bounds.\n",
	      this->ipString, this->portNum, idx, begin, begin + len );
    return -1;
  }

  char header[ 13 ];
  int respLen = 9 + len;
  int nrespLen = htonl( respLen );
  memcpy( &header[0], &nrespLen, 4 );
  char id = 7;
  memcpy( &header[4], &id, 1 );
  memcpy( &header[5], &this->incomingMessageData[5], 8 );
  SS_Push( this->outgoingData, header, 13 );
  SS_Push( this->outgoingData, torrent->chunks[idx].data + begin, len );
  torrent->numBytesUploaded += len;

  // If the torrent is finished, then we use our own upload
  // speed for choking purposes instead of our download speed
  // from them
  if ( torrent->completed ) {
    this->downloadAmt += len ;
  }

  return 0;
}


void handlePieceMessage( struct peerInfo * this, 
			 struct torrentInfo * torrent ) {

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


  logToFile(torrent, "MESSAGE PIECE %d.%d-%d FROM %s:%d\n", 
	    idx, offset, offset+messageLen, 
	    this->ipString, this->portNum );


  // Update our downloaded stats
  torrent->numBytesDownloaded += dataLen ;
  this->downloadAmt += dataLen ;
  // This chunk is already finished. No need to continue.
  if ( torrent->chunks[ idx ].have ) {
    logToFile( torrent, 
	       "WARNING Duplicate Block Message %d.%d-%d FROM %s:%d\n",
	       idx, offset, offset+messageLen, 
	       this->ipString, this->portNum );
    return ;
  }

  // Find the subchunk that we have just received
  struct subChunk sc = torrent->chunks[idx].subChunks[ offset / (1 << 14) ];

  // Check that we didn't get the chunk from somewhere else in the mean
  // time
  if ( ! torrent->chunks[idx].subChunks[ offset / (1 << 14) ].have ) {
    // Check that this is the right subchunk
    if ( sc.start == offset && sc.len == dataLen ) {
      torrent->chunks[idx].subChunks[ offset / (1 << 14) ].have = 1;
    }
    
    memcpy( & torrent->chunks[idx].data[ offset ], 
	    & this->incomingMessageData[13], 
	    dataLen );
  } 
  else {
    logToFile( torrent, 
	       "WARNING Duplicate Block Message %d.%d-%d FROM %s:%d\n",
	       idx, offset, offset+messageLen, 
	       this->ipString, this->portNum );
  }

  // Are we done downloading this chunk?
  int i;
  for ( i = 0; i < torrent->chunks[idx].numSubChunks; i ++ ) {
    if ( torrent->chunks[idx].subChunks[i].have == 0 ) {
      return ;
    }
  }

  // If we get here, then we have all of the subchunks
  // Check the SHA1 hash of the block and if its good, 
  // then broadcast a HAVE message to all our peers.
  unsigned char * hash = computeSHA1( torrent->chunks[idx].data, 
				      torrent->chunks[idx].size );
  if ( memcmp( hash, torrent->chunks[idx].hash, 20 ) ) {
    free( hash );
    logToFile( torrent, 
	       "WARNING Invalid SHA1 Hash for block %d from %s:%d.\n", 
	       idx, this->ipString, this->portNum);
    for ( i = 0; i < torrent->chunks[idx].numSubChunks; i ++ ) {
      torrent->chunks[idx].subChunks[i].have = 0;
    }
  } 
  else {
    free( hash );
    printf("Finished downloading block %d.\n", idx);
    logToFile( torrent, "STATUS Finished downloading block %d.\n", idx);
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
	return ;
      }
    }
    // Yes, we are!
    doTrackerCommunication( torrent, TRACKER_COMPLETED );
    torrent->completed = 1;
  }

  return ;
}


void handleFullMessage( struct peerInfo * this, 
			struct torrentInfo * torrent ) {


  // Log that we heard something from this connection,
  // so that our timeout signal handler doesn't kill it
  struct timeval tv;
  if ( gettimeofday( &tv, NULL ) ){
    perror("gettimeofday");
    exit(1);
  }
  this->lastMessage = tv.tv_sec;

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
      logToFile( torrent, 
		 "WARNING Invalid handshake from %s:%d\n", 
		 this->ipString, this->portNum);
      destroyPeer( this, torrent);
      return;
    }
    if ( this->status == BT_AWAIT_RESPONSE_HANDSHAKE ) {
      logToFile( torrent, 
		 "HANDSHAKE SUCCESS %s:%d\n", 
		 this->ipString, this->portNum );
    }
    if ( this->status == BT_AWAIT_INITIAL_HANDSHAKE ) {
      // Send our handshake back
      logToFile( torrent, 
		 "HANDSHAKE REQUEST from %s:%d\n", 
		 this->ipString, this->portNum );
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

  // If we were reading the header, 
  // then realloc enough space for the whole message
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
      logToFile( torrent, 
		 "MESSAGE CHOKE FROM %s:%d\n", 
		 this->ipString, this->portNum);
      break; 
    case ( 1 ) :       // Unchoke
      this->peer_choking = 0; 
      logToFile( torrent, 
		 "MESSAGE UNCHOKE FROM %s:%d\n", 
		 this->ipString, this->portNum);
      break; 
    case ( 2 ) :       // Interested
      this->peer_interested = 1; 
      logToFile( torrent, 
		 "MESSAGE INTERESTED FROM %s:%d\n", 
		 this->ipString, this->portNum);
      break; 
    case ( 3 ) :
      this->peer_interested = 0;
      logToFile( torrent, 
		 "MESSAGE NOT INTERESTED FROM %s:%d\n", 
		 this->ipString, this->portNum);
      break; 
    case ( 4 ) :
      error = handleHaveMessage( this, torrent );
      break;
    case ( 5 ) :
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
      handlePieceMessage( this, torrent );
      break;
    case ( 8 ) : // Cancel
      logToFile( torrent, "WARNING Received CANCEL from %s:%d. Ignoring.",
		 this->ipString, this->portNum );
      break;
    case ( 9 ) : // DHT Port
      logToFile( torrent, "WARNING Received PORT from %s:%d. Ignoring.",
		 this->ipString, this->portNum );
      break;
    default :
      error = 1;
    };

    if ( error ) {
      logToFile( torrent, "Destroying %s:%d - error processing %d\n",
		 this->ipString, this->portNum, 
		 this->incomingMessageData[4] );
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
