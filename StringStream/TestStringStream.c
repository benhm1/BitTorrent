#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>

#include "StringStream.h"


int main() {

  StringStream * s = SS_Init() ;

  SS_Print( s );

  char * buf = Malloc(32);

  strcpy( buf, "ABCD " );
  SS_Push( s, buf, strlen(buf) );
  SS_Print( s );

  strcpy( buf, "The quick brown foxes jumped" );
  SS_Push( s, buf, strlen(buf) );
  SS_Print( s );
  
  SS_Pop( s, 15 );
  SS_Print( s );

  strcpy( buf, "over the lazy dogs of yes the");
  SS_Push( s, buf, strlen(buf) );
  SS_Print( s );

  SS_Pop( s, 30 );
  SS_Print( s );

  strcpy( buf, "One two three!");
  SS_Push( s, buf, strlen(buf) );
  SS_Print( s );
  
  
  free( buf );
  SS_Destroy( s );

  return 0;
}
