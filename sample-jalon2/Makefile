CC = gcc
CFLAGS = -Wall -Wextra

all: server client

server: server.c common.h msg_struct.h
	$(CC) $(CFLAGS) server.c -o server

client: client.c common.h msg_struct.h
	$(CC) $(CFLAGS) client.c -o client

clean:
	rm -f server client

