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
  markers, counters (HP and the like), notes, saved characters to place again, scenes to
  reset an encounter, a ruler and area-of-effect templates.
- Turn order or a spotlight, clocks, dice and named rolls, a session log.
- Fog of war that the party's creatures reveal as they move.
- A players' view in any browser on the local network, with tap-to-point, text handouts and
  whispers to one player.
- Tools for AI agents to build and edit maps, from a file or live alongside the GM.

Written in C11 with no dependencies beyond libc, POSIX and `-lm`.

## Getting started

vtt needs a C11 compiler, `make`, and a terminal with UTF-8 (`--ascii` if it cannot draw
box characters). It is built and tested on Linux. Players need only a browser.

```sh
make                              # build (release, -O2)
./vtt                             # the menu: open or create a map
./vtt crypt.vtt                   # open a map file directly
cp vtt ~/.local/bin/              # optional: run it as plain `vtt` from anywhere
```

The examples below say `vtt`; from the build directory that is `./vtt`. Press `?` at any
time for every key available where you are.

**Where maps are kept.** The menu lists the maps in the current directory and in
`~/.local/share/vtt/maps` (`$XDG_DATA_HOME/vtt/maps`). A map created with **New Map**, and a
bare name given to `:w` or `:e` (`:w crypt`, `:e town`), is in `~/.local/share/vtt/maps`. To
use another folder, give a path: `:w ./crypt.vtt`, `:e ~/games/town.vtt`, or `vtt
~/games/town.vtt`.

A first session:

