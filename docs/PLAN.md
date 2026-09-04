# VTT — a rules-agnostic virtual tabletop TUI in C

> **This is the founding plan, kept as it was written.** Where it and the code disagree,
> the code won: the per-frame arena it promises was never needed (the renderer reuses two
> fixed buffers; the only per-keystroke allocation left is the route search's scratch,
> which [docs/PERFORMANCE.md](PERFORMANCE.md) tracks), and the feature set has grown well
> past this document — the README is the living description, PERFORMANCE.md the living
> numbers.

## Context

You want a terminal virtual tabletop for running encounters at the table — first with Daggerheart, but the
tool itself knows nothing about any ruleset. It has two halves: a **Build mode** for authoring an encounter
map (a vim-like keyboard editor), and a **Play mode** where that map becomes the live battle map with
labeled player and enemy tokens you push around the grid.

The working directory is empty, so this is greenfield. Constraints you set, which drive every decision below:

- **C, zero dependencies.** libc plus POSIX (`termios`, `poll`, `dirent`) only. No ncurses, no terminfo —
  we emit ANSI/xterm escape sequences directly.
- **As responsive as possible.** The architecture is built around this: a diffing cell renderer, one
  `write()` per frame, input coalescing, and a blocking event loop that burns 0% CPU when idle.
- **Profiling from day one**, not bolted on later, so hot paths are always visible.
- **Minimal graphics.** White lines for walls, thin grey lines for grid (walkable tiles only), circles for
  players, inset squares for enemies.

Decisions already settled: walls are **edge-based** (a wall lives on the boundary between two tiles, so it
consumes no floor space and gives exact "can I step from A to B?" blocking); **keyboard only**, no mouse;
zoom is **adjustable at runtime** across four levels with 4×2 as the startup default.

## Layout

Everything lives in `~/Projects/vtt-tui` — source, build, and docs. The planning document itself moves into
the repo as `docs/PLAN.md` (copied from `~/.claude/plans/`), so the project is self-contained and nothing
related to it is left outside the directory.

```
/home/dml/Projects/vtt-tui/
  Makefile
  README.md          what it is, build + run, keybinding reference
  docs/
    PLAN.md          this document
  src/
    main.c        arg parsing, lifecycle, the event loop
    app.c/.h      app state, screen stack (Menu/Browser/Editor/Play), mode switching
    term.c/.h     raw termios, alt screen, SIGWINCH self-pipe, size query, ANSI output buffer
    input.c/.h    byte stream -> key events (escape-sequence parser with ESC timeout)
    render.c/.h   double-buffered cell grid, diff, minimal-escape flush
    draw.c/.h     primitives over the framebuffer: put_cell, hline, vline, box, text, fill
    map.c/.h      Map model: tiles, edges, queries, resize
    mapio.c/.h    text-format load/save
    grid.c/.h     zoom levels, tile<->screen transforms, camera, box-junction glyph tables
    editor.c/.h   Build mode: modal cursor, wall tracing, visual rect, undo
    play.c/.h     Play mode: token placement, selection, movement
    token.c/.h    token model + circle/square/label rendering per zoom level
    ui.c/.h       menu, file browser, text prompt, keybind bar, modals
    prof.c/.h     zones, frame stats, F12 overlay, Chrome-trace export
    util.c/.h     arena allocator, dynamic array macros, string helpers
  tests/
    run.c         tiny assert harness + unit tests
    golden/       expected frame dumps
```

## Core architecture

### Renderer — the thing that makes it feel instant

`struct Cell { uint32_t ch; uint32_t fg, bg; uint8_t attr; }` (16 bytes). Two buffers, front and back.

Every frame: clear back → draw into it → diff against front → emit into a single byte buffer → one
`write(1, ...)`. The diff coalesces runs, emits `CUP` only when a run breaks, and emits `SGR` only when
attributes actually change. Wrap each frame in synchronized-output mode (`CSI ?2026h` / `CSI ?2026l`) to
prevent tearing; terminals that don't support it ignore it harmlessly.

At 200×60 the whole buffer is 192 KB and a full diff is microseconds. No per-frame allocation — everything
comes from an arena.

### Event loop

```
while (running) {
    poll(stdin + sigwinch_pipe, timeout = dirty ? 0 : -1);
    drain ALL available input bytes, parse into events;
    dispatch every event;
    if (dirty || resized) render_frame();
}
```

Two properties matter here. **Blocking when idle** means zero CPU between keystrokes. **Draining all input
before rendering once** means a held-down `j` processes 30 queued moves and paints one frame — this is
exactly why vim feels instant and naive TUIs feel like mud.

### Map model — edge-based walls

