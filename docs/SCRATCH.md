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
| a cost-per-call row's scenario alone, under two binaries | step 6 part B (`fog.sight.rim`), step 7's timing (`app.draw`, `editor.draw`, `disk.check`) | `ONLY='regex' tools/perf.sh` (2026-10-21) |
| a served vtt fed keys through a fifo, a loopback WebSocket client recording its stream, decoded by the page's `feed()` against a fresh client's | once: step 7's phone check, to tell the phone's staleness from vtt's (vtt sent all 156 frames) | not yet: `tools/phonecheck.sh`'s `start_vtt` and tests/page_feed.js are the parts, the second time |
| the bench's stalled-watcher message checked against its trace (net.catch_up, net.enc_full counts and nesting) | the review of 000a9e5, its recheck | `tools/stalled.sh` (2026-10-21) |
| a Python heredoc that replaces one unique string in a file (asserting it occurs once) | in nearly every edit by the implementer, step 7 through B15 | not a tool: the Edit tool does exactly this, and is what the implementer should use |
| a node harness for the page's connection code: a Chrome-like WebSocket stub, fake timers, a stub document, invariants after each step | twice: B15's review (24 checks), its recheck (31) | suite `pageconn`, tests/page_conn.js (2026-10-22) |
