#ifndef _BM_TIMER_H_
#define _BM_TIMER_H_

#include <sys/types.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <signal.h>
#include <time.h>

void blockSignal( int signum );
void unblockSignal( int signum );
timer_t setupSignal( int signum, void (*handler)(int, siginfo_t*, void*), 
		 int delay, void * value ) ;


#endif
