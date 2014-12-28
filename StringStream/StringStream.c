#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>

#include "StringStream.h"



StringStream * SS_Init() {

  StringStream * toRet = Malloc( sizeof( StringStream ) );
  toRet->size = 0;
  toRet->capacity = 8 ;
  toRet->data = Malloc( 8 );
  toRet->head = toRet->data;
  toRet->tail = toRet->data;

  return toRet;
} 

void SS_Destroy( StringStream * s ) {

  free( s->data );
  free( s );
  return;

}


/* Returns the lowest power of 2 that is greater than
   or equal to the passed in parameter */
static int roundToTwo( int x ) {

  if ( x < 0 ) {
    return 1;
  }

  x |= x >> 1 ;  
  x |= x >> 2 ;
  x |= x >> 4 ;
  x |= x >> 8 ;
  x |= x >> 16 ;

  x ++;

  return x;

}

/*
  SS_EnsureCapacity - Ensure that the stream can fit 
  at least newLen additional bytes of data. 

  First checks if the data could fit between the tail pointer and
  the end of the array. If not, checks if the data could fit between
  the tail pointer and the end of the array after moving all data to
  the front of the array. Finally, allocates a larger buffer if needed.

  
 */
static void SS_EnsureCapacity( StringStream * s, int newLen ) {
  // Can we fit this at the end of our current array ?

  int bytesRemaining = ( s->data + s->capacity ) - s->tail ;
  if ( newLen < bytesRemaining ) {
    return;
  }

  // Could we fit this at the end of our current array if
  // we moved stuff down to the beginning?
  int bytesWithShuffle = bytesRemaining + ( s->head - s->data );
  if ( newLen < bytesWithShuffle ) {
    memmove( s->data, s->head, s->size );
    s->head = s->data;
    s->tail = s->data + s->size;
    return;
  }

  // Shuffle everything down, and then reallocate a large enough
  // chunk
  int newSize = roundToTwo( s->size + newLen );
  memmove( s->data, s->head, s->size );
  s->data = realloc( s->data, newSize );
  s->head = s->data;
  s->tail = s->data + s->size;
  if ( ! s->data ) {
    perror( "realloc" );
    exit(1);
  }
  s->capacity = newSize;
  return;



}

void SS_Push( StringStream * s, void * new, int len ) {
  SS_EnsureCapacity( s, len );
  memmove( s->tail, new, len );
  s->size += len;
  s->tail += len;
  return; 
}

void SS_Pop( StringStream * s, int numBytes ) {

  if ( s->size < numBytes ) {
    printf("Error: Tried to pop more bytes than exist!");
    exit(1);
  }

  s->head += numBytes;
  s->size -= numBytes;


}

void SS_Print( StringStream * s ) {

  int i;

  for ( i = 0; i < s->capacity; i ++ ) {
    if ( isprint( s->data[i] ) ) {
      printf("%c", s->data[i]);
    } else {
      printf("*");
    }
  }
  printf("\n");
  
  assert( s->head >= s->data && s->head < s->data + s->capacity );
  assert( s->tail >= s->data && s->tail < s->data + s->capacity );
  assert( s->head <= s->tail );

  for ( i = 0; i < s->capacity; i ++ ) {
    if ( &s->data[i] == s->head ) {
      printf("H");
    } else {
      printf(" ");
    }
  }
  printf("\n");

  for ( i = 0; i < s->capacity; i ++ ) {
    if ( &s->data[i] == s->tail ) {
      printf("T");
    } else {
      printf(" ");
    }
  }
  printf("\n");

  
  


}
