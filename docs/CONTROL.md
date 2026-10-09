# The control channel: an agent in the GM's live session

An AI agent (or any script) talking to a running `vtt`: reading the map the GM
has open, asking what the GM is pointing at, and editing it, every change one
undo batch the GM takes back with `u`. The map tools (`--dump-map`, `--check`,
`--describe`) read files; this is the same reading, and writing, on the map in
memory while the GM watches. Signed off 2026-09-26; built the same day. The README's
*Control channel* is the user's reference and docs/AGENTS.md *Working in a live session*
the agent's; this page is why it is the way it is.

## Decisions

| question | answer |
|---|---|
| transport | a Unix socket and a one-shot client, `vtt --ctl`. Never the network: `:serve`'s port is on the Wi-Fi. An MCP server could wrap `vtt --ctl` later without touching vtt. |
| on | off until `:agent on` (or `--agent` at start); `:agent off` closes it |
| where edits land | build mode only, so nothing an agent does reaches the players' phones in play. Proposals come in anywhere (play mode too) and wait; they land in build mode. Reads work anywhere a map is open. |
| freedom | ~~free editing, undo as the safety net; no drafts to approve~~ **Reversed 2026-10-07** (docs/CONFLICTS.md, built 2026-10-09): an agent's edits are a **proposal** the GM reviews and accepts, whole or by box; `:agent accept auto` lands each at once |
| a request | one undo batch, all or nothing: a line that fails rolls the whole request back and says which line and why |
| fog | `fog paint` only; making and setting patches stays `:fog`'s |
| not here | saving, opening, closing maps (the files stay the GM's); a room-level description language; MCP; undoing part of an agent's batch (IDEAS.md) |

## Shape

- **The socket** is `$XDG_RUNTIME_DIR/vtt/<pid>.sock`, else `/tmp/vtt-<uid>/<pid>.sock`,
  in a directory made `0700` and refused if it is anyone else's or open to anyone else.
  Nothing but the same user can connect.
- **One connection, one request.** The client writes the request and shuts its side;
  vtt reads to the end, runs it between keystrokes, queues the answer and closes. The
  answer's first line is `ok`, `error: ...` or `busy: ...`; the rest is what the request
  printed. `vtt --ctl` prints the answer and exits 0 on `ok`, 1 otherwise, 2 when there
  is no vtt to talk to.
- **Finding the vtt.** With one running, `vtt --ctl` finds it; with several, it names
  them with their maps and `--ctl-pid N` picks one. A socket nobody answers on is a
  crashed vtt's and is removed.
- **Bounded.** A request is at most 64 KB and four connections are held at once; one
  that has not finished its request, or not taken its answer, in ten seconds is dropped.
  An answer is written without blocking, so a stuck caller never stalls the GM's
  screen. Off, it costs nothing; on and idle, one more descriptor in `poll`.
- **Where it lives.** `ctl.c` owns the socket and the client; `app_ctl.c` reads a
  request and runs it against the App. main's loop hands finished requests over.

## The language

A request is lines. Words are separated by spaces; `"..."` is one word (with `\"` and
`\\`); a line starting with `#` and a blank line are ignored. Squares are named as the
app names them (`C3`, `AB12`); a region is `B2:K12`; a boundary is named by the
squares either side, `G5|H5` across a vertical one and `C3/C4` across a horizontal one,
as `--describe` names them. Lines run in order, so a read sees the edits before it.

**Reads** (anywhere a map is open):

| line | answer |
|---|---|
| `status` | the map, its file, unsaved or not; the screen and mode; the floor on the GM's screen (a `floor NAME` line, when the map has floors); undo depth; whether edits are taken now and why not |
| `dump [REGION]` | `--dump-map` of the live map |
| `describe [json]` | `--describe` |
| `check [json]` | `--check` of the map in memory (so no file line numbers) |
| `stamps` | the saved stamps and their sizes |
| `characters` | the saved character templates, each with its card's first line when it has one (added with character templates, docs/CHARACTERS.md; cards, docs/CARDS.md) |
| `scenes`, `scene diff NAME` | the map's scenes, and what changed since one was saved: the checkpoints this page once left for later (added with scenes, docs/SCENES.md) |
| `links [json]` | every link (added with links) |
| `floors` | the floors and which the GM is looking at (added with floors) |
| `marked [json]` | what the GM is pointing at: the cursor, a `v` box (rect or circle), wall mode's corner and anchor, the selected creatures and a selection box, the ruler's ends, the GM's last `g p` and each phone's last ping with their age, and the floor on the GM's screen (`floor`). A ping stays on record after its ring fades. |

**Edits** (each request's edits are one proposal, run on a copy of the map; it lands when the
GM accepts it, or at once under `:agent accept auto`):

| line | does |
|---|---|
| `room REGION` | floor over the region, walls round it |
| `tile REGION KIND` | KIND: void floor water rough brush wood hazard |
| `wall REGION [EDGE]` | the region's outline as EDGE (default wall; `none` clears) |
| `edge BOUNDARY EDGE` | one boundary. EDGE: none wall door open window secret opensecret |
| `token add player\|enemy SQ [size N] [hidden] "Label"` | a creature; `hidden` keeps it off the players' screens (added with hidden creatures) |
| `token add player\|enemy SQ from NAME [hidden]` | a character template; its rolls go through the log (`OP_ROLL`), so a failed request takes them back too (added with character templates). Its card comes with it when the map has none by that name; cards are not in the log, so a failed request leaves such a card on the map, named by nobody |
| `token move WHO SQ`, `token del WHO` | WHO is a label, or a square it stands on |
| `token set WHO label "..."\|size N\|note "..."\|hidden on\|off` | |
| `note SQ "text"`, `note SQ` | a GM-only note on a square, or clears it |
| `fog paint REGION N` | paints the region into fog patch N (0 scrubs) |
| `room NAME ...`, `area NAME ...`, `door ROOM SIDE ...`, `corridor A B ...` | the room language (added after stamps): named areas, rooms placed beside rooms, doors by side, corridors; docs/AGENTS.md has the whole of it |
| `stamp NAME SQUARE [rotate 90\|180\|270] [mirror]` | a saved stamp, top-left square here, turned then mirrored; see-through and all or nothing like the GM's `p` (added with stamps) |
| `link A to MAP PLACE [KIND] [size N] [secret]` | a link to another map, checked against that file (added with links between map files, docs/MAPLINKS.md); taking one opens maps, which stays the GM's |
| `link A B [KIND] [size N] [oneway] [secret]`, `link N ... \| remove` | a link between two places, or a change to link N (added with links); docs/AGENTS.md has the details |
| `floor NAME LEVEL`, `floor NAME off` | mark a named area a floor, or unmark it (added with floors) |
| `scene NAME`; `scene save NAME [REGION]`, `scene NAME remove` | put a scene back (an edit, one step with the request); save or remove one, each alone in its request since neither is in the undo log (added with scenes) |
| `undo` | takes back the agent's last accepted change, only while nothing came after it; alone in its request, since it cannot roll back with other lines |

**Jobs** (docs/CONFLICTS.md; `job` and `jobs` lines come before a request's edits):

| line | does |
|---|---|
| `jobs [json]` | every job: number, state (asked, working, ready, accepted, scrapped), who it is from, the GM's text and box, the agent's area, the thread, and a ready proposal's summary and conflicts |
| `job N take` | the job is the agent's (working); the GM sees `#N taken` |
| `job N area REGION` | where the agent will work, tinted on the GM's screen |
| `job N say "..."` | a line for the GM's status line and the job's thread |
| `propose ["..."]`, `job N propose ["..."]` | first in a request: its edits are an idea of the agent's own (a new job), or job N's answer. Edits with neither are an idea of the agent's own |
| `job N drop` | give it back: the GM's job waits as asked; the agent's own is scrapped, kept for the GM's `:review N` (KEYS.md rule 9: only `remove` destroys, and that is the GM's `:ask N remove`) |
| `job N dump [REGION]`, `job N describe [json]`, `job N check [json]` | the map as accepting job N's proposal would make it now |

A proposal is never refused for the GM being busy. One that lands at once waits, as ready,
while a prompt or dialog is open, the `:` line is being typed, a key prefix is waiting, a
wall stroke is open (wall mode's pen keeps an undo batch open across keys, and a change must
never land inside the GM's batch), or the screen is not build mode; it lands at the GM's next
key after that. `undo`, `scene save` and `scene NAME remove`, which act on the live map, are
still refused with `busy:` and the reason.

**The GM sees it happen.** A proposal tints its squares and says `#N ready: ...` on the
status line. One that lands at once says `#N accepted: ... - u takes it back`, the session
log records it, and a ring marks the changed area for a moment. The GM's cursor and camera
never move.

## As built: what the plan did not say

- **One batch.** A request opens one undo batch at its first edit; helpers that open their
  own inside it (`ed_wall_shape`, which `room` and `wall` use) nest in it since undo
  batches nest. `token del` refuses the creature holding the turn: passing the turn on
  would move the fight, which is the GM's, as a side effect of a map edit (the user's
  call, 2026-09-26).
- **Rolling back** is `undo_abort`: the open batch's ops are applied backwards and
  forgotten. A redo tail the GM had (from an undo of their own) is let go by the first
  op, as any edit would, and does not come back with the rollback.
- **The agent's `undo`** compares `Undo.stamp`, which every record, undo, redo and clear
  moves, and `Map.gen`, which every change moves (the GM's own square notes and fog
  settings go round the log), with what its last request left; any difference means
  something came after, and only the GM's `u` may take it back. It goes in a request of
  its own: it cannot be rolled back with the lines around it.
- **The client checks the directory too.** `/tmp/vtt-<uid>` could be made by anyone
  first, and a socket in it answered by anyone, straight into an agent that believes the
  answer; `vtt --ctl` refuses a directory that is not this user's alone, as the server
  does. The server binds under `<pid>.new` and renames into place once listening, so a
  client never finds it refusing and removes it as stale.
- **An oversized request is read to its end** and thrown away before the answer: closing
  with bytes unread would reset the connection and lose the answer.
- **Square notes undo.** They did not before: `OP_NOTE` (`undo_set_note`) carries the text
  before and after in two token slots' `note`. The GM's own `s n` on a square goes
  through it too (since the health check), so `u` takes back a note whoever wrote it.
- **Bounded.** A request records at most `CTL_OPS_MAX` (twice the largest map's squares)
  undo ops; the undo log never trims an open batch, so this is what bounds the memory one
  request can take.
- **The ring** is `App.agent_ring`, drawn on the GM's screen only; `app_view_differs`
  counts it, so it never reaches the phones even if the GM goes to play mode within the
  two seconds.
- **Measured** (docs/PERFORMANCE.md): a 40x40 room with a dozen creatures is tens of
  microseconds (`ctl` zone); a `dump` of a 512x512 map is about 10 ms, once, when asked.
- `make fuzz-ctl` runs requests under libFuzzer against a fixture; each input is undone.
- **Proposals (2026-10-09, docs/CONFLICTS.md step 4).**
  - A request's first edit copies the live map into a kept scratch map
    (`map_copy_into`) and swaps it, with a scratch log, in for the App's own; the request
    then runs as before, and the change set is the difference where the scratch log
    wrote. The live map, its log (the GM's redo tail too), `modified` and `Map.gen` never
    move for a proposal.
  - Lines after an edit read the scratch map: a request that ends in `dump` reads back its
    own proposal.
  - The App's side of a creature removed or a scene put back (the range overlay, the
    selection, the turn notice) happens when the change lands (`land` in app_job.c), not
    when the plan runs.
  - `--apply`, with no GM, edits the map itself as before (`App.ctl_direct`); the
    `propose` line is refused there. `--bench-ctl` and the fuzzer run under accept auto.
  - Sixteen jobs at most; a finished one (accepted or scrapped) makes room for a new one.
    With sixteen waiting, a proposal is refused.
  - `job N dump`/`describe`/`check` read the scratch map with the change set applied, not
    the review's swap: the swap leaves areas and rolls out, since drawing does not need
    them. Only a ready proposal, made for the map's present size.
  - Reads about the GM's side (`status`, `marked`) after a request's edits read the live
    map and log, not the scratch: the GM's selection holds live indices.
  - `job` and `jobs` lines go before a request's `propose` line and edits, so the job a
    proposal answers cannot be dropped from under it.
  - **At once is fixed when a proposal arrives:** one that came in under `accept auto`
    while the GM was busy still lands at the next key if the GM switches to `accept
    review` meanwhile. A proposal for the job the GM is reviewing never lands by itself;
    the GM's `enter` takes it.

## Build order

1. The socket, `:agent`, `--agent`, `vtt --ctl`; `status`, `dump`, `describe`, `check`.
2. `marked`, and the GM's last ping kept on record.
3. The edits, all or nothing, the busy refusals, `undo`, the status line and ring.
4. docs/AGENTS.md *Working in a live session*, README, `tools/perf.sh` rows (a 40x40
   room with a dozen creatures, and a 512x512 dump), the request parser under the
   fuzzer; then the review.

## Instrumentation

`PROF_ZONE("ctl")` round running a request. perf.sh rows run requests through
the same function headlessly (`--bench-ctl FILE`).
