/*
  base.c - Function implementations for library-type
  functions used in the bittorrent client - ie:
  functions that are useful outside of the bittorrent
  world.
 */

#include "base.h"

// Minimum of two numbers
int min( int a, int b ) { return a > b ? b : a; }

void * Malloc( size_t size ) {

  void * toRet = malloc( size );
  if ( ! toRet ) {
    perror( "malloc" );
  }
  return toRet;

}

handler_t * setupSignals(int signum, handler_t * handler) {  
  struct sigaction action, old_action; 
  action.sa_handler = handler;   
  sigemptyset(&action.sa_mask);  
  action.sa_flags = SA_RESTART; 
  if (sigaction(signum, &action, &old_action) < 0) {  
    perror("sigaction"); 
  }
  return (old_action.sa_handler);     
}  


// Given a hostname, returns its IP address or exits if DNS lookup fails
void lookupIP( char * hostname, char * IP, int len ) {

  struct addrinfo * result;
  if (getaddrinfo(hostname, 0, 0, &result)) {
    printf("\nError: Invalid hostname provided: %s\n", hostname);
    exit(1);
  }
  
  if (result != NULL) {
    // From sample code - converts the address to a char array and copies it
    strncpy(IP, inet_ntoa(((struct sockaddr_in*)result->ai_addr)->sin_addr),
	   len);
  }
  else {
    printf("\nError: Could not resolve hostname to an IP address.\n");
    exit(1);
  }
  freeaddrinfo(result);
}



unsigned char * computeSHA1( char * data, int size ) {

  unsigned char * toRet = Malloc( 20 ); // SHA_DIGEST_LENGTH
  SHA1( (unsigned char*) data, size, toRet );
  return toRet;

}

void logToFile( struct torrentInfo * torrent, const char * format, ... ) {

  va_list args;
  va_start( args, format );

  struct timeval tv;
  if ( gettimeofday( &tv, NULL ) ) {
    perror("gettimeofday");
    exit(1);
  }

  unsigned long long msDiff = 
    tv.tv_sec * 1000000 + tv.tv_usec - torrent->timer ;

  fprintf( torrent->logFile, "[%10.4f] ", 1.0 * msDiff / 1000000 );

  vfprintf( torrent->logFile, format, args );


}

int nonBlockingConnect( char * ip, unsigned short port, int sock ) {

  struct sockaddr_in addr;
  addr.sin_family = AF_INET;
  addr.sin_port = htons( port ); 
  addr.sin_addr.s_addr = inet_addr( ip ) ;
 
  // Set socket to nonblocking for connect, in case it fails
  int flags = fcntl( sock, F_GETFL );
  int res = fcntl(sock, F_SETFL, O_NONBLOCK);  // set to non-blocking
  if ( res < 0 ) {
    perror("fcntl");
    exit(1);
  }

  res = connect( sock, (struct sockaddr*) &addr, sizeof(addr) );
  // By default, since we are non blocking, we will return 
  // EINPROGRESS. The canonical way to check if connect() was 
  // successful is to call select() on the file descriptor
  // to see if we can write to it. This allows us to use a 
  // timeout and cancel the operation while remaining non-blocking
  // See: http://developerweb.net/viewtopic.php?id=3196
  int error = 0;

  if (res < 0) { 
    if (errno == EINPROGRESS) { 
      fd_set myset;
      struct timeval tv;
      tv.tv_sec = 3; 
      tv.tv_usec = 0;
      FD_ZERO(&myset); 
      FD_SET(sock, &myset); 
      res = select(sock+1, NULL, &myset, NULL, &tv); 
      if (res < 0 && errno != EINTR) { 
	perror("select");
	exit(1);
      } 
      else if (res > 0) { 
	// Socket selected for write 
	int valopt;
	unsigned int lenInt = sizeof(int);
	if (getsockopt(sock, SOL_SOCKET, SO_ERROR, (void*)(&valopt), &lenInt) < 0) { 
	  // Error in getsockopt() 
	  error = 1;
	} 
	// Check the value returned... 
	if (valopt) { 
	  // Error in delayed connection() 
	  error = 1;
	} 
      }
      else { 
	// Timeout in select()
	error = 1;
      }
    } 
    else { 
      // Error connecting address
      error = 1;
    } 
  }


  // And, make it blocking again
  res = fcntl( sock, F_SETFL, flags );
  if ( res < 0 ) {
    perror("fcntl set blocking");
    exit(1);
  }


  return error;

}
