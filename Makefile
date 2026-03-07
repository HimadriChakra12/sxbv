CC      = cc
CFLAGS  = -std=gnu99 -Wall -Wextra -O2 -I./mupdf $(shell pkg-config --cflags xft fontconfig)
LDFLAGS = -lX11 -lmupdf -lm $(shell pkg-config --libs xft fontconfig)

PREFIX  = /usr/local
BINDIR  = $(PREFIX)/bin
APPDIR  = $(PREFIX)/share/applications

SRC = main.c image.c window.c search.c thumb.c
OBJ = $(SRC:.c=.o)

all: sxbv

sxbv: $(OBJ)
	$(CC) $(OBJ) -o $@ $(LDFLAGS)

$(OBJ): sxbv.h config.h

install: sxbv
	install -Dm755 sxbv $(DESTDIR)$(BINDIR)/sxbv
	install -Dm644 sxbv.desktop $(DESTDIR)$(APPDIR)/sxbv.desktop
	@if command -v update-desktop-database >/dev/null 2>&1; then \
		update-desktop-database $(DESTDIR)$(APPDIR); \
	fi

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/sxbv
	rm -f $(DESTDIR)$(APPDIR)/sxbv.desktop
	@if command -v update-desktop-database >/dev/null 2>&1; then \
		update-desktop-database $(DESTDIR)$(APPDIR); \
	fi

clean:
	rm -f sxbv $(OBJ)

.PHONY: all clean install uninstall
