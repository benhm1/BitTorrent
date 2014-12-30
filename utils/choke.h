#ifndef _BM_BT_CHOKE_H
#define _BM_BT_CHOKE_H

/*
  choke.h - contains function declarations for all functions
  associated with choking and unchoking peers as described
  in Cohen's specification. Includes functions for choosing
  a peer to optimistically unchoke, selecting the other peers
  to be unchoked, and sending the necessary choke and unchoke
  messages.
*/


#include "../common.h"


/*
  Choose a peer at random to be unchoked. Maybe they'll
  become more fruitful than our current set of peers!

  Parameters: 
    struct torrentInfo * t - pointer to current torrentInfo

  Returns:
    1 if the chosen peer was interested; 0 if they were not 
    interested.
 */
int optimisticUnchoke( struct torrentInfo * t ) ;

/*
  Chooses NUM interested peers to be unchoked, choosing
  the peers that have sent us the most or who we have
  uploaded the most to (depending on whether we are
  still downloading or not).

  Parameters: 
    struct torrentInfo * t - pointer to current torrentInfo
    int num - How many interested peers to unchoke

  Returns: Nothing. Sets the willUnchoke flag in the 
  peerInfo struct of peers that will be unchoked.
 */
void unchokePeers( struct torrentInfo * t, int num ) ;

/*
  Send people whose status has changed a message notifying them.
  
  Interested + Choked => Interested + Unchoked 
      Send Unchoke message
  
  Interested + Unchoked => Interested + Choked
      Send Choke message
  
  Not interested + High download speeds +
    Choked => Not interested + Unchoked
      Send Unchoke Message
  
  Parameters: 
    struct torrentInfo * t - pointer to current torrentInfo

  Returns: Nothing. Adds data to the outgoingData stream
  of the connections whose state has changed.

*/
void sendChokeUnchokeMessages( struct torrentInfo * t ) ;

/*
  Wrapper function that chooses a peer to optimistically 
  unchoke, chooses other peers to unchoke, and sends the
  appropriate messages.

  Parameters: 
    struct torrentInfo * t - pointer to current torrentInfo

  Returns: Nothing. Modifies the downloadAmt parameter 
  in the peerInfo struct setting it to zero for the next
  round.

 */
void manageChoking( struct torrentInfo * t );


/*
  Wrapper function that is compatible with the timer signals.
  Basically extracts the torrentInfo pointer and calls 
  manageChoking.

  Parameters:
  => sig - signal number that was received, unused
  => si - siginfo struct with information about the signal
  => uc - unix context, unused

  Returns: Nothing.
 */
void chokingHandler( int sig, siginfo_t * si, void * uc );


#endif
