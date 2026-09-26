# Writing a vtt map by hand

For an AI agent (or anyone at a text editor) turning a GM's description into a map file.
The file is plain text; this page is everything needed to write one that loads as meant,
and the tools to check it. The README's *File format* section is the full reference.

## The loop

1. Write the `.vtt` file.
2. `vtt map.vtt --dump-map` and read it back: is every wall where the description says,
   is every door in a wall, is every creature on the floor? `vtt map.vtt --describe`
   says what rooms that makes and how they connect.
3. Fix, and dump again, until it is right. Then show the GM the dump, name squares
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
|  |  +
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

## Reading the dump

## Reading the map back

`vtt map.vtt --describe` lists the rooms -- areas of ground joined by open floor, every
wall, window and door being a room's edge -- each named by its first square in reading
order (`room 2 (C2)`), with its extent, terrain, every door and window on its edge and the
room it leads to, and the creatures, notes and fog in it. A room the party cannot reach
through doors says `NOT REACHABLE`. Check it against the description: one room where two
were meant means a wall with a gap in it. `--json` gives the same as JSON.

`vtt map.vtt --dump-map` prints the lattice in the file's own characters: squares at odd
positions, boundaries between them, a wall's corners drawn as its line (`-` or `|`, never
`+`, so a `+` is always a door). Creatures show as `1`-`9`, `a`-`z`, `A`-`Z` and are listed
underneath with their squares. `--region B2:K12` prints part of a big map.
