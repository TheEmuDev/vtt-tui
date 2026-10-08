# One map, many editors: the GM decides

*Plan, 2026-10-07, third draft with Fable's review folded in. Signed off 2026-10-07: every
recommendation below is the decision.*
- *The first draft refused `--apply` on an open map.*
- *The second sent it straight into the open map.*
- *This one follows the user's direction: the GM whose vtt owns the map has the highest
  priority. Nobody else's change lands until the GM has seen it and said yes, unless the
  GM asked for it to land at once. `u` works throughout.*

*This reverses one 2026-09-26 decision in docs/CONTROL.md: "free editing, undo as the
safety net; no drafts to approve". Agents now propose and the GM approves.*

## Progress

*Updated 2026-10-07, end of the day.*

- **Step 1: built, reviewed by Fable, and fixed (2026-10-08).** The review's fixes:
  - an accept could leave two creatures holding the turn (a plan's turn now lands only
    where nobody else has it);
  - two creatures of one label paired out of order after a removal;
  - a new link previewed with the agent's number, not the one the accept gives;
  - the benchmark timed setup in the diff and only the first preview frame;
  - a test that never added where it meant to, and the missing tests (far-edge
    boundaries, a box's east and south sides, unlabeled moves, duplicate labels, the turn,
    the small parts' conflicts);
  - "is this label taken" and "is this the same creature" extracted to token.c/h
    (`tokens_find_label`, `token_same_key`; scene.c uses the second).

  What step 1 holds:
  - `map_copy`/`map_copy_into` (map.c) and `xstrdup` (util.c);
  - `changeset.c/h` (the change set: diff, conflicts, accept whole or by box, the summary,
    bounds, the preview swap);
  - `tests/test_changeset.c` (the suite `changeset`, including a 60-round differential
    check);
  - `tools/proposals.c`/`.sh` (the timing table);
  - PERFORMANCE.md's *Proposals* section, set against the speed of light.
- **One file, not two.** `mapdiff.c` and `changeset.c` became one file, `changeset.c`.
  The diff, the apply and the preview share one structure.
- **Next, step 2:** the copy-on-write checkpoint (16×16 blocks, the hook in the five
  writers, `fog_delete` through `map_fog_set`, reset on resize, change counters for
  scenes, cards, clocks and rolls), with A/B rows for a 512×512 fill and `undo.step`
  against the previous commit's binary.
  - **The counters also close a hole in `cs_check`.** Its gate on `Map.gen` misses a card
    the GM edits, because `card_set` touches nothing. The counters fix it.
- **Then steps 3-9 in the order below.** Nothing in the app calls the change set yet:
  `:ask`, `:review` and the requests come in steps 3-4.
- **docs/AGENTS.md still tells agents never to `--apply` an open map.** That stays true
  until step 6.

## How it feels to the GM

1. **The GM asks.** The GM is in build mode. They draw a `v` box round the cellar and type
   `:ask make this a flooded crypt with two ghouls`. The box stays on the map, tinted in
   the **working** color with a small `#3` label: an agent has a job there. The GM carries
   on editing anywhere, inside the box too, with `u` as always.
2. **The agent picks the job up and works.** It reads the map as it likes. Before
   changing anything it declares where it will work, and those squares tint in the
   working color too. When there was no box, this is how the GM still sees where the
   work will land. When the proposal comes, the squares it really touches replace the
   declared ones.
3. **The agent finishes.** The tint turns to the **ready** color, and the status line says
   `#3 ready: flooded crypt, 2 ghouls, 3 doors - :review 3`. The agent can add a line of
   its own, shown under it.
4. **The GM reviews.** `:review 3`, or `:review` for the oldest ready one, shows the map
   **with the change drawn in place**. Squares where the change would overwrite something
   that changed since the agent saw it (the GM's own edits since, usually) are drawn in
   the **conflict** color, so accepting is never a surprise. Then:
   - `enter`: accept. The change lands as one undo step, and `u` takes it back like any
     other.
   - `d`: reject and scrap. The agent is told.
   - `c`: reject with feedback. A prompt takes a line, it goes to the agent, and the job
     goes back to working in the same place.
   - `v`, then `enter`: **accept only the part inside the box** (counter-suggestion A
     below). The rest goes back to the agent as feedback, or is scrapped.
   - `esc`: leave the review. The proposal stays ready for later.
   - `n` / `N`: the next or the previous ready proposal.
   - `u`: undo, as anywhere. The preview never changes the map, so `u` undoes the GM's own
     last step, and the preview and its conflicts redraw on top of the result.
5. **Accepting at once.** `:ask! TEXT` makes a job whose result lands as soon as it
   arrives, still as one undo step and still announced. `:agent accept auto` does the same
   for everything that comes in, and `:agent accept review` goes back. A result that
   arrives while landing isn't possible (play mode, a wall stroke, a prompt open: today's
   `app_ctl_busy` reasons) waits as ready, and lands when the GM is back. It is never
   refused.
6. **Asking from anywhere.** `:ask` works in play mode too, without a box; a GM in the
   middle of a fight can ask now and review later. Reviewing is build mode's.

**Changes the GM didn't ask for come the same way:**
- an agent's own idea (`propose` with no job);
- an `--apply` to the open map;
- the file changed on disk by another vtt or a text editor.

Each shows its squares in the ready color, gets a line on the status line, and waits for
`:review`.

## Counter-suggestions folded in

The user invited these and kept all three (2026-10-07).

- **A. Accept part of a proposal.** In review, a `v` box limits `enter` to the elements
  inside it, and the rest is handled with `d` or `c`. Agents often get most of a job
  right. Without this, the GM's only choices are all or nothing, or a round of feedback.
  It costs a filter on the change set by box.
- **B. The preview is drawn, never applied.** The change set is swapped into the map's
  arrays for the length of one draw and swapped back, the way `stamp_show`/`stamp_unshow`
  preview a stamp: no `map_touch`, no `Map.gen`, no undo record. This gives three
  things:
  - `u` works during review (the user's requirement);
  - the players' frame cannot show a preview, because the map never holds one, so the
    phones need no special handling;
  - leaving a review costs nothing.

  The alternative, applying inside an open undo batch, would have blocked `u` and every
  edit while the review was open.
- **C. A job keeps its history.** Feedback doesn't open a new job: `#3` keeps the GM's text,
  each proposal's summary, and each line of feedback. `:jobs` shows them, and the agent
  gets the whole thread with the job, so a second attempt knows what the first one got
  wrong.

## Pieces

### A change set: what a proposal is

Every proposal is a **change set**: each element it changes, with its value **before**
(as the agent saw it) and **after**. The elements:
- squares (tile, both boundaries, fog painting);
- creatures, keyed by their label **before** the change (a rename is keyed by the old
  name, with the new one in its after);
- areas and floors, by name;
- links, by number;
- notes, by square;
- the fight's round and spotlight (a `scene` line restores them);
- named rolls, by name (`token add ... from` adds them);
- cards, by name, with their text before and after (`token add ... from` brings a
  character's card; today that goes around the undo log);
- fog painting carries the patch's **name** as well as its number, so a patch the GM
  deleted meanwhile (its number tombstoned, `dead`) is a conflict, not paint into
  nothing.

The change set is the one representation everything here uses:

| use | how |
|---|---|
| highlight | the squares it touches, kept in 16×16 blocks so a frame draws only the visible ones |
| summary | `2 ghouls added, walls in B2:F6`, for the status line and the agent |
| conflicts | elements whose current value is no longer the change set's **before**: the squares an accept would overwrite. Two special cases. A creature the change adds is a conflict if one with its label has appeared meanwhile, and accept never makes a duplicate label. A **new** link takes the lowest free number at accept, not the number it was drawn with ("link 3, now 5" in the summary), so it never overwrites a link the GM made |
| preview | the **after** values swapped in for one draw of the grid and swapped back (B): the map is never changed. The status line describes the real map, not the preview |
| accept | applied as values in one undo batch, so what the GM previewed is what lands, inside the highlight. `u` takes it back |
| partial accept | the same, for the elements **wholly** inside a box (A): a 2-3 square creature, both ends of a link, an area's whole box. The summary says what stayed out |

**Where a change set comes from:**
- **An agent's plan** (`propose`, `job N propose`, `--apply` to an open map) runs on a
  **scratch copy** of the map, never the live one. The request runs exactly as requests
  run today (all or nothing, with errors to the agent), with the App's map and undo log
  swapped for the copy and a scratch log. Then the change set is the difference between
  the live map and the copy, and the copy and the scratch log are freed.
  - **Why not run it live and roll it back:** that was this draft's first version, and
    the review showed it touches the live map in four ways.
    - Opening a batch drops the GM's redo tail for good (`batch_start`, undo.c:102-106).
    - Every edit calls `map_touch`: the map is marked unsaved (the title shows `[+]`,
      and `:q` asks) and `Map.gen` moves, which triggers an autosave, a full fog rebuild,
      and the checkpoint's diff, all for a change that didn't happen.
    - `Undo.stamp` moves, so the agent's `undo` of its last accepted change would refuse
      after any later proposal.
    - Creature ops carry indices that shift within the batch, so labels can't be
      recovered afterwards.
  - **A new `map_copy`** (map.c): tiles, both boundary arrays, fog, `TokenList`, areas,
    links, notes, rolls, clocks, round and spotlight, cards (their heap text), and scenes
    (their own lists). Sight is left empty.
  - **The difference costs what changed, not the map.** The scratch log names every
    square the plan wrote, so only those squares are compared. The small parts are
    compared whole, creatures by label.
  - **In proposal mode, nothing reaches the App:** no `finish_edits`, no
    `app_fog_sync`, no status line or ring, and none of the scene line's App side effects
    (`last_acting`, `play_focus`, `range_clear`, app_ctl.c:894-898).
- **One producer.** Disk proposals use `changeset.c` too, so there is one way to make a
  change set, not two.
- **The file on disk changing.** A change set is the difference between the file as vtt
  last read or wrote it (the **base**, kept as its bytes) and the file now: `changeset.c`
  compares two `Map`s.

### Jobs (`:ask`)

`App.jobs[16]`. Each job has:
- a number;
- the GM's text;
- its region: the `v` box, or none;
- an `at once` flag;
- a state: **asked**, then **working** (an agent took it), then **ready** (a change set
  came in), then accepted, scrapped, or back to working with feedback;
- the thread (C);
- the change set, once it has one.

`:jobs` lists them, and `:ask N remove` withdraws one (KEYS.md rule 9). A scrapped
proposal stays in `:jobs` until the map closes, so `:review N` can bring it back if
scrapping was a mistake.

How the GM's job reaches an agent is decision 1. Both ways end at the same requests:

| request | does |
|---|---|
| `jobs [json]` | the open jobs: number, text, region, state, thread |
| `job N take` | working. The job's region tints, or the GM sees `#N taken` when it has none |
| `job N area REGION` | declare where it will work, before proposing; tints those squares |
| `job N say "text"` | a line for the GM's status line, and the log |
| `job N propose` + plan lines | run as described above. The answer is the change set's summary, or the line that failed |
| `job N drop` | give it back, as asked |
| `job N dump [REGION]`, `job N check`, `job N describe` | the map with the job's proposal in place (the same swap as the preview), so the agent can read back its own work before the GM sees it. `jobs` also counts each proposal's conflicts |
| `propose` + plan lines | an agent's own idea, as a job with no GM text |
| `undo` | take back the agent's last **accepted** change, only while nothing has happened since: today's rule, which now applies to accepted proposals |
| `wait [SEQ]` | block until there is an event after SEQ (below), then answer every one |

### Telling the agent: events

vtt keeps a numbered ring of events for agents:
- **job asked** (a new `:ask`);
- **job accepted**, whole or in part (with what was kept);
- **job scrapped**;
- **feedback** (the GM's line);
- **the map changed**;
- **the map closed**, or the GM traveled to another map. Jobs belong to their map:
  they close with it, and the agent is told rather than left to time out.

`wait` is a long poll: the connection is held until something happens, or until a
timeout the agent names (at most 10 minutes). An agent's loop is `vtt --ctl 'wait 41'`,
act on what comes back, then wait again.

What the socket code needs for it (ctl.c, main.c):
- **A new `CTL_WAITING` state.** It is skipped by `ctl_due`, which would otherwise spin
  the loop: a held request in `CTL_READY` makes the poll timeout 0. It is also skipped by
  the 10 s deadline sweep and by the connection cap. It has its own deadline, the
  agent's, and `listen`'s backlog grows with the cap (4 requests + 4 waiters).
- **A waiter that hangs up is noticed** (`POLLHUP`) and its slot freed.
- **`app_ctl_exec` gains a third outcome, "held"**, beside the answer and out of memory.
  The fuzz harness, which has no loop, gets an immediate answer from `wait`.
- **Waiters are answered right after the key that made the event,** not at the next wake.
  Otherwise an accept would reach the agent only at the GM's next keystroke, since an
  idle poll blocks for ever.
- **The client waits as long as the request says.** `exchange` gives up at 15 s today
  (`CTL_TIMEOUT_MS` + 5 s), so `vtt --ctl wait` and `--apply --wait` take their timeout
  from the request.

**The map changed**, so an agent knows what moved under it. When an agent is waiting or a
job is open, the map is checkpointed copy-on-write. Once the map has been quiet for 1.5 s
(the autosave's rule), its change set against the checkpoint becomes one event:
`the GM changed: walls in B2:F6, "Ghoul" moved C3 -> D5`. The checkpoint then moves on.
With no agent waiting and no job open, there is no checkpoint and nothing runs.

The checkpoint is the second draft's design:
- 16×16 blocks, with a block's old contents saved on its first write;
- a hook in `map_set_tile`/`vedge`/`hedge`, the fill and `map_fog_set`, with
  `fog_delete` changed to go through them;
- fog compared by painting only;
- reset on `map_resize`;
- change counters for scenes, cards, clocks and rolls.

### Other writers of the file

Another vtt, `git checkout`, or a text editor:
- `Map.disk` records the file's identity (`dev`, `ino`, `size`, `mtim`) at load and save.
  The file is checked with one `stat` on a key (at most once a second), and before a save
  or a trip. There is no timer.
- **A change becomes a proposal** from `the file on disk`. Once keys are quiet, vtt
  reads the file, compares it with the base, and shows the result like any proposal:
  highlighted, summarized, waiting for `:review`, with partial accept too. Accepting
  applies it to the open map as one undo step, and the base becomes the file now.
- **`:w` refuses while a proposal from the file is unreviewed**, because it would write over
  what the other writer did without the GM having looked. `:w!` saves anyway, and
  scrapping the proposal answers it too (the base becomes the file now, so the GM's `:w`
  is a deliberate overwrite).
- **Bare `:e` reloads**, asking first when there are unsaved changes.
- **A trip** through a link to another map is refused while its file has an unreviewed
  change.
- **Opening a map another vtt has open** asks first. Every interactive vtt listens on its
  socket, and `status` names its file.

### `--apply` to an open map

`--apply` asks each running vtt which file it has open. If one holds the map, the plan
goes there as a proposal from `--apply plan.txt` and does not touch the file. The answer
is the proposal's number and summary. `--apply --wait SECONDS` waits for the GM's
verdict: 0 when accepted (wholly or in part, said on stdout), 4 when scrapped, 5 with the
feedback on stdout. With no vtt holding the map, `--apply` works as today.

## What the screen shows

- **Three new theme colors:** working, ready, conflict. They are drawn as a tint under
  the map's own marks, the way a range overlay is, plus a `#N` label at the region's top
  left corner. They are all GM-only: `app_view_differs` counts open jobs and proposals,
  so none of it reaches the phones.
- **The review is a mode of build mode** with its own key bar (six hints: accept, scrap,
  feedback, box, next, back). It is the GM's alone, like the rest of build mode. Build
  mode is never sent to the phones (`app_remote_live` is play mode only), which is a
  stronger reason than B's that a preview never reaches them. The tints are drawn in play
  mode too, and that is why `app_view_differs` counts open jobs.
- **The review's state.** The editor has one `mode`, and a review can hold a `v` box. The
  review is an `ED_REVIEW` mode that carries its own box, so the two don't fight over the
  one field. `esc` drops the box first, then leaves the review (KEYS.md rule 7: one layer
  at a time).
- **The preview swap** keeps its buffers across frames, where the stamp preview mallocs
  each one. It swaps the whole creature list (128 KB at the 500 cap, about 4 µs) and the
  squares meeting the window. It writes straight into the arrays, bypassing the
  checkpoint hook, so review frames never dirty the checkpoint.
- **The status line** announces each event once. `:jobs` is the list.

## Decisions

*Signed off 2026-10-07: the user took the recommendation on each.*

1. **How `:ask` reaches an agent.**
   - (a) **The agent waits for work.** The GM starts an agent and tells it to watch
     vtt; it loops on `wait`. Nothing runs that the GM didn't start.
   - (b) **vtt starts one.** `:agent command CMD` names a command (for instance
     `claude -p`) that vtt runs in the background for each `:ask`, with the job and its
     thread on stdin. It reports back through the same requests. The process runs
     detached from the terminal, with its output going to the session log.
   - Recommended: **(a) first and (b) after it**, both in this plan. (b) is a few dozen
     lines on top of (a), and it makes `:ask` work with no agent already running.
2. **What accept applies.**
   - Recommended: **the change set's values**, so what the GM previewed is exactly what
     lands, overwriting the conflict squares, which the GM was shown.
   - The other choice is to re-run the agent's plan text against the map as it is now. It
     can follow things that moved (`east of Crypt`), but the result can differ from the
     preview.
3. **The review keys.** `enter` accept, `d` scrap, `c` feedback, `v` box, `esc` back out,
   `n`/`N` next and previous, `u` undo. KEYS.md rules 1, 7 and 9 read this way to me, and
   `d`, `n`/`N`, `u` and `v` do the jobs they do elsewhere. **`c` strains rule 2:** play's
   `c` changes a label. The case for it is vim's `c`, "change": `c` here asks the agent
   for a change, with the GM's words. If that is too loose, feedback could be `:review
   say TEXT`, with no key.
   Alternatively a `ui_choice` dialog over the preview. Recommended: the **keys**. A dialog
   would cover part of the map being reviewed, and would stop `u`.
4. **Does accepting save?** Recommended: **no**. Saving stays the GM's (`:w`), and the
   autosave covers a crash. `--apply --wait` reports `accepted, not saved`.
5. **`--apply` and proposals while `:agent` is off.** Recommended: **yes, they're taken**.
   A proposal changes nothing until the GM accepts it, so it is safe to take one even
   with the channel off. Everything else (`marked`, live `dump`, `--ctl` edits) stays
   behind `:agent on`.
   - That changes what `:agent off` means. Today it closes the socket (`ctl_stop`).
     Under this plan the socket stays and `off` turns off the reads and at-once landing.
     Rule 9 still holds, since `on` brings it back as it was.
   - The test that `:agent off` removes the socket (test_ctl.c:1404-1419) and the
     connection-cap test (1306-1326) change with it.
6. **Do `--ctl`'s direct edits stay?** These are today's requests (`room ...` straight
   onto the map). Recommended: **they become proposals too**, accepted at once only under
   `:agent accept auto`, so there is one road in. The agent's `undo` stays, for its
   accepted changes.
   - The existing tests that expect edits to land directly (test_ctl.c 402-660, 775-1100,
     1502-1614) run under `accept auto`, and their expected answers gain the proposal
     line.
   - The fuzz harness sets `accept auto`. Otherwise each input would leave a job, and
     after 16 inputs the corpus would stop reaching the apply path.
   - `--bench-ctl` does the same.
   - Headless `--apply` (tests 1109-1200) is unchanged, since no vtt holds the map.
7. **Counter-suggestions A, B and C.** *Decided 2026-10-07: the user keeps all three.*
8. **Shelved:** a three-way merge of an outside change into the GM's unsaved work, and
   reloading by itself (`autoread`). With review and partial accept in place neither is
   needed. They go to IDEAS.md.

## Tradeoffs

- **Proposals instead of edits.** An agent's change costs the GM a look and a key. That
  is the point, and `:ask!` and `accept auto` are the way out. AGENTS.md's live loop
  changes: propose, then wait for the verdict.
- **An accept applies values, unchecked.** A proposed floor skips `floor_problem`, a link
  skips `link_problem`, and an added creature can land on a square the GM has filled since.
  The agent's plan was checked when it ran, against the map as it was then. The conflict
  color marks every one of these squares, and `check` catches what's left.
- **Values, not intent.** An accept puts back the squares as the agent left them. If the
  GM moved the Crypt meanwhile, the change lands where the Crypt was. The conflict color
  shows it, and `c` is the answer.
- **Creatures by label.** A proposal that moves an unlabeled creature names it by side
  and square. If that creature has moved since, the element is a conflict, not a guess.
- **A partial accept can leave a half.** For example, a corridor kept without the door at
  its end. The preview shows exactly the part in the box, and `describe`/`check` will
  catch a loose door.
- **The preview swap has one rule:** nothing may run between the swap in and the swap
  out. That is the rule the stamp preview already lives by. It holds because a draw
  takes no input.
- **The base costs memory.** The file's bytes, as last read or written, are kept: 4 KB
  for a typical map, 789 KB for the largest.
- **A held `wait` takes a connection.** The cap goes from 4 to 4 plus 4 waiting, so a
  waiting agent never locks out a request.
- **The socket is always open** while vtt runs interactively: one descriptor, nothing
  when idle.
- **Today's `--ctl` edits change behavior** (decision 6): scripts that expect an edit to
  land must use `accept auto`.

## Performance considerations

Reference figures: PERFORMANCE.md's *Speed of light* (the desktop, 2026-10-07). Built
rows are measured on the same machine.

| path | runs | speed of light | this design, expected | gap and why |
|---|---|---|---|---|
| making a proposal | each `propose` | the plan's own work (21 µs for five rooms) + comparing what it wrote | a scratch copy of the map (30 µs at 512×512, under 1 µs at 40×25, plus creatures and cards) + the plan + comparing only the squares the scratch log names + the small parts whole | the copy, up to 30 µs. A copy-on-write scratch map would remove it, but the plan's writers would then need two maps' hooks. Proposals are seconds apart, and the rejected design (run live, roll back) cost an autosave (up to 3.17 ms) and a fog rebuild (up to 270 µs) each time. Measured at step 1; if the copy shows, copy-on-write is the next step |
| the highlight | each GM frame while a job or proposal is open | the visible cells inside it: a tint per cell | the blocks meeting the window, then their squares; no work at all for regions off screen | none. Culled like every drawing path. Bytes: one changed background per cell when it first appears, nothing after (the diff) |
| the preview swap (B) | each GM frame in review | the change set's squares that fall in the window, swapped in and out | the same, twice, plus the whole creature list (4 µs at 500), with buffers kept across frames | the creature list. Swapping only the changed creatures would mean editing the list in place; at 4 µs it isn't worth it |
| conflicts | entering review, and after a key in review **that changed the map** (`Map.gen` moved) | one compare per element | the same; a cursor move compares nothing | none |
| accept (whole or partial) | `enter` | applying the elements kept | the same, in one undo batch | none |
| the map-changed event | 1.5 s after the map goes quiet, only with an agent waiting or a job open | the changed blocks + the small parts (about 1 µs for 24 creatures) | the second draft's checkpoint, unchanged | none. Zero with no agent |
| `wait` | held connections | idle: nothing | one descriptor each, in `poll` | none |
| the file check | a key (at most once a second), each save and trip | one `stat`: 0.9 µs | the same | none |
| an outside change | once per change, when keys are quiet | parsing two files (`mapio.load` is 4.2 ms at 512×512 on the laptop, so about 8.4 ms) + comparing the maps (59 µs per MB) | the same | parsing the base again when it could be kept parsed. That would cost a whole `Map` held all session (up to 1 MB plus its creatures) to save one parse per outside change, which is rare. Keeping bytes is the cheaper side |
| keeping the base | each load and save | copying the file's bytes: 25 µs for 789 KB | the same, from the writer's buffer | none once the writer builds its text in memory (PERFORMANCE.md's known gap). Until then, keeping the bytes means reading the file back after a save, about the size of the copy |
| `--apply` finding the holder | each `--apply` | 3 µs per running vtt | the same | none |

**Phones.** Nothing new reaches them, and they need no handling during review: the map
never holds a preview (B).

**The GM's terminal.** A highlight appearing writes one background change per visible
cell, once. A color change (working to ready) writes that cell count again, once.
Entering a review redraws the changed cells in the window, once.

**Memory.**
- A change set stores before and after per element: about 4 bytes a square, 512 per
  creature.
- 16 jobs, plus a few outside proposals, are capped together at the undo log's limit.
- The base's bytes.
- The checkpoint, only while an agent waits or a job is open.

**Zones and rows.**
- `PROF_ZONE` for `job.propose`, `job.highlight` (a drawing path), `job.preview` (a
  drawing path), `job.review` (the review's keys), `job.accept`, `map.copy`, `mapdiff`,
  `ctl.wait` (answering waiters), `disk.check` and `disk.reload`.
- **The regression that matters most:** the checkpoint's hook sits on every write to the
  map. A/B rows run a 512×512 fill and an `undo.step` with and without a checkpoint
  active, against the previous commit's binary.
- New `tools/perf.sh` rows:
  - on a 512×512 map: a proposal of five rooms, review frames with the change set in and
    out of the window, and accept;
  - the frame with a highlight covering the window, and with one off it;
  - the event after a dozen GM edits;
  - an outside change on a 512×512 map.
- `tools/saves.sh` runs before and after, since keeping the base touches the save.
- Each row is published beside its speed-of-light figure.

## Tests

- **test_jobs (new suite).**
  - `:ask` with and without a box.
  - The state machine: take, area, say, propose, accept, scrap, feedback, and back to
    working, with the thread kept.
  - `:ask!` and `accept auto` land at once, as one undo step.
  - A proposal leaves the map untouched until it is accepted: `Map.gen`, the undo log,
    the file, and the players' frame are all unchanged.
  - The review:
    - `esc` changes nothing;
    - `enter` gives one `u` step;
    - `u` inside the review undoes the GM's own last step, and the conflicts follow;
    - a partial accept takes only the box's elements;
    - `:review N` brings back a scrapped proposal.
  - Conflicts: an element the GM changed after the proposal is flagged and is
    overwritten on accept. A duplicate label and a dead fog patch are conflicts. A new
    link takes a free number at accept.
  - A proposal leaves the live map, its undo log (redo tail included), `modified` and
    `Map.gen` exactly as they were.
  - An at-once result that arrives in play mode waits as ready, and lands back in build
    mode.
  - `job N dump` shows the proposal in place.
  - Closing the map closes its jobs and sends the event.
  - Golden frames:
    - the working, ready and conflict tints, as text and with colors;
    - the review;
    - the players' frame, with none of them, including during review.
- **test_ctl.c.**
  - The new requests, and their errors.
  - `wait` returns at once on a pending event, blocks otherwise, and times out.
  - `wait` doesn't spin the loop: `ctl_due` is not 0 with only waiters.
  - A waiter that hangs up frees its slot.
  - A verdict reaches a waiter with no further key.
  - The existing direct-edit cases, under `accept auto` (decision 6).
  - `:agent off` under decision 5.
  - The agent's `undo` of an accepted change, and its refusal once something has
    happened since.
  - `--apply` to an open map becomes a proposal. `--wait` gives each of the three exit
    codes.
  - With `:agent off`, decision 5's set.
- **test_mapdiff.**
  - Every kind of element, both directions; identical maps give no changes.
  - A differential check: random plans on a scratch copy, then the change set (compared
    only where the scratch log wrote) agrees with a brute-force comparison of every
    element of the two maps.
  - `map_copy` copies every part, and freeing the copy leaves the original whole (ASan).
  - The preview swap restores every byte.
  - The checkpoint's writers (the second draft's tests).
- **test_core.c.**
  - The file replaced behind the app: a proposal from the disk, highlighted; `:w`
    refuses; accept, then `:w` passes.
  - The file deleted behind the app: `:w` recreates it.
  - Bare `:e`.
- **test_places.c.** A trip is refused while its file has an unreviewed change.
- **Fuzzing.** The `make fuzz-ctl` corpus gains `job`, `propose` and `wait` lines.

## Docs

- **AGENTS.md:** rewritten round jobs. The loop (`wait`, `take`, `area`, `propose`, wait
  for the verdict, act on feedback or a partial accept), proposals of its own, the
  events, the exit codes.
- **CONTROL.md:** the reversed decision, the requests, `wait`, and an "As built" section.
- **README:** a section *Asking an agent* (`:ask`, `:ask!`, `:jobs`, `:review` and its
  keys, `:agent accept`, `:agent command`), outside changes and `:w!`, and bare `:e`.
- **KEYS.md and `src/keys.c`:** the review mode and its bar.
- **CLAUDE.md:** rows for `app_job.c` (jobs, review), `changeset.c`, and
  the checkpoint in map.c; the `ctl.c` and `mapio.c` rows.
- **IDEAS.md:** decision 8's two.
- **This page:** becomes the design record.

## Order

Each step is a commit with its tests.

1. `map_copy` and `changeset.c` (the change set: element kinds, keys, conflicts, applying as
   values whole or by box, the summary), and the preview swap.
2. The copy-on-write checkpoint, with its A/B rows.
3. Jobs and the review mode: `:ask`, `:jobs`, `:review`, the three tints, and the thread.
4. The requests: `jobs`, `job N ...`, `propose`, the agent's `undo` for accepted changes,
   and decision 6's change to `--ctl`.
5. Events and `wait` (the waiting state, the hang-up, answering after the key, the client's
   timeout); the map-changed and map-closed events.
6. `--apply` to an open map, and `--wait`.
7. Other writers: `Map.disk`, the base, the disk proposal, `:w!`, `:e`, trips, and
   opening a map held elsewhere.
8. `:agent command` (decision 1b).
9. The docs, the perf rows, PERFORMANCE.md, then the Fable review.
