CC = gcc
CFLAGS= -g -lcrypto
LDFLAGS=-L/usr/local/opt/openssl/lib
CPPFLAGS=-I/usr/local/opt/openssl/include
SOURCES = dfc.c dfs.c
OBJECTS = $(SOURCES:.c=.o)

all: dfc dfs

re: clean all

dfc: dfc.o
	$(CC) $(CFLAGS) dfc.o -o dfc $(LDFLAGS) $(CPPFLAGS)

dfs: dfs.o
	$(CC) $(CFLAGS) dfs.o -o dfs $(LDFLAGS) $(CPPFLAGS)

%.o: %.c
	$(CC) -c -o $@ $< $(CFLAGS) $(CPPFLAGS)

clean_files:
	- @rm -f logs/*
	- @rm -rf DFS1/*
	- @rm -rf DFS2/*
	- @rm -rf DFS3/*
	- @rm -rf DFS4/*
	- @rm -rf client/received/*

clean: clean_files
	- @rm -f dfc dfs $(OBJECTS)