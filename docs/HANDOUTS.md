# Handouts

A short text card pushed to the players' screens -- an inscription, a letter, a riddle --
and taken down again. Signed off 2026-09-27. The README (*The players' view*) is the user's
reference; this page is why it is the way it is.

## Decisions

| question | answer |
|---|---|
| how the phones show it | an HTML card over the map, in a reading font, wrapped to the phone. The shared frame scales the GM's grid to the phone -- about 4 pixels a character held upright -- which keeps a map's shapes and loses a letter's words |
| where the text comes from | prepared files, `$XDG_DATA_HOME/vtt/handouts/NAME.txt` written in any editor (the name is the card's title), and `:handout say TEXT` for a line made up at the table |
| who closes it | the GM puts it up and takes it down; a player may close it on their own phone and reopen it with a button while it is up. A new handout opens on every phone, closed or not |
| formatting | plain text, line breaks kept; nothing else |
| off and on | `:handout off` takes it down and keeps it; `:handout on` puts it back (docs/KEYS.md rule 9) |
| not here | pictures (a drawn map as an image), a handout for one player (the phones have no identity), the control channel |

## Shape

- **The wire.** An `'H'` record, `u16 n` then `n` bytes of UTF-8: the title, a newline,
  the body; `n` 0 takes it down. It is not a frame: it goes to every stream client when it
  changes, live or not, and to a client that joins after its `FULL`. The server keeps its
  own copy (`Net.handout`), across a restart. 2 KB of body: the record fits the decoder's
  hold and a client's send buffer with room to spare.
- **The page** decodes the record with `TextDecoder` and sets `textContent` -- never HTML,
  so nothing in a handout can reach the page as markup. The card is its own element, so a
  tap on it is never a ping.
- **The terminal mirror** keeps the text and draws `ui_handout_draw` over its picture: a
  centered box, the title on its border, the body wrapped (at a space when there is one),
  an ellipsis when the terminal is too short.
- **The GM's screen** says `HANDOUT` in the title bar -- in the players' frame too, which is
  true and saves drawing that frame apart -- and `:player preview` draws the card. While
  previewing with a handout up, `app_view_differs` is true, so the card is never copied
  into the phones' frame.
- **Files** are read whole on `:handout NAME`: UTF-8 checked (`utf8_valid`), CRLF made LF,
  tabs made spaces, trailing blank lines dropped; over 2 KB, not UTF-8, or empty is refused
  and says why.

## As built: what the plan did not say

- **The join leak.** Checking the card in Chrome showed a GM-only status line on the phone.
  The server's picture was the GM's own terminal until the app first drew a players' frame,
  which it did only once a client was connected and play mode live -- so a phone joining
  first was sent the GM's screen as its `FULL`, and kept it until the next key: status lines,
  notes, counters, hidden creatures, build mode. Now `app_frame` switches the server to the
  players' renderer from the first frame after `:serve`, `net_start` makes that renderer
  blank rather than the renderer's force-a-redraw cells, and a join asks the app for a
  fresh frame (`Net.joined`). `test_join_frame` holds it.
- **The page budget.** The card took the page past 12 KB. `tools/embed.sh` now drops the
  page's block comments when it embeds it: the budget is what every phone is sent, and the
  source keeps its comments. 10.1 KB as sent.

## Instrumentation

`PROF_ZONE("handout.send")` round the record going to the clients and
`PROF_ZONE("handout.draw")` round the card; a perf.sh row putting a handout up and taking
it down with four watchers.
