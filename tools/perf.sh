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
# Two rules for a scenario script, both learned the hard way:
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

# The maps live in the repo (in .perf.*, ignored by git), on the filesystem a GM's maps would be on:
# /tmp is often memory, where flushing a save to the disk costs nothing and
# the save and trip rows would quote a figure no table sees.
DIR=$(mktemp -d "${PERF_DIR:-.}/.perf.XXXXXX")
trap 'rm -rf "$DIR"' EXIT
export XDG_DATA_HOME="$DIR/xdg"
mkdir -p "$XDG_DATA_HOME"

TAB=$(printf '\t')

# genmap: tools/genmap.sh, shared with tools/pagebench.sh.
. "$(dirname "$0")/genmap.sh"

WALLED=$(genmap walled 40 25 1 0)     # every edge walled: junction worst case
OPEN=$(genmap open    40 25 0 0)      # nothing to resolve: the floor of the cost
BIG=$(genmap big     200 200 1 0)     # far more map than window
MOB=$(genmap mob      40 25 0 24)     # 24 tokens, each wearing a marker
# The same crowd, each with a card (a stat block's worth): the box beside the map.
CARDMOB="$DIR/cardmob.vtt"
{ sed '1s/^VTT [0-9]*/VTT 13/' "$MOB" | awk '{ print } /^token / { print "tokencard \"Mob\"" }'
  printf 'card "Mob"\n| Mob - Tier 1 Standard\n| A crowd of them, and each one angry.\n'
  printf '| Motives: shove, shout, swarm\n| Difficulty: 11   Thresholds: 5/10   HP: 3   Stress: 1\n'
  printf '| Attack: +1   Club (Melee) 1d6+1 phy\n|\n'
  printf '| Group Attack - Action: **Spend a Fear** to choose a target and spotlight every Mob within Close range of it. They move into Melee and make one attack roll together; on a success they deal **1d6** physical damage each, combined.\n'
  printf 'endcard\n'; } > "$CARDMOB"
# The same with HP on each: :dmg against the card's thresholds.
DMGMOB="$DIR/dmgmob.vtt"
awk '{ print } /^tokencard / { print "tokencounter HP 9 9" }' "$CARDMOB" > "$DMGMOB"
HORDE=$(genmap horde  60 40 0 500)    # 500 creatures: what a scene puts back at worst
BIGMOB=$(genmap bigmob 200 200 0 24)  # the route search's worst case: big and crowded
BIGHORDE=$(genmap bighorde 200 200 0 500) # as many, mostly off the window: cost follows the window
PLAIN=$(genmap plain   40 25 0 24 none) # no ruleset: r is a radius, not a band
FIGHT=$(genmap fight   40 25 0 24 daggerheart 1)  # all 24 in the turn order
# MOB with its first enemy hidden: the players' frame is drawn, not copied.
HIDMOB="$DIR/hidmob.vtt"
awk '{ print } /"Mob 1"/ { print "tokenhidden" }' "$MOB" | sed 's/^VTT 2$/VTT 10/' > "$HIDMOB"

# Rooms on a void canvas, which is what a map under construction looks like
# and the only shape that exercises the void marks.
# 500 saved characters for the picker: what a long campaign's worth looks like.
CHARS="$XDG_DATA_HOME/vtt/characters"
mkdir -p "$CHARS"
awk -v dir="$CHARS" 'BEGIN {
    for (i = 0; i < 500; i++) {
        f = sprintf("%s/beast-%03d.vtt", dir, i); s = i % 3 + 1
        printf "VTT 6\nname beast-%03d\nsize %d %d\ntiles\n", i, s, s > f
        for (y = 0; y < s; y++) { for (x = 0; x < s; x++) printf "." > f; printf "\n" > f }
        printf "token enemy 0 0 %d \"Beast %d\"\ntokencounter HP %d %d\nroll bite%d \"1d8+%d\"\n", s, i, i % 20 + 1, i % 20 + 1, i % 7, i % 5 > f
        close(f)
    }
}'
# Two maps linked both ways, each link landing on the other's end, so a party
# can walk there and back every loop: the trip saves one map and opens the other.
TOWN="$DIR/town.vtt"
{ printf 'VTT 12\nname town\nsize 40 25\ntiles\n'
  awk 'BEGIN{ for (y = 0; y < 25; y++) { for (x = 0; x < 40; x++) printf "."; print "" } }'
  printf 'token player 2 2 1 "Aria"\narea 2 2 2 2 "Gate"\nlink 1 stairs 1 2 2 to "crypt" "Entrance"\n'; } > "$TOWN"
{ printf 'VTT 12\nname crypt\nsize 40 25\ntiles\n'
  awk 'BEGIN{ for (y = 0; y < 25; y++) { for (x = 0; x < 40; x++) printf "."; print "" } }'
  printf 'area 3 3 3 3 "Entrance"\nlink 1 stairs 1 3 3 to "town" "Gate"\n'; } > "$DIR/crypt.vtt"
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

