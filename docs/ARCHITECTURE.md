# How vtt works

**Status:** kept current (written 2026-10-23 from the code on branch `vtt-2.0`, traced
function by function). Like docs/CODEMAP.md, this page changes in the same commit as the code
it describes (CLAUDE.md, *A change is not done*): a new subsystem, a new path data takes
between two of them, or a boundary moved. Suite `docs` fails when a function or macro with an
underscore, a file named alone, or a path under src/, tests/, tools/, docs/ or web/ named here
is gone; every review's *Docs drift* section reads the rest (tools/review-brief.md).

This page is the map of the whole: the subsystems, how they talk, what data crosses each
boundary and what never may. Each section names the files; docs/CODEMAP.md has the row for
each file (what it owns, its invariants), and the design records (docs/README.md) have why.

## The shape

One process, one thread, one `poll` loop. Everything the GM does, every phone, every agent
and the disk meet in it.

```
            keyboard (tty)                         another program writing the map file
                 |                                               |
                 v                                               v
  term.c --> input.c --> app_key ----------------------> app_disk.c (stat, diff)
                              |                                   |
                              v                                   v
           App (app*.c) --- World (world*.c) <--- request*.c <--- ctl.c  <-- the channel socket
           screens, modes    the live Map,          the request        (agents: vtt --ctl,
           prompts, modals   undo log, jobs,        language            --apply, the Claude
                              events                                    agent, :agent command)
                              |
                              v
           app_frame (app_draw.c): the GM's frame and the players' frame
                 |                                   |
                 v                                   v
           render.c --> term.c --> terminal     net.c/wire.c --> phones (web/index.html),
                                                                 a --watch terminal
                                                       ^
                                                       | taps, names
           mapio.c (through disk.c) <--> map files, autosave; util.c, settings.c, store.c
           <--> everything else on disk; apikey.c <--> the keyring
```

## The processes, and how vtt is started

`main.c:main` reads the arguments and picks one of these; only the first is the app.

| started as | what runs | talks to |
|---|---|---|
| `vtt [MAP]` | `run_interactive`: the terminal app, its loop, the channel socket always, the server with `--serve` | everything below |
| `vtt --watch ADDRESS` (`host:port?k=code`, as `:serve` shows it) | `watch_main` (watch.c): a terminal showing the players' frame | a vtt's server, as a raw watcher |
| `vtt --ctl [--ctl-pid N]` | `ctl_client_main`: one request to a running vtt, the answer on stdout | the channel socket |
| `vtt MAP --apply plan.txt` | `run_apply`: a plan proposed to the vtt that holds the map, or run in-process and saved when none does | the channel, or the file |
| `vtt --claude-agent ...` | `apiagent_main` (apiagent.c): vtt as its own agent, calling the Claude API through curl | the channel, curl, the key |
| `vtt MAP --check / --describe / --dump-map`, `--import-adversaries`, `--import-environments` | the map tools and the imports, no terminal | files only |
| an agent command (Claude Code, a script) | `/bin/sh -c CMD`, forked by `app_agent_start` | the channel, as `vtt --ctl` |

## The loop

`run_interactive` (main.c) sets up in this order: the terminal (`term_init`: raw mode, the
alternate screen, a self-pipe for SIGWINCH and SIGCHLD), the renderer, `app_init`, the
settings (`settings_load`), the map (`app_open_map`), output on its own non-blocking descriptor
(`term_async`), `loop_init`, the server if `--serve`, the channel (`ctl_start`), and the agent
command. It draws one frame, then loops: `loop_wait`, `poll`, `loop_once`.

- `loop_wait` (loop.c) builds the poll set each turn: the keyboard, the signal pipe, the
  terminal's output only while bytes are queued for it, the server's sockets, the channel's.
  The timeout is the nearest deadline: a lone ESC (25 ms, to tell ESC from Alt), the autosave,
  a ping fading, an agent's deadline, a waiting channel request, the file check, a phone's
  keepalive. With none it blocks.
