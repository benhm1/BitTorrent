#ifndef _BM_BT_BASE
#define _BM_BT_BASE
/*
  base.h - Function declarations for library-type
  functions used in the bittorrent client - ie:
  functions that are useful outside of the bittorrent
  world.
 */

#include "../common.h"

/*
  computeSHA1 - computes the SHA1 hash of a set of bytes, returning
  the resulting hash in a dynamically allocated buffer of 20 bytes.

  Parameters: 
  => data - a pointer to the data to be hashed
  => size - the number of bytes in the buffer

  Returns:
  => A pointer to a dynamically allocated buffer containing the
  SHA1 hash.
*/
unsigned char * computeSHA1( char * data, int size ) ;


/*
  logToFile - Write out a timestamp and log message to a file
  detailing program status.

  Parameters:
  => torrent - torrentInfo struct with current download status
  => format - a format string to write out to the file
  => ...    - specifications for the format strings

  Returns: Nothing.
*/
void logToFile( struct torrentInfo * torrent, const char * format, ... ) ;

/*
  nonBlockingConnect - Connect to a host in a non-blocking way. Does this
  by putting the sockets into non-blocking mode and then using select 
  to wait for the socket to be writable with a timeout. The socket
  returned is in no longer non-blocking.

  Parameters:
  => ip - pointer to string IP to connect to
  => port - the port number we are connecting to
  => sock - the socket to connect

  Returns: zero if conecting was successful; non-zero if an
  error occurred.
*/
int nonBlockingConnect( char * ip, unsigned short port, int sock ) ;

/*
  Malloc - wrapper around malloc() that exits if there is an 
  error allocating memory.

  Parameters:
  => size - number of bytes to allocate

  Returns:
  => pointer to allocated block
 */
void * Malloc( size_t size ) ;

/*
  setupSignals - a wrapper function for installing a signal
  handler taken from the CMU systems textbook.

  Parameters:
  => signum - the signal number we are installing the handler for
  => handler - a function that takes in an int and returns nothing
               that is installed as the handler.

  => Returns : The old handler function
 */
typedef void handler_t(int);
handler_t * setupSignals(int signum, handler_t * handler) ;


/*
  lookupIP - given a hostname, looks up the IP address
  and copies the string representation into a buffer.

  Parameters:
  => hostname - the hostname to lookup the IP for
  => IP - buffer where IP string should be placed
  => len - length of IP buffer.

  Returns: Nothing. Modifies the passed in IP buffer.
 */
void lookupIP( char * hostname, char * IP, int len ) ;

/* Compute the minimum of a and b */
int min( int a, int b );

#endif

