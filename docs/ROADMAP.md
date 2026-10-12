# Roadmap

Features decided on. Each gets a plan signed off before any code. An item that ships moves to
*Built* at the end and keeps its number -- the design docs cite items by number, so numbers are
never reused -- and the README describes it from then on. Ideas not yet decided live in
IDEAS.md.

## Order

Suggested, of what is left: 12, 18, 8, 19, 22, 7, 11; then 13, 14 and 15 when play asks for
them, and 1 if play testing of hidden creatures asks for it.

## To build

Item 1 was decided 2026-09-27, the rest 2026-09-29.

### Any ruleset

- **1. Messages marked at the source**, if play testing of hidden creatures (built 2026-09-27)
  asks for it. While any creature is hidden, the players' screens show no status messages --
  safe, but costly when something stays hidden all session (no rolls, rounds or spotlight news
  on the phones). The fix: messages that name no creature stay public; those that do go
  through one helper that keeps them to the GM when a creature they name is hidden; counts
  count only what the players can see; and a sweep test runs every play key and command with a
  creature hidden and checks its name never reaches the players' frame.
- **7. Lasting effect zones.** A named area of effect -- a wall of fire, magical darkness, a
  blessed circle -- that stays on the map, tinted, for the GM and, when the GM says so, the
  players; taken away by name (`remove`). Perhaps blocking sight, as fog does. Builds on named
  areas and the range tint.
- **8. Map import from generators.** A `--import` map tool reading Watabou's One Page Dungeon
  JSON (and perhaps donjon's text) into a `.vtt` map: rooms, doors, walls, the generator's
  notes as GM notes. For prep, which is all hand work or an agent's room language today. JSON
  read by hand, no dependencies.
- **22. Export a map as SVG.** `vtt --svg crypt.vtt`: walls, doors, windows, terrain, labels
  and the grid as SVG (plain text, no dependencies), creatures optional; GM-only things
  (secret doors, notes, fog) left out unless asked. To print a battle map, or post the dungeon
  after the session.

### Daggerheart

Decided 2026-09-29 from the SRD 2.0 of 2026-08-25. Each applies only
under `:ruleset daggerheart`, keeps the core rules-agnostic (the ruleset supplies the
numbers and words), and never rolls or ticks by itself: the dice stay physical and the GM
says what happened. The Fear pool stays set aside (IDEAS.md).

- **11. Battle Points.** `:battle` tallies the adversaries on the map, or in a scene, by type
  against [(3 x PCs in combat) + 2] and the SRD's adjustments (-1 easier, -2 for two or more
  Solos, -2 for +1d4 damage, +1 for a lower-tier adversary, +1 with no Bruiser, Horde, Leader
  or Solo, +2 harder) at the SRD's costs (Minions 1 per party-sized group, Social or Support
  1, Horde, Ranged, Skulk or Standard 2, Leader 3, Bruiser 4, Solo 5).
- **12. Dynamic countdowns.** Clocks gain the SRD's kinds: progress and consequence clocks
  that the GM advances by a roll's outcome (`:outcome success fear`: each moves 0-3 by the
  SRD's chart), linked pairs, loops that grow or shrink, and long-term clocks that advance on
  `:rest`.
- **13. Conditions.** Hidden, Restrained and Vulnerable as known markers carrying their rules
  text; Vulnerable set when a creature marks its last Stress and cleared when it clears any.
- **14. The action tracker** (the SRD's optional rule): each player's tokens in the spotlight
  panel, three a scene by default, spent with a key and refilled per scene.
- **15. Adversary tokens and pools**: the tokens Slow and Relentless count on a stat block,
  and pools shared by adversaries of a name, emptied when the scene ends. Perhaps folded into
  item 9.
- **18. Environment cards.** The SRD's environment stat block -- type, Difficulty, impulses,
  features, Fear features, potential adversaries -- as a GM-only card on a map or a named
  area, beside item 6's adversary card; its potential adversaries open the template picker
  filtered to them.
- **19. Summoning at a range.** For features like "summon three Lackeys at Far range":
  `:summon 3 lackey far` puts three of a template at that band from the selected creature, on
  free ground, numbered. Needs item 9.

## Parked

- **20. Picture handouts.** *Parked 2026-10-03 with its plan finished (docs/PICTURES.md): at
  the table, physical props instead.* `:handout crypt-door.jpg` sends an image from the
  handouts folder to the players' screens -- a creature, a drawn map, a letter's scan -- shown
  as the text card is, closable the same way; the terminal names it. The image travels as data
  over the existing connection, not as page code, so the page's size budget holds.

- **24. Vehicles.** *Parked 2026-10-12 with its plan written and reviewed (docs/VEHICLES.md):
  not enough gained at the table for the size of the change.* A carriage, a cart, a raft: a
  named area that moves, carrying whoever stands on it; any creature can be switched to not
  riding. Trains and airships are maps of their own, with what exists.

## Built

| # | item | built | where it is written up |
|---|---|---|---|
| 2 | Character templates: a creature saved once and placed by name from a picker | 2026-09-27 | docs/CHARACTERS.md |
| 3 | Scenes: the creatures and the fight saved under a name and put back | 2026-09-27 | docs/SCENES.md |
| 4 | Handouts on the phones: a short text card pushed to the players' screens | 2026-09-27 | docs/HANDOUTS.md |
| 5 | Links between map files: a link end that opens another map and carries the party | 2026-09-27 | docs/MAPLINKS.md |
| 6 | Adversary reference cards: a GM-only card saved with a template, shown for the selected creature | 2026-09-29 | docs/CARDS.md |
| 9 | Adversary stat blocks as templates: cards read by label, and `--import-adversaries` | 2026-09-29, with 6 | docs/CARDS.md |
| 10 | Damage against thresholds: `:dmg 11` marks HP by the card's thresholds and says why | 2026-10-02 | docs/DAMAGE.md |
| 16 | Movement readout that knows the rules: the SRD's rule for the band a carried creature has reached | 2026-09-29 | the README, *Rulesets*, *Daggerheart* |
| 17 | Group effects from a point: `g e` rings Very Close round the cursor and lists who is caught | 2026-09-29 | the README, *Group effects* |
| 21 | A camera for a TV: `:player camera follow\|party\|hold` | 2026-10-04 | docs/CAMERA.md |
| 23 | Whisper to one player: a handout to one phone; each phone picks a name when it joins | 2026-10-03 | docs/WHISPER.md |

## Done, not features

**Housekeeping** (2026-09-27): the `:` command chain became a table, and `tests/run.c` was
split by area. docs/CODEMAP.md's watch list keeps what is left.

**Health check** (2026-09-28): findings and a proposed order in docs/HEALTH.md -- disk
flushes first (a plan), then test hygiene, the README's gaps, creature culling and the perf
harness, test coverage, and behavior-neutral refactors. All six done 2026-09-28; what is
left is listed there as still open.
