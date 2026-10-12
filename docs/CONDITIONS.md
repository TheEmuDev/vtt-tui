# Conditions

> **Status: a plan, awaiting sign-off (written 2026-10-12).** Nothing here is built.

Roadmap item 13. Under `:ruleset daggerheart`, Hidden, Restrained and Vulnerable are markers
the tool knows: typed in any case they get one spelling and one color, the GM can read what
each does, and Vulnerable comes and goes with a player character's last Stress.
**Additive: a table in the ruleset, one hook where counters change. No new data, no file
change.** About 300 lines with its tests and docs.

## What it does

- **A known marker is still a marker.** `s` then the whole word in any case (`vulnerable`)
  puts down `Vulnerable` in the ruleset's color for it, whatever color is current. Only the
  whole word: a start of one (`v`, `re`, `hid`) stays the GM's own marker, as does
  `hidden guard`. Nothing
  else about markers changes: four a creature, a color and a word, saved as they are now.
  A map opened without the ruleset shows the same markers as plain words.
- **The rules text.** With the cursor on a creature carrying a known marker, the GM's card
  box lists each one's text under the card, or alone when the creature has no card. Today
  the box shows only for a creature with a card (`app_card_shown`), and that one function is
  also what keeps it off the phones (`app_view_differs`), so the new case goes into that
  function and nowhere else.
  `:condition NAME` shows one by name; `:condition` lists them. The text is the SRD's,
  copied word for word when built and cited by page.
- **Vulnerable follows Stress**, for player characters (the SRD's rule is about a PC).
  Counters count what is left, so "marks its last Stress" is Stress reaching 0:
  - Stress goes from above 0 to 0: Vulnerable is added, in the same undo step.
  - Stress goes from 0 to above 0: Vulnerable is taken off, in the same undo step.
  - Only the change does it. A Vulnerable the GM put on for another reason stays while
    Stress is above 0 and is not touched until Stress has been to 0 and back.
  - With four markers already on, the Stress is marked and the GM is told the marker did
    not fit.
  - A Stress counter made at 0 (`stress 0/6`) adds it; a Stress counter removed takes it
    off; a Vulnerable taken off by hand while Stress is 0 stays off until Stress next
    reaches 0.
- **Hidden is the condition, not the tool's hidden creature.** `Token.hidden` (`i h`) keeps
  a creature off the players' screens; the Hidden marker is a word the players can see.
  The marker's text says so in one line, and neither sets the other.
- The players see markers as they do now. The rules text and every message about a
  creature's Stress are the GM's alone (`app_note_gm`/`app_set_status_gm`).

## Where it lives

- `ruler.c`: `Ruleset.conditions` (name, color, text) and `Ruleset.auto_condition` (the
  counter's name, the condition's name, players only). The core reads the table and knows
  no game: another ruleset fills in its own or leaves it NULL.
- One function, `ruleset_counter_changed(rs, before, after)`, edits `after` before it is
  recorded, so the marker rides the same `OP_TOKEN_EDIT`. It is called from the two places
  the GM edits a Stress: the `s v` prompt (app.c) and `<`/`>` (app_play.c). If a third
  caller appears the three become one helper first.
- **Where it does not run, on purpose**: `:dmg` (HP only), a scene put back and an accepted
  proposal (both write creatures whole, markers and all, as they were saved or proposed).
  Those carry Vulnerable as data; the hook is about the GM's own edit.
- Undo needs nothing: undoing the Stress undoes the marker, being one edit.

## Performance considerations

Reference figures: docs/PERFORMANCE.md, *Speed of light* (desktop).

| path | speed of light | the design | the gap, and why accepted |
|---|---|---|---|
| a counter step | compare one counter before and after: 4 bytes | find the named counter among at most 4, compare, perhaps add or remove one of at most 4 markers | none: tens of nanoseconds on a keystroke that costs 0.4 µs (`counter.step`) |
| naming a marker | compare the typed word with 3 names | the same, on enter in the prompt | none |
| the card box | the text's bytes, when the cursor is on such a creature | up to 4 lookups in a 3-row table, then the box's own wrap | none; no cost on a creature with no known marker, none in the players' frame |
| a map with no ruleset | nothing | one NULL test | none |

## Instrumentation

No new zone: the work is inside `counter.step` and `card.draw`, whose rows are re-measured.
A `perf.sh` row `play, stress to zero and back` (`<` and `>` on a PC at 1 Stress,
loop-neutral). Tests: the spelling and color, Stress to 0 and back with one undo each, the
GM's own Vulnerable kept, four markers full, an adversary untouched, no ruleset, and the
players' frame never holding the text (a creature with a marker and no card included).
Docs: `:condition` on the `?` page (src/keys.c), the README's *Daggerheart*, CODEMAP's rows.

## Open questions

1. **Adversaries.** Proposed: Vulnerable follows Stress for player characters only, as the
   SRD has it. The roadmap's line says "a creature"; say if adversaries should do it too.
2. **More conditions.** Proposed: the SRD's three. Table-driven, so a fourth is one row.
