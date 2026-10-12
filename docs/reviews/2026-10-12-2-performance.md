# Review 2 of 3: performance, concurrency and resource footprint (vtt 2.0 input)

Reviewer: Principal Systems Architect / HPC (Fable 5.1). Date: 2026-10-12. Branch `vtt-2.0`
(== `main`, e7c58e9). Read-only: everything was built and run from a copy of the tree in the
scratchpad (`perf-build/`), under its own `XDG_DATA_HOME`/`XDG_RUNTIME_DIR`; the live vtts I
drove ran on a pty I opened and were stopped by PID. Review 1 (agentic) was read first; its
claims that I lean on are marked **checked**.

Machine: Intel Core i7-8700K @ 3.70GHz (6 cores, 12 threads, 4.7 GHz max, powersave governor),
16 GB RAM, Samsung SSD 860 EVO 1TB (btrfs), Linux 7.2.5-3-omarchy, gcc 16.2.1 -O2 — the
**desktop** of PERFORMANCE.md. The machine was not quiet (load ~1.5, other work running), so
every timing below is rough (±10-20%); byte counts, allocation counts and RSS are exact.

Each finding says **read** (code at `file:line`), **measured** (a command run, with its output)
or **inferred**.

How I measured (so the testing reviewer can reproduce):
- `./vtt MAP --bench KEYFILE --bench-loops N --size WxH [--bench-clients K --bench-record F
  | --bench-ctl REQ]` for frame p50/p99, cells and bytes a frame (the bench prints them).
- Allocation counts: an `LD_PRELOAD` shim counting `malloc/calloc/realloc/free` (scratchpad
  `mcount.c`), compared at 1 loop and at N loops of the same script, so the difference is the
  per-frame cost.
- Max RSS: a `wait4` runner (`rss.c`) printing `ru_maxrss`.
- Bytes to a phone: `--bench-clients 1 --bench-record F`, the stream's size over the frame
  count the bench prints.
- The stall experiments: vtt on a pty I opened from Python (`openpty`, 200x50), keys written
  to the master 8-10 ms apart, the channel probed with `vtt --ctl status` under a 3 s timeout.
- Fixtures: `tools/genmap.sh` (40x25/24 creatures, 200x200/24, 200x200/500, 200x200/5000,
  512x512 empty, 512x512/500), plus the 36-byte `void512.vtt` perf.sh also makes.

---

## 1. Performance bottlenecks

Severity: **High** = can stop or visibly stall the GM, the phones or the agents in ordinary
use; **Medium** = a real cost that is bounded today but is a property the rewrite must decide
on; **Low** = cheap to fix, small to lose.

### B1 (High, design): one blocking write to the terminal stalls everything else

