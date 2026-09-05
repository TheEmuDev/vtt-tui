#!/bin/sh
# Measures every path that costs anything and prints the tables in
# docs/PERFORMANCE.md. Run it with `make perf` against a release build; the
# numbers in that document are this script's output, so regenerating them is
# how they stay true rather than aspirational.
#
# Two measurements, because they answer different questions:
#
#   --bench  replays its script once per loop, a frame per key, and reports
#            frame times. This is what the app costs to use.
#   --trace  records every occurrence of every zone in a single run. This is
#            what a path costs *when it runs* -- the bench's per-zone figure
#            is a per-frame average, so a path that fires in a minority of
#            frames reads as 0ns there however expensive it actually is.
#
# Two rules for a scenario script, both learnt the hard way:
#   - no toggles. The bench runs the script again every loop, so a lone # would
#     turn the labels on and off in alternate frames and measure neither.
#   - no bare ESC. Every loop feeds the whole script at once, and ESC followed
#     by a letter is Alt-letter, not two keys.
#
# Fixtures are generated here rather than checked in: they are large, boring,
# and a worst case that quietly drifts out of step with the file format is
# worse than no worst case at all.
set -eu

BIN=${BIN:-./vtt}
LOOPS=${LOOPS:-400}

[ -x "$BIN" ] || { echo "no $BIN -- run make first" >&2; exit 1; }

DIR=$(mktemp -d)
trap 'rm -rf "$DIR"' EXIT
export XDG_DATA_HOME="$DIR/xdg"
mkdir -p "$XDG_DATA_HOME"

TAB=$(printf '\t')

# genmap NAME W H WALLS [TOKENS]
#   WALLS=1 puts a wall on every edge, the worst case for junction glyphs:
#   every crossing has to be resolved rather than skipped.
genmap() {
    _n=$1 _w=$2 _h=$3 _walls=$4 _tok=${5:-0}
    _f="$DIR/$_n.vtt"

    _row=$(awk "BEGIN{ for(i=0;i<$_w;i++) printf \".\" }")
    if [ "$_walls" = 1 ]; then
        _v=$(awk "BEGIN{ for(i=0;i<=$_w;i++) printf \"|\" }")
        _e=$(awk "BEGIN{ for(i=0;i<$_w;i++) printf \"-\" }")
    else
        _v=$(awk "BEGIN{ for(i=0;i<=$_w;i++) printf \" \" }")
        _e=$(awk "BEGIN{ for(i=0;i<$_w;i++) printf \" \" }")
    fi

    {
        printf 'VTT 2\nname %s\nsize %d %d\nzoom 1\nruleset daggerheart\ntiles\n' \
               "$_n" "$_w" "$_h"
        i=0; while [ $i -lt "$_h" ]; do echo "$_row"; i=$((i + 1)); done
        echo vedges
        i=0; while [ $i -lt "$_h" ]; do echo "$_v"; i=$((i + 1)); done
        echo hedges
        i=0; while [ $i -le "$_h" ]; do echo "$_e"; i=$((i + 1)); done

        i=0
        while [ $i -lt "$_tok" ]; do
            if [ $((i % 2)) = 0 ]; then _k=player; else _k=enemy; fi
            printf 'token %s %d %d 1 "Mob %d"\n' "$_k" \
                   $((i * 3 % (_w - 2) + 1)) $((i * 5 % (_h - 2) + 1)) "$i"
            printf 'tokenstatus red "Poisoned"\n'
            i=$((i + 1))
        done
    } > "$_f"
    echo "$_f"
}

WALLED=$(genmap walled 40 25 1 0)     # every edge walled: junction worst case
OPEN=$(genmap open    40 25 0 0)      # nothing to resolve: the floor of the cost
BIG=$(genmap big     200 200 1 0)     # far more map than window
MOB=$(genmap mob      40 25 0 24)     # 24 tokens, each wearing a marker
BIGMOB=$(genmap bigmob 200 200 0 24)  # the route search's worst case: big and crowded

# Rooms on a void canvas, which is what a map under construction looks like
# and the only shape that exercises the void marks.
VOIDY="$DIR/voidy.vtt"
awk 'BEGIN {
    w = 200; h = 200;
    printf "VTT 2\nname Voidy\nsize %d %d\nzoom 1\ntiles\n", w, h;
    for (y = 0; y < h; y++) {
        line = "";
        for (x = 0; x < w; x++)
            line = line ((int(x / 17) % 3 && int(y / 13) % 3) ? "." : " ");
        print line;
    }
    print "vedges";
    for (y = 0; y < h; y++) { s = ""; for (x = 0; x <= w; x++) s = s " "; print s }
    print "hedges";
    for (y = 0; y <= h; y++) { s = ""; for (x = 0; x < w; x++) s = s " "; print s }
}' > "$VOIDY"

