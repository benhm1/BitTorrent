
/*
  timer.c - function definitions for timed signal handling and signal
  blocking / unblocking. This allows the user to define a large set of
  handlers that run at specified intervals (ie - multiple alarms). 
*/

#include "timer.h"

void blockSignal( int signum ) {

  sigset_t mask;
  sigemptyset(&mask);
  sigaddset(&mask, signum);
  if (sigprocmask(SIG_BLOCK, &mask, NULL) == -1) {
    perror("sigprocmask");
    exit(1);
  }
  return;

}

void unblockSignal( int signum ) {

  sigset_t mask;
  sigemptyset(&mask);
  sigaddset(&mask, signum);
  if (sigprocmask(SIG_UNBLOCK, &mask, NULL) == -1) {
    perror("sigprocmask");
    exit(1);
  }
  return;

}

timer_t setupSignal( int signum, void (*handler)(int, siginfo_t*, void*), 
		     int delay, void * value ) {
  /*
    Sets up a signal handler for signal signum that will run after a 
    delay of delay seconds and pass the value value into the handler as an
    argument
   */
  
  blockSignal( signum );

  /* Set up the signal handler */
  struct sigaction sa;
  sa.sa_flags = SA_SIGINFO;
  sa.sa_sigaction = handler;
  sigemptyset(&sa.sa_mask);
  if (sigaction(signum, &sa, NULL) == -1) {
    perror("sigaction");
    exit(1);
  }

  /* Set up the timer */
  struct sigevent sev;
  timer_t timerid;

  sev.sigev_notify = SIGEV_SIGNAL;
  sev.sigev_signo = signum;
  sev.sigev_value.sival_ptr = value;
  if (timer_create(CLOCK_REALTIME, &sev, &timerid) == -1) {
    perror("timer_create");
    exit(1);
  }

  struct itimerspec its;
  its.it_value.tv_sec = delay;
  its.it_value.tv_nsec = 0;
  its.it_interval.tv_sec = delay;
  its.it_interval.tv_nsec = 0;

  if (timer_settime(timerid, 0, &its, NULL) == -1) {
    perror("timer_settime");
    exit(1);
  }

  unblockSignal( signum );

  return timerid;

}


