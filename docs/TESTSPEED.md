# `make test`, faster: the plan

**Status:** built 2026-10-23, all four steps (*As built*, below, has each measured), from the
plan signed off the same day (the user: "Yes go with your recommendations"); `make test-fast`
removed after step 3 (the user). This page owns the test run's times: other docs point here.

## What a run costs now (measured)

Machine: Intel Core i7-8700K @ 3.70GHz (6 cores, 12 threads, 4.7 GHz max, powersave governor),
16 GB RAM, Samsung SSD 860 EVO 1TB (btrfs), Linux 7.2.8-9-omarchy, gcc 16.2.1. One run each,
the machine to itself, after one source file was touched (the usual case before a commit).

| phase | wall | CPU | what it is |
|---|---|---|---|
| `test-build` | 64.9 s | (one `cc`) | every source and every test compiled in **one** compiler call, ASan, nothing kept from the last run |
| the suites (`build/run-tests`) | 65.8 s | 19.1 s | 184 suites one after another in one process: **71% waiting** |
| `test-after` | 22.4 s | | `protocol` compiled again from every source but the app's (about 17 s), run, and the oracle over ./vtt (5.1 s) |
| the oracle over the ASan vtt | 27.2 s | 28.0 s | 26 map files, each command an ASan process: all CPU, one core |
| ./vtt and the ASan vtt | 0.6 s | | already per object and incremental |
| **total** | **about 180 s** | | |

The suites over a second (each alone, wall and CPU):

| suite | wall | CPU | waiting |
|---|---|---|---|
| agentcmd | 18.6 s | 0.36 s | 98% |
| netmsg | 6.9 s | 0.02 s | 100% |
| netserver | 4.5 s | 0.02 s | 100% |
| fogdiff | 3.5 s | 3.5 s | 0% |
| ctllive | 3.4 s | 0.30 s | 91% |
| netlag | 3.1 s | 0.22 s | 93% |
| apiagent | 2.4 s | 0.37 s | 85% |
| netlive, whisper, disk, netedges | 2.3, 2.3, 2.1, 1.4 s | 0.02-0.06 s | 97-100% |
| mapshift, sightwalk, privacy, mapcheck, docs, cull | 1.8, 1.7, 1.5, 1.3, 1.1, 1.1 s | the same | 0% |
| the other 167 | 4.8 s together | | |