LONG=$(awk 'BEGIN{ for (i = 0; i < 60; i++) printf "l" }')

# ------------------------------------------------------------ frame times

# One run answers both questions: the bench reports frame times on stderr, and
# tracing the same run records every occurrence of every zone.
#
# run LABEL MAP SIZE KEYS
run() {
    _label=$1 _map=$2 _size=$3 _keys=$4
    printf '%s' "$_keys" > "$DIR/keys"

    "$BIN" "$_map" --bench "$DIR/keys" --bench-loops "$LOOPS" --size "$_size" \
        --trace "$DIR/t.json" > /dev/null 2> "$DIR/out" \
        || { echo "  $_label FAILED" >&2; return; }

    awk -v label="$_label" -v size="$_size" '
        /^  frame/           { p50 = $5; p99 = $7 }
        /cells\/frame/       { cells = $3; bytes = $6 }
        END {
            printf "| %-20s | %-6s | %9s | %9s | %5s | %5s |\n",
                   label, size, p50, p99, cells, bytes
        }
    ' "$DIR/out"

    python3 "$DIR/sum.py" "$DIR/t.json" "$_label $_size" >> "$DIR/ev"
}

: > "$DIR/ev"

cat > "$DIR/sum.py" <<'SUMMARISER'
import json, sys

path, label = sys.argv[1], sys.argv[2]
try:
    events = json.load(open(path))["traceEvents"]
except Exception:
    sys.exit(0)

by = {}
for e in events:
    if e.get("ph") == "X":
        by.setdefault(e["name"], []).append(e["dur"])

for name, durs in by.items():
    durs.sort()
    n = len(durs)
    print("%s\t%.2f\t%.2f\t%.2f\t%d\t%s"
          % (name, durs[n // 2], durs[min(n - 1, int(n * 0.99))], durs[-1], n, label))
SUMMARISER

echo '| scenario             | size   | frame p50 | frame p99 | cells | bytes |'
echo '|----------------------|--------|-----------|-----------|-------|-------|'

run "build, open"          "$OPEN"   80x24  'jjllkkhh'
run "build, every edge"    "$WALLED" 80x24  'jjllkkhh'
run "build, 200x200"       "$BIG"    80x24  'jjllkkhh'
run "build, 200x200"       "$BIG"    200x50 'jjllkkhh'
run "build, mostly void"   "$VOIDY"  200x50 'jjllkkhh'
run "build, tracing"       "$WALLED" 80x24  'wjjllkkhh'
run "build, circle brush"  "$OPEN"   80x24  'Vlllljjjjhhhhkkkk'
run "ruler, three legs"    "$WALLED" 80x24  'mlll\rjjj\rll'
run "play, 24 tokens"      "$MOB"    80x24  ':play\rjjllkkhh'
run "play, 24 tokens"      "$MOB"    200x50 ':play\rjjllkkhh'
run "play, carrying"       "$MOB"    80x24  ':play\rt\rlllljjjj\r'
run "play, carry 200x200"  "$BIGMOB" 80x24  ':play\rt\rlllllllljjjjjjjj\r'
run "play, 3x3 cursor"     "$MOB"    80x24  ':play\r3llllhhhh'
run "play, choosing"       "$MOB"    80x24  ':play\r3\r\r\r\rjjjj'
run "play, group box"      "$MOB"    80x24  ':play\rvlllljjjj'
run "play, group carry"    "$MOB"    80x24  ':play\rvlljj\rjjjjllll'
run "play, range bands"    "$MOB"    80x24  ':play\rtrrrrrr'
run "help page"            "$WALLED" 80x24  '?jjjj'
run "profiler overlay"     "$WALLED" 80x24  '\e[24~jjll'

# Sorted by p99 rather than by the median: a zone whose guard turns it away
# still counts as a call, so a path that only does work sometimes has a median
# near zero and a p99 that says what it costs when it does.
echo
echo '| path             | p50     | p99     | worst   | calls | heaviest scenario      |'
echo '|------------------|---------|---------|---------|-------|------------------------|'
sort -t"$TAB" -k1,1 -k3,3gr "$DIR/ev" | awk -F"$TAB" '
    !seen[$1]++ { printf "| %-16s | %5.1fus | %5.1fus | %5.1fus | %5d | %-22s |\n",
                         $1, $2, $3, $4, $5, $6 }
' | sort