# Rooms 6x5 in walls, one gap in every wall, and a closed door at the first
# room's east gap: the only fixture where a sight line can be stopped. A
# player at (6k+1, 5j+1) in each of twelve rooms with four free squares east
# of it, and an enemy at (6k+3, 5j+3) beside it. The three players on row 11
# (H12, N12, T12) are the group the party row carries.
WARREN="$DIR/warren.vtt"
awk 'BEGIN {
    w = 40; h = 25;
    printf "VTT 2\nname Warren\nsize %d %d\nzoom 1\nruleset daggerheart\ntiles\n", w, h;
    for (y = 0; y < h; y++) { s = ""; for (x = 0; x < w; x++) s = s "."; print s }
    print "vedges";
    for (y = 0; y < h; y++) {
        s = "";
        for (x = 0; x <= w; x++) {
            c = " ";
            if (x > 0 && x < w && x % 6 == 0 && y % 5 != 2) c = "|";
            if (x == 6 && y == 2) c = "+";
            s = s c;
        }
        print s;
    }
    print "hedges";
    for (y = 0; y <= h; y++) {
        s = "";
        for (x = 0; x < w; x++)
            s = s ((y > 0 && y < h && y % 5 == 0 && x % 6 != 3) ? "-" : " ");
        print s;
    }
    n = 0;
    for (j = 0; j < 3; j++)
        for (k = 0; k < 6; k++) {
            if ((j == 1 && k > 2) || (j == 2 && (k < 1 || k > 3))) continue;
            printf "token player %d %d 1 \"P%d\"\n", 6 * k + 1, 5 * j + 1, n;
            printf "token enemy %d %d 1 \"E%d\"\n",  6 * k + 3, 5 * j + 3, n;
            n++;
        }
}' > "$WARREN"

# Sixty-four links, the most a map holds, all on screen at 80x24 -- the
# worst the marks can cost -- and a player on link 1's first end (A1), to be
# sent across and back.
LINKS="$DIR/links.vtt"
awk 'BEGIN {
    w = 40; h = 25;
    printf "VTT 8\nname Links\nsize %d %d\nzoom 1\ntiles\n", w, h;
    for (y = 0; y < h; y++) { s = ""; for (x = 0; x < w; x++) s = s "."; print s }
    split("stairs ladder trapdoor portal", kind, " ");
    for (i = 0; i < 64; i++)
        printf "link %d %s 1 %d %d %d %d\n", i + 1, kind[i % 4 + 1],
               i % 8 * 2, int(i / 8) * 2, 20 + i % 8 * 2, int(i / 8) * 2;
    print "token player 0 0 1 \"Aria\"";
}' > "$LINKS"

# Three floors of 40x25 side by side, 24 creatures on each, a floor each
# for the GM and the players: the players' frame drawn through its own
# camera. Party on Ground; the GM looks at Upper.
FLOORS="$DIR/floors.vtt"
awk 'BEGIN {
    w = 124; h = 25;
    printf "VTT 9\nname Floors\nsize %d %d\nzoom 1\nruleset daggerheart\ntiles\n", w, h;
    for (y = 0; y < h; y++) {
        s = "";
        for (x = 0; x < w; x++) s = s ((x % 42 < 40) ? "." : " ");
        print s;
    }
    for (f = 0; f < 3; f++)
        for (i = 0; i < 24; i++)
            printf "token %s %d %d 1 \"F%d %d\"\n", (f == 1 && i % 2 == 0) ? "player" : "enemy",
                   f * 42 + i * 3 % 38 + 1, i * 5 % 23 + 1, f, i;
    print "area 0 0 39 24 \"Cellar\"";
    print "area 42 0 81 24 \"Ground\"";
    print "area 84 0 123 24 \"Upper\"";
    print "floor \"Cellar\" -1";
    print "floor \"Ground\" 0";
    print "floor \"Upper\" 1";
}' > "$FLOORS"

LONG=$(awk 'BEGIN{ for (i = 0; i < 60; i++) printf "l" }')

