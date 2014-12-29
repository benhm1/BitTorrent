
/*
  outgoingMessages.c - function definitions for functions that generate
  and append different bittorrent protocol messages to our peers.

  Messages: Have, Bitfield, Unchoke, Choke, Interested, Request

  Piece messages are generated in handlePieceMessage function,
  declared in incomingMessages.h and implemented in incomingMessages.c

  We do not support Port and Cancel messages. We do not send KeepAlive 
  messages. 

*/

#include "outgoingMessages.h"



void broadcastHaveMessage( struct torrentInfo * torrent, int blockIdx ) {

  int i;
  char msg[9];
  int len = htonl(5);
  char id = 4;
  int nBlockIdx = htonl( blockIdx );
  memcpy( &msg[0], &len, 4 );
  memcpy( &msg[4], &id, 1 );
  memcpy( &msg[5], &nBlockIdx, 4 );
  
  logToFile(torrent, "SEND BROADCAST HAVE %d\n", blockIdx);

  for ( i = 0; i < torrent->peerListLen; i ++ ) {
    struct peerInfo * peerPtr = &torrent->peerList[i];
    if ( peerPtr->defined && 
	 peerPtr->status != BT_AWAIT_INITIAL_HANDSHAKE &&
	 peerPtr->status != BT_AWAIT_RESPONSE_HANDSHAKE ) 
      {
      int val;
      if ( Bitfield_Get( peerPtr->haveBlocks, blockIdx, &val ) ) {
	perror("bitfield_get");
	exit(1);
      }
      if ( ! val ) {
	// Only send them the have message if they don't have
	// this block already
	logToFile( torrent, "SEND HAVE %d TO %s:%d\n",
		   blockIdx, peerPtr->ipString, peerPtr->portNum);
	SS_Push( peerPtr->outgoingData, msg, 9 );
      }
    }
  }

  return;

}


void sendBitfield( struct peerInfo * this, struct torrentInfo * torrent ) {

  int len =  torrent->ourBitfield->numBytes + 1;
  char id = 5;
  char message[ len + 4];
  int nlen = htonl( len );
  memcpy( message, &nlen, 4 );
  memcpy( &message[4], &id, 1 );
  memcpy( &message[5], torrent->ourBitfield->buffer, 
	  torrent->ourBitfield->numBytes ); 
  SS_Push( this->outgoingData, message, len + 4 );

  return;

}

void sendInterested( struct peerInfo * p, struct torrentInfo * t ) {


  int len = htonl(1);
  char id = 2;
  char msg[5];
  memcpy( &msg[0], &len, 4 );
  memcpy( &msg[4], &id, 1 );
  logToFile( t, "SEND MESSAGE INTERESTED to %s:%d\n", p->ipString,
	     p->portNum);
  SS_Push( p->outgoingData, msg, 5 );

  return;

}

void sendUnchoke( struct peerInfo * this, struct torrentInfo * t ) {


  // Send them back an unchoke message.
  logToFile( t, "SEND MESSAGE UNCHOKE to %s:%d\n", this->ipString,
	     this->portNum );
  int nlenUnchoke = htonl(1);
  char unchokeID = 1;
  char msg[5];
  memmove( &msg[0], &nlenUnchoke, 4 );
  memmove( &msg[4], &unchokeID, 1 );
  SS_Push( this->outgoingData, msg, 5 );

  return;
}

void sendChoke( struct peerInfo * this, struct torrentInfo * t ) {


  // Send them back an unchoke message.
  logToFile( t, "SEND MESSAGE CHOKE to %s:%d\n", this->ipString,
	     this->portNum );
  int nlenChoke = htonl(1);
  char chokeID = 0;
  char msg[5];
  memmove( &msg[0], &nlenChoke, 4 );
  memmove( &msg[4], &chokeID, 1 );
  SS_Push( this->outgoingData, msg, 5 );

  return;
}

void sendPieceRequest( struct peerInfo * p, 
		       struct torrentInfo * t , 
		       int pieceNum, 
		       int subChunkNum ) {

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

  logToFile( t, "SEND REQUEST %d.%d ( %d-%d ) FROM %s\n", 
	     pieceNum, subChunkNum, sc.start, sc.end, p->ipString);

  SS_Push( p->outgoingData, request, 17 );

  p->numPendingSubchunks ++;
  
}

