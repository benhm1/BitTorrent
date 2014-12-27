#ifndef _BM_BT_INCOMING_H
#define _BM_BT_INCOMING_H

/* 
   incomingMessages.h - Function declarations for all 
   functions related to parsing received headers and
   messages. 
*/

#include "../common.h"
#include "outgoingMessages.h"

extern void logToFile( struct torrentInfo * torrent, const char * format, ... ) ;
extern unsigned char * computeSHA1( char * data, int size ) ;
extern void destroyPeer( struct peerInfo * peer, struct torrentInfo * torrent ) ;
extern int doTrackerCommunication( struct torrentInfo * t, int type );

/*
  handleFullMessage - takes any fully received message
  and processes it. For the purposes of this program,
  a full message is either :
  => A complete header 
  => A complete header with the content
  => A complete handshake message

  In the case of a full header, we allocate enough space
  to receive the full message; the next call to this function
  will process the entire message. 

  Any invalid messages or errors processing lead to 
  disconnection from the peer who sent the message.

  Message receive times are logged for the timeout 
  signal handler. 

  Parameters:
  => this - a peerInfo struct for the person who sent the message
  => torrent - the torrentInfo struct for our current download

  Returns nothing, but modifies state and potentially destroys
  the connection if an error occurs.

 */
void handleFullMessage( struct peerInfo * this, 
			struct torrentInfo * torrent ) ;

/*
  handleHaveMessage - takes a fully received HAVE header and body
  and updates the peer's bitfield accordingly. Additionally updates
  the prevalence of the piece the peer has. Finally, updates the 
  classification of this connection (peer, seed).

  Parameters:
  => this - a peerInfo struct for the person who sent the message
  => torrent - the torrentInfo struct for our current download

  Returns 0 if operation was successful and nonzero on an error,
  which will trigger destruction of the peer.

 */
int handleHaveMessage( struct peerInfo * this, struct torrentInfo * torrent ) ;

/*
  handleBitfieldMessage - takes a fully received BITFIELD header and body
  and updates the peer's bitfield accordingly. Additionally updates
  the prevalence of the piece the peer has. Finally, updates the 
  classification of this connection (peer, seed).

  Parameters:
  => this - a peerInfo struct for the person who sent the message
  => torrent - the torrentInfo struct for our current download

  Returns 0 if operation was successful and nonzero on an error,
  which will trigger destruction of the peer.

 */
int handleBitfieldMessage( struct peerInfo * this, 
			   struct torrentInfo * torrent ) ;


/*
  handleRequestMessage - takes a fully received REQUEST header and,
  if the request is valid, constructs a PIECE message to send to
  the connected peer. 

  Parameters:
  => this - a peerInfo struct for the person who sent the message
  => torrent - the torrentInfo struct for our current download

  Returns 0 if operation was successful and nonzero on an error,
  which will trigger destruction of the peer.
  
 */
int handleRequestMessage( struct peerInfo * this, 
			  struct torrentInfo * torrent );

/*
  handlePieceMessage - takes a fully received PIECE header and body
  and updates our state accordingly. Copies the received data into
  the memory mapped file in the right place. If we have finished downloading
  the piece, then we check the validity with a SHA1 hash. 

  If the piece is valid, then we send a HAVE message to all of our
  connected peers, update our bitfield, and clean up the chunk
  state so that all future requests will be served directly out
  of the file.

  If the torrent is completed, notifies the tracker server.

  Parameters:
  => this - a peerInfo struct for the person who sent the message
  => torrent - the torrentInfo struct for our current download

  Returns: Nothing. But, modifies chunk state.

 */

void handlePieceMessage( struct peerInfo * this, 
			 struct torrentInfo * torrent ) ;

#endif
