#!/bin/sh
# The save table in docs/PERFORMANCE.md: tools/saves.c against the release
# objects (run make first), in a directory on the disk the maps are on --
# in the repo (.perf.*) unless PERF_DIR says otherwise, since on a tmpfs /tmp a
# flush is free and the table would say nothing.
set -eu
[ "$(cat build/.mode 2>/dev/null)" = release ] \
    || { echo "no release build in build/ -- run make first" >&2; exit 1; }
DIR=$(mktemp -d "${PERF_DIR:-.}/.perf.XXXXXX")
trap 'rm -rf "$DIR"' EXIT
make -s build/tool-saves
"$(dirname "$0")/machine.sh" "$DIR"
echo
build/tool-saves "$DIR"