The loop is one thread: `poll` over stdin, the signal pipe, the phones and the channel
(`src/main.c:567-605`), then `app_tick`, keys, `app_events_flush`, and a frame. Every
network write is non-blocking with a bounded user buffer (`src/net.c:273-311`,
`src/ctl.c:217-229`). The one exception is the terminal: its descriptors are left blocking
on purpose (`src/term.c:84-89`: O_NONBLOCK on stdin would also set it on stdout, the same
open file description), and `term_write` waits in `poll(POLLOUT, -1)` until the tty drains
(`src/term.c:199-233`); main.c says so (`:678`, "falling behind is waited out inside
term_write()").

**Measured.** vtt on a pty whose master I stopped reading, 80 help-page toggles 10 ms apart:
`/proc/PID/wchan` = `wait_woken` (blocked in the tty write); `vtt --ctl status` got no answer
and gave up after its 2.00 s probe (`exit=2`, the client's "no vtt is taking requests");
49,314 bytes drained from the pty and the same probe answered in 0.00 s. So roughly 50-60 KB
of unread output (pty buffer plus the ldisc) is enough: an ssh/mosh stall, a terminal paused
with ctrl-S, a tmux client detached without a reader, or a slow serial console freezes the
phones (no frames, no pings answered — after 60 s they are dropped as idle, `net.c:893`),
the channel (`--apply` is told "did not say whether it has the map open" and does nothing,
`src/ctl.c:666-670` — safe, but every agent is stopped), the autosave and held `wait`s'
deadlines. Property of the design (single sink, blocking), not of an implementation slip.

What 2.0 should do: open the output as its own description (`open("/dev/tty", O_WRONLY |
O_NONBLOCK)`, or `dup` + a pipe-free trick is not enough; it must be a separate open), keep
the frame's bytes in a pending buffer, add the fd to `poll` with POLLOUT only while bytes
are pending, and **coalesce**: if a frame is drawn while the previous one has not fully
gone out, diff against the last *delivered* front (the renderer already keeps that invariant
on a short write, `src/render.c:233-250`, by forcing a full repaint; the cheaper rule is to
append the new diff and let the next write carry both). Speed of light: zero extra bytes,
one more pollfd. Nothing else in the loop needs to change.

### B2 (High, design): agent requests run inside the GM's key loop, unbudgeted

`app_tick` runs every READY request to completion, all of them in one turn
(`src/app.c:431-450`: `while ((i = ctl_next(...)) >= 0) { ... app_ctl_exec ... }`), before
the keys of that turn are handled and the frame drawn.

**Measured.** `dump` of an empty 512x512 map inside the bench loop: frame p99 **5.64 ms**
(p50 24.6 µs) — the `ctl` zone in PERFORMANCE.md's table reads the same (5.2 ms p50). A
fill-the-map proposal (`tile A1:SR512 floor`): p99 1.57 ms in my run; the published
proposals table has the plan 3.3 ms, the diff 2.5 ms, the accept 4.3 ms for that case. With
four connections READY at once (CTL_MAX_CONN is 4, `src/ctl.h:17`) one turn can hold four
dumps: ~22 ms, past the 16 ms budget. **Checked against review 1 S1-4**: the dump of the
512x512 map with 500 creatures is 550,097 bytes (529,722 empty); `describe` is 16,067 bytes
text / 36,691 JSON at 500 creatures and scales with the creature count; the region dump
`B2:U21` is 1,428 bytes.

Also measured: a dump request allocates about **3.2 MB of transient memory for a 530 KB
answer** (the shim: 10 dumps requested 35.2 MB against 6.7 MB for one; `open_memstream`
doubling in `app_ctl_exec`, `src/app_ctl.c:1449`, plus the 64 KB `in` buffer malloc'd per
accepted connection, `src/ctl.c:268`). Property of the design (synchronous, no budget); the
dump's size is an implementation choice review 1 already covers (R3).

### B3 (High for the 2.0 design, Medium today): the file is O(map area), and it is flushed on keys

The format writes every row of tiles, vedges and hedges whole, one character a square
(`src/mapio.c:330`, `fwrite(line, 1, w + 1, f)` per row). **Measured:** the 36-byte
`void512.vtt` (a header and `size 512 512`, no rows) saves back as **789,068 bytes**; the
empty and the walled 512x512 are 789,069 and 789,074; 200x200 is 122 KB. Load is 2.7 ms
(3.4 ns a byte, HEALTH B10), the writer 1.18 ms unflushed (B9), an outside change parses
twice: 5.9 ms (PERFORMANCE *Other writers*). The size follows the canvas, never the content.

The flush is on the key path: `write_map` fsyncs before the rename (`src/mapio.c:357-371`),
2.0-2.2 ms on this disk (the speed-of-light table), and the laptop's `:w` "runs fifteen times
this". The worst key in the whole perf table is a trip through a link to another map:
`link.trip.map` 4.3 ms p50, **16.5 ms worst** (`input.key` worst 16,474 µs), which saves the
map it leaves (flushed) and loads the next. The autosave is unflushed (`mapio_write_unflushed`,
`src/app.c:489`), which is the right tier. Design properties: dense rows, and durability
bought synchronously on a keystroke.

### B4 (Medium, design): per-frame cost grows with the creature count, not the window

The drawing culls to the window (`grid_visible_tiles`, `src/grid.c:347`, called at :390,
:547, :716, :769 and `src/play.c:478, 660`), so the frame is flat in map area: **measured**
22.6 µs on 40x25 and 22.6 µs on 512x512 (80x24 window). It is not flat in creatures: several
full walks a frame — `tokens_any_hidden` in `app_view_differs` (`src/app_draw.c:558`),
`turn_acting`, `floor.pick`, the panel, the cursor's size (PERFORMANCE *The budget* names
them) — about 18 ns a creature a frame. **Measured:** play cursor walk, 200x200 map:
24 creatures 22.0 µs, 500 27.1 µs, **5,000 117.5 µs** p50 (146 µs p99). Still 7x inside
the budget, but it is the one axis the frame scales on. `--check` at 5,000 creatures: 91 ms
(16 ms at 500 on the larger map), with 60,650 overlap errors in my fixture — the overlap
check is pairwise. Property of the design (one flat list, linear questions).

### B5 (Medium, design, keep): the players' frame is a second draw or a second copy, per frame

When the views can differ the players' frame is drawn again (`net.players_frame`, 33 µs p50
in the table, `src/app_draw.c:589-594`); when they cannot, the GM's back buffer is copied
(160 KB at 200x50, `rnd_copy_back`, `src/render.c:141-145`) and diffed again. With fog on,
`fog_any` makes the views differ on **every** play frame (`src/app_draw.c:541`). Then one
`write` per client (3.8 µs each, the floor). **Measured:** `play, 24 + 4 clients` 38.8 µs
against 22.0 µs with none. This is the privacy boundary built as two renderers, and it is
the right construction: bounded by the window, impossible to leak by omission. Keep it;
2.0 can only shave the copy (draw the players' view directly when it differs, skip the copy
when no client is live — already done, `wanted`, `:574`).

