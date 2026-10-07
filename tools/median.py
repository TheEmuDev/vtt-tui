#!/usr/bin/env python3
"""Per-row medians of several tools/perf.sh outputs.

    tools/perf.sh > a.log; tools/perf.sh > b.log; tools/perf.sh > c.log
    tools/median.py a.log b.log c.log
    (tools/pagebench.sh's single table works the same way)

Any single run has one row spiking somewhere, and never the same row twice,
so docs/PERFORMANCE.md publishes the median of three. Prints the two tables
in the form that document carries them.
"""
import re
import statistics
import sys


def hardware(line):
    """A Machine: line without what can change between runs on one machine:
    the governor (a laptop's on unplugging), the kernel, the compiler, node."""
    key = re.sub(r", \w+ governor", "", line)
    key = re.sub(r", (Linux|Darwin|FreeBSD) \S+", "", key)
    key = re.sub(r", (gcc|clang|cc unknown) [^,]*$", "", key)
    return re.sub(r", node \S+", "", key)


def machine(paths):
    """Prints the runs' "Machine:" line (tools/machine.sh) above the tables.
    Runs on different hardware stop it: their median would mean nothing. Runs
    whose setting differs (governor, kernel, compiler) are warned about and the
    last run's line is published."""
    seen = []
    for p in paths:
        lines = [l for l in open(p).read().split("\n") if l.startswith("Machine: ")]
        seen.append(lines[0] if lines else None)
    have = [l for l in seen if l]
    if not have:
        print("Machine: not recorded (runs without tools/machine.sh)")
        print()
        return
    if len({hardware(l) for l in have}) > 1:
        sys.exit("the runs' Machine lines name different hardware:\n  " + "\n  ".join(have))
    if len(set(have)) > 1:
        sys.stderr.write("warning: the runs' settings differ (governor, kernel or compiler):\n  "
                         + "\n  ".join(have) + "\n")
    note = "" if len(have) == len(seen) else " (%d of %d runs recorded it)" % (len(have), len(seen))
    print(have[-1] + note)
    print()


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
    if len(out) not in (1, 2):
        sys.exit("%s: expected one table or two, found %d" % (path, len(out)))
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


def main_one(runs):
    """One table (tools/pagebench.sh): each row by its first cell, every cell
    that reads as a number the median of the runs, the rest the last run's."""
    last = runs[-1][0]
    print(last[0])
    print(last[1])
    idx = [{r[0].strip(): r for r in rows(run[0])[1:]} for run in runs]
    for r in rows(last)[1:]:
        have = [i[r[0].strip()] for i in idx if r[0].strip() in i]
        cells = []
        for c, cell in enumerate(r):
            try:
                vals = [us(h[c]) for h in have]
            except ValueError:
                cells.append(cell)
                continue
            v = statistics.median(vals)
            text = ("%.1fus" % v) if cell.strip().endswith("us") else ("%d" % round(v))
            cells.append(" " + text.rjust(len(cell) - 2) + " ")
        print("|" + "|".join(cells) + "|")


def main(paths):
    machine(paths)
    runs = [tables(p) for p in paths]
    if len(runs[-1]) == 1:
        return main_one(runs)
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
