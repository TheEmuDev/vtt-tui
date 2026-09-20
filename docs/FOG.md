# Fog of war, counters, and the players' frame: a plan

Three of the features asked for on 2026-09-20 turn out to be one design.
Fog of war needs the players to see less than the GM. Counters on creatures
(an ogre's hit points) are the GM's business, not the table's. And the
remote view today sends the players the GM's frame, byte for byte. The
piece all three want is a **players' frame**: a second rendering of play
mode, drawn for the phones, with the fog opaque and the GM's numbers left
out. This document plans the three in the order they should land, with the
decisions and what each costs. Nothing here is built.

## 1. Counters on creatures

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

**The tradeoff to accept for now.** Play mode's status line and panel are in
the frame the phones receive, so until part 3 lands, a creature's counters
are visible to anyone watching. At an in-person table with the ogre's HP
on the GM's screen that is a small leak, and the fix is the same one fog
needs, so it is not worth a stopgap here.

## 2. Fog of war

**What.** Each tile is revealed or not. The GM reveals as the party goes;
the players' frame draws unrevealed tiles as nothing at all -- no floor,
no walls, no creatures -- and the GM's own frame draws them dimmed, so the
GM sees the whole map and also what the players see. Fog is off by
default; `:fog on` hides everything, `:fog off` clears it and forgets the
reveals.

**Keys.** Revealing is frequent and needs a place, so it reads the cursor
(rule 6) and gets a family (rule 5). `g` is vim's "extra commands" prefix
and is unused in play mode:

| key | does |
|---|---|
| `g r` | reveal the cursor's footprint, or the `v` box when one is open |
| `g h` | hide the same |
| `g R` `g H` | reveal / hide everything |
| `:fog on` `:fog off` | the switch |
| `:fog auto N` | reveal within N squares of every player creature as it moves, by line of sight -- see below |

The cursor's size (`b`, `3b`) is the brush, as it is for everything else.

**Line of sight.** The reveal a GM actually wants is "what the party can
see from here", and the boundaries already know it: `map_edge_opaque`
answers whether a crossing stops sight, windows and open doors included.
`:fog auto N` walks every tile within N of a player creature's centre and
reveals it if a line from the centre reaches it without an opaque
crossing (a step-by-step walk of the line, checking each crossing).
That is at most (2N+1)² tiles × N steps a move -- 2,000 crossing tests at
N=6, microseconds -- and only on a move of a player creature, never per
frame. Reveals accumulate: what was seen stays seen. Doors opening trigger
it too, since the view changed without a step.

**Model and file.** `Map.fog` (on/off) and `uint8_t *seen`, one byte a
tile like `tiles`, 256 KB at the largest map. Reveals go through the undo
log as `OP_SEEN` cells, one op a tile, the same 20-byte op as a tile paint,
so `u` after a wrong `g r` is exact and a whole-map `g R` is one batch
(and, like a fill, bounded by the log cap). In the file: `fog on` and a
`seen` section of rows, `#` for revealed, version 6 when fog is on.

**Drawing.** The GM's frame: `grid_draw` takes a fog pointer and dims the
tile's cells (a darker background, the same glyphs) -- one lookup per
visible tile, on the path that already reads the tile. The players' frame
(part 3): the same call with a flag that draws an unseen tile as void and
skips creatures standing on unseen tiles. Cost is confined to the window by
`grid_visible_tiles` as everything is. Zones `fog.reveal` (the key and
the auto walk) and the existing `grid.draw` carrying the extra lookup; perf
rows `play, fog` (cursor moves with fog on) and `play, fog auto` (a
carried player creature with `:fog auto 6`).

**Decisions to confirm.**

1. Fog hides creatures on unseen tiles from the players' frame, but a
   creature that has been *seen* on a revealed tile and then stepped into
   the dark is simply not drawn -- no "last known position". Simpler, and
   what a table expects.
2. Reveals are tiles, not boundaries: a wall between a seen and an unseen
   tile is drawn on the seen side, which is what a wall looks like from a
   lit room.
3. `:fog auto` is off by default; the GM turns it on per map and it is
   saved with the map.

## 3. The players' frame

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

**Cost.** A second full draw per frame, only while serving in play mode:
`app.draw` today is 40-70 µs for a busy 80×24 frame, so the server's per-
frame cost with clients roughly doubles from the 3.7 µs/client measured to
"one more draw", still well under a millisecond, and zero with no clients.
Memory: one more renderer, two cell buffers of the terminal's size, at
`:serve`. Zone `net.players_frame`; the `play, 1 watcher` and `play, 4
watchers` rows will move and are the measurement; the budget in
`docs/REMOTE.md` (2 µs per client per frame) is re-stated as "the players'
frame plus 2 µs per client", since the draw is shared.

**What this is not.** It is not model streaming (each phone drawing at its
own size), which `docs/REMOTE.md` left for later and which this makes
less urgent: the phone page already fits the GM's frame to the screen.
Model streaming remains the way to a touch map that pans on its own, and
is a separate plan.

## Order

1. Counters (a day: token field, prompt, two keys, panel row, file, tests).
2. Fog: model, `g` keys, GM dimming, file, undo, tests. Then `:fog auto`
   with line of sight.
3. The players' frame, which switches fog on for the phones and takes the
   counters and notes off them.

Each is its own commit series with its perf rows; the version bump to 6
happens once, with counters, and fog's `seen` section rides on it.
