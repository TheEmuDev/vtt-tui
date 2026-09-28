# Links between map files

A link whose other end is in another map file -- the town gate to the dungeon, a portal to
another plane. `g o` on its end takes the party there, and vtt, the phones and the session
go with them. Signed off 2026-09-27. The README (*Links to another map*) is the user's
reference; this page is why it is the way it is.

## Decisions

| question | answer |
|---|---|
| holding maps | one map open at a time, as ever: leaving a map through a link saves it. Keeping every visited map open in memory (unsaved edits and undo on both sides) would change how the app holds a map, and waits |
| where the party lands | an area named in the other map (the formation inside it, nearest its middle), or a square (the formation's box nearest it). Pairing ends by link number would need two files edited together |
| the fight | creatures that travel leave the turn order; each map keeps its own round |
| a handout | comes down, as when a map closes |
| finding the other map | by its file's name, beside this map's file |
| the way back | a separate link in the other map, made the same way; arriving where there is none says so |
| undo | a trip is not an undo step, as opening a map is not |
| the channel | makes links to other maps (checked against the file) and lists them; taking one opens and saves maps, which stays the GM's |

## Shape

- **The link.** `Link.to_map` (the other file's name without `.vtt`) and `to_place` (an
  area's name or a square); "" for an ordinary link. It has one end here; the second is
  kept equal to the first, so every loop written for two ends does no harm, and
  `link_ends` answers where it matters -- the overlap test, drawing, describing. The record
  outgrew a token slot's note, which undo keeps a link in: it is packed across the slot's
  `.note` and `.label`. A link to another map runs one way and has no `reverse`.
- **Making one** is `:link to MAP PLACE` in build mode, the end the brush's block at the
  cursor, the kind the one `g l` makes. `link_map_check` reads the other file first: it
  must be there, and the place an area in it or a square on its ground.
- **The trip** (`travel`, app_link.c) is checked whole before anything moves: this map has a
  file; the other is not this one; it opens; it has no newer autosave (the recovery
  question must not be skipped); `link_land` finds room for the party as it stands. Then
  the party leaves through the undo log, this map is saved (a failed save takes that back
  and goes nowhere), `app_travel_to` swaps the maps -- keeping the server, the session log
  and the play settings, dropping what points into the old map -- and the party arrives,
  labeled as a paste is, their turn places gone. The arrivals are unsaved changes in the
  new map, which the autosave covers.
- **The file**: version 12, `link N KIND SIZE X Y to "MAP" "PLACE" [secret]`.
- **The map tools**: `--check` W151 when the other file or its place is missing (it reads
  the file beside the one checked); `--describe` lists such a link as an exit, and its JSON
  carries `to_map` and `to_place`.

## Instrumentation

`PROF_ZONE("link.trip.map")` round the trip, loading and saving included; a perf.sh row
walking a party from one map to another and back.
