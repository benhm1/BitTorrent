#ifndef BITFIELD_BM_H
#define BITFIELD_BM_H

/*
  bitfield.h - contains function declarations for a bitfield structure,
  which implements an abstraction over getting and setting individual
  bits in a flag-type structure.
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>


typedef struct {

  char * buffer; // Characters storing 8 bits each
  int numBits;   // Number of bits we are storing
  int numBytes;  // Number of bytes in the buffer

} Bitfield ;

/*
  Bitfield_Init - Initializes a Bitfield struct designed to hold
  and keep track of numBits bits. All bits are initialized to zero.

  Returns: A pointer to a dynamically allocated bitfield struct,
  which must be freed using Bitfield_Destroy.
*/
Bitfield * Bitfield_Init( int numBits ) ;

/*
  Bitfield_Destroy - Frees and destroys a Bitfield initialized using
  Bitfield_Init.

  Returns - Nothing.
 */
void Bitfield_Destroy( Bitfield * cur );

/*
  Bitfield_FromExisting - Initializes the bits in a bitfield to be the
  same as some other existing buffer, after verifying that this buffer 
  is a valid bitfield for this structure (right size, etc).

  Returns: 0 on success; non-zero on error
*/
int Bitfield_FromExisting( Bitfield * cur, char * other, int numBytes ) ;

/*
  Bitfield_Get - Gets the value of a bit from the bitfield
  and stores it in the memory pointed to by val.

  Parameters:
  => cur - Bitfield to access
  => bitNum - Bit number to access
  => val - pointer to int that is set to 0 or 1 if the bit is not set
  or set.

  Returns: 0 on success; non-zero on error
 */
int Bitfield_Get( Bitfield * cur, int bitNum, int * val ) ;


/*
  Bitfield_Set - Set bit number bitNum in Bitfield cur
  to be 1. 

  Returns: 0 on success; non-zero on error
*/
int Bitfield_Set( Bitfield * cur, int bitNum ) ;

/*
  Bitfield_Clear: Set bit number bitNum in Bitfield cur
  to be 0.  

  Returns: 0 on success; non-zero on error
 */
int Bitfield_Clear( Bitfield * cur, int bitNum ) ;


/*
  Bitfield_AllSet - Returns 1 if all of the bits are set;
  0 if some of the bits are not set.
*/

int Bitfield_AllSet( Bitfield * cur );


/*
  Bitfield_NoneSet - Returns 1 if none of the bits are set;
  0 if some of the bits are set.
*/
int Bitfield_NoneSet( Bitfield * cur );


#endif