### B6 (Medium, implementation): a proposal copies the whole map, and the diff is 17 ns a cell

Every request's first edit line copies the live map into the kept scratch map
(`proposal_begin` → `app_scratch_copy`, `src/app_ctl.c:979-989`): 17.7-28 µs at 512x512
(the table), O(map) for a 650-cell plan whose own work is ~2 µs. The diff then costs ~17 ns
a changed cell against a floor near 2 (HEALTH B2: bucketing and the dedupe hash). The
checkpoint, by contrast, is already copy-on-write by cell with an inline note costing ~2 ns a
cell (`src/checkpoint.h:51-66`), and allocates `blocks × 1,152` bytes at start — 1.25 MB at
512x512 (`src/checkpoint.c:66-70`), untouched until a block is noted. **Measured** RSS: a
fill-the-map proposal on 512x512 peaks at 6.2 MB total; the whole process is 3.4 MB on the
40x25 map, 5.2 MB on 512x512 with 500 creatures. HEALTH 44 (change sets unbounded: 6 MB a
fill, 16 jobs ~100 MB) stands. Allocation per request at steady state: a handful (the shim:
fill proposal 19 mallocs for 1 request, 23 for 3), because scratch map, change set and
checkpoint buffers are kept. Implementation: the structure to copy is the overlay, not the
map (see P3).

### B7 (Medium, design): the channel's connection model

One connection per request; `accept` mallocs a 64 KB request buffer each time
(`src/ctl.c:268`), freed on a held `wait` (`:337`); 4 in-flight + 4 held waits
(`src/ctl.h:17-20`), the fifth is refused with "as many agents as there is room for are
waiting already" (`src/app.c:446`) or dropped at accept (`src/ctl.c:260`). Every `vtt --ctl`
and `--apply` walks every socket in the directory and gives each vtt 2 s to answer
(`ask_each`, `CTL_FIND_MS`, `src/ctl.c:478-520`); opening a map blocks the loop up to 2 s
per silent vtt (HEALTH 35). **Measured:** the healthy round trip through a fresh process is
~1 ms (PERFORMANCE: `--apply` 1.06-1.17 ms, mostly waking the sleeper); the socket round trip
itself is 3.1 µs. The limits are fine for one agent; they are the wrong shape for review 1's
identity (R4) and for more than four listeners. Design property.

### B8 (Low-Medium, implementation): a lagging phone is not "dropped, never waited for" until the kernel is full

