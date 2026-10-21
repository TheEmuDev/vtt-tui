# vtt for agents

Everything an AI agent needs to build and change `vtt` maps for a GM: from a description, in
a file, or live in the GM's session while they watch. Read this page whole before the first
map; the README is the reference for the rest of the app. `vtt --agent-guide` prints this
page as the vtt installed has it, so an agent reads the guide that matches the binary. The
agent skill (`tools/skill/SKILL.md`, installed with `make install-skill` to
`~/.claude/skills/vtt/`) is a dozen lines that send an agent here.

> **Not the page for working on vtt's own code.** An agent changing the C source reads
> `CLAUDE.md` at the top of the repository and `docs/CODEMAP.md`; this page is for an agent
> *using* vtt to make maps.

`vtt` is a virtual tabletop in the terminal, used beside a physical table: the GM builds an
encounter map, then runs the fight on it while the players watch on their phones. It knows
no game's rules. A map is a grid of squares (`A1` top left; columns `A`..`Z`, `AA`..; rows
from 1), with walls, doors and windows on the **boundaries between** squares, terrain on the
squares, creatures (players and enemies, 1-3 squares wide), notes the players never see, and
fog.

## Three ways in

| way | when | how |
|---|---|---|
| **a plan** | a new map from a description, or a batch of changes to a map file | write requests to a file, `vtt map.vtt --apply plan.txt [--new 40x30]` |
| **live** | the GM has the map open and has typed `:agent on` | `vtt --ctl` with requests on stdin; each change is a proposal the GM reviews |
| **by hand** | only when neither works: the file format itself | [Writing the file by hand](#writing-the-file-by-hand) |

A plan and a live session speak the same language, the one below, and follow the same rules:
**a request is all or nothing** (if any line fails, nothing changed, and the answer names
the line and why), and **one request is one undo step** for the GM, once accepted.

**Refusals.** The answer's first line is the verdict: `ok`, `busy: WHY` (the GM is part way
through something: ask again shortly), or `error CLASS: line N: WHY`. The class is one of
six words, and says what to do next; the sentence after it says which square or which name.