- `loop_once` runs, in order: `net_service` (phones), `ctl_service` (the channel), `app_tick`
  (taps, channel requests, agents, the file, autosave), a resize, `drain_input` (keys), the
  ESC timeout, `app_events_flush` (answers to agents waiting), `term_flush`, and the frame if
  anything marked the app dirty. Many keys in one turn make one frame; a turn reads at most
  64 KB of keys so a flood cannot starve the screen.

## The state

| struct | owns | file |
|---|---|---|
| `App` | the GM's session: the screen and mode, prompts and modals, the editor and play state, the players' view and camera, the server (`Net`), the channel (`Ctl`), the agents, the settings, and a `World` by value | app.h |
| `World` | what a request may touch with no screen: the live `Map`, the one session undo log, jobs and their proposals, the event ring, the checkpoint marks, the request's scratch map; it calls the GM back through `host` | world.h |
| `Map` | squares (tiles, edges, fog), creatures, notes, areas, links, scenes, cards, clocks; `gen` and `modified`; the sight cache; the file's identity and the text it was read from (`base`) | map.h |
| `Undo` | the edit log: operations in batches, a batch per key or request | undo.h |
| `Renderer`, `Term` | the frame as cells (`front` sent, `back` drawn), the terminal and its output queue | render.h, term.h |

The suite `protocol` builds the channel with no `src/app*.c` at all: `World` and the request
code stand on their own, and `App` is one host of them.

## A key, to the map, to the terminal

1. `drain_input` reads the keyboard; `input_next` (input.c) parses plain keys, UTF-8, CSI and
   SS3 sequences and ESC-prefixed Alt into a `Key`.
2. `app_key` (app.c) dispatches it: the profiler key, an open modal, the global keys (help,
   floors, the F1/F2 mode switch), then the screen's own handler -- `app_menu_key`,
   `app_browser_key`, `app_options_key`, `app_editor_key` (build mode), `app_play_key` (play).
   Every key marks the frame dirty. After it, `app_key` syncs what keys may have moved: the
   ruler's aim, the floor, fog (recomputed only when the map's `gen` changed), jobs waiting to
   land, the file check.
3. An edit goes through the undo log: `undo_begin`, the recorders (`undo_set_tile`,
   `undo_move_token` and the rest), `undo_end`. A recorder returns early when nothing would
   change, notes the square for the agents' change sets (`cp_note`), logs the operation, then
   writes the map (`map_touch` bumps `gen`). One key is one undo step; between keys nothing is
   open but a stroke (`undo_balanced`, checked by every test key in tests/harness.c).
4. The frame: `loop_frame` calls `app_frame` (app_draw.c), which clears the back buffer
   (`rnd_begin`) and draws the screen (`app_draw`: the title bar, then `play_draw` or
   `ed_draw` over `grid_draw`, overlays, modals, the profiler). `draw_cell` writes cells.
5. `rnd_flush` (render.c) sends only what changed: rows equal to what was sent are skipped by
   `memcmp` (so `Cell` padding stays zero), cells by `cell_eq`, cursor moves and colors only
   when they change, inside a synchronized update. If the terminal is still taking the last
   frame, this one is skipped and redrawn whole later. `term_write` (term.c) writes what it
   can and queues the rest; `front` means sent, queued bytes included.

## The players' frame, to the phones and back

### The frame, and the privacy boundary

Each frame, after the GM's is drawn, `app_frame` makes the players' -- only in play mode
(`app_remote_live`), only with the server running and someone watching.

- The server holds its own renderer (`net_players_renderer`), blank at start: the phones are
  never sent the GM's renderer.
- `app_view_differs` is the boundary, and the function itself is the full list. When anything
  GM-only is on screen -- among them a modal, the command line, a GM-only status, fog, a
  hidden creature, the profiler, the agents' ring, job tints, a card, summoning, a creature's
  notes or counters, a map note under the cursor, the players on their own camera, a status
  line that differs; under the handout preview, only the handout -- the players' frame is drawn a second time with `app_draw_view`
  (`VIEW_PLAYERS`), which leaves all of those out. Otherwise the GM's cells are copied
  (`rnd_copy_back`). Anything new that is GM-only must be added there (CLAUDE.md).
