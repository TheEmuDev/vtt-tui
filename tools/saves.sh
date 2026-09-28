#!/bin/sh
# The save table in docs/PERFORMANCE.md: tools/saves.c against the release
# objects (run make first), in a directory on the disk the maps are on --
# in the repo (.perf.*) unless PERF_DIR says otherwise, since on a tmpfs /tmp a
# flush is free and the table would say nothing.
set -eu
[ -f build/mapio.o ] && [ "$(cat build/.mode 2>/dev/null)" = release ] \
    || { echo "no release build in build/ -- run make first" >&2; exit 1; }
DIR=$(mktemp -d "${PERF_DIR:-.}/.perf.XXXXXX")
trap 'rm -rf "$DIR"' EXIT
OBJS=$(ls build/*.o | grep -v '/main\.o$')
# shellcheck disable=SC2086
${CC:-cc} -std=c11 -D_POSIX_C_SOURCE=200809L -O2 -Isrc -o "$DIR/saves" tools/saves.c $OBJS -lm
"$DIR/saves" "$DIR"
