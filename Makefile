CC      := gcc
CFLAGS  := -Wall -Wextra -O2
LDFLAGS := -lm
TARGET  := server
SRC     := src/server.c

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -f $(TARGET)
