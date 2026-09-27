# vtt

A rules-agnostic virtual tabletop for the terminal.

vtt is for running encounters at a physical table. The GM builds a map in a keyboard-driven
editor, then switches to play mode to move creatures, measure distances, track turns and
reveal the map as the party explores it. Players can follow along on their phones or a
second screen. The core knows no game's rules; a map can name a ruleset to add
game-specific readouts such as range bands (see [Rulesets](#rulesets)).

```
┏━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┓
┃   │   │   │   │   │   │   │   │   │   ┃
┃───┏━━━━━━━━━━━━━━━┓───┼───┼───┼───┼───┃
┃   ┃   │(A)│   │   ┃   │   │   │   │   ┃
┃───┃───┼───┼───┼───╹───┼───┏━━━━━━━┓───┃     ━━  wall
┃   ┃   │   │   │   │   │   ┃ Ogre  ┃   ┃     ──  grid line
┃───┗━━━━━━━━━━━━━━━┛───┼───┃       ┃───┃     (A) player
┃   │   │   │   │   │   │   ┗━━━━━━━┛   ┃     [ ] enemy
┗━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┛
```

**Features**

- A vim-style map editor: walls, doors, windows and secret doors on the boundaries between
  squares; terrain; box and circle selections; reusable stamps; named areas; floors shown one
  at a time, and links (stairs, ladders, portals) between them.
- Play mode: creatures of 1-3 squares, movement with route and distance readouts, status
  markers, counters (HP and the like), notes, a ruler and area-of-effect templates.
- Turn order or a spotlight, clocks, dice and named rolls, a session log.
- Fog of war that the party's creatures reveal as they move.
- A players' view in any browser on the local network, with tap-to-point.
- Tools for AI agents to build and edit maps, from a file or live alongside the GM.

Written in C11 with no dependencies beyond libc, POSIX and `-lm`.

## Getting started

```sh
make            # build (release, -O2)
./vtt           # the menu: open or create a map
./vtt crypt.vtt # open a map directly
```

Maps are kept in the current directory and in `~/.local/share/vtt/maps`. Press `?` at any
time for every key available where you are.

A first session:

1. Choose **New Map**, give it a name and a size. It starts as a floored room with a wall
   round it.
2. Draw: `w` enters wall mode, `space` puts the pen down, `hjkl` lays walls, `esc` returns.
   To add a door, press `t` until the status line says `boundary: door`, then `H` `J` `K` or `L` puts
   one on that side of the cursor's square.
3. Press `F2` for play mode. `i p` places a player, `i e` an enemy. Move the cursor onto a
   creature, `enter` to pick it up, `hjkl` to move, `enter` to put it down.
4. `:w` saves. `F1` returns to build mode.

**Other build targets**

```sh
make debug      # -Og -g3 with ASan and UBSan
make test       # unit and golden-frame tests
make perf       # performance measurements (see docs/PERFORMANCE.md)
make fuzz       # libFuzzer on the map loader (clang); FUZZ_SECONDS=600 for longer
make fuzz-ctl   # libFuzzer on control-channel requests
```

## Options

```
vtt [options] [map.vtt]

  --ascii            use ASCII instead of box-drawing characters
  --seed N           seed the dice, for a repeatable session
  --serve [PORT]     start the players' view at startup (:serve does it later)
  --stay-alive       keep that server running when the map closes
  --no-pings         ignore taps from the players' phones
  --watch HOST:PORT  mirror a serving vtt in this terminal, read-only
  --agent            open the control channel at startup (:agent on does it later)
  --ctl [REQUEST]    send a request to a running vtt and print the answer
                     (no REQUEST, or -: read it from stdin)
  --ctl-pid N [REQ]  with several vtts running, send to the one with pid N
  --apply FILE       run FILE's requests against the map and save it
  --new WxH          with --apply, create an empty map of this size if the file is missing

  map tools (print a report and exit; see Map tools):
  --dump-map         the whole map as text, with a legend
  --region A1:P9     only that part of it
  --describe         the rooms, their doors and their contents
  --check            find mistakes: exit 0 clean, 1 findings, 2 unreadable
  --json             --describe or --check as JSON

  testing and profiling:
  --script PATH      replay a keystroke script instead of reading the terminal
  --dump-frame       render one frame as plain text and exit
  --size WxH         screen size for headless modes (default 80x24)
  --bench PATH       replay a script headlessly and report frame statistics
  --bench-loops N    how many times --bench replays it (default 400)
  --bench-clients N  attach N players' views to a --bench run
  --bench-pings      and have each of them ping every frame
  --bench-ctl FILE   run a control-channel request at the start of each --bench loop
  --trace PATH       write a Chrome Tracing profile on exit
```

Script files contain the bytes a terminal would send, with `\e` for escape, `\r` enter, `\t`
tab, `\xHH` any byte, and `\.` a pause. A pause is needed after `\e` when it is meant as the
Escape key: `\e` followed immediately by a key is read as Alt+key.

## The map

A map is a grid of squares. Columns are lettered `A`…`Z`, `AA`, `AB`…; rows are numbered
from 1. `C6` is the third column of the sixth row. Every readout uses these names, and `:c6`
jumps to that square.

**Boundaries.** Walls, doors and windows sit on the boundary between two squares, not on a
square. Each kind answers movement and sight separately:

| kind | drawn | stops movement | stops sight |
|------|-------|----------------|-------------|
| wall | `━` solid white | yes | yes |
| door | `═` double, amber | yes | yes |
| open door | thin, amber | no | no |
| window | `┅` dashed, cyan | yes | no |
| secret door | as a wall in play mode; `╳` in build mode | yes | yes |

To make one, choose its kind with `t` and lay it with `H` `J` `K` `L` (or with the pen in wall
mode). `o` opens and closes a door beside the cursor; `O` does the same for a secret door. In
play mode a closed secret door is drawn exactly as a wall.

**Terrain.** Floor, water, rough, brush, wood and hazard are all walkable and behave
identically; the difference is only in how they are drawn. Movement costs are left to the
table. Void, the absence of map, is not walkable and is marked with a dim dot in each square.

**Measurement.** A square is 5 feet by default (`:scale N` changes it). Four distance
metrics are available, set per map with `:metric`:

| metric | 4×3 offset | description |
|--------|-----------|-------------|
| `alt` | 25 ft | diagonals alternate 1 and 2 squares (5-10-5). The default |
| `chebyshev` | 20 ft | every step costs one square, diagonals included |
| `euclidean` | 25 ft | straight-line distance |
| `manhattan` | 35 ft | no diagonal movement |

**Zoom.** `+` and `-` change the zoom level (0-3, default 1). Each square takes a block of
screen cells plus the one-cell boundary it shares with its neighbor:

| level | square | squares visible in 80×24 |
|-------|--------|--------------------------|
| 0 | 1×1 | ~39×11 |
| 1 | 3×1 | ~19×11 |
| 2 | 5×2 | ~13×7 |
| 3 | 7×3 | ~9×5 |

**Labels.** Column letters and row numbers are shown round the map; `#` toggles them. The
cursor's row and column are highlighted. At tight zoom levels the labels are shown for every
second or third column.

## Keys

`?` opens a page listing every key for what you are doing now, followed by the other modes.
Scroll it with `j`/`k` or `Ctrl-d`/`Ctrl-u`; `g`/`G` jump to the ends; `q` or `esc` closes it.
The bar at the bottom of the screen shows the most-used keys for the current mode.

The keys follow vim where vim has an equivalent: `d` deletes, `y` yanks, `p` pastes, `c`
changes, `u` and `Ctrl-r` undo and redo, `/` `n` `N` search, `:` opens the command line. A
letter used in both build and play mode does the same job in both.

- **Counts.** A number before a key repeats it or selects a value: `3j` moves three squares,
  `2b` sets the cursor to 2×2.
- **Prefixes.** `i`, `s` and `g` start two-key commands (`i p`, `s a`, `g p`). Press the
  prefix alone to see its options on the status line.
- **`esc`** cancels one thing at a time, innermost first. **`enter`** confirms.
- `F1` build mode, `F2` play mode, `F12` the profiler overlay.

**Menu and file browser**

| key | action |
|-----|--------|
| `j` `k`, arrows | move |
| `enter` | select / open |
| `g` `G` | first / last map |
| `R` | rename the selected map |
| `c` | duplicate the selected map |
| `d` | delete the selected map (asks first) |
| `r` | rescan for maps |
| `esc` `q` | back / quit |

Renaming changes the file name and the map's title together, and will not overwrite an
existing map. Duplicating copies the file as `name copy`, `name copy 2` and so on. Deleting
removes the file permanently and asks first.

## Build mode

Build mode is for drawing the map. `F1` switches to it.

| key | action |
|-----|--------|
| `h` `j` `k` `l`, arrows | move the cursor |
| `10j` | a count repeats a motion |
| `0` `$` `gg` `G` | start / end of the row, top / bottom of the map |
| `Ctrl-d` `Ctrl-u` | half a page down / up |
| `:d6` | jump to a square (`:6` jumps to row 6, keeping the column) |
| `[` `]` | show the [floor](#floors) below / above |
| `z` | center the view on the cursor |
| `+` `-` | zoom in / out |
| `#` | row and column labels on / off |
| `b` `B` | brush size 1×1, 2×2, 3×3; `2b` sets it directly |
| `f` | paint the current terrain over the brush or selection |
| `T` | choose the terrain `f` paints |
| `x` | clear the brush or selection to void |
| `space` | toggle the cursor's square between floor and void |
| `H` `J` `K` `L` | toggle the wall on the west / south / north / east side of the cursor |
| `t` | choose the boundary kind `H` `J` `K` `L` lay |
| `o` `O` | open or close a door / secret door beside the cursor |
| `w` | wall mode |
| `v` `V` | select a box / a circle |
| `y` `p` | copy the brush or selection / paste it as a stamp |
| `s n` | a note on this square |
| `g f` `g c` | paint the current fog patch / clear fog |
| `g l` | make a [link](#links-stairs-ladders-trapdoors-portals): press on one end, then on the other |
| `m` | ruler |
| `u` `Ctrl-r` | undo / redo |
| `:` | command line |
| `q` | close the map |

**The brush.** `f`, `x` and `space` act on the whole brush (the cursor's footprint, set with
`b`), and `H` `J` `K` `L` wall the whole of that side of it. If every boundary on that side is
already the chosen kind, the key clears them instead.

**Selections.** `v` starts a box and `V` a circle at the cursor; move the cursor to size it.
Pressing the other key switches shape and keeps the anchor; pressing the same key again
cancels. A circle is centered on its starting square, and the status line shows its radius
(`circle r4`). With a selection open, `f`, `x`, `y`, `g f` and `g c` act on it.

**Wall mode (`w`).** The cursor moves along the corners between squares, and each step
crosses one boundary. With the pen down, every boundary crossed becomes a wall, so you draw a
room by walking its outline. A pen-down stroke is one undo step.

| key | action |
|-----|--------|
| `h` `j` `k` `l` | move corner to corner, laying wall when the pen is down |
| `space` | pen up / down |
| `t` | choose what the pen lays (wall, door, window, …) |
| `d` | erase instead of lay |
| `v` `V` | anchor a box / circle at this corner |
| `enter` | wall round the anchored shape |
| `esc` | clear the anchor, or leave wall mode |

**Stamps.** A stamp is a piece of map you can place again: a pillar, a table and benches, a
stretch of cave wall. `y` copies the brush or selection: its terrain, its walls, doors and
windows, the creatures wholly inside it, its square notes and the links with both ends inside it
(not fog). `p` then shows the
copy on the cursor as a preview:

| key | while previewing a stamp |
|-----|--------------------------|
| `h` `j` `k` `l` | move it; the cursor is its top-left square |
| `r` `R` | rotate a quarter turn clockwise / counterclockwise |
| `\|` | mirror left to right |
| `p` `enter` | place it (one undo step) |
| `:` | run a command and return to the preview (`:J6` moves it to J6) |
| `esc` | cancel |

Void squares and empty boundaries in a stamp leave the map underneath unchanged, so a pillar
stamp adds only its walls. A stamp that would run off the map, put a creature on void or on
another creature, or exceed the map's note limit is refused, and the preview stays up.
Copied creatures keep their notes and counters but not their status markers or place in the
turn order; a label already in use gets a number (`Ogre 2`).

| command | action |
|---------|--------|
| `:stamp save NAME` | save the current stamp (replacing one of the same name) |
| `:stamp NAME` | load a saved stamp and preview it |
| `:stamp NAME -f` | load and place it at the cursor immediately |
| `:stamp` | list saved stamps |

Saved stamps are map files in `~/.local/share/vtt/stamps/` (or `$XDG_DATA_HOME/vtt/stamps/`).
Names may use letters, digits, `-` and `_`. The current stamp is kept when you close a map,
so you can copy from one map to another.

**Named areas.** Select a box with `v`, then `:area Crypt` to name it. `:area Crypt` jumps to
it later (in build or play mode), `:areas` lists them, and `:area Crypt off` removes the name.
A name draws nothing on the map; it is used by `--describe` and by agents to refer to places
(`token add enemy Crypt "Ghoul"`). Area names are shown only on the GM's screen.

**Notes.** `s n` opens a note on the cursor's square. Enter text and press `enter`; to remove
a note, clear the text (`Ctrl-u`) and press `enter`. Noted squares are marked `”` in build
mode. A map holds up to 64 square notes. Notes are never shown to the players.

## Play mode

Play mode is for running the encounter. `F2` switches to it.

| key | action |
|-----|--------|
| `h` `j` `k` `l` | move the cursor, or the creature being carried |
| `i p` `i e` | place a player / an enemy (asks for a label) |
| `b` `B` | cursor size 1×1, 2×2, 3×3; also resizes the selected creature |
| `enter` | pick up / put down the creature under the cursor |
| `v` | select several creatures with a box |
| `d` `x` | remove (the creature is kept for `p`) |
| `y` `p` | copy / paste creatures |
| `c` | change the selected creature's label |
| `t` `T` | next / previous creature (in turn order during a fight) |
| `f` `F` | next / previous player creature |
| `e` `E` | next / previous enemy |
| `tab` `shift-tab` | same as `t` / `T` |
| `/` `n` `N` | search labels / next / previous match |
| `s a` `s c` `s d` | add a status marker / choose its color / remove one |
| `s n` | note on the selected creature, or on the square if none is selected |
| `s v` | edit the selected creature's counters |
| `<` `>` | decrease / increase the current counter by one (`3<` by three) |
| `s i` | set initiative (a blank answer removes the creature from the turn order) |
| `s t` | give the turn to the selected creature |
| `a` `A` | next / previous turn |
| `m` | ruler |
| `r` `R` | range highlight / its shape |
| `o` `O` | open or close a door / secret door beside the cursor |
| `g r` `g h` | reveal / hide fog under the cursor or selection |
| `g R` `g H` | reveal / hide the whole fog patch under the cursor |
| `g p` | ping the cursor's squares on every screen |
| `g o` | send the creatures on this [link](#links-stairs-ladders-trapdoors-portals)'s end to the other end |
| `Ctrl-w` | turn movement blocking off / on |
| `esc` | cancel: close the selection, cancel a move, clear the range, deselect |
| `u` `Ctrl-r` | undo / redo |
| `:d6` | jump to a square |
| `[` `]` | show the [floor](#floors) below / above |

`V`, `P` and `S` show which key to use instead (`v`, `p`, `s c`).

**Placing creatures.** `i p` or `i e` asks for a label and places a creature at the cursor,
at the cursor's size (`b`). A square can hold only one creature.

**Moving creatures.** Put the cursor on a creature and press `enter` to pick it up. Move it
with `hjkl` and press `enter` to put it down, or `esc` to return it to where it started.
While it is carried:

- `◆` marks the starting square.
- A green trail shows the shortest walkable route from the start to the current square.
- The distance moved is shown beside the creature: the range band if the map has a ruleset
  (`20 ft  Close`), otherwise squares and feet (`4 sq  20 ft`).
- The status line shows the number of steps along the route.

The distance is the straight-line distance by the map's metric; the step count follows the
route round walls, so the two differ when a wall is in the way. Each key press is one undo
step. A creature can pass through creatures on its own side but not through enemies; walls
and closed doors block both. `Ctrl-w` turns all blocking off.

With a large cursor over several creatures, `enter` cycles through them; the first movement
key picks up the highlighted one.

**Selecting several.** `v` starts a box; move the cursor to cover the creatures you want.
Then:

- `enter` moves them together. If any one of them is blocked, none of them moves. `esc`
  returns them all to their starting squares.
- `y` copies them and `d` removes them; `p` pastes them in the same formation at the cursor,
  or refuses if any of them would not fit.
- `v` or `esc` closes the box.

Selected creatures are outlined in their own color.

**Finding creatures.** `t` cycles through all creatures, `f` through players, `e` through
enemies; the capitals go backwards. During a fight they follow the turn order. The view
scrolls to the selected creature and the status line names it. `/` searches labels (any part,
any case: `gob` finds `Goblin 3`); `n` and `N` repeat the search. An empty search repeats the
last one.

**Copying.** `y` copies the creature under the cursor and `p` pastes it; each copy is
numbered (`Goblin`, `Goblin 2`, `Goblin 3`). `d` removes a creature and keeps it, so `d` then
`p` moves it; the first paste after `d` keeps the original label. Pasted creatures have no
status markers.

**Status markers.** `s a` adds a marker to the selected creature: a word of your choice in a
color chosen with `s c`. Markers are drawn as the first letter of the word above the
creature; the status line spells them out. A creature shows four, with more continuing below.
`s d` removes one, asking which when there are several.

**Counters.** `s v` edits numbers kept on the selected creature, such as hit points. Each
counter has a name, a value and a maximum; a creature holds four. The prompt accepts one or
more changes separated by commas:

```
hp 6          new counter at 6/6, or set an existing one to 6
hp 4/8        set value and maximum
hp -2         subtract 2;  stress +1  add 1
armor         make it the current counter
-hp           remove the counter
```

`<` and `>` change the current counter (the last one named, or the ruleset's first) by one,
or by a count (`3<`). Values stay between zero and the maximum. Counters are shown on the
GM's screen only: in the status line for the selected creature, and in the turn panel for
the creature whose turn it is. A ruleset can name standard counters (Daggerheart: HP, Stress,
Armor).

**Notes.** `s n` opens a note on the selected creature, or on the cursor's square if no
creature is selected. Notes are never shown to the players; the status line shows `(note)`
when the cursor is on one. `:notes` lists where they are.

## Links (stairs, ladders, trapdoors, portals)

A link joins two places on the map so that creatures can be sent from one to the other: a
staircase to the floor above, a ladder down a well, a portal to a room somewhere else. Lay out
each floor or location on the same map, separated by void, and join them with links.

**Making a link.** In build mode, put the cursor on one end and press `g l`, move to the other
end and press `g l` again. `esc` cancels after the first press. Each end is the size of the
brush: set `b` to 2 or 3 first for a 2×2 or 3×3 link, such as a wide portal a whole party can
step onto. Both ends must be on map squares, must not overlap each other, and cannot share a
square with another link. A map holds up to 64 links. If you later clear ground under an end,
the link stays, trips onto it are refused, and `--check` reports it.

Each link has a kind, which sets the symbol drawn on its ends: `≡` stairs, `‡` ladder, `□`
trapdoor, `◎` portal. New links are stairs; `:link portal` changes the kind the next `g l`
makes. Each link also has a number, drawn beside the symbol on each end's first square so you
can see which ends belong together. A link keeps its number for as long as it exists.

**Taking a link.** In play mode, put the cursor on a link's end, or on a creature standing on
one, and press `g o`. Every creature with at least one square on that end moves to the other
end, keeping its position relative to the end, so a party standing on a 3×3 portal arrives in
the same formation. The cursor moves with them. The trip is one undo step.

The trip is refused, and nobody moves, if any creature would land off the map, on void, or on
a creature that is not moving; the status line names the square. `Ctrl-w` lifts the void and
creature checks. If you are carrying a creature, `g o` puts it down first.

**One-way and secret links.** A one-way link can only be taken from its first end (the end
where `g l` was first pressed). A secret link is never drawn in play mode or shown to the
players; the GM's status line still names it when the cursor is on one. Links are otherwise
visible to the players, except for ends that fog hides.

| command | action |
|---------|--------|
| `:links` | list the links |
| `:link 3` | jump to link 3's first end; again for the other end |
| `:link 3 portal` | change its kind (`stairs`, `ladder`, `trapdoor`, `portal`) |
| `:link 3 oneway`, `:link 3 twoway` | make it one-way or two-way |
| `:link 3 reverse` | swap its ends, so a one-way link runs the other way |
| `:link 3 secret`, `:link 3 seen` | hide it from the players, or show it |
| `:link 3 off` | remove it |
| `:link ladder` | the kind `g l` makes next |

Several changes can go on one line (`:link 3 portal oneway secret`). Every change can be
undone with `u`. The status line names the link under the cursor and where it leads
(`stairs 3 to K12`).

## Floors

A building with several storeys, or a set of separate locations, can be drawn on one map and
shown one part at a time. Draw each floor as its own block of the map, separated by void, name
it as an area, and mark it as a floor with a level:

```
:area Ground        (with the ground floor selected with v)
:floor Ground 0
:floor Upper 1
:floor Cellar -1
```

The level orders the floors: `]` shows the floor above the one on screen and `[` the floor
below. `:floor all` shows the whole map again, and `:floor Upper` shows one floor directly.
Drawing floors the same size, with their stairs in the same place, keeps the cursor on the
stairs when you step between them.

While a floor is shown, only that floor is drawn, the view and the cursor stay inside it, and
a carried creature stops at its edge. The status line names the floor. Whenever the cursor
lands on another floor, the view goes with it: taking a link with `g o`, jumping with `:K12` or
`:link 3`, finding a creature with `t` `f` `e`, or passing the turn. A square outside every
floor shows the whole map. Which floor you are looking at is not saved with the map.

**The players' floor.** When a map has floors, the players' screens show the party's floor:

- In a fight, when a player creature's turn starts, both your screen and the players' go to
  that creature's floor. When an enemy's turn starts, only your screen goes to it; the players'
  screens stay where they are.
- Under a spotlight ruleset, when the spotlight passes to the players, both screens go to the
  party's floor; when it passes to you, only your screen goes, to the floor with the enemies.
- Otherwise the players see the party's floor.

The party's floor (and the enemies', for the spotlight) is chosen in this order: the floor
already shown, if one of that side's creatures is still on it; the floor with the most of
them; the floor where one of them last moved or took a link; the lowest floor.

`:player floor Cellar` keeps the players' screens on a floor until a player creature's turn
starts; `:player floor auto` returns them to following the party. When their floor is not the
one on your screen, their status line is left blank, and a ping from a phone on a floor you
are not looking at is named on your status line (`ping on Upper at K12`).

| command | action |
|---------|--------|
| `:floor NAME LEVEL` | make the named area a floor at that level (-99 to 99) |
| `:floor NAME off` | stop it being a floor (the area stays) |
| `:floor NAME`, `:floor all` | show that floor / the whole map |
| `:floors` | list the floors, top first |
| `:player floor NAME`, `:player floor auto` | pin the players' screens to a floor / follow the party |

Floors cannot overlap. A room inside a floor keeps its own area name.

## Measuring

### Ruler (`m`)

`m` anchors a ruler at the cursor (in play mode, at the creature under it). Move the cursor to
measure; the reading is shown beside the cursor and on the status line:

```
RULER   6 tiles  30 ft  Close  sight blocked  [chebyshev]
```

| key | action |
|-----|--------|
| `h` `j` `k` `l` | move the far end |
| `enter` | add a waypoint, to measure a path that bends |
| `backspace` `u` | remove the last waypoint |
| `M` | cycle the distance metric |
| `m` | re-anchor at the cursor |
| `esc` | stop measuring |

"Sight blocked" means a wall, closed door or secret door crosses the straight line from the
anchor.

### Range highlight (`r`)

`r` highlights every square within reach of the selected creature, or of the cursor if none
is selected. The highlight stays anchored to that creature as it moves.

- **Without a ruleset**, each `r` extends the reach by one square; a count sets it directly
  (`20r` is 100 ft at 5 ft per square).
- **With a ruleset**, `r` steps through its range bands and then off; `2r` selects the second
  band.

`R` changes the shape (circle, cone, line, square), or a count selects one (`2R` is the cone).
Cones, lines and squares point towards the cursor.

| shape | covers |
|-------|--------|
| circle | every square within reach, by the map's metric |
| cone | within reach and within a cone as wide at any point as it is far from the origin |
| line | within reach, one square wide |
| square | a square with sides as long as the reach, its near edge at the origin |

A square is included when its center is; a creature is included when any of its squares is.
Squares in range but out of line of sight are shaded more faintly. The status line lists the
creatures caught, marking those out of sight:

```
Close (30 ft, 6 sq) from Aria - 3 in range: Ogre, Goblin*, Bram   * no line of sight
```

`esc` removes the highlight. Selecting a different creature also removes a highlight
anchored to a creature.

## Running a fight

### Turn order (`a`)

Give creatures initiative with `s i`. The highest number acts first; ties go to the creature
placed first. `a` advances to the next turn and `A` goes back; a count moves several turns.
Passing the last creature starts a new round. The view moves to the creature whose turn it is
and selects it.

| key or command | action |
|----------------|--------|
| `s i` | set the selected creature's initiative; a blank answer removes it from the order |
| `a` `A` | next / previous turn |
| `s t` | give the turn to the selected creature, in or out of order |
| `:turns` | list the order; `:turns off` ends the fight |
| `:panel` | show / hide the side panel |

Advancing the turn is recorded in the undo history and the session log, so `u` reverts it.
Removing the creature whose turn it is passes the turn to the next. Initiative is saved with
the map.

The title bar shows the round and the order (`Round 2 - Ogre's turn, then Aria, Bram`). On
terminals 80 columns or wider, a panel on the right lists the order, marks the creature
acting with `▶`, and shows how many creatures are not in the fight:

```
│ Turn order
│ Round 2
│
│ ▶  18  Aria
│    15  Ogre
│    15  Bram
│
│ 2 not in the fight
```

**Spotlight.** Under a ruleset without initiative (such as Daggerheart), the turn belongs to
a side, the players or the GM. `a` passes it to the other side and `s t` gives it to the
selected creature's side. Setting anyone's initiative with `s i` switches back to an order.

### Clocks (`:clock`, `:tick`)

A clock is a named row of segments that you fill or empty by hand: a countdown, a progress
track, "three more rounds until the roof comes in". A clock counts up (empty to full) or down
(full to empty). Nothing advances automatically.

```
:clock Dragon 6       create a six-segment clock, or resize Dragon
:clock Fuse 4 down    create a countdown
:clock Storm d8       roll a d8 for the size and start there
:tick                 advance the clock last started or ticked by one
:tick Dragon          advance Dragon by one
:tick Dragon 2        advance by two;  -1 goes back one;  =3 sets it to 3
:tick Dragon reset    return to the start
:clock                list clocks
:clock Dragon off     remove it
```

Names are one word; any unique prefix works (`:tick dr`). A map holds eight clocks. They are
shown in the side panel. Ticks are undoable; creating, resizing and removing a clock are not.
Clocks are saved with the map.

### Dice (`:roll`)

```
:roll 2d6+3          2d6+3 = 9  [4 2]
:roll d20            d20 = 17  [17]
:roll 4d6 + 1d4 - 1  4d6+1d4-1 = 15  [3 6 2 4 1]
```

Any sum of dice and constants, up to 100 dice of up to 1000 sides per group. Each die is
shown. `--seed N` makes the rolls repeatable. A bare `:roll` or `:roll +2` makes the ruleset's
action roll (Daggerheart's duality roll); without a ruleset it asks for an expression.

**Named rolls** are saved with the map:

```
:roll attack = 2d12+3    save            :roll attack     roll it
:roll swing = duality +2 the action roll with a modifier
:rolls                   list            :roll attack =   remove it
```

A unique prefix works (`:roll att`). Dice expressions always take precedence over names. A
map holds sixteen named rolls.

## Fog of war (`:fog`)

Fog hides parts of the map from the players until their creatures can see them. It is made of
**patches**: named groups of squares, each with its own settings. Squares in no patch are
always visible.

```
:fog Crypt              create a patch (or select one) for g f to paint
:fog Crypt 3            how far a creature reveals it, in squares  (manual: by hand only)
:fog Crypt memory off   hide squares again once no one can see them
:fog Crypt clear        reveal the whole patch;  hide  covers it again
:fog Crypt disable      keep the patch but hide nothing;  enable  restores it
:fog Crypt delete       remove the patch
:fog all                one patch over the whole map
:fog on | off           turn fog on or off without losing the patches
:fog --soft-edge        show the edge of the dark (below);  --no-soft-edge  hides it
:fog Crypt --soft-edge  the same for one patch
:fog                    list the patches
```

**Painting.** In build mode, `g f` paints the current patch over the brush or selection and
`g c` clears fog from it. Build mode shows each patch in its own color.

**Revealing.** Player creatures reveal fog around them as they move, up to each patch's
reveal distance, along lines not blocked by walls, closed doors or secret doors. Enemies
reveal nothing. In play mode, `g r` and `g h` reveal and hide the squares under the cursor or
selection by hand, and `g R` and `g H` the whole patch under the cursor. Squares revealed by
hand stay revealed until hidden again.

**Memory.** With memory on (the default), squares the party has seen stay visible after they
move away, though creatures on them are shown only while someone can see them. With memory
off, squares go dark again as soon as no one can see them.

**What the players see.** Fogged squares are not drawn at all: no floor, walls, creatures or
void markers. Walls between a visible and a fogged square are drawn from the visible side.
Creatures in fog are shown as `?` in the title bar and turn panel. The cursor, the ruler,
the range highlight and status messages are hidden from the players while they concern fogged
squares. The GM sees fogged areas shaded dark blue, with everything in them.

**Soft edge.** With `:fog --soft-edge`, the squares bordering what the party can see are
partly shown: their walls, dimmed (doors appear as walls until the square beyond has been
seen), and any creature on them as a gray `?` silhouette of its size.

## The players' view (`:serve`, `:mirror`)

Players can watch the map on their own devices. vtt serves the **players' view**: play mode
as the GM sees it, without anything that is the GM's alone (prompts, dialogs, notes, counters,
fogged areas). While the GM is in build mode or a menu, the players' view keeps its last frame.

| command | action |
|---------|--------|
| `:serve` | start serving; the status line shows the address and join code. Typed again while serving, it shows them again without restarting |
| `:serve 7777` | serve on a fixed port; if already serving on 7777, show the address again. A different port restarts the server, with a new join code |
| `:serve --stay-alive` | keep serving after the map closes; `--no-stay-alive` reverts |
| `:serve --no-pings` | ignore taps from phones; `--pings` accepts them again |
| `:serve off` | stop serving and disconnect everyone |
| `:mirror` | open a second terminal window showing the players' view |
| `:player preview` | show the players' view on the GM's screen; `q` returns |
| `vtt --watch HOST:PORT` | show the players' view in a terminal on another machine |

**Connecting a phone or tablet.** The device needs a browser and the same Wi-Fi network as the
GM's machine.

1. Open the map, press `F2`, and type `:serve`. The status line shows an address such as
   `http://192.168.1.10:7777/?k=482913`.
2. Open that address, including the `?k=` join code, in the device's browser. Landscape
   orientation works best.
3. The map appears and follows the GM's screen.

The page is served by vtt and needs no internet connection. It scales the map to the screen,
keeps the screen awake and reconnects automatically. The join code is new each time the
server starts; with `:serve 7777` the port stays the same.

If the device cannot connect, check that the GM machine's firewall allows the port, and that
the network does not isolate wireless clients from each other (common on guest networks).
The join code keeps out other devices on the network; the connection is not encrypted.

**Lifetime.** Closing the map stops the server and disconnects everyone, unless the server was
started with `--stay-alive`, in which case it carries on into the next map opened. Opening
another map with `:e` does not stop it.

**Pinging.** A ping rings a square on every screen for two seconds and names it on the status
line (`ping at C4`). Players ping by tapping a square on their device (at most one ping per
second per device). The GM pings with `g p`: the cursor's squares, or the selection. Over fog,
players see a ping only on squares they can see.

## Sessions and files

### Session log (`:log`)

`:log` starts a plain-text record of what happens at the table; `:log` again stops it. `:log
on`, `:log off` and `:log path/to/file.log` are also accepted. The log is written next to the
map as `name.log` by default.

```
--- 2026-09-16 19:02:11  log on: Crypt ---
[19:02:40] placed enemy Ogre (2x2) at H6
[19:03:05] dropped after 4 steps
[19:03:22] red marker on Aria: Poisoned
[19:03:40] 2d6+3 = 9  [4 2]
[19:04:01] Duality +2 = 17 with Hope  [hope 9, fear 6]
[19:04:15] undo
--- 19:20:03  log off ---
```

It records creatures placed, moved, removed, pasted and relabeled, markers, doors, rolls,
ruleset changes, and undo and redo. Each line is written immediately. Closing the map closes
the log.

### Recovery

Unsaved changes are copied to `name.vtt.autosave` next to the map shortly after you stop
editing. Saving, or discarding changes on purpose, removes the copy. If vtt exits
unexpectedly, the next time the map is opened it offers to recover the changes:

```
╭─ Unsaved work found ───────────────────────────────────────╮
│                                                            │
│  Crypt was still being edited at 21:14 on 20 Sep when it   │
│  was last open, and those changes were never saved.        │
│  Recover them?                                             │
│                                                            │
│  y  recover them      n / esc  let them go                 │
╰────────────────────────────────────────────────────────────╯
```

`y` loads the recovered map (still unsaved: `:w` keeps it); `n` deletes the copy.

## Rulesets

A map can name a ruleset with `:ruleset NAME`. This adds game-specific readouts; it never
enforces rules. The setting is saved with the map. Without a ruleset (`none`, the default),
distances are shown in squares and feet and the range highlight grows one square at a time.

A ruleset defines range bands, the meaning of a bare `:roll`, whether turns use a spotlight
instead of initiative, the default direction of new clocks, and the counters its creatures
use. Available: `none`, `daggerheart`.

### Daggerheart

`:ruleset daggerheart` adds:

- **Range bands**, used by the ruler, the movement readout and the range highlight. The
  thresholds follow the SRD's estimates for playing on a map, at 1 inch = 5 feet:

  | band | threshold | squares at 5 ft |
  |------|-----------|-----------------|
  | Melee | 5 ft | 1 |
  | Very Close | 15 ft | 3 |
  | Close | 30 ft | 6 |
  | Far | 60 ft | 12 |
  | Very Far | beyond Far | 13+ |

  Thresholds are stored in feet, so changing `:scale` keeps them at the same distance.
- **The spotlight** instead of an initiative order (see [Turn order](#turn-order-a)):

  ```
  │ Spotlight
  │
  │ ▶ Players
  │     Aria
  │   GM
  ```
- **Countdown clocks.** New clocks count down; `:clock Fuse 4 up` makes a progress clock. The
  SRD's variants are made by hand: a random start is `:clock Ambush d6`, a looping countdown
  is `:tick Ambush reset`, and a countdown that moves with the fiction is ticked in either
  direction.
- **Duality dice.** A bare `:roll` or `:roll +2` rolls two d12s, Hope and Fear, and reports
  the total and which die was higher:

  ```
  :roll +2             Duality +2 = 17 with Hope  [hope 9, fear 6]
  :roll                Duality = 14 critical success  [hope 7, fear 7]
  ```

  The Hope die is shown in gold and the Fear die in purple. `:roll duality +2` makes the same
  roll under any ruleset.
- **Counters**: HP, Stress and Armor.

## Commands

| command | action |
|---------|--------|
| `:w [name]` | save |
| `:wq` `:x` | save and close |
| `:q` `:q!` | close / close without saving |
| `:e NAME` | open another map (asks if there are unsaved changes) |
| `:name TEXT` | rename the map |
| `:resize WxH` | resize the map (clears the undo history) |
| `:zoom N` | set the zoom level, 0-3 |
| `:scale N` | feet per square (default 5) |
| `:metric NAME` | `alt`, `chebyshev`, `euclidean` or `manhattan` |
| `:ruleset NAME` | set the map's [ruleset](#rulesets) |
| `:c6`, `:6` | jump to a square / a row |
| `:area NAME` | name the selection, or jump to a named area; `:areas` lists, `:area NAME off` removes |
| `:stamp ...` | save, load and list [stamps](#build-mode) |
| `:link ...`, `:links` | change, remove, list and jump to [links](#links-stairs-ladders-trapdoors-portals) |
| `:floor ...`, `:floors` | mark, show and list [floors](#floors) |
| `:turns` | list the [turn order](#turn-order-a); `:turns off` ends the fight |
| `:panel` | show / hide the side panel |
| `:clock ...`, `:tick ...` | [clocks](#clocks-clock-tick) |
| `:notes` | list where notes are |
| `:fog ...` | [fog of war](#fog-of-war-fog) |
| `:serve ...` | the [players' view](#the-players-view-serve-mirror); `:serve off` stops it |
| `:player preview` | show the players' view on your screen; `:player floor NAME\|auto` pins their [floor](#floors) |
| `:mirror` | open a second terminal with the players' view |
| `:agent on` | open the [control channel](#control-channel-agent-vtt---ctl) for an AI agent; `:agent off` closes it |
| `:roll EXPR` | roll dice; `:roll NAME = EXPR` saves a named roll, `:rolls` lists them |
| `:log` | start / stop the [session log](#session-log-log) |
| `:play` `:build` | switch mode |

## Working with AI agents

An AI agent can build and edit maps for the GM, either by writing a plan file and running it
with `--apply`, or live in the GM's session through the control channel.
[docs/AGENTS.md](docs/AGENTS.md) is the complete guide for an agent, including the request
language.

## Map tools (`--dump-map`, `--check`, `--describe`)

These print a report about a map file and exit, without changing it.

**`vtt map.vtt --dump-map`** prints the map as text using the file format's characters, with
column letters and row numbers. `--region B2:K12` limits it to part of the map.

```
   A B C D E
  -----------
1 |1 . .S. .|
  |     |   |
2 |. ~ ~|2 2|
  |     |   |
3 |. . .+2 2|
  -----------
```

Creatures are shown as `1`-`9`, `a`-`z`, `A`-`Z` on every square they cover and listed below
the map with their names, sizes and squares, followed by notes and named areas. On a map with
fog, a second grid shows each square's patch.

**`vtt map.vtt --describe`** lists the map's rooms:

```
room 2 Crypt (C2)  C2:J6  40 squares
  terrain  floor 25  water 8  rough 2  brush 4  hazard 1
  window       J3|K3    to room 1 (A1)
  door         B4|C4    to room 1 (A1)
  enemy        E4       Ghoul
```

A room is an area of ground bounded by walls, doors and windows. Rooms are numbered in reading
order and identified by their first square, and by the smallest named area containing that
square if there is one. Boundaries are named by the squares on either side: `J3|K3` across a
vertical boundary, `F6/F7` across a horizontal one. A room that cannot be reached through
doors from the party's starting room is marked `NOT REACHABLE`.

**`vtt map.vtt --check`** reports mistakes, one per line with a code, and exits 0 when there
are none, 1 when there are, and 2 when the file cannot be read. Errors (`E`) change what the
map means, warnings (`W`) are probable mistakes, and notes (`N`) are allowed but worth
knowing; notes do not affect the exit status.

```
E011 section-short     line 29   'hedges' read as vedges row 9 of 9: the section is short
E101 door-in-void      G5|H5     door between two squares that are not map
W120 unreachable-room  room K2   18 squares, no door leads to it from room B2
2 errors, 1 warning
```

| code | finds |
|---|---|
| `E001 unreadable` | not a map, a bad size, or a newer format |
| `E010 row-long` | a row longer than its section's width |
| `E011 section-short` | a section that consumed the next line, or a file ending inside one |
| `E013 bad-char` | a character not valid in that section (read as empty) |
| `E014 bad-record` | a line that could not be read and was dropped |
| `E101 door-in-void` | a door or window with no map on either side |
| `E110 token-on-void` | a creature on a square that is not map |
| `E111 token-overhang` | a creature extending off the edge of the map |
| `E112 token-overlap` | two creatures on one square |
| `W015 unknown-line` | an unrecognized line |
| `W016 unknown-ruleset`, `W017 unknown-metric` | an unknown setting |
| `W018 fog-unknown-patch` | a fog row naming a patch that is not defined |
| `W019 stray-row` | rows outside any section |
| `W020 clamped` | a setting out of range, replaced by the nearest valid value |
| `W022 edge-row-short` | a `vedges` row missing its east boundary |
| `W023 link-dropped` | a link with an end off the map, its ends overlapping, or on another link's squares |
| `W024 floor-dropped` | a floor naming no area, marked twice, or overlapping another |
| `W102 door-to-void` | a door or window leading into void |
| `W103 wall-in-void` | a wall with no map on either side |
| `W104 door-loose` | a door or window with no wall at either end |
| `W113 duplicate-label` | two creatures with the same name |
| `W120 unreachable-room` | a room no door leads to from the party's start |
| `W121 party-split` | a player creature in such a room |
| `W130 fog-patch-empty` | a fog patch with no squares |
| `W140 note-on-void` | a note on a square that is not map |
| `W150 link-on-void` | a link with an end on a square that is not map (nobody can be sent there) |
| `W160 floors-overlap` | two floors sharing squares |
| `N021 row-short` | rows shorter than their section, read as trailing blanks |
| `N105 door-off-map` | a door or window on the edge of the map |
| `N131 fog-patch-disabled` | a disabled fog patch |

**`--json`** gives `--describe` or `--check` as JSON, with 0-based coordinates alongside the
square names, room `area` names and `floor`s, the lists of `areas`, `floors` and `links`, and
each finding's `line` and `column`. `--describe` names each room's floor. `--describe` lists each room's link ends and where they lead, and counts a room
reached only by a link as reachable (a one-way link only in its direction).

## Control channel (`:agent`, `vtt --ctl`)

The control channel lets an agent or script read and edit the map open in a running vtt.
`:agent on` opens it (or `--agent` at startup) and `:agent off` closes it. It listens on a
Unix socket in `$XDG_RUNTIME_DIR/vtt/` that only the same user can access.

`vtt --ctl REQUEST` sends a request and prints the answer; with no request it reads one from
stdin. It exits 0 on success, 1 on an error or when the GM is busy (the reason is printed on
stderr), and 2 when no vtt is listening. With several vtts listening, `--ctl-pid N` chooses
one.

```
$ vtt --ctl <<'EOF'
room Crypt K2:O6
door Crypt west
token add enemy Crypt "Ghoul"
EOF
changed J2:O6: 3 lines, one undo step
```

`vtt map.vtt --apply plan.txt` runs the same requests against a map file without a live
session and saves it; `--new WxH` creates an empty map first if the file does not exist.

Each request is applied as a single undo step, and only if every line in it succeeds. The GM
sees a summary on the status line and a highlight round the changed squares. Edits are
accepted only in build mode, and not while the GM is in the middle of an action such as
typing a command or drawing a wall. The full request language is in
[docs/AGENTS.md](docs/AGENTS.md).

## File format

Maps are plain text, one record per line:

```
VTT 9
name Goblin Ambush
size 16 9
zoom 1
scale 5
metric alt
ruleset daggerheart
tiles
vedges
hedges
token player 2 2 1 "Aria"
tokenstatus red "Poisoned"
tokenturn 18 acting
tokennote "wants the amulet"
tokencounter HP 4 6
token enemy 10 4 2 "Ogre"
tokenturn 12
round 2
spotlight gm
clock Dragon 3 6
clock Fuse 4 4 down
roll attack "2d12+3"
note 5 3 "pressure plate"
area 1 1 6 4 "Crypt"
area 0 0 15 8 "Ground"
floor "Ground" 0
link 1 stairs 1 5 2 14 2
link 2 portal 2 1 6 12 6 oneway secret
fog on
fogpatch 1 Crypt reveal 2 memory on
fog
```

(The `tiles`, `vedges`, `hedges` and `fog` sections are followed by their rows; see below.)
Coordinates in the file are 0-based `x y`. The file has no comment syntax.

**Sections.** A map `W` squares wide and `H` tall has:

| section | rows | characters per row | content |
|---|---|---|---|
| `tiles` | `H` | `W` | one character per square |
| `vedges` | `H` | `W + 1` | the vertical boundaries in each row, west edge first |
| `hedges` | `H + 1` | `W` | the horizontal boundaries above each row, and below the last |
| `fog` | `H` | `W` | each square's fog patch |

| terrain | char | | boundary | char |
|---------|------|-|----------|------|
| void | space | | none | space |
| floor | `.` | | wall | `\|` (or `-` in `hedges`) |
| water | `~` | | door | `+` |
| rough | `:` | | open door | `/` |
| brush | `"` | | window | `%` |
| wood | `=` | | secret door | `S` |
| hazard | `^` | | open secret door | `s` |

A 4×2 room with a closet behind a door in its north-east corner:

```
size 4 2
tiles
....
....
vedges
|  +|
|  ||
hedges
----
    
----
```

Sections are read by row count. A section with a missing row takes the next line as its last
row, and the sections after it shift; `vtt --check` reports this. Short rows are read as if
padded with spaces.

**Records.**

| record | meaning |
|--------|---------|
| `token KIND X Y SIZE "Label"` | a creature (`player` or `enemy`), anchored at its top-left square |
| `tokenstatus COLOR "Word"` | a status marker on the preceding token; COLOR is `red`, `orange`, `yellow`, `green`, `cyan`, `blue`, `violet` or `gray` (`grey` is read as `gray`) |
| `tokenturn N [acting]`, `tokenturn - acting` | initiative, and whether it is this creature's turn |
| `tokennote "text"` | a note on the preceding token |
| `tokencounter NAME VALUE MAX` | a counter on the preceding token |
| `round N`, `spotlight gm` | the fight's round; the GM has the spotlight |
| `clock NAME FILLED SIZE [down]` | a clock |
| `roll NAME "EXPR"` | a named roll |
| `note X Y "text"` | a note on a square |
| `area X0 Y0 X1 Y1 "Name"` | a named area |
| `floor "Name" LEVEL` | the named area is a floor at that level |
| `link N KIND SIZE X0 Y0 X1 Y1 [oneway] [secret]` | link number N between the SIZE×SIZE blocks whose top-left squares are X0,Y0 and X1,Y1; one-way links run from the first |
| `fog on`, `fog soft-edge` | fog settings |
| `fogpatch N NAME reveal R\|manual memory on\|off` | a fog patch; `fog` rows use `A`-`O` for patches 1-15 (unseen), `a`-`o` (seen), `1`-`9` `!"#$%&` (revealed by hand), `.` for none |

**Versions.** The first line is the format version. A map is written with the lowest version
that can hold its contents:

| version | needed for |
|---|---|
| 3 | terrain, boundaries, creatures and status markers |
| 4 | a fight: initiative, round, spotlight |
| 5 | clocks, named rolls, notes |
| 6 | counters, fog |
| 7 | named areas |
| 8 | links |
| 9 | floors |

vtt reads every version up to 9 and refuses newer files.

## Performance

vtt draws only what changed since the last frame and writes each frame to the terminal in a
single call. Holding a key coalesces into one frame per batch of input, and an idle vtt uses
no CPU. Drawing cost depends on the size of the window, not of the map.

| | 80×24 | 200×50 |
|---|---|---|
| build mode, 200×200 map | 31 µs | 128 µs |
| play mode, 24 creatures | 31 µs | 112 µs |

`F12` shows a live profiler overlay with per-zone timings. [docs/PERFORMANCE.md](docs/PERFORMANCE.md)
has measurements for every path; `make perf` regenerates them.

## Documentation

| | |
|---|---|
| [docs/AGENTS.md](docs/AGENTS.md) | for AI agents: building and editing maps |
| [docs/KEYS.md](docs/KEYS.md) | the rules for choosing key bindings |
| [docs/CONTROL.md](docs/CONTROL.md) | the control channel's design |
| [docs/REMOTE.md](docs/REMOTE.md) | the players' view: server, page and watcher |
| [docs/FLOORS.md](docs/FLOORS.md) | floors: the view, the players' floor, and room for stacked layers |
| [docs/FOG.md](docs/FOG.md) | fog of war and sight |
| [docs/PERFORMANCE.md](docs/PERFORMANCE.md) | performance measurements |
| [docs/IDEAS.md](docs/IDEAS.md) | features considered and not built |

## License

MIT — see [LICENSE](LICENSE).
