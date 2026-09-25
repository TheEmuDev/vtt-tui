# Performance

Every path that costs anything, measured. `make perf` regenerates the tables below;
they are its output, not estimates, so keeping them true is a command rather than a
promise.

Recorded on an **Intel i7-4510U @ 2.00GHz**, gcc 16.2.1, `-O2`. Treat the absolute
numbers as a baseline for *this* machine and the ratios between rows as the part that
travels.

## The budget

A keystroke has about **16ms** before a person notices. Nothing here is close, and the
point of writing the numbers down is to keep it that way: a change that moves a row by
a factor is worth a conversation before it lands, and a change that adds a new row is
worth measuring before it is called done.

Two properties matter more than any single figure:

- **Cost follows the window, not the map.** A 200×200 map costs the same at 80×24 as a
  40×25 one does. Every drawing path culls to the visible tiles first.
- **Idle costs nothing.** `poll()` blocks until there is input; there is no frame loop.

## Frame times

What the app costs to use. Each scenario replays a keystroke script, a frame per key,
400 times. Every figure is the median of three such runs, row by row
(`tools/median.py`): a single run reliably has one row spiking somewhere, and never the
same one twice. The runs want the machine to themselves -- editing a file while one was
in flight was enough to lift the play rows by a fifth, and an A/B against the previous
binary is what showed it was the machine and not the code.

| scenario             | size   | frame p50 | frame p99 | cells | bytes |
|----------------------|--------|-----------|-----------|-------|-------|
| build, open          | 80x24  |    30.4us |    69.2us |    10 |   207 |
| build, every edge    | 80x24  |    31.1us |    59.8us |    10 |   207 |
| build, 200x200       | 80x24  |    31.0us |    43.5us |    12 |   209 |
| build, 200x200       | 200x50 |   128.4us |   156.0us |    12 |   212 |
| build, mostly void   | 200x50 |   108.7us |   141.1us |    20 |   267 |
| build, noted squares | 80x24  |    31.1us |    46.9us |    88 |   524 |
| build, tracing       | 80x24  |    30.6us |    38.0us |     2 |   140 |
| build, circle brush  | 80x24  |    31.6us |    36.2us |    28 |   292 |
| build, 3x3 brush     | 80x24  |    26.9us |    38.1us |    18 |   128 |
| build, fill+undo 200 | 80x24  |    28.0us |    93.1us |   303 |  3398 |
| build, fill history  | 80x24  |    29.5us |   108.3us |   335 |  4145 |
| ruler, three legs    | 80x24  |    27.3us |    43.9us |    11 |    40 |
| play, 24 tokens      | 80x24  |    31.3us |    49.2us |    19 |   298 |
| play, 24 tokens      | 200x50 |   112.3us |   138.6us |    36 |   315 |
| play, carrying       | 80x24  |    26.6us |    37.8us |    33 |   205 |
| play, carry 200x200  | 80x24  |    27.2us |    37.5us |    22 |   136 |
| play, 3x3 cursor     | 80x24  |    34.4us |    42.5us |    37 |   606 |
| play, choosing       | 80x24  |    28.4us |    41.3us |    12 |    95 |
| play, group box      | 80x24  |    26.6us |    36.3us |    18 |   133 |
| play, group carry    | 80x24  |    26.2us |    37.3us |    22 |   131 |
| play, range bands    | 80x24  |    36.4us |    66.4us |   129 |   657 |
| play, range radius   | 80x24  |    58.2us |    87.3us |   169 |   958 |
| play, range cone     | 80x24  |    36.8us |    81.0us |    60 |   654 |
| play, range line     | 80x24  |    36.7us |    80.8us |    55 |   620 |
| play, range square   | 80x24  |    36.0us |    79.4us |    63 |   669 |
| play, turn order     | 80x24  |    34.8us |    48.8us |    49 |   488 |
| play, fight cycling  | 80x24  |    45.9us |    91.6us |   317 |  4596 |
| play, spotlight      | 80x24  |    29.7us |    40.2us |    37 |   245 |
| play, named roll     | 80x24  |    28.3us |    32.9us |    16 |   164 |
| play, fog            | 80x24  |    32.3us |    45.4us |    24 |   265 |
| play, fog, 4 watchers | 80x24  |    73.9us |    88.1us |    24 |   265 |
| play, fog by hand    | 80x24  |    32.2us |    40.3us |    35 |   231 |
| play, fog range, 4 watchers | 80x24  |   115.4us |   171.4us |    77 |   541 |
| play, fog sight      | 80x24  |    32.0us |    66.7us |    51 |   554 |
| play, fog lantern    | 80x24  |    30.4us |    55.2us |    39 |   430 |
| play, fog warren     | 80x24  |    31.1us |    68.7us |    65 |   815 |
| play, fog warren 12  | 80x24  |    30.7us |    71.8us |    63 |   793 |
| play, fog warren, party | 80x24  |    32.4us |    74.4us |   114 |  1665 |
| play, fog door       | 80x24  |    26.9us |    38.2us |    28 |   215 |
| play, fog full rebuild | 80x24  |    28.8us |    50.8us |    21 |   180 |
| play, fog reveal 12  | 80x24  |    31.5us |    66.9us |    48 |   533 |
| play, fog sight, 4 watchers | 80x24  |    85.6us |   169.3us |    51 |   554 |
| play, fog soft edge, 4 watchers | 80x24  |    79.8us |   117.9us |    38 |   412 |
| build, fog paint     | 80x24  |    29.8us |    41.2us |    24 |   219 |
| play, counters       | 80x24  |    30.6us |    64.1us |    54 |   402 |
| play, clocks         | 80x24  |    29.5us |    39.2us |    15 |   162 |
| play, 1 watcher      | 80x24  |    42.6us |    49.8us |    19 |   298 |
| play, 4 watchers     | 80x24  |    52.1us |    69.8us |    19 |   298 |
| play, 4 watchers, differing | 80x24  |    75.3us |   157.5us |    58 |   530 |
| play, carry, 4 watch | 80x24  |    45.0us |    62.9us |    33 |   205 |
| play, logging        | 80x24  |    26.6us |    59.1us |    31 |   198 |
| play, rolling        | 80x24  |    28.1us |    48.2us |    23 |   191 |
| help page            | 80x24  |    40.4us |    70.9us |   559 |  2714 |
| profiler overlay     | 80x24  |    37.4us |   159.6us |   154 |   928 |

