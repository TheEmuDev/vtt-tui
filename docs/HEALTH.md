# Health checks

What each project health check found, dated, with the evidence and what was decided. A
finding stays here until it is fixed (then its entry says by which commit) or consciously
left (then it moves to docs/CODEMAP.md's watch list with the point at which it stops being cheap
to ignore).

## The questions

A full health check asks these of the whole code. Every review asks them too, of the change
and the code it touches, alongside correctness. Check each answer by hand before it is
written down.

**Duplication**
- Is the same logic written out more than once? Two copies are allowed. A third copy is
  extracted into one function before it is written (the rule of three).
- Does the change add a copy of something on docs/CODEMAP.md's watch list? Then that item is due.

**Organization**
- Does the code live in the file that owns it? docs/CODEMAP.md's file table answers this.
- Has a file grown too big to read as one subject? Split it along a seam, as app_browser.c
  and app_build.c came out of app.c.
- Does every source file still have a row in docs/CODEMAP.md's file table, and is that row true?

**Performance**
- Does every new drawing or keystroke path have a `PROF_ZONE`, a scenario in `tools/perf.sh`
  that reaches it, and a row in a regenerated PERFORMANCE.md?
- Does each perf row measure what its label says?
- Does the window, not the map, set a drawing path's cost? Every draw culls with
  `grid_visible_tiles` first. Anything else that grows with the map (load, save, sight,
  route search) says so in its perf row.
- Count bytes written as well as frame time: bytes matter more.
- Did the plan estimate the speed of light, and was the built path measured against it? Is
  every gap explained? Does the cost follow the work that must be done (what changed, what
  is on screen), or the size of the map?
- Are PERFORMANCE.md's numbers stale? Were they measured somewhere different from where
  the cost really falls (for example tmpfs instead of the real disk)?
- Is the phone page still under 12 KB as sent? Below the limit, effort goes to its run-time
  JavaScript, never to shaving bytes.

**Tests**
- If the behavior broke, would a test fail?
- Are the error paths tested: refusals, damaged files, a full buffer, a closed socket?
- Is the players' frame tested? GM-only things must never reach it.
- Is every new screen or dialog drawn in a test (a golden frame)?
- Do the fuzzers' seeds (tests/fixtures, tests/fuzz-ctl, tests/fuzz-json) carry the newest
  file-format record and channel line?
- Is the suite clean: does it leave nothing in `/tmp`, test the binary it just built, and
  avoid tests that pass without testing anything? Does
  `make test CC=clang CFLAGS=-fno-sanitize-recover=all` still pass?

**Docs**
- Does the README tell a user how to use the feature, in plain instructional terms?
- Are CLAUDE.md, docs/CODEMAP.md, docs/KEYS.md (and the `?` page from `src/keys.c`) and the feature's
  design doc in step with the code?
