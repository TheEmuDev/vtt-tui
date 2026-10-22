# Scratch work, and what became a tool

**Status:** kept current (started 2026-10-21, at the user's request). A ledger of the kinds
of task that reviews and fixes build in a scratch directory. The rule is the user's global
one: a kind of task seen twice becomes a tool in tools/ (or a make target), listed in
`tools/review-brief.md` so the next reviewer uses it rather than writing it again. Each
reviewer ends its report with a *Scratch work* list, and those lines are added here.

| kind of task | seen | became |
|---|---|---|
| mutation check: a file aside, one string replaced, rebuild, the suites, restore | step 7: three rounds by the implementer, reviewers 1 and 2 (mutations A-D; the shrink, the drop's name, `out_cap`) | `tools/mutate.sh` (2026-10-21) |
| run ./vtt in a hand-built sandbox (XDG dirs, no keyring, the editor) | perf and A/B runs; `--bench-stalled`'s check; five scripts with their own, two missing the editor and the keyring | `tools/sandbox.sh`, sourced by oracle, soak, perf, sight and pagebench; suite `sandbox` holds it to `sandbox_enter` (2026-10-21) |
| loopback socket capacity, with and without SO_SNDBUF | step 7: reviewer 1, then the implementer's recheck | `tools/sockcap.py` (2026-10-21) |
| the reviewer's brief: checkout, PIDs, sandbox, cleanup, report format | rewritten in full for each review | `tools/review-brief.md` (2026-10-21) |
| a C program linking vtt's sources to time one function (`play_status` at -O2) | once: step 7, reviewer 2 | not yet: a `make microbench SRC=file.c` target the second time |
| a frame or a status line printed from both views when they differ | twice, inside tests: `press_private` (tests/harness.c) and the privacy walk (tests/test_privacy.c) | not yet: a third copy is extracted (CLAUDE.md, *One owner*) |
