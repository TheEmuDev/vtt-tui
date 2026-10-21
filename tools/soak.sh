#!/bin/sh
# Long sessions and their peak memory (docs/V2.md, *Memory: the plan*, 3;
# docs/reviews/2026-10-20-4-memory.md, T6): what grows with a session's length
# shows here and in no single test. Each run is a --bench of many loops; its
# peak is the bench's own `maxrss_kb` line; past its bound the run fails, and
# the script exits 1. Bounds are a measured peak with room (2026-10-20, the
# desktop: 5,552, 31,272, 38,292, 6,940 and 5,364 KB in the order below): raise
# one only with the reason, in the commit.
#
#   make soak            or   tools/soak.sh        (about 20 s: not part of make test)
#   SOAK_ONLY=NAME tools/soak.sh                   one run
set -eu
BIN=${BIN:-./vtt}
[ -x "$BIN" ] || { echo "no $BIN -- run make first" >&2; exit 2; }

DIR=$(mktemp -d "${SOAK_DIR:-.}/.soak.XXXXXX")
trap 'rm -rf "$DIR"' EXIT
export XDG_DATA_HOME="$DIR/xdg" XDG_CONFIG_HOME="$DIR/config" XDG_RUNTIME_DIR="$DIR/run"
export VTT_SECRET_TOOL=/nonexistent/vtt-soak/secret-tool
export VISUAL=true EDITOR=true          # a card key must not start the user's editor and wait
mkdir -p "$XDG_DATA_HOME" "$XDG_CONFIG_HOME" "$XDG_RUNTIME_DIR"
chmod 700 "$XDG_RUNTIME_DIR"
. "$(dirname "$0")/genmap.sh"

MOB=$(genmap mob 40 25 0 24)
HORDE=$(genmap horde 60 40 0 500)
HUGE=$(genmap huge 512 512 0 0)
echo 'dump all' > "$DIR/dump.ctl"
echo 'wait for 0' > "$DIR/wait.ctl"

failed=0
# soak NAME BOUND_KB LOOPS MAP KEYS [FLAGS]
soak() {
    _name=$1 _bound=$2 _loops=$3 _map=$4 _keys=$5 _extra=${6:-}
    if [ -n "${SOAK_ONLY:-}" ] && [ "$SOAK_ONLY" != "$_name" ]; then return; fi
    printf '%s' "$_keys" > "$DIR/keys"
    # shellcheck disable=SC2086
    if ! "$BIN" "$_map" --bench "$DIR/keys" --bench-loops "$_loops" $_extra > /dev/null 2> "$DIR/out"; then
        echo "  $_name: the bench failed"; tail -3 "$DIR/out"; failed=1; return
    fi
    _peak=$(awk '/^  maxrss_kb/ { print $2 }' "$DIR/out")
    if [ -z "$_peak" ]; then echo "  $_name: no maxrss_kb line"; failed=1; return; fi
    if [ "$_peak" -gt "$_bound" ]; then
        printf '  %-28s %7s KB  over its bound of %s KB  FAIL\n' "$_name" "$_peak" "$_bound"; failed=1
    else
        printf '  %-28s %7s KB  (bound %s)\n' "$_name" "$_peak" "$_bound"
    fi
}

# Not here: proposals waiting for review. The bench numbers its jobs from 1 again
# every loop (so a script's `:ask 1 remove` names the job it made), so they do
# not pile up; HEALTH 44's cap has a test of its own with sixteen waiting.
echo "peak memory of long sessions, the release vtt:"
soak "play, mixed, 4 phones"  8000 10000 "$MOB"   ':fog all 6\r:play\rf\rllllhhhh\r' "--bench-clients 4"
soak "28 whole-map fills"    40000    14 "$HUGE"  'v511l511jx511h511kv511l511jf511h511k'
soak "1,500 scene restores"  48000  1500 "$HORDE" ':play\r:scene save A\r:scene A\r'
soak "50 whole-map dumps"    10000    50 "$HUGE"  'lh' "--bench-ctl $DIR/dump.ctl"
soak "300 event waits"        8000   300 "$HUGE"  'xlxlxlxlxlxlfhfhfhfhfhfh' "--bench-ctl $DIR/wait.ctl"
exit $failed
