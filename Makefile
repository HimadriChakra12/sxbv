PREFIX   ?= /usr/local
BINDIR   ?= $(PREFIX)/bin

CC   = clang
CXX  = clang++
AR   = ar

POPPLER  = include/poppler-src
POPINC   = -I$(POPPLER) -I$(POPPLER)/poppler -I$(POPPLER)/goo \
           -I$(POPPLER)/fofi -I$(POPPLER)/splash

POPCXX   = $(wildcard $(POPPLER)/poppler/*.cc) \
           $(wildcard $(POPPLER)/goo/*.cc)     \
           $(wildcard $(POPPLER)/fofi/*.cc)    \
           $(wildcard $(POPPLER)/splash/*.cc)

POPC     = $(wildcard $(POPPLER)/poppler/*.pregenerated.c)

POPOBJ   = $(POPCXX:%.cc=%.o) $(POPC:%.pregenerated.c=%.pregenerated.o)
POPDEP   = $(POPCXX:%.cc=%.d) $(POPC:%.pregenerated.c=%.pregenerated.d)
POPLIB   = include/libpoppler-core.a

CODECDEPS = $(shell pkg-config --cflags freetype2 fontconfig libopenjp2)

SRCS_C   = src/image.c src/search.c src/thumb.c src/window.c main.c
SRCS_CXX = src/pdfshim.cc

OBJS     = $(SRCS_C:%.c=%.o) $(SRCS_CXX:%.cc=%.o)
DEPS     = $(SRCS_C:%.c=%.d) $(SRCS_CXX:%.cc=%.d)

SXBV_CFLAGS  = -std=c99 -D_POSIX_C_SOURCE=200809L \
               -Wall -Wextra -O2 -march=native -flto=thin \
               -fvisibility=hidden \
               -I. -Isrc $(POPINC) $(CODECDEPS) \
               $(shell pkg-config --cflags xft) \
               -MMD -MP

SXBV_CXXFLAGS = -std=c++23 \
                -Wall -Wextra -O2 -march=native -flto=thin \
                -fvisibility=hidden \
                -I. -Isrc $(POPINC) $(CODECDEPS) \
                -MMD -MP

POP_CFLAGS   = -std=c99  -O2 -march=native -w $(POPINC) $(CODECDEPS) -MMD -MP
POP_CXXFLAGS = -std=c++23 -O2 -march=native -w $(POPINC) $(CODECDEPS) -MMD -MP

LDFLAGS = -flto=thin

LIBS  = $(POPLIB) \
        $(shell pkg-config --libs freetype2 fontconfig libopenjp2 xft) \
        -lX11 -llcms2 -ljpeg -lz -lstdc++ -lm -lpthread

.PHONY: all clean install uninstall

all: sxbv

$(POPLIB): $(POPOBJ)
	$(AR) rcs $@ $^

$(POPPLER)/%.o: $(POPPLER)/%.cc
	$(CXX) $(POP_CXXFLAGS) -c $< -o $@

$(POPPLER)/poppler/%.pregenerated.o: $(POPPLER)/poppler/%.pregenerated.c
	$(CC) $(POP_CFLAGS) -c $< -o $@

src/pdfshim.o: src/pdfshim.cc src/pdfshim.h
	$(CXX) $(SXBV_CXXFLAGS) -c $< -o $@

src/%.o: src/%.c src/sxbv.h src/pdfshim.h config.h
	$(CC) $(SXBV_CFLAGS) -c $< -o $@

main.o: main.c src/sxbv.h src/pdfshim.h config.h
	$(CC) $(SXBV_CFLAGS) -c $< -o $@

sxbv: $(OBJS) $(POPLIB)
	$(CXX) $(LDFLAGS) $(OBJS) $(LIBS) -o $@

-include $(DEPS) $(POPDEP)

clean:
	rm -f sxbv main.o src/*.o src/*.d
	rm -f $(POPOBJ) $(POPDEP) $(POPLIB)

install: sxbv
	install -Dm755 sxbv $(DESTDIR)$(BINDIR)/sxbv
	install -Dm644 sxbv.desktop $(DESTDIR)$(PREFIX)/share/applications/sxbv.desktop

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/sxbv
	rm -f $(DESTDIR)$(PREFIX)/share/applications/sxbv.desktop
