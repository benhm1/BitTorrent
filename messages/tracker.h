#ifndef _BM_BT_TRACKER_H
#define _BM_BT_TRACKER_H

/*
  tracker.h - Contains function declarations for all functions
  related to communicating with the tracker server.
*/

#include "../common.h"
#include "../utils/percentEncode.h"

extern int getFreeSlot( struct torrentInfo * torrent ) ;
extern int nonBlockingConnect( char * ip, unsigned short port, int sock ) ;
extern void logToFile( struct torrentInfo * torrent, const char * format, ... ) ;
extern int connectToPeer( struct peerInfo * this, 
			  struct torrentInfo * torrent , 
			  char * handshake) ;


/*
  doTrackerCommunication - function encapsulating a complete
  transaction with the tracker, including creating the socket,
  connecting, creating the message, sending the message, 
  receiving the response, and parsing the response.

  Parameters:
  => t - struct torrentInfo with current download state
  => type - defines the type of message we're sending to the tracker.
  It can be TRACKER_STARTED, TRACKER_STOPPED, TRACKER_COMPLETED,
  or TRACKER_STATUS.

  Returns the number of seconds until the next checkin with the
  tracker.

 */
int doTrackerCommunication( struct torrentInfo * t, int type );


/*
  createTrackerMessage - Constructs a HTTP message to send to the 
  tracker server, incorporating our current state.

  Parameters:
  => torrent - struct torrentInfo with current download state
  => msgType - defines the type of message we're sending to the tracker.
  It can be TRACKER_STARTED, TRACKER_STOPPED, TRACKER_COMPLETED,
  or TRACKER_STATUS.

  Returns - a pointer to a dynamically allocated buffer containing the HTTP
  message.
 */
char * createTrackerMessage( struct torrentInfo * torrent, int msgType ) ;


/*
  parseTrackerResponse - Given a tracker response message
  (bencoded dictionary), parse the response, extracting the
  peers and time until the next checkin. Initializes and 
  connects to any new peers in the response that we do 
  not have existing connections with.

  Parameters:
  => torrent - torrentInfo struct with current download state
  => response - buffer containing the tracker response
  => responseLen - length of message stored in the buffer

  Returns:
  => 0 if there was an error with parsing the response.
  => Positive integer representing the number of seconds
     the tracker wants us to wait before checking in again.

 */
int parseTrackerResponse( struct torrentInfo * torrent, 
			  char * response, int responseLen ) ;


/*
  trackerCheckin - signal handing function for SIGALRM,
  which is our cue to contact the tracker server. Basically,
  a wrapper around doTrackerCommunication that sets a 
  SIGALRM for the next checkin.

  Parameters:
  => sig - signal number we received.
  
  Returns: Nothing.

 */
void trackerCheckin( int sig ) ;

#endif

