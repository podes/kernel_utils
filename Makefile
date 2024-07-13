SRC := $(wildcard *.c)
OBJ := $(SRC:.c=.o)
CC := gcc
CFLAGS := -Wall -g -fno-strict-aliasing -D_GNU_SOURCE -Wno-pointer-arith -Wmissing-prototypes -Wdeprecated-declarations 
TARGET := libkutils.a

all: $(TARGET)

$(TARGET): $(OBJ)
	$(AR) rcs $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(TARGET)

.PHONY: all clean
