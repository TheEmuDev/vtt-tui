# vtt

A rules-agnostic virtual tabletop for the terminal.

Build an encounter map in a vim-like keyboard editor, then flip to play mode and run the
fight on it with labeled player and enemy tokens. The core knows nothing about any specific
ruleset — it is a grid, walls, and tokens — and a map can *name* one to switch on the
rules-aware readouts, such as range bands. See [Rulesets](#rulesets).

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
make fuzz      # libFuzzer on the map loader for a minute (clang); FUZZ_SECONDS=600 for longer
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
  --bench-loops N    how many times the bench replays it (default 400)
  --dump-frame       render one frame as plain text to stdout and exit
  --size WxH         geometry for headless modes (default 80x24)
  --seed N           seed the dice, for a repeatable session or script
  --serve [PORT]     open the remote view at startup (:serve does it later)
  --stay-alive       keep that server up when the map closes
  --watch HOST:PORT  mirror a serving vtt in this terminal, read-only
  --bench-clients N  attach N loopback watchers to a --bench run
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

**How a key is chosen.** The rules every binding so far has followed, written down so the
next one does too:

1. **Vim's key, if vim has one.** `d` `y` `p` `c` `i` `u` `/` `n` `v` `:` mean what they mean
   there. Nothing else may take them.
2. **One meaning in every mode.** A letter that works in build and in play does the same
   job in both — `b` is the cursor's size, `m` measures, `o` is a door. If a key cannot
   mean the same thing in both, it is the wrong key (`1` `2` `3` were sizes until build
   mode needed them as counts; the size moved to `b` in both rather than differ).
3. **Digits are counts, and a count names a value outright.** `3l` moves three; on a key
   that cycles, the count picks — `2b` is 2×2, `2r` the second band, `20r` twenty squares,
   `2R` the cone. A bare press cycles; past the end a count names the last.
4. **Lowercase is the tool, the capital is its variant.** `m` measures and `M` changes the
   metric; `r` is the range and `R` its shape; `v` and `V` are the two selections; `b` and
   `B`, `t` and `T`, `a` and `A` run the same cycle both ways. A capital never starts
   something unrelated.
5. **A family gets a prefix, not a row of keys.** `i p` `i e`, `s a` `s c` `s d`, `g r` `g h`. The
   prefix alone lists its options; `esc` abandons it; it swallows the next key whatever it is.
6. **The cursor is the pointer.** Whatever needs a place or a direction reads the cursor —
   the ruler's far end, the brush's footprint, the range template's aim. There are no
   aiming keys, because `h` `j` `k` `l` already are.
7. **`esc` backs out one layer at a time**, and `enter` commits. Settings survive `esc`;
   what was switched on does not.
8. **Six hints on the bar, everything on `?`**, both read from the one table in
   `src/keys.c`, and anything rare is a `:` command rather than a key.

**Squares are named the way a battle map names them** — columns run `A`, `B` … `Z`, `AA`,
`AB`, and rows count from one, so the token in the third column of the sixth row is on `C6`.
That is what every readout says and what `:c6` jumps to. The file format is unchanged: it
still stores plain 0-based x,y, because that is a format, not something anyone says out loud.

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
| `:d6` | jump to a square (`:6` for a row, keeping the column) |
| `#` | column letters and row numbers, on or off |
| `Ctrl-d` `Ctrl-u` | half-page down / up |
| `H` `J` `K` `L` | toggle the wall on the west / south / north / east face of the cursor tile |
| `w` | wall-tracing mode |
| `m` | measure (ruler) |
| `v` `V` | select a box / a circle |
| `f` `x` | fill selection with floor / clear it to void |
| `b` `B` | brush size, 1×1 → 2×2 → 3×3 — `2b` names it |
| `space` | toggle the cursor tile between floor and void |
| `s n` | a note on this square |
| `g f` `g c` | paint the current [fog](#fog-of-war-fog) patch over the brush or the box / scrub fog off it |
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
deliberately unlike the [range highlight](#range-highlight-r), which follows the map's distance
metric because it measures reach — a tower is a tower whatever the movement rules say.

Wall tracing anchors on a lattice corner rather than a square, which is where its cursor
lives, so its circles sit between squares and come out even across rather than odd.

**Jumping.** `:c6` puts the cursor on `C6` and centres the view there — a jump is for going
somewhere else, and arriving pinned against an edge shows half of where you went. `:12` moves
to row 12 and keeps the column, the way vim's `:12` keeps yours.

There is no collision with the `:` commands, and there could not be: every verb is pure
letters and a square always ends in digits. That is also why the row is required — a bare
column letter would be `:e`, `:w`, `:x` or `:q`, so `:d` is still an unknown command rather
than a jump to column D.

**Labels.** `#` turns the column letters and row numbers on and off; they start on, because
a coordinate you cannot read is a coordinate you cannot jump to. Whichever row and column
the cursor is on is lit, so finding where you are is a glance rather than a count. Where a
zoom is too tight to fit a label over every square, they thin out to every second or third
column the way an axis thins its ticks:

```
      A   B   C   D   E              A B C D E F G H I J K L M
    ┌───┬───┬───┬───┬───┐          ┌─┬─┬─┬─┬─┬─┬─┬─┬─┬─┬─┬─┬─┐
  1 │   │   │   │   │   │        1 │ │ │ │ │ │ │ │ │ │ │ │ │ │
    ├───┼───┼───┼───┼───┤          ├─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┤
  2 │   │   │   │[G]│   │        2 │ │ │ │ │ │ │ │ │ │ │ │ │ │
    └───┴───┴───┴───┴───┘          └─┴─┴─┴─┴─┴─┴─┴─┴─┴─┴─┴─┴─┘
```

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
| `b` `B` | the cursor's size, cycled — `2b` names it; resizes the selected creature too |
| `3l` | counts repeat a motion, the same as build mode |
| `enter` | pick up / put down; under a big cursor, walk what it covers |
| `v` | select several: a box from here to the cursor |
| `d` `x` | remove — and keep it (or them), so `p` puts it back |
| `y` `p` | yank what the cursor or box covers / paste it here |
| `c` | change its label |
| `t` `T` | next / previous token, any kind — in turn order once there is one |
| `f` `F` | next / previous **f**riendly — player tokens only |
| `e` `E` | next / previous **e**nemy |
| `tab`, `shift-tab` | the same as `t` / `T` |
| `/` | find a token by part of its label |
| `n` `N` | next / previous match for the last search |
| `s a` | add a status marker (prompts for a word) |
| `s c` | change the colour the next marker will use |
| `s d` | take a marker off (asks which, when there is more than one) |
| `s n` | a note on the selected creature, or on this square when there is none |
| `s v` | the selected creature's counters: `hp 6`, `hp -2`, `stress 0/6`, `-hp` |
| `<` `>` | one off / one on its current counter — `3<` takes three (see *Counters* below) |
| `g r` `g h` | light / darken the [fog](#fog-of-war-fog) under the cursor or the box, by hand |
| `g R` `g H` | light / darken the whole fog patch under the cursor |
| `a` `A` | next / previous turn — `3a` moves three on (see [Turn order](#turn-order-a)) |
| `s i` | initiative: a number puts the creature in the turn order, a blank takes it out |
| `s t` | hand the turn to this creature, whether or not it is in the order |
| `m` | measure (ruler) |
| `r` | the range highlight: bands, or a square's worth a press (see below) |
| `R` | its shape — circle, cone, line, square; `2R` names one; the cursor aims it |
| `o` `O` | open or close a door / a secret door on this tile |
| `Ctrl-w` | toggle blocking — walls and creatures alike |
| `esc` | close the box, cancel the move, range off, deselect — one at a time |
| `u`, `Ctrl-r` | undo / redo |
| `:d6` | jump to a square (`:6` for a row, keeping the column) |
| `#` | column letters and row numbers, on or off |
| `?` | every key, in full |

Three prefixes carry a family each, which is what keeps the bar to six hints: `i` inserts
(`i p`, `i e`), `s` is for a creature's state — its markers (`s a`, `s c`, `s d`), its place in the
fight (`s i`, `s t`), its note (`s n`) and its counters (`s v`) — and `g` is the GM's hand on the
[fog](#fog-of-war-fog) (`g r`, `g h`, `g R`, `g H`). Press any of them alone and
the status line names the options; `esc` abandons it. A prefix swallows whatever comes
next, so a half-typed command can never turn into a different whole one.

Freeing `p` for paste is the point of `i`: `p` means paste everywhere else, and `P` was an
odd place for it. The keys that moved — `V` `P` `S` — say where they went if you press
them out of habit. (Three came back with new work: `v` selects several creatures, `R` is
the range highlight's shape, and `a` moves the turn on.)

`esc` backs out of one thing at a time, innermost first: drop the selection box, cancel the
move (the creature returns to where it set out), take the overlay off, let go of the
creature. Esc is a cancel, never a commit — putting a creature down where it stands is
`enter`.

**Moving a token.** Pick one up with `enter` and it walks with `hjkl`, with a `◆` on the
square it set out from and a green ribbon along the **shortest walkable route** from there
to where it now stands:

```
    ┌───┬───┬───┬───┬───┬───┐
    │ ◆ │▓▓▓│▓▓▓│   │   │   │        ◆  where it set out
    ├───┼───┼───┼───┼───┼───┤        ▓  the route back to it
    │   │   │▓▓▓│▓▓▓│[O]│ 20 ft  Close
    ├───┼───┼───┼───┼───┼───┤
```

**How far it has come is written beside it**, not at the bottom of the screen: while moving,
the eye is on the creature, and a number it has to travel for is a number it reads late. It
names the range band too when the map has a [ruleset](#rulesets) — `20 ft  Close` — since
the band is what the distance is *for*. Without one it gives the squares as well as the feet, which is
the thing you would otherwise be counting: `4 sq  20 ft`.

It sits out to the side rather than above or below, because those two rows belong to the
creature's [status markers](#play-mode-f2) — a distance covering up a condition would be the
worse trade. Against the right-hand edge it flips to the other side rather than being cut
off, the same as the ruler's readout.

The ribbon is the route, not the wandering — walk out six squares and back three and it
shows the three. It is recut every time the creature lands somewhere new, so it always
answers "what would this move cost", and the step count in the status line is that route's
length rather than the number of keys pressed. Walls lengthen a route rather than being cut
through, and where several routes are equally short the one drawn hugs the straight line,
so open floor gives a staircase rather than an L. A multi-tile creature tints its whole
footprint, showing the ground covered rather than a thread along its top-left corner.

That distance is the straight line by the map's metric — what a range band cares about —
while the step count is the route around the walls, so the two disagree whenever a wall is
in the way, which is the point of showing both. A creature carried somewhere it could not
walk to (blocking switched off, then back on) has no route to price: the ribbon goes, the
readout says `no route`, and the count falls back to keystrokes.

One keypress is one undo step, count and all, so `u` walks the creature back a step at a
time and the route shortens with it. `enter` puts it down where it stands, `esc` puts it
back where it began; either way the ribbon goes. A multi-tile token is blocked by a wall
anywhere along its leading face.

Because the square it set out from is the one square nothing else can have moved onto,
**a cancel always works** — including from on top of an ally, where a drop is refused.

**The cursor is the footprint, and `b` is its size in both modes.** `b` cycles 1×1 →
2×2 → 3×3, `B` cycles back, and a count names the size outright — `2b` is 2×2 without
cycling past it. (`1` `2` `3` could not be the size keys: digits are count prefixes, and
`2j` has to mean two squares down in build and play alike.) In play mode the number is
what the next placement will cover, and a selected creature is resized to it — `b` on a
selected creature cycles up from the size it already is. While a creature is carried the
cursor is that creature's own size, and putting it down brings back the size that was
set. Against the map's edge the cursor is clipped rather than clamped: a footprint that
shrinks against the edge is one that would not fit, said without a message.

**In build mode the same cursor is the brush.** `f`, `space` and `x` paint, toggle and
clear its whole footprint in one undo step, and `H` `J` `K` `L` wall its whole face —
one, two or three edges in a press. A face is only cleared when every edge of it is
already the material, so a partly built wall is completed rather than dismantled. The
brush is a build-mode setting of its own: painting with a 3×3 brush does not make the
next creature Large.

A big cursor can cover several creatures, so `enter` under one walks them — the cursor
holds still, the highlight moves, and the first movement key settles which one you meant:
the cursor goes to it, takes its size, and the step is made from there. One candidate is
not a choice, so a plain cursor over a plain creature is picked up exactly as before.

**Selecting several (`v`).** `v` opens a box from the cursor, the same gesture build mode
uses. Stretching it lights every creature it catches — a big creature merely lapping into
the box counts, the same reading the cursor uses. Then:

- `enter` carries them all. **They move together, or not at all**: a step blocked for any
  one of them is refused for the lot, and the members are transparent to each other on the
  way — without that, a marching column could never move, each blocked by the one in front.
  One ribbon and one distance label follow the lead creature; `esc` cancels the whole walk,
  every creature back to where it set out.
- `y` yanks them all, `d` removes them all and keeps them. `p` stamps the formation back
  out **in the shape it was copied in**, anchored at the cursor — and it is all or nothing:
  if any creature has no room, none of them lands, because a paste that half-arrives leaves
  you reconstructing which half.
- `v` again, or `esc`, drops the box.

Every selected creature wears a bright ring on the grid lines around it — its own colour,
not the cursor's blue, so *what is selected* and *where the cursor is* stay two different
questions. The ring recolours lines that were already drawn, which is why a creature
standing still while selected costs the renderer nothing at all.

**Finding a creature.** Three tracks, because a GM running a fight wants the next of
*their own* creatures far more often than the next of anything: `t` walks every token, `f`
walks the friendlies, `e` walks the enemies. Shift reverses any of them. Each starts from
whatever is selected now rather than the top of the list, so switching tracks picks up near
where you were looking. The cursor follows the selection and the view scrolls to it — a
selection you cannot see is no use for finding a creature — and the status line names what
you landed on. A track with nothing in it leaves the selection alone, so pressing the wrong
one of three keys costs nothing.

Once there is a [turn order](#turn-order-a), all three walk it: `t` visits the creatures in
the order they will act, the ones not in the fight after them, and `f` and `e` do the same
for their side. With no order it is the list, as it always was. Looking stays free either
way — these keys move the selection and nothing else; moving the *turn* is `a`.

`/` searches labels: any part, any case, so `gob` finds `Goblin 3`. It walks on from the
current selection, and `n` / `N` repeat the search forwards and back without retyping it.
The prompt opens empty rather than pre-filled with the last search — a prompt you have to
clear before you can type is worse than one you have to retype — and submitting a blank
line repeats the last search.

**Copying tokens.** `y` copies the token under the cursor and `p` stamps it down at the
cursor, as often as you like — five goblins is `y` then `p p p p`. Each copy is numbered
(`Goblin`, `Goblin 2`, `Goblin 3`) so the readout can tell them apart, continuing an
existing run rather than stacking numbers. A pasted creature arrives with no status
markers: those describe what is happening to one creature right now, not what it is.

**`d` yanks too**, the way vim's does, so moving a creature across the map is `d` then `p`.
Its name comes back with it rather than being numbered — the label is free again once the
token is gone, so the first paste is the original and only a second one is a copy.

**Two creatures cannot share a square.** Placing, pasting and putting down all refuse an
occupied square and say what is on it. Passing over is a different question, and answered
differently: **a creature steps through its own side but not through the other one.** An
ally is somebody you squeeze past; an enemy is a wall. So a held token walks over its
friends freely and stops dead at a foe — and the route ribbon goes round the foe, because
it is drawn from the same rule the movement keys obey.

`Ctrl-w` turns all of that off together, walls included. Rules-agnostic means never fighting
the GM: when the map and the table disagree, the table wins.

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

**Counters.** `s v` keeps numbers on the selected creature — its hit points, its stress,
whatever the game counts. Each is a short name, a value and a maximum, and a creature
holds four. The prompt takes one change or several, separated by commas:

```
hp 6          a new counter at 6/6, or an existing one set to 6
hp 4/8        value and maximum together
hp -2         two off;  stress +1  one on
armor         make it the counter < and > step
-hp           take it off the creature
```

`<` and `>` then take one off or put one on the *current* counter — the last one named,
or the ruleset's first (HP) until one is — and a count says how many: `3<` is three
off. Values stay between zero and the maximum, and every change is one undo step, so
`u` takes back a hit. The tool attaches no meaning to a counter reaching zero.

Counters are the GM's. The status line shows the selected creature's
(`Ogre (enemy 1x1) at D3  HP 4/6  Stress 0/3`), and the side panel shows the current
one beside whoever is acting (`▶  12  Ogre   4/6`), but neither reaches the
[players' frame](#remote-view-serve-mirror): the phones see the Ogre and its place in the
order, never its number. A [ruleset](#rulesets) names the counters its game uses,
which the prompt offers and whose spelling it keeps; Daggerheart's are HP, Stress and
Armor. They travel with the creature through copy, paste, undo and the file.

**Notes.** `s n` is a line of the GM's own text on the selected creature — what it wants,
what it is hiding, what it does when cornered — or, with no creature under the cursor, on
the square: "pressure plate", "the altar hides the key". One prompt reads and writes it: it
opens holding what is there, `enter` keeps or changes it, `ctrl-u` then `enter` takes it
away. A creature's note travels with it through copy, paste, undo and the file; a square's
note is a setting of that square, written or blanked.

Play mode is what the players may see, so a note never appears there. The status line says
`(note)` when the cursor is on one and nothing more; `:notes` says where they all are; and
while a note is open the [remote view](#remote-view-serve-mirror) holds its last frame, so
a phone never shows the prompt. Build mode, which is the GM's alone, marks every noted
square with a `”` in its corner, and has `s n` too, for the square. A map holds sixty-four
notes on squares and one on every creature.

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

The readout gives length in tiles, the distance in feet, the range band if a
[ruleset](#rulesets) is set, and whether a wall breaks the line:

```
RULER   6 tiles  30 ft  Close  sight blocked  [chebyshev]
```

Sight is measured from the anchor straight to the far end and ignores whether the ground is
walkable — you can see across a pit you cannot walk over.

### Range highlight (`r`)

For effects that catch everything in range rather than a single target. `r` highlights
every square within reach of the selected token — or of the cursor, if nothing is
selected. The anchor is fixed when you switch it on, so later presses only change the
reach rather than dragging the highlight along with the cursor, and it follows the
creature it is anchored to as that creature moves.

What a press means depends on the map's [ruleset](#rulesets):

- **Without one** — most games say "creatures within 50 ft" rather than naming bands —
  each `r` grows the reach by one square's worth (5 ft at the default scale), and a count
  names it outright: `20r` is a 100 ft radius, `esc` takes it off. It never cycles off
  the end, because there is no end.
- **With one**, `r` cycles the ruleset's named bands from nearest to farthest, then off,
  and a count names a band outright (`2r` is the second).

**Shapes.** `R` changes the shape the reach is laid out as — circle, cone, line, square,
round again — and a count names one: `2R` is the cone. The capital is the tool's variant,
as `M` is the ruler's. It is a setting, like the cursor's size: it survives `esc`, and
pressing it with the overlay off only says what the next `r` will draw.

The three that point somewhere point at the cursor. Switch the overlay on, move the
cursor, and the template swings round to follow it — there are no aiming keys because
`h` `j` `k` `l` already are. Until the cursor leaves the origin there is nowhere to point,
and the status line says so.

| shape | what it covers |
|-------|----------------|
| circle | everything within the reach, by the map's distance metric |
| cone | the same reach, but only where it is no further off the aim than half as far along it — as wide at any point as it is far from the origin |
| line | the same reach, one square wide |
| square | a side as long as the reach, its near face against the origin, along whichever axis the cursor is further out on; an even side leans the way the cursor does |

A square is counted as "in" when its centre is, and a creature is caught when any square
it stands on is. That is geometry rather than any one game's wording: a system that
draws its cone differently still gets the nearest honest picture of one, and the status
line's list of who is caught is a suggestion to the GM, as it always was.

```
Cone (30 ft, 6 sq) from Aria - 2 in range: Ogre, Goblin*   * no line of sight
Close line (30 ft, 6 sq) from Aria - move the cursor to aim it
```

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

(the example is [Daggerheart](#daggerheart)'s *Close* band)

The highlighted shape follows the distance metric — an octagon under the default 5-10-5,
a square under `chebyshev`. Its bands come from the [ruleset](#rulesets); without one it
is a plain radius.
Sight uses the same test as the ruler, so the two never disagree about the same line.

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

Each kind carries a glyph of its own, so none of them rests on colour alone. Floor is the
one kind drawn blank, and blank is what floor means — so **void is marked instead**, with a
single dim dot in the middle of the square:

```
   ┌───┬───┬───┬───┬───┐          ·   ·   ·   ·   ·   ·
   │   │   │   │   │   │            ┌───┬───┬───┬───┐
   ├───┼───┼───┼───┼───┤          · │   │   │   │   │ ·
   │   │   │ · │   │   │            ├───┼───┼───┼───┤
   ├───┼───┼───┼───┼───┤          · │   │   │   │   │ ·
   │   │   │   │   │   │            └───┴───┴───┴───┘
   └───┴───┴───┴───┴───┘          ·   ·   ·   ·   ·   ·
     a hole in the floor            a room on a canvas
```

A shade would have done it in principle, and doesn't in practice: the page is close enough
to black that a background quiet enough not to shout is one the eye cannot find, and lifting
the *floor* far enough to see would put rough and wood underneath it. One dot per square is
legible at any brightness a terminal renders at, and it shows the extent of a map that has
nothing on it yet.

The dot sits below the grid lines in colour, so it recedes rather than competing, and it is
one cell rather than a fill — a void square is never more marked than the lattice around it.
That lattice is unchanged either way: a lone void square keeps the lines its floor
neighbours draw, because those lines belong to the neighbours.

### Measurement

One tile is five feet by default. Distance and metric are stored per map, like the
[ruleset](#rulesets), since they belong to the game being played rather than to the
session.

| metric | 4x3 offset | notes |
|--------|-----------|-------|
| `alt` | 25 ft | diagonals alternate 1 and 2, the 5-10-5 rule (D&D 3.5 / Pathfinder). **The default** |
| `chebyshev` | 20 ft | every step costs one tile, diagonals included (D&D 5e) |
| `euclidean` | 25 ft | true straight line, what a tape measure reads |
| `manhattan` | 35 ft | no diagonals |

`alt` is the default because it stays within half a square of the true straight line while
keeping every reading a whole number of squares. `chebyshev` is a square cheaper on long
diagonals, which can pull a target into a nearer band than the fiction would put it in.

### Turn order (`a`)

Whose turn it is, for any game that takes turns. There are no rules in it: a creature in
the order has a number, the highest acts first, ties go to whoever was placed on the map
first, and going past the last starts a new round.

| key | what it does |
|-----|--------------|
| `s i` | asks for the selected creature's number. A number joins the order, or moves within it; a blank answer leaves |
| `a` `A` | next / previous turn. `3a` moves three on. The view goes to whoever is up, and they become the selection |
| `s t` | hands the turn to the selected creature, out of order, or with no order at all |
| `:turns` | reads the whole order out; `:turns off` ends the fight |
| `:panel` | the side panel, on or off (on by default) — it also carries the [clocks](#clocks-clock-tick) |
| `:serve` | the [remote view](#remote-view-serve-mirror) for phones; `:serve off` closes it. It goes down with the map unless `--stay-alive` |
| `:mirror` | a second terminal window mirroring play mode |

`a` is the fourth pair shaped like `t` `T`, `f` `F` and `e` `E`, and the odd one out in
one respect: those look, this one acts. The turn passes, the round counts up, the session
log records it, and it is in the undo history — so `u` takes back an advance made by
mistake, round and all, and `A` is for deliberately going back. Neither will move the turn
while a creature is in hand.

The title bar carries the fight, because it is true of the whole table rather than of
whatever is selected:

```
 crypt [+]    Round 2 - Ogre's turn, then Aria, Bram                              PLAY
```

**The panel.** When there is a fight and the terminal is 80 columns or wider, a panel down
the right lists the order top to bottom — number, name, the actor marked with `▶` — with
the round above it and how many creatures are not in the fight below. It takes its
columns from the map, never from the bars, and goes away with the fight; `:panel off`
keeps it away. On a narrower terminal the title bar carries the fight alone.

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

On the map the creature whose turn it is has the grid line above and below it lit, where a
selected one has all four sides lit in its own colour. The two marks share a creature
without hiding each other, which matters because advancing the turn selects whoever it
went to: the sides say *selected*, the bars say *acting*.

The order is not a list kept beside the creatures. It is the numbers on them, sorted, so
everything that already handles a creature handles its place in the fight: delete one and
`p` puts it back in the order where it was; copy one and the copy has the same number but
never the turn; remove the creature whose turn it is and the turn passes to the next, in the
same undo step. It is saved with the map.

**Games with no initiative** get a spotlight instead, when their [ruleset](#rulesets) says
so. The turn is then a *side* — the players or the GM — and `a` passes it across, `s t`
hands it to a creature and the side follows that creature's kind, and the panel shows the
two sides with the lit one marked. Nobody needs a number, and there are no rounds to
count. [Daggerheart](#daggerheart) plays this way.

### Clocks (`:clock`, `:tick`)

A clock is a name and a row of segments, some of them filled — the countdown a Daggerheart
GM ticks while the party dawdles, the progress clock a heist fills, "three more rounds
until the roof comes in". It runs one of two ways: *up*, from empty to full, or *down*,
from full to nothing. A tick is always a step towards the end, and nothing ticks by itself:
the tool serves a table that rolls its own dice and decides for itself when time has
passed. It attaches no meaning to a clock reaching its end either. It lights the row, says
so, and the table decides what it means.

```
:clock Dragon 6       start a six-segment clock (or resize one already called Dragon)
:clock Fuse 4 down    counting down -- the default under a ruleset whose clocks do
:clock Storm d8       a die for the size: eight segments, starting at the roll
:tick                 one step on the clock in hand -- the last started or ticked
:tick Dragon          one step on that clock, which becomes the one in hand
:tick Dragon 2        two;   :tick Dragon -1  one back;   :tick Dragon =3  set outright
:tick Dragon reset    back to the start, for a clock that loops
:clock                list them:  Dragon 3/6, Fuse 4/4
:clock Dragon off     drop it
```

Names are one word, starting with a letter, and a prefix will do: `:tick dr` ticks Dragon
unless another clock starts the same way. A map holds eight. They are drawn in the side
panel under the turn order, as dots when they fit the row and as `5/24` when they do not —
for a countdown the dots are what is left — and the panel appears for them whether or not
there is a fight. A tick is one undo step; starting, resizing and dropping a clock are not
undone, since they are as easy to redo by hand as a tick is not. Resizing keeps the
count, unless the direction changes, when the clock starts over. Clocks are saved with the
map and go to the [session log](#session-log-log) as they change.

### Fog of war (`:fog`)

Fog hides parts of the map from the players until they are lit. It is made of **patches**:
named areas painted onto chosen squares, each with its own settings, so a map can have a lit
entrance hall, a dark crypt behind it, and a mist over the lake. Ground in no patch is always
visible. Fog is off until the first patch is made.

What a patch still hides is drawn three ways. In the [players' frame](#remote-view-serve-mirror)
— the phones, the mirror, and `:player preview` — it is not drawn at all: no floor, no walls
inside it, no creatures, and not even the dot that marks void, since a field of dots would
trace the room. The wall between a lit room and a dark one is drawn, from the lit side. The
void around a painted room counts as dark too, so painting a room's floor is enough to hide
its outline -- only a wall with lit ground on one side is drawn. The
GM's own screen shows the same ground on a dark blue shadow, with everything on it, so the
GM sees the whole map and what the table sees at once. Build mode tints each patch in a colour
of its own.

```
:fog Crypt          make a patch (or pick one) for g f to paint; fog comes on with the first
:fog Crypt 3        how far a creature will light it, in squares  (manual: only by hand)
:fog Crypt memory off   the dark closes behind the party instead of staying mapped
:fog Crypt clear    light the whole patch, for when the door opens;  hide  puts it back
:fog Crypt disable  keep the painting, hide nothing;  enable  puts it back to work
:fog Crypt delete   scrub it off the map for good
:fog all            one patch over the whole map, for plain fog of war
:fog on | off       the master switch; the painting is kept either way
:fog --soft-edge    show the rim of the dark, below;  --no-soft-edge  hides it again
:fog Crypt --soft-edge   the same for one patch, over the map's setting
:fog                list them:  fog on: Crypt r3 12/40 soft *, Mist r2 0/16
```

**Painting** is build mode's: `g f` paints the current patch over the brush's footprint, or
over the `v` box or `V` circle, and `g c` scrubs fog off it. The status line names the patch the
cursor stands in and the one `g f` would paint. **Lighting** is play mode's: `g r` lights what
the cursor covers (or the `v` box), `g h` puts it back in the dark, and `g R` / `g H` do the
whole patch under the cursor. Painting and lighting are undoable, a stroke at a time; a
patch's settings are not, any more than starting a clock is.

In the players' frame fog also keeps the dark from being described. A creature in it is named
`?` in the title bar and the turn panel. The cursor is not drawn while it rests in the dark,
since its size follows whatever it rests on, and neither are its row and column lit in the
margins. The status line says only `dark` for such a square, not which one it is; a creature
carried out of the dark does not say where it set out from or how far it has come. The panel's
count of creatures outside the fight leaves out the ones in the dark. The status line
names no hidden creature, leaves out the count of creatures on the map, and shows no message
at all — most of what the app says names a creature or a square. The range highlight and the
ruler anchored in the dark are not drawn for the players, and one anchored in the light tints
only lit ground.

**Sight.** Player creatures light fog as they move. Each patch lights to its own `reveal`,
counted in squares by the map's [metric](#measurement) — the same count the ruler makes — from
the nearest square of the creature, along lines that walls, closed doors and secret doors stop
and windows and open doors do not, which is the test the ruler and the range highlight already
use. Opening a door lights what is beyond it; closing it puts that back in the dark. Enemies
light nothing. `reveal manual` is a patch only the GM's hand lights.

With **memory** on, the default, ground the party has seen stays drawn after they move on, the
way a map is drawn as a dungeon is explored — but a creature standing on it is drawn only while
someone can see it now. With memory off the dark closes behind them; switching it off
forgets what the patch had remembered, apart from what the GM holds lit. The GM's own light (`g r`)
stays down whoever walks away, until `g h` takes it back.

Sight is worked out again after any keystroke that changed the map — a step, a door, an undo,
a patch painted or reset — and at no other time, and it covers only the party's reach: a map
with a crypt at one end costs nothing while the party is at the other.

**The soft edge.** `:fog --soft-edge`, off by default, half-shows the rim of the dark: the
squares next to one the party can see now, all eight round it, a diagonal only when nothing
blocks the corner — so it moves with the party, and never reaches through a wall. On the rim
the players see its walls, dimmed, with any door in them drawn as a wall until the square
beside it is lit; no ground and no grid lines; and a creature as a **silhouette** — its shape
and its size in a neutral grey, `?` for a name, no markers. A big creature with one square on
the rim is a silhouette too. The rim is anchored on sight, not on the GM's `g r` light or on
remembered ground. The map's setting is every patch's default; `:fog Crypt --soft-edge` or
`--no-soft-edge` gives one patch its own.

### Dice (`:roll`)

```
:roll 2d6+3          2d6+3 = 9  [4 2]
:roll d20            d20 = 17  [17]
:roll 4d6 + 1d4 - 1  4d6+1d4-1 = 15  [3 6 2 4 1]
```

Any sum of dice groups and constants, up to a hundred dice of up to a thousand sides a
group. The dice are reported one by one so nobody has to take the total on trust. The
generator is seeded from the OS; `--seed N` makes a session repeatable.

A bare `:roll`, or a bare modifier like `:roll +2`, is the ruleset's *action roll* — the
one a system means by "roll" with nothing else said. Without a ruleset there is no such
thing and `:roll` asks for an expression. [Daggerheart](#daggerheart)'s is the duality
roll, described there.

**Named rolls.** A stat block's rolls can be saved with the map and rolled by name:

```
:roll attack = 2d12+3    save it        :roll attack     attack: 2d12+3 = 17  [9 5]
:roll swing = duality +2 the action roll, with its modifier
:rolls                   list them      :roll attack =   forget it
```

A prefix will do (`:roll att`) when only one name starts that way. Plain dice always win:
`:roll d20` is a d20 however many rolls are saved, and a name that reads as dice is
refused. A map holds sixteen.

Rolls go to the [session log](#session-log-log) when it is on.

### Session log (`:log`)

`:log` starts a plain-text record of the things that happened at the table, and a second
`:log` stops it. `:log on`, `:log off` and `:log path/to/file.log` say so exactly. By default
the log sits beside the map as `name.log`, or in the map directory when the map has never
been saved.

```
--- 2026-09-16 19:02:11  log on: Crypt ---
[19:02:40] placed enemy Ogre (2x2) at h6
[19:03:05] dropped after 4 steps
[19:03:22] red marker on Aria: Poisoned
[19:03:40] 2d6+3 = 9  [4 2]
[19:04:01] Duality +2 = 17 with Hope  [hope 9, fear 6]
[19:04:15] undo
--- 19:20:03  log off ---
```

What is logged is what changed: creatures placed, put down, removed, pasted, relabelled,
markers added and cleared, doors, rolls, ruleset changes, and undo and redo. Errors, hints
and the things the app says about itself stay on the status line. Every line is flushed
as it is written, so a crash loses nothing, and closing the map closes the log.

### Recovery

Unsaved work is copied to `name.vtt.autosave` beside the map once the changes have gone
quiet for a moment — never mid-keystroke, and never for a map that has nothing unsaved. A
save removes the copy, and so does deliberately discarding (`:q!`, `:e` to another map, or
answering yes to any of the questions, including the one on quitting). Only a crash or a lost terminal leaves it behind, and the next time that map is
opened the tool asks:

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

`y` puts the copy in place of the map, unsaved, so `:w` keeps it and `:q!` lets it go;
`n` deletes the copy and opens the map as it was saved. The question is asked once. The
copy is a whole map in the same [format](#file-format), so it can be opened by hand in a
pinch, and the undo history is not part of it. Headless runs (`--script`, `--bench`)
never write one.

### Remote view (`:serve`, `:mirror`)

Players watch the map from their own devices. The GM's `vtt` serves; a phone, a tablet or
a second terminal is a client of the same stream, and all of them see the **players'
frame**: play mode as the GM sees it, less anything that is the GM's alone -- no prompt,
no question box, no profiler, no hint that a [note](#play-mode-f2) exists. They keep
seeing the last frame, frozen, while the GM is in build mode or the menus.

`:player preview` puts that frame on the GM's own screen, so what the table sees can be
checked without walking round to a phone; `q` returns to the GM's view, and nothing else
changes while previewing. The frame is drawn a second time only when the two could
differ; otherwise it is the GM's, copied, and costs a memcpy.

| | |
|---|---|
| `:serve` | open the remote view; the status line shows the URL with its join code |
| `:serve 7777` | on a port of your choosing, so the address is the same every session |
| `:serve --stay-alive` | keep it up when the map closes; `--no-stay-alive` takes that back |
| `:serve off` | close it and drop everyone |
| `:mirror` | a second terminal window mirroring play mode, to drag to a TV; serves if it has to |
| `:player preview` | the players' frame on the GM's own screen; `q` returns |
| `vtt --watch HOST:PORT` | the same mirror by hand, on any machine on the LAN |

**How long it lasts.** The remote view belongs to the encounter, so closing the map
closes it and drops everyone watching. That is deliberate: the players were watching
*that* map, and when the GM puts it down there is nothing left for them to see. The
status line says so, alongside whatever else the close was about:

```
wrote /home/gm/maps/crypt.vtt - remote view off - 3 clients dropped
```

`:serve --stay-alive` says otherwise. The server then survives a map close, keeps every
client connected, and carries them straight into the next map the GM opens. Use it when
one session runs through several encounters, because the alternative costs everyone a
retype: a fresh `:serve` makes a **new join code**, and on a port chosen at random a new
port too. The flag lasts as long as that server does — `:serve off`, or a restart on
another port, starts again without it.

Switching maps with `:e` is not a close, so the players keep watching either way; the
view simply becomes the new map.

Nothing keeps the server alive past `vtt` itself. Quitting the application closes the
listener, and the operating system would close it even if the application forgot.

**Connecting a phone or tablet.** Nothing to install; the phone needs a browser and the
same Wi-Fi as the GM's machine.

1. On the GM's machine, open the map, press `F2` for play mode, and type `:serve`. The
   status line shows the address, something like `serving at http://192.168.1.10:7777/?k=482913`.
   `:serve 7777` names the port, so the address is the same every session; the six-digit
   join code is new each time. Add `--stay-alive` to keep the same server, and the same
   code, across several encounters in one sitting.
2. On the phone, open Chrome (or any browser) and type that address in exactly, code and
   all. Turn the phone sideways: an 80-column terminal wants the width.
3. The map appears and follows the GM from then on. The corner of the page shows the
   size of the GM's terminal and how long each frame took to draw; that is the only thing
   on the page that is not the GM's screen.

The page is 10 KB, served by `vtt` itself, and fetches nothing from anywhere. It fits the
GM's whole terminal to the screen, is crisp again after a pinch, keeps the screen awake,
and reconnects by itself if the Wi-Fi drops or the GM restarts the server.

If the phone says it cannot reach the address, one of two things is in the way. The GM
machine's firewall may not allow the port -- open it, or pick one that is open. Or the
router keeps Wi-Fi devices from talking to each other ("client isolation" or "AP
isolation", common on guest networks) -- use the main network, or a phone hotspot with
both devices on it. A wrong or missing join code gets a short refusal page instead.

The join code is a living-room lock, not a secure one: it stops the neighbours' devices
wandering in, and there is no TLS by design.

**The second terminal** is the same picture in a terminal: `:mirror` opens a new window
running `vtt --watch` against the GM's own server, via `$TERMINAL` or whichever terminal it
finds, detached so it can be dragged to another screen. `q` closes it. On a machine with no
terminal to open, the command to type by hand is on the status line.

**What it costs.** Frames go out only when something changed, as the same diff the GM's
terminal receives, encoded once and written once per client; nothing is sent while nothing
moves. At most eight clients, each with a fixed buffer, and one that cannot keep up is
dropped and resynced when it reconnects, never waited for. The measured cost is in
[docs/PERFORMANCE.md](docs/PERFORMANCE.md); the design and its budgets are in
[docs/REMOTE.md](docs/REMOTE.md).

The page lives in `web/index.html` and is embedded by `tools/embed.sh`; its copy loop is a
WebAssembly module `tools/blit_wasm.py` assembles by hand, so neither needs a toolchain.

### Rulesets

The core is rules-agnostic and stays that way: movement, walls, terrain, tokens, the
ruler and every distance work identically whatever the table is playing, and the tool
decides no costs and enforces no rules. On top of that sit a few **rules-aware**
features that need to know what game the map is for, and those switch on when the map
names its ruleset:

```
:ruleset daggerheart
```

Naming a ruleset gives the map its **range bands** — the `r` [overlay](#range-highlight-r)
cycles through them, and the ruler and the moving-creature readout name the band a
distance falls in (`20 ft  Close`) instead of leaving the conversion to you. The setting
is stored per map, because it belongs to the game being played rather than to the
session. `none` is the default: every readout reports plain squares and feet, and the
`r` overlay is a plain radius grown a square's worth at a time (5 ft at the default
scale), which is how most games phrase
reach anyway.

A ruleset is a small table: named thresholds, what a bare `:roll` means, whether the turn
is a spotlight, and which way a new clock runs. Nothing more — the tool still attaches no
meaning to a band, enforces nothing, and never spends a creature's movement for it.
Supported: `none`, `daggerheart`.

#### Daggerheart

`:ruleset daggerheart` adds the five bands: Melee, Very Close, Close, Far, Very Far, and
names the [counters](#play-mode-f2) a creature keeps: HP, Stress and Armor.

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

**The spotlight.** Daggerheart has no initiative: the spotlight passes between the players
and the GM as the fiction and the dice dictate. Under this ruleset the
[turn tracker](#turn-order-a) tracks that instead of an order. `a` passes the spotlight
to the other side, `s t` hands it to a particular creature — a player's turn is the
players' spotlight, an enemy's the GM's — and the panel shows which side has it:

```
│ Spotlight
│
│ ▶ Players
│     Aria
│   GM
```

The title bar says the same in words, *Players' spotlight - Aria*, for a terminal too
narrow for the panel. Rolling initiative with `s i` still works here; the moment anyone
has a number the tracker is an order again.

**Countdowns.** Daggerheart's clocks count down, so under this ruleset a new
[clock](#clocks-clock-tick) starts full and `:tick` brings it towards nothing; `:clock
Fuse 4 up` asks for a progress clock instead. The SRD's variants are all the GM's hand on
the same clock, which is the point at an in-person table: a *random start* is `:clock
Ambush d6`, which rolls the die and starts there; a *looping* countdown is `:tick Ambush
reset` when it reaches nothing, and the loop that grows or shrinks each time round is
`:clock Ambush 7` before the reset; a countdown that moves with the fiction goes `:tick
Ambush -1` as readily as `:tick Ambush 2`; two that advance together are two clocks and
two ticks; a *long-term* countdown is one ticked after rests. Nothing advances on a roll
by itself, because the dice at the table are usually not the tool's.

The SRD is explicit that these ranges "aren't intended to be precisely measured during play"
and are a quick guide for the GM — the readout is the same kind of aid.

**Duality dice.** Under this ruleset a bare `:roll`, or `:roll +2`, is the action roll: two
d12s, one for Hope and one for Fear, plus the modifier. The readout gives the total and
which die was higher — *with Hope*, *with Fear*, or *critical success* when they match —
and both dice, so the table can see them:

```
:roll +2             Duality +2 = 17 with Hope  [hope 9, fear 6]
:roll                Duality = 14 critical success  [hope 7, fear 7]
```

The two dice are coloured where they are printed — the Hope die gold, the Fear die
purple, as the game's own dice are — and chosen to differ in brightness too, so they stay
apart without colour vision. The session log keeps them as plain text.

`:roll duality +2` asks for the same roll on any map, and `:roll 2d12+2` is two plain d12s
with no verdict, ruleset or not.

### Commands

| command | action |
|---------|--------|
| `:w [name]` | save |
| `:wq` `:x` | save and close |
| `:q` `:q!` | close, with or without asking |
| `:e <name>` | open another map; asks first if this one has unsaved changes |
| `:name <text>` | rename the map |
| `:resize WxH` | resize (clears the undo history) |
| `:zoom N` | set the zoom level, 0–3 |
| `:scale N` | feet per tile (default 5) |
| `:metric NAME` | `chebyshev`, `euclidean`, `alt` or `manhattan` |
| `:ruleset NAME` | switch the rules-aware readouts — see [Rulesets](#rulesets) |
| `:turns` | read the [turn order](#turn-order-a) out; `:turns off` ends the fight |
| `:panel` | the side panel, on or off |
| `:clock NAME N` | start a [clock](#clocks-clock-tick); `:tick` fills a segment |
| `:notes` | where the [notes](#play-mode-f2) are |
| `:fog ...` | [fog of war](#fog-of-war-fog): patches, their settings, the master switch |
| `:serve [PORT] [--stay-alive]` | the [remote view](#remote-view-serve-mirror); `:serve off` closes it |
| `:player preview` | see the players' frame on your own screen; `q` returns |
| `:mirror` | a second terminal window mirroring play mode |
| `:roll 2d6+3` | roll dice — see [Dice](#dice-roll) |
| `:roll NAME = EXPR` | save a roll under a name; `:rolls` lists them |
| `:log` | the session log, on or off — see [Session log](#session-log-log) |
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
VTT 6
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
fog on
fogpatch 1 Crypt reveal 2 memory on
fog             # one line per row, like tiles: ..........AAAaaa
```

A `tokenstatus` line hangs a marker on the token above it, so the attachment needs no index
to go wrong. A `tokenturn` line does the same for the [turn order](#turn-order-a): the
creature's number, then `acting` if the turn is its. `tokenturn - acting` is a creature
holding the turn from outside the order. `spotlight gm` says the GM has the spotlight, for
a game that passes one; the players having it is the default and is not written. A
`clock` line is a [clock](#clocks-clock-tick): its name, then filled and total segments,
then `down` for one that counts down;
a `roll` line a [named roll](#dice-roll). `tokennote` hangs a note on the token above it, `tokencounter NAME VALUE MAX` a counter,
and `fogpatch` lines name the [fog](#fog-of-war-fog) patches whose ground the `fog` section's rows
hold, one character a square: `.` for none, `A`-`O` for patch 1-15 unseen, `a`-`o` for seen,
and `1`-`9` then `!"#$%&` for lit by hand,
and `note x y` puts one on a square.

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
silently drop them, losing combat state from a saved fight, so it refuses too. Version 4
added the turn order, for the same reason — but only a map with a fight in it says 4. One
without is still written as version 3, which says everything it needs to and stays
loadable by the builds that came before. Version 5 added clocks, named rolls and notes, and
version 6 counters and fog, on the same terms: the writer always picks the lowest version that
says everything in the map. Each version
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

Idle cost is zero: `poll()` blocks until there is input. Cost follows the *window*, not
the map — a 200×200 map costs the same at 80×24 as a small one, because every drawing
path culls to the visible tiles first.

| | 80×24 | 200×50 |
|---|---|---|
| build, 200×200 map | 29 µs | 121 µs |
| play, 24 tokens | 30 µs | 108 µs |
| bytes written per frame | ~209 | ~212 |

**[docs/PERFORMANCE.md](docs/PERFORMANCE.md) has every path measured**, what dominates a
frame, and the rules for keeping it that way. `make perf` regenerates it, so the numbers
there are output rather than estimates.

`F12` shows the live overlay — per-zone timings with p50/p99, a frame-time sparkline, and
the cells-changed and bytes-written counters that predict perceived latency better than
wall-clock alone. The overlay reports its own cost as a `prof.overlay` zone, since an
instrument that quietly adds to the number it displays is worse than no instrument.
`--trace out.json` writes a Chrome Tracing profile for perfetto.

Undo history is bounded rather than endless: the log keeps about four full fills of the
largest map (a million single-cell changes) and drops its oldest steps past that, so a
long session of painting cannot grow without limit. Each step costs 20 bytes; the tokens
a step adds, removes or edits are kept alongside it.

## Design notes

See `docs/PLAN.md` for the architecture.

## License

MIT — see [LICENSE](LICENSE).
