#!/bin/sh
# ROADMAP 27's evaluations (docs/AGENTLOOP.md, part 1): an agent given a GM's
# ask and the skill, in a sandbox, and what it made graded by vtt
# (tools/agentgrade.py).
#
#   tools/agenteval.sh [--yes] [-n N] TASK... | all
#
# A task is a directory under EVAL_TASKS (tools/skill/evals/tasks): prompt.txt
# (the ask, in a GM's words), start.vtt (the map to work on) or size (a new
# map's WxH), and expect (the facts). Each of N runs (3 unless -n) writes
# EVAL_RUNS/DATE-MODEL-TASK-K/ (tools/skill/evals/runs): the verdict, the call
# log, the agent's answer, its cost and the map it left.
#
# Every run calls a model, and a model costs money or the user's plan: the
# runner says what it will run and what the last such run cost, and asks. --yes
# is the user's answer given ahead; nothing else answers for them.
#
#   EVAL_AGENT    the agent's command, run by sh from the map's directory with
#                 the prompt on stdin (default: claude -p, below); a command
#                 given here is a stand-in (tests/agenteval.sh) and is told
#                 EVAL_TASK_DIR, which the default agent never is
#   EVAL_MODEL    the model (sonnet), and its name in the run's directory
#   EVAL_BUDGET   claude's --max-budget-usd a run (2: a ceiling, not an estimate)
#   EVAL_TIMEOUT  seconds a run (900)
#
# The agent's environment is the caller's, so claude keeps its login; vtt,
# on its PATH, is a wrapper that runs the real ./vtt in a sandbox of its own
# (tools/sandbox.sh) and logs each call: its exit status, bytes out, the
# refusal's class and its words.
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
TASKS=${EVAL_TASKS:-$ROOT/tools/skill/evals/tasks}
RUNS=${EVAL_RUNS:-$ROOT/tools/skill/evals/runs}
MODEL=${EVAL_MODEL:-sonnet}
BUDGET=${EVAL_BUDGET:-2}
TIMEOUT=${EVAL_TIMEOUT:-900}
VTT=$ROOT/vtt
N=3
YES=0
usage() { echo "usage: tools/agenteval.sh [--yes] [-n N] TASK... | all" >&2; exit 2; }
while [ $# -gt 0 ]; do
    case $1 in
        --yes) YES=1 ;;
        -n) [ $# -ge 2 ] || usage; N=$2; shift ;;
        -*) usage ;;
        *) break ;;
    esac
    shift
done
[ $# -ge 1 ] || usage
case $N in ''|*[!0-9]*|0) usage ;; esac
if [ "$1" = all ]; then set -- $(ls "$TASKS"); fi
for t in "$@"; do
    [ -f "$TASKS/$t/prompt.txt" ] && [ -f "$TASKS/$t/expect" ] || { echo "agenteval: no task $t under $TASKS" >&2; exit 2; }
    [ -f "$TASKS/$t/start.vtt" ] || [ -f "$TASKS/$t/size" ] || { echo "agenteval: task $t has neither start.vtt nor size" >&2; exit 2; }
done
[ -x "$VTT" ] || { echo "agenteval: no vtt at $VTT -- make first" >&2; exit 2; }
command -v python3 >/dev/null || { echo "agenteval: the grader needs python3" >&2; exit 2; }

if [ -n "${EVAL_AGENT:-}" ]; then AGENT=$EVAL_AGENT
else
    # Claude Code with the skill in the map's directory and nothing else of the
    # user's: no settings files (--restricted), the file tools confined to the
    # map's directory, these tools alone, vtt the only command it may run, no
    # session kept. Read from `claude --help`; what it is given beyond the
    # skill is checked at the first run (docs/AGENTLOOP.md).
    AGENT="claude -p --model '$MODEL' --output-format json --no-session-persistence \
--restricted --tools Bash,Read,Write,Edit,Glob,Grep,Skill --permission-mode dontAsk \
--max-budget-usd '$BUDGET' --allowedTools 'Bash(vtt *)' Read Write Edit Glob Grep Skill"
fi

