# Vehicles

> **Status: parked 2026-10-12.** A plan written and reviewed 2026-10-11, never signed off:
> the user judged the gain too small for the size of the change (docs/IDEAS.md, *Vehicles*).
> Nothing here is built.

A **vehicle** is a named area that moves, and whoever stands on it moves with it. That
covers what a table means by a horse-drawn carriage, a cart, a raft: a thing on the map,
bigger than a creature, with people on it. A train and an airship are mostly places a fight
happens *on*; they need no new code and are at the end, under *Trains and airships*.

Decided with the user, 2026-10-11: **riding is automatic** (a creature standing on a vehicle
rides it), and **any creature can be switched to not riding at any time**.

## The decision that shapes the rest: an area, not a big creature

The first thought was a creature wider than 3 squares and rectangular. `Token.size` is one
number read in about 150 places across 25 files (`maptools.c`, `play.c`, `link.c`, `fog.c`,
the loader, the channel, the phones' frame): making it a width and a height reshapes the
most shared struct in the program for one feature.

A vehicle is instead **an area marked as one**, the way a floor is an area marked with a
level. What that buys:

- `Token` gains one byte (`afoot`, below) and no reader of `size` changes.
- An area already has a name, a box of any shape, an undo op (`OP_AREA`), a file line, a
  channel request and a place in `--describe`.
- The horses, the driver and the passengers are ordinary creatures. They have their own
  counters, cards and turns; a horse can be hurt, or cut loose by stepping it off.

What it costs, said plainly:

- **A vehicle has no hit points of its own.** An area carries no counters. A carriage that
  can be wrecked gets a creature standing for it (a 1-square "Carriage" with HP, riding like
  anyone else). *Open question 2.*
- **A vehicle blocks nothing.** It is not a wall and not a creature, so sight and movement
  pass over it. A covered wagon's sides are not drawn. If that is wanted later it is the
  moving-stamp or the layer idea (IDEAS.md, *Stacked layers*), not this.

## Data

- `Area.vehicle` (uint8, marked). An area is a floor or a vehicle, never both. Everything
  that carries an area whole must carry the mark, or it is dropped silently: `OP_AREA`'s
  slots (floor and level ride in `counters[1]` today; `undo_set_vehicle` beside
  `undo_set_floor`), `area_equal` and the apply loop in changeset.c (else a proposal or
  `--apply` loses it), `map_copy_into`.
- `Token.afoot` (uint8): this creature does not ride. Zero for every creature today.
  `token_equal` compares it, or the renderer and a change set miss the change.
- **A vehicle is not a room.** `map_area_at` (the smallest area holding a square) skips
  vehicles as it skips floors, so a carriage in the Crypt does not rename the Crypt's
  squares in `--describe` and the channel's answers. `vehicle_at` is asked instead.
- **Riders are derived, never stored**: the creatures whose whole footprint is inside the
  vehicle's box and who are not `afoot`. There is no list to fall out of step with the map.
  A creature half on (a 2x2 with one column over the side) is not riding.
- At most 32 riders move with a vehicle (`PLAY_GROUP_MAX`, the group move's limit). A box
  holding more is refused with a message naming the count.
- Undo: a new op, `OP_AREA_MOVE` (the area's slot, its top-left before and after: one
  20-byte op). `OP_AREA` would also work but spends two token slots, about half a kilobyte,
  on every step of every vehicle; the log is the thing that fills.
- File version 14: `vehicle "Name"` after the area and floor lines, and `tokenafoot` beside
  `tokenhidden`. A version, not an ignored line, for the reason floors gave: an older build
  would drop every vehicle silently. Each dropped line calls `diag()` with a code of its own.

## One owner: `vehicle.c`

Every vehicle question goes through it, as every floor question goes through `floor.c`:

| function | answers |
|---|---|
| `vehicle_at(m, x, y)` | the vehicle holding a square, or -1 |
| `vehicle_riders(m, v, out, max)` | its riders' indices, low first; the count even when over `max` |
| `vehicle_problem(m, name, box)` | why a box may not be a vehicle: overlaps another vehicle, straddles a floor's edge (asked of `floor_at`, not read off boxes), is a floor |
| `vehicle_can_move(m, v, dx, dy, enforce)` | may the box and every rider make this step |
| `vehicle_move(u, m, v, dx, dy)` | does it: one batch, the box and each rider |
| `vehicle_turn(u, m, v, dir)` | a quarter turn, riders carried round with it |

## Moving

- **The check, all or nothing**, like a formation's: every rider must be able to step, with
  the other riders transparent to each other (`token_can_move_set`, the group move's own
  test). The box's leading edge may cross no wall, door or map edge, and may not leave its
  floor. With `enforce_walls` off, as for a creature, nothing blocks.
- **A vehicle does not scoop people up.** The squares the box *newly* covers in a step may
  hold no creature the players can see that is not afoot. Without this rule a carriage
  driven past a guard would carry the guard off, since standing in the box is what riding
  means. Only the new squares are tested: a creature afoot inside the box never pins it,
  and one afoot outside never blocks it (it would not be carried).
- **A hidden creature blocks nothing**, as it blocks no creature today (play.c: a refusal
  would show the players where it is). A vehicle that rolls over one carries it from then
  on, unless it is afoot; the GM rules on what happened.
- **Boarding and leaving are just walking.** Step onto it and you ride; step off and you do
  not. No command.
- **`afoot` is the switch the user asked for**: a creature marked afoot stays where it
  stands when the vehicle moves, and the vehicle slides out from under it. `:ride off` and
  `:ride on` set it for the selection (KEYS.md's ninth rule: `off` switches off). The GM
  sees "afoot" where a creature's note and counters are shown, never on the map, and that
  surface joins `app_view_differs` if it is not there already. *Open question 1: does the
  mark clear itself.*
- **One step is one undo step**: `OP_AREA_MOVE` and an `OP_TOKEN_MOVE` a rider, in one
  batch. The checkpoint needs nothing new: it finds creatures and areas by comparing its
  copy of the small parts.
- **Sight must not lose its step path.** `fog.sight` relights only the creatures that moved
  when it can prove that nothing else changed: `Map.gen` moved by k and exactly k creatures
  stand elsewhere. The box's shift is one more `map_touch`, so as things are every vehicle
  step would fail the proof and rebuild all sight. Sight does not read areas, so the box's
  shift is counted apart (`Map.blind`, bumped with `gen` by `vehicle_move` alone) and the
  proof subtracts it. The step path takes up to 32 creatures at once (`FOG_STEP_MAX`), the
  same as the riders' limit, so a full vehicle still fits.
- **Turning.** `vehicle_turn` rotates the box a quarter turn about its center (a 2x4
  becomes a 4x2) and carries each rider to its turned place. A center falls on the grid
  only when width and height are both odd or both even; otherwise (a 2x3) the turned box is
  placed half a square up and to the left of true, the same way every time, so four turns
  come home. The arithmetic is the one stamp.c applies to a stamp's creatures (`cw_token`);
  that function turns a whole stamp about its origin and cannot be called, so the point
  transform is lifted out for both to use rather than written a second time. Checked whole
  first; refused if the turned box or any rider would be blocked.
- **What does not move with it**: a note on a square, a link's end, a fog patch. And a
  scene saved with the vehicle elsewhere puts its creatures back at the squares they stood
  on, not on the vehicle. The README says so; `--check` warns of a link's end inside one.
- **Taking the reins.** `:vehicle NAME` (or with the cursor on it, `:vehicle`) selects the
  vehicle: its riders become the group, and the movement keys then move the vehicle, esc
  lets go. Which key, if any, does this without the command line is settled against KEYS.md
  when that step is built. *Open question 3.*

## The view

- A vehicle is **tinted**, not outlined: the boundaries between squares belong to walls, so
  a frame drawn there would read as one. Its name is shown where an area's is.
- **The players see it**, tint and all, on the squares they can see; fog hides its squares
  as it hides any others. Nothing about a vehicle is the GM's alone, so `app_view_differs`
  does not change. A hidden rider stays hidden: `fog_token_unseen` is still the only
  question asked about a creature.
- Messages: "the carriage moves" is plain (`app_note`). A refusal that names a creature
  (`blocked by Guard 2`) goes through `app_note_gm`/`app_set_status_gm`, as the rules for
  creatures' names already require when one may be hidden.

## Channel, map tools

- Requests: `vehicle NAME REGION`, `vehicle NAME remove`, `vehicle NAME move DX DY`,
  `vehicle NAME turn left|right`, `ride LABEL off|on`. All are edits, so all arrive as a
  proposal through `app_job_set_proposal`.
- `app_event`: `vehicle moved: NAME to REGION`, so an agent watching hears of it.
- `--describe` lists each vehicle with its riders; `--check` warns of a vehicle overlapping
  another or straddling a floor, and of a link's end inside a vehicle (the end does not move
  with it): W030 and up, the next free.

## Performance considerations

Reference figures: docs/PERFORMANCE.md, *Speed of light* (desktop, i7-8700K). R is the
riders, N the creatures on the map, B the squares along the box's leading edge.

| path | speed of light | the design | the gap, and why accepted |
|---|---|---|---|
| a step: the check | read B boundary bytes and R creatures' squares: tens of bytes, well under 0.1 µs | `token_can_move_set` a rider, each scanning the creature list: O(R x N), plus one walk of N to find the riders and one for the newly covered squares | The group move pays the same (`group.move`: 0.3 µs typical, 5.6 µs at p99 in the published table). At R = 6, N = 24 it is noise; if `perf.sh` shows otherwise at N = 500 the riders' walk is the thing to bound by the box |
| a step: sight, with fog on | relight the party's riders that moved: 6.0 µs a creature on the bench map | the step path, kept by counting the box's shift apart | none if the proof holds. Without it every step is the full rebuild, about 28 µs on that map: the step's test asserts the step path was taken |
| a step: the edit | 4 bytes a rider and 4 for the box | the same, through `undo_move_token` | none |
| a step: undo | 20 bytes x (R + 1) | the same with `OP_AREA_MOVE` | none; `OP_AREA` would be about 0.5 KB a step, which is why it is not used |
| a step: the frame | the cells that changed: two edges of the tint and each rider's old and new squares; at about 0.75 ns a byte to the terminal | the renderer's diff sends changed cells only | none expected. To be measured in bytes against a group of R moving without a vehicle; the difference should be the two edges of tint and nothing else |
| drawing the tint | a write a visible cell of the box | each vehicle's box clipped to the window, then its cells: cost follows the cells on screen, not the box or the map | none |
| a frame with a vehicle standing still | nothing: no cell changed | a walk of the areas (at most 64), culled to the window, then the diff finds no change | the walk, under 0.1 µs; the same shape as `link.marks` |
| a frame on a map with no vehicle | nothing | one test of a count kept on the map | none |
| a save | one line a vehicle, 11 bytes an afoot creature | the same | none |

## Instrumentation

- `PROF_ZONE("vehicle.move")`, `("vehicle.turn")`, `("vehicle.marks")`.
- `tools/perf.sh`: `play, vehicle` (a 2x4 carriage with six riders stepped four right and
  four left, loop-neutral) and `play, vehicle idle`; both regenerated into PERFORMANCE.md
  with a paragraph each, measured against the table above.
- Tests: a `vehicle` suite (riders derived; afoot; the scoop rule; all-or-nothing; turn;
  undo and redo balance; the file's round trip at version 14 and an older map still written
  at its own version), golden frames for the tint in both views, the new lines added to the
  loader's fuzz corpus.

## Build order

1. `vehicle.c`, `Area.vehicle`, `Token.afoot`, `OP_AREA_MOVE`, the file's version 14,
   `:vehicle NAME REGION|remove`, `:ride`. No drawing: testable from the suite.
2. Taking the reins and moving in play mode; the tint; the players' view.
3. Turning.
4. The channel's requests, the event, `--describe` and `--check`.
5. The docs (README *Vehicles*, with trains and airships; CODEMAP; KEYS; the `?` page),
   `perf.sh`, PERFORMANCE.md, the roadmap's item to *Built*.

Each step is reviewed before the next begins.

## Trains and airships

No code. The README's *Vehicles* section says how, with what exists:

- **A train** goes only where the rails go, so the train is the map: the cars drawn end to
  end, doors between them, the roofs as a second floor. A countdown clock is the bridge
  three rounds off; a scene a stretch of track. Riders alongside on a cart are a vehicle.
- **An airship's** decks are floors (hold, main deck, rigging), with links for the ladders.
  A boarding action is both hulls on one map with open sky between.
- **Not covered:** an airship crossing a battle map with a fight on its deck and another on
  the ground. That needs the vehicle's squares in a layer of their own. It goes to IDEAS.md
  beside *Stacked layers*, to be taken up when a session asks for it.

## Open questions for sign-off

1. **Does `afoot` clear itself?** Proposed: no. It stays until `:ride on`, and the marker
   shows it. The other choice clears it when the creature leaves the vehicle, which is
   friendlier but means every move of every creature asks "was it on a vehicle".
2. **A vehicle's hit points.** Proposed: a creature stands for it. The other choice gives
   areas counters, which is a larger change to a shared struct.
3. **The key for taking the reins**, to be chosen against KEYS.md in step 2, or left as
   `:vehicle` alone.