```c
typedef struct {
    int w, h;
    uint8_t *tiles;   //  w      * h        TILE_VOID | TILE_FLOOR
    uint8_t *vedges;  // (w+1)   * h        wall between tile (x-1,y) and (x,y)
    uint8_t *hedges;  //  w      * (h+1)    wall between tile (x,y-1) and (x,y)
    char name[64];
} Map;
```

Separate edge arrays keep the boundary cases (east edge of the last column, south edge of the last row)
representable without special-casing. Edge values are an enum so doors/windows/secret doors drop in later
without a format change. `map_blocked(m, x, y, dx, dy)` is a single array lookup — the one query play mode
needs.

`tiles` carries walkability, which is what gates grid-line rendering: grey grid lines are drawn only on
`TILE_FLOOR`, `TILE_VOID` renders as blank.

### Zoom and the tile↔screen transform

Because walls live *between* tiles, the screen grid uses a **pitch** — each tile gets an interior block plus
shared 1-cell boundary rows/columns that the walls and grid lines are drawn on. Interior widths are odd so
there is a true center cell to anchor circles and labels on.

| Level | Interior | Pitch | Tiles in 80×24 | Use |
|-------|----------|-------|----------------|-----|
| Z0 | 1×1 | 2×2 | ~39×11 | whole-encounter overview |
| **Z1** | **3×1** | **4×2** | **~19×11** | **default** |
| Z2 | 5×2 | 6×3 | ~13×7 | comfortable play |
| Z3 | 7×3 | 8×4 | ~9×5 | detail, full in-token labels |

`+` / `-` change zoom, keeping the cursor tile fixed on screen. The level is saved with the map.

Camera is tracked in **cells**, not tiles, so scrolling is smooth rather than jumping a whole tile, with a
vim-style `scrolloff` margin. If the map is smaller than the viewport it is centered.

### Line rendering

Boundary cells between two floor tiles draw a **thin grey** light line (`│ ─`); boundary cells carrying a
wall draw **white** heavy (`┃ ━`). Corner cells resolve from a 4-bit mask of which incident segments exist,
via one 16-entry glyph table instantiated twice — once light/grey, once heavy/white. A junction is heavy if
*any* incident segment is a wall, which sidesteps the combinatorial explosion of mixed-weight box-drawing
glyphs and still looks right.

A `--ascii` flag swaps in `| - +` and `( ) [ ]` fallbacks for terminals without good Unicode coverage.

### Tokens

```c
typedef struct { int x, y; uint8_t size; /* 1,2,3 */ uint8_t kind; /* PLAYER|ENEMY */ char label[32]; } Token;
```

Anchored at the top-left tile, spanning `size × size` tiles — a multi-tile token paints over the internal
boundary cells it covers, so it reads as one solid piece. Players draw as circles (rounded box-drawing
`╭─╮ ╰─╯` at mid zoom, half-block arcs at Z3, `●`/`(A)` at Z0/Z1); enemies as squares inset one cell inside
their tile so they sit visibly *inside* the grid square. Labels render inside the token when they fit,
truncated otherwise, with the full label always shown in the status bar for the selected token.

## Build mode (editor)

Centered map viewport, title/status line above, keybinding bar below. Modal, vim-flavored:

- **NORMAL** — cursor on tiles. `hjkl` move, counts (`10j`), `gg`/`G`/`0`/`$`/`H`/`M`/`L`, `Ctrl-d`/`Ctrl-u`.
  `Shift-H/J/K/L` toggle the wall on the west/south/north/east edge of the cursor tile.
  `v` starts a visual rectangle; `f` fills it with floor, `x` clears it to void.
- **WALL** (`w`) — the cursor snaps to the *lattice corners* (a `(w+1) × (h+1)` grid) and `hjkl` moves
  corner to corner, laying a wall along each edge it crosses. This is a polyline tool: rooms and corridors
  get drawn by just walking the outline. `Space` toggles pen up/down, `d` + movement erases, `v` sets an
  anchor and `Enter` lays a full rectangle outline. `Esc` returns to NORMAL.
- **COMMAND** (`:`) — `:w`, `:q`, `:wq`, `:e <file>`, `:play`, `:resize <w> <h>`.
- `u` / `Ctrl-r` undo/redo.

Undo is an op-log of batched `(target, old, new)` changes in a ring buffer, so a whole rectangle fill or a
traced wall run is one undo step.

## Play mode

`F1`/`F2` (or `:build`/`:play`) switch modes. The map is read-only here; only tokens move.

`p` places a player, `e` places an enemy — both prompt for a label, with `1`/`2`/`3` choosing 1×1/2×2/3×3.
`Tab` cycles tokens, `Enter` grabs/drops the token under the cursor, `hjkl` moves a grabbed token while
showing a live step count from its origin (with a ghost marker on the origin tile), `d` deletes, `L`
relabels, `Esc` deselects. Wall blocking is enforced through `map_blocked` but is toggleable (`Ctrl-w`) —
rules-agnostic means never fighting the GM.

