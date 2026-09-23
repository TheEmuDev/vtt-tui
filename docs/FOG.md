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
  which patch, one says lit. Everything that varies between patches --
  reveal distance, memory, silhouettes, colour, name -- lives in a
  fifteen-entry table that no drawing path ever reads. A tile costs one
  load and two masks either way, and the array is the 256 KB it always was.
- **Nothing gains a drawing pass.** The GM's dim, the players' blank and
  the build-mode tint all fold into the single per-tile loop `grid_draw`
  already runs, writing the cells it was going to write in another colour.
  Where fog hides a tile the players' frame writes *less* than the GM's, so
  a fogged map is cheaper to draw for the players than for the GM.
- **Many patches are cheaper than one big one.** Each carries a bounding
  box, so a party in the entrance hall does no work at all for the warren
  or the lake, and reveal distance is per patch: a `reveal 1` dark room
  tests 9 tiles a move where a map-wide `reveal 6` tests 169.
- **Silhouettes are per creature, never per tile.** "Is this one at the
  edge of the dark?" is its own footprint against its eight neighbours, not
  an edge set computed over the map.
- **One trait, `memory off`, is the only real new cost**, and it is the
  only part that can be cut without disturbing the rest. See *Line of
  sight*.
- **It moves the players' frame to the front.** Silhouettes live there and
  so does the point of fog. Building fog first would ship a half-feature
  and retrofit it. See *Order*.
- **One addition falls out of it:** `:fog preview`, drawing the GM's own
  terminal the players' way. Authoring a fogged map is guesswork without it,
  and once `app_draw` takes a view argument it is a flag, not a feature.

## Fog of war: the agreed behaviour

Your summary of 2026-09-22, which is the spec the rest of this document now
serves. Two of these changed what was written above, and *What it costs*
below says how.

1. Fog is off by default.
2. With it on, ground outside the players' sight is not drawn.
3. **Memory is off by default** and can be turned on per patch.
4. **With memory on, ground the party has seen stays drawn once they have
   left. Creatures in it do not**: a creature is drawn only where the party
   can see right now.
5. Sight range can be changed at any time, mid-session.
6. Fog applies to painted regions, not the whole map.
7. A map holds several patches, each updated or disabled on its own.
8. Optionally, the fog's outer rim is half-hidden: creatures there are drawn
   without anything that identifies them, `?` in place of the name. Off by
   default, enabled by a `--flag`.

### What the summary does not settle

Nine things the eight points leave open. The first four change the code; the
rest are wording and defaults.

1. **Whose screen loses the ground?** The GM must keep seeing the whole map
   or they cannot run the encounter, so fog hides things in the *players'*
   frame and only dims them in the GM's. That is why the players' frame has
   to be built before fog, and it is the biggest structural claim in this
   document.
2. **Does sight stop at walls, or is it a plain radius?** "Visual range"
   reads as distance alone. Every design above assumes line of sight as
   well, using the boundaries the map already carries, so a creature cannot
   see through the wall of a dark room. A plain radius is cheaper and much
   less useful.
3. **Where is the half-hidden rim?** Point 8 says "outer perimeter of the
   fog patch", which is a fixed border drawn where the painting ends.
   Request 4 said "the edge of view", which moves with the party. They are
   the same only while the party is outside the patch looking in. The
   moving one is what simulates a silhouette at the limit of a torch.
4. **What else shows on a half-hidden tile?** A creature shows as `?`. Does
   its ground show, or does the tile stay blank with the silhouette on it?
   Showing the ground tells the players the shape of the room.
5. **"Disabled" is a third state.** A patch can be scrubbed off the map, or
   lit all at once, and now also switched off while keeping its painting.
   Three acts needing three names that cannot be confused at the table.
6. **Revealing by hand.** `g r` and `g h` light and darken what the cursor
   covers, and `reveal manual` is a patch that only ever does that. The
   summary describes sight as automatic throughout.
7. **The build-mode indicator** that request 4 asked for: painted tiles
   tinted in their patch's colour, so the GM can see what is covered and
   which patch the brush will extend.
8. **Which `--flag`, and on what?** `:serve --stay-alive` is the precedent.
   The rim setting belongs to a patch and is saved with the map, so it
   wants `:fog Crypt --silhouettes` rather than a command-line option.
9. **`:fog preview`**, drawing the GM's own terminal the players' way.
   Still unanswered, and with memory off it is the only way to check an
   authored map without walking round to a phone.

