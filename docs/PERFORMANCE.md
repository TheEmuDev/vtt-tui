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
| build, open          | 80x24  |    40.0us |    49.3us |    10 |   207 |
| build, every edge    | 80x24  |    39.5us |    77.9us |    10 |   207 |
| build, 200x200       | 80x24  |    39.6us |    59.4us |    12 |   209 |
| build, 200x200       | 200x50 |   170.4us |   196.2us |    12 |   212 |
| build, mostly void   | 200x50 |   156.1us |   324.2us |    20 |   267 |
| build, tracing       | 80x24  |    39.1us |    52.9us |     2 |   140 |
| build, circle brush  | 80x24  |    41.2us |    84.5us |    28 |   292 |
| ruler, three legs    | 80x24  |    36.3us |    46.5us |    11 |    40 |
| play, 24 tokens      | 80x24  |    43.0us |    52.9us |    19 |   298 |
| play, 24 tokens      | 200x50 |   149.2us |   163.5us |    36 |   319 |
| play, carrying       | 80x24  |    42.3us |    88.8us |    33 |   205 |
| play, 3x3 cursor     | 80x24  |    45.6us |   103.4us |    40 |   663 |
| play, choosing       | 80x24  |    40.2us |    66.5us |    13 |   101 |
| play, group box      | 80x24  |    38.9us |    51.0us |    18 |   133 |
| play, group carry    | 80x24  |    44.4us |    88.7us |    22 |   131 |
| play, range bands    | 80x24  |    48.4us |   102.9us |   163 |   845 |
| help page            | 80x24  |    45.4us |   147.7us |   532 |  2656 |
| profiler overlay     | 80x24  |    45.6us |   160.4us |   118 |   759 |

> Every row in this table is about 10% slower than the one recorded before it, and
> none of it is the code. The previous binary run through the same harness on the same
> day reads 40.0us for `build, open` against the current binary's 40.0us, and a direct
> A/B on one fixture reads 31.0us against 31.0us. The machine's own baseline moved.
> This is the reason the page says to read the ratios between rows rather than the
> absolute numbers, and the reason a suspicious row is worth an A/B before it is worth
> a commit message.

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
| app.draw         | 125.9us | 264.4us | 332.5us |  3200 | build, 200x200 200x50  |
| editor.draw      | 119.8us | 253.7us | 320.3us |  3200 | build, 200x200 200x50  |
| grid.draw        | 107.0us | 223.6us | 301.5us |  3200 | build, 200x200 200x50  |
| grid.labels      |  12.7us |  29.7us |  41.0us |  3200 | build, mostly void 200x50 |
| group.box        |   0.1us |   0.2us |   7.2us |   400 | play, group carry 80x24 |
| group.move       |   0.1us |   2.0us | 142.8us |  3200 | play, carrying 80x24   |
| input.key        |   0.2us |  16.8us | 148.1us |  6800 | play, carrying 80x24   |
| move.label       |   0.0us |   1.9us |   6.7us |  6795 | play, carrying 80x24   |
| play.draw        |  98.3us | 218.8us | 450.2us |  5595 | play, 24 tokens 200x50 |
| prof.overlay     |   8.5us | 126.6us | 156.0us |  1000 | profiler overlay 80x24 |
| range.draw       |   0.0us |  28.5us |  45.1us |  5195 | play, range bands 80x24 |
| ruler.draw       |   0.8us |   1.9us |   5.5us |  4400 | ruler, three legs 80x24 |
| trail.draw       |   0.1us |   0.3us |   0.9us |  6795 | play, carrying 80x24   |
| trail.path       |   1.9us |  24.7us |  37.6us |   986 | play, carrying 80x24   |

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

**Selecting several creatures is cheaper than selecting one**, which is not a typo:
`play, group box` writes 18 cells and 133 bytes against `play, 24 tokens`' 19 and 298.
The box holds still while it is being stretched, and a frame where only the box edge
and a ring or two change is a frame with almost nothing to write. Carrying the group
is 22 cells and 131 bytes -- the creatures move, so their rings move with them, but
three creatures walking abreast redraw barely more than one does, because the ribbon
and the label are drawn once for the group rather than once each.

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
creature back to where it set out. About 2µs on a 40×25 map; 32µs typical on 200×200
(129µs worst), and it runs on a keystroke rather than a frame, so even that has a
hundredfold headroom. Three planned wins live here, none yet taken: keep the two
map-sized scratch arrays in `Play` instead of malloc/free per keystroke, stamp `dist`
with a generation counter instead of an O(map) clear, and share the occupancy grid the
`group.move` note above already names, so a probe stops scanning the token list.
Together they make the search cost the route rather than the map.

**`prof.overlay` is the most expensive thing here relative to its worth**, which is why
it reports itself: an instrument that quietly adds to the number it displays is worse
than no instrument. It is off unless you press F12.

**`input.key`'s worst is a carried step**, not a cursor move: the route is recut
underneath it, so it is `trail.path`'s worst wearing a different name. The median
keystroke is 0.2µs. On a 200×200 map a carried step measures 32µs p50 and 129µs worst
(spot measurement, 100 bench loops) — the search's scratch is allocated, zeroed and
freed per keystroke and every probe scans the token list, which is the planned fix
recorded above.

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
