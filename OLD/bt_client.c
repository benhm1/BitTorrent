#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bencode.h"
#include <assert.h>
//#include <openssl/sha.h> //hashing pieces


struct chunkInfo {

  int size;
  int have;
  char * data;
  char hash[20];

};

struct torrentInfo {

  char * announceURL;
  int totalSize;
  int chunkSize;
  
  char * name;
  int date;
  char * comment; 

  struct chunkInfo * chunks;

  char * infoHash;

};

void * Malloc( size_t size ) {

  void * toRet = malloc( size );
  if ( ! toRet ) {
    perror( "malloc" );
  }
  return toRet;

}

char * computeSHA1( char * data, int size ) {

  int i;
  char * toRet = Malloc( 20 ); // SHA_DIGEST_LENGTH
  SHA1( data, size, toRet );
  //print the hash
  for(i=0;i< 20;i++){
    printf("%02x",toRet[i]);
  }
  printf("\n");
  return toRet;

}


struct torrentInfo* connectToTracker( be_node * data ) {

  int i, j;
  int chunkHashesLen;
  char * chunkHashes;


  struct torrentInfo * toRet = Malloc( sizeof( struct torrentInfo ) );

  for ( i = 0; data->val.d[i].val != NULL; i ++ ) {

    // Keys can include: announce (string) and info (dict), among others
    if ( 0 == strcmp( data->val.d[i].key, "announce" ) ) {
      toRet->announceURL = strdup( data->val.d[i].val->val.s );
      printf("Announce URL: %s\n", toRet->announceURL);
    }
    else if ( 0 == strcmp( data->val.d[i].key, "info" ) ) {

      be_node * infoIter = data->val.d[i].val;
      for ( j = 0; infoIter->val.d[j].val != NULL; j ++ ) {

	char * key = infoIter->val.d[j].key;
	if ( 0 == strcmp( key, "length" ) ) {
	  toRet->totalSize = infoIter->val.d[j].val->val.i;
	  printf("Torrent Size: %d\n", toRet->totalSize );
	}
	else if ( 0 == strcmp( key, "piece length" ) ) {
	  toRet->chunkSize = infoIter->val.d[j].val->val.i;
	  printf("Chunk Size: %d\n", toRet->chunkSize );
	}
	else if ( 0 == strcmp( key, "name" ) ) {
	  toRet->name = strdup( infoIter->val.d[j].val->val.s );
	  printf("Torrent Name: %s\n", toRet->name );
	}
	else if ( 0 == strcmp( key, "pieces" ) ) {
	  chunkHashesLen = be_str_len( infoIter->val.d[j].val );
	  chunkHashes = Malloc( chunkHashesLen );
	  memcpy( chunkHashes, infoIter->val.d[j].val->val.s, 
		  chunkHashesLen );
	  printf( "Hashes Length: %d\n", chunkHashesLen );
	}
	else {
	  printf("Ignoring Field: %s\n", key );
	}
      }
    }
    else if ( 0 == strcmp( data->val.d[i].key, "creation date" ) ) {
      toRet->date = data->val.d[i].val->val.i;
      printf("Creation Date: %d\n", toRet->date);
    }
    else if ( 0 == strcmp( data->val.d[i].key, "comment" ) ) {
      toRet->comment = strdup( data->val.d[i].val->val.s );
      printf("Comment: %s\n", toRet->comment);
    }
    else {
      printf("Ignoring Field : %s\n", data->val.d[i].key );
    }
  }

  // Now, set up the chunks for the torrent
  int numChunks = toRet->totalSize/toRet->chunkSize + 
    ( toRet->totalSize % toRet->chunkSize ? 1 : 0 );
  toRet->chunks = Malloc( numChunks * sizeof(struct chunkInfo) );
  for ( i = 0; i < numChunks ; i ++ ) {
    memcpy( toRet->chunks[i].hash, chunkHashes + 20*i, 20 );
    toRet->chunks[i].size = ( i == numChunks - 1 ? 
			      toRet-> totalSize % toRet->chunkSize 
			      : toRet->chunkSize ) ;
    toRet->chunks[i].have = 0;
    toRet->chunks[i].data = NULL;
  }

  // Finally, read in the info dictionary so that we can compute the hash
  // for our connection to the tracker. 
  FILE * fd = fopen("infoDict.bencoding", "rb");
  fseek(fd, 0, SEEK_END);
  long fsize = ftell(fd);
  fseek(fd, 0, SEEK_SET);

  char *string = Malloc(fsize);
  long numRead = fread(string, fsize, 1, fd);
  assert( fsize == numRead );
  fclose(fd);

  toRet->infoHash = computeSHA1( string, numRead );
 
  return toRet;


}




int main(int argc, char ** argv) {

  be_node* data = load_be_node( argv[1] );
  connectToTracker( data );


  
  be_dump( data );
  be_free( data );

  return 0;

}
