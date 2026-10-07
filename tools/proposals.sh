#!/bin/sh
# The proposal table in docs/PERFORMANCE.md: tools/proposals.c against the
# release objects (run make first). Memory only, so it runs anywhere.
set -eu
[ -f build/changeset.o ] && [ "$(cat build/.mode 2>/dev/null)" = release ] \
    || { echo "no release build in build/ -- run make first" >&2; exit 1; }
DIR=$(mktemp -d)
trap 'rm -rf "$DIR"' EXIT
OBJS=$(ls build/*.o | grep -v '/main\.o$')
# shellcheck disable=SC2086
${CC:-cc} -std=c11 -D_POSIX_C_SOURCE=200809L -O2 -Isrc -o "$DIR/proposals" tools/proposals.c $OBJS -lm
"$(dirname "$0")/machine.sh" .
echo
"$DIR/proposals"
