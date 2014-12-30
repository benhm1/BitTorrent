#ifndef _BM_TIMER_H_
#define _BM_TIMER_H_

/*
  timer.h - function declarations for timed signal handling and signal
  blocking / unblocking. This allows the user to define a large set of
  handlers that run at specified intervals (ie - multiple alarms). 
*/

#include <sys/types.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <signal.h>
#include <time.h>


/*
  blockSignal - wrapper to block processing of a signal until it 
  is unblocked explicitly by a call to unblockSignal.

  Parameters:
  => signum - the signal number to block.

  Returns nothing.
 */
void blockSignal( int signum );


/*
  unblockSignal - wrapper to unblock a signal previously blocked
  by a call to blockSignal.

  Parameters:
  => signum - the signal number to unblock.

  Returns nothing.
 */
void unblockSignal( int signum );

/*
  setupSignal - generate a timer and associate it with a signal and
  associated handler. The result is that the passed in function is 
  installed as a signal handler and is called on a recurring basis,
  with value passed in as a parameter.

  Parameters:
  => signum - the signal to cue and use
  => handler - the function to call upon receiving this signal. The function
  should be of the form

    void handler( int signum, siginfo_t * si, void * uc );

  => delay - amount of time between each call to the signal. Use 0 to
  not have the signal raised at all.
  => value - argument accessible in the handler through 
  si->si_value.sival_ptr; you must cast it back to something other
  than a void*.

  Returns: A timer_t timer ID, which must be freed by a call to 
  timer_delete. This is essentially opaque; you don't need to use
  it anywhere else.
 */
timer_t setupSignal( int signum, void (*handler)(int, siginfo_t*, void*), 
		 int delay, void * value ) ;


#endif