The budget says a stalled client is dropped (REMOTE.md *Shape*). The code drops only when the
64 KB user buffer would overflow (`client_queue`, `src/net.c:277-285`), which happens only
after the kernel's send buffer has refused a write (`client_flush` returns on EAGAIN,
`:304`). **Measured:** a raw watcher with SO_RCVBUF 2048 that never read, 450 play-mode pan
keys at 200x50 (458 KB to the GM's terminal meanwhile): the GM's loop stayed live (`--ctl
status` answered in 1 ms), the client was **still connected** and had **368 KB** queued for
it (drained on resume). On loopback the kernel absorbs a few MB before EAGAIN; so a phone
that wakes from a stall replays every intermediate DIFF instead of getting one FULL. Nothing
blocks (good); the lag is unbounded in frames (not what the design says). Fix is cheap:
track bytes-in-flight per client (or `last_tx_ms` against pending) and, past N frames of
backlog, mark the client stale and send a FULL instead of queuing diffs.

### B9 (Low-Medium, implementation): the frame buffer for phones is a fixed 128 KB, and overflow closes the client

`NET_FRAME_CAP` is 128 KB (`src/net.h:25`); a FULL that overflows closes the joining client
(`client_send_full`, `src/net.c:354`) and an overflowing DIFF is dropped with `stale` left set,
so the next frame tries a FULL again (`:948-951`). **Measured:** a FULL at 200x50 is 20,511
bytes (50 runs, one a row: 8-byte header + 2 bytes a glyph). **Inferred from the format:** the
worst case is a run per cell, ~10 bytes a cell; a 200x50 window with a color change every
cell is ~100 KB, a 250x60 one is over the cap — phones could never join, with no message.
Size the encoder's buffer from the renderer at resize (`w*h*10 + palette`), one malloc per
resize.

### B10 (Low, implementation): O(n²) in the file picker; disk I/O on a key

`picker.open` is 3.2 ms with 500 character files (the table). `scan_dir` dedups each entry
against the whole list with `strcmp` (`src/mapio.c:1463-1466`): 125,000 compares at 500
files, 12.5 M at 5,000 — on a keystroke, plus a `stat` per file. Sort first, dedup adjacent.

### B11 (Low, implementation): `undo.trim` moves the log

Past 1 M ops the oldest quarter is dropped by `memmove` of up to 15 MB of ops and the token
side array, then every op's `tok` is rewritten (`src/undo.c:54-78`): 2.3 ms once per quarter
log (the table). **Measured:** 12 fills of 512x512 drove RSS to 32 MB (the cap holds: "a
little over 20 MB at the very worst", `src/undo.h:49-55`). A ring with a base offset makes it
O(1). The op itself — 20 bytes with payloads out of line — is right; keep it.

### B12 (Low, known): process start for an agent

Double fork, `waitpid` for the middle child, and 253 `close()` calls (`src/app_agent.c:119-145`):
0.36 ms at p50. `posix_spawn` + `close_range` is tens of µs; fork's cost grows with RSS, which
the undo log can take to 30 MB.

### What is *not* a bottleneck (measured, so the rewrite does not fix it)

- **The frame path allocates nothing.** Shim counts for the same script at 1 loop and at 400
  loops: build cursor 12 → 24 mallocs (3,192 extra frames), play 500 creatures 12 → 30, fog
  sight and range radius likewise flat, 4 clients 56 total. The dozen extra are the bench's
  own (the trace, the autosave's path). No `malloc` per frame or per keystroke anywhere I
  drove.
- **Bytes to the terminal are at the floor.** A cursor step is 207-298 bytes, 10-20 cells; the
  renderer skips clean rows with one `memcmp` and swaps pointers instead of copying
  (`src/render.c:162-173, 244-247`); the 16-byte `Cell` with zero padding is what makes the
  row compare legal.
- **Bytes to a phone are smaller than to the terminal.** Measured streams: cursor walk 72 B a
  frame (298 to the terminal), carry 252 B, fog sight 2 B (the players' frame barely moves);
  build mode and the help page send nothing (the players' frame is blank until play mode,
  `app_remote_live`, `src/app.h:420`). The run format is the right one.
- **Idle is zero CPU**, every timer folded into one `poll` timeout (`src/main.c:577-599`); with
  phones attached the loop wakes once a second for keep-alives (`:580`).
- **Sight is box-bounded, not map-bounded**: `clear_box`/`rim_box` run over the union of the
  movers' reach boxes (`src/fog.c:212-240, 296-370`), the per-creature `vis` is cached; fog on
  costs the frame nothing (22.7 vs 22.6 µs).
- **Memory is small and bounded**: 3.4 MB RSS on a 40x25 map, 5.2 MB on 512x512 with 500
  creatures, 6.7 MB with a 512x512 dump in flight; the undo log is the only thing that grows,
  and it is capped. The fixed `App` is 247 KB (jobs 178 KB of it), `Map` 20 KB, `Token` 256 B.

---

## 2. Optimization strategies for 2.0

Each pattern gives the speed of light beside today's cost. "Keep" means the current construction
is at or near the floor and the rewrite should carry it over unchanged.

| path | speed of light | today | 2.0 |
|---|---|---|---|
| a keystroke frame to the terminal | the changed cells' bytes (~200 B, 1 µs to write) | 207-298 B, ~22 µs of drawing (11 ns a cell for 1,920 cells) | **keep** the diffing renderer, row memcmp, pointer swap, the cull. Drawing could be dirty-region but 22 µs is 0.14% of the budget |
| the same, terminal stalled | nothing lost, one pollfd | the whole process blocks (B1) | separate non-blocking output description; pending buffer; coalesce frames against the last delivered front |
| the players' frame | a second draw of the window, or a copy | 27 µs draw / 7 µs copy + diff; 3.8 µs a client write | **keep** two renderers (privacy by construction) |
| bytes to a phone | the changed runs | 72 B a cursor step; 20.5 KB a FULL at 200x50 | **keep** the wire; size the frame buffer from the window (B9); bound the lag in frames (B8) |
| an agent request | 3.1 µs round trip + the work | connect, accept, 64 KB malloc, close, ~1 ms with a process start; the work unbudgeted in the loop (B2) | persistent framed connection per agent (identity for free, events pushed, no `wait` slot accounting); a per-turn **budget** (e.g. 2 ms of request work, then keys and a frame; the rest resumes next turn — reads are on the live map, so a read that spans turns must snapshot or be re-entrant: simplest is a cap on answer size per turn with streaming, since the out path is already non-blocking) |
| a `dump` | the region's bytes | 530 KB and 3.2 MB transient for the whole map; 5.6 ms in the loop | region required above N squares (R3); answer buffer sized from the region (`rows × (w+1)`), no doubling |
| a proposal | the changed cells, twice (before/after) | 786 KB copy (18-28 µs) + 17 ns a changed cell | **one COW overlay type** (16x16 blocks, a bit a cell, as `checkpoint.h` already does) used for the scratch map, the checkpoint and `changes SEQ`; the scratch becomes an overlay over the live map, the diff is the overlay's noted cells; O(change) in time and memory |
| a change event / `changes SEQ` | the cells noted since SEQ | `checkpoint_changes` 1.6 µs for a dozen edits (then restarted: history is one deep) | keep each `map changed` event's ChangeSet in the 32-event ring up to a byte budget (a dozen edits ~100 B; a fill 2 MB — cap and say `lost`); `changes SEQ [REGION]` concatenates |
| a save | the file's bytes into the page cache: 0.16 ms for 789 KB; durability 2 ms | 1.18 ms writer (memstream doubling, B9) + 2 ms fsync on the key | one buffer sized from the map; `write` + `rename` on the key, `fsync` **after the frame** (next loop turn, or on quit/close) — the autosave is the unflushed tier already |
| the file's size | ∝ content | ∝ canvas (789 KB for a void 512x512) | sparse rows in the text format behind a new version number (`void ×512` runs, or omit void rows): the writer already "picks the lowest version that says everything", so small maps stay byte-identical and hand-editable |
| a load | ~1 ns a byte | 3.4 ns a byte (table search a character, B10) | 256-entry lookup tables, rows parsed in place from the kept `Map.base` |
| creature questions a frame | the visible creatures | several O(n) walks, 18 ns a creature | gen-stamped caches for the frame's yes/no questions (`any_hidden`, `acting`, the players' floor) — `pcam_for`/`box_gen` already do this pattern; or a per-block creature index shared by the cull, `tokens_at` and `--check` |
| the undo log | 20 B an op | 20 B an op; trim = memmove of 15 MB once a quarter log | **keep** the op; make the arrays rings |
| starting an agent | one `posix_spawn`: tens of µs | 0.36 ms (double fork, 253 closes) | `posix_spawn` with `POSIX_SPAWN_SETSID`, `close_range` |
| the picker | one `readdir` + sort | + O(n²) dedup, on a key | sort, then dedup adjacent |

**Concurrency model.** Stay single-threaded. Everything the loop talks to is already
non-blocking except the terminal (B1) and the disk flush (B3); fix those two and there is no
operation left that needs a thread. A thread would buy nothing the budget (B2) does not, and
would cost the invariant that `app_frame` sees one consistent `App`. If a flush must be
truly asynchronous rather than deferred, `fsync` in a short-lived child (`fork` + `fsync` +
`_exit` — the fd is shared) keeps the main thread single without locks; measure first,
deferring to the next turn is probably enough.

**Instrumentation to preserve, and add.** Keep `PROF_ZONE`, `--bench`/`--trace`, the
cells/bytes columns, `tools/median.py`, the speed-of-light discipline, `pagebench` with
checksums. Add: a *bytes to phones* column (REMOTE/PERFORMANCE *Known gaps* already asks),
an allocation counter in the bench (the shim approach: assert zero mallocs across loops of a
frame scenario), a `--bench-stall` that holds the terminal fd to pin B1's fix, and a row for
`ctl.wait` (HEALTH 8) once the loop body is a function (review 1 R8).

---

## 3. Scalability risks: what breaks first at 10x

In the order it would be met:

1. **A stalled terminal (not a 10x, a 1x).** The one failure that stops phones and agents
   outright today (B1). Any multi-hour session over ssh will meet it.
2. **Concurrent agents.** Hard cap 4 in flight + 4 waiting (`ctl.h:17-20`); the fifth is
   refused. Ten agents cannot connect, and four `dump`s in one turn are ~22 ms of frozen
   keyboard (B2). The per-request process model is ~1 ms a call. Breaks at 2x, not 10x.
3. **Map area.** `MAP_MAX_DIM` 512 bounds everything O(map) at 1-6 ms today (dump 5.6,
   outside change 5.9, trip 4-16, flushed save 2-3, copy 0.03). 10x area is impossible without
   raising the cap; at 1,600x1,600 those become 60 ms dumps, 60 ms reloads, 8 MB files, 50 MB
   undo fills, 12 MB checkpoint stores. The file format and the loader go first (B3), then
   the proposal copy (B6). If the cap stays, only the format's density matters.
4. **Creatures.** 5,000 is 117 µs a frame (fine), `--check` 91 ms, the picker's dedup
   quadratic, `describe` 160 KB. Comfortable at 10x, not at 100x (B4, B10).
5. **Phones.** Capped at 8; 3.8 µs a client a frame and a 20 KB FULL per join; the LAN is the
   limit before vtt is. The first real break is a large colorful window overflowing the
   128 KB frame cap (B9) — phones silently unable to join.
6. **Session length.** Nothing grows unbounded except by design (the session log file) and by
   HEALTH 44 (16 change sets, worst ~100 MB). The undo cap holds (32 MB RSS after 12 whole-map
   fills). Fork cost for `:ask` grows with RSS (B12).
7. **Jobs.** 16 slots, then "nothing more until some are reviewed" (`src/app_ctl.c:1043`).
   A design limit, cheap to raise once change sets are capped in bytes.

---

## 4. Verdict on review 1's recommendations, from the systems side

- **R1 one `REQUESTS[]` table** — agree; free at run time. Add a *cost class* column
  (read / edit / long) so the loop's budget (B2) can decide what runs now and what streams.
- **R2 typed envelope with codes; JSON on request** — agree; a code costs bytes in the tens.
  Caution from the byte counts: `describe json` is 2.0-2.3x the text (2,014 vs 903 B;
  36.7 vs 16.1 KB at 500 creatures) because of the duplicated `at`/`x,y` and the quoting.
  Keep line text as the wire's default and JSON opt-in, as R2 proposes — not JSON-only.
- **R3 `changes SEQ [REGION]`, `status seq`, `wait 0` fix, dump caps** — agree, with a
  construction: **do not** build `changes` by diffing maps (O(map), 42-54 µs at 512 and a
  second map held). Keep each event's ChangeSet in the ring up to a byte budget (above). The
  checkpoint's cost while running is ~2 ns a cell written and nothing on the frame
  (`agent, map changed event` 22.2 µs = `build, open`), so one per listening agent is
  affordable (1.25 MB store each at 512, lazily touched), but the ring is cheaper and gives
  history. The `dump` cap removes the single worst in-loop stall (B2).
- **R4 identity, per-job undo, review guard, PID table** — agree; identity is free with a
  persistent connection (B7) or a header line. `job N undo` for the last-landed job is the
  existing `ctl_stamp` check; for an older job it is `cs_apply` of the inverse set with
  `cs_check` first — the machinery exists and is O(change). No performance conflict.
- **R5 conflicts as data** — agree; `cs_check` is 0.5 µs, printing is the formatter.
- **R8 split `App`, loop body as `app_loop_once`** — agree strongly, and it is where B1 and
  B2 land: the loop function is what a test can drive with a held output fd and a budget.
  Splitting `App` has no run-time cost (it is one 247 KB struct either way).
- **R9 no MCP server inside vtt** — agree. One process per tool call is ~1 ms (measured
  `--apply`); a wrapper that holds one persistent connection (B7) is the cheap shape, and it
  lives outside the binary as R9 says.
- **Checked and confirmed from review 1:** the 541 KB dump (550,097 here with 500 creatures),
  `describe`'s growth with creatures, `app_tick` running requests synchronously. **Not
  checked:** the `wait 0` 60 s stall (S1-3), the review-mode `undo` (S1-5) — behavior, not
  mechanics; the testing reviewer should.
- **Conflicts:** none with performance. Two cautions: a JSON-only wire doubles bytes; a
  `changes` built from whole-map diffs would be O(map). Both avoidable as above.

---

## 5. Open questions for the maintainer

1. **Is blocking on the terminal acceptable?** The GM's screen is the product; phones and
   agents are secondary. If a stalled terminal should still serve phones and answer agents,
   2.0 needs the separate non-blocking output description and frame coalescing (B1). If not,
   say so in the design, and let `--apply`'s "did not say" stand as the agents' answer.
2. **A per-turn budget for agent work** (e.g. 2 ms, then keys and a frame): acceptable that a
   large read is answered over several turns, or should large reads be refused (R3's cap)
   and that be the whole answer to B2?
3. **Does `MAP_MAX_DIM` stay 512?** If yes, every O(map) path is bounded at ~6 ms and only
   the file's density (B3) and the flush-on-key matter. If it may grow, the COW overlay (P3)
   and the sparse format become load-bearing.
4. **Durability tier.** Is `fsync` on `:w` and on a trip (2-16 ms on the key) required, or is
   write+rename on the key with the flush deferred to the next turn / on close acceptable,
   given the unflushed autosave already exists?
5. **File format.** Stay dense text (diffable, hand-editable at any size) or add sparse rows
   behind a version number so a void-heavy map is small? Both can coexist via "lowest version
   that says everything".
6. **Agents: how many at once, and connection model.** Persistent connections (identity,
   pushed events, no 2 s directory probe per call) vs today's one-shot `vtt --ctl` process
   per call (~1 ms)? Four concurrent is the current ceiling.
7. **Phones: biggest window to support**, and should a lagging phone be resynced with a FULL
   after N frames rather than queued (B8)? Should the frame buffer follow the window (B9)?
8. **Creature scale.** Is 500 the design point (linear walks are fine) or should 2.0 index
   creatures by block so the frame and `tokens_at` follow the window?
9. **Threads.** My recommendation is none; is there an appetite for an fsync helper (child
   process or thread) if deferring the flush is not enough?
10. **Budget for transient allocation.** The frame path is allocation-free today (measured);
    should 2.0 make that a tested invariant (a shim in the bench), and extend it to "a
    request allocates O(its answer)" (the dump's 3.2 MB for 530 KB)?
