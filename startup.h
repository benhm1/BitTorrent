#ifndef _BM_BT_STARTUP
#define _BM_BT_STARTUP

/*
  startup.h - Function declarations for things we need to do on startup.
*/

#include "common.h"
#include "utils/base.h"
#include "utils/bencode.h"

/*
  processBencodedTorrent - isolates the messiness of the bencode
  library in a single place; iterates through the .torrent file
  and uses it to initialize the struct torrentInfo structure that
  will be used for the entire duration of the program.
*/

struct torrentInfo* processBencodedTorrent( be_node * data, 
					    struct argsInfo * args ) ;

/*
  Bind to our listening address and port, and 
  begin listening for connections on that port.

  Parameters:
  => args - argsInfo struct containing the command line
  arguments, specifically the bind IP and port.

  Returns - the socket that we will use to listen
  for connections.
 */
int setupListeningSocket( struct argsInfo * args ) ;

/*
  freeArgs - clean up a struct argsInfo so that
  we don't leak memory.

  Parameters: 
  => args - struct argsInfo to destroy

  Returns: Nothing.
 */
void freeArgs( struct argsInfo * args ) ;

/*
  parseArgs - parse the command line arguments and use them
  to initialize an argsInfo struct, which is returned and 
  used for more processing.

  Arguments:
  => argv and argc (system generated)

  Returns:
  => pointer to a dynamically allocated argsInfo structure
  that has been initialized based on the command line.
 */
struct argsInfo * parseArgs( int argc, char ** argv ) ;


/*
  usage - prints out the expected command line arguments
  and an explination of their meaning.

  Parameters:
  file - the file to print out to (stdout or stderr)

  Returns: 
  Nothing - exits.
 */
void usage(FILE * file);

/*
  generateID - generates a client ID to send to the tracker based on
  the startup time and the IP addresses of the system.

  Parameters: None.

  Returns - a pointer to a dynamically allocated buffer containing
  the generated ID.
 */
char * generateID() ;

#endif
