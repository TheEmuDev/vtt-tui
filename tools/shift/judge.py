#!/usr/bin/env python3
"""Judge flagged files by their sections, not their text: for each finding with
a span in FILE (beside FILE.orig, as the generators keep them), is there a line
in the span whose fix gives the named section the original's rows? RIGHT or
WRONG a finding. A flag from a two-edit generator is often RIGHT: its other
edit changed no square.

    VTT=./vtt python3 -I tools/shift/judge.py FILE...
"""
import sys, json, subprocess, os, tempfile
VTT=os.environ.get('VTT', './vtt')
_SANDBOX=tempfile.mkdtemp(prefix='vtt-judge-')
ENV=dict(os.environ, XDG_DATA_HOME=_SANDBOX+'/xdg', XDG_RUNTIME_DIR=_SANDBOX+'/run')
NAMES=('tiles','vedges','hedges')
def secs(lines):
    out={}; cur=None
    for l in lines:
        if l in NAMES+('fog',): cur=l; out[cur]=[]; continue
        if cur is not None: out[cur].append(l)
    return out
def sec_of_line(lines, ln):
    cur=None
    for i,l in enumerate(lines[:ln]):
        if l in NAMES+('fog',): cur=l
    return cur
def norm(rows, nr):
    r=list(rows)[:nr]; return r+['']*(nr-len(r))
for path in sys.argv[1:]:
    bad=open(path, newline='').read().replace('\r','').split('\n'); orig=open(path+'.orig', newline='').read().replace('\r','').split('\n')
    if bad and bad[-1]=='': bad=bad[:-1]
    if orig and orig[-1]=='': orig=orig[:-1]
    w,h=[int(x) for x in [l for l in orig if l.startswith('size ')][0].split()[1:3]]
    nr={'tiles':h,'vedges':h,'hedges':h+1}
    so=secs(orig)
    p=subprocess.run([VTT,path,'--check','--json'],env=ENV,capture_output=True,text=True)
    j=json.loads(p.stdout)
    for f in j['findings']:
        if not f.get('span'): continue
        a,b=f['span']['first'],f['span']['last']; extra='too many' in f['message'] or f['code']=='W031'
        good=[]
        for ln in range(a,b+1):
            if extra:
                fixed=bad[:ln-1]+bad[ln:]; sec=sec_of_line(bad, ln)
                if sec and norm(secs(fixed)[sec],nr[sec])==norm(so[sec],nr[sec]): good.append(ln)
            else:
                sec=sec_of_line(bad, ln+1) if ln < len(bad) else sec_of_line(bad, ln)
                # the header line counts as "after line header"
                if bad[ln-1] in NAMES: sec=bad[ln-1]
                if not sec: continue
                for row in set(so[sec]):
                    fixed=bad[:ln]+[row]+bad[ln:]
                    if norm(secs(fixed)[sec],nr[sec])==norm(so[sec],nr[sec]): good.append(ln); break
        print('RIGHT' if good else 'WRONG', path, f['code'], 'span', a, b, 'good lines', good, '|', f['message'][:90])