# The control channel's requests (docs/CONTROL.md): a 40x40 room over the void
# canvas with a dozen creatures in it, taken back by the u that follows; and
# the whole of the largest map dumped.
HUGE=$(genmap huge 512 512 0 0)
{
    echo 'room A1:AN40'
    for c in C D E F G H I J K L M N; do printf 'token add enemy %s5 "M%s"\n' "$c" "$c"; done
} > "$DIR/room.ctl"
echo 'dump' > "$DIR/dump.ctl"
# The room language: five named rooms placed by each other, five corridors
# (one two wide), creatures by room, on a void canvas.
VOIDMAP="$DIR/void.vtt"
printf 'VTT 2\nname Void\nsize 100 60\nzoom 1\n' > "$VOIDMAP"
VOID512="$DIR/void512.vtt"
printf 'VTT 2\nname Void\nsize 512 512\nzoom 1\n' > "$VOID512"
cat > "$DIR/plan.ctl" <<'PLAN'
room A B2 10x8
room B 10x8 east of A gap 6
room C 10x8 south of A gap 6
room D 10x8 east of C gap 6
room E 8x6 east of B gap 6
corridor A B
corridor A C width 2
corridor C D
corridor B D
corridor B E
token add enemy A "G1"
token add enemy B "G2"
token add enemy D "G3"
token add player C size 2 "P1"
PLAN

# ------------------------------------------------------------ frame times

# One run answers both questions: the bench reports frame times on stderr, and
# tracing the same run records every occurrence of every zone.
#
# run LABEL MAP SIZE KEYS [EXTRA FLAGS]
run() {
    _label=$1 _map=$2 _size=$3 _keys=$4 _extra=${5:-}
    printf '%s' "$_keys" > "$DIR/keys"

    # shellcheck disable=SC2086
    "$BIN" "$_map" --bench "$DIR/keys" --bench-loops "$LOOPS" --size "$_size" $_extra \
        --trace "$DIR/t.json" > /dev/null 2> "$DIR/out" \
        || { echo "  $_label FAILED" >&2; return; }
    # Its zone rows then count only the calls before the cap.
    _full=""
    grep -q "trace is full" "$DIR/out" && _full=+

    awk -v label="$_label" -v size="$_size" '
        /^  frame/           { p50 = $5; p99 = $7 }
        /cells\/frame/       { cells = $3; bytes = $6 }
        END {
            printf "| %-20s | %-6s | %9s | %9s | %5s | %5s |\n",
                   label, size, p50, p99, cells, bytes
        }
    ' "$DIR/out"

    python3 "$DIR/sum.py" "$DIR/t.json" "$_label $_size" "$_full" >> "$DIR/ev"
}

: > "$DIR/ev"

cat > "$DIR/sum.py" <<'SUMMARIZER'
import json, sys

