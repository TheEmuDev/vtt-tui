#!/usr/bin/env python3
"""Damaged maps for the line search (--check's W031 / E011 with a span): a told
place must be right. From the line search's third review (docs/V2.md, step 5).

    python3 -I tools/shift/gen_shapes.py ./vtt OUT [ROUNDS] [SEED]
    env TWO=1 two edits at once; SEP=0..3 separators; OPS=01234; KINDS=doors,...

Prints WRONG for a span no fix in it can make right (the file kept in OUT, with
.orig): judge those by map with judge.py -- with TWO=1 this generator compares
text, so a fix that restores the map is flagged too.

New shapes: outlines of doors/windows/secret doors; walls with gaps; rooms with
freestanding wall segments in void by design; 512-wide maps with few rows; long
runs of identical rows; thick (double) outlines; a cave whose blob rows differ
(unique rows); rooms with a 2x2/3x3 token straddling rows.
New edits: op 3 pastes a copy of a NON-adjacent row (a unique line in context);
op 4 deletes a row unique in its section; TWO=1 damages two sections at once.
SEP=3 puts a separator after exactly the first two sections and edits the third.
"""
import json, os, random, subprocess, sys, shutil

VTT = sys.argv[1]
OUT = sys.argv[2]
ROUNDS = int(sys.argv[3]) if len(sys.argv) > 3 else 200
SEED = int(sys.argv[4]) if len(sys.argv) > 4 else 1
rng = random.Random(SEED)
os.makedirs(OUT, exist_ok=True)
ENV = dict(os.environ, XDG_DATA_HOME=OUT + "/d", XDG_RUNTIME_DIR=OUT + "/r")
os.makedirs(ENV["XDG_DATA_HOME"], exist_ok=True); os.makedirs(ENV["XDG_RUNTIME_DIR"], exist_ok=True)

class M:
    def __init__(s, w, h):
        s.w, s.h = w, h
        s.t = [[' '] * w for _ in range(h)]
        s.v = [[' '] * (w + 1) for _ in range(h)]
        s.hh = [[' '] * w for _ in range(h + 1)]
        s.tokens = []; s.notes = []; s.extra = []
    def fill(s, x0, y0, x1, y1, ch='.'):
        for y in range(max(0, y0), min(s.h, y1 + 1)):
            for x in range(max(0, x0), min(s.w, x1 + 1)):
                s.t[y][x] = ch
    def rect_walls(s, x0, y0, x1, y1, ch=None):
        for y in range(y0, y1 + 1):
            if 0 <= y < s.h:
                if 0 <= x0 <= s.w: s.v[y][x0] = ch or '|'
                if 0 <= x1 + 1 <= s.w: s.v[y][x1 + 1] = ch or '|'
        for x in range(x0, x1 + 1):
            if 0 <= x < s.w:
                if 0 <= y0 <= s.h: s.hh[y0][x] = ch or '-'
                if 0 <= y1 + 1 <= s.h: s.hh[y1 + 1][x] = ch or '-'

def rooms(m, nmin=1, nmax=5, maxw=12, maxh=10):
    out = []
    for r in range(rng.randint(nmin, nmax)):
        rw, rh = rng.randint(2, min(maxw, m.w)), rng.randint(2, min(maxh, m.h))
        x0, y0 = rng.randint(0, m.w - rw), rng.randint(0, m.h - rh)
        m.fill(x0, y0, x0 + rw - 1, y0 + rh - 1)
        m.rect_walls(x0, y0, x0 + rw - 1, y0 + rh - 1)
        out.append((x0, y0, rw, rh))
    return out

