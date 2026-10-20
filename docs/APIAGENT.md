# The Claude API agent (ROADMAP item 26)

> **Status: built 2026-10-19 to 20** (887f306, cbd4c99, 46395d8, b98d8c7/a30a55e, e24b7cd,
> ab97c33), from the plan signed off 2026-10-19 ("yes to the keyring, go with
> recommendations"). A design record: what is true now is the README (*Working with AI
> agents*) and the files' rows in docs/CODEMAP.md; *As built* at the end says where the code
> left the plan. Owed: the Fable review, the live check (it spends money: the user's go), and
> the timings (PERFORMANCE.md, also with the user's go).

An agent option that calls the Claude API itself, beside `:agent command CMD`: the GM types
`:agent claude`, and every `:ask` (and every send-back) is answered by Claude through the
API, with no Claude Code or other program installed. The user has API credits ($100,
2026-10-17) to spend on it.

## What it is

**vtt runs itself as the agent.** `:agent claude [MODEL]` sets the agent command to this
vtt's own path with `--claude-agent` (and the model, if one is named), so everything
`:agent command` already does is reused unchanged: the fork and `setsid`, the job prompt
down stdin, `VTT`/`VTT_PID`/`VTT_JOB`/`VTT_AGENT` in the environment, the output to the
session log, the reaping, the ten minutes of silence before vtt stops it, a job left taken
going back to `asked` with how the run ended, and a send-back starting it again. The new
code is the agent loop in that child process; the app gains only the `:agent claude`
command.

**The loop** (`src/apiagent.c`, `apiagent_main` from `main.c`):

1. Read the job prompt from stdin (`app_agent_prompt`'s text, as any agent gets).
2. Send the Messages API a request: the system prompt, one tool, the job prompt as the
   first user message.
3. While the answer stops for `tool_use`: run each call's request on the channel as
   `job-N`, append the answer as a `tool_result` (with `is_error` when the verdict is not
   `ok`), and send again.
4. On `end_turn`: print the summary line and exit 0. vtt does the rest: a proposal made is
   waiting for the GM's review; none made, and the job goes back to `asked` as for any
   agent.

**The one tool**, `vtt`: input `{request: string}` (strict, no other fields), the text of a
channel request -- what would follow `vtt --ctl`, one line or several. It covers everything
an agent does: `job N take`, `dump`, `describe`, `check`, `changes`, `job N propose` with
its edits, `job N dump`/`check` to read the proposal back. The answer goes back verbatim,
cut at 64 KB with a line saying so (a read that big should have named a smaller region). A
request is sent with `agent job-N` as its first line, so the channel's own rules hold: a
line `agent X` from the model later in the request is refused (`rq_agent`), and undo is
only its own.

**The system prompt:** a short role text (you are vtt's built-in agent for job N; your one
tool sends a channel request as `job-N`; the job's text shows shell lines -- send only the
request part; take the job, read the box, propose once, read it back, then end your turn
with one line for the GM; never `wait`), then the agents' guide itself, `AGENT_GUIDE` (item
25: the guide of this binary, 35 KB, about 10k tokens), with a cache breakpoint after it.
The tool list and the system prompt never change within a run or between runs of one vtt,
so the guide is written to the cache once and read from it on every later call.

**The request:** `model` (default below), `max_tokens` 16000 (not streamed), `thinking`
adaptive (the default on the current models; display omitted), `output_config.effort`
(`medium` unless the GM names another), top-level `cache_control` for the growing
conversation, and `fallbacks: "default"` with the `server-side-fallback-2026-07-01` beta on
the models that take it (Opus 5.5, Sonnet 5.5, Fable 5.1): a request a safety classifier
declines is re-run on a fallback model inside the same call, rather than ending the job.

**History is append-only, byte for byte.** Each assistant turn's `content` array goes back
into the next request exactly as it came, copied from the response's bytes, never parsed
and written again: the current models check that thinking blocks come back unchanged
(preserved thinking), and a `fallback` block or a tool input's number must not be
re-encoded. For that, `json_parse` records each value's place in the text (two fields on
`JsonVal`: where it starts, how long it is). The user and tool-result turns are written
with the JSON writer.

## HTTPS without a library: curl, one process a call

No TLS goes in the binary (no dependencies beyond libc/POSIX). Each API call is one `curl`
process, run like this:

```
curl -sS -i --config /dev/fd/3 --data-binary @- --max-time 300 --connect-timeout 20 \
     https://api.anthropic.com/v1/messages
```

- the body goes down stdin; the answer, headers first (`-i`), comes back on stdout: the
  status line and `retry-after` are read from the headers, then the body is parsed;
- **the key goes down a pipe on fd 3** as one config line (`header = "x-api-key: ..."`),
  with the `anthropic-version` and `anthropic-beta` headers. It is never on curl's command
  line (which `ps` shows to anyone), never in a file vtt writes, never in the request body,
  the session log or the trace below;
- `curl` not on `PATH` is found out when `:agent claude` is typed, and refused there with a
  line saying so.

`VTT_CLAUDE_CURL` names another program to run in curl's place, which is how the tests run
the loop with no network and no money (below).

## The key

Both ways are supported (the user, 2026-10-19): the environment variable, and typing the key
into vtt. A typed key goes to the **system keyring**, not a file and not the environment:

| where | how | when |
|---|---|---|
| `ANTHROPIC_API_KEY` in the environment vtt was started with | read by the agent process, which inherits it | always wins when set |
| the keyring (Linux: the Secret Service -- GNOME Keyring, KWallet -- through `secret-tool`) | stored encrypted, unlocked with your login, as `service vtt account anthropic-api-key` | where `secret-tool` and a Secret Service are there (this machine: GNOME Keyring) |
| `$XDG_CONFIG_HOME/vtt/anthropic-key`, mode 600 | a file only you can read | only where there is no keyring (a machine over SSH, a desktop without one); the Options screen says it is a file |

- **Typing it in** (the Options screen's API key row, or `:agent key`): a prompt whose
  characters show as `*`. vtt hands the key to `secret-tool store` down a pipe (never on a
  command line, which `ps` shows) with no newline after it (`secret-tool` stores all it is
  piped, a newline too), then wipes its copy. With no keyring, it writes the file
  instead: a temporary file of mode 600 and a `rename`, never readable by others even for a
  moment. A key with a character outside `A-Z a-z 0-9 - _` is refused before it is stored.
- **Reading it:** only the Claude agent process reads the key, at its start: the variable,
  else `secret-tool lookup`, else the file (a file with any group or other bit set is
  refused, with the `chmod 600` that fixes it). It gives the key to curl down the fd-3 pipe
  (*HTTPS without a library*) and to nothing else.
- **Never in the environment from vtt:** vtt does not set `ANTHROPIC_API_KEY`, so agents
  started with `:agent command` (Claude Code, a script) get the environment you started vtt
  with, as today, and never a key typed into vtt (the user, 2026-10-19: only the Claude
  agent). That matters for Claude Code: given `ANTHROPIC_API_KEY` it bills the key's API
  credits instead of the subscription. A variable you exported yourself is yours, and they
  see it as they do now.
- **What vtt shows:** where the key in use comes from (`environment`, `keyring`, `file`, or
  `none`) and its last four characters, never more. When the variable is set, a key typed
  in is stored but not used, and the row says so.
- **Removing it:** `d` on the row, or `:agent key remove`, asks, then runs
  `secret-tool clear` (or removes the file). A variable is yours to unset; vtt says so.
- `:agent claude` checks a key is there (a lookup, the copy wiped) and refuses with how to
  give one when there is none. A key the API turns down (401) ends the run with
  `job N say "the API key was refused (401)"`, and an exit status vtt reports. OAuth
  profiles (`ant auth login`) are not read.
- A locked keyring may ask for your password in a window of its own (GNOME's prompt); with
  no window to ask in, the lookup fails and the agent says the keyring is locked.

## Errors, limits, cost

| what comes back | what the agent does |
|---|---|
| 429, 500, 502, 503, 529, or curl could not reach the API (exits 5, 6, 7, 35, 52, 55, 56) | waits `retry-after` in seconds (else, or for a date, 2, 4, 8 s) and sends it again, up to three times (four attempts), then ends the run saying why |
| curl timed out (exit 28) | ends the run at once: the API may have answered, and billed, after curl gave up, so it is never sent again |
| 400, 401, 403, 404, 413 | ends the run at once: the API's message on the GM's line (`job N say`), the status in the log |
| `stop_reason: refusal` (after any fallback) | ends the run: "Claude declined this job (CATEGORY)" |
| `stop_reason: max_tokens` | ends the run: "the answer ran past 16,000 tokens" |
| more than 25 calls in one run | ends the run: "25 calls and not done" |
| spend past the job's cap ($1.00 by default) | ends the run, saying what it spent |

A run's cost is reckoned from each answer's `usage` (input, cache write, cache read,
output) and a price table for the models it knows; the table is a guard and a report, not a
bill, and says so where it is printed. Each call adds a line to the session log
(`claude: call 3: 1.2k in, 15.4k cached, 0.8k out`) and the run ends with one
(`claude: job 4 done: 7 calls, 9.1k out, about $0.21, claude-opus-5-5`), also appended to
`$XDG_DATA_HOME/vtt/claude-usage.tsv` (date, job, model, calls, tokens of each kind,
dollars), so the credits can be watched.

**What a job should cost** (an estimate, to be measured): the guide about 11k tokens, written
once a run ($0.06 on Opus 5.5) and read at $0.20 a million after; a job of seven or eight
calls, each answer a few hundred to two thousand tokens of thinking and a request, about
10-15k output tokens ($0.20-0.30 on Opus, half on Sonnet 5.5); the growing history mostly
read from the cache. About **$0.40 a job on Opus 5.5 at medium effort, $0.20 on Sonnet
5.5**: some 250 jobs of the $100 on Opus. The live runs measure it.

`VTT_CLAUDE_TRACE=DIR` writes each request's and answer's body (never a header) for
debugging; off unless set.

## What the GM sees and types

- `:agent claude` (Opus 5.5), `:agent claude sonnet`, `... haiku`, `... fable`, or a full
  model id; `effort low|medium|high` after it. The command line it sets is shown as
  `:agent command` shows one. `:agent command off`/`on` work on it unchanged.
- An `:ask` is then answered as by any agent: `#N taken`, the box tinted, the proposal to
  review; the summary line in the session log; a failure on the GM's line.
- README *Working with AI agents* gets a section: the key, the command, the models and what
  they cost, the usage file. `src/keys.c`'s `?` page gets `:agent claude`.

## Settings: the Options screen and the `:agent` commands

The user asked (2026-10-19) for both: an options menu on the landing screen where the
agent is set up, and commands in a map that change the same settings. One set of settings
serves both, saved so it holds from one session to the next.

**Where they live:** `$XDG_CONFIG_HOME/vtt/settings` (`~/.config/vtt/settings`), a line a
setting, `name value`. vtt has no settings file today: `:agent command CMD` and the rest
last a session. The file is read once at start-up and written whole (a temporary file and
a `rename`) each time a setting changes. A line it does not know is kept as it is and
named once on the status line, never dropped; a value it cannot read leaves that setting
at its default and says so. `VTT_AGENT_COMMAND` and `--agent` still win for the session
they start (and are not saved). New code, `src/settings.c/h`: the `Settings` struct, read,
write, each setting's name, kinds and default. The app holds one (`App.settings`).

**What is in them:**

| setting | values | default |
|---|---|---|
| agent | `unset`, `off`, `command`, `claude`: what an `:ask` starts (*An `:ask` with no agent chosen*) | `unset` |
| command | the shell line for `command` | none |
| model | `opus`, `sonnet`, `haiku`, `fable` (the current model of each), or a full model id | `opus` |
| effort | `low`, `medium`, `high` | `medium` |
| cap | dollars a job, the spend cap | `1.00` |
| calls | API calls a job | `25` |
| fallbacks | `on`, `off` | `on` |
| channel | `on` or `off` at start-up (what `--agent` does for one session) | `off` |
| accept | `review` or `auto` at start-up | `review` |

The key is not a setting: it is kept in the keyring (or its own file), never in the
settings file (*The key*).

**The Options screen** (the landing menu becomes Open Map, New Map, **Options**, Quit): a
list of the settings above, each a row with its value; `j`/`k` move, `Enter` changes the
row (a choice goes to the next value, `h`/`l` step back and forward through it; a number or
the command line opens a prompt with the value in it), `Esc` goes back. Two more rows:

- **API key:** where the key in use comes from (`environment`, `keyring`, `file`, `none`)
  and its last four characters. `Enter` opens the hidden prompt and stores the key as *The
  key* says (the keyring, else the file); `d` removes it after a yes. The key is never
  drawn, logged or put on the status line.
- **Spent so far:** read from the usage file when the screen opens: dollars and runs, all
  time and this month. Nothing to change; the credits at a glance.

Rows that do not apply are shown dim (the model and the caps when the agent is `command`;
the command when it is `claude`). A change is saved at once and says so on the status
line.

**The commands in a map** change the same settings, save them, and take effect on the next
`:ask` (a run already going keeps what it started with):

| command | does |
|---|---|
| `:agent` | what it says today, plus the settings in one line: `agent claude: opus, medium effort, $1.00 and 25 calls a job, fallbacks on` |
| `:agent claude [MODEL]` | the agent is the Claude API one (the key and curl checked first) |
| `:agent command CMD` | the agent is CMD (as today, now saved) |
| `:agent command off` / `on` | an `:ask` starts nothing / starts the agent again, whichever kind (as today, now saved: `agent off`, or back) |
| `:agent model MODEL`, `:agent effort E`, `:agent cap 2.50`, `:agent calls 30`, `:agent fallbacks on\|off` | one setting each |
| `:agent key` | the same hidden prompt as the Options screen's; `:agent key remove` takes the key away (asked first) |
| `:agent on` / `off`, `:agent accept auto\|review` | this session only, as today; their start-up values are the Options screen's `channel` and `accept` |

All of them are GM-only messages (`app_set_status_gm`), as every `:agent` line is. The
hidden key prompt is a prompt, so it is GM-only and never on the players' screens
(`app_view_differs`), and its text is wiped from memory when it closes.

**The agent command line is built from the settings** each time an `:ask` starts one:
`'vtt' --claude-agent --model claude-opus-5-5 --effort medium --cap 1.00 --calls 25`, so the
agent process never reads the settings file, and what a run used is in the session log.

## An `:ask` with no agent chosen

The user asked (2026-10-19) what happens when the GM asks before choosing how. Today, with
no agent command, `:ask` says `#3 asked: ...` and the job waits for an agent that comes to
the channel by itself; if none is listening (or the channel is off) nothing ever comes and
nothing says so. The plan's default (`agent off`) would make that every new GM's first
`:ask`. So the `agent` setting has a fourth value, **`unset`** (the default: never chosen),
apart from `off` (chosen: nothing is started), and every `:ask` says what will happen to
it:

| the `agent` setting | what an `:ask` does |
|---|---|
| `unset` | asks the GM once, with the choice below; the job is made either way and waits for the answer |
| `claude`, with a key and curl | starts the Claude agent (as planned) |
| `claude`, no key or no curl | the job waits; the GM's line says why and how to fix it (`#3 asked - the Claude agent has no API key: :agent key, or Options`) |
| `command` | starts the command (as today) |
| `off`, an agent listening on the channel | the job waits for it: `#3 asked - for the agent on the channel (job-1 is waiting for work)` |
| `off`, nobody listening, channel on | `#3 asked - no agent is set to start and none is listening: :agent claude, :agent command CMD, or Options` |
| `off`, channel off | the same, and `:agent on` lets one come by itself |

"Listening" is what the channel knows: a `wait` held now, or a request from an agent in the
last ten minutes (the deadline's window); nothing is guessed.

**The choice** (on the first `:ask` while `unset`): a small GM-only box over the map, as the
review's is, which never reaches the phones (`a->view`, `app_view_differs`):

```
 How should agents answer :ask?
   c  the Claude API agent      key: keyring ...ab12      (or: no key yet - c asks for it)
   m  a command (Claude Code, a script...)
   w  wait for an agent that comes to the channel by itself
   o  the Options screen
   esc  not now: the job waits, and the next :ask asks again
```

`c` saves `agent claude` and starts the agent on the waiting job. When no key is found (no
variable, nothing in the keyring or the file), `c` first opens the hidden key prompt, the
same one as Options' and `:agent key`: the key typed is checked for its characters (a bad
one is refused and the prompt stays open, saying why), stored in the keyring (the file
where there is none), and then the agent starts. `Esc` in the key prompt goes back to the
box with nothing saved, and the job still waits. A key that is stored but wrong is found
out at the first call (401): the run ends, the GM's line says the key was refused and that
`:agent key` replaces it; `m` opens a prompt for the command line, then saves and starts it; `w`
saves `off`, switches the channel on, and says so; `o` opens Options (in a map: the commands
it lists on its line, since Options is the landing screen's). The choice is saved, so it is
asked once, not every session; `:agent` and Options change it after.

## Code

| where | what |
|---|---|
| `src/apiagent.c/h` (new) | the loop: request built (writer + the raw assistant turns), curl run (fork, pipes on 0, 1 and 3, `waitpid`), the answer's headers and body read, tool calls run, usage and price, the log lines |
| `src/ctl.c` | `ctl_ask_pid`: one request to one vtt, the answer returned in a buffer -- taken out of `ctl_client_main`, which then calls it |
| `src/json.c/h` | `JsonVal.at`/`.len`: each value's span in the text (additive; the reader's other users ignore it) |
| `src/settings.c/h` (new) | the settings: the struct, the file read and written, each setting's values and default |
| `src/app_agent.c`, `app_ctl.c` | the `:agent` commands above; the agent command line built from the settings at each start |
| `src/app_browser.c`, `app_draw.c`, `app.c` | the menu's Options row; the Options screen (`SCREEN_OPTIONS`): its keys, its rows, the hidden key prompt (`PROMPT_API_KEY`), the usage summary |
| `src/keys.c` | the Options screen's key table; `:agent`'s new words on the `?` page; the choice box's keys |
| `src/app_job.c` | `:ask` says what will happen to the job (the table above); the choice box when `unset` |
| `src/main.c` | `--claude-agent [--model M] [--effort E]`: `apiagent_main`, before any terminal |
| tests | suite `apiagent` (below); `make fuzz-json` with the spans |

It reshapes no struct the app depends on (two fields added to `JsonVal`) and changes one
shared signature's home (`ctl_client_main` keeps its own); it is planned because it adds a
mode, a process boundary, and something that spends money.

## Tests (no network, no money)

`VTT_CLAUDE_CURL` points at a test script that plays the API: it reads fd 3 (checks the key
line is there and right, records only that it was), records each body it is sent and its
own command line, and answers from a script of canned answers. The suite runs a real
`./vtt` with the channel on in a sandbox, as `agentcmd` does today:

1. a job done: `tool_use` `job 7 take`, then `dump`, then `job 7 propose` with edits, then
   `end_turn` -- the job ends proposed, the proposal is what the edits say;
2. the second request carries the first answer's `content` byte for byte (a thinking block
   with a signature, a number written `1.50`), the tool results in order, `is_error` on a
   refused request, the cache markers, the model and the effort;
3. the key is in no recorded command line, no body, no trace, not in the session log;
4. 429 with `retry-after: 1`, then 200: one retry; 401: the run ends, the GM's line says
   the key was refused, the job goes back to `asked`; `refusal`; `max_tokens`; the
   25-call cap; the spend cap;
5. the key: the variable wins over the file; a file with mode 644 refused with the chmod; a
   key with a quote or a newline in it refused; `:agent claude` with no key and with no
   curl refused, saying which;
6. an `agent X` line from the model is refused by the channel;
7. `json_parse`'s spans: every value's span re-parses to the same value (and the fuzz
   target checks it).
8. the settings: written and read back the same; a line it does not know kept and named;
   a bad value left at the default and named; `VTT_AGENT_COMMAND` and `--agent` win for the
   session and are not saved; each `:agent` command changes the setting, the file and the
   next agent's command line; a run already going keeps its own;
9. the Options screen: a golden frame of it (with and without a key, a dim row), each row's
   change through its keys, the usage summary from a usage file;
10. the key prompt: its frame shows only `*`; the key is in no frame, the players' frame,
   the session log or the status line; stored through a stand-in for `secret-tool`
   (`VTT_SECRET_TOOL`, as `VTT_CLAUDE_CURL` is for curl) that records it got the key on
   stdin and on no command line; with no keyring, the file is mode 600 from the first byte;
   the variable wins over both; a `:agent command` agent's environment has no key from vtt;
   `d` and `:agent key remove` ask, then remove it.
11. an `:ask` in each row of *An `:ask` with no agent chosen*: the line it says; the choice
   box on `unset` (a golden frame; never in the players' frame); `c` with and without a key
   (the key prompt from the box: stored, then the agent started; a bad key refused with the
   prompt kept; `Esc` back to the box, nothing saved),
   `m`, `w`, `esc` (asked again next time), each saving what it says; "listening" from a held
   `wait` and from a recent request, and not from an old one.

**Then one live check, with the user's go** (it spends money): three jobs on a small map --
a room from nothing, a change inside a box, a send-back with feedback -- each judged by
`job N check` clean and `describe` having what was asked, with the tokens, seconds and
dollars of each. This is also the comparison owed from item 25: the same three jobs given
to the agent with the guide in its system prompt and with only the skill's text, to see
what the guide buys.

## Performance considerations

The work is the network's and the model's: a call is seconds; nothing local comes near it.

| path | speed of light | the design | the gap |
|---|---|---|---|
| `:agent claude` typed | a `stat` of the key file (0.9 µs) and a search of `PATH` for curl (a `stat` a directory, about ten) | the same, then `:agent command`'s path | none |
| starting the run | `:agent command`'s: one `fork` and `exec` (92.5 µs at p50, PERFORMANCE.md) | unchanged | none |
| a call, locally | writing the request: its bytes (the guide 35 KB, the history up to a few hundred KB) at memory speed, tens of µs; reading the answer, under 100 KB | the same, plus one `fork`/`exec` of curl (about 1-2 ms) and curl's TLS handshake (about 100 ms on the network) | the handshake, once a call: no connection is kept between calls because each is its own curl. About 1 s over a job of eight calls against 30-90 s of model time; accepted. A long-lived helper holding the connection is the fix, if the live check says it matters |
| a call, in tokens | the guide read from the cache (11k tokens at 0.05x), the history read from the cache, only the newest turn written | that | measured: the cache's share of input tokens per call goes in the usage log and the table below |
| the channel's work for the agent | as for any agent's requests | unchanged | none |
| the Options screen drawn | its dozen rows: a few hundred bytes to the terminal | a list drawn as the menu is | none; a `PROF_ZONE` and a `tools/perf.sh` scenario, as every new drawing path gets |
| a setting changed | the file's few hundred bytes written and made durable: 2.0 ms (*Speed of light*, a durable save) | the same: write, `fsync`, `rename` | none; on a key the GM pressed to change a setting |
| the usage summary | reading the usage file: a line a run, 100 bytes; a thousand runs is 100 KB, about 30 µs to copy | read once when the Options screen opens | none; never per frame |
| start-up | one `open` and read of the settings file | that | none |

Nothing is added to a frame, a keystroke or the loop: the agent is another process, and the
app's side of it is `:agent command`'s. No `PROF_ZONE` is owed (no drawing or key path);
the agent's own numbers are its usage log. Once built, the live check's calls, seconds,
tokens (cached and not) and dollars a job go in PERFORMANCE.md, *The Claude API agent*,
against the estimate above.

## Decisions for the user

1. **The shape: vtt as its own agent** (`--claude-agent`, picked with `:agent claude`) --
   recommended: it reuses the whole of `:agent command`, and the app gains one command.
   The other shapes: a script in tools/ (needs a JSON tool such as `jq` or Python: a
   dependency), or the calls made inside the app's loop (non-blocking curl children polled
   by the loop: far more code in the app, for nothing the GM would see).
2. **The default model: Opus 5.5** (the current Opus; about $0.40 a job) -- recommended --
   or Sonnet 5.5 (about $0.20). Either way `:agent claude sonnet` and the rest name
   another.
3. **The caps:** $1.00 and 25 calls a job by default (`:agent claude ... cap 2.50` to
   change the money). Recommended.
4. **Fallbacks on** for a request a classifier declines (re-run on a fallback model inside
   the call, billed at that model's rates). Recommended; off with `fallbacks off`.
5. **The usage file** `~/.local/share/vtt/claude-usage.tsv`, a line a run. Recommended.
6. **Which settings are saved:** the agent's (kind, command, model, effort, caps,
   fallbacks) are saved by both the screen and the commands -- recommended. `:agent on/off`
   and `:agent accept` stay this-session, with their start-up values on the Options screen,
   so a GM who switched to `accept auto` for one fight does not find it on next week
   without having chosen it there. The other way: every `:agent` command saves.
7. **Settled 2026-10-19 (the user):** both the variable and a key typed into vtt, the typed
   one only for the Claude agent. How a typed key is kept is the proposal in *The key*: the
   system keyring, a mode-600 file only where there is none -- waiting for the user's yes.
8. **The Options screen in a map too** (`:options` opening the same screen over the map,
   GM-only) -- not recommended now: the commands do the same, and a screen over a map
   brings the players'-view rules to every row of it. Easy to add later.

## As built

Where the code left the plan, and what the build found:

- **`:agent claude` with no key asks for one** (the hidden prompt) instead of refusing: the
  choice box's `c` does the same, and one way in is simpler than two. With no curl it
  refuses, saying so.
- **The choice box has no `o` row.** `:ask` needs a map open, and Options is the landing
  screen's (decision 8: not over a map), so `o` could only have printed the commands; the
  footer and every `:ask` line name `:agent claude`, `:agent command` and Options instead.
  The box takes `c m w` or `1-3`.
- **"Listening" is what the channel records:** a `wait` held now (by the waiter's name), or
  one answered in the last ten minutes (`World.agent_seen_ms`). Other requests leave no time
  behind them, so they do not count.
- **A locked keyring is not told apart:** `secret-tool lookup` fails alike for "none" and
  "locked with nobody to unlock it", so both read as no key (GNOME unlocks it in a window of
  its own when there is one).
- **`app_init` starts with agent `off`, `main` with the file's `unset`:** a test or a bench
  has no GM to ask, so its `:ask` waits for a channel agent as before; a GM who never chose
  is asked.
- **One line for an `:ask`:** the job's own line (`#3 asked in C3:F5: ...`) and why nothing
  started are said together, so the session log has one line, as before.
- **Any agent's last word is kept:** `run_ended` puts an agent's last `job N say` line on
  the GM's when it stops with nothing proposed (how the Claude agent says a key was
  refused); every agent gets it.
- **`:agent command off`/`on` pause and resume whichever agent is chosen** (`paused` in the
  settings), the Claude agent too.
- **The agent command buffer** is `AGENT_LINE_MAX` (4400): the Claude agent's line has vtt's
  own path in it. A command typed in is still at most 255 characters.
- **Found while building:** the JSON reader's number scan overflowed a 64-byte stack buffer
  on an exponent after 63 digits (the fuzz target's span check found it on its first
  minute; fixed in 887f306, the input kept as a seed). The test sandbox, `tools/oracle.sh`
  and `tools/perf.sh` now give vtt its own `XDG_CONFIG_HOME` and no keyring tool, so no
  suite or measurement reads the user's settings or key.
- **Shared code:** `file_write_whole` (util.c) is the one temp-flush-rename for everything but
  the map's own save (import.c moved onto it); `stream_read` moved from ctl.c to util.c;
  `ctl_ask_pid` came out of `ctl_client_main`; the test pumps for started agents moved to
  harness.c.
- **The review (Fable, 2026-10-20), eight findings, all reproduced and fixed:** the choice
  box looked the key up every frame (now once, when it opens); the test that a command agent
  gets no key checked the test's own environment (now the agent's, dumped; a planted leak
  fails it); a curl timeout was sent again up to three times, each perhaps billed (now never,
  and said; only exits that never reached the API are retried); the spend cap was silently off
  for a model vtt has no price for (now said at the run's start and on the Options row);
  `:agent key remove` with the variable set left a stored key (now removes it); a `job N say`
  past a channel word was refused (now cut to fit; the chmod message names the path once); an
  HTTP-date `Retry-After` read as 0 s (now the backoff); Options ran off a short terminal (now
  it scrolls). Of the tests listed above, two took another form: the Options golden frame is
  the no-key one only (the key row's other values are checked as text), and "a run already
  going keeps its own" holds by construction (the settings reach a run as its command line
  when it starts) rather than by a test.