- Inside `VIEW_PLAYERS`, "may the players see this creature" is `fog_token_unseen`; an unseen
  creature is skipped or drawn as a silhouette and named "?" in the turn panel.
- The players' camera (`app_players_camera`, app_floor.c): follow the GM, frame the party, or
  hold, each kept to the floor.
- Messages about a phone, a whisper, a creature's numbers or a job go through
  `app_note_gm`/`app_set_status_gm` and never reach the players' status line.

### The server and the wire (net.c, wire.c, conn.c)

- `:serve` starts `net_start`: a TCP listener on every address, eight client slots, a
  six-digit join code from /dev/urandom, the address shown as `http://IP:PORT/?k=CODE`.
- A connection's first bytes decide its kind: `GET ` is HTTP, `VTT1` a raw watcher. HTTP
  (`http_request`) serves the page (`/` and `/?...`, from src/webpage.c, embedded from
  web/index.html; gzip to a browser that takes it), upgrades `/ws` to a WebSocket, and from off
  this machine asks for the join code: the page's address gets a small form (403, or 429 past
  the wrong-code limit), `/ws` a close of its own (4001, 4002), the rest a plain 403; every
  code is judged by `code_check`. A `n=` in the address is the phone's name, cleaned by
  `net_name_clean`.
- The frame goes out as records (wire.c): `F` a whole picture's header, `R` a run of cells
  with their colors, `P` a palette entry, `E` the end of a frame, `Z` a keepalive, and three
  text records -- `H` a handout, `W` a whisper, `N` the names a phone may pick. The renderer's
  flush feeds each changed cell to the encoder (`wire_observe_cell`); `net_frame_end` queues
  the frame for each client, a WebSocket binary frame for a browser.
- A phone that stops reading is not queued for without end: once what it holds would pass a
  whole picture, frames are withheld, and when it drains it is sent one fresh `F`
  (`client_catch_up`). A queue past its cap drops the phone, and the GM is told.
- Keepalive: a WebSocket ping (a watcher, `Z`) after 15 s quiet; a client silent 60 s is
  closed, except a watcher that has said hello.

### Back from the phone

- A phone sends one kind of message: `P col row`, a tap, at most one a second. `app_tick`
  takes the taps (`net_take_pings`) and `app_ping_cell` maps the cell through the players'
  camera to a square: a ping the GM and the players see.
- A phone's name (`client_named`) tells the GM it arrived and delivers a whisper kept for it.
  `:whisper` sends `W` to the phones of that name, or keeps it. Handouts go to every phone.

### The page (web/index.html)

One request under 12 KB as sent, gzip to a browser that takes it (docs/REMOTE.md). It connects
to `/ws`, reconnects after a close (2 s doubling to 30 s while vtt is away; a 4001, its code
refused, sends it to the join form instead) and at once when shown again, decodes the records in `feed()`, and draws through a glyph atlas
and colored tiles copied into the canvas by a small WebAssembly loop (SIMD where it compiles).
A tap sends `P x y`. The name sheet sends `n=`. The suites `pagefeed` and `pageconn` run its
decoder and its connection code under node.

### A watcher

`:mirror` starts the server if it is not running and opens a terminal running `vtt --watch`
on this machine; `watch_main` reads the same records with `wire_dec_feed` and draws them. A
connection from this machine skips the join code.

## The channel, its requests, proposals and jobs

### The socket (ctl.c)

A Unix socket at `$XDG_RUNTIME_DIR/vtt/PID.sock` (else `/tmp/vtt-UID/`), in a directory that
must be this user's alone, mode 0700, no link (`dir_ours`). An interactive vtt always listens.
One request a connection, up to 64 KB, four requests at once and eight `wait`s held beside
them, a deadline each.

### A request (request.c, world*.c)

