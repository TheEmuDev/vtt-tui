# A camera for the TV

`:player camera follow|party|hold`: what the players' frame looks at. Roadmap item 21.
Signed off 2026-10-04 with the recommendations; built the same day.

## What is there now

There is one players' frame. The phones and a TV running `--watch` (or `:mirror`) all show
it, so a camera for the TV is a camera for the phones too. Today it looks through the GM's
camera (`a->ed.view`). There is one exception: when the players are on a floor the GM is not
showing (`app_players_split`), it uses its own camera, `a->pview`. That camera centers on the
party once, when their floor changes, and then holds still (`app_players_camera`,
src/app_floor.c). The pieces this needs mostly exist already:

- `pview`, swapped in for one draw (`draw_editor`, src/app_draw.c), with `hold_camera` so the
  GM's cursor does not drag it;
- `app_view_differs` drawing the players' frame on its own whenever it could differ;
- the status message held back from the players' frame under `psplit`, because it describes
  the GM's cursor;
- a phone's tap mapped through `pview` when their floor is split (`app_ping_cell`).

## Decisions (my recommendations)

| question | recommendation |
|---|---|
| the modes | **follow** (as now: the GM's camera, `pview` only on a split floor); **party**: frames every player creature the players can see on their floor; **hold**: stays where the GM's camera was when the command was given |
| `:player camera hold` again | takes the GM's view *now*: the GM scouts, finds the room, says hold, and the table sees it. One command for "freeze" and for "show them this" |
| party: when it moves | not with every step. It recenters only when a player creature comes within 2 squares of the frame's edge, or leaves the frame, the way the GM's cursor scrolls (`grid_ensure_visible` with a margin, applied to the party's box). A screen that swims on every step is worse on a TV than one that jumps now and then |
| party: zoom | the GM's zoom, unless the party does not fit; then the closest zoom out at which they do, never closer in than the GM's. The GM's zoom keys still set the scale the table sees. If they do not fit even at the farthest zoom, it frames the creature whose turn it is, else the party's middle |
| party: who counts | player creatures that are not hidden and are on the players' floor: the same set `app_players_camera` centers on now. A creature in the dark to the players is still theirs and still counts |
| the GM's cursor | **follow**: shown, as now (the GM points at things). **party**: shown when it is inside their frame, because in a fight the GM moves enemies with it and the table should see what moves. **hold**: never, because the GM is scouting and the cursor is the GM's own |
| the status message | in party and hold, held back from the players' frame, as on a split floor: it describes the GM's cursor, which may be somewhere the players are not looking. Turn and fight lines in the title bar are unchanged |
| floors | the camera mode applies on every floor. A players' floor change (a player's turn, `:player floor`) recenters on the party there, as now; hold then holds that new spot. In follow, a split floor behaves exactly as today |
| a phone's tap | mapped through whichever camera drew their frame (`pview` whenever it is not the GM's), so a ping lands where the player pointed |
| changing maps, travel | the mode stays for the session; a held spot belongs to its map, so hold becomes follow on another map (said in the GM's status line) |
| what the GM sees | the title bar says `CAM PARTY` or `CAM HOLD` beside `PLAY`, as it says `HANDOUT`. `:player preview` shows the table's view as always. No key: a command is enough for something set a few times a session. It can get a key later if play asks for one |
| saved? | no: a table setting for this sitting, like `:player floor` |
| not here | one camera per phone (each phone panning on its own: the phones would need their own frames, a much bigger change); following a named creature (`:player camera Aria` is the natural next step if play asks for it); smooth panning |

## Privacy

The camera only chooses which part of the players' frame is shown. Everything that frame
already withholds (fog, hidden creatures, notes, GM-only messages) is unchanged, because it
is drawn by the same `VIEW_PLAYERS` path. Two new points:

- the status message is withheld in party and hold (above);
- `app_view_differs` returns 1 whenever the players' camera is not the GM's, so their frame
  is always drawn and never copied from the GM's screen, which would show the GM's view.

## Cost, and how it is measured

- In party and hold the players' frame is always drawn on its own (`net.players_frame`),
  as it already is over fog or with a hidden creature. That is the price of a second camera.
- Party's framing is O(creatures). It is worked out only when `Map.gen`, the players' floor
  or the GM's zoom changes, the way the whisper offer is. `PROF_ZONE("camera.party")`.
- A party recenter redraws the whole frame for every phone (a pan, about 0.5 ms of copying
  on a phone). The 2-square margin keeps those rare. A test counts recenters over a walk.
- perf rows: `play, camera party, 4 clients` (the crowd walking), `play, camera hold, 4
  clients` (the GM's cursor moving away while the table holds). Both are compared against the
  follow row; if either is more than 15% above what the hidden-creature case already costs,
  I will look at it before shipping.
- The phone page does not change: no bytes, no new records.

## Where it goes

- `app.h`: `CameraMode pcam` (follow, party, hold) and the held map.
- `app_floor.c`: `app_players_camera` grows the three modes; `app_players_split` is joined by
  `app_players_own_camera(a)`, "is their frame drawn through `pview`", which `draw_editor`,
  `app_ping_cell`, `status_msg_shown` and `app_view_differs` ask instead of the split test.
- `app_cmd.c`: `:player camera follow|party|hold` beside `:player floor` and `:player preview`;
  the title bar tag in `app_draw.c`; the cursor rule in `play_draw` (it already takes
  `players`; it gets told whether to draw the cursor).
- `keys.c` `?` entry, README (*The players' view*), KEYS.md is not touched (no key).
- Tests (a new `camera` suite):
  - each mode's frame, read off the players' renderer;
  - party: recenters only past the margin, zooms out to fit, and never zooms in past the GM;
  - hold: re-issuing takes the GM's view now, and the GM's cursor never shows;
  - a tap pinging the right square under hold;
  - the status message withheld in party and hold;
  - `app_view_differs` returning 1 in party and hold;
  - a floor change under hold;
  - hold becoming follow on another map, after `:open` and after a link trip.

## Build order

1. `app_players_own_camera`, refactored in with follow only. Behavior-neutral: the existing
   floor tests pass unchanged.
2. hold, with its tests.
3. party, with its tests and the zone.
4. The title bar tag, `?`, README; perf rows; PERFORMANCE.md regenerated (median of three);
   the review (health-check questions included).

## As built

- `App.pcam` (`PCAM_FOLLOW`, `PCAM_PARTY`, `PCAM_HOLD`) and `App.pcam_for`, what party's
  framing was last worked out from (the map, `Map.gen`, their floor, the GM's zoom, the
  view). `app_players_own_camera` replaced the split test at all three places that asked it:
  `draw_editor`, `app_ping_cell` and `app_view_differs`.
- `app_players_camera` (src/app_floor.c): follow takes the GM's zoom; party runs
  `party_frame` (`PROF_ZONE("camera.party")`) when its key changes; hold keeps its own zoom
  and spot. The old centering on a floor change is `center_on_party`, unchanged.
- The fallback, when the party cannot fit even at zoom 0, keeps the same 2-square margin
  round the square it frames. The first version re-centered on every change there.
- Held, `Editor.hide_cursor` takes the cursor, its lit row and column, and the `v` box off
  their frame.
- On another map, hold becomes follow in `app_floor_reset`, which every way of putting a new
  map up already calls. **Changed from the plan:** nothing is said on the status line; the
  `CAM HOLD` tag going from the title bar says it, and the open or the trip has its own
  message to show.
- Tests: suites `camera` and `camerafloors` (tests/test_camera.c). Taking out the tap
  mapping, the hidden cursor or party's own zoom each fails one of them.
- Measured by hand before the perf run (80x24, the perf crowd, 4 watchers): hold sends
  nothing while the GM's cursor walks away; party carrying a creature sends 125 bytes a
  frame for all four, against 1,457 under follow, because the table's view does not move.
  `camera.party` costs under 0.3 µs when it runs.
