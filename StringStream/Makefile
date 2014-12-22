TARGET = TestStringStream

CC = gcc

#CFLAGS = -m32 -g -Wall
CFLAGS =  -g -Wall

all: $(TARGET)

$(TARGET):  $(TARGET).c StringStream.o 
	$(CC) $(CFLAGS) -o $(TARGET)  $(TARGET).c StringStream.o

StringStream.o: StringStream.c StringStream.h
	$(CC) $(CFLAGS) -c StringStream.c

clean:
	$(RM) $(TARGET) *.o