# A trace that filled up stopped recording partway: the timings still sample
# every zone (each loop runs the whole script), the call count is a floor.
path, label, full = sys.argv[1], sys.argv[2], sys.argv[3]
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
    print("%s\t%.2f\t%.2f\t%.2f\t%d%s\t%s"
          % (name, durs[n // 2], durs[min(n - 1, int(n * 0.99))], durs[-1], n, full, label))
SUMMARIZER

"$(dirname "$0")/machine.sh" "$DIR"
echo
echo '| scenario             | size   | frame p50 | frame p99 | cells | bytes |'
echo '|----------------------|--------|-----------|-----------|-------|-------|'

run "build, open"          "$OPEN"   80x24  'jjllkkhh'
run "build, every edge"    "$WALLED" 80x24  'jjllkkhh'
run "build, 200x200"       "$BIG"    80x24  'jjllkkhh'
run "build, 200x200"       "$BIG"    200x50 'jjllkkhh'
run "build, mostly void"   "$VOIDY"  200x50 'jjllkkhh'
run "build, noted squares" "$MOB"    80x24  'snx\rllsnx\rjjsnx\rhhkk'
run "build, tracing"       "$WALLED" 80x24  'wjjllkkhh'
run "build, circle brush"  "$OPEN"   80x24  'Vlllljjjjhhhhkkkk'
run "build, 3x3 brush"     "$OPEN"   80x24  '3bxfllxfllxfjjLLKK'
run "build, fill+undo 200"  "$BIG"    80x24  'v199l199jxu\x12u199h199k'
run "build, fill history"   "$BIG"    80x24  'v199l199jx199h199kv199l199jf199h199k'
# A job asked over a box filling the window, its tint drawn under every
# frame's moves, then taken away and the cursor brought back, so every loop
# boxes the same 40x20 (docs/CONFLICTS.md).
run "build, a job's tint"   "$BIG"    80x24  'v39l19j:ask flood it\rllllhhhh:ask 1 remove\r39h19k'
run "ruler, three legs"    "$WALLED" 80x24  'mlll\rjjj\rll'
run "play, 24 tokens"      "$MOB"    80x24  ':play\rjjllkkhh'
run "play, 24 tokens"      "$MOB"    200x50 ':play\rjjllkkhh'
run "play, carrying"       "$MOB"    80x24  ':play\rt\rlllljjjj\r'
run "play, carry 200x200"  "$BIGMOB" 80x24  ':play\rt\rlllllllljjjjjjjj\r'
run "play, 24 on 200x200"  "$BIGMOB" 80x24  ':play\rjjllkkhh'
run "play, 500 on 200x200" "$BIGHORDE" 80x24 ':play\rjjllkkhh'
run "play, 3x3 cursor"     "$MOB"    80x24  ':play\r3bllllhhhh'
run "play, choosing"       "$MOB"    80x24  ':play\r3b\r\r\r\rjjjj'
run "play, group box"      "$MOB"    80x24  ':play\rvlllljjjj'
run "play, group carry"    "$MOB"    80x24  ':play\rvlljj\rjjjjllll'
run "play, range bands"    "$MOB"    80x24  ':play\rtrrrrrr'
run "play, range radius"   "$PLAIN"  80x24  ':play\rt20r'
run "play, range cone"     "$PLAIN"  80x24  ':play\rt2R6rllllhhhh'
run "play, range line"     "$PLAIN"  80x24  ':play\rt3R6rllllhhhh'
run "play, range square"   "$PLAIN"  80x24  ':play\rt4R6rllllhhhh'
run "play, turn order"     "$FIGHT"  80x24  ':play\r8a8A'
run "play, fight cycling"  "$FIGHT"  80x24  ':play\rttttTTTT'
run "play, spotlight"      "$MOB"    80x24  ':play\raa'
run "play, named roll"     "$MOB"    80x24  ':play\r:roll attack = 2d12+3\r:roll attack\r:roll att\r'
run "play, fog"            "$MOB"    80x24  ':fog all\r:play\rjjllkkhh'
run "play, fog all dark, 4 watch" "$MOB"   80x24  ':fog all\r:play\rjjllkkhh' "--bench-clients 4"
run "play, fog by hand"    "$MOB"    80x24  ':fog all\r:play\rgrghllgrghhh'
run "play, fog range, 4 watchers" "$MOB" 80x24 ':fog all\r:play\rtgR6rllhh' "--bench-clients 4"
run "play, fog sight"      "$MOB"    80x24  ':fog all 6\r:play\rf\rllllhhhh\r'
run "play, fog lantern"    "$MOB"    80x24  ':fog all 6\r:fog All memory off\r:play\rf\rllllhhhh\r'
run "play, fog warren"     "$WARREN" 80x24  ':fog all 6\r:play\rf\rllllhhhh\r'
run "play, fog warren 12"  "$WARREN" 80x24  ':fog all 12\r:play\rf\rllllhhhh\r'
run "play, fog warren, party" "$WARREN" 80x24 ':fog all 6\r:play\r:H12\rvllllllllllll\rjjkk\r'
run "play, fog door"       "$WARREN" 80x24  ':fog all 6\r:play\r:F3\roo'
run "play, fog full rebuild" "$MOB"  80x24  ':fog all 6\r:play\r:fog All 5\r:fog All 6\r'
run "play, fog reveal 12"  "$MOB"    80x24  ':fog all 12\r:play\rf\rllllhhhh\r'
run "play, fog sight, 4 watchers" "$MOB" 80x24 ':fog all 6\r:play\rf\rllllhhhh\r' "--bench-clients 4"
run "play, fog soft edge, 4 watchers" "$MOB" 80x24 ':fog all 6\r:fog --soft-edge\r:play\rf\rllllhhhh\r' "--bench-clients 4"
run "build, stamp 20x20"   "$VOIDY"  80x24  'v19l19jy19h19k20lppu20h'
run "build, fog paint"     "$MOB"    80x24  ':fog Crypt\r3bgfgcllgfgchh'
run "build, 64 links"      "$LINKS"  80x24  'jjllkkhh'
run "play, 64 links"       "$LINKS"  80x24  ':play\rjjllkkhh'
run "play, link there+back" "$LINKS" 80x24  ':play\r:a1\rgogo'
run "build, one floor"     "$FLOORS" 80x24  ':floor Ground\rjjllkkhh'
run "play, other floor, 4 watchers" "$FLOORS" 80x24 ':play\r:floor Upper\rjjllkkhh' "--bench-clients 4"
run "play, floor steps"    "$FLOORS" 80x24  ':play\r][]['
run "play, counters"       "$MOB"    80x24  ':play\rtsvhp 9\r><><><><'
run "play, clocks"         "$MOB"    80x24  ':play\r:clock Dragon 6\r:clock Ritual 8\r:tick Dragon 2\r:tick -2\r'
run "play, 1 watcher"      "$MOB"    80x24  ':play\rjjllkkhh' "--bench-clients 1"
run "play, 4 watchers"     "$MOB"    80x24  ':play\rjjllkkhh' "--bench-clients 4"
run "play, 4 watchers, differing" "$MOB" 80x24 ':play\rtsnhidden\rjjllkkhh' "--bench-clients 4"
run "play, hidden, 4 watchers" "$HIDMOB" 80x24 ':play\rjjllkkhh' "--bench-clients 4"
run "play, camera party, 4 watchers" "$MOB" 80x24 ':play\r:player camera party\rt\rllllhhhh\r' "--bench-clients 4"
run "play, camera hold, 4 watchers" "$MOB" 80x24 ':play\r:player camera hold\rjjllkkhh' "--bench-clients 4"
run "play, whisper, 4 named" "$MOB" 80x24  ':play\r:whisper P2 The floor is warm\r' "--bench-clients 4 --bench-names"
run "play, pings, 4 watchers" "$MOB"  80x24  ':play\rjjllkkhh' "--bench-clients 4 --bench-pings"
run "play, fog pings, 4 watchers" "$MOB" 80x24 ':fog all\r:play\rjjllkkhh' "--bench-clients 4 --bench-pings"
run "play, GM ping"        "$MOB"    80x24  ':play\rgpllgphh'
run "play, carry, 4 watch" "$MOB"    80x24  ':play\rt\rlllljjjj\r' "--bench-clients 4"
run "play, logging"        "$MOB"    80x24  ':play\r:log on\rt\rlllljjjj\r'
run "play, rolling"        "$MOB"    80x24  ':play\r:roll 2d6+3\r:roll +1\r'
run "play, group effect"   "$MOB"    80x24  ':play\r2gellllhhhhjjkk'
run "play, card box"      "$CARDMOB" 80x24 ':play\rtjjllkkhh'
run "play, damage"         "$DMGMOB" 80x24  ':play\r4ge:dmg 6\ru'   # 13 caught
run "play, 500 characters" "$MOB"    80x24  ':play\ritebeast-4\t\t\ru'
run "play, scene of 500"   "$HORDE"  80x24  ':play\r:scene save A\r:scene A\ru'
run "play, map trip there+back" "$TOWN" 80x24 ':play\r:C3\rgogo'
run "play, handout typed, 4 watch" "$MOB"  80x24  ':play\r:handout say The door reads: SPEAK, FRIEND\r:handout off\r' "--bench-clients 4"
run "agent, room + 12"     "$VOIDY"  80x24  'u' "--bench-ctl $DIR/room.ctl"
run "agent, plan of rooms" "$VOIDMAP" 80x24  'u' "--bench-ctl $DIR/plan.ctl"
run "agent, dump 512x512"  "$HUGE"   80x24  'lh' "--bench-ctl $DIR/dump.ctl"
# A proposal (docs/CONFLICTS.md, step 4): the plan of rooms on a copy of the
# largest map, then reviewed and scrapped, or accepted and undone. Each loop's
# request is a new job; finished ones make room for the next.
run "agent, proposal, review" "$VOID512" 80x24 ':review\rllllhhhhd' "--bench-ctl $DIR/plan.ctl --bench-review"
run "agent, proposal, accept" "$VOID512" 80x24 ':review\r\ru'      "--bench-ctl $DIR/plan.ctl --bench-review"
run "help page"            "$WALLED" 80x24  '?jjjj'
run "profiler overlay"     "$WALLED" 80x24  '\e[24~jjll'

# Sorted by p99 rather than by the median: a zone whose guard turns it away
# still counts as a call, so a path that only does work sometimes has a median
# near zero and a p99 that says what it costs when it does.
echo
echo '| path             | p50     | p99     | worst   | calls | heaviest scenario      |'
echo '|------------------|---------|---------|---------|-------|------------------------|'
sort -t"$TAB" -k1,1 -k3,3gr "$DIR/ev" | awk -F"$TAB" '
    !seen[$1]++ { printf "| %-16s | %5.1fus | %5.1fus | %5.1fus | %5s | %-22s |\n",
                         $1, $2, $3, $4, $5, $6 }
' | sort
