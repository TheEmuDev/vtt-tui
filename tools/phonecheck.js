// The phone's half of tools/phonecheck.sh: drives Chrome on an Android phone
// through its DevTools socket (adb forward to localhost), opens the page a
// sandboxed vtt serves, and times what arrives, by the phone's own clock.
// A hook installed before the page's script records every WebSocket message
// (when, how big, its first record) and every visibility change; the page's
// code is not changed.
//
//   node tools/phonecheck.js fulls CDP URL N
//       load the page N times after a warm-up load; per load, the time from
//       the socket's open to the FULL's arrival, and the FULL's bytes. JSON
//       on stdout.
//   node tools/phonecheck.js lock CDP URL FIFO KEYS SECONDS
//       load the page, turn the screen off (adb), write KEYS into vtt's FIFO
//       (its terminal), a character ten times a second for SECONDS, wake the screen and
//       wait for the page to be visible (unlock it by hand if it has a PIN);
//       then what arrived after: messages, bytes, FULLs, the time from
//       visible to the last of them; and whether the screen the page ends on
//       is the one a fresh load draws. JSON on stdout.
//   node tools/phonecheck.js away CDP URL FIFO KEYS SECONDS
//       the same with Chrome sent behind the home screen and brought back
//       (no unlock: it can run with nobody at the phone).
//
// CDP is http://127.0.0.1:PORT (adb forward tcp:PORT localabstract:chrome_devtools_remote).
// The tab is opened for the run and closed after; the user's tabs are not touched.
'use strict';
const fs = require('fs');
const { execFileSync } = require('child_process');

const HOOK = `(() => {
  const W = window.WebSocket, pc = window.__pc = { msgs: [], vis: [], opens: [] };
  window.WebSocket = function (u, p) {
    const s = new W(u, p);
    (pc.made = pc.made || []).push(performance.now());
    s.addEventListener('open', () => pc.opens.push(performance.now()));
    s.addEventListener('close', e => (pc.closes = pc.closes || []).push([performance.now(), e.code]));
    s.addEventListener('message', e => {
      const b = e.data, n = b.byteLength || b.length || 0;
      pc.msgs.push([performance.now(), n, n ? new Uint8Array(b, 0, 1)[0] : -1]);
    });
    return s;
  };
  window.WebSocket.prototype = W.prototype;
  for (const k of ['CONNECTING', 'OPEN', 'CLOSING', 'CLOSED']) window.WebSocket[k] = W[k];
  document.addEventListener('visibilitychange', () => pc.vis.push([performance.now(), document.visibilityState]));
  // The screen wake lock: only in a secure context (https, or localhost).
  pc.lock = { secure: window.isSecureContext, api: !!navigator.wakeLock, got: [], released: [] };
  if (navigator.wakeLock) {
    const req = navigator.wakeLock.request.bind(navigator.wakeLock);
    navigator.wakeLock.request = t => req(t).then(s => {
      pc.lock.got.push(performance.now());
      s.addEventListener('release', () => pc.lock.released.push(performance.now()));
      return s;
    });
  }
})()`;

// The screen the page holds, colors resolved through its palette: equal on
// two loads when they draw the same thing. Script-level lets are reachable
// from the console's scope, which Runtime.evaluate shares.
const SCREEN = `(() => {
  if (!gl) return null;
  let h = 2166136261 >>> 0;
  const mix = v => { h = Math.imul(h ^ (v >>> 0), 16777619) >>> 0; };
  mix(cols); mix(rows);
  for (let i = 0; i < gl.length; i++) { mix(gl[i]); mix(pal[fg[i]]); mix(pal[bg[i]]); mix(at[i]); }
  return cols + 'x' + rows + ':' + h.toString(16);
})()`;

// The screen's text, a string a row: what differs, when two screens do.
const TEXT = `(() => gl ? Array.from({ length: rows }, (_, y) =>
  String.fromCharCode(...gl.subarray(y * cols, (y + 1) * cols)).replace(/\\0/g, ' ')) : [])()`;

