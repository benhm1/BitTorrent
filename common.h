#ifndef _BM_BT_COMMON_H_
#define _BM_BT_COMMON_H_

/***************************************************
  Included Headers
****************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <netdb.h>
#include <time.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <sys/signal.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <ifaddrs.h>
#include <fcntl.h>
#include <assert.h>
#include <openssl/sha.h> //hashing pieces

#include "bitfield/bitfield.h"
#include "StringStream/StringStream.h"

/***************************************************
  Preprocessor defined variables     
****************************************************/

// Different states that connections can be in
#define BT_AWAIT_INITIAL_HANDSHAKE 2  // They connected to us
#define BT_AWAIT_RESPONSE_HANDSHAKE 3 // We connected to them
#define BT_AWAIT_BITFIELD 4           // Bitfield messages acceptable
#define BT_RUNNING 5                  // Connection is normal

// Different classifications for connections
#define BT_PEER 1    // Sharing data, but may not have complete file
#define BT_SEED 2    // Has complete file, sharing data
#define BT_UNKNOWN 3 // We just connected; don't know whether peer or seed

// Different types of tracker messages
#define TRACKER_STARTED 1   // Sent when torrent download is starting
#define TRACKER_STOPPED 2   // Sent when torrent download has stopped 
#define TRACKER_COMPLETED 3 // Sent when torrent has finished downloading
#define TRACKER_STATUS 4    // Sent for routine updates

#define BACKLOG 20  // Max number of connections to wait for

// How many requests should each peer have at once?
#define MAX_PENDING_SUBCHUNKS 10 

// How long should we wait for idle connections before closing them?
#define MAX_TIMEOUT_WAIT 20 

/***************************************************
  Structure Definitions
****************************************************/


/* 
   An argsInfo struct stores information passed in through
   the command line.
 */
struct argsInfo {
  char * saveFile;  // Path for saving torrent file
  char * logFile ;  // Path for logfile
  char * nodeID;    // Unique ID for our client
  char * fileName;  // Name of .torrent file
  int maxPeers;     // Max number of peers to support
  int bindAddress ; // IP address to listen for connections
  unsigned short bindPort; // Port to bind to when listening
};

/*
  A subChunk struct stores information about a part of a larger
  file piece, since file pieces are too large to be transmitted 
  int a single message.
 */
struct subChunk {
  int start;  // Offset into larger piece where this subchunk data starts
  int end;    // Ending offset into this larger piece where this subchunk
              // data ends.
  int len;    // Length of subchunk
  int have;   // Boolean if subchunk has been received or not
  int requested; // Boolean if subchunk has been requested or not
  int requestTime; // Time in seconds past the epoch of last request sent
};

/*
  A chunkInfo struct stores information about a piece of the file
  being torrented.
 */
struct chunkInfo {

  int prevalence; // How many people that we are connected to have this piece?
  int size;  // How long is this piece?
  int have;  // Boolean do we have this piece or not? 
  int requested; // Boolean have we requested this piece?
  char * data;  // Pointer to data buffer for this piece
  char hash[20]; // SHA1 hash of piece
  int numSubChunks; // Number of subChunks corresponding to this piece
  struct subChunk * subChunks; // Pointer to array of subChunk structs
};


/*
  A torrentInfo struct stores all of the relevant information for the current 
  download that is occurring. 
 */
struct torrentInfo {

  /*
    Information for communicating with the tracker
   */
  char * announceURL;   // Full tracker URL from .torrent file
  char * trackerDomain; // Truncated URL for DNS queries
  char * trackerIP;     // String IP of tracker server
  int trackerPort;      // Port to connect to tracker server on.

  // Hash of bencoded dictioanry in .torrent file
  unsigned char * infoHash;

  // Our unique ID 
  char * peerID;

  /*
    Download file metadata from .torrent file
   */
  char * name;
  char * comment; 
  int date;

