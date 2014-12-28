CC=gcc
CPFLAGS=-g -Wall
LDFLAGS= -lcrypto -lcrypt -lrt


SRC= utils/algorithms.c managePeers.c startup.c utils/base.c utils/choke.c utils/bencode.c messages/tracker.c messages/incomingMessages.c messages/outgoingMessages.c utils/percentEncode.c bt_client.c StringStream/StringStream.c bitfield/bitfield.c timer/timer.c
OBJ=$(SRC:.c=.o)
BIN=bt_client

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(CPFLAGS) -o $(BIN) $(OBJ) $(LDFLAGS) 


%.o:%.c
	$(CC) -c $(CPFLAGS) -o $@ $<  

$(SRC):

clean:
	rm -rf $(OBJ) $(BIN) *~
