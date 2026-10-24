// Runs the phone page's connection code -- from `function connect(){` to
// `connect();` in web/index.html -- against a stand-in for Chrome: a
// WebSocket whose close of a connecting socket fires error then close a turn
// later, timers on a clock of our own, a document that can be hidden and
// shown, and a screen wake lock that, as the spec says, is released whenever
// the page is hidden and refused while it is. After every step: at most one
// live socket, no retry pending while one lives, a lock held whenever the
// page is visible with its socket open, and never neither a socket nor a
// retry. Prints a line per failure and then
// "verified N checks", or exits 1 (HEALTH.md B15, B17).
//
//   node tests/page_conn.js PAGE
'use strict';
const fs = require('fs');
const vm = require('vm');

const html = fs.readFileSync(process.argv[2], 'utf8');
const at0 = html.indexOf('function connect(){');
const at1 = html.indexOf('\nconnect();\n', at0);
if (at0 < 0 || at1 < 0) { console.error('no connection block in the page'); process.exit(2); }
const src = html.slice(at0, at1 + '\nconnect();\n'.length);

let checks = 0, failed = 0;
function check(ok, what) { checks++; if (!ok) { failed++; console.log('FAIL ' + what); } }

function world() {
  let now = 0, nextId = 1;
  const timers = new Map(), sockets = [], locks = [], w = {};
  const doc = { hidden: false, on: null, addEventListener(t, f) { if (t === 'visibilitychange') this.on = f; } };
  class WebSocket {
    constructor(url) { this.url = url; this.readyState = 0; this.onopen = this.onmessage = this.onclose = this.onerror = null; sockets.push(this); }
    close() {
      if (this.readyState > 1) return;
      const was = this.readyState;
      this.readyState = 2;
      later(() => { this.readyState = 3; if (was === 0 && this.onerror) this.onerror({}); if (this.onclose) this.onclose({ code: 1006 }); });
    }
    // The server closing an opened socket with a code of its own (4001: the code refused).
    refuse(code) {
      this.readyState = 3;
      later(() => { if (this.onclose) this.onclose({ code }); });
    }
  }
  // A socket's own event, a turn later: not a timer of the page's.
  function later(f) { const id = nextId++; timers.set(id, { at: now, f, event: true }); }
  const env = {
    WebSocket, document: doc, location: { protocol: 'http:', host: 'h:7793', search: '?k=123456', replace(u) { w.went = u; } },
    URLSearchParams, encodeURIComponent, me: null, ws: null, retry: null, back: 2000, gone: false,
    status(m) { w.status = m; },
    feed() {},
    setTimeout(f, ms) { const id = nextId++; timers.set(id, { at: now + ms, f }); return id; },
    clearTimeout(id) { timers.delete(id); },
    navigator: { wakeLock: { request() {
      if (doc.hidden) return Promise.reject(new Error('hidden'));
      const s = { released: false }; locks.push(s); return Promise.resolve(s);
    } } },
  };
  vm.createContext(env);
  vm.runInContext(src, env);
  Object.assign(w, {
    env, sockets, doc,
    // Runs every timer due by now + ms, in order.
    tick(ms) {
      const end = now + (ms || 0);
      for (;;) {
        let best = null;
        for (const [id, t] of timers) if (t.at <= end && (!best || t.at < best[1].at)) best = [id, t];
        if (!best) break;
        timers.delete(best[0]); now = Math.max(now, best[1].at); best[1].f();
      }
      now = end;
    },
    // The page's own timers not yet run, one due now included.
    pending() { let n = 0; for (const t of timers.values()) if (!t.event) n++; return n; },
    live() { return sockets.filter(s => s.readyState < 2 && (s.onclose || s.onopen)); },
    held() { return locks.filter(s => !s.released).length; },
    open(s) { s.readyState = 1; s.onopen && s.onopen(); },
    kill(s) { s.readyState = 3; s.onclose && s.onclose({ code: 1006 }); },
    hide() { doc.hidden = true; for (const l of locks) l.released = true; doc.on && doc.on(); },
    show() { doc.hidden = false; doc.on && doc.on(); },
  });
  return w;
}

// A step is checked after the socket's events have run: a scenario that
// closes a socket runs `tick(0)` before its step, or a socket closing with
// its handlers still set reads as neither live nor retrying.
const settle = () => new Promise(r => setImmediate(r));
async function step(w, name) {
  await settle();
  const live = w.live();
  check(live.length <= 1, name + ': one live socket at most, ' + live.length);
  check(live.length === 0 || w.pending() === 0, name + ': no retry pending while a socket lives');
  // Never given up: a socket lives or a retry is coming -- unless the page has
  // gone to the join form (its code refused), with neither left behind.
  if (w.went) check(live.length + w.pending() === 0, name + ': gone to the form, nothing left trying');
  else check(live.length + w.pending() === 1, name + ': a socket or a retry, ' + live.length + '+' + w.pending());
  const ws = w.env.ws;
  if (!w.doc.hidden && ws && ws.readyState === 1) check(w.held() >= 1, name + ': visible and open, a lock held');
}

