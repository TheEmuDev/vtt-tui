#!/bin/sh
# What the phone page's own code costs per frame: each scenario is played
# headless with `vtt --bench --bench-record`, which saves the stream a phone
# would be sent, and that stream is replayed through the page as the binary
# serves it, in node (tools/pagebench.js). Prints a table for
# docs/PERFORMANCE.md's "The phone page"; publish the median of three runs,
# as for tools/perf.sh.
#
#   tools/pagebench.sh                 every scenario
#   VIEW=1280x720@1 tools/pagebench.sh another screen (default 915x412@2.625)
#   KEEP=1 tools/pagebench.sh          keep the recordings and the page in .pagebench.*
#   PAGE=page.html tools/pagebench.sh  replay through another page: an A/B of
#                                      a change before it is embedded
#
# The columns: `decode` is feed() (records into the cell arrays), `present`
# is the frame put together (`copy`, blitRow with its tiles, is most of it),
# `px` the pixels a frame pushes to the canvas, which the browser then pays
# for natively -- the one cost here that node cannot time. A real browser's
# share is tools/pageprobe.js's to measure.
set -eu
cd "$(dirname "$0")/.."

BIN=${BIN:-./vtt}
LOOPS=${LOOPS:-40}           # bench loops recorded per scenario
REPLAY=${REPLAY:-6}          # replays of the recording; the first warms up
[ -x "$BIN" ] || { echo "no $BIN -- run make first" >&2; exit 1; }
command -v node >/dev/null || { echo "pagebench needs node" >&2; exit 1; }

DIR=$(mktemp -d "${PERF_DIR:-.}/.pagebench.XXXXXX")
[ "${KEEP:-0}" = 1 ] || trap 'rm -rf "$DIR"' EXIT
export XDG_DATA_HOME="$DIR/xdg"
mkdir -p "$XDG_DATA_HOME"
. tools/genmap.sh

# The page as the binary serves it: src/webpage.c's string, unescaped.
if [ -z "${PAGE:-}" ]; then
    PAGE="$DIR/page.html"
    python3 - src/webpage.c "$PAGE" <<'PY'
import re, sys
src = open(sys.argv[1]).read()
body = src[src.index('const char WEBPAGE[] ='):src.index(';\n\nconst size_t')]
text = ''.join(re.findall(r'^\s*"((?:[^"\\]|\\.)*)"$', body, re.M))
text = re.sub(r'\\(.)', lambda m: {'n': '\n', '"': '"', '\\': '\\'}[m.group(1)], text)
open(sys.argv[2], 'w').write(text)
PY
fi

MOB=$(genmap mob 40 25 0 24)              # the perf crowd: 24 creatures, no walls
WALLED=$(genmap walled 40 25 1 24)        # every edge walled: the most glyphs a row holds
BIG=$(genmap big 200 200 1 60)            # far more map than screen: a pan redraws it all

printf '| scenario             | frames |   decode |  present |     copy | px/frame |\n'
printf '|----------------------|--------|----------|----------|----------|----------|\n'
run() {
    _label=$1 _map=$2 _keys=$3
    printf '%b' "$_keys" > "$DIR/keys"
    "$BIN" "$_map" --bench "$DIR/keys" --bench-loops "$LOOPS" --size 120x40 \
        --bench-record "$DIR/$_label.rec" > /dev/null 2>&1 || { echo "  $_label: bench FAILED" >&2; return; }
    JSON=1 node tools/pagebench.js "$PAGE" "$DIR/$_label.rec" "$REPLAY" | python3 -c '
import json, sys
d = json.load(sys.stdin)
us = lambda v: "%.1fus" % v
print("| %-20s | %6d | %8s | %8s | %8s | %8d |" % (sys.argv[1], d["frames"], us(d["feed_us"]),
      us(d["present_us"]), us(d["blitRow_us"]), d["px_per_frame"]))' "$_label"
}

run "cursor walk"    "$MOB"    ':play\rllllhhhh'
run "cursor, walls"  "$WALLED" ':play\rjjjjkkkk'
run "carry"          "$MOB"    ':play\rt\rllllhhhh\r'
run "pan 200x200"    "$BIG"    ':play\r150l150h'
