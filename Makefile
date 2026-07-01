PREFIX   ?= /usr/local
BINDIR   ?= $(PREFIX)/bin

CC   = clang
CXX  = clang++
AR   = ar

BOLD  = \033[1m
DIM   = \033[2m
CYAN  = \033[1;36m
GREEN = \033[1;32m
ORANGE = \033[38;5;208m
BLUE    = \033[1;34m
RESET = \033[0m

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

define compile_c
	@t=$$(date +%s%N); \
	printf "$(GREEN)$(CC)$(RESET)  $(BOLD)$<$(RESET)\t $@\t"; \
	$(CC) $(1) -c $< -o $@ ; \
	rc=$$?; \
	ms=$$(( ($$(date +%s%N) - t) / 1000000 )); \
	[ $$rc -eq 0 ] && printf "$(CYAN)%dms$(RESET)\n" $$ms || exit $$rc
endef

define compile_cxx
	@t=$$(date +%s%N); \
	printf "$(BLUE)$(CXX)$(RESET)  $(BOLD)$<$(RESET)\t $@\t"; \
	$(CXX) $(1) -c $< -o $@ ; \
	rc=$$?; \
	ms=$$(( ($$(date +%s%N) - t) / 1000000 )); \
	[ $$rc -eq 0 ] && printf "$(CYAN)%dms$(RESET)\n" $$ms || exit $$rc
endef

define link_bin
	@t=$$(date +%s%N); \
	printf "$(GREEN)link$(RESET)   $(BOLD)$@$(RESET)\n"; \
	$(CXX) $(LDFLAGS) $(OBJS) $(LIBS) -o $@ ; \
	rc=$$?; \
	ms=$$(( ($$(date +%s%N) - t) / 1000000 )); \
	[ $$rc -eq 0 ] && printf "$(CYAN)%dms$(RESET)\n" $$ms || exit $$rc
endef

.PHONY: all clean install uninstall

all: sxbv

$(POPLIB): $(POPOBJ)
	@printf "$(GREEN)ar$(RESET)     $(BOLD)$@$(RESET)\n"
	@$(AR) rcs $@ $^

$(POPPLER)/%.o: $(POPPLER)/%.cc
	$(call compile_cxx,$(POP_CXXFLAGS))

$(POPPLER)/poppler/%.pregenerated.o: $(POPPLER)/poppler/%.pregenerated.c
	$(call compile_c,$(POP_CFLAGS))

src/pdfshim.o: src/pdfshim.cc src/pdfshim.h
	$(call compile_cxx,$(SXBV_CXXFLAGS))

src/%.o: src/%.c src/sxbv.h src/pdfshim.h config.h
	$(call compile_c,$(SXBV_CFLAGS))

main.o: main.c src/sxbv.h src/pdfshim.h config.h
	$(call compile_c,$(SXBV_CFLAGS))

sxbv: $(OBJS) $(POPLIB)
	$(call link_bin)

-include $(DEPS) $(POPDEP)

clean:
	@$(RM) sxbv main.o src/*.o src/*.d
	@$(RM) $(POPOBJ) $(POPDEP) $(POPLIB)

install: sxbv
	install -Dm755 sxbv $(DESTDIR)$(BINDIR)/sxbv
	install -Dm644 sxbv.desktop $(DESTDIR)$(PREFIX)/share/applications/sxbv.desktop

uninstall:
	@$(RM) $(DESTDIR)$(BINDIR)/sxbv
	@$(RM) $(DESTDIR)$(PREFIX)/share/applications/sxbv.desktop
