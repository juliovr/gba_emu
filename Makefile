CC=gcc

all: build

build:
	mkdir -p bin/
	$(CC) -g -ggdb -D_LINUX -D_DEBUG src/main.c -o bin/main

run: build
	./bin/main
