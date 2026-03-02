CC      = cc
CFLAGS  = -std=gnu99 -Wall -Wextra -O2 -I./mupdf $(shell pkg-config --cflags xft fontconfig)
LDFLAGS = -lX11 -lmupdf -lm $(shell pkg-config --libs xft fontconfig)

PREFIX  = /usr/local
BINDIR  = $(PREFIX)/bin

SRC = main.c image.c window.c search.c
OBJ = $(SRC:.c=.o)

all: sxbv

sxbv: $(OBJ)
	$(CC) $(OBJ) -o $@ $(LDFLAGS)

$(OBJ): sxbv.h config.h

install: sxbv
	install -Dm755 sxbv $(DESTDIR)$(BINDIR)/sxbv

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/sxbv

clean:
	rm -f sxbv $(OBJ)

.PHONY: all clean install uninstall
