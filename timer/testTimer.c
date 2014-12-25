#include "timer.h"

void handler( int sig, siginfo_t * si, void * uc ) {

  printf("Caught signal %d\n", sig);
  printf("Value is: %d\n", *(int*)si->si_value.sival_ptr );
  return;

}

int main() {

  int a = 45;
  // Recurring signal here ...
  setupSignal( SIGUSR1, handler, 3, &a );

  // Manual signal here ...
  setupSignal( SIGTSTP, handler, 0, &a );

  while ( 1 ) {
    sleep(1);
  }
  return 0;

}
