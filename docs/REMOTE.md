# Remote view: design and budgets

> **Status: phase one built; of phase two the players' frame (2026-09-23) and pings
> (2026-09-24) are built, and moves from a player's device are set aside (docs/IDEAS.md).** A
> design record: the plan each phase was built to. What is true now is the README (*The
> players' view*) and the files' rows in docs/CODEMAP.md; the budgets here are still the
> limits. *Phase two* marks each part.

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
| the page | one request, under 12 KB as sent -- since 2026-10-23 the gzip a browser receives (tools/embed.sh drops its comments, then compresses it; about 5.4 KB) -- no fonts or scripts fetched, no framework. While vtt cannot be reached the page retries at 2 s doubling to 30 s. **Why 12 KB:** the page and its HTTP headers must arrive in TCP's first flight -- Linux sends 10 segments before it waits for an acknowledgement, about 14.4 KB on Wi-Fi -- so a load is one round trip. Past that every load costs another round trip (two at ~29 KB, three at ~58 KB). 12 KB keeps a margin for headers and segment size. The limit is hard: every millisecond matters, and performance slips away a little at a time. Below it, shaving bytes buys nothing; the effort goes into the JavaScript being close to optimal at run time (2026-10-04) |
| client memory | one cell buffer plus one glyph atlas, both allocated once |
| client, per keystroke frame | 0.5 ms on a mid-range phone; a full 200×50 frame in 5 ms |
| latency added by our code | one write per frame per client, sent the moment the GM's frame is |
| the phone's resources | a rule (the user, 2026-10-22): the page spends the phone's CPU, memory and battery only on what the players see, so they get the best experience we can give. Nothing runs on a timer or a loop of its own while the screen is still (today every timer starts from an event -- a frame, a resize, a close -- except that while vtt cannot be reached the page retries every 2 s without end: a backoff is in the page plan, docs/V2.md *Next*); a feature that costs the phone idle work (HEALTH B16's video stand-in measured 2.5-3x Chrome's idle CPU in one run each) needs that cost measured first and is weighed against this rule |

## Shape

- **One listener, two kinds of client.** The first bytes decide: `GET` is a
  browser (the page, or an upgrade to WebSocket); the magic `VTT1` is a
  watcher on a raw socket. Both receive identical frame payloads.
- **The frame is the players' renderer's diff.** The server keeps a second
  renderer, and each frame the app either draws the players' view into it
  (no prompt, no modal, no profiler, no note hint) or, when the two views
  cannot differ, copies the GM's back buffer across. Its flush hands each
  changed cell to the encoder, so its front buffer is always exactly what
  the clients show. A new client gets a `FULL` from that front, then `DIFF`s.
  That front is the players' from the first frame after `:serve`, drawn or blank --
  until 2026-09-27 it was the GM's own terminal until a client first wanted a frame,
  so a phone joining then was sent the GM's screen (found building handouts).
- **The handout** (docs/HANDOUTS.md) is an `'H'` record, not a frame: its text, sent
  when it changes and after a new client's `FULL`. The page draws it as an HTML card;
  the terminal mirror as a box.
- **Runs, not cells.** A record is a run of consecutive cells in one row
  sharing colors: an 8-byte header and two bytes a glyph. Colors are
  one-byte indices into a palette sent as colors are first met. Glyphs are
  sixteen-bit; the second half of a wide glyph is a zero.
- **Coalescing at both ends.** The server writes one buffer per client per
  frame with Nagle off. The page folds frames into its cell buffer and paints
  the dirty rectangle once per animation frame.
- **A framebuffer on the client.** Glyphs are rasterized once into an atlas;
  a frame is typed-array copies into a pixel buffer and one `putImageData` of
  the dirty rectangle. Pinch zoom is a transform; the atlas is re-rasterized
  when the gesture ends. This loop is the one candidate for WebAssembly, if
  measuring ever says so.
- **A stalled client is dropped, never waited for**, and resynced with a
  `FULL` on reconnect. *Since 2026-10-21 (docs/V2.md, step 7):* a stalled client is
  no longer dropped; frames are withheld from it once it is a FULL behind, and it is
  sent one FULL when it reads again. A drop vtt makes is told to the GM.
  Checked on a phone (2026-10-21, `tools/phonecheck.sh`; docs/PERFORMANCE.md, *A phone that
  falls behind*): with the screen off it is caught up as built; behind the home screen,
  Android's Chrome closes the socket within about 5 s; the page makes a fresh socket as it is
  shown again, open within 32-72 ms (5 s before: HEALTH.md B15, closed).
- **The watcher is `vtt --watch host:port`**: the same decoder painting into
  the same renderer, at the GM's size, centered or clipped. `q` closes it.
- **Access.** LAN only, a join code in the URL, at most eight clients,
  a request-size limit. No TLS, deliberately.
- **Liveness.** A browser silent for 15 s is sent a WebSocket ping, which
  browsers answer by themselves, hidden tab or not; a minute without an
  answer (three pings) and it is dropped, counted in `Net.idle_dropped` beside
  `dropped` (too slow to keep up), and resynced with a FULL when it comes
  back. A watcher gets a `'Z'` record when its line is quiet and, once its
  hello is read, is never dropped for silence. A request or a hello not
  finished within a minute is dropped, so connections that never finish
  cannot hold the eight slots. Before 2026-09-25 a browser was sent the `'Z'`,
  never answered, and was dropped and reconnected every minute.
- **The server belongs to the encounter.** Closing the map closes it and drops
  every client, because the players were watching that map. `--stay-alive`
  keeps one server, and one join code, across a session of several
  encounters. Nothing outlives the process; the kernel sees to that.

## Commands

| | |
|---|---|
| `:serve` | open the listener; the status line shows the URL with its code |
| `:serve --stay-alive` | keep it open when the map closes; `--no-stay-alive` takes that back |
| `:serve off` | close it and drop every client |
| `:mirror` | serve if needed, then open a detached terminal window running the watcher, via `$TERMINAL` |
| `:player preview` | the players' frame on the GM's own screen; `q` returns |
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
| server, 2 µs per frame per client | 3.7 µs: the encode is shared, the rest is one `write` syscall per client. Missed by the syscall. Since the players' frame: plus 7 µs once per frame for the copy and second diff, or a second draw of about 27 µs when the views differ -- `docs/PERFORMANCE.md` has the rows. |
| zero bytes when nothing changed | zero |
| server memory fixed at `:serve` | 64 KB send and 4 KB request buffer per client, one 128 KB frame buffer, all at start. *Since 2026-10-21 (step 7):* the frame buffer and the send queues grow when a frame of the window does not fit, to its worst case (12 bytes a cell), and stay at the largest used |
| the page under 12 KB, one request | 11.4 KB (2026-10-04: two copy loops, trailing `//` comments no longer sent), nothing fetched |
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

## Phase two

Phase one sends the GM's frame. Phase two lets the phones send something back.
Three steps, each shippable alone, in this order. The first is planned in full
in `docs/FOG.md` (part 3); the other two are planned here to the level of the
decisions, and get their own instrumentation section before code.

**1. The players' frame.** *Built.* The server draws play mode a second time,
the players' way -- prompts, modals, the profiler and the note hint absent,
fog and counters to follow -- and streams that. Same wire, same page, same
watcher. See `docs/FOG.md`.

**2. Pings.** *Built 2026-09-24* (server, app and page; checked by tapping in Chrome). Budgets, set before code and
met: parsing a tap under 1µs (0.1), a ring under 2µs to draw (0.6), under 16 bytes up
the wire a tap, the downstream cost read off the bench's bytes column (a few hundred bytes
a tap). Decisions taken: the page sends the screen cell and the server turns it into a
square with the players' frame's own layout; a ping into the dark is accepted, and the
players see a ring only round what they can see; not logged; anonymous (a connection id);
`:serve --no-pings`; the watcher stays watch-only; the GM pings with `g p`, without a
server. Taps are one a second per client into a fixed inbox; a bad payload is ignored,
bad framing closes. Was: A tap on the phone names a square. The page already knows the
cell under a touch; it sends `P x y` up the WebSocket (the socket is
bidirectional and unused upstream today). The server draws a ring around
that square on both frames for two seconds and puts `ping at C4` on the
status line; a second ping from the same phone replaces the first. Rate: one a
second per client, the rest dropped. Cost: one 4-byte message, one small
overlay. This is the whole of "a touch map" that an in-person table needs: the
player points, the GM looks. Pan and zoom the page does already.

