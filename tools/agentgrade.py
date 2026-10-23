#!/usr/bin/env python3
"""The grader of ROADMAP 27's evaluations (docs/AGENTLOOP.md, part 1).

    tools/agentgrade.py RUNDIR [--vtt PATH]

RUNDIR holds `expect` (the facts, a line each), `calls.tsv` (the runner's log
of every vtt call the agent made) and `map.vtt` when the agent left one. Each
fact is answered by vtt itself -- `--check --json`, `--describe --json`, and
`describe AREA json` through `--apply` for the rooms that meet an area -- run
through tools/sandbox.sh on a copy of the map, so nothing of the run's is
written to and no setting or key of the user's is reached. A fact a model
would have to judge is not a fact here.

Prints a line a fact, `pass: FACT` or `FAIL: FACT: what was found`, then
`N of M facts`. Exit 0 when every fact passed, 1 when one did not, 2 when
`expect` holds a line that is not a fact (it is never passed over).

The facts:
    check clean                    errors and warnings 0
    COUNT OP N                     COUNT one of: rooms creatures areas floors
                                   links errors warnings calls refusals; OP == != <= >= < >
    reachable all                  every room reachable
    area NAME                      an area of that name (any case)
    door A B                       a room with a square in area A has a door
                                   (of any kind but a window) into a room
                                   with a square in B
    joined A B                     a way from A to B: doors as above, open
                                   ground, links (a one-way link only its way)
    creature WORD [in AREA]        a creature whose label holds WORD (any
                                   case), a square of it in AREA's box when named
    creatures WORD OP N            how many labels hold WORD
    terrain AREA KIND              a square of AREA's box is KIND ground
"""
import json
import os
import shlex
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
COUNTS = ("rooms", "creatures", "areas", "floors", "links", "errors", "warnings", "calls", "refusals")
OPS = {"==": lambda a, b: a == b, "!=": lambda a, b: a != b, "<=": lambda a, b: a <= b,
       ">=": lambda a, b: a >= b, "<": lambda a, b: a < b, ">": lambda a, b: a > b}
# The one opening a party does not pass (src/maptools.c, edge_passable_kind):
# describe lists only openings, never a wall, so every other kind is a way
# through. Its name is edge_name's; tests/agenteval.sh fails if it changes.
CLOSED = "window"


class NotAFact(Exception):
    pass


class Run:
    """What vtt says of one run's map, asked once each and kept."""

    def __init__(self, rundir, vtt):
        self.dir = rundir
        self.vtt = vtt
        self.tmp = tempfile.mkdtemp(prefix="vtt-grade.")
        self.has_map = os.path.exists(os.path.join(rundir, "map.vtt"))
        if self.has_map:
            shutil.copy(os.path.join(rundir, "map.vtt"), os.path.join(self.tmp, "map.vtt"))
        self._check = self._describe = None
        self._area = {}
        self.calls = []
        path = os.path.join(rundir, "calls.tsv")
        if os.path.exists(path):
            with open(path, encoding="utf-8", errors="replace") as f:
                self.calls = [ln.rstrip("\n").split("\t") for ln in f if ln.strip()]

    def close(self):
        shutil.rmtree(self.tmp, ignore_errors=True)

    def _vtt(self, *args):
        """vtt in a sandbox of its own (tools/sandbox.sh: the one list of what
        a sandbox sets), from the copy's directory: its stdout, exit status."""
        cmd = [os.path.join(ROOT, "tools", "sandbox.sh"), "sh", "-c",
               "cd " + shlex.quote(self.tmp) + " && exec " + shlex.quote(self.vtt) + ' "$@"', "vtt"] + list(args)
        p = subprocess.run(cmd, capture_output=True, text=True)
        return p.stdout, p.returncode

    def _json(self, *args):
        out, _ = self._vtt(*args)
        try:
            return json.loads(out)
        except ValueError:
            raise Failed("vtt could not read the map")

    def check(self):
        if self._check is None:
            self._check = self._json("map.vtt", "--check", "--json")
        return self._check

    def describe(self):
        if self._describe is None:
            self._describe = self._json("map.vtt", "--describe", "--json")
        return self._describe

    def area_rooms(self, name):
        """The rooms with a square in the area, as vtt's `describe AREA`
        tells them: room id -> its `in_region` (those squares and their
        terrain). None when the map has no such area."""
        key = name.lower()
        if key not in self._area:
            if self.area(name) is None:
                self._area[key] = None
            else:
                plan = os.path.join(self.tmp, "q.txt")
                with open(plan, "w", encoding="utf-8") as f:   # vtt reads the name's bytes as they are
                    f.write("describe %s json\n" % json.dumps(self.area(name)["name"], ensure_ascii=False))
                out, rc = self._vtt("map.vtt", "--apply", "q.txt")
                if rc != 0:
                    raise Failed("vtt did not describe area %s (exit %d)" % (name, rc))
                self._area[key] = {r["id"]: r["in_region"] for r in json.loads(out)["rooms"]}
        return self._area[key]

    def area(self, name):
        for a in self.describe()["areas"]:
            if a["name"].lower() == name.lower():
                return a
        return None


def count(run, what):
    if what == "calls":
        return len(run.calls)
    if what == "refusals":
        return sum(1 for c in run.calls if len(c) > 2 and c[2] != "-")
    if what in ("errors", "warnings"):
        return run.check()[what]
    if what in ("areas", "floors", "links"):
        return len(run.describe()[what])
    return run.describe()["map"][what]


def op_number(words, at):
    if len(words) != at + 2 or words[at] not in OPS:
        raise NotAFact("wants OP N, OP one of " + " ".join(OPS))
    try:
        return OPS[words[at]], int(words[at + 1])
    except ValueError:
        raise NotAFact("N is not a number")


