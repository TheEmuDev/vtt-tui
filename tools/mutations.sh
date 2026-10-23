#!/bin/sh
# A batch of mutation checks from a list file, run one after another through
# tools/mutate.sh (which puts each file back, newer than its mutant's
# objects), with a line each and a count at the end.
#
#   tools/mutations.sh LIST SUITE...
#
# LIST holds records; a line `== FILE` starts one, `--- old` and `--- new`
# start its two texts (any number of lines each, the last newline not part
# of the text); `#` lines outside a text are comments:
#
#   == src/apikey.c
#   --- old
#   || pending[0] || looking
#   --- new
#   || looking
#
# Exits 0 when every mutation was caught, 1 when one survived, 2 when one
# could not be made.
set -u
cd "$(dirname "$0")/.."
[ $# -ge 2 ] || { echo "usage: tools/mutations.sh LIST SUITE..." >&2; exit 2; }
list=$1
shift
exec python3 -I - "$list" "$@" <<'PY'
import subprocess, sys
path, suites = sys.argv[1], sys.argv[2:]
recs, cur, part = [], None, None
for line in open(path).read().split('\n'):
    if part is None and line.startswith('#'):
        continue
    if line.startswith('== '):
        cur = {'file': line[3:].strip(), 'old': [], 'new': []}
        recs.append(cur); part = None
    elif line in ('--- old', '--- new') and cur is not None:
        part = line[4:]
    elif cur is not None and part:
        cur[part].append(line)
caught = survived = unmade = 0
for r in recs:
    old, new = '\n'.join(r['old']).rstrip('\n'), '\n'.join(r['new']).rstrip('\n')
    rc = subprocess.run(['tools/mutate.sh', r['file'], old, new] + suites,
                        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    first = old.split('\n')[0].strip()
    word = {0: 'caught', 1: 'SURVIVED'}.get(rc.returncode, 'not made')
    print('%-9s %s: %s' % (word, r['file'], first[:70]))
    if rc.returncode == 0: caught += 1
    elif rc.returncode == 1: survived += 1
    else:
        unmade += 1
        print('          ' + rc.stdout.strip().split('\n')[-1])
print('%d caught, %d survived, %d not made' % (caught, survived, unmade))
sys.exit(2 if unmade else 1 if survived else 0)
PY
