#!/usr/bin/env python3
"""Each zone's median in a vtt Chrome trace (--trace, tools/checkbench.c).

    tools/zones.py TRACE.json

One line a zone: its name, the median duration of its calls, and how many
there were. For a quick look at one run; published tables come from
tools/perf.sh and tools/median.py.
"""
import json
import sys

events = json.load(open(sys.argv[1]))["traceEvents"]
by = {}
for e in events:
    if e.get("ph") == "X":
        by.setdefault(e["name"], []).append(e["dur"])
for name, d in sorted(by.items()):
    d.sort()
    print("%-16s p50 %10.1f us  n %d" % (name, d[len(d) // 2], len(d)))
