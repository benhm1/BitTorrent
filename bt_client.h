#ifndef _BM_BT_CLIENT
#define _BM_BT_CLIENT

/*
  bt_client.h - Function declarations for the main select loop and handling 
  I/O with connected peers and seeds.
 */

#include "common.h"


/*
  destroyTorrentInfo - tear down our torrentInfo struct and exit the program.
  This destroys all of our state associated with the current download and 
  frees all of our resources.

  Parameters: None.

  Returns: Nothing.
 */
void destroyTorrentInfo( ) ;

/*
  setupReadWriteSets - setup our read and write file descriptor sets
  for our next call to select. We want to read from everybody and write
  to anybody who has pending data.

  Parameters:
  => readPtr - pointer to fd_set for read fds
  => writePtr - pointer to fd_set for write fds
  => torrent - pointer to torrentInfo struct for current download

  Returns: The highest numbered FD in all sets.
 */
int setupReadWriteSets( fd_set * readPtr, 
			fd_set * writePtr, 
			struct torrentInfo * torrent ) ;

/*
  handleWrite - write as much pending data as we can to this connected
  client. Advances the write pointer in the outgoing buffer and updates
  the timestamp for our last write.

  Parameters:
  => this - peerInfo struct for the connected client we want to write to
  => torrent - pointer to current torrentInfo structure for download

  Returns: Nothing.
 */
void handleWrite( struct peerInfo * this, struct torrentInfo * torrent );

/*
  handleRead - read from a connected client and put the data in the
  incoming data buffer. If we have received all we were expecting to
  receive, then handle thhe complete message. Disconnects the peer 
  and cleans up state on error or connection close.

  Parameters:

  Parameters:
  => this - peerInfo struct for the connected client we want to read from
  => torrent - pointer to current torrentInfo structure for download

  Returns: Nothing.

 */
void handleRead( struct peerInfo * this, struct torrentInfo * torrent ) ;

/*
  handleActiveFDs - Iterates through all the file descriptors, calling
  handleRead or handleWrite as appropriate based on the returned fd_sets
  from select.

  Parameters:
  => readFDs - fd_set returned by select() as ready for reading
  => writeFDs - fd_set returend by select() as ready for writing
  => torrent - pointer to current torrentInfo structure for download
  => listeningSock - fd of our socket that is listening for connections

  Returns: Nothing.
 */
void handleActiveFDs( fd_set * readFDs, 
		      fd_set * writeFDs, 
		      struct torrentInfo * torrent, 
		      int listeningSock ) ;

/*
  generateMessages - creates and appends intersted and request messages to 
  connected peers and seeds based on what they have, what we want, what is
  rare, whether or not they are choking us, how much we have requested from
  them already, and how much time they have had to respond to earlier requests.

  Parameters: 
  => torrent - pointer to current torrentInfo structure for download

  Returns: Nothing.

 */
void generateMessages( struct torrentInfo * t ) ;

/*
  printStatus - prints out a graphical display of the blocks downloaded
  and requested so far, the number of connections, and the amount of data
  uploaded and downloaded.

  Parameters: 
  => torrent - pointer to current torrentInfo structure for download

  Returns: Nothing.  

 */

void printStatus( struct torrentInfo * t ) ;


/*
  main - Core program that parses command line arguments and the .torrent 
  file, communicates with the tracker, sets up our signal handlers,
  and calls select() in a loop, handling the connections.

  Parameters: system argv and argc

  Returns: 0 (but never actually returns, since SIGINT handler calls exit()
 */

int main(int argc, char ** argv) ;

#endif
