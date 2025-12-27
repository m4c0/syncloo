.PHONY: all

all: syncloo

syncloo: syncloo.c
	$(CC) -o syncloo syncloo.c