const FULL = 0x46;     // 'F': a FULL's first record (src/wire.c)
const sleep = ms => new Promise(r => setTimeout(r, ms));
const adb = (...a) => execFileSync('adb', ['shell', ...a], { stdio: ['ignore', 'pipe', 'inherit'] });

// One connection to the browser; the tab is a target made for the run and
// attached with a session (Android's Chrome refuses /json/new).
class Tab {
  // The browser's socket alone: no tab made, none attached.
  static async browser(base) {
    const v = await (await fetch(base + '/json/version')).json();
    const tab = new Tab();
    await tab.connect(v.webSocketDebuggerUrl);
    return tab;
  }
  static async open(base) {
    const v = await (await fetch(base + '/json/version')).json();
    const tab = new Tab();
    await tab.connect(v.webSocketDebuggerUrl);
    tab.target = (await tab.send('Target.createTarget', { url: 'about:blank' })).targetId;
    tab.session = (await tab.send('Target.attachToTarget', { targetId: tab.target, flatten: true })).sessionId;
    return tab;
  }
  constructor() { this.seq = 0; this.wait = new Map(); this.events = []; this.session = null; }
  connect(url) {
    return new Promise((ok, bad) => {
      this.ws = new WebSocket(url);
      this.ws.onopen = ok;
      this.ws.onerror = () => bad(new Error('DevTools socket: ' + url));
      this.ws.onmessage = e => {
        const m = JSON.parse(e.data);
        if (m.id && this.wait.has(m.id)) { this.wait.get(m.id)(m); this.wait.delete(m.id); }
        else if (m.method && m.sessionId === this.session) this.events.push(m.method);
      };
    });
  }
  // A command to the tab once attached, to the browser before.
  send(method, params, ms) {
    const id = ++this.seq, msg = { id, method, params: params || {} };
    if (this.session && !method.startsWith('Target.')) msg.sessionId = this.session;
    this.ws.send(JSON.stringify(msg));
    return new Promise((ok, bad) => {
      const t = setTimeout(() => { this.wait.delete(id); bad(new Error(method + ': no answer')); }, ms || 15000);
      this.wait.set(id, m => { clearTimeout(t); m.error ? bad(new Error(method + ': ' + m.error.message)) : ok(m.result); });
    });
  }
  async eval(expr, ms) {
    const r = await this.send('Runtime.evaluate', { expression: expr, returnByValue: true }, ms);
    if (r.exceptionDetails) throw new Error('page: ' + (r.exceptionDetails.exception?.description || r.exceptionDetails.text));
    return r.result.value;
  }
  async load(url) {
    this.events = [];
    await this.send('Page.navigate', { url });
    for (let i = 0; i < 300 && !this.events.includes('Page.loadEventFired'); i++) await sleep(50);
  }
  async close() {
    if (this.target) await this.send('Target.closeTarget', { targetId: this.target }, 5000).catch(() => { });
    try { this.ws.close(); } catch (e) { }
  }
}

// Waits until the page holds a message whose first record is 'F'; its index.
async function untilFull(tab, from, ms) {
  for (let t = 0; t < ms; t += 50) {
    const i = await tab.eval(`window.__pc ? __pc.msgs.findIndex((m, i) => i >= ${from} && m[2] === ${FULL}) : -1`);
    if (i >= 0) return i;
    await sleep(50);
  }
  throw new Error('no FULL arrived in ' + ms + ' ms');
}

// The first load is a warm-up, reported apart: the first phone to join is
// sent the blank picture vtt holds before the players' frame is first drawn
// (src/net.c, net_players_renderer), and the screen after it.
async function fulls(tab, url, n) {
  const out = [];
  for (let k = 0; k <= n; k++) {
    await tab.load(url);
    const i = await untilFull(tab, 0, 15000);
    const pc = await tab.eval('__pc');
    out.push({ ms: +(pc.msgs[i][0] - pc.opens[0]).toFixed(1), bytes: pc.msgs[i][1] });
  }
  return { first: out[0], loads: out.slice(1) };
}

// Messages arriving after the page was visible again, until none for 2 s.
async function settle(tab, ms) {
  let last = -1, quiet = 0;
  for (let t = 0; t < ms && quiet < 2000; t += 100) {
    const n = await tab.eval('__pc.msgs.length');
    quiet = n === last ? quiet + 100 : 0;
    last = n;
    await sleep(100);
  }
}

