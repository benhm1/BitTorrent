#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

char * percentEncode( char * original ) {

  // Calculate the length of the percent encoded string
  int newLen = 0;
  char * ptr = original;

  while ( *ptr != '\0' ) {

    if ( isalnum(*ptr) || *ptr == '-' || *ptr == '_' || 
	 *ptr == '.' || *ptr == '~' ) {
      newLen ++;
    }
    else {
      newLen += 3;
    }
    ptr ++;
  }

  char * percentEncoded = malloc( (newLen + 1) * sizeof( char ) );
  if ( ! percentEncoded ) {
    perror("malloc");
  }

  *percentEncoded = '\0';
  ptr = original;
  char * newStringPtr = percentEncoded;

  while ( *ptr != '\0' ) {

    unsigned char ch = *ptr;

    if ( isalnum(ch) || ch == '-' || ch == '_' || 
	 ch == '.' || ch == '~' ) {
      snprintf( newStringPtr, 1+1, "%c", ch );
      newStringPtr ++;
    }
    else {
      snprintf( newStringPtr, 3+1, "%%%02x", ch );
      newStringPtr += 3;
    }
    ptr ++;
  }

  return percentEncoded;


}

char * percentDecode ( char * encoded ) {

  // Calculate the length of the percent encoded string
  int newLen = 0;
  char * ptr = encoded;

  while ( *ptr != '\0' ) {

    if ( *ptr == '%' ) {
      ptr += 2;
    }

    ptr += 1;
    newLen += 1;
  }

  char * percentDecoded = malloc( (newLen + 1) * sizeof( char ) );
  if ( ! percentDecoded ) {
    perror("malloc");
  }

  *percentDecoded = '\0';
  ptr = encoded;
  char * newStringPtr = percentDecoded;
  char encodedByte[3];
  encodedByte[2] = '\0';

  printf("Encoded: %s\n", encoded);

  while ( *ptr != '\0' ) {

    unsigned char ch = *ptr;

    if ( ch == '%' ) {
      encodedByte[0] = *(ptr+1);
      encodedByte[1] = *(ptr+2);

      int byteAsInt;

      sscanf( encodedByte, "%x", &byteAsInt ) ;

      unsigned char byteAsChar = (unsigned char ) byteAsInt;
      
      *newStringPtr = byteAsChar;

      ptr += 3;
      newStringPtr += 1;
    }
    else {
      *newStringPtr = ch;
      ptr += 1;
      newStringPtr += 1;
    }
  }
  
  return percentDecoded;
  
  
 

}

int main() {

  char * first = malloc( 1000 * sizeof(char) );
  fgets( first, 999, stdin );
  printf("    You Entered: %s\n", first);
  char * encoded = percentEncode( first );
  printf("Percent Encoded: %s\n", encoded );
  char * decoded = percentDecode( encoded );
  printf("Percent Decoded: %s\n", decoded );

  free( first );
  free( encoded ) ;
  free( decoded ) ;

  return;

}
