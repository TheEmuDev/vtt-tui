#!/bin/sh
# Races that only load shows: two whole build/run-tests at once, ROUNDS times
# (twice the load a make test puts on the machine), and every failure line
# counted. A test that waits on wall time it should not, or that races a
# process it started, fails here first (docs/TESTSPEED.md, step 3).
#
#   tools/loadcheck.sh [ROUNDS]        (5 by default; make test-build first)
#
# Exit 0 when every run passed, 1 when one did not. Its output files go in a
# scratch directory of its own, removed after.
set -u
cd "$(dirname "$0")/.."
ROUNDS=${1:-5}
[ -x build/run-tests ] || { echo "no build/run-tests -- make test-build first" >&2; exit 2; }
dir=$(mktemp -d "${TMPDIR:-/tmp}/loadcheck.XXXXXX")
trap 'rm -rf "$dir"' EXIT
bad=0
r=1
while [ "$r" -le "$ROUNDS" ]; do
    build/run-tests > "$dir/a.$r" 2>&1 & p1=$!
    build/run-tests > "$dir/b.$r" 2>&1 & p2=$!
    wait "$p1" || bad=1
    wait "$p2" || bad=1
    r=$((r + 1))
done
for f in "$dir"/[ab].*; do grep -E '^  FAIL|checks,' "$f"; done | sort | uniq -c
exit "$bad"
