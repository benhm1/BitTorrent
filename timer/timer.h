#ifndef _BM_TIMER_H_
#define _BM_TIMER_H_

#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <signal.h>
#include <time.h>

void blockSignal( int signum );
void unblockSignal( int signum );
int setupSignal( int signum, void (*handler)(int, siginfo_t*, void*), 
		 int delay, void * value ) ;


#endif