Token moves go through the same undo log as the editor.

## Menu and file browser

Landing screen: **Open Map** / **New Map** / **Quit**. Open lists `*.vtt` from the current directory and
`~/.local/share/vtt/maps` via `dirent.h`. New prompts for name and dimensions.

The file format is line-oriented text — human-readable, diffable, and parseable with `strtol`/`memchr`
alone. One file holds the map and its tokens, so an encounter is a single artifact:

```
VTT 1
name Goblin Ambush
size 40 25
zoom 1
tiles           # '.' floor, ' ' void, one line per row
vedges          # '|' wall, ' ' none, (w+1) chars per row
hedges          # '-' wall, ' ' none, w chars per row, h+1 rows
token player 3 5 1 "Aria"
token enemy 10 7 2 "Ogre"
```

## Profiling

Compiled in by default, `-DVTT_PROF=0` compiles it to nothing.

- `PROF_ZONE("draw_grid")` — scoped begin/end via `__attribute__((cleanup))`, backed by
  `clock_gettime(CLOCK_MONOTONIC)`.
- A 256-frame ring buffer of per-zone timings yields mean / p50 / p99 / max.
- **F12 overlay**: zone table plus a frame-time sparkline, drawn in the corner.
- TUI-specific counters that generic profilers won't give you: **cells changed per frame** and **bytes
  written per frame**. These are the real predictors of perceived latency, and they catch the classic bug
  where a full-screen repaint sneaks in.
- `--trace out.json` writes Chrome Tracing format for perfetto / `chrome://tracing` (a dev artifact; the
  viewer is external, the binary stays dependency-free).

## Testing

Rendering targets a cell buffer rather than the terminal, which makes the whole app testable headlessly:

- `--script keys.txt` replays a deterministic keystroke sequence.
- `--dump-frame` renders one frame to stdout as plain text with colors stripped.

Together those give end-to-end golden tests with zero dependencies. `--bench keys.txt` replays as fast as
possible and prints frame stats, so performance regressions are a `make bench` away.

Unit tests cover: the input escape-sequence parser, map load/save round-trip, `map_blocked` edge logic, the
box-junction glyph table, and the renderer diff (assert that a one-cell change emits a handful of bytes,
not a full repaint).

## Build

`make` (release: `-O2`), `make debug` (`-Og -g -fsanitize=address,undefined`), `make test`, `make bench`.
Warnings: `-std=c11 -Wall -Wextra -Wpedantic -Wshadow -Wconversion`.

## Milestones

Each milestone compiles and runs, so the app is playable-in-progress and the feel can be judged early.

0. **Scaffold** — create `~/Projects/vtt-tui`, `git init`, move `docs/PLAN.md` in from `~/.claude/plans/`,
   write the `Makefile`, `README.md`, and a `.gitignore`. Confirm `make` builds an empty `main.c` cleanly
   under the full warning set.
1. **Foundation** — `term`, `input`, `render`, `draw`, `util`, `prof`, main loop. Renders a test pattern and
   the F12 overlay; proves the diff renderer and 0% idle CPU.
2. **Map + shell** — `map`, `mapio`, `grid`, `ui`. Menu, file browser, new/open, a viewport you can move a
   cursor around with hjkl at every zoom level, walls and grid lines rendering correctly.
3. **Build mode** — `editor`: modal cursor, `Shift-HJKL` edge toggles, WALL tracing mode, visual rect
   fill/clear, undo/redo, command line, save.
4. **Play mode** — `token`, `play`: placement, labels, sizes, selection, movement with step counting and
   wall blocking.
5. **Polish + perf** — keybinding bar, modals, color theme, `--ascii` fallback, golden tests, and a
   profiler-guided pass over whatever the F12 overlay says is hot.

## Verification

- `cd ~/Projects/vtt-tui && make && ./vtt` — menu appears, create a 40×25 map, draw a room in WALL mode, save, quit, reopen, confirm
  the room round-tripped.
- Switch to Play mode, place a 1×1 player "Aria" and a 2×2 enemy "Ogre", move Aria into the room, confirm
  the step counter is right and that she cannot walk through a wall.
- F12 during all of the above: frame time should stay well under 1 ms and bytes/frame in the hundreds for
  cursor movement, not the tens of thousands.
- `make test` — unit + golden frame tests pass.
- `make bench` — replay script reports frame stats; use as the regression baseline.
- Resize the terminal mid-session and confirm reflow; run under `make debug` (ASan/UBSan) to confirm clean.