async function main() {
  { // A: the first open
    const w = world();
    check(w.sockets.length === 1, 'A: one socket made at once');
    w.open(w.sockets[0]); await step(w, 'A open');
    check(w.status === 'connected', 'A: status connected');
  }
  { // B: hidden and shown with the socket open: no new socket, the lock again
    const w = world();
    w.open(w.sockets[0]); await step(w, 'B open');
    w.hide(); await step(w, 'B hidden');
    check(w.held() === 0, 'B: hiding released the lock');
    w.show(); await step(w, 'B shown');
    check(w.sockets.length === 1, 'B: the open socket kept');
    check(w.held() === 1, 'B: the lock taken again on show');
  }
  { // C: B15 itself: closed while hidden, the retry's socket stuck connecting, then shown
    const w = world();
    w.open(w.sockets[0]); w.hide(); await step(w, 'C hidden');
    w.kill(w.sockets[0]); await step(w, 'C killed');
    check(w.pending() === 1, 'C: a retry pending');
    w.tick(2000); await step(w, 'C retry made');
    check(w.sockets.length === 2 && w.sockets[1].readyState === 0, 'C: the retry socket is connecting, hidden');
    w.show(); await step(w, 'C shown');
    check(w.sockets.length === 3, 'C: shown, a fresh socket at once');
    w.tick(0); await step(w, 'C stale close');
    check(w.pending() === 0, 'C: the abandoned socket scheduled no retry');
    w.open(w.env.ws); await step(w, 'C open');
    check(w.held() === 1 && w.status === 'connected', 'C: connected, the lock held');
  }
  { // D: closed while hidden, shown before the retry fires
    const w = world();
    w.open(w.sockets[0]); w.hide(); w.kill(w.sockets[0]); await step(w, 'D killed');
    w.show(); await step(w, 'D shown');
    check(w.sockets.length === 2 && w.pending() === 0, 'D: reconnected now, the retry cleared');
    w.tick(10000); await step(w, 'D later');
    check(w.sockets.length === 2, 'D: no second socket from the old retry');
  }
  { // E: the server down, the page flapping between hidden and shown
    const w = world();
    for (let i = 0; i < 6; i++) {
      const s = w.env.ws;
      if (s.readyState === 0) { s.close(); w.tick(0); }      // refused
      await step(w, 'E down ' + i);
      check(w.pending() <= 1, 'E: one retry at most, ' + i);
      if (i % 2) w.show(); else w.hide();
      await step(w, 'E flap ' + i);
      w.tick(2000); await step(w, 'E retry ' + i);
    }
    w.show(); await step(w, 'E shown');
    w.open(w.env.ws); await step(w, 'E up');
    check(w.live().length === 1 && w.status === 'connected', 'E: one socket once the server is back');
  }
  { // F: vtt down for two minutes, every attempt refused at once: the retry
    // backs off, 2 s doubling to 30 s (docs/REMOTE.md, *The page revisited*,
    // item 2): attempts at 0, 2, 6, 14, 30, 60, 90 and 120 s -- 8, not 61
    const w = world();
    let at20 = 0;
    for (let t = 0; t <= 120000; t += 100) {
      if (w.env.ws.readyState === 0) { w.env.ws.close(); w.tick(0); }
      if (t === 20000) at20 = w.sockets.length;
      w.tick(100);
    }
    await step(w, 'F down 120 s');
    check(at20 === 4, 'F: 4 attempts in the first 20 s (0, 2, 6, 14 s), ' + at20);
    check(w.sockets.length === 8, 'F: 8 attempts in two minutes, the cap 30 s, ' + w.sockets.length);
  }
  { // G: back up, then down again: the wait starts over at 2 s
    const w = world();
    for (let t = 0; t < 30000; t += 100) {               // down long enough to back off
      if (w.env.ws.readyState === 0) { w.env.ws.close(); w.tick(0); }
      w.tick(100);
    }
    w.tick(30000);                                         // the next attempt made, and it opens
    w.open(w.env.ws); await step(w, 'G up');
    const n = w.sockets.length;
    w.env.ws.close(); w.tick(0);                           // vtt goes away again
    w.tick(1900); const early = w.sockets.length;
    w.tick(200); await step(w, 'G retry');
    check(early === n && w.sockets.length === n + 1, 'G: after a connection the first retry is at 2 s again, ' + (early - n) + ' then ' + (w.sockets.length - n));
  }
  { // H: vtt refuses the code (started again since the page was opened, a new code):
    // the page goes to the join form and stops trying, so a stale page spends
    // one wrong code, not one each retry (docs/REMOTE.md, *As built*, item 3)
    const w = world();
    w.open(w.sockets[0]); await step(w, 'H up');
    w.sockets[0].refuse(4001); w.tick(0); await step(w, 'H refused');
    check(w.went === '/' && w.pending() === 0, 'H: to the form, no retry, ' + w.went + ' ' + w.pending());
    w.hide(); w.show(); await step(w, 'H shown on the way');   // before the form's page has come
    check(w.sockets.length === 1, 'H: no socket with the old code when shown meanwhile, ' + w.sockets.length);
    w.tick(120000); await step(w, 'H later');
    check(w.sockets.length === 1, 'H: no socket after, ' + w.sockets.length);
  }
  { // I: any other close (4002: held back past the wrong-code limit) is retried as before
    const w = world();
    w.open(w.sockets[0]); w.sockets[0].refuse(4002); w.tick(0); await step(w, 'I closed');
    check(!w.went && w.pending() === 1, 'I: a retry pending, no form');
  }
  console.log(failed ? failed + ' of ' + checks + ' checks failed' : 'verified ' + checks + ' checks');
  process.exit(failed ? 1 : 0);
}
main();