`app_tick` takes each request (`ctl_next`) and runs it with `request_exec`: lines of words, a
first line `agent NAME` saying who asks, each line to its verb's handler. The verbs: edits
(room, area, door, corridor, tile, wall, edge, stamp, note, fog, floor, env, link, token,
scene), reads (dump, changes, describe, check, status and others), jobs (propose, `job N ...`,
jobs) and control (agent, wait, undo, holds, help). The answer is a verdict line -- `ok`,
`busy: ...`, `error CLASS: line N: ...` -- then the body. Each request's form and rules are
`REQUESTS[]`'s (request.c), printed by `help VERB`; the agents' guide (docs/AGENTS.md, embedded as
src/agentguide.c) is the concepts and the steps, and `vtt --agent-guide TOPIC` prints a topic with the
table's rows for it (guide.c).

### Edits arrive as proposals

An agent never edits the live map. `request_proposal_begin` copies the map to a scratch one
and points the request at it; at the end the difference (`cs_diff`) becomes a job's proposal
through `world_job_set_proposal`. The GM reviews it (`app_review_key`): enter lands it as one
undo step tagged with the agent (`world_job_land`), `d` scraps it, `c` sends feedback and
restarts the job's agent. With `:agent on` and `:agent accept auto` a proposal lands at once;
while the GM is part way through something it waits, and lands at their next key
(`world_jobs_land_waiting`, called from `app_key`). Only with no vtt holding the map does `--apply` edit directly, and save.

### Events: what agents hear

`world_event` writes a line to a 32-line ring: a job asked, proposed, accepted, scrapped, a map
changed or closed. An agent's `wait` holds its connection until an event it did not cause
arrives, or the time runs out (`app_events_flush` answers it). Map changes are summed into
`changed` events from the checkpoint marks (`cp_note`) only while someone listens.

## Agents

- `:ask` makes a job from the GM (`app_ask_command`) and starts the agent
  (`app_agent_start`): `/bin/sh -c CMD`, in its own session, the prompt on stdin, output to the
  session log or nowhere, every other descriptor closed, and `VTT_JOB`, `VTT_AGENT`,
  `VTT_PID` and `VTT` set so it can talk back with `vtt --ctl`. It is stopped at its deadline
  (SIGTERM, then SIGKILL, to its group) and reaped by `app_agents_tick`.
- The Claude agent is the same with vtt's own command: `vtt --claude-agent`. `apiagent_main`
  gets the key (below), sends the job to the Claude API with the agents'
  guide as its system prompt and one tool, `vtt`, whose calls it runs on the channel
  (`ask_vtt`), and reports how the job ended with `job N say`. Each job's calls, tokens and dollars go
  to a row of `claude-usage.tsv`.
- **The key** (docs/APIAGENT.md, *The key, made safer*). It is kept in the keyring
  (`secret-tool`, service `vtt` account `anthropic-api-key`), or held in vtt's memory: one
  typed in for this session only, or `ANTHROPIC_API_KEY`, which vtt takes out of its
  environment at start (`apikey_take_env`) so nothing it starts inherits it. A process
  holding a key is not dumpable (no core file, its memory closed to other processes); the
  Claude agent never is (`apikey_harden`), and has a core limit of 0 that curl inherits.
  A typed key waits in a slot of its own while the GM picks `r` (the keyring, down a pipe)
  or `s` (held). The Claude agent gets a held key down a pipe on descriptor 3
  (`VTT_KEY_FD`), and looks a keyring key up itself for each call. To curl it goes down
  descriptor 3 as a config line (`call_api`, through `proc_run`), curl run with `-q`,
  `--proto =https` and an environment of its own: never on a command line, in a log or an
  environment, and wiped after use (`apikey_wipe`). An agent command gets the exported key
  back only with `:agent passkey on`.

## The disk

### The map file

- Opening (`app_open_map`) asks the other vtts whether one holds the map (`ctl_who_holds`),
  then reads it whole and parses it from memory (`mapio_load_base_diag`), keeping its text as
  the base and the file's identity (device, inode, size, time). The loader forgives damage:
  each thing it drops calls `diag()` with a code, shown by `--check`.
