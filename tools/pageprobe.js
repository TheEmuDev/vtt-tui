// Times the phone page in a real browser, native drawing included: paste it
// into the DevTools console of a page `:serve` is serving (or run it through
// a browser-automation tool), drive the map from the GM's side, then call
// probe.report(). tools/pagebench.js times the page's own code more precisely
// in node; this is for what node cannot see -- putImageData, the status
// line's DOM work -- and for the whole of a frame on a real device.
//
//   probe.reset()     start counting again
//   probe.report()    per function: calls, total ms, average us; the pixels
//                     each push sent; rows per frame
//
// Two things to know. Without cross-origin isolation the browser's clock
// moves in 0.1 ms steps, so one call's time is noise: read totals over a few
// hundred frames, never a single call. And a window that is hidden (another
// workspace, a background tab) gets no animation frames, so nothing would be
// presented: `probe.sync = true` (the default here) presents each message at
// once instead, which times the same work without the wait.
(() => {
  const P = {}, put = { n: 0, px: 0, t: 0 };
  let t0 = performance.now(), msgs = 0, bytes = 0, rows = 0;
  const wrap = name => {
    const f = window[name];
    window[name] = function (...a) {
      const s = performance.now();
      const r = f.apply(this, a);
      const st = P[name] || (P[name] = { n: 0, t: 0 });
      st.n++; st.t += performance.now() - s;
      return r;
    };
  };
  ['feed', 'present', 'blitRow', 'tile', 'status'].forEach(wrap);
  const pi = ctx.putImageData.bind(ctx);
  ctx.putImageData = function (img, dx, dy, x, y, w, h) {
    const s = performance.now();
    pi(img, dx, dy, x, y, w, h);
    put.t += performance.now() - s; put.n++; put.px += w * h;
  };
  const br = window.blitRow;
  window.blitRow = function (...a) { rows++; return br.apply(this, a); };
  const sched = window.schedule;
  let want = 0;
  window.schedule = function () { if (window.probe.sync) want = 1; else sched(); };
  const fd = window.feed;
  window.feed = function (u8) {
    msgs++; bytes += u8.byteLength;
    const r = fd(u8);
    if (want) { want = 0; present(); }
    return r;
  };
  window.probe = {
    sync: true,
    reset() { for (const k in P) delete P[k]; put.n = put.px = put.t = 0; msgs = bytes = rows = 0; t0 = performance.now(); },
    report() {
      const frames = (P.present && P.present.n) || 0, o = { wall_ms: Math.round(performance.now() - t0), msgs, bytes, frames };
      for (const k in P) o[k] = { calls: P[k].n, total_ms: +P[k].t.toFixed(1), avg_us: +(P[k].t / P[k].n * 1000).toFixed(1) };
      o.putImageData = { calls: put.n, total_ms: +put.t.toFixed(1), px_per_call: Math.round(put.px / Math.max(1, put.n)) };
      o.rows_per_frame = frames ? +(rows / frames).toFixed(1) : 0;
      return o;
    },
  };
  return 'probe ready: drive the map, then probe.report()';
})();
