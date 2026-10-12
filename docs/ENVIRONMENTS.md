# Environment cards

> **Status: built 2026-10-12.** A design record: the plan as it was signed off. What is true
> now is the README (*Environments*) and the rows of `card.c`, `import.c` and `ruler.c` in
> docs/CODEMAP.md. *As built*, at the end, says where the code left the plan, step by step.

Roadmap item 18. The SRD's environment stat block (type, Difficulty, impulses, features,
Fear features, potential adversaries) as a GM-only card on a map or on a named area, beside
the adversary card of item 6; its potential adversaries open the template picker filtered
to them. **The largest of the three: two new fields, a file version, a store, an importer.**
About 700 lines with its tests and docs, in three steps of which the last two can be
dropped; step 1 alone is about 300.

## Progress

*All three steps are built and reviewed, 2026-10-12. Nothing of the plan is waiting. One
finding is left open: docs/HEALTH.md, row 47 (how a card's names are matched to saved
characters).*

## What it does

- **An environment is an ordinary card** (docs/CARDS.md) that the map or an area names, as
  a creature names its card. `:env NAME` gives it to the area under the cursor, or to the
  map when the cursor is in none; `:env map NAME` and `:env area "Crypt" NAME` say which
  outright; `:env remove` takes the name off (the card stays in the map's table). A floor
  is not found under the cursor (`map_area_at` skips floors), so a floor takes `:env area`.
- **Reading it.** `:env` with nothing after it opens the card for the cursor's square (the
  area's, else the map's) in the card modal, as `:card` does. The side box does not show it
  by itself: it would be up all session.
- **Writing one.** `:env edit` opens it in `$EDITOR` as `s k` does a creature's, starting
  from the ruleset's environment skeleton (`Ruleset.env_skeleton`: the SRD's labels ready
  to fill in) when it is new.
- **Potential adversaries.** `:env adversaries` reads the card's `Potential Adversaries:`
  line (`card_value`), keeps the names that are character templates, and opens the character
  picker holding only those. The SRD writes the line as groups with lists in parentheses
  (`Beasts (Bear, Dire Wolf)`), so the split respects parentheses and each name is turned
  into a template's name the way the importer does (`character_name_from_label`). Names
  with no template are listed on the status line.
- **GM-only throughout**: the modal is already in `app_view_differs`; every message is
  `app_note_gm`/`app_set_status_gm`.

## Data

- `Map.env` and `Area.env`: a card's name (`CARD_NAME_MAX`), "" for none.
- **The area's** is undone with the area: `OP_AREA`'s slot has a `card` field free for it,
  and `undo_set_area_env` sits beside `undo_set_floor`. A proposal carries it only if
  `area_equal` compares it and the apply loop calls the new setter (it sets an area's box
  and floor by separate calls, so a field it is not told of is lost). `map_copy_into`
  copies the fixed parts whole and needs nothing.
- **The map's** is changed round the undo log, as cards are. A change set has no slot for
  it, so an agent cannot propose a map's environment in this plan (`:env map` is the GM's);
  an area's it can.
- **A card that is not there.** A boxed accept never brings cards, and the map's table holds
  64, so an environment may name a card the map lacks, as a creature's card may today. It
  is tolerated the same way: `:env` says the card is missing, `--check` warns.
- File version 14: `env "card"` in the header and `areaenv "Area" "card"` after the area
  lines. An older build ignores an unknown line with a warning (W015) and would then save
  the map without it; the version makes it refuse instead, as each feature's has. A line
  naming an area the file lacks is dropped with a `diag()` of its own, checked at the end
  of the load as floors and links are, since the header comes before the cards. Codes: the
  next free when built (docs/VEHICLES.md, parked, also wrote down W030).
- `--check` warns of an environment naming a missing card; `--describe` prints the name
  beside the map and the area.

## Build order

1. **The card on the map and the area**: the fields, the file, `:env`, `:env edit`, the
   skeleton, `--check` and `--describe`. Useful alone: an environment typed or pasted in.
2. **Potential adversaries**: the picker's list-builder (`pick_items`, which reads the
   whole folder and loads every template) takes a set of names and loads only those.
3. **The SRD's environments, imported**: `--import-environments FILE` writes each as a text
   file in `$XDG_DATA_HOME/vtt/environments` (the importer and JSON reader exist;
   `store_name_ok` names them), and `:env NAME` copies one into the map's cards when the
   map has no card of that name. A fifth list in the picker.

## Performance considerations

Reference figures: docs/PERFORMANCE.md, *Speed of light* (desktop).

| path | speed of light | the design | the gap, and why accepted |
|---|---|---|---|
| a frame | nothing: nothing is drawn | nothing | none |
| `:env` | the card's text, wrapped once | the card modal as it is | none |
| `:env` finding the card | one area's name | `map_area_at`: at most 64 box tests, once a command | none worth closing; nothing walks the areas on a cursor move or a frame |
| `:env adversaries` | one directory read, and a file read a template named (a few KB each) | the same: only the named templates are loaded, where the full picker loads them all (3.2 ms for 500, PERFORMANCE.md) | none |
| a save | about 20 bytes an environment | the same | none |
| the import | read the file once, write a file an environment | `json_parse`, then a write each, no flush between | as `--import-adversaries` |

## Instrumentation

