PREFIX   ?= /usr/local
BINDIR   ?= $(PREFIX)/bin

NPROC    := $(shell nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)
MAKEFLAGS += -j$(NPROC)

CCACHE   := $(shell command -v ccache 2>/dev/null)
CC       = $(CCACHE) clang
CXX      = $(CCACHE) clang++
AR       = ar

# override with NATIVE=0 for portable/CI builds
NATIVE   ?= 1
ifeq ($(NATIVE),1)
ARCH_FLAGS = -march=native
else
ARCH_FLAGS = -march=x86-64-v2
endif

COLOR = # -fcolor-diagnostics
FLT   = # -flto=thin
LDFLAGS = $(FLT) -lpng -ltiff

POPPLER  = include/poppler-src
POPINC   = -I$(POPPLER) -I$(POPPLER)/poppler -I$(POPPLER)/goo \
           -I$(POPPLER)/fofi -I$(POPPLER)/splash
POPCXX   = $(wildcard $(POPPLER)/poppler/*.cc) \
           $(wildcard $(POPPLER)/goo/*.cc)     \
           $(wildcard $(POPPLER)/fofi/*.cc)    \
           $(wildcard $(POPPLER)/splash/*.cc)
POPC     = $(wildcard $(POPPLER)/poppler/*.pregenerated.c)
POPOBJ  := $(POPCXX:.cc=.o)  $(POPC:.c=.o)
POPDEP  := $(POPCXX:.cc=.d)  $(POPC:.c=.d)
POPLIB   = include/libpoppler-core.a

CODEC_CFLAGS := $(shell pkg-config --cflags freetype2 fontconfig libopenjp2 xft)
CODEC_LIBS   := $(shell pkg-config --libs   freetype2 fontconfig libopenjp2 xft)
# same set as CODEC_CFLAGS minus xft, computed once (no second pkg-config call)
CODECDEPS    := $(filter-out %/include/xft% -I/usr/include/X11%,$(CODEC_CFLAGS))

SRCS_C   = src/image.c src/search.c src/thumb.c src/window.c src/annotate.c main.c
SRCS_CXX = src/pdfshim.cc
OBJS     = $(SRCS_C:%.c=%.o) $(SRCS_CXX:%.cc=%.o)
DEPS     = $(SRCS_C:%.c=%.d) $(SRCS_CXX:%.cc=%.d)

SXBV_CFLAGS   := -std=c99  -D_POSIX_C_SOURCE=200809L  -Wall -Wextra  -O2  $(ARCH_FLAGS)  -fvisibility=hidden  $(COLOR)  -I. -Isrc  $(POPINC)  $(CODEC_CFLAGS)  -MMD -MP -Os -pipe -s
SXBV_CXXFLAGS := -std=c++23  -Wall -Wextra -O2 $(ARCH_FLAGS) $(FLT)  -fvisibility=hidden $(COLOR)  -I. -Isrc $(POPINC) $(CODECDEPS)  -MMD -MP
POP_CFLAGS    := -std=c99  -O2  $(ARCH_FLAGS)  $(COLOR)  $(POPINC)  $(CODEC_CFLAGS)  -MMD -MP
POP_CXXFLAGS  := -std=c++23  -O2  $(ARCH_FLAGS)  $(COLOR)  $(POPINC)  $(CODEC_CFLAGS)  -MMD -MP

LIBS := $(POPLIB)  $(CODEC_LIBS)  -lX11  -llcms2  -ljpeg  -lz  -lstdc++  -lm  -lpthread