### What it costs, now that memory is off by default

Point 4 is the one that reshapes the code, and point 3 removes the escape
hatch this document was relying on.

Remembered ground and visible ground are **two different things**, because
creatures follow one and not the other. So the tile carries two bits, not
one:

| bits | holds |
|---|---|
| 0-3 | which patch, 1 to 15; 0 for none |
| 4 | **seen**: the party has been here (only kept when the patch remembers) |
| 5 | **lit**: the party can see it right now |
| 6-7 | spare |

Still one byte a tile, still one load. Ground is drawn when *seen* or *lit*;
a creature is drawn only when *lit*.

The consequence is that **the lit set has to be recomputed as the party
moves, whatever memory says.** The earlier draft leaned on memory-on being
the cheap case, where lighting only ever set bits and a tile already lit
could be skipped without a line test. That saving is gone: a tile lit last
move may not be lit this one, so it must be tested again. Memory now decides
only whether the *seen* bit sticks, and one mechanism runs for every patch.

It is still area-bounded, never patch-bounded: the bits that can change on a
move live inside the union of the old and new sight circles, so clear that
box and relight it from every player creature whose own circle meets it.
What it costs, worst case, walking a whole party through a wide patch:

| | |
|---|---|
| tiles in the box at `reveal 6` | about 196 |
| line steps each | up to 6 |
| creatures relighting it | up to the party |

Which lands in the tens of microseconds on a keystroke whose whole frame is
about thirty. Two things are now mandatory rather than nice:

- **One recompute per keystroke, not per creature.** A group walking
  together is one keypress; the lit set is rebuilt once after every member
  has stepped. Doing it per member would repeat the whole box per creature.
- **The bounding-box test comes first**, so a party in the entrance hall
  does no work for the warren or the lake.

Two smaller effects. Fog now *changes* on most moves rather than only
growing, so more cells differ per frame and more bytes go to the phones: a
moving rim is a few hundred bytes a step against the 5 KB a full frame
measured, which is comfortable. And the GM's dimmed view and `:fog preview`
matter more than they did, because with memory off the GM is the only one
who can see where the party has been.

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
| `memory on/off` | **default off** | whether ground the party has seen stays drawn after they leave. Off is a lantern, and the default: the dark closes behind them. On is classic fog of war, the party mapping the dungeon as it goes -- but it remembers *ground*, never creatures |
| `edge on/off` | default off | silhouettes at this patch's rim, below |

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
| `:fog NAME memory off` | change a setting; `edge on` likewise |
| `:fog NAME clear` | light the whole patch at once, for when the door opens |
| `:fog NAME hide` | put it all back into the dark |
| `:fog NAME off` | scrub the patch off the map entirely |
| `:fog all [N]` | a patch covering every tile, for plain fog of war |
| `:fog on` `:fog off` | the master switch, changing no painting |
| `:fog preview` | draw the GM's own terminal the players' way |

The cursor's size (`b`, `3b`) is the brush, as it is for everything else.

`g` is vim's prefix for its odds and ends and is free in play mode; build
mode already holds a pending `g` for `gg`, which a second key distinguishes.
The whole family works in both modes, because rule 2 wants one meaning
everywhere: painting fog is authoring and belongs to build mode, revealing
is play, and a GM who wants to fog a room they forgot mid-session should
not have to change modes to do it.

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
Three things keep it off the floor, and all three must be in the code from
the start rather than added when somebody notices:

- **Every patch keeps a bounding box**, maintained as it is painted. A
  creature's reveal circle is tested against fifteen boxes before any tile
  is touched, so a party in the entrance hall does no work for the warren
  or the lake at all. This is what makes many patches cheaper than one big
  one, not dearer.
- **A tile that is already lit needs no line test.** Test the bit first and
  skip. In a corridor the party has walked, almost everything in range is
  already seen, so the steady state collapses to the handful of tiles at
  the new edge. It flattens the group case too: the second creature's walk
  is nearly all skips, because the first one lit it.
- **A tile in no patch needs no test either**, which is most of the map.

`reveal` is per patch, so the cost is per patch too: a `reveal 1` dark room
is 9 tiles a move, not 169.

Only on a move of a player creature, never per frame. Doors opening trigger
it too, since the view changed without a step. Zone `fog.auto`, and a perf
row that walks a group through a patch, which is the worst thing anyone
will actually type.

