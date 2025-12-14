CC = gcc
CFLAGS = -O2 -Wall
LDFLAGS = -lpthread
TUI_LDFLAGS = -lncurses

TARGET = lumos
TUI_TARGET = lumos-tui
SRC = main.c
TUI_SRC = lumos-tui.c

all: $(TARGET) $(TUI_TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC) $(LDFLAGS)

$(TUI_TARGET): $(TUI_SRC)
	$(CC) $(CFLAGS) -o $(TUI_TARGET) $(TUI_SRC) $(TUI_LDFLAGS)

clean:
	rm -f $(TARGET) $(TUI_TARGET)

.PHONY: all clean
