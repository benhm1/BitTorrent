TARGET = testBitfield

CC = gcc

#CFLAGS = -m32 -g -Wall
CFLAGS =  -g -Wall
LIBS = -lrt

all: $(TARGET)

$(TARGET):  $(TARGET).c bitfield.o 
	$(CC) $(CFLAGS) -o $(TARGET)  $(TARGET).c bitfield.o $(LIBS)

bitfield.o: bitfield.c bitfield.h
	$(CC) $(CFLAGS) -c bitfield.c

clean:
	$(RM) $(TARGET) *.o *~
