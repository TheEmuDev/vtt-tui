#!/usr/bin/env python3
"""Undamaged files: every way three row sections may end, mixed; two row forms
(1.x full rows, 2.0 empty rows as ""), CRLF, section order, a fog section, a
record between sections. Any line-search finding is a false positive.
No finding of the line search may come of any of them.

    VTT=./vtt WORKERS=4 python3 -I tools/shift/scan_endings.py OUT MAP...
    env ROWJUNK=1: row-like junk endings (----, ...., |) instead
"""
import json, os, sys, itertools, subprocess, multiprocessing

OUT = sys.argv[1]
MAPS = sys.argv[2:]
VTT = os.environ.get('VTT')
WORKERS = int(os.environ.get('WORKERS', '4'))
NAMES = ('tiles', 'vedges', 'hedges')

ENDS = {
    'none': '', 'b1': '\n', 'b2': '\n\n', 'b3': '\n\n\n', 'sp': '   \n', 'tab': '\t\n',
    'hash': '# note\n', 'words': 'comment here\n', 'unk': 'lights\n', 'reclike': 'tokenfoo 1 2\n',
    'cr': '\r\n', 'hashsp': '  # x\n', 'b1hash': '\n# note\n', 'hashb1': '# note\n\n',
    'sptab': ' \t \n', 'rec': 'note 1 1 "between"\n', 'b1sp': '\n   \n',
    'hashhall': '# hall\n', 'wallcmt': '# wall ----\n', 'odd': 'x...\n', 'star': '****\n',
}
if os.environ.get('ROWJUNK'):
    ENDS = {'none': '', 'b1': '\n', 'dash': '----\n', 'dots': '....\n', 'eq': '====\n', 'bar': '|\n',
            'b1dash': '\n----\n', 'dashb1': '----\n\n', 'tilde': '~~~\n', 'plus': '+\n', 'sdash': '  --\n'}

def parse(path):
    text = open(path, newline='').read()
    lines = text.split('\n')
    if lines and lines[-1] == '': lines.pop()
    w = h = None
    for l in lines:
        if l.startswith('size '):
            w, h = map(int, l.split()[1:3])
    hdr = {}
    for i, l in enumerate(lines):
        if l in NAMES: hdr[l] = i
    if len(hdr) < 3 or 'fog' in lines: return None
    secs = {}
    for name in NAMES:
        i = hdr[name]
        nr = h + 1 if name == 'hedges' else h
        rows = lines[i + 1:i + 1 + nr]
        assert len(rows) == nr, (path, name)
        secs[name] = rows
    # the lines that are not the three sections, in order, with markers
    rest_before = lines[:min(hdr.values())]
    order = sorted(NAMES, key=lambda n: hdr[n])
    last = hdr[order[-1]]
    nr_last = h + 1 if order[-1] == 'hedges' else h
    rest_after = lines[last + 1 + nr_last:]
    return w, h, secs, order, rest_before, rest_after

def form_rows(rows, style, w):
    if style == '2.0':
        return ['' if r.strip() == '' else r.rstrip(' ') if False else r for r in rows]
    return [r + ' ' * (w - len(r)) if len(r) < w else r for r in rows]

def build(parsed, style, order, ends, nl, fog):
    w, h, secs, _, before, after = parsed
    L = list(before)
    for name in order:
        rows = form_rows(secs[name], style, w if name != 'vedges' else w + 1)
        if style == '2.0':
            rows = ['' if r.strip() == '' else r for r in rows]
        L.append(name)
        L.extend(rows)
        L.append('\0' + ENDS[ends[name]])      # raw tail, spliced below
    if fog:
        L.append('fog on')
        L.append('fogpatch 1 Crypt reveal 2')
        L.append('fog')
        L.extend(['.' * w] * h)
    L.extend(after)
    out = []
    for l in L:
        if l.startswith('\0'):
            out.append(l[1:].replace('\n', nl))
        else:
            out.append(l + nl)
    return ''.join(out)

def classify(j):
    hits = []
    for f in j['findings']:
        if f['code'] == 'W031' or f.get('span') or (f['code'] == 'E011' and (f.get('help') or 'missing' in f['message'])):
            hits.append((f['code'], f['line'], f['message'][:160]))
    return hits

def run(args):
    tag, text, wid = args
    d = os.path.join(OUT, 'p%d' % os.getpid())
    os.makedirs(d + '/d', exist_ok=True); os.makedirs(d + '/r', exist_ok=True)
    env = dict(os.environ, XDG_DATA_HOME=d + '/d', XDG_RUNTIME_DIR=d + '/r')
    bad = os.path.join(d, 'bad.vtt')
    with open(bad, 'w', newline='') as f: f.write(text)
    p = subprocess.run([VTT, bad, '--check', '--json'], env=env, capture_output=True, text=True, timeout=120)
    if p.returncode > 2 or p.returncode < 0:
        return tag, 'CRASH', p.returncode, p.stderr[-200:], text
    try:
        j = json.loads(p.stdout)
    except Exception:
        return tag, 'NOJSON', p.returncode, p.stdout[-200:], text
    hits = classify(j)
    return tag, 'HIT' if hits else 'ok', hits, None, text if hits else None

def main():
    os.makedirs(OUT, exist_ok=True)
    jobs = []
    keys = list(ENDS)
    for path in MAPS:
        parsed = parse(path)
        name = os.path.basename(path)
        if parsed is None: print('skip', name); continue
        orders = [parsed[3], ['hedges', 'tiles', 'vedges'], ['vedges', 'hedges', 'tiles']]
        for style in ('1.x', '2.0'):
            for combo in itertools.product(keys, repeat=3):
                ends = dict(zip(NAMES, combo))
                tag = '%s|%s|%s' % (name, style, ','.join(combo))
                jobs.append((tag, build(parsed, style, orders[0], ends, '\n', False)))
            # CRLF, other orders and a fog section: a subset of ends
            sub = [k for k in ['none', 'b1', 'b2', 'sp', 'hash', 'tab', 'unk'] if k in ENDS] or list(ENDS)[:6]
            for combo in itertools.product(sub, repeat=3):
                ends = dict(zip(NAMES, combo))
                jobs.append(('%s|%s|crlf|%s' % (name, style, ','.join(combo)), build(parsed, style, orders[0], ends, '\r\n', False)))
                jobs.append(('%s|%s|fog|%s' % (name, style, ','.join(combo)), build(parsed, style, orders[0], ends, '\n', True)))
                for oi, order in enumerate(orders[1:], 1):
                    jobs.append(('%s|%s|ord%d|%s' % (name, style, oi, ','.join(combo)), build(parsed, style, order, ends, '\n', False)))
    print('jobs', len(jobs), flush=True)
    args = [(t, x, i % WORKERS) for i, (t, x) in enumerate(jobs)]
    n = 0
    with multiprocessing.Pool(WORKERS) as pool:
        for tag, kind, info, err, text in pool.imap_unordered(run, args, chunksize=16):
            n += 1
            if kind != 'ok':
                print(kind, tag, info, err or '', flush=True)
                if text is not None:
                    fn = os.path.join(OUT, 'hit-%d.vtt' % n)
                    with open(fn, 'w', newline='') as f: f.write(text)
                    print('   saved', fn, flush=True)
    print('done', n, flush=True)

if __name__ == '__main__':
    main()
