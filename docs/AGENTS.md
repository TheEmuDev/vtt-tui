# Writing a vtt map by hand

For an AI agent (or anyone at a text editor) turning a GM's description into a map file.
The file is plain text; this page is everything needed to write one that loads as meant,
and the tools to check it. The README's *File format* section is the full reference.

## The loop

1. Write the `.vtt` file.
2. `vtt map.vtt --check`: every line it prints is a mistake, with the file line or the
   square. Fix until it says `no findings` (notes, `N...`, may stay).
3. `vtt map.vtt --dump-map` and read it back: is every wall where the description says,
   is every door in a wall, is every creature on the floor? `vtt map.vtt --describe`
   says what rooms that makes and how they connect.
4. Fix, and dump again, until it is right. Then show the GM the dump, name squares
   (`C3`, `the door at F7`) and take corrections the same way.

## A complete small map

```
VTT 6
name Crypt Entrance
size 6 4
scale 5
metric alt
tiles
......
......
..~~..
......
vedges
|     |
|  +  |
|  |  |
|     |
hedges
------
      
---  -
      
------
token player 1 1 1 "Aria"
token enemy 4 2 1 "Ghoul"
note 2 3 "loose flagstone"
```

`vedges` has 4 rows of 7 characters; `hedges` has 5 rows of 6 -- its second and fourth rows
are six spaces each (no boundary), easy to lose sight of.

`VTT 6` first; then header lines (`name`, `size W H`, `scale` feet per square, `metric`
chebyshev / euclidean / alt / manhattan, optionally `ruleset daggerheart`); then the
sections. Coordinates in the file are **0-based x then y**; the app and the tools name
squares with letters and 1-based rows (`x 4, y 2` is `E3`).

## The three grids, and the one mistake to avoid

A map `W` wide and `H` tall has:

| section | rows | characters a row | what a row is |
|---|---|---|---|
| `tiles` | `H` | `W` | the squares of one row |
| `vedges` | `H` | **`W + 1`** | the boundaries *between* squares in one row, west edge first |
| `hedges` | **`H + 1`** | `W` | the boundaries above row 1, between each pair of rows, below the last |

A wall sits on the line between two squares, never on a square. So there is one more
vertical boundary than there are columns, and one more horizontal row of boundaries than
there are rows. Write `vedges` rows `W + 1` characters long and `hedges` with `H + 1`
rows, and count them.

**The loader reads rows by count, not by looking for the next header.** A `vedges`
section one row short swallows the `hedges` line as its last row and every section after
it shifts; a row one character short moves nothing but leaves the east wall off. The map
still loads, silently wrong. The dump shows it at once: the room will not close.

Short rows are allowed (trailing spaces may be dropped by an editor, and read as void or
no boundary), so a row may end early -- but never contain too few characters *before* the
last thing on it.

**Other things the format does not forgive:**

- Header lines (`name`, `size`, `scale`, `metric`, `ruleset`) come before the first
  section; after it they are ignored.
- There are no comments. A `#` line is an unknown line; inside a section it is a row.
- A blank line inside a section is a row (of void, or of no boundaries).
- A door on the map's own edge is a way out, and `--check` notes it (`N105`) rather than
  warning.

## Characters

| square | char | | boundary | char |
|---|---|---|---|---|
| void (not map) | space | | none | space |
| floor | `.` | | wall | `\|` (in `hedges`, `-` also) |
| water | `~` | | door | `+` |
| rough | `:` | | open door | `/` |
| brush | `"` | | window | `%` |
| wood | `=` | | secret door | `S` (players see a wall) |
| hazard | `^` | | open secret door | `s` |

## Creatures, notes, fog

- `token player X Y SIZE "Label"` or `token enemy ...`; `SIZE` 1-3 squares wide, anchored
  at its top-left square. Labels should be unique on a map.
- `note X Y "text"` -- a GM-only note on a square.
- `tokennote "text"` after a token -- a note on that creature.
- Fog: `fog on`, `fogpatch N Name reveal R memory on` (N 1-15, R squares or `manual`), then
  a `fog` section of `H` rows of `W` characters: `.` no fog, `A`-`O` patch 1-15.

## Reading the map back

`vtt map.vtt --describe` lists the rooms -- areas of ground joined by open floor, every
wall, window and door being a room's edge -- each named by its first square in reading
order (`room 2 (C2)`), with its extent, terrain, every door and window on its edge and the
room it leads to, and the creatures, notes and fog in it. A room the party cannot reach
through doors says `NOT REACHABLE`. Check it against the description: one room where two
were meant means a wall with a gap in it. `--json` gives the same as JSON.

The linter's codes that matter most when writing by hand: `E011 section-short` (a
section one row short swallowed the next header -- count the `vedges` and `hedges` rows),
`E010 row-long` (a `vedges` row is `W + 1`, not more), `W104 door-loose` (a door with no wall
at either end: usually in the wrong row), `E110`/`E111` (a creature on void or off the
edge), `W120` (a room nothing leads to). The README lists them all.

`vtt map.vtt --dump-map` prints the lattice in the file's own characters: squares at odd
positions, boundaries between them, a wall's corners drawn as its line (`-` or `|`, never
`+`, so a `+` is always a door). Creatures show as `1`-`9`, `a`-`z`, `A`-`Z` and are listed
underneath with their squares. `--region B2:K12` prints part of a big map.

## Working in a live session

When the GM has the map open in `vtt` and has typed `:agent on`, work on the map in memory
instead of the file: the GM sees every change as it lands, and `u` takes each one back.
README *Control channel* lists every request; this is how to use them.

1. `vtt --ctl status` first: which map, whether it is saved, and whether edits are taken
   (`edits taken`, or `not now:` and why -- usually the GM is in play mode).
2. Read before writing: `vtt --ctl 'dump'` (or `'dump B2:K12'` on a big map) and
   `vtt --ctl 'describe'`. Name squares back to the GM from the dump.
3. When the GM says "here" or "this room", ask `vtt --ctl marked`: the cursor, the box
   they drew with `v`, the creatures they selected, where they last pinged (`g p`). Work
   from those squares rather than guessing.
4. Send each change the GM asked for as **one request** -- a heredoc, one line an edit --
   so it is one `u` for the GM:

   ```
   vtt --ctl <<'EOF'
   room K2:O6
   edge J4|K4 door
   token add enemy M4 "Ghoul"
   EOF
   ```

   A request is all or nothing: if a line fails, nothing changed, and the answer says which
   line and why. Fix it and send the whole request again.
5. Read back what you did (`dump` the region, `check`), and tell the GM in squares.
6. For anything the GM's table has a stamp for (`vtt --ctl stamps`), put the stamp down
   (`stamp Table F4 rotate 90`) rather than drawing it square by square; `vtt
   ~/.local/share/vtt/stamps/Table.vtt --dump-map` shows what one looks like first.
7. Suggestions the GM has not agreed to go on the map as notes (`note F7 "secret door?"`),
   which the players never see; the GM keeps or clears them.
8. If the GM does not like a change and nothing has happened since, `vtt --ctl undo` (a
   request of its own) takes it back; otherwise ask them to press `u`. Never try to repair a change by undoing the
   GM's own work.

Exit status: 0 done, 1 an error or `busy:` (read stderr), 2 no vtt is listening -- ask the
GM to type `:agent on`. `busy:` means the GM is in the middle of something: wait, ask, and
send the same request again. Saving is the GM's (`:w`); the channel never writes files.
