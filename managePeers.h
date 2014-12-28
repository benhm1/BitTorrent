#ifndef _BM_BT_MANAGE_PEERS
#define _BM_BT_MANAGE_PEERS

/*

  managePeers.h - Function declarations for accepting new connections,
  destroying connections, and maintaining the peerList in the 
  torrentInfo struct.

*/

#include "common.h"
#include "utils/base.h"

/*
  destroyPeer - close down our connection and clean up any associated state
  we had with them. Update the appropriate counters to reflect them leaving.

  Parameters:
  => peer - pointer to the peerInfo struct to destroy
  => torrent - the torrentInfo structure for our current download

  Returns:
  Nothing.
 */

void destroyPeer( struct peerInfo * peer, struct torrentInfo * torrent ) ;

/*
  getFreeSlot - find an unused slot in the peerList for our torrent,
  expanding the list if it is full.

  Parameters:
  => torrent - the torrentInfo struct for our current download.

  Returns:
  => Index into the peerList array that we should use for this peer
  
 */
int getFreeSlot( struct torrentInfo * torrent ) ;

/*
  peerConnectedToUs - called when somebody has initiated a connection
  with us; initializes the data structures for their connection and
  sets us up to receive their handshake.

  Arguments:
  => torrent - the torrentInfo struct for the current download
  => listenFD - the file descriptor for our listening socket who we 
     accept from

  Returns: Nothing.
 */
void peerConnectedToUs( struct torrentInfo * torrent, int listenFD ) ;

/*
  connectToPeer - initialize the socket for a new peer and connect
  to them, immediately sending the handshake upon connecting.

  Parameters:
  => this - peerInfo struct we are connecting
  => torrent - torrentInfo struct for the current download
  => handshake - the handshake we will need to send to the newly connected
     peer.

  Returns: zero if connecting was successful; non-zero if there was an 
  error connecting.
 */
int connectToPeer( struct peerInfo * this, 
		   struct torrentInfo * torrent, 
		   char * handshake) ;

/*
  initializePeer - initialize all of the data structures in a new 
  peerInfo struct, and set up the peer to receive a handshake message,
  receive preferential treatment in the unchoking lottery, etc.

  Arguments:
  => this - pointer to peerInfo struct to initialize
  => torrent - pointer to torrentInfo struct for current download
 */
void initializePeer( struct peerInfo * this, struct torrentInfo * torrent );

#endif
