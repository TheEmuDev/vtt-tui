# vtt — working notes for Claude

A rules-agnostic virtual tabletop for the terminal, in C11 with no dependencies
beyond libc, POSIX and `-lm`. Tested with Daggerheart. The user is the sole
maintainer; this file is what survives a context reset, so keep it true.

## Standing rules (the user's, not defaults)

- **Plan before refactors.** Anything that reshapes a shared struct, changes a
  shared signature or touches several subsystems gets a written plan with the
  engineering decisions, their tradeoffs and the instrumentation, and waits for
  explicit sign-off. Small additive work goes straight to code.
- **Performance discipline.** Every new drawing or keystroke path gets a
  `PROF_ZONE`, a scenario in `tools/perf.sh`, and a regenerated
  `docs/PERFORMANCE.md`. Bytes written matter more than frame time. Cost follows
  the window, not the map: cull with `grid_visible_tiles` first.
- **Push only when told** ("push it"). Commit freely; never push on your own.
- **A subagent reviews every finished change** before it is reported done; verify
  its findings and fix what holds up. The reviewer is Fable 5.1 for now (Agent
  `model: "fable"`); the user will name a different one when that changes.
- **Rules-agnostic core.** Game-specific behaviour lives behind the `Ruleset`
  table in `ruler.c` (bands, `action_roll`, `spotlight`, `countdown`), documented under the README's
  *Rulesets* section with a subsection per game. Nothing else may know a game.
- **Keys follow the eight rules** in README *Keys → How a key is chosen*. Read
  them before binding anything. The `?` page and the bar both come from
  `src/keys.c`; the bar holds six hints; no duplicate key strings per map.

## Commands

```
make            release build (-O2), profiler compiled in
make test       ASan+UBSan build, unit + golden-frame tests (VTT_UPDATE_GOLDEN=1 regenerates)
make perf       one perf run; publish the per-row MEDIAN of three quiet runs (tools/median.py a b c)
make fuzz       libFuzzer on the map loader (clang), FUZZ_SECONDS=600 for longer
tools/embed.sh  after editing web/index.html; tools/blit_wasm.py after editing the blitter
./vtt map.vtt --serve 7777      serve; a raw client: printf 'VTT1\n' | nc 127.0.0.1 7777
./vtt map --bench keys --bench-clients 4   the frame with four watchers attached
./vtt map.vtt --script keys --dump-frame --size 100x30     render one frame as text
./vtt map.vtt --bench keys --bench-loops 400 --trace t.json  headless timing + Chrome trace
```

Perf runs want the machine to themselves: editing a file during a run lifted rows
by a fifth. When a row moves, A/B the previous commit's binary through the same
scenario before believing it (`git worktree add /tmp/prev HEAD~1 && make -C /tmp/prev`).
Bench scripts replay whole; no toggles — use loop-neutral pairs (`llllhhhh`,
`8a8A`) or absolute keys (`3b`, `2R`, `6r`).

## Where things are

