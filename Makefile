CC=gcc

all: build

build:
	mkdir -p bin/
	$(CC) -g -D_LINUX -D_DEBUG src/main.c -o bin/main

run: build
	./bin/main
