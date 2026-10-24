# vtt for agents

`vtt` is a virtual tabletop in the terminal: the GM builds an encounter map, then runs the
fight on it while the players watch on their phones. It knows no game's rules. A map is a
grid of squares (`A1` top left; columns `A`..`Z`, `AA`..; rows from 1), with walls, doors and
windows on the **boundaries between** squares, terrain on the squares, creatures (players and
enemies, 1-3 squares wide), notes the players never see, and fog.

## Start here
<!-- read for: core -->

**Two ways in, one language.**

- **A plan** -- a new map, or changes to a map file: write the requests to `plan.txt` with
  your file tool, then `vtt map.vtt --apply plan.txt` (`--new 40x30` when there is no file
  yet). It is safe on a map the GM has open: it goes to the GM as a proposal instead.
- **Live** -- the GM has the map open and typed `:agent on`: `vtt --ctl` with the request on
  stdin or as one argument; each change is a proposal the GM reviews. `vtt --ctl status`
  says which (exit 2: no vtt is taking requests -- use a plan; or a list of vtts listening:
  add `--ctl-pid N` for the GM's to every call).

Run each `vtt` command on its own, with no shell around it (no `cat >`, no `&&`): an agent's
permissions often allow `vtt ...` and nothing else.

**Every request is all or nothing:** if a line fails, nothing changed, and the answer says
which line and why. Accepted, one request is one undo step for the GM.

**The loop:** plan or propose, then read it back -- `vtt map.vtt --check` (exit 0 is clean),
`--describe` (the rooms the walls make and how they join), `--dump-map` -- and fix with
another plan against the same file.

**When done, answer in a line or two:** what changed, by square and name (`Crypt K2:R7, door
J4, two ghouls`), and anything left undone. No description of the map, no repeat of the plan:
the GM has the map in front of them.

## Refusals
<!-- read for: core, live -->

A refusal is exit 1 with `vtt: error CLASS: line N: WHY` on stderr (over the channel, the
answer's first line), and nothing on stdout. `usage`: written wrong (its `help` is named).
`missing`: a name names nothing -- list them (`describe`, `jobs`, `stamps`, `characters`,
`links`, `floors`). `conflict`: something on the map is in the way -- `dump` there, then other
squares or another name. `state`: fine, but not now or not yours -- read `status` and `jobs`,
do not resend unchanged. `limit`: too big -- split it (`vtt --agent-guide limits`).
`internal`: tell the GM.

## The request language
<!-- read for: plan, live -->

One request is lines. Words are separated by spaces; `"..."` is one word (`\"` and `\\`
inside); blank lines and lines starting with `#` are skipped. Lines run in order, so a read
sees the edits before it.

- A square is `C3`. A region is `B2:K12` (either corner first) or one square.
- A boundary is named by the squares either side: `G5|H5` across a vertical one (a square
  and the one east of it), `C3/C4` across a horizontal one (a square and the one south of
  it), `-` for off the map (`-|A1` is A1's west edge).
- **A room's name** works wherever a region does (its box) and wherever a creature's square
  does (the free square nearest the room's middle). Names are 1-31 characters, no quote or
  colon, never something that could be a square (`C3`) or a row (`5`); `"Great Hall"` goes
  in quotes.
- An **area** is a named box; a **room** is a floored area with a wall round it. A **floor**
  is an area the GM's screen shows alone, with a level; a **link** (stairs, a ladder, a
  trapdoor, a portal) joins two places, so floors of a building, or a place reached by magic,
  go on the same map apart, void between, joined by links.

The requests, each as `help VERB` gives it -- its forms, what it does, its rules:

<!-- requests -->

## From a description to a map
<!-- read for: plan, live -->

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

2. **Read it back:** is every room where the description puts it, every door in a wall,
   every creature on the floor? `describe` names a room nothing reaches; `check` must say
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

## Plans and files
<!-- read for: plan -->

`vtt map.vtt --apply plan.txt` exits 0 done, 1 when a line failed (nothing saved, the line on
stderr), 2 when the file cannot be read or written, or a running vtt did not answer. `--new
WxH` makes the file, void to start; a file it made is taken away again when the plan fails or
changes nothing. A map with an autosave newer than it is applied to as saved (stderr says so). A plan
has no `propose` line of its own; one that only reads leaves the file as it was.

When a running vtt has the file open, the plan goes to its GM as a proposal and the file is
not touched (stderr says so). `--wait SECONDS` waits for the verdict: exit 0 accepted (whole or
in part; saving is the GM's), 3 no verdict yet (it still waits), 4 scrapped or gone, 5 sent
back with the GM's feedback on stdout.

**Where the GM's maps are:** the directory vtt was started in, and `~/.local/share/vtt/maps`
(`$XDG_DATA_HOME/vtt/maps`); `:e crypt` opens `crypt.vtt` there, and a new map for the GM goes
there unless they name a folder. Spell the path out to `--apply`. A map a link leads to sits
beside the map the link is on. The GM's stamps and characters: read them with `stamps` and
`characters`, not their files.

Writing the map's file yourself while a vtt has it open is safe too: vtt offers what you
changed to the GM as a change to review.

## Checking a map
<!-- read for: plan, live -->

`--dump-map`, `--describe` and `--check` are the `dump`, `describe` and `check` requests run on
the file (`--region B2:K12` for part of a big map, `--json` for JSON). In a dump a wall's
corners are drawn as its line, so a `+` is always a door. `--check` exits 0 clean, 1 with
findings, 2 when the file cannot be read; the codes that matter most are `W104 door-loose` (a door with no wall at either end),
`E110`/`E111` (a creature on void or off the edge) and `W120` (a room nothing leads to).

## Working live with the GM
<!-- read for: live -->

**If vtt started you** (`:agent command`): your standard input is one job -- what the GM
asked, the box they drew, its history with any feedback last -- and the exact `vtt --ctl`
lines to use (`$VTT`, `$VTT_PID`, `$VTT_JOB`, `$VTT_AGENT` hold the same). Take the job, read
the map, propose once, read your proposal back, and exit: you are started again if the GM
sends it back, so no `wait` loop. What you print goes to the GM's session log. Exit with the
job taken and nothing proposed and it goes back to `asked`; ten minutes with no request from
you and vtt stops you.

**Your edits are proposals.** They run on a copy of the map and wait, tinted on the GM's
screen, until the GM reviews them (`:review`): accepted whole or the part in a box, scrapped,
or sent back with a line of feedback. Under `:agent accept auto` each lands at once. The
channel never saves (`:w` is the GM's), and moving creatures, fog and the turn order in a
fight stay the GM's; in play mode proposals still come in and wait.

1. `vtt --ctl status`: which map, and whether edits are proposed or land at once.
2. `vtt --ctl jobs`: what the GM asked for (`:ask` over a box), each with its number, box and
   thread.
3. Answer a job:

   ```
   vtt --ctl 'job 3 take'
   vtt --ctl 'job 3 say "two ghouls, a flooded floor"'
   vtt --ctl <<'EOF'
   job 3 propose "the crypt, flooded"
   room Crypt B2:K12
   tile C3:J10 water
   token add enemy D4 "Ghoul"
   EOF
   ```

   Without a job (an idea of your own), start the request with `propose "what it is"`.
4. Read it back before the GM looks: `job 3 check`, `job 3 describe`, `job 3 dump REGION`.
   Plain `dump` reads the GM's map as it is.
5. **The verdict:** `vtt --ctl 'wait SEQ for 300'`, SEQ the `seq` that `status` gave, then
   again from each answer's `seq`. Each event is a line: its number, `job N` or `map`, and one
   word that says what happened (`accepted`, `feedback`, `scrapped`, `map changed`, ...):
   match on that word. `vtt --agent-guide events` lists them. On `feedback`, `job N propose`
   again; after `map changed` or an accept, read `changes SEQ`, not the whole map.

When the GM says "here" or "this room", ask `marked`. Put down the GM's stamps and characters
rather than drawing square by square; suggestions go on the map as notes (the players never
see them).

**Who you are:** set `VTT_AGENT` to a name of your own and `vtt --ctl` sends it with every
request (an agent vtt started has it). A job is its taker's: once you `take` it or propose one
of your own, only your name may change it. Your own requests are never told back to you, so
event numbers you read can skip. With two agents, give each a name: one `wait` a name.

`vtt --ctl` exits 0 done; 1 an error or `busy:` (the verdict on stderr; `busy` is the GM
part way through something: ask again shortly); 2 no vtt is listening -- ask the GM for
`:agent on`.

## Events
<!-- read for: events -->

`wait SEQ` answers `seq N`, then each event after SEQ a line: its number, its subject (`job N`
or `map`), then **one word that says what happened**, ended by a colon or the line.

| event | means |
|---|---|
| `job N asked` | `: [in REGION, ][to land at once: ]TEXT` -- the GM asked for a change: take it |
| `job N accepted` | `: WHAT` -- it landed, as one undo step |
| `job N part` | `: REGION: WHAT [- the rest waits]` -- the part in the GM's box landed |
| `job N scrapped` | the GM threw the proposal away |
| `job N feedback` | `: TEXT` -- yours again, with what to do instead: `job N propose` |
| `job N back` | `: for review` -- a scrapped or undone proposal brought back |
| `job N removed` | the job is gone: its number answers `no job #N` |
| `job N taken` | `: by NAME` -- another agent has it: leave it |
| `job N proposed` | `: WHAT` -- another agent's proposal waits: its squares may change under you |
| `job N undone` | `: taken back by NAME` -- read `changes` |
| `map changed` | `: WHAT` -- the GM changed the map (told once quiet for 1.5 s) |
| `map closed` | `: NAME ...` -- the map and every job on it are gone |

`wait 0` answers at once with every event still kept (the last 32). A `lost A-B` line means
more happened than is kept, and `reset N` that the vtt was started again: read `jobs` and the
map again. An event's text is cut to 239 characters (`jobs` has a job's whole text). A job
you never took can show up accepted: a change from the map's file, which the GM reviewed. The
GM undoing with `u` comes as `map changed`. vtt follows the map's changes only while an agent
is about (a `wait` held, a job open, a read in the last ten minutes); later, a change in
between is one event, `map changed: while no agent was listening`, with every region on it.

## Limits
<!-- read for: limits -->

A request is at most 65,536 bytes (a plan for `--apply`, 64,896), a line 1,023 characters, a
word 255.
One request reads at most 300,000 squares of map (a `dump` counts its region; `describe`,
`check` and a job's read-back the whole map each; `stamps` and `characters` open every saved
file: three of them a request), and its edits cover at most 524,288 squares (a `tile` or `fog
paint` its region, a `wall` its outline, a `room` its box). A plan run on a file has neither
bound. Every request, a plan on a file too, makes at most 524,288 changes (a square's ground,
an edge, a fog square, a creature or a note each). A `dump` with no region is refused on a map over 4,096 squares. Once a request's answer
has passed 2 MB its next line is refused: send it in another request. At most 16 proposals wait at
once, and the GM's kept proposals hold at most 4,194,304 squares together. On a 512x512 map send `describe` and `check` alone, and `dump` by region.

## Writing the file by hand
<!-- read for: file -->

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