def labels(run):
    for r in run.describe()["rooms"]:
        for c in r["creatures"]:
            yield c


def graph(run):
    """Room id -> the room ids a party can step to."""
    d = run.describe()
    g = {r["id"]: set() for r in d["rooms"]}
    for r in d["rooms"]:
        for door in r["doors"]:
            if door["kind"] != CLOSED and isinstance(door["to"], int):
                g[r["id"]].add(door["to"])
                g.setdefault(door["to"], set()).add(r["id"])
    for link in d["links"]:
        ends = [e["room"] for e in link["ends"]]       # one end for a link to another map
        if len(ends) != 2 or None in ends or ends[0] == ends[1]:
            continue
        g.setdefault(ends[0], set()).add(ends[1])
        if not link["oneway"]:                         # a one-way link runs from its first end only
            g.setdefault(ends[1], set()).add(ends[0])
    return g


def need_area(run, name):
    rooms = run.area_rooms(name)
    if rooms is None:
        raise Failed("no area " + name)
    return rooms


class Failed(Exception):
    pass


def shape(words):
    """Which fact the line is, from its words alone, before any is judged:
    a line that is no fact stops the grading whatever the map (with no map
    every fact fails, and a misspelt one would fail with them unseen)."""
    if not words:
        raise NotAFact("empty")
    w, n = words[0], len(words)
    if w == "creatures" and n == 4:
        op_number(words, 2)
        return "label-count"
    if w in COUNTS:
        op_number(words, 1)
        return "count"
    if words in (["check", "clean"], ["reachable", "all"]):
        return w
    if (w, n) in (("area", 2), ("door", 3), ("joined", 3), ("terrain", 3)):
        return w
    if w == "creature" and (n == 2 or (n == 4 and words[2] == "in")):
        return w
    raise NotAFact("not a fact this grader knows (tools/agentgrade.py lists them)")


def judge(run, kind, words):
    """Nothing when the fact holds; Failed with what was found when not."""
    if not run.has_map and not (kind == "count" and words[0] in ("calls", "refusals")):
        raise Failed("no map was made")
    if kind == "count":
        f, n = op_number(words, 1)
        got = count(run, words[0])
        if not f(got, n):
            raise Failed("%s is %d" % (words[0], got))
    elif kind == "check":
        c = run.check()
        if c["errors"] or c["warnings"]:
            codes = sorted({x["code"] for x in c["findings"]})
            raise Failed("%d errors, %d warnings (%s)" % (c["errors"], c["warnings"], " ".join(codes)))
    elif kind == "reachable":
        bad = ["%s (%s)" % (r["area"], r["name"]) if r["area"] else r["name"]
               for r in run.describe()["rooms"] if not r["reachable"]]
        if bad:
            raise Failed("not reachable: " + " ".join(bad))
    elif kind == "area":
        if run.area(words[1]) is None:
            raise Failed("areas: " + (" ".join(a["name"] for a in run.describe()["areas"]) or "none"))
    elif kind in ("door", "joined"):
        a, b = need_area(run, words[1]), need_area(run, words[2])
        g = graph(run)
        if kind == "door":
            if not any(y in g.get(x, ()) for x in a for y in b if x != y):
                raise Failed("no door between them")
            return
        seen, todo = set(a), list(a)
        while todo:
            for y in g.get(todo.pop(), ()):
                if y not in seen:
                    seen.add(y)
                    todo.append(y)
        if not seen & set(b):
            raise Failed("no way from %s to %s" % (words[1], words[2]))
    elif kind == "creature":
        hits = [c for c in labels(run) if words[1].lower() in c["label"].lower()]
        if not hits:
            raise Failed("no creature labeled with " + words[1])
        if len(words) == 4:
            box = run.area(words[3])
            if box is None:
                raise Failed("no area " + words[3])
            # Any square of its footprint (size 2 and 3 run right and down from x, y).
            if not any(c["x"] <= box["x1"] and c["x"] + c["size"] - 1 >= box["x0"] and
                       c["y"] <= box["y1"] and c["y"] + c["size"] - 1 >= box["y0"] for c in hits):
                raise Failed("at " + " ".join(c["at"] for c in hits))
    elif kind == "label-count":
        f, n = op_number(words, 2)
        got = sum(1 for c in labels(run) if words[1].lower() in c["label"].lower())
        if not f(got, n):
            raise Failed("%d labeled with %s" % (got, words[1]))
    elif kind == "terrain":
        rooms = need_area(run, words[1])
        if not any(part["terrain"].get(words[2], 0) for part in rooms.values()):
            raise Failed("none in " + words[1])


def main(argv):
    if len(argv) not in (2, 4) or (len(argv) == 4 and argv[2] != "--vtt"):
        print("usage: tools/agentgrade.py RUNDIR [--vtt PATH]", file=sys.stderr)
        return 2
    rundir = argv[1]
    vtt = os.path.abspath(argv[3]) if len(argv) == 4 else os.path.join(ROOT, "vtt")
    facts = []
    with open(os.path.join(rundir, "expect"), encoding="utf-8") as f:
        for n, line in enumerate(f, 1):
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            try:
                words = shlex.split(line)
                facts.append((line, shape(words), words))
            except (NotAFact, ValueError) as e:
                print("agentgrade: expect line %d: %s: %s" % (n, line, e), file=sys.stderr)
                return 2
    run = Run(rundir, vtt)
    passed = 0
    try:
        for line, kind, words in facts:
            try:
                judge(run, kind, words)
                passed += 1
                print("pass: " + line)
            except Failed as e:
                print("FAIL: %s: %s" % (line, e))
    finally:
        run.close()
    print("%d of %d facts" % (passed, len(facts)))
    return 0 if passed == len(facts) else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
