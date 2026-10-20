# The Claude API agent (ROADMAP item 26)

> **Status: a plan, written 2026-10-19, waiting for the user's sign-off.** Nothing of it is
> built. *Decisions for the user* at the end are the questions open.

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

Looked for in this order, read by the agent process only:

1. `ANTHROPIC_API_KEY` in the environment vtt was started with (the child inherits it);
2. `$XDG_CONFIG_HOME/vtt/anthropic-key` (`~/.config/vtt/anthropic-key`): a regular file of
   the user's, readable by nobody else. A file with any group or other bit set is refused,
   with the `chmod 600` that fixes it; so is a key with a character outside `A-Z a-z 0-9 -
   _` (which also keeps the config line from being written into).

`:agent claude` checks there is one (the variable set, or the file there with the right
mode; it does not read the file) and refuses with where to put it when there is none. A key
the API turns down (401) ends the run with `job N say "the API key was refused (401)"`, and
an exit status vtt reports. OAuth profiles (`ant auth login`) are not read: a key or
nothing.

## Errors, limits, cost

| what comes back | what the agent does |
|---|---|
| 429, 500, 529, or curl could not connect | waits `retry-after` (else 2, 4, 8 s), three tries, then ends the run saying why |
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

## Code

| where | what |
|---|---|
| `src/apiagent.c/h` (new) | the loop: request built (writer + the raw assistant turns), curl run (fork, pipes on 0, 1 and 3, `waitpid`), the answer's headers and body read, tool calls run, usage and price, the log lines |
| `src/ctl.c` | `ctl_ask_pid`: one request to one vtt, the answer returned in a buffer -- taken out of `ctl_client_main`, which then calls it |
| `src/json.c/h` | `JsonVal.at`/`.len`: each value's span in the text (additive; the reader's other users ignore it) |
| `src/app_agent.c`, `app_cmd.c` | `:agent claude [MODEL] [effort E]`: the key and curl checked, then the agent command set |
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
