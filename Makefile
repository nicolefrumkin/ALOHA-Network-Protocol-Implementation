# Makefile for compiling channel.c, server.c, and test programs

CC = gcc
CFLAGS = -Wall -Wextra
LDFLAGS = -lws2_32

all: channel server test_channel test_server

# Main programs
channel: channel.c
	$(CC) $(CFLAGS) -o channel.exe channel.c $(LDFLAGS)

server: server.c
	$(CC) $(CFLAGS) -o server.exe server.c $(LDFLAGS)

# Test programs
test_channel: test_channel.c
	$(CC) $(CFLAGS) -o test_channel.exe test_channel.c $(LDFLAGS)

test_server: test_server.c
	$(CC) $(CFLAGS) -o test_server.exe test_server.c $(LDFLAGS)

# Create test data file for testing
test_data:
	dd if=/dev/urandom of=test_data.bin bs=1k count=100

# Clean up compiled files
clean:
	del *.exe
	del *.o

# Run all tests
test: test_channel test_server
	echo "Running channel tests..."
	start cmd /k test_channel.exe
	timeout 2
	echo "Running server tests..."
	start cmd /k test_server.exe

.PHONY: all clean test test_data
