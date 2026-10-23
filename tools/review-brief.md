# The standing part of a reviewer's brief

A Fable review (CLAUDE.md, *Model roles*) is briefed with this file and three lines of its
own: the sha, the diff range, and what to look hard for. Paste this text after them, or
tell the reviewer to read `tools/review-brief.md` first. What is said here is not repeated
in the brief.

## Before anything

- Run `git checkout --detach SHA`. If that is refused, run `git archive SHA | tar -x -C SCRATCH/tree`
  and work there. Then confirm that `git log -1` shows SHA. A worktree may start on main.
- Read CLAUDE.md (the project's rules), the plan section and the *Progress* entries the
  brief names, and the docs/CODEMAP.md row of each file you touch. Never read the whole
  file. docs/HEALTH.md's *The questions* are part of every review: duplication,
  organization, performance, test gaps and the docs, for the change and what it touches.

## The tools: use these before writing your own

| task | tool |
|---|---|
| does a test catch this fault? | `tools/mutate.sh FILE 'old' 'new' SUITE...`: the old text must occur once; the file is restored (after a SIGKILL the next run refuses and names the original); prints caught or SURVIVED |
| does suite pageconn catch a fault in the page's connection code? | `tools/pagemutate.sh 'old' 'new'`: seconds, no rebuild, the page untouched |
| a test that only fails under load? | `tools/loadcheck.sh [ROUNDS]` |
| several mutations in one go | `tools/mutations.sh LIST SUITE...` (the list's form is in its header) |
| does the API key linger in memory after the app reads it? | `tools/keyscan.sh`: never the real keyring; exit 1 on a copy |
| run ./vtt or a script away from the user's files | `tools/sandbox.sh CMD...`, or `. tools/sandbox.sh; sandbox_env DIR` in a script |
| only some suites | `make test-build && build/run-tests NAME...` (`-j1` for one process, in order; `VTT_CASE_TIMES=1` for each case's time) |
| the whole suite | `make test` (ASan; docs/TESTSPEED.md has what it takes now); wrap long runs in `timeout` |
| how much a socket takes toward a peer that never reads | `tools/sockcap.py [SNDBUF...]` |
| a frame as text, or the players' | `./vtt MAP --script KEYS --dump-frame [--players] --size WxH` |
| headless channel requests | `./vtt MAP --apply plan.txt [--new WxH]` |
| line-search counterexamples | tools/shift/ (README.md) |
| some perf scenarios only (a user's go needed for timing) | `ONLY='regex' tools/perf.sh`, `BIN=` for another binary |
| the page on the user's phone (manual, the user's go and phone) | `tools/phonecheck.sh` (`VIA=usb`: no firewall rule, but Chrome keeps a background socket open) |
| does the lag bound hold in the bench (untimed) | `tools/stalled.sh [LOOPS]` |

If you build something in scratch that is not in this table, list it at the end of your
report (see the report format below).

## Rules

- Work only in your worktree and one scratch directory of your own. Delete your scratch
  files when done, and leave the worktree clean. Do not commit. Edit nothing outside it.
- Stop only processes whose PID you recorded when you started them. Never kill by name
  (never `pkill vtt`): the user may have vtts open.
- Anything that runs ./vtt or opens the channel goes in the sandbox. Never touch the user's
  ~/.local/share/vtt, settings, keyring, editor or running vtts, and use no network beyond
  loopback. Never call a paid API.
- No timing runs (`tools/perf.sh`, `tools/vs1x.sh`): they need the user's go. Timing one
  function in a scratch program of your own is fine; say the machine.
- Never report a finding you have not checked. Reproduce it, or label it reasoning only and
  name what would settle it. A false positive costs more than a missed finding.

## The report

- Each finding: `file:line`, a concrete failure scenario, and the evidence (a command you
  ran and its output, a test you wrote that fails, or a mutation from `tools/mutate.sh`).
  Say which findings you reproduced and which are reasoning only.
- Then the health questions, briefly.
- Then **Docs drift**, required in every review (the user, 2026-10-23): run
  `build/run-tests docs` (after `make test-build`), then read each kept-current doc
  (the whole list is docs/README.md's *Kept current*: its table and the files outside docs/
  under it) where it speaks of what the
  change touches, and say for each whether it is still true. A sentence the code now
  contradicts is a finding like any other: the doc's line, the code's line, the fix. "No drift"
  is a valid answer only after naming the docs read.
- Last, **Scratch work**: one line per thing you built in scratch to do the review (a
  harness, a timing program, a script), and whether it would be reused. These lines go into
  docs/SCRATCH.md. A kind of task seen twice becomes a tool.
- Keep it under the word count the brief gives (900 if it gives none).
