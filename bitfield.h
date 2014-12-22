#ifndef BITFIELD_BM_H
#define BITFIELD_BM_H

typedef struct {

  char * buffer;
  int numBits;
  int numBytes;

} Bitfield ;


Bitfield * Bitfield_Init( int numBits ) ;

void Bitfield_Destroy( Bitfield * cur );

int Bitfield_FromExisting( Bitfield * cur, char * other, int numBytes ) ;

int Bitfield_Get( Bitfield * cur, int bitNum, int * val ) ;

int Bitfield_Set( Bitfield * cur, int bitNum ) ;

int Bitfield_Clear( Bitfield * cur, int bitNum ) ;

#endif