// lock: the screen off and on, the lock screen unlocked by hand if it asks;
// away: Chrome sent behind the home screen and brought back, no unlock.
async function lock(tab, url, fifo, key, seconds, away) {
  await tab.load(url);
  await untilFull(tab, 0, 15000);
  await sleep(1000);
  adb('input', 'keyevent', away ? 'KEYCODE_HOME' : 'KEYCODE_SLEEP');
  await sleep(2000);
  const before = await tab.eval('__pc.msgs.length', 60000).catch(() => null);
  // KEY's characters in turn, one a tick: '+-' zooms in and out, every cell.
  for (let i = 0; i < seconds * 10; i++) { fs.appendFileSync(fifo, key[i % key.length]); await sleep(100); }
  const sent = seconds * 10;
  await sleep(1000);
  if (away) {
    adb('am', 'start', '-n', 'com.android.chrome/com.google.android.apps.chrome.Main');
  } else {
    adb('input', 'keyevent', 'KEYCODE_WAKEUP');
    process.stderr.write('phonecheck: the screen is on; unlock the phone if it asks (waiting up to 120 s)\n');
  }
  let pc = null;
  for (let t = 0; t < 120000; t += 250) {
    pc = await tab.eval('__pc', 5000).catch(() => null);
    if (pc && pc.vis.some(v => v[1] === 'visible')) break;
    await sleep(250);
  }
  if (!pc || !pc.vis.some(v => v[1] === 'visible')) throw new Error('the page never became visible again');
  // Chrome may have closed the page's socket while it was hidden (Android,
  // in the background): the page retries on a timer, which runs once it is
  // visible. Wait for that open before the quiet that ends the run.
  if ((pc.closes || []).length >= pc.opens.length)
    for (let t = 0; t < 30000; t += 100) {
      const o = await tab.eval('__pc.opens.length');
      if (o > pc.opens.length) break;
      await sleep(100);
    }
  await settle(tab, 60000);
  pc = await tab.eval('__pc');
  const seen = await tab.eval(SCREEN), seenText = await tab.eval(TEXT);
  const hidden = pc.vis.find(v => v[1] === 'hidden');
  const visible = pc.vis.filter(v => v[1] === 'visible').pop();
  const during = pc.msgs.filter(m => hidden && m[0] >= hidden[0] && m[0] < visible[0]);
  const after = pc.msgs.filter(m => m[0] >= visible[0]);
  const bytes = a => a.reduce((s, m) => s + m[1], 0);
  await tab.load(url);
  await untilFull(tab, 0, 15000);
  await sleep(500);
  const fresh = await tab.eval(SCREEN), freshText = await tab.eval(TEXT);
  const differ = [];
  for (let y = 0; y < Math.max(seenText.length, freshText.length) && differ.length < 4; y++)
    if (seenText[y] !== freshText[y]) differ.push({ row: y, held: seenText[y], fresh: freshText[y] });
  return {
    keys: sent, before_sleep: before,
    while_hidden: { msgs: during.length, bytes: bytes(during), fulls: during.filter(m => m[2] === FULL).length },
    after_visible: { msgs: after.length, bytes: bytes(after), fulls: after.filter(m => m[2] === FULL).length,
                     ms_to_last: after.length ? +(after[after.length - 1][0] - visible[0]).toFixed(1) : 0 },
    screen: seen, fresh, same: seen !== null && seen === fresh, differ,
    lock: { ...pc.lock, got: pc.lock.got.map(t => +t.toFixed(0)), released: pc.lock.released.map(t => +t.toFixed(0)) },
    made: (pc.made || []).map(t => +t.toFixed(0)), open_at: pc.opens.map(t => +t.toFixed(0)),
    opens: pc.opens.length, closes: (pc.closes || []).map(c => [+c[0].toFixed(0), c[1]]),
    reopened_ms: pc.opens.length > 1 ? +(pc.opens[pc.opens.length - 1] - visible[0]).toFixed(1) : null,
    hidden_at: hidden && +hidden[0].toFixed(0), visible_at: +visible[0].toFixed(0),
    timeline: process.env.DEBUG ? pc.msgs.map(m => [+m[0].toFixed(0), m[1], m[2]]) : undefined,
  };
}

