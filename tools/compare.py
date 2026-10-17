#!/usr/bin/env python3
"""Two tools/median.py outputs side by side: the rows both have.

    tools/compare.py OLD.md NEW.md

Prints the Machine: line, then the frame rows (frame p50, p99 and bytes) and
the cost-per-call rows (p50, p99) each run has, old beside new with the change
as a ratio; a row in one run only is listed after, by name. Used by
tools/vs1x.sh for docs/PERFORMANCE.md, *2.0 against 1.x*.
"""
import re
import sys


def us(s):
    m = re.match(r"([\d.]+)(us|ms|s)\b", s.strip())
    if not m:
        return None
    return float(m.group(1)) * {"us": 1, "ms": 1000, "s": 1e6}[m.group(2)]


def tables(path):
    frames, calls, machine, part = {}, {}, "", None
    for line in open(path):
        if line.startswith("Machine: "):
            machine = line.strip()
        if line.startswith("| scenario"):
            part = frames
            continue
        if line.startswith("| path"):
            part = calls
            continue
        if not line.startswith("| ") or part is None or line.startswith("|--"):
            continue
        c = [x.strip() for x in line.strip().strip("|").split("|")]
        if part is frames:
            frames[c[0] + " " + c[1]] = c
        else:
            calls[c[0]] = c
    return machine, frames, calls


def ratio(a, b):
    if a is None or b is None or a == 0:
        return ""
    r = b / a
    return "%.2fx" % r


def main():
    om, of, oc = tables(sys.argv[1])
    nm, nf, nc = tables(sys.argv[2])
    print(nm)
    print()
    print("| scenario | size | 1.x p50 | 2.0 p50 | | 1.x p99 | 2.0 p99 | | 1.x bytes | 2.0 bytes |")
    print("|---|---|---|---|---|---|---|---|---|---|")
    for k in of:
        if k not in nf:
            continue
        o, n = of[k], nf[k]
        print("| %s | %s | %s | %s | %s | %s | %s | %s | %s | %s |" % (
            o[0], o[1], o[2], n[2], ratio(us(o[2]), us(n[2])), o[3], n[3], ratio(us(o[3]), us(n[3])), o[5], n[5]))
    print()
    print("| path | 1.x p50 | 2.0 p50 | | 1.x p99 | 2.0 p99 | | heaviest in 1.x / in 2.0 |")
    print("|---|---|---|---|---|---|---|---|")
    for k in oc:
        if k not in nc:
            continue
        o, n = oc[k], nc[k]
        # A row is its heaviest scenario's: when that differs, the change is the mix.
        heavy = o[5] if o[5] == n[5] else "%s / %s" % (o[5], n[5])
        print("| %s | %s | %s | %s | %s | %s | %s | %s |" % (
            k, o[1], n[1], ratio(us(o[1]), us(n[1])), o[2], n[2], ratio(us(o[2]), us(n[2])), heavy))
    print()
    only_old = [k for k in list(of) + list(oc) if k not in nf and k not in nc]
    only_new = [k for k in list(nf) + list(nc) if k not in of and k not in oc]
    print("In 1.x only: " + (", ".join(only_old) if only_old else "none"))
    print()
    print("In 2.0 only: " + (", ".join(only_new) if only_new else "none"))


if __name__ == "__main__":
    main()
