# Summoning at a range

> **Status: signed off 2026-10-12 (second draft), being built.**

Roadmap item 19. For a feature like "summon three Lackeys at Far range": `:summon 3 lackey
far` lights up everything within Far of the summoner and hands the GM three Lackeys to put
down inside it, one at a time, wherever they choose. **One new command and a small placing
mode; no new data, no file change.** About 400 lines, nearly half of it tests.

The first draft chose the squares itself (nearest the cursor). The user's ruling,
2026-10-12: **where a summoned creature goes is the GM's choice**; the tool's job is to show
the range and refuse a square outside it.

## What it does

- `:summon [N|any] TEMPLATE [BAND]`. N is 1 when left out and at most 32. `any` in its
  place keeps placing until `esc`, for "summon any number" (`:summon any lackey far`); the
  status line then counts what has been placed, not what is left. BAND is what the range highlight
  can already draw. Under a ruleset with bands it is one of their names, case and spaces
  aside (`far`, `veryclose`); a number is refused there with the names as the hint, because
  the overlay reads its reach from the band alone. Under no ruleset it is a distance in the
  map's units, rounded down to whole squares (`30` at 5 ft is 6 squares). Left out, it is
  beside the summoner: the nearest band (Daggerheart's Melee), or one square.
- **Refused while something else holds the cursor**: a carried creature, a box being drawn,
  the ruler.
- **The summoner** is the creature every play command acts on (`app_target_token`): a player
  character, a leader, anyone. With none, refused.
- **The range is the range highlight.** The command switches on the overlay `r` already
  draws, as a circle whatever shape `R` was left on (a cone or line with nothing to point at
  holds no square at all), anchored to the summoner, every square *within* it lit,
  those out of the summoner's sight shaded more faintly. Nothing about how it is measured or
  drawn is new, and the players see it exactly as they see `r` today (not at all when the
  summoner is one they cannot see).
- **Placing.** The cursor takes the template's footprint (a 2x2 creature shows a 2x2
  cursor) and moves freely. `enter` puts one down at the cursor when every square of its
  footprint is in the highlight, on the map (`map_walkable`) and free of creatures
  (`play_can_place`); otherwise nothing changes and the status line says which of the three
  failed. The status line counts: `summoning Lackey from Korr: 2 of 3 to place - enter
  places, u takes one back, esc stops`.
- **Sight is shown, not enforced.** A square in range but out of the summoner's sight is
  fainter and still allowed: whether a summon needs sight is the feature's text, and the
  GM's call.
- **Undo.** Each creature placed is one undo step, placed through `character_place` so its
  label numbers as a paste's does (`Lackey`, `Lackey 2`) and its card, counters and rolls
  come with it. While summoning, `u` takes back the last one placed and gives it back to
  the count; it does not reach past the summon's own placements.
- **Ending.** After the last one, or on `esc`, the mode ends: the range overlay is put back
  whole as it was (its shape, and a highlight the GM had on elsewhere), and the cursor's
  footprint and the selection are as they were. Stopping early keeps what was placed.
- **What else ends it**, keeping what was placed: leaving play mode (F1), a scene put back,
  the map closing, and anything that renumbers the creatures. `[` and `]` are refused while
  summoning (the cursor would be on a floor the highlight is not), as they are while a box
  is open. `:` is refused too: the mode owns the keys until it ends.
- **Side:** the template's own. **Hidden:** `:summon hidden 3 lackey far` puts them down
  hidden, as the channel's `place ... hidden` does. The highlight is still drawn for the
  players when they can see the summoner; a GM who wants no tell summons from a hidden
  creature or says so aloud.
- Every message naming a creature is `app_note_gm`/`app_set_status_gm` (one may be hidden);
  the usage hint is plain.

## Where it lives

- `App.summon`: the template (loaded once, `character_load`), how many are left and placed,
  the hidden flag, the undo log's stamp after the last placement, and what to put back (the
  cursor's footprint, the whole `RangeOverlay`). While it is set, `app_play_key` hands the
  key to the mode first, before the ruler's keys: movement, `enter`, `u`, `esc`; anything
  else gets the hint. `?`, `[`, `]` and F1 are dispatched in app.c above play mode, so each
  of those places learns of the mode. Ended in one function, called from every exit above.
- **`u` is guarded by the undo log's stamp**: it takes one back only when the last thing in
  the log is the summon's own placement.
- **The status line.** The frame's status prints the range's line whenever the overlay is
  on, so the mode's line is a branch of its own there, in the GM's view only; the players'
  line stays the range's. The mode names a template the players have not seen yet, so
  `App.summon` is a case in `app_view_differs`.
- **Nothing lands mid-summon.** The channel answers "in play mode" and jobs wait for build
  mode, as today; a change to the file on disk only files a job.
- The command and the mode's keys are in `app_character.c`. The in-range question is
  `range_contains`, asked a footprint square; nothing in play.c is written a second time.
- Placing calls `character_place` itself, not the picker's `app_character_place`: that one
  selects what it placed, and selecting drops a highlight anchored to someone else.
- Keys: a table of its own in `src/keys.c` (`KEYS_PLAY_SUMMON`: `enter` place, `u` take
  back, `esc` stop, the movement keys) chosen by `app_keymap_id`, so the bar and the `?`
  page both show it; the `:summon` line joins the commands' page.
- The channel gets nothing in this plan: an agent places creatures with `place`, which
  already says where.

## Performance considerations

Reference figures: docs/PERFORMANCE.md, *Speed of light* (desktop). C is the creatures on
the map; F the squares of the template's footprint, at most 9.

| path | speed of light | the design | the gap, and why accepted |
|---|---|---|---|
| `:summon` | read the template's file once, a few KB | `character_load`, once for the whole summon, not once a creature | none |
| a frame while summoning | what `r` costs: the highlight's cells when they change, nothing when they do not | the range overlay as it is (`play, range radius` in the published table) | none: no new drawing |
| a cursor move | the cursor's cells | as any move with a sized cursor | none |
| `enter` | F squares tested for range, map and creatures | F calls of `range_contains` and a tile read, then one `play_can_place` (a scan of C) | the scan of C is what every placing pays today: a few microseconds at C = 500, on a key |
| the frame after | one creature's cells | the renderer's diff | none |

## Instrumentation

`PROF_ZONE("summon.place")` on `enter`. A `perf.sh` row `play, summon` (summon two, take
both back with `u`, `esc`: loop-neutral; the row includes reading the template's file,
once a loop, and says so), with its row and paragraph in PERFORMANCE.md.
Tests in the `characters` suite: the band by name and by number, no ruleset, a square out
of range refused, a 2x2 footprint half out refused, an occupied square refused, the count,
`u` giving one back and stopping at the summon's first, `esc` early, `any`, the cursor and
the overlay put back (a cone left on `R` included), each other exit, `[` and `:` refused,
numbering, hidden, the players' frame without the template's name, and a golden frame of
the mode with its bar.
Docs: the README (*Characters*, *Daggerheart*), CODEMAP's rows, the `?` page.

## Decided with the user, 2026-10-12

- Where each summoned creature goes is the GM's choice, inside the highlighted range.
- The count is 1 when left out; `any` places until `esc`.
- The players see the highlight, as they see `r`.
- `any` is the word for placing until `esc`.
