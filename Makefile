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
# The tests build Token literals positionally and format long paths into
# fixed buffers on purpose; those two warnings say nothing there and bury
# the ones that do. The sources themselves keep every warning.
TESTWARN  := -Wno-missing-field-initializers -Wno-format-truncation

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

# A stamp, the one check for both sets of objects: the file named by $(1)
# holds the flags (the variable named by $(2)) its objects were built with,
# written and compared by make itself so a flag is kept as given, quotes and
# all; a change removes what $(3) names. Never on a dry run (make -n).
DRYRUN := $(findstring n,$(firstword -$(MAKEFLAGS)))
define STAMP_CHECK
ifeq (,$(DRYRUN))
ifneq ($$(file <$$($(1))),$$($(2)))
$$(shell mkdir -p $$(dir $$($(1))); rm -rf $$($(3)))
$$(file >$$($(1)),$$($(2)))
endif
endif
endef

MODETEXT  := $(BUILDMODE) PROF=$(PROF) CC=$(CC) $(CFLAGS)
MODEWIPE  := $(OBJDIR)/*.o $(OBJDIR)/*.d $(BIN)
$(eval $(call STAMP_CHECK,MODEFILE,MODETEXT,MODEWIPE))

SRCS      := $(wildcard $(SRCDIR)/*.c)
OBJS      := $(SRCS:$(SRCDIR)/%.c=$(OBJDIR)/%.o)
DEPS      := $(OBJS:.o=.d)

# Test binary links every unit except main.c, plus the test harness.
LIBSRCS   := $(filter-out $(SRCDIR)/main.c,$(SRCS))
TESTSRCS  := $(filter-out $(TESTDIR)/fuzz_%.c,$(wildcard $(TESTDIR)/*.c))

.PHONY: all debug test test-after test-build soak mcount oracle bench perf fuzz fuzz-ctl fuzz-json install-skill clean help
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

# The same vtt built with the sanitizers, for the oracle to run (docs/V2.md,
# *Memory: the plan*, 1): LeakSanitizer then sees main.c and every path the
# oracle drives, which the release vtt cannot tell. Objects of its own, so the
# mode stamp above never wipes them and a change rebuilds only its file.
ASANDIR   := $(OBJDIR)/asan
ASANOBJS  := $(SRCS:$(SRCDIR)/%.c=$(ASANDIR)/%.o)
# Its objects are stamped with the flags they were built with, as the mode is
# above: `make test-build CFLAGS=-O2` rebuilds them all at -O2, and the next
# build without it rebuilds them back, never mixing the two.
# Checked only for the goals that build them: a plain `make PROF=0` must not
# throw them away.
ASANSTAMP := $(ASANDIR)/.flags
ASANTEXT  := CC=$(CC) $(DBGFLAGS) $(CFLAGS)
ASANWIPE  := $(ASANDIR)/*.o $(ASANDIR)/*.d $(ASANDIR)/tests $(OBJDIR)/run-tests $(OBJDIR)/protocol $(ASANDIR)/vtt
ifneq (,$(filter test test-build test-after $(ASANDIR)/% $(OBJDIR)/run-tests $(OBJDIR)/protocol,$(MAKECMDGOALS)))
$(eval $(call STAMP_CHECK,ASANSTAMP,ASANTEXT,ASANWIPE))
endif
$(ASANDIR)/vtt: $(ASANOBJS)
	$(CC) $(ASANOBJS) -o $@ -fsanitize=address,undefined $(LDFLAGS) $(LDLIBS)
$(ASANDIR)/%.o: $(SRCDIR)/%.c
	@mkdir -p $(ASANDIR)
	$(CC) $(BASEFLAGS) $(DBGFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

# The test binaries link the sanitizer objects above (the same flags: one
# variable, DBGFLAGS, for both) with the tests' own, each compiled only when
# it or a header it includes changed, and side by side (docs/TESTSPEED.md,
# step 2): `make test-build && build/run-tests NAME...` while iterating on a
# suite. The release vtt too: the --apply and channel tests run ./vtt, and a
# stale one would be tested silently. protocol is the World's suite again
# with no src/app*.c and no loop (test-after says why).
TESTOBJDIR := $(ASANDIR)/tests
TESTOBJS   := $(TESTSRCS:$(TESTDIR)/%.c=$(TESTOBJDIR)/%.o)
PROTOOBJS  := $(filter-out $(ASANDIR)/main.o $(ASANDIR)/app%.o $(ASANDIR)/loop.o,$(ASANOBJS))
# How many at once, for the builds, the suites and the oracle alike: VTT_JOBS,
# else a job a core. run-tests and tools/oracle.sh read the same variable.
NPROC      := $(or $(VTT_JOBS),$(shell nproc 2>/dev/null || echo 4))

$(TESTOBJDIR)/%.o: $(TESTDIR)/%.c
	@mkdir -p $(TESTOBJDIR)
	$(CC) $(BASEFLAGS) $(DBGFLAGS) $(CFLAGS) -I$(TESTDIR) $(TESTWARN) -MMD -MP -c $< -o $@
$(TESTOBJDIR)/protocol_main.o: $(TESTDIR)/test_protocol.c
	@mkdir -p $(TESTOBJDIR)
	$(CC) $(BASEFLAGS) $(DBGFLAGS) $(CFLAGS) -I$(TESTDIR) $(TESTWARN) -DPROTOCOL_MAIN -MMD -MP -c $< -o $@
$(OBJDIR)/run-tests: $(filter-out $(ASANDIR)/main.o,$(ASANOBJS)) $(TESTOBJS)
	$(CC) $^ -o $@ -fsanitize=address,undefined $(LDFLAGS) $(LDLIBS)
$(OBJDIR)/protocol: $(PROTOOBJS) $(TESTOBJDIR)/protocol_main.o
	$(CC) $^ -o $@ -fsanitize=address,undefined $(LDFLAGS) $(LDLIBS)

# Every binary the tests need, built with a job a core.
test-build:
	@$(MAKE) --no-print-directory -j$(NPROC) all $(OBJDIR)/run-tests $(OBJDIR)/protocol $(ASANDIR)/vtt

test: test-build
	@$(OBJDIR)/run-tests
	@$(MAKE) --no-print-directory test-after
	@echo "the oracle again, over the sanitizer vtt (a leak or an error is a difference):"
	@./tools/oracle.sh check $(ASANDIR)/vtt || { echo "the sanitizer vtt differs (a leak exits 1, with a LeakSanitizer report on stderr): run a differing command with $(ASANDIR)/vtt to read it" >&2; exit 1; }

# What make test runs after the suites.
test-after:
	@# The seam (docs/V2.md, D2): the protocol's suite again, built with no
	@# src/app*.c and no loop: a request handler or world.c that reached for
	@# the app would not link.
	@# ... and would not get this far with an app header, whose inlines and
	@# types need no object file to be reached through.
	@if grep -n '#include "app' $(SRCDIR)/world.c $(SRCDIR)/world.h $(SRCDIR)/world_ops.c $(SRCDIR)/world_ops.h $(SRCDIR)/request.c \
	    $(SRCDIR)/request_job.c $(SRCDIR)/request_propose.c $(SRCDIR)/request.h $(TESTDIR)/test_protocol.c; then \
	    echo "the World's files must not include an app header (docs/V2.md, D2)" >&2; exit 1; fi
	@$(MAKE) -s --no-print-directory -j$(NPROC) $(OBJDIR)/protocol
	@$(OBJDIR)/protocol
	@./tests/agenteval.sh
	@./tools/oracle.sh check ./$(BIN)

# Long sessions and their peak memory, each against a bound (tools/soak.sh):
# about 20 s, so not part of make test.
soak: all
	@./tools/soak.sh

# A developer's allocation counter (tools/mcount.c): LD_PRELOAD=build/mcount.so
# ./vtt ... counts every libc allocation; Linux and glibc only, part of no build.
mcount: $(OBJDIR)/mcount.so
$(OBJDIR)/mcount.so: tools/mcount.c
	@mkdir -p $(OBJDIR)
	$(CC) -O2 -shared -fPIC -o $@ $< -ldl

# What ./vtt prints for a fixed set of maps, keys and requests, against the
# record under tests/oracle/expected (docs/V2.md, D11). After a change that is
# meant to alter that output: tools/oracle.sh record, and read the diff.
oracle: all
	@./tools/oracle.sh check ./$(BIN)

# Deterministic keystroke replay; prints frame statistics as a perf baseline.
bench: all
	@./$(BIN) --bench $(TESTDIR)/bench.keys

# Every path that costs anything, across a set of scenarios. Regenerates the
# tables in docs/PERFORMANCE.md -- paste its output there when a row moves.
perf: all
	@./tools/perf.sh

# Fuzzes the map loader with libFuzzer (clang only). The corpus is a scratch
# copy of the fixtures so libFuzzer's additions do not land in the tree.
FUZZ_SECONDS ?= 60
fuzz:
	@mkdir -p $(OBJDIR)/fuzz-corpus && cp $(TESTDIR)/fixtures/*.vtt $(TESTDIR)/fuzz-found/*.vtt $(OBJDIR)/fuzz-corpus/
	clang $(BASEFLAGS) -O1 -g -DVTT_PROF=0 -fsanitize=fuzzer,address,undefined \
	    $(LIBSRCS) $(TESTDIR)/fuzz_mapio.c -o $(OBJDIR)/fuzz-mapio $(LDLIBS)
	$(OBJDIR)/fuzz-mapio -max_total_time=$(FUZZ_SECONDS) -max_len=65536 $(OBJDIR)/fuzz-corpus

# The control channel's requests, against a fixture map (docs/CONTROL.md).
fuzz-ctl:
	@mkdir -p $(OBJDIR)/fuzz-ctl-corpus && cp $(TESTDIR)/fuzz-ctl/* $(OBJDIR)/fuzz-ctl-corpus/
	clang $(BASEFLAGS) -O1 -g -DVTT_PROF=0 -fsanitize=fuzzer,address,undefined \
	    $(LIBSRCS) $(TESTDIR)/fuzz_ctl.c -o $(OBJDIR)/fuzz-ctl $(LDLIBS)
	$(OBJDIR)/fuzz-ctl -max_total_time=$(FUZZ_SECONDS) -max_len=65536 $(OBJDIR)/fuzz-ctl-corpus

fuzz-json:
	@mkdir -p $(OBJDIR)/fuzz-json-corpus && cp $(TESTDIR)/fuzz-json/* $(OBJDIR)/fuzz-json-corpus/
	clang $(BASEFLAGS) -O1 -g -DVTT_PROF=0 -fsanitize=fuzzer,address,undefined \
	    $(LIBSRCS) $(TESTDIR)/fuzz_json.c -o $(OBJDIR)/fuzz-json $(LDLIBS)
	$(OBJDIR)/fuzz-json -max_total_time=$(FUZZ_SECONDS) -max_len=65536 $(OBJDIR)/fuzz-json-corpus

# The measuring programs tools/saves.sh and tools/proposals.sh run
# (tools/NAME.c against the release objects), linked here because make holds
# today's object list: build/ keeps the objects of sources since renamed or
# removed, which a glob there would link. Release flags on any object this
# rebuilds, as `all` gives them.
$(OBJDIR)/tool-%: BUILDFLAGS := $(RELFLAGS)
$(OBJDIR)/tool-%: tools/%.c $(filter-out $(OBJDIR)/main.o,$(OBJS))
	$(CC) $(BASEFLAGS) $(RELFLAGS) $(CFLAGS) $^ -o $@ $(LDLIBS)

# The agent skill (docs/AGENTS.md's top): user-level, because the agents
# that build maps run wherever the GM starts them, not in this repository.
SKILLDIR  ?= $(HOME)/.claude/skills/vtt
install-skill:
	mkdir -p $(SKILLDIR)
	cp tools/skill/SKILL.md $(SKILLDIR)/SKILL.md

clean:
	@rm -rf $(OBJDIR) $(BIN)

help:
	@echo "make          release build (-O2)"
	@echo "make debug    -Og -g3 with ASan + UBSan"
	@echo "make test     build and run the unit/golden test suite"
	@echo "make test-build   build build/run-tests only, for build/run-tests NAME..."
	@echo "make soak     peak memory of long sessions against their bounds (about 20 s)"
	@echo "make mcount   build/mcount.so: every libc allocation counted (LD_PRELOAD)"
	@echo "make oracle   compare ./vtt's output with tests/oracle/expected"
	@echo "make bench    replay tests/bench.keys and print frame stats"
	@echo "make perf     measure every path; regenerates docs/PERFORMANCE.md"
	@echo "make install-skill  the agent skill to ~/.claude/skills/vtt/"
	@echo "make clean    remove build artifacts"
	@echo "make PROF=0   build with profiling instrumentation compiled out"

-include $(DEPS) $(ASANOBJS:.o=.d) $(TESTOBJS:.o=.d) $(TESTOBJDIR)/protocol_main.d