**3. A move from the player's own device, on their turn.** *Set aside
2026-09-25*: at an in-person table a ping does the job -- the player points, the
GM moves the creature. The full plan is in `docs/IDEAS.md` under *Moves from a
player's phone*; what follows is the original sketch. Decisions:

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

## The page revisited (2026-10-22): the plan

**Status:** written 2026-10-22; signed off 2026-10-23 ("Go with recommendations"); items 1-6
built 2026-10-23 (*As built*, below); their timings wait on the user's go; item 6's size is the
user's once a phone reports its high-water mark; item 7 not yet (the user). Asked by the user: "revisit the phone-side code and see if it can be redesigned or
improved in some way that would give us more to work with"; a phone that opens the address
without the join code is asked for it; and, made a rule the same day, the page respects the
phone's CPU, memory and battery (*Budgets*). The byte table and options below are a planning
agent's, measured with tools/embed.sh's own cut unless marked an estimate.

### Where the 11,816 bytes go (measured)

| part | sent bytes |
|---|---|
| HTML head, CSS, DOM | 2,017 |
| the decoder `feed()` | 1,227 |
| `layout` | 1,186 |
| tiles (`tile`, `clearTiles`) | 1,145 |
| `present`, `schedule`, `touch` | 875 |
| the connection and the wake lock | 757 |
| the name sheet | 756 |
| `blitRow` and its JS copy | 633 |
| the glyph atlas | 625 |
| the two WebAssembly modules (base64) | 599 |
| handout, whisper, status, pointing, globals, the rest | 1,996 |