- Saving (`app_save_map`) refuses while another writer's change is unresolved, then
  `write_map` builds the text, writes `PATH.PID.tmp`, flushes it, and renames it over the
  map; every write of the map goes through the `disk()` table (disk.c), which the tests swap.
- Autosave: a modified map quiet for a moment is written to `PATH.autosave` (not flushed);
  a failure is told to the GM once. On open, an autosave newer than the map is offered.
- Another program writing the file: `app_disk_key` stats it at most once a second; after a
  quiet moment `disk_read` diffs it against the base and the change becomes a job only the GM
  may decide, like an agent's proposal.
- Closing (`app_close_map`): the handout comes down, the autosave goes, the server stops
  (unless kept), the log closes, `map closed` is sent to the agents, every job goes
  (`world_jobs_clear`), the undo log is cleared. Anything that would discard work asks first
  (`app_leave_map_for`).

### Everything vtt keeps

| what | where | written by |
|---|---|---|
| maps | `$XDG_DATA_HOME/vtt/maps/NAME.vtt`, or any path given | `write_map` |
| the autosave | `PATH.autosave`, the map's whole path (`.damaged` when cut short) | `app_autosave` |
| the session log | `STEM.log` beside the map (its path with `.vtt` cut), with `:log` | slog.c |
| characters, stamps | `.../vtt/characters/`, `.../vtt/stamps/`, each a small map file | character.c, stamp.c |
| handouts | `.../vtt/handouts/NAME.txt` | the GM, by hand |
| environment cards | `.../vtt/environments/NAME.txt` | the import |
| Claude usage | `.../vtt/claude-usage.tsv` | apiagent.c |
| settings | `$XDG_CONFIG_HOME/vtt/settings` | settings.c |
| the API key | the keyring, or nowhere (held in memory for the session) | apikey.c |
| the channel | `$XDG_RUNTIME_DIR/vtt/PID.sock` | ctl.c |

File names a user types go through `store_name_ok`; files other than the map are written
whole beside and renamed (`file_write_whole`, util.c). Imports read the SRD's JSON
(`json_parse`) into characters and environment cards.

### The settings

`settings_load` reads `$XDG_CONFIG_HOME/vtt/settings` at start-up, before the map opens;
the accept mode comes from it (`world.ctl_auto`), and which agent runs -- a command, or the
Claude agent with its model, effort and limits. The Options
screen and the `:agent` commands change a setting and write the file back (`settings_save`).

## The profiler

Every drawing and keystroke path is timed by a `PROF_ZONE` (prof.h; compiled out when the
profiler is off). F12 shows the zones over the screen (`prof_overlay_draw`); `--trace PATH`
writes a Chrome trace on exit; `--bench` replays keys headless and reports. tools/perf.sh
runs the scenarios and docs/PERFORMANCE.md holds what they measured.

## What never crosses which boundary

| boundary | rule | held by |
|---|---|---|
| the GM's screen to the phones | nothing GM-only; an unseen creature is not shown | `app_view_differs`, `VIEW_PLAYERS`, `fog_token_unseen`, `app_note_gm`; the players' frame check after every key in the tests |
| an agent to the live map | edits only as a proposal the GM decides | `request_proposal_begin`, `world_job_set_proposal`, `world_job_gm_only` for the disk's |
| any edit to the map | through the undo log, one step a key or request | the `undo_*` recorders, `undo_balanced`; `cp_note` for a writer round the log |
| a phone or a watcher to the app | only taps and a name, after the join code | `http_request`, `net_msg`, `net_name_clean` |
| the API key to anywhere | only the keyring's pipe, the Claude agent's descriptor 3 and curl's; never an environment, a command line, a log or a core file | apikey.c (`apikey_take_env`, `apikey_hold`, `apikey_harden`), `app_agent_start`, `call_api` |
| a test or a script to the user's files | a sandbox of its own (XDG dirs, no keyring, editor `true`) | `sandbox_enter`, tools/sandbox.sh, suite `sandbox` |