// What keeps the phone's screen on: the window holding it (dumpsys window's
// mHoldScreenWindow). Not to be trusted alone: it reads Chrome once DevTools
// has attached to any tab, and read Chrome for the plain page never attached
// (2026-10-22); the screen going dark under a short timeout is the test
// (docs/V2.md, *Next*). And Chrome's memory: each of its processes'
// RSS in KB (this Android lists RSS, not PSS), the renderers among them.
function holder() {
  const w = execFileSync('adb', ['shell', 'dumpsys', 'window'], { encoding: 'utf8' });
  const m = w.match(/mHoldScreenWindow=(\S+(?: \S+)*)/);
  return m ? m[1].trim() : null;
}
function chromeKB() {
  const m = execFileSync('adb', ['shell', 'dumpsys', 'meminfo'], { encoding: 'utf8', maxBuffer: 1 << 24 });
  const at = m.search(/Total [PR]SS by process/);
  const kb = {};
  for (const l of m.slice(at, m.indexOf('\n\n', at)).split('\n')) {
    const p = l.match(/([\d,]+)K: (com\.android\.chrome\S*)/);
    if (p) kb[p[2].replace(/:org\.chromium.*/, '')] = +p[1].replace(/,/g, '');
  }
  return kb;
}
// Chrome's CPU time so far, in clock ticks (user + system, every process).
function chromeTicks() {
  const out = execFileSync('adb', ['shell',
    'for p in $(ps -A -o PID,NAME | awk \'/com\\.android\\.chrome/ { print $1 }\'); do cat /proc/$p/stat; done'], { encoding: 'utf8' });
  let t = 0;
  for (const l of out.split('\n')) {
    const f = l.slice(l.lastIndexOf(')') + 2).split(' ');
    if (f.length > 13) t += +f[11] + +f[12];
  }
  return t;
}

// wake: the page opened in a tab DevTools never attaches to until the end
// (once attached, Chrome holds the screen on until the tab closes); over
// HOLD_MS (20 s): Chrome's CPU ticks, then who holds the screen and Chrome's
// memory. Then attached: whether the page drew, and its video's state, if it
// has one (a build with HEALTH B16's stand-in).
async function wake(tab, url) {
  const t0 = chromeTicks();
  tab.target = (await tab.send('Target.createTarget', { url })).targetId;
  await sleep(+process.env.HOLD_MS || 20000);
  const r = { cpu_ticks: chromeTicks() - t0, holder: holder(), chrome_kb: chromeKB() };
  tab.session = (await tab.send('Target.attachToTarget', { targetId: tab.target, flatten: true })).sessionId;
  r.page = await tab.eval(`(() => { const v = document.querySelector('video');
    return { drew: typeof gl !== 'undefined' && gl !== null, secure: isSecureContext,
             video: v && { paused: v.paused, readyState: v.readyState, time: v.currentTime } } })()`);
  return r;
}

async function main(argv) {
  const [mode, port, url] = argv;
  const base = 'http://127.0.0.1:' + port;
  const tab = mode === 'wake' ? await Tab.browser(base) : await Tab.open(base);
  try {
    if (mode === 'wake') { console.log(JSON.stringify(await wake(tab, url))); return; }
    await tab.send('Page.enable');
    await tab.send('Page.addScriptToEvaluateOnNewDocument', { source: HOOK });
    let r;
    if (mode === 'fulls') r = await fulls(tab, url, +argv[3] || 10);
    else if (mode === 'lock' || mode === 'away') r = await lock(tab, url, argv[3], argv[4], +argv[5] || 15, mode === 'away');
    else throw new Error('mode: fulls, lock, away or wake');
    console.log(JSON.stringify(r));
  } finally {
    await tab.close();
  }
}

main(process.argv.slice(2)).catch(e => { console.error('phonecheck: ' + e.message); process.exit(1); });
