#!/bin/sh
# A mutation of the phone page's connection code, checked in seconds: the
# page's text with one exact string replaced, run through tests/page_conn.js
# (suite pageconn's harness) with no rebuild. tools/mutate.sh does the same
# through the C suite, two rebuilds and minutes a mutation.
#
#   tools/pagemutate.sh 'old text' 'new text' [PAGE]     (PAGE: web/index.html)
#
# The old text must occur exactly once. Prints "caught" with the failing
# checks, or "SURVIVED"; exits 0 caught, 1 survived, 2 not made. The page is
# never touched: the mutant is a copy in a temporary directory, removed.
set -u
cd "$(dirname "$0")/.."
[ $# -ge 2 ] || { echo "usage: tools/pagemutate.sh 'old' 'new' [PAGE]" >&2; exit 2; }
page=${3:-web/index.html}
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
if out=$(node tests/page_conn.js "$dir/page.html" 2>&1); then
    echo "SURVIVED: $(printf '%s\n' "$out" | tail -1)"
    exit 1
fi
printf '%s\n' "$out" | awk '/FAIL|Error/' | head -5
echo "caught"
exit 0
