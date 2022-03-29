src=$(wildcard ./src/*.cpp)
inc_path=./header/

CC=g++
target=server
CFLAGS= -std=c++17 -pthread 


$(target):$(src)
	$(CC) $^ $(CFLAGS) -o $(target) 