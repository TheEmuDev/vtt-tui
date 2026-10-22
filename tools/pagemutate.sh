#!/bin/sh
# A mutation of the phone page's connection code, checked in seconds: the
# page's text with one exact string replaced, run through tests/page_conn.js
# (suite pageconn's harness) with no rebuild. tools/mutate.sh does the same
# through the C suite, two rebuilds and minutes a mutation.
#
#   tools/pagemutate.sh 'old text' 'new text' [PAGE]     (PAGE: web/index.html)
#
# The old text must occur exactly once. Prints "caught" with the failing
# checks, or "SURVIVED"; exits 0 caught, 1 survived, 2 not made. A mutant
# that does not parse, or runs past PAGEMUTATE_TIMEOUT (30 s), is caught too:
# the page would be broken. The page is never touched: the mutant is a copy
# in a temporary directory, removed. PAGE is relative to where it is run.
set -u
[ $# -ge 2 ] || { echo "usage: tools/pagemutate.sh 'old' 'new' [PAGE]" >&2; exit 2; }
if [ $# -ge 3 ]; then
    [ -f "$3" ] || { echo "pagemutate: no file $3" >&2; exit 2; }
    page="$(cd "$(dirname "$3")" && pwd -P)/$(basename "$3")"
fi
cd "$(dirname "$0")/.."
page=${page:-web/index.html}
dir=$(mktemp -d "${TMPDIR:-/tmp}/pagemutate.XXXXXX") || exit 2
trap 'rm -rf "$dir"' EXIT
python3 -I -c '
import sys
p, o, n, out = sys.argv[1:5]
s = open(p).read()
c = s.count(o)
if c != 1:
    sys.exit("pagemutate: the old text occurs %d times in %s, not once" % (c, p))
open(out, "w").write(s.replace(o, n, 1))' "$page" "$1" "$2" "$dir/page.html" || exit 2
out=$(timeout "${PAGEMUTATE_TIMEOUT:-30}" node tests/page_conn.js "$dir/page.html" 2>&1)
rc=$?
if [ $rc = 0 ]; then
    echo "SURVIVED: $(printf '%s\n' "$out" | tail -1)"
    exit 1
fi
printf '%s\n' "$out" | awk '/FAIL|Error/' | head -5
[ $rc = 124 ] && echo "caught: the mutant ran past ${PAGEMUTATE_TIMEOUT:-30} s" || echo "caught"
exit 0
