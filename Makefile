CC=gcc
CPFLAGS=-g -Wall
LDFLAGS= -lcrypto -lcrypt -lrt


SRC= utils/algorithms.c          \
     utils/base.c                \
     utils/choke.c               \
     utils/bencode.c             \
     utils/percentEncode.c       \
     messages/tracker.c          \
     messages/incomingMessages.c \
     messages/outgoingMessages.c \
     StringStream/StringStream.c \
     bitfield/bitfield.c         \
     timer/timer.c               \
     managePeers.c               \
     startup.c                   \
     bt_client.c 

OBJ=$(SRC:.c=.o)
BIN=bt_client

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(CPFLAGS) -o $(BIN) $(OBJ) $(LDFLAGS) 


%.o:%.c %.h
	$(CC) -c $(CPFLAGS) -o $@ $<  

$(SRC):

clean:
	rm -rf $(OBJ) $(BIN) *~
