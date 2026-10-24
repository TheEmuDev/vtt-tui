# The agent features' improvement loop (ROADMAP 27)

**Status:** a plan, written 2026-10-21; signed off by the user the same day ("Go with your
recommendations": counts only, `claude -p` with the skill, Sonnet with N = 3). **Part 1 built
2026-10-23** (*Part 1, as built*, below: the runner, the grader and its test, twelve tasks, the
trigger queries, the procedure); no evaluation has run on a model yet, which waits on the user's
go. Parts 2 and 3 are not built.

The user, 2026-10-21: "a self improvement loop in agent based features". What an agent is
told (the skill, the guide, the Claude agent's prompt) should get better from evidence, with
the GM's hand on every change. This plan says what is measured, where it is kept, who reads
it, and what must hold before a change ships.

## What exists (read 2026-10-21)

- **What agents are told, three texts.** The skill, `tools/skill/SKILL.md` (28 lines,
  installed with `make install-skill`). The guide, docs/AGENTS.md (36,156 bytes), embedded by
  `tools/embed.sh` as `AGENT_GUIDE` and printed by `vtt --agent-guide`. And the Claude agent's
  own first block, `src/apiagent.c:73-90`, sent before the guide.
- **Checks of what they say.** Suite `agentguide` compares the embedded guide with
  docs/AGENTS.md byte for byte; suite `ctltable` checks the guide names exactly the verbs of
  `REQUESTS[]`. Nothing checks that an agent *succeeds* with them.
- **What a job leaves behind.**
  - `Job` (src/job.h) keeps the ask, the taker, a thread of 8 lines (the GM's feedback, the
    agent's `say`, what came of a proposal) and the change set. It is gone when the map closes.
  - The session log (`slog.c`) gets a refusal once (`host_refused`, app_ctl.c), but only while
    the GM has `:log` on, and it holds the map's content (creature names, notes).
  - The Claude agent appends a line a run to `$XDG_DATA_HOME/vtt/claude-usage.tsv`: the job's
    number, model, calls, tokens of each kind, dollars, how it ended (`usage_record`,
    apiagent.c:398). It is the only lasting record of a job today, and only for that agent.
- **A measurement made once.** The skill's description was tuned 2026-10-19 with
  skill-creator's trigger loop. Its queries were not kept. The scores of its first rounds were
  noise: parallel runs stole each other's triggers (memory, *skill-creator serially*).

## The loop

1. **Evaluations in the repo**, run on demand and graded by vtt.
2. **Each job's outcome on record**, with which texts the agent was given.
3. **A rollup**, local, of counts.
4. **A written procedure** that turns the evidence into a change of the texts, through the
   user's sign-off, shipped only if no evaluation gets worse.

Nothing in the loop changes the texts by itself. The guide steers every agent that reads it,
so a path by which an agent edits the guide is a prompt-injection path into every later job
(ROADMAP 27's *Not*).

### 1. Evaluations: `tools/skill/evals/`

**Trigger queries** (`triggers.json`, skill-creator's format): about 20, half that should
start the skill (make a map, edit a .vtt file, answer a GM's job) and half near misses that
should not (a TUI question in another project, a D&D rule, a JSON file named map). Run with
skill-creator's `run_eval --num-workers 1` from a scratch dir with its own `.claude/`
(memory, *skill-creator serially*). The score is recall and precision; each run is kept as
`runs/DATE-triggers.json`.

**Tasks** (`tasks/NAME/`), about a dozen, each:

- `prompt.txt`: what a GM would ask, in a GM's words ("a crypt of three rooms off a corridor,
  a skeleton in the last").
- `start.vtt` or `size` (a new map's WxH): the map the agent works on. Fixtures written for
  the evaluations, never a user's map.
- `expect`: facts vtt can answer, a line each, read by the grader (`check clean`,
  `reachable all`, counts, an area, a door, a creature in an area; the facts as built are
  listed once, in `tools/agentgrade.py`'s header).

  A fact a model would have to judge ("it looks like a crypt") is not a line. If a task
  cannot be graded by vtt's answers, it is not a task.
- Two kinds: **plan tasks** (the agent writes a plan, `vtt map.vtt --apply plan.txt`; no vtt
  open) and **live tasks** (a sandboxed vtt with `--agent` and a job asked; the GM's side is a
  `--script` that accepts whatever is proposed). Plan tasks first; live tasks need a GM's
  side driven from a script, which the tests do today through a fifo (CODEMAP, *Conventions*).

**The runner**, `tools/agenteval.sh TASK... | all`:

- Each task in a fresh scratch dir under `sandbox_enter`'s rules (its own `XDG_DATA_HOME`,
  `XDG_CONFIG_HOME`, `XDG_RUNTIME_DIR`, no keyring; CLAUDE.md, *Shell*), the skill installed
  into the dir's `.claude/skills/vtt/`.
- `vtt` on the agent's `PATH` is a wrapper that logs each call (its words, exit status,
  bytes out) and runs the real binary. The call log is where `requests` and `refusals` come
  from, without a change to vtt.
- The agent is a command (`EVAL_AGENT`, default `claude -p` with the task's prompt and
  `--output-format json`, which reports the run's cost; the field's name is checked when it
  is built). The Claude agent itself runs only in live tasks, as vtt starts it.
- `N` runs a task (default 3: a model is not deterministic, one run says little). Each run
  writes `runs/DATE-MODEL-TASK-K/`: the grader's verdict line a fact, the call log, the
  agent's final answer, tokens and dollars. The maps the agent made are kept for reading.
- **Nothing runs without the user's go:** every run calls a model and spends money. The
  runner prints what it will run (tasks × N × model) and the cost of the last such run, and
  asks; `--yes` is for the user.

**The grader**, `tools/agentgrade.py`: reads `expect`, the run's `--check --json`,
`--describe --json` and call log, and prints a line a fact, `pass` or `FAIL: what was
found`. Pure: given the same files, the same lines. It has its own test, a hand-written plan
as the agent (no model): a passing and a failing case for every kind of fact, run in `make
test` with the oracle.

### 2. Each job's outcome: `agent-jobs.tsv` (with step 8)

A line a job when it ends (accepted, scrapped, or the map closed under it), appended to
`$XDG_DATA_HOME/vtt/agent-jobs.tsv` beside `claude-usage.tsv`, mode 600:

| column | what |
|---|---|
| when, job | as `claude-usage.tsv`, so the two join on them |
| guide | a hash of what the agent was told: FNV-1a 64 over `AGENT_GUIDE` (and the Claude agent's first block, for that agent) |
| agent | `command`, `claude` or `other` (a name an agent chose is its content, not kept) |
| from | `gm`, `agent`, `apply` |
| verdict | `accepted`, `accepted-part`, `scrapped`, `closed` |
| rounds | times sent back with feedback |
| requests, refusals | counted under the job's name or with `job N` |
| by class | refusals by class (`usage`, `limit`, `conflict`, the six) |
| by verb | the three verbs refused most, `verb:count` |
| changes | the change set's size when the verdict came |
| seconds | from the ask to the verdict |

**Not kept:** what was asked, the GM's feedback, the agent's `say` lines, names, squares.
They are the map's content and the GM's secrets (CLAUDE.md, *The players must never see the
GM's things*; here, nobody else either). The file never leaves the machine.

Step 8 owns the session log and the agent's spawner (V2.md, *Build order*), so this part is
built with it: the counters are three ints and a small table on `Job`, counted where
`host_refused` is called today.

### 3. `tools/agentreport`

A local rollup of `agent-jobs.tsv` and `claude-usage.tsv`, by guide hash: jobs, the share
accepted, sent back, scrapped; refusals a job by class and the verbs refused most; dollars a
job. Text, a screen of it. It reads counts only, so what it prints can be shown to a reviewer.

### 4. The procedure (kept here, not in docs/AGENTS.md: *Part 1, as built* says why)

1. Run the evaluations on the current texts (the user's go): the baseline, kept in `runs/`.
   The tasks: `tools/agenteval.sh all` (it prints what it will run and asks). The triggers:
   skill-creator's `run_eval.py --eval-set tools/skill/evals/triggers.json --skill-path
   tools/skill --num-workers 1 --model sonnet`, from a scratch directory, its output kept as
   `tools/skill/evals/runs/DATE-triggers.json`.
2. A reviewer (a Fable subagent, as every review here) reads the failed runs and the rollup
   and drafts edits to the skill, the guide or the Claude agent's first block. The runs are of
   the evaluations' own maps, so nothing of a user's game reaches the reviewer.
3. The edits go into a plan, with the evidence for each, to the user for sign-off.
4. Built; the evaluations run again on the same model and N. **It ships only if no
   evaluation gets worse:** the total passes are not fewer, and no task drops by more than one
   run of N. Otherwise it goes back to 2 or is dropped, and the plan says which.
5. The runs of both sides are kept; the commit names them.

## Part 1, as built (2026-10-23)

- **`tools/agenteval.sh [--yes] [-n N] TASK... | all`**, as planned. Each run is a fresh
  `/tmp/vtt-eval.*` (short, for the channel's socket) holding the map's directory with the
  skill in `.claude/skills/vtt/`, and a `bin/vtt` wrapper first on the agent's `PATH`: it runs
  the real `./vtt` in a sandbox of its own (`tools/sandbox.sh`'s `sandbox_env`) and appends a
  line a call to `calls.tsv` (exit status, bytes out, the refusal's class from `vtt: error
  CLASS:`, the words). The agent's own environment is left as it is, so `claude` keeps its
  login. The run's directory under `tools/skill/evals/runs/` keeps `prompt.txt`, `expect`,
  `calls.tsv`, `answer` (the agent's stdout), `agent.err`, `agent` (its exit and seconds),
  `cost` (claude's `total_cost_usd`, when its JSON has one), `map.vtt` and `verdict`. Without
  `--yes` it asks at a terminal and refuses with none (exit 2).
- **The facts and their meaning are the grader's** (`tools/agentgrade.py`'s header is the one
  list). The plan's `requests <= 12` is **`calls`**: every vtt call counts, reads too, since
  the wrapper cannot tell a plan's requests apart without parsing them. Added beside the plan's:
  `floors` and `links` counts, `creatures WORD OP N`, `terrain AREA KIND` (a square of the
  area is that ground), `joined A B` (a way from A to B through openings and links, a one-way
  link only its way, where `door A B` is a door between them alone). Which rooms meet an area,
  and their squares and terrain inside it, is vtt's answer: `describe AREA json` through
  `--apply` (a read: the file's bytes are left), whose `in_region` the review's fix added. A
  wide corridor's open ends make two areas one room, so the room's whole terrain would not do;
  and before the fix `describe REGION` told every room whose *bounds* met the box (an L of
  ground round a walled room met it with no square inside), against the guide's words then,
  "only the rooms that meet it" (now "with a square in it"). A creature is in an area when a square of its footprint is. The one opening a party cannot pass is a
  window (`edge_passable_kind`); the grader names only that one, so it says nothing of the
  others' names. A line of `expect` that is no fact stops the grader (exit 2) before any is
  judged: with no map every fact fails, and a misspelt one would fail among them unseen.
- **Twelve tasks** (`tools/skill/evals/tasks/`): nine made from nothing (crypt, tavern, tower,
  row, secret, road, river, cellar, barracks) and three edits of a map given (addroom,
  fixreach, cull). Each has a **`reference.txt`**, a plan written by hand that meets every
  fact, not in the plan: `make test` runs each through the runner, so a task that cannot be met,
  or one vtt changed under, fails there and not in a paid run. The task's directory is told
  only to a stand-in agent (`EVAL_TASK_DIR`, set only when `EVAL_AGENT` is): the default
  agent is never pointed at the answer.
- **The test, `tests/agenteval.sh`** (in `make test`, after `protocol`; skipped with a line
  when there is no python3): four stand-in tasks (`tests/agenteval/tasks/`, an `agent.sh`
  each, no model) graded against their `want` -- every kind of fact passing (`made`), failing
  on the same map (`missed`), no map at all (`nothing`), a map given and kept (`kept`); the
  twelve references; a line that is no fact; a run with nobody to ask; the default agent's
  command line, through a stand-in `claude` first on `PATH` (its words, and that it is not told
  the task's directory); each exit status the wrapper passes back (the stand-ins check them);
  and the caller's XDG directories as a canary (nothing written there, and a character planted
  there never listed). Checked by mutation (by hand, 2026-10-23, and after the review): a window
  made passable, `joined` reduced to "any room", the area box test dropped and each of its
  edges moved, a refusal counted by exit status, `check clean` blind to warnings, a count's
  comparison dropped, a label matched by case, the closed kind renamed, a door to void taken
  for a room, one-way ignored, terrain read from the whole room, the wrapper's exit status or
  its sandbox dropped, the task's directory told to every agent, `--restricted` dropped -- each
  caught. One survives: `door A B`'s guard against one room meeting both areas, which only a
  door inside one room would show (HEALTH 95).
- **The procedure is here (§4), not in docs/AGENTS.md** as the plan said: AGENTS.md is the
  guide every agent is given and pays for in tokens, and a maintainers' procedure is nothing an
  agent should read.
- **`triggers.json`**: twenty queries, ten that should start the skill and ten near misses.
- **The default agent, probed on the user's subscription (2026-10-23, the user's go; three
  probes about $0.02 each, read from each run's `init` event and its permission denials):**
  - `--restricted`, as first written, **drops the project's skills**: the `init` event listed
    no `vtt` skill, so a run would never have seen what it tests. Replaced by
    `--setting-sources project` (the `vtt` skill listed; the model said no line of the user's
    CLAUDE.md was in its context) with **path-scoped allow rules** (`Read(./**)` and the like)
    under `dontAsk`: a Read of the repo's CLAUDE.md was denied and recorded as a denial.
  - `--strict-mcp-config` with no config: no MCP servers (the user's connectors were there
    otherwise).
  - Auto-memory made an empty `memory` folder under `~/.claude/projects` for each new working
    directory; `CLAUDE_CODE_DISABLE_AUTO_MEMORY=1` stops it (checked: none made).
  - The prompt is taken on stdin; the login is used (`apiKeySource: none`); the cost field is
    `total_cost_usd`.
  - Left as any Claude Code user has them: the built-in skills and plugins, and the user's own
    `context-bar` plugin. No flag drops those and keeps the project's skill (`--safe-mode`
    drops every skill). `--bare` is Claude Code's flag for none of this, but it reads only
    `ANTHROPIC_API_KEY`, never the login, so it is not used.
  - `vtt` still reads any path it is given and the wrapper names the repo (HEALTH 94).
    `--max-budget-usd 2` is a ceiling a run, not an estimate.
- **The first run (crypt, N = 1, 2026-10-23, `runs/2026-10-23-200205-sonnet-crypt-1`):** 13 s,
  $0.046, five turns, no map (1 of 12 facts). The agent ran `vtt --agent-guide`; the 36 KB
  guide came back cut short (its words; my guess, not checked: the Bash tool's output limit,
  about 30,000 characters); it tried `vtt --agent-guide > /tmp/guide.txt`, which `Bash(vtt *)`
  does not allow (a redirect), was refused, and stopped saying why. A GM at a terminal would be
  asked and could allow it; the cut would happen there too. **How the guide is read is the
  first thing the evidence says to change** -- through §4's procedure, the user's call.
- **A signal to the runner** stops the agent at once: it runs in the background and the exit
  trap kills its `timeout`, which passes the signal on (the review found a TERM waited for the
  whole run; checked after: TERM and INT end it in under a second, nothing left running).
- **Its cost in `make test`** is docs/TESTSPEED.md's (that page owns the test run's times); it
  runs after `protocol`, one run at a time.

## The guide's size (2026-10-23)

**What an agent can be sent, measured.** Two probes of Claude Code 2.1.306's Bash tool on the
user's subscription, read from the tool results in the stream-json output (what the model
was given, not what it said): a command's output of 30,000 bytes arrived whole; 32,000 came
back as a 2 KB preview and a `<persisted-output>` path under `~/.claude/projects/` (where the
run's path-scoped `Read(./**)` cannot reach, which is what the first crypt run met). The
limit lies between the two, so a part of the guide is held to **under 30,000 bytes**. The
probe that sent 10-32 KB six times cost $0.455: a large output stays in every later turn's
input, so what is read costs again each turn.

**The decision (the user, 2026-10-23):** first `vtt --agent-guide SECTION`, so an agent reads
only the parts it needs, each under the limit; then the guide slimmed, once that shows how
much can be sent. The guide by section (bytes, at 595931f's text): the opening 1,202, *Three ways in*
4,574, *The request language* 10,562, *From a description to a map* 2,813, *Working live with
the GM* 13,681, *Checking a map* 832, *Writing the file by hand* 2,602; 36,267 in all.

**The sections, built (2026-10-23):** `vtt --agent-guide toc` (the opening and each
section's size and command) and `vtt --agent-guide "NAME"` (src/guide.c); the whole guide is
still `vtt --agent-guide` alone. The skill's first step reads `toc` and the sections it marks
for the work: each section says who reads it on a `<!-- read for: plan, live -->` line under
its heading, which `toc` prints, so the skill names no section (about 19 KB for a plan).
Suite `agentguide` holds each part under `GUIDE_PART_MAX` and every section to a read-for line.

**The crypt task again, with the sections (2026-10-23, `runs/2026-10-23-202241-sonnet-crypt-1`):**
12 of 12 facts, 17 s, $0.086. The agent read `toc`, then the four sections it marks `plan`
(18,895 bytes), wrote one plan through `--apply` from stdin, then `--check`, `--dump-map` and
`--describe`; ten vtt calls, no refusal; nothing left under `~/.claude/projects`. Against the
first run (no map, $0.046), the guide's reading was the whole difference.

**The cuts measured for the slimming, not made yet:** the opening's notes for maintainers
(about 1 KB of 1,202); the size limits said three times (the `limit` row 375, the read and edit
bounds 1,184, the plan's 64,896), which belong to vtt (one sentence and a `help limits`);
the exit tables (`--wait`'s 671) to a line each; *Who you are* (2,172) to what one agent
needs, about 700; the live section's item 5 (`wait`, events, `changes`: 5,044) to the loop
and the events an agent acts on, the event table (1,130) to a `help events`. About 36 KB to
27-28 KB, an estimate from those counts. *The request language* stays: `help` gives each
request's form (2,768 bytes for all of them), the guide's tables the rules a map comes out
right by.

## The guide redesigned: the plan (2026-10-23; the user, the same day: "Do the redesign")

**What a run costs, and why the shape matters.** The passing crypt run (8 turns, $0.086):
14,896 tokens written to the cache (Claude Code's own part about 6-8k, by the first run's
8,413 with only a 2 KB preview of the guide), 99,116 read from it (the context again each
turn, at a tenth), 1,639 out. Bytes read in count most, then turns, then what the agent
writes. Five of the eight turns fetched the guide (`toc`, then four sections).

**What an agent needs, in three tiers.** (1) Always: what a map is, the two ways in, all or
nothing, the loop (plan, apply, check, describe), the refusal classes -- about 3 KB. (2) For
the work: the grammar, a worked example, the rules a map comes out right by (placement,
corridors, rooms over rooms) -- about 6 KB for a plan, plus the requests themselves. (3) Only
when it is needed: a request's exact edge rules, the limits, exit codes, events, several
agents' names. Today the third is read with the first two, and the request tables restate
`help`.

**The decisions.**
1. **One entry, topics:** `vtt --agent-guide [TOPIC]`. No topic: tier 1 and the list of
   topics, each with its size (replaces `toc`; the whole guide is `all`). `plan` and `live`:
   each a whole bundle in one call -- the concepts, the requests it uses with their rules,
   the prose for that work. `file`: the file by hand. `VERB` (`room`, `corridor`): that
   request alone, as `help VERB` gives it. `limits`, `events`: reference. Each under
   `GUIDE_PART_MAX`.
2. **Each request's rules are `REQUESTS[]`'s** (request.c), a `rules` field beside `use` and
   `doc`: `help VERB` prints all three, and the bundles print the rows they need from the table.
   docs/AGENTS.md loses its request tables and keeps the concepts and the prose; nothing is
   written twice. This reverses the table's comment ("Nothing longer belongs here: the page is
   AGENTS.md"). No generated copy is kept in docs/: a reader runs `vtt --agent-guide VERB`.
3. **A refusal of class `usage` ends with `(help VERB)`**, its line's verb, at the one place a
   line's refusal is written (request.c, after `run_line`). The guide's class table shrinks to
   a line each: the detail comes when the agent errs.
4. **The skill is its trigger and one line:** run `vtt --agent-guide`, which says what to read
   next; steps 2-5 (status, plans, live, refusals) move into the topics they belong to.
5. **The Claude API agent's prompt is the `live` bundle**, not the whole guide (apiagent.c
   sends `AGENT_GUIDE` today, about 9,000 tokens a cache write).
6. **The slimming measured before** goes in with it: the size limits said once (`limits`, from
   the code's constants), the events to `events`, the maintainers' notes out, *Who you are*
   to what one agent needs.
7. **Not now:** asking agents for short answers (the user has not decided; it is the GM's
   experience too).

**Shipping rule (§4):** the baseline is all twelve tasks once on the current guide
(2026-10-23); the redesign runs the same twelve once on the same model. It ships if no task
that passed fails (a task that changes is run twice more on both, to tell noise from a
loss), and the report gives each task's tokens, turns and dollars both ways.

**Performance considerations.** The bundles are composed when asked: a few hundred lines
copied from two tables in memory, once a call (speed of light: the bytes written, 10-20 KB;
expected tens of microseconds, nothing on a key or frame). An agent's cost, estimated from the
four-bytes-a-token rule of thumb (the two crypt runs fit it: 6,483 tokens more for about 24 KB
more read), not measured: a plan task's guide from about 19 KB in 5 calls to about 12 KB in 1,
about 5,000 tokens to about 3,000 and two to four turns fewer. Measured by the evaluations.

**Tests.** Suite `agentguide`: each topic under `GUIDE_PART_MAX`; `--agent-guide VERB` is `help
VERB`; every verb in `plan` or `live`; the topic list in the core names every topic; the skill
names only `--agent-guide`. A `usage` refusal ends with its `help`. The API agent's prompt
holds the live bundle (suite `apiagent`). The oracle records the refusals' new ends.

**After its review (2026-10-23, HEALTH B23):** the core 2,763 bytes, `plan` 11,121, `live` 15,795
(it now holds the refusal classes, in a section of their own the core shares), `all` 24,117;
docs/AGENTS.md 16,144. The figures below are 26ba513's.

**As built (2026-10-23).** As planned, with these facts:
- **Sizes** (bytes, `vtt --agent-guide ... | wc -c`): the core 2,859 (with the topic list);
  `plan` 11,648; `live` 15,935 (with *From a description to a map*, which a live agent
  building a map needs too); `events` 1,795; `limits` 966; `file` 2,602; `all` 24,740
  -- the whole now under the limit (it was 36,524). A plan task reads the core and `plan`,
  about 14.5 KB in two calls, against 21 KB in five with the sections.
- docs/AGENTS.md is 16,860 bytes of prose; `REQUESTS[]` carries each request's rules; suite
  `ctltable` fails if AGENTS.md's language section grows a request row again.
- The core says to write a plan with the file tool and run each `vtt` command alone: the
  baseline's agents wrote `cat > plan.txt <<EOF ... && vtt ...`, which `Bash(vtt *)` refuses
  (10 of 12 tasks, below).
- `toc` is still taken, as the core. `--agent-guide` alone is the core, no longer the whole.
- Checked by mutation: the topic filter of `request_reference`, the refusal's `(help VERB)`,
  the `<!-- requests -->` expansion -- each caught. One equivalent survives: `for_topic`'s
  test that a tag word ends where the topic does; no topic is a prefix of another's tag.

**The baseline (2026-10-23, all twelve once on the sectioned guide, `runs/2026-10-23-21*`):**
2 of 12 met every fact (addroom, cull), $0.95 in all ($0.064-0.148 a task, 6-14 turns). In
the other ten the agent read the guide, wrote a plan that looks right, then ran it as one
shell command (`cat > /tmp/plan.txt <<E ... E && vtt ...`): refused by the run's
`Bash(vtt *)`, it stopped and said so. So the baseline measures the permissions an agent meets
as much as the guide; the redesign's core answers it in the text an agent reads first.

**The redesign's run (2026-10-23, the same twelve once, `runs/2026-10-23-2118*`..`2121*`):
12 of 12 met every fact** (the baseline 2), $0.776 in all against $0.953 (-19%): cache writes
128,862 tokens against 164,890 (-22%), output 13,940 against 19,938 (-30%), cache reads
1,209,492 against 937,659 (more turns: the work is now done, where the baseline's agents mostly
stopped after reading the guide). Per task $0.060-0.077, 9-14 turns. Crypt, the one task that
passed with the sections too: $0.086 in 8 turns then, $0.060 in 9 now. By §4's rule it ships:
no task that passed fails (addroom and cull, the baseline's two, pass again). N = 1 each side:
a single run says little of a task's spread; N = 3 is the plan's for a change to the texts.

**After the review's fixes (2026-10-23, `runs/2026-10-23-2137*`..`2140*`): 12 of 12 again**,
$0.739 in all (cache writes 123,946, output 13,442), $0.054-0.070 a task.

## Order

The user's order (ROADMAP 27, V2.md *Next*): step 7 is built next. Then part 1's runner and
grader, with the grader's test and the procedure (no model is called to build them); its
first run with the user's go, folded into ROADMAP 26's live check. Part 2 with step 8, then
part 3. A Fable review after each part.

## Performance considerations

| path | speed of light | expected | the gap |
|---|---|---|---|
| a request under a job | nothing new | an int incremented, a refusal's class and verb counted (a table of 8) | none worth a row |
| a job's end | one line appended, one `write` (about 150 bytes) | `open`, `write`, `close` of a line, once a job | the `open` and `close`: a job ends a few times a session |
| the guide's hash | 36,156 bytes read once | FNV-1a once a process, at the first job's end (about 36 µs at 1 ns a byte, a guess from FNV's one multiply a byte; measured when built) | none: once a process |
| an evaluation run (the plan's figures, 2026-10-21; measured since: *The guide redesigned*, *As built*) | the model's tokens, of which the guide is about 9,000 (36,156 bytes; 4 bytes a token is a rule of thumb, not measured) | tasks × N × a task's tokens | the money is the cost: unknown until the first run measures one task; the runner reports it before asking for the next |
| the rollup | the two files' bytes | one pass | none |

Nothing of this is on a key or a frame: no `PROF_ZONE`, no perf scenario. The evaluation
run's cost goes in this doc and PERFORMANCE.md once measured.

## Questions for the user (answered 2026-10-21: each as recommended)

1. **What the job record keeps.** Counts only, as above (recommended): the GM's feedback is
   the richest signal, but it is the GM's words about their game. Or also the feedback lines,
   on this machine only, shown by `tools/agentreport --feedback` to you and never handed to a
   reviewer without your go.
2. **Which agent the task evaluations run.** `claude -p` with the skill (recommended: it
   tests the skill and the guide, the path an outside agent takes), with the Claude agent
   added in the live tasks; or the Claude agent only.
3. **The model and N for a run.** Sonnet, N = 3 (recommended: the cost is measured on one
   task first and put to you before a full run); or the model the GM's Claude agent is set to.
