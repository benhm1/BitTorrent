
/* 
   algorithms.c - function definitions for various utility functions
   associated with extensions to the core BT protocol, such as 
   requesting rare chunks first and timing out idle connections.

*/

#include "algorithms.h"

int sortChunksComparator( const void * a, const void * b ) {
  struct chunkInfo * ca = (struct chunkInfo *) a;
  struct chunkInfo * cb = (struct chunkInfo *) b;
  
  return ca->prevalence - cb->prevalence  ;
  
}


void sortChunks( struct torrentInfo * t ) {

  if ( t->numPrevalenceChanges > 5 ) {
    qsort( t->chunkOrdering, t->numChunks, 
	   sizeof( struct chunkInfo * ), 
	   sortChunksComparator );
    t->numPrevalenceChanges = 0;
  } 
}



void timeoutDetection( int sig, siginfo_t * si, void * uc ) {

  int i;

  struct torrentInfo* t = (struct torrentInfo *) si->si_value.sival_ptr ;

  struct timeval tv;
  if ( gettimeofday( &tv, NULL ) ) {
    perror("gettimeofday");
    exit(1);
  }


  for ( i = 0; i < t->peerListLen; i ++ ) {
    if ( t->peerList[i].defined &&
	 tv.tv_sec - t->peerList[i].lastWrite > 3 &&
	 tv.tv_sec - t->peerList[i].lastMessage > MAX_TIMEOUT_WAIT ) {
      /*
	Stop talking to people who we've sent stuff to a while ago and
	they haven't responded to us in a reasonable amount of time.
       */
      logToFile( t, "STATUS TIMEOUT %s:%d\n", 
		 t->peerList[i].ipString, t->peerList[i].portNum );
      destroyPeer( &t->peerList[i], t );
    }
  }

  return;

}

