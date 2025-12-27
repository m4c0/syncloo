.PHONY: all

all: syncloo
	./syncloo

syncloo: syncloo.c
	$(CC) -Wall -o syncloo syncloo.c

