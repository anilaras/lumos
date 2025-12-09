CC = gcc
CFLAGS = -O2 -Wall
LDFLAGS = -lpthread
TARGET = lumos
SRC = main.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC) $(LDFLAGS)

clean:
	rm -f $(TARGET)

.PHONY: all clean
