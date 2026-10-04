# The phone page's speed

Where a frame goes on a phone, measured 2026-10-04, and the plan it led to. A plan awaiting
sign-off. docs/REMOTE.md is the page's design; docs/PERFORMANCE.md, *The phone page*, keeps
the current numbers. The standing rule (CLAUDE.md): the page stays inside one round trip,
and below that the effort goes into its JavaScript being close to optimal at run time.

## How it was measured

- **In Chrome**, against a busy 120×40 map served by a live vtt, keys driven from the GM's
  side, the page's functions and `putImageData` timed by totals (`tools/pageprobe.js`).
  Chrome's clock moves in 0.1 ms steps here, and the window was hidden, so frames were
  presented at once rather than at an animation frame: this times the work, not the delay
  from key to screen.
- **In node**, the stream a phone is sent, saved by `vtt --bench --bench-record`, replayed
  through the page as served (`tools/pagebench.sh`, `tools/pagebench.js`), on a phone's
  screen (915×412 at 2.625). Every replay ends with a checksum of the framebuffer, so a
  change meant to be faster, not different, can be shown to draw the same pixels.
- The first node harness ran the page under `vm` and then `with`, which made every global
  name a slow lookup: it put decoding at 550 µs a frame and pointed at the wrong fix.
  Running the page as a plain function put it at 57 µs.

## What a frame costs

A cursor step over the busy map, in Chrome: **684 µs**.

| part | µs a frame | share |
|---|---|---|
| `putImageData`: the push to the canvas | 393 | 57% |
| the copy into the framebuffer (`blitRow`, glyph tiles) | 144 | 21% |
| `status()`: the bottom-left line's HTML, rewritten every frame | 121 | 18% |
| decoding (`feed`) | 25 | 4% |

In node, on the phone's screen (median of three):

| scenario | decode | copy | px pushed a frame |
|---|---|---|---|
| cursor walk | 8.7 µs | 18.4 µs | 737,280 |
| cursor, every edge walled | 7.7 µs | 38.4 µs | 324,849 |
| carry a creature | 7.2 µs | 77.9 µs | 662,723 |
| pan a 200×200 map | 56.9 µs | 803.3 µs | 1,045,987 |

## The plan

### 1. Push bands of rows, not one bounding box

**Now:** more than four dirty rows are pushed as one box round all of them. A cursor step
dirties five rows that span the screen -- the column letters at the top, three round the
cursor, the status line at the bottom -- so the box is most of the canvas: 737,280 pixels
to change about five rows.

**Change:** consecutive dirty rows become one band, its width the widest of them, and each
band is one `putImageData`. A full redraw is still one push; scattered rows are a few small
ones.

**Measured** (a prototype in a copy of the page, same checksums): pixels pushed a frame,
cursor walk 737,280 → 20,893 (−97%); walled 324,849 → 83,859; carry 662,723 → 165,367; a
pan unchanged. In Chrome the push was 393 µs for 356,000 pixels on a cursor step; at about
21,000 it should be a few tens of µs. Confirmed with `pageprobe.js` after the change.

**Tradeoff:** a frame with many short, separated bands makes more calls, each with a fixed
cost. A band is merged with the next when the gap between them is a row or two, if the
measurement after says that helps; not before.

### 2. Stop rebuilding the status line every frame

**Now:** `status()` sets `innerHTML` after every frame, with the frame's size, its time, the
cells it changed and a frame count -- a parse and a layout, 121 µs of a 684 µs frame.

**Change:** the line says only what a player needs -- *connecting*, *connected*,
*disconnected · retrying* -- set with `textContent`, and only when it changes. The frame
figures, a developer's readout, show only when the address carries `&stats`, refreshed at
most twice a second.

**Gain:** about 120 µs a frame in Chrome, and a few bytes of page.

**Tradeoff:** the figures are no longer on every phone by default; a GM chasing a slow
phone adds `&stats` to the address.

### 3. Decoding: not worth changing

The first harness said decoding was 30% of a frame; the corrected one says 8.7 µs on a
cursor step and 57 µs on a full redraw. Two rewrites of the glyph loop (byte reads instead
of `DataView`; native `fill` and a block copy) were no faster, one slower. Left as it is.

### 4. The copy loop with SIMD: measured, a decision

The copy loop is hand-assembled WebAssembly copying 8 bytes at a time
(`tools/blit_wasm.py`). Variants, copy time a frame, median of five (same checksums):

| variant | cursor walk | carry | pan |
|---|---|---|---|
| 8-byte loop, tile by tile (now) | 19.6 µs | 76.9 µs | 801.9 µs |
| `memory.copy` | 29.1 | 112.7 | 1,165.1 |
| 16-byte SIMD, tile by tile | 20.6 | 70.1 | 711.0 |
| 8-byte, a pixel row at a time | 21.3 | 67.9 | 669.7 |
| 16-byte SIMD, a pixel row at a time | **18.4** | **56.1** | **585.1** |

On a 3× phone the last is 6-10% faster than now, on a 1× screen 3-12%; on the 2.625 phone
27% on a pan. `memory.copy` is slower still, as the module's own note says (a call into the
runtime per tile row).

**Tradeoff:** SIMD needs a browser from 2021 on, and Safari 16.4 (2023) on iOS. A module the
browser cannot compile must not take the page down, so it needs a fallback:

- **(a) Both modules:** SIMD, else today's loop. About +300 bytes; the page goes from 11.8 KB
  to about 12.0 KB (with item 2's saving), under the 12 KB limit by about 200 bytes.
- **(b) SIMD only, else the JavaScript copy:** about +100 bytes, but an older iPhone falls to
  the JavaScript loop, several times slower than today's.
- **(c) Leave it:** the gain is real but only on frames that redraw most of the screen
  (a pan, a big jump, a resize), 0.1-0.2 ms each.

Recommendation: **(a)** if it fits after 1 and 2, with the page size checked at the time; if
it would leave less than 150 bytes of margin, **(c)**.

## Build order

1. Bands (`present`), measured with `pagebench.sh` (pixels, checksum) and `pageprobe.js`.
2. The status line.
3. If signed off, the SIMD module in `tools/blit_wasm.py` (the variants kept there, as
   `memory.copy` is, with their numbers), the fallback, the size checked.
4. PERFORMANCE.md's phone table regenerated; the review (health-check questions included).