> The machine's own baseline drifts: one recording of this table sat ~10% above its
> neighbours on every row, and none of it was the code -- the previous binary run
> through the same harness on the same day read identically (40.0us against 40.0us on
> `build, open`; 31.0us against 31.0us in a direct A/B). This is the reason the page
> says to read the ratios between rows rather than the absolute numbers, and the
> reason a suspicious row is worth an A/B before it is worth a commit message.

`cells` and `bytes` are what actually reached the terminal. They predict perceived
latency better than wall-clock does: a frame that recomputes everything but changes ten
cells still writes ten cells, because the renderer diffs against the previous frame and
emits only the difference. **A change that raises `bytes` is worse than one that raises
`frame p50`**, since the write is the part that leaves the process.

## Cost per call

What a path costs when it runs. Sorted by p99 rather than the median: a zone whose
guard turns it away still counts as a call, so a path that only does work sometimes has
a median near zero and a p99 that says what it costs when it does.

| path             | p50     | p99     | worst   | calls | heaviest scenario      |
|------------------|---------|---------|---------|-------|------------------------|
| app.draw         | 112.8us | 168.6us | 261.2us |  3200 | build, 200x200 200x50  |
| clock.draw       |   1.6us |   1.8us |  16.7us | 17748 | play, clocks 80x24     |
| counter.step     |   0.4us |   3.5us |   7.5us |  3200 | play, counters 80x24   |
| editor.draw      | 107.2us | 169.4us | 245.5us |  3200 | build, 200x200 200x50  |
| fog.blank        |   0.7us |   2.5us |  10.0us |  7280 | play, fog range, 4 watchers 80x24 |
| fog.paint        |   0.2us |   2.9us |  46.3us |  1600 | build, fog paint 80x24 |
| fog.reveal       |   3.6us |  11.4us |  91.0us |   365 | play, fog range, 4 watchers 80x24 |
| fog.sight        |  21.3us | 207.3us | 276.2us |  3599 | play, fog reveal 12 80x24 |
| fog.sight.build  |  20.4us | 144.2us | 210.0us |  3601 | play, fog warren 12 80x24 |
| fog.sight.rim    |   8.0us |  30.0us |  47.7us |   367 | play, fog range, 4 watchers 80x24 |
| fog.sight.union  |  12.6us | 199.6us | 267.6us |  3599 | play, fog reveal 12 80x24 |
| grid.draw        |  99.7us | 151.9us | 231.3us |  3200 | build, 200x200 200x50  |
| grid.labels      |   7.5us |  17.8us |  27.5us |  3200 | build, 200x200 200x50  |
| group.box        |   0.2us |   0.2us |   0.3us |   400 | play, fog warren, party 80x24 |
| group.move       |   0.7us |   4.1us |  54.2us |  1600 | play, fog warren, party 80x24 |
| input.key        |   0.1us | 415.7us | 5767.1us | 14400 | build, fill history 80x24 |
| log.write        |   1.8us |   5.7us |  10.1us |   253 | play, logging 80x24    |
| move.label       |   0.0us |   3.0us |  10.6us |  6795 | play, carry, 4 watch 80x24 |
| net.accept       |   8.2us |   8.5us |   8.5us |     4 | play, 4 watchers 80x24 |
| net.frame        |  13.5us |  55.0us | 330.2us |  6795 | play, carry, 4 watch 80x24 |
| net.players_frame |  46.6us | 111.5us | 246.2us |  8739 | play, fog range, 4 watchers 80x24 |
| note.marks       |   0.1us |   0.1us |  10.2us |  7997 | build, noted squares 80x24 |
| panel.draw       |   8.6us |  13.8us |  31.9us |  5595 | play, fight cycling 80x24 |
| play.draw        |  84.2us | 110.4us | 190.8us |  5595 | play, 24 tokens 200x50 |
| prof.overlay     |   8.8us | 126.1us | 171.6us |  1000 | profiler overlay 80x24 |
| range.draw       |  21.1us |  38.0us |  62.1us | 17480 | play, fog range, 4 watchers 80x24 |
| range.status     |   6.5us |  16.8us |  49.5us |  7280 | play, fog range, 4 watchers 80x24 |
| ruler.draw       |   0.7us |   1.0us |  11.3us |  4400 | ruler, three legs 80x24 |
| trail.draw       |   0.1us |   0.4us |   3.9us |  6795 | play, carry, 4 watch 80x24 |
| trail.path       |   4.3us |  17.9us |  90.1us |  6231 | play, carry 200x200 80x24 |
| turn.advance     |   1.0us |   5.9us |  66.5us |   800 | play, turn order 80x24 |
| turn.status      |   0.2us |   1.0us |  10.7us |  5595 | play, 1 watcher 80x24  |
| undo.step        | 217.2us | 266.1us | 397.3us |  1200 | build, fill+undo 200 80x24 |
| undo.trim        | 4202.5us | 5156.8us | 5156.8us |    97 | build, fill history 80x24 |

