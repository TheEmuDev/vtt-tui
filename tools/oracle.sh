#!/bin/sh
# The record of what a vtt does, taken from outside it: the map tools, frames
# and channel answers over a fixed set of maps, as text under
# tests/oracle/expected/. docs/V2.md, D11: 2.0 is judged against 1.x by this
# and not by tests that read 1.x's structs.
#
#   tools/oracle.sh record [VTT]   write expected/ from VTT (default ./vtt)
#   tools/oracle.sh check  [VTT]   run VTT over the same and diff; exit 1 on a difference
#
# Everything runs in a temporary directory with its own XDG_DATA_HOME,
# XDG_RUNTIME_DIR and XDG_CONFIG_HOME, and no keyring tool, on copies of the
# maps, so no saved character, stamp, setting, key or running vtt of the
# user's is reached and no fixture is written to.
set -eu
export LC_ALL=C          # globs sort by collation: the record's order must not follow the caller's locale
MODE=${1:-check}
VTT=$(realpath "${2:-./vtt}")
ROOT=$(cd "$(dirname "$0")/.." && pwd)
EXP="$ROOT/tests/oracle/expected"
[ -x "$VTT" ] || { echo "no vtt at $VTT -- run make first" >&2; exit 2; }
case $MODE in record|check) ;; *) echo "usage: tools/oracle.sh record|check [VTT]" >&2; exit 2 ;; esac

DIR=$(mktemp -d)
trap 'rm -rf "$DIR"' EXIT
mkdir "$DIR/maps" "$DIR/out" "$DIR/work"
. "$ROOT/tools/sandbox.sh"
sandbox_env "$DIR"
cp "$ROOT"/tests/fixtures/*.vtt "$ROOT"/tests/fuzz-found/*.vtt "$ROOT"/tests/oracle/maps/*.vtt "$DIR/maps/"
cd "$DIR/maps"

# A command's output and exit status, under a heading; `sum` keeps only the
# output's checksum, for what is too long to keep (ORACLE_KEEP=DIR keeps the
# text there, named by the checksum, to diff two binaries' by hand).
sum() {
    rc=0; "$VTT" "$@" > "$DIR/sum.txt" 2>&1 || rc=$?
    c=$(cksum < "$DIR/sum.txt" | tr ' ' '-')
    echo "== $*"; echo "-- cksum $c exit $rc"
    if [ -n "${ORACLE_KEEP:-}" ]; then mkdir -p "$ORACLE_KEEP"; cp "$DIR/sum.txt" "$ORACLE_KEEP/$c.txt"; fi
}
run() {
    echo "== $*"
    rc=0; "$VTT" "$@" 2>&1 || rc=$?
    echo "-- exit $rc"
}

# 1. Each map: the tools, then for each key script the GM's frame and the
#    players' as text, and with every cell's colors at two sizes as checksums.
for f in *.vtt; do
    m=${f%.vtt}
    {
        run "$f" --check
        run "$f" --check --json
        run "$f" --describe
        run "$f" --describe --json
        run "$f" --dump-map
        run "$f" --dump-map --region B2:F5
        for k in "$ROOT"/tests/oracle/keys/*.keys; do
            cp "$k" "../work/$(basename "$k")"
            run "$f" --seed 1 --script "../work/$(basename "$k")" --dump-frame --size 80x24
            run "$f" --seed 1 --script "../work/$(basename "$k")" --dump-frame --players --size 80x24
            for size in 80x24 110x34; do
                sum "$f" --seed 1 --script "../work/$(basename "$k")" --dump-frame --cells --size "$size"
                sum "$f" --seed 1 --script "../work/$(basename "$k")" --dump-frame --cells --players --size "$size"
            done
            # A window smaller than the map: the cull.
            sum "$f" --seed 1 --script "../work/$(basename "$k")" --dump-frame --cells --size 40x12
        done
    } > "../out/map-$m.txt"
done

# 2. The channel's language: each request of the fuzz corpus against a fresh
#    copy of two maps, with what it answered and the map it left.
{
    for base in two-rooms everything; do
        for q in "$ROOT"/tests/fuzz-ctl/*; do
            cp "$base.vtt" ../work/req.vtt
            cp "$q" "../work/$(basename "$q").req"
            ( cd ../work && run req.vtt --apply "$(basename "$q").req" && echo "   (on $base)" && run req.vtt --dump-map \
                         && run req.vtt --check )          # the file the request left is a sound one
            # A plan that changed nothing leaves the file's bytes (HEALTH 60).
            if cmp -s "$base.vtt" ../work/req.vtt; then echo "   file: as it was"; else echo "   file: written"; fi
        done
    done
    # A map made from nothing.
    ( cd ../work && rm -f new.vtt && run new.vtt --apply rooms.req --new 30x20 && run new.vtt --dump-map && run new.vtt --check )
} > ../out/requests.txt

# 3. Big generated maps: checksums only (the text is megabytes).
(
    DIR="$DIR/work"          # genmap writes to $DIR; a subshell, so the trap's DIR is untouched
    . "$ROOT/tools/genmap.sh"
    for spec in "gen-small 40 25 1 24" "gen-wide 200 200 0 500" "gen-turns 60 40 1 60 daggerheart 1" "gen-norules 64 64 0 12 none"; do
        # shellcheck disable=SC2086
        g=$(genmap $spec)
        cd "$DIR"
        for tool in --check --describe --dump-map; do sum "$(basename "$g")" $tool; done
        sum "$(basename "$g")" --dump-frame --cells --size 120x40
    done
) > ../out/generated.txt

if [ "$MODE" = record ]; then
    rm -rf "$EXP"; mkdir -p "$EXP"
    cp "$DIR"/out/*.txt "$EXP/"
    echo "recorded $(ls "$EXP" | wc -l) files, $(cat "$EXP"/*.txt | wc -c) bytes, from $VTT"
else
    if diff -r "$EXP" "$DIR/out" > "$DIR/diff.txt"; then echo "oracle: the same ($(ls "$EXP" | wc -l) files)"
    else
        grep -c '^[<>]' "$DIR/diff.txt" | xargs echo "oracle: lines that differ:"
        head -60 "$DIR/diff.txt"
        exit 1
    fi
fi