No drawing path, so no new zone in a frame. `PROF_ZONE("env.pick")` on the adversaries'
filter; the save and load tables re-measured with an environment on every area. Tests: an
`env` suite (set, remove, the area's before the map's, undo of the area's, the file's round
trip at 14 and an older map still written at its own version, each dropped line's code, the
players' frame never holding the text, a proposal carrying an area's), and the loader's
fuzz corpus gains the two lines. Docs: `:env` on the `?` page (src/keys.c), the README
(*Cards*, *Daggerheart*, *File format*), AGENTS.md's request, CODEMAP's rows.

## Decided with the user, 2026-10-12

- All three steps are built.
- Imported environments live in their own folder, as plain text.

## As built: what the plan did not say (step 1)

- **`env` is not a header line.** It is written in the body after the floor lines, with
  `areaenv`: header lines must come before the sections, and nothing was gained by putting
  one more rule on a hand-written file.
- **One code for a dropped line**, W030 `env-dropped`, whatever was wrong with it (no such
  area, not a card's name, not two quoted names, a second one for the same place). `--check` has W170 `env-no-card` for an
  environment whose card the map does not hold. The skeleton's first line carries a type
  where the adversary's carries one (`Tier 1 Exploration`).
- **`:env` reads the room's, else its floor's, else the map's** (`env_here`). The plan said
  "the area's, else the map's" and had floors reachable only by name; a floor's environment
  now shows from anywhere on the floor that no room with one of its own covers.
- **`:env NAME` with a missing card is allowed** and says so: name it, then `:env edit`
  writes it from the ruleset's block (`Ruleset.env_skeleton`, labels only).
- **`:env` shows its own copy of the card** (`App.card_own`), since a card's text can be
  replaced under an open view.
- **A change set does carry the map's own** (`env_changed`, beside the round and the
  spotlight), against the plan's *Data*. Without it another program's change to the `env`
  line alone read as "no changes" and the next save wrote it away; the review found it.
  There is still no request for an agent to set it. It is applied round the undo log.
- **The channel's request is `env AREA CARD` / `env AREA remove`.** An agent cannot write a
  card (the channel has no card request), only name one.
- **The save and load tables were not re-measured.** At most 65 short lines in a file; the
  tables' maps have no areas at all, and a row would show nothing. Nothing is on a frame
  or a keystroke's path, so there is no new zone and no `perf.sh` row for this step.
- **The suite is `env`** (tests/test_cards.c). `tests/fixtures/env.vtt` puts the two lines
  in the loader's fuzz corpus.

## As built: step 2

- **A group's own word is not a creature.** `Beasts (Bear, Dire Wolf), Bandit` gives Bear,
  Dire Wolf and Bandit: the text before a parenthesis is dropped, each name is made a
  character's with `character_name_from_label`, repeats go, at most 32 are read.
- **The picker is given names, not a filter** (`App.pick_only`): a name with no character
  file never reaches it, and is listed on the GM's status line instead.
- **The whole line is the list** (`card_line`, beside `card_value`): two spaces or a bar
  in it are part of what was written, where on a stat line they start another field.
- **`character_exists`** (character.c) is the one answer to "is there a saved character of
  this name"; the importer asks it too.
- **The names are matched as written**: `dire wolf` does not find `Dire-Wolf`, a name with
  a number at its end (`Skeleton 2`) is read as Skeleton, one over 31 bytes is cut. None of
  the SRD's names trips these; docs/HEALTH.md, *Left open by reviews*, row 47.
- **The GM is told how many were found**, the count first and the missing names after, as
  a note, so the log has the whole list when the line is too narrow for it.
- **Play mode only**, since choosing places the creature at the cursor.
- **Measured**: `env.pick` 25.6 µs typical, 50.2 µs at p99, for three of 500 saved
  (docs/PERFORMANCE.md), against 3.2 ms for the whole folder. As the plan's table said.
- The first benchmark script ended with `esc`, which the next loop's `:` turned into
  Alt-`:`; the command ran once. The row chooses a creature and takes it back instead.

## As built: step 3

- **`:env pick`** is how the fifth list is reached; choosing one is `:env NAME`, so it goes
  to the area under the cursor, else the map.
- **A kept environment is a card's text**, cleaned and cut as a card stores it when it is
  imported, so what `:env` copies in is what the file holds. It is copied only when the map
  has no card of that name; after that the map's copy is the map's.
- **A feature's question** (the SRD lists one under some features) goes on the line after
  the feature's text.
- **The names are a card's**, made as an adversary's are (`Misty Bog` is `Misty-Bog`). A
  file in the folder whose name no card can have is not offered, and a name the picker
  chose is never read as one of `:env`'s own words (`app_env_name`): a kept environment may
  be called `remove`.
- **Each feature is a block of its own**, a blank line between them; a file is written
  whole or not at all (a `.tmp` beside it, renamed).
- **The channel's `env AREA CARD` does not copy from the folder.** The card is the GM's to
  bring in; an agent names it.
- **The picker's refactor was A/B'd**: `env.pick` reads 29 to 30 µs on the commit before
  and after it, three runs each (a single later run read a tenth over the published
  median, which is what prompted the check).
- **`app_pick_named`** is now the one builder of a picker's rows (characters, stamps,
  handouts, environments): this list was the third of its kind. `card_clean_cut` says when
  a text was more than a card holds, which is what the import's message reads.
- **No new zone or row**: the import is a command-line run, and `:env pick` is the picker
  (`picker.open`), reading one small file an environment as the handouts' list does.
- **The suite is `envimport`** (tests/test_cards.c), with made-up environments: vtt ships
  none of the SRD's. The public SRD project's list was imported once into a scratch
  folder by the reviewer: 19 written, none skipped or cut.