  /*
    General torrent state information.
   */
  int completed; // Have we finished downloading the file 
  long long numBytesDownloaded;
  long long numBytesUploaded  ;
  unsigned long long timer;   // Startup time ms
  int lastPrint;  // Time of last printing of status to screen


  /*
    Information about the downloaded file data.
   */
  int totalSize; // How much data is the file we are downloading?
  int chunkSize; // How large is each chunk?
  int numChunks; // How many chunks is it broken into?
  struct chunkInfo * chunks; // Array of chunkInfo structs, defined above
  struct chunkInfo ** chunkOrdering; // Array of pointers to each chunk,
                                     // with chunks sorted in order of 
                                     // prevalence (rarest chunks first).
  int numPrevalenceChanges; // Number of times prevalence numbers have
                            // changed (due to have messages, bitfields,
                            // closed connections) since we last sorted
                            // the chunks by prevalence.
  // Pointer to the beginning of a memory mapped file that we are downloading.
  // AKA - a huge array storing all of the downloaded data.
  char * fileData;

  // Which file pieces do we have?
  Bitfield * ourBitfield;


  /*
    Information about people connected to us ...
   */

  // Array of peerInfo structs with state for each connection
  struct peerInfo * peerList;

  // How many people are in this list?
  int peerListLen;

  // Pointer to the peerInfo struct of the peer that is currently
  // optimistically unchoked
  struct peerInfo * optimisticUnchoke;

  // How many times have we executed the choking algorithm?
  // Since we execute it every 10 seconds, but only change
  // optimistic unchoking every 30 seconds, this allows us to
  // keep track of whether we need to optimisticlaly unchoke
  // somebody else.
  int chokingIter;
 
  // Type breakdown of connections
  int numPeers;  
  int numSeeds;
  int numUnknown;
  int maxPeers;  

  // Where we're listening for incoming connections
  int bindAddress;
  unsigned short bindPort;


  /*
    Misc other important parameters
   */
  
  // ID of timers we're using to signal the 
  // timeout and choking algorithms.
  timer_t timerTimeoutID; 
  timer_t timerChokeID;
  
  // Logs at a high level of verbosity
  FILE * logFile;

};

/*
  A peerInfo struct stores all of the information and state
  about somebody connected to us.
 */
struct peerInfo {

  // Is this struct initialized to current data?
  int defined;   

  // Where are we in the connection process?
  int status ; // Awaiting handshake, connected, etc...
  int type; // Peer, seed, unknown
  
  // Connection information
  int socket ;  
  char ipString[16];
  unsigned short portNum;

  // Boolean state variables
  int peer_choking ;
  int am_choking ;
  int peer_interested ;
  int am_interested ;

  // What blocks do they have
  Bitfield * haveBlocks ;

  /*
    Information for receiving from them 
  */
  int readingHeader; // Are we reading in a message header?
  // How much more data in this message?
  int incomingMessageRemaining ; 
  // What data have we received ?
  char * incomingMessageData ;
  // How much data have we received?
  int incomingMessageOffset;
  // When was the last time we heard from them?
  int lastMessage;
  // How much have we downloaded from them? 
  /*
    Note - technically used to store upload amounts
    as well once we have finished downloading the
    torrent. This is used when running the choking 
    algorithm to determine who we should unchoke.
   */
  int downloadAmt;
  // Have they been marked to be unchoked soon?
  int willUnchoke;
  // Are they new? (And thus more likely to be
  // opportunisitically unchoked?)
  int firstChokePass;

  /*
    Information for sending to them.
   */
  // What do we want to send them
  StringStream * outgoingData ;
  // If they're choking us, when was the last time we
  // asked to be unchoked?
  int lastInterestedRequest;
  // When was the last time we wrote to them ?
  int lastWrite;
  // How many subchunks have we requested from them?
  int numPendingSubchunks;
  


};


/*
  Global variable for signal handling
*/
struct torrentInfo * globalTorrentInfo;


#endif
