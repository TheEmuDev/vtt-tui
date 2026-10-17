#!/usr/bin/env python3
"""Damaged maps by named scenario, for the line search: every placed line checked
against the original by section, and every count-only W031 and no-place E011
claim checked true. From the line search's ninth review (docs/V2.md, step 5).

    python3 -I tools/shift/gen_scenarios.py ./vtt OUT [ROUNDS] [SEED]
    env SCEN=dblsep|triple|onesep2|tiny|hidden|tinyform|sepwant1|junk|twoextra|
             odd|twolost|paste|repeat|all;  SAY=1 keeps silent files too

Prints WRONG (a wrong place), CLAIM (an untrue count or reason), FP (a finding
about an undamaged section), and a JSON tally."""
import json, os, random, subprocess, sys, shutil, re

VTT, OUT = sys.argv[1], sys.argv[2]
ROUNDS = int(sys.argv[3]) if len(sys.argv) > 3 else 100
SEED = int(sys.argv[4]) if len(sys.argv) > 4 else 1
SCEN = os.environ.get('SCEN', 'all')
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
        s.tokens = []
    def fill(s, x0, y0, x1, y1, ch='.'):
        for y in range(max(0, y0), min(s.h, y1 + 1)):
            for x in range(max(0, x0), min(s.w, x1 + 1)):
                s.t[y][x] = ch
    def rect_walls(s, x0, y0, x1, y1):
        for y in range(y0, y1 + 1):
            if 0 <= y < s.h:
                if 0 <= x0 <= s.w: s.v[y][x0] = '|'
                if 0 <= x1 + 1 <= s.w: s.v[y][x1 + 1] = '|'
        for x in range(x0, x1 + 1):
            if 0 <= x < s.w:
                if 0 <= y0 <= s.h: s.hh[y0][x] = '-'
                if 0 <= y1 + 1 <= s.h: s.hh[y1 + 1][x] = '-'

def rooms(m, nmin=1, nmax=5, maxw=12, maxh=10):
    for r in range(rng.randint(nmin, nmax)):
        rw, rh = rng.randint(2, min(maxw, m.w)), rng.randint(2, min(maxh, m.h))
        x0, y0 = rng.randint(0, m.w - rw), rng.randint(0, m.h - rh)
        m.fill(x0, y0, x0 + rw - 1, y0 + rh - 1)
        m.rect_walls(x0, y0, x0 + rw - 1, y0 + rh - 1)
        if rng.random() < .5: m.tokens.append((x0 + rng.randrange(rw), y0 + rng.randrange(rh), 1, 'Ogre%d' % r))
        if rng.random() < .3:   # a door in the top wall
            m.hh[y0][x0 + rng.randrange(rw)] = '+'

def shape(kind):
    if kind == 'rooms':
        w, h = rng.randint(6, 40), rng.randint(6, 40)
        m = M(w, h); rooms(m)
    elif kind == 'hall':
        w, h = rng.randint(8, 40), rng.randint(8, 40)
        m = M(w, h)
        x0, y0 = rng.randint(0, 2), rng.randint(0, 2)
        x1, y1 = w - 1 - rng.randint(0, 2), h - 1 - rng.randint(0, 2)
        m.fill(x0, y0, x1, y1); m.rect_walls(x0, y0, x1, y1)
        for p in range(rng.randint(0, 3)):
            px, py = rng.randint(x0 + 1, x1 - 1), rng.randint(y0 + 1, y1 - 1)
            m.t[py][px] = ' '; m.rect_walls(px, py, px, py)
        m.tokens.append((rng.randint(x0, x1), rng.randint(y0, y1), 1, 'Rat'))
    elif kind == 'wide':
        w, h = 512, rng.randint(2, 6)
        m = M(w, h)
        for r in range(rng.randint(1, 6)):
            rw, rh = rng.randint(2, 60), rng.randint(1, h)
            x0, y0 = rng.randint(0, w - rw), rng.randint(0, h - rh)
            m.fill(x0, y0, x0 + rw - 1, y0 + rh - 1); m.rect_walls(x0, y0, x0 + rw - 1, y0 + rh - 1)
    elif kind == 'tiny':
        w, h = rng.choice([1, 1, 2, 3]), rng.choice([1, 1, 2, 3])
        m = M(w, h)
        if rng.random() < .7:
            m.fill(0, 0, w - 1, h - 1); m.rect_walls(0, 0, w - 1, h - 1)
        if rng.random() < .5: m.tokens.append((rng.randrange(w), rng.randrange(h), 1, 'Imp'))
    elif kind == 'voidwalls':
        w, h = rng.randint(8, 40), rng.randint(6, 40)
        m = M(w, h); rooms(m, 1, 3)
        for g in range(rng.randint(1, 3)):
            if rng.random() < .5:
                x = rng.randrange(w + 1); y0 = rng.randrange(h); y1 = min(h - 1, y0 + rng.randint(0, 4))
                for y in range(y0, y1 + 1): m.v[y][x] = '|'
            else:
                y = rng.randrange(h + 1); x0 = rng.randrange(w); x1 = min(w - 1, x0 + rng.randint(0, 6))
                for x in range(x0, x1 + 1): m.hh[y][x] = '-'
    elif kind == 'cave':   # plain floor, no walls: nothing tells a place
        w, h = rng.randint(4, 30), rng.randint(4, 30)
        m = M(w, h)
        m.fill(rng.randint(0, 2), rng.randint(0, 2), w - 1 - rng.randint(0, 2), h - 1 - rng.randint(0, 2))
        if rng.random() < .5: m.tokens.append((rng.randrange(w), rng.randrange(h), 1, 'Bat'))
    else:
        raise ValueError(kind)
    return m