### Reading it

**The renderer stopped paying twice for the window.** Two changes to the flush, one
commit, measured together at 200×50: the delivered frame becomes the front buffer by a
pointer swap rather than a 160KB copy, and clean rows are skipped with one `memcmp`
each instead of a per-cell walk — a typical frame changes two or three rows of fifty.
**158µs → 132µs at 200×50, 36µs → 32µs at 80×24**, medians of three runs each way,
with every `cells` and `bytes` column identical. These were the only per-frame costs
that scaled with the window without being culled by it.

**`grid.draw` is the frame.** Roughly 85% of `app.draw` at every size, and it scales
with the window: it walks the visible tiles, resolving a junction glyph from a four-bit
incidence mask at every crossing. Walling every edge of the map costs nothing extra —
the mask is computed either way. A one-row cache now carries the vertical segments down
the way the horizontal ones were already carried across, two lookups per corner instead
of three: 100.0µs → 92.3µs at 200×50, medians of three runs.

**Everything layered on top is noise by comparison.** The ruler, the movement ribbon and
the range overlay are each under 2% of a frame. Only the range band reaches 25µs, and
only for a band with no upper bound, where it shades every visible square.

**Marking void costs nothing where there is no void, and about 8µs at 200×50 where the
screen is mostly void** — one dot per empty square. The alternative, tinting the floor
instead, was measured both ways and is the wrong trade in both directions: it costs 3µs
on a map that is all floor, where marking void is free, and it is invisible anyway,
because a background dark enough not to shout is one the eye cannot find. Lifting the
floor far enough to see would also have put rough and wood *underneath* it, which is a
palette rewrite rather than a tweak.

| 200×50 window | baseline | floor tinted | void marked |
|---|---|---|---|
| all floor | 77.6µs | 80.3µs | **77.9µs** |
| mostly void | 122.0µs | 125.3µs | 130.1µs |

**`grid.labels` at 7.7µs is the largest optional cost**, about 6% of a wide frame. It
was 11.4µs until the "widest visible label" question stopped formatting every visible
column per frame: bijective base-26 names lengthen monotonically, so the widest is
simply the rightmost column's. `#` takes the whole cost to zero and gives back the row
and the gutter as well.

