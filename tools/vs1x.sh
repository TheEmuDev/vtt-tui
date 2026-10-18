#!/bin/sh
# 2.0 against 1.x (docs/PERFORMANCE.md, *2.0 against 1.x*): main's binary and
# this tree's, each tools/perf.sh three times, alternating, both run from a
# directory on the repository's own disk -- a run from /tmp (tmpfs here)
# makes every row that writes a file look free. Prints the per-row medians of
# each (tools/median.py) and then the rows both have, side by side
# (tools/compare.py). Wants the machine to itself, as make perf does.
#
#   tools/vs1x.sh [REF]      REF: the 1.x commit, main by default
#   KEEP=DIR tools/vs1x.sh   also leaves old.md and new.md (the medians) in DIR: new.md is
#                            this tree's tables for PERFORMANCE.md, with no run of its own
set -eu
cd "$(dirname "$0")/.."
REF=${1:-main}
OLD=$(mktemp -d "$PWD/.vs1x.XXXXXX")     # absolute: the old half runs from its worktree
trap 'git worktree remove --force "$OLD/tree" >/dev/null 2>&1 || true; rm -rf "$OLD"' EXIT INT TERM
git worktree add -f "$OLD/tree" "$REF" >/dev/null 2>&1
make -s -C "$OLD/tree" >/dev/null
make -s >/dev/null
for r in 1 2 3; do
    timeout 900 ./tools/perf.sh > "$OLD/new-$r.log" 2>&1
    ( cd "$OLD/tree" && timeout 900 ./tools/perf.sh > "$OLD/old-$r.log" 2>&1 )
done
python3 tools/median.py "$OLD/old-1.log" "$OLD/old-2.log" "$OLD/old-3.log" > "$OLD/old.md"
python3 tools/median.py "$OLD/new-1.log" "$OLD/new-2.log" "$OLD/new-3.log" > "$OLD/new.md"
[ -n "${KEEP:-}" ] && mkdir -p "$KEEP" && cp "$OLD/old.md" "$OLD/new.md" "$KEEP/"
echo "1.x is $(git rev-parse --short "$REF"), 2.0 is $(git rev-parse --short HEAD)$(git diff --quiet || echo ' plus uncommitted changes')"
python3 tools/compare.py "$OLD/old.md" "$OLD/new.md"
