# Ideas set aside

Things worth building that were consciously not built, with the reason, so
the reason can be re-examined rather than the idea re-invented. Add to the
top; move an entry to the README when it ships.

## A floor view, and height

*Links built 2026-09-27; the rest not decided.* Floors of a building and "pocket" places
reached by a portal are built today as areas on one map, apart and separated by void,
joined by links (README, *Links*). Two layers of the original proposal remain:

- **A floor view.** The screen shows one area at a time, filling the window; `[` `]` flip
  floors, and following a creature through a link switches with it; the players' frame
  shows the party's floor. Underneath, still one flat map. Worth it when jumping between
  floors with `g o` or `:link N` stops being enough -- a building of many floors, or a
  window too small to hold the map.
- **True stacked layers** (floors sharing coordinates, a level on every grid, fog, sight,
  room finder and file), which buy seeing between floors -- a pit lined up over the room
  below, a balcony -- at the price of a rewrite of the core. A lighter, separate idea:
  **height on a creature** (flying, on a ledge) that the ruler and range count.

Links left as they are: a trip is a key press, never a step onto the square (nothing fires
by itself); the ruler, range and route do not measure through a link.

## Undoing part of an agent's request

*Set aside 2026-09-26, when the control channel was planned (CONTROL.md).* A request
from an agent is one undo batch, all or nothing: `u` takes back the whole room, doors
and creatures together. The GM may want to keep the room and lose one door. That wants
the batch split by line (each line its own sub-batch, `U` or a count stepping through
them) or a list of the request's lines to pick from. All or nothing is enough until an
agent's requests grow large enough that redoing one by hand is a chore.

## Moves from a player's phone

*Set aside 2026-09-25, after a full plan.* REMOTE.md's phase two step 3: a phone
bound to a creature gets a step pad on that creature's turn and walks it under the
GM's rules. Why not now: at an in-person table, a ping does the job. The player taps
where they want to go, the GM sees the ring and moves the creature, and the whole
table watches it happen. Moving from the phone saves the GM a few keystrokes at the
cost of names, bindings, a pad, turn gating and a new fog rule. It becomes worth it
when players are not at the table -- remote play -- or when the GM's hands are the
bottleneck.

The plan, as signed off by nobody but ready to be (a Fable 5.1 planner, checked
against the code of 2026-09-25):

- **Carry.** A phone step rides the GM's own carry (`Play.sel`/`grabbed`/trail): pick
  up as `enter` does, step as `l` does, one undo batch a step, `esc` rewinds the walk,
  the GM's cursor follows the creature. No second carry: `play_step`, the trail, the
  move label, `undo_rewind_moves` and `play_status` all read `Play`.
- **Identity.** The page asks for a name once (`localStorage`, or `?n=` in the URL the
  GM hands out) and sends it as `&n=` on `/ws`; a raw hello may carry one
  (`VTT1<code> <name>`) so tests and the bench can be phones. `:serve who` lists them,
  `:serve as NAME LABEL` binds a name to a creature by label (unique substring),
  `:serve as NAME off` unbinds. Bindings live in `App`, last the session, are never
  saved. Table trust: names are self-declared, the join code is the only gate.
- **Protocol.** Up: `M dx dy` (four directions), `D` (done); 10 a second and four
  queued per client, in a FIFO beside the ping inbox. Down: WebSocket *text* frames,
  not wire records -- `S <bits> <label>` (bound, acting, in hand; sent only when it
  changes) and `N <notice>` from a fixed vocabulary that never names a creature.
- **Gate.** Only while the bound creature is `TURN_ACTING`, in play mode, with no
  modal, box, ruler or chooser open and the GM not carrying something else (the `:`
  line open does not refuse). Put down on `D`, on the turn passing (`a` puts a phone's
  carry down first instead of refusing), on disconnect, on the GM's `enter`/`esc`.
- **Fog.** A phone step into any square hidden from the party is refused as `dark`,
  whatever is there -- otherwise a hidden enemy blocking the step would reveal itself.
  The GM still carries creatures into the dark by hand.
- **Page.** A 3x3 pad (four arrows, `done` in the middle) fixed bottom-right over the
  key bar, `pointerdown`, a sibling of the canvas so it never triggers a ping. About
  1.3 KB of page: strip comment-only lines in `tools/embed.sh` to stay under 12 KB.
- **Costs.** Zones `remote.step`, `remote.sync`; `--bench-moves`; rows on `$FIGHT`
  with and without fog. Three commits: net (names, `M`/`D`), app (bindings, gate,
  put-down, fog rule), page (pad).

## More ways to point

*Set aside 2026-09-24, when pings were built.* Three extensions nobody needs yet:

- **A pinging watcher.** `vtt --watch` could send a ping from a mouse click. It would
  need terminal mouse reporting (`?1000h`, SGR 1006) in the shared input parser, which
  is a feature of its own touching the GM's keys; the server already takes a watcher's
  `P col row` line, so the watcher side is the only work.
- **`:ping C6`**, a ping at a named square without moving the cursor. Worth it if the GM
  finds themself walking the cursor across the map just to point.
- **`g P`**, the GM's ping drawn even on ground the players cannot see, for "something is
  over here". Today a ring in the dark is the GM's alone, because a ring drawn in the
  players' blank traces a square they are not meant to know about.

## A Fear pool for Daggerheart

*Set aside 2026-09-20.* The GM's Fear is a number that changes every few
minutes of a Daggerheart session, and the tool already knows when a duality
roll lands with Fear. The natural shape: a counter in the side panel under
the daggerheart ruleset, bumped by one when a `:roll` under that ruleset
comes up Fear, adjustable by hand (`:fear 4`, `:fear +1`), saved with the
map, and stepped back by undo like the round counter is. Cap it at twelve,
the rule's ceiling, and light the row when it is full.

Why not now: the table this tool serves runs Fear on a physical tracker in
the middle of the table, which everyone can see and which the GM can move
without looking at a screen. In-person play is what the tool augments, and
the tracker already does this job better than a panel row would. It becomes
worth doing when a session is run with players who cannot see the table --
the remote view's later phases -- or when the roll-driven bump saves enough
reaching for the tracker to matter.

Where it would live: `Map.fear` beside `Map.round`; `OP_FEAR` in the undo
log; a `fear` line in the file, written only when non-zero; a `fear` flag
on the `Ruleset` table so no other game grows one; the panel row under the
spotlight block; a `:fear` command rather than a key, since the roll does
the frequent part.
