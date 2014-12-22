
#include <stdlib.h>
#include <string.h>

#include "bitfield.h"

extern void* Malloc( size_t );

Bitfield * Bitfield_Init( int numBits ) {

  Bitfield * toRet = Malloc( sizeof( Bitfield ) );

  int numBytes = (numBits + 7) / 8;
  char * buf = Malloc( numBytes );

  int i;
  for ( i = 0; i < numBytes; i ++ ) {
    buf[i] = '\0';
  }

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

  if ( cur-> numBytes != numBytes ) {
    return -1;
  }
  memcpy( cur->buffer, other, numBytes );
  return 0;

}

int Bitfield_Get( Bitfield * cur, int bitNum, int * val ) {

  if ( cur->numBits <= bitNum ) {
    return -1;
  }

  int byteNum = bitNum / 8;
  *val = cur->buffer[ byteNum ] && (0x1 << (7-( bitNum % 8 )) );
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
