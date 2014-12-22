#ifndef STRING_STREAM_BM_H
#define STRING_STREAM_BM_H

typedef struct {

  char * data;
  char * head;
  char * tail;
  int size;
  int capacity;

} StringStream ;

void * Malloc( size_t size );
StringStream * SS_Init() ;
void SS_Destroy( StringStream * s ) ;
void SS_Push( StringStream * s, void * new, int len ) ;
void SS_Pop( StringStream * s, int numBytes ) ;
void SS_Print( StringStream * s ) ;
#endif 
