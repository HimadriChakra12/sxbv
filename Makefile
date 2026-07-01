include include/sxbv.mk

BOLD   = \033[1m
DIM    = \033[2m
CYAN   = \033[1;36m
GREEN  = \033[1;32m
ORANGE = \033[38;5;208m
BLUE   = \033[1;34m
RESET  = \033[0m

pretty = $(patsubst include/poppler-src%,POP%,$1)

define compile_c
	@t0=$$(date +%s%N); \
	out=$$($(CC) $(1) -c $< -o $@ 2>&1); rc=$$?; \
	t1=$$(date +%s%N); ms=$$(( (t1 - t0) / 1000000 )); \
	( \
		flock -w 10 200; \
		if [ $$rc -eq 0 ]; then \
			printf "$(GREEN)%-8s$(RESET) $(BOLD)%-55s$(RESET) %-55s\t$(CYAN)%dms$(RESET)\n" \
				"$(CC)" "$(call pretty,$<)" "$(call pretty,$@)" "$$ms"; \
		else \
			printf "$(GREEN)%-8s$(RESET) $(BOLD)%-55s$(RESET) FAILED\t$(CYAN)%dms$(RESET)\n" \
				"$(CC)" "$(call pretty,$<)" "$$ms"; \
			printf "%s\n" "$$out"; \
		fi \
	) 200>/tmp/.sxbv-build.lock; \
	exit $$rc
endef

define compile_cxx
	@t0=$$(date +%s%N); \
	out=$$($(CXX) $(1) -c $< -o $@ 2>&1); rc=$$?; \
	t1=$$(date +%s%N); ms=$$(( (t1 - t0) / 1000000 )); \
	( \
		flock -w 10 200; \
		if [ $$rc -eq 0 ]; then \
			printf "$(BLUE)%-8s$(RESET) $(BOLD)%-55s$(RESET) %-55s\t$(CYAN)%dms$(RESET)\n" \
				"$(CXX)" "$(call pretty,$<)" "$(call pretty,$@)" "$$ms"; \
		else \
			printf "$(BLUE)%-8s$(RESET) $(BOLD)%-55s$(RESET) FAILED\t$(CYAN)%dms$(RESET)\n" \
				"$(CXX)" "$(call pretty,$<)" "$$ms"; \
			printf "%s\n" "$$out"; \
		fi \
	) 200>/tmp/.sxbv-build.lock; \
	exit $$rc
endef

define link_bin
	@t=$$(date +%s%N); \
	printf "$(GREEN)link$(RESET)   $(BOLD)$@$(RESET) %-111s"; \
	$(CXX) $(LDFLAGS) $(OBJS) $(LIBS) -o $@ ; \
	rc=$$?; \
	ms=$$(( ($$(date +%s%N) - t) / 1000000 )); \
	[ $$rc -eq 0 ] && printf "$(CYAN)%dms$(RESET)\n" $$ms || exit $$rc
endef

.PHONY: all bench clean distclean install uninstall

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

bench:
	@t=$$(date +%s%N); \
	$(MAKE) --no-print-directory sxbv; \
	rc=$$?; \
	ms=$$(( ($$(date +%s%N) - t) / 1000000 )); \
	if [ $$rc -eq 0 ]; then \
		printf "\n$(BOLD)Build finished in$(RESET) $(CYAN)%dms$(RESET)\n" $$ms; \
	else \
		exit $$rc; \
	fi

# clean: your code only — Poppler stays cached
clean:
	@$(RM) sxbv main.o main.d src/*.o src/*.d
	@printf "$(GREEN)CLEANED$(RESET)\n"

# distclean: also wipe the cached Poppler build
distclean: clean
	@$(RM) $(POPOBJ) $(POPDEP) $(POPLIB)
	@printf "$(GREEN)DISTCLEANED$(RESET)\n"

install: sxbv
	install -Dm755 sxbv $(DESTDIR)$(BINDIR)/sxbv
	install -Dm644 sxbv.desktop $(DESTDIR)$(PREFIX)/share/applications/sxbv.desktop

uninstall:
	@$(RM) $(DESTDIR)$(BINDIR)/sxbv
	@$(RM) $(DESTDIR)$(PREFIX)/share/applications/sxbv.desktop
