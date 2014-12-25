TARGET = testTimer

CC = gcc

#CFLAGS = -m32 -g -Wall
CFLAGS =  -g -Wall
LIBS = -lrt

all: $(TARGET)

$(TARGET):  $(TARGET).c timer.o 
	$(CC) $(CFLAGS) -o $(TARGET)  $(TARGET).c timer.o $(LIBS)

timer.o: timer.c timer.h
	$(CC) $(CFLAGS) -c timer.c

clean:
	$(RM) $(TARGET) *.o *~
