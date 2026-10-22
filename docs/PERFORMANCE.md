# Performance

Every path that costs anything, measured. `make perf` regenerates the tables below;
they are its output, not estimates, so keeping them true is a command rather than a
promise.

**Which machine.** The user measures on two:

| | CPU | disk |
|---|---|---|
| **laptop** | Intel Core i7-4510U @ 2.00GHz (2 cores, 4 threads) | btrfs on an SSD |
| **desktop** | Intel Core i7-8700K @ 3.70GHz (6 cores, 12 threads, 4.7 GHz max), 16 GB RAM | btrfs on a Samsung SSD 860 EVO 1TB |

Both use gcc 16.2.1 with `-O2`. Absolute numbers belong to the machine they were taken on.
Only rows from the same machine compare, and the ratios between rows are the part that
carries over. Every table here before 2026-10-07 is the **laptop's**. On 2026-10-08 *Frame
times* and *Cost per call* were regenerated on the **desktop**, so the figures quoted in the
paragraphs under them, written earlier, were the laptop's. On 2026-10-13 every figure there
that sits beside a named row was brought to the desktop's tables (*Reading it* says how to
read one); the before-and-after pairs, the A/Bs and the dated figures are still the
laptop's: their ratios hold, their absolute values do not match the tables. The save table, the phone page's and the
fog sight figures are still the laptop's. From then on, each
table is published under the `Machine:` line that `tools/machine.sh` prints:
- `perf.sh`, `saves.sh`, `pagebench.sh` and `sight.sh` print it above their tables;
- `median.py` keeps it, and stops when its runs came from different machines.

A table regenerated on the other machine replaces the whole table, never single rows.

**Where the maps are.** `make perf` works in a directory inside the repo, ignored by git
(`PERF_DIR` moves it), on the disk a GM's maps are on, because a save's cost is the flush to that disk:
on tmpfs a flush is free and every saving row would read thirty times too cheap. The
figures here are btrfs on an SSD.

**Known gaps (health check, 2026-09-28; docs/HEALTH.md §5).** The frame columns leave out
key handling (a picker opening, a trip, an undo trim never shows in them), and bytes sent
to the phones are not published. A zone's call count marked `+` is a floor: that
scenario filled the trace (200,000 events), though every zone is still sampled, since each
loop runs the whole script. The help page is 14% dearer than before (41 to 46µs, A/B'd side by side; the table's run
read it higher):
a key wider than its column is measured on every line to give it one of its own.

## The budget

A keystroke has about **16ms** before a person notices. Nothing here is close, and the
point of writing the numbers down is to keep it that way: a change that moves a row by
a factor is worth a conversation before it lands, and a change that adds a new row is
worth measuring before it is called done.

Two properties matter more than any single figure:

