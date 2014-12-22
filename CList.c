#include <stdio.h>
#include <stdlib.h>



void * Malloc( size_t size ) {

  void * ret = malloc ( size );
  if ( ! ret ) {
    perror("Malloc failed");
    exit(1);
  }
  return ret;

}

struct CQueue {

  void ** array;
  int capacity;
  int size;
  int head;
  int tail;

};

void checkCapacity( struct CQueue * q );

struct CQueue * CQueueInit() {

  struct CQueue * q = Malloc( sizeof( struct CQueue ) );

  q->array = Malloc( sizeof( void * ) * 8 );
  q->capacity = 8;
  q->size = 0;
  q->head = 0;
  q->tail = 0;

  return q;

}

void CQueueDestroy(struct CQueue * q) {

  free( q->array );
  free( q );
  return ;
  
}

void push( struct CQueue * q, void * val ) {
  checkCapacity( q );
  q->array[ q->tail ] = val;
  q->tail = ( q->tail + 1 ) % (q->capacity);
  q->size += 1;
}

void * pop( struct CQueue * q ) {

  if ( isEmpty( q ) ) {
    return NULL;
  }
  void * ret = q->array[q->head];
  q->head = ( q->head + 1 ) % q->capacity;
  q->size -= 1;
  return ret;

}

int isEmpty( struct CQueue * q ) {

  return (q->size == 0);

}

void checkCapacity( struct CQueue * q ) {
  int i;
  if ( q->size < q->capacity ) {
    return;
  }

  int newCapacity = q->capacity * 2;
  void ** newArray = Malloc( sizeof( void * ) * newCapacity );
  for ( i = 0; i < q->size; i ++ ) {
    newArray[i] = q->array[ ( q->head + i ) % q->capacity ];
  }
  
  free( q->array );
  q->array = newArray;
  q->capacity = newCapacity;
  q->head = 0;
  q->tail = q->size;

  return;

}



int main() {

  struct CQueue * q = CQueueInit();

  int i;
  for ( i = 100 ; i < 205; i ++ ) {
    int * val = Malloc( sizeof(int));
    *val = i;
    push( q, val );
    val = pop(q);
    printf( "%d\n", *val);
    free(val);

  }
  while ( ! isEmpty(q) ) {
  }




  CQueueDestroy( q );

}
