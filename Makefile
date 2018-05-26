#
# This is an example Makefile for a read-text-line program. It is
# based on select() system call and calling read() in non-blocking
# mode. Reads a line of text from stdin where CRLF and LF conventions
# for end-of-line are both supported.
# Typing 'make' or 'make rd-multi-strm' will create the executable file.
#

#  -g    adds debugging information to the executable file
#  -Wall turns on most, but not all, compiler warnings
#
# for C++ define  CC = g++
CC = g++
CFLAGS  = -Wall -fPIC -std=gnu++11
LINKER_FLAGS = -Wl,-rpath,'$$ORIGIN/'

# typing 'make' will invoke the first target entry in the file 
# (in this case the all target entry)
all: rd-multi-strm

rd-multi-strm:  main.o signal-handling.o util.o uncompress-stream.o child-process-tracking.o read-buf-ctx.o read-multi-strm.o
	$(CC) $(LINKER_FLAGS) -o rd-multi-strm main.o signal-handling.o util.o uncompress-stream.o child-process-tracking.o \
	read-buf-ctx.o read-multi-strm.o -lrt -lpthread

main.o:  main.cpp signal-handling.h util.h uncompress-stream.h read-buf-ctx.h
	$(CC) $(CFLAGS) -c main.cpp

signal-handling.o:  signal-handling.cpp signal-handling.h
	$(CC) $(CFLAGS) -c signal-handling.cpp

util.o:  util.cpp util.h
	$(CC) $(CFLAGS) -c util.cpp

uncompress-stream.o:  uncompress-stream.cpp uncompress-stream.h util.h child-process-tracking.h
	$(CC) $(CFLAGS) -c uncompress-stream.cpp

child-process-tracking.o:  child-process-tracking.cpp child-process-tracking.h signal-handling.h
	$(CC) $(CFLAGS) -c child-process-tracking.cpp

read-buf-ctx.o:  read-buf-ctx.cpp read-buf-ctx.h signal-handling.h
	$(CC) $(CFLAGS) -c read-buf-ctx.cpp

read-multi-strm.o:  read-multi-strm.cpp read-multi-strm.h
	$(CC) $(CFLAGS) -c read-multi-strm.cpp

# To start over from scratch, type 'make clean'.  This
# removes the executable file, as well as old .o object
# files and *~ backup files:
#
clean: 
	$(RM) rd-multi-strm *.o *~