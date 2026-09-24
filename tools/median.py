#!/usr/bin/env python3
"""Per-row medians of several tools/perf.sh outputs.

    tools/perf.sh > a.log; tools/perf.sh > b.log; tools/perf.sh > c.log
    tools/median.py a.log b.log c.log

Any single run has one row spiking somewhere, and never the same row twice,
so docs/PERFORMANCE.md publishes the median of three. Prints the two tables
in the form that document carries them.
"""
import statistics
import sys


def tables(path):
    out, cur = [], None
    for line in open(path).read().split("\n"):
        if line.startswith("|"):
            cur = (cur or []) + [line]
        elif cur:
            out.append(cur)
            cur = None
    if cur:
        out.append(cur)
    if len(out) != 2:
        sys.exit("%s: expected two tables, found %d" % (path, len(out)))
    return out


def rows(table):
    return [l.split("|")[1:-1] for l in table if not l.startswith("|--")]


def us(cell):
    cell = cell.strip()
    # The bench prints a slow frame in milliseconds rather than a four-digit
    # microsecond figure; read both.
    if cell.endswith("ms"):
        return float(cell[:-2]) * 1000.0
    return float(cell.replace("us", ""))


def fmt(v, width):
    return ("%.1fus" % v).rjust(width)


def main(paths):
    runs = [tables(p) for p in paths]
    last = runs[-1]

    print(last[0][0])
    print("|----------------------|--------|-----------|-----------|-------|-------|")
    idx = [{(r[0].strip(), r[1].strip()): r for r in rows(run[0])[1:]} for run in runs]
    for r in rows(last[0])[1:]:
        k = (r[0].strip(), r[1].strip())
        have = [i[k] for i in idx if k in i]
        p50 = statistics.median(us(h[2]) for h in have)
        p99 = statistics.median(us(h[3]) for h in have)
        print("|%s|%s| %s | %s |%s|%s|" % (r[0], r[1], fmt(p50, 9), fmt(p99, 9), r[4], r[5]))

    print()
    print(last[1][0])
    print("|------------------|---------|---------|---------|-------|------------------------|")
    cidx = [{r[0].strip(): r for r in rows(run[1])[1:]} for run in runs]
    for r in rows(last[1])[1:]:
        k = r[0].strip()
        have = [i[k] for i in cidx if k in i]
        vals = [statistics.median(us(h[c]) for h in have) for c in (1, 2, 3)]
        print("|%s| %s |%s|%s|" % (r[0], " | ".join(fmt(v, 7) for v in vals), r[4], r[5]))


if __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    main(sys.argv[1:])