**The distance label beside a carried creature costs 1µs**, and only in the frames where a
creature is actually being carried — 36.0µs against 35.0µs, medians of three runs with the
call in and out. Cells and bytes did not move at all: the distance was taken off the status
line at the same time, so the same characters change per frame, just somewhere more useful.

**Selecting several creatures is cheaper than selecting one**, which is not a typo:
`play, group box` writes 18 cells and 133 bytes against `play, 24 tokens`' 19 and 298.
The box holds still while it is being stretched, and a frame where only the box edge
and a ring or two change is a frame with almost nothing to write. Carrying the group
is 22 cells and 131 bytes -- the creatures move, so their rings move with them, but
three creatures walking abreast redraw barely more than one does, because the ribbon
and the label are drawn once for the group rather than once each.

**Undo is 20 bytes an op and bounded.** An op used to carry two whole tokens, 228 bytes,
for a tile paint that needs two. The token payloads now live in a side array the log
owns, and a full fill of the largest map (512×512, 262,144 ops) went from 60 MB to
5 MB of history. Taking one back is `undo.step`: 181µs for the 40,000-op fill of a
200×200 map, 4.5ns an op, all of it the tile writes. The log holds four such largest-map
fills and then drops its oldest quarter in one memmove, `undo.trim`: about 5ms, once
per 262,144 new ops, which is 19ns an op amortised and a thing that happens a handful
of times in a long session of painting. The scenario that measures it (`fill history`)
paints and clears 200×200 without undoing so the log grows 80,000 ops a loop.

**The range highlight costs the window when it covers the window.** `play, range radius`
holds a 100 ft reach over a 40×25 map, so every visible tile is in range and every one
of them gets a line-of-sight trace: `range.draw` at 21µs is that, and `range.status` at
5µs is the same trace once per creature for the 24 named in the status line. The band
scenario reads cheaper only because it spends most of its frames on the near bands; its
*Very Far* frames cost the same. The radius itself is free, and `:scale` cannot make it
dearer -- reach is compared in feet, tiles are still counted in tiles.

**A template costs less than the circle it is cut from.** Cone, line and square test the
same visible tiles the circle does and then turn most of them away, so they shade fewer
squares and trace fewer sight lines: 34-36µs a frame at six squares of reach against the
circle's 38µs for its bands. The geometry is worked out once per frame -- origin, aim
vector, the square's box -- and the per-tile test is a dot and a cross product. Aiming is
free: the template reads the cursor after each key rather than owning any keys of its own.
With the overlay off the whole path is one early return, which is why an A/B of `play, 24
tokens` against the binary from before templates reads 31.7µs on both.

**The turn order is never sorted, or stored.** It is a number on each creature, and every
question asked of it -- who is next, who is after that, where does `t` go from here -- is
"the smallest key greater than this one", which is one pass over the tokens with nothing
allocated. `turn.advance` is that pass plus two token edits and a round op through the undo
log: 0.9µs with 24 creatures in the fight. `turn.status` is the title bar's readout, the
same pass three times a frame: 0.5µs. A fight costs the frame nothing that shows --
`play, turn order` reads 31.4µs against 31.9µs for the same map with nobody fighting.

**The side panel is 24 columns of the frame, and costs like it.** `panel.draw` is 8.4µs at
200×50 and under 3µs at 80×24: a fill, a separator down every row and a dozen short
strings, redrawn each frame like everything else and diffed away by the renderer when
nothing changed (`play, 24 tokens` writes 315 bytes a frame with the panel, 319 without).
It takes its columns from the map view, so every play row on a Daggerheart map moved
when it landed: the bench's 24-token map is one, and under a spotlight ruleset the panel
is always up. At 80×24 the rows fell by a microsecond or two, the grid being 24 columns
narrower; at 200×50 `play, 24 tokens` rose from 102µs to 108µs, the panel's 8µs less the
grid it displaced. None of it is the turn order, which the paragraph above prices.

