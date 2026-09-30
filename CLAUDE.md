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
- **Model roles** (set 2026-09-26): **Opus 5.5** plans -- the plan goes to
  the user for sign-off -- and implements; a **Fable 5.1** subagent (Agent
  `model: "fable"`) reviews every finished change before it is reported
  done -- verify its findings and fix what holds up. The user will name
  different models when that changes.
- **Every review includes the health check's questions** (the user, 2026-09-29), for the
  change and what it touches, beside correctness: duplication (the rule of three -- a
  third copy is extracted), organization (the right file, a file grown too big),
  performance (a new path has a `PROF_ZONE` and a perf row; the window, not the map, sets
  the cost; stale PERFORMANCE.md numbers), test gaps (would a test fail if the behavior
  broke? error paths, the players' frame), and the docs (does the README tell a user how to
  use it; CLAUDE.md, KEYS.md, the design doc in step). docs/HEALTH.md is the full list.
- **Rules-agnostic core.** Game-specific behavior lives behind the `Ruleset`
  table in `ruler.c` (bands, `action_roll`, `spotlight`, `countdown`), documented under the README's
  *Rulesets* section with a subsection per game. Nothing else may know a game.
- **Keys follow the nine rules** in docs/KEYS.md (the ninth: `off` switches off, `remove` destroys). Read
  them before binding anything. The `?` page and the bar both come from
  `src/keys.c`; the bar holds six hints; no duplicate key strings per map.

## Commands