Of `agentcmd`'s 18.6 s, about 15 are fixed waits in the test, not vtt: two
`pump_until_file` calls for text that should never come -- "let the second run finish"
(tests/test_jobs.c, `"\x01never"`) and `Job #2` not started while the agent is off -- each
run to the helper's end, 1,000 turns of 5 ms (at least 5 s); and the 5 s `AGENT_GRACE_MS`
between SIGTERM and SIGKILL in "one deaf to TERM is killed", which the test leaves at its
default (my reading of the test; the step's measurement settles it).

## The plan, in order

1. **Fixed waits made event waits** (tests only). In `agentcmd`: "let the second run finish"
   becomes `pump_until_reaped`; "no `Job #2` while off" checks that no run was started
   (`a.agents.nrun`), which is known at once; the deaf-to-TERM case sets `a.agents.grace_ms`
   small, as it already sets the deadline. Then the same audit of every suite over a second
   that is more than 80% waiting (netmsg, netserver, ctllive, netlag, apiagent, netlive,
   whisper, disk, netedges): a wait for something that never comes is replaced by the event
   that ends it, or bounded by the least time the check needs; a wait the test is about (a
   deadline, a keepalive) is kept and said why. *Expected:* agentcmd to about 3 s, the rest
   by an amount the audit finds (estimates; measured after).
2. **The tests built per object, reusing the ASan objects.** `make` already keeps
   `build/asan/*.o` for the ASan vtt, compiled with the same flags `test-build` uses for the
   sources. `run-tests` would link those (all but main.o) with the tests' own objects,
   `build/asan/tests/*.o` (with `-I tests` and the tests' warnings), and `protocol` would link
   them again without the app's, with `test_protocol.c` compiled once with
   `-DPROTOCOL_MAIN`. Every object is compiled only when its source or a header it includes
   changed (`-MMD`, as now), and in parallel under `make -j`. *Expected:* one file changed,
   about 2-5 s for both links and the file, against 82 s (64.9 + about 17); everything from
   clean with `-j12`, about 10-15 s (estimates). *Checked (the Makefile, 2026-10-23):* both
   compile vtt's sources with `BASEFLAGS DBGFLAGS CFLAGS`, and `DBGFLAGS` carries the
   sanitizers; `test-build` adds only `-I tests` and the tests' warning flags, neither of which
   changes a source's object. The step keeps one variable for both, so they cannot drift.
3. **Suites in parallel.** `run-tests -jN` (N the cores, `-j1` today's order): each suite in
   a forked child, a few at a time, its output collected and printed in the table's order,
   its failures counted from its exit and a last line it writes. A fork also gives each suite
   a process of its own: the held key that carried from one suite into the next (2026-10-23)
   cannot happen. Suites that cannot share a run -- a fixed port, a change of the process's
   working directory or a global outside the sandbox, the five `SLOW` socket suites if they
   collide -- run after the rest, one at a time; the audit names them. `make test-fast` stays
   as it is. *Expected:* the suites' phase near its critical path, the longest suite after
   step 1 (about 3.5 s, fogdiff's CPU) plus the serial group, against 65.8 s; CPU is 19.1 s
   over 12 threads, so the floor is about 2-4 s (estimates).
4. **The oracle in parallel.** tools/oracle.sh runs its 26 map files one after another; each
   file's commands write a record of their own, so files can run side by side
   (`xargs -P`), compared in a fixed order; and the two oracle checks (./vtt and the ASan
   vtt) run at once. *Expected:* 27.2 + 5.1 s to about 5-6 s (28 s of CPU over 6 cores,
   an estimate).

*Expected in all:* about 180 s to about 20-30 s after a one-file change (an estimate; each
step is measured as it lands, the same way as above, and recorded here: this page owns the
test run's times, and docs/PERFORMANCE.md points here rather than copying them).

## Speed of light

The work a run cannot avoid: compile what changed and link twice (a second or two); the
suites' CPU, 19.1 s, spread over 12 threads (about 2 s) but no less than the longest suite's
own CPU (fogdiff, 3.5 s); the oracles' CPU, 28 + 5.8 s, over 6 cores (about 6 s). About 10 s.
The plan's expected 20-30 s leaves the waits a test is about (deadlines, keepalives) and the
serial group; each is named when it is measured.

## Decisions for the user

1. The order above (recommended: as written, each step measured before the next).
2. `run-tests -jN` by default, `-j1` for today's order (recommended: yes, N the cores), with
   `make test` passing `-j`.
3. Whether `make test-fast` is still wanted once `make test` is this fast (recommended: keep it
   until step 3 is measured, then decide).

## As built

**Step 1, 2026-10-23.** `VTT_CASE_TIMES=1 build/run-tests SUITE` now prints each case's own
time on stderr (tests/check.h `case_begin`; `CASE` is it), which found the waits:
- `agentcmd` 18.6 s to 3.2 s: three waits for something that must not happen (two
  `pump_until_file` for text never written, 5 s each at least) became checks that no run was
  started -- a start is synchronous, inside the key -- and "let the second run finish" became
  `pump_until_reaped`. The plan's guess at the kill grace was wrong: that case already set
  it to 150 ms. Two mutations, each removing both guards before a start (the paused check at
  `:ask` and in `app_agent_start`; feedback's `theirs` and the start's `JOB_FROM_GM`), are
  caught by the new checks; removing either guard alone is not, by any version of the test,
  since the other holds.
- `netmsg` 6.9 s to 1.9 s: `netmsg_ws` drained a new connection for a fixed 20 turns, about
  0.8 s each; it stops on quiet once the join has come.
- `netserver` 4.5 s to 0.16 s: a read straight after a pump, before the server's answer,
  waited out `net_connect`'s 2 s `SO_RCVTIMEO` (the server runs in the test's thread); the
  loops read only when `poll` says there is something, and the page's loop stops at the end
  of the headers (it waited for `</body>` in a buffer smaller than the page).
- Kept, the waits the tests are about: `ctllive` and `disk` each wait out the channel
  client's 2 s answer time for "a vtt that is there and does not answer"; `apiagent` waits
  its retry after curl's exit 7.

The suites' phase: **65.8 s to 38.8 s** (two runs, 38.79 and 38.83 s; CPU unchanged, 18.7 s).

**Step 2, 2026-10-23.** `run-tests` links the ASan objects (`build/asan/*.o` but main.o) with
the tests' own (`build/asan/tests/*.o`, `-MMD` like the rest); `protocol` links the same
less the app's and the loop with `test_protocol.c` compiled once more under
`-DPROTOCOL_MAIN`; `make test-build` builds them all with `-j` a core (`NPROC`). Measured:

| starting point | before | after |
|---|---|---|
| one vtt source changed | 64.9 s (+ about 17 for protocol) | 0.6 s, both linked |
| one test file changed | 64.9 s | 3.2 s |
| nothing changed | 64.9 s | 0.08 s |
| everything from clean (`make clean`) | about 82 s and the release and ASan vtts | 16.1 s |
| **`make test`, one source changed** | **about 180 s** | **72.0 s** |

Built alone, the ASan objects showed what the one-cc build had hidden with
`-Wno-format-truncation` for every source: four status lines `-Og`'s analysis says could be
cut (HEALTH B22), their buffers sized now; a full rebuild has no warnings.

**Step 3, 2026-10-23.** `run-tests -jN` (else `VTT_JOBS`, else a job a core; `-j1` the one
process and order of before): each suite in a forked child, its output to a file of its own
and printed whole in the table's order, its counts in another, a crash or a LeakSanitizer
failure at its exit failing that suite alone. Every test server listens on port 0 and every
sandbox is its own `mkdtemp` directory and runtime directory, so no two suites meet. The
suites' phase: **38.8 s to 5.3 s**; ten runs clean. Two full runs at once (twice the load)
showed two races that load alone finds, in any mode: an agent killed by its TERM before its
shell had set `trap` (the test now waits for the agent to say its trap is set), and a turn
of the channel that ran out of time before the second of two requests meant for it (that
check lifts the turn's limit). After those, five rounds of two at once: clean.

**Step 4, 2026-10-23.** tools/oracle.sh runs the channel's requests, the generated maps and
each map's block side by side, `VTT_JOBS` at a time, each map in a tree of its own shaped
like the record's (`maps/` beside `work/`, every map there), so every command and heading is
what it was: the record is unchanged. The oracle over ./vtt **5.1 s to 0.78 s**, over the
ASan vtt **27.2 s to 6.7 s**; ten runs clean. Running the two checks at once would save
under a second more, so they stay one after the other.

**`make test`, all four steps: about 180 s to 13.0-13.5 s** (two runs; the plan's estimate
was 20-30 s, its speed of light about 10 s). `make test-fast` now saved 0.6 s (its suites
4.7 s against 5.3 s): **removed** (the user, 2026-10-23: "Yeah remove it"), with run-tests'
`--fast` and its `SLOW` list -- one way to run the tests, nothing to keep in step. Next (the
user, the same message): the mutation checks made faster too.

One knob for how many at once: `VTT_JOBS`, read by the Makefile (`-j`), run-tests and
tools/oracle.sh alike (the user, 2026-10-23: nothing built to drift).

**The mutation checks, 2026-10-23** (the user: "Mutant tests also take a while"). Steps 2
and 3 were the cure: tools/mutate.sh builds only the mutated file and links, then runs the
suites it names side by side. One mutation of src/apikey.c against suite `apikey`, end to end:
**about 2.3 minutes before (two one-cc builds) to 1.3 s**. A mutation in a header many files
include rebuilds each of them, so seconds, not one. Running several mutants at once would
need a tree each (they mutate the source in place) and a full build in each: not worth it at
these times.

**Its review (Fable, 98e49e4..e8205eb), 2026-10-23.** Mutations of what the faster waits test
(the 403 not sent, the idle drop, a close frame ignored) were all caught; a seeded leak in a
suite fails it from the child; the README parser finds exactly the 14 pages. Fixed after:
- a suite whose child ended early (an `exit` under it) passed with no counts: now a failure
  ("ended before its counts were written"), mutation-checked;
- `make test-build CFLAGS=-O2` built nothing on an up-to-date tree, or left -O2 objects for
  every later run: build/asan's objects are stamped with their flags as the mode is, and a
  change rebuilds them all (checked: -O2 then plain rebuilt 78 back). The -O2 build then
  showed `words` read uninitialized in `:link` with a blank argument (app_link.c), now zeroed;
- tools/oracle.sh waited for its jobs without their statuses, so a part that failed under
  `record` would have been recorded short: each pid is waited for, and a failure stops it
  (exit 2; checked with a failure planted in a copy);
- the pump-poll-read loop at three places in test_net.c is `read_until`;
- `make test` no longer prints the sub-make's "up to date".
The review timed `make test` at 17.0 s with nothing changed, 29.1 s from `make clean`; here
three runs after it gave 12.9-13.0 s. Which the machine gives depends on what else it runs.
`tools/loadcheck.sh` (two whole runs at once, seen twice) is the load check of step 3.

**Its recheck, 2026-10-23.** Each fix above reproduced as working. Fixed after it:
- under `-j1` a suite that exited the process still passed: an exit hook now fails any run
  that does not reach its end, one process or a child (mutation-checked in both modes);
- the flags stamp fired for every goal and on a dry run (`make PROF=0` threw the ASan objects
  away; `make -n` wiped them), and lost a quote in CFLAGS: both stamps -- the mode's and
  build/asan's -- are now one `STAMP_CHECK`, written and read by make's `$(file)` so flags are
  kept as given, skipped on `make -n`, and build/asan's checked only for the goals that build
  it. The mode's stamp now carries PROF and CFLAGS, so `make PROF=0` rebuilds the release
  objects, which it silently did not before.

**Added since: the evaluations' test, 2026-10-23** (ROADMAP 27 part 1, docs/AGENTLOOP.md).
`tests/agenteval.sh` runs in `test-after`, after `protocol`: **1.7 s** alone (16 stand-in
runs, each a few vtt calls through tools/sandbox.sh and a grading in python), one at a time.
A whole `make test` after it: **14.7 s** (one run, the desktop, nothing changed). Running its
runs side by side, or beside the oracle, would take most of the 1.7 s back; not done yet,
written down here as the gap.
