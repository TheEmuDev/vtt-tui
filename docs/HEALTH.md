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

## 2026-09-28, at 17e31e3

Four read-only audits in parallel -- duplication and organization, performance and
profiling, the test suite, the documentation -- each claim that mattered checked by hand
before it was written down here. Nothing was changed by the check itself.

### 1. Disk flushes are the real cost of saving, and nothing measures them

- `tools/perf.sh` works in `mktemp -d`, which is `/tmp`, which is tmpfs on this machine: an
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
