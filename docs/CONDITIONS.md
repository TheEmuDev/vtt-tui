# Conditions

> **Status: signed off 2026-10-12, to be built after summoning (docs/SUMMON.md).**

Roadmap item 13. Under `:ruleset daggerheart`, Hidden, Restrained and Vulnerable are markers
the tool knows: typed in any case they get one spelling and one color, the GM can read what
each does, and Vulnerable comes and goes with a creature's last Stress.
**Additive: a table in the ruleset, one hook where counters change. No new data, no file
change.** About 300 lines with its tests and docs.

## Progress

*Not started. Next to build (2026-10-12): summoning, item 19, went in first as 8097d0c.*

Where to begin, from what building summoning turned up:

1. `ruler.c`/`ruler.h`: `Ruleset.conditions` and `Ruleset.auto_condition`, filled in for
   Daggerheart. The three texts are the SRD's, word for word: check them against the SRD
   itself before writing them, and cite the page.
2. The marker prompt (`PROMPT_STATUS_LABEL`, app.c about line 741): a whole-word match,
   any case, takes the table's spelling and color.
3. `ruleset_counter_changed(rs, before, after)`, called where the GM edits a counter: the
   `s v` prompt (app.c, the `counter_apply` call) and `<`/`>` (`counter_step`, app_play.c).
   Counters count what is left, so the last Stress marked is Stress at 0.
4. The card box: `app_card_shown` (app_draw.c) is the one test, and `app_view_differs`
   reads it; the creature with a known marker and no card goes into that function.
5. `:condition`, the `?` page (src/keys.c), the README's *Daggerheart*, CODEMAP, a
   `perf.sh` row, then the Fable review, HEALTH.md's line for it, and roadmap item 13 to
   *Built*.

Lessons from summoning that apply here: a plain `app_set_status` reaches the phones unless
fog or a hidden creature is about, so anything naming a creature or its numbers is
`app_set_status_gm`; `make perf` three times and `tools/median.py` for the tables, and A/B
any row that moves by a tenth against the commit before; `make test` takes over two
minutes from clean.

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
- **Vulnerable follows Stress**, for every creature that has a Stress counter, whichever
  side it is on. Counters count what is left, so "marks its last Stress" is Stress reaching 0:
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
  counter's name, the condition's name). The core reads the table and knows
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
GM's own Vulnerable kept, four markers full, an adversary the same as a PC, no ruleset, and the
players' frame never holding the text (a creature with a marker and no card included).
Docs: `:condition` on the `?` page (src/keys.c), the README's *Daggerheart*, CODEMAP's rows.

## Decided with the user, 2026-10-12

- **Adversaries too.** Vulnerable follows the last Stress for every creature, player
  characters and adversaries alike (the user's ruling, written into this page).
- The SRD's three conditions; a fourth is one row in the table.
