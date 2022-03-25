src=$(wildcard ./source/*.c)
inc_path=./header/

CC=gcc
target=server
CFLAGS= -lpthread -I 



$(target):$(src)
		$(CC) $^ $(CFLAGS) $(inc_path)  -o $(target) 