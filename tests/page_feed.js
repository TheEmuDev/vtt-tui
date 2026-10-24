// Runs the phone page's own wire decoder, feed() from web/index.html, over a
// stream the C encoder wrote, and prints what it decoded, so test_page_feed
// can compare it with the C decoder: the page is a third decoder and would
// otherwise only ever be checked by eye.
//
//   node tests/page_feed.js PAGE STREAM      (PAGE: the page as served)
//   node tests/page_feed.js PAGE STREAM clears  -> only "clears:N", how many
//                                    times feed() threw the tile cache away, on a
//                                    page that has drawn with every color;
//                                    clears-fresh: with none yet
//
// Prints "WxH", then a line per row: each cell as glyph.fg.bg.attr (colors
// as RRGGBB through the palette), then "handout:", "names:" and "whisper:"
// and the last of each.
'use strict';
const fs = require('fs');
const [page, stream, mode] = process.argv.slice(2);
const html = fs.readFileSync(page, 'utf8');
const at0 = html.indexOf('function feed(u8){');
const at1 = html.indexOf('\n}\n', at0);
if (at0 < 0 || at1 < 0) { console.error('no feed() in the page'); process.exit(2); }
// feed() and the field reader it calls, both the page's own.
const u0 = html.indexOf('function u16(');
if (u0 < 0) { console.error('no u16() in the page'); process.exit(2); }
const src = html.slice(u0, html.indexOf('\n', u0) + 1) + html.slice(at0, at1 + 2);

let lastHandout = '(none)', lastNames = '', lastWhisper = '', clears = 0;
const env = {
  cols: 0, rows: 0, gl: null, fg: null, bg: null, at: null,
  pal: new Uint32Array(256), used: new Uint8Array(256).fill(mode === 'clears' ? 1 : 0), rd: null, rx0: null, rx1: null, dirtyAll: false,
  dec: new TextDecoder(),
  layout() {}, touch() {}, schedule() {}, clearTiles() { clears++; },
  handout(s) { lastHandout = s; },
  names(s) { lastNames = s; },
  whisper(s) { lastWhisper = s; },
};
// The page's globals are the environment's fields; feed() assigns to them.
const run = new Function('env', 'u8',
  'with (env) { ' + src + '\n feed(u8); }');
// A with-block cannot run in strict mode; the page's code is strict itself,
// but nothing in feed() depends on it.
const bytes = new Uint8Array(fs.readFileSync(stream));
run(env, bytes);
if (mode === 'clears' || mode === 'clears-fresh') { process.stdout.write('clears:' + clears + '\n'); process.exit(0); }

const hex = v => v.toString(16).padStart(6, '0');
const out = [env.cols + 'x' + env.rows];
for (let y = 0; y < env.rows; y++) {
  const row = [];
  for (let x = 0; x < env.cols; x++) {
    const i = y * env.cols + x;
    row.push(env.gl[i] + '.' + hex(env.pal[env.fg[i]]) + '.' + hex(env.pal[env.bg[i]]) + '.' + env.at[i]);
  }
  out.push(row.join(' '));
}
out.push('handout:' + lastHandout);
out.push('names:' + lastNames);
out.push('whisper:' + lastWhisper);
process.stdout.write(out.join('\n') + '\n');
