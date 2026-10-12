# Summoning at a range

> **Status: a plan, awaiting sign-off (written 2026-10-12).** Nothing here is built.

Roadmap item 19. For a feature like "summon three Lackeys at Far range": `:summon 3 lackey
far` puts three of the template `lackey` down at Far from the selected creature, on free
ground, numbered. **The smallest of the three plans: one new command, no new data, no file
change.** About 250 lines with its tests and docs.

## What it does

- `:summon [N] TEMPLATE [BAND]`. N is 1 when left out and at most 32 (`PLAY_GROUP_MAX`,
  what a paste may hold). BAND is one of the ruleset's band
  names, case and spaces aside (`far`, `veryclose`), or a number in the map's units (`30`);
  left out, it is the nearest band. Under no ruleset only the number works: the core knows
  no band, the ruleset's table supplies them (`ruleset_band_of`).
- **From** the creature every play command acts on (`app_target_token`); with none, refused.
- **Where.** A candidate is a top-left square for the template's footprint such that: the
  distance from the summoner's footprint to the *nearest* square of the new one falls *in*
  that band (past the band below, up to this one's edge), measured as the range overlay
  measures; every square of the footprint is on the map (`map_walkable`: only void fails;
  walls are boundaries, not squares) and on the summoner's floor (`floor_at`); and no
  creature stands on any of them. They are taken **nearest the cursor first**, ties in
  reading order, and a square a creature already chosen would cover is passed over, so a
  2x2 or 3x3 template's copies never overlap. The GM points roughly where and the command
  fills in; with the cursor on the summoner it is simply the band's nearest squares. Sight
  is not asked: a creature may be summoned behind a wall.
- **Another floor.** With the cursor on a different floor from the summoner the command is
  refused and says so: the creatures would land where the screen is not looking.
- **All or nothing.** Fewer free squares than N changes nothing and says how many there
  were. One undo batch, and if a `character_place` refuses part-way the batch is taken back
  (`undo_abort`); each creature goes down through `character_place`, so labels number
  as pastes do (`Lackey`, `Lackey 2`), the card and counters come with it, and the rolls a
  template carries are added as they are today.
- **Side:** the template's own. **Hidden:** `:summon hidden 3 lackey far` puts them down
  hidden, as the channel's `place ... hidden` does (the picker has no such choice today).
- Every message is `app_note_gm`/`app_set_status_gm` (it names creatures, and one may be
  hidden); only the usage hint is plain.

## Where it lives

`app_character.c` holds the command; the choice of squares is one function in `play.c`
(`play_ring_squares`: summoner, the band's two edges, footprint, cursor, out, max) so the
channel can ask it later. Distance is the range overlay's own measure from a footprint
(`range_units_to`'s arithmetic, shared rather than written again). The channel's request
(`summon N TEMPLATE BAND at LABEL`) is left for when an agent needs it.

## Performance considerations

Reference figures: docs/PERFORMANCE.md, *Speed of light* (desktop). N creatures summoned, S
squares in the band's bounding box, C creatures on the map.

| path | speed of light | the design | the gap, and why accepted |
|---|---|---|---|
| choosing the squares | read the S squares' tiles and test each against the creatures on them: Far at 5 ft is a 25x25 box, 625 bytes of tiles | walk the box, clipped to the map; a distance and a tile test a square; the creature test (a scan of C) only for squares that pass; keep the N best | the creature test is O(C) a surviving square, as every placing test is today. A command, typed once: at C = 500 about 300,000 comparisons, under a millisecond. Measured, and bounded by the box, never the map |
| reading the template | one file read, a few KB | `character_load`, once, not once a creature | none |
| placing | N creatures appended | N calls of `character_place` in one batch | none |
| the frame after | the cells of N new creatures | the renderer's diff | none |

## Instrumentation

`PROF_ZONE("summon")`; a `perf.sh` row `play, summon 3 far` (summon, then undo: loop-neutral);
PERFORMANCE.md's row and paragraph. Tests in the `characters` suite: the band's edges, the
cursor's ordering, a 2x2 template's copies not overlapping, a blocked ring (all or nothing),
the other-floor refusal, numbering, one undo step, no ruleset. Docs: the command's line on
the `?` page (src/keys.c), the README (*Characters* and *Daggerheart*), CODEMAP's rows.

## Open question

1. **The band's ring or the band's disk?** Proposed: the ring ("at Far" means farther than
   Close). The other reading, anywhere within Far, is one comparison fewer.
