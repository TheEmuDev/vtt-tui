# Remote view: design and budgets

Players watch the GM's map from their own devices -- a browser on a phone or
tablet, or a second terminal on a TV. The GM's `vtt` serves; everything else is
a client of the same stream. This is the plan phase one was built to; the
numbers are limits, not estimates, and `docs/PERFORMANCE.md` records what was
measured against them.

## Budgets

| where | limit |
|---|---|
| server, per frame per client | 2 µs, and zero bytes when nothing changed |
| server memory | fixed at `:serve`: one 64 KB send buffer per client, eight clients, no heap after that |
| the page | one request, under 12 KB, no fonts or scripts fetched, no framework |
| client memory | one cell buffer plus one glyph atlas, both allocated once |
| client, per keystroke frame | 0.5 ms on a mid-range phone; a full 200×50 frame in 5 ms |
| latency added by our code | one write per frame per client, sent the moment the GM's frame is |

## Shape

- **One listener, two kinds of client.** The first bytes decide: `GET` is a
  browser (the page, or an upgrade to WebSocket); the magic `VTT1` is a
  watcher on a raw socket. Both receive identical frame payloads.
- **The frame is the renderer's diff.** The flush already walks the changed
  cells to emit ANSI; with clients attached it hands each cell to the encoder
  as well. A new client gets a `FULL` from the front buffer, then `DIFF`s.
- **Runs, not cells.** A record is a run of consecutive cells in one row
  sharing colours: an 8-byte header and two bytes a glyph. Colours are
  one-byte indices into a palette sent as colours are first met. Glyphs are
  sixteen-bit; the second half of a wide glyph is a zero.
- **Coalescing at both ends.** The server writes one buffer per client per
  frame with Nagle off. The page folds frames into its cell buffer and paints
  the dirty rectangle once per animation frame.
- **A framebuffer on the client.** Glyphs are rasterised once into an atlas;
  a frame is typed-array copies into a pixel buffer and one `putImageData` of
  the dirty rectangle. Pinch zoom is a transform; the atlas is re-rasterised
  when the gesture ends. This loop is the one candidate for WebAssembly, if
  measuring ever says so.
- **A stalled client is dropped, never waited for**, and resynced with a
  `FULL` on reconnect.
- **The watcher is `vtt --watch host:port`**: the same decoder painting into
  the same renderer, at the GM's size, centred or clipped. `q` closes it.
- **Access.** LAN only, a join code in the URL, at most eight clients,
  request-size and idle timeouts. No TLS, deliberately.

## Commands

| | |
|---|---|
| `:serve` | open the listener; the status line shows the URL with its code |
| `:serve off` | close it and drop every client |
| `:mirror` | serve if needed, then open a detached terminal window running the watcher, via `$TERMINAL` |
| `vtt --watch host:port` | the watcher, for scripts, tests and other machines |

## Instrumentation

Zones `net.frame` (encode and write) and `net.accept`; bytes per client on
the profiler overlay beside bytes per frame; `--bench-clients N` attaches N
loopback clients to a bench run so the perf tables carry a row with sockets
on. The encoder and decoder are one file shared by server, watcher and
tests; the end-to-end test runs both over a socket pair. The page shows its
own frame time.

## Measured, phase one

| budget | measured |
|---|---|
| server, 2 µs per frame per client | 3.7 µs: the encode is shared, the rest is one `write` syscall per client. Missed by the syscall. |
| zero bytes when nothing changed | zero |
| server memory fixed at `:serve` | 64 KB send and 4 KB request buffer per client, one 128 KB frame buffer, all at start |
| the page under 12 KB, one request | 10 KB, nothing fetched |
| client memory allocated once | a cell buffer, the atlas, and one WebAssembly memory holding the framebuffer and the tile arena, reallocated only on resize |
| keystroke frame 0.5 ms on a phone | 0.2-0.5 ms in desktop Chrome for a cursor move; phones to be measured |
| full 200×50 frame in 5 ms | 0.21 µs a cell in the copy loop puts it near 2 ms plus the pixel push; a 1220-cell scroll measured 0.7-2 ms |
| one write per client per frame | yes, with Nagle off |

The copy loop went to WebAssembly after measurement, not before: JavaScript row copies
were 4-5 µs a cell, twenty times the budget. The module is hand-assembled, so the page
still needs no toolchain.

## Order

1. Wire format, server, watcher, tests.
2. `:serve`, `:mirror`, bench clients, perf rows.
3. The page.
4. README, `?` rows, PERFORMANCE.md.

## Phase two: a plan, awaiting sign-off

Phase one sends the GM's frame. Phase two lets the phones send something back.
Three steps, each shippable alone, in this order. The first is planned in full
in `docs/FOG.md` (part 3); the other two are planned here to the level of the
decisions, and get their own instrumentation section before code.

**1. The players' frame.** The server draws play mode a second time, the
players' way -- fog opaque, counters and notes absent -- and streams that.
Same wire, same page, same watcher. See `docs/FOG.md`.

**2. Pings.** A tap on the phone names a square. The page already knows the
cell under a touch; it sends `P x y` up the WebSocket (the socket is
bidirectional and unused upstream today). The server draws a ring around
that square on both frames for two seconds and puts `ping at C4` on the
status line; a second ping from the same phone replaces the first. Rate: one a
second per client, the rest dropped. Cost: one 4-byte message, one small
overlay. This is the whole of "a touch map" that an in-person table needs: the
player points, the GM looks. Pan and zoom the page does already.

**3. A move from the player's own device, on their turn.** Decisions:

- *Identity.* The page asks for a name once (kept in the browser), sent with
  the WebSocket request. `:serve who` lists the phones by name. The GM binds a
  phone to a creature with `:serve as Aria Bram` (phone "Aria" plays the
  creature "Bram"); a phone with no binding can only watch and ping.
- *Consent is the turn.* A bound phone gets a step pad only while its creature
  is acting (`TURN_ACTING`), and only in play mode. Each press sends `M dx dy`;
  the server applies `play_step` under the GM's own rules (walls on or off,
  creatures block) and the same undo batch shape a GM's move has, so `u` on
  the GM's keyboard takes it back. Put-down happens when the turn passes, or
  when the player taps "done"; there is no pick-up from a phone, since the
  creature is already theirs.
- *Nothing else.* No dice, no markers, no notes from a phone. The tool serves
  the table; the phone is a way to reach across it.

Cost: a message per step, the same move code the GM's keys run, no new
drawing. Zone `net.cmd`; a bench with a scripted client sending steps.

Model streaming (each phone drawing at its own size) stays out of scope: the
page fits the GM's frame to the screen well enough, and the players' frame
gets the privacy without it.
