#!/usr/bin/env python3
"""Every single edit of the shipped maps (a line repeated, an empty line added, a
line deleted in a row section) that changes the map, classed by what --check
says of it: a line named, said without a place, the loader's finding only, or
nothing new. Run from the repository's root.

    python3 -I tools/shift/shipped_edits.py ./vtt OUT
"""
import json, os, subprocess, sys, collections
VTT, OUT = sys.argv[1], sys.argv[2]
os.makedirs(OUT + "/d", exist_ok=True); os.makedirs(OUT + "/r", exist_ok=True)
ENV = dict(os.environ, XDG_DATA_HOME=OUT + "/d", XDG_RUNTIME_DIR=OUT + "/r")
maps = ["tests/fixtures/areas.vtt","tests/fixtures/crowd.vtt","tests/fixtures/env.vtt","tests/fixtures/everything.vtt",
        "tests/fixtures/floors.vtt","tests/fixtures/kinds.vtt","tests/fixtures/links.vtt","tests/fixtures/two-rooms.vtt",
        "tests/oracle/maps/v3.vtt","tests/oracle/maps/v10.vtt","tests/oracle/maps/v12.vtt","tests/oracle/maps/w024.vtt"]
ROWCODES = {"E011","W019","W031","N021","W022","E010"}
def run(path, *a): return subprocess.run([VTT, path, *a], capture_output=True, text=True, env=ENV).stdout
def dump(path): return run(path, "--dump-map")
cat = collections.Counter(); examples = collections.defaultdict(list)
for mp in maps:
    lines = open(mp).read().split("\n")
    d0 = dump(mp)
    base_codes = collections.Counter(f["code"] for f in json.loads(run(mp, "--check", "--json"))["findings"])
    sec = None
    for i, l in enumerate(lines):
        if l in ("tiles", "vedges", "hedges"): sec = l; continue
        if sec is None: continue
        if l and l.split(" ")[0] in ("fog","token","note","link","area","scene","card","end","floor","name","size","zoom","grid") : sec = None; continue
        for op in ("repeat", "empty", "delete"):
            m = lines[:]
            if op == "repeat": m.insert(i, l)
            elif op == "empty": m.insert(i, "")
            else: del m[i]
            p = OUT + "/bad.vtt"; open(p, "w").write("\n".join(m))
            if dump(p) == d0: continue
            fs = json.loads(run(p, "--check", "--json"))["findings"]
            codes = collections.Counter(f["code"] for f in fs)
            rows = {c for c in codes if c in ROWCODES}
            new = codes - base_codes
            if any(f.get("span") for f in fs): k = "a line named"
            elif any("cannot tell which" in f["message"] or "missing row is one of" in (f.get("help") or "")
                     or "lines where the map has" in f["message"] or "more lines than its" in f["message"] for f in fs): k = "said, no place"
            elif rows: k = "loader only: " + ",".join(sorted(rows))
            elif new: k = "map findings only (new: " + ",".join(sorted(new)) + ")"
            else: k = "nothing new at all"
            cat[k] += 1
            if len(examples[k]) < 3: examples[k].append((mp, sec, op, i + 1))
for k, v in cat.most_common(): print("%5d  %s   e.g. %s" % (v, k, examples[k][:2]))
