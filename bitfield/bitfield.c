
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "bitfield.h"

/*
  bitfield.h - contains function definitions for a bitfield structure,
  which implements an abstraction over getting and setting individual
  bits in a flag-type structure.
 */

static void* Malloc( size_t size ) {
  void * toRet = malloc( size );
  if ( ! toRet ) {
    perror("malloc");
    exit(1);
  }
  return toRet;
}

int Bitfield_AllSet( Bitfield * cur ) {

  int i;
  char mask[ cur->numBytes ];
  memset( mask, 0xff, cur->numBytes );

  int numExtra = (cur->numBits % 8 == 0 ? 0 : 
		  8 - cur->numBits % 8 ) ;
  
  for ( i = 0; i < numExtra; i ++ ) {
    mask[cur->numBytes-1] &= ~( 1 << i ) ;
  }


  for ( i = 0; i < cur->numBytes; i ++ ) {
    if ( (cur->buffer[i] & mask[i]) != mask[i] ) {
      // There is some difference 
      return 0;
    }
  }
  return 1;
    
}


int Bitfield_NoneSet( Bitfield * cur ) {
  int i;
  char mask[ cur->numBytes ];
  memset( mask, 0xff, cur->numBytes );

  int numExtra = (cur->numBits % 8 == 0 ? 0 : 
		  8 - cur->numBits % 8 ) ;
  
  for ( i = 0; i < numExtra; i ++ ) {
    mask[cur->numBytes-1] &= ~( 1 << i ) ;
  }

  char zeroMask[ cur->numBytes ];
  memset( zeroMask, 0x0, cur->numBytes );

  for ( i = 0; i < cur->numBytes; i ++ ) {
    if ( (cur->buffer[i] & mask[i]) != 0 ) {
      // There is some difference 
      return 0;
    }
  }
  return 1;
}


Bitfield * Bitfield_Init( int numBits ) {

  Bitfield * toRet = Malloc( sizeof( Bitfield ) );

  int numBytes = (numBits + 7) / 8;
  char * buf = Malloc( numBytes );

  memset( buf, 0x0, numBytes );

  toRet-> buffer = buf;
  toRet-> numBytes = numBytes;
  toRet-> numBits  = numBits ;

  return toRet;

}

void Bitfield_Destroy( Bitfield * cur ) {

  free( cur->buffer );
  free( cur );
  return;
   
}

int Bitfield_FromExisting( Bitfield * cur, char * other, int numBytes ) {

  int i;

  if ( cur-> numBytes != numBytes ) {
    return -1;
  }

  // Check that all of the bits that should be empty are empty
  for ( i = cur->numBits; i < cur->numBytes * 8; i ++ ) {
    if ( other[ cur->numBytes - 1 ] & (0x1 << (7-( i % 8 )) ) ) {
      return -1;
    }
  }


  memcpy( cur->buffer, other, numBytes );
  return 0;

}

int Bitfield_Get( Bitfield * cur, int bitNum, int * val ) {

  if ( cur->numBits <= bitNum ) {
    return -1;
  }

  int byteNum = bitNum / 8;
  *val = cur->buffer[ byteNum ] & (0x1 << (7-( bitNum % 8 )) );
  return 0 ;


}


int Bitfield_Set( Bitfield * cur, int bitNum ) {

  if ( cur->numBits <= bitNum ) {
    return -1;
  }

  int byteNum = bitNum / 8;
  cur->buffer[ byteNum ] |= 0x1 << (7-( bitNum % 8 )) ;
  return 0;

}


int Bitfield_Clear( Bitfield * cur, int bitNum ) {

  if ( cur->numBits <= bitNum ) {
    return -1;
  }

  int byteNum = bitNum / 8;
  cur->buffer[ byteNum ] &= ~(0x1 << (7-( bitNum % 8 ))) ;
  return 0;

}