**The players' frame costs a copy and a second diff, or a second draw.** Since
the players' frame landed, what the clients receive is a second renderer's
diff. When nothing GM-only is on screen that renderer takes a copy of the
GM's back buffer -- 30 KB, one memcpy -- and is flushed against its own
front, which is one more row-memcmp sweep and the same observer walk the
GM's flush used to carry. `play, 1 watcher` moved from 35.7µs to 42.5µs
for it, A/B'd against the previous binary in the same run; `play, 4
watchers` did not move past noise, since the per-client writes dominate
there. When the two views could differ -- a prompt or a modal up, the
profiler on, a note in view -- the frame is drawn a second time instead:
`net.players_frame` is 27.4µs typical, and `play, 4 watchers, differing`
(a creature with a note selected, so every frame differs) reads 74.2µs
against 51.1µs. That is the price the plan in `docs/FOG.md` accepted for a
frame that is obviously correct at a privacy boundary.

A win is on the table and is recorded here rather than taken: in the
identical case the encoder could read the GM's own flush, as it did before,
and the players' front be kept in step by copying the GM's front after the
swap, which drops the second sweep and saves roughly 5µs a frame while
serving. The catch is the frame after a differing one, whose diff has to
be against what the clients hold rather than the GM's old front; it needs
one bit of state and a test for the transition. Worth doing when the
watcher rows matter more than they do now.

**A remote watcher costs one write.** `play, 1 watcher` is `play, 24 tokens` with a
loopback client attached: 36.5µs against 30.2µs. Four watchers are 45.2µs, so each is
about 3.7µs a frame, and `net.frame` -- encode once, then per client the palette check,
a copy into its buffer and a `write` -- is 13.6µs with four. The encode is shared and the
copy is a few hundred bytes, so nearly all of the 3.7µs is the syscall, which is the floor
for a design that sends every frame the moment it exists rather than batching. The plan
budgeted 2µs a client; it missed by the cost of a write on this kernel. With nothing
changed nothing is sent, and with no client attached the whole path is one compare per
changed cell. Bytes per frame to the clients, all of them together, are on the F12
overlay and the bench summary.

**The page, measured in Chrome on a 1280×768 frame.** A cursor move (about 40 cells)
paints in 0.2-0.5ms; a scroll of 1220 cells in 0.7-2ms, of which the pixel push is about
1ms. Three implementations of the copy loop were tried: JavaScript typed-array row copies
at 4-5µs a cell, `memory.copy` in WebAssembly at 40ns a row, and a WebAssembly loop of
64-bit words at 0.21µs a cell, which is the one shipped. A canvas `drawImage` sprite path
measured no better than the JavaScript copies. The tile cache -- a glyph in its colours
rendered once -- is what makes the copies the only per-cell work; the first frame after a
palette change or a resize pays for every tile again, about 15ms for a full screen.

`play, fight cycling` is dearer at 42µs and 4.7KB a frame, and none of that is the turn
order: it is the first scenario that presses `t` at all, and `t` sends the view to a
creature that is usually somewhere else, so most of the window repaints. Walking the
tokens in list order scrolls exactly as far.

**Clocks cost a panel row each, and only when the panel is drawn.** `clock.draw` is
1.6µs typical for two clocks: a fill of its rows, a name column, a dot per segment.
`play, clocks` runs two `:clock` and two `:tick` commands a loop and its frame is the
cost of typing them, 28.5µs against 30.2µs for the same map with no panel -- the panel
takes columns off the map, so the frame has fewer cells to diff. Ticks are one undo
op of the tile kind. **Notes cost nothing per frame in play mode**, by design: the
text is never drawn there. Build mode's `note.marks` walks the notes, not the tiles,
0.1µs for three. **A named roll is a string lookup**: `play, named roll` reads the
same as `play, rolling`.

**Fog costs the GM 1.5µs a frame and a map without it nothing.** `grid_draw` decides once a
frame whether any patch is live; a map with none takes the path it always took, and
`play, 24 tokens` sits where it did (31.0µs). With fog over the whole map, `play, fog` is
32.2µs: one byte load and a mask a visible tile, writing the dim colour into cells that were
being written anyway. The players' frame over fog is cheaper to draw than the GM's -- a hidden
tile is a `continue` with no writes, and walls between hidden tiles are never resolved -- so
`play, fog, 4 watchers` at 67.3µs is the second draw the plan priced in, less than the
differing-frame row pays for a note. `fog.paint` is 0.2µs a brush stroke, a handful of 20-byte
undo ops. `fog.reveal`, a `g r` or `g h`, is 6.1µs: a footprint of undo ops, or a
patch's extent for `g R`. `fog.blank`, the pass that takes the dark back out of a range wash or a trail in the
players' frame, runs only while one of those is showing over fog.

**Sight is a keystroke cost, not a frame cost, and follows the party's reach.** `fog.sight`
runs after a keystroke that changed the map, never per frame, so it is not in the frame
columns above: `play, fog sight` walks a creature through a map-wide patch at `reveal 6` and
its frame reads 31.7µs, level with the rest. A full recompute is about 28µs on that bench map, which has twelve player creatures each
reaching 169 squares; a step, which relights only the creature that moved, is 6.0µs.
It clears only where the creatures lit last time and tests each patch's extent before any
square.

It was 57.4µs as first written. The bench map has no walls, and every one of those squares
paid for a line walk that could only come back clear. A rectangle that holds a creature and
its reach, with no opaque boundary inside it, cannot block any line between them, so one pass
over its edges now stands in for a line walk a square; a room with walls in reach still walks
every line. Recomputing only the creatures that moved is now built (*Sight: the plan*,
below); shadowcasting was declined, since it cannot agree with the ruler square for square.
One more thing worth knowing: a keystroke that is not only moves still rebuilds everything
-- a counter, a clock, a note, the round, which cannot change sight. That is the price of one rule
instead of a list of call sites that could miss one, and a missed one would be a leak.

**Sight, scenario by scenario.** The zone table above keeps only each zone's worst
scenario, so `tools/sight.sh` measures `fog.sight` per fog scenario (median of three,
every recompute counted):

| scenario               | before | now          | calls |
|------------------------|--------|--------------|-------|
| fog sight              | 33.7us |        6.0us |  3599 |
| fog lantern            | 33.9us |        6.1us |  3723 |
| fog reveal 12          | 73.8us |       21.4us |  3599 |
| fog full rebuild       | 33.2us |       27.5us |  1201 |
| fog warren             | 51.8us |       13.8us |  3601 |
| fog warren 12          |     -- |       43.8us |  3601 |
| fog warren, party      | 54.9us |       30.6us |  2001 |
| fog door               | 47.6us |       42.6us |  1201 |

`fog warren` is the fixture the two sight wins in *Sight: the plan* below are for: 40x25
cut into rooms by walls with one gap each and a closed door, twelve player creatures,
`reveal 6`. The open map never walks a line -- `rect_opaque` passes every reach -- so
its 34µs is bookkeeping; the warren's 52µs is the same bookkeeping plus the lines that
walls make necessary. `fog reveal 12` is the same walk as `fog sight` with a reach four
times the area, and costs about twice as much. `fog full rebuild` changes a patch's
setting each keystroke, which no incremental scheme can skip; `fog door` toggles a door.
Each recompute here is one keystroke's; a frame never pays for it.

The "before" column is sight as first built; "now" is the build as committed, which
recomputes only the creatures that moved (below) and walks lines with the shared
`sight_walk` -- measured after step 3, with the mask taken out again. A step on the open
map is 6.0µs where it was 34, and 14µs in the warren where it was 52. The rows that must rebuild everything -- a setting changed, a door
toggled -- got cheaper too, 28 and 43 against 33 and 48, because the rebuild now skips the
line walks the old loop skipped and works distance out a row at a time. `warren, party`
moves three creatures a step through walls, and pays for three creatures' line walks: 31µs
against 55. Those walks are what step 3 is for. The p99 column is the full rebuilds, one
a loop (the script's `:fog all 6`), and for `reveal 12` the first step after one, which
settles the squares the rebuild left unasked.

`fog warren 12` came in with step 3, so it has no "before": it is the walled case
the line walks matter most in, a reach of 625 squares in a map of rooms.

**Cheaper line walks: measured and declined.** Step 3 read a creature's reach into an
opacity mask with prefix sums, so a square with no wall between it and the creature
skipped its walk and every other walk read bits instead of the map. Exact -- every line
pair on 200 random walled maps agreed -- and worth nothing, in its own back-to-back A/B
against step 2 (so these pairs differ a little from the table, a separate run): the warren 13.9 -> 13.8µs, the
walled reveal 12 44.6 -> 43.2 against a bar of half, the door row 42.9 -> 48.1, worse.
In rooms a blocked line stops at the first wall a step or two out, so the walks were
already short, and building the mask cost what it saved. What stayed is `sight_walk`,
the one loop fog, the ruler and the range now share, which measured level on every
ruler and range row. The mask would earn its keep at reveals far beyond a room's size.

**Sight: the plan.** Signed off 2026-09-24 (docs/FOG.md, *3d*): recompute only the
creatures that moved, proven by `Map.gen` rather than by a list of call sites, then --
only if the numbers still call for it -- make the line walks themselves cheaper, keeping
the ruler's rule exactly. Shadowcasting was considered and declined: it cannot reproduce
the ruler's line test square for square, and fog, ruler and range must agree. Every step
answers to `fogdiff`, a test that checks the fog bits against a brute force after every
keystroke of 12,000 random operations on random maps (36,000 with `VTT_FOGDIFF_OPS`).

**The soft edge costs nothing that can be measured.** It stores nothing new: the rim bit is
set by the recompute whether or not any patch shows it, and drawing asks one more question
only of a boundary the players' frame was about to leave blank -- a lookup of the two
squares' bits and the patch's setting -- and of a creature it was about to skip. `play, fog
soft edge, 4 watchers` walks the same creature as `play, fog sight, 4 watchers` with the
edge shown: 83.1µs against 85.2µs, the difference inside the noise. `fog.sight`'s p99 of
133µs comes from a watched scenario, which the table now has for the first time; the
recompute's code is unchanged by this step and its typical figure did not move, so the tail
is the four clients' encoding sharing the machine with it, not the edge.

**A counter step is a token edit.** `counter.step` is 0.4µs typical: find the
current counter, clamp, and one `undo_edit_token`, the same path a relabel takes.
`play, counters` sets HP through the prompt and then steps it eight times a loop,
and its frame reads 29.9µs, level with `play, 24 tokens`. The panel's counter is one
`snprintf` on the actor's row, inside `panel.draw`'s existing 8µs. Counters are the
GM's, so a selected creature carrying one makes the players' frame differ and draws it
twice while serving -- the cost `net.players_frame` already measures, paid only then.

**The session log costs a line, once per thing that happened.** `log.write` is a
`strftime`, an `fprintf` and an `fflush`: 2-3µs, and only on the keystroke that put a
creature down or rolled the dice, never per frame. The flush is deliberate -- the log is
for the crash as much as the recap -- and it is why the figure is microseconds rather than
nanoseconds; buffering would save nothing anyone could feel and lose the last line when it
mattered. With the log off the call is a null check. Dice are cheaper still: `play,
rolling` is two `:roll` commands a loop and reads as the cost of typing them.

**The recovery autosave is a whole-map write, off the keystroke path.** It writes the
map to `name.vtt.autosave` once the changes have been quiet for 1.5 seconds, never
while keys are arriving, and never twice for the same state: the map carries a
generation counter and the copy is owed only while it is behind. A full write,
`fsync` included, best of five:

| map | bytes | write |
|---|---|---|
| 40×25, 24 creatures | 3.9 KB | 0.05 ms |
| 200×200 | 122 KB | 0.6 ms |
| 512×512 | 790 KB | 4.1 ms |

Those are the costs of a save, and of a crash losing nothing. A snapshot was chosen
over replaying the undo log because the log does not see everything the map is: the
ruleset, the scale, the clocks and named rolls, a resize. Anything that goes through
`map_touch` is covered, which is everything. An idle `vtt` with nothing owed still
blocks in `poll` for ever; the only timer is the one write it is waiting to make.

`group.move` is the whole formation's move: the check for every member, then the
steps. **0.1us typical and 2.0us at p99** for the sizes a table plays at. It is
O(members x tokens x members), because each member asks the token list whether
anything is in its way and each of those asks walks the group to see if the answer is
one of its own. At the 32-creature cap on a crowded map that is tens of thousands of
comparisons on a keystroke -- still microseconds, and the same shape as `trail.path`
below. The fix, if it ever earns one, is the same occupancy grid.

`group.box` is the enumeration behind enter, y and d: **0.2us at p99** over a
24-creature map. The membership test that runs per token per frame while the box is
open is deliberately *not* instrumented. It was, briefly, and at one zone per token
per frame it fired 143,880 times in a perf run and cost more than the rectangle test
it was measuring -- the same objection this page makes to `prof.overlay`.

**The ring around a selected creature costs one cell and nine bytes a frame**, and
only in the frames where the selection is moving -- 33 cells against 32 on the carrying
scenario. A ring that sits still is a ring the diff never writes, which is why the cue
that fixed the contrast problem is also nearly the cheapest one available: it recolours
grid lines that were already on screen rather than painting anything new. A creature
standing still while selected costs nothing at all.

**Build mode's brush rides the same footprint cursor** and costs less than an idle
build frame -- 18 cells and 128 bytes on the `build, 3x3 brush` scenario -- because the
painting itself is keystroke work that was always O(area), and the tint barely moves
between frames. The corner marks the play cursor wears were deliberately left off it:
they measured at half again the bytes of an idle build frame, for a cursor that has no
tokens to be lost behind.

**A multi-tile cursor is the most expensive thing a keystroke can switch on**,
because it paints a block rather than a square. It is opt-in -- the cost arrives with
the size key, and the default 1x1 cursor is unchanged -- but it is the only path here
where choosing a setting more than doubles the bytes a frame writes.

| cursor footprint | frame p50 | cells | bytes |
|---|---|---|---|
| 1x1 | 25.1us | 20 | 326 |
| 2x2 | 26.6us | 32 | 564 |
| 3x3 | 27.4us | 41 | 672 |

Moving a 3x3 cursor one square changes four pitch-columns of an eleven-by-five block
twice over, once leaving and once arriving, which is where the cells go. There is a
win available and it is not worth taking yet: the block is tinted whole and then the
creature is drawn over the middle of it, so on a carried token most of those cells are
painted twice. Tinting only the ring the token does not cover would save perhaps a
third of them, at the cost of a second shape to keep in step with `grid_token_area`.
At 672 bytes -- a quarter of what the help page writes -- the simpler code is worth
more than the bytes.

**Choosing which creature to pick up is the cheapest thing in the table** -- 13 cells
and 101 bytes a frame, less than half what an idle build frame writes -- for the reason
that makes it worth having: the cursor holds still while enter walks the creatures it
covers, so between one press and the next almost nothing on screen changes. Only the
selection highlight moves, and that is two token bodies. The walk itself is a scan of
the token list per press, on a keystroke rather than a frame, and does not register.

**The route search costs the route now, not the map.** It used to malloc, zero and
free two map-sized arrays per keystroke, and ask the token list about every square it
probed — O(map × tokens) however short the walk. Three changes took that apart: the
scratch is kept across searches, the search restores it by touching only what it
touched (every reached cell is in its own queue), and the blockers are painted into an
occupancy grid once per search so a probe reads cells instead of scanning tokens.
Measured on a 200×200 carry, 100 bench loops each way: **32.4µs → 1.4µs p50, 76.9µs →
5.7µs p99, per keystroke** — and the median carried keystroke (`input.key`) went from
25.9µs to 0.8µs. The `play, carry 200x200` scenario keeps it measured.

**`trail.path` is the only search in the app** — a breadth-first sweep from the held
creature back to where it set out, and after the rework above its cost follows the
route: about 2µs beside a short walk on any map size, with the long walks in the p99.
It runs on a keystroke rather than a frame, so even its worst has hundredfold headroom.

**`prof.overlay` is the most expensive thing here relative to its worth**, which is why
it reports itself: an instrument that quietly adds to the number it displays is worse
than no instrument. It is off unless you press F12.

**`input.key`'s worst is a carried step**, not a cursor move: the route is recut
underneath it, so it is `trail.path`'s worst wearing a different name. A bare cursor
keystroke is 0.2µs.

## Working rules

1. **A new drawing path gets a scenario in `tools/perf.sh`.** A path with no row in
   these tables is a path nobody is watching.
2. **Regenerate after anything that touches drawing**, and put the new numbers in the
   commit if a row moved by more than noise (about ±10% here).
3. **Cull to the window before doing work.** Every path that could scale with the map
   calls `grid_visible_tiles` first. This is the single rule that keeps a 512×512 map as
   cheap as a small one.
4. **Prefer not drawing to drawing quickly.** The diff means an unchanged cell costs
   nothing to leave alone, so a guard that skips work beats an optimisation that does it
   faster.
5. **When a measurement suggests a win, plan it rather than taking it silently.** Say
   what it costs now, what it would cost, and what the change buys — some of these paths
   are worth leaving slow and obvious.

## Tools

| | |
|---|---|
| `make perf` | regenerates every table on this page |
| `tools/median.py a b c` | the per-row median of several `make perf` outputs, which is what is published |
| `make bench` | one scenario, quick |
| `F12` | live overlay: per-zone p50/p99, a frame-time sparkline, cells and bytes |
| `--trace out.json` | Chrome Tracing profile, every occurrence of every zone; open in perfetto |
| `--bench-loops N` | more repetitions when a number looks noisy |
| `make fuzz` | not a timing tool: libFuzzer on the map loader, the one untrusted input |
| `tools/sight.sh [bin]` | `fog.sight` per fog scenario, which the zone table cannot show |
| `--bench-clients N` | attaches N loopback watchers to a bench run, so a row can carry the remote view's cost |

The overlay and the trace share the zone table, so anything wrapped in `PROF_ZONE`
appears in all three without further work. `-DVTT_PROF=0` compiles the instrumentation
out entirely.
