# The docs, and who each is for

The user's manual is the [README](../README.md) at the top of the repository; nothing here
replaces it. This folder holds three other kinds of page. Check a page's kind before trusting
its present tense: a design record describes the app as it was when the plan was written.

## Kept current

True of the code today, and changed in the same commit as the code they describe.

| page | for | what it is |
|---|---|---|
| [AGENTS.md](AGENTS.md) | an AI agent building or editing maps for a GM (not the agent working on vtt's code: that is `CLAUDE.md`) | the request language, the three ways in, working live with the GM. Read whole |
| [CODEMAP.md](CODEMAP.md) | the coding agent | what each source file owns and its invariants, every command, the conventions, the watch list. Read a row at a time |
| [KEYS.md](KEYS.md) | anyone binding a key | the nine rules every binding follows |
| [PERFORMANCE.md](PERFORMANCE.md) | the maintainer and the coding agent | every measured path; *Speed of light* and *Working rules* are what a plan's estimates use |
| [HEALTH.md](HEALTH.md) | the maintainer and the coding agent | the questions every review asks, the findings left open, and each health check's record |
| [ROADMAP.md](ROADMAP.md) | the maintainer | features decided on: to build, parked, built |
| [IDEAS.md](IDEAS.md) | the maintainer | features set aside, each with the reason |

## Design records

Why a feature is the way it is: the plan as it was signed off, then what the building
changed. Read one before changing its feature, for the decisions and the roads not taken; do
not read one to learn what the app does now. Each opens with a **Status** line, which is the
source: the column below is a copy, changed in the same commit.

| page | feature | status |
|---|---|---|
| [REMOTE.md](REMOTE.md) | the players' view: server, page, watcher, and the page's size budget | built; moves from a phone set aside |
| [FOG.md](FOG.md) | fog of war, sight, counters, the players' frame | built 2026-09-23/24 |
| [CONTROL.md](CONTROL.md) | the control channel and the room language | built 2026-09-26; one decision reversed by CONFLICTS.md |
| [FLOORS.md](FLOORS.md) | floors | built 2026-09-27 |
| [CHARACTERS.md](CHARACTERS.md) | character templates and the picker | built 2026-09-27 |
| [SCENES.md](SCENES.md) | scenes | built 2026-09-27 |
| [HANDOUTS.md](HANDOUTS.md) | text handouts on the players' screens | built 2026-09-27 |
| [MAPLINKS.md](MAPLINKS.md) | links between map files | built 2026-09-27 |
| [CARDS.md](CARDS.md) | cards, and importing the SRD's adversaries | built 2026-09-29 |
| [DAMAGE.md](DAMAGE.md) | `:dmg` | built 2026-10-02 |
| [WHISPER.md](WHISPER.md) | phones' names and whispers | built 2026-10-03 |
| [CAMERA.md](CAMERA.md) | the players' camera | built 2026-10-04 |
| [SUMMON.md](SUMMON.md) | summoning at a range: `:summon` | built 2026-10-12 |
| [CONDITIONS.md](CONDITIONS.md) | Daggerheart's conditions as known markers; Vulnerable follows Stress | built 2026-10-12 |
| [ENVIRONMENTS.md](ENVIRONMENTS.md) | environment cards on a map or an area: `:env`, its adversaries, the import | built 2026-10-12 |
| [PAGESPEED.md](PAGESPEED.md) | where a frame goes on a phone, and what was done about it | built 2026-10-04 |
| [CONFLICTS.md](CONFLICTS.md) | proposals, jobs and the review; other writers of the map's file | built 2026-10-08 to 10; *Progress* says what is still owed; decision 5 (an off channel takes proposals) reversed 2026-10-12 by V2.md's ruling 9 |
| [APIAGENT.md](APIAGENT.md) | the Claude API agent: `:agent claude`, vtt as its own agent through curl; the settings and Options; the first `:ask` | built 2026-10-20; its review, live check and timings owed |

## Not built

| page | what it is |
|---|---|
| [PICTURES.md](PICTURES.md) | picture handouts: a finished plan, parked 2026-10-03 |
| [VEHICLES.md](VEHICLES.md) | vehicles (an area that moves, with riders): a reviewed plan, parked 2026-10-12 |
| [V2.md](V2.md) | vtt 2.0, the rewrite (branch `vtt-2.0`): a plan, its questions answered 2026-10-12; being built (steps 0 to 2 and part of 4: its *Progress*) |
| [reviews/](reviews/) | the reviews V2.md rests on: agent architecture, performance, testing (2026-10-12, true of e7c58e9), and memory (2026-10-20, true of d66b883; its M1 and M2 reproduced by the planner the same day). Reports as written |

A new page gets a row here in the commit that adds it.
