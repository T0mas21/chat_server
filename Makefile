CC = gcc
CFLAGS = -Wall -Wextra -std=gnu99 -pthread

SRCS = ipk24chat-server.c server-run.c tcp-auth.c tcp-open.c udp-decode.c udp-clients.c
OBJS = $(SRCS:.c=.o)
TARGET = ipk24chat-server

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o $(TARGET)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

