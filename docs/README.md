# The docs, and who each is for

The user's manual is the [README](../README.md) at the top of the repository; nothing here
replaces it. This folder holds three other kinds of page. Check a page's kind before trusting
its present tense: a design record describes the app as it was when the plan was written.

## Kept current

True of the code today, and changed in the same commit as the code they describe (CLAUDE.md,
*A change is not done*). Held to it twice: suite `docs` (tests/test_docs.c) fails when a source
file has no row in CODEMAP's tables, a code-describing doc (CLAUDE.md, tests/CLAUDE.md, CODEMAP,
ARCHITECTURE, the review brief) names a function or macro with an underscore or a file alone
that the code no longer has, or any of these pages names a path under src/, tests/, tools/,
docs/ or web/ that is gone; and every
review's *Docs drift* section (tools/review-brief.md) reads these pages where they speak of what
the change touched, since a sentence gone wrong is beyond any test (the user, 2026-10-23).

This is the whole list of kept-current pages: the table below, and outside this folder the
[README](../README.md) (the user's manual), `CLAUDE.md` and `tests/CLAUDE.md` (the coding
agent's rules), `src/keys.c` (the bar and the `?` page), and tools/review-brief.md.

| page | for | what it is |
|---|---|---|
| [AGENTS.md](AGENTS.md) | an AI agent building or editing maps for a GM (not the agent working on vtt's code: that is `CLAUDE.md`) | the request language, the three ways in, working live with the GM. Read whole |
| [ARCHITECTURE.md](ARCHITECTURE.md) | anyone learning how vtt works: the maintainer, the coding agent, a reviewer | the subsystems, how they interact and what data goes where: start-up and the loop, a key to the terminal, the players' frame to the phones and back, the channel and its agents, the disk. Read first; CODEMAP has the detail |
| [CODEMAP.md](CODEMAP.md) | the coding agent | what each source file owns and its invariants, every command, the conventions, the watch list. Read a row at a time |
| [KEYS.md](KEYS.md) | anyone binding a key | the nine rules every binding follows |
| [PERFORMANCE.md](PERFORMANCE.md) | the maintainer and the coding agent | every measured path; *Speed of light* and *Working rules* are what a plan's estimates use |
| [HEALTH.md](HEALTH.md) | the maintainer and the coding agent | the questions every review asks, the findings left open, and each health check's record |
| [ROADMAP.md](ROADMAP.md) | the maintainer | features decided on: to build, parked, built |
| [IDEAS.md](IDEAS.md) | the maintainer | features set aside, each with the reason |
| [SCRATCH.md](SCRATCH.md) | the coding agent and its reviewers | the kinds of scratch work reviews build, and which became tools (seen twice: a tool) |

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
| [APIAGENT.md](APIAGENT.md) | the Claude API agent: `:agent claude`, vtt as its own agent through curl; the settings and Options; the first `:ask` | built 2026-10-20 (reviewed and timed; the live check owed); its key's flow redone 2026-10-23, *The key, made safer* (one check waits on the user's go) |
| [AGENTLOOP.md](AGENTLOOP.md) | ROADMAP 27, the agent features' improvement loop: evaluations graded by vtt, each job's outcome on record, a rollup, the procedure | a plan, signed off 2026-10-21; nothing built |

## Not built

| page | what it is |
|---|---|
| [PICTURES.md](PICTURES.md) | picture handouts: a finished plan, parked 2026-10-03 |
| [VEHICLES.md](VEHICLES.md) | vehicles (an area that moves, with riders): a reviewed plan, parked 2026-10-12 |
| [V2.md](V2.md) | vtt 2.0, the rewrite (branch `vtt-2.0`): a plan, its questions answered 2026-10-12; being built: its *Next* is the order of what is owed and its *Progress* what is done (2026-10-23: work through step 7 recorded there, step 3 cut, steps 8 and 9 to come) |
| [reviews/](reviews/) | the reviews V2.md rests on: agent architecture, performance, testing (2026-10-12, true of e7c58e9), and memory (2026-10-20, true of d66b883; its M1 and M2 reproduced by the planner the same day). Reports as written |

A new page gets a row here in the commit that adds it.
