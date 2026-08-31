# vtt

A rules-agnostic virtual tabletop for the terminal.

Build an encounter map in a vim-like keyboard editor, then flip to play mode and run the
fight on it with labeled player and enemy tokens. The tool knows nothing about any specific
ruleset — it is a grid, walls, and tokens.

Written in C11 with **zero dependencies**: libc plus POSIX (`termios`, `poll`, `dirent`)
and `-lm` for one square root. No ncurses, no terminfo. ANSI escape sequences are emitted
directly.

```
┏━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┓
┃   │   │   │   │   │   │   │   │   │   ┃
┃───┏━━━━━━━━━━━━━━━┓───┼───┼───┼───┼───┃
┃   ┃   │(A)│   │   ┃   │   │   │   │   ┃
┃───┃───┼───┼───┼───╹───┼───┏━━━━━━━┓───┃     ━━  wall
┃   ┃   │   │   │   │   │   ┃ Ogre  ┃   ┃     ──  grid line (walkable tiles only)
┃───┗━━━━━━━━━━━━━━━┛───┼───┃       ┃───┃     (A) player token
┃   │   │   │   │   │   │   ┗━━━━━━━┛   ┃     [ ] enemy token
┗━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┛
```

## Build

```sh
make           # release (-O2)
make debug     # -Og -g3 with ASan + UBSan
make test      # unit and golden-frame tests
make bench     # deterministic keystroke replay + frame statistics
./vtt          # run
```

## Options

```
vtt [options] [map.vtt]

  --ascii            avoid box-drawing glyphs; use ASCII fallbacks
  --trace PATH       write a Chrome Tracing profile on exit
  --script PATH      replay a keystroke script instead of reading the tty
  --bench PATH       replay a script headlessly and report frame statistics
  --dump-frame       render one frame as plain text to stdout and exit
  --size WxH         geometry for headless modes (default 80x24)
```

`--dump-frame` honours `--script`, so a whole session can be replayed and its final frame
compared as text. That is what the golden tests use.

Script files are the byte stream a terminal would send, with `\e` escape, `\r` enter, `\t`
tab, `\xHH` for any byte, and `\.` for a pause. The pause matters: ESC followed immediately
by a key is Alt+key, while ESC followed by a gap is the Escape key.

## Keys

**`?` shows every key for whatever you are doing**, as a scrolling page with the other modes
below it. `j`/`k` and `Ctrl-d`/`Ctrl-u` scroll, `g`/`G` jump to the ends, `q` or `esc` closes.

The bar along the bottom carries at most six hints and its last one is always `? keys`,
pinned to the right so a narrow terminal drops the others before it. The bar teaches the
shape of a mode; `?` tells the whole story. Both read the same tables in `src/keys.c`, so
neither can drift from the other.

The scheme follows vim where vim has an analogue — `d` deletes, `y` yanks, `p` pastes, `c`
changes, `i` inserts, `u` and `Ctrl-r` undo and redo, `/` `n` `N` search, `:` for anything
rare — and a letter shared between build and play means the same thing in both.

`F12` toggles the profiler overlay everywhere. `F1` and `F2` switch between build and play.

### Menu and file browser

| key | action |
|-----|--------|
| `j` `k`, arrows | move |
| `enter` | select / open |
| `g` `G` | first / last (browser) |
| `R` | rename the selected map |
| `c` | duplicate the selected map |
| `d` | delete the selected map (asks first) |
| `r` | rescan for maps (browser) |
| `esc` `q` | back / quit |

**Renaming** moves the file and retitles the map inside it, so the name in the browser and
the name in the title bar do not drift apart. It refuses to overwrite an existing map, and a
name containing `/` is rejected — this renames, it does not move. A map too damaged to load
still renames; only its title is left alone, which is reported. The caret follows the file
to wherever it now sorts.

**Duplicating** copies the file byte for byte and titles the copy, so a map the loader
would choke on still duplicates exactly. It offers a free name — `goblin copy`, then
`goblin copy 2` — counting up from the original rather than stacking the word, and refuses
to write over an existing map.

**Deleting** removes the file from disk permanently — there is no undo for it — so it asks
first, naming the exact path. The caret keeps its place afterwards, so clearing out several
old maps is `d y d y`.

Maps are found in the current directory and in `~/.local/share/vtt/maps`.

### Build mode

