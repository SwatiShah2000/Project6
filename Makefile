# Makefile for CS 4760 Assignment #6 - Memory Management

CC = gcc
CFLAGS = -Wall -g
DEPS = oss.h

all: oss user

oss: oss.c $(DEPS)
	$(CC) $(CFLAGS) -o oss oss.c

user: user.c $(DEPS)
	$(CC) $(CFLAGS) -o user user.c

clean:
	rm -f *.o oss user *.log

.PHONY: all clean