- **Cost follows the window, not the map.** A 200×200 map costs the same at 80×24 as a
  40×25 one does. Every drawing path culls to the visible tiles first, creatures included:
  500 on a 200×200 map draw in 27µs against 22µs for 24 (on the laptop they drew in 85µs
  before the creatures were culled and 41 after, A/B'd; the 5µs left is a handful of walks of the whole list --
  `floor.pick`, `turn.status`, the panel, the cursor's size -- each a microsecond or two).
- **Idle costs nothing.** `poll()` blocks until there is input; there is no frame loop.

## Speed of light

Every change plan estimates the least its work could cost: the bytes it must touch, and the
syscalls and flushes it can't avoid, priced with the figures below. Once built, it is
measured against that estimate. A gap with no explanation is waste. These are the reference
figures, measured 2026-10-07 (best of many runs, `-O2`, the repo's disk) on:

Machine: Intel Core i7-8700K @ 3.70GHz (6 cores, 12 threads, 4.7 GHz max, powersave governor), 16 GB RAM, Samsung SSD 860 EVO 1TB (btrfs), Linux 7.2.5-3-omarchy, gcc 16.2.1 -O2

That is the **desktop**. The laptop's figures are still to be taken. Compare a row only with
the speed of light of the machine it was measured on.

| operation | cost | so |
|---|---|---|
| copy, in cache (1 MB) | 28 µs (37 GB/s) | a 512×512 map's four layers (1.05 MB: tiles, both boundary arrays, fog) copy in about 30 µs |
| copy, from memory (256 MB) | 11.9 GB/s | a working set past the cache costs three times as much per byte |
| copy + compare, in cache (1 MB) | 59 µs | comparing a whole map with a copy of it |
| `stat` | 0.9 µs | |
| Unix socket round trip | 3.1 µs | one control-channel exchange, before any work |
| `write` to a pty, raw mode, reader draining | 1.0 µs (1 KB), 12.2 µs (16 KB) | the terminal's floor for a frame's bytes: about 0.75 ns a byte past the first KB, so bytes written are the frame's cost |
| `write` to loopback TCP, `TCP_NODELAY` | 3.8 µs (1 KB), 4.4 µs (16 KB) | the phones' floor per push: the syscall, nearly flat in size up to 16 KB |
| `write` 789 KB into the page cache | 0.16 ms | a 512×512 save with nothing but the bytes |
| a save made durable: write, `fsync`, `rename` | 2.0 ms (4 KB), 2.2 ms (789 KB) | what a flushed save cannot go under. The disk sets it: the laptop's `:w` table runs fifteen times this |
| the same with the directory flushed too | 3.4-4.4 ms | |

**Known gaps** (each to close, or to keep with its reason):

- **The map writer built its text one `fputc` at a time: fixed 2026-10-10** (step 7 of
  docs/CONFLICTS.md, which needed the file's text in memory anyway). A row is built whole
  from a table of characters and the file is written once: an unflushed 512×512 save went
  from 3.26 ms to 1.18 ms on the desktop, a flushed one from 5.70 to 3.33.
- **What is left of it:** 1.18 ms against about 0.5 (0.16 for the bytes, about 0.3 to turn
  786,000 squares into characters). The rest is the text growing in a memory stream
  (`open_memstream`: a reallocation and a copy each doubling) and a locked `fwrite` a row.
  Sizing one buffer from the map and filling it would close most of it. Not yet planned
  (docs/HEALTH.md, *Left open by reviews*, B9).
- **The loader turns a file character into a square by searching a table** (`tile_from_file_char`,
  `edge_from_file_char`: a loop a character), and reads a line at a time: 2.7 ms for a
  787 KB map, about 3.4 ns a byte, where a table lookup would be near 1. It is what an
  outside change costs twice (below). Not yet planned (HEALTH.md, B10).

## Frame times

What the app costs to use. Each scenario replays a keystroke script, a frame per key,
400 times. Every time is the median of three such runs, row by row
(`tools/median.py`): a single run reliably has one row spiking somewhere, and never the
same one twice. The `peak KB` column (the run's `ru_maxrss`, from 2026-10-20) is the
largest of the three, not the median: a peak does not jitter as a time does (the user's rule). The runs want the machine to themselves -- editing a file while one was
in flight was enough to lift the play rows by a fifth, and an A/B against the previous
binary is what showed it was the machine and not the code.

Machine: Intel Core i7-8700K @ 3.70GHz (6 cores, 12 threads, 4.7 GHz max, powersave governor), 16 GB RAM, Samsung SSD 860 EVO 1TB (btrfs), Linux 7.2.8-9-omarchy, gcc 16.2.1 -O2

| scenario             | size   | frame p50 | frame p99 | cells | bytes | peak KB |
|----------------------|--------|-----------|-----------|-------|-------|---------|
| build, open          | 80x24  |    23.0us |    25.7us |    10 |   207 |    4440 |
| build, every edge    | 80x24  |    22.1us |    35.8us |    10 |   207 |    4324 |
| build, 200x200       | 80x24  |    22.8us |    25.1us |    12 |   209 |    4664 |
| build, 200x200       | 200x50 |    88.2us |   119.0us |    12 |   212 |    4924 |
| build, mostly void   | 200x50 |    76.9us |   141.2us |    20 |   267 |    4960 |
| build, noted squares | 80x24  |    22.3us |    38.8us |    88 |   524 |    6740 |
| build, tracing       | 80x24  |    21.9us |    24.0us |     2 |   140 |    4424 |
| build, circle brush  | 80x24  |    22.8us |    26.5us |    28 |   292 |    6440 |
| build, 3x3 brush     | 80x24  |    19.6us |    23.0us |    18 |   128 |    6604 |
| build, fill+undo 200 | 80x24  |    19.9us |    64.6us |   303 |  3398 |    7648 |
| build, fill history  | 80x24  |    20.7us |    76.3us |   335 |  4145 |   27952 |
| build, :ask starts an agent | 80x24  |    22.9us |    74.4us |    15 |   158 |    8672 |
| build, a job's tint  | 80x24  |    22.5us |    33.9us |    50 |   302 |    8828 |
| ruler, three legs    | 80x24  |    19.4us |    23.0us |    11 |    40 |    6568 |
| menu, options        | 80x24  |     9.9us |    11.2us |   127 |   274 |    4192 |
| play, 24 tokens      | 80x24  |    22.4us |    25.4us |    19 |   298 |    6552 |
| play, 24 tokens      | 200x50 |    78.7us |   110.7us |    36 |   315 |    6876 |
| play, carrying       | 80x24  |    19.3us |    27.4us |    33 |   205 |    6584 |
| play, carry 200x200  | 80x24  |    18.9us |    29.0us |    22 |   136 |    9116 |
| play, 24 on 200x200  | 80x24  |    21.2us |    23.7us |    21 |   304 |    6760 |
| play, 500 on 200x200 | 80x24  |    27.4us |    48.9us |    20 |   299 |    7064 |
| play, 3x3 cursor     | 80x24  |    24.2us |    27.1us |    37 |   606 |    6580 |
| play, choosing       | 80x24  |    19.7us |    22.3us |    12 |    95 |    6568 |
| play, group box      | 80x24  |    18.4us |    21.2us |    18 |   133 |    6536 |
| play, group carry    | 80x24  |    18.2us |    21.6us |    22 |   131 |    6496 |
| play, range bands    | 80x24  |    24.5us |    46.9us |   129 |   657 |    6496 |
| play, range radius   | 80x24  |    42.4us |    58.0us |   169 |   958 |    6580 |
| play, range cone     | 80x24  |    25.3us |    60.5us |    60 |   654 |    6456 |
| play, range line     | 80x24  |    24.9us |    57.2us |    55 |   620 |    6548 |
| play, range square   | 80x24  |    24.6us |    56.6us |    63 |   669 |    6552 |
| play, turn order     | 80x24  |    24.2us |    33.5us |    49 |   488 |    7328 |
| play, fight cycling  | 80x24  |    32.2us |    80.0us |   317 |  4596 |    6472 |
| play, spotlight      | 80x24  |    20.7us |    24.1us |    37 |   245 |    6508 |
| play, named roll     | 80x24  |    19.5us |    34.8us |    17 |   165 |    8628 |
| play, fog            | 80x24  |    22.1us |    36.7us |    24 |   265 |    6568 |
| play, fog all dark, 4 watch | 80x24  |    41.6us |    82.7us |    24 |   265 |    8780 |
| play, fog by hand    | 80x24  |    22.7us |    26.8us |    35 |   231 |    8644 |
| play, fog range, 4 watchers | 80x24  |    71.9us |   122.9us |    77 |   541 |    8836 |
| play, fog sight      | 80x24  |    21.9us |    47.1us |    58 |   565 |    8696 |
| play, fog lantern    | 80x24  |    21.0us |    49.1us |    43 |   438 |    8580 |
| play, fog warren     | 80x24  |    22.4us |    50.0us |    72 |   826 |    8712 |
| play, fog warren 12  | 80x24  |    22.0us |    49.5us |    69 |   803 |    8732 |
| play, fog warren, party | 80x24  |    23.0us |    51.3us |   118 |  1673 |    8696 |
| play, fog door       | 80x24  |    19.7us |    32.6us |    28 |   215 |    8652 |
| play, fog full rebuild | 80x24  |    20.2us |    25.6us |    21 |   180 |    8652 |
| play, fog reveal 12  | 80x24  |    21.6us |    47.2us |    55 |   544 |    8736 |
| play, fog sight, 4 watchers | 80x24  |    45.4us |   120.6us |    58 |   565 |    8896 |
| play, fog soft edge, 4 watchers | 80x24  |    44.8us |    82.7us |    42 |   420 |    8808 |
| build, stamp 20x20   | 80x24  |    16.6us |    35.7us |   146 |  1696 |    6760 |
| build, fog paint     | 80x24  |    21.7us |    24.8us |    24 |   219 |    6840 |
| build, 64 links      | 80x24  |    26.3us |    30.8us |    35 |   274 |    6520 |
| play, 64 links       | 80x24  |    26.5us |    41.6us |    47 |   348 |    6544 |
| play, link there+back | 80x24  |    24.6us |    35.8us |    78 |   637 |    6584 |
| build, one floor     | 80x24  |    21.4us |    24.0us |    12 |   176 |    6576 |
| play, other floor, 4 watchers | 80x24  |    40.3us |    57.8us |    18 |   238 |    8828 |
| play, floor steps    | 80x24  |    21.4us |    36.2us |    40 |   290 |    6528 |
| play, counters       | 80x24  |    21.3us |    46.1us |    54 |   402 |   10228 |
| play, clocks         | 80x24  |    20.5us |    33.1us |    15 |   162 |    8608 |
| play, 1 watcher      | 80x24  |    30.9us |    45.8us |    19 |   298 |    6748 |
| play, 4 watchers     | 80x24  |    38.5us |    43.8us |    19 |   298 |    6676 |
| play, a phone that falls behind | 80x24  |    38.9us |    52.4us |    19 |   298 |    8868 |
| play, 4 watchers, differing | 80x24  |    52.6us |   109.5us |    58 |   530 |    8988 |
| play, hidden, 4 watchers | 80x24  |    55.4us |    85.0us |    19 |   298 |    8736 |
| play, camera party, 4 watchers | 80x24  |    45.6us |    86.4us |    49 |   463 |    8828 |
| play, camera hold, 4 watchers | 80x24  |    39.9us |    58.0us |    16 |   212 |    8664 |
| play, whisper, 4 named | 80x24  |    39.1us |    55.9us |    13 |   147 |    8820 |
| play, pings, 4 watchers | 80x24  |    48.1us |    79.4us |    98 |   978 |    8640 |
| play, fog pings, 4 watchers | 80x24  |    61.4us |    70.1us |    72 |   696 |    8756 |
| play, GM ping        | 80x24  |    22.3us |    25.4us |    35 |   345 |    6576 |
| play, carry, 4 watch | 80x24  |    37.2us |    49.7us |    33 |   205 |    8840 |
| play, logging        | 80x24  |    18.6us |    25.9us |    31 |   198 |    8584 |
| play, rolling        | 80x24  |    20.0us |    24.1us |    23 |   191 |    8576 |
| play, group effect   | 80x24  |    26.4us |    42.5us |    44 |   476 |    6580 |
| play, card box       | 80x24  |    32.0us |    60.7us |    56 |   620 |    6488 |
| play, damage         | 80x24  |    32.0us |    37.1us |    33 |   198 |    6560 |
| play, stress to zero and back | 80x24  |    22.4us |    34.8us |    59 |   376 |    9060 |
| play, 500 characters | 80x24  |    28.3us |    44.0us |   110 |   628 |    8692 |
| play, env adversaries | 80x24  |    19.3us |    34.5us |    51 |   340 |    8548 |
| play, summon         | 80x24  |    18.5us |    38.9us |    36 |   257 |    8644 |
| play, scene of 500   | 80x24  |    27.9us |    56.5us |    22 |   182 |    9232 |
| play, map trip there+back | 80x24  |    22.3us |    34.4us |    45 |   306 |    6516 |
| play, handout typed, 4 watch | 80x24  |    39.2us |    57.4us |    12 |   142 |    8780 |
| agent, room + 12     | 80x24  |    96.8us |   129.0us |    55 |   157 |    5052 |
| agent, plan of rooms | 80x24  |    72.6us |    77.0us |    55 |   157 |    4412 |
| agent, dump 512x512  | 80x24  |    22.7us |  5650.0us |     7 |   138 |    7188 |
| agent, dump a region | 80x24  |    23.0us |    52.8us |     7 |   138 |    5704 |
| agent, describe 512x512 | 80x24  |    22.5us |  3480.0us |     7 |   138 |    7224 |
| agent, check 512x512 | 80x24  |    22.9us |  6080.0us |     7 |   138 |    7244 |
| agent, check 500 creatures | 80x24  |    24.7us |  1560.0us |     7 |   138 |    4924 |
| agent, proposal, review | 80x24  |    16.8us |    69.9us |    26 |   264 |    7964 |
| agent, proposal, accept | 80x24  |    15.7us |    71.5us |    40 |   241 |    8044 |
| agent, job conflicts | 80x24  |    22.5us |    37.5us |    36 |   279 |    9412 |
| agent, map changed event | 80x24  |    22.7us |    25.2us |    36 |   279 |    8104 |
| agent, changes read  | 80x24  |    22.7us |    51.9us |    36 |   279 |    8208 |
| build, outside change 512x512 | 80x24  |    17.1us |  1700.0us |    26 |   271 |    7328 |
| build, :w empty 512x512 | 80x24  |    16.7us |    30.3us |    38 |   329 |    4140 |
| build, :w full 512x512 | 80x24  |    24.1us |    47.5us |    38 |   300 |    8440 |
| :check, rooms 512x512 | 80x24  |    16.0us |    72.6us |   282 |  1718 |    8008 |
| :check, every edge 512x512 | 80x24  |    22.3us |   149.3us |   336 |  1880 |  137428 |
| help page            | 80x24  |    35.2us |    51.1us |   542 |  2601 |    4184 |
| profiler overlay     | 80x24  |    30.7us |   139.5us |   201 |  1139 |    4144 |

> The machine's own baseline drifts: one recording of this table sat ~10% above its
> neighbors on every row, and none of it was the code -- the previous binary run
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

Machine: Intel Core i7-8700K @ 3.70GHz (6 cores, 12 threads, 4.7 GHz max, powersave governor), 16 GB RAM, Samsung SSD 860 EVO 1TB (btrfs), Linux 7.2.8-9-omarchy, gcc 16.2.1 -O2

| path             | p50     | p99     | worst   | calls | heaviest scenario      |
|------------------|---------|---------|---------|-------|------------------------|
| agent.reap       |   0.7us |   3.1us |  64.5us | 19136 | build, :ask starts an agent 80x24 |
| agent.spawn      |  93.6us | 173.9us | 212.2us |   400 | build, :ask starts an agent 80x24 |
| app.check        | 377654.5us | 402841.9us | 402841.9us |    40 | :check, every edge 512x512 80x24 |
| app.draw         |  68.8us | 132.2us | 206.0us |  2000 | profiler overlay 80x24 |
| camera.party     |   0.1us |   0.2us |   2.8us | 1925+ | play, camera party, 4 watchers 80x24 |
| card.draw        |  37.1us |  63.0us |  63.0us |    40 | :check, every edge 512x512 80x24 |
| character.place  |   0.6us |   0.9us |   1.2us |  262+ | play, 500 characters 80x24 |
| check.fold       | 1987.8us | 2175.9us | 2175.9us |    41 | :check, rooms 512x512 80x24 |
| check.joint      | 480.7us | 523.0us | 523.0us |    81 | :check, rooms 512x512 80x24 |
| check.pairs      |  49.9us |  81.9us | 117.4us |   400 | agent, check 512x512 80x24 |
| check.shift      | 6086.2us | 6931.4us | 6931.4us |    81 | :check, every edge 512x512 80x24 |
| checkpoint.read  |   1.6us |   1.9us |  15.2us |   399 | agent, map changed event 80x24 |
| clock.draw       |   1.2us |   1.3us |  22.1us | 15067+ | play, clocks 80x24     |
| counter.step     |   0.4us |   3.2us |  53.0us |   800 | play, stress to zero and back 80x24 |
| ctl              | 5924.3us | 6657.1us | 7747.4us |   400 | agent, check 512x512 80x24 |
| ctl.checkpoint   |  15.7us |  37.7us |  69.8us |   400 | build, :ask starts an agent 80x24 |
| disk.check       |   1.7us |   6.3us |  16.1us |   399 | build, outside change 512x512 80x24 |
| disk.reload      | 1638.6us | 1801.2us | 1852.9us |   399 | build, outside change 512x512 80x24 |
| dmg.apply        |  12.7us |  21.4us |  28.7us |   400 | play, damage 80x24     |
| draw.options     |   6.4us |   8.5us |  29.7us |  7998 | menu, options 80x24    |
| editor.draw      |  64.3us | 123.8us | 186.0us |  3200 | build, mostly void 200x50 |
| env.pick         |  28.0us |  47.6us |  75.6us |   400 | play, env adversaries 80x24 |
| floor.pick       |   0.7us |   1.0us |  13.2us |  2000 | agent, check 500 creatures 80x24 |
| floor.switch     |   0.1us |   0.1us |   0.1us |     1 | build, one floor 80x24 |
| fog.blank        |   0.4us |   0.9us |  13.2us | 1638+ | play, fog soft edge, 4 watchers 80x24 |
| fog.paint        |   0.1us |   1.8us |  35.1us |  1600 | build, fog paint 80x24 |
| fog.reveal       |   3.0us |   3.1us |  22.3us |  335+ | play, fog range, 4 watchers 80x24 |
| fog.sight        |  14.8us | 164.6us | 245.6us |  3599 | play, fog reveal 12 80x24 |
| fog.sight.build  |  15.8us | 111.1us | 194.2us |  3601 | play, fog warren 12 80x24 |
| fog.sight.rim    |   6.2us |  19.3us |  25.4us |  338+ | play, fog range, 4 watchers 80x24 |
| fog.sight.union  |   8.2us | 159.1us | 239.6us |  3599 | play, fog reveal 12 80x24 |
| grid.draw        |  71.5us | 114.5us | 158.8us |  3200 | build, 200x200 200x50  |
| grid.labels      |   2.4us |  18.2us |  31.4us |  2000 | build, outside change 512x512 80x24 |
| group.box        |   0.1us |   0.2us |   0.2us |  383+ | play, fog warren, party 80x24 |
| group.move       |   0.3us |   2.6us |  15.6us | 1532+ | play, fog warren, party 80x24 |
| handout.send     |  12.0us |  24.1us |  29.4us |  282+ | play, handout typed, 4 watch 80x24 |
| input.key        |   0.1us | 389301.4us | 402849.5us |   320 | :check, every edge 512x512 80x24 |
| job.accept       |  11.1us |  26.6us |  32.1us |   400 | agent, room + 12 80x24 |
| job.conflicts    |   9.0us |  11.6us |  24.1us |   400 | agent, job conflicts 80x24 |
| job.highlight    |   0.3us |   0.4us |  23.7us |  8800 | build, a job's tint 80x24 |
| job.preview      |   0.6us |   0.8us |   1.0us |   400 | agent, proposal, accept 80x24 |
| job.propose      |  40.1us |  80.1us |  98.2us |   400 | agent, room + 12 80x24 |
| job.review       |  12.7us |  15.7us |  31.9us |   400 | agent, proposal, accept 80x24 |
| link.marks       |   3.8us |   4.2us |  24.5us |  5600 | play, link there+back 80x24 |
| link.trip.map    | 4185.2us | 7368.9us | 16558.3us |   800 | play, map trip there+back 80x24 |
| log.write        |   2.8us |   9.0us |  15.6us |   253 | play, logging 80x24    |
| map.copy         | 370.2us | 370.2us | 370.2us |     2 | agent, job conflicts 80x24 |
| mapdiff          |  55.2us |  89.8us | 110.9us |   399 | build, outside change 512x512 80x24 |
| mapio.fsync      | 2014.1us | 5070.0us | 8208.5us |   400 | build, :w empty 512x512 80x24 |
| mapio.load       | 3115.8us | 3551.1us | 3813.1us |    41 | :check, every edge 512x512 80x24 |
| mapio.sections   | 220.9us | 263.1us | 455.3us |   401 | agent, check 512x512 80x24 |
| mapio.write      | 2449.9us | 5510.0us | 8634.9us |   400 | build, :w empty 512x512 80x24 |
| move.label       |   0.0us |   0.9us |  15.8us | 15854+ | play, fog sight, 4 watchers 80x24 |
| names.sync       |   4.0us |   4.0us |   4.0us |     1 | play, pings, 4 watchers 80x24 |
| net.accept       |   5.2us |  11.9us |  11.9us |    4+ | play, fog soft edge, 4 watchers 80x24 |
| net.catch_up     |  22.9us |  36.3us |  36.3us |    10 | play, a phone that falls behind 80x24 |
| net.cmd          |   0.0us |   0.1us |  13.4us | 22400 | play, pings, 4 watchers 80x24 |
| net.enc_full     |  19.1us |  32.6us |  32.6us |    15 | play, a phone that falls behind 80x24 |
| net.frame        |  11.0us |  24.3us |  86.9us |  5595 | play, pings, 4 watchers 80x24 |
| net.names        |   0.3us |   0.3us |   0.3us |    1+ | play, whisper, 4 named 80x24 |
| net.players_frame |  32.9us |  52.7us | 102.8us | 8035+ | play, fog range, 4 watchers 80x24 |
| net.whisper      |   4.8us |  18.8us |  22.5us |  244+ | play, whisper, 4 named 80x24 |
| note.marks       |   0.0us |   0.1us |   0.1us |  7997 | build, noted squares 80x24 |
| open.file        | 6876.1us | 6876.1us | 6876.1us |     1 | :check, every edge 512x512 80x24 |
| panel.draw       |   6.2us |  12.0us |  56.7us |  3995 | play, turn order 80x24 |
| picker           |  17.2us | 289.0us | 570.2us | 2099+ | play, 500 characters 80x24 |
| picker.draw      |   8.8us |  17.0us |  36.6us | 2622+ | play, 500 characters 80x24 |
| picker.open      | 3188.2us | 3908.1us | 3977.9us |  263+ | play, 500 characters 80x24 |
| ping.draw        |   0.5us |   0.7us |  15.5us | 9120+ | play, fog pings, 4 watchers 80x24 |
| play.draw        |  61.6us |  91.8us | 157.5us |  5595 | play, 24 tokens 200x50 |
| play.link        |   0.3us |   0.4us |   4.7us |   800 | play, link there+back 80x24 |
| prof.overlay     |   9.6us | 104.6us | 139.6us |  1000 | profiler overlay 80x24 |
| range.draw       |  17.1us |  22.5us |  81.4us |  3995 | play, range radius 80x24 |
| range.status     |   4.9us |   7.2us |  23.3us | 6695+ | play, fog range, 4 watchers 80x24 |
| rooms.build      | 3858.1us | 5744.6us | 5744.6us |    80 | :check, every edge 512x512 80x24 |
| ruler.draw       |   0.6us |   0.7us |  15.9us |  4400 | ruler, three legs 80x24 |
| scene.restore    |  25.9us |  40.4us | 142.3us |   400 | play, scene of 500 80x24 |
| scene.save       |   8.0us |  21.7us |  46.2us |   400 | play, scene of 500 80x24 |
| stamp.place      |   2.5us |   2.7us |  15.8us |   400 | build, stamp 20x20 80x24 |
| stamp.show       |   3.7us |   4.3us |  21.6us |   400 | build, stamp 20x20 80x24 |
| summon.place     |   0.9us |   1.1us |   3.5us |  756+ | play, summon 80x24     |
| trail.draw       |   0.1us |   0.2us |   2.9us |  9995 | play, carry 200x200 80x24 |
| trail.path       |   3.0us |  12.3us |  70.1us |  6231 | play, carry 200x200 80x24 |
| turn.advance     |   0.6us |   4.2us |  28.6us |   800 | play, turn order 80x24 |
| turn.status      |   0.7us |   0.8us |  15.8us |  5595 | play, 500 on 200x200 80x24 |
| undo.step        | 163.9us | 208.2us | 307.3us |  1200 | build, fill+undo 200 80x24 |
| undo.trim        | 2386.8us | 3160.9us | 3160.9us |    97 | build, fill history 80x24 |

### Reading it

A finding a paragraph for the most part, its bold lead the finding, in the groups headed below.

**How to read a figure here** (reread against the tables 2026-10-13, HEALTH 67). A figure
beside a named row or zone is that row's median in the tables above, brought up to them when
they are regenerated. A pair with an arrow or a "was", a figure given as an A/B, and one
with a date or a machine beside it are the finding's own measurements, as taken then. Most
were read off the laptop's tables (the page's opening says which machine each table is), whose frame rows are about
a third higher (`build, open` 31µs there, 23 here), so such a pair's ratio is what to read,
not its numbers.

#### The frame, what is layered on it, selecting several

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

#### Undo

**Undo is 20 bytes an op and bounded.** An op used to carry two whole tokens, 228 bytes,
for a tile paint that needs two. The token payloads now live in a side array the log
owns, and a full fill of the largest map (512×512, 262,144 ops) went from 60 MB to
5 MB of history. Taking one back is `undo.step`: 164µs for the 40,000-op fill of a
200×200 map, 4.1ns an op, all of it the tile writes (140µs with the binary built aligned:
*The run of 2026-10-13*). The log holds four such largest-map
fills and then drops its oldest quarter in one memmove, `undo.trim`: about 2.4ms, once
per 262,144 new ops, which is 9ns an op amortized and a thing that happens a handful
of times in a long session of painting. The scenario that measures it (`fill history`)
paints and clears 200×200 without undoing so the log grows 80,000 ops a loop.
**And it is bounded in bytes** (2026-10-20, review 4's M1): a creature's op carries one or two
256-byte token slots, which the cap in ops did not count, so 1,500 scene restores of 500
creatures held 286,768 KB at their peak. With `UNDO_MAX_BYTES` (32 MB of ops and slots) the
same run peaks at 37,000 KB (38,768 at 400 restores, 18,960 at 50: where the trim falls), the
desktop, `ru_maxrss` of the release `./vtt --bench`, one run each. The check is one
multiply-add per batch closed.

#### Play: range and templates, cards, damage, whispers, the camera, the turn order, the panel

**The range highlight costs the window when it covers the window.** `play, range radius`
holds a 100 ft reach over a 40×25 map, so every visible tile is in range and every one
of them gets a line-of-sight trace: `range.draw` at 17µs is that, and `range.status` at
5µs is the same trace once per creature for the 24 named in the status line. The band
scenario reads cheaper only because it spends most of its frames on the near bands; its
*Very Far* frames cost the same. The radius itself is free, and `:scale` cannot make it
dearer -- reach is compared in feet, tiles are still counted in tiles.

**A card beside the map costs its text, not the window.** `play, card box` walks the
cursor with a creature selected whose card is a stat block's worth (about 700 bytes):
32.5µs a frame against 22.2µs for the same crowd with no box. `card.draw` is 8.2µs of that:
the text cleaned of its marks, wrapped to measure and wrapped again to draw -- a cost that
follows the card's length, at most 4 KB, and not the map's. Loading a map is unchanged by
the card block's check on every line (a 512×512 map, A/B'd against the build before cards:
342-348ms a run either way).

**Damage is one command, not a frame.** `play, damage` puts a Far burst over 13 of a crowd
of 24, each with a card, and marks 6 damage on them all, then `u`: `dmg.apply` is 12.5µs at the
median and 15.4µs at p99 -- the burst's catch (`range_caught`, once over the creatures), each
card's thresholds read, one undo batch. The frame around it, 32.3µs, is the card box and the
burst's highlight; the Horde title the box asks for (`app_horde_note`) is a scan of one card.

**A whisper is a few writes, and naming phones costs nothing per frame.** `play, whisper, 4
named` whispers to one of four named phones every loop: `net.whisper` is 4.8 µs at the
median, 12.4 µs at p99 (a socket write and flush per phone of the name). The names offered
are rebuilt only when the map changes -- `names.sync`, 4.8 µs, ran once in the whole
`play, pings, 4 watchers` run -- and `net.names` sends them only when the text differs.
The frame row sits with the other four-watcher rows (39 µs). (The run that first published
it read the circle brush at 55 µs; an A/B against the previous build gave both 32 µs, and the
next median of three agrees: the machine, not the code.)

**A camera of the players' own costs a second draw, and sends less.** With `:player camera
party` or `hold` the players' frame is always drawn apart, as it is with a creature hidden:
`play, camera party, 4 watchers` (carrying a creature through the crowd) is 46.0 µs a frame
and `play, camera hold, 4 watchers` (the GM's cursor walking) 40.5 µs, between `play, 4
watchers` (38.9 µs) and `play, hidden, 4 watchers` (56.8 µs). What goes to the phones falls:
held, the GM's cursor is not on their frame, so walking it sends them nothing; a party
camera moves only when a creature nears its edge, so carrying one through the crowd sent
125 bytes a frame to all four in a by-hand run, against 1,457 under follow, whose view
moves with the GM's. `camera.party` works the framing out only when the map, their floor,
the GM's zoom or the screen changed: 0.1 µs at the median, 0.3 µs at worst.

**A group effect is the range highlight on a moving center.** `play, group effect` walks a
Very Close burst across a crowd of 24: 26.0µs a frame against 22.2µs for the same map with
no overlay (`play, 24 tokens`), the tint and a sight trace for the dozen or so squares it
covers plus the status line's trace per creature. Moving it re-aims it and nothing else.
The Daggerheart movement rule on the MOVING line is one lookup into the band table the
label beside the creature already made; the carry rows do not move.

**A template costs less than the circle it is cut from.** Cone, line and square test the
same visible tiles the circle does and then turn most of them away, so they shade fewer
squares and trace fewer sight lines: 55 to 63 cells a frame at six squares of reach against
129 for the circle's bands and 169 for a whole radius, and 24.6-25.1µs against 25.3 and
42.8. The geometry is worked out once per frame -- origin, aim
vector, the square's box -- and the per-tile test is a dot and a cross product. Aiming is
free: the template reads the cursor after each key rather than owning any keys of its own.
With the overlay off the whole path is one early return, which is why an A/B of `play, 24
tokens` against the binary from before templates reads 31.7µs on both.

**The turn order is never sorted, or stored.** It is a number on each creature, and every
question asked of it -- who is next, who is after that, where does `t` go from here -- is
"the smallest key greater than this one", which is one pass over the tokens with nothing
allocated. `turn.advance` is that pass plus two token edits and a round op through the undo
log: 0.6µs with 24 creatures in the fight. `turn.status` is the title bar's readout, the
same pass three times a frame: 0.7µs. A fight costs the frame about two microseconds,
inside this page's noise -- `play, turn order` reads 24.2µs against 22.2µs for the same map
with nobody fighting, and writes 49 cells against 19, the title bar's readout (31.4
against 31.9 on the laptop: the two have not been profiled apart since).

**The side panel is 24 columns of the frame, and costs like it.** `panel.draw` is about 6µs at
80×24 (on the laptop 8.4µs at 200×50 and under 3 at 80×24): a fill, a separator down every row and a dozen short
strings, redrawn each frame like everything else and diffed away by the renderer when
nothing changed (`play, 24 tokens` writes 315 bytes a frame with the panel, 319 without).
It takes its columns from the map view, so every play row on a Daggerheart map moved
when it landed: the bench's 24-token map is one, and under a spotlight ruleset the panel
is always up. At 80×24 the rows fell by a microsecond or two, the grid being 24 columns
narrower; at 200×50 `play, 24 tokens` rose from 102µs to 108µs, the panel's 8µs less the
grid it displaced. None of it is the turn order, which the paragraph above prices.

#### The players' frame, a watcher, the page, a jump of the view

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
`net.players_frame` is 33.2µs typical, and `play, 4 watchers, differing`
(a creature with a note selected, so every frame differs) reads 53.6µs
against 38.9µs. That is the price the plan in `docs/FOG.md` accepted for a
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
loopback client attached: 31.7µs against 22.2µs. Four watchers are 38.9µs, so each is
about 4.2µs a frame, and `net.frame` -- encode once, then per client the palette check,
a copy into its buffer and a `write` -- is 11.2µs with four. The encode is shared and the
copy is a few hundred bytes, so nearly all of the 4.2µs is the syscall (3.8µs for a loopback write, *Speed of light*), which is the floor
for a design that sends every frame the moment it exists rather than batching. The plan
budgeted 2µs a client; it missed by about the cost of a write on this kernel. With nothing
changed nothing is sent, and with no client attached the whole path is one compare per
changed cell. Bytes per frame to the clients, all of them together, are on the F12
overlay and the bench summary.

**The page, measured in Chrome on a 1280×768 frame.** A cursor move (about 40 cells)
paints in 0.2-0.5ms; a scroll of 1220 cells in 0.7-2ms, of which the pixel push is about
1ms. Three implementations of the copy loop were tried: JavaScript typed-array row copies
at 4-5µs a cell, `memory.copy` in WebAssembly at 40ns a row, and a WebAssembly loop of
64-bit words at 0.21µs a cell, which is the one shipped. A canvas `drawImage` sprite path
measured no better than the JavaScript copies. The tile cache -- a glyph in its colors
rendered once -- is what makes the copies the only per-cell work; the first frame after a
palette change or a resize pays for every tile again, about 15ms for a full screen.

`play, fight cycling` is dearer at 32.6µs and 4.6KB a frame, and none of that is the turn
order: it is the first scenario that presses `t` at all, and `t` sends the view to a
creature that is usually somewhere else, so most of the window repaints. Walking the
tokens in list order scrolls exactly as far.

#### Clocks, notes, named rolls

**Clocks cost a panel row each, and only when the panel is drawn.** `clock.draw` is
1.2µs typical for two clocks: a fill of its rows, a name column, a dot per segment.
`play, clocks` runs two `:clock` and two `:tick` commands a loop and its frame is the
cost of typing them, 20.7µs against 22.2µs for the same map with no panel -- the panel
takes columns off the map, so the frame has fewer cells to diff. Ticks are one undo
op of the tile kind. **Notes cost nothing per frame in play mode**, by design: the
text is never drawn there. Build mode's `note.marks` walks the notes, not the tiles,
0.1µs for three. **A named roll is a string lookup**: `play, named roll` reads the
same as `play, rolling`.

#### Fog and sight

**Fog costs the GM under a microsecond a frame and a map without it nothing.** `grid_draw` decides once a
frame whether any patch is live; a map with none takes the path it always took, and
`play, 24 tokens` sits where it did (22.2µs). With fog over the whole map, `play, fog` is
22.5µs (1.5µs over it when fog landed): one byte load and a mask a visible tile, writing the dim color into cells that were
being written anyway. The players' frame over fog is cheaper to draw than the GM's -- a hidden
tile is a `continue` with no writes, and walls between hidden tiles are never resolved -- so
`play, fog all dark, 4 watch` at 42.6µs is the second draw the plan priced in, less than the
differing-frame row pays for a note. `fog.paint` is 0.2µs a brush stroke, a handful of 20-byte
undo ops. `fog.reveal`, a `g r` or `g h`, is 3.0µs: a footprint of undo ops, or a
patch's extent for `g R`. `fog.blank`, the pass that takes the dark back out of a range wash or a trail in the
players' frame, runs only while one of those is showing over fog.

**Sight is a keystroke cost, not a frame cost, and follows the party's reach.** `fog.sight`
runs after a keystroke that changed the map, never per frame, so it is not in the frame
columns above: `play, fog sight` walks a creature through a map-wide patch at `reveal 6` and
its frame reads 22.2µs, level with the rest. A full recompute is about 28µs on that bench map, which has twelve player creatures each
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

#### Phones: the keepalive and pings

**A quiet phone costs 32 bytes a minute, not a reconnect.** A browser never speaks on
its own, and the server used to drop one after a minute of silence: a phone whose player
never tapped reconnected every minute -- a TCP handshake, a 300-byte upgrade, a 150-byte
reply and a 5 KB FULL, about 5.5 KB a minute and a visible blink. Now a browser silent
for 15 s is sent a two-byte WebSocket ping and answers with a six-byte pong on its own:
32 bytes a minute, and the FULL is paid once when a sleeping phone wakes. No row: the
check is a compare inside the loop that already runs once a second with clients
attached, and the bench's clients are watchers, which are unchanged.

**A ping costs a tenth of a microsecond to read and half of one to draw.** `net.cmd`
parses a tap in 0.1µs and `ping.draw` rings a square in 0.5µs. `play, pings, 4 watchers`
is the worst case on purpose: every one of four phones taps every frame, a second of
synthetic time apart, so four rings are always up and moving and the status line changes
every frame. It draws in 48.2µs against `play, 4 watchers`'s 38.9, and sends 978 bytes a
frame to each client against 298 -- the rings' cells and the status line, as ordinary
frame runs; a single tap at a real table is a few hundred bytes once. Over fog the
players' frame is drawn apart from the GM's anyway, and pinging adds about the same.
`play, GM ping` is `g p` with no server: level with the plain play rows. A tap up the
wire is under 16 bytes.

#### Sight, scenario by scenario; the plan; the soft edge

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
edge shown: 45.1µs against 45.8µs, the difference inside the noise. `fog.sight`'s p99
(165µs, in `play, fog reveal 12`) is the full rebuilds and the step after one, as *Sight,
scenario by scenario* says; the recompute's code is unchanged by this step and its typical
figure did not move.

#### Counters, the session log, the autosave and saves

**A counter step is a token edit.** `counter.step` is 0.4µs typical: find the
current counter, clamp, and one `undo_edit_token`, the same path a relabel takes.
`play, counters` sets HP through the prompt and then steps it eight times a loop,
and its frame reads 21.5µs, level with `play, 24 tokens`. The panel's counter is one
`snprintf` on the actor's row, inside `panel.draw`'s existing 6µs. Counters are the
GM's, so a selected creature carrying one makes the players' frame differ and draws it
twice while serving -- the cost `net.players_frame` already measures, paid only then.

**A condition that follows a counter costs nothing that shows.** Under Daggerheart every
counter edit asks whether Stress reached or left 0 (`ruleset_counter_changed`: two lookups
among at most four counters, one among at most four markers) and adds or removes
Vulnerable in the same edit. `play, stress to zero and back` marks a creature's last
Stress and clears it, a marker on and off each loop: `counter.step` is 0.4µs typical and
2.7µs at p99 there, where a plain step in `play, counters` was 0.2 and 2.3 before, and the
frame is 22.6µs and 376 bytes. The plan estimated tens of nanoseconds over a step; the two
tenths of a microsecond are that and the longer message. The box beside the map builds a
marked creature's text once and keeps it (`card.draw` is unchanged at 8.2µs in `play, card
box`). First measured 2026-10-12 with every other row. Two fog rows read a tenth lower than the
day before (`play, fog warren` 24.7 to 22.1µs, `play, fog reveal 12` 24.3 to 21.7): run
against the commit before, three times each, both binaries read 21.5 to 22.5, so it was the
earlier run that was high.

**The session log costs a line, once per thing that happened.** `log.write` is a
`strftime`, an `fprintf` and an `fflush`: 2-3µs, and only on the keystroke that put a
creature down or rolled the dice, never per frame. The flush is deliberate -- the log is
for the crash as much as the recap -- and it is why the figure is microseconds rather than
nanoseconds; buffering would save nothing anyone could feel and lose the last line when it
mattered. With the log off the call is a null check. Dice are cheaper still: `play,
rolling` is two `:roll` commands a loop and reads as the cost of typing them.

**The recovery autosave is a whole-map write, off the keystroke path, and not flushed.**
It writes the map to `name.vtt.autosave` once the changes have been quiet for 1.5 seconds,
never while keys are arriving, and never twice for the same state: the map carries a
generation counter and the copy is owed only while it is behind. It is written and renamed
but not `fsync`ed (`mapio_write_unflushed`): the flush is the whole cost of a save on a
real disk, the autosave runs dozens of times a session on the main loop, and what it is
for -- vtt crashing, the terminal closing -- the system survives with the file in hand.
Only a system crash or power cut within about half a minute of it can lose it -- or, on
ext4 or xfs, leave just its start, which recovery refuses -- and the map's own file is
never touched by it. `:w`, a trip and every other save still flush.
`tools/saves.sh` prints this table (perf.sh cannot: a bench never writes an autosave);
median of nine, on the **laptop** (btrfs on an SSD), before the writer was changed
(2026-10-10: rows built whole, one write). The desktop's, median of three runs after it:
2.08 / 2.45 / 3.33 ms flushed and 0.06 / 0.32 / 1.18 ms unflushed (before: 1.97 / 2.76 /
5.70 and 0.07 / 0.63 / 3.26). Its disk flushes about fifteen times faster. The laptop's
table is to be taken again. After 2.0's step 5 (2026-10-14, a row of nothing written as an
empty line) the same maps are 3.0 / 81.3 / 527.4 KB (were 4.0 / 121.1 / 789.1: each holds a
floor in a ring of walls, so only the inside `hedges` rows are empty) and the desktop's
medians of three runs are 2.05 / 2.47 / 3.24 ms flushed and 0.06 / 0.31 / 0.98 ms unflushed.

| map | bytes | autosave (unflushed) | `:w` (flushed) |
|---|---|---|---|
| 40×25, 24 creatures | 4.0 KB | 0.15 ms (worst 0.2) | 33.42 ms (worst 55.7) |
| 200×200 | 121.1 KB | 1.05 ms (worst 1.8) | 55.53 ms (worst 66.4) |
| 512×512 | 789.1 KB | 5.28 ms (worst 5.7) | 66.71 ms (worst 141.0) |

The flushed column is a save's cost, and the price of a power cut losing nothing. A snapshot was chosen
over replaying the undo log because the log does not see everything the map is: the
ruleset, the scale, the clocks and named rolls, a resize. Anything that goes through
`map_touch` is covered, which is everything. An idle `vtt` with nothing owed still
blocks in `poll` for ever; the only timer is the one write it is waiting to make.

#### Groups, selection and the cursor

`group.move` is the whole formation's move: the check for every member, then the
steps. **0.2us typical and 2.6us at p99** for the sizes a table plays at. It is
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
that fixed the contrast problem is also nearly the cheapest one available: it recolors
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

As measured when the cursor landed; the tables' `play, 3x3 cursor` row reads 24.9us, 37
cells and 606 bytes now.

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

**Choosing which creature to pick up is the cheapest thing in the table** -- 12 cells
and 95 bytes a frame, less than half what an idle build frame writes -- for the reason
that makes it worth having: the cursor holds still while enter walks the creatures it
covers, so between one press and the next almost nothing on screen changes. Only the
selection highlight moves, and that is two token bodies. The walk itself is a scan of
the token list per press, on a keystroke rather than a frame, and does not register.

#### Routes, the profiler's overlay, a key's worst

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
route: about 3µs beside a short walk on any map size, with the long walks in the p99.
It runs on a keystroke rather than a frame, so even its worst has hundredfold headroom.

**`prof.overlay` is the most expensive thing here relative to its worth**, which is why
it reports itself: an instrument that quietly adds to the number it displays is worse
than no instrument. It is off unless you press F12.

**`input.key`'s worst is a carried step**, not a cursor move: the route is recut
underneath it, so it is `trail.path`'s worst wearing a different name. A bare cursor
keystroke is 0.2µs.

#### Links, floors, hidden creatures, trips

**Links cost their marks and nothing else.** `link.marks` walks the links rather than the
tiles, culls each end to the window, and writes a glyph and a number into cells the grid
already drew: 4.0µs a frame at p50 with all sixty-four a map can hold on screen (`build, 64
links` 26.9µs against 23.0µs for `build, open`), and a map without links returns before the
zone opens. Taking one is a few token moves in a batch: `play.link` is 0.3µs typical. The
trip's frame writes more (`play, link there+back`, 637 bytes) because the view recenters on
the far end, which redraws the window; that is the cost of a jump, not of the link.

**A floor costs nothing to show; a second floor costs a second frame.** Showing one floor
culls to its box, so `build, one floor` draws fewer cells than the whole map (12 cells and
176 bytes a frame, against 209). When the players are on a floor the GM is not showing,
their frame cannot be copied from the GM's and is drawn again through its own camera:
`play, other floor, 4 watchers` is 41.2µs against 38.9µs for `play, 4 watchers`, under
what a frame that differs with a note to draw costs (`play, 4 watchers, differing`, 53.6µs). `floor.pick` runs after
every key and before every draw to keep the players' floor current: 0.1µs for 24
creatures, 0.7µs for 500, a walk of them all. `floor.switch` is under a microsecond.

**A hidden creature costs the players' frame its copy.** With anything hidden the players'
frame is drawn rather than copied from the GM's, so `play, hidden, 4 watchers` is 56.8µs against
38.9µs for `play, 4 watchers` -- the same as any frame that differs (`play, 4 watchers,
differing`, 53.6µs), and nothing more: the question each creature is asked,
`fog_token_unseen`, is a flag test before any fog work. Bytes do not move (298), since the
players are sent what changed on their screen and a hidden creature changes nothing there.

**A trip to another map is two flushed saves: about 4 milliseconds on the desktop, 65 on
the laptop.** `link.trip.map` (4.3ms at the median, 7.5ms at p99, 16.7ms at worst) reads the
other map, finds the party room, and writes both maps -- the one left and the one arrived
in -- for two 40×25 maps. Nearly all of it is the two flushes (`mapio.fsync`, 2.0ms each at
the median; `mapio.write` round the whole write is a tenth of a millisecond more). That is
kept on purpose: a trip is a deliberate change of map, like opening a file, and it is never
half on disk. On the laptop, whose disk flushes about fifteen times slower, the same trip
was 66ms at the median, 144 at p99 and 378 at worst, a flush 32ms; and there the frames
right after a flush drew slower (`play, map trip there+back`, 50µs against 30µs with the
same maps on tmpfs, A/B'd): the same code, run cold.

#### Handouts, scenes, the character picker, stamps

**Handouts cost a send, not a frame; and the players' frame stopped changing when only the
GM's message does.** Putting a handout up is one `'H'` record to each client
(`handout.send`, 13µs for four watchers); the frames around it are ordinary
(`play, handout typed, 4 watch`, 40µs; mostly typing the `:handout say` line). The same commits made four rows 15-20% cheaper --
`play, fog all dark, 4 watch` 75.5µs to 62.9µs, fog sight 87.3 to 69.7, soft edge 82.7 to 67.3,
other floor 80.1 to 66.3 -- A/B'd against 3c7cf55 over three runs each. It is the
review fix for the players' status line: it was cut short by the width of the GM's
message even where that message is not drawn for the players, so every key that changed
the GM's message changed the players' frame too, and was encoded and sent (216 bytes a
frame to four watchers over total fog, where they see nothing move; 0 now). `net.frame`
in those scenarios went from 14.8µs at the median to nothing to do.

**A scene of 500 creatures goes back in 26µs.** `scene.restore` removes every creature
from the end of the list (no memmove) and adds the scene's, each through the undo log, and
sets the round and spotlight: 25.9µs at the median for 500, 56µs at p99. Saving the same
500 (`scene.save`, a copy of the list) is 7.5µs. The frame around it, 28µs (on the laptop 83µs before
creatures off the window were culled and 38 after), is ordinary; both zones run once per `:scene`,
never per frame.

**Opening the picker is its one dear moment: 3.2ms to read 500 saved characters.**
`picker.open` reads every file in the directory once, for the label, side, size and
counters the list shows, so it is the directory's size -- about 6.5µs a template since `file_read` reads a file in one piece (2026-10-10; 9 before, 4.7ms) -- and it
is the slowest keystroke there is but a trip to another map (`picker.open`; `input.key`'s
worst is the trip's). Under a
frame at that size, once, when the GM asks for the list; typing into it (`picker`, a ranked
filter over the names in memory) is 17µs at the median, drawing it (`picker.draw`) 9µs, and
placing one
(`character.place`) is a microsecond. A campaign with thousands of templates would want the
details read lazily, for the rows on screen.

**A summoned creature costs a microsecond to place, and the highlight is `r`'s.**
`summon.place` is 0.9µs typical and 1.1µs at p99 (`play, summon`: two put down inside Far of
a creature among 24, then taken back): the test of the square (its footprint against the
range, the map, the floor and the creatures on it) and `character_place`, which is 0.6µs of
it. The plan's estimate was a few bytes read for the test and one creature appended; what
is over that is the scan of the creatures every placing makes and the line written to the
log, both there before this. The frame is 18.6µs and 257 bytes, 36 cells: no drawing is
new, the highlight is the range overlay's own (`range.draw`) and it does not move. The row reads the
template's file once a loop, in the command, not once a creature. With phones watching, the
mode's line is the GM's alone, so each frame while summoning draws the players' view apart
(a second draw) where it would otherwise be copied: what every GM-only line costs, for as
long as the summon is open. First measured 2026-10-12 with
every other row; the one row that moved by more than a tenth, `build, outside change
512x512` (22.2 to 28.5µs), was run three times against the commit before and reads 19 to 32
on both, being the disk's.

**The picker for an environment's adversaries costs the three it names, not the 500 saved.**
`env.pick` is 28.6µs typical and 57.6µs at p99 (`play, env adversaries`: a card naming
three of 500 saved characters, one chosen and taken back): the card's line read and split,
a file looked for a name, and three templates loaded, where `picker.open` reads the whole
folder at 3.2ms. The plan's estimate was one file read a template named and a compare a
name; about 8µs a template is what `character_load` costs here. The frame is 19.9µs and
340 bytes. First measured 2026-10-12 with every other row. No frame row's typical figure moved
by a tenth. In the table below fourteen paths name a different scenario than before
(`app.draw`, `mapio.load`, `panel.draw`, `link.marks` among them): the table keeps each
path's worst p99, and where two scenarios are close, which is worst changes from run to
run. With that, several p99s under 25µs read a fifth or more off their last figure
(`picker.draw` 21.7 to 12.5, `job.review` 23.9 to 15.9, `stamp.place` 2.8 to 4.7): at
that size a p99 is a handful of samples, and none of those paths was touched.

**`stamp.show` is the preview of a stamp on the cursor**, and it is the stamp's size, not
the map's: its squares, boundaries and creatures are swapped into the map for the one draw
and swapped straight back, with nothing touched that sight or the autosave watch. A 20×20
stamp is about 4µs a draw, at the median and the p99, and 6µs at worst on a 200×200 map
(`build, stamp 20x20`); turning or mirroring one (`stamp.turn`) is a new stamp built once,
on the key, never a frame's work; `stamp.place`, the
real thing, is one batch of undo edits, a fill's cost.

#### An agent's request

**`ctl` is an agent's request, and it is dearest when it reads the most.** A 40×40
room over void with a dozen creatures in it -- 1,600 squares, the outline, twelve
placements, one undo batch -- is tens of microseconds (`agent, room + 12`). A whole plan of
five rooms placed by each other, five corridors and four creatures (`agent, plan of
rooms`) is about 21µs of edits (its frame is 73µs now that each request is a proposal made
and accepted, as the paragraph's end says): a corridor is planned as at most two boxes and checked square by
square before a square is dug, so its cost is its length. A `dump` of
the whole 512×512 map writes about half a megabyte of text and takes 5.3ms; it is a
one-off read an agent asks for by name (`dump all`), not a keystroke, and `dump B2:K12`
makes it the size of the region (*Reads that cost what was asked*, below). Nothing of the channel is on the frame path: off it costs nothing, on and
idle one more descriptor in `poll`, and the GM's frame after a request is an ordinary
frame (`agent, room + 12`'s frame row is the redraw of the changed squares). Since step 4 of
docs/CONFLICTS.md these rows run under `:agent accept auto`, so each request is a proposal
made and accepted at once: *Proposals from the channel*, below, has what that costs.

## Proposals

**Reading the figures in this section.** *Frame times* and *Cost per call* at the top are
the latest run (2026-10-13). The floor tables below were read off the run of 2026-10-10,
after step 9's review, and a figure beside a named row was brought to the tables since. Two limits: the microsecond rows move 5-10% run to run and their
p99 can double on a busier machine, and *Cost per call* keeps only each zone's heaviest
scenario, so a figure for a lighter one (a proposal's own `mapdiff`, now that an outside
change's is the heavier) is from that scenario's own trace and is not in the table.

An agent's change is a proposal the GM reviews (docs/CONFLICTS.md). The plan runs on a copy
of the map, and the difference is a change set the GM previews, then accepts whole or by
box. `tools/proposals.sh` times each piece apart from any key, in microseconds: the median
of 51 within a run, and the per-row median of three runs (`tools/median.py`).
- *diff* reads the scratch log; *diff (all)* compares every square.
- *preview, first* is one 80×24 frame's window (about 40×21 squares) swapped in and out,
  including building the preview's creatures, links and notes. *preview, frame* is every
  frame after that.
- The "fill" row is the worst a proposal can be: every square of the largest map.

Machine: Intel Core i7-8700K @ 3.70GHz (6 cores, 12 threads, 4.7 GHz max, powersave governor), 16 GB RAM, Samsung SSD 860 EVO 1TB (btrfs), Linux 7.2.5-3-omarchy, gcc 16.2.1 -O2

| plan and map                              |      copy |      plan |      diff | diff (all) |     check | preview, first | preview, frame |    accept |
|-------------------------------------------|-----------|-----------|-----------|-----------|-----------|-----------|-----------|-----------|
| five rooms, 40x25, 24 creatures           |     0.6us |     2.3us |     3.1us |     3.0us |     0.5us |     1.5us |     1.3us |     3.2us |
| five rooms, 512x512, 24 creatures         |    27.8us |     2.5us |     4.7us |    37.4us |     0.5us |     1.7us |     1.3us |     3.3us |
| five rooms, 512x512, 500 creatures        |    31.6us |     2.5us |    10.3us |    41.6us |     0.5us |     4.5us |     1.3us |     3.5us |
| fill the whole map, 512x512, 24 creatures |    54.6us |  3291.9us |  2539.3us |  2307.1us |   525.9us |     9.2us |     6.2us |  4320.5us |

**Rows that moved between publications.** The fill row's *accept* read 3.5 ms when step 1
was published and 4.1 ms after step 2, with no change on that path. An A/B of the two
commits found the later one no slower, so it is placement and noise. These tables are from
default builds, where placement alone moves a tight loop 5-9% (*The checkpoint*, below).
Treat a whole-map row's change under about 15% as noise unless an aligned A/B confirms it.

Against the speed of light (the desktop's figures above):

| path | floor | measured, 512×512 | gap, and why it is kept |
|---|---|---|---|
| copy | 1.05 MB at 37 GB/s: 28 µs | 28-32 µs | none. It is a copy **into** the scratch map the app keeps, so its pages stay mapped. A fresh `map_copy` of a 512×512 map is 434 µs: 1 MB of new pages, a fault each on first touch |
| diff, five rooms | the 300 squares written, about 1 µs, plus the creatures | 4.7 µs; 3.1 on 40×25 | about 2 µs: the 300 reads are scattered across two 1 MB maps, and the cost follows the arrays' stride, not their blocks. An empty log on 512×512 is 0.8 µs, so indexing the 1,089 blocks is half a microsecond. The fallback for a big log is the full diff (*diff (all)*: one `memcmp` a row) |
| diff, 500 creatures | streaming 2 × 128 KB of creatures: about 7 µs | 5.6 µs over the 24-creature row | none. The lists are walked in step (a copy keeps the order) and hashed only on a miss. Pairing them by search was 89 µs |
| conflicts | one compare per element | 0.5 µs | none |
| preview frame | the window's changed cells, swapped in and out | 1.3 µs | none. Only the used slots of the link and note arrays are swapped. Swapping the whole fixed arrays (9 KB a side) made a frame 2 µs. The first frame after a change also builds the preview's lists (4.5 µs with 500 creatures) |
| accept | the undo log's cost for the same writes | 3.3 µs | none. It is the plan's writes, recorded |
| a fill of the whole map | 262k cells: writing 2 MB and reading 1 MB, about 0.2 ms | diff 2.5 ms, check 0.53 ms | about 2 ms: a cell is pushed one at a time and bucketed by block. The accept, which only records the same writes in the undo log, is 4.3 ms, so the diff is not what a fill costs. Kept |

Findings on the way, each fixed before the table was taken:
- **The copy was paying for fresh pages.** It cost 434 µs, and keeping the scratch map
  brought it to 28 µs.
- **Sorting the cells with `qsort` was the dearest step.** It made a diff of five rooms
  14 µs and a fill 16 ms. Bucketing by block, plus a hash that drops a square written
  twice, is linear.
- **Reserving every list's worst case up front** (520 KB for 1,000 creatures) was an `mmap`
  and a `munmap` per diff. The lists double from 8 instead.
- **The first version of this table mismeasured two columns.** It timed the diff with a
  freshly allocated live map each run (9.8 µs, the setup's pages), and it timed the
  preview's first frame only. Fable's review of the step found both.

### The checkpoint

What the map changed since an agent last looked (docs/CONFLICTS.md, step 2). It is
copy-on-write by cell, and the cell's value at the start comes from the undo log's ops, so
the map's writers carry no test. The same `tools/proposals.sh` run; *fill* records an edit
of every square through the log, *undo* takes it back, each with the checkpoint off and on;
*read* is the changes since the start.

| checkpoint and map                        |     start | fill, off |  fill, on | undo, off |  undo, on | read, a dozen | read, a fill |
|-------------------------------------------|-----------|-----------|-----------|-----------|-----------|-----------|-----------|
| checkpoint, 40x25, 24 creatures           |     0.5us |     7.0us |     8.8us |     4.1us |     7.8us |     1.0us |     8.6us |
| checkpoint, 512x512, 24 creatures         |     1.9us |  1785.6us |  2285.1us |  1140.7us |  2220.8us |     3.5us |  2898.8us |
| checkpoint, 512x512, 500 creatures        |     6.7us |  1784.9us |  2280.5us |  1146.8us |  2212.4us |     9.1us |  2808.3us |

| path | floor | measured, 512×512 | gap, and why it is kept |
|---|---|---|---|
| any edit, no checkpoint | as before | as before | none. The undo apply loop has no test per op, and the map's writers none at all; the recorders test one pointer, expected false. An A/B against the commit before (both rebuilt with `-falign-functions=64 -falign-loops=32`) measures no difference |
| start | the small parts: 18 KB plus 256 bytes a creature, about 1 µs | 1.0-1.9 µs; 4.3-6.7 at 500 creatures, run to run | none |
| recording, while one runs | a value byte and a bit a cell, under 0.5 ns | about 2 ns a cell (a whole-map fill 1.79 → 2.29 ms) | about 1.5 ns: the block index and the bit's read-modify-write in the recorder. Out of line it was 3 ns |
| an undo step, while one runs | nothing for cells recorded since the start (recording noted them) | about 4 ns an op (1.14 → 2.22 ms for the whole map) | the walk over the batch's ops, though every cell in it is already noted; each reads the live cell, which the review's fix needs (below) and `apply` reads next anyway. Skipping a batch recorded since the start would need the log to know about the checkpoint. It only shows when a whole-map fill is undone while an agent is connected. Kept |
| reading a dozen edits | the twelve cells and the small parts | 2.2-3.5 µs; 7.8-9.1 at 500 creatures | none. The range is run to run: the table above is the median of three (3.5 and 9.1), a run on 2026-10-10 read 2.2 and 7.8, and step 2 published 2.3 and 8.0 |
| reading a fill | 262k cells: about 0.3 ms | 2.9 ms | the same pushing and bucketing as the full diff's, above: one cost, kept once |

**The value noted is the live cell's, not the op's.** Fable's review of the step found the
first version noting an undo's `after` (a redo's `before`). That is wrong once the log and the
map disagree: `fog_delete` clears painting round the log, so after one made before the start,
undoing the paint reported a change that never happened. Reading the live byte costs one load
per op, which `apply` makes next anyway.

**Code placement moves a tight loop by 5-9%.** The first A/B of this step showed
`undo.step` 9% dearer with no checkpoint running. It held with the hook emptied, with
identical machine code for `map_set_tile` and `apply`, and with page-aligned allocations,
and it vanished once both binaries were rebuilt with `-falign-functions=64
-falign-loops=32`. Adding `checkpoint.o` early in the link order had moved the hot loops
to addresses that happened to run slower. Two lessons for every A/B:
- Rebuild both sides aligned before believing a gap under about 10% on a tight loop.
- `make CFLAGS=...` does **not** rebuild, because the Makefile tracks only the
  release/debug mode. Use `make -B`.

### The jobs' tints

A job's box, a change waiting, and the squares it would overwrite are tinted on the GM's
screen (docs/CONFLICTS.md, step 3), drawn through `Editor.overlay` between the ground and the
map's marks.
- **Speed of light:** a background store for each visible cell the job covers. A box
  filling an 80×24 window is about 135 visible squares, three cells each: about 405 stores,
  about 0.15 µs.
- **Measured** (`build, a job's tint`, median of three on the desktop): `job.highlight` is
  0.3 µs at p50 and 0.4 µs at p99, twice the floor; the gap is the walk over the change
  set's blocks and the conflict pass. The frame is 22.5 µs at p50, no more than opening the
  map (23.0 µs); `editor.draw` measured 17.7 µs with the tint against 17.6 µs without.
- **What changed it:** the tint first set each square's cells one at a time (0.9 µs);
  `grid_tint_tiles` now tints a row of a box at once. An earlier figure here, 0.1 µs, came
  from a scenario that was not loop-neutral (after the third loop its box was one square),
  so it was below its own floor.
- **The cost follows the window:** a box is clipped to the squares on screen, and a change
  set is walked only in the blocks meeting the window.
- **Bytes:** the tint costs nothing once it is up, because the renderer sends only cells
  that changed. The scenario, which puts a tint up and takes it down every loop, averages
  301 bytes a frame.
- **The worst call, 15-25 µs,** is one frame of 8,800. It is the first draw after `:ask`, cold.

### Proposals from the channel

A request's edits run on a copy of the map and become a proposal (docs/CONFLICTS.md, step
4). The rows `agent, proposal, review` and `agent, proposal, accept` run the plan of rooms
(five rooms, five corridors, four creatures: about 650 cells) on the largest map, 512×512,
then review and scrap it, or accept it and undo. Median of three, the desktop, 2026-10-10:

| piece | measured p50 | speed of light | gap, and why |
|---|---|---|---|
| the copy (`map.copy`, 512×512) | 17.6 µs | about 22 µs (786 KB at 28 µs/MB) | none: at the floor |
| the diff (`mapdiff`, ~650 cells) | 13.7 µs (the scenario's own trace, 2026-10-09) | about 1.5 µs (two reads a cell) | about 17-20 ns a cell, the rate *Proposals* above measured (block sort, the dedupe hash); accepted there |
| the summary and the `#N` corner | about 4.5 µs inside `job.propose` | well under 1 µs | each walks the set's cells once: two walks where one would do. A finding, not fixed: a proposal comes seconds apart |
| a review frame (`job.preview`) | 0.6 µs | the cells in the window, swapped twice | none |
| the accept (`job.accept`) | 11.5 µs for the heavier `agent, room + 12` | applying the cells through the log | none: one undo op a cell |

**The whole request.** The plan of rooms costs about 30 µs a request more as a proposal
accepted at once (`agent, plan of rooms`: 42 µs a frame before, 73 µs now), and the 40×40
room with a dozen creatures about 46 µs more (53 to 99 µs; 1,760 cells, so its diff and
accept are the larger). That is the price of one road in (decision 6): every change is made
on the copy, compared, then applied, where it was once applied straight. Running a request
straight onto the map when it would land at once anyway would save it, at the cost of a
second road that no review ever sees; at under 0.1 ms a request, it stays.

**Nothing on the frame path.** With a proposal waiting, a frame costs its tint (*The jobs'
tints*); in review, the swap above. The rows' frames are 16-17 µs at p50.

### Events and `wait`

An agent is told what happened through a held request, `wait` (docs/CONFLICTS.md, step 5).
The desktop, median of three; the live checks 2026-10-09, the rows 2026-10-10.

| path | measured | speed of light | gap, and why |
|---|---|---|---|
| a held `wait`, idle | no CPU in 3 s (a live vtt, `/proc/PID/stat`), before and after an edit | nothing | none: one descriptor in `poll`, asked for no events, and the timeout is its own deadline |
| answering a waiter after a key | 1 ms from the key to the client's exit, the client's start-up included (a live vtt) | a `send` (about 3 µs, the unix round trip) | none in vtt: the flush runs the turn of the loop the key was handled in |
| the flush with no agent, every turn of the loop | not a row: a scan of 8 connections and 16 jobs | nothing | tens of nanoseconds; the price of answering after the key without a flag every job site must set |
| the map-changed event (`checkpoint.read`): a dozen edits on 512×512 | 1.6 µs p50, 2-4 µs p99 | the changed block and the small parts, about 1 µs | the small parts are compared whole (areas, links, notes, rolls, clocks), as *The checkpoint* found |
| the checkpoint starting again (`ctl.checkpoint`) | 0.1 µs | a byte a block | none |

`agent, map changed event` is an agent listening, a dozen of the GM's edits, then the map
quiet: its frame (22.7 µs) is `build, open`'s (23.0), so the checkpoint running under the GM's edits
does not show on the frame. `ctl.wait`, the zone round answering waiters, has no row: the
bench has no socket to hold one on; the live check above stands in for it, and is written
out in docs/CONFLICTS.md's *Progress, step by step*.

### Reads that cost what was asked

2.0's step 4 (docs/V2.md, D7; 2026-10-12). An agent told "the map changed" used to read the
map again: on the largest map that is `agent, dump 512x512`, 5.3 ms inside one turn of the
loop and half a megabyte of answer (HEALTH 51). Three things now stand in its way. A `dump`
that names no region is refused above 4,096 squares, so the whole map is asked for by name
(`dump all`); `dump` no longer carries its 330-byte key (`dump key`); and `changes SEQ`
answers with the map only where the events since `SEQ` changed it. An event keeps where its
change was as a bit a piece of the map cut sixteen by sixteen (32 bytes, `cs_where`), so
`changes` reads no change set and diffs nothing.

The desktop (`Machine:` above), the medians of three runs of `make perf` on a quiet machine,
2026-10-13.

| path | measured | speed of light | gap, and why |
|---|---|---|---|
| `dump` of 32×32 squares of the 512×512 map (`agent, dump a region`) | frame p99 51 µs against 22 with no request: about 29 µs | the 4.2 KB it writes, about 1 µs, and one allocation | the lattice is made a character at a time through `corner_char`, `hedge_char` and `vedge_char`, and the creature list is walked whole for a region of it. Left: 30 µs is a thousandth of the frame budget, and 190 times less than the dump it replaces |
| `changes` after a dozen edits (`agent, changes read`) | frame p99 51 µs: about 29 µs | the events kept (32 lines) and one piece's lattice, about 4 KB | the same writer, so the same gap |
| keeping where an event's change was (`cs_where`), a dozen edits | inside `checkpoint.read`'s 1.6 µs: unmoved | one bit a changed block | none |
| the same for a room of 40×40 and a dozen creatures landing (`agent, room + 12`: about 1,700 changed cells on a 200×200 map) | `job.propose` 40.8 µs where it was 37.3: about 3.4 µs, 2 ns a cell | reading the cells once, 13.6 KB: about 1 µs | a map under 256 squares a side has pieces finer than a change set's blocks, so each cell is looked at: two clamps and four compares against the last cell's piece, two divisions when it leaves it. Left: 3% of the row. **As first built it was 11 µs** (48.4): every cell was divided out and clamped afresh. Three runs of `make perf` showed it, and an A/B of five commits' binaries put it on the reads' commit (97 to 100 µs a frame on each before it, 109 to 111 on it, 101 to 103 with this) |
| a region `changes` lists and does not send | not timed: written aside and thrown away, at most one an answer (once one is listed the rest are, unread) | nothing | one region's dump, about 30 µs, for deciding on its real bytes and not an estimate that a crowd of creatures makes 2.6 times too small |
| a whole-map `dump all` (`agent, dump 512x512`) | the request 5.5 ms at the median, its frame 6.0 at p99 | 530 KB written | (`ctl`, three runs of 100, as the row below; *Cost per call*'s `ctl` row is `describe`'s) nothing asks for it by accident now. It is the most one request may read (`CTL_READ_SQUARES`, docs/V2.md, D5), and four ready together are four turns with the keys between, not one turn of 22 ms; the bench has no socket to show that on, so suite `loop` holds it (HEALTH 51) |
| a whole-map `describe`, and `check` (`agent, describe 512x512`, `agent, check 512x512`) | the frame the request lands in 3.7 and 3.2 ms at p99 (the rows; the request is all of it), where it was 11.5 and 9.9; finding the rooms is 2.2 ms of each (`rooms.build`) | the squares read once, 262,144 of them, and a room's number written for each: under 1 ms | **was a finding (HEALTH 70), now found.** Both flood every square for the rooms first, and the flood went through `map_walkable`, `map_tile` and `map_vedge`, a call and a bounds check each, a million times, and divided an index back into x and y for every square: 5.5 ms of each request by a profile. `describe` then walked the room's box asking each square's four edges the same way (3 ms), and `check` called a function for every edge of the map, nearly all of them none (1.5 ms). All three read the arrays now, and the flood keeps x and y on its stack. What is left over the floor is the flood's own shape: a stack, each square pushed once and its four neighbors tried, 8.5 ns a square. A fill by runs would halve it; a request three times cheaper than it was, and now under a dump, does not ask for that yet. The flood's four neighbor tests are written out four times: put in one inlined helper, as its review asked, the walk went from 2.2 to 3.0 ms (the two binaries turn about, three times each), so the copies stay and the code says why |
| `check` of a map with 500 creatures (`agent, check 500 creatures`, 200×200) | 0.67 ms the frame at p99; building the two indexes that pair the creatures off is 22.5 µs of it (`check.pairs`; the look-up for each creature after is not in the zone, and the scenario's creatures share squares and no labels) | each creature's squares once, and its label | **was the square of the creatures** (HEALTH 71): every one compared with every other for a shared square and a shared label, 125,000 pairs here and 34 thousand million at one a square on the largest map, which ran over two minutes. Now a chain a square and the labels sorted: 0.18 s for that map, load and all (one run, not the bench), and 0.04 s for 50,000 where the old walk took 0.78 s for 20,000 |
| a read that walks nothing, written as often as 64 KB holds | `help` 12,800 times: 43 ms and tens of megabytes (one run of five, before the cap); `status` 6.0 ms, `marked` 3.6, `jobs` 3.1, `links` 2.1 | nothing asked for twice is needed twice | once an answer has passed 2 MB no further line is run (`CTL_ANSWER_CAP`), which stops `help` at its 847th line, about 3 ms by the same rate (not timed again); the others stay under it and under the budget, and are left. One line's answer is never refused for its size |

### A terminal that stops reading

The GM's terminal can stop taking output: an ssh that stalls, ctrl-S, a tmux client gone
with nothing reading. Until 2.0's step 2 (docs/V2.md, D4) the first frame it would not take
stopped the whole program in `term_write`, phones and agents with it. The desktop, a live
vtt on a pty nobody read, 120 help pages toggled 10 ms apart, 2026-10-12 (the machine not
quiet; these are seconds against milliseconds, and a kernel wait channel).

| path | before | after | speed of light | gap, and why |
|---|---|---|---|---|
| `vtt --ctl status` while the terminal is not reading | no answer in the client's 2 s; the process in `wait_woken` (the tty write) | 0.00 s; the process in `poll` | the 1 ms a request costs | none |
| memory held for a terminal that is behind | none (it waited instead) | at most the one frame that did not fit (suite `loop`: not a byte more a hundred frames later) | the bytes the terminal has not taken | none: later frames are dropped whole, not queued |
| catching up when it reads again | the frames it missed, in order, each waited for | the queue, then one diff from the last frame sent to the screen as it is | the cells that differ | none |
| a turn with the terminal keeping up | -- | two tests of a length that is 0 (`loop_once`, `rnd_flush`) and a descriptor of -1 in `poll` | nothing | nanoseconds; not a row (`--bench` runs with no terminal) |

The output is a second open of the same terminal, non-blocking (`term_async`): `O_NONBLOCK`
on stdout itself would set it on stdin, one open file description. `term_write` queues what
will not go and the loop sends it when the descriptor polls writable (`term.flush`). While
anything is queued `rnd_flush` drops the GM's frame whole and leaves `front` alone, so the
renderer's "front is what the terminal has been sent" still holds and the next frame is one
diff however many were skipped. The players' frame and the agents' requests go on meanwhile.
`--bench-stall`, which the plan promised, was not built. It could be (a `Term` over a pipe
nobody reads, as the suite's case makes), but what it would time is one length test and one
`write`; the suite's case and the live check above stand in for it.

### Other writers of the file

vtt keeps the open map's file as it last read or wrote it, looks at the file on a key, and
makes someone else's change a proposal (docs/CONFLICTS.md, step 7). `tools/proposals.sh`
times the pieces (the desktop, 2026-10-10: the median of 51 in a run, and of three runs):

| map | stat, a key | read + parse the file | parse the base | compare | all, once a change |
|---|---|---|---|---|---|
| 40×25, 24 creatures, 3 KB | 0.78 µs | 28 µs | 23 µs | 1.2 µs | 53 µs |
| 512×512, 24 creatures, 771 KB | 0.81 µs | 2.81 ms | 2.76 ms | 42 µs | 5.62 ms |
| 512×512, 500 creatures, 787 KB | 0.81 µs | 3.08 ms | 2.98 ms | 54 µs | 6.12 ms |

| path | measured | speed of light | gap, and why |
|---|---|---|---|
| the look, on a key, at most once a second | 0.8 µs in a loop; 2.3-3.1 µs p50 as the bench meets it (`disk.check` in `build, outside change 512x512`: the file was just rewritten, so its entry is cold; 21-23 µs at p99) | one `stat`: 0.9 µs | none in the call; the bench's figure is the cache |
| keeping the base at a load | nothing added: the file is read whole into the buffer that is kept, and parsed from it. An A/B of the two loaders on one 512×512 file, 51 loads each, three times: 3.0-3.2 ms before the step, 2.6-2.8 after | a copy of the bytes, 25 µs for 789 KB | none; under it, since the copy is the read. *Cost per call*'s `mapio.load` row is not this comparison: until 2026-10-10 it was one cold load at startup (3.3 ms before and after), and it was then the 799 loads of `build, outside change 512x512`, each into new pages (2.8 ms). (As first built it was 5% **slower**, 3.3 ms: `file_read` grew its buffer by doubling through stdio and copied the file three times. It sizes one buffer from the file and reads once now. The first figure published here, "3.2 to 2.7", had set a cold single load against a warm median, and was wrong when it was written.) |
| keeping the base at a save | nothing added: the text written is the buffer kept | the same | none |
| an outside change, once, when the keys are quiet | 5.6-6.1 ms on the largest map by the tool; 5.4 ms p50, 5.8 ms p99 in the bench (`disk.reload`, the row `build, outside change 512x512`: another program rewrites the file every loop, no creatures). About 0.55 ms of it is new pages, below | the plan put it at two parses (about 8.4 ms by the laptop's loader) plus the compare | the two parses are all of it, and the loader's 3.4 ns a byte is the gap (*Known gaps*). Parsing the base again, where a parsed copy could be kept, is the plan's choice: a megabyte held all session to save 2.7 ms a change |
| the compare of two whole maps | 42-54 µs | 59 µs a MB estimated | none |

**A reload pays for new pages, about 0.55 ms of its 5.9 (found 2026-10-10).** Each outside
change parses two whole maps and frees them. When the freed megabytes are at the top of
the heap, the allocator gives them back to the system, and the next reload's arrays fault
every page in again. An A/B showed it: the build before the review's fixes read 5.3 ms and
the one after 5.9, and with the allocator told to keep its memory
(`MALLOC_TRIM_THRESHOLD_` and `MALLOC_MMAP_THRESHOLD_` at 1 GB) both read 5.35. The
earlier build was faster by accident: a file's job counted as an agent listening, so a
checkpoint was allocated above the maps and the heap's top was never free. That checkpoint
is gone (a file's job is nobody's to hear of), and 5.9 ms is what a GM with no agent
meets. Keeping the two scratch maps between reloads would take the 0.55 ms back at the cost
of 2 MB held for the session; outside changes are rare, so it is left (HEALTH.md B12).

No timer: a vtt nobody is typing in never looks. The read waits for 1.5 s without a key
(`app_disk_due` feeds `poll`), so the 5 ms does not land under an ordinary keystroke. It
does under `:w` and `g o` when the file has just changed: they look, and read, before they
write.

**The Options screen** (docs/APIAGENT.md, ROADMAP 26; the desktop, 2026-10-20, the median of
three quiet runs): `menu, options` is 9.7 µs at p50 a frame and 274 bytes, `draw.options` 6.3 µs
at p50 and 8.3 at p99 -- a dozen rows drawn whole, against an estimate of a few hundred bytes
to the terminal: met. What is costly to learn is read when the screen opens, not in the zone:
the key's source (a `secret-tool lookup`, a keyring round trip) and the usage file. The scenario
runs with no keyring tool, so its opening costs only a failed `PATH` search; with GNOME Keyring
it is one D-Bus round trip an opening, never a frame. The agent itself adds nothing to a frame,
a key or the loop: it is another process.

**Two rows moved in this recording, both writes of the 512×512 map's file:** `mapio.write`
under `build, :w full 512x512` 2.6 ms to 3.2 ms (+23%), and `build, outside change 512x512`
17.1 to 20.4 µs (+19%). Nothing in ROADMAP 26 touches either path, and the A/B says the code did
not move them: the binary before it (b51b030) and this one, `tools/vs1x.sh b51b030`, three
runs each alternating on the repo's disk (2026-10-20): `outside change` 17.2 against 17.0 µs,
`mapio.write` 3.18 against 2.49 ms, `mapio.fsync` 2.60 against 2.05 ms -- the old binary as
slow as the recording, or slower. That much is shown. Why the rows moved is not: the save is
mostly `fsync` (about 80%), and the flush moved by most of what the save did, which points at
the disk, but one binary's spread from run to run was not measured, and the part of the save
outside the flush moved too (0.58 against 0.44 ms). `outside change` reads and never flushes;
its 20.4 µs did not come back in the A/B, cause unknown. What would settle it: one binary,
ten runs on the repo's disk and ten on tmpfs -- if the disk's spread covers the move and
tmpfs's does not, it is the flush.

**The `Disk` seam** (docs/V2.md, *The `Disk` seam*; the desktop, 2026-10-20): the build before
it (027fef2) against this one (59544ce), `tools/vs1x.sh 027fef2`, three runs each alternating
on the repo's disk. The plan's estimate was six indirect calls a save, nanoseconds. Measured:
`mapio.write` 3311.1 against 2493.2 µs and `mapio.fsync` 2631.6 against 2061.6 µs at p50, the
new build faster; `disk.check` 1.9 and 1.9 µs; no scenario row's p50 moved past a tenth. So
the seam's cost does not show; the save's own swing between builds is the flush, as before,
and it went the same way as this morning's A/B (the older binary slower both times). Why the
older binary is slower twice is not known; one guess, unchecked, is the order the script runs
them in. One zone moved the other way: `mapio.sections` (`:check, every edge 512x512`, 81
samples) 74.5 against 216.8 µs at p50, but 514.4 against 276.5 at p95. The seam did not touch
it (it reads text already in memory; `git diff 027fef2` changes no line of it), and its median
and p95 moving opposite ways points at two populations of samples whose middle falls on either
side from run to run; that reading is a guess, not shown.

**`:check` on a map with a finding a square** (review 4's M2; the desktop, 2026-10-20, the
walled 512x512 of 262,145 one-square rooms, `ru_maxrss`, one run each against the commit
before): `:check` in the app 255,820 KB before, 85,584 KB after; `--check` 116,536 KB before,
60,176 KB after, its report 20,385,921 bytes before and 770,265 after. Two changes: a finding's
message and place are strings of their own length (they were 336 bytes inline in each of the
262,145), and the report lists 10,000 of a code and counts the rest (the user's choice). The
review's speed of light is about 30 MB; the gap is two small allocations a finding
(about 524,000 a `:check`, each with malloc's overhead) and the array's doubling, accepted:
only a map that is a finding a square pays it, and real maps have tens.

**`:ask` starting an agent** (`:agent command`; the desktop, 2026-10-13): `agent.spawn` is
92.5 µs at p50 and 175 µs at p99 (`build, :ask starts an agent`, a 200×200 map), once an
`:ask`. It was 0.36 ms in 1.x, which forked twice and waited for the first child so that
the agent was nobody's child; 2.0 forks once and is the agent's parent (docs/V2.md, D8), and
an A/B of the two binaries on one quiet machine gave 357.6 against 92.5. The floor is one
process started, some tens of microseconds with `posix_spawn`, which is not used because
the child must call `setsid`; what is left above the floor is `fork` copying this
process's page tables. It is on a key the GM presses to hand work to a program that will
take seconds. **Reaping** (`agent.reap`) is 0.7 µs at p50 and 3.0 µs at p99 a tick while
runs are alive, a `waitpid(WNOHANG)` for each (up to sixteen in this scenario, which asks
and removes a job every loop), and nothing with none; the estimate was 0.2 µs a run. The
scenario's frames now hold the tick while an agent runs, as the real loop does: 22.9 µs at
p50, as before, and 74.8 µs at p99 where the checkpoint is started for the agent that will
listen (`ctl.checkpoint`, 14.4 µs, once an `:ask`).

**What changed under a proposal** (`job N conflicts`, docs/V2.md D8; the desktop,
2026-10-13): `job.conflicts` is 9.2 µs at p50 and 11.7 µs at p99 (`agent, job conflicts`:
1,024 squares proposed on the 512×512 map, every one changed since, and the GM editing each
loop so the set is checked against the map afresh). The estimate was 7 µs: a nanosecond a
cell for the check and about 6 µs for the 64 lines and the count it writes. The two
microseconds over are `fprintf` into the answer's buffer with a square's name made for
each line; not worth a hand-written formatter for a request an agent makes once a proposal.

**The run of 2026-10-13, and what it moved.** Three rows differed from the tables before by
more than a tenth, and each was taken against the binary of the commit before the work
(77d1ec7), three runs of the whole script and then six of the three scenarios alone,
turn about. `picker` (`play, 500 characters`) read 20.1 µs once and 16.3 to 17.8 in every
other run of either binary: one run's spike. `build, outside change 512x512` has two
values for its frame's p50, about 18.5 µs and about 30 µs, and both binaries show both (the
old one 18.4, 18.6, 21.0, 31.4; the new 18.3, 18.5, 27.0, 28.0, 31.9): which one a run
lands on is not the code's, and the table's 33.3 is two runs of three landing high. It is
not explained (HEALTH 77). `undo.step` (`build, fill+undo 200`) was 150 µs in the old
binary and 164 in the new, every time; with both built `-falign-functions=64
-falign-loops=32` they are 140 to 142 µs each, so it is where the loop fell in the file
and not the tags the log now carries (*The checkpoint* found the same 9% the same way). The prose
elsewhere on this page was brought to the tables of the run before (HEALTH 67); of the rows
it quotes only these three moved by more than a tenth, and `undo.step`'s figure is changed
where it is quoted. The rest differ from today's tables by a run's noise, a few percent. The tables were
regenerated once more that evening for the rooms and the creature pairs (below, *Reads that
cost what was asked*): `build, outside change 512x512` landed at 23.5 µs this time, between
its two values (HEALTH 77), and `picker` back at 17.4.

**`World` out of `App`** (docs/V2.md, D2; the desktop, 2026-10-13): the request handlers
were turned from the whole `App` to a `World`, reaching the GM's screen through a table of
function pointers. The plan's estimate was no cost: the same loads from a different base,
and an indirect call where there was a direct one, some three a request. Measured by an
A/B of the binary before the move (079206f) and after it (27c2181), the whole script three
times each, turn about, medians: `ctl` (a `describe` of the largest map) 10,830.6 against
10,895.6 µs; `job.propose` 40.5 against 40.8; `job.conflicts` 9.1 and 9.1; `agent.spawn`
92.6 against 91.8; `undo.step` 164.0 against 163.9; and every agent scenario's frame within
3% (`agent, room + 12` 98.6 and 98.9 µs, `agent, proposal, accept` 16.2 and 15.9). Nothing
moved, so the tables above stand as they were regenerated the same day.

**`--apply` finding the map's holder** (step 6; the desktop, 300 runs a figure, the median of
five): a failing plan on a file nobody has open takes 1.06 ms with no vtt running and 1.17 ms
with one, so asking a running vtt costs about 0.1 ms. The plan's estimate was 3 µs, the
round trip between two processes already running; this one wakes a vtt asleep in `poll`
(on the powersave governor), and the wake is the cost. A liveness probe before each ask woke
it twice (0.2 ms a vtt); it is gone. It is a tenth of what starting `vtt --apply` costs at
all, once a plan.

**The bench's tick.** Rows run with `--bench-ctl` now tick the app once a loop, inside the
request's frame, on a clock that lets the map go quiet. With a job open (`agent, proposal,
...`) that puts a `checkpoint.read` in each loop's request frame, about 2 µs of its p99.

### The file's form (2.0, step 5)

A row holding nothing is written as an empty line, and every map as version 14 (docs/V2.md,
*Step 5, the file format*). Measured 2026-10-14 on the desktop: the commit before the step
(51deec3) and after it (c2d3704), `make perf` three times each, alternating, and the two
`:w` scenarios traced alone three times each (400 saves a run, the median of each run, the
median of the three):

| path | before | after | speed of light | gap, and why |
|---|---|---|---|---|
| `:w` of an empty 512×512 map | 789,068 bytes; 3.70 ms (`mapio.write`), of it 2.82 ms the flush | 1,613 bytes; 2.61 ms, of it 2.15 ms the flush | 2 KB written and one flush: the flush of a page is 2.0-2.1 ms on this disk (the 40×25 row of the save table) | 0.45 ms of opening, writing, closing and renaming beside the flush, the file system's; the build of 1.6 KB is microseconds |
| `:w` of a full 512×512 map (walls everywhere) | 789 KB; 3.39 ms, 2.83 the flush | 789 KB; 3.40 ms, 2.80 the flush | the same bytes, one flush | the scan that finds a row of nothing (the plan's 0.05 ms) is inside the noise: 0.60 ms beside the flush after, 0.56 before |
| an outside change of the largest map (`disk.reload`, `build, outside change 512x512`) | 5.35 ms p50; the row's frame p99 5.59 ms | 2.64 ms p50; 2.73 ms p99 | two parses of what the file holds | both files are vtt's writing, now 6.8 and 29.5 KB where each was 789: the parses follow the rooms, not the canvas (*Other writers of the file* has the whole-file figures) |
| the autosave of the save table's 512×512 | 1.18 ms, 789 KB | 0.98 ms, 527 KB | the bytes | none |
| a load | 2.6 ms for the empty 512×512 as 789 KB | an empty line is a line read; rows are zeroed already | the bytes | none |

No other row of *Frame times* or *Cost per call* moved by more than 15% at its median
between the two binaries; tails moved both ways on figures of tens of microseconds, as
they do between runs of one binary. `mapio.load`'s row in *Cost per call* now names the
startup load of `build, :w full 512x512` as its heaviest: the outside-change scenario's
loads, which it named before, are of small files now.

### The line search (2.0, step 5's second piece)

`--check` finding a line added, repeated or lost in a row section (docs/V2.md, *Step 5,
second piece*). Measured 2026-10-17 on the desktop with a harness that calls
`maptools_check` 20 times a run under the profiler, three runs, the median of the run
medians; before is f6ae5aa, after is the never-a-wrong-line change. Two 512x512 maps saved
by vtt (an empty one, and 40 walled rooms with a void bottom row), and the rooms map with a
floor row repeated in `tiles` (the two readings are weighed) and with a `vedges` row lost
(the answer is weighed against a loss hidden under `hedges`' last blank, twice: two
sections end in one).

| path | speed of light | before | after | gap, and why |
|---|---|---|---|---|
| `mapio.sections`, the scan by headers (434 KB) | each line's first word once | 25 us | 25 us | none |
| `check.shift` on an undamaged file (both maps) | nothing to search | 0.1 us | 0.1 us | none |
| `check.shift`, a row repeated (rooms) | the three sections' squares read once, about 0.3 ms (the plan) | 13.8 ms | 9.3 ms, of it 4.7 the two readings (`check.joint`) | below |
| `check.shift`, a row lost (rooms) | the same | 9.3 ms | 17.1 ms, of it 2 x 5.2 the hidden-loss weighing | below |
| `check.fold` | the file parsed and checked once more (the plan: about 6 ms) | 3.7 ms | 3.6 ms | none |
| `mapio.load` | the bytes | 1.07 / 1.9 ms | the same | none |

**The search scores a band, not a byte.** A band reads five rows of 512 squares and
counts walls on void, loose ends and open edges with a branch or two a square: 1.5 us a
band on this machine, where the plan's 0.3 ms reckoned a read of the bytes. Without the
weighing the search reads about 3,000 bands on these maps: the band sums as read and one
off (1.6 ms), the candidates' seams (0.2 ms for a line too many, 2.6 ms for a lost row,
which has three rows to try at each place), and two whole-map scorings of the answer to
tell what its fix mends (2.3 ms). That last pair was done twice, once to decide and once
to tell; now once (the 13.8 -> 9.3 above, with the lone-witness search's removal). Left as
a finding: per-part running sums would give the answer's two scorings without a pass.

**The weighing costs three whole-map scorings and what the seams need.** `shift_joint`
scores the map with each of the two sections as read or moved (four running sums, about
2,000 bands), then weighs every pair of places but reads a seam only when the running sums
alone leave the pair within the limit, and stops at the first pair that fits. As first
built it read every seam and every meeting pair to find the least: 13.4 ms and 2 x 27.5 ms
here. One of the four sums (both sections as read) is the map as read and is the same in
every call of a check; sharing it would save a quarter, not done.

All of this is for a damaged file under `--check`, on the largest map; an undamaged file
pays nothing. Machine: Intel Core i7-8700K @ 3.70GHz (6 cores, 12 threads, 4.7 GHz max,
powersave governor), 16 GB RAM, Linux 7.2.5-3-omarchy, gcc 16.2.1 -O2.

### What an open says of its file (2.0, step 5's second piece, part 2)

Measured 2026-10-17 on the desktop: three `make perf` runs of this tree on the repository's
own disk, the median of each row, against the tables committed before (the same disk).
An A/B against the previous commit's binary was run too, but from a worktree under `/tmp`,
which is in memory: there a flush costs nothing, so its rows that write (`mapio.write`,
`mapio.fsync`, `link.trip.map`) are not comparable, and were run again on the disk, both
binaries alternating: the trip 71.8 us against 67.5 us (+6%: the destination's findings
read in the same load), the save the same to the microsecond.

| path | speed of light | before | after | gap, and why |
|---|---|---|---|---|
| an open of a clean file (the sink on, the section scan) | the file's bytes parsed once | today's load | the same load, plus `mapio_sections`: 0.22 ms on a 512x512 file, microseconds on a small one | the scan reads every line start once, which the loader has read already; kept apart so the loader stays one table-driven pass |
| an open of a damaged file (`open.file`, a row repeated, walled on every edge) | the line search and its fold | -- | 6.9 ms | the search and the fold of a 512x512 map: only a damaged file, once |
| the channel's `check` on the largest map (`ctl`) | the file's bytes parsed once, the map's checks | 5.4 ms (`dump`, then the heaviest) | 6.0 ms, now the heaviest request | the parse of `Map.base` for the file's findings, about 3 ms on the largest; written down, not kept: HEALTH 84 |
| `:check`, rooms 512x512 (`app.check`) | as `--check` | -- | a key's worth (the frame row: 77 us p99, the work in `input.key`) | -- |
| `:check`, walled on every edge (`app.check`) | 262,144 findings made and printed | -- | 0.40 s | a finding a square, each a line of the report: the worst a report can be. As first built it took over 100 s: the live findings were matched against the file's fold by a scan each, quadratic, which a sort and a binary search replaced |

**The section scan tested every line's characters.** As first built, `mapio_sections`
marked each line `row` by a test of every character: 2.7 ms of a full 789 KB file. Only the
lines past a section's rows are ever asked, and are now the only ones tested (0.22 ms).
**And the open-map report checked the map the bytes make every time**, which only the fold
needs: now only when the line search finds something. Both found by this run.


### Every finding to the standard (2.0, step 5's second piece, part 3)

Measured 2026-10-18 on the desktop: `vtt --check` end to end (the report printed), three runs
each of the commit before part 3 (975d383, built in a worktree on the repository's disk) and
after it, on two 512x512 maps walled on every boundary -- one with no floor (525,312 walls on
void), one with floor everywhere (262,144 one-square rooms, each a W120).

| map, report | speed of light | before | after | gap, and why |
|---|---|---|---|---|
| no floor, text | the walls read once; one finding to print | 0.35 s, 40.7 MB, 525,313 lines | 0.24 s, 279 bytes | the 0.24 s is the load and 525,312 findings made, then joined by stretch at the report (HEALTH 87: joining boundary by boundary would skip making them) |
| no floor, JSON | the same | 0.71 s, 133 MB | 0.24 s, 618 bytes | as above |
| all floor, text | 262,144 findings printed | 0.226 s, 20.4 MB | 0.232 s, 20.4 MB + one help line | 23 ns a finding: the help's lookup and the stretch pass's scan of every finding; kept |
| all floor, JSON | the same | 0.41 s, 68.0 MB | 0.42 s | the same, and the `codes` object |

### `App` in its parts (2.0, step 6, part B)

Machine: Intel Core i7-8700K @ 3.70GHz (6 cores, 12 threads, 4.7 GHz max, powersave governor),
16 GB RAM, Samsung SSD 860 EVO 1TB (btrfs), Linux 7.2.8-9-omarchy, gcc 16.2.1 -O2.

Part B moved `App`'s loose fields into eleven parts and the proposal code into its own file:
no path gained or lost work, so the plan's estimate was "the same". Measured 2026-10-21: the
build before it (579eda9) against d20eb6a's code, both rebuilt with `-falign-functions=64
-falign-loops=32`, `tools/perf.sh` three times each, alternating, on the repo's disk; per-row
medians by `tools/median.py`, side by side by `tools/compare.py`.

- **Frames:** all 100 rows within 5% at p50; the median ratio 1.000. Bytes the same but two
  rows (`play, named roll` 164 and 165, `profiler overlay` 1,142 and 1,104; my guess, not
  checked: the overlay prints timings and the roll is random, so their text varies by run).
- **Cost per call:** 82 rows, median ratio 1.000. The rows that moved past 5% compare different
  scenarios: the column is each zone's heaviest, and which scenario that is changed between runs
  (`app.draw`, `mapio.write`, `mapio.load`, `net.accept`, `range.status`, `floor.switch`).
  `fog.sight.rim` read 4.4 µs before and 6.0 after, with a different scenario named in all three
  new runs; that scenario alone (`play, fog pings, 4 watchers`), five runs a binary alternating:
  6.09-6.22 µs before, 6.06-6.26 after, frames 58.8-60.4 and 59.3-61.1 µs. Not the change.
  `scene.save` (8.0 to 9.1 µs in two of three runs, 8.1 in the third) and `map.copy` (two calls
  a run) are within what one run's spread shows; my guess is noise, not shown: a rerun of each
  scenario alone, as for the rim, would settle it.


### A phone that falls behind (2.0, step 7)

Measured 2026-10-21 on the desktop (the tables above), and on the user's phone over the Wi-Fi
with `tools/phonecheck.sh`.

**The lag bound holds, and costs nothing a frame.** `play, a phone that falls behind` (four
watchers, one reading only once vtt withholds frames from it) runs at 38.9 us a frame against
39.2 us for `play, 4 watchers`. In 800 loops vtt caught that watcher up ten times and never held
more than 8,038 bytes for it, under the bound: the FULL of the screen then, 8,086 (the review
found the first message paired the peak with the last FULL, 7,966; it prints the FULL at the
peak now). Before 2026-10-21 the row's watcher never read at all, and at 80x24 it never fell
behind: 400 loops (3,200 frames of about 100 bytes) ended with nothing queued by vtt, the kernel
holding them all. How many frames the kernel takes varies: the first fall behind came near
frame 3,000 in my run and at loop 311 (about 2,490 frames) in the reviewer's, on the same
machine; `tools/sockcap.py`'s 296,960 bytes is for 4 KB writes.

**A catch-up is a FULL's encode.** `net.catch_up` is 22.9 us (p99 36.3), of which the encode,
`net.enc_full` (2026-10-21; it covers a join's FULL and a resize's too, untimed before), is 19.1
us; queueing and the write are the rest. The plan's estimate, one FULL as a joining phone costs,
holds. The encode itself is a finding: 1,920 cells into 7,966 bytes at about 10 ns a cell. The
least it could cost is reading the cells (30 KB, 16 bytes a `Cell`) and writing 8 KB, about 1 to 2 us (my estimate
from memory bandwidth, not measured). A one-entry cache of the last foreground and background in
`wire_enc_cell` (each cell does two palette hash lookups today) took it from 20.0 to 17.3 us
(-14%, an A/B of three runs each), so the lookups are not most of it; reverted, as out of this
step. My guess for the rest, not shown: `put8` writes a byte at a time through `e->buf` with a
bounds check, and a `uint8_t` store may alias the encoder, so `len` and `buf` are reloaded each
byte. A profile of `wire_enc_full` would settle it. Every frame to a phone goes through the same
per-cell path (`net.frame` 11 us a frame). HEALTH.md, *Left open by reviews*.

**The send buffer (question 3): 128 KB costs a FULL a few percent, if anything.** Pixel 8 Pro,
Android 17, Chrome 155, 802.11ax at 5 GHz, link 816/1080 Mbit/s, RSSI -51; the page's own clock
from the socket's open to the FULL's arrival (a phone is sent its FULL on joining), the two
buffers alternating, three turns of ten loads each:

| window  | send buffer (ss's tb, largest) | open to FULL, median (range), 30 loads | FULL bytes |
|---------|--------------------------------|----------------------------------------|------------|
| 200x50  | 128 KB (262,144)               | 9.1 ms (5.5-30.9)                      | 42,826     |
| 200x50  | the kernel's (130,560)         | 8.4 ms (5.8-34.5)                      | 42,826     |
| 400x170 | 128 KB (262,144)               | 46.0 ms (36.6-264.2)                   | 299,226    |
| 400x170 | the kernel's (1,122,816)       | 43.8 ms (36.1-645.4)                   | 299,226    |

Two earlier runs, the shipped buffer always first, disagreed: ten loads each read 8.9 against
6.7 ms and 41.9 against 43.0; thirty, 8.4 against 6.9 and 48.1 against 41.5. Alternated, the gap
is 0.7 ms and 2.2 ms (5 to 8%), and the Wi-Fi's own spikes (to 645 ms) are far larger on either
side. The lag bound is what 128 KB buys; kept.

**A phone that goes away comes back right.** The GM pressed `l` ten times a second for 15 s
(and, the first time, `+` and `-`, which redraw every cell but alternate between two screens, so
a stale page could match by luck) while the phone was away; then the page's screen was compared,
cell and color, with the one a fresh load draws:
- *Screen off, then unlocked by hand.* At 200x50 Chrome kept reading: 52 messages while dark,
  the rest live after; the same screen. At 400x170 vtt withheld frames and caught the phone up
  with one FULL, both times: zooming, 8.1 MB arrived while dark and 4.9 MB after, the last 2.7 s
  after the page was visible; with `l`, the FULL came while dark, then Chrome closed the socket
  and the page reconnected as the screen came on. The same screen each time.
- *Chrome behind the home screen, then brought back.* Chrome closes the page's socket about 5 s
  after it goes to the background (`close` 1006, no close frame; the phone's TCP still
  acknowledged vtt's bytes after it). The page reconnected 5.2 s (200x50) and 5.1 s (400x170)
  after it was visible again, then took a FULL; the same screen. Why 5 s is not shown: the
  page's retry is a 2 s timer (web/index.html), each run recorded one close and no failed
  attempt, so my guess is that Chrome resumes the page's timers late; a timeline of the timer's
  firing (phonecheck's `DEBUG=1`, with the timer hooked) would settle it.
  Until then it shows the old screen and "disconnected · retrying". A finding: the page could
  reconnect when it becomes visible rather than wait for the timer; not built (HEALTH.md).

## 2.0 against 1.x

Kept current as 2.0 is built (the user, 2026-10-17): after each step's own timing,
`tools/vs1x.sh` runs `main`'s binary (1.x) and this tree's, `tools/perf.sh` three times each,
alternating, both from a directory on the repository's own disk, and `tools/compare.py`
prints the rows both have. Rows only one has are named at the end (1.x has none 2.0 lacks).
Measured 2026-10-21, after step 7 (the `Conn` seam, the lag bound, frames of any window, the
privacy checks).

**What a key costs is unchanged.** Of the 89 scenarios both have, every frame p50 is within 6%
of 1.x, the median ratio 1.00; the one faster is still `build, outside change 512x512` (0.83x,
step 5). The bytes are the same but in the three rows the last measurement explained (`build, a
job's tint` 302 against 301; `profiler overlay` 1,139 against 1,018; `play, named roll` 165
against 164).

**What moved, and why.** Real, as before: `disk.reload` 3.2 times faster (5.29 ms -> 1.64 ms)
and `agent.spawn` 3.7 times (350 -> 94 us). Nothing new is real. The rows whose last column
names two scenarios compare two different heaviest ones, as before. That is also the answer to
`app.draw`, left unexplained at the last measurement: its heaviest scenario is `profiler overlay`
(about 28 us) in some runs and `build, 200x200 200x50` (about 81 us) in others, their p99s within
10% of each other, and the median takes each column on its own. Shown 2026-10-21: step 6's
binary against step 7's, those scenarios alone, three runs each, gave 28.6 us against 82.6 us,
the direction reversed from the run above, every frame row of those scenarios within 3%.
`names.sync` (1.18x) is one call a run. `undo.step` (1.09x) is the placement the last
measurement showed. `scene.save` (1.10x), `prof.overlay` (1.19x) and the rows from 1.05x to 1.08x
were not checked; under 10% on a tight loop can be placement alone, a guess for these.

1.x is e7c58e9 (main), 2.0 is f4df1f7 (nothing uncommitted).
Machine: Intel Core i7-8700K @ 3.70GHz (6 cores, 12 threads, 4.7 GHz max, powersave governor), 16 GB RAM, Samsung SSD 860 EVO 1TB (btrfs), Linux 7.2.8-9-omarchy, gcc 16.2.1 -O2

| scenario | size | 1.x p50 | 2.0 p50 | | 1.x p99 | 2.0 p99 | | 1.x bytes | 2.0 bytes |
|---|---|---|---|---|---|---|---|---|---|
| build, open | 80x24 | 22.9us | 23.0us | 1.00x | 25.3us | 25.7us | 1.02x | 207 | 207 |
| build, every edge | 80x24 | 22.1us | 22.1us | 1.00x | 25.1us | 35.8us | 1.43x | 207 | 207 |
| build, 200x200 | 80x24 | 22.6us | 22.8us | 1.01x | 25.2us | 25.1us | 1.00x | 209 | 209 |
| build, 200x200 | 200x50 | 89.2us | 88.2us | 0.99x | 142.7us | 119.0us | 0.83x | 212 | 212 |
| build, mostly void | 200x50 | 76.7us | 76.9us | 1.00x | 104.6us | 141.2us | 1.35x | 267 | 267 |
| build, noted squares | 80x24 | 22.7us | 22.3us | 0.98x | 30.7us | 38.8us | 1.26x | 524 | 524 |
| build, tracing | 80x24 | 21.9us | 21.9us | 1.00x | 25.3us | 24.0us | 0.95x | 140 | 140 |
| build, circle brush | 80x24 | 22.6us | 22.8us | 1.01x | 26.9us | 26.5us | 0.99x | 292 | 292 |
| build, 3x3 brush | 80x24 | 19.5us | 19.6us | 1.01x | 21.5us | 23.0us | 1.07x | 128 | 128 |
| build, fill+undo 200 | 80x24 | 19.6us | 19.9us | 1.02x | 64.7us | 64.6us | 1.00x | 3398 | 3398 |
| build, fill history | 80x24 | 20.3us | 20.7us | 1.02x | 76.4us | 76.3us | 1.00x | 4145 | 4145 |
| build, :ask starts an agent | 80x24 | 22.5us | 22.9us | 1.02x | 58.5us | 74.4us | 1.27x | 158 | 158 |
| build, a job's tint | 80x24 | 21.2us | 22.5us | 1.06x | 35.4us | 33.9us | 0.96x | 301 | 302 |
| ruler, three legs | 80x24 | 19.3us | 19.4us | 1.01x | 22.0us | 23.0us | 1.05x | 40 | 40 |
| play, 24 tokens | 80x24 | 21.9us | 22.4us | 1.02x | 24.2us | 25.4us | 1.05x | 298 | 298 |
| play, 24 tokens | 200x50 | 80.0us | 78.7us | 0.98x | 108.6us | 110.7us | 1.02x | 315 | 315 |
| play, carrying | 80x24 | 19.3us | 19.3us | 1.00x | 26.1us | 27.4us | 1.05x | 205 | 205 |
| play, carry 200x200 | 80x24 | 18.7us | 18.9us | 1.01x | 24.4us | 29.0us | 1.19x | 136 | 136 |
| play, 24 on 200x200 | 80x24 | 21.2us | 21.2us | 1.00x | 23.5us | 23.7us | 1.01x | 304 | 304 |
| play, 500 on 200x200 | 80x24 | 26.9us | 27.4us | 1.02x | 40.4us | 48.9us | 1.21x | 299 | 299 |
| play, 3x3 cursor | 80x24 | 24.3us | 24.2us | 1.00x | 27.7us | 27.1us | 0.98x | 606 | 606 |
| play, choosing | 80x24 | 19.9us | 19.7us | 0.99x | 22.5us | 22.3us | 0.99x | 95 | 95 |
| play, group box | 80x24 | 18.5us | 18.4us | 0.99x | 21.6us | 21.2us | 0.98x | 133 | 133 |
| play, group carry | 80x24 | 18.4us | 18.2us | 0.99x | 23.3us | 21.6us | 0.93x | 131 | 131 |
| play, range bands | 80x24 | 24.5us | 24.5us | 1.00x | 47.6us | 46.9us | 0.99x | 657 | 657 |
| play, range radius | 80x24 | 42.4us | 42.4us | 1.00x | 61.0us | 58.0us | 0.95x | 958 | 958 |
| play, range cone | 80x24 | 24.7us | 25.3us | 1.02x | 59.9us | 60.5us | 1.01x | 654 | 654 |
| play, range line | 80x24 | 24.2us | 24.9us | 1.03x | 56.7us | 57.2us | 1.01x | 620 | 620 |
| play, range square | 80x24 | 24.1us | 24.6us | 1.02x | 57.5us | 56.6us | 0.98x | 669 | 669 |
| play, turn order | 80x24 | 24.3us | 24.2us | 1.00x | 33.7us | 33.5us | 0.99x | 488 | 488 |
| play, fight cycling | 80x24 | 31.5us | 32.2us | 1.02x | 52.8us | 80.0us | 1.52x | 4596 | 4596 |
| play, spotlight | 80x24 | 20.9us | 20.7us | 0.99x | 23.3us | 24.1us | 1.03x | 245 | 245 |
| play, named roll | 80x24 | 19.6us | 19.5us | 0.99x | 23.3us | 34.8us | 1.49x | 164 | 165 |
| play, fog | 80x24 | 22.5us | 22.1us | 0.98x | 49.2us | 36.7us | 0.75x | 265 | 265 |
| play, fog all dark, 4 watch | 80x24 | 41.8us | 41.6us | 1.00x | 57.8us | 82.7us | 1.43x | 265 | 265 |
| play, fog by hand | 80x24 | 22.5us | 22.7us | 1.01x | 38.8us | 26.8us | 0.69x | 231 | 231 |
| play, fog range, 4 watchers | 80x24 | 71.2us | 71.9us | 1.01x | 124.6us | 122.9us | 0.99x | 541 | 541 |
| play, fog sight | 80x24 | 22.3us | 21.9us | 0.98x | 47.9us | 47.1us | 0.98x | 565 | 565 |
| play, fog lantern | 80x24 | 20.9us | 21.0us | 1.00x | 45.5us | 49.1us | 1.08x | 438 | 438 |
| play, fog warren | 80x24 | 22.2us | 22.4us | 1.01x | 50.3us | 50.0us | 0.99x | 826 | 826 |
| play, fog warren 12 | 80x24 | 22.0us | 22.0us | 1.00x | 49.2us | 49.5us | 1.01x | 803 | 803 |
| play, fog warren, party | 80x24 | 23.2us | 23.0us | 0.99x | 55.2us | 51.3us | 0.93x | 1673 | 1673 |
| play, fog door | 80x24 | 19.3us | 19.7us | 1.02x | 35.5us | 32.6us | 0.92x | 215 | 215 |
| play, fog full rebuild | 80x24 | 20.3us | 20.2us | 1.00x | 34.9us | 25.6us | 0.73x | 180 | 180 |
| play, fog reveal 12 | 80x24 | 22.1us | 21.6us | 0.98x | 48.2us | 47.2us | 0.98x | 544 | 544 |
| play, fog sight, 4 watchers | 80x24 | 45.5us | 45.4us | 1.00x | 118.2us | 120.6us | 1.02x | 565 | 565 |
| play, fog soft edge, 4 watchers | 80x24 | 44.2us | 44.8us | 1.01x | 88.8us | 82.7us | 0.93x | 420 | 420 |
| build, stamp 20x20 | 80x24 | 16.4us | 16.6us | 1.01x | 38.3us | 35.7us | 0.93x | 1696 | 1696 |
| build, fog paint | 80x24 | 21.6us | 21.7us | 1.00x | 27.1us | 24.8us | 0.92x | 219 | 219 |
| build, 64 links | 80x24 | 26.4us | 26.3us | 1.00x | 54.0us | 30.8us | 0.57x | 274 | 274 |
| play, 64 links | 80x24 | 26.8us | 26.5us | 0.99x | 29.4us | 41.6us | 1.41x | 348 | 348 |
| play, link there+back | 80x24 | 24.8us | 24.6us | 0.99x | 38.4us | 35.8us | 0.93x | 637 | 637 |
| build, one floor | 80x24 | 21.6us | 21.4us | 0.99x | 35.0us | 24.0us | 0.69x | 176 | 176 |
| play, other floor, 4 watchers | 80x24 | 40.2us | 40.3us | 1.00x | 69.0us | 57.8us | 0.84x | 238 | 238 |
| play, floor steps | 80x24 | 21.7us | 21.4us | 0.99x | 24.9us | 36.2us | 1.45x | 290 | 290 |
| play, counters | 80x24 | 21.0us | 21.3us | 1.01x | 46.4us | 46.1us | 0.99x | 402 | 402 |
| play, clocks | 80x24 | 20.3us | 20.5us | 1.01x | 35.1us | 33.1us | 0.94x | 162 | 162 |
| play, 1 watcher | 80x24 | 30.5us | 30.9us | 1.01x | 44.8us | 45.8us | 1.02x | 298 | 298 |
| play, 4 watchers | 80x24 | 38.5us | 38.5us | 1.00x | 54.5us | 43.8us | 0.80x | 298 | 298 |
| play, 4 watchers, differing | 80x24 | 52.7us | 52.6us | 1.00x | 112.9us | 109.5us | 0.97x | 530 | 530 |
| play, hidden, 4 watchers | 80x24 | 55.7us | 55.4us | 0.99x | 70.0us | 85.0us | 1.21x | 298 | 298 |
| play, camera party, 4 watchers | 80x24 | 45.0us | 45.6us | 1.01x | 92.9us | 86.4us | 0.93x | 463 | 463 |
| play, camera hold, 4 watchers | 80x24 | 40.7us | 39.9us | 0.98x | 55.7us | 58.0us | 1.04x | 212 | 212 |
| play, whisper, 4 named | 80x24 | 40.0us | 39.1us | 0.98x | 56.9us | 55.9us | 0.98x | 147 | 147 |
| play, pings, 4 watchers | 80x24 | 46.8us | 48.1us | 1.03x | 89.4us | 79.4us | 0.89x | 978 | 978 |
| play, fog pings, 4 watchers | 80x24 | 60.1us | 61.4us | 1.02x | 92.1us | 70.1us | 0.76x | 696 | 696 |
| play, GM ping | 80x24 | 22.5us | 22.3us | 0.99x | 34.7us | 25.4us | 0.73x | 345 | 345 |
| play, carry, 4 watch | 80x24 | 36.0us | 37.2us | 1.03x | 67.1us | 49.7us | 0.74x | 205 | 205 |
| play, logging | 80x24 | 18.9us | 18.6us | 0.98x | 25.0us | 25.9us | 1.04x | 198 | 198 |
| play, rolling | 80x24 | 19.3us | 20.0us | 1.04x | 35.5us | 24.1us | 0.68x | 191 | 191 |
| play, group effect | 80x24 | 25.9us | 26.4us | 1.02x | 43.2us | 42.5us | 0.98x | 476 | 476 |
| play, card box | 80x24 | 31.8us | 32.0us | 1.01x | 60.6us | 60.7us | 1.00x | 620 | 620 |
| play, damage | 80x24 | 31.7us | 32.0us | 1.01x | 45.4us | 37.1us | 0.82x | 198 | 198 |
| play, stress to zero and back | 80x24 | 22.1us | 22.4us | 1.01x | 30.0us | 34.8us | 1.16x | 376 | 376 |
| play, 500 characters | 80x24 | 28.6us | 28.3us | 0.99x | 53.0us | 44.0us | 0.83x | 628 | 628 |
| play, env adversaries | 80x24 | 19.4us | 19.3us | 0.99x | 43.5us | 34.5us | 0.79x | 340 | 340 |
| play, summon | 80x24 | 18.6us | 18.5us | 0.99x | 41.4us | 38.9us | 0.94x | 257 | 257 |
| play, scene of 500 | 80x24 | 27.9us | 27.9us | 1.00x | 43.3us | 56.5us | 1.30x | 182 | 182 |
| play, map trip there+back | 80x24 | 22.6us | 22.3us | 0.99x | 34.5us | 34.4us | 1.00x | 306 | 306 |
| play, handout typed, 4 watch | 80x24 | 40.3us | 39.2us | 0.97x | 60.2us | 57.4us | 0.95x | 142 | 142 |
| agent, room + 12 | 80x24 | 94.4us | 96.8us | 1.03x | 124.7us | 129.0us | 1.03x | 157 | 157 |
| agent, plan of rooms | 80x24 | 70.0us | 72.6us | 1.04x | 74.3us | 77.0us | 1.04x | 157 | 157 |
| agent, dump 512x512 | 80x24 | 23.1us | 22.7us | 0.98x | 5520.0us | 5650.0us | 1.02x | 138 | 138 |
| agent, proposal, review | 80x24 | 16.7us | 16.8us | 1.01x | 68.8us | 69.9us | 1.02x | 264 | 264 |
| agent, proposal, accept | 80x24 | 15.9us | 15.7us | 0.99x | 69.2us | 71.5us | 1.03x | 241 | 241 |
| agent, map changed event | 80x24 | 22.6us | 22.7us | 1.00x | 25.0us | 25.2us | 1.01x | 279 | 279 |
| build, outside change 512x512 | 80x24 | 20.7us | 17.1us | 0.83x | 5600.0us | 1700.0us | 0.30x | 271 | 271 |
| help page | 80x24 | 37.2us | 35.2us | 0.95x | 52.4us | 51.1us | 0.98x | 2601 | 2601 |
| profiler overlay | 80x24 | 29.1us | 30.7us | 1.05x | 127.9us | 139.5us | 1.09x | 1018 | 1139 |

| path | 1.x p50 | 2.0 p50 | | 1.x p99 | 2.0 p99 | | heaviest in 1.x / in 2.0 |
|---|---|---|---|---|---|---|---|
| agent.spawn | 349.6us | 93.6us | 0.27x | 435.8us | 173.9us | 0.40x | build, :ask starts an agent 80x24 |
| app.draw | 80.6us | 68.8us | 0.85x | 112.6us | 132.2us | 1.17x | build, 200x200 200x50 / profiler overlay 80x24 |
| camera.party | 0.1us | 0.1us | 1.00x | 0.2us | 0.2us | 1.00x | play, camera party, 4 watchers 80x24 |
| card.draw | 8.2us | 37.1us | 4.52x | 10.9us | 63.0us | 5.78x | play, card box 80x24 / :check, every edge 512x512 80x24 |
| character.place | 0.6us | 0.6us | 1.00x | 0.7us | 0.9us | 1.29x | play, 500 characters 80x24 |
| checkpoint.read | 1.5us | 1.6us | 1.07x | 1.8us | 1.9us | 1.06x | agent, map changed event 80x24 |
| clock.draw | 1.2us | 1.2us | 1.00x | 1.2us | 1.3us | 1.08x | play, clocks 80x24 |
| counter.step | 0.5us | 0.4us | 0.80x | 3.0us | 3.2us | 1.07x | play, stress to zero and back 80x24 |
| ctl | 5225.3us | 5924.3us | 1.13x | 5984.5us | 6657.1us | 1.11x | agent, dump 512x512 80x24 / agent, check 512x512 80x24 |
| ctl.checkpoint | 0.1us | 15.7us | 157.00x | 0.1us | 37.7us | 377.00x | agent, map changed event 80x24 / build, :ask starts an agent 80x24 |
| disk.check | 2.2us | 1.7us | 0.77x | 19.7us | 6.3us | 0.32x | build, outside change 512x512 80x24 |
| disk.reload | 5291.2us | 1638.6us | 0.31x | 5705.1us | 1801.2us | 0.32x | build, outside change 512x512 80x24 |
| dmg.apply | 12.5us | 12.7us | 1.02x | 26.5us | 21.4us | 0.81x | play, damage 80x24 |
| editor.draw | 18.4us | 64.3us | 3.49x | 127.5us | 123.8us | 0.97x | build, 200x200 200x50 / build, mostly void 200x50 |
| env.pick | 28.3us | 28.0us | 0.99x | 51.2us | 47.6us | 0.93x | play, env adversaries 80x24 |
| floor.pick | 0.6us | 0.7us | 1.17x | 0.7us | 1.0us | 1.43x | play, 500 on 200x200 80x24 / agent, check 500 creatures 80x24 |
| floor.switch | 0.1us | 0.1us | 1.00x | 0.1us | 0.1us | 1.00x | play, other floor, 4 watchers 80x24 / build, one floor 80x24 |
| fog.blank | 0.4us | 0.4us | 1.00x | 0.9us | 0.9us | 1.00x | play, fog soft edge, 4 watchers 80x24 |
| fog.paint | 0.1us | 0.1us | 1.00x | 1.7us | 1.8us | 1.06x | build, fog paint 80x24 |
| fog.reveal | 2.9us | 3.0us | 1.03x | 5.8us | 3.1us | 0.53x | play, fog range, 4 watchers 80x24 |
| fog.sight | 14.8us | 14.8us | 1.00x | 163.1us | 164.6us | 1.01x | play, fog reveal 12 80x24 |
| fog.sight.build | 15.9us | 15.8us | 0.99x | 111.0us | 111.1us | 1.00x | play, fog warren 12 80x24 |
| fog.sight.rim | 7.9us | 6.2us | 0.78x | 20.9us | 19.3us | 0.92x | play, fog full rebuild 80x24 / play, fog range, 4 watchers 80x24 |
| fog.sight.union | 8.3us | 8.2us | 0.99x | 157.7us | 159.1us | 1.01x | play, fog reveal 12 80x24 |
| grid.draw | 71.2us | 71.5us | 1.00x | 101.5us | 114.5us | 1.13x | build, 200x200 200x50 |
| grid.labels | 2.3us | 2.4us | 1.04x | 18.8us | 18.2us | 0.97x | build, outside change 512x512 80x24 |
| group.box | 0.1us | 0.1us | 1.00x | 0.1us | 0.2us | 2.00x | play, fog warren, party 80x24 |
| group.move | 0.2us | 0.3us | 1.50x | 2.4us | 2.6us | 1.08x | play, fog warren, party 80x24 |
| handout.send | 12.5us | 12.0us | 0.96x | 25.0us | 24.1us | 0.96x | play, handout typed, 4 watch 80x24 |
| input.key | 0.2us | 0.1us | 0.50x | 5660.5us | 389301.4us | 68.78x | play, map trip there+back 80x24 / :check, every edge 512x512 80x24 |
| job.accept | 11.4us | 11.1us | 0.97x | 26.8us | 26.6us | 0.99x | agent, room + 12 80x24 |
| job.highlight | 0.3us | 0.3us | 1.00x | 0.4us | 0.4us | 1.00x | build, a job's tint 80x24 |
| job.preview | 0.6us | 0.6us | 1.00x | 0.9us | 0.8us | 0.89x | agent, proposal, accept 80x24 |
| job.propose | 37.1us | 40.1us | 1.08x | 75.2us | 80.1us | 1.07x | agent, room + 12 80x24 |
| job.review | 11.9us | 12.7us | 1.07x | 16.9us | 15.7us | 0.93x | agent, proposal, accept 80x24 |
| link.marks | 3.8us | 3.8us | 1.00x | 4.1us | 4.2us | 1.02x | play, link there+back 80x24 |
| link.trip.map | 4317.3us | 4185.2us | 0.97x | 8009.2us | 7368.9us | 0.92x | play, map trip there+back 80x24 |
| log.write | 2.8us | 2.8us | 1.00x | 7.0us | 9.0us | 1.29x | play, logging 80x24 |
| map.copy | 17.5us | 370.2us | 21.15x | 37.3us | 370.2us | 9.92x | agent, proposal, review 80x24 / agent, job conflicts 80x24 |
| mapdiff | 57.1us | 55.2us | 0.97x | 70.6us | 89.8us | 1.27x | build, outside change 512x512 80x24 |
| mapio.fsync | 2053.0us | 2014.1us | 0.98x | 5177.1us | 5070.0us | 0.98x | play, map trip there+back 80x24 / build, :w empty 512x512 80x24 |
| mapio.load | 2496.6us | 3115.8us | 1.25x | 2727.8us | 3551.1us | 1.30x | build, outside change 512x512 80x24 / :check, every edge 512x512 80x24 |
| mapio.write | 2132.7us | 2449.9us | 1.15x | 5258.1us | 5510.0us | 1.05x | play, map trip there+back 80x24 / build, :w empty 512x512 80x24 |
| move.label | 0.0us | 0.0us |  | 1.0us | 0.9us | 0.90x | play, fog sight, 4 watchers 80x24 |
| names.sync | 3.4us | 4.0us | 1.18x | 3.5us | 4.0us | 1.14x | play, pings, 4 watchers 80x24 |
| net.accept | 19.9us | 5.2us | 0.26x | 21.9us | 11.9us | 0.54x | play, 1 watcher 80x24 / play, fog soft edge, 4 watchers 80x24 |
| net.cmd | 0.0us | 0.0us |  | 0.1us | 0.1us | 1.00x | play, fog pings, 4 watchers 80x24 / play, pings, 4 watchers 80x24 |
| net.frame | 10.7us | 11.0us | 1.03x | 20.6us | 24.3us | 1.18x | play, pings, 4 watchers 80x24 |
| net.names | 0.2us | 0.3us | 1.50x | 0.2us | 0.3us | 1.50x | play, whisper, 4 named 80x24 |
| net.players_frame | 33.0us | 32.9us | 1.00x | 52.9us | 52.7us | 1.00x | play, fog range, 4 watchers 80x24 |
| net.whisper | 4.5us | 4.8us | 1.07x | 7.7us | 18.8us | 2.44x | play, whisper, 4 named 80x24 |
| note.marks | 0.0us | 0.0us |  | 0.1us | 0.1us | 1.00x | build, noted squares 80x24 |
| panel.draw | 6.2us | 6.2us | 1.00x | 8.3us | 12.0us | 1.45x | play, fight cycling 80x24 / play, turn order 80x24 |
| picker | 16.7us | 17.2us | 1.03x | 287.5us | 289.0us | 1.01x | play, 500 characters 80x24 |
| picker.draw | 8.8us | 8.8us | 1.00x | 12.2us | 17.0us | 1.39x | play, 500 characters 80x24 |
| picker.open | 3179.7us | 3188.2us | 1.00x | 3625.4us | 3908.1us | 1.08x | play, 500 characters 80x24 |
| ping.draw | 0.5us | 0.5us | 1.00x | 0.6us | 0.7us | 1.17x | play, fog pings, 4 watchers 80x24 |
| play.draw | 61.2us | 61.6us | 1.01x | 91.4us | 91.8us | 1.00x | play, 24 tokens 200x50 |
| play.link | 0.3us | 0.3us | 1.00x | 0.5us | 0.4us | 0.80x | play, link there+back 80x24 |
| prof.overlay | 8.1us | 9.6us | 1.19x | 94.3us | 104.6us | 1.11x | profiler overlay 80x24 |
| range.draw | 17.1us | 17.1us | 1.00x | 22.7us | 22.5us | 0.99x | play, fog range, 4 watchers 80x24 / play, range radius 80x24 |
| range.status | 4.9us | 4.9us | 1.00x | 7.3us | 7.2us | 0.99x | play, fog range, 4 watchers 80x24 |
| ruler.draw | 0.5us | 0.6us | 1.20x | 0.7us | 0.7us | 1.00x | ruler, three legs 80x24 |
| scene.restore | 25.3us | 25.9us | 1.02x | 50.7us | 40.4us | 0.80x | play, scene of 500 80x24 |
| scene.save | 7.3us | 8.0us | 1.10x | 21.6us | 21.7us | 1.00x | play, scene of 500 80x24 |
| stamp.place | 2.5us | 2.5us | 1.00x | 2.7us | 2.7us | 1.00x | build, stamp 20x20 80x24 |
| stamp.show | 3.5us | 3.7us | 1.06x | 4.2us | 4.3us | 1.02x | build, stamp 20x20 80x24 |
| summon.place | 0.9us | 0.9us | 1.00x | 1.2us | 1.1us | 0.92x | play, summon 80x24 |
| trail.draw | 0.1us | 0.1us | 1.00x | 0.2us | 0.2us | 1.00x | play, carry 200x200 80x24 |
| trail.path | 3.0us | 3.0us | 1.00x | 12.2us | 12.3us | 1.01x | play, carry 200x200 80x24 |
| turn.advance | 0.6us | 0.6us | 1.00x | 4.2us | 4.2us | 1.00x | play, turn order 80x24 |
| turn.status | 0.7us | 0.7us | 1.00x | 0.7us | 0.8us | 1.14x | play, scene of 500 80x24 / play, 500 on 200x200 80x24 |
| undo.step | 149.8us | 163.9us | 1.09x | 205.3us | 208.2us | 1.01x | build, fill+undo 200 80x24 |
| undo.trim | 2328.9us | 2386.8us | 1.02x | 2793.3us | 3160.9us | 1.13x | build, fill history 80x24 |

In 1.x only: none

In 2.0 only: menu, options 80x24, play, a phone that stops reading 80x24, agent, dump a region 80x24, agent, describe 512x512 80x24, agent, check 512x512 80x24, agent, check 500 creatures 80x24, agent, job conflicts 80x24, agent, changes read 80x24, build, :w empty 512x512 80x24, build, :w full 512x512 80x24, :check, rooms 512x512 80x24, :check, every edge 512x512 80x24, agent.reap, app.check, check.fold, check.joint, check.pairs, check.shift, draw.options, job.conflicts, mapio.sections, open.file, rooms.build

## The phone page

What the page's own code costs a phone per frame, from `tools/pagebench.sh`: each scenario
is played headless with `vtt --bench --bench-record`, which saves the stream a phone is
sent, and the stream is replayed through the page as the binary serves it, in node, on a
phone's screen (915×412 CSS pixels at 2.625, held sideways; `VIEW=` sets another). Median
of three runs (`tools/median.py`), as above. Measured 2026-10-04, after the bands, the quiet
status line and the SIMD copy loop (docs/PAGESPEED.md).

| scenario             | frames |   decode |  present |     copy | px/frame |
|----------------------|--------|----------|----------|----------|----------|
| cursor walk          |   1610 |    9.4us |   19.2us |   16.3us |    20893 |
| cursor, walls        |   1610 |    7.7us |   33.7us |   31.4us |    83859 |
| carry                |   2400 |    6.7us |   53.4us |   51.1us |   165367 |
| pan 200x200          |    410 |   52.4us |  515.2us |  501.6us |  1045987 |

`decode` is `feed()`, the records into the cell arrays; `present` puts the frame together,
nearly all of it `copy` (`blitRow`: glyph tiles copied into the framebuffer by the
WebAssembly loop); `px/frame` is the pixels a frame pushes to the canvas, which the
browser then pays for natively and node cannot time. For that, `tools/pageprobe.js` in a
real browser: on a cursor step over a busy 120×40 map in Chrome the push was 393 µs of a
684 µs frame, and the status line's HTML 121 µs (docs/PAGESPEED.md has the measurements
and the plan they led to).

**The page is not slow to decode.** 9 µs for a cursor step, 57 µs for a frame that redraws
nearly everything. The copy loop and the push to the canvas are where a frame goes.

**A cursor step pushes about five rows.** Its dirty rows -- the column letters, the three
round the cursor, the status line -- span the screen top to bottom; until 2026-10-04 the
page pushed their bounding box, 737,280 pixels, and now pushes each band of consecutive
rows on its own, about 21,000 (docs/PAGESPEED.md).

**Two lessons about measuring it.** Chrome's clock without cross-origin isolation moves in
0.1 ms steps, so only totals over hundreds of frames mean anything there; and a hidden
window gets no animation frames, so `pageprobe.js` presents each message at once. In node,
running the page through `vm` or under `with` made every global name a slow lookup and
timed the harness instead (decode read 550 µs a frame that way); `pagebench.js` runs it as
a plain function with the stubs as parameters.

## Working rules

1. **A new drawing path gets a scenario in `tools/perf.sh`.** A path with no row in
   these tables is a path nobody is watching.
2. **Regenerate after anything that touches drawing**, and put the new numbers in the
   commit if a row moved by more than noise (about ±10% here).
3. **Cull to the window before doing work.** Every path that could scale with the map
   calls `grid_visible_tiles` first. This is the single rule that keeps a 512×512 map as
   cheap as a small one.
4. **Prefer not drawing to drawing quickly.** The diff means an unchanged cell costs
   nothing to leave alone, so a guard that skips work beats an optimization that does it
   faster.
5. **When a measurement suggests a win, plan it rather than taking it silently.** Say
   what it costs now, what it would cost, and what the change buys — some of these paths
   are worth leaving slow and obvious.
6. **Estimate the speed of light, then measure against it.** A plan's *Performance
   considerations* gives each path's least possible cost (the figures under *Speed of
   light*) beside the design's expected cost. The built path is measured against both. A
   path should cost in proportion to the work that must be done (what changed, what is
   on screen), never in proportion to the map.

## Tools

| | |
|---|---|
| `make perf` | regenerates every table on this page |
| `tools/proposals.sh` | the proposal table: the copy, the diff, the conflict check, the preview and the accept, apart from any key |
| `tools/machine.sh [DIR]` | the `Machine:` line above every published table; DIR names the disk the run writes to |
| `tools/median.py a b c` | the per-row median of several `make perf` outputs, which is what is published |
| `make bench` | one scenario, quick |
| `F12` | live overlay: per-zone p50/p99, a frame-time sparkline, cells and bytes |
| `--trace out.json` | Chrome Tracing profile, every occurrence of every zone; open in perfetto |
| `--bench-loops N` | more repetitions when a number looks noisy |
| `ONLY='regex' tools/perf.sh` | only the scenarios whose label matches; with `BIN=`, the A/B of one row's scenario under two binaries |
| `tools/phonecheck.sh` | manual: the page on the user's Android phone over the Wi-Fi (adb, Chrome's DevTools socket): a FULL's time, the send buffer A/B, the screen off and Chrome in the background |
| `make fuzz` | not a timing tool: libFuzzer on the map loader, the one untrusted input |
| `tools/sight.sh [bin]` | `fog.sight` per fog scenario, which the zone table cannot show |
| `--bench-clients N` | attaches N loopback watchers to a bench run, so a row can carry the remote view's cost |
| `--bench-stalled` | the last of those watchers reads only once frames are withheld from it, then is caught up with a FULL (`net.catch_up`); the bench prints the most vtt queued for it (step 7's lag bound: about a FULL) and the catch-ups |
| `--bench-names` | those watchers say they are phones P1, P2...: rows for whispers and the names offered |
| `--bench-record FILE` | saves the stream the first watcher is sent, for `tools/pagebench.js` to replay |
| `tools/pagebench.sh` | the phone page's table above: records each scenario, verifies it is drawn right, and replays it through the page in node; `PAGE=x.html` to A/B a page before embedding it; exits 1 on any failure |
| `tools/pagebench.js PAGE STREAM` | one replay, its timings and a framebuffer checksum (glyph masks differ per glyph, so a wrong glyph shows); `VERIFY=1` models the canvas and checks the canvas holds the framebuffer and a full repaint changes nothing; `MODULE=plain` or `js` forces the other copy loops; `WRAP=`, `VIEW=`, `JSON=1` |
| `tools/pageprobe.js` | pasted into a served page's console: totals for `feed`, `present`, `blitRow`, `tile`, `status` and `putImageData`, then `probe.report()` |
| `tools/genmap.sh` | the fixture map generator `perf.sh` and `pagebench.sh` share |

The overlay and the trace share the zone table, so anything wrapped in `PROF_ZONE`
appears in all three without further work. `-DVTT_PROF=0` compiles the instrumentation
out entirely.
