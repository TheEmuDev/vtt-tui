# vtt -- the baseline for Claude

A rules-agnostic virtual tabletop for the terminal: C11, nothing beyond libc, POSIX and
`-lm`. Tested with Daggerheart. The user is the sole maintainer. This file is loaded into
every session and is what survives a context reset, so keep it true: rules and
prohibitions only, at most 150 lines. Reference lives where it is read on demand.

## Read on demand

| when | read |
|---|---|
| before touching a source file | its row in docs/CODEMAP.md: `grep -n 'fog\.c' docs/CODEMAP.md` -- what the file owns and the invariants kept there. Never the whole file (84 KB) |
| how the parts fit; which subsystems a change crosses, what data goes where | docs/ARCHITECTURE.md, by section |
| a command not listed below; a convention's reason; the watch list; **which files restate a fact you change** | docs/CODEMAP.md, by heading (*Facts kept in more than one place*; every drift found gets a row in *Drift found*, the fix's commit) |
| writing or running tests | tests/CLAUDE.md (loads itself under tests/) |
| a Daggerheart rule's wording or page | the SRD 2.0 PDF: its link is in docs/CODEMAP.md's `ruler.c` row. Summarize with the page; never copy its text in |
| binding a key | docs/KEYS.md, the nine rules (the ninth: `off` switches off, `remove` destroys) |
| a plan's performance section | docs/PERFORMANCE.md, *Speed of light* |
| reviewing; why 2.0 does something | docs/HEALTH.md: *The questions*, *Left open by reviews*; docs/reviews/, the reviews docs/V2.md rests on (agentic, performance, testing, memory), by heading |
| the channel, agents, proposals | docs/CONTROL.md, docs/AGENTS.md, docs/CONFLICTS.md |
| a big doc (PERFORMANCE.md, README.md, CONFLICTS.md, FOG.md) | by section: `grep -n '^#' FILE`, then the lines needed |
| which doc is current, which is a design record | docs/README.md, the index; a design record's Status line says where the truth is now |
| resuming work, or after a compaction | docs/V2.md's *Next, in the order the user set*, then memory, then the plan's *Progress* (CONFLICTS.md's log is *Progress, step by step*) |

## Standing rules (the user's, not defaults)

- **Plan before refactors.** Anything that reshapes a shared struct, changes a shared signature or
  touches several subsystems gets a written plan -- the engineering decisions, their tradeoffs,
  the instrumentation -- and waits for explicit sign-off. Small additive work goes straight to code.
- **No software waste** (2026-10-07). Every plan has a **Performance considerations**
  section giving, per new or changed path, the **speed-of-light estimate** (the least the
  work could cost: bytes it must touch, syscalls and flushes it cannot avoid; reference
  figures in PERFORMANCE.md, naming the machine), the design's expected cost beside it, and
  why any gap is accepted. Once built, measure against the estimate and record both in the
  design doc and PERFORMANCE.md. An unexplained gap is waste: remove it or write it down as
  a finding. Cost follows the work that must be done (what changed, what is on screen),
  never the size of what could have been touched.
- **Performance discipline.** Every new drawing or keystroke path gets a `PROF_ZONE`, a
  scenario in `tools/perf.sh` and a regenerated PERFORMANCE.md. Bytes written matter more
  than frame time. Cost follows the window, not the map: cull with `grid_visible_tiles`.
- **The phone page stays inside one round trip** (2026-10-04): under 12 KB as sent
  (docs/REMOTE.md). Below the limit spend nothing on shaving bytes; spend it on the page's
  JavaScript being near optimal at run time. Never be complacent: performance slips slowly.
  **Respect the phone's resources** (2026-10-22): CPU, memory, battery; the players get the best experience we can give.
- **Push when convenient** (2026-10-20, the user: "We can git push whenever is convenient"; before: only when told). Push a commit only after `make test` passed on it. Commit freely.
- **Model roles** (2026-09-26): **Opus 5.5** plans (the plan goes to the user for sign-off)
  and implements. A **Fable 5.1** subagent (Agent `model: "fable"`) reviews every finished
  change before it is reported done; verify its findings and fix what holds up. The user
  will name different models when that changes. Brief every subagent: stop only PIDs it started,
  never kill by name. A review's brief: sha, range, what to look at, and `tools/review-brief.md`
  (its *Docs drift* section is required: every review checks the docs did not drift, 2026-10-23).
- **Every review includes the health check's questions** (2026-09-29) beside correctness, for the
  change and what it touches: duplication, organization, performance, test gaps, the docs (HEALTH.md).
- **Never dismiss a finding without validating it** (2026-10-09): reproduce or disprove it with a check
  actually run, and say which; memory is not a check. Ask the reviewer (SendMessage) when unclear.
- **Every finding not fixed is written down** in HEALTH.md, *Left open by reviews* (why, what would
  close it; a disproved one under *Disproved*, with the check); the report names the rows added.
- **A change is not done** (2026-10-23: any change) until the kept-current docs (docs/README.md's list)
  agree with the code, in its commit: README.md (keys, features, *Rulesets*, *File format*; plain, for a user), PERFORMANCE.md (tables + a paragraph per finding), `src/keys.c` (the `?` page),
  docs/CODEMAP.md's rows, docs/ARCHITECTURE.md (a subsystem, or a path data takes between them),
  the design doc and, for what agents see, docs/AGENTS.md and the agent skill. Suite `docs` fails
  on drift it can see (a file with no row; a name, macro, file or path gone); the rest is every review's. Its
  ROADMAP.md item moves to *Built*; a new design doc has a Status line and a docs/README.md row.
  HEALTH.md records each check, a fixed item by commit. Ideas: docs/IDEAS.md. **A decision the user
  makes goes into its doc the same turn** (plan, ROADMAP.md, V2.md's *Next*); memory only points at it.

## Commands

```
make                 release build (-O2), profiler compiled in
make test            ASan+UBSan build, unit + golden-frame tests, then tools/oracle.sh check over ./vtt and build/asan/vtt (a leak fails it; `record` after a meant change of output)
make test-build; build/run-tests NAME...   only the suites named (per object, a job a core: only what changed is rebuilt); `make test` before a commit
make perf            one run; publish the per-row MEDIAN of three quiet runs (tools/median.py a b c)
make fuzz | fuzz-ctl | fuzz-json    libFuzzer (clang): map loader, channel requests, JSON; FUZZ_SECONDS=600
tools/embed.sh       after editing web/index.html or docs/AGENTS.md (-> src/webpage.c, src/agentguide.c); tools/blit_wasm.py after the blitter
tools/machine.sh     the `Machine:` line every published table carries
./vtt map.vtt --check [--json] | --describe [--json] | --dump-map [--region B2:K12]
./vtt map.vtt --script keys --dump-frame --size 100x30      one frame as text
./vtt map.vtt --bench keys --bench-loops 400 --trace t.json headless timing + Chrome trace
./vtt new.vtt --apply plan.txt --new 40x30                  channel requests headless, saved
```

- Perf wants the machine to itself (editing a file mid-run lifted rows by a fifth). Two
  machines, laptop i7-4510U and desktop i7-8700K: compare only within one. When a row moves,
  A/B the previous commit's binary **on the repo's disk** (`/tmp` is tmpfs, flushes free):
  `git worktree add .vs1x.prev HEAD~1 && make -C .vs1x.prev`, removed after.
  Each 2.0 step's timing ends with `tools/vs1x.sh` -> PERFORMANCE.md, *2.0 against 1.x*.
  A gap under ~10% on a tight loop can be code placement alone: rebuild **both** with
  `make -B CFLAGS="-falign-functions=64 -falign-loops=32"` first (PERFORMANCE.md, *The checkpoint*).
- Bench scripts replay whole: loop-neutral pairs (`llllhhhh`, `8a8A`) or absolute keys (`3b`, `2R`).
  `tools/pagebench.sh`, `sight.sh`, `saves.sh`, `proposals.sh`, other `--bench-*` flags: CODEMAP.

## Prohibitions and invariants

Architecture
- **No dependencies.** No library beyond libc/POSIX/`-lm`; no build tool beyond `make` and tools/.
- **Rules-agnostic core.** Game behavior lives only behind the `Ruleset` table in `ruler.c`
  (bands, `action_roll`, `spotlight`, `countdown`, `damage`), documented under the README's
  *Rulesets* with a subsection per game. Nothing else may know a game.
- **One owner per question.** Before writing a check, find its function in CODEMAP: floors go through
  `floor.c`, sight through `sight_walk`, file names `store_name_ok`, map draws `grid_clip_push`, fog writes `map_fog_set`.
  Two copies are allowed; a third is extracted first (the watch list says which are at two).
  **Nothing built to drift** (the user, 2026-10-23): a fact -- a list, a count, a time, a policy -- has one owner, and every other place derives it or points at it, never restates it.

The players must never see the GM's things
- Every frame goes through `app_frame`. Anything GM-only (a modal, prompt, overlay, tint)
  checks `a->view` **and is added to `app_view_differs`**, or it reaches the phones.
- "May the players see this creature" is `fog_token_unseen(m, t, fog_any)`, never
  `fog_token_hidden` alone.
- A message about a creature's numbers, the damage rule, a phone, a whisper, a job, a
  scene, an area or a secret link is `app_note_gm`/`app_set_status_gm`, refusals included.
  Only usage hints are plain.

The map, the log, the file
- Edits to the live map go through the undo log; `undo_balanced` must hold between keys.
  **A writer of the map's squares round the log must call `cp_note`** (today: `fog_delete`).
  LIT/RIM fog bits are never recorded and never saved.
- A request's edits are a proposal: they come in through `world_job_set_proposal`; anything
  an edit does to the live App is guarded by `!ed->proposal`. Only the GM decides a disk
  job. **A new thing an agent should hear of calls `world_event`.**
- The loader forgives damage silently: **every new silent-drop site calls `diag()`** with a
  stable code. The writer writes one version, the newest (`FORMAT_VERSION`).
- The map the app will hold opens with `mapio_load_base`; a read-only look is `mapio_load`.
  A save of the open map is `app_save_map`. A map is put down only by `app_close_map`;
  each place a map leaves the app's hands sends `map closed` and calls `world_jobs_clear`;
  anything that might discard work asks through `app_leave_map_for`.
- `client_flush` returns -1 when it closed the client (the next one shifts into the slot):
  every caller checks.

Code
- `Cell` padding stays zero (the renderer memcmps rows). Tokens: `token_equal`, never `memcmp`.
- Print through the one helper: `dist_fmt`, `map_coord_name`, `token_name`. Overlap is
  `token_meets`/`tokens_at`/`tokens_overlapping`; a label in a list `tokens_find_label`; the
  same creature in two lists `token_same_key`. Never written out again.
- `app_note` for what *happened* (logged); `app_set_status` for hints and errors.
  `range_clear` resets the overlay; `range_off` keeps the shape. A prompt whose answer
  lands in a fixed field sets `a->prompt.max`.
- Keys: bar and `?` page both come from `src/keys.c`; six hints on the bar; no duplicate key strings per map.
- American spelling everywhere (color, center, gray, canceled, labeled). Comments say
  *why*; rationale lives in the header or the README, not at every call site.

## Shell

- Never `pkill -f <pattern>` from a command whose own line contains the pattern (it kills the shell, exit 144);
  never kill `vtt` by name (the user may have one open): record PIDs. `grep` is aliased oddly: `awk`, or `grep -n` in a subshell.
- Scratch: the session scratchpad, never the repo; a dir a task, deleted when done, swept before a
  commit. First check docs/SCRATCH.md (seen twice: a tool in tools/), then add its row. Key scripts: `ESC`+letter is Alt.
- A test or script that runs `./vtt` or opens the channel sits in a sandbox (`sandbox_enter`; `tools/sandbox.sh` for
  the shell, held to it by suite `sandbox`: XDG dirs, no keyring, editor `true`), or it reaches the user's files, key and vtts.
  Driving a live vtt from a script (a fifo and `script`): CODEMAP, *Conventions*.
