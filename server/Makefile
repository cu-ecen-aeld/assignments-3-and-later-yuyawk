ifeq ($(CROSS_COMPILE), aarch64-none-linux-gnu-)
CC = aarch64-none-linux-gnu-gcc
else
CC = gcc
endif


CFLAGS = -Wall -Wextra -Werror -pedantic-errors -std=c11 -g
TARGET = aesdsocket
SRC = aesdsocket.c

.PHONY: all default clean

all: default

default: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC)

clean:
	rm -f $(TARGET)