def rowtext(row, style):
    s = ''.join(row)
    return '' if style == '2.0' and s.strip() == '' else s

NAMES = ['tiles', 'vedges', 'hedges']

def write(m, style, order, blanks_after, crlf=False, tail=False, junk_after=None):
    L = ['VTT 14', 'name gen', 'size %d %d' % (m.w, m.h), 'zoom 1']
    secs = {'tiles': ['tiles'] + [rowtext(r, style) for r in m.t],
            'vedges': ['vedges'] + [rowtext(r, style) for r in m.v],
            'hedges': ['hedges'] + [rowtext(r, style) for r in m.hh]}
    for name in order:
        L += secs[name]
        if junk_after and name in junk_after: L += junk_after[name]
        L += [''] * blanks_after.get(name, 0)
    L += ['token %s %d %d %d "%s"' % (rng.choice(['player', 'enemy', 'ally']), x, y, sz, lab) for (x, y, sz, lab) in m.tokens]
    if tail: L += ['scene "Ambush"', 'token enemy 0 0 1 "Imp"', 'endscene', 'end']
    return L, ('\r\n' if crlf else '\n')

def run_check(path):
    p = subprocess.run([VTT, path, '--check', '--json'], env=ENV, capture_output=True, text=True, timeout=120)
    if p.returncode > 2 or p.returncode < 0: return None, p
    try: return json.loads(p.stdout), p
    except Exception: return None, p

RECORD = re.compile(r'^(token|tokenstatus|tokenturn|tokencounter|tokennote|tokenhidden|tokencard|card|endcard|note|area|floor|env|areaenv|link|spotlight|clock|roll|round|name|size|zoom|scale|ruleset|rule|metric|scene|endscene|end|fog|fogpatch)( |$)')

def secs_of(lines):
    """Section name -> list of (1-based line, text), as mapio_sections splits them
    (a header or a record ends a section)."""
    out = {}; cur = None
    for i, l in enumerate(lines, 1):
        if l in ('tiles', 'vedges', 'hedges', 'fog'): cur = l; out[cur] = []; continue
        if cur is not None and RECORD.match(l): cur = None; continue
        if cur is not None: out[cur].append((i, l))
    return out

def strip_tail(a):
    a = list(a)
    while a and a[-1].strip() == '': a.pop()
    return a

def texts(pairs): return [t for _, t in pairs]

def section_rows(name, m):
    return m.h + 1 if name == 'hedges' else m.h

def hdr(lines):
    return {name: i for i, name in enumerate(lines) if name in NAMES}

def ed_dup(lines, m, sec, r=None):
    h0 = hdr(lines)[sec]; nr = section_rows(sec, m)
    r = rng.randrange(nr) if r is None else r
    at = h0 + 1 + r
    return lines[:at + 1] + [lines[at]] + lines[at + 1:], at + 2
