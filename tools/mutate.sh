#!/bin/sh
# A mutation check: does a test catch this fault? Replaces one exact string in
# a source file, rebuilds the test runner, runs the suites named, and puts the
# file back whatever happens (an interrupt included), then rebuilds again.
#
#   tools/mutate.sh FILE 'old text' 'new text' SUITE...
#
# The old text must occur exactly once (a mutation that lands twice, or
# nowhere, proves nothing). Prints "caught" with the failing checks when a
# suite fails, as it should, and "SURVIVED" when every suite still passes;
# exits 0 for caught, 1 for survived, 2 for a mutation that could not be made
# or did not compile. Each run is under `timeout` (MUTATE_TIMEOUT, default
# 300 s): a mutation that hangs counts as caught, said so. The suites' names
# are build/run-tests's (tests/run.c).
set -u
[ $# -ge 4 ] || { echo "usage: tools/mutate.sh FILE OLD NEW SUITE..." >&2; exit 2; }
file=$1 old=$2 new=$3
shift 3
[ -f "$file" ] || { echo "mutate: no file $file" >&2; exit 2; }

# The original under a fixed name: a run killed past its traps (SIGKILL)
# leaves the file mutated, and the next run finds the original and says so.
# Named from the file's real path, so `./src/x.c` and `src/x.c` are one file.
real="$(cd "$(dirname "$file")" && pwd -P)/$(basename "$file")"
keep="${TMPDIR:-/tmp}/mutate.$(printf '%s' "$real" | tr / _).orig"
if [ -e "$keep" ]; then
    echo "mutate: a killed run left $file mutated; its original is $keep (cp -p it back, then rm it)" >&2
    exit 2
fi
cp -p "$file" "$keep" || exit 2
restore() {
    # The original's text with a time newer than anything built from the
    # mutant: `cp -p` kept the old time, make took the mutant's objects for
    # current, and the next build -- the release vtt too -- carried the
    # mutation (2026-10-23: a hardening lost from ./vtt that way).
    cp "$keep" "$file" && rm -f "$keep"
    touch "$file"
    make -s test-build >/dev/null 2>&1
}
trap 'restore; exit 2' INT TERM HUP

count=$(python3 -I -c 'import sys; print(open(sys.argv[1]).read().count(sys.argv[2]))' "$file" "$old")
if [ "$count" != 1 ]; then
    echo "mutate: the old text occurs $count times in $file, not once" >&2
    rm -f "$keep"; exit 2
fi
python3 -I -c '
import sys
p, o, n = sys.argv[1:4]
s = open(p).read()
open(p, "w").write(s.replace(o, n, 1))' "$file" "$old" "$new"

if ! make -s test-build >/dev/null 2>&1; then
    echo "mutate: the mutation does not compile" >&2
    restore; exit 2
fi
out=$(timeout "${MUTATE_TIMEOUT:-300}" build/run-tests "$@" 2>&1)
rc=$?
restore
trap - INT TERM HUP

if [ $rc = 124 ]; then
    echo "caught (by a hang: the suites ran past ${MUTATE_TIMEOUT:-300} s)"; exit 0
fi
fails=$(printf '%s\n' "$out" | grep -c 'FAIL')
if [ "$fails" -gt 0 ] || [ $rc != 0 ]; then
    echo "caught: $fails failing lines"
    printf '%s\n' "$out" | grep 'FAIL' | head -5 | cut -c1-200
    exit 0
fi
echo "SURVIVED: $(printf '%s\n' "$out" | tail -1)"
exit 1