### The plan, in the recommended order

1. **The palette record stops throwing the tile cache away** (run time; the phone's battery).
   Today every `P` record (web/index.html:215) runs `clearTiles()` -- a fill of 65,536 slots --
   and forces a full repaint, so the first time any new color appears mid-session every phone
   rebuilds its whole screen, and a FULL with N palette entries fills the array N times. Built:
   a record whose color is the one held changes nothing; a changed one marks the cache stale,
   and the cache is cleared once at the end of the message. *Speed of light:* a palette record
   is 4 bytes stored; a clear is owed only when a color a tile was drawn with changed.
   *Expected:* no clear for a repeated color, one clear a message otherwise. About +40 bytes
   (estimate). *Measured with:* a `tools/pagebench.sh` scenario of a new color mid-session,
   before and after; `test_page_feed` keeps the drawing equal.
2. **The retry backs off while vtt cannot be reached** (the phone's battery). Today a page
   whose server is gone tries every 2 s without end (docs/REMOTE.md, *Budgets*). Built: 2 s,
   doubling to a cap (**30 s recommended**; the user's decision), back to 2 s once connected;
   being shown still reconnects at once (B15), so a long cap never delays a player who looks.
   *Expected:* attempts at 2, 6, 14, 30 and 60 s -- 5 in the first minute -- then 2 a
   minute, against 30 a minute today (worked out from the delays). Suite `pageconn`'s scenario F tightened to the backoff. About +30 bytes.
3. **The join code asked for on a page of its own** (the user's request). Today a phone that
   opens `http://IP:PORT/` without `?k=` gets a plain-text 403. Built: that 403 becomes a small
   HTML form -- a number field and a button, `GET /?k=CODE`, no JavaScript -- and a wrong code
   gets the same form saying so. It is its own response, not part of the 12 KB page (about 300
   bytes, estimate). **Wrong codes are not limited today** (src/net.c:697-701, checked): with a
   form inviting typing, the plan adds a limit of wrong codes a minute across all addresses
   (**10 recommended**; a right code is never held back, so the limit cannot lock out a player
   with the code; at 10 a minute half the million codes take about 35 days, estimate). Tests:
   the form served without a code, a wrong code, the limit, a right code past the limit.
   Related, the user's decision: **a fixed default port** (today a new port at each start
   unless `:serve PORT`), so a player can bookmark `http://IP:PORT/` and type only the code.
4. **The page sent compressed** (bytes; the user's decision: the 12 KB rule would mean the
   bytes a browser receives). gzip -9 takes the page from 11,816 to **5,330 bytes** (measured),
   freeing about 6.5 KB for whatever comes next. vtt needs no zlib: tools/embed.sh stores a
   second, pre-compressed constant (python's `gzip.compress(..., mtime=0)`, so the output is
   the same every time); the server sends it with `Content-Encoding: gzip` and `Vary:
   Accept-Encoding` when the request accepts gzip (every browser does over http), the raw page
   otherwise (curl, the tests). *Speed of light:* one write of about 5.5 KB instead of 12 KB;
   the phone inflates it in about 0.2 ms (estimate: 44 µs here, measured with python's zlib).
   The binary grows about 5.3 KB. Tests: the budget check moves to the compressed length; node's
   `zlib.gunzipSync` must give back the page exactly. Once in, "spend nothing shaving bytes"
   holds again.
5. **Small run-time fixes** (each a few bytes): the window's `resize` debounced as the
   visual viewport's already is (line 201: a rotation can lay out, and allocate the wasm
   memory, several times); `status()` writes `st.hidden` only when it changes (today once a
   frame); `feed` reuses one `DataView` rather than making one a message. *Measured with:*
   pagebench, before and after.
6. **The tile arena's size, measured first.** It is sized for 4,096 tiles (about 3.3 MB at
   200x50 on the user's phone, estimate) while the code's comment says a frame has a few
   hundred. Built first: the high-water mark of tiles in use, shown with `&stats`; the size is
   then the user's decision, on that number.
7. **The wake lock** (HEALTH B16): the stand-in's screen test is on 2026-10-23; one run each
   showed 2.5-3x Chrome's idle CPU, against the rule, so it is likely rejected and the README's
   advice stands.

### Not taken, and why

- Dropping old Safari's plain WebAssembly (267 bytes) or the JS copy (about 216): every player
  on an old device would pay 20x the copy cost; with compression the bytes do not matter.
- Deriving the plain module from the SIMD one at load (about 190 bytes): unproven, for the
  same few devices.
- Whitespace minification (618 raw, about 146 compressed) and shorter names (about 1.0-1.3 KB):
  a second minifier in C to keep `embed_cut` alike, and a page no one can read on the phone.
- Rarely used UI in a second request, or sent over the WebSocket: breaks "one request", or
  changes the wire, for about 2 KB that compression frees anyway.

### Decisions for the user (answered 2026-10-23, the user: "Go with recommendations": 1-3 as recommended; 4 after item 6 measures it)

1. Compression, and the 12 KB rule redefined as the compressed bytes sent (recommended: yes).
2. The retry's cap (recommended 30 s).
3. The wrong-code limit (recommended 10 a minute) and a fixed default port (recommended:
   yes, 7793 when free, a new one when not).
4. The arena's size, once item 6 has measured it.

### As built (2026-10-23)

1. **The palette record:** a repeated color changes nothing; a changed one marks the tiles
   stale, and they are cleared once at the end of the message. The changed color arrives in
   practice after a reconnect: when the last phone leaves, the server's palette starts again
   (net.c, `wire_enc_reset_palette`) and a page that reconnects without reloading is sent its
   new indices. tests/page_feed.js counts the page's clears (`clears` mode); suite `pagefeed`
   holds a repeated color to none and two changed ones in a message to one (mutation-checked:
   it had no case where a stale tile could show, and still would not, since the tile cache is
   stubbed there).
2. **The retry** backs off 2 s doubling to 30 s, back to 2 s once a socket opens; being shown
   still reconnects at once. Suite `pageconn`: F, two minutes down, 8 attempts (0, 2, 6, 14,
   30, 60, 90, 120 s; 61 before); G, the wait back at 2 s after a connection. The doubling,
   the cap and the reset each mutation-caught.
3. **The join-code form:** the page's request with no code, or a wrong one, is answered by a
   small form of its own (403; `code_form`), no script, `GET /?k=`. **Changed from the plan:**
   the plan said a right code is never held back, but then the limit only slows a guesser,
   never stops one -- the plan's own 35-day estimate assumed codes past the limit go
   unchecked. Built: past 10 wrong codes in a minute, from any address, no code is checked
   for the rest of the minute (429, "try again in a minute"), except from an address that has
   given the right code since the server started (`Net.known`, 16): the players in are never
   locked out, and a new one waits at most a minute while someone guesses. A watcher's hello
   goes through the same `code_check`. **The port:** `:serve`, `--serve` and `:watch`'s own
   server take 7793 (`NET_DEFAULT_PORT`), another when it is taken; `VTT_SERVE_PORT` overrides
   it, and the tests set it to 0 (run.c's main and both sandboxes), so no suite takes the port
   the user's vtt may want. Suite `netserver`: the form both ways, the limit, a right code from
   a new address held back, the known one let in, a minute on; the default port and its
   fallback. Each rule mutation-caught.
4. **Compression:** tools/embed.sh writes `WEBPAGE_GZ`, python's `gzip.compress(..., 9,
   mtime=0)` of the cut page: **5,443 bytes against 12,032** uncompressed (at this commit,
   items 5 and 6 in). Sent with `Content-Encoding: gzip` and `Vary: Accept-Encoding` to a
   request that accepts gzip; the raw page otherwise. The 12 KB budget is now the compressed
   bytes (suite `webpage`, which also inflates it under node and compares with the page).
5. **The small fixes:** the window's `resize` debounced with the viewport's (one `relayout`);
   `status()` writes `st.hidden` only when it changes; `feed()` reads 16-bit fields with
   `u16()` and makes no `DataView` a message (tests/page_feed.js lifts `u16` from the page).
6. **The arena's high-water mark:** the most tiles held and arena bytes used, shown with
   `&stats` (`tiles N, U of A KB`). Its size is the user's decision once a phone has shown it.

Not yet measured: the before-and-after timings items 1 and 5 call for (`tools/pagebench.sh`),
and the time of a FULL with the page compressed on the phone: they wait on the user's go.