def ed_empty(lines, m, sec, r=None):
    h0 = hdr(lines)[sec]; nr = section_rows(sec, m)
    r = rng.randrange(nr) if r is None else r
    at = h0 + 1 + r
    return lines[:at + 1] + [''] + lines[at + 1:], at + 2
def ed_del(lines, m, sec, r=None):
    h0 = hdr(lines)[sec]; nr = section_rows(sec, m)
    r = rng.randrange(nr) if r is None else r
    at = h0 + 1 + r
    return lines[:at] + lines[at + 1:], at + 1

def ed_paste(lines, m, sec, r=None):
    h0 = hdr(lines)[sec]; nr = section_rows(sec, m)
    if nr < 3: return ed_dup(lines, m, sec, r)
    r = rng.randrange(nr) if r is None else r
    src = rng.choice([x for x in range(nr) if abs(x - r) > 1] or [r])
    at = h0 + 1 + r
    return lines[:at + 1] + [lines[h0 + 1 + src]] + lines[at + 1:], at + 2
def odd_row(lines, m, sec, r=None, ch='x'):
    h0 = hdr(lines)[sec]; nr = section_rows(sec, m)
    r = nr - 1 if r is None else r
    at = h0 + 1 + r
    row = lines[at]
    wd = section_width(sec, m)
    row = row + ' ' * (wd - len(row)) if len(row) < wd else row
    c = rng.randrange(wd)
    lines = list(lines); lines[at] = row[:c] + ch + row[c + 1:]
    return lines
def section_width(name, m):
    return m.w + 1 if name == 'vedges' else m.w

def eq_rows(a, b):
    """Two sections' rows the same map (trailing blank rows aside, spaces aside)."""
    a, b = strip_tail(a), strip_tail(b)
    return len(a) == len(b) and all(x.rstrip(' ') == y.rstrip(' ') for x, y in zip(a, b))

def is_subseq(small, big):
    it = iter(big)
    return all(any(x == y for y in it) for x in small)

def named(f):
    """The section a finding is about."""
    msg = f['message']
    for pat in (r"a '(\w+)' row is missing", r"^'(\w+)' has", r"inside '(\w+)'", r"read as (\w+) row"):
        mm = re.search(pat, msg)
        if mm: return mm.group(1)
    return None

def sec_ok(fixed, orig, S):
    """Section S of the fixed file holds the original's rows (trailing blank rows aside)."""
    so = texts(secs_of(orig).get(S, []))
    sf = texts(secs_of(fixed).get(S, []))
    return eq_rows(sf, so)

def right_fix(f, bad, orig, m, nl='\n'):
    """Some line in the span, deleted (extra) or with a row put back after it
    (lost), gives the named section its original rows."""
    a, b = f['span']['first'], f['span']['last']
    S = named(f)
    names = [S] if S else NAMES
    so = {k: texts(v) for k, v in secs_of(orig).items()}
    for ln in range(a, b + 1):
        if f['code'] == 'W031' or 'too many' in f['message']:
            if any(sec_ok(bad[:ln - 1] + bad[ln:], orig, k) for k in names): return True
        else:
            rows = set([''])
            for k in names: rows |= set(so.get(k, []))
            for row in rows:
                if any(sec_ok(bad[:ln] + [row] + bad[ln:], orig, k) for k in names): return True
    return False

def count_only_true(f, bad, orig, m, nl='\n'):
    mm = re.match(r"'(\w+)' has (\d+) lines where the map has (\d+) rows: (?:one|some) of lines (\d+)-(\d+)", f['message'])
    if not mm: return False, 'unparsed'
    S, N, Mr, A, B = mm.group(1), int(mm.group(2)), int(mm.group(3)), int(mm.group(4)), int(mm.group(5))
    if N - Mr == 1:
        for ln in range(A, B + 1):
            if sec_ok(bad[:ln - 1] + bad[ln:], orig, S): return True, ''
        return False, 'no single line in %d-%d restores %s' % (A, B, S)
    so = strip_tail(texts(secs_of(orig)[S]))
    sb = [t for ln, t in secs_of(bad)[S] if A <= ln <= B]
    if is_subseq([x.rstrip(' ') for x in so], [x.rstrip(' ') for x in sb]): return True, ''
    return False, "the original's rows are not a subsequence of lines %d-%d" % (A, B)

