CC      ?= cc
CFLAGS  ?= -O2 -Wall -Wextra -std=c11
CFLAGS  += -D_XOPEN_SOURCE_EXTENDED $(shell pkg-config --cflags ncursesw 2>/dev/null)
LDLIBS  := $(shell pkg-config --libs ncursesw 2>/dev/null || echo -lncursesw) -lcrypt

SRC := $(wildcard src/*.c)
OBJ := $(SRC:.c=.o)

lyw: $(OBJ)
	$(CC) -o $@ $(OBJ) $(LDLIBS)

$(OBJ): src/lyw.h

clean:
	rm -f lyw src/*.o

.PHONY: clean
