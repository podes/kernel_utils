SRC := $(wildcard *.c)
OBJ := $(SRC:.c=.o)
CC := gcc
CFLAGS := -Wall -g -O4 -fno-strict-aliasing -D_GNU_SOURCE -Wno-pointer-arith -Wmissing-prototypes -Wdeprecated-declarations 
TARGET := libkutils.a

all: $(TARGET)

$(TARGET): $(OBJ)
	$(AR) rcs $@ $^
	mkdir -p include
	cp *.h include/

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(TARGET)
	rm -rf include

install:
	cp $(TARGET) /usr/lib/
	cp -r include /usr/local/include/kutils

uninstall:
	rm -f /usr/lib/$(TARGET)
	rm -rf /usr/local/include/kutils

.PHONY: all clean