def nowhere_true(f, bad, orig, m, nl='\n'):
    mm = re.search(r"the missing row is one of the (\d+) after line (\d+)", f.get('help') or '')
    if not mm: return True, ''
    N, H = int(mm.group(1)), int(mm.group(2))
    S = named(f)
    rows = set(['']) | set(texts(secs_of(orig).get(S, [])))
    for ln in range(H, H + N):
        for row in rows:
            if sec_ok(bad[:ln] + [row] + bad[ln:], orig, S): return True, ''
    return False, 'no row put back after lines %d-%d restores %s' % (H, H + N - 1, S)

stats = {'trials': 0, 'placed': 0, 'wrong': 0, 'either': 0, 'nowhere': 0, 'count_only': 0, 'claim_false': 0, 'silent': 0, 'crash': 0, 'short_only': 0}

def trial(tag, lines, orig, nl):
    bad = os.path.join(OUT, 'bad.vtt')
    with open(bad, 'w', newline='') as f: f.write(nl.join(lines) + nl)
    stats['trials'] += 1
    j, p = run_check(bad)
    if j is None:
        stats['crash'] += 1; shutil.copy(bad, os.path.join(OUT, 'crash-%s.vtt' % tag)); print('CRASH', tag, p.returncode, p.stderr[-300:]); return
    def keep(why, f):
        k = os.path.join(OUT, '%s-%s.vtt' % (why, tag))
        shutil.copy(bad, k)
        with open(k + '.orig', 'w', newline='') as g: g.write(nl.join(orig) + nl)
        print(why.upper(), k, f['code'], f['message'], '| help:', f.get('help'))
    spans = [f for f in j['findings'] if f.get('span')]
    sb_, so_ = secs_of(lines), secs_of(orig)
    for f in j['findings']:
        S = named(f)
        is_ls = f['code'] == 'W031' or (f['code'] == 'E011' and ('missing' in f['message'] or f.get('help')))
        if is_ls and S and texts(sb_.get(S, [])) == texts(so_.get(S, [])):
            stats['claim_false'] += 1; keep('fp', f); print('    section %s is undamaged' % S)
        if f['code'] == 'W031' and 'another section wrong too' in f['message']:
            others = [k for k in NAMES if k != S and texts(sb_.get(k, [])) != texts(so_.get(k, []))]
            if not others: stats['claim_false'] += 1; keep('claim', f); print('    reason 2 but every other section equals the original')
        if f['code'] == 'W031' and 'lines where the map has' in f['message']:
            stats['count_only'] += 1
            ok, why = count_only_true(f, lines, orig, None, nl)
            if not ok: stats['claim_false'] += 1; keep('claim', f); print('   ', why)
        if f['code'] == 'E011' and f.get('help') and 'the missing row is one of' in (f.get('help') or ''):
            stats['nowhere'] += 1
            ok, why = nowhere_true(f, lines, orig, None, nl)
            if not ok: stats['claim_false'] += 1; keep('claim', f); print('   ', why)
    if not spans:
        msgs = ' '.join(f['message'] + ' ' + (f.get('help') or '') for f in j['findings'])
        if 'cannot tell which' in msgs: stats['either'] += 1
        elif 'lines where the map has' in msgs or 'the missing row is one of' in msgs: pass
        elif 'section is short' in msgs or 'stray' in msgs: stats['short_only'] += 1
        else:
            stats['silent'] += 1
            if os.environ.get('SAY'): print('SILENT', tag, [f['code'] for f in j['findings']]); shutil.copy(bad, os.path.join(OUT, 'silent-%s.vtt' % tag))
        return
    stats['placed'] += 1
    f = spans[0]
    if not right_fix(f, lines, orig, None, nl):
        stats['wrong'] += 1
        keep('wrong', f)