def shape(kind):
    if kind == 'doors':         # outlines drawn mostly with doors, windows, secret doors
        w, h = rng.randint(6, 50), rng.randint(3, 40)
        m = M(w, h)
        for (x0, y0, rw, rh) in rooms(m):
            for y in range(y0, y0 + rh):
                for x in (x0, x0 + rw):
                    if rng.random() < .6: m.v[y][x] = rng.choice('+/%S')
            for x in range(x0, x0 + rw):
                for y in (y0, y0 + rh):
                    if rng.random() < .6: m.hh[y][x] = rng.choice('+/%S')
            if rng.random() < .6: m.tokens.append((x0 + rng.randrange(rw), y0 + rng.randrange(rh), 1, 'Ogre'))
    elif kind == 'gappy':       # walls with gaps
        w, h = rng.randint(6, 50), rng.randint(3, 40)
        m = M(w, h)
        for (x0, y0, rw, rh) in rooms(m):
            for g in range(rng.randint(1, 4)):
                if rng.random() < .5: m.v[y0 + rng.randrange(rh)][rng.choice([x0, x0 + rw])] = ' '
                else: m.hh[rng.choice([y0, y0 + rh])][x0 + rng.randrange(rw)] = ' '
            if rng.random() < .6: m.tokens.append((x0 + rng.randrange(rw), y0 + rng.randrange(rh), 1, 'Ogre'))
    elif kind == 'voidwalls':   # rooms plus freestanding wall segments in void by design
        w, h = rng.randint(8, 50), rng.randint(5, 40)
        m = M(w, h)
        rs = rooms(m, 1, 3)
        for g in range(rng.randint(1, 4)):
            if rng.random() < .5:
                x = rng.randrange(w + 1); y0 = rng.randrange(h); y1 = min(h - 1, y0 + rng.randint(0, 4))
                for y in range(y0, y1 + 1): m.v[y][x] = '|'
            else:
                y = rng.randrange(h + 1); x0 = rng.randrange(w); x1 = min(w - 1, x0 + rng.randint(0, 6))
                for x in range(x0, x1 + 1): m.hh[y][x] = '-'
        x0, y0, rw, rh = rs[0]
        m.tokens.append((x0, y0, 1, 'Ogre'))
    elif kind == 'thick':       # double outline: walls one row/col outside the floor too (on void by design)
        w, h = rng.randint(8, 50), rng.randint(5, 40)
        m = M(w, h)
        for (x0, y0, rw, rh) in rooms(m, 1, 3):
            m.rect_walls(x0 - 1, y0 - 1, x0 + rw, y0 + rh)
            if rng.random() < .6: m.tokens.append((x0 + rng.randrange(rw), y0 + rng.randrange(rh), 1, 'Ogre'))
    elif kind == 'wide':        # 512 columns, few rows
        w, h = 512, rng.randint(1, 5)
        m = M(w, h)
        for r in range(rng.randint(1, 6)):
            rw, rh = rng.randint(2, 60), rng.randint(1, h)
            x0, y0 = rng.randint(0, w - rw), rng.randint(0, h - rh)
            m.fill(x0, y0, x0 + rw - 1, y0 + rh - 1); m.rect_walls(x0, y0, x0 + rw - 1, y0 + rh - 1)
            if rng.random() < .5: m.tokens.append((x0, y0, 1, 'Ogre%d' % r))
    elif kind == 'runs':        # long runs of identical rows: a wide hall, a corridor crossing
        w, h = rng.randint(6, 40), rng.randint(10, 80)
        m = M(w, h)
        x0, rw = rng.randint(0, w - 3), rng.randint(3, min(12, w))
        rw = min(rw, w - x0)
        y0, rh = rng.randint(0, h - 8), rng.randint(8, min(60, h))
        rh = min(rh, h - y0)
        m.fill(x0, y0, x0 + rw - 1, y0 + rh - 1); m.rect_walls(x0, y0, x0 + rw - 1, y0 + rh - 1)
        if rng.random() < .5:
            y = y0 + rng.randrange(rh); m.v[y][x0] = '+'
        m.tokens.append((x0 + rng.randrange(rw), y0 + rng.randrange(rh), 1, 'Rat'))
    elif kind == 'ucave':       # a cave whose every row differs (unique rows), with a big token
        w, h = rng.randint(8, 40), rng.randint(5, 30)
        m = M(w, h)
        cx = w // 2
        for y in range(h):
            half = rng.randint(0, w // 2)
            m.fill(cx - half, y, cx + half, y)
        for r in range(rng.randint(0, 3)):
            m.tokens.append((rng.randrange(w - 2), rng.randrange(h - 2), rng.choice([1, 2, 3]), 'Wyrm%d' % r))
    elif kind == 'bigtok':      # walled rooms, a 2x2 or 3x3 token straddling rows
        w, h = rng.randint(8, 50), rng.randint(5, 40)
        m = M(w, h)
        rs = rooms(m, 1, 4, 12, 10)
        for (x0, y0, rw, rh) in rs:
            sz = rng.choice([2, 3])
            if rw >= sz and rh >= sz: m.tokens.append((x0 + rng.randrange(rw - sz + 1), y0 + rng.randrange(rh - sz + 1), sz, 'Dragon'))
        if rng.random() < .5: m.notes.append((rng.randrange(w), rng.randrange(h), 'a note'))
    elif kind == 'mixed':       # a walled room and an unwalled blob on one map
        w, h = rng.randint(10, 50), rng.randint(6, 40)
        m = M(w, h)
        rooms(m, 1, 2)
        for r in range(rng.randint(1, 3)):
            rw, rh = rng.randint(2, min(10, w)), rng.randint(2, min(8, h))
            x0, y0 = rng.randint(0, w - rw), rng.randint(0, h - rh)
            m.fill(x0, y0, x0 + rw - 1, y0 + rh - 1, rng.choice('.~:'))
        m.tokens.append((rng.randrange(w), rng.randrange(h), 1, 'Bat'))
    else:
        raise ValueError(kind)
    return m

def rowtext(row, style):
    s = ''.join(row)
    if style == '2.0':
        return '' if s.strip() == '' else s
    return s

def write(m, style, sep, order, crlf, tail_records):
    L = ['VTT 14', 'name gen', 'size %d %d' % (m.w, m.h), 'zoom 1']
    secs = {'tiles': ['tiles'] + [rowtext(r, style) for r in m.t],
            'vedges': ['vedges'] + [rowtext(r, style) for r in m.v],
            'hedges': ['hedges'] + [rowtext(r, style) for r in m.hh]}
    toks = ['token %s %d %d %d "%s"' % (rng.choice(['player', 'enemy', 'ally']), x, y, sz, lab) for (x, y, sz, lab) in m.tokens]
    toks += ['note %d %d "%s"' % (x, y, t) for (x, y, t) in m.notes]
    toks += m.extra
    for i, name in enumerate(order):
        L += secs[name]
        if sep == 1 or (sep == 2 and rng.random() < .5) or (sep == 3 and i < 2): L.append('')
    L += toks
    if tail_records:
        L += ['scene "Ambush"', 'token enemy 0 0 1 "Imp"', 'endscene', 'end']
    nl = '\r\n' if crlf else '\n'
    return nl.join(L) + nl, L

def run_check(path):
    p = subprocess.run([VTT, path, '--check', '--json'], env=ENV, capture_output=True, text=True, timeout=60)
    if p.returncode > 2 or p.returncode < 0:
        return None, p
    try:
        return json.loads(p.stdout), p
    except Exception:
        return None, p

def dump(path):
    p = subprocess.run([VTT, path, '--dump-map'], env=ENV, capture_output=True, text=True, timeout=60)
    return p.stdout

stats = {'trials': 0, 'found': 0, 'silent': 0, 'silent_harm': 0, 'nowhere': 0, 'wrong': 0, 'crash': 0, 'falsealarm': 0}
by_shape = {}
kinds = os.environ.get('KINDS', 'doors,gappy,voidwalls,thick,wide,runs,ucave,bigtok,mixed').split(',')
TWO = os.environ.get('TWO') == '1'
OPS = [int(c) for c in os.environ.get('OPS', '01234')]

def edit(lines, hdr, order, m, sec, op):
    """One edit of section sec; returns (new lines, 1-based line of the change, texts that undo it)."""
    nrows = m.h + 1 if sec == 'hedges' else m.h
    h0 = hdr[sec]
    rowlines = lines[h0 + 1:h0 + 1 + nrows]
    if op == 4:
        uniq = [i for i, l in enumerate(rowlines) if rowlines.count(l) == 1]
        if not uniq: op = 2
        r = rng.choice(uniq) if uniq else rng.randrange(nrows)
    else:
        r = rng.randrange(nrows)
    at = h0 + 1 + r
    mut = []
    changed = None
    paste = None
    if op == 3:
        others = [i for i in range(nrows) if abs(i - r) > 1 and rowlines[i] != rowlines[r] and rowlines[i] != '']
        if not others: op = 0
        else: paste = rowlines[rng.choice(others)]
    for i, l in enumerate(lines):
        if op in (2, 4) and i == at: continue
        mut.append(l)
        if i == at and op == 0: mut.append(l); changed = len(mut)
        if i == at and op == 1: mut.append(''); changed = len(mut)
        if i == at and op == 3: mut.append(paste); changed = len(mut)
    if op in (2, 4): changed = at
    return mut, changed, op

for rnd in range(ROUNDS):
    kind = rng.choice(kinds)
    m = shape(kind)
    style = rng.choice(['2.0', '2.0', '1.x'])
    sep = rng.choice([0, 0, 1, 2, 3]) if os.environ.get('SEP') is None else int(os.environ['SEP'])
    order = ['tiles', 'vedges', 'hedges']
    if rng.random() < .3: rng.shuffle(order)
    crlf = rng.random() < .1
    tail_records = rng.random() < .3
    text, L = write(m, style, sep, order, crlf, tail_records)
    nl = '\r\n' if crlf else '\n'
    base = os.path.join(OUT, 'base.vtt')
    with open(base, 'w', newline='') as f: f.write(text)
    j0, p0 = run_check(base)
    if j0 is None:
        stats['crash'] += 1; shutil.copy(base, os.path.join(OUT, 'crash-base-%d.vtt' % rnd)); print('CRASH on base', rnd, p0.returncode, p0.stderr[-300:]); continue
    bad0 = [f for f in j0['findings'] if f.get('span') or f['code'] in ('W031',) or (f['code'] == 'E011')]
    if bad0:
        stats['falsealarm'] += 1
        shutil.copy(base, os.path.join(OUT, 'falsealarm-%d-%s.vtt' % (rnd, kind)))
        print('FALSE ALARM on an unedited file', rnd, kind, [(f['code'], f['message'], f.get('span')) for f in bad0])
    d0 = dump(base)
    lines = L[:]
    hdr = {name: i for i, name in enumerate(lines) if name in ('tiles', 'vedges', 'hedges')}
    for t in range(4):
        sec = order[int(os.environ['EDITIDX'])] if 'EDITIDX' in os.environ else (order[2] if sep == 3 else rng.choice(order))
        op = rng.choice(OPS)
        mut, changed, op = edit(lines, hdr, order, m, sec, op)
        goals = [lines]            # texts a right fix may restore
        tag = '%s-op%d-line%d' % (sec, op, changed)
        if TWO:
            # a second edit, in another section, below or above
            sec2 = rng.choice([s for s in order if s != sec])
            hdr2 = {name: i for i, name in enumerate(mut) if name in ('tiles', 'vedges', 'hedges')}
            mut2, changed2, op2 = edit(mut, hdr2, order, m, sec2, rng.choice(OPS))
            # the right fixes: undo edit 2 (gives mut) or undo edit 1 as it stands in mut2
            goals = [mut]
            # undoing edit 1 inside mut2: recompute by applying edit 2 to the original
            hdrL = {name: i for i, name in enumerate(lines) if name in ('tiles', 'vedges', 'hedges')}
            # replay edit 2 on the original with the same (sec2, op2, row) is not reproducible by rng; approximate:
            # a fix in the span must restore mut (one edit undone) or any text that differs from lines by
            # one line in sec2 -- checked loosely below by section counts.
            mut, changed = mut2, (changed, changed2)
            tag += '+%s-op%d-line%d' % (sec2, op2, changed2)
        mtext = nl.join(mut) + nl
        bad = os.path.join(OUT, 'bad.vtt')
        with open(bad, 'w', newline='') as f: f.write(mtext)
        stats['trials'] += 1
        bs = by_shape.setdefault(kind, {'trials': 0, 'found': 0, 'wrong': 0, 'silent_harm': 0, 'nowhere': 0})
        bs['trials'] += 1
        j, p = run_check(bad)
        if j is None:
            stats['crash'] += 1; shutil.copy(bad, os.path.join(OUT, 'crash-%d-%d.vtt' % (rnd, t))); print('CRASH', rnd, t, p.returncode, p.stderr[-400:]); continue
        spans = [f for f in j['findings'] if f.get('span')]
        if not spans:
            if any('no place is told' in ' '.join(f.get('notes') or []) for f in j['findings']):
                stats['nowhere'] += 1; bs['nowhere'] += 1
                continue
            stats['silent'] += 1
            d1 = dump(bad)
            if d1 != d0:
                stats['silent_harm'] += 1; bs['silent_harm'] += 1
            continue
        f = spans[0]
        a, b = f['span']['first'], f['span']['last']
        ok = False
        if TWO:
            ok_exact = any(a <= c <= b for c in changed)
        else:
            ok_exact = a <= changed <= b
        # semantic: some fix within the span yields a goal text
        for ln in range(a, b + 1):
            fixed = mut[:ln - 1] + mut[ln:]
            if fixed in goals: ok = True; break
            fills = ['', mut[ln - 1] if ln - 1 < len(mut) else None, mut[ln] if ln < len(mut) else None]
            for g in goals:
                # the true lost line, whatever it was, put back at ln
                if len(g) == len(mut) + 1 and g[:ln] == mut[:ln] and g[ln + 1:] == mut[ln:]: ok = True
            for fill in fills:
                if fill is None: continue
                fixed = mut[:ln] + [fill] + mut[ln:]
                if fixed in goals: ok = True; break
            if ok: break
        if TWO and not ok:
            # loose: the fix line must lie in the edited section of one of the edits
            ok = ok_exact
        if ok or ok_exact:
            stats['found'] += 1; bs['found'] += 1
        else:
            stats['wrong'] += 1; bs['wrong'] += 1
            keep = os.path.join(OUT, 'wrong-%d-%d-%s-%s.vtt' % (rnd, t, kind, tag))
            shutil.copy(bad, keep)
            shutil.copy(base, keep + '.orig')
            print('WRONG', keep, f['code'], f['message'], 'span', a, b, '| help:', f.get('help'))
print(json.dumps(stats))
for k, v in sorted(by_shape.items()): print(k, v)
