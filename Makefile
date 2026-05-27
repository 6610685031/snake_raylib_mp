CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -O2
LDFLAGS = -lraylib -lGL -lm -lpthread -ldl -lrt -lX11
SERVER_LDFLAGS = -lpthread -lrt

SRC = snake.c snake_client.c snake_server.c
OBJ = snake.o snake_client.o snake_server.o
BINS = snake snake_client snake_server

all: $(BINS)

snake: snake.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

snake_client: snake_client.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

snake_server: snake_server.o
	$(CC) $(CFLAGS) -o $@ $^ $(SERVER_LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJ) $(BINS)

.PHONY: all clean
