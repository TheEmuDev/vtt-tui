# vtt — rules-agnostic virtual tabletop TUI
# Zero dependencies: C11 + libc + POSIX (termios, poll, dirent).

BIN       := vtt
SRCDIR    := src
OBJDIR    := build
TESTDIR   := tests

WARNINGS  := -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion \
             -Wstrict-prototypes -Wmissing-prototypes -Wpointer-arith \
             -Wwrite-strings -Wno-unused-parameter
BASEFLAGS := -std=c11 -D_POSIX_C_SOURCE=200809L -I$(SRCDIR) $(WARNINGS)

# Profiling instrumentation is compiled in by default; -DVTT_PROF=0 removes it.
PROF      ?= 1

RELFLAGS  := -O2 -DNDEBUG -DVTT_PROF=$(PROF)
DBGFLAGS  := -Og -g3 -fno-omit-frame-pointer -DVTT_PROF=$(PROF) \
             -fsanitize=address,undefined -fno-sanitize-recover=all

CC        ?= cc
CFLAGS    ?=
LDFLAGS   ?=
# sqrt() for the euclidean metric; libm is part of the C standard library.
LDLIBS    := -lm

# Objects from a debug build cannot be linked into a release one, so the mode
# is stamped and a change wipes the objects. This has to happen while the
# makefile is being read: as an ordinary prerequisite it would delete objects
# make had already decided were up to date, and the link would then fail on a
# file that was never rebuilt.
MODEFILE  := $(OBJDIR)/.mode
ifneq (,$(filter debug,$(MAKECMDGOALS)))
BUILDMODE := debug
else
BUILDMODE := release
endif

$(shell mkdir -p $(OBJDIR); \
        if [ "$$(cat $(MODEFILE) 2>/dev/null)" != "$(BUILDMODE)" ]; then \
            rm -f $(OBJDIR)/*.o $(OBJDIR)/*.d $(BIN); \
            echo "$(BUILDMODE)" > $(MODEFILE); \
        fi)

SRCS      := $(wildcard $(SRCDIR)/*.c)
OBJS      := $(SRCS:$(SRCDIR)/%.c=$(OBJDIR)/%.o)
DEPS      := $(OBJS:.o=.d)

# Test binary links every unit except main.c, plus the test harness.
LIBSRCS   := $(filter-out $(SRCDIR)/main.c,$(SRCS))
TESTSRCS  := $(wildcard $(TESTDIR)/*.c)

.PHONY: all debug test bench clean help
.DEFAULT_GOAL := all

all: BUILDFLAGS := $(RELFLAGS)
all: $(BIN)

debug: BUILDFLAGS := $(DBGFLAGS)
debug: LDFLAGS    += -fsanitize=address,undefined
debug: $(BIN)

$(BIN): $(OBJS)
	$(CC) $(OBJS) -o $@ $(LDFLAGS) $(LDLIBS)

$(OBJDIR)/%.o: $(SRCDIR)/%.c
	$(CC) $(BASEFLAGS) $(BUILDFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

test: BUILDFLAGS := $(DBGFLAGS)
test:
	@mkdir -p $(OBJDIR)
	$(CC) $(BASEFLAGS) $(DBGFLAGS) $(CFLAGS) -I$(TESTDIR) \
	    $(LIBSRCS) $(TESTSRCS) -o $(OBJDIR)/run-tests \
	    -fsanitize=address,undefined $(LDLIBS)
	@$(OBJDIR)/run-tests

# Deterministic keystroke replay; prints frame statistics as a perf baseline.
bench: all
	@./$(BIN) --bench $(TESTDIR)/bench.keys

clean:
	@rm -rf $(OBJDIR) $(BIN)

help:
	@echo "make          release build (-O2)"
	@echo "make debug    -Og -g3 with ASan + UBSan"
	@echo "make test     build and run the unit/golden test suite"
	@echo "make bench    replay tests/bench.keys and print frame stats"
	@echo "make clean    remove build artifacts"
	@echo "make PROF=0   build with profiling instrumentation compiled out"

-include $(DEPS)
