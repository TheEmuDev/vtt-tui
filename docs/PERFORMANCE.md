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
| build, open          | 80x24  |    36.4us |    40.1us |    10 |   207 |
| build, every edge    | 80x24  |    39.2us |    49.0us |    10 |   207 |
| build, 200x200       | 80x24  |    35.7us |    47.7us |    12 |   209 |
| build, 200x200       | 200x50 |   153.9us |   192.6us |    12 |   212 |
| build, mostly void   | 200x50 |   140.9us |   304.4us |    20 |   267 |
| build, tracing       | 80x24  |    57.8us |    73.7us |     2 |   140 |
| build, circle brush  | 80x24  |    38.1us |    75.6us |    28 |   292 |
| ruler, three legs    | 80x24  |    33.1us |    76.6us |    11 |    40 |
| play, 24 tokens      | 80x24  |    38.8us |    76.8us |    19 |   298 |
| play, 24 tokens      | 200x50 |   136.0us |   300.9us |    36 |   319 |
| play, carrying       | 80x24  |    35.2us |    78.1us |    31 |   192 |
| play, range bands    | 80x24  |    47.0us |    90.5us |   163 |   842 |
| help page            | 80x24  |    39.6us |    70.2us |   532 |  2656 |
| profiler overlay     | 80x24  |    40.6us |   136.8us |   118 |   761 |

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
| app.draw         | 113.7us | 247.2us | 290.3us |  3200 | build, 200x200 200x50  |
| editor.draw      | 108.5us | 237.2us | 279.6us |  3200 | build, 200x200 200x50  |
| grid.draw        |  97.0us | 210.1us | 253.0us |  3200 | build, 200x200 200x50  |
| grid.labels      |  11.3us |  26.9us |  44.1us |  3200 | build, 200x200 200x50  |
| input.key        |   0.1us |  10.2us |  72.5us |  6800 | play, carrying 80x24   |
| play.draw        |  90.0us | 206.2us | 374.8us |  5595 | play, 24 tokens 200x50 |
| prof.overlay     |   7.0us | 110.6us | 130.2us |  1000 | profiler overlay 80x24 |
| range.draw       |   0.0us |  27.6us |  58.7us |  5195 | play, range bands 80x24 |
| ruler.draw       |   0.7us |   1.6us |  10.2us |  4400 | ruler, three legs 80x24 |
| trail.draw       |   0.1us |   0.2us |   6.1us |  6795 | play, carrying 80x24   |
| trail.path       |   1.3us |  13.6us |  25.8us |  1009 | play, carrying 80x24   |

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
