#ifndef _BM_BT_PERCENT_ENCODE
#define _BM_BT_PERCENT_ENCODE

/*
  percentEncode.h - Contains function declarations for percent
  encoding and percent decoding messages. When a message is percent
  encoded, all non-letters and non- digits are represented as %XX, where
  XX is a hex representation of the value. It is used for communicating
  with a tracker. 
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

/*
  percentEncode - encode a data buffer with potentially
  unprintable characters using percent encoding.

  Parameters:
  => original - buffer containing the data to encode
  => len - length of data in buffer

  Returns: A pointer to a dynamically allocated buffer
  containing the percent encoded data.

  Example:
  Hello World!  => Hello%20World%21
*/
void * percentEncode ( void * original, int len ) ;


/*
  percentDecode - decode a data buffer of percent
  encoded data back into the orignal data.

  Parameters:
  => encoded - buffer containing the data to decode
  => len - length of data in buffer

  Returns: A pointer to a dynamically allocated buffer
  containing the decoded data.

  Example:
  Hello%20World%21 => Hello World!  
*/
void * percentDecode ( void * encoded, int len ) ;

#endif
