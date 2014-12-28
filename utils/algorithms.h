#ifndef _BM_BT_ALGORITHMS
#define _BM_BT_ALGORITHMS

/* 
   algorithms.h - function declarations for various utility functions
   associated with extensions to the core BT protocol, such as 
   requesting rare chunks first and timing out idle connections.

 */

#include "../common.h"
#include "base.h"
#include "../managePeers.h"

/*
  sortChunksComparator - comparison function used to compare two chunks
  by their prevalence. Returns less than zero, zero, or more than zero
  if the first chunk is less than, equal to, or greater than the
  second chunk.
 */
int sortChunksComparator( const void * a, const void * b );


/*
  sortChunks - a wrapper around qsort for sorting chunks in the torrent
  based on their prevalence (so that we request the rarest chunks first.

  Parameters:
  => t - torrentInfo struct for current download

  Returns: Nothing.
 */
void sortChunks( struct torrentInfo * t );


/*
  timoutDetection - function that kicks off peers who have not communicated
  with us in a while.

  Parameters:
  => sig - number of signal we received
  => si - information about received signal
  => uc - signal context

  Returns: Nothing.
 */
void timeoutDetection( int sig, siginfo_t * si, void * uc ) ;

#endif
