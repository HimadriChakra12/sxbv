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
               -fvisibility=hidden -fcolor-diagnostics \
               -I. -Isrc $(POPINC) $(CODECDEPS) \
               $(shell pkg-config --cflags xft) \
               -MMD -MP

SXBV_CXXFLAGS = -std=c++23 \
                -Wall -Wextra -O2 -march=native -flto=thin \
                -fvisibility=hidden -fcolor-diagnostics \
                -I. -Isrc $(POPINC) $(CODECDEPS) \
                -MMD -MP

POP_CFLAGS   = -std=c99  -O2 -march=native -w -fcolor-diagnostics $(POPINC) $(CODECDEPS) -MMD -MP
POP_CXXFLAGS = -std=c++23 -O2 -march=native -w -fcolor-diagnostics $(POPINC) $(CODECDEPS) -MMD -MP

LDFLAGS = -flto=thin

LIBS  = $(POPLIB) \
        $(shell pkg-config --libs freetype2 fontconfig libopenjp2 xft) \
        -lX11 -llcms2 -ljpeg -lz -lstdc++ -lm -lpthread
