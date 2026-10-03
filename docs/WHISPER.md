# Whispers

`:whisper Aria You notice the floor is warm here`: a handout to one phone only. Roadmap
item 23. Signed off 2026-10-03 with the recommendations.

The whisper is the small part. The real work is that the phones have no names: today every
phone is an anonymous viewer of one shared frame. This gives each phone a name when it
joins, which later per-player features (item 21's camera, a ping that says who) can use too.

## Decisions

| question | answer |
|---|---|
| how a phone gets a name | on joining, the page asks *Who are you?* with a button for each player creature on the map (their labels, sent by the server; hidden ones and ones in the dark too -- every player is at the table), a box for anything else, and *Just watching* (no name, no whispers). The page sends `N Aria` up the WebSocket, as it sends `P col row` for a tap |
| asking again | the phone remembers its answer (the browser's local storage) and sends it by itself on every reconnect, so a phone that slept or a server restarted asks nobody twice. A small `Aria` button at the top of the page changes it |
| who may be who | anyone with the join code may say they are Aria -- this is a living room, and the code is the door. Two phones may both be Aria; both get Aria's whispers. The README says so plainly |
| a name | 1 to 24 bytes of UTF-8 text (the page trims what is typed to fit), control characters refused, case kept for showing, matched case aside |
| the terminal mirror | stays the table's: a watcher has no name and never gets a whisper, so a TV running `--watch` (or `:mirror`) shows nothing private |
| `:whisper NAME TEXT` | the name is the longest connected name the line starts with (so `Crypt Ghoul` works), then the text. GM-only messages: `whispered to Aria (1 phone)` |
| a phone asleep | phones lock their screens, and a locked phone drops off within a minute. A whisper to a name with no phone connected is **kept** -- the last one for each name, until delivered -- and the GM is told `Aria's phone is not here - it gets this when it comes back`. A name never seen is refused: `no phone is Aria - :players lists who is here` |
| on the phone | a card like the handout's, marked *to you*, over the map and over any handout; the player closes it; a `whisper` button reopens the last one. A new whisper opens on top |
| what the GM sees | `:players`: the phones, by name (`Aria, Brin (2), 1 unnamed, 1 terminal`); a phone naming itself says so in the GM's status line (`Aria's phone is here`), not in the log. The session log keeps every whisper, GM-side |
| privacy | the whisper is a record sent to the named phones' sockets only -- never in the shared frame, never to a watcher, never to another phone. `app_view_differs` is untouched: nothing is drawn. The list of names the page offers is every player creature's label: the party's own names, which every player at the table knows |
| the server's part | per client a name; a `'W'` record (`u16 n`, UTF-8) sent to the clients of a name; an `'N'` record to browsers listing the names to offer, sent on join and when the player creatures change; the kept whispers (one per name, 16 names, 2 KB each) |
| not here | a prepared or picture handout to one player (`:handout NAME to Aria` is the natural next step; pictures are parked, docs/PICTURES.md), a phone's reply, the control channel, per-player fog |

## Where it goes

- `net.c/h`: `NetClient.name`; `N name` parsed in `net_msg` beside `P` (bad ones counted, as
  now); `net_whisper(n, name, text)` returning how many phones got it, keeping it when none;
  sending kept ones when a phone names itself; `net_set_names` for the offer list; a
  callback or flag so the app can say a phone arrived by name; `net_names` for `:players`.
- `wire.c/h`: `'W'` and `'N'` records (the watcher's decoder skips both -- it is the table).
- `app_cmd.c` or a small `app_whisper.c`: `:whisper`, `:players`; keeping the offer list in
  step with the player creatures (after each key, as the fog sync is, compared cheaply).
- `web/index.html`: the *Who are you?* sheet, local storage, the name button, the whisper
  card and its reopen button. The page is 10.1 KB of a 12 KB budget; if this does not fit, I
  will come back with what to trim rather than raise the budget quietly.
- Tests (`test_net.c`): names over real sockets, `N` edge cases (too long, empty, bad UTF-8,
  control bytes), a whisper reaching only its phones and never a watcher or another phone,
  kept for a sleeping phone and delivered on its return, two phones one name, the longest
  name match, the offer list following creatures (hidden left out); the page decoder test
  taking `'W'` and `'N'`.
- `PROF_ZONE("net.whisper")` and `"net.names"`; a perf row whispering with four phones named.
- README (the players' view: naming a phone, whispers, the trust note), docs/KEYS.md is not
  touched (commands only), CLAUDE.md, ROADMAP.md.

## Build order

1. Names on the server: `N`, the offer list, `:players`; tests over sockets.
2. The page's sheet and remembering; checked in Chrome.
3. `:whisper`, the `'W'` record, kept whispers; the page's card.
4. Docs, perf row, the review (health-check questions included).

## As built

- **The name rides the connection, not a message.** The plan had the page send `N Aria` up
  the socket. Built instead as docs/IDEAS.md's earlier phone-moves plan had it: `n=Aria` on
  `/ws` (URL-encoded, so `Crypt Ghoul` works), and in a raw hello after the code
  (`VTT1<code> Aria`), so the tests and the bench (`--bench-names`) can be phones. A phone is
  its name for the life of its connection; choosing another reconnects at once. A bad name
  (too long, control characters, not UTF-8) is no name, never a refusal.
- **Where:** names, the offer, kept whispers, seen names and arrivals are in `net.c`
  (`net_whisper`, `net_set_offer`, `net_who`, `net_take_arrival`, `net_name_clean`);
  `app_whisper.c` has `:whisper`, `:players` and `app_whisper_tick`, which runs from
  `app_tick` while serving: it keeps the offer in step with the player creatures (compared
  before anything is sent) and says arrivals on the GM's line. The offer, the kept whispers
  and the seen names outlive a restart of the server, as the handout does.
- **Records:** `'W'` and `'N'` share the handout's shape (`wire_text`); the watcher's decoder
  takes neither (`NULL` in its sink). `'N'` goes to browsers only.
- **Matching:** `:whisper` takes the longest name a phone has used -- here now or seen
  before -- that the line starts with. A player creature's label that no phone has chosen is
  not a name yet.
- **The page** is 11.4 KB as sent (11,682 bytes) of the 12 KB budget. Checked in Chrome
  against a served map: the sheet offered the players' labels, a chosen name and a typed one
  with a space reached the server, the phone came back as itself after the server restarted,
  and the whisper card showed, closed and reopened. The sheet's text box overflowed its card
  at first; fixed (`box-sizing`).

## As reviewed

The review of f952f74 found, and these fixed:
- the names offered skipped hidden player creatures but not ones in the dark. The fix first
  made it ask `fog_token_unseen`; the user corrected that -- every player is at the table,
  so every player creature is offered, hidden or in the dark (2026-10-03). It is rebuilt
  only when the map changes (`App.offer_map`/`offer_gen`), so its cost per tick is a compare;
- a phone dropped while a whisper was being sent was counted as reached, and a kept whisper
  was removed before it was sent: `send_text` counts only what went, and a kept one goes
  only once sent. `send_text` is now the one loop for every text record ('H', 'W', 'N'),
  and `stream_client` the one test of who gets the stream;
- a phone reconnecting on a flaky network put "Aria's phone is here" in the session log
  every time: it is said on the status line only;
- whispers waiting outlived the map: they are the encounter's, like the handout, and close
  with it (`net_clear_kept` in `app_close_map` and `app_travel_to`); the names seen stay;
- the page: changing the name while the socket was still connecting let the old socket's
  error close the new one (each socket's handlers now name that socket); the name box
  counted characters where the server counts bytes (trimmed to 24 bytes before sending);
  the sheet opened again on every names record (once a page load now);
- the session log kept only the first 100 bytes of a whisper (it keeps all of it now), and
  a status message too long for the line could be cut inside a character -- any status
  message, not only these: `app_set_status` cuts on a character's edge;
- `:players` with the server down hid the whispers waiting;
- the WebSocket upgrade request was written three times in the tests (`ws_connect` in
  harness.c) and the three text-record sinks were one shape (`wc_text`).

Left: the unframing loops in test_net.c and test_whisper.c read two different shapes of
stream (with and without the 101's headers) and stay apart. A player creature's label of
25-31 bytes is never offered (a phone may still type a shorter name). The page is 11.5 KB.
