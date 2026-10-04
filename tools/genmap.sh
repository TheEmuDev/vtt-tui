# Sourced, not run: the map generator tools/perf.sh and tools/pagebench.sh
# share. The caller sets DIR, the directory the maps are written to; genmap
# prints the path of the map it wrote.
#
# genmap NAME W H WALLS [TOKENS] [RULESET] [TURNS]
#   WALLS=1 puts a wall on every edge, the worst case for junction glyphs:
#   every crossing has to be resolved rather than skipped. RULESET defaults
#   to daggerheart; "none" writes no ruleset line, so r grows a radius.
#   TURNS=1 gives every token a place in the turn order.
genmap() {
    _n=$1 _w=$2 _h=$3 _walls=$4 _tok=${5:-0} _rs=${6:-daggerheart} _turns=${7:-0}
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
        if [ "$_turns" = 1 ]; then _ver=4; else _ver=2; fi
        printf 'VTT %d\nname %s\nsize %d %d\nzoom 1\n' "$_ver" "$_n" "$_w" "$_h"
        [ "$_rs" = none ] || printf 'ruleset %s\n' "$_rs"
        printf 'tiles\n'
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
            [ "$_turns" = 1 ] && printf 'tokenturn %d\n' $((i * 7 % 20 + 1))
            i=$((i + 1))
        done
    } > "$_f"
    echo "$_f"
}
