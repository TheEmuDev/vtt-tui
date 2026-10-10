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
paragraphs under them, written earlier, are the laptop's: their ratios hold, but their
absolute values don't match the tables any more. The save table, the phone page's and the
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
  500 on a 200×200 map draw in 41µs against 33µs for 24 (they drew in 85µs before the
  creatures were culled, A/B'd; the 8µs left is a handful of walks of the whole list --
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

- **The map writer builds its text one `fputc` at a time.** On the desktop, an unflushed
  512×512 save is 3.17 ms against 0.16 ms for the bytes, and formatting 789 KB should cost well under a
  millisecond. The autosave runs this on the main loop. Not yet planned.

## Frame times

What the app costs to use. Each scenario replays a keystroke script, a frame per key,
400 times. Every figure is the median of three such runs, row by row
(`tools/median.py`): a single run reliably has one row spiking somewhere, and never the
same one twice. The runs want the machine to themselves -- editing a file while one was
in flight was enough to lift the play rows by a fifth, and an A/B against the previous
binary is what showed it was the machine and not the code.

Machine: Intel Core i7-8700K @ 3.70GHz (6 cores, 12 threads, 4.7 GHz max, powersave governor), 16 GB RAM, Samsung SSD 860 EVO 1TB (btrfs), Linux 7.2.5-3-omarchy, gcc 16.2.1 -O2  (the desktop, 2026-10-08; until then this table was the laptop's)

| scenario             | size   | frame p50 | frame p99 | cells | bytes |
|----------------------|--------|-----------|-----------|-------|-------|
| build, open          | 80x24  |    22.7us |    49.6us |    10 |   207 |
| build, every edge    | 80x24  |    23.0us |    38.9us |    10 |   207 |
| build, 200x200       | 80x24  |    23.0us |    25.9us |    12 |   209 |
| build, 200x200       | 200x50 |    91.8us |   127.3us |    12 |   212 |
| build, mostly void   | 200x50 |    77.4us |   117.6us |    20 |   267 |
| build, noted squares | 80x24  |    23.5us |    34.0us |    88 |   524 |
| build, tracing       | 80x24  |    22.7us |    27.2us |     2 |   140 |
| build, circle brush  | 80x24  |    23.6us |    53.6us |    28 |   292 |
| build, 3x3 brush     | 80x24  |    20.2us |    36.7us |    18 |   128 |
| build, fill+undo 200 | 80x24  |    20.9us |    69.0us |   303 |  3398 |
| build, fill history  | 80x24  |    21.6us |    81.6us |   335 |  4145 |
| build, a job's tint  | 80x24  |    22.3us |    45.3us |    49 |   301 |
| ruler, three legs    | 80x24  |    20.4us |    41.9us |    11 |    40 |
| play, 24 tokens      | 80x24  |    22.7us |    28.1us |    19 |   298 |
| play, 24 tokens      | 200x50 |    82.7us |   154.1us |    36 |   315 |
| play, carrying       | 80x24  |    20.1us |    38.3us |    33 |   205 |
| play, carry 200x200  | 80x24  |    20.6us |    59.6us |    22 |   136 |
| play, 24 on 200x200  | 80x24  |    21.9us |    25.0us |    21 |   304 |
| play, 500 on 200x200 | 80x24  |    28.3us |    61.8us |    20 |   299 |
| play, 3x3 cursor     | 80x24  |    25.0us |    48.6us |    37 |   606 |
| play, choosing       | 80x24  |    20.6us |    35.9us |    12 |    95 |
| play, group box      | 80x24  |    19.3us |    45.9us |    18 |   133 |
| play, group carry    | 80x24  |    18.8us |    45.7us |    22 |   131 |
| play, range bands    | 80x24  |    26.2us |    51.2us |   129 |   657 |
| play, range radius   | 80x24  |    45.6us |    96.0us |   169 |   958 |
| play, range cone     | 80x24  |    26.0us |    62.4us |    60 |   654 |
| play, range line     | 80x24  |    25.7us |    61.5us |    55 |   620 |
| play, range square   | 80x24  |    25.5us |    62.5us |    63 |   669 |
| play, turn order     | 80x24  |    25.1us |    59.9us |    49 |   488 |
| play, fight cycling  | 80x24  |    33.4us |    74.4us |   317 |  4596 |
| play, spotlight      | 80x24  |    21.6us |    53.4us |    37 |   245 |
| play, named roll     | 80x24  |    20.6us |    34.9us |    17 |   165 |
| play, fog            | 80x24  |    23.7us |    29.4us |    24 |   265 |
| play, fog all dark, 4 watch | 80x24  |    44.4us |    50.4us |    24 |   265 |
| play, fog by hand    | 80x24  |    23.6us |    50.7us |    35 |   231 |
| play, fog range, 4 watchers | 80x24  |    75.6us |   131.4us |    77 |   541 |
| play, fog sight      | 80x24  |    23.3us |    53.2us |    58 |   565 |
| play, fog lantern    | 80x24  |    21.8us |    52.0us |    43 |   438 |
| play, fog warren     | 80x24  |    23.8us |    64.5us |    72 |   826 |
| play, fog warren 12  | 80x24  |    23.6us |    62.1us |    69 |   803 |
| play, fog warren, party | 80x24  |    24.7us |    68.5us |   118 |  1673 |
| play, fog door       | 80x24  |    21.1us |    47.7us |    28 |   215 |
| play, fog full rebuild | 80x24  |    21.2us |    41.5us |    21 |   180 |
| play, fog reveal 12  | 80x24  |    22.2us |    50.5us |    55 |   544 |
| play, fog sight, 4 watchers | 80x24  |    47.7us |   126.2us |    58 |   565 |
| play, fog soft edge, 4 watchers | 80x24  |    46.8us |   109.2us |    42 |   420 |
| build, stamp 20x20   | 80x24  |    17.5us |    49.9us |   146 |  1696 |
| build, fog paint     | 80x24  |    23.0us |    44.5us |    24 |   219 |
| build, 64 links      | 80x24  |    27.2us |    61.3us |    35 |   274 |
| play, 64 links       | 80x24  |    28.1us |    56.1us |    47 |   348 |
| play, link there+back | 80x24  |    25.5us |    38.7us |    78 |   637 |
| build, one floor     | 80x24  |    22.2us |    29.4us |    12 |   176 |
| play, other floor, 4 watchers | 80x24  |    42.1us |    81.6us |    18 |   238 |
| play, floor steps    | 80x24  |    22.3us |    26.1us |    40 |   290 |
| play, counters       | 80x24  |    22.2us |    48.3us |    54 |   402 |
| play, clocks         | 80x24  |    21.4us |    36.8us |    15 |   162 |
| play, 1 watcher      | 80x24  |    31.7us |    74.8us |    19 |   298 |
| play, 4 watchers     | 80x24  |    40.2us |    47.7us |    19 |   298 |
| play, 4 watchers, differing | 80x24  |    55.9us |   118.8us |    58 |   530 |
| play, hidden, 4 watchers | 80x24  |    58.0us |   103.7us |    19 |   298 |
| play, camera party, 4 watchers | 80x24  |    47.2us |   113.4us |    49 |   463 |
| play, camera hold, 4 watchers | 80x24  |    43.2us |    52.1us |    16 |   212 |
| play, whisper, 4 named | 80x24  |    41.6us |    63.2us |    13 |   147 |
| play, pings, 4 watchers | 80x24  |    49.6us |   104.1us |    98 |   978 |
| play, fog pings, 4 watchers | 80x24  |    63.9us |    99.8us |    72 |   696 |
| play, GM ping        | 80x24  |    23.3us |    50.2us |    35 |   345 |
| play, carry, 4 watch | 80x24  |    39.7us |    83.7us |    33 |   205 |
| play, logging        | 80x24  |    20.0us |    51.5us |    31 |   198 |
| play, rolling        | 80x24  |    20.5us |    38.6us |    23 |   191 |
| play, group effect   | 80x24  |    27.7us |    49.9us |    44 |   476 |
| play, card box       | 80x24  |    33.6us |    73.5us |    56 |   620 |
| play, damage         | 80x24  |    34.1us |    66.3us |    33 |   198 |
| play, 500 characters | 80x24  |    30.4us |    80.7us |   110 |   628 |
| play, scene of 500   | 80x24  |    29.7us |    54.7us |    22 |   182 |
| play, map trip there+back | 80x24  |    23.0us |    53.1us |    45 |   306 |
| play, handout typed, 4 watch | 80x24  |    41.6us |    83.7us |    12 |   142 |
| agent, room + 12     | 80x24  |    96.8us |   148.6us |    55 |   157 |
| agent, plan of rooms | 80x24  |    69.3us |    91.2us |    55 |   157 |
| agent, dump 512x512  | 80x24  |    23.6us |  5520.0us |     7 |   138 |
| agent, proposal, review | 80x24  |    17.0us |    70.0us |    26 |   264 |
| agent, proposal, accept | 80x24  |    16.1us |    92.3us |    40 |   241 |
| agent, map changed event | 80x24  |    23.5us |    26.0us |    36 |   279 |
| help page            | 80x24  |    37.3us |    52.8us |   542 |  2601 |
| profiler overlay     | 80x24  |    30.0us |   132.5us |   171 |   962 |

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

Machine: Intel Core i7-8700K @ 3.70GHz (6 cores, 12 threads, 4.7 GHz max, powersave governor), 16 GB RAM, Samsung SSD 860 EVO 1TB (btrfs), Linux 7.2.5-3-omarchy, gcc 16.2.1 -O2  (the desktop, 2026-10-08; until then this table was the laptop's)

| path             | p50     | p99     | worst   | calls | heaviest scenario      |
|------------------|---------|---------|---------|-------|------------------------|
| app.draw         |  72.9us | 143.9us | 235.5us |  3200 | build, mostly void 200x50 |
| camera.party     |   0.1us |   0.3us |   0.6us | 1925+ | play, camera party, 4 watchers 80x24 |
| card.draw        |   8.6us |  17.4us |  59.4us |  5994 | play, card box 80x24   |
| character.place  |   0.8us |   1.4us |   1.6us |  262+ | play, 500 characters 80x24 |
| checkpoint.read  |   1.6us |   2.7us |   3.5us |   398 | agent, map changed event 80x24 |
| clock.draw       |   1.2us |   2.6us |  26.7us | 15067+ | play, clocks 80x24     |
| counter.step     |   0.3us |   2.7us |  18.6us |  3200 | play, counters 80x24   |
| ctl              | 5070.6us | 7235.6us | 9195.3us |   400 | agent, dump 512x512 80x24 |
| ctl.checkpoint   |   0.1us |   0.2us |  15.9us |   399 | agent, map changed event 80x24 |
| dmg.apply        |  13.6us |  28.4us |  36.8us |   400 | play, damage 80x24     |
| editor.draw      |  77.4us | 133.1us | 225.8us |  3200 | build, mostly void 200x50 |
| floor.pick       |   0.7us |   1.7us |  23.3us | 11200 | play, 500 on 200x200 80x24 |
| floor.switch     |   0.2us |   0.2us |   0.2us |    1+ | play, other floor, 4 watchers 80x24 |
| fog.blank        |   0.5us |   1.0us |  19.0us | 2548+ | play, fog sight, 4 watchers 80x24 |
| fog.paint        |   0.2us |   3.7us |  45.8us |  1600 | build, fog paint 80x24 |
| fog.reveal       |   3.1us |   6.2us |  19.6us |  335+ | play, fog range, 4 watchers 80x24 |
| fog.sight        |  15.6us | 173.6us | 212.5us |  3599 | play, fog reveal 12 80x24 |
| fog.sight.build  |  17.1us | 128.9us | 224.2us |  3601 | play, fog warren 12 80x24 |
| fog.sight.rim    |   6.5us |  20.2us |  30.6us |  338+ | play, fog range, 4 watchers 80x24 |
| fog.sight.union  |   8.8us | 167.7us | 206.0us |  3599 | play, fog reveal 12 80x24 |
| grid.draw        |  72.1us | 118.3us | 211.2us |  3200 | build, mostly void 200x50 |
| grid.labels      |   4.1us |  14.0us |  31.6us |  1000 | help page 80x24        |
| group.box        |   0.2us |   0.2us |   0.2us |  383+ | play, fog warren, party 80x24 |
| group.move       |   0.3us |   3.3us |  43.2us | 1532+ | play, fog warren, party 80x24 |
| handout.send     |  13.5us |  27.3us |  33.8us |  282+ | play, handout typed, 4 watch 80x24 |
| input.key        |   0.5us | 4852.9us | 6455.6us | 5251+ | play, 500 characters 80x24 |
| job.accept       |  12.5us |  28.3us |  42.0us |   400 | agent, room + 12 80x24 |
| job.highlight    |   0.3us |   0.5us |   4.3us |  8800 | build, a job's tint 80x24 |
| job.preview      |   0.6us |   1.1us |  14.5us |   400 | agent, proposal, accept 80x24 |
| job.propose      |  38.1us |  77.2us |  90.4us |   400 | agent, room + 12 80x24 |
| job.review       |  11.5us |  34.0us |  38.9us |   400 | agent, proposal, accept 80x24 |
| link.marks       |   4.0us |  10.4us |  31.1us |  5600 | play, 64 links 80x24   |
| link.trip.map    | 4252.6us | 7436.5us | 19992.2us |   800 | play, map trip there+back 80x24 |
| log.write        |   3.4us |  10.3us |  15.3us |   253 | play, logging 80x24    |
| map.copy         |  18.0us |  42.0us | 536.7us |   400 | agent, proposal, accept 80x24 |
| mapdiff          |  14.2us |  32.2us |  50.0us |   400 | agent, room + 12 80x24 |
| mapio.fsync      | 1984.7us | 3719.5us | 15804.5us |  1600 | play, map trip there+back 80x24 |
| mapio.load       | 3296.0us | 3296.0us | 3296.0us |     1 | agent, map changed event 80x24 |
| mapio.write      | 2100.9us | 3810.2us | 16056.4us |  1600 | play, map trip there+back 80x24 |
| move.label       |   0.0us |   1.8us |  11.0us |  9995 | play, carry 200x200 80x24 |
| names.sync       |   4.6us |   4.6us |   4.6us |     1 | play, pings, 4 watchers 80x24 |
| net.accept       |   5.1us |  18.0us |  18.0us |    4+ | play, fog all dark, 4 watch 80x24 |
| net.cmd          |   0.0us |   0.1us |  22.9us | 28036+ | play, fog pings, 4 watchers 80x24 |
| net.frame        |  11.9us |  26.4us |  76.7us | 8155+ | play, camera party, 4 watchers 80x24 |
| net.names        |   0.4us |   0.4us |   0.4us |     1 | play, pings, 4 watchers 80x24 |
| net.players_frame |  35.4us |  73.3us | 143.5us | 8035+ | play, fog range, 4 watchers 80x24 |
| net.whisper      |   6.6us |  15.0us |  21.8us |  244+ | play, whisper, 4 named 80x24 |
| note.marks       |   0.0us |   0.1us |   0.5us |  7997 | build, noted squares 80x24 |
| panel.draw       |   6.6us |  19.7us |  33.0us |  3995 | play, turn order 80x24 |
| picker           |  18.2us | 328.7us | 818.6us | 2099+ | play, 500 characters 80x24 |
| picker.draw      |   9.4us |  20.6us |  61.3us | 2623+ | play, 500 characters 80x24 |
| picker.open      | 4677.2us | 5272.3us | 6435.0us |  263+ | play, 500 characters 80x24 |
| ping.draw        |   0.5us |   0.8us |  18.2us | 9438+ | play, fog pings, 4 watchers 80x24 |
| play.draw        |  61.5us | 114.6us | 371.8us |  5595 | play, 24 tokens 200x50 |
| play.link        |   0.3us |   1.0us |   5.9us |   800 | play, link there+back 80x24 |
| prof.overlay     |   8.6us |  98.2us | 134.8us |  1000 | profiler overlay 80x24 |
| range.draw       |  18.2us |  34.1us |  84.7us |  3995 | play, range radius 80x24 |
| range.status     |   5.4us |  11.3us |  37.5us |  2000 | play, range bands 80x24 |
| ruler.draw       |   0.6us |   1.3us |   8.6us |  4400 | ruler, three legs 80x24 |
| scene.restore    |  26.9us |  65.8us | 135.9us |   400 | play, scene of 500 80x24 |
| scene.save       |   9.8us |  33.0us |  70.1us |   400 | play, scene of 500 80x24 |
| stamp.place      |   2.6us |   5.8us |   6.9us |   400 | build, stamp 20x20 80x24 |
| stamp.show       |   3.6us |   8.3us |  17.6us |   400 | build, stamp 20x20 80x24 |
| trail.draw       |   0.1us |   0.3us |   5.1us |  9995 | play, carry 200x200 80x24 |
| trail.path       |   3.2us |  17.0us |  77.8us |  6231 | play, carry 200x200 80x24 |
| turn.advance     |   0.6us |   6.7us |  34.4us |   800 | play, turn order 80x24 |
| turn.status      |   0.7us |   1.2us |  25.1us |  5595 | play, 500 on 200x200 80x24 |
| undo.step        | 180.8us | 361.5us | 507.6us |  1200 | build, fill+undo 200 80x24 |
| undo.trim        | 2864.4us | 4069.9us | 4069.9us |    97 | build, fill history 80x24 |

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
per 262,144 new ops, which is 19ns an op amortized and a thing that happens a handful
of times in a long session of painting. The scenario that measures it (`fill history`)
paints and clears 200×200 without undoing so the log grows 80,000 ops a loop.

**The range highlight costs the window when it covers the window.** `play, range radius`
holds a 100 ft reach over a 40×25 map, so every visible tile is in range and every one
of them gets a line-of-sight trace: `range.draw` at 21µs is that, and `range.status` at
5µs is the same trace once per creature for the 24 named in the status line. The band
scenario reads cheaper only because it spends most of its frames on the near bands; its
*Very Far* frames cost the same. The radius itself is free, and `:scale` cannot make it
dearer -- reach is compared in feet, tiles are still counted in tiles.

**A card beside the map costs its text, not the window.** `play, card box` walks the
cursor with a creature selected whose card is a stat block's worth (about 700 bytes):
43.2µs a frame against 30µs for the same crowd with no box. `card.draw` is 10.5µs of that:
the text cleaned of its marks, wrapped to measure and wrapped again to draw -- a cost that
follows the card's length, at most 4 KB, and not the map's. Loading a map is unchanged by
the card block's check on every line (a 512×512 map, A/B'd against the build before cards:
342-348ms a run either way).

**Damage is one command, not a frame.** `play, damage` puts a Far burst over 13 of a crowd
of 24, each with a card, and marks 6 damage on them all, then `u`: `dmg.apply` is 18.9µs at the
median and 32.3µs at p99 -- the burst's catch (`range_caught`, once over the creatures), each
card's thresholds read, one undo batch. The frame around it, 43.7µs, is the card box and the
burst's highlight; the Horde title the box asks for (`app_horde_note`) is a scan of one card.

**A whisper is a few writes, and naming phones costs nothing per frame.** `play, whisper, 4
named` whispers to one of four named phones every loop: `net.whisper` is 7.6 µs at the
median, 58 µs at p99 (a socket write and flush per phone of the name). The names offered
are rebuilt only when the map changes -- `names.sync`, 17 µs, ran once in the whole
`play, pings, 4 watchers` run -- and `net.names` sends them only when the text differs.
The frame row sits with the other four-watcher rows (55 µs). (The run that first published
it read the circle brush at 55 µs; an A/B against the previous build gave both 32 µs, and the
next median of three agrees: the machine, not the code.)

**A camera of the players' own costs a second draw, and sends less.** With `:player camera
party` or `hold` the players' frame is always drawn apart, as it is with a creature hidden:
`play, camera party, 4 watchers` (carrying a creature through the crowd) is 65.4 µs a frame
and `play, camera hold, 4 watchers` (the GM's cursor walking) 57.0 µs, between `play, 4
watchers` (51.9 µs) and `play, hidden, 4 watchers` (75.9 µs). What goes to the phones falls:
held, the GM's cursor is not on their frame, so walking it sends them nothing; a party
camera moves only when a creature nears its edge, so carrying one through the crowd sent
125 bytes a frame to all four in a by-hand run, against 1,457 under follow, whose view
moves with the GM's. `camera.party` works the framing out only when the map, their floor,
the GM's zoom or the screen changed: 0.2 µs at the median, 1.4 µs at worst.

**A group effect is the range highlight on a moving center.** `play, group effect` walks a
Very Close burst across a crowd of 24: 40.7µs a frame against 30.4µs for the same map with
no overlay (`play, 24 tokens`), the tint and a sight trace for the dozen or so squares it
covers plus the status line's trace per creature. Moving it re-aims it and nothing else.
The Daggerheart movement rule on the MOVING line is one lookup into the band table the
label beside the creature already made; the carry rows do not move.

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
measured no better than the JavaScript copies. The tile cache -- a glyph in its colors
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
32.2µs: one byte load and a mask a visible tile, writing the dim color into cells that were
being written anyway. The players' frame over fog is cheaper to draw than the GM's -- a hidden
tile is a `continue` with no writes, and walls between hidden tiles are never resolved -- so
`play, fog all dark, 4 watch` at 60.3µs is the second draw the plan priced in, less than the
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

**A quiet phone costs 32 bytes a minute, not a reconnect.** A browser never speaks on
its own, and the server used to drop one after a minute of silence: a phone whose player
never tapped reconnected every minute -- a TCP handshake, a 300-byte upgrade, a 150-byte
reply and a 5 KB FULL, about 5.5 KB a minute and a visible blink. Now a browser silent
for 15 s is sent a two-byte WebSocket ping and answers with a six-byte pong on its own:
32 bytes a minute, and the FULL is paid once when a sleeping phone wakes. No row: the
check is a compare inside the loop that already runs once a second with clients
attached, and the bench's clients are watchers, which are unchanged.

**A ping costs a tenth of a microsecond to read and half of one to draw.** `net.cmd`
parses a tap in 0.1µs and `ping.draw` rings a square in 0.6µs. `play, pings, 4 watchers`
is the worst case on purpose: every one of four phones taps every frame, a second of
synthetic time apart, so four rings are always up and moving and the status line changes
every frame. It draws in 63.9µs against `play, 4 watchers`'s 52.1, and sends 978 bytes a
frame to each client against 298 -- the rings' cells and the status line, as ordinary
frame runs; a single tap at a real table is a few hundred bytes once. Over fog the
players' frame is drawn apart from the GM's anyway, and pinging adds about the same.
`play, GM ping` is `g p` with no server: level with the plain play rows. A tap up the
wire is under 16 bytes.

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
median of nine, on the **laptop** (btrfs on an SSD). The desktop's, 2026-10-07: 2.18 /
2.83 / 5.45 ms flushed and 0.07 / 0.62 / 3.17 ms unflushed. Its disk flushes about fifteen
times faster.

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

**Links cost their marks and nothing else.** `link.marks` walks the links rather than the
tiles, culls each end to the window, and writes a glyph and a number into cells the grid
already drew: 4.1µs a frame at p50 with all sixty-four a map can hold on screen (`build, 64
links` 35.3µs against 30.4µs for `build, open`), and a map without links returns before the
zone opens. Taking one is a few token moves in a batch: `play.link` is 0.4µs typical. The
trip's frame writes more (`play, link there+back`, 637 bytes) because the view recenters on
the far end, which redraws the window; that is the cost of a jump, not of the link.

**A floor costs nothing to show; a second floor costs a second frame.** Showing one floor
culls to its box, so `build, one floor` draws fewer cells than the whole map (12 cells and
176 bytes a frame, against 209). When the players are on a floor the GM is not showing,
their frame cannot be copied from the GM's and is drawn again through its own camera:
`play, other floor, 4 watchers` is 57.1µs against 52.0µs for `play, 4 watchers`, the same
cost as any frame that differs (`play, 4 watchers, differing`). `floor.pick` runs after
every key and before every draw to keep the players' floor current: 0.1µs for 24
creatures, 1.3µs for 500, a walk of them all. `floor.switch` is under a microsecond.

**A hidden creature costs the players' frame its copy.** With anything hidden the players'
frame is drawn rather than copied from the GM's, so `play, hidden, 4 watchers` is 79.5µs against
52.7µs for `play, 4 watchers` -- the same as any frame that differs (`play, 4 watchers,
differing`, 78.7µs), and nothing more: the question each creature is asked,
`fog_token_unseen`, is a flag test before any fog work. Bytes do not move (298), since the
players are sent what changed on their screen and a hidden creature changes nothing there.

**A trip to another map is two flushed saves, about 65 milliseconds.** `link.trip.map`
(66ms at the median, 144ms at p99, 378ms at worst) reads the other map, finds the party
room, and writes both maps -- the one left and the one arrived in -- for two 40×25 maps.
Nearly all of it is the two flushes (`mapio.fsync`, 32ms each at the median; `mapio.write`
round the whole write is half a millisecond more). That is kept on purpose: a trip is a
deliberate change of map, like opening a file, and it is never half on disk. The frames
right after a flush draw slower (`play, map trip there+back`, 50µs against 30µs with the
same maps on tmpfs, A/B'd): the same code, run cold.

**Handouts cost a send, not a frame; and the players' frame stopped changing when only the
GM's message does.** Putting a handout up is one `'H'` record to each client
(`handout.send`, 16µs for four watchers); the frames around it are ordinary
(`play, handout typed, 4 watch`, 59µs; mostly typing the `:handout say` line). The same commits made four rows 15-20% cheaper --
`play, fog all dark, 4 watch` 75.5µs to 62.9µs, fog sight 87.3 to 69.7, soft edge 82.7 to 67.3,
other floor 80.1 to 66.3 -- A/B'd against 3c7cf55 over three runs each. It is the
review fix for the players' status line: it was cut short by the width of the GM's
message even where that message is not drawn for the players, so every key that changed
the GM's message changed the players' frame too, and was encoded and sent (216 bytes a
frame to four watchers over total fog, where they see nothing move; 0 now). `net.frame`
in those scenarios went from 14.8µs at the median to nothing to do.

**A scene of 500 creatures goes back in 40µs.** `scene.restore` removes every creature
from the end of the list (no memmove) and adds the scene's, each through the undo log, and
sets the round and spotlight: 39.8µs at the median for 500, 53µs at p99. Saving the same
500 (`scene.save`, a copy of the list) is 14µs. The frame around it, 38µs (83µs before
creatures off the window were culled), is ordinary; both zones run once per `:scene`,
never per frame.

**Opening the picker is its one dear moment: 4.7ms to read 500 saved characters.**
`picker.open` reads every file in the directory once, for the label, side, size and
counters the list shows, so it is the directory's size -- about 9µs a template -- and it
is now the slowest keystroke there is (`input.key`'s p99 in `play, 500 characters`). Under a
frame at that size, once, when the GM asks for the list; typing into it (`picker`, a ranked
filter over the names in memory) is 23µs at the median, drawing it (`picker.draw`) 11µs, and
placing one
(`character.place`) is a microsecond. A campaign with thousands of templates would want the
details read lazily, for the rows on screen.

**`stamp.show` is the preview of a stamp on the cursor**, and it is the stamp's size, not
the map's: its squares, boundaries and creatures are swapped into the map for the one draw
and swapped straight back, with nothing touched that sight or the autosave watch. A 20×20
stamp is under 10µs a draw at the p99 and about 20µs at worst on a 200×200 map
(`build, stamp 20x20`); turning or mirroring one (`stamp.turn`) is a new stamp built once,
on the key, never a frame's work; `stamp.place`, the
real thing, is one batch of undo edits, a fill's cost.

**`ctl` is an agent's request, and it is dearest when it reads the most.** A 40×40
room over void with a dozen creatures in it -- 1,600 squares, the outline, twelve
placements, one undo batch -- is tens of microseconds (`agent, room + 12`). A whole plan of
five rooms placed by each other, five corridors and four creatures (`agent, plan of
rooms`) is about 21µs: a corridor is planned as at most two boxes and checked square by
square before a square is dug, so its cost is its length. A `dump` of
the whole 512×512 map writes about a megabyte of text and takes about 8ms; it is a
one-off read an agent asks for, not a keystroke, and `dump B2:K12` makes it the size of
the region. Nothing of the channel is on the frame path: off it costs nothing, on and
idle one more descriptor in `poll`, and the GM's frame after a request is an ordinary
frame (`agent, room + 12`'s frame row is the redraw of the changed squares). Since step 4 of
docs/CONFLICTS.md these rows run under `:agent accept auto`, so each request is a proposal
made and accepted at once: *Proposals from the channel*, below, has what that costs.

## Proposals

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
| five rooms, 40x25, 24 creatures           |     0.6us |     2.4us |     2.9us |     2.8us |     0.5us |     1.3us |     1.1us |     3.3us |
| five rooms, 512x512, 24 creatures         |    28.1us |     2.5us |     4.7us |    38.1us |     0.5us |     1.5us |     1.1us |     3.5us |
| five rooms, 512x512, 500 creatures        |    31.9us |     2.6us |    11.2us |    44.1us |     0.5us |     4.3us |     1.1us |     3.6us |
| fill the whole map, 512x512, 24 creatures |    49.0us |  3242.0us |  2205.9us |  2113.8us |   442.2us |     7.2us |     5.2us |  4066.7us |

**Rows that moved between publications.** The fill row's *accept* read 3.5 ms when step 1
was published and 4.1 ms after step 2, with no change on that path. An A/B of the two
commits found the later one no slower, so it is placement and noise. These tables are from
default builds, where placement alone moves a tight loop 5-9% (*The checkpoint*, below).
Treat a whole-map row's change under about 15% as noise unless an aligned A/B confirms it.

Against the speed of light (the desktop's figures above):

| path | floor | measured, 512×512 | gap, and why it is kept |
|---|---|---|---|
| copy | 1.05 MB at 37 GB/s: 28 µs | 28-32 µs | none. It is a copy **into** the scratch map the app keeps, so its pages stay mapped. A fresh `map_copy` of a 512×512 map is 434 µs: 1 MB of new pages, a fault each on first touch |
| diff, five rooms | the 300 squares written, about 1 µs, plus the creatures | 4.9 µs; 3.1 on 40×25 | about 2 µs: the 300 reads are scattered across two 1 MB maps, and the cost follows the arrays' stride, not their blocks. An empty log on 512×512 is 0.8 µs, so indexing the 1,089 blocks is half a microsecond. The fallback for a big log is the full diff (*diff (all)*: one `memcmp` a row) |
| diff, 500 creatures | streaming 2 × 128 KB of creatures: about 7 µs | 6.7 µs over the 24-creature row | none. The lists are walked in step (a copy keeps the order) and hashed only on a miss. Pairing them by search was 89 µs |
| conflicts | one compare per element | 0.5 µs | none |
| preview frame | the window's changed cells, swapped in and out | 1.1 µs | none. Only the used slots of the link and note arrays are swapped. Swapping the whole fixed arrays (9 KB a side) made a frame 2 µs. The first frame after a change also builds the preview's lists (4.2 µs with 500 creatures) |
| accept | the undo log's cost for the same writes | 3.3 µs | none. It is the plan's writes, recorded |
| a fill of the whole map | 262k cells: writing 2 MB and reading 1 MB, about 0.2 ms | diff 2.4 ms, check 0.45 ms | about 2 ms: a cell is pushed one at a time and bucketed by block. The accept, which only records the same writes in the undo log, is 3.5 ms, so the diff is not what a fill costs. Kept |

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
| checkpoint, 40x25, 24 creatures           |     0.5us |     7.4us |     9.4us |     4.6us |     8.2us |     1.0us |     8.3us |
| checkpoint, 512x512, 24 creatures         |     1.4us |  1855.2us |  2428.6us |  1231.2us |  2254.3us |     2.3us |  2494.2us |
| checkpoint, 512x512, 500 creatures        |     5.2us |  1856.6us |  2432.8us |  1234.1us |  2253.7us |     8.0us |  2352.0us |

| path | floor | measured, 512×512 | gap, and why it is kept |
|---|---|---|---|
| any edit, no checkpoint | as before | as before | none. The undo apply loop has no test per op, and the map's writers none at all; the recorders test one pointer, expected false. An A/B against the commit before (both rebuilt with `-falign-functions=64 -falign-loops=32`) measures no difference |
| start | the small parts: 18 KB plus 256 bytes a creature, about 1 µs | 1.4 µs; 5.2 at 500 creatures | none |
| recording, while one runs | a value byte and a bit a cell, under 0.5 ns | about 2 ns a cell (a whole-map fill 1.86 → 2.43 ms) | about 1.5 ns: the block index and the bit's read-modify-write in the recorder. Out of line it was 3 ns |
| an undo step, while one runs | nothing for cells recorded since the start (recording noted them) | about 4 ns an op (1.23 → 2.25 ms for the whole map) | the walk over the batch's ops, though every cell in it is already noted; each reads the live cell, which the review's fix needs (below) and `apply` reads next anyway. Skipping a batch recorded since the start would need the log to know about the checkpoint. It only shows when a whole-map fill is undone while an agent is connected. Kept |
| reading a dozen edits | the twelve cells and the small parts | 2.3 µs; 8.0 at 500 creatures | none |
| reading a fill | 262k cells: about 0.3 ms | 2.4 ms | the same pushing and bucketing as the full diff's, above: one cost, kept once |

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
  0.3 µs at p50 and 0.6 µs at p99, twice the floor; the gap is the walk over the change
  set's blocks and the conflict pass. The frame is 22.3 µs at p50, no more than opening the
  map (23.2 µs); `editor.draw` measured 17.7 µs with the tint against 17.6 µs without.
- **What changed it:** the tint first set each square's cells one at a time (0.9 µs);
  `grid_tint_tiles` now tints a row of a box at once. An earlier figure here, 0.1 µs, came
  from a scenario that was not loop-neutral (after the third loop its box was one square),
  so it was below its own floor.
- **The cost follows the window:** a box is clipped to the squares on screen, and a change
  set is walked only in the blocks meeting the window.
- **Bytes:** the tint costs nothing once it is up, because the renderer sends only cells
  that changed. The scenario, which puts a tint up and takes it down every loop, averages
  301 bytes a frame.
- **The worst call, 25 µs,** is one frame of 8,800. It is the first draw after `:ask`, cold.

### Proposals from the channel

A request's edits run on a copy of the map and become a proposal (docs/CONFLICTS.md, step
4). The rows `agent, proposal, review` and `agent, proposal, accept` run the plan of rooms
(five rooms, five corridors, four creatures: about 650 cells) on the largest map, 512×512,
then review and scrap it, or accept it and undo. Median of three, the desktop, 2026-10-09:

| piece | measured p50 | speed of light | gap, and why |
|---|---|---|---|
| the copy (`map.copy`, 512×512) | 18.3 µs | about 22 µs (786 KB at 28 µs/MB) | none: at the floor |
| the diff (`mapdiff`, ~650 cells) | 13.7 µs | about 1.5 µs (two reads a cell) | about 17-20 ns a cell, the rate *Proposals* above measured (block sort, the dedupe hash); accepted there |
| the summary and the `#N` corner | about 4.5 µs inside `job.propose` | well under 1 µs | each walks the set's cells once: two walks where one would do. A finding, not fixed: a proposal comes seconds apart |
| a review frame (`job.preview`) | 0.6 µs | the cells in the window, swapped twice | none |
| the accept (`job.accept`) | 12.4 µs | applying the cells through the log | none: one undo op a cell |

**The whole request.** The plan of rooms costs about 25 µs a request more as a proposal
accepted at once (`agent, plan of rooms`: 42 µs a frame before, 67 µs now), and the 40×40
room with a dozen creatures about 41 µs more (53 to 95 µs; 1,760 cells, so its diff and
accept are the larger). That is the price of one road in (decision 6): every change is made
on the copy, compared, then applied, where it was once applied straight. Running a request
straight onto the map when it would land at once anyway would save it, at the cost of a
second road that no review ever sees; at under 0.1 ms a request, it stays.

**Nothing on the frame path.** With a proposal waiting, a frame costs its tint (*The jobs'
tints*); in review, the swap above. The rows' frames are 15-16 µs at p50.

### Events and `wait`

An agent is told what happened through a held request, `wait` (docs/CONFLICTS.md, step 5).
The desktop, 2026-10-09, median of three.

| path | measured | speed of light | gap, and why |
|---|---|---|---|
| a held `wait`, idle | no CPU in 3 s (a live vtt, `/proc/PID/stat`), before and after an edit | nothing | none: one descriptor in `poll`, asked for no events, and the timeout is its own deadline |
| answering a waiter after a key | 1 ms from the key to the client's exit, the client's start-up included (a live vtt) | a `send` (about 3 µs, the unix round trip) | none in vtt: the flush runs the turn of the loop the key was handled in |
| the flush with no agent, every turn of the loop | not a row: a scan of 8 connections and 16 jobs | nothing | tens of nanoseconds; the price of answering after the key without a flag every job site must set |
| the map-changed event (`checkpoint.read`): a dozen edits on 512×512 | 1.6 µs p50, 2.7 µs p99 | the changed block and the small parts, about 1 µs | the small parts are compared whole (areas, links, notes, rolls, clocks), as *The checkpoint* found |
| the checkpoint starting again (`ctl.checkpoint`) | 0.1 µs | a byte a block | none |

`agent, map changed event` is an agent listening, a dozen of the GM's edits, then the map
quiet: its frame (23.5 µs) is `build, open`'s, so the checkpoint running under the GM's edits
does not show on the frame. `ctl.wait`, the zone round answering waiters, has no row: the
bench has no socket to hold one on; the live check above stands in for it, and is written
out in docs/CONFLICTS.md's *Progress*.

**The bench's tick.** Rows run with `--bench-ctl` now tick the app once a loop, inside the
request's frame, on a clock that lets the map go quiet. With a job open (`agent, proposal,
...`) that puts a `checkpoint.read` in each loop's request frame, about 2 µs of its p99.

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
| `make fuzz` | not a timing tool: libFuzzer on the map loader, the one untrusted input |
| `tools/sight.sh [bin]` | `fog.sight` per fog scenario, which the zone table cannot show |
| `--bench-clients N` | attaches N loopback watchers to a bench run, so a row can carry the remote view's cost |
| `--bench-names` | those watchers say they are phones P1, P2...: rows for whispers and the names offered |
| `--bench-record FILE` | saves the stream the first watcher is sent, for `tools/pagebench.js` to replay |
| `tools/pagebench.sh` | the phone page's table above: records each scenario, verifies it is drawn right, and replays it through the page in node; `PAGE=x.html` to A/B a page before embedding it; exits 1 on any failure |
| `tools/pagebench.js PAGE STREAM` | one replay, its timings and a framebuffer checksum (glyph masks differ per glyph, so a wrong glyph shows); `VERIFY=1` models the canvas and checks the canvas holds the framebuffer and a full repaint changes nothing; `MODULE=plain` or `js` forces the other copy loops; `WRAP=`, `VIEW=`, `JSON=1` |
| `tools/pageprobe.js` | pasted into a served page's console: totals for `feed`, `present`, `blitRow`, `tile`, `status` and `putImageData`, then `probe.report()` |
| `tools/genmap.sh` | the fixture map generator `perf.sh` and `pagebench.sh` share |

The overlay and the trace share the zone table, so anything wrapped in `PROF_ZONE`
appears in all three without further work. `-DVTT_PROF=0` compiles the instrumentation
out entirely.
