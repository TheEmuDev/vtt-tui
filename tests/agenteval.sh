#!/bin/sh
# The test of ROADMAP 27's runner and grader (docs/AGENTLOOP.md, part 1), run
# by make test: stand-in agents under tests/agenteval/tasks/ (an agent.sh
# each, no model) through tools/agenteval.sh, and each task's verdict against
# its `want`. Between them the tasks hold every kind of fact passing and
# failing, a map made, a map given and kept, and no map at all. Then the two
# refusals: a line of `expect` that is not a fact, and a run with nobody to
# ask and no --yes.
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
command -v python3 > /dev/null || { echo "agenteval: skipped (no python3 for the grader)"; exit 0; }
D=$(mktemp -d "${TMPDIR:-/tmp}/agenteval-test.XXXXXX") || exit 2
trap 'rm -rf "$D"' EXIT
fail=0
T=$ROOT/tests/agenteval/tasks
EVAL_TASKS=$T EVAL_RUNS=$D/runs EVAL_MODEL=stub EVAL_AGENT='sh "$EVAL_TASK_DIR/agent.sh"' \
    "$ROOT/tools/agenteval.sh" --yes -n 1 all > "$D/out" 2>&1 || { echo "  FAIL agenteval: the runner exited $?"; cat "$D/out"; fail=1; }
n=0
for t in $(ls "$T"); do
    v=$(ls -d "$D"/runs/*-stub-"$t"-1 2>/dev/null)
    if [ -z "$v" ]; then echo "  FAIL agenteval: task $t left no run"; fail=1; continue; fi
    if ! diff "$T/$t/want" "$v/verdict" > "$D/diff"; then
        echo "  FAIL agenteval: task $t's verdict (< want, > got):"; cat "$D/diff"; fail=1
    fi
    n=$((n + 1))
done

# Every real task can be met: its reference.txt, a plan written by hand, meets
# each fact through vtt as it is now (a request or a name vtt changed under a
# task fails here, not in a paid run).
R=$ROOT/tools/skill/evals/tasks
EVAL_RUNS=$D/ref EVAL_MODEL=ref EVAL_AGENT="sh '$ROOT/tests/agenteval/reference.sh'" \
    "$ROOT/tools/agenteval.sh" --yes -n 1 all > "$D/ref.out" 2>&1 || { echo "  FAIL agenteval: the references' run exited $?"; cat "$D/ref.out"; fail=1; }
nref=0
for t in $(ls "$R"); do
    v=$(ls -d "$D"/ref/*-ref-"$t"-1 2>/dev/null)
    if [ -z "$v" ] || ! awk 'END { exit !($1 == $3 && $1 > 0) }' "$v/verdict"; then
        echo "  FAIL agenteval: task $t's reference does not meet its facts:"; grep FAIL "$v/verdict" 2>/dev/null; fail=1
    fi
    nref=$((nref + 1))
done

# A line that is not a fact stops the grader (exit 2), never passed over.
mkdir "$D/bad"
printf 'rooms >= 1\nrooms are many\n' > "$D/bad/expect"
python3 "$ROOT/tools/agentgrade.py" "$D/bad" > "$D/bad/out" 2>&1
rc=$?
if [ "$rc" != 2 ] || ! grep -q 'expect line 2' "$D/bad/out"; then
    echo "  FAIL agenteval: a line that is not a fact: exit $rc"; cat "$D/bad/out"; fail=1
fi

# Nobody to ask and no --yes: nothing runs.
EVAL_TASKS=$T EVAL_RUNS=$D/asked EVAL_MODEL=stub EVAL_AGENT='sh "$EVAL_TASK_DIR/agent.sh"' \
    "$ROOT/tools/agenteval.sh" -n 1 made < /dev/null > "$D/asked.out" 2>&1
rc=$?
if [ "$rc" != 2 ] || [ -e "$D/asked" ]; then echo "  FAIL agenteval: run without --yes or a terminal: exit $rc"; fail=1; fi

[ "$fail" = 0 ] && echo "agenteval: $n stand-in tasks graded as wanted, $nref tasks' references meet their facts, both refusals held"
exit "$fail"