KINDS = ['rooms', 'hall', 'wide', 'voidwalls', 'rooms', 'cave']
for rnd in range(ROUNDS):
    scen = SCEN if SCEN != 'all' else rng.choice(['dblsep', 'triple', 'onesep2', 'tiny', 'hidden', 'tinyform', 'sepwant1', 'junk', 'twoextra', 'twoextra', 'odd', 'odd', 'twolost', 'paste', 'paste', 'repeat'])
    kind = 'tiny' if scen.startswith('tiny') else rng.choice(KINDS)
    m = shape(kind)
    style = rng.choice(['2.0', '2.0', '1.x'])
    order = NAMES[:]
    if rng.random() < .3: rng.shuffle(order)
    crlf = rng.random() < .15
    tail = rng.random() < .3
    A, B, C = rng.sample(NAMES, 3)
    if scen == 'dblsep':
        orig, nl = write(m, style, order, {n: 2 for n in NAMES}, crlf, tail)
        L, _ = ed_del(orig, m, B)
        L, ch = (ed_dup if rng.random() < .5 else ed_empty)(L, m, A)
        trial('dblsep-%d-%s-%s' % (rnd, kind, A), L, orig, nl)
    elif scen == 'triple':
        orig, nl = write(m, style, order, {n: 1 for n in NAMES}, crlf, tail)
        y = rng.randrange(m.h)
        L, _ = ed_del(orig, m, B, y)
        L, _ = ed_del(L, m, C, y if rng.random() < .7 else None)
        L, ch = (ed_dup if rng.random() < .5 else ed_empty)(L, m, A)
        trial('triple-%d-%s-%s' % (rnd, kind, A), L, orig, nl)
    elif scen == 'onesep2':
        orig, nl = write(m, style, order, {B: 2}, crlf, tail)
        L, _ = ed_del(orig, m, B)
        L, ch = (ed_dup if rng.random() < .5 else ed_empty)(L, m, A)
        trial('onesep2-%d-%s-%s' % (rnd, kind, A), L, orig, nl)
    elif scen == 'hidden':
        blanks = {B: 1} if rng.random() < .5 else {n: 1 for n in NAMES}
        orig, nl = write(m, style, order, blanks, crlf, tail)
        L, _ = ed_del(orig, m, B)
        op = rng.choice([ed_dup, ed_empty, ed_del])
        L, ch = op(L, m, A)
        trial('hidden-%d-%s-%s-%s' % (rnd, kind, A, op.__name__), L, orig, nl)
    elif scen == 'sepwant1':
        # separators after two or three sections; A gains a line (its last row
        # blank or not); maybe B loses one, maybe C too
        blanks = {n: 1 for n in NAMES} if rng.random() < .6 else {A: 1, B: 1}
        if rng.random() < .3: blanks = {n: rng.choice([1, 2]) for n in blanks}
        orig, nl = write(m, style, order, blanks, crlf, tail)
        L, ch = (ed_dup if rng.random() < .5 else ed_empty)(orig, m, A)
        extra = ''
        if rng.random() < .5: L, _ = ed_del(L, m, B); extra += '-delB'
        if rng.random() < .2: L, _ = ed_del(L, m, C); extra += '-delC'
        trial('sepwant1-%d-%s-%s%s' % (rnd, kind, A, extra), L, orig, nl)
    elif scen == 'junk':
        # a hand-writer's row-like junk after a section ("----", "....", "===="),
        # alone or with a blank before it, and maybe an edit elsewhere
        junk = rng.choice(['----', '....', '====', '|', '-' * m.w, '.' * (m.w + 3), '~'])
        pre = rng.choice([[], [''], ['', '']])
        blanks = {n: rng.choice([0, 0, 1]) for n in NAMES}
        if rng.random() < .3: blanks = {n: 1 for n in NAMES}
        orig, nl = write(m, style, order, blanks, crlf, tail)
        L, nl2 = write(m, style, order, blanks, crlf, tail, junk_after={A: pre + [junk]})
        # the orig for comparison is the file without the junk; an edit elsewhere maybe
        extra = ''
        r = rng.random()
        if r < .3: L, _ = ed_del(L, m, B); extra = '-delB'
        elif r < .5: L, _ = (ed_dup if rng.random() < .5 else ed_empty)(L, m, B); extra = '-addB'
        trial('junk-%d-%s-%s-%s-pre%d%s' % (rnd, kind, A, junk[:2].replace('|', 'bar'), len(pre), extra), L, orig, nl)
    elif scen == 'twoextra':
        blanks = {n: rng.choice([0, 0, 1]) for n in NAMES}
        if rng.random() < .3: blanks = {n: 1 for n in NAMES}
        orig, nl = write(m, style, order, blanks, crlf, tail)
        opa, opb = rng.choice([ed_dup, ed_empty, ed_paste]), rng.choice([ed_dup, ed_empty, ed_paste])
        L, _ = opa(orig, m, A); L, _ = opb(L, m, B)
        extra = ''
        if rng.random() < .3: L, _ = ed_del(L, m, C); extra = '-delC'
        trial('twoextra-%d-%s-%s%s-%s%s' % (rnd, kind, A, opa.__name__, B, extra), L, orig, nl)
    elif scen == 'odd':
        blanks = {n: rng.choice([0, 0, 1]) for n in NAMES}
        orig, nl = write(m, style, order, blanks, crlf, tail)
        r = rng.choice([None, None, rng.randrange(section_rows(A, m))])
        orig = odd_row(orig, m, A, r, rng.choice(['x', 'x', 'T', '*', '#']))
        ops = [rng.choice([ed_dup, ed_empty, ed_paste, ed_del])]
        if rng.random() < .4: ops.append(rng.choice([ed_dup, ed_empty]))
        L = orig
        for op in ops: L, _ = op(L, m, A)
        extra = ''
        if rng.random() < .4: L, _ = rng.choice([ed_dup, ed_empty, ed_del])(L, m, B); extra = '-B'
        trial('odd-%d-%s-%s-%s%s' % (rnd, kind, A, '+'.join(o.__name__ for o in ops), extra), L, orig, nl)
    elif scen == 'twolost':
        blanks = {n: rng.choice([0, 0, 1]) for n in NAMES}
        orig, nl = write(m, style, order, blanks, crlf, tail)
        L, _ = rng.choice([ed_dup, ed_empty, ed_paste])(orig, m, A)
        L, _ = ed_del(L, m, B); L, _ = ed_del(L, m, B)
        trial('twolost-%d-%s-%s' % (rnd, kind, A), L, orig, nl)
    elif scen == 'paste':
        blanks = {n: rng.choice([0, 0, 1]) for n in NAMES}
        if rng.random() < .3: blanks = {n: 1 for n in NAMES}
        orig, nl = write(m, style, order, blanks, crlf, tail)
        L, _ = ed_paste(orig, m, A)
        extra = ''
        if rng.random() < .3: L, _ = ed_del(L, m, B); extra = '-delB'
        trial('paste-%d-%s-%s%s' % (rnd, kind, A, extra), L, orig, nl)
    elif scen == 'repeat':
        blanks = {n: rng.choice([0, 1]) for n in NAMES}
        orig, nl = write(m, style, order, blanks, crlf, tail)
        h0 = hdr(orig)[A]; nr = section_rows(A, m)
        block = orig[h0:h0 + 1 + nr]
        which = rng.choice(['first', 'second', 'both', 'none'])
        L = list(orig)
        if which in ('first', 'both'): L, _ = rng.choice([ed_dup, ed_empty])(L, m, A)
        second = list(block)
        if which in ('second', 'both'):
            at = 1 + rng.randrange(nr); second = second[:at + 1] + [second[at]] + second[at + 1:]
        if rng.random() < .5: second = second + ['']
        # the second block goes after the last section (before the records)
        last = max(hdr(L).values()); lastn = section_rows([k for k, v in hdr(L).items() if v == last][0], m)
        ins = last + 1 + lastn
        while ins < len(L) and (L[ins] == '' or not RECORD.match(L[ins])): ins += 1
        L = L[:ins] + second + L[ins:]
        trial('repeat-%d-%s-%s-%s' % (rnd, kind, A, which), L, orig, nl)
    elif scen == 'tiny':
        blanks = {n: rng.choice([0, 0, 1, 2]) for n in NAMES}
        orig, nl = write(m, style, order, blanks, crlf, tail)
        for op in (ed_dup, ed_empty, ed_del):
            for sec in NAMES:
                for r in range(section_rows(sec, m)):
                    L, ch = op(orig, m, sec, r)
                    trial('tiny-%d-%s-%s-%d' % (rnd, sec, op.__name__, r), L, orig, nl)
    elif scen == 'tinyform':
        blanks = {n: rng.choice([0, 1]) for n in NAMES}
        orig, nl = write(m, style, order, blanks, crlf, tail)
        for _ in range(6):
            op1, op2 = rng.choice([ed_dup, ed_empty, ed_del]), rng.choice([ed_dup, ed_empty, ed_del])
            L, _ = op1(orig, m, A)
            L, _ = op2(L, m, B)
            trial('tinyform-%d-%s-%s' % (rnd, op1.__name__, op2.__name__), L, orig, nl)
print(json.dumps(stats))