**`memory off` is the one trait that costs something.** With memory on,
lighting only ever adds bits, so the walk above is the whole story. With it
off the dark has to close behind the party, and that cannot be done by
clearing the patch and relighting it: clearing is O(patch), and a patch can
be the whole map.

It is done by area instead. The bits that can change on a move are inside
the union of the old and new reveal circles, a box of about (2N+2)² tiles.
Clear that box, then relight it from *every* player creature whose own
circle meets it, which is what stops one creature's lantern putting out
another's. Cost stays O(N²) per move and never touches patch size. It is
worth writing the test for two creatures standing one tile apart before
writing the code, because that is the case a naive clear gets wrong.

If this trait is cut, everything above it still stands.

**Model and file.** Still one byte a tile, `uint8_t *fog` beside `tiles`,
256 KB at the largest map. The byte is now:

| bits | holds |
|---|---|
| 0-3 | which patch, 1 to 15; 0 for none |
| 4 | lit |
| 5-7 | spare |

A tile is hidden when the master switch is on, its patch is not 0, and the
lit bit is clear. One load and two masks, which is what the single-bit
version cost: **patches are free per tile.** Everything that varies between
patches lives in the patch, not in the tile.

Beside it, `FogPatch patches[15]` on the Map: name, `reveal`, `memory`,
`edge`, and the bounding box the walk tests first. Fifteen of those is well
under a kilobyte, and it is read on a move, not on a frame.

Both painting and lighting go through the undo log as `OP_FOG` cells, one
op a tile carrying the byte before and after -- the same 20-byte op as a
tile paint, so `u` after a wrong `g f` or `g r` is exact, and `:fog all` is
one batch bounded by the log cap like any fill. A patch's *settings* are
not undoable, matching clocks, where a tick undoes and starting a clock
does not: they are as easy to retype as to take back.

In the file, patches are header lines and the map is one `fog` section of
rows, a character a tile: `.` for no patch, `A`-`O` for patch 1-15 unlit,
`a`-`o` for lit. Still one character a tile, so the section is the size the
two-state one would have been, and it is still legible in a text editor.

