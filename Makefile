include include/sxbv.mk 

BOLD  = \033[1m
DIM   = \033[2m
CYAN  = \033[1;36m
GREEN = \033[1;32m
ORANGE = \033[38;5;208m
BLUE    = \033[1;34m
RESET = \033[0m

pretty = $(patsubst include/poppler-src%,POP%,$1)

define compile_c
	@t=$$(date +%s%N); \
	printf "$(GREEN)%-8s$(RESET) $(BOLD)%-55s$(RESET) %-55s" \
		"$(CC)" "$(call pretty,$<)" "$(call pretty,$@)"; \
	$(CC) $(1) -c $< -o $@ ; \
	rc=$$?; \
	ms=$$(( ($$(date +%s%N) - t) / 1000000 )); \
	[ $$rc -eq 0 ] && printf "\t$(CYAN)%dms$(RESET)\n" $$ms || exit $$rc
endef

define compile_cxx
	@t=$$(date +%s%N); \
	printf "$(BLUE)%-8s$(RESET) $(BOLD)%-55s$(RESET) %-55s" \
		"$(CXX)" "$(call pretty,$<)" "$(call pretty,$@)"; \
	$(CXX) $(1) -c $< -o $@ ; \
	rc=$$?; \
	ms=$$(( ($$(date +%s%N) - t) / 1000000 )); \
	[ $$rc -eq 0 ] && printf "\t$(CYAN)%dms$(RESET)\n" $$ms || exit $$rc
endef

define link_bin
	@t=$$(date +%s%N); \
	printf "$(GREEN)link$(RESET)   $(BOLD)$@$(RESET) %-111s"; \
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
	@$(RM) sxbv main.o src/*.o src/*.d main.d
	@$(RM) $(POPOBJ) $(POPDEP) $(POPLIB)
	@printf "$(GREEN)CLEANED$(RESET)"

install: sxbv
	install -Dm755 sxbv $(DESTDIR)$(BINDIR)/sxbv
	install -Dm644 sxbv.desktop $(DESTDIR)$(PREFIX)/share/applications/sxbv.desktop

uninstall:
	@$(RM) $(DESTDIR)$(BINDIR)/sxbv
	@$(RM) $(DESTDIR)$(PREFIX)/share/applications/sxbv.desktop
