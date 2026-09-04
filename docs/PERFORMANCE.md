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
400 times.

| scenario             | size   | frame p50 | frame p99 | cells | bytes |
|----------------------|--------|-----------|-----------|-------|-------|
| build, open          | 80x24  |    36.2us |    51.1us |    10 |   207 |
| build, every edge    | 80x24  |    36.1us |    73.4us |    10 |   207 |
| build, 200x200       | 80x24  |    35.8us |    81.4us |    12 |   209 |
| build, 200x200       | 200x50 |   154.5us |   197.5us |    12 |   212 |
| build, mostly void   | 200x50 |   140.2us |   167.2us |    20 |   267 |
| build, tracing       | 80x24  |    35.3us |    60.9us |     2 |   140 |
| build, circle brush  | 80x24  |    36.9us |    40.6us |    28 |   292 |
| ruler, three legs    | 80x24  |    32.7us |    41.7us |    11 |    40 |
| play, 24 tokens      | 80x24  |    38.6us |    81.1us |    19 |   298 |
| play, 24 tokens      | 200x50 |   134.0us |   214.9us |    36 |   319 |
| play, carrying       | 80x24  |    35.0us |    75.6us |    32 |   195 |
| play, 3x3 cursor     | 80x24  |    45.6us |    82.9us |    40 |   663 |
| play, choosing       | 80x24  |    36.1us |    42.0us |    13 |   101 |
| play, range bands    | 80x24  |    43.4us |    84.3us |   163 |   842 |
| help page            | 80x24  |    39.2us |    70.9us |   532 |  2656 |
| profiler overlay     | 80x24  |    40.7us |   137.4us |   118 |   759 |

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
| app.draw         | 114.1us | 237.1us | 280.1us |  3200 | build, 200x200 200x50  |
| editor.draw      | 108.8us | 227.7us | 270.1us |  3200 | build, 200x200 200x50  |
| grid.draw        |  97.4us | 200.9us | 243.2us |  3200 | build, 200x200 200x50  |
| grid.labels      |  11.3us |  27.2us |  43.9us |  3200 | build, 200x200 200x50  |
| input.key        |   0.1us |   9.4us |  73.0us |  6800 | play, carrying 80x24   |
| move.label       |   0.0us |   1.0us |  25.6us |  6795 | play, carrying 80x24   |
| play.draw        |  88.9us | 201.1us | 239.8us |  5595 | play, 24 tokens 200x50 |
| prof.overlay     |   7.1us | 102.8us | 134.8us |  1000 | profiler overlay 80x24 |
| range.draw       |   0.0us |  25.7us |  44.9us |  5195 | play, range bands 80x24 |
| ruler.draw       |   0.7us |   1.9us |  13.6us |  4400 | ruler, three legs 80x24 |
| trail.draw       |   0.1us |   0.2us |   0.5us |  6795 | play, carrying 80x24   |
| trail.path       |   1.1us |  13.3us |  27.5us |   986 | play, carrying 80x24   |

### Reading it

**`grid.draw` is the frame.** Roughly 85% of `app.draw` at every size, and it scales
with the window: it walks the visible tiles, resolving a junction glyph from a four-bit
incidence mask at every crossing. Walling every edge of the map costs nothing extra
(36.4µs against 37.3µs open) — the mask is computed either way.

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

**`grid.labels` at 11.4µs is the largest optional cost**, about 7% of a wide frame. It
formats a column name per visible column and a row number per visible row. `#` takes it
to zero and gives back the row and the gutter as well.

**The distance label beside a carried creature costs 1µs**, and only in the frames where a
creature is actually being carried — 36.0µs against 35.0µs, medians of three runs with the
call in and out. Cells and bytes did not move at all: the distance was taken off the status
line at the same time, so the same characters change per frame, just somewhere more useful.

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

**`trail.path` doubled when creatures started blocking each other** — 0.6µs to 1.2µs
typical, 5µs to 13µs worst — because the search now asks whether each square it probes is
occupied, and that ask is a walk of the token list. The shape is O(area x tokens), so it
grows with both a long route and a crowded map. At 13µs on a keystroke it is not worth
fixing yet; the fix, when it is, is an occupancy grid built once per search rather than a
scan per probe.

**`trail.path` is the only search in the app** — a breadth-first sweep from the held
creature back to where it set out. 1.2µs on a 40×25 map; the worst is a long walk
across 200×200, and it runs on a keystroke rather than a frame, so it has a thousandfold
more headroom than the table suggests.

**`prof.overlay` is the most expensive thing here relative to its worth**, which is why
it reports itself: an instrument that quietly adds to the number it displays is worse
than no instrument. It is off unless you press F12.

**`input.key`'s 73µs worst** is a keystroke that opened a prompt or ran a search, not a
cursor move. The median keystroke is 0.1µs.

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
| `make bench` | one scenario, quick |
| `F12` | live overlay: per-zone p50/p99, a frame-time sparkline, cells and bytes |
| `--trace out.json` | Chrome Tracing profile, every occurrence of every zone; open in perfetto |
| `--bench-loops N` | more repetitions when a number looks noisy |

The overlay and the trace share the zone table, so anything wrapped in `PROF_ZONE`
appears in all three without further work. `-DVTT_PROF=0` compiles the instrumentation
out entirely.
