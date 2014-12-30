/*
  choke.c - contains function definitions for all functions
  associated with choking and unchoking peers as described
  in Cohen's specification. Includes functions for choosing
  a peer to optimistically unchoke, selecting the other peers
  to be unchoked, and sending the necessary choke and unchoke
  messages.
*/


#include "choke.h"
extern void * Malloc( size_t );
extern void sendChoke( struct peerInfo *, struct torrentInfo * );
extern void sendUnchoke( struct peerInfo *, struct torrentInfo * );

int optimisticUnchoke( struct torrentInfo * t ) {
  /*
    Optimistically unchoke one peer.

    Returns 1 if the peer was interested; 0 if
    the peer was not interested.
   */
  int i, numInLottery;

  // Each peer is put into an array. Peers that are
  // relatively new are put in 3 times, to make it
  // more likely that we optimistically unchoke them.
  struct peerInfo ** lottery = 
    Malloc( 3 * sizeof( struct peerInfo * ) * t->numPeers );

  numInLottery = 0;
  for ( i = 0; i < t->peerListLen; i ++ ) {
    if (! t->peerList[i].defined ) {
      continue;
    }
    if ( t->peerList[i].type == BT_PEER ) {
      lottery[ numInLottery++ ] = &t->peerList[i];
      if ( t->peerList[i].firstChokePass ) {
	t->peerList[i].firstChokePass = 0;
	lottery[ numInLottery++ ] = &t->peerList[i];
	lottery[ numInLottery++ ] = &t->peerList[i];
      }
    }
  }

  struct peerInfo * chosen;
  int ret;
  if ( numInLottery > 0 ) {
    chosen = lottery[ rand() % numInLottery ];
    chosen->willUnchoke = 1;
    ret = chosen->peer_interested;
  }
  else {
    chosen = NULL;
    ret = 0;
  }

  t->optimisticUnchoke = chosen;
  free( lottery );
  return ret;

}

void unchokePeers( struct torrentInfo * t, int num ) {

  int i, j;

  // Set willUnchoke to zero for all peers except the
  // optimisticllly unchoked one.
  for ( i = 0; i < t->peerListLen; i ++ ) {
    if ( ! t->peerList[i].defined ) {
      continue;
    }
    if ( t->optimisticUnchoke &&
	 ( t->optimisticUnchoke != &t->peerList[i] ) ) {
      t->peerList[i].willUnchoke = 0;
    }
  }
  
  // Set willUnchoke to 1 for the top NUM peers who are
  // interested in downloading from us.
  int maxSpeed, maxIdx;
  for ( i = 0; i < num; i ++ ) {
    maxSpeed = maxIdx = -1;
    for ( j = 0; j < t->peerListLen; j ++ ) {
      if ( ! t->peerList[j].defined ) {
	continue;
      }
      if ( t->peerList[j].willUnchoke ) {
	continue;
      }
      if ( ! t->peerList[j].peer_interested ) {
	// Peer is not interested
	continue;
      }
      if ( t->peerList[j].downloadAmt > maxSpeed ) {
	maxSpeed = t->peerList[j].downloadAmt ;
	maxIdx = j;
      }
    }
    if ( maxIdx >= 0 ) {
      t->peerList[ maxIdx ].willUnchoke = 1;
    }
    else {
      // No more viable unchoking candidates.
      break;
    }
  }

  /* Unchoke anybody who is uninterested but has sent
     us a lot of data */
  for ( i = 0; i < t->peerListLen; i ++ ) { 
    if ( ! t->peerList[i].defined ) {
      continue;
    }
    if ( t->peerList[i].peer_interested ) {
      // Peer is interested - we've already
      // considered unchoking them
      continue;
    }

    if ( t->peerList[i].downloadAmt > maxSpeed ) {
      t->peerList[i].willUnchoke = 1;
    }
  }

  return;
  
}

void sendChokeUnchokeMessages( struct torrentInfo * t ) {
  /*
    Send people whose status has changed a message notifying them.

    Interested + Choked => Interested + Unchoked 
        Send Unchoke message

    Interested + Unchoked => Interested + Choked
        Send Choke message

    Not interested + High download speeds +
    Choked => Not interested + Unchoked
        Send Unchoke Message
  
  */
  int i;
  for ( i = 0; i < t->peerListLen; i ++ ) {
    if ( t->peerList[i].defined == 0 ) {
      continue;
    }
    if ( t->peerList[i].willUnchoke &&
	 t->peerList[i].am_choking == 1 ) {
      sendUnchoke( &t->peerList[i], t );
      t->peerList[i].am_choking = 0;
    }
    else if ( t->peerList[i].am_choking == 0 &&
	      t->peerList[i].willUnchoke == 0 ) {
      // They were unchoked, now they are not
      t->peerList[i].am_choking = 1;
      sendChoke( &t->peerList[i], t );
    }
    else { 
      // Status was unchanged; do nothing
    }
  }


}

void manageChoking( struct torrentInfo * t ) {

  // Every 30 seconds, choose a random peer to be optimistically unchoked
  if ( !((t->chokingIter ++) % 3) ) {
    optimisticUnchoke ( t );
  }

  int numRemainingUnchokes = 
    4 - ( ( t->optimisticUnchoke &&
	    t->optimisticUnchoke->defined &&
	    t->optimisticUnchoke->peer_interested ) ? 1 : 0 );

  // If this peer is interested, then choose 3 other interested peers
  // to also be unchoked. Otherwise, choose 4 interested peers to be
  // unchoked.
  unchokePeers(t, numRemainingUnchokes );

  sendChokeUnchokeMessages( t );

  // Zero out download amounts for then ext round
  int i;
  for ( i = 0; i < t->peerListLen; i ++ ) {
    if ( t->peerList[i].defined ) {
      t->peerList[i].downloadAmt = 0;
    }
  }

  return ;

}

void chokingHandler( int sig, siginfo_t * si, void * uc ) {

  struct torrentInfo* t = (struct torrentInfo *) si->si_value.sival_ptr ;

  manageChoking( t );
  return;
}
