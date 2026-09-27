# Floors

Signed off 2026-09-27. A **floor** is a named area marked with a **level** (an elevation:
-1 a basement, 0 the ground, 1 upstairs; gaps and negatives allowed). The screen can show
one floor at a time; `[` `]` step down and up through them in level order; a creature
taking a link to another floor takes the view with it; the players' screens show the
party's floor. A map with no floors behaves exactly as before.

## Built so stacked layers can follow

Stacked layers (floors sharing coordinates, each its own grid, seeing between them) are
wanted later (IDEAS.md). They are the same idea with different storage, so:

- **One small API answers every floor question** (`floor.c`): which floor holds a square,
  a floor's box, the floors in level order, the floor a side's creatures are on. Keys,
  drawing, the players' view and the channel never read area boxes for floors. Layers
  would reimplement that API alone.
- **Levels are elevations from the start**, the ordering layers need. A flat map of
  floors converts to layers mechanically: each floor's box a layer at its level, its
  links the ways between them.
- **Messages that cross floors name the floor** (`stairs 3 to Upper K12`). Under layers,
  square names repeat per floor and the name becomes necessary.
- **Floors never overlap**, so a square is on at most one: true of layers too.
- **Not now:** squares typed as `Upper:K12`. Global names stay unique on a flat map; the
  qualified form belongs with layers.

## Data

- `Area.floor` (marked) and `Area.level` (int8). Undo rides `OP_AREA`'s slots.
- File version 9: `floor "Name" LEVEL` after the area lines. A version, not an ignored
  line, because an older build would drop every floor silently.
- `--check`: overlapping floors (W160). `--describe` groups rooms under their floor.

## The view

- `:floor Upper 1` marks area Upper a floor at level 1; `:floor Upper off` unmarks it;
  `:floors` lists them; `:floor Upper` shows it; `:floor all` shows the whole map.
- `[` `]` the floor below / above, in both modes; at the ends a message, no wrap.
- Showing a floor: the camera stays inside its box, the cursor cannot leave it, only the
  floor is drawn -- one clip rectangle over the whole draw, so every path is covered at
  once -- and the labels cover its rows and columns.
- The view follows the cursor: whenever the cursor lands on another floor (`g o`,
  `:link N`, `:K12`, `t`/`f`/`e`, the turn passing) the view switches to the floor
  holding it, or to the whole map for a square on none.
- The status line names the floor shown. Not saved with the map (like the cursor).

## Turns

- **A creature takes the turn** (`a`/`A`, a new round, `s t`): the GM's view goes to its
  floor. If it is a player creature, the players' view goes there too, and a
  `:player floor` pin is released.
- **The spotlight crosses to a side** without naming a creature: `floor_pick(side)`.
  To the players, both views go to the floor it picks; to the GM, only the GM's view
  goes (the players' view stays on the party's floor and never shows where monsters
  are).
- **`floor_pick(side)`**, the one tie-breaker, in order:
  1. stay, if the floor being shown still has a creature of that side;
  2. the floor with the most creatures of that side;
  3. the floor where a creature of that side last moved or took a link (each side
     remembers its own, for the session);
  4. the lowest level.
- Outside a fight the players' view is `floor_pick(players)`, unless pinned.

## The players' view

- `:player floor Upper` pins it; `:player floor auto` follows the party again.
- When the players' floor is not the GM's, their view has its own camera, centered on
  the party within that floor, and their frame is drawn rather than copied
  (`app_view_differs`).
- Phone taps map through the players' camera. A ping on a floor the GM is not viewing
  says so on the GM's status line (`ping on Upper at K12`).

## Channel

- `floor NAME LEVEL`, `floor NAME off`; `floors` reads them. `status` and `marked` name
  the floor the GM is viewing. AGENTS.md gets the multi-floor recipe.

## Instrumentation

Zones `floor.pick` and `floor.switch`. Perf rows: build mode on one floor; play mode with
four watchers while the players are on a floor the GM is not (their frame drawn a second
time through its own camera).

## Build order

1. Data, file, undo, `floor.c`, map tools.
2. The view: clip, `[` `]`, `:floor`, following the cursor, status, labels.
3. Turns and the players' floor: `floor_pick`, their camera, `:player floor`, taps.
4. Channel, docs, perf. Then the Fable review.

## As built: what the plan did not say

- `map_area_at` no longer counts floors: a room with no area of its own would otherwise be
  named after the floor, and `marked`'s "in" would say the floor rather than the room.
- `:player floor auto` picks afresh (it forgets where the players were), or step 1 of the
  tie-breaker would hold them on the floor the pin left them on.
- When the players' floor is not the GM's, their status line is blank and no message
  reaches them: every one describes the GM's cursor or names a square on the GM's floor.
- A trip's message (`Aria takes stairs 1 to I2`) names no floor: it is a public message,
  and an area's name is the GM's. The GM's status line names the floor.
- `0` `$` `gg` `G`, wall mode's corners and a carried creature stop at the floor's edge.
