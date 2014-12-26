
/*

  struct peerInfo * unchokedPeers[4];

  int chokingIter;
  struct peerInfo * unchokedPeers[4];
  stuct peerInfo * optimisticUnchoked;


  t->unchokedPeers is a peerInfo **, that is
  t->unchokedPeers[0] is a peerInfo * pointing to the
  fastest peer;
 */


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

int comparePeersDownload( void * p1, void * p2 ) {
  
  // Handle null elements
  if ( !p1 && !p2 ) {
    return 0;
  }
  if ( !p1 && p2 ) {
    return -1;
  }
  if ( !p2 && p1 ) {
    return 1;
  }

  struct peerInfo * peer1 = (struct peerInfo *) p1;
  struct peerInfo * peer2 = (struct peerInfo *) p2;
  
  return peer1->downloadAmt - peer2->downloadAmt ;

}

void unchokePeers( struct torrentInfo * t, int num ) {

  int i, j;

  for ( i = 0; i < t->peerListLen; i ++ ) {
    if ( ! t->peerList[i].defined ) {
      continue;
    }
    if ( t->optimisticUnchoke &&
	 ( t->optimisticUnchoke != &t->peerList[i] ) ) {
      t->peerList[i].willUnchoke = 0;
    }
  }
  
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

int manageChoking( struct torrentInfo * t ) {

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

  return 0;

}

void chokingHandler( int sig, siginfo_t * si, void * uc ) {

  struct torrentInfo* t = (struct torrentInfo *) si->si_value.sival_ptr ;

  manageChoking( t );
  return;
}
