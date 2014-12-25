#include "bitfield.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <time.h>


char * genMask( int numBits ) {


  int i;
  int numBytes =  numBits/8 + ( numBits % 8 ? 1 : 0 ) ;
  char * mask = malloc( numBytes );
  memset( mask, 0xff, numBytes );

  int numExtra = (numBits % 8 == 0 ? 0 :
                  8 - numBits % 8 ) ;

  for ( i = 0; i < numExtra; i ++ ) {
    mask[numBytes-1] &= ~( 1 << i ) ;
  }

  return mask;


}

int main() {

  int i, j;
  Bitfield * bitfield;
  printf("Testing Bitfield_NoneSet and Bitfield_AllSet\n");
  for ( i = 0; i < 24; i ++ ) {

    bitfield = Bitfield_Init(i);
    assert( Bitfield_NoneSet( bitfield ) );

    for ( j = 0; j < i; j ++ ) {
      Bitfield_Set( bitfield, j );
    }
    assert( Bitfield_AllSet( bitfield ) );

    Bitfield_Destroy( bitfield );

  }

  printf("Testing Bitfield_FromExisting\n");
  for ( i = 9; i < 16; i ++ ) {
    for ( j = 9; j < 16; j ++ ) {
      bitfield = Bitfield_Init(i);
      char * mask = genMask( j );
      assert( i < j ? 
	      Bitfield_FromExisting( bitfield, mask, 2 ) == -1 :
	      Bitfield_FromExisting( bitfield, mask, 2 ) == 0 );

      free( mask );
      Bitfield_Destroy( bitfield );
      
    }
  }


  printf("Testing Bitfield_Get, Bitfield_Set, and Bitfield_Clear\n");
  bitfield = Bitfield_Init( 2048 );
  srand(time(NULL));
  int r ;
  int val;
  for ( i = 0; i < 10000; i ++ ) {

    r = rand() % 2048;
    assert( !Bitfield_Get( bitfield, r, &val ) );
    assert( ! val );
    assert( !Bitfield_Set( bitfield, r ) );
    assert( !Bitfield_Get( bitfield, r, &val ) );
    assert(  val );
    assert( !Bitfield_Clear( bitfield, r ) );
    assert( !Bitfield_Get( bitfield, r, &val ) );
    assert( ! val );
    assert( Bitfield_NoneSet( bitfield ) );

  }


  Bitfield_Destroy( bitfield );

  printf("PASS\n\n");


  return 0;

}
