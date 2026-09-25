#!/bin/sh
# fog.sight per fog scenario: what the recompute costs a keystroke, which
# tools/perf.sh cannot show -- its zone table keeps only each zone's worst
# scenario. Median of three runs a row, p50 and p99 of every recompute.
#
#   tools/sight.sh [BIN]
#
# The fixtures are perf.sh's own, read out of it, so the two cannot drift.
set -eu
BIN=${1:-./vtt}
[ -x "$BIN" ] || { echo "no $BIN -- run make first" >&2; exit 1; }
DIR=$(mktemp -d)
trap 'rm -rf "$DIR"' EXIT
export XDG_DATA_HOME="$DIR/xdg"
mkdir -p "$XDG_DATA_HOME"

sed -n '/^genmap() {/,/^}/p' tools/perf.sh > "$DIR/fixtures.sh"
sed -n '/^WARREN=/,/> "\$WARREN"/p' tools/perf.sh >> "$DIR/fixtures.sh"
# shellcheck disable=SC1091
. "$DIR/fixtures.sh"
MOB=$(genmap mob 40 25 0 24)

row() {
    _label=$1 _map=$2 _keys=$3
    for _i in 1 2 3; do
        printf '%s' "$_keys" > "$DIR/keys"
        cp "$_map" "$DIR/m.vtt"
        "$BIN" "$DIR/m.vtt" --bench "$DIR/keys" --bench-loops 400 --size 80x24 \
            --trace "$DIR/t.json" > /dev/null 2>&1
        python3 - "$DIR/t.json" <<'PY'
import json, sys
d = sorted(e["dur"] for e in json.load(open(sys.argv[1]))["traceEvents"]
           if e.get("ph") == "X" and e["name"] == "fog.sight")
print("%.1f %.1f %d" % (d[len(d) // 2], d[int(len(d) * .99)], len(d)))
PY
    done | sort -n | sed -n 2p |
        awk -v l="$_label" '{ printf "| %-22s | %7.1fus | %7.1fus | %5d |\n", l, $1, $2, $3 }'
}

echo '| scenario               | sight p50 | sight p99 | calls |'
echo '|------------------------|-----------|-----------|-------|'
row "fog sight"         "$MOB"    ':fog all 6\r:play\rf\rllllhhhh\r'
row "fog lantern"       "$MOB"    ':fog all 6\r:fog All memory off\r:play\rf\rllllhhhh\r'
row "fog reveal 12"     "$MOB"    ':fog all 12\r:play\rf\rllllhhhh\r'
row "fog full rebuild"  "$MOB"    ':fog all 6\r:play\r:fog All 5\r:fog All 6\r'
row "fog warren"        "$WARREN" ':fog all 6\r:play\rf\rllllhhhh\r'
row "fog warren reveal 12" "$WARREN" ':fog all 12\r:play\rf\rllllhhhh\r'
row "fog warren, party" "$WARREN" ':fog all 6\r:play\r:H12\rvllllllllllll\rjjkk\r'
row "fog door"          "$WARREN" ':fog all 6\r:play\r:F3\roo'
