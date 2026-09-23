# Fog of war, counters, and the players' frame: a plan

Three of the features asked for on 2026-09-20 turn out to be one design.
Fog of war needs the players to see less than the GM. Counters on creatures
(an ogre's hit points) are the GM's business, not the table's. And the
remote view today sends the players the GM's frame, byte for byte. The
piece all three want is a **players' frame**: a second rendering of play
mode, drawn for the phones, with the fog opaque and the GM's numbers left
out. This document describes the three, with the decisions and what each costs;
*Order* at the end says which lands first. Nothing here is built.

## What requests 4 and 5 changed

Fog was a bit per tile. Two later requests made it *patches*: areas painted
on chosen tiles (4), each named, separately cleared, separately settable,
and changeable afterwards (5), with silhouettes at the rim of the dark (4).
That is a bigger idea than the one this document started with, and it costs
less than it sounds.

- **The tile stays one byte, so patches are free per tile.** Four bits say
  which patch; the rest say seen, lit, rim and held. Everything that varies
  between patches -- reveal distance, memory, the soft edge, colour, name --
  lives in a fifteen-entry table that no drawing path ever reads. A tile
  costs one load and a mask either way, and the array is the 256 KB it
  always was.
- **Nothing gains a drawing pass.** The GM's dim, the players' blank and
  the build-mode tint all fold into the single per-tile loop `grid_draw`
  already runs, writing the cells it was going to write in another colour.
  Where fog hides a tile the players' frame writes *less* than the GM's, so
  a fogged map is cheaper to draw for the players than for the GM.
- **Many patches are cheaper than one big one.** Each carries a bounding
  box, so a party in the entrance hall does no work at all for the warren
  or the lake, and reveal distance is per patch: a `reveal 1` dark room
  tests 9 tiles a move where a map-wide `reveal 6` tests 169.
- **The rim is worked out when the party moves, not when the frame is
  drawn.** It is a bit set in the same walk that sets *lit*, over the same
  box, so a rim tile costs the drawing path one mask like any other, and a
  creature asks only its own footprint.
- **The one real cost is recomputing sight on every move**, which the rule
  "creatures are drawn only where the party can see now" makes unavoidable.
  It is bounded by the party's reach, never by patch size. See *What memory
  costs*.
- **It moves the players' frame to the front.** Silhouettes live there and
  so does the point of fog. Building fog first would ship a half-feature
  and retrofit it. See *Order*.
- **One addition falls out of it:** `:player preview`, drawing the GM's own
  terminal the players' way. Authoring a fogged map is guesswork without it,
  and once `app_draw` takes a view argument it is a flag, not a feature.

## Fog of war: the agreed behaviour

Your summary of 2026-09-22, which is the spec the rest of this document now
serves. Two of these changed what was written above, and *What it costs*
below says how.

1. Fog is off by default.
2. With it on, ground outside the players' sight is not drawn.
3. **Memory is on by default** and can be turned off per patch.
4. **With memory on, ground the party has seen stays drawn once they have
   left. Creatures in it do not**: a creature is drawn only where the party
   can see right now. Memory paints only ground that has actually been
   inside someone's sight; a corner of a patch nobody ever saw stays dark.
5. Sight range can be changed at any time, mid-session.
6. Fog applies to painted regions, not the whole map.
7. A map holds several patches, each updated or disabled on its own.
8. Optionally -- the **soft edge**, `:fog --soft-edge`, off by default --
   the rim of the dark is half-shown rather than blank.

Answers of 2026-09-23 settle the rest:

9. **Sight stops at walls**, and at everything else the map says is opaque.
10. **The soft edge is the fog tiles adjacent to a lit tile**, so it moves
    with the party rather than sitting on the painted border. Anchored on
    *lit*, all eight neighbours, a diagonal only when both of its
    orthogonal crossings are clear.
11. **On a soft-edge tile**: walls are drawn, but a door in one is drawn as
    a wall and only becomes a door when the tile is fully lit; terrain is
    not drawn at all; creatures are drawn, including a big creature with
    only part of itself there; and everything drawn is dimmed.
12. **Delete and disable are different.** Deleting scrubs a patch off the
    map. Disabling leaves it and its painting in place but makes it hide
    nothing, and it can be enabled again.
13. **`:player preview`** shows the players' view on the GM's own screen;
    `q` leaves it.

### What memory costs, which is not what it looked like

**Memory on is not the cheap path, and cannot be.** That was true while the
only question a tile answered was "has the party been here", which
accumulates and never has to be taken back. It stopped being true with the
rule that creatures are drawn only where the party can see *now*: current
sight shrinks behind a walking party, so it has to be recomputed on every
move whether the ground is remembered or not.

So memory decides almost nothing about cost. It is one extra bit set on a
tile as it is first lit, and one extra term when the ground is drawn. On by
default is a good choice for play -- the party maps the dungeon as it goes
-- and it is free to change per patch. The thing that actually drives the
work is the creature rule, and that is not negotiable without giving up
hidden creatures.

**The tile carries four things**, all of them in the one byte it always had:

| bits | holds |
|---|---|
| 0-3 | which patch, 1 to 15; 0 for none |
| 4 | **seen**: has been inside someone's sight (kept only while the patch remembers) |
| 5 | **lit**: inside someone's sight right now |
| 6 | **rim**: unlit, and next to a lit tile across a boundary that does not stop sight |
| 7 | **held**: lit by the GM's hand (`g r`), which the walk leaves alone |

Ground is drawn when *seen*, *lit* or *held*; a creature when *lit* or
*held*; the soft edge on *rim*. Every drawing path still does one load and
a mask.

**Held is what makes revealing by hand mean something.** If `g r` merely set
*lit*, the next step anyone took would clear it. Held is a light the GM has
put down: the recompute never touches it, `g h` takes it away, and `g r` on
a remembering patch sets *seen* as well so the ground stays. A `reveal
manual` patch is one that is only ever held-lit, which is what a scripted
reveal wants, and creatures in it are drawn exactly where the GM has put
light and nowhere else.

**When sight is recomputed.** Anything that can change what the party sees,
and a missed one draws a creature that should be hidden, which is the one
bug this design must not have. The list, so it is a checklist and not a
guess: a player creature moves, is placed, pasted, deleted or resized; undo
or redo of any of those; a door or window opens or closes; a patch is
created, painted, scrubbed, enabled, disabled, or has its `reveal` changed;
the master switch flips; the map opens, resizes, or is recovered. One
recompute per keystroke however many of those a keystroke does.

**The rim is a bit, not a question asked while drawing.** Answering "is this
tile next to a lit one" at draw time would be four extra lookups on every
hidden tile in the window, every frame -- eight hundred or so on a full
screen of fog, for an answer that only changes when somebody moves. It is
computed instead in the same walk that computes *lit*, over the same box,
and read back as one bit. That is the difference between paying per frame
and paying per move, and it is what keeps the richer soft-edge rules free.

**What a move costs.** The bits that can change live inside the union of
the old and new sight circles. Clear that box, relight it from every player
creature whose circle meets it, then set *rim* on its unlit fog tiles that
touch a lit one. All three passes are over the same box:

| | |
|---|---|
| tiles in the box at `reveal 6` | about 196 |
| line steps each | up to 6 |
| creatures relighting it | up to the party |

Tens of microseconds on a keystroke whose whole frame is about thirty. Two
things are mandatory rather than nice:

- **One recompute per keystroke, not per creature.** A group walking
  together is one keypress; the set is rebuilt once after every member has
  stepped, not once per member.
- **The bounding-box test comes first**, so a party in the entrance hall
  does no work for the warren or the lake.

**Drawing a soft-edge tile** costs less than a lit one, not more: no
terrain glyphs, no terrain background. The walls it does draw go through
`seg_look`, which already turns a secret door into a wall for anyone but
the GM -- disguising an ordinary door at the rim is the same substitution
one line further down, not a new mechanism.

**A creature at the rim.** Its whole footprint decides, in this order: any
tile of it lit, and it is drawn as itself; else any tile of it on rim, and
it is drawn as its own shape, dimmed, with `?` where its name would be;
else not drawn. That is what covers a big creature with only one square in
the half-light, and it is a scan of at most nine tiles for a creature the
frame was already drawing.

**Two side effects.** Fog changes on most moves rather than only growing,
so more cells differ per frame and the phones get a few hundred extra bytes
a step, against the 5 KB a full frame measured: comfortable. And with the
lit set shrinking behind the party, `:player preview` and the GM's dimmed
view are the only way the GM can see where the party has been.

### Standing assumptions

Nothing that changes the shape of the work. Three assumptions stand
unopposed and are built on; say the word and any of them turns round:

1. **A disabled patch still shows its tint in build mode.** Invisible in
   play, as asked, but if it vanished from build mode too the GM could not
   find it to enable it again.
2. **`:fog --soft-edge`** is written map-wide, so it is taken as the
   default for every patch, with `:fog Crypt --soft-edge` overriding one of
   them, since patches are otherwise settable one at a time.
3. **`reveal manual`** exists: a patch that never lights itself and waits
   for `g r`. It falls out of `reveal` being a number, and it is what a
   scripted reveal wants. It needs the *held* bit to mean anything, which
   *What memory costs* sets out.

## Counters on creatures

**What.** Named numbers on a creature: `HP 4/6`, `Stress 2/6`, `Armor 1/3`
for Daggerheart, a lone `HP 23/40` for anything else. Rules-agnostic: a
counter is a short name, a value and a maximum, and a creature holds four.
The ruleset supplies the names a new creature starts with (`daggerheart`:
HP, Stress, Armor; `none`: nothing) and the GM supplies every number.

**Keys.** The frequent act is "two off the ogre's HP". Rule 5 says a family,
so:

| key | does |
|---|---|
| `s v` | the counters prompt for the selected creature: shows `HP 4/6  Stress 2/6`, takes `hp -2`, `stress +1`, `hp 6/6` (sets both), `armor` (makes it the current one), `-hp` (removes it) |
| `<` `>` | one off / one on the *current* counter of the selected creature; `3<` takes three. The current counter is the last one named, HP to begin with |

`<` and `>` are free in play mode, unbound in vim's normal mode for anything
we use, and a pair, which rule 4 asks of a step-down/step-up. Alternative
considered: `:hp -2` as a command. Rejected: it is the most frequent
change a GM makes in a fight, and rule 8 puts frequent things on keys.

**Where they show.** On the status line for the selected creature, after
the markers: `[Poisoned]  HP 4/6  Stress 2/6`. In the side panel beside
the acting creature's name when the row has room (`▶  18  Ogre  4/6`),
which is the number a GM wants without selecting anything.

**Model and file.** `Token.counters[4]` of `{ char name[8]; int16_t value,
max; }`, 48 bytes a creature; `token_equal` compares them; every change is
an `undo_edit_token`, so `u` takes back a hit. In the file, `tokencounter
HP 4 6` lines under the token, version 6.

**Cost.** Nothing per frame that is not already drawn: the status line and
one panel row change text. Zone `counter.step` on the key; a perf row
`play, counters` stepping `<` and `>` in a loop-neutral pair.

**A tradeoff that request 4 removed.** Play mode's status line and panel are
in the frame the phones receive, so counters built before the players' frame
would be visible to anyone watching. That was written as a leak to accept
for a while. Reordering means it never happens: the players' frame lands
first and counters are never on a phone at all.

## Fog of war

**What.** Fog is made of **patches**: named areas the GM paints on, each
with its own settings, each cleared and changed on its own. A tile belongs
to one patch or to none, and a tile in no patch is always visible, so a map
can have a lit entrance hall, a dark warren behind it that lights a square
at a time, and a mist over the lake that lights three.

The players' frame draws a tile that its patch is still hiding as nothing
at all -- no floor, no walls, no creatures, not even the dot that marks
void, since a field of dots would trace the shape of the room nobody is
supposed to see. The GM's own frame draws it dimmed, so the GM sees the
whole map and also what the players see.

There is no separate "whole map" fog: `:fog all` is a patch that happens to
cover everything, which is one mechanism instead of two. Fog is off by
default, and `:fog off` is a master switch that stops every patch hiding
without unpainting anything -- turning the lights on rather than knocking
the walls down.

**A patch.** Name, extent, and three settings the GM can change at any time,
which is what "make this fog thicker later" means:

| setting | is | does |
|---|---|---|
| `reveal N` | tiles, default 2 | how far a player creature lights the patch as it moves. `reveal 0` lights only the square it stands on; `reveal manual` lights nothing by itself and leaves it all to `g r` |
| `memory on/off` | **default on** | whether ground the party has seen stays drawn after they leave. On is classic fog of war, the party mapping the dungeon as it goes, and it remembers *ground* that was actually inside someone's sight, never creatures and never a corner nobody looked at. Off is a lantern: the dark closes behind them |
| `--soft-edge` | default off | the half-lit rim, below |

`:fog Crypt 1` makes the patch "Crypt" the one the brush paints and gives it
`reveal 1`. Making it thicker later is `:fog Crypt 0`, or `:fog Crypt memory
off`. Patches are addressed by name or any prefix of one, the way
[clocks](../README.md#clocks-clock-tick) are, and a map holds fifteen.

**Keys.** Revealing is frequent and needs a place, so it reads the cursor
(rule 6) and gets a family (rule 5). `g` is vim's "extra commands" prefix
and is unused in play mode:

| key | does |
|---|---|
| `g r` | reveal the cursor's footprint, or the `v` box when one is open |
| `g h` | hide the same again |
| `g R` `g H` | reveal / hide the whole patch under the cursor |
| `g f` | paint the current patch over the cursor's footprint or the box |
| `g c` | scrub any patch off it: this ground is always visible |
| `:fog` | list the patches: `Crypt 1, seen 40/210 · Mist 3` |
| `:fog NAME [N]` | make NAME the current patch, creating it, and set its reveal |
| `:fog NAME memory off` | change a setting |
| `:fog NAME clear` | light the whole patch at once, for when the door opens |
| `:fog NAME hide` | put it all back into the dark |
| `:fog NAME disable` | stop it hiding anything, keeping it and its painting; `enable` puts it back to work |
| `:fog NAME delete` | scrub the patch off the map for good |
| `:fog all [N]` | a patch covering every tile, for plain fog of war |
| `:fog on` `:fog off` | the master switch, changing no painting |
| `:fog --soft-edge` | the half-lit rim, for every patch; `:fog NAME --soft-edge` for one |
| `:player preview` | the players' view on the GM's own screen; `q` leaves it |

The cursor's size (`b`, `3b`) is the brush, as it is for everything else.

`g` is vim's prefix for its odds and ends and is free in play mode; build
mode already holds a pending `g` for `gg`, which a second key distinguishes.
The family splits by what the key *is*: `g f` and `g c` author a patch and
live in build mode with the rest of authoring, while `g r` and `g h` light
and darken during play and live there. Rule 2 is kept because no key means
two things -- each one simply has one home.

**The build-mode indicator.** Painted tiles take a tint under their terrain,
and the tint is *the patch's own colour*, indexed off its number out of a
row of fifteen in the theme, so two patches that touch can be told apart at
a glance and the GM can see which one the brush is about to extend. It is a
different colour again from the dim the GM sees in play: in build mode the
tint says "this patch covers this", in play "the players cannot see this
now". It costs one indexed load per visible tile in build mode and nothing
in play -- see *Drawing* -- and being a background it cannot collide with
the note mark in the tile's corner.

**Line of sight.** The reveal a GM actually wants is "what the party can
see from here", and the boundaries already know it: `map_edge_opaque`
answers whether a crossing stops sight, windows and open doors included.
On a move, each patch within reach lights every tile inside its own
`reveal` of the creature that a line from the creature reaches without an
opaque crossing.

Worst case for one patch is (2N+1)² tiles × N steps: 2,000 crossing tests
at `reveal 6`, and five of those if a party of five walks as a group, which
is tens of microseconds on a keystroke whose whole frame is about thirty.
Two things keep it off the floor, and both must be in the code from the
start rather than added when somebody notices:

- **Every patch keeps a bounding box**, maintained as it is painted. A
  creature's reveal circle is tested against fifteen boxes before any tile
  is touched, so a party in the entrance hall does no work for the warren
  or the lake at all. This is what makes many patches cheaper than one big
  one, not dearer.
- **A tile in no patch needs no test**, which is most of the map.

There is no skipping a tile because it was lit last move: it may not be lit
this move, which is the whole reason sight is recomputed. *Seen* is the bit
that only ever accumulates, and it is derived from *lit*, so it costs no
test of its own.

`reveal` is per patch, so the cost is per patch too: a `reveal 1` dark room
is 9 tiles a move, not 169.

Only on a move of a player creature, never per frame. Doors opening trigger
it too, since the view changed without a step. Zone `fog.auto`, and a perf
row that walks a group through a patch, which is the worst thing anyone
will actually type.

The recompute itself is by area, never by patch: the bits that can change
on a move are inside the union of the old and new reveal circles, a box of
about (2N+2)² tiles. Clear *lit* and *rim* in that box, then relight it from
*every* player creature whose own circle meets it, which is what stops one
creature's lantern putting out another's. It is worth writing the test for
two creatures standing one tile apart before writing the code, because that
is the case a naive clear gets wrong. *What memory costs*, above, has the
numbers.

**Model and file.** Still one byte a tile, `uint8_t *fog` beside `tiles`,
256 KB at the largest map. The byte is now:

| bits | holds |
|---|---|
| 0-3 | which patch, 1 to 15; 0 for none |
| 4 | seen |
| 5 | lit |
| 6 | rim |
| 7 | held |

Ground is drawn when *seen*, *lit* or *held*; a creature when *lit* or
*held*; the soft edge on *rim*. One load and a mask on every drawing path,
which is what the single-bit version cost: **patches are free per tile.**
Everything that varies between patches lives in the patch, not in the tile.

Beside it, `FogPatch patches[15]` on the Map: name, `reveal`, `memory`,
`soft_edge`, `disabled`, and the bounding box the walk tests first. Fifteen
of those is well under a kilobyte, and it is read on a move, not on a
frame.

Painting (`g f`, `g c`, `:fog all`) and the GM's own lighting (`g r`, `g h`,
which set and clear *held* and *seen*) go through the undo log as `OP_FOG`
cells, one op a tile carrying the byte before and after -- the same 20-byte
op as a tile paint, so `u` after a wrong `g f` or `g r` is exact, and
`:fog all` is one batch bounded by the log cap like any fill. *Lit* and
*rim* are never in the log: they are recomputed from where the creatures
stand, and an undo that moves a creature recomputes them like any move. A
patch's *settings* are not undoable, matching clocks, where a tick undoes
and starting a clock does not: they are as easy to retype as to take back.

In the file, patches are header lines and the map is one `fog` section of
rows, a character a tile: `.` for no patch, `A`-`O` for patch 1-15 unseen,
`a`-`o` for seen, and `1`-`9`/`!`-`&` -- the same fifteen shifted -- for
held, since a light the GM put down should still be there after a save.
*Lit* and *rim* are not written: they are where the party is standing this
second, and they are rebuilt from the creatures on the map the moment it
opens. Still one character a tile, so the section is the size the
two-state one would have been, and it is still legible in a text editor.

```
fogpatch 1 Crypt reveal 1 memory off soft-edge
fogpatch 2 Mist reveal 3
fog
.....AAAAA.....
....AAaaaAA....
```

Version 6 when any patch exists. Saving mid-session keeps what the party has
lit, so a recovered autosave resumes the encounter rather than relighting
the dungeon.

**Drawing, and why it is close to free.** `grid_draw` already runs one loop
over the visible tiles, reading each tile and writing its interior cells.
Fog adds a byte load and a branch inside that loop, and then writes the same
cells it was going to write anyway, in a different colour:

| view | a tile its patch is still hiding |
|---|---|
| build | its terrain, tinted in the patch's own colour |
| play, GM | its terrain, dimmed |
| play, players | nothing: `continue`, zero cell writes |

Build mode's tint is the only one that reads the patch, and it reads a
fifteen-entry colour row by index, which is one L1 load. Play mode needs
only "hidden or not", which is the tile byte it already has. **The patch
record is never touched on a drawing path.**

The players' frame is therefore *cheaper* to draw than the GM's wherever fog
is doing its job, which pays back part of the second draw. Nothing
gets its own pass over the tiles, and the window cull stays the only thing
standing between this and map size.

The one place fog costs more than a single load is the lattice loop that
draws walls and junctions: a boundary between two hidden tiles must not be
drawn, or the walls would trace the rooms. That is a second byte load per
corner, still per visible tile.

Creatures: the players' frame skips any whose footprint is wholly hidden,
and silhouettes the ones at the edge (below).

One more guard worth having: a map with no patches takes the path it takes
today, with not even the branch, chosen once per frame rather than once per
tile.

Zones: `fog.reveal` for the keys, `fog.auto` for the walk, and the existing
`grid.draw` carrying the lookups. Perf rows `play, fog` (cursor moves over a
fogged map), `play, fog auto` (a group walking through a `reveal 6` patch),
`play, fog lantern` (the same through a `memory off` patch) and `build, fog
paint`.

### The soft edge

`:fog --soft-edge`, off by default. The rim of the dark is half-lit rather
than blank, so the players see that the room goes on and that something is
moving in it, without being told what.

**The rim travels with the party** rather than sitting on the painted
border. It is the *rim* bit, set during the move walk, never worked out
while drawing. A wall between the two tiles stops it, for the same reason
sight stops at walls: a silhouette through stone would read as a bug.

**A rim tile is one that fog still hides, next to a tile that is lit right
now, across a boundary that does not stop sight.** Neighbours are all
eight, and a diagonal counts only when both of the orthogonal crossings it
is made of are clear, which is the rule `map_blocked` already applies to
movement and is what stops a silhouette appearing around the outside of a
corner. Settled 2026-09-23 from two drafts, taking the anchor from one and
the neighbours from the other:

| | anchored on | neighbours |
|---|---|---|
| first draft | a *visible* tile | eight |
| 2026-09-23 | a **lit** tile | four |
| **settled** | a **lit** tile | **eight** |

*Lit* rather than *visible* because "visible" was written before memory
existed and became ambiguous with it: ground the party walked through an
hour ago is still drawn, and a rim hanging off remembered ground would
scatter silhouettes through rooms nobody is standing near.

Eight rather than four because of a rule that arrived later: a rim tile
draws its walls, which makes corners matter.

- **A creature in melee would vanish.** At `reveal 0` the lit set is the
  one square a creature stands on, so with four neighbours a creature
  diagonally beside it is not on the rim and is not drawn at all, while one
  orthogonally beside it is a silhouette. Both are in melee.
- **A dim wall would get a hole at the corner.** A lit region with a convex
  corner has an unlit tile touching it only diagonally; with four
  neighbours its walls go undrawn, and a room corner reads as a gap in a
  wall that is drawn either side of it.

The four-way rim is tighter and gives away a little less, but not much: at
`reveal 3` the two differ by about four tiles out of twenty, and those four
are the corners. Performance decided nothing here -- per frame both are one
bit, and per move eight costs twelve crossing tests a candidate tile
against four, inside a box the walk already steps through.


**On a rim tile**, in the players' frame:

| | |
|---|---|
| terrain | not drawn, glyph or ground colour, until the tile is fully lit |
| walls | drawn, dimmed |
| a door | drawn as a **wall**, and only becomes a door when the tile is lit. `seg_look` already makes this substitution for secret doors; this is the same line one case further down |
| a creature | drawn, dimmed, its own shape, `?` where its name would be |
| status markers | not drawn |

Drawing one costs *less* than drawing a lit tile: no terrain glyphs, no
terrain background.

**A creature at the rim**, footprint first, so a big one with a single
square in the half-light is covered:

1. any tile of it lit, and it is drawn as itself;
2. else any tile of it on rim, and it is the dimmed `?`;
3. else it is not drawn.

At most nine tiles scanned for a creature the frame was walking anyway.

A silhouette keeps the creature's shape and footprint, since that is what a
silhouette is, but takes a **neutral colour**: the `?` would be undone by a
red square still saying "enemy". Settled 2026-09-23.

**Decisions, and where they stand.**

1. No last-known position: a creature that walks out of sight is simply not
   drawn. **Approved.**
2. A wall between a lit and an unlit tile is drawn on the lit side.
   **Approved**, and the soft edge extends it: at the rim the wall is drawn
   dimmed, and a door in it is drawn as a wall.
3. Auto-reveal is off by default and saved with the map. **Approved**, then
   superseded: it is each patch's `reveal` rather than one switch for the
   map, and `reveal manual` is the old "off".
4. Fog on chosen tiles, a build-mode indicator, and `?` creatures at the
   rim. **Approved and designed above**, the rim settled under 9.
5. Patches with settings that can be changed afterwards, cleared and
   disabled one at a time. **Approved and designed above.** `memory` stays,
   on by default; `delete` and `disable` are separate acts; the rim setting
   is `--soft-edge`.

6. A silhouette loses its side's colour. **Settled: neutral.**
7. Painting fog is **build mode only**, where the rest of authoring lives.
   `g r` and `g h`, which light and darken rather than author, stay in
   play mode. **Settled.**
8. Fifteen patches a map, named and prefix-matched like clocks, with a
   current one the brush paints. **Settled: enough for now.**
9. The rim is anchored on *lit* and counts all eight neighbours.
   **Settled.**

Nothing blocking remains. What is left is three standing assumptions,
listed under *Standing assumptions* near the top of this document, any of which can
be turned round without disturbing the rest.

## The players' frame

**What.** With clients attached, the server renders play mode a second
time into a second renderer, the players' way, and the wire encoder reads
that renderer's diff instead of the GM's. The phones and the watcher
change nothing: they receive the same records, the same size. What the
players' frame leaves out:

- unseen tiles and the creatures on them (fog),
- counters on the status line and in the panel,
- the note hint `(note)`, and every modal and prompt, the note's among
  them -- so the freeze added with notes is gone, replaced by a frame that
  never had them,
- the profiler overlay. (The `[+]` unsaved mark stays: it says nothing
  about the encounter, and hiding it would make every unsaved map draw
  twice.)

...and what it adds: silhouettes at the edge of the dark, which exist in no
other view.

Everything else is identical -- the clocks, the rolls on the status line,
the map itself -- **except anything that would betray a hidden creature's
position or name.** Each of these is drawn in the GM's frame and must be
suppressed or blanked in the players' when its creature is not *lit* or
*held*: the cursor and the selection ring resting on it; the range overlay,
the ruler or the route trail anchored on it; its name in the turn panel and
in the title bar's "Ogre's turn, then Aria", which show `?` instead. And
no modal or prompt of any kind is drawn in the players' frame: they are the
GM's questions, and one of them holds a note's text. This is the checklist
the frame's test walks, one item at a time, with a creature standing in
the dark.

**How.** `app_draw` already takes the renderer from the App; it gains a
`view` argument (`VIEW_GM` / `VIEW_PLAYERS`) threaded to `play_draw`,
`grid_draw`, `turn_draw_panel`, `play_status` and the status-line drawing.
`Net` owns the second `Renderer`, sized with the GM's, resized with it.
The frame sequence in `main` becomes: draw the GM frame and flush it; if
clients are attached, `rnd_begin` the players' renderer, `app_draw(a,
VIEW_PLAYERS)`, then `net_frame_begin`/`rnd_diff`/`net_frame_end`, which
is the existing flush path minus the terminal write. The observer hook
stays as it is.

**`:player preview`** falls out of that argument. It draws the GM's own
terminal with `VIEW_PLAYERS`, so the GM can see what the table sees without
walking round to a phone. Authoring a fogged map without it is guesswork,
and it makes fog testable on its own: a golden frame of the players' view.
`q` leaves it, the way `q` leaves the `?` page, and the key is caught before
the `q` that closes a map. It is a flag on a draw that already takes one,
not a feature.

**Cost, which is the one real price in this document.** A second full draw
per frame while serving. A play frame at 80×24 measures 30 µs end to end
today, so drawing it twice is the largest single cost anything here adds,
and it is paid on a keystroke rather than on a timer. Three things hold it
down, in order of how much they are worth:

1. **Only when someone is watching.** No clients, no second draw, and the
   frame is exactly what it is today. Not live (build mode, the menus), no
   second draw either.
2. **Only when the two frames could differ.** One predicate, in one place:
   is any tile painted with fog, does any creature on screen carry a
   counter, is a note hint showing. If none of that is true the players'
   frame is the GM's frame, and the encoder reads the GM's renderer as it
   does now. This is a privacy boundary, so the predicate is conservative
   by construction -- it answers *true* unless it is certain -- and it gets
   a test that asserts it for every GM-only thing that exists.
3. **Fog makes the players' draw cheaper**, as *Drawing* sets out: a hidden
   tile is a `continue`.

What we are **not** doing: copying the GM's back buffer and overdrawing the
parts that differ. It would be faster than a second draw and it is the
wrong trade here, because getting the layering subtly wrong leaks the GM's
screen to the table. A second draw is obviously correct. If the measurement
says the double draw hurts, that is the moment to plan something cleverer,
which is this project's rule 5 rather than an excuse.

Memory: one more renderer, two cell buffers of the terminal's size, at
`:serve`. Zone `net.players_frame`; the `play, 1 watcher` and `play, 4
watchers` rows will move and are the measurement, with a new `play, 4
watchers, fogged` beside them; the budget in `docs/REMOTE.md` (2 µs per
client per frame) is re-stated as "the players' frame, once, plus 2 µs per
client", since the draw is shared between them.

**What this is not.** It is not model streaming (each phone drawing at its
own size), which `docs/REMOTE.md` left for later and which this makes
less urgent: the phone page already fits the GM's frame to the screen.
Model streaming remains the way to a touch map that pans on its own, and
is a separate plan.

## Order

The fourth request turns the order round. It was counters, fog, players'
frame. It should now be:

1. **The players' frame.** *Built 2026-09-23.* The `VIEW_GM` /
   `VIEW_PLAYERS` argument (`app_draw_view`, `a->view`), `Net.players`, the
   gate (`app_view_differs`), `app_frame` as the one frame sequence, and
   `:player preview`. Two things were decided in the building: the `[+]`
   unsaved mark stays in the players' frame, since it says nothing about
   the encounter and hiding it would make every unsaved map draw twice; and
   the copy happens before the GM's flush, because the flush swaps its
   buffers. The note freeze is gone.
2. **Counters.** With the frame already in place they never reach a phone,
   so the leak described under *Counters on creatures* never happens.
3. **Fog**, which requests 4 and 5 grew past one commit's worth. In order,
   each one usable at the table before the next starts:

   a. Patches and painting: the tile byte, the patch table, `g f`, `g c`,
      `g r`, `g h`, the build tint, the GM's dim, the players' blank, the
      file, undo. Fog works here, revealed by hand.
   b. Line of sight: per-patch `reveal`, the bounding-box test, the
      recompute on every trigger in the list, *held* for the GM's hand,
      memory both ways.
   c. The soft edge, which by now has somewhere to be drawn.

The reason for the change is that both halves of request 4 live in the
players' frame, and so does the point of fog: built the old way round, fog
would ship as a dimming effect on the GM's own screen and then be retrofitted
into a view that did not exist yet. Doing the plumbing first also means each
later feature adds one condition to a mechanism that is already tested,
rather than threading a new argument through five files while also being a
new feature.

The cost of the change is that step 1 delivers nothing anyone can see. It is
one commit, it is measurable (the frame must not move when nobody is
watching), and it is the shortest path to the rest.

Each step is its own commit series with its perf rows. The file format goes
to version 6 once, with counters, and fog's `fog` section rides on it.
