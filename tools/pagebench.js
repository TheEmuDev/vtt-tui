// Replays a players'-view stream through the phone page's own JavaScript and
// WebAssembly, in node, and times them. The canvas and the DOM are stubbed, so
// what this measures is the page's own work -- decoding (feed), copying tiles
// into the framebuffer (present, blitRow, tile) -- and how many pixels each
// frame would push to the canvas, not the browser's native drawing (for that,
// tools/pageprobe.js in a real browser). docs/PERFORMANCE.md, "The phone page".
//
//   node tools/pagebench.js PAGE STREAM [loops]
//
//   PAGE    the page as the binary serves it (tools/pagebench.sh writes it out
//           of src/webpage.c), or web/index.html
//   STREAM  a raw stream, as `vtt --bench-record` saves it: records back to
//           back, cut into messages at each 'E', the way the server frames them
//
// Environment: VIEW=WxH@DPR is the phone's viewport in CSS pixels and its
// pixel ratio (default 915x412@2.625, a common phone held sideways); WRAP is
// the page functions to time (default feed,present,blitRow; each wrap costs
// a clock read per call, so for an A/B of the whole compare `wall` with
// WRAP= set empty); JSON=1 prints JSON instead of a table row. Every run ends
// with a checksum of the framebuffer: a change meant to be faster, not
// different, must leave it the same.
'use strict';
const fs = require('fs');
const [pagePath, recPath, loopsArg] = process.argv.slice(2);
if (!pagePath || !recPath) { console.error('usage: node tools/pagebench.js PAGE STREAM [loops]'); process.exit(2); }
const html = fs.readFileSync(pagePath, 'utf8');
let script = html.slice(html.indexOf('<script>') + 8, html.lastIndexOf('</script>'));

// ---- the stream, cut into the server's messages: one per frame (ending 'E')
// and one per record outside a frame (a handout, a keep-alive).
const raw = fs.readFileSync(recPath);
const u16 = o => raw[o] | raw[o + 1] << 8;
const msgs = [];
let start = 0, inFrame = false;
for (let p = 0; p < raw.length; ) {
  const t = raw[p];
  let len;
  if (t === 0x46 || t === 0x50) len = 5;                         // 'F', 'P'
  else if (t === 0x45 || t === 0x5a) len = 1;                    // 'E', 'Z'
  else if (t === 0x52) len = 10 + 2 * u16(p + 5);                // 'R'
  else if (t === 0x48 || t === 0x57 || t === 0x4e) len = 3 + u16(p + 1);   // 'H', 'W', 'N'
  else { console.error('pagebench: unknown record 0x' + t.toString(16) + ' at byte ' + p); process.exit(2); }
  if (t === 0x46 || t === 0x52 || t === 0x50) inFrame = true;
  p += len;
  if (t === 0x45 || !inFrame) { msgs.push(new Uint8Array(raw.subarray(start, p))); start = p; inFrame = false; }
}
if (start < raw.length) msgs.push(new Uint8Array(raw.subarray(start)));

// ---- a browser, as little of one as the page touches
const [vw, vh, dpr] = (process.env.VIEW || '915x412@2.625').match(/^(\d+)x(\d+)@([\d.]+)$/).slice(1).map(Number);
const el = () => ({ hidden: true, textContent: '', innerHTML: '', style: {}, onclick: null, appendChild() {},
  addEventListener() {}, getBoundingClientRect: () => ({ left: 0, top: 0, width: 1, height: 1 }), value: '' });
const els = {};
const stat = { puts: 0, putPx: 0, badPuts: 0 };
// VERIFY=1 models the canvas -- each push copies its rectangle out of the
// framebuffer, as a browser's would -- so a wrong push shows, at the price of
// the copy's time; without it a push is only counted.
const verify = !!process.env.VERIFY;
let canvasPx = null, canvasW = 0;
let glyph = 0;
const ctx2d = { clearRect() {}, set fillStyle(v) {}, set font(v) {}, set textBaseline(v) {},
  fillText(t) { glyph = t.charCodeAt(0); },
  // A mask of the glyph last drawn, different for every glyph, so drawing the
  // wrong one shows in the checksum.
  getImageData(x, y, w, h) {
    const d = new Uint8ClampedArray(w * h * 4);
    for (let i = 3, k = 0; i < d.length; i += 4, k++) { const v = Math.imul(k + 1, 2654435761) ^ Math.imul(glyph + 7, 40503); d[i] = (v >>> 13) & 1 ? 255 : (v >>> 7) & 3 ? 0 : 128; }
    return { data: d };
  },
  putImageData(img, dx, dy, x, y, w, h) {
    stat.puts++; stat.putPx += w * h;
    if (x < 0 || y < 0 || w <= 0 || h <= 0 || x + w > img.width || y + h > img.height) stat.badPuts++;
    if (!verify) return;
    const src = new Uint32Array(img.data.buffer, img.data.byteOffset, img.width * img.height);
    if (!canvasPx || canvasPx.length !== src.length) { canvasPx = new Uint32Array(src.length); canvasW = img.width; }
    for (let r = Math.max(0, y); r < Math.min(img.height, y + h); r++)
      canvasPx.set(src.subarray(r * canvasW + Math.max(0, x), r * canvasW + Math.min(img.width, x + w)), r * canvasW + Math.max(0, x));
  } };
