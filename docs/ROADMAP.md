# Roadmap

Features decided on. Each gets a plan signed off before any code. An item that ships moves to
*Built* at the end and keeps its number -- the design docs cite items by number, so numbers are
never reused -- and the README describes it from then on. Ideas not yet decided live in
IDEAS.md.

## Order

**On branch vtt-2.0, decided 2026-10-18:** step 5's second piece, part 3 (docs/V2.md); then
25; then 26 (its plan first); then the rest of 2.0 (steps 3, 6-9). **27** (the user,
2026-10-21) is planned after step 7's plan; its parts ride on step 8 and on 26's live check.

Suggested, of what is left: 12, 8, 22, 7, 11; then 14 and 15 when play asks for
them, and 1 if play testing of hidden creatures asks for it.

## To build

Item 1 was decided 2026-09-27, the rest 2026-09-29.

### Any ruleset

- **26. A Claude API agent** (the user, 2026-10-17: API credits in hand). An agent option
  that calls the Claude API directly, beside `:agent command CMD`. A written plan first: no
  dependencies beyond libc/POSIX means no TLS in the binary (a helper process, e.g. curl, is
  the likely shape); the API key is never written into a map, a log or the repo. **The plan: docs/APIAGENT.md** (signed off, built and reviewed 2026-10-20; owed: the live check, held until the user has a key set up).

- **27. The agent features improve themselves, with the GM's hand on it** (the user,
  2026-10-21: "a self improvement loop in agent based features"). What exists: the guide is
  checked against the binary (suite `agentguide`), refusals reach the session log, each job
  keeps the GM's verdict and feedback, the API agent logs its cost; the skill's description
  was measured once (2026-10-19) but its queries were not kept. The loop, in this order:
  (1) the evaluations kept in the repo (`tools/skill/evals/`): the trigger queries, and a
  dozen tasks graded by vtt itself (`--check` clean, `--describe`'s facts, refusals and
  requests a task), never by a model; (2) each job's outcome logged with the guide's hash
  (verdict, refusals by class and verb, requests, tokens) -- with step 8, which owns the
  session log; (3) `tools/agentreport`, a local rollup of the session logs: counts, verbs and
  classes only, never a map's content (the logs hold the GM's secrets); (4) a written
  procedure: a reviewer drafts guide or skill edits from the rollup and the failed
  evaluations, they go through a plan and the user's sign-off, and ship only if no
  evaluation gets worse. Not: an agent editing the guide itself (the guide steers every
  agent: a prompt-injection path), logs leaving the machine, or spending on evaluations
  without the user's go (the first task run folds into 26's live check). **The plan: docs/AGENTLOOP.md** (signed off 2026-10-21, each question as recommended).

- **28. The phone page tested in a real browser** (proposed 2026-10-21, with step 7's plan,
  after the user asked for phone-side tests). What exists: suite `pagefeed` runs the page's
  decoder under node, and `pagebench.js VERIFY=1` checks its pixels; nothing runs the page
  whole (connecting, the name choice, reconnecting, a handout on screen, a turned screen). A
  node script drives `chromium --headless` over its DevTools protocol (node's own WebSocket,
  nothing installed) against a sandboxed `:serve`, with a phone's screen size and touch
  emulated, and checks the canvas against the C decoder's frame. Skipped without Chromium, as
  `pagefeed` is without node. Not taken: a mock browser in node (a second copy of the browser
  to keep in step with the page). A plan first.

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

Decided 2026-09-29 from the [SRD 2.0 of 2026-08-25](https://www.daggerheart.com/wp-content/uploads/2026/08/DH_SRD_2_2026_08_25.pdf). Each applies only
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
- **14. The action tracker** (the SRD's optional rule): each player's tokens in the spotlight
  panel, three a scene by default, spent with a key and refilled per scene.
- **15. Adversary tokens and pools**: the tokens Slow and Relentless count on a stat block,
  and pools shared by adversaries of a name, emptied when the scene ends. Perhaps folded into
  item 9.

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
| 13 | Conditions: Hidden, Restrained and Vulnerable as known markers; Vulnerable follows the last Stress | 2026-10-12 | docs/CONDITIONS.md |
| 16 | Movement readout that knows the rules: the SRD's rule for the band a carried creature has reached | 2026-09-29 | the README, *Rulesets*, *Daggerheart* |
| 17 | Group effects from a point: `g e` rings Very Close round the cursor and lists who is caught | 2026-09-29 | the README, *Group effects* |
| 18 | Environment cards: a card as the environment of a map or an area (`:env`), its potential adversaries in the picker, the SRD's imported | 2026-10-12 | docs/ENVIRONMENTS.md |
| 19 | Summoning at a range: `:summon 3 lackey far` lights the range round a creature and the GM places them inside it | 2026-10-12 | docs/SUMMON.md |
| 21 | A camera for a TV: `:player camera follow\|party\|hold` | 2026-10-04 | docs/CAMERA.md |
| 23 | Whisper to one player: a handout to one phone; each phone picks a name when it joins | 2026-10-03 | docs/WHISPER.md |
| 25 | A thin agent skill (`make install-skill`) and `vtt --agent-guide`, the guide built into the binary | 2026-10-18 | docs/AGENTS.md's top; the README, *Working with AI agents* |

## Done, not features

**Housekeeping** (2026-09-27): the `:` command chain became a table, and `tests/run.c` was
split by area. docs/CODEMAP.md's watch list keeps what is left.

**Health check** (2026-09-28): findings and a proposed order in docs/HEALTH.md -- disk
flushes first (a plan), then test hygiene, the README's gaps, creature culling and the perf
harness, test coverage, and behavior-neutral refactors. All six done 2026-09-28; what is
left is listed there as still open.