# What it will run, and what the last run of each task with this model cost.
last=0; known=1
for t in "$@"; do
    c=$(ls -d "$RUNS"/*-"$MODEL-$t"-[0-9]* 2>/dev/null | tail -1)
    if [ -n "$c" ] && [ -f "$c/cost" ]; then last=$(echo "$last $(cat "$c/cost")" | awk '{ printf "%.4f", $1 + $2 }')
    else known=0; fi
done
echo "agenteval: $# task(s) x $N run(s), model $MODEL, agent: $AGENT"
if [ "$known" = 1 ]; then echo "agenteval: the last run of each cost \$$last a run, so about \$$(echo "$last $N" | awk '{ printf "%.2f", $1 * $2 }')"
else echo "agenteval: what it costs is not known (no earlier run of every task)"; fi
if [ "$YES" = 0 ]; then
    [ -t 0 ] || { echo "agenteval: nobody to ask -- run it at a terminal, or --yes" >&2; exit 2; }
    printf 'run it? [y/N] '; read -r a
    case $a in y|Y|yes) ;; *) echo "agenteval: nothing run"; exit 0 ;; esac
fi

mkdir -p "$RUNS"
WORK=
APID=
trap '[ -n "$APID" ] && kill "$APID" 2>/dev/null && wait "$APID"; rm -rf "$WORK"' EXIT
trap 'exit 130' INT TERM
total=0; good=0
for t in "$@"; do
    T="$TASKS/$t"
    k=1
    while [ "$k" -le "$N" ]; do
        # A short path: vtt's channel socket must fit in 107 bytes.
        WORK=$(mktemp -d /tmp/vtt-eval.XXXXXX) || exit 2
        mkdir -p "$WORK/bin" "$WORK/sb" "$WORK/map/.claude/skills/vtt"
        cp "$ROOT/tools/skill/SKILL.md" "$WORK/map/.claude/skills/vtt/"
        : > "$WORK/calls.tsv"
        cat > "$WORK/bin/vtt" <<EOF
#!/bin/sh
. '$ROOT/tools/sandbox.sh'
sandbox_env '$WORK/sb' || exit 2
'$VTT' "\$@" > '$WORK/out.'\$\$ 2> '$WORK/err.'\$\$
rc=\$?
cat '$WORK/out.'\$\$; cat '$WORK/err.'\$\$ >&2
class=\$(sed -n 's/^vtt: error \\([a-z-]*\\):.*/\\1/p' '$WORK/err.'\$\$ | head -1)
printf '%s\t%s\t%s\t%s\n' "\$rc" "\$(wc -c < '$WORK/out.'\$\$ | tr -d ' ')" "\${class:--}" "\$(printf '%s' "\$*" | tr '\t\n' '  ')" >> '$WORK/calls.tsv'
rm -f '$WORK/out.'\$\$ '$WORK/err.'\$\$
exit \$rc
EOF
        chmod +x "$WORK/bin/vtt"
        { cat "$T/prompt.txt"; echo
          if [ -f "$T/start.vtt" ]; then cp "$T/start.vtt" "$WORK/map/map.vtt"
              echo "The map is map.vtt, in the current directory."
          else echo "Make the map as map.vtt in the current directory, $(cat "$T/size") squares (it is not there yet)."; fi
        } > "$WORK/prompt.txt"
        start=$(date +%s)
        # The task's directory only to a stand-in: it holds reference.txt, the
        # answer, which a model must not be pointed at.
        # In the background, so a signal to the runner stops a paid run at once
        # (the trap kills timeout, which passes it on) rather than after it.
        ( cd "$WORK/map" || exit 2
          if [ -n "${EVAL_AGENT:-}" ]; then EVAL_TASK_DIR=$T; export EVAL_TASK_DIR; fi
          PATH="$WORK/bin:$PATH"
          exec timeout "$TIMEOUT" sh -c "$AGENT" < "$WORK/prompt.txt" > "$WORK/answer" 2> "$WORK/agent.err" ) &
        APID=$!
        wait "$APID"
        rc=$?
        APID=
        R="$RUNS/$(date +%Y-%m-%d-%H%M%S)-$MODEL-$t-$k"
        mkdir -p "$R"
        cp "$WORK/prompt.txt" "$WORK/calls.tsv" "$WORK/answer" "$WORK/agent.err" "$T/expect" "$R/"
        [ -f "$WORK/map/map.vtt" ] && cp "$WORK/map/map.vtt" "$R/"
        echo "exit $rc, $(( $(date +%s) - start )) s" > "$R/agent"
        # The agent's own report of its cost, when it gave one (claude's JSON).
        python3 -c 'import json,sys
try: d = json.load(open(sys.argv[1]))
except Exception: sys.exit(0)
c = d.get("total_cost_usd") if isinstance(d, dict) else None
if c is not None: print("%.4f" % c)' "$R/answer" > "$R/cost"
        [ -s "$R/cost" ] || rm -f "$R/cost"
        python3 "$ROOT/tools/agentgrade.py" "$R" --vtt "$VTT" > "$R/verdict"
        g=$?
        [ "$g" = 2 ] && exit 2
        total=$((total + 1)); [ "$g" = 0 ] && good=$((good + 1))
        note=; [ "$rc" = 0 ] || note=", the agent exited $rc"
        [ -f "$R/cost" ] && note="$note, \$$(cat "$R/cost")"
        echo "$t $k: $(tail -1 "$R/verdict")$note -> ${R#"$ROOT"/}"
        rm -rf "$WORK"; WORK=
        k=$((k + 1))
    done
done
echo "agenteval: $good of $total runs met every fact"
