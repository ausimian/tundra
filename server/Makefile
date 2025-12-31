CC = gcc
CFLAGS = -Wall -Wextra -Werror -Wfatal-errors -O2 -std=c11 -pedantic
TARGET = tundra_server
TEST_CLIENT = test_client
SRCDIR = src
SRCS = $(SRCDIR)/main.c $(SRCDIR)/protocol.c

# Platform-specific sources
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Linux)
    SRCS += $(SRCDIR)/tun_linux.c
endif
ifeq ($(UNAME_S),Darwin)
    SRCS += $(SRCDIR)/tun_darwin.c
endif

OBJS = $(SRCS:.c=.o)

.PHONY: all clean install test

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

$(TEST_CLIENT): test_client.c $(SRCDIR)/protocol.c
	$(CC) $(CFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

test: $(TEST_CLIENT)
	@echo "Build the test client. Run server with: sudo ./$(TARGET)"
	@echo "Then in another terminal: ./$(TEST_CLIENT)"

clean:
	rm -f $(TARGET) $(TEST_CLIENT) $(OBJS)

install: $(TARGET)
	install -m 755 $(TARGET) /usr/local/bin/