```
make            release build (-O2), profiler compiled in
make test       ASan+UBSan build, unit + golden-frame tests (VTT_UPDATE_GOLDEN=1 regenerates; VTT_REQUIRE_NODE=1 fails rather than skips the page decoder test)
make perf       one perf run; publish the per-row MEDIAN of three quiet runs (tools/median.py a b c)
./vtt map.vtt --dump-map [--region B2:K12]      the whole map as text in the file's alphabet
./vtt map.vtt --describe [--json]    rooms (rooms_build: every door splits), doors, contents
./vtt map.vtt --check [--json]       lint: exit 0/1/2; codes in README; tests/fixtures/broken.vtt has one of each
./vtt map.vtt --agent; ./vtt --ctl 'status'   the control channel (docs/CONTROL.md); requests on stdin with no argument
./vtt new.vtt --apply plan.txt --new 40x30     the same requests headless, saved (docs/AGENTS.md is the agent's guide)
tools/sight.sh  fog.sight per fog scenario (the zone table keeps only each zone's worst)
tools/saves.sh  a save flushed and unflushed, three map sizes, on the real disk (PERFORMANCE.md's save table)
VTT_FOGDIFF_OPS=36000 ./build/run-tests   the long run of the fog differential test
make fuzz       libFuzzer on the map loader (clang), FUZZ_SECONDS=600 for longer
make fuzz-ctl   libFuzzer on control-channel requests (tests/fuzz_ctl.c, corpus tests/fuzz-ctl)
make fuzz-json  libFuzzer on the JSON reader (tests/fuzz_json.c, corpus tests/fuzz-json)
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
| `app_draw.c` | the frame: every screen, modals, the bars, `app_draw_view`, `app_view_differs` (the privacy boundary), `app_frame`, ping and agent-ring drawing (`ping_visible`) |
| `app.c` | lifecycle, screens, prompts (`prompt_accept`), modals' keys, `app_key` dispatch, `app_note` (status + session log) vs `app_set_status` (status only); pings (`App.pings`, `app_ping`/`app_ping_cell`, drained and expired in `app_tick`, `app_ping_due` feeds the poll timeout, drawn after `play_draw` with `grid_draw_tile_ring`, only round visible squares in the players' frame over fog); `App.pinged` keeps each source's last ping after its ring comes down (`until_ms` there is when it was made) for the channel's `marked`, cleared with the map |
| `app_browser.c` | the menu and the map browser (`app_menu_key`, `app_browser_key`), and the file operations they and the prompts ask for: rename, duplicate, delete (each with the `.autosave`/`.autosave.damaged` copy), `app_refresh_entries` |
| `app_build.c` | build mode's keys: `app_editor_key` (normal mode) and wall (trace) mode's |
| `app_play.c` | play-mode keys (`app_play_key`), prefix families `i`/`s`, `retired_key` hints; `s n` notes (build mode has the same key in `app.c`) |
| `app_cmd.c` | every `:` command: the `COMMANDS` table (name, second name, `cmd_*` handler taking the verb and the rest of the line); a word not in it may be a square to jump to |
| `app_priv.h` | what the app_*.c files share; `count_digit`, `take_count` (silence=1), `take_count_raw` (silence=0) |
| `play.c/h` | play state, route search (`trail.path`), groups, the range overlay (`RangeGeom`, shapes) |
| `turn.c/h` | turn order and spotlight: state is `Token.turn`/`Token.init` + `Map.round`/`Map.spotlight`, never a list; `turn_walk` also drives `t`/`f`/`e`; draws the side panel |
| `editor.c` | build mode: brush, visual box/circle, wall trace |
| `undo.c/h` | flat op log, batches, `OP_ROUND`; tokens in a side array; capped at four fills of the largest map. `OP_NOTE` (`undo_set_note`) carries a square note's text before/after in two token slots' `.note`. `undo_abort` rolls back and forgets the open batch. `Undo.stamp` moves on every record/undo/redo/clear. Batches nest (`Undo.nest`): only the outermost `undo_end` closes, so any helper may open its own inside an operation. Wall mode's pen-down run is a **stroke** (`undo_stroke` each step, `undo_stroke_end` when the pen lifts or the tool changes); an `undo_begin` mid-stroke ends the stroke first. Between keys `undo_balanced` must hold -- the tests' `press()` checks it after every key |
| `mapio.c` | file format; the writer picks the lowest version that says everything: 3, 4 with a fight, 5 with clocks, named rolls or notes, 6 with counters or fog, 7 with areas, 8 with links, 9 with floors, 10 with hidden creatures, 11 with scenes, 12 with links to other maps (a `scene "Name" [box]` ... `endscene` block; the loader swaps the scene's `TokenList` in for the map's while it reads one, so every creature parser serves both; W025). The loader forgives damage silently; `mapio_load_diag` reports each forgiveness (stable codes, 1-based line) to a sink without changing what loads -- every new silent-drop site must call `diag()` |
| `clock.c/h` | clocks: fixed slots on the map (`Map.clocks`), `down` per clock, `OP_CLOCK` for ticks only and it carries the slot's `gen` so a reused slot ignores old ops; `:clock`/`:tick` in `app_cmd.c`; drawn under the turn panel |
| `counter.c/h` | counters on creatures: `Token.counters[4]`, `counter_apply` parses the `s v` prompt, `<`/`>` step `Play.counter` (`app_current_counter` falls back to the ruleset's first, `Ruleset.counters`); GM-only via `play_status(gm)`, `turn_draw_panel(counter)` and `app_note_gm`/`app_set_status_gm` (`App.status_gm`) -- every message about a creature's numbers, refusals included, uses one of those two |
| `fog.c/h` | fog of war: `Map.fog` byte per tile (patch id + SEEN/LIT/RIM/HELD), `Map.fog_patches[15]`; `fog_ground_hidden`/`fog_creature_hidden` are the questions every view asks of ground; of a creature it is `fog_token_unseen` (fog, or `Token.hidden`), never `fog_token_hidden` alone; `map_fog_set` is the only writer (grows the patch extent); deleted patches are tombstoned (`dead`) because undo can put their number back. Sight: `fog_recompute` works LIT/RIM (and SEEN under memory) out from player creatures via `sight_blocked` + `dist_tiles`, caching each creature's own light in `Map.sight` (`SightEntry.vis`: NO/YES/ASK, ASK settled lazily); a keystroke proven to be only moves (gen moved by k, exactly k creatures elsewhere, nothing else sight reads changed -- see the comment at `map_touch`) relights only around the movers, anything else rebuilds all. `app_fog_sync` runs it after each `app_key` when `Map.gen` != `Map.sight.gen`, and on open/recover. `fogdiff` in tests checks every bit against a brute force after random ops; `fog_sight_counts` says which path ran. Every line-of-sight question -- fog, ruler, range -- goes through `sight_walk` (ruler.h), one inline loop with an opacity callback. LIT/RIM are never undo-recorded (`undo_set_fog` strips them) and never saved. Drawn by `grid_draw(..., FogView)`; the players' frame skips hidden ground, walls between hidden tiles (`g_fog_blank`), hidden creatures, the cursor in the dark, and masks names via `turn_status_view`. Soft edge: `fog_rim_shown` (RIM + the patch's or map's `soft_edge`); `seg_fog` in grid.c dims boundaries at the rim (doors become walls, no grid lines); `fog_token_silhouette` + `grid_draw_token_silhouette` draw a hidden creature on the rim as a `Theme.dim` square with `?`; turn.c `side_style` grays `?` rows |
| `maptools.c` | headless map tools behind `--dump-map` (and `--check`, `--describe`): read a `Map`, write text, change nothing; `run_tool` in main.c dispatches before any terminal or profiler |
| `ctl.c/h` | the control channel's socket: `$XDG_RUNTIME_DIR/vtt/<pid>.sock` in a `0700` dir it refuses if not ours alone, 4 connections, one request each (read to EOF, 64 KB cap), answered without blocking, 10 s deadline (`ctl_due` feeds poll); `ctl_client_main` is `vtt --ctl` (checks the dir is ours alone too -- `/tmp/vtt-<uid>` could be anyone's; finds the one live socket, removes stale ones). The server binds `<pid>.new` and renames into place after listen; a request over the cap is drained to EOF before the answer, or the close resets it. Moves bytes only |
| `store.c/h` | the folders of saved things under the data directory: `store_dir(SUB)` (maps, stamps, characters, handouts), `store_name_ok` (the one file-name rule), `store_path`, `store_list`/`store_list_all` (sorted NAME.ext listings) |
| `stamp.c/h` | stamps: a stamp is a small `Map` (no fog); `stamp_copy` (creatures wholly inside, made fresh), `stamp_turned`/`stamp_mirrored` (one `transform` over tiles, both edge arrays, tokens, notes), `stamp_place` (checked first, then one nesting batch; void and EDGE_NONE are see-through), files in `$XDG_DATA_HOME/vtt/stamps/NAME.vtt`, and the preview `stamp_show`/`stamp_unshow`: swapped straight into the arrays for one draw and back (no touch, no gen, no TokenList.shape) |
| `app_stamp.c` | build mode's `y`, `p`, `ED_STAMP` keys (`r`/`R` turn, `\|` mirror, `p`/enter place, esc), `:stamp`; `App.stamp` is the one in hand, kept across maps; the preview is drawn in app_draw.c round `ed_draw` |
| `character.c/h`, `app_character.c` | character templates (docs/CHARACTERS.md): a template is a map file in `$XDG_DATA_HOME/vtt/characters` holding one creature at its corner (the size square) and the rolls saved with it; `character_save` keeps it fresh (counters full, no markers/turn/hidden, label without its copy number via `token_label_root`), `character_load` refuses anything but one creature, `character_place` (checked with `play_can_place`, one nesting batch, labels numbered like pastes, missing rolls added through `OP_ROLL` = `undo_set_roll`, so `u` and the channel's rollback take them back). `app_character.c`: `:character`, the `i t p`/`i t e` prefix (`App.pending == PENDING_IT`), and the picker's glue in app_picker.c for all four lists -- characters, stamps, scenes, handouts (`app_pick_open`, `MODAL_PICKER`, `App.picker`/`pick_what`/`pick_kind`); choosing a character calls `app_character_place`; `:stamp` with no name opens the stamp picker. The picker itself is `UiPicker` in ui.c (ranked filter, tab fills and cycles without narrowing); it is a modal, so the players' frame never has it. Channel: `token add ... from NAME [hidden]`, `characters` |
| `scene.c/h`, `app_scene.c` | scenes (docs/SCENES.md): `Map.scenes[16]` (`Scene` in map.h: name, optional box, round, spotlight, its own `TokenList`, freed by `map_free`, trimmed by `map_resize`); `scene_save` (not undoable, like named rolls), `scene_remove`, `scene_restore` (checked first, then one nesting batch: removes every creature or those meeting the box, adds the scene's; a whole scene sets round/spotlight, a boxed one keeps an outside actor's turn), `scene_diff` (by label, unlabeled by side+square). `app_scene.c`: `:scene`/`:scenes`, `app_scene_restore` clears selection, group, range; every message `app_note_gm`. `:scene` alone is the picker (`PICK_SCENE`). Channel: `scene NAME` is an edit; `scene save`/`scene NAME remove` are `lonely` (alone in a request, like `undo`); `scene diff`, `scenes` are reads |
| `app_ctl.c` | what a request says (docs/CONTROL.md): `app_ctl_exec` splits lines into words and runs them, verdict line (`ok`/`error: line N: ...`) first; `app_ctl_busy` is why an edit would be refused (incl. `undo.open`: a wall stroke); edits (`edit_line`, `token_line`) open one batch at the first edit line, nest helpers' batches in it (`room`/`wall` use `ed_wall_shape`); `token del` refuses the creature holding the turn -- the fight is the GM's, `undo_abort` on any error, cap `CTL_OPS_MAX` ops; `finish_edits` sets `App.ctl_stamp`/`ctl_gen`/`ctl_undoable` (the agent's `undo`, alone in its request: `undo_alone`), the GM-only status and `App.agent_ring`; `marked` (`gather_marked` once, then text or JSON); `:agent`. The room language: `room_box` (every room form, relative placement), `area`, `door` by side, `corridor` (`straight`/`bent` plan legs and end faces, `corridor_clear` refuses ground and other areas, `dig` walls every face out and puts the kind on the ends), `spot` (an area name where a creature goes: nearest free square to its middle), area names accepted by `region`. `--apply` in main.c runs `app_ctl_exec` headless and saves. `app_tick` hands it each request `ctl_next` has ready |
| `app_ctl_marked.c` | the channel's `marked` read (the cursor, a `v` box, wall mode's corner, the ruler, pings), gathered once into `Marked` and written as text or JSON |
| `corridor.c/h` | the room language's `corridor A B`: `corridor_plan` (straight when the rooms share enough rows or columns, else one bend, across first then down first; through void and no other named area) and `corridor_dig` (through the undo log). Needs only the map and the log, not the app |
| areas (`map.c`) | `Map.areas[64]`: a name on a box, nothing drawn; `map_area_set/find/at/remove`, names never squares (`map_area_name_ok`); `OP_AREA` (`undo_set_area`/`undo_remove_area`, two token slots); file version 7; `:area`/`:areas` in app_cmd.c (GM-only messages; `Editor.cmd_from_visual` lets `:area` see the v box); `describe`/`dump`/`marked` use them |
| `link.c/h`, `app_link.c` | links: `Map.links[64]` (the `Link` type is in map.h), kept in number order, a number kept for life (`link_free_num`); each end a 1-3 square block; `link_problem` is the one test of where a link may go when one is made; the loader holds a link only to `link_misplaced` (shape, map edge, overlaps -- `W023` drops one) because ground can leave from under an end later, which stays and `check` reports (`W150`); `link_trip` plans g o (everyone meeting the near end, one shift, all or nothing); `OP_LINK` carries the whole `Link` in a token slot's `.note`; file version 8. `app_link.c`: build `g l` (`Editor.link_on`, first end then second; the brush is the size), play `g o` is `link_here` in app_play.c (the cursor's square or the creature under the cursor, never the selection; puts a carried creature down first); `app_view_differs` counts a secret link under the cursor (the GM's status line names it), `:link`/`:links` (GM-only messages; a trip over a secret link too). Drawn by `grid_draw_links` (reveal = build only). Stamps carry links wholly inside; `rooms_build` reach follows links (one-way only onward); channel `link`/`links` in app_ctl.c (`link_spot` puts an end in a room) |
| links to other maps (`link.c`, `app_link.c`) | docs/MAPLINKS.md: `Link.to_map`/`to_place` ("" for an ordinary link); one end here, the second kept equal to the first so a loop over two ends is harmless, `link_ends` where it matters (overlap, drawing, describe). `Link` is over 64 bytes now: undo packs it into a token slot's `.note` and `.label`. `link_map_path` (the file beside this one), `link_map_check` (reads it: the place an area or a square on ground; `:link to`, the channel's `link A to MAP PLACE`, `--check` W151), `link_land` (formation offsets, the nearest fit inside the area or by the square). `g o` on one is `travel` in app_link.c: checked whole (the map saved before, not this map, no pending autosave there, room), the party removed and this map saved, then `app_travel_to` (app.c) swaps maps keeping the server, the log and the play settings; arrivals lose their turn place; a handout comes down |
| `floor.c/h`, `app_floor.c` | floors (docs/FLOORS.md): an area with `Area.floor` + `Area.level`; **every floor question goes through floor.c** (`floor_at`, `floor_order`/`floor_step`, `floor_problem`, `floor_box`, `floor_pick` -- the one tie-breaker: stay, most, last moved, lowest) so stacked layers can replace the storage later. `map_area_at` skips floors. File version 9 (`floor "Name" LEVEL`, W024), W160 overlap. The view: `Editor.floor` (by name) -> `GridView.bounded/bx0..by1`; `grid_bounds`, `grid_clip_push` (every map draw goes through it), camera/visible/tap all bounded; `app_floor_sync` after every key and before the GM's draw follows the cursor onto another floor; `app_floor_show` keeps the cursor's place in the box; `[` `]` global in app.c. Players: `app_players_floor` (pin, acting player, GM's turn stays, else `floor_pick`), `App.pview` their own camera when `app_players_split` (drawn in app_draw.c's `draw_editor` swap, status line blank, taps via pview), `app_floor_note_move` feeds the tie-breaker; `app_floor_spotlight` on a spotlight flip |
| handouts (`app_handout.c`) | docs/HANDOUTS.md: `App.handout_title/body/up` (kept for `:handout on`), files `$XDG_DATA_HOME/vtt/handouts/NAME.txt` (UTF-8 checked with `utf8_valid`, 2 KB), `:handout NAME|say TEXT|off|on`, the picker (`PICK_HANDOUT`, `store_list_all`). The server holds its own copy (`Net.handout`, kept across `net_stop`) and sends a `'H'` record (wire.h) on change and after a joiner's FULL; the page draws an HTML card, the watcher and `:player preview` draw `ui_handout_draw`. The title bar says HANDOUT in both views; `app_view_differs` is true under preview while one is up, so the card is never copied into the phones' frame. Since 2026-09-27 `app_frame` switches `Net.rnd` to the players' renderer from the first frame (blank at `net_start`) -- before, a phone joining first was sent the GM's screen; `Net.joined` asks for a redraw |
| `json.c/h` | the JSON writer the map tools and the channel share: write only, escaped, no parser |
| `keys.c` | key tables for the bar and the `?` page |
| autosave (`app.c`) | `map_touch` bumps `Map.gen`; `app_tick`/`app_autosave_due` in main's loop write `path.autosave` after 1.5 s quiet, unflushed (`mapio_write_unflushed`: the fsync is the whole cost on a real disk; every other save flushes; it ends with an `end` line, `autosave_whole` requires it, a copy without it is renamed `.autosave.damaged`) (a failed write still counts as attempted, or the loop spins); `offer_recovery` on open; dropped by save, `:q!`, discard, quit-with-y, delete; renamed with the map; `autosave_on` is set only in the interactive loop |
| `dice.c`, `slog.c` | `:roll` (xoshiro, duality), the session log; named rolls are `Map.rolls`, expanded in `app_cmd.c` |
| `wire.c/h` | the remote view's frame format: runs of cells, palette indices; encoder and incremental decoder shared by server, watcher and tests |
| `net.c/h` | the server: listener, up to 8 clients with fixed buffers, HTTP + WebSocket (SHA-1/base64), raw watcher hello, broadcast from `Net.players`'s observer (the renderer given to `net_start` until `net_players_renderer` is first asked for). `Net.stay` (`:serve --stay-alive`) is the only thing that keeps it alive past `app_close_map`. Upstream: `net_msg` reads `P col row` (a WebSocket text/binary frame, or a line after a watcher's hello), one a second per client (`NetClient.next_ping_ms`), into a fixed per-client inbox drained by `net_take_pings`; bad payloads are counted (`bad_msgs`) and ignored, bad framing closes. "Keep-alive" (`NET_KEEPALIVE_MS`) is a WebSocket ping (op 9) to a browser silent that long (`NetClient.probed_ms`; the pong, like any bytes, stamps `last_rx_ms` in `client_read`) and a 'Z' record (`WireSink.keepalive`) to a watcher whose line is quiet; `NET_IDLE_MS` drops silent browsers (`idle_dropped`) and unfinished requests or hellos, never a greeted watcher. `client_flush` returns -1 when it closed the client: a close shifts the next client into the slot, so every caller checks. "Ping" is a tap |
| `watch.c` | `vtt --watch host:port`, the read-only terminal mirror; `:mirror` spawns it in `$TERMINAL` |
| `web/index.html` → `src/webpage.c` | the phone page; edit the HTML, run `tools/embed.sh` (it drops block comments: the 12 KB budget is what is sent). Its copy loop is `tools/blit_wasm.py`, a hand-assembled wasm module pasted in as base64 |
| `map.c/h` | the `Map`: tiles, both edge arrays, tokens, areas, scenes, links, clocks, rolls, fog; `map_touch` (bumps `Map.gen`), `map_coord_name` ("C3"), `map_region_name` ("C3" or "B2:F6"), areas' functions |
| `ui.c/h` | the widgets every screen draws with: the key bar and `?` page (`ui_keypage`, a wide key on a line of its own), the title bar, lists, `TextPrompt` and the `:` line, `UiPicker`, the handout card, the dialogs (`ui_modal`, `ui_confirm`, `ui_choice`), all framed by `dialog_frame` |
| `main.c` | options (`parse_args`), and the four ways to run: interactive, headless (`--bench`, `--dump-frame`, `--script`), the map tools, `--apply`; the interactive loop's poll timeout is every due thing's (autosave, pings, the channel) |
| `util.c/h`, `input.c`, `theme.c`, `prof.c` | `xmalloc`, `ByteBuf`, UTF-8, `str_lcpy`/`str_cut_word`/`str_cut_suffix`/`path_stem`, `dir_make`; the key parser (bytes to `Key`, ESC's timeout); the colors; the profiler (`PROF_ZONE`, the overlay, `--trace`) |
| `grid.c`, `token.c`, `draw.c`, `render.c`, `term.c` | drawing down to the diffing renderer and the terminal |
| `tests/` | `run.c` is the suite table and `main` only. Suites by area: `test_core.c` (text, input, render, map, file, undo, browser, golden, autosave), `test_build.c` (edges, terrain, brush, shapes, stamps), `test_play.c` (creatures, cursor, groups, moving, trail, labels, culling), `test_keys.c` (keymaps, key bar, `?`, remapping, cycling, focus), `test_marks.c` (markers, notes, counters, hidden), `test_saved.c` (picker, characters, scene and handout keys), `test_measure.c` (distances, rulesets, ruler, sight, range), `test_session.c` (dice, log, clocks, turns), `test_net.c` (wire, server, players' frame, pings, `:serve`, the page), `test_fog.c`, `test_maptools.c`, `test_ctl.c` (channel, room language, corridors, `--apply`), `test_places.c` (areas, links, floors). `build/run-tests NAME...` runs only the suites named in run.c's table. `harness.c` holds the counters and every helper more than one file uses (`press`, `players_text`, `sandbox_enter`, `write_map_file`, `golden`, `json_valid`, `ctl_ask`, ...), declared in `harness.h` with the `CHECK` macros and the shared types; a helper one file uses stays `static` there. `fuzz_mapio.c`, `fuzz_ctl.c` are libFuzzer only |

## Writing tests

- `press(&a, "keys")` feeds bytes through the real parser; `\r` is enter, `\x1b`
  esc, `\025` ctrl-u (clears a prompt). `Key k = { KEY_ENTER, 0, 0 }` for raw keys.
- A new suite: a `void test_x(void)` in the file for its area (make it non-static and
  declare it in `harness.h`), then a row in `run.c`'s table. A helper another file needs
  moves its prototype to `harness.h` and loses `static`.
- App tests: `Sandbox sb = sandbox_enter("name")`, `write_map_file(sb.dir, "fight.vtt")`
  (a **2×2 empty map** — place tokens with `ipAria\r` after setting `a.ed.cx/cy`),
  `app_open_map`, `Key f2 = {KEY_F2,0,0}` for play mode, `sandbox_leave(&sb)`.
- `play_focus` moves the selection, not the cursor; `enter` acts on the cursor.
- Rendered cells: `rnd_begin(&r); app_draw(&a);` then read `r.back[]`; `rnd_dump` for text.
- Checks are `CHECK` / `CHECK_EQ` under a `CASE("...")`; failures print `FAIL file:line`.
- Golden frames live under `tests/`; regenerate only when the change is intended.
  `golden_bytes(name, data, len)` compares any text (a map tool's report) the same way.

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
  the GM's alone. Whether the players may see a creature is one question,
  `fog_token_unseen(m, t, fog_any)` (its `Token.hidden` flag or fog over it): every
  players' path asks it, never `fog_token_hidden` alone. While any creature is hidden the
  players' frame is drawn (never copied), carries no status message, and gets the plain
  status line (no count, no ruler or range line), as over fog.
- Ideas consciously set aside live in `docs/IDEAS.md` with the reason (the Fear pool).
- Distances print through `dist_fmt`; coordinates through `map_coord_name`; a
  creature in a message through `token_name` (label, else its side). Overlap is
  `token_meets` / `tokens_at` / `tokens_overlapping`, never written out again.
- `range_clear` resets the overlay; `range_off` switches it off and keeps the shape.
- The `Cell` padding must stay zero (row memcmp in the renderer); tokens compare
  with `token_equal`, never `memcmp`.
- American spelling everywhere -- docs, messages, comments, identifiers (color, center,
  gray, canceled, labeled, counterclockwise). The loader still reads a marker saved as
  `grey`.
- Comments explain *why*; code says what. Rationale lives in the header or the README,
  not repeated at every call site.
- Never `pkill -f <pattern>` from a Bash call whose own command line contains the
  pattern: it kills the shell running it (exit 144). Record PIDs instead.
- To drive a live `vtt` from a script: `mkfifo f; (sleep 3600 > f &); script -qfc "./vtt map --serve 7792" /dev/null < f &`
  then `printf 'l' > f`. Chrome (via the claude-in-chrome skill) can open the served page.
- Environment quirks: `grep` is aliased oddly in this shell (use `awk` or plain
  `grep -n` in a subshell); `ESC` + letter in a script means Alt; scratch files go in
  the session scratchpad, not the repo.

## Watch list

Tech debt looked at in a health check and left on purpose. Each has the point
at which it stops being cheap to ignore; check it whenever the code it names is
touched, and move an item out of here once it is fixed. docs/HEALTH.md is the
full record of each check -- what it found, the evidence, what is still open.
The 2026-09-28 check's open items are there, in a proposed order.

- **Squares and regions are parsed in two places:** `maptools_region` (clips,
  takes a bare row `5:6`) and app_ctl.c's `region` (strict, refuses off-map).
  Two is within the rule of three; a third caller makes them one function
  with a clip flag, before the third copy is written. (2026-09-28: still two;
  but "an area name, else a square, into a box" has reached three -- see
  docs/HEALTH.md.)
- **Two copies, left (2026-09-28):** removing a group highest index first with
  `turn_before_remove`/`turn_settle` (app_play.c, app_link.c); describing a
  character or a stamp (the picker and the channel); taking a creature out of
  the fight (turn and init zeroed: stamp.c, character.c, app_link.c -- two
  assignments each). A third copy of either of the first two, or any change to
  what "out of the fight" means, makes them one function.
- **`picker.open` reads every file** (about 12 µs each): 5 ms for 500 templates,
  16 ms near 1,400. Cache details by mtime when someone's library gets there.

## Docs to keep in step

`README.md` (keys tables, feature sections, *Rulesets*, *File format*; written for a user,
in a plain instructional tone: what a feature does and how to use it, no history and no
design argument -- that belongs in docs/),
`docs/PERFORMANCE.md` (tables + a paragraph per finding), `src/keys.c` (the `?`
page). A feature is not done until all three agree with the code. `docs/HEALTH.md`
records each health check; an item fixed says so there, by commit.
