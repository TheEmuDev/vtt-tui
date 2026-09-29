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

**Daggerheart** (decided 2026-09-29, from the SRD 2.0 of 2026-08-25). Each applies only
under `:ruleset daggerheart`, keeps the core rules-agnostic (the ruleset supplies the
numbers and words), and never rolls or ticks by itself: the dice stay physical and the GM
says what happened. The Fear pool stays set aside (IDEAS.md).

9. **Adversary stat blocks as templates.** A template gains the stat block's fields: tier,
   type (Bruiser, Horde, Leader, Minion, Ranged, Skulk, Social, Solo, Standard, Support),
   Difficulty, Major and Severe thresholds, attack modifier, standard attack (name,
   range, damage), Experiences, and features (actions, reactions, passives, Fear
   features), which item 6's card shows. With item 8, an import of the SRD's adversary
   list from a file the GM downloads (the SRD's license asks for attribution; vtt ships
   no SRD data).
10. **Damage against thresholds.** `:dmg 11` on the selected adversary marks HP by its
    thresholds -- 1 below Major, 2 at Major, 3 at Severe, 4 at twice Severe if the table
    uses massive damage -- and says why. Minion (X): any damage defeats it, and every X
    damage defeats another minion in range, which vtt lists. Horde (X): once half its HP is
    marked the status line says its attack is now X.
11. **Battle Points.** `:battle` tallies the adversaries on the map, or in a scene, by
    type against [(3 x PCs in combat) + 2] and the SRD's adjustments (-1 easier, -2 for
    two or more Solos, -2 for +1d4 damage, +1 for a lower-tier adversary, +1 with no
    Bruiser, Horde, Leader or Solo, +2 harder) at the SRD's costs (Minions 1 per party-sized
    group, Social or Support 1, Horde, Ranged, Skulk or Standard 2, Leader 3, Bruiser 4,
    Solo 5).
12. **Dynamic countdowns.** Clocks gain the SRD's kinds: progress and consequence clocks
    that the GM advances by a roll's outcome (`:outcome success fear`: each moves 0-3 by
    the SRD's chart), linked pairs, loops that grow or shrink, and long-term clocks that
    advance on `:rest`.
13. **Conditions.** Hidden, Restrained and Vulnerable as known markers carrying their
    rules text; Vulnerable set when a creature marks its last Stress and cleared when it
    clears any.
14. **The action tracker** (the SRD's optional rule): each player's tokens in the
    spotlight panel, three a scene by default, spent with a key and refilled per scene.
15. **Adversary tokens and pools**: the tokens Slow and Relentless count on a stat
    block, and pools shared by adversaries of a name, emptied when the scene ends.
    Perhaps folded into item 9.

16. **Movement readout that knows the rules.** Carrying a creature, the readout adds the
    SRD's rule for the band it has reached: a PC under pressure moves within Close as part
    of an action and needs an Agility Roll to go farther; an adversary moves within Close
    for free, or within Very Far as a separate action ("Far: Agility Roll to do it
    safely").
17. **Group effects from a point.** The SRD's default area of effect: unless an effect
    says otherwise, its targets are within Very Close of one origin point. A key (`g e`)
    rings that area round the cursor and lists who is caught; a band word or count widens
    it for the effects that say otherwise (`:effect close`).
18. **Environment cards.** The SRD's environment stat block -- type, Difficulty, impulses,
    features, Fear features, potential adversaries -- as a GM-only card on a map or a
    named area, beside item 6's adversary card; its potential adversaries open the
    template picker filtered to them.
19. **Summoning at a range.** For features like "summon three Lackeys at Far range":
    `:summon 3 lackey far` puts three of a template at that band from the selected
    creature, on free ground, numbered. Needs item 9.

**Any ruleset** (decided 2026-09-29):

20. **Picture handouts.** `:handout crypt-door.jpg` sends an image from the handouts
    folder to the players' screens -- a creature, a drawn map, a letter's scan -- shown as
    the text card is, closable the same way; the terminal names it. The image travels as
    data over the existing connection, not as page code, so the page's size budget holds.
21. **A camera for a TV.** `:player camera follow|party|hold`: the players' view follows
    the GM's screen (as now), frames every player creature, or holds still while the GM
    scouts elsewhere. The players' view already has its own camera for floors.
22. **Export a map as SVG.** `vtt --svg crypt.vtt`: walls, doors, windows, terrain, labels
    and the grid as SVG (plain text, no dependencies), creatures optional; GM-only things
    (secret doors, notes, fog) left out unless asked. To print a battle map, or post the
    dungeon after the session.
23. **Whisper to one player.** `:whisper Aria You notice the floor is warm here`: a handout
    to one phone only. Each phone picks a name when it joins (a player creature, or free
    text), which later per-player features can use too; the join step is the real work.

Suggested order: 16 and 17 (small, every fight), then 6 and 9 together (the card and what
it holds), then 10, 20, 21, 23, 12, 18, 8, 19, 22, 7, 11; 13, 14 and 15 when play asks for them.

**Housekeeping** (2026-09-27): the `:` command chain became a table, and `tests/run.c` was
split by area. CLAUDE.md's watch list keeps what is left.

**Health check** (2026-09-28): findings and a proposed order in docs/HEALTH.md -- disk
flushes first (a plan), then test hygiene, the README's gaps, creature culling and the perf
harness, test coverage, and behavior-neutral refactors. All six done 2026-09-28; what is
left is listed there as still open.