| key | action |
|-----|--------|
| `h` `j` `k` `l`, arrows | move the cursor |
| `10j` | counts repeat a motion |
| `0` `$` `gg` `G` | row and column extremes |
| `Ctrl-d` `Ctrl-u` | half-page down / up |
| `H` `J` `K` `L` | toggle the wall on the west / south / north / east face of the cursor tile |
| `w` | wall-tracing mode |
| `m` | measure (ruler) |
| `v` `V` | select a box / a circle |
| `f` `x` | fill selection with floor / clear it to void |
| `space` | toggle the cursor tile between floor and void |
| `u`, `Ctrl-r` | undo / redo |
| `+` `-` | zoom in / out |
| `z` | centre the view on the cursor |
| `:` | command line |
| `q` | close the map |

**Shapes.** `v` anchors a box and `V` anchors a circle — the pair vim uses for its two
visual modes, so the other key swaps the shape without losing the anchor and the same key
twice lets go. A circle is centred where you pressed the key and the cursor sets its radius,
which the status line reports as `circle r4` so you can aim for a size rather than count
tinted squares:

```
    V here, cursor four east            then f, or enter in wall mode
    ·  ·  ·  ▓  ▓  ▓  ·  ·  ·                  ┌──────┐
    ·  ▓  ▓  ▓  ▓  ▓  ▓  ▓  ·               ┌──┘      └──┐
    ▓  ▓  ▓  ▓  ▓  ▓  ▓  ▓  ▓               │            │
    ·  ▓  ▓  ▓  ◆  ▓  ▓  ▓  ·               │            │
    ▓  ▓  ▓  ▓  ▓  ▓  ▓  ▓  ▓               │            │
    ·  ▓  ▓  ▓  ▓  ▓  ▓  ▓  ·               └──┐      ┌──┘
    ·  ·  ·  ▓  ▓  ▓  ·  ·  ·                  └──────┘
```

Both shapes work the same way in visual mode and in wall tracing: visual mode paints the
tiles a shape covers, tracing walls its boundary — every face a covered tile shares with
one outside it. That is exactly the old rectangle outline for a box, and the only sensible
reading of a circle of wall.