```
fogpatch 1 Crypt reveal 1 memory off
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
`play, fog lantern` (the same through a `memory off` patch, which is the
expensive trait) and `build, fog paint`.

### Silhouettes at the edge of the dark

A rule that lives in the players' frame alone: a creature standing on the
first hidden tile beyond what the party can see is drawn, but as a shape
with `?` where its letter would be. It says *something is there* and
nothing else. Deeper in the dark a creature is not drawn at all.

**Edge** means: the tile is hidden, and at least one of its eight
neighbours is visible to the players across a boundary that does not stop
sight. Diagonals follow the rule `map_blocked` already uses -- a diagonal
counts only if both of the orthogonal crossings it is made of are clear --
so a silhouette cannot appear around the outside of a corner.

**Cost.** Asked per creature, never per tile: for each creature the frame
was going to draw anyway, its own footprint (at most 9 tiles) against at
most 8 neighbours. Two dozen creatures is a couple of hundred byte loads on
a path that is already walking them. Computing an edge set over the map
would be O(tiles) and is the trap to avoid.

**Off by default**, and per patch rather than per map: `:fog Crypt edge on`.
A dark room wants silhouettes; a patch hiding a corridor the party has not
reached yet should give nothing away. Saved with the patch.

**To confirm.** A silhouette keeps the creature's shape and footprint, since
that is what a silhouette is, but takes a neutral colour rather than the
player blue or enemy red -- otherwise the `?` hides the name while the
colour still says which side it is on. Status markers are not drawn.

**Decisions to confirm.**

1. Fog hides creatures on unseen tiles from the players' frame, but a
   creature that has been *seen* on a revealed tile and then stepped into
   the dark is simply not drawn -- no "last known position". Simpler, and
   what a table expects. ----  Approved
2. Reveals are tiles, not boundaries: a wall between a seen and an unseen
   tile is drawn on the seen side, which is what a wall looks like from a
   lit room. ---- Approved
3. `:fog auto` is off by default; the GM turns it on per map and it is
   saved with the map.  ---- Approved

   *Superseded by request 5, and worth re-reading before it counts as
   approved.* Auto-reveal is no longer a per-map switch: it is each patch's
   `reveal`, so a map can light one room a square at a time and another
   three squares at a time. `reveal manual` is the old "off". It is still
   saved with the map, now with the patch.
4. I would also like fog to be something that can be localized onto specific tiles. For example
   I want the ability to design a map such that a specific location can have fog while the rest of
   the map behaves normally. It would also need a visual indicator in build mode. Additionally, 
   as an optional behavior. The first tile that is considered hidden due to fog should still show tokens but should not
   show any other information. The letter should be replaced with '?' so that it simulates seeing a silhouette at the edge of view.
   The other tiles in fog should behave as normal.

   *Designed above*, in *What.*, *Keys*, *Model and file*, *Drawing* and
   *Silhouettes at the edge of the dark*. It costs no memory and no extra
   drawing pass. Three smaller things it raises, which need an answer:

   4a. A silhouette keeps its shape and footprint but loses its colour, so
       the `?` is not undone by a red square saying "enemy". Or keep the
       colour, and accept that the side shows?
   4b. `g f` paints fog and `g c` clears it, in both modes. The alternative
       is to confine painting to build mode, where the rest of authoring
       lives.

5. Patches, their settings, and changing them later (*designed above*, in
   *What*, *Keys*, *Model and file* and *Line of sight*). Three answers
   needed:

   5a. The settings are `reveal`, `memory` and `edge`. `reveal` and the
       separate clearing were asked for; `memory` (does the dark close
       behind the party) and `edge` are my reading of "more foggy". Is
       `memory` worth its cost, which is the only awkward code in this
       document? Cutting it changes nothing else.
   5b. Fifteen patches a map, named and prefix-matched like clocks, with a
       current one the brush paints. Enough?
   5c. `:fog Crypt clear` lights the patch and `:fog Crypt off` scrubs it
       off the map. Two very different acts a keystroke apart. Better
       words?


## The players' frame

**What.** With clients attached, the server renders play mode a second
time into a second renderer, the players' way, and the wire encoder reads
that renderer's diff instead of the GM's. The phones and the watcher
change nothing: they receive the same records, the same size. What the
players' frame leaves out:

- unseen tiles and the creatures on them (fog),
- counters on the status line and in the panel,
- the note hint `(note)` and the note prompt -- so the freeze added with
  notes goes away, replaced by a frame that never had them,
- the profiler overlay, and the `[+]` unsaved mark.

...and what it adds: silhouettes at the edge of the dark, which exist in no
other view.

Everything else is identical: the cursor, the range overlay, the ruler,
the turn order, the clocks, the rolls on the status line.

**How.** `app_draw` already takes the renderer from the App; it gains a
`view` argument (`VIEW_GM` / `VIEW_PLAYERS`) threaded to `play_draw`,
`grid_draw`, `turn_draw_panel`, `play_status` and the status-line drawing.
`Net` owns the second `Renderer`, sized with the GM's, resized with it.
The frame sequence in `main` becomes: draw the GM frame and flush it; if
clients are attached, `rnd_begin` the players' renderer, `app_draw(a,
VIEW_PLAYERS)`, then `net_frame_begin`/`rnd_diff`/`net_frame_end`, which
is the existing flush path minus the terminal write. The observer hook
stays as it is.

**`:fog preview`** falls out of that argument. It draws the GM's own
terminal with `VIEW_PLAYERS`, so the GM can see what the table sees without
walking round to a phone. Authoring a fogged map without it is guesswork,
and it makes fog testable on its own: a golden frame of the players' view.
`esc` or a second `:fog preview` puts the GM's view back. It is a flag on a
draw that already takes one, not a feature.

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

1. **The players' frame.** The `VIEW_GM` / `VIEW_PLAYERS` argument, the
   second renderer, the gate that skips it, `:fog preview` riding along.
   Nothing is hidden yet, so it ships as pure plumbing with the frames
   identical, which is the easiest possible thing to verify: byte for byte
   the same as today.
2. **Counters.** With the frame already in place they never reach a phone,
   so the leak described under *Counters on creatures* never happens.
3. **Fog**, which requests 4 and 5 grew past one commit's worth. In order,
   each one usable at the table before the next starts:

   a. Patches and painting: the tile byte, the patch table, `g f`, `g c`,
      `g r`, `g h`, the build tint, the GM's dim, the players' blank, the
      file, undo. Fog works here, revealed by hand.
   b. Line of sight: per-patch `reveal`, the bounding-box test, the
      skip-if-lit test. `memory off` only if 5a says so.
   c. Silhouettes, which by now have somewhere to be drawn.

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
