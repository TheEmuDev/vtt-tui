#!/bin/sh
# The proposal table in docs/PERFORMANCE.md: tools/proposals.c against the
# release objects (run make first). Memory only, so it runs anywhere.
set -eu
[ "$(cat build/.mode 2>/dev/null)" = release ] \
    || { echo "no release build in build/ -- run make first" >&2; exit 1; }
make -s build/tool-proposals
"$(dirname "$0")/machine.sh" .
echo
build/tool-proposals