The radius is measured as a straight line, so a circle is round on every map. This is
deliberately unlike the [range bands](#range-bands-r), which follow the map's distance
metric because they measure reach — a tower is a tower whatever the movement rules say.

Wall tracing anchors on a lattice corner rather than a square, which is where its cursor
lives, so its circles sit between squares and come out even across rather than odd.

### Wall-tracing mode (`w`)

The cursor snaps to the lattice corners between tiles. Every step crosses exactly one edge,
and with the pen down that edge becomes a wall — so a room is drawn by walking its outline.
A whole pen-down stroke is one undo step.

| key | action |
|-----|--------|
| `h` `j` `k` `l` | move corner to corner, laying wall when the pen is down |
| `space` | pen up / pen down |
| `t` | cycle what the pen lays |
| `d` | erase instead of lay |
| `v` `V` | anchor a box / a circle (the result is previewed) |
| `enter` | wall around the anchored shape — the bar names which |
| `esc` | clear the anchor, or return to normal mode |

### Play mode (`F2`)

| key | action |
|-----|--------|
| `h` `j` `k` `l` | move the cursor, or the held token |
| `i p`, `i e` | place a player / an enemy, prompting for a label |
| `1` `2` `3` | size of the next token, or resize the selected one |
| `enter` | pick up the token under the cursor / put it down |
| `d` `x` | remove |
| `y` `p` | yank a token / paste the copy here |
| `c` | change its label |
| `t` `T` | next / previous token, any kind |
| `f` `F` | next / previous **f**riendly — player tokens only |
| `e` `E` | next / previous **e**nemy |
| `tab`, `shift-tab` | the same as `t` / `T` |
| `/` | find a token by part of its label |
| `n` `N` | next / previous match for the last search |
| `s a` | add a status marker (prompts for a word) |
| `s c` | change the colour the next marker will use |
| `s d` | take a marker off (asks which, when there is more than one) |
| `m` | measure (ruler) |
| `r` | cycle a range band highlight (see below) |
| `o` `O` | open or close a door / a secret door on this tile |
| `Ctrl-w` | toggle wall blocking |
| `esc` | put down, then take the range overlay off, then deselect |
| `u`, `Ctrl-r` | undo / redo |
| `?` | every key, in full |

Two prefixes carry a family each, which is what keeps the bar to six hints: `i` inserts
(`i p`, `i e`) and `s` is for status markers (`s a`, `s c`, `s d`). Press either alone and
the status line names the options; `esc` abandons it. A prefix swallows whatever comes
next, so a half-typed command can never turn into a different whole one.

Freeing `p` for paste is the point of `i`: `p` means paste everywhere else, and `P` was an
odd place for it. The keys that moved — `a` `A` `v` `V` `P` `R` `S` — say where they went
if you press them out of habit.

`esc` backs out of one thing at a time, innermost first: put the creature down, take the
overlay off, let go of the creature.

**Moving a token.** Pick one up with `enter` and it walks with `hjkl`, with a `◆` on the
square it set out from and a green ribbon along the **shortest walkable route** from there
to where it now stands:

```
    ┌───┬───┬───┬───┬───┬───┐
    │ ◆ │▓▓▓│▓▓▓│   │   │   │        ◆  where it set out
    ├───┼───┼───┼───┼───┼───┤        ▓  the route back to it
    │   │   │▓▓▓│▓▓▓│[O]│   │
    ├───┼───┼───┼───┼───┼───┤
```

The ribbon is the route, not the wandering — walk out six squares and back three and it
shows the three. It is recut every time the creature lands somewhere new, so it always
answers "what would this move cost", and the step count in the status line is that route's
length rather than the number of keys pressed. Walls lengthen a route rather than being cut
through, and where several routes are equally short the one drawn hugs the straight line,
so open floor gives a staircase rather than an L. A multi-tile creature tints its whole
footprint, showing the ground covered rather than a thread along its top-left corner.

The distance in feet beside the step count is the straight line by the map's metric — what
a range band cares about — so the two disagree whenever a wall is in the way, which is the
point of showing both. A creature carried somewhere it could not walk to (blocking switched
off, then back on) has no route to price: the ribbon goes, the readout says `no route`, and
the count falls back to keystrokes.

One keypress is one undo step, count and all, so `u` walks the creature back a step at a
time and the route shortens with it. `enter` or `esc` puts the creature down and clears the
ribbon. A multi-tile token is blocked by a wall anywhere along its leading face.

**Finding a creature.** Three tracks, because a GM running a fight wants the next of
*their own* creatures far more often than the next of anything: `t` walks every token, `f`
walks the friendlies, `e` walks the enemies. Shift reverses any of them. Each starts from
whatever is selected now rather than the top of the list, so switching tracks picks up near
where you were looking. The cursor follows the selection and the view scrolls to it — a
selection you cannot see is no use for finding a creature — and the status line names what
you landed on. A track with nothing in it leaves the selection alone, so pressing the wrong
one of three keys costs nothing.

`/` searches labels: any part, any case, so `gob` finds `Goblin 3`. It walks on from the
current selection, and `n` / `N` repeat the search forwards and back without retyping it.
The prompt opens empty rather than pre-filled with the last search — a prompt you have to
clear before you can type is worse than one you have to retype — and submitting a blank
line repeats the last search.

**Copying tokens.** `y` copies the token under the cursor and `p` stamps it down at the
cursor, as often as you like — five goblins is `y` then `P P P P`. Each copy is numbered
(`Goblin`, `Goblin 2`, `Goblin 3`) so the readout can tell them apart, continuing an
existing run rather than stacking numbers. A pasted creature arrives with no status
markers: those describe what is happening to one creature right now, not what it is.

**Status markers.** `s a` hangs a marker on a token — a colour and a word, whatever your table
calls it. The tool attaches no meaning to them. They draw as the first letter of the word,
in colour, on the boundary above the token so they never cover its name; a token holds four,
and the overflow continues on the row below. `s c` cycles the colour before you add, and every
change goes through undo. The status line spells out the full words for the selected token,
since a letter alone does not say which condition it is.

`s d` takes a marker off. Conditions end on their own schedule, so when a token is wearing
more than one it asks which:

```
╭─ Clear marker on Goblin ───────────────────────╮
│                                                │
│  1  red  Poisoned                              │
│  2  orange  Marked                             │
│  3  yellow  Burning                            │
│                                                │
│  1-3  clear one      a  all      esc  cancel   │
╰────────────────────────────────────────────────╯
```

The rows are spelled out and coloured, because the map only ever showed initials and two
conditions can share one. A token wearing a single marker skips the question — a chooser
with one row asks nothing.

### Ruler (`m`)

`m` drops an anchor at the cursor and starts measuring; in play mode it snaps to a token
under the cursor. The line is drawn across the map, with the reading beside the cursor and
in the status bar.

| key | action |
|-----|--------|
| `h` `j` `k` `l` | move the far end |
| `enter` | add a leg, to measure a path that bends |
| `backspace`, `u` | remove the last leg |
| `M` | cycle the distance metric |
| `m` | re-anchor here |
| `esc` | done |

The readout gives length in tiles, the distance in feet, the range band if a ruleset is
set, and whether a wall breaks the line:

```
RULER   6 tiles  30 ft  Close  sight blocked  [chebyshev]
```

Sight is measured from the anchor straight to the far end and ignores whether the ground is
walkable — you can see across a pit you cannot walk over.

### Range bands (`r`)

For effects that catch everything in range rather than a single target. `r` highlights every
square within one band of the selected token — or of the cursor, if nothing is selected —
and cycles Melee → Very Close → Close → Far → Very Far → off. The anchor is fixed when you
switch it on, so flipping through bands afterwards doesn't drag it along with the cursor.
It follows the creature it is anchored to as that creature moves.

`esc` takes it off without cycling all the way round. So does moving the focus: an overlay
anchored to a creature goes when you tab to another one, because a highlight still sitting
around whoever you were looking at a moment ago is worse than no highlight at all. One
dropped on bare ground belongs to nobody and stays where you put it.

Squares with a clear line get the full tint; squares in range but with a wall in the way get
a dimmer one, because you can't target what you can't see. The status line names who is
caught, which also catches anyone scrolled off screen:

```
Close (30 ft, 6 sq) from Aria - 3 in range: Ogre, Goblin*, Bram   * no line of sight
```

The highlighted shape follows the distance metric — an octagon under the default 5-10-5,
a square under `chebyshev`. It needs a ruleset for its bands; `:ruleset daggerheart` sets
one. Sight uses the same test as the ruler, so the two never disagree about the same line.

### Boundaries and terrain

A boundary sits between two tiles. Movement and sight are separate questions, so each kind
answers them separately:

| kind | drawn | stops movement | stops sight |
|------|-------|----------------|-------------|
| wall | `━` solid white | yes | yes |
| door | `═` double, amber | yes | yes |
| open door | thin, amber | no | no |
| window | `┅` dashed, cyan | **yes** | **no** |
| secret door | a wall (see below) | yes | yes |

Doors toggle with `o`. Secret doors need `O`, so opening an ordinary door beside one cannot
give it away — and in play mode a closed secret door is drawn *exactly* as a wall, glyph and
colour, with nothing to notice for anyone reading the screen. Build mode marks it with `╳`
so you can see your own door. There is a test that renders both and asserts the play frame
is identical to a plain wall.

Terrain is decoration: **water, rough, brush, wood and hazard behave exactly like floor.**
What difficult ground costs is a ruling between you and your players, not something the tool
decides. Only *void* — the absence of map — is not walkable.

### Measurement

One tile is five feet by default. Distance, metric and ruleset are stored per map, since
they belong to the game being played rather than to the session.

| metric | 4x3 offset | notes |
|--------|-----------|-------|
| `alt` | 25 ft | diagonals alternate 1 and 2, the 5-10-5 rule (D&D 3.5 / Pathfinder). **The default** |
| `chebyshev` | 20 ft | every step costs one tile, diagonals included (D&D 5e) |
| `euclidean` | 25 ft | true straight line, what a tape measure reads |
| `manhattan` | 35 ft | no diagonals |

`alt` is the default because it stays within half a square of the true straight line while
keeping every reading a whole number of squares. `chebyshev` is a square cheaper on long
diagonals, which can pull a target into a nearer band than the fiction would put it in.

Range bands come from a named ruleset. `none` reports plain distances; `daggerheart` adds
Melee / Very Close / Close / Far / Very Far.

The Daggerheart SRD describes each band twice — a fiction distance in feet, and an estimate
for a physical battle map — and the two do not agree (Far is "about 30–100 feet" in the
fiction but "the long edge of a piece of paper, 11–12 inches" on the map). These are the
map estimates, converted at the book's own *1 inch represents roughly 5 feet*, because that
is the column written for playing on a grid:

| band | book's map estimate | threshold | squares at 5 ft |
|------|--------------------|-----------|-----------------|
| Melee | touching | 5 ft | 1 |
| Very Close | short edge of a game card, 2–3 in | 15 ft | 3 |
| Close | a pen or pencil, 5–6 in | 30 ft | 6 |
| Far | long edge of a sheet of paper, 11–12 in | 60 ft | 12 |
| Very Far | beyond Far, still within the scene | — | 13+ |

Thresholds are stored in feet rather than squares, so `:scale` keeps them describing the
same fictional distance. There is no *Out of Range* band: the book defines it as beyond the
bounds of the conflict, which is a call about the scene rather than a distance, and anything
the cursor can reach is on the map by definition.

The SRD is explicit that these ranges "aren't intended to be precisely measured during play"
and are a quick guide for the GM — the readout is the same kind of aid.

### Commands

| command | action |
|---------|--------|
| `:w [name]` | save |
| `:wq` `:x` | save and close |
| `:q` `:q!` | close, with or without asking |
| `:e <name>` | open another map |
| `:name <text>` | rename the map |
| `:resize WxH` | resize (clears the undo history) |
| `:zoom N` | set the zoom level, 0–3 |
| `:scale N` | feet per tile (default 5) |
| `:metric NAME` | `chebyshev`, `euclidean`, `alt` or `manhattan` |
| `:ruleset NAME` | `none` or `daggerheart` |
| `:play` `:build` | switch mode |

## Zoom

Because walls live *between* tiles, the screen grid uses a pitch: each tile gets an interior
block plus the 1-cell boundary it shares with its neighbour, which is where walls and grid
lines are drawn. Interior widths are odd so there is a true centre cell for a token.

| level | interior | pitch | tiles in 80×24 |
|-------|----------|-------|----------------|
| 0 | 1×1 | 2×2 | ~39×11 |
| 1 | 3×1 | 4×2 | ~19×11 (default) |
| 2 | 5×2 | 6×3 | ~13×7 |
| 3 | 7×3 | 8×4 | ~9×5 |

## File format

Line-oriented text, so a map is diffable and hand-editable. Rows are read by index and
short lines are treated as trailing blanks, so an editor that strips trailing whitespace
cannot corrupt a map.

```
VTT 3
name Goblin Ambush
size 16 9
zoom 1
scale 5         # feet per tile
metric alt
ruleset daggerheart
tiles           # one line per row
vedges          # w+1 chars per row
hedges          # w chars per row, h+1 rows
token player 2 2 1 "Aria"
tokenstatus red "Poisoned"
token enemy 10 4 2 "Ogre"
```

A `tokenstatus` line hangs a marker on the token above it, so the attachment needs no index
to go wrong.

| terrain | char | | boundary | char |
|---------|------|-|----------|------|
| void | space | | none | space |
| floor | `.` | | wall | `\|` (or `-`) |
| water | `~` | | door | `+` |
| rough | `:` | | open door | `/` |
| brush | `"` | | window | `%` |
| wood | `=` | | secret door | `S` |
| hazard | `^` | | open secret | `s` |

Version 2 added terrain and the boundary kinds. A version 1 reader would take a closed door
for an opening and water for a hole, so it refuses the file rather than misreading a sealed
room as open. Version 3 added status markers: an older reader would ignore those lines and
silently drop them, losing combat state from a saved fight, so it refuses too. Each version
still loads everything older, and an unrecognised character reads as empty rather than
failing the load.

## Performance

The design target is that a keystroke costs as little as possible. Two things do most of
the work: the event loop drains every pending byte before rendering once, so a held key
collapses into a single frame; and the renderer diffs a cell buffer and emits one `write()`
per frame.

The diff rests on one invariant: the front buffer means *what the terminal has actually
received*. So a frame that does not fully reach the terminal must not advance it —
otherwise the missing cells are diffed away on every later frame and stay wrong on screen.
Writes wait out backpressure rather than dropping the tail, and if a write is ever short
anyway the next frame repaints in full. The terminal descriptors are deliberately left
blocking: in a terminal stdin and stdout are the same open file description, so making
stdin non-blocking makes writes to stdout fail with `EAGAIN` under load. The event loop
polls before each read instead.

Measured with `make bench` on a 200×200 map:

| terminal | frame p50 | cells changed | bytes written |
|----------|-----------|---------------|---------------|
| 80×24 | 34 µs | 7 | 110 |
| 100×30 | 50 µs | 7 | 110 |
| 200×50 | 146 µs | 7 | 112 |

Idle cost is zero: `poll()` blocks until there is input.

`F12` shows the live overlay — per-zone timings with p50/p99, a frame-time sparkline, and
the cells-changed and bytes-written counters that predict perceived latency better than
wall-clock alone. The overlay reports its own cost as a `prof.overlay` zone, since an
instrument that quietly adds to the number it displays is worse than no instrument.
`--trace out.json` writes a Chrome Tracing profile for perfetto.

## Design notes

See `docs/PLAN.md` for the architecture.

## License

MIT — see [LICENSE](LICENSE).
