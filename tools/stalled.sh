#!/bin/sh
# perf.sh's row 'play, a phone that falls behind' once, untimed, in the
# sandbox, for the line the bench prints at its end: the most vtt held for
# the stalled watcher, the FULL that bounded it then, and the catch-ups
# (docs/V2.md, step 7; docs/PERFORMANCE.md, *A phone that falls behind*).
# The lag bound holds when the first number is at most the second.
#
#   tools/stalled.sh [LOOPS]     LOOPS: 800 by default, as perf.sh's row
set -eu
cd "$(dirname "$0")/.."
[ -x ./vtt ] || { echo "no ./vtt -- run make first" >&2; exit 1; }
DIR=$(mktemp -d /tmp/vtt-st.XXXXXX)
trap 'rm -rf "$DIR"' EXIT
. tools/sandbox.sh
sandbox_env "$DIR"
. tools/genmap.sh
MOB=$(genmap mob 40 25 0 24)
printf ':play\rjjllkkhh' > "$DIR/keys"
./vtt "$MOB" --bench "$DIR/keys" --bench-loops "${1:-800}" --size 80x24 \
    --bench-clients 4 --bench-stalled 2>&1 > /dev/null | awk '/stalled watcher/'