- Where would a user get stuck: something undocumented, unexplained or cut off?
- Is each thing called by one name (creature, the players' view, wall mode)?

## Left open by reviews

Every finding a review made that was **not fixed**: left on purpose, put off, or answered
with a note instead of a change. One row each, so nothing a reviewer said is lost between a
review and the next. The rules (the user, 2026-10-09):

- A finding ends one of three ways: **fixed** (it is not here), **disproved** by a check that
  was run (it is under *Disproved*, with the check), or **left** (a row here, with why and
  what would close it). Never dropped without one of the three.
- A row leaves this table only when it is fixed; then it moves to *Closed*, with the commit.
- "Checked" is the date the row was last compared with the code and found still true.

### Open

| # | review | item | kind | why it was left | closes when | checked |
|---|---|---|---|---|---|---|
| 1 | step 4 (ced6c35) | The proposal code (`propose` parsing, `proposal_begin`/`end`, `finish_proposal`, about 120 lines) sits in `app_ctl.c`, 1,564 lines, though `app_ctl_job.c` (204) now exists for jobs. | organization | It shares `app_ctl.c`'s `Edits` struct, which is private to that file; moving it means exporting the struct or splitting it. Skipped at the time with no reason written down. | `Edits` is exported or split and the three functions move; or `app_ctl.c` is next reorganized | 2026-10-09 |
| ~~2~~ | *closed, below* | | | | | |
| ~~3~~ | *closed, below* | | | | | |
| ~~4~~ | *closed, below* | | | | | |
| 5 | step 4 | A proposal that came in under `:agent accept auto` while the GM was busy still lands at the next key after the GM types `:agent accept review`. | behavior | Kept: "at once" is decided when a proposal arrives, and is documented so in CONTROL.md. Arguable; the reviewer said so too. | the user decides it should follow the setting at landing time | 2026-10-09 |
| 6 | step 5 (93e9a1c) | The "map closed" event when the GM travels through a link to another map (`app.c:196`) has no test. Closing a map is tested; traveling is not. | test gap | Needs two map files and a party to travel; skipped as "hard". | a case in `events` (or test_places.c's trip tests) checks the event after `g o` | 2026-10-10: the final review found the sibling path, `:e`, untested and broken (fixed, suite `diskwhole`); the trip is still untested |
| ~~7~~ | *closed, below* | | | | | |
| 8 | step 5; first raised by the review of the plan's third draft | `ctl.wait`, the zone round answering held waits, has no perf row: the bench has no socket to hold a wait on. | performance | Stood in for by a manual check against a live vtt (1 ms from key to answer, no CPU while held), written out in CONFLICTS.md *Progress, step by step*. | the bench gains a client that holds a wait, or a test times the flush | 2026-10-09 |
| ~~9~~ | *closed, below* | | | | | |
| 10 | step 6 (f306906); step 7's review: opening now asks first, so it is rarer | Two vtts holding one file: the proposal goes to the first found, and stderr says how many others. The others' GMs are not told. No test covers it. | behavior, test gap | Step 7 makes it rare ("opening a map another vtt has open asks first"); the warning was the cheap part. | step 7 lands; a test with two listening apps on one file checks the warning | 2026-10-09 |
| 11 | step 6 | No test that `vtt --ctl` picks the one vtt with `agent on` when two are listening. | test gap | Needs two apps listening in one test. | a `ctllive` case with a second `Ctl` | 2026-10-09 |
| ~~12~~ | *closed, below* | | | | | |
| ~~13~~ | *closed, below* | | | | | |
| ~~14~~ | *closed, below* | | | | | |
| ~~15~~ | *closed, below* | | | | | |
| ~~16~~ | *closed, below* | | | | | |
| ~~17~~ | *closed, below* | | | | | |
| 18 | commit 98c9ec2 (the `Machine:` line, 2026-10-07) | The header table's laptop row has no RAM or top frequency, and the speed-of-light figures exist only for the desktop. | docs | Only the user can run `tools/machine.sh` and the measurements on the laptop. | they are run there and pasted in | 2026-10-09 |
| ~~19~~ | *closed, below* | | | | | |
| 20 | step 1 | No test for what `cs_apply` reports as left out when the map's notes, areas or rolls are full ("no room", `changeset.c:659-710`). | test gap | Not written. | a case fills each list, accepts, and checks the report and that nothing was half applied | 2026-10-09 |
| 21 | step 1 (and step 4) | No test shows a preview after the live map was resized (the guard at `changeset.c:998`), nor `job N dump` refusing a proposal made before a `:resize`. | test gap | Not written. | a case resizes the live map, then `cs_show`/`cs_unshow` leaves every byte, and `job N dump` says to propose again | 2026-10-09 |
| 22 | step 1 | No test that an accept of nothing (a box holding none of it) records no undo step, nor of its message ("nothing of it is in"). | test gap | Holds today by the reviewer's probe; not pinned. | a case checks the undo depth and the status text | 2026-10-09 |
| 23 | step 1 | Conflicts in the small parts are tested for an area's box, a roll, a card and the round, not for the spotlight or an area renamed only by case. | test gap | Not written. | two lines in that case | 2026-10-09 |
| 24 | step 1 | The Proposals table's separator row is narrower than three of its headers (printed by `tools/proposals.c`). | cosmetic | Renders correctly; only the source looks ragged. | the format string is widened | 2026-10-09 |
| 25 | step 1 | `grow` (`changeset.c:215`) is a general doubling helper, beside hand-written copies in json.c, maptools.c, store.c and mapio.c. | duplication | It predates the change; the reviewer called it optional. | it moves to util.c/h and the copies use it | 2026-10-09 |
| 26 | step 1 | `map_copy_into(dst, src)` with `dst == src` would free its own scenes and cards first. Nothing calls it that way, and nothing says not to. | robustness | No caller does it. | an early return, an assert, or a line in map.h | 2026-10-09 |
| 27 | step 2 (043ca4f) | A checkpoint test accepts between 4 and 8 saved blocks where the squares it writes fix the number (test_changeset.c:680). | test gap | Called harmless by the reviewer. | it checks the exact count | 2026-10-09 |
| 28 | step 3 (6337c26) | No test that an at-once result lands while the GM reviews **another** job, with the review left open and the status line changed (the behavior CONFLICTS.md:101-103 describes). | test gap | Not written; the same-job case was (step 4). | a `jobs` case | 2026-10-09 |
| 29 | step 3 | No test presses `u` after an accept by box: that the map reverts and the proposal keeps only the rest (CONFLICTS.md:104-106). | test gap | Not written. | a `u` after the boxed accept in test_jobs.c:143 | 2026-10-09 |
| 30 | step 3 | The players' frame is checked for the tint of an asked job, not of a ready one. | test gap | The same drawing path; the reviewer called it acceptable. | the check is repeated after a proposal | 2026-10-09 |
| 31 | step 7 (28e5563) | `:w NAME` onto a file that already exists and is not this map's writes over it without asking. | data safety | Older than step 7, which guards only the map's own file. | saving onto an existing other file asks first (a confirm, or `:w! NAME`) | 2026-10-10 |
| 32 | step 7 | After a crash, recovery is offered only when the autosave is newer than the map's file. If someone else writes the file after the crash, the autosave is the older one and the GM's unsaved work is never offered back. | data safety | Older than step 7; found while reading who writes the file. | recovery is offered whenever an autosave is there, saying which is newer | 2026-10-10 |
| 33 | step 7 | A trip through a link saves the map it arrives at (`app_link.c:194`) and opens it without asking the other vtts whether one has it open; only `app_open_map` asks. | data safety | The plan's words cover opening a map, and a trip is not `app_open_map`. | the trip asks `ctl_who_holds` for the destination, and refuses or asks | 2026-10-10 |
| 34 | step 7 | The new dialog (`MODAL_CONFIRM_HELD`, two wordings) is never drawn in a test; the health questions want a golden frame for every dialog. | test gap | Its text is checked in a child process, which cannot hand a frame back. | a golden frame of each wording, the modal set by hand | 2026-10-10 |
| 35 | step 7 | Every map opened waits two seconds for each vtt that is stopped or in an editor, with the main loop held, before asking the GM. | performance | The wait is how silence is told from "no"; README says so. | the ask goes out without blocking, and the map opens when the answers are in | 2026-10-10 |
| 36 | step 7 | The outside-change table printed by `tools/proposals.c` has the same narrow separator as row 24. | cosmetic | With row 24. | with row 24 | 2026-10-10 |
| 37 | step 8 (d5ed619) | The `:` line holds 255 characters and cuts a longer one without saying: an `:agent command` of 250 characters was set as its first 241. | behavior | The limit is the `:` line's own, older than the step, and true of every command. | the `:` line refuses to take a character past its length, or says it cut | 2026-10-10 |
| 38 | step 8 | Three places start a process, each with its own care: the card editor (resets SIGINT/SIGQUIT), `:mirror` (a pipe that reports a failed exec), the agent command (closes descriptors, resets SIGPIPE, sets the environment). | duplication | The detaching double fork is two copies; what differs is the hygiene. | one `spawn` helper holds signals, descriptors and the exec-failure report | 2026-10-10 |
| 39 | step 8 | A command that cannot run (a mistyped name) is seen only in the session log, and not at all with the log off: `sh` starts fine, and the failure is `sh`'s own message. | behavior | No way to tell from outside that the shell's command failed, short of waiting for it; README says to switch `:log` on. | the command's first lines of output, or its exit, reach the status line | 2026-10-10 |
| 40 | step 8 | Nothing bounds how many agents run: each `:ask` and each `c` starts one, and feedback sent while a run is still working puts two on one job. | behavior | By design for now (README says so); vtt does not track what it started, since the agent is nobody's child. | vtt keeps the started pids and holds a second run of a job until the first is gone | 2026-10-10 ; 2026-10-10: the final review found this worse with reused job numbers (an orphaned run taking a new job); the numbers are no longer reused at once |
| 41 | step 8 | Tests do not cover a fork or pipe failing (now said on the GM's line), a command that never reads its stdin, or descriptors at 256 and above being passed to the agent. | test gap | The first needs fault injection; the last is an edge of the GM's own shell. | a test with `RLIMIT_NPROC` or a stubbed `fork`; `close_range` where there is one | 2026-10-10 |
| 42 | step 8's review, older than it | A vtt ended by SIGTERM leaves its socket file behind (`on_fatal` does not stop the channel); the next client's refused connect removes it. | robustness | Harmless: stale sockets are removed on sight. | `on_fatal` unlinks the socket | 2026-10-10 |
| 43 | step 9's doc pass (2026-10-10) | The plan promised perf rows for a highlight and a review with the change off the window. Neither exists, and no test shows a job's tint drawing nothing when its box is off screen (the preview's cull is tested, test_changeset.c:391). | test gap, performance | The cull is by block before any square is read, the same code path as the measured rows with fewer blocks. | a `jobs` case scrolls the window off a job's box and checks no cell is tinted; a perf row only if the tint's walk ever shows | 2026-10-10 |
| 44 | the final review (2026-10-10) | The plan says change sets are "capped together at the undo log's limit". Nothing bounds a change set's memory: 8 bytes a changed cell, so a proposal of every square of a 512×512 map is about 6 MB, and 16 of them about 100 MB. | robustness | Only a deliberate fill of the largest map sixteen times gets there; the request cap (64 KB, `CTL_OPS_MAX` ops) bounds what one request can write. | a cap on cells across all waiting jobs, with the refusal an agent is told | 2026-10-10 |
| 45 | the CLAUDE.md split's review (2026-10-10) | `:dmg massive` with no argument answers with plain `app_set_status` (`app_damage.c:107`: "massive damage on - twice Severe marks 4 HP ..."), though the file's row in docs/CODEMAP.md says every message about the rule is GM-only and only the usage hint is plain. Read in the source; not drawn in a players' frame. | correctness, docs | Found while moving docs; no code was in that change. It gives away a table rule, not a creature's numbers. | the line becomes `app_set_status_gm` with a players'-frame test, or the row says the rule's state is plain | 2026-10-10 |
| ~~46~~ | *closed, below* | | | | | |
| 47 | environment cards, step 2 (2026-10-12) | `:env adversaries` matches a card's names to saved characters as written: `dire wolf` does not find `Dire-Wolf.vtt` (the file's own case), a name ending in a number (`Skeleton 2`, `Captain (Tier 2)`) is read without it, as a copy's number is, and a name over 31 bytes is cut, so two long names can collapse to one. | behavior | None of the SRD's adversaries trips any of them, and each has a plain way round (write the name as the character is saved). A case-blind match means listing the folder, which the command exists to avoid. | a name with no file is tried again unrooted, and matched without case against a listing read only when a name misses; a test for each | 2026-10-12 |
| ~~48~~ | *closed, below* | | | | | |
| 49 | the 2.0 reviews, 1 and 3 (2026-10-12) | An agent's `undo` lands while the GM is in a review: `app_ctl_busy` (`app_ctl.c:99-111`) does not count `ED_REVIEW`. Reproduced by review 3: with job 2 under review, `undo` took back accepted job 1. `ctl_undoable`/`ctl_stamp` are one per App (`app_job.c:125-129`), so with two agents one undoes the other's landing (read, not reproduced with two). | behavior | **Ruled 2026-10-12 (the user, docs/V2.md question 5): allowed for now, to be looked at again.** CONFLICTS.md chose that `u` works in a review, and row 28 is its sibling. What is still open is the two-agent case (D8) and the revisit itself. | the user looks at it again once 2.0's `job N undo` and agent names exist (does a named agent's undo of its own job during another job's review still read right on the GM's line?) and rules for good; a test of `undo` during a review either way, and a two-client test | 2026-10-12 |
| ~~50~~ | *closed, below* | | | | | |
| 51 | the 2.0 reviews, 2 (2026-10-12) | `app_tick` runs every ready request to completion before the turn's keys (`app.c:431-450`): a whole-map `dump` at 512×512 put the frame's p99 at 5.64 ms, four at once ~22 ms; the dump allocates ~3.2 MB for a 530 KB answer. Nothing refuses a whole-map `dump`, and each carries a ~330-byte key. | performance | A property of the design. docs/V2.md, D5 and D7. | a per-turn budget for requests and a cap on an unregioned `dump`, with a bench row for four dumps in a turn | 2026-10-12: the cap is built (2.0's step 4, the reads: a bare `dump` is refused above 4,096 squares, `dump all` asks by name, and `changes` reads back only where events were). Still open: the budget, and `dump all` still 5.6 ms in one turn |
| 52 | the 2.0 reviews, 3 (2026-10-12) | A failed autosave is never told to the GM: `app_autosave` returns -1 and `app_tick` drops it (`app.c:469`). Deliberate for a map never saved (the comment at `app.c:484-488`), but the same silence covers a full disk. No test makes any save fail. | data safety, test gap | Needs fault injection (row 41's reason). docs/V2.md, D3 and D13. | said once on the GM's line through `app_note_gm`, with a test through a disk seam | 2026-10-12 |
| 53 | the 2.0 reviews, 2 and 3 (2026-10-12) | A phone whose FULL exceeds `NET_FRAME_CAP` (128 KB) is closed with no count and no message (`net.c:354`). Reproduced by review 3 at 400×170 (138,986 bytes): `clients=0 dropped=0`. And a phone that stops reading is not dropped until the kernel's buffers fill: 368 KB queued and still connected (review 2), so its lag is unbounded in frames. | robustness | The overflow needs ~64,000 cells, past any terminal today; the lag harms only the lagging phone. docs/V2.md, D14. | the frame buffer sized from the window, the close counted and said, and a stale client sent one FULL after N frames, each with a test | 2026-10-12 |
| 54 | the 2.0 reviews, 1 and 3 (2026-10-12) | With `XDG_RUNTIME_DIR` deep enough that the socket's path passes 108 bytes, `:agent on` cannot listen and every client says `no vtt is taking requests - type :agent on in the one you mean`. Met by both reviewers by accident; neither captured what the GM's line said. | behavior | Low: a sandboxed agent's problem, not the GM's. | the server's reason on the GM's line and a client message that names the path's length, or a `VTT_SOCKET` override; a test | 2026-10-12 |
| 55 | the 2.0 reviews, 3 (2026-10-12) | A refused request is answered to the agent and written nowhere else (no `slog_write` on `app_ctl_exec`'s error path), and no undo op carries an author, so a session's log cannot say why an agent was refused or who changed a square. A spawned agent that dies leaves its job `WORKING` for ever (no PID kept, no deadline). Read by review 3; not rechecked by the planner. | observability | A design gap. docs/V2.md, D8 and D13. | a log line per request, an agent and job stamp on each landing, a deadline on a working job | 2026-10-12: the first half is done by the codes' commit (a refusal is written to the session log, with its class, once for each different one in a row: `log_refusal`, suite `slog`). Still open: an author on each undo op, and the agent that dies |
| ~~56~~ | *closed, below* | | | | | |
| 57 | the 2.0 reviews, all three (2026-10-12) | The design-level findings of the three reviews that are not a row above: docs/V2.md's table, F3, F6 to F16, F18, F19 (prose verdicts, no delta read, conflicts as a count, the file's density and the flush on a key, the whole-map proposal copy, the untested loop, no fault injection, handlers needing `App`, white-box tests, the glyph-only players' oracle, truth inside design records, per-creature frame cost). | organization, performance, test gaps | They are the case for the rewrite, and a plan's to answer, not a patch's. | docs/V2.md is signed off and its steps close them, or the user rules one out there | 2026-10-12 |
| 58 | 2.0's step 1, the loop (2026-10-12) | What suite `loop` does not reach: main.c's lines round the loop (the `poll` itself, `EINTR`, `a.running`, start-up, `--serve`), the input drain's 64 KB cap, and `loop_wait`'s folds other than the lone ESC and a held wait's deadline (keep-alives with a phone attached, the autosave, a ping's ring, `app_events_due`, the disk). | test gap | Row 9's residue. The folds are one line each over functions that have their own tests; the cap needs 64 KB through a pipe in pieces. | a case per fold with the thing owed (an edit with `autosave_on`, a phone on a `Conn`), and a pty test of the real binary (docs/V2.md, *Verification* 5) | 2026-10-12 |
| 59 | 2.0's step 2, the terminal (2026-10-12) | Leaving a terminal that has not read for over a second (`:q`, SIGTERM, the card editor's `term_suspend`) gives up on it after `DRAIN_MS`: the sequences that leave the alternate screen and show the cursor are never sent, so when it reads again the shell is on the alternate screen with no cursor (`reset` puts it right; cooked mode is restored either way). Quitting can take that second. Reproduced by the reviewer: SIGTERM on an unread pty, exit in 1.02 s, no `?1049l` in what the pty held. | robustness | Before step 2 the process never exited in that state, so this is the better end; waiting longer only trades it for a hang. | a longer wait on a deliberate `:q` than on a signal, or the README's troubleshooting says `reset`; a pty test | 2026-10-12 |
| ~~60~~ | *closed, below* | | | | | |
| 61 | row 48's remainder; the off channel's review (2026-10-12) | An agent's own proposal makes no event: `seq` does not move, a second agent waiting hears nothing of it, and `--apply --wait` is given 0 as the number to wait from on a vtt where nothing else has happened. | behavior | Events were written for one agent, to whom its own doing is no news (docs/CONFLICTS.md, step 5). | with names (docs/V2.md, D8) a proposal is an event for every name but its maker's; a two-client test | 2026-10-13: the reads' review adds that the agent's own `undo` makes no event either, so with a second agent `changes` would miss it. With D8 |
| ~~62~~ | *closed, below* | | | | | |
| 63 | 2.0's step 4, the codes' review (2026-10-12) | A stamp refused because the map's links or notes are full (`stamp.c:208`, `:235`, "no room: a map holds 32 links") is classed `conflict` with `stamp_place`'s other refusals, where `limit` is the class. Read by the reviewer; not reproduced (it needs a stamp carrying links onto a full map). | behavior | `stamp_place` returns one "no" for all of them, and telling them apart by their words is what the codes are there to stop. | `stamp_place` says which kind of refusal it made, as `link_map_check` now does; a case with a full map | 2026-10-12 |
| 64 | the same review | The log's "once" for a refusal is "not twice running": refusal A, a request taken, then A again writes A once. And a `vtt --ctl` that asks a vtt whose channel is off writes one `agent refused: ... the channel is off` line in that GM's log. Read. | observability | The one-deep memory is there so a caller in a loop cannot write the log as fast as it asks; a request taken clearing it would let two alternating requests do that. A new `:log` does clear it. | the budget (docs/V2.md, D5) bounds requests a turn, and then every refusal can be written | 2026-10-12 |
| 65 | the same review | `app_event`'s cut of a long text (240 bytes, `EVENT_TEXT_MAX`) and its guard for a prefix that leaves no room have no test; the oracle's proof that an edit is written rests on `scenes*.req` and the new `plain-edit` seed, since every other edit seed is refused on both of its maps. | test gap | Not written; the cut is `vsnprintf`'s. | an `events` case with a 300-byte feedback; edit seeds that land on the oracle's two maps | 2026-10-12 |
| ~~66~~ | *closed, below* | | | | | |
| 67 | the tables' regeneration, 2026-10-13, found while doing it | PERFORMANCE.md's prose quotes figures off its two tables in many sections (*Proposals*, *Events and `wait`*, the fog and phone sections), and only *Reads that cost what was asked* was reread against the new medians. The medians' p50s match what was published on every row but `agent, room + 12`, `agent, plan of rooms` and `job.propose` (the finding written up there); p99s moved both ways, as they do run to run. | docs | A reread of 1,200 lines against 169 rows; the moved rows are the ones written up. | each section's quoted figures are checked against the tables, or the quotes become references to the rows | 2026-10-13 |

### A second opinion on this list (Fable, 2026-10-10)

The reviewer of step 7 was asked to check every row against the code and rank it. All were
still true. Its ranking, to work from:

- **Do now:** B11 (a card does not save and load to the same bytes; root cause found: `card_clean`
  (`card.c:31-40`) drops leading newlines only from the raw text, so a first line holding
  only control characters or spaces is cleaned to nothing and its newline kept; the loader's
  `card_take` never keeps a leading blank line. Fix: in the newline branch, drop a blank
  line while the output is still empty; then the input joins the fixtures). Row 10's missing
  test (two vtts holding one file).
- **Worth doing:** 3, 4, 6, 7, 9, 11, 12, 14, 16, 19, 20, 21, 25, 28, 29, B5, B6, and the new
  31-33.
- **Leave** (with the reason in each row): 1, 2, 5, 8, 13, 15, 18, 22, 23, 24, 26, 27, 30, B1-B4,
  B7-B10. On the three kept on purpose it agreed: 5 ("at once" is a property of how the
  proposal was made), 15 (same user, and a plan must be told why it failed), and 14 is worth
  making the same on both roads.

### Found while building, not by a review

| # | where | item | why it was left | closes when | checked |
|---|---|---|---|---|---|
| B1 | step 4, PERFORMANCE.md *Proposals from the channel* | Making a proposal walks the change set's cells twice more than it must: once for the summary line (3.3 µs) and once for the `#N` label's corner (1.2 µs), for about 650 cells. | A proposal comes seconds apart; recorded as a finding. | `cs_summary` returns the bounds it already computes, and `corner_from_set` uses them | 2026-10-09 |
| B2 | steps 1 and 4, PERFORMANCE.md *Proposals* | The diff costs about 17 ns a changed cell against a floor near 2 (a block sort and a dedupe hash). | Accepted when step 1 was measured. | a proposal's diff shows up in a profile that matters | 2026-10-09 |
| B3 | step 4 | A request that lands at once costs about 25-41 µs more than editing straight (copy, diff, accept). | The price of one road in (decision 6). | the user wants at-once requests to skip the copy, at the cost of a second road | 2026-10-09 |
| B4 | step 6, PERFORMANCE.md | Finding which vtt holds a map costs about 0.1 ms a running vtt, not the 3 µs estimated: it wakes a sleeping process. | The wake is the cost; the redundant probe is already gone. | nothing known would | 2026-10-09 |
| ~~B5~~ | *closed, below* | | | | |
| ~~B6~~ | *closed, below* | | | | |
| B7 | the audit | `checkpoint_note_ops` takes a `forward` argument it no longer uses (`checkpoint.c:29`). | Dead since step 2's fix. | the parameter goes, with its two callers | 2026-10-09 |
| ~~B8~~ | *closed, below* | | | | |
| B9 | step 7, PERFORMANCE.md *Known gaps* | The map writer is 1.18 ms for the largest map against about 0.5: the text grows in a memory stream (a reallocation and a copy each doubling) and each row is a locked `fwrite`. | Left after the larger gap closed. | the writer sizes one buffer from the map and fills it | 2026-10-10 |
| B10 | step 7, PERFORMANCE.md *Known gaps* | The loader finds a square's kind by searching a table a character (`tile_from_file_char`, `edge_from_file_char`) and reads a line at a time: 3.4 ns a byte, 2.7 ms for the largest map. An outside change pays it twice. | It predates the step; measured here because step 7 leans on it. | a 256-entry table each way, and the rows parsed in place | 2026-10-10 |
| B11 | step 7: `make fuzz` (the loader), 2026-10-10 | **A bug, not from step 7** (the commit before it fails the same input): a card whose text starts with a line holding only a control character saves with two leading blank lines (`\| ` and `\|`), and loading that file drops them, so the map saved, loaded and saved again is not the same bytes. The fuzzer's round-trip check aborts on it. The input is kept: `tests/fuzz-found/card-leading-blank.vtt`. | Outside step 7 (cards, card.c's cleaning and the loader's `card_take`); found while fuzzing the loader this step changed. To be fixed with the review items. | the card's text is normalized the same way when it is read as when it is set (leading blank lines dropped after control characters are cleaned), the input joins the fixtures, and `make fuzz` passes on it | 2026-10-10. Met again 2026-10-12 fuzzing the loader for environment cards: a second input, `tests/fuzz-found/roundtrip-damaged.vtt` (a card whose first line starts with a control character, in a badly damaged file); the commit before that work fails it the same way |
| B12 | the final review's fixes, 2026-10-10, PERFORMANCE.md *Other writers of the file* | Reading an outside change pays about 0.55 ms of its 5.9 for new pages: the two maps it parses are freed, the allocator hands the memory back, and the next reload faults it in again. | Outside changes are rare, and keeping two scratch maps would hold 2 MB for the session. | outside changes become frequent (an agent writing the file in a loop): keep the scratch maps | 2026-10-10 |

### Disproved

(Before 2026-10-11 there were none. One finding was first set aside unchecked and was right: step 6's "the tests reach the
user's real socket directory". It is fixed, 3535bb8, and is why the rules above exist.)

| from | the finding | the check that disproved it |
|---|---|---|
| the review of docs/VEHICLES.md's first draft (2026-10-11) | "ROADMAP's highest item is 22; the new one should be 23, not 24." | `grep -n -E '^\| *23 ' docs/ROADMAP.md`: item 23 (whisper to one player, built 2026-10-03) is in *Built*, line 96. 24 is the next free number. |
| the 2.0 reviews, 2 (2026-10-12), B9 | "A FULL's worst case is ~10 bytes a cell: a 200×50 window with a color change every cell is ~100 KB and 250×60 is over the 128 KB cap, so phones could not join." | Review 3's harness (a renderer with a different fg and bg in every cell, linked against the release objects): 200×50 is 21,786 bytes, 300×60 37,886, 400×170 138,986. About 2 bytes a cell, since the palette is indexed; the cap is near 64,000 cells. The silent close is real and is row 53. |

### Closed

Rows move here when fixed, with the commit.

| # | item | closed by |
|---|---|---|
| 46 | CLAUDE.md did not know the docs' new shape: no *Read on demand* row for docs/README.md, *Progress* called the source of truth, and "A feature is not done" silent on the roadmap's *Built* table, the Status line and the index row. | the docs index's commit (2026-10-10), on the user's word: the three lines are in CLAUDE.md. |
| 17 | The map writer built its text one `fputc` at a time. | step 7 (2026-10-10): rows built whole from a table, the file written once; 3.26 ms to 1.18 ms unflushed on 512×512. What is left of the gap is B9. |
| 7 | The plan said the event reads `the GM changed: ...`; the code and AGENTS.md say `map changed: ...`. | step 9 (2026-10-10): the plan's line corrected. |
| 19 | The plan promised `"link 3, now 5"` in a summary; a new link is summarized without a number. | step 9: the plan's line says what was built. |
| B5 | CONFLICTS.md said "the checkpoint in map.c". | step 9: checkpoint.c. |
| B6 | PERFORMANCE.md's Proposals floor table quoted figures older than the table above it. | step 9: every figure in the two floor tables read off the current tables; the microsecond rows that move run to run are given as ranges. |
| B8 | docs/KEYS.md did not mention the review mode. | step 9: *The review, read against the rules*. |
| 48 | `wait 0` on a vtt with no event yet was held 60 s; `status` did not print `seq`. | 2.0's step 4 (2026-10-12): `wait 0` is an answer, not a wait (suite `loop` asks it over the socket); `status` ends with `seq N`. Left for the events' own piece: an agent's own proposal still makes no event. |
| 15 | With the channel off a caller still learned the map's name and file and what a plan's lines said back. | 2.0's step 4 (2026-10-12), on the user's ruling (docs/V2.md, question 9): off refuses every request over the socket with one line naming the map and `:agent on`; only `--apply`'s `holds` is answered. Reverses docs/CONFLICTS.md's decision 5. The refusal still names the map, on purpose. |
| 56 | The agent's prompt listed the edit verbs without `env`; the verb set was kept by hand in six lists. | 2.0's step 4, first piece (2026-10-12): `REQUESTS[]` in app_ctl.c is the one list; the prompt, `help`, the dispatcher and the off channel read it, and suite `ctltable` holds docs/AGENTS.md to it. Still by hand, each in one place: the README's prose and docs/CONTROL.md (a record). |
| 50 | A terminal that stopped reading blocked the whole process in `term_write`. | 2.0's step 2 (2026-10-12): output through `term_async`'s non-blocking open, a queue of at most a frame, the loop flushing. A live vtt on an unread pty answers `--ctl status` in 0.00 s where it gave none in 2 s (PERFORMANCE.md, *A terminal that stops reading*); suite `loop` has the case. |
| 9 | The main loop (`main.c`) had no test. | 0421c0a and the commit after it (2026-10-12): the loop is `src/loop.c`, suite `loop` drives `loop_once` through a pipe with time as an argument, and step 5's bug has a case (the reviewer's mutation that flushes before the drain fails it). What it does not reach is row 58. |
| 3 | `jobs json` is checked for validity only with one asked job (test_jobs.c:375). Nothing checks it with a ready proposal, a thread of several lines, or a conflict count above 0. | 2.0's step 4, the codes (2026-10-12): a `jobsctl` case reads `jobs json` with a ready proposal, a thread of three lines and one conflict. |
| 4 | No test for `job N say` with the wrong number of words, or `:agent accept` with no word after it. | the same commit: `job N say` with no line and with two words, bare `job N`, and `:agent accept` alone, each pinned in `jobsctl`. |
| 12 | No test for `--apply --wait` when the vtt quits before a verdict (it should exit 2). | the same commit: a `ctllive` case stops the channel while a forked `--apply --wait 20` is held: exit 2. |
| 13 | `holds` is tested with no map open, not with a map never saved (an empty path). | the same commit: `events` asks `holds` with the map's own device and inode, then again with its path cleared. |
| 14 | A plan of about 60-64 KB is taken by `--apply` on a file but refused when a vtt has the map open: the added `propose apply "NAME"` line pushes the request over the 64 KB cap. | the same commit: `--apply` reads at most `CTL_PLAN_CAP` (64,896 bytes: the request cap less the room its header line may take), so a file and an open map take the same plans; a `ctllive` case sends one of exactly that size under a 255-byte name of quotes. |
| 16 | The words the `--apply` client matches in events (`job N accepted`, `scrapped`, `feedback:`) are written twice: where `app_job.c` emits them and in `ctl_verdict_in`. A changed wording breaks `--wait` silently. | the same commit: an event's kind is one word from `ctl.h`'s table, written by `app_event` and read back by `ctl_event_parse`, which is all `ctl_verdict_in` knows; `events` feeds the answers of real emitted events to `ctl_verdict_in`. |
| 60 | `--apply` of a plan that only reads (`dump`, `help`) saves the file all the same: `tests/fixtures/two-rooms.vtt`, 595 bytes at `VTT 1`, comes back 614 bytes at `VTT 3` with `scale` and `metric` lines it did not have. Reproduced (`--apply` of a one-line `dump` on a copy). Older than the table. | the same commit, after reproducing it (595 bytes came back 614): `run_apply` saves only when `gen`, `scenes_gen` or `cards_gen` moved. The oracle prints `file: as it was` or `file: written` after each request; the reviewer tried twenty kinds of edit and five reads by hand. |
| 62 | `vtt --ctl REQUEST` with every vtt's channel off says `no vtt is taking requests - type :agent on in the one you mean` and never shows the refusal that names each map; only `--ctl-pid N` and `--apply` do. Reproduced by the reviewer on a live vtt. | the same commit: `on_each` keeps each off vtt's refusal and `--ctl` prints them, by pid, under its line; a `ctllive` case pins the text. |
| 66 | PERFORMANCE.md's tables were not regenerated for the reads: two rows were added by hand from runs on a machine that was not quiet. | 2026-10-13, the commit after the reads': three quiet runs of `make perf`, their medians published. They found what the hand-added rows could not: `cs_where` cost 11 µs on a room's landing (`job.propose` 37.3 to 48.4 µs), put on the reads' commit by an A/B of five binaries and brought to 3.4 µs (PERFORMANCE.md, *Reads that cost what was asked*). |
| 2 | `box_name` (a `CsBox` to "B2:F6") was defined twice. | the final review found the third copy (app_agent.c): `cs_box_name` in changeset.c, 2026-10-10. |

### Where the rows came from

Thirty-five reviews, 2026-10-07 to 2026-10-13. The first seven predate this section: on 2026-10-09
their reports were read back from the session's record and every item checked against the
code as it stood (137 items: 117 fixed, 4 overtaken when the plan dropped "accept saves", 16
open, none disproved). Steps 4-6 were entered from their reports as they came.

| review | of | items open here |
|---|---|---|
| 2026-10-07 | the questions added to this page | none |
| 2026-10-07 | commit 1edbc8e and the plan's second draft | 14, 17 |
| 2026-10-07 | commit 98c9ec2, the `Machine:` line | 18 |
| 2026-10-07 | the plan's third draft | 8 |
| 2026-10-08 | step 1 | 19-26 |
| 2026-10-08 | step 2 | 27 |
| 2026-10-08 | step 3 | 28-30 |
| 2026-10-09 | step 4 | 1-5 |
| 2026-10-09 | step 5 | 6-9 |
| 2026-10-09 | step 6 | 10-16 |
| 2026-10-10 | step 7 | 31-36 |
| 2026-10-10 | step 8 | 37-42 |
| 2026-10-10 | step 9 and the whole feature (two high, two medium, four low: all fixed but the memory cap) | 43, 44, B12 |
| 2026-10-10 | the CLAUDE.md split (b9ae6bc) | 45 |
| 2026-10-10 | the docs index and the status lines | none (46, closed) |
| 2026-10-10 | PERFORMANCE.md's sub-headings and AGENTS.md's note | none |
| 2026-10-11 | the first draft of docs/VEHICLES.md (a plan; thirteen findings taken into it, one disproved) | none |
| 2026-10-12 | the first drafts of docs/SUMMON.md, CONDITIONS.md and ENVIRONMENTS.md (plans; all seventeen findings taken into them) | none |
| 2026-10-12 | docs/SUMMON.md's second draft (the GM places inside the highlight; all seven findings taken in) | none |
| 2026-10-12 | `:summon` as built (two medium, two low, five test gaps: all fixed) | none |
| 2026-10-12 | conditions as built, and where the SRD's link is kept (two medium, three low, three test gaps: all fixed) | none |
| 2026-10-12 | environment cards, step 1 (one medium: an outside change to the map's environment was lost; five low; four test gaps: all fixed) | none (B11 gained a second input) |
| 2026-10-12 | environment cards, step 2, `:env adversaries` (two medium, four low: fixed but the names' matching) | 47 |
| 2026-10-12 | environment cards, step 3, the import and `:env pick` (two medium, five low: all fixed; the importers' shared shape went to the watch list) | none |
| 2026-10-12 | the three 2.0 reviews of the whole codebase (agent architecture, performance, testing: docs/reviews/; read-only, so nothing fixed; one finding disproved) | 48-57 |
| 2026-10-12 | 2.0's step 0, the oracle (one high: the record's order followed the caller's locale; one medium: `--players` outside play mode dumped the GM's screen; three low; six coverage gaps: all fixed) | none |
| 2026-10-12 | 2.0's step 1, the loop moved to `src/loop.c` (a faithful move; one medium: a case that could not see a draw; three low; five untested paths, four given a case) | 58 |
| 2026-10-12 | 2.0's step 2, the non-blocking terminal (two medium: an allocation inside the fatal handler, a case that passed with the renderer's invariant broken; five low: four fixed) | 59 |
| 2026-10-12 | 2.0's step 4, `REQUESTS[]` and `help` (the dispatch found equivalent; two medium: `help job` offered a form that is refused and hid two, `help` was refused with no map; test gaps and four low: all fixed; one older finding) | 60 |
| 2026-10-12 | 2.0's step 4, the off channel, `seq` and `wait 0` (one high: `--apply --wait` on a vtt with no event yet asked `wait 0 for N` 97,000 times in three seconds, fixed; the file found safe on a live vtt; one medium and three low: two rows, the rest fixed) | 61 |
| 2026-10-12 | 2.0's step 4, the codes on refusals and events (three medium: `wait`'s malformed clause, a seventeenth scene and a link to a void square in another map each had the wrong class; the class word written by hand in six verdicts; a class left set by the search for a free square; test gaps: all fixed but three rows. Rows 3, 4, 12, 13, 14, 16, 60 and 62 closed with it, each checked first) | 63-65 |
| 2026-10-13 | 2.0's step 4, the reads: `dump`'s cap and key, `describe REGION`, `changes` (one high: a change made while no agent had been about for ten minutes was no event, so `changes` answered nothing for a map that had moved; three medium: a region's size was judged by its lattice where creatures and notes made it 2.6 times that, `changes` after another map was opened dumped the new map in the old one's pieces, the refusal's example region was off a short map; four low and the test gaps: all fixed) | none (61 gained a line) |
| 2026-10-13 | `cs_where`'s walk of a small map's cells, after it was committed (a46d3c2: found equal to the old walk over two million sequences in the reviewer's harness; one medium: the suite's random case walked the skip three times in forty rounds and no far boundary, so the clamps could be deleted and it passed; the write-up said one division where there are two: both fixed, and the case now fails with the clamps out) | none |

## 2026-09-28, at 17e31e3

Four read-only audits in parallel -- duplication and organization, performance and
profiling, the test suite, the documentation -- each claim that mattered checked by hand
before it was written down here. Nothing was changed by the check itself.

### 1. Disk flushes are the real cost of saving, and nothing measures them

- `tools/perf.sh` works in `mktemp -d`, which is `/tmp`, which is tmpfs on this machine (the laptop): an
  `fsync` there is free. Maps live on the GM's own filesystem (btrfs under `/home` here).
- Checked by hand: the same load, one edit and save (`--apply` on `crowd.vtt`) is **1 ms on
  tmpfs and 29 ms on btrfs**. The audit measured single `mapio_write` flushes of 45-390 ms
  on btrfs, `link.trip.map` at 66 ms p50 and 345 ms p99 there (111 µs published), and a
  512x512 trip at 12-16 ms even on tmpfs.
- The recovery autosave writes on the main loop, so a slow flush delays the next key; a trip
  flushes twice before the arrival frame is drawn.
- PERFORMANCE.md's autosave table ("fsync included") and its trip paragraph are tmpfs
  figures.
- **Fixed** (the user chose: the autosave skips the flush, everything else keeps it).
  Measured on btrfs first: an autosave-sized write is 0.1-5 ms unflushed against 33-76 ms
  flushed (median of nine, 40×25 to 512×512), so the autosave now writes and renames
  without `fsync` (`mapio_write_unflushed`); a vtt crash still finds it, a power cut within
  half a minute can lose it, the map's own file is never at risk. Review: on ext4 or xfs a
  power cut can leave only the copy's start, cut anywhere, so an autosave now ends with an
  `end` line (the loader stops there) and recovery refuses a copy without it, setting it
  aside as `.autosave.damaged` (the table is `tools/saves.sh`). `:w`, trips and other
  saves flush as before -- a trip stays at about 65 ms, two flushes, accepted as the cost
  of a deliberate map change. `make perf` now works in the repo (`.perf.*`), `mapio.write`,
  `mapio.fsync` and `mapio.load` are zones, and PERFORMANCE.md is regenerated there.

### 2. Test hygiene

- **The sandbox leaks.** `sandbox_leave` (tests/harness.c:174) removes only the data
  directory; 9,306 `/tmp/vtt-*` directories were left by past runs.
- **`make test` does not build `vtt`**, yet `test_apply` runs `./vtt`: a stale release
  binary is tested silently, and with none the suite fails 13 checks (exit 127).
- **`chmod`-based tests** (test_places.c:1398, test_core.c:1779) pass without testing
  anything when run as root; no `geteuid` guard.
- **Undefined behavior gcc hides**: net.c casts `wire_enc_cell` to `RndObserver` (a
  `WireEnc*` for a `void*`); clang's UBSan flags it, and `-fno-sanitize-recover` would stop
  a clang test run.
- **Fixed**: `sandbox_leave` removes the whole sandbox (a run leaves nothing in `/tmp`; it
  left 45); `make test` builds `vtt` first; the read-only-folder trip test is skipped as
  root; `wire_observe_cell` is the observer (net.c and the tests), and `make test CC=clang
  CFLAGS=-fno-sanitize-recover=all` passes. The directories earlier runs left in `/tmp` are
  for the user to delete (`rm -rf /tmp/vtt-*`).

### 3. Test coverage and quality

Measured with clang source coverage: lines 88.6%, functions 94.7%, branches 76.0%. The gaps
are error paths.

| area | branches | not exercised |
|---|---|---|
| net.c | 74% | real backpressure (the compaction and EAGAIN paths), WebSocket close/continuation/bad opcodes/oversized and split frames, 404s, a ninth client, a remote watcher with a wrong code, a handout over the cap |
| mapio.c | 81% | every refusal in the version 12 link branch, a scene cut short by another, an inverted scene box, a seventeenth scene, a clamped scene round |
| main.c | 19% | the map tools' argument errors, `--dump-map`/`--describe` through the binary, `--apply` limits, `--help`; the script parser is static and untested |
| app_ctl.c | 78% | busy refusals for an open prompt, a message modal, a pending prefix; boxed and unknown `scene NAME`; `link ... size`, `link N twoway|secret|seen` |
| watch.c | 0% | all of it; `parse_target` is pure but static |
| web/index.html | -- | `feed()` is a third wire decoder with no behavioral test |

- Never drawn in a test: `ui_modal`/`ui_confirm` (quit, discard, recover, delete), the `:`
  line, the picker through `app_draw`, play mode's visual box.
- Fuzzing: the loader's seeds come from tests/fixtures, which stop at version 9 (no clocks,
  rolls, counters, turns, hidden, scenes, map links); `fuzz_mapio` saves and reloads but
  never compares; the channel corpus has none of the recent lines, and scenes pile up
  across inputs (saving one is not undone).
- Cases that claim more than they check: test_core.c:1773 (a failing delete is never
  pressed), test_net.c:630-656 (a 50 ms sleep, and the close is never checked),
  test_places.c:1221 (diagnostics unchecked), test_play.c:4158 (the picker's presence in
  the GM's frame unchecked).
- Structure: test_play.c is 4,483 lines and 40 suites; scenes are tested in three files and
  handouts in two; CLAUDE.md's tests row is stale; run.c cannot run one suite. **Fixed**: test_play.c split into
  test_play.c, test_keys.c, test_marks.c, test_saved.c; `build/run-tests NAME...` runs
  named suites; the tests row updated. Scenes and handouts are still tested across files
  (the loader, the channel and the keys each beside their kind).
- **Mostly fixed** (8525e48..5c5ffd6): the loader fuzzer compares save, load, save byte for
  byte (it found a name of carriage returns that did not survive; fixed), seeded with a
  version 12 fixture of every record; the channel fuzzer drops its scenes per input and has
  seeds for the new lines; `watch_parse_target` is tested; the v12 link and v11 scene
  refusals and the scene round clamp (`test_loader_damage`); the busy refusals; the page's
  `feed()` run under node against the C decoder (`test_page_feed`, skipped without node);
  the server's 404, wrong code, close frame, ninth client, handout cap and slow watcher
  (`test_net_edges`); the four tests that claimed more now check it; golden frames of the
  `:q` question, the `:` line and play's box. **Still open**: main.c's argument errors and
  script parser, watch.c's loop, WebSocket continuation frames, the recover and delete
  dialogs as frames, and the structure bullet (item 6).

### 4. Documentation: where a user gets stuck

- Where maps live: `:e NAME`, `:w NAME` and New Map use `~/.local/share/vtt/maps`
  (`mapio_resolve_path`), never the current directory; the README does not say so.
- `vtt --watch HOST:PORT` from another machine needs the join code (`HOST:PORT?k=CODE`);
  undocumented, and pasting the `http://` address fails.
- "The selected creature" is never explained; it stays selected when the cursor leaves it,
  so `c`, `s a`, `s v`, `s i`, `s t`, `b`, `r` can act on another creature than the one
  under the cursor (reproduced).
- The `?` page clips key strings at 13 characters with no ellipsis (`:character sa`,
  `:serve --stay`, `:link to cryp` and a dozen more).
- No requirements or install step, while examples after Getting started use a bare `vtt`.
- `:serve` picks a free port, but the example shows 7777 and the firewall advice assumes a
  fixed one.
- Wrong facts: the build bar's `t/T kind` (`T` cycles terrain), `--bench-loops` default 400
  (it is 50), "a creature shows four markers, with more continuing below" (four is the
  most).
- Polish: history and design argument in a few README passages; "round" for "around" and
  "towards"; mixed terms (token/creature, remote view/players' view, trace/wall mode, you/the
  GM); gaps on the `?` page; the README's documentation table lacks CHARACTERS, HANDOUTS,
  MAPLINKS and SCENES; `:roll NAME =` is the one destructive command not spelled `remove`.
- **Fixed** (dd89d71): where maps live, requirements and install, the selected creature,
  `--watch` (which now takes the pasted address), the free `:serve` port, the `?` page (a
  wide key gets its own line), the three wrong facts, "round"/"towards", the documentation
  table, `:roll NAME remove`. **Still open**: the mixed terms (token/creature, remote
  view/players' view, trace/wall mode) and the gaps on the `?` page; no history or design
  argument was found left in the README. "Drawing cost depends on the size of the window"
  becomes true with §5's creature culling.

### 5. Performance and profiling

Wins:
- **Creatures are not culled to the window** (play.c:659-680, token.c:288): 500 creatures on
  a 200x200 map draw in 69 µs against 22 µs for 24, same window, same bytes -- against the
  rule that cost follows the window. About ten lines.
- `--check` W151 loads the other map once per link (235 ms for 64 links to a 512x512 map):
  load each target once.
- The picker filter ranks every item four times (p99 about 0.5 ms over 500 templates): one
  pass.
- Not worth it now: `picker.open` (5 ms for 500, 16 ms near 1,400 templates), map load and
  save speed (the flush dominates), boxed scene restore, `link_land`.

Measurement:
- The frame columns leave out key handling (`run_headless` calls `app_key` outside the
  frame), so a picker open, a trip or an undo trim never shows; nothing measures key to
  bytes.
- Bytes sent to the phones are not published; the pings paragraph quotes the GM terminal's
  bytes as a client's.
- p50/p99 come from the last 256 frames only; the trace stops silently at 200k events (a
  dozen rows pass it, hence short call counts).
- Rows that measure something other than their label: `play, fog, 4 watchers` sends 0
  bytes; `play, handout, 4 watch` is mostly typing the `:handout say` prompt;
  `floors split` sends 18 bytes a frame.
- Zones never fired (`stamp.turn`, `autosave`, `handout.draw`) and paths with none (map
  load, `:w`, `:character save`, the trip's steps, `scene diff`, `link_map_check`, the
  watcher, the stamp and handout pickers).
- About a dozen PERFORMANCE.md paragraphs quote numbers the tables no longer hold.
- **Partly fixed** (item 4): creatures off the window are culled (`grid_visible_tiles` plus a square;
  500 on 200x200 drew in 85µs, now 40µs against 29µs for 24; `test_cull` proves a culled
  one would have drawn nothing, and fails when the margin is removed); new rows `play, 24/500 on 200x200`; the three mislabeled
  rows renamed for what they measure; a full trace is reported and its call counts marked
  `+`; PERFORMANCE.md regenerated. **Still open**: key handling outside the frame columns,
  phone bytes unpublished, p50/p99 from the last 256 frames, zones never fired and paths
  with none, W151's reload per link, the picker's four ranking passes, the dozen stale
  paragraphs.

### 6. Duplication and organization

Three copies or more (the rule of three):
- The saved-things store -- data directory, `dir/NAME.ext`, "count, allocate, list again",
  the file-name rule -- lives in stamp.c but serves stamps, characters and handouts, with a
  fourth data-directory copy in `mapio_default_dir`. **Fixed**: store.c.
- A name followed by a trailing verb (`:area`, `:scene`, `:floor`) parsed three different
  ways; word splitting in `:` commands in three styles. **Fixed**: `str_cut_word` (and `:roll`).
  The word splitting is **left**: `sscanf` widths in `:clock`/`:tick`/`:fog` cut an
  over-long word short where the channel's `split_words` refuses it, so one splitter would
  change what those commands accept; worth doing when one of them is next changed.
- The dialog frame and padded title drawn five times in ui.c, and not the same way. **Fixed**: `dialog_frame`, and `entry_field` for the prompt's and picker's field.
- `.vtt` stems (**fixed**: `str_cut_suffix`, `path_stem`) and "the file beside this map"
  (seven and three sites); reading a whole file
  with a cap (four, with different limits); the free roll slot (three); jumping the cursor
  to a place (three); the link modifier words (the app and the channel); the quoted-name
  validator core (three); "an area name, else a square" into a box (three:
  app_ctl.c `region`, `link_map_check`, `link_land`).

Organization:
- The picker's glue (four lists) lives in app_character.c. **Fixed**: app_picker.c.
- app.c (2,075 lines) holds the map browser and build mode's keys; app_ctl.c (1,781) holds
  the room language and an if-chain of reads. **Fixed**: app_browser.c and app_build.c (app.c 1,394
  lines), corridor.c and app_ctl_marked.c (app_ctl.c 1,330).
- CLAUDE.md's file table is stale in several rows (app_priv.h, the character row, tests)
  and has no rows for util.c, ui.c, main.c, map.c. **Fixed**: every source file has a row.
- The watch list's square/region parsers are still two copies.
- **Open.**

### Proposed order

1. Disk flushes: measure on the real filesystem, then a plan for the autosave and trips.
2. Test hygiene: the sandbox cleanup (and the leftover directories), `make test` building
   `vtt`, the `chmod` guard, the clang cast.
3. The README's "stuck" list and the `?` page truncation.
4. Creature culling; the mislabeled perf rows and harness gaps; PERFORMANCE.md refreshed.
5. Test gaps: net.c edges, v11/v12 loader damage, fuzz seeds, busy refusals, GM-only golden
   frames, a testable watch.c, a node check of the page's decoder.
6. Behavior-neutral refactors, planned: the store module, app_picker.c, one dialog frame,
   word and name helpers, splitting app.c / app_ctl.c / test_play.c, CLAUDE.md.