| class | what to do next |
|---|---|
| `usage` | the line is written wrong: `help VERB` shows its form |
| `missing` | a name in it names nothing: list them (`jobs`, `scenes`, `stamps`, `characters`, `links`, `floors`, `describe`) |
| `conflict` | what is on the map is in the way (a square taken, a room over another, a label in use, a box off the map): `dump REGION`, then other squares or another name |
| `state` | the line is fine, but not now or not yours (the channel off, no map open, a job the GM is deciding, nothing to undo): read `status` and `jobs`; do not send it again unchanged |
| `limit` | over a fixed size (a request over 64 KB, a line over 1023 characters, sixteen proposals waiting, or the GM's kept proposals holding 4,194,304 squares together, reads of more than 300,000 squares of map or edits over more than 524,288 in one request, more lines after an answer has passed 2 MB): split it, or wait for the GM |
| `internal` | vtt's own failure: tell the GM |

**Which way.** Either works at any time. `vtt map.vtt --apply plan.txt` asks every running
vtt whether it has that file open. If one does, the plan goes to it as a **proposal** for its
GM to review and the file is not touched (stderr says so); if none does, the plan is run on
the file and saved, as always. So `--apply` is safe on a map the GM has open, with or without
`:agent on`. Add `--wait SECONDS` to wait for the GM's verdict:

| exit | means | stdout |
|---|---|---|
| 0 | accepted, whole or in part (or run on the file, with no vtt holding it) | the event (`job N accepted: ...` or `job N part: ...`), then `, not saved` -- saving is the GM's |
| 1 | the plan failed: nothing proposed, nothing saved | |
| 2 | nothing done: the file could not be read, or a running vtt did not say whether it has the map open (stopped, or its GM is in an editor) -- try again; or the vtt quit before a verdict | |
| 3 | no verdict in that time; the proposal still waits | `no verdict in N seconds ...` |
| 4 | scrapped, or the job or the map went away | the event |
| 5 | sent back | the GM's feedback, one line |

Writing the map's file yourself (by hand, below) while a vtt has it open is safe too: vtt
notices, and offers what you changed to its GM as a change to review. Until the GM has
looked, their `:w` is refused rather than written over yours.

A plan for `--apply` has no `propose` line of its own, and is at most 64,896 bytes, whether
it goes to a file or to an open map. A plan that only reads leaves the file as it was. To do
more than propose (read the map as it is in memory, answer the GM's jobs, hear events), the GM types `:agent on` and you
use `vtt --ctl`; it exits 2 when no vtt has its channel on.

**Where the GM's maps are.** The GM's menu lists the maps in the directory vtt was started
in and in `~/.local/share/vtt/maps` (`$XDG_DATA_HOME/vtt/maps` when that is set), and
`:e crypt` opens `~/.local/share/vtt/maps/crypt.vtt`. A new map for the GM goes there unless
they name a folder. `--apply` takes its path as given, so spell it out:
`vtt ~/.local/share/vtt/maps/crypt.vtt --apply plan.txt --new 40x24`. A map that a link
leads to must sit beside the map the link is on. The GM's stamps and characters are in
`~/.local/share/vtt/stamps` and `characters`; read them with `stamps` and `characters`
rather than opening the files.

## The request language

One request is lines. Words are separated by spaces; `"..."` is one word (`\"` and `\\`
inside); blank lines and lines starting with `#` are skipped. Lines run in order, so a read
sees the edits before it.

**Places.**

- A square is `C3`. A region is `B2:K12` (either corner first) or one square.
- A boundary is named by the squares either side: `G5|H5` across a vertical one (between
  a square and the one east of it), `C3/C4` across a horizontal one (between a square and
  the one south of it), `-` for off the map (`-|A1` is A1's west edge).
- **A room's name** works wherever a region does (its box) and wherever a creature's square
  does (the free square nearest the room's middle), and a note on a room goes on its
  middle square. Names are 1-31 characters, no quote or colon, and never something that
  could be a square (`C3`, `AB12`) or a row (`5`); `Room1` and `Cell2` are fine, `"Great
  Hall"` goes in quotes. An area may hold others (a floor holding its rooms): a square is
  in the smallest area that holds it.

**Rooms and areas.** An area is a named box; a room is a floored area with a wall round it.

| line | does |
|---|---|
| `room Crypt B2 8x6` | a room with its top-left square and size, named |
| `room Crypt B2:I7` | the same, by region |
| `room Vault 6x4 east of Crypt gap 3 [top\|middle\|bottom]` | placed beside another room: `gap` squares between (0: they share a wall), lined up on the other room's `middle` unless told; `north`/`south` line up `left`/`middle`/`right` |
| `room B2:I7` | an unnamed room |
| `area Upper B1:Z20`, `area Upper remove` | name a box without drawing anything (a floor, a region you drew by hand), or take the name off |
| `door Crypt east [N\|middle] [KIND]` | a door on a room's side: the Nth square along it from the top or left (`middle` by default); KIND below, a door by default |
| `corridor Crypt Vault [width 1-3] [KIND]` | dug between two named rooms through void: **straight** when one is beside or above the other and they share at least `width` rows (or columns) -- refused when they share fewer; **one bend** when they are apart both ways, leaving the first room's side and entering the second's, each side at least `width` long. Walled along (a door or window already on a boundary it runs past stays); a door at each end when one wide, open ends when wider. Rooms sharing a wall just get the doorway. Ground, or a named room that does not hold both, in the way refuses it |

**Squares and boundaries.**

| line | does |
|---|---|
| `tile REGION KIND` | `void` `floor` `water` `rough` `brush` `wood` `hazard` |
| `wall REGION [KIND]` | the region's outline; KIND `wall` (default) `door` `open` (an open door) `window` `secret` `opensecret`, or `none` to clear |
| `edge BOUNDARY KIND` | one boundary |
| `stamp NAME SQUARE [rotate 90\|180\|270] [mirror]` | one of the GM's saved stamps (a table, a pillar row), top-left here; void and blank boundaries in it leave the map as it was |
| `note SQUARE "text"`, `note SQUARE` | a note on a square only the GM sees, or take it off |
| `fog paint REGION N` | into fog patch N (0 scrubs); the GM makes patches |

**Floors.** A floor is an area the GM's screen can show alone, with a level (-1 a
basement, 0 the ground, 1 upstairs). The GM steps between floors with `[` `]`; the players'
screens show the party's floor.

| line | does |
|---|---|
| `floor Upper 1`, `floor Upper off` | the named area is a floor at level 1 (-99 to 99), or no longer one. Floors may not overlap |
| `env Crypt haunted-crypt`, `env Crypt remove` | the named area's environment is the card of that name (letters, digits, `-`, `_`), or it has none. The card is the GM's to write; the map's own environment is the GM's to set |

**Links.** Stairs, a ladder, a trapdoor or a portal: two places joined, so the GM can send
creatures from one to the other with a key. Floors of a building, or a place reached by
magic, go on the same map apart from each other, separated by void, and a link joins them.

| line | does |
|---|---|
| `link Hall Tower [KIND] [size 2\|3] [oneway] [secret]` | a new link, numbered the lowest number free. Each end is a room's name (the free ground nearest its middle) or a square (the end's top-left). KIND `stairs` (default) `ladder` `trapdoor` `portal`; `size 3` makes each end 3x3, room for a party; `oneway` runs from the first end only; `secret` hides it from the players. Both ends on ground, apart, and on no other link's squares |
| `link B2 to crypt Entrance [KIND] [size N] [secret]` | a link to another map: one end here (a square, or a room's middle), the party arriving in `crypt.vtt`'s area `Entrance` (or a square there); the file must be beside this map's and the place must be there. Taking it is the GM's (`g o`) |
| `link 3 portal oneway`, `link 3 reverse`, `link 3 remove` | change link 3 (a kind, `oneway`, `twoway`, `reverse` swaps its ends, `secret`, `seen`), or remove it |

**Creatures.**

| line | does |
|---|---|
| `token add enemy SQUARE [size 2] [hidden] "Ghoul"` | `player` or `enemy`; size 1-3; labels unique; on ground and on nobody; `hidden` keeps it off the players' screens (an ambush) |
| `token add enemy Crypt "Ghoul"` | in a room: the free square nearest its middle |
| `token add enemy Crypt from ghoul [hidden]` | one of the GM's saved characters (`characters` lists them), on a square or in a room: its size, label (numbered to stay unique), note, counters, and any of its rolls the map lacks. The answer says the label it got: `placed "Crypt Ghoul 2" at L4` |
| `token move Ghoul F6`, `token del Ghoul` | by label (any case), or a square it stands on |
| `token set Ghoul label "..."`, `size 2`, `note "..."`, `hidden on\|off` | |

**Scenes.** A scene is the creatures as they stand, saved on the map under a name: a
checkpoint to compare against or go back to. Saving and removing one change nothing `undo`
can take back, so each goes in a request of its own.

| line | does |
|---|---|
| `scene save "Before" [REGION]` | alone in its request: save every creature, or those meeting REGION (an area name works), with the round and spotlight |
| `scene "Before"` | put it back: one undo step with the rest of the request; a boxed scene replaces only the creatures in its box |
| `scene "Before" remove` | alone in its request: throw it away |

**Jobs and events.** *Working live with the GM*, below, says how these are used.

| line | does |
|---|---|
| `propose ["a line for the GM"]` | live only, the first line of a request: its edits are a proposal of your own for the GM to review |
| `job N take`, `job N drop` | the job is yours (the GM sees it taken), or you give it up |
| `job N area REGION`, `job N say "..."` | show the GM where you will work, or put a line on the GM's status line |
| `job N propose ["a line for the GM"]` | the first line of a request: its edits are your answer to job N |
| `job N dump [REGION\|all\|key]`, `job N check`, `job N describe` | your proposal read back, as the map would be with it |
| `job N conflicts` | what the GM changed under a waiting proposal since you made it, a line each (the first 64): what was there when you proposed and what is there now. Propose again from these |
| `job N undo` | live only, alone in its request: take back job N's accepted change, every part of it, while it is the last thing done to the map |
| `jobs [json]` | every job and where it stands, with what was said on each |
| `wait [SEQ] [for SECONDS]` | alone in its request: every event after SEQ, held until there is one or the time is up |

**Reads.**

| line | answers |
|---|---|
| `dump [REGION]` | the map as text: squares in the file's characters, boundaries between, creatures as `1`-`9` `a`-`z` `A`-`Z`, then the creatures, notes, areas and links there. With no region it is the whole map, and refused (`limit`) on a map over 4,096 squares: name a region, or say `dump all`. An area the GM has named `all` or `key` is read as that area |
| `dump key` | what the characters of a dump mean; read it once |
| `changes SEQ [REGION]` | the events after `SEQ` (as `wait` gives them), then the map **as it is now, only where they changed it**: a `changed REGION` line and that region's dump for each. After an event, read this and not the map again |
| `describe [REGION] [json]` | the rooms walls make, named by their areas, with doors and links and where they lead, and what is in each; with a region (or an area's name), only the rooms that meet it, each whole |
| `check [json]` | mistakes in the map's file, with its line numbers (a row added or lost by hand, a line vtt does not know), as vtt last read or wrote it, then on the map: loose doors, creatures on void, rooms nothing reaches ([codes](../README.md#map-tools---dump-map---check---describe)); what a row out of place breaks is folded into its finding; with `json`, a `codes` object says what to do about each kind of finding, once |
| `stamps` | the GM's saved stamps and their sizes |
| `characters` | the GM's saved characters: name, label, side, size, counters, rolls |
| `scenes` | the map's scenes: name, creatures, round, box |
| `scene diff "Before"` | what changed since: `moved "Ghoul" C3 -> D5`, `gone`, `new`, `changed "Aria": HP 4/6 (was 6/6), ...`, the round; `no changes` |
| `links [json]` | every link: its number, kind, ends, size, one-way and secret |
| `floors` | the floors, top first, with their levels and extents, and which the GM is looking at |
| `agent NAME` | who you are, as a request's **first line**: `vtt --ctl` writes it for you when `VTT_AGENT` is set in your environment, so set that once and never type it. A job is its taker's and `undo` is your own; with no name you are `cli` |
| `status` | live only: the map, the floor on the GM's screen, whether edits are taken now, and `seq`, the newest event's number, to give your first `wait` |
| `marked [json]` | live only: what the GM is pointing at -- the cursor (and the area it is in, and the floor on screen), a `v` box, selected creatures, the ruler, recent pings |
| `help`, `help token`, `help json` | every request with a line each; how one is written; or all of it as JSON. The list vtt itself works from: if this page and `help` ever disagree, `help` is right |
| `undo` | live only, alone in its request: take back your last change to land, once, while nothing has happened since |

## From a description to a map

1. **Name every place first**, then place the rest by name. A description is rooms and how
   they join; so is the plan:

   ```
   # The drowned crypt: an entry hall, the crypt east of it, a flooded well below.
   room Hall B2 6x4
   room Crypt 8x6 east of Hall gap 3
   room Well 4x3 south of Hall gap 2 left
   corridor Hall Crypt
   corridor Hall Well width 2
   tile Well water
   token add enemy Crypt "Ghoul"
   token add enemy Crypt "Ghoul 2"
   token add player Hall "Aria"
   note Crypt "the sarcophagus lid is loose"
   ```

   `vtt crypt.vtt --apply plan.txt --new 40x24` makes the file (void to start) and exits 0,
   or 1 with the failing line on stderr and nothing saved (a file `--new` made is taken away
   again), or 2 when the map cannot be read or written. A map with an autosave newer than it is applied to as saved, with a line on
   stderr saying so.
2. **Read it back.** `vtt crypt.vtt --dump-map` and look: is every room where the
   description puts it, every door in a wall, every creature on the floor?
   `vtt crypt.vtt --describe` says what rooms the walls make and how they join -- a room
   `NOT REACHABLE` is a missing door (or link). `vtt crypt.vtt --check` must say
   `no findings`.
3. **Fix with another plan** against the same file (it opens what is there), not by
   rewriting the first. To move a room, clear it first, then draw it again:
   `wall Crypt none`, `tile Crypt void`, then `room Crypt ...` (the name moves with the new
   box; the old floor and walls would otherwise stay).
4. **Show the GM the dump** and name squares and rooms back ("the door at I4", "the Crypt").

**What to know about the geometry.**

- Walls sit **between** squares, so two rooms side by side (`gap 0`) share one wall, and a
  door in it joins them. `corridor` between them makes that door.
- A corridor with open ends (wider than one) joins its rooms into one space, and
  `describe` reports them as one room -- that is right: rooms are what walls divide.
- A room drawn over another's ground takes it: `room` floors its whole box and walls its
  outline. Plan rooms apart and join them with corridors or `gap 0` and a door.
- Creatures need ground under every square of their footprint; `in` a room they find it.
- **A building of several floors** is several blocks of the same map, apart with void
  between: name each block as an area, mark it a `floor` with its level, draw its rooms
  inside it, and join the floors with `link`. Make the floors the same size with their
  stairs in the same place, and the GM's cursor stays on the stairs stepping between them:

  ```
  area Ground A1:L10
  area Upper O1:Z10
  floor Ground 0
  floor Upper 1
  room Hall B2 8x6
  room Loft P2 8x6
  link Hall Loft stairs
  ```

## Working live with the GM

**If vtt started you** (`:agent command`): your standard input is one job -- what the GM
asked, the box they drew, the job's history with any feedback last -- and the `vtt --ctl`
lines to use, with this vtt's path and pid filled in. `$VTT`, `$VTT_PID`, `$VTT_JOB` and `$VTT_AGENT` (your name, below) hold
the same. Take the job, read the map, propose once, read your proposal back, and exit; you
are started again if the GM sends it back. You need no `wait` loop: one run is one attempt.
What you print goes to the GM's session log. **Exit when you are done, and say something
while you work:** if you exit with the job taken and nothing proposed, it goes back to
`asked` and the GM is told your exit status; if vtt hears no request from you for ten
minutes (a held `wait` counts as one), it stops you and everything you started, and the job
goes back the same way.

The GM has the map open in `vtt` and has typed `:agent on`. Send requests with `vtt --ctl`
(requests on stdin, or one as an argument). **Your edits are proposals:** they run on a copy
of the map and wait, tinted on the GM's screen, until the GM reviews them (`:review`) and
accepts them (whole, or the part in a box), scraps them, or sends them back with a line of
feedback. Nothing you send changes the GM's map until then, unless the GM has typed
`:agent accept auto`; then each proposal lands at once, one `u` for the GM.

1. `vtt --ctl status` first: which map, and how edits are taken (`edits proposed, for the GM
   to review`, or `edits land at once`).
2. `vtt --ctl jobs` lists what the GM has asked for (`:ask` over a box), each with its
   number, its box, and its thread: the GM's words, yours, and what came of each proposal.
   `jobs json` is the same as JSON.
3. **Answering a job:**

   ```
   vtt --ctl 'job 3 take'                       # yours: the GM sees "#3 taken"
   vtt --ctl 'job 3 area B2:K12'                # where you will work, tinted for the GM
   vtt --ctl 'job 3 say "two ghouls, a flooded floor"'   # a line on the GM's status line
   vtt --ctl <<'EOF'
   job 3 propose "the crypt, flooded"
   room Crypt B2:K12
   tile C3:J10 water
   token add enemy D4 "Ghoul"
   EOF
   ```
   The answer is `proposal #3: ` and what it changes, then `waiting for the GM's review`.
   Without a job (an idea of your own), start the request with `propose "what it is"`, or
   send the edits alone: either makes a new job of yours.
4. **Read your proposal back before the GM looks:** `job 3 dump [REGION]`, `job 3 describe`
   and `job 3 check` read the map as accepting it would make it. Plain `dump` reads the GM's
   map as it is.
5. **The verdict: `wait`.** `vtt --ctl 'wait 41'` blocks until something has happened after
   event 41, then prints `seq N` and each event since, one a line with its number:

   ```
   seq 44
   42 job 3 feedback: no water in the east half
   43 map changed: walls and doors in F2:F9
   44 job 5 asked: in M3:Q8: a guard room
   ```
   Pass the `seq` it gave you to the next `wait`, so nothing is missed between two waits.
   `wait` alone hears only what happens from now; `wait 0` answers at once with everything
   still kept (the last 32), and is a read, never a wait: do not call it in a loop. To wait,
   give a time, with the `seq` you were last given (`status` has it too), 0 included; `wait 41 for 300` waits up to 300 seconds (60 unsaid, 600 at most) and then answers
   with `seq` alone. A `lost A-B` line means more happened than is kept, and a `reset N`
   line that the vtt was started again since your number (it is followed by everything
   kept): either way read `jobs` and the map again. An event is its number, its subject
   (`job N` or `map`), then **one word that says what happened**, ended by a colon or the
   line; match on that word, never on the words after it. The events:

   | event | means |
   |---|---|
   | `job N asked` | `: [in REGION, ][to land at once: ]TEXT` -- the GM asked for a change: take it |
   | `job N accepted` | `: WHAT` -- it landed, as one undo step |
   | `job N part` | `: REGION: WHAT [- the rest waits]` -- the part in the GM's box landed |
   | `job N scrapped` | the GM threw the proposal away |
   | `job N feedback` | `: TEXT` -- the job is yours again, with what to do instead: `job N propose` |
   | `job N back` | `: for review` -- the GM brought a scrapped or undone proposal back |
   | `job N removed` | the GM removed the job: its number answers `no job #N` now |
   | `job N taken` | `: by NAME` -- another agent has the job: leave it |
   | `job N proposed` | `: WHAT` -- another agent's proposal is waiting for the GM: its squares may change under you |
   | `job N undone` | `: taken back by NAME` -- another agent took its accepted change back: read `changes` |
   | `map changed` | `: WHAT` -- the GM changed the map (told once it has been quiet 1.5 s): what you read may be stale |
   | `map closed` | `: NAME ...` -- the map, and every job on it, is gone |

   `jobs` shows the same state at any time, and counts a ready proposal's **conflicts**:
   squares the GM changed since you looked, which accepting would overwrite; `job N
   conflicts` lists them (`tile C3: floor when proposed, water now`), so you can propose
   again without reading the map over. A part accepted
   by box leaves the rest `ready`; feedback means propose again with `job N propose`, and
   the new proposal replaces the old.

   A job you never took can show up accepted: a change that came from the map's **file**
   (another program wrote it, and the GM reviewed it) is a job too, and `jobs` says `the file
   on disk`. No `map changed` follows an accept.

   An event's text is cut at 240 characters; `jobs` has a job's whole text. The GM undoing
   an accepted change with `u` comes as `map changed`, not as a job event.

   **What you do yourself is not told to you.** Taking a job, proposing, giving one back
   and undoing are events for every other name and none for yours, so your own `wait` is
   never woken by your own request. Their numbers are still used: the events you read can
   skip one (`41`, `43`), which is not a `lost`. Callers with no name are all `cli`, and
   none of them hears another: give each agent a name when there are two.

   **After `map changed` or an accept, do not dump the map again.** `changes 41` gives the
   events after 41 and then the map as it is now in the parts they touched:

   ```
   seq 43
   42 map changed: walls and doors in F2:F9
   43 job 3 accepted: ground in K4
   changed F1:J8
   (that region's dump)
   ```
   The regions are coarse (the map cut sixteen by sixteen), so a `changed` region holds the
   change and some squares round it. `changes 41 B2:K12` keeps to a region of yours. When
   more changed than one answer holds (16 KB as written, creatures and notes counted), the rest
   is a `more: REGION REGION ...` line:
   `dump` each. An `also:` line says something on no square changed (rolls, cards, scenes,
   clocks, the fight): `describe` has those. A `lost` line means the events are too old to
   say where, and `map closed` that it is another map now: then read the map. vtt follows
   the map's changes only while an agent is about (a `wait` held, a job open, or a `wait`
   or `changes` in the last ten minutes); come back later than that and a change in between
   is one event, `map changed: while no agent was listening`, with every region on it.
   `changes` goes before a request's edits, never after them.

   So an agent that serves a GM is a loop: `wait`, act on what came back (`take` a job that
   was asked, `propose`, answer feedback), `wait` again from the last `seq`.
6. `job N drop` gives a job back: the GM's waits as asked; one of your own is withdrawn
   (the GM can still bring it back with `:review N`).

   Two kinds of job are the GM's alone to decide, and `take`, `area`, `say`, `propose` and
   `drop` on them are refused: a change that came from the map's file, and one sent by
   `--apply`. You may read them (`job N dump`, `check`, `describe`). To offer something
   else, `propose` a change of your own.

   A job's number is yours while the job lasts and is not given to another job straight
   after: numbers climb to 99 before they start again at 1. If a number you hold answers
   `no job #N`, the job was removed or its map was closed (`:e` to another map closes one
   too, with a `map closed` event).
7. When the GM says "here", "this room" or "that one", ask `vtt --ctl marked` and work from
   the squares it names. `marked` says which area the cursor is in.
8. Put down the GM's stamps (`vtt --ctl stamps`) for anything they have one for, rather than
   drawing it square by square, and their characters (`vtt --ctl characters`) rather than
   bare creatures.
9. Suggestions go on the map as notes (`note F7 "secret door?"`); the players never see them.
10. If the GM does not like a change you made that was accepted, and nothing has happened
    since, `vtt --ctl undo` (on its own) takes it back; otherwise ask them to press `u`.
    Never repair a change by undoing the GM's own work.

**Who you are.** Set `VTT_AGENT` to a name of your own (letters, digits, `-`, `_`; 31 at
most) and `vtt --ctl` sends it with every request; an agent the GM's `:agent command`
started has it set already, to `job-N`. `status` shows the name vtt took you for (`you
NAME`; `cli` with none). It matters in three places. **A job is its taker's:** once you
`take` a job, or propose one of your own, only your name may `take`, `area`, `say`,
`propose` to or `drop` it, and another's answers `state: #N is NAME's`; anyone may read one
(`job N dump`, `check`, `describe`, `conflicts`), and `jobs` says who has what. **`undo` is
your own:** it takes back the last change to land only if it was yours, and once. **`job N
undo` takes back job N's accepted change by number,** every part of it, while it is the
last thing done to the map; under a later change (the GM's, or another job's) it answers
`state`, and the way to put it right is a new proposal, which the GM sees. What it takes
back is what **you** landed for that job: a job you were given after another agent gave it
back has that agent's steps too, and they are not yours. Both undos answer `busy` while the
GM is in the middle of something, reviewing the rest of that job included: ask again. **One `wait` a name:** a second
`wait` from your name takes the first one's place, and the first is answered `superseded`,
so an agent that was stopped and starts again is never locked out by its own old wait; the
answer it gets carries every event since the number it waited from, so nothing is lost
between the two. **Never run two `wait`s under one name at once:** each would answer the
other for ever. A line's number in a refusal counts from your first line, not from the
name's.

A name is not a password: vtt takes the one it is given, from anyone who can reach the
socket, which is you. It keeps agents out of each other's way; it does not keep one out.
If the GM sends your job back, the `feedback` event is yours to answer (`job N propose`):
vtt starts an agent of its own again only for a job its own agent (`job-N`) has, or nobody.
A plan sent with `--apply` carries no name and its job is the GM's alone to decide.

**One request reads at most 300,000 squares of map** (the GM's screen waits on it). A
`dump` counts its region's squares; `describe`, `check` and a `job N` read back count the
whole map each (`check` the map's squares or its file's bytes at a third of a square each,
whichever is more). On most maps that is dozens of reads; on a 512×512 map it is one whole-map
read a request, so send `describe` and `check` separately there and `dump` by region.
`stamps` and `characters` open every saved file: three of them a request, and `changes`
three times at most. One read's answer is never refused for its size, but once a request's
answer has passed 2 MB its next line is: send that in another request. **Its edits cover
at most 524,288 squares**, whether or not they change them: a `tile` or `fog paint` counts
its region, a `wall` its outline, a `room` its box, a `stamp` its size. A plan run on a
file with `--apply` has neither bound.

Each request is still all or nothing, and each accepted proposal is one undo step. A request
may read before its edits (`jobs` and `job N ...` lines come first, before `propose`) and
after them: `dump`, `describe` and `check` after an edit see your proposal; `status` and
`marked` always describe the GM's side.

Exit status: 0 done; 1 an error or `busy:` (the verdict on stderr); 2 no vtt is listening --
ask the GM to type `:agent on` (stderr lists each vtt whose channel is off, by pid and map). Proposals are never refused for the GM being busy: under
`accept auto` one waits and lands at the GM's next key. Only `undo`, `scene save` and `scene
NAME remove` still answer `busy:` when the GM is part way through something.

**In play mode,** proposals still come in and wait; the GM reviews in build mode. Reads still
work, so `dump`, `describe` and `marked` can follow the fight. Do not send the same proposal
in a loop: one is enough, and `wait` tells you what became of it. Moving creatures, fog and
the turn order during play stay the GM's.

The channel never saves; saving is the GM's (`:w`). It never takes away the creature whose
turn it is in a fight: the fight is the GM's.

## Checking a map

`vtt map.vtt --dump-map` prints the lattice: squares at odd positions, boundaries between,
a wall's corners drawn as its line (`-` or `|`, never `+`, so a `+` is always a door),
creatures as `1`-`9` `a`-`z` `A`-`Z` listed underneath, then the notes and named areas.
`--region B2:K12` prints part of a big map. `--describe` lists the rooms, each named by its
area when one holds its first square (`room 2 Crypt (J3)`), with its extent, terrain, doors
and windows and the room each leads to, and the creatures, notes and fog in it; `--json`
gives the same as JSON. `--check` exits 0 clean, 1 with findings, 2 when the file cannot be
read; the codes that matter most are `W104 door-loose` (a door with no wall at either end),
`E110`/`E111` (a creature on void or off the edge) and `W120` (a room nothing leads to).

## Writing the file by hand

A last resort, when you must write or repair the file itself. The file is plain text; the
README's *File format* section is the full reference.

```
VTT 10
name Crypt Entrance
size 6 4
scale 5
metric alt
tiles
......
......
..~~..
......
vedges
|     |
|  +  |
|  |  |
|     |
hedges
------
      
---  -
      
------
token player 1 1 1 "Aria"
token enemy 4 2 1 "Ghoul"
note 2 3 "loose flagstone"
area 0 0 5 3 "Entrance"
```

`VTT 10` first (a lower number is fine if the map has no hidden creatures, floors, links or areas); then header lines (`name`,
`size W H`, `scale` feet per square, `metric` chebyshev / euclidean / alt / manhattan,
optionally `ruleset daggerheart`); then the sections. **Coordinates in the file are 0-based
x then y**; the app and the tools name squares with letters and 1-based rows (`x 4, y 2` is
`E3`).

A map `W` wide and `H` tall has three grids, and the one mistake to avoid is their sizes:

| section | rows | characters a row | what a row is |
|---|---|---|---|
| `tiles` | `H` | `W` | the squares of one row |
| `vedges` | `H` | **`W + 1`** | the boundaries *between* squares in one row, west edge first |
| `hedges` | **`H + 1`** | `W` | the boundaries above row 1, between each pair of rows, below the last |

The loader reads rows by count, not by looking for the next header: a `vedges` section one
row short swallows the `hedges` line and every section after it shifts, silently; `--check`
reports it (`E011`). Header lines come before the first section. There are no comments. A
blank line inside a section is a row. A door on the map's own edge is a way out (`N105`).

| square | char | | boundary | char |
|---|---|---|---|---|
| void | space | | none | space |
| floor | `.` | | wall | `\|` (in `hedges`, `-` also) |
| water | `~` | | door | `+` |
| rough | `:` | | open door | `/` |
| brush | `"` | | window | `%` |
| wood | `=` | | secret door | `S` (players see a wall) |
| hazard | `^` | | open secret door | `s` |

Creatures: `token player X Y SIZE "Label"` (or `enemy`), anchored at the top-left square;
`tokennote "text"` after one, and `tokenhidden` to keep it off the players' screens. Square notes: `note X Y "text"`. Areas: `area X0 Y0 X1 Y1
"Name"`. Floors: `floor "Name" LEVEL` after the area lines. Environments: `env "card"` for the map, `areaenv "Name" "card"` for an area, after those. Links: `link N KIND SIZE X0 Y0 X1 Y1
[oneway] [secret]`, each end by its top-left square. Fog: `fog on`, `fogpatch N Name reveal R memory on`, then a `fog` section of `H`
rows of `W` characters (`.` none, `A`-`O` patch 1-15).
