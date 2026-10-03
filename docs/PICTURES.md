# Picture handouts

`:handout crypt-door.jpg` puts an image from the handouts folder up on the players' phones --
a creature, a drawn map, a letter's scan -- the way a text handout goes up, and takes it
down the same way. Roadmap item 20; docs/HANDOUTS.md is the text handout this builds on.

**Parked 2026-10-03, the plan complete but not built.** At an in-person table the GM would
rather hand over a physical prop -- a printed letter, a drawn map, a picture on a card --
than put it on the phones. The plan below is finished and checked against the code of
2026-10-02, so it can be signed off and built as it stands if that changes: remote play,
or a table that wants the art on the TV. docs/IDEAS.md lists it with the other ideas set
aside.

## The problem

The phones are fed through a 64 KB send buffer per client (`NET_SEND_CAP`), and a client that
cannot take a record is dropped, never waited for. A phone photo is 1-5 MB. The page itself
has a 12 KB budget (10.1 KB as sent), so the picture cannot ride in the page either. vtt
decodes no image formats and links nothing to do it.

## Decisions

| question | answer |
|---|---|
| how it reaches the phones | **the phone fetches it over HTTP from the same server**, `GET /picture/GEN?k=CODE`, the join code checked as for the page. The handout record names it; the browser downloads, caches and decodes it, and draws it in an `<img>`. Recommended over sending the bytes down the WebSocket, which would need the send buffer raised to the largest picture (8 phones x 5 MB held at once) or flow control on every client, for nothing the browser does not already do |
| sending a big body | an HTTP client gets a third state, *sending a picture*: its buffer takes the headers, then the body is written straight from the server's one copy (`Net.picture`) a buffer's worth at a time as the socket takes it. Nothing is copied per phone. A new picture closes any download of the old one (its URL is gone; the phone is already being told the new one) |
| room for the downloads | 8 phones watching fill all 8 client slots today, and the download would be refused. Slots go to 16, the watchers still capped at 8, as now; the other 8 are for page and picture requests, which last a moment. About 0.5 MB more held while serving |
| the record | `'H'` grows a kind: text as now, or a picture -- its title (the file name), its generation and its size in bytes. The page builds the URL from the generation; a new picture has a new generation, so a cached old one is never shown |
| formats | JPEG, PNG, GIF and WebP, known by their first bytes, sent with that `Content-Type` and `X-Content-Type-Options: nosniff`. Anything else is refused (SVG too: an image that is a document is not worth reasoning about) |
| size | 8 MB at most; over that is refused and says to shrink it. Read whole into memory on `:handout`, as the text handout is |
| names | the handouts folder holds both: `:handout crypt-door` finds `crypt-door.txt`, else the one picture called `crypt-door`; `:handout crypt-door.jpg` names the file outright. The picker lists both, a picture's detail saying `picture, 340 KB` |
| on the phone | the card shows the picture fitted to the screen, the title on it; tapping the picture opens it alone in a new tab, where the phone's own zoom works (the page's canvas turns zoom off). Closing and the reopen button work as for text |
| the terminal mirror and `:player preview` | they draw characters and cannot show it: the card says `picture: crypt-door.jpg - on the phones`. A TV showing the players' view should run the page in its browser, not the mirror, to show pictures; the README says so |
| the GM's screen | `HANDOUT` in the title bar as now; the status line says `handout up: crypt-door.jpg (340 KB)` |
| one at a time | a picture and a text handout are the same slot: putting one up replaces the other; `:handout off`/`on` take down and put back whichever was last |
| its lifetime | the text handout's: across build and play, down and forgotten when the map closes. A phone that joins later is told after its `FULL` and fetches it then |
| not here | captions beside the picture (a text handout next to it does that), several pictures at once, the control channel, a picture to one player (item 23 can add it once whispers exist) |

## Where it goes

- `net.c/h`: `Net.picture` (bytes, length, type, generation), `net_set_picture`; the
  `/picture/GEN` route; `ClientKind` gains the sending state (`out` for the headers, then
  `pic_off` into `Net.picture`, written on `POLLOUT`); `NET_MAX_CLIENTS` 16 with a watcher
  count capped at 8; `NET_IDLE_MS` applies to a stalled download too.
- `wire.c/h`: the `'H'` record's kind byte and the picture fields; `WireSink.handout` gets
  the kind.
- `app_handout.c`: finding a name among `.txt` and the four picture extensions
  (`store_list_all` for each), the size and first-bytes check, `publish` for either kind.
- `web/index.html`: the picture card (`<img>` in a link, fitted), the URL from the code and
  the generation; `tools/embed.sh`; the 12 KB budget held.
- `watch.c`, `ui_handout_draw`'s caller in app_draw.c: the stand-in card.
- Tests (`test_net.c`, `test_saved.c`): the record's round trip; a download through a real
  socket, whole and byte-exact, of a picture several times the send buffer; the code
  refused without it; a download cut by a new picture; 8 watchers plus a download; the
  wrong format, too big, a name that is both; the stand-in in the mirror and the preview;
  the page's decoder (`VTT_REQUIRE_NODE=1`) taking the new record.
- `PROF_ZONE("handout.picture")` round reading and checking the file, and
  `"net.picture_send"` round each chunk written; a perf row putting a picture up with four
  watchers.
- README (the players' view's handout section, the folder, the TV note), docs/HANDOUTS.md
  pointing here, CLAUDE.md, ROADMAP.md.

## Build order

1. `Net.picture`, the route, the sending state, 16 slots; tests through a socket.
2. The record and the page; checked in Chrome on a served map.
3. `:handout` finding pictures, the picker, the stand-in in the mirror and preview.
4. Docs, perf row, the review (health-check questions included).
