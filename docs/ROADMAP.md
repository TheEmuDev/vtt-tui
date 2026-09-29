# Roadmap

Features decided on, in the order they will be built. Each gets a plan signed off before
any code, and moves to the README when it ships. Ideas not yet decided live in IDEAS.md.

1. **Messages marked at the source**, if play testing of hidden creatures (built
   2026-09-27) asks for it. While any creature is hidden, the players' screens show no
   status messages -- safe, but costly when something stays hidden all session (no rolls,
   rounds or spotlight news on the phones). The fix: messages that name no creature stay
   public; those that do go through one helper that keeps them to the GM when a creature
   they name is hidden; counts count only what the players can see; and a sweep test runs
   every play key and command with a creature hidden and checks its name never reaches the
   players' frame.
2. **Character templates.** A creature saved once -- label, size, counters, note, named
   rolls -- and placed by name from a picker (`i t e`, `i t p`, the channel's `token add
   enemy C3 from ghoul`). Stamps for creatures: fight prep without the typing. The picker
   serves `:stamp` too. Plan: docs/CHARACTERS.md.
3. **Scenes.** The creatures' places, markers and turn order saved under a name and put
   back: reset an encounter, stage "before" and "after the ambush", give an agent a point
   to diff against (the channel's unplanned checkpoints, CONTROL.md). Plan: docs/SCENES.md.
4. **Handouts on the phones.** A short text card -- an inscription, a letter -- pushed to
   the players' screens and taken down again: `:handout NAME`, `:handout say TEXT`,
   `:handout off`. Plan: docs/HANDOUTS.md.
5. **Links between map files.** A link end that opens another map and carries the party
   over (the town to the dungeon): `:link to crypt Entrance`, `g o`. Plan:
   docs/MAPLINKS.md.

Items 2-5 are built (2026-09-27). Next, decided 2026-09-29:

6. **Adversary reference cards.** A multi-line, GM-only card saved with a character
   template -- difficulty, thresholds, attack, features, whatever the GM writes -- edited
   with `s k` and shown in a side panel for the selected creature, open while the fight
   runs. What the book is otherwise open for mid-turn. Reuses the handout card's wrapping
   and the template files.
7. **Lasting effect zones.** A named area of effect -- a wall of fire, magical darkness, a
   blessed circle -- that stays on the map, tinted, for the GM and, when the GM says so,
   the players; taken away by name (`remove`). Perhaps blocking sight, as fog does. Builds
   on named areas and the range tint.
8. **Map import from generators.** A `--import` map tool reading Watabou's One Page
   Dungeon JSON (and perhaps donjon's text) into a `.vtt` map: rooms, doors, walls, the
   generator's notes as GM notes. For prep, which is all hand work or an agent's room
   language today. JSON read by hand, no dependencies.

**Housekeeping** (2026-09-27): the `:` command chain became a table, and `tests/run.c` was
split by area. CLAUDE.md's watch list keeps what is left.

**Health check** (2026-09-28): findings and a proposed order in docs/HEALTH.md -- disk
flushes first (a plan), then test hygiene, the README's gaps, creature culling and the perf
harness, test coverage, and behavior-neutral refactors. All six done 2026-09-28; what is
left is listed there as still open.