1. Choose **New Map**, give it a name and a size. It starts as a floored room with a wall
   around it.
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
make fuzz-json  # libFuzzer on the JSON reader imports use
```

## Options

```
vtt [options] [map.vtt]

  --ascii            use ASCII instead of box-drawing characters
  --seed N           seed the dice, for a repeatable session
  --serve [PORT]     start the players' view at startup (:serve does it later)
  --stay-alive       keep that server running when the map closes
  --no-pings         ignore taps from the players' phones
  --watch ADDRESS    mirror a serving vtt in this terminal, read-only; ADDRESS is
                     what :serve shows (host:port?k=code, http:// or not)
  --agent            open the control channel at startup (:agent on does it later)
  --ctl [REQUEST]    send a request to a running vtt and print the answer
                     (no REQUEST, or -: read it from stdin)
  --ctl-pid N [REQ]  with several vtts running, send to the one with pid N
  --import-adversaries FILE
                     the Daggerheart SRD's adversaries as characters with cards
                     (see Cards); --force replaces ones already saved
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
  --bench-loops N    how many times --bench replays it (default 50)
  --bench-clients N  attach N players' views to a --bench run
  --bench-names      those views are named phones, P1, P2... (for :whisper)
  --bench-record FILE  save the stream the first view is sent (tools/pagebench.sh)
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

**Labels.** Column letters and row numbers are shown around the map; `#` toggles them. The
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
| `0` `$` `gg` `G` | start / end of the row, top / bottom of the map (or of the floor shown) |
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
| `enter` | wall around the anchored shape |
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
| `:stamp` | choose from your saved stamps (see [the picker](#the-picker)) |

Saved stamps are map files in `~/.local/share/vtt/stamps/` (or `$XDG_DATA_HOME/vtt/stamps/`).
Names may use letters, digits, `-` and `_`. The current stamp is kept when you close a map,
so you can copy from one map to another.

**Named areas.** Select a box with `v`, then `:area Crypt` to name it. `:area Crypt` jumps to
it later (in build or play mode), `:areas` lists them, and `:area Crypt remove` removes the name.
A name draws nothing on the map; it is used by `--describe` and by agents to refer to places
(`token add enemy Crypt "Ghoul"`). Area names are shown only on the GM's screen.

**Notes.** `s n` opens a note on the cursor's square. Enter text and press `enter`; to remove
a note, clear the text (`Ctrl-u`) and press `enter`. Noted squares are marked `”` in build
mode. A map holds up to 64 square notes. Notes are never shown to the players.

## Play mode

Play mode is for running the encounter. `F2` switches to it.

**The selected creature.** Keys that act on a creature (`c`, `r`, `s a`, `s v`, `s i`, `s t`
and others) act on the *selected* one: the creature you last placed or picked up,
jumped to with `t` `f` `e` or `/`, or gave the turn. It is ringed on the map and named on the
status line, and it stays selected when the cursor moves away. `esc` clears the selection;
with nothing selected, those keys act on the creature under the cursor. `b` resizes only a
selected creature; with none it sets the size of the next one placed.

| key | action |
|-----|--------|
| `h` `j` `k` `l` | move the cursor, or the creature being carried |
| `i p` `i e` | place a player / an enemy (asks for a label) |
| `i t p` `i t e` | place a saved character as a player / an enemy ([characters](#characters-character-i-t)) |
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
| `s h` | hide the creature from the players, or show it again |
| `s v` | edit the selected creature's counters |
| `s k` | write the creature's [card](#cards-s-k-card) in your editor |
| `<` `>` | decrease / increase the current counter by one (`3<` by three) |
| `:dmg 11` | mark [damage](#damage-dmg) on its HP, by its card's thresholds |
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
| `g e` | a [group effect](#group-effects-g-e) at the cursor: who it catches |
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
route around walls, so the two differ when a wall is in the way. Each key press is one undo
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
creature; the status line spells them out. A creature holds up to four.
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

**Hidden creatures.** `s h` hides creatures from the players: everything in a `v` box while one
is open, otherwise the selected creatures, otherwise the creature under the cursor. A hidden
creature is not drawn on their screens, even on lit ground, and the turn panel shows it as `?`.
On your screen it is drawn dimmed. `s h` again shows it; with several, `s h` hides them all
unless all are already hidden, in which case it shows them all. `u` undoes it. `:hidden` lists
the hidden creatures and where they are.

While any creature is hidden, the players' screens show no status messages and their status
line leaves out the count of creatures, as over fog. A hidden creature does not block other
creatures' movement or bend their routes; what happens when one walks into it is your call. A
hidden player creature still reveals fog around it, but the players' screens do not follow it
to another floor. Copies, pastes and stamps keep a creature hidden.

**Notes.** `s n` opens a note on the selected creature, or on the cursor's square if no
creature is selected. Notes are never shown to the players; the status line shows `(note)`
when the cursor is on one. `:notes` lists where they are.

## Characters (`:character`, `i t`)

A character is a creature saved to place again, on any map: a Ghoul with its
counters, note and attacks, typed once. Put the cursor on a creature (or select it) and
save it:

| command | action |
|---------|--------|
| `:character save` | save the creature under the cursor, named after its label (`Crypt Ghoul 2` saves as `Crypt-Ghoul`) |
| `:character save NAME` | save it as NAME, replacing one of the same name |
| `:character save NAME ROLL...` | save it with named rolls from this map (`:character save ghoul claw bite`) |
| `:character` `:character NAME` | open the picker (with NAME typed in) and place one on the side it was saved on |

`i t e` opens the picker and places the character you choose at the cursor as an enemy; `i t p`
places it as a player. It keeps its size, label, note and counters (full, however hurt it was
when saved); a label already in use gets a number (`Crypt Ghoul 2`). A saved roll the map lacks
is added to the map; a roll the map already has by that name is kept, and the status line says
so if the two differ. `u` takes back the creature and the rolls it added. Status markers, the
turn order and hidden are not saved: a character arrives visible, and `s h` hides it.

Saved characters are map files in `~/.local/share/vtt/characters/` (or
`$XDG_DATA_HOME/vtt/characters/`). Names may use letters, digits, `-` and `_`.

### The picker

`:stamp`, `:character` and `i t` open a list of what you have saved. Type to narrow it: a name
or a label matches anywhere, ignoring case, and the closest matches come first.

| key | in the picker |
|-----|---------------|
| letters | narrow the list |
| `tab` `shift-tab` | fill in the highlighted name; press again for the next / previous match |
| `up` `down` (`Ctrl-p` `Ctrl-n`) | move the highlight |
| `enter` | take the highlighted one |
| `esc` | cancel |

## Cards (`s k`, `:card`)

A card is what you keep about a kind of creature: its stat block, its tactics, anything you
want in front of you while it acts. In play mode the selected creature's card shows in a box
beside the map, and only on your screen: the players never see it.

```
╭─ Acid Burrower ─────────────────────────────╮
│ Acid Burrower - Tier 1 Solo                 │
│ A horse-sized insect with digging claws.    │
│ Difficulty: 14   Thresholds: 8/15   HP: 8   │
│ Attack: +3   Claws (Very Close) 1d12+2 phy  │
│                                             │
│ Relentless (3) - Passive: The Burrower can… │
╰───────────────────────────────────────── … ─╯
```

| key or command | action |
|----------------|--------|
| `s k` | write the selected creature's card (else the one under the cursor's) in your editor |
| `:card` | show the card whole; `j` `k` `ctrl-d` `ctrl-u` `g` `G` scroll, `esc` closes |
| `:card off` `:card on` | hide or show the box beside the map |

`s k` opens the card in your editor (`$VISUAL`, else `$EDITOR`, else `vi`), the way git opens a
commit message. A creature without a card starts from a skeleton: under Daggerheart, the
stat block's lines ready to fill in; otherwise its label. Below a line of dashes marked `>8`
is help, left out when you save; a line of your own starting with `#` is kept. Save and quit
to keep the card; quit without saving, or empty it, to change nothing. `**word**` shows bold
and `_word_` shows plain. Card edits are not undone by `u` (and `u` never takes a card away).
`$VISUAL` or `$EDITOR` may name the editor with its flags or in quotes, as for git. Bytes that
are not UTF-8 are kept as the replacement character, and a card is at most 4 KB.

Every creature of a kind shares one card: a new card goes to the creature and to the others on
its side with its name (`Goblin 2`, `Goblin 3`) that have none, and editing it changes it for all of
them. Cards are saved with the map, and a saved character keeps its card, so placing one
brings its card too (a map that has its own card by that name keeps it, and says so).

### Importing the SRD's adversaries

```sh
vtt --import-adversaries adversaries.json
```

reads the Daggerheart SRD's adversary list as JSON -- such as `adversaries.json` from the
[daggerheart-srd](https://github.com/seansbox/daggerheart-srd) project -- and saves each
adversary as a character with its stat block as its card: an enemy, 1×1, HP and Stress full.
Place one with `i t e` and the picker; typing a type (`solo`, `minion`) finds them. Characters
already saved are kept unless `--force` is given. vtt includes no SRD content; the SRD's license
asks that it be credited where it is used.

## Damage (`:dmg`)

`:dmg` marks damage rolled at the table on a creature's HP counter. It works on the selected
creature, else the one under the cursor. The dice stay on the table: you type the total.

| command | action |
|---------|--------|
| `:dmg 11` | mark 11 damage |
| `:dmg 6+4` | damage from several sources at once, added up first |
| `:dmg 11 half` | the creature resists it: half, rounded up, before the thresholds |
| `:dmg massive on` `:dmg massive off` | play the massive damage rule (saved with the map) |

Under Daggerheart, the HP marked follow the thresholds on the creature's
[card](#cards-s-k-card), from its `Thresholds: 8/15` line (the SRD's own `**Thresholds:** 8/15 |`
works too): below Major 1 HP, at Major 2, at Severe 3, and with massive damage on, at twice
Severe 4. 0 damage marks none. A creature with `Thresholds: 4/None` has no Severe threshold,
so it marks 2 at most. The status line
says why, on your screen only:

```
Goblin: 11 is Major (8/15) - 2 HP marked, 3/5 left
```

At 0 HP the creature is defeated; vtt says so and leaves it on the map for you to remove. Under
any other ruleset `:dmg 11` takes 11 off HP. `u` undoes it.

**Minions.** A card with a `Minion (3)` line (the SRD's adversaries have one) is defeated by any
damage, and every 3 damage defeats another Minion within range of the attack. Which ones is
your call, so vtt lists the Minions on the same side that are still up, nearest first, and
marks none of them:

```
Rat 1: 7 defeats it (Minion 3) - 7 damage defeats 2 more Minions within the attack's range: Rat 2 (Very Close), Rat 3 (Far)
```

`:dmg 1` defeats each one you pick.

**Hordes.** A card with a `Horde (1d4+1)` line has a weaker attack once half or more of its HP
is marked. The hit that gets it there says so, and the card box's title shows the new attack
for as long as it lasts: `Pirate Raiders - attack now 1d4+1`.

**Group effects.** While `g e` is up, `:dmg` hits every creature the burst catches, each against
its own thresholds, as one undo step. Creatures with no HP or no thresholds are named as
skipped. `esc` takes the burst down to damage one creature again.

If a creature has no HP counter, `s v` sets one (`hp 5`); if its card has no thresholds, `s k`
adds a `Thresholds: 8/15` line.

## Scenes (`:scene`)

A scene is the creatures as they stand, saved under a name on the map and put back later:
reset an encounter, or set up "before" and "after the ambush" in advance. It keeps every
creature's place, size, side, label, markers, counters, note, hidden setting and place in
the turn order, and the fight's round and spotlight.

| command | action |
|---------|--------|
| `:scene save NAME` | save every creature on the map as scene NAME (replacing one of the same name) |
| `:scene save NAME` with a `v` box up | save only the creatures in the box |
| `:scene NAME` | put the scene back |
| `:scene` | choose a scene to put back from [the picker](#the-picker) |
| `:scenes` | list the scenes |
| `:scene NAME remove` | throw the scene away |

Putting a scene back replaces every creature on the map with the scene's, and sets the
round and spotlight. A scene saved with a box replaces only the creatures in that box and
leaves the rest of the map, the round and the spotlight alone; if someone outside the box
has the turn, they keep it. If a creature outside the box now stands where one of the
scene's would go, nothing changes and the status line says which. Putting a scene back is
one undo step. Neither saving nor putting back works while you are carrying a creature.

A scene does not change the map itself: terrain, walls, doors (open or closed), notes and
fog stay as they are. Scene names may contain spaces; a map holds up to 16. Scenes are
saved in the map file, and messages about them are never shown to the players.

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
| `:link 3 remove` | remove it |
| `:link ladder` | the kind `g l` makes next |
| `:link to crypt Entrance` | a link from the cursor (the brush's size) to the area Entrance, or a square, in crypt.vtt |

Several changes can go on one line (`:link 3 portal oneway secret`). Every change can be
undone with `u`. The status line names the link under the cursor and where it leads
(`stairs 3 to K12`).

**Links to another map.** A link can lead to another map file: the town gate to the
dungeon, a portal to another plane. In build mode, set the brush to the end's size, put the
cursor on it and type `:link to crypt Entrance`. The end is here; the other map is
`crypt.vtt` in the same folder as this map, and the party arrives in its named area
`Entrance` (or at a square: `:link to crypt C4`). The map must have been saved, and vtt
checks that the other map and the place exist. The end shows its number with an arrow
(`≡4→`). Kinds, `secret` and `remove` work as for any link; such a link has one end here and
always runs one way.

In play mode, `g o` on the end takes everyone standing on it to the other map, in the same
formation, as close to the middle of the area (or to the square) as they fit. Both maps are
saved: this one without them, the other with them; the players' devices follow.
Creatures that travel keep their markers, counters, notes and hidden setting, but leave the
turn order. A handout comes down; the session log carries on. The trip is refused, and
nobody moves, if the other map has no room for them as they stand, if it has unsaved work
from a crash (open it with `:e` first), or if this map has never been saved. A trip cannot
be undone with `u`; the way back is a link in the other map, made the same way. The status
line says when the map you arrive in has no way back yet.

## Floors

A building with several floors, or a set of separate locations, can be drawn on one map and
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

While a floor is shown, only that floor is drawn and changed: the view, the cursor, the brush,
stamps and a carried creature all stop at its edge. `[` and `]` wait while a `v` box, a wall
mode anchor or the ruler is out (`esc` first). The status line names the floor. Whenever the cursor
lands on another floor, the view goes with it: taking a link with `g o`, jumping with `:K12` or
`:link 3`, finding a creature with `t` `f` `e`, or passing the turn. A square outside every
floor shows the whole map. Which floor you are looking at is not saved with the map.

**The players' floor.** When a map has floors, the players' screens show the party's floor:

- In a fight, when a player creature's turn starts, both your screen and the players' go to
  that creature's floor. When an enemy's turn starts, only your screen goes to it; the players'
  screens stay where they are.
- Under a spotlight ruleset, when the spotlight passes to the players, both screens go to the
  party's floor (or to the floor the players are pinned to); when it passes to you, only your
  screen goes, to the floor with the enemies.
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
Cones, lines and squares point toward the cursor.

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

### Group effects (`g e`)

`g e` shows a group effect: a circle centered on the cursor that moves with the cursor, for
finding where a spell or blast should land. The status line says who is caught and, when a
creature is selected, how far the center is from it, so you can check the effect's own
range:

```
Very Close burst at I6, Far from Aria (15 ft, 3 sq) - 2 caught: Ogre, Goblin
```

The circle's size is the ruleset's area size: Very Close under Daggerheart, whose group
effects reach that far around one point unless they say otherwise. A count names another
band (`3ge` is Close). Without a ruleset it is one square around, and a count is squares
(`4ge`). `g e` again or `esc` removes it. A group effect and the range highlight replace each
other.

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
| `:turns` | list the order; `:turns end` ends the fight |
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
:clock Dragon remove  remove it
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
:rolls                   list            :roll attack remove   remove it
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
:fog Crypt remove       remove the patch
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
| `:serve` | start serving on a free port; the status line shows the address and join code. Typed again while serving, it shows them again without restarting |
| `:serve 7777` | serve on a fixed port, the same every session; if already serving on 7777, show the address again. A different port restarts the server, with a new join code |
| `:serve --stay-alive` | keep serving after the map closes; `--no-stay-alive` reverts |
| `:serve --no-pings` | ignore taps from phones; `--pings` accepts them again |
| `:serve off` | stop serving and disconnect everyone |
| `:mirror` | open a second terminal window showing the players' view |
| `:player preview` | show the players' view on the GM's screen; `q` returns |
| `:player camera follow\|party\|hold` | what the players' view looks at: [the camera](#the-players-view-serve-mirror) |
| `vtt --watch ADDRESS` | show the players' view in a terminal on another machine; ADDRESS is the one `:serve` shows, join code included |

**Connecting a phone or tablet.** The device needs a browser and the same Wi-Fi network as the
GM's machine.

1. Open the map, press `F2`, and type `:serve`. The status line shows an address such as
   `http://192.168.1.10:41873/?k=482913`.
2. Open that address, including the `?k=` join code, in the device's browser. Landscape
   orientation works best.
3. The map appears and follows the GM's screen.

The page is served by vtt and needs no internet connection. It scales the map to the screen,
keeps the screen awake and reconnects automatically. The port and the join code are new
each time the server starts; with `:serve 7777` the port stays the same.

If the device cannot connect, check that the GM machine's firewall allows the port (with a
firewall, use a fixed port such as `:serve 7777` and allow that one), and that
the network does not isolate wireless clients from each other (common on guest networks).
The join code keeps out other devices on the network; the connection is not encrypted.

A line in the bottom-left corner says when the page is connecting or has lost the
connection, and is hidden otherwise. If a device seems slow, add `&stats` to its address
(`...?k=482913&stats`): the line then shows the screen's size in cells, how long the last
frame took to draw, and how many cells it changed.

**Lifetime.** Closing the map stops the server and disconnects everyone, unless the server was
started with `--stay-alive`, in which case it carries on into the next map opened. Opening
another map with `:e` does not stop it.

**Pinging.** A ping rings a square on every screen for two seconds and names it on the status
line (`ping at C4`). Players ping by tapping a square on their device (at most one ping per
second per device). The GM pings with `g p`: the cursor's squares, or the selection. Over fog,
players see a ping only on squares they can see.

**The camera.** The players' screens show what yours shows, unless you give them a camera of
their own. It is the same for every phone and for the terminal mirror on a TV.

| command | action |
|---------|--------|
| `:player camera follow` | the players' screens follow yours (the default) |
| `:player camera party` | the players' screens keep every player creature they can see in view |
| `:player camera hold` | the players' screens stay on what yours shows now, while you look elsewhere |
| `:player camera` | say which camera is on |

With `party`, the view moves only when a creature comes within two squares of its edge, and
then centers on the whole party. It uses your zoom, or zooms out until the party fits with
room to move. With no player creature on their floor, it shows the floor's middle. If
they are too far apart to fit at all, it shows the creature whose turn it is. Your cursor
shows on their screens when it is in their view.

With `hold`, your cursor never shows on their screens. Typing `:player camera hold` again
moves their view to what yours shows now: scout ahead, find the room, then show it. If your
screen is on another floor, their screens go to that floor too (with every floor shown, the
floor under your cursor); leaving `hold` lets them follow the party's floor again, unless you
pinned it yourself with `:player floor` meanwhile.
Opening another map, or taking a link to one, puts a held camera back to `follow`.

While the players have a camera of their own, their status line is left blank (it describes
your cursor), taps from phones land on the square the player tapped, and your title bar says
`CAM PARTY` or `CAM HOLD`.

**Handouts.** A handout is a short text card on the players' devices: an inscription, a
letter, a riddle. It appears over the map in a readable font, whatever the map's size on
the screen. Write prepared handouts as plain text files in
`~/.local/share/vtt/handouts/NAME.txt` (or `$XDG_DATA_HOME/vtt/handouts/`), up to 2 KB each;
line breaks are kept.

| command | action |
|---------|--------|
| `:handout NAME` | show NAME.txt on every player's screen, titled NAME |
| `:handout` | choose a handout from [the picker](#the-picker) |
| `:handout say TEXT` | show a line of text made up on the spot |
| `:handout off` | take the handout down |
| `:handout on` | show the last handout again |

While a handout is up the title bar says `HANDOUT`, and `:player preview` shows the card as
the players see it. A player can close the card with `×` and reopen it with the `handout`
button for as long as it is up; showing a new handout opens it again on every screen. A
device that connects while a handout is up gets it too, and the terminal mirror shows it as
a box. A handout stays up in build mode, and comes down when the map is closed.

**Whispers.** A whisper is a handout for one player: only the phones with that player's name
get it.

| command | action |
|---------|--------|
| `:whisper NAME TEXT` | show TEXT on NAME's phone only, as a card marked *to you* |
| `:players` | list the phones watching, by name, and any whisper waiting for one |

When a phone first connects it asks *Who are you?*: it offers every player creature on the map
(by label), a box for any other name (up to 24 bytes), and *just watching*. The phone remembers the answer and
sends it every time it reconnects; the name button in the top-left corner changes it. Your
status line says when a named phone arrives (`Aria's phone is here`).

`:whisper Aria You notice the floor is warm here` sends to every phone named Aria (case does not
matter, and a name may have spaces: `:whisper Crypt Ghoul ...`). If Aria's phone has been
here but is not connected now -- a locked phone drops off within a minute -- the whisper
waits and arrives when it reconnects; the last one for each name is kept. A name no phone has
used is refused. The player closes the card with `×` and reopens the last whisper with the
`whisper` button. Whispers never appear in the shared view, in the terminal mirror or on
another phone; the session log keeps them on your side. Whispers still waiting are dropped
when the map closes.

Anyone with the join code can say they are Aria, and two phones with the same name both get
its whispers. The join code is the only gate: this is for a table of friends.

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
unexpectedly, the next time the map is opened it offers to recover the changes. The copy
covers vtt or its terminal closing. A power cut or a computer that stops without shutting
down can lose the last half minute of unsaved work, so save with `:w` when you pause. A
copy the power cut left incomplete is not recovered: vtt says so and keeps it as
`name.vtt.autosave.damaged`.

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
instead of initiative, the default direction of new clocks, the counters its creatures
use, and how `:dmg` marks HP. Available: `none`, `daggerheart`.

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
- **Movement under pressure.** While a creature is carried, the status line says what
  moving that far takes, by the band of the straight distance from where it started:

  | band | a player | an adversary |
  |------|----------|--------------|
  | Melee to Close | part of an action roll | free with an action |
  | Far, Very Far | Agility Roll to move | a separate action |

  A player moving within Close outside an action roll needs the Agility Roll too; the line
  cannot know which it is.

  ```
  MOVING  Aria 1x1  8 steps  from B2  walls on  Far: Agility Roll to move
  ```

  The players' screens show it too.
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
- **Group effects** (`g e`) are Very Close around their center unless a count names another
  band.
- **Duality dice.** A bare `:roll` or `:roll +2` rolls two d12s, Hope and Fear, and reports
  the total and which die was higher:

  ```
  :roll +2             Duality +2 = 17 with Hope  [hope 9, fear 6]
  :roll                Duality = 14 critical success  [hope 7, fear 7]
  ```

  The Hope die is shown in gold and the Fear die in purple. `:roll duality +2` makes the same
  roll under any ruleset.
- **Counters**: HP, Stress and Armor.
- **Damage thresholds.** `:dmg` marks HP by the card's `Thresholds:` line, and knows the
  `Minion (X)` and `Horde (X)` features (see [Damage](#damage-dmg)).

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
| `:area NAME` | name the selection, or jump to a named area; `:areas` lists, `:area NAME remove` removes |
| `:stamp ...` | save, load and choose [stamps](#build-mode) |
| `:character ...` | save and place [characters](#characters-character-i-t) |
| `:scene ...`, `:scenes` | save, put back and list [scenes](#scenes-scene) |
| `:handout ...` | show a [handout](#the-players-view-serve-mirror) on the players' screens; `:handout off` takes it down |
| `:whisper NAME TEXT` | a [whisper](#the-players-view-serve-mirror) to one player's phone; `:players` lists who is watching |
| `:link ...`, `:links` | change, remove, list and jump to [links](#links-stairs-ladders-trapdoors-portals) |
| `:floor ...`, `:floors` | mark, show and list [floors](#floors) |
| `:turns` | list the [turn order](#turn-order-a); `:turns end` ends the fight |
| `:panel` | show / hide the side panel |
| `:clock ...`, `:tick ...` | [clocks](#clocks-clock-tick) |
| `:dmg ...` | mark [damage](#damage-dmg) on a creature's HP |
| `:notes` | list where notes are |
| `:hidden` | list the [hidden creatures](#play-mode) |
| `:fog ...` | [fog of war](#fog-of-war-fog) |
| `:serve ...` | the [players' view](#the-players-view-serve-mirror); `:serve off` stops it |
| `:player preview` | show the players' view on your screen; `:player floor NAME\|auto` pins their [floor](#floors); `:player camera follow\|party\|hold` their camera |
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

### Asking an agent

With the control channel on (`:agent on`), an agent's changes come to you as **proposals**:
nothing lands on your map until you accept it.

1. **Ask.** In build mode, draw a `v` box round the place you mean and type `:ask` and what
   you want, such as `:ask make this a flooded crypt with two ghouls`. The box is tinted
   with a `#3` label: job 3. Keep working, inside the box too. `:ask` works without a box,
   and in play mode.
2. **The agent works.** When it takes the job the status line says `#3 taken by an agent`,
   and it may tint the squares it will work in. A line it sends shows on the status line.
3. **It is ready.** The tint changes color and the status line says `#3 ready: ...
   - :review 3`.
4. **Review.** `:review 3` (or `:review` for the oldest) draws the change in place on your
   map. Squares in red would overwrite something that changed since the agent looked,
   usually your own edits. Then:

   | key | does |
   |---|---|
   | `enter` | accept it, as one undo step |
   | `v`, move, `enter` | accept only what lies wholly inside the box; the rest stays ready |
   | `d` | scrap it (`:review 3` brings it back until the map closes) |
   | `c` | send it back with a line of what to do instead |
   | `n` / `N` | the next or the previous ready change |
   | `u`, `ctrl-r` | undo and redo as anywhere; the review stays open |
   | `esc` | drop the box, then leave; the change stays ready |

An agent may also propose a change you did not ask for; it comes the same way, ready for
`:review`.

| command | does |
|---|---|
| `:ask TEXT` | ask for a change, over the `v` box if there is one |
| `:ask! TEXT` | the same, accepted at once when it arrives (still one undo step) |
| `:jobs`, `:jobs N` | list the jobs; one job's history (your words, the agent's, each proposal) |
| `:review [N]` | review a ready change |
| `:ask N remove` | take a job away |
| `:agent accept auto` | every change an agent sends lands at once, one undo step each; `:agent accept review` goes back to reviewing |

A change waiting to land at once never lands while you are typing, answering a prompt,
drawing a wall or in play mode: it lands at your next key once you are back.

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
| `W020 clamped` | a setting out of range, replaced by the nearest valid value; a card over 4 KB, cut there |
| `W022 edge-row-short` | a `vedges` row missing its east boundary |
| `W023 link-dropped` | a link with an end off the map, its ends overlapping, or on another link's squares |
| `W024 floor-dropped` | a floor naming no area, marked twice, or overlapping another |
| `W025 scene-dropped` | a scene with a bad name or box, a name used twice, a seventeenth, or no `endscene` |
| `W026 after-end` | lines after an `end` line, which ends the file |
| `W027 name-cleaned` | control characters in the map's name, taken out |
| `W028 card-dropped`, `card-cut` | a card with a bad or repeated name, a sixty-fifth, one with no `endcard`, or a stray `endcard` |
| `W029 unknown-rule` | a `rule` line naming a rule other than `massive`, ignored |
| `W102 door-to-void` | a door or window leading into void |
| `W103 wall-in-void` | a wall with no map on either side |
| `W104 door-loose` | a door or window with no wall at either end |
| `W113 duplicate-label` | two creatures with the same name |
| `W120 unreachable-room` | a room no door leads to from the party's start |
| `W121 party-split` | a player creature in such a room |
| `W130 fog-patch-empty` | a fog patch with no squares |
| `W140 note-on-void` | a note on a square that is not map |
| `W150 link-on-void` | a link with an end on a square that is not map (nobody can be sent there) |
| `W151 link-to-nowhere` | a link to another map whose file is not beside this one, or has no such area or square, or the square is void |
| `W160 floors-overlap` | two floors sharing squares |
| `N021 row-short` | rows shorter than their section, read as trailing blanks |
| `N105 door-off-map` | a door or window on the edge of the map |
| `N131 fog-patch-disabled` | a disabled fog patch |

**`--json`** gives `--describe` or `--check` as JSON, with 0-based coordinates alongside the
square names, room `area` names and `floor`s, the lists of `areas`, `floors` and `links`, and
each finding's `line` and `column`. `--describe` names each room's floor, lists its link ends and where they lead, and counts a room
reached only by a link as reachable (a one-way link only in its direction).

## Control channel (`:agent`, `vtt --ctl`)

The control channel lets an agent or script read the map open in a running vtt and propose
changes to it (see *Asking an agent*).
`:agent on` opens it (or `--agent` at startup) and `:agent off` closes it. It listens on a
Unix socket in `$XDG_RUNTIME_DIR/vtt/` that only the same user can access.

`vtt --ctl REQUEST` sends a request and prints the answer; with no request it reads one from
stdin. It exits 0 on success, 1 on an error (the reason is printed on stderr), and 2 when no
vtt is listening. With several vtts listening, `--ctl-pid N` chooses
one.

```
$ vtt --ctl <<'EOF'
room Crypt K2:O6
door Crypt west
token add enemy Crypt "Ghoul"
EOF
proposal #1: ground in K2:O6, walls and doors in K2:P7, Ghoul added, area Crypt added
waiting for the GM's review - :review 1
```

`vtt map.vtt --apply plan.txt` runs the same requests against a map file without a live
session and saves it; `--new WxH` creates an empty map first if the file does not exist.
Don't use `--apply` on a map that is open in vtt: vtt doesn't notice the file changing,
and your next `:w` writes over the plan's changes. Send the plan to the open map instead:
`vtt --ctl < plan.txt` after `:agent on`.

A request's edits become one proposal, made only if every line in it succeeds, and land as a
single undo step when accepted. Under `:agent accept auto` a request lands at once, with a
summary on the status line and a highlight around the changed squares; if you are in play
mode or in the middle of something (typing a command, drawing a wall), it lands at your next
key once you are back. `vtt --ctl undo` takes back an agent's last accepted change while
nothing has happened since. The full request language is in [docs/AGENTS.md](docs/AGENTS.md).

## File format

Maps are plain text, one record per line:

```
VTT 13
name Goblin Ambush
size 16 9
zoom 1
scale 5
metric alt
ruleset daggerheart
rule massive
tiles
vedges
hedges
token player 2 2 1 "Aria"
tokenstatus red "Poisoned"
tokenturn 18 acting
tokennote "wants the amulet"
tokencounter HP 4 6
token enemy 10 4 2 "Ogre"
tokenhidden
tokenturn 12
tokencard "Ogre"
round 2
spotlight gm
clock Dragon 3 6
clock Fuse 4 4 down
roll attack "2d12+3"
note 5 3 "pressure plate"
card "Ogre"
| Ogre - Tier 2 Bruiser
| Difficulty: 14   Thresholds: 10/20
endcard
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
Coordinates in the file are 0-based `x y`. The file has no comment syntax. `rule massive` is there while
`:dmg massive on` is. A card's text
follows its `card` line a line at a time after `| `; a long line continues on lines starting
`+ `. A recovery
autosave ends with a line `end`, which shows it was written whole; reading stops there.

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
| `tokenhidden` | the preceding token is hidden from the players |
| `tokencounter NAME VALUE MAX` | a counter on the preceding token |
| `round N`, `spotlight gm` | the fight's round; the GM has the spotlight |
| `clock NAME FILLED SIZE [down]` | a clock |
| `roll NAME "EXPR"` | a named roll |
| `note X Y "text"` | a note on a square |
| `area X0 Y0 X1 Y1 "Name"` | a named area |
| `floor "Name" LEVEL` | the named area is a floor at that level |
| `link N KIND SIZE X0 Y0 X1 Y1 [oneway] [secret]` | link number N between the SIZE×SIZE blocks whose top-left squares are X0,Y0 and X1,Y1; one-way links run from the first |
| `link N KIND SIZE X0 Y0 to "MAP" "PLACE" [secret]` | link number N from the block at X0,Y0 to PLACE (an area's name or a square) in MAP.vtt beside this file |
| `scene "Name" [X0 Y0 X1 Y1]` ... `endscene` | a [scene](#scenes-scene); the creature lines and `round`/`spotlight gm` between them are the scene's, not the map's |
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
| 10 | hidden creatures |
| 11 | scenes |
| 12 | links to other maps |

vtt reads every version up to 12 and refuses newer files.

## Performance

vtt draws only what changed since the last frame and writes each frame to the terminal in a
single call. Holding a key coalesces into one frame per batch of input, and an idle vtt uses
no CPU. Drawing cost depends on the size of the window, not of the map.

| | 80×24 | 200×50 |
|---|---|---|
| build mode, 200×200 map | 31 µs | 128 µs |
| play mode, 24 creatures | 32 µs | 114 µs |

`F12` shows a live profiler overlay with per-zone timings. [docs/PERFORMANCE.md](docs/PERFORMANCE.md)
has measurements for every path; `make perf` regenerates them.

## Documentation

| | |
|---|---|
| [docs/AGENTS.md](docs/AGENTS.md) | for AI agents: building and editing maps |
| [docs/KEYS.md](docs/KEYS.md) | the rules for choosing key bindings |
| [docs/CHARACTERS.md](docs/CHARACTERS.md) | character templates and the picker |
| [docs/CARDS.md](docs/CARDS.md) | cards, and importing the SRD's adversaries |
| [docs/SCENES.md](docs/SCENES.md) | scenes: saving and restoring an encounter |
| [docs/HANDOUTS.md](docs/HANDOUTS.md) | handouts on the players' screens |
| [docs/WHISPER.md](docs/WHISPER.md) | phones' names and whispers to one player |
| [docs/MAPLINKS.md](docs/MAPLINKS.md) | links between map files |
| [docs/CONTROL.md](docs/CONTROL.md) | the control channel's design |
| [docs/REMOTE.md](docs/REMOTE.md) | the players' view: server, page and watcher |
| [docs/FLOORS.md](docs/FLOORS.md) | floors: the view, the players' floor, and room for stacked layers |
| [docs/FOG.md](docs/FOG.md) | fog of war and sight |
| [docs/PERFORMANCE.md](docs/PERFORMANCE.md) | performance measurements |
| [docs/ROADMAP.md](docs/ROADMAP.md) | features decided on, in the order they will be built |
| [docs/IDEAS.md](docs/IDEAS.md) | features considered and not built |
| [docs/HEALTH.md](docs/HEALTH.md) | project health checks: what each found, and what is still open |

## License

MIT — see [LICENSE](LICENSE).