const canvas = () => ({ ...el(), width: 0, height: 0, getContext: () => ctx2d });
class ImageData { constructor(data, w, h) { this.data = data; this.width = w; this.height = h; } }
const env = {
  ImageData, setTimeout: () => 0, clearTimeout() {}, requestAnimationFrame: () => 0, cancelAnimationFrame() {},
  addEventListener() {}, innerWidth: vw, innerHeight: vh, devicePixelRatio: dpr,
  location: { search: '', protocol: 'http:', host: 'x' }, localStorage: { getItem: () => '', setItem() {} },
  navigator: {}, WebSocket: class { close() {} },
  // MODULE=plain forces the 8-byte copy loop (as a browser without SIMD),
  // MODULE=js no WebAssembly at all; the default is what the page picks.
  WebAssembly: process.env.MODULE === 'js' ? undefined
    : process.env.MODULE === 'plain' ? { Module: WebAssembly.Module, Memory: WebAssembly.Memory, Instance: WebAssembly.Instance, validate: () => false }
    : WebAssembly,
  document: { getElementById: id => (els[id] ||= id === 'c' ? canvas() : el()), createElement: t => t === 'canvas' ? canvas() : el() },
};
env.window = env;

// ---- the page's script inside a function, so its names are locals as fast
// as a browser's script scope (Math, performance and the rest are node's own), with timing wraps and a present after each
// message (the browser would present at its next animation frame).
const names = (process.env.WRAP ?? 'feed,present,blitRow').split(',').filter(Boolean);
const T = {};
const ns = () => Number(process.hrtime.bigint());
const tail = `
  let __want = 0, __frames = 0;
  schedule = function () { __want = 1; };
  ${names.map(n => `{ const f = ${n}; const s = T['${n}'] = { n: 0, t: 0 };
    ${n} = function (a, b, c, d, e) { const t0 = ns(); const r = f(a, b, c, d, e); s.n++; s.t += ns() - t0; return r; }; }`).join('\n')}
  return { step(m) { feed(m); if (__want) { __want = 0; present(); __frames++; } }, frames: () => __frames, fb: () => px,
    repaint() { dirtyAll = true; present(); } };`;
script = script.replace("'use strict';", '').replace(/\nconnect\(\);\n/, '\n');
// The stubs come in as parameters, not through `with`: a `with` makes every
// free name the page uses (Math, performance...) a dynamic lookup, which would
// time the harness rather than the page.
const keys = Object.keys(env);
const page = new Function(...keys, 'T', 'ns', script + tail)(...keys.map(k => env[k]), T, ns);

// ---- replay: the first loop warms the JIT and the tile arena, and is not counted
const loops = Math.max(2, +(loopsArg || 6));
let wall = 0, counted = 0;
for (let l = 0; l < loops; l++) {
  if (l === 1) { for (const k in T) { T[k].n = 0; T[k].t = 0; } stat.puts = stat.putPx = 0; wall = ns(); counted = page.frames(); }
  for (const m of msgs) page.step(m);
}
wall = ns() - wall;
const frames = page.frames() - counted;
const per = k => (T[k] && frames ? T[k].t / frames / 1e3 : 0);
// FNV-1a over the framebuffer's pixels: what the phone would show.
let sum = 0x811c9dc5;
const fb = page.fb();
for (let i = 0; i < fb.length; i++) { sum ^= fb[i]; sum = Math.imul(sum, 0x01000193) >>> 0; }
// VERIFY=1: the canvas shows what the framebuffer holds, and repainting
// everything changes nothing -- what was drawn a piece at a time is what a
// full redraw draws.
const fails = [];
if (stat.badPuts) fails.push(stat.badPuts + ' pushes outside the image');
if (verify) {
  const same = (a, b) => a.length === b.length && a.every((v, i) => v === b[i]);
  if (!canvasPx || !same(canvasPx, fb)) fails.push('the canvas differs from the framebuffer');
  const before = fb.slice();
  page.repaint();
  if (!same(before, page.fb())) fails.push('a full repaint changed pixels: drawn piecewise wrong');
}
const out = { checksum: sum.toString(16).padStart(8, '0'), verified: verify ? !fails.length : null, fails, msgs: msgs.length, frames, view: `${vw}x${vh}@${dpr}`, wall_us_per_frame: frames ? wall / frames / 1e3 : 0,
  puts_per_frame: frames ? stat.puts / frames : 0, px_per_frame: frames ? stat.putPx / frames : 0 };
for (const k of names) out[k + '_us'] = per(k);
if (process.env.JSON) { console.log(JSON.stringify(out)); process.exit(fails.length ? 1 : 0); }
const f1 = v => v.toFixed(1).padStart(7);
console.log(`frames ${String(frames).padStart(5)}  wall ${f1(out.wall_us_per_frame)}us  ` +
  names.map(k => `${k} ${f1(per(k))}us`).join('  ') +
  `  puts ${out.puts_per_frame.toFixed(1)}  px ${Math.round(out.px_per_frame)}  fb ${out.checksum}` +
  (verify ? (fails.length ? '  VERIFY FAILED: ' + fails.join('; ') : '  verified') : ''));
process.exit(fails.length ? 1 : 0);
