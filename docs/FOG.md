# Fog of war, counters, and the players' frame: a plan

Three of the features asked for on 2026-09-20 turn out to be one design.
Fog of war needs the players to see less than the GM. Counters on creatures
(an ogre's hit points) are the GM's business, not the table's. And the
remote view today sends the players the GM's frame, byte for byte. The
piece all three want is a **players' frame**: a second rendering of play
mode, drawn for the phones, with the fog opaque and the GM's numbers left
out. This document describes the three, with the decisions and what each costs;
*Order* at the end says which lands first. Nothing here is built.

## What the fourth request changes

Request 4 -- fog painted on chosen tiles rather than the whole map, with a
build-mode indicator, and silhouettes at the edge of the dark -- lands more
cheaply than it looks, and it moves the order.

- **The model absorbs it for free.** The planned `seen` byte per tile
  becomes a fog byte holding two bits: *hides* (authored in build mode) and
  *seen* (revealed in play). Same array, same one-byte load, same 256 KB at
  the largest map. Whole-map fog stops being a special case and becomes
  "every tile authored".
- **Nothing gains a drawing pass.** Both the GM's dimming and the build
  indicator fold into the single per-tile loop `grid_draw` already runs over
  the visible tiles: one byte load and a branch, choosing a different colour
  for cell writes that were happening anyway. Fog is never its own sweep.
  Where fog hides a tile the players' frame writes *less* than the GM's, so
  a fogged map is cheaper to draw for the players than for the GM.
- **Silhouettes are per token, never per tile.** "Is this creature at the
  edge of the dark?" is answered from its own footprint and its neighbours,
  eight lookups a creature, not by computing an edge set over the map.
- **It moves the players' frame to the front.** Silhouettes only exist in
  the players' frame, and so does the point of fog. Building fog first would
  ship a half-feature and then retrofit it. See *Order*.
- **One addition falls out of it:** `:fog preview`, drawing the GM's own
  terminal the players' way. Authoring a fogged map is guesswork without it,
  and once `app_draw` takes a view argument it is a flag, not a feature.

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

**What.** Fog covers the tiles the GM paints it on, and those tiles are
either revealed or not. A tile with no fog on it is always visible, so a
map can have a lit entrance hall and a dark warren behind it. The players'
frame draws a covered, unrevealed tile as nothing at all -- no floor, no
walls, no creatures, not even the dot that marks void, since a field of
dots would trace the shape of the room nobody is supposed to see. The GM's
own frame draws it dimmed, so the GM sees the whole map and also what the
players see.

Fog is off by default. `:fog on` is the master switch, `:fog all` paints
every tile, `:fog none` scrubs the painting off, and `:fog off` leaves the
painting alone and stops it hiding anything -- the difference between
turning the lights on and demolishing the walls.

**Keys.** Revealing is frequent and needs a place, so it reads the cursor
(rule 6) and gets a family (rule 5). `g` is vim's "extra commands" prefix
and is unused in play mode:

| key | does |
|---|---|
| `g r` | reveal the cursor's footprint, or the `v` box when one is open |
| `g h` | hide the same again |
| `g R` `g H` | reveal / hide everywhere |
| `g f` | paint fog over the cursor's footprint or the box: this ground *can* be hidden |
| `g c` | clear the painting off it: this ground is always visible |
| `:fog on` `:fog off` | the master switch |
| `:fog all` `:fog none` | paint every tile, or none |
| `:fog auto N` | reveal within N squares of every player creature as it moves, by line of sight -- see below |
| `:fog edge on` | silhouettes at the edge of the dark -- see below |
| `:fog preview` | draw the GM's own terminal the players' way |

The cursor's size (`b`, `3b`) is the brush, as it is for everything else.

`g` is vim's prefix for its odds and ends and is free in play mode; build
mode already holds a pending `g` for `gg`, which a second key distinguishes.
The whole family works in both modes, because rule 2 wants one meaning
everywhere: painting fog is authoring and belongs to build mode, revealing
is play, and a GM who wants to fog a room they forgot mid-session should
not have to change modes to do it.

**The build-mode indicator.** Painted tiles take a tint under their terrain,
a different colour from the dim the GM sees in play: in build mode it says
"fog may hide this", in play mode "the players cannot see this now". It
costs nothing extra to draw -- see *Drawing* below -- and it is a background,
so it cannot collide with the note mark in the tile's corner.

**Line of sight.** The reveal a GM actually wants is "what the party can
see from here", and the boundaries already know it: `map_edge_opaque`
answers whether a crossing stops sight, windows and open doors included.
`:fog auto N` walks every tile within N of a player creature's centre and
reveals it if a line from the centre reaches it without an opaque
crossing (a step-by-step walk of the line, checking each crossing).
Worst case that is (2N+1)² tiles × N steps a move: 2,000 crossing tests at
N=6, and five of those if a party of five walks as a group, which is tens
of microseconds on a keystroke whose whole frame is about thirty. Two
things keep it off the floor, and both must be in the code from the start:

- **A tile that is already revealed needs no line test.** Test the bit
  first and skip. In a corridor the party has walked, almost everything in
  range is already seen, so the steady-state cost collapses to the handful
  of tiles at the new edge. This also flattens the group case: the second
  creature's walk is nearly all skips, because the first one revealed it.
- **A tile with no fog painted on it needs no test either**, which is the
  whole map outside the warren.

Only on a move of a player creature, never per frame. Reveals accumulate:
what was seen stays seen. Doors opening trigger it too, since the view
changed without a step. Zone `fog.auto`, and a perf row that moves a group,
which is the worst case anyone will actually type.

**Model and file.** `Map.fog` (the master switch) and `uint8_t *fog`, one
byte a tile like `tiles`, 256 KB at the largest map, holding two bits:

| bit | set by | means |
|---|---|---|
| `FOG_HIDES` | `g f`, `:fog all` (build) | fog is painted here |
| `FOG_SEEN` | `g r`, `:fog auto` (play) | and the party has seen it |

A tile is hidden from the players when the master switch is on, `FOG_HIDES`
is set and `FOG_SEEN` is not. Two bits in the byte the plan already spent,
so localising fog costs no memory and no extra lookup: the one load answers
both questions.

Both painting and revealing go through the undo log as `OP_FOG` cells, one
op a tile carrying the byte before and after -- the same 20-byte op as a
tile paint, so `u` after a wrong `g f` or `g r` is exact, and `:fog all` is
one batch bounded by the log cap like any fill.

In the file, one `fog` section of rows with three characters: a space for
unpainted, `#` for painted and unseen, `.` for painted and seen. One
section, the same size the two-state `seen` section would have been.
Version 6 when any tile is painted. Saving mid-session keeps what the party
has seen, so a recovered autosave resumes the encounter rather than
relighting the dungeon.

**Drawing, and why it is close to free.** `grid_draw` already runs one loop
over the visible tiles, reading each tile and writing its interior cells.
Fog adds a byte load and a branch inside that loop, and then writes the same
cells it was going to write anyway, in a different colour:

| view | a painted tile the party has not seen |
|---|---|
| build | its terrain, tinted, so the GM can see what is covered |
| play, GM | its terrain, dimmed |
| play, players | nothing: `continue`, zero cell writes |

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

Zones: `fog.reveal` for the keys, `fog.auto` for the walk, and the existing
`grid.draw` carrying the lookups. Perf rows `play, fog` (cursor moves over a
fogged map), `play, fog auto` (a group walking with `:fog auto 6`) and
`build, fog paint`.

### Silhouettes at the edge of the dark

Request 4's second half, and a rule that lives in the players' frame alone:
a creature standing on
the first hidden tile beyond what the party can see is drawn, but as a
shape with `?` where its letter would be. It says *something is there* and
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

**Off by default**, per the request: `:fog edge on`, saved with the map.

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
   4b. `:fog off` leaves the painting in place and only stops it hiding;
       `:fog none` scrubs the painting. Two words for two different acts,
       or is that a distinction that will be misremembered at the table?
   4c. `g f` paints fog and `g c` clears it, in both modes. The alternative
       is to confine painting to build mode, where the rest of authoring
       lives.


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
3. **Fog.** The model and the `g` family, the build tint, the GM's dim, the
   players' blank, the file, undo. Then `:fog auto` with line of sight.
   Then silhouettes, which have somewhere to be drawn.

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