| file | owns |
|---|---|
| `app.c` | lifecycle, screens, prompts (`prompt_accept`), modals, drawing the frame, `app_key` dispatch, `app_note` (status + session log) vs `app_set_status` (status only) |
| `app_play.c` | play-mode keys (`app_play_key`), prefix families `i`/`s`, `retired_key` hints; `s n` notes (build mode has the same key in `app.c`) |
| `app_cmd.c` | every `:` command |
| `app_priv.h` | what those three share; `count_digit`, `take_count` (silence=1), `take_count_raw` (silence=0) |
| `play.c/h` | play state, route search (`trail.path`), groups, the range overlay (`RangeGeom`, shapes) |
| `turn.c/h` | turn order and spotlight: state is `Token.turn`/`Token.init` + `Map.round`/`Map.spotlight`, never a list; `turn_walk` also drives `t`/`f`/`e`; draws the side panel |
| `editor.c` | build mode: brush, visual box/circle, wall trace |
| `undo.c/h` | flat op log, batches, `OP_ROUND`; tokens in a side array; capped at four fills of the largest map |
| `mapio.c` | file format; the writer picks the lowest version that says everything: 3, 4 with a fight, 5 with clocks, named rolls or notes, 6 with counters or fog |
| `clock.c/h` | clocks: fixed slots on the map (`Map.clocks`), `down` per clock, `OP_CLOCK` for ticks only and it carries the slot's `gen` so a reused slot ignores old ops; `:clock`/`:tick` in `app_cmd.c`; drawn under the turn panel |
| `counter.c/h` | counters on creatures: `Token.counters[4]`, `counter_apply` parses the `s v` prompt, `<`/`>` step `Play.counter` (`app_current_counter` falls back to the ruleset's first, `Ruleset.counters`); GM-only via `play_status(gm)`, `turn_draw_panel(counter)` and `app_note_gm`/`app_set_status_gm` (`App.status_gm`) -- every message about a creature's numbers, refusals included, uses one of those two |
| `fog.c/h` | fog of war: `Map.fog` byte per tile (patch id + SEEN/LIT/RIM/HELD), `Map.fog_patches[15]`; `fog_ground_hidden`/`fog_creature_hidden`/`fog_token_hidden` are the questions every view asks; `map_fog_set` is the only writer (grows the patch extent); deleted patches are tombstoned (`dead`) because undo can put their number back. Sight: `fog_recompute` rebuilds LIT/RIM (and SEEN under memory) from player creatures via `sight_blocked` + `dist_tiles`, clearing only `Map.fog_lit` rects; `app_fog_sync` runs it after each `app_key` when `Map.gen` moved, and on open/recover. LIT/RIM are never undo-recorded (`undo_set_fog` strips them) and never saved. Drawn by `grid_draw(..., FogView)`; the players' frame skips hidden ground, walls between hidden tiles (`g_fog_blank`), hidden creatures, the cursor in the dark, and masks names via `turn_status_view`. Soft edge: `fog_rim_shown` (RIM + the patch's or map's `soft_edge`); `seg_fog` in grid.c dims boundaries at the rim (doors become walls, no grid lines); `fog_token_silhouette` + `grid_draw_token_silhouette` draw a hidden creature on the rim in `Theme.dim` with `?` |
| `keys.c` | key tables for the bar and the `?` page |
| autosave (`app.c`) | `map_touch` bumps `Map.gen`; `app_tick`/`app_autosave_due` in main's loop write `path.autosave` after 1.5 s quiet (a failed write still counts as attempted, or the loop spins); `offer_recovery` on open; dropped by save, `:q!`, discard, quit-with-y, delete; renamed with the map; `autosave_on` is set only in the interactive loop |
| `dice.c`, `slog.c` | `:roll` (xoshiro, duality), the session log; named rolls are `Map.rolls`, expanded in `app_cmd.c` |
| `wire.c/h` | the remote view's frame format: runs of cells, palette indices; encoder and incremental decoder shared by server, watcher and tests |
| `net.c/h` | the server: listener, up to 8 clients with fixed buffers, HTTP + WebSocket (SHA-1/base64), raw watcher hello, broadcast from `Net.players`'s observer (the renderer given to `net_start` until `net_players_renderer` is first asked for). `Net.stay` (`:serve --stay-alive`) is the only thing that keeps it alive past `app_close_map` |
| `watch.c` | `vtt --watch host:port`, the read-only terminal mirror; `:mirror` spawns it in `$TERMINAL` |
| `web/index.html` → `src/webpage.c` | the phone page; edit the HTML, run `tools/embed.sh`. Its copy loop is `tools/blit_wasm.py`, a hand-assembled wasm module pasted in as base64 |
| `grid.c`, `token.c`, `draw.c`, `render.c`, `term.c` | drawing down to the diffing renderer and the terminal |
| `tests/run.c` | one file, suites in a table at the bottom; `tests/fuzz_mapio.c` is libFuzzer only |

## Writing tests

- `press(&a, "keys")` feeds bytes through the real parser; `\r` is enter, `\x1b`
  esc, `\025` ctrl-u (clears a prompt). `Key k = { KEY_ENTER, 0, 0 }` for raw keys.
- App tests: `Sandbox sb = sandbox_enter("name")`, `write_map_file(sb.dir, "fight.vtt")`
  (a **2×2 empty map** — place tokens with `ipAria\r` after setting `a.ed.cx/cy`),
  `app_open_map`, `Key f2 = {KEY_F2,0,0}` for play mode, `sandbox_leave(&sb)`.
- `play_focus` moves the selection, not the cursor; `enter` acts on the cursor.
- Rendered cells: `rnd_begin(&r); app_draw(&a);` then read `r.back[]`; `rnd_dump` for text.
- Checks are `CHECK` / `CHECK_EQ` under a `CASE("...")`; failures print `FAIL file:line`.
- Golden frames live under `tests/`; regenerate only when the change is intended.

## Conventions

- Status messages: `app_note` for things that *happened* (logged), `app_set_status`
  for hints and errors. Anything that puts a map down goes through `app_close_map`;
  anything that might discard work asks via `app_leave_map_for` (which `:e` uses).
- A prompt whose answer lands in a fixed field sets `a->prompt.max` to the field size.
- Every frame goes through `app_frame` (main, the bench and the net tests alike): the
  GM's view to the terminal, then the players' frame to the clients from `Net.players`,
  drawn with `app_draw_view(a, VIEW_PLAYERS)` when `app_view_differs` says the two
  could differ, else copied from the GM's back buffer. `a->view` is what is being drawn;
  GM-only things (modals, prompts, the profiler, the `(note)` hint, counters, a status
  message set with `app_note_gm`) check it. Add any
  new GM-only thing to `app_view_differs` too, or it reaches the phones. Build mode is
  the GM's alone.
- Ideas consciously set aside live in `docs/IDEAS.md` with the reason (the Fear pool).
- Distances print through `dist_fmt`; coordinates through `map_coord_name`.
- `range_clear` resets the overlay; `range_off` switches it off and keeps the shape.
- The `Cell` padding must stay zero (row memcmp in the renderer); tokens compare
  with `token_equal`, never `memcmp`.
- Comments explain *why*; code says what. Rationale lives in the header or the README,
  not repeated at every call site.
- Never `pkill -f <pattern>` from a Bash call whose own command line contains the
  pattern: it kills the shell running it (exit 144). Record PIDs instead.
- To drive a live `vtt` from a script: `mkfifo f; (sleep 3600 > f &); script -qfc "./vtt map --serve 7792" /dev/null < f &`
  then `printf 'l' > f`. Chrome (via the claude-in-chrome skill) can open the served page.
- Environment quirks: `grep` is aliased oddly in this shell (use `awk` or plain
  `grep -n` in a subshell); `ESC` + letter in a script means Alt; scratch files go in
  the session scratchpad, not the repo.

## Docs to keep in step

`README.md` (keys tables, feature sections, *Rulesets*, *File format*),
`docs/PERFORMANCE.md` (tables + a paragraph per finding), `src/keys.c` (the `?`
page). A feature is not done until all three agree with the code.
