#ifndef _BM_BT_OUTGOING_H_
#define _BM_BT_OUTGOING_H_

/*
  outgoingMessages.h - function declarations for functions that generate
  and append different bittorrent protocol messages to our peers.

  Messages: Have, Bitfield, Unchoke, Choke, Interested, Request

  Piece messages are generated in handlePieceMessage function,
  declared in incomingMessages.h and implemented in incomingMessages.c

  We do not support Port and Cancel messages. We do not send KeepAlive 
  messages. 

*/

#include "../common.h"

/*
  broadcastHaveMessage - Send a HAVE message to all of our connected peers
  notifying them that we have finished downloading a new piece.

  Parameters:
  => torrent - a torrentInfo struct pointer to the current torrent
  => blockIdx - the piece number we have finished downloading.

  Returns: Nothing, but modifies the outgoingData stream for the
  peers that will receive the message.
 */
void broadcastHaveMessage( struct torrentInfo * torrent, int blockIdx ) ;


/*
  sendBitfield - Send a BITFIELD message to one of our connected
  clients notifying them of the pieces we have.

  Parameters:
  => this - a peerInfo struct pointer to the peer we are sending our
            bitfield to.
  => torrent - a torrentInfo struct pointer to the current torrent

  Returns: Nothing, but modifies the outgoingData stream for the
  peer that will receive the message.
 */
void sendBitfield( struct peerInfo * this, struct torrentInfo * torrent ) ;


/*
  sendInterested - Send a INTERESTED message to one of our connected
  clients notifying them that if they unchoke us we will download from
  them.

  Parameters:
  => p - a peerInfo struct pointer to the peer we are sending
            the message to.
  => t - a torrentInfo struct pointer to the current torrent

  Returns: Nothing, but modifies the outgoingData stream for the
  peer that will receive the message.
 */
void sendInterested( struct peerInfo * p, struct torrentInfo * t ) ;


/*
  sendUnchoke - Send a UNCHOKE message to one of our connected clients
  notifying them that we will satisfy requests if they send us
  requests.

  Parameters:
  => this - a peerInfo struct pointer to the peer we are sending
            the message to.
  => t - a torrentInfo struct pointer to the current torrent

  Returns: Nothing, but modifies the outgoingData stream for the
  peer that will receive the message.
 */
void sendUnchoke( struct peerInfo * this, struct torrentInfo * t ) ;


/*
  sendChoke- Send a CHOKE message to one of our connected clients
  notifying them that we will not satisfy requests if they send us
  requests.

  Parameters:
  => this - a peerInfo struct pointer to the peer we are sending
            the message to.
  => t - a torrentInfo struct pointer to the current torrent

  Returns: Nothing, but modifies the outgoingData stream for the
  peer that will receive the message.
 */
void sendChoke( struct peerInfo * this, struct torrentInfo * t ) ;

/*
  sendPieceRequest- Send a REQUEST message to one of our connected
  clients requesting that they send us some file data.

  Parameters:
  => p - a peerInfo struct pointer to the peer we are sending
            the message to.
  => t - a torrentInfo struct pointer to the current torrent
  => pieceNum - The piece number we are requesting from
  => subChunkNum - The subchunk of pieceNum that we are requesting

  Returns: Nothing, but modifies the outgoingData stream for the
  peer that will receive the message.
 */
void sendPieceRequest( struct peerInfo * p, 
		       struct torrentInfo * t , 
		       int pieceNum, 
		       int subChunkNum ) ;

#endif
