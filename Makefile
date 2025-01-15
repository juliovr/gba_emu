CC=gcc

all:
	mkdir -p bin/
	$(CC) -g -D_LINUX -D_DEBUG src/main.c -o bin/main
