# Health checks

What each project health check found, dated, with the evidence and what was decided. A
finding stays here until it is fixed (then its entry says by which commit) or consciously
left (then it moves to CLAUDE.md's watch list with the point at which it stops being cheap
to ignore).

## The questions

A full health check asks these of the whole code. Every review asks them too, of the change
and the code it touches, alongside correctness. Check each answer by hand before it is
written down.

**Duplication**
- Is the same logic written out more than once? Two copies are allowed. A third copy is
  extracted into one function before it is written (the rule of three).
- Does the change add a copy of something on CLAUDE.md's watch list? Then that item is due.

**Organization**
- Does the code live in the file that owns it? CLAUDE.md's file table answers this.
- Has a file grown too big to read as one subject? Split it along a seam, as app_browser.c
  and app_build.c came out of app.c.
- Does every source file still have a row in CLAUDE.md's file table, and is that row true?

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
- Are CLAUDE.md, docs/KEYS.md (and the `?` page from `src/keys.c`) and the feature's
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
| 2 | step 4 | `box_name` (a `CsBox` to "B2:F6") is defined twice: `app_job.c:89`, `app_ctl_job.c:14`. | duplication | Two copies, within the rule of three. | a third caller appears: it becomes one function in `app_priv.h` | 2026-10-09 |
| 3 | step 4 | `jobs json` is checked for validity only with one asked job (test_jobs.c:375). Nothing checks it with a ready proposal, a thread of several lines, or a conflict count above 0. | test gap | Not written. | a `jobsctl` case runs `json_valid` on `jobs json` after a proposal and a conflicting GM edit | 2026-10-09 |
| 4 | step 4 | No test for `job N say` with the wrong number of words, or `:agent accept` with no word after it. | test gap | Not written. | two error-path cases in `jobsctl` | 2026-10-09 |
| 5 | step 4 | A proposal that came in under `:agent accept auto` while the GM was busy still lands at the next key after the GM types `:agent accept review`. | behavior | Kept: "at once" is decided when a proposal arrives, and is documented so in CONTROL.md. Arguable; the reviewer said so too. | the user decides it should follow the setting at landing time | 2026-10-09 |
| 6 | step 5 (93e9a1c) | The "map closed" event when the GM travels through a link to another map (`app.c:196`) has no test. Closing a map is tested; traveling is not. | test gap | Needs two map files and a party to travel; skipped as "hard". | a case in `events` (or test_places.c's trip tests) checks the event after `g o` | 2026-10-09 |
| 7 | step 5 | The plan says the event reads `the GM changed: ...` (CONFLICTS.md:455); the code and AGENTS.md say `map changed: ...`. Progress does not list the difference. | docs | Overlooked. | the plan's line is corrected, or Progress lists it under "changed from the plan" | 2026-10-09 |
| 8 | step 5; first raised by the review of the plan's third draft | `ctl.wait`, the zone round answering held waits, has no perf row: the bench has no socket to hold a wait on. | performance | Stood in for by a manual check against a live vtt (1 ms from key to answer, no CPU while held), written out in CONFLICTS.md *Progress*. | the bench gains a client that holds a wait, or a test times the flush | 2026-10-09 |
| 9 | step 5 | The main loop (`main.c`) has no test. Its bug in step 5 (keys handled in the drain never reached the flush) was found only by driving a live vtt. | test gap | The loop needs a terminal; every test calls `app_key`/`app_tick` directly. | the loop's body becomes a function a test can call with a fake terminal | 2026-10-09 |
| 10 | step 6 (f306906); step 7's review: opening now asks first, so it is rarer | Two vtts holding one file: the proposal goes to the first found, and stderr says how many others. The others' GMs are not told. No test covers it. | behavior, test gap | Step 7 makes it rare ("opening a map another vtt has open asks first"); the warning was the cheap part. | step 7 lands; a test with two listening apps on one file checks the warning | 2026-10-09 |
| 11 | step 6 | No test that `vtt --ctl` picks the one vtt with `agent on` when two are listening. | test gap | Needs two apps listening in one test. | a `ctllive` case with a second `Ctl` | 2026-10-09 |
| 12 | step 6 | No test for `--apply --wait` when the vtt quits before a verdict (it should exit 2). | test gap | Not written. | a `ctllive` case stops the channel while a forked `--wait` is held | 2026-10-09 |
| 13 | step 6 | `holds` is tested with no map open, not with a map never saved (an empty path). | test gap | Not written; the code checks `m->path[0]`. | one line in `events` or `ctllive` after clearing `path` | 2026-10-09 |
| 14 | step 6; first raised by the review of the plan's second draft | A plan of about 60-64 KB is taken by `--apply` on a file but refused when a vtt has the map open: the added `propose apply "NAME"` line pushes the request over the 64 KB cap. | behavior | Documented in AGENTS.md ("at most 64 KB less a line") instead of fixed. | `--apply` reads at most the cap less the header, so both roads take the same plans; or the server allows the header on top | 2026-10-09 |
| 15 | step 6 | With the channel off, a caller still learns the map's name, size and file (`status`), and what a plan's own lines say back: that a creature, area or scene it names is not there, a square is off the map, and a proposal's summary. | security boundary | Same user only (a `0700` directory), and decision 5 accepts proposals with the channel off, which cannot be checked without saying why they fail. The README now says what is given out. | the user wants off to mean silent: errors to an off channel become "refused" with no reason | 2026-10-09 |
| 16 | step 6 | The words the `--apply` client matches in events (`job N accepted`, `scrapped`, `feedback:`) are written twice: where `app_job.c` emits them and in `ctl_verdict_in`. A changed wording breaks `--wait` silently. | duplication | A test pins `ctl_verdict_in` to today's wording, and the `events` suite pins the emitters; nothing ties the two. | the event words become shared constants, or one test feeds real emitted events to `ctl_verdict_in` | 2026-10-09 |
| ~~17~~ | *closed, below* | | | | | |
| 18 | commit 98c9ec2 (the `Machine:` line, 2026-10-07) | The header table's laptop row has no RAM or top frequency, and the speed-of-light figures exist only for the desktop. | docs | Only the user can run `tools/machine.sh` and the measurements on the laptop. | they are run there and pasted in | 2026-10-09 |
| 19 | step 1 (fdadbce) | The plan still promises `"link 3, now 5" in the summary` (CONFLICTS.md:355). The code went the other way: a new link is summarized without a number (`changeset.c:825`, pinned by test_changeset.c:612), and Progress mentions only the preview. | docs | The fix chose not to number it; the plan's sentence was not corrected. | CONFLICTS.md:355 is corrected, or Progress lists it under "changed from the plan" | 2026-10-09 |
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
| B5 | the audit of earlier reviews, 2026-10-09 | CONFLICTS.md:719 still says "the checkpoint in map.c"; it is checkpoint.c. | Stale since step 2. | the line is corrected (step 9's doc pass) | 2026-10-09 |
| B6 | the audit | PERFORMANCE.md's Proposals floor table quotes 4.9 µs, 3.1 µs, 2.4 ms and 3.5 ms where the table above it now has 4.7, 2.9, 2.2 ms and 4.07 ms. | The upper table was re-measured; the prose under it was not. | the floor table is rewritten from the current rows | 2026-10-09 |
| B7 | the audit | `checkpoint_note_ops` takes a `forward` argument it no longer uses (`checkpoint.c:29`). | Dead since step 2's fix. | the parameter goes, with its two callers | 2026-10-09 |
| B8 | the audit | docs/KEYS.md does not mention the review mode. | The plan puts it in step 9's doc pass. | step 9 | 2026-10-09 |
| B9 | step 7, PERFORMANCE.md *Known gaps* | The map writer is 1.18 ms for the largest map against about 0.5: the text grows in a memory stream (a reallocation and a copy each doubling) and each row is a locked `fwrite`. | Left after the larger gap closed. | the writer sizes one buffer from the map and fills it | 2026-10-10 |
| B10 | step 7, PERFORMANCE.md *Known gaps* | The loader finds a square's kind by searching a table a character (`tile_from_file_char`, `edge_from_file_char`) and reads a line at a time: 3.4 ns a byte, 2.7 ms for the largest map. An outside change pays it twice. | It predates the step; measured here because step 7 leans on it. | a 256-entry table each way, and the rows parsed in place | 2026-10-10 |
| B11 | step 7: `make fuzz` (the loader), 2026-10-10 | **A bug, not from step 7** (the commit before it fails the same input): a card whose text starts with a line holding only a control character saves with two leading blank lines (`\| ` and `\|`), and loading that file drops them, so the map saved, loaded and saved again is not the same bytes. The fuzzer's round-trip check aborts on it. The input is kept: `tests/fuzz-found/card-leading-blank.vtt`. | Outside step 7 (cards, card.c's cleaning and the loader's `card_take`); found while fuzzing the loader this step changed. To be fixed with the review items. | the card's text is normalized the same way when it is read as when it is set (leading blank lines dropped after control characters are cleaned), the input joins the fixtures, and `make fuzz` passes on it | 2026-10-10 |

### Disproved

None. (One finding was first set aside unchecked and was right: step 6's "the tests reach the
user's real socket directory". It is fixed, 3535bb8, and is why the rules above exist.)

### Closed

Rows move here when fixed, with the commit.

| # | item | closed by |
|---|---|---|
| 17 | The map writer built its text one `fputc` at a time. | step 7 (2026-10-10): rows built whole from a table, the file written once; 3.26 ms to 1.18 ms unflushed on 512×512. What is left of the gap is B9. |

### Where the rows came from

Ten reviews, 2026-10-07 to 2026-10-09. The first seven predate this section: on 2026-10-09
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
