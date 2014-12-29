#ifndef STRING_STREAM_BM_H
#define STRING_STREAM_BM_H

/*
  StringStream.h - Function declarations for the StringStream interface,
  which provides a buffer abstraction for pushing data to the end of the
  buffer and reading data from the start of the buffer.
 */

extern void * Malloc( size_t );

typedef struct {

  char * data;  // What are we storing 
  char * head;  // Where does the unread data start 
  char * tail;  // Where should new data be inserted
  int size;     // How much data do we have?
  int capacity; // How much data do we have room for?

} StringStream ;


/*
  SS_Init - Create and initialize a StringStream object.

  Parameters: None.

  Returns: A pointer to an initialized StringStream structure.
 */
StringStream * SS_Init() ;

/*
  SS_Destroy - destroy and free all resources associated with an 
  existing StringStream.

  Parameters: 
  => s - StringStream object pointer to destroy

  Returns: Nothing.
 */
void SS_Destroy( StringStream * s ) ;

/*
  SS_Push - append data to the end of the data stream and
  update the associated state.
 
  Parameters:
  => s - the StringStream to append the data to
  => new - a pointer to a buffer containing the data
  => len - the number of bytes in the buffer

  Returns:
  Nothing.
 */
void SS_Push( StringStream * s, void * new, int len ) ;

/*
  SS_Pop - Remove data from the beginning of the data stream
  and update the associated state. Exits on error if more 
  bytes are removed than are currently on the stream.

  Parameters:
  => s - the StringStream to read from
  => numBytes - the number of bytes to read from the stream

  Returns: Nothing
 */
void SS_Pop( StringStream * s, int numBytes ) ;

/*
  SS_Print - Print a graphical representation of the 
  StringStream object.

  Parameters:
  => s - StringStream object to print.
 */
void SS_Print( StringStream * s ) ;
#endif 
