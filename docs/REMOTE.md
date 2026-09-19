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

## Order

1. Wire format, server, watcher, tests.
2. `:serve`, `:mirror`, bench clients, perf rows.
3. The page.
4. README, `?` rows, PERFORMANCE.md.

Later phases, not designed here: streaming the model instead of cells so each
client draws at its own size (the map file format itself, plus a state line),
a touch map on the phone, and moves from a player's own device on their turn.
