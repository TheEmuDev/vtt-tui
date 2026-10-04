#!/usr/bin/env python3
"""Assembles the page's copy loop: one function that copies a row of tiles into
the framebuffer. Hand-assembled so the page needs no toolchain; it writes the
modules to web/ and prints their base64, which is pasted into web/index.html. Two modules ship (docs/PAGESPEED.md):

  WASM_SIMD  16 bytes at a time with SIMD, a pixel row at a time across the
             tiles, so the writes run straight along the framebuffer
  WASM       8 bytes at a time, tile by tile: for a browser without SIMD
             (Safari before 16.4)

  w(dst, ptrs, n, rows, stride)
    tiles i in 0..n:  src, w = ptrs[i]      (8 bytes each: byte offset, row bytes)
    copies rows lines of w bytes from src (packed) to dst (stride apart), the
    tiles side by side. w is a multiple of 8 (a cell is an even number of
    pixels wide).

Measured 2026-10-04 with tools/pagebench.js (copy time a frame, a phone
915x412 at 2.625; same framebuffer checksum for all):

  variant                          cursor walk   carry    pan 200x200
  8 bytes, tile by tile (WASM)        19.6us      76.9us    801.9us
  memory.copy, tile by tile           29.1       112.7     1165.1
  16 bytes SIMD, tile by tile         20.6        70.1      711.0
  8 bytes, row by row                 21.3        67.9      669.7
  16 bytes SIMD, row by row (SIMD)    18.4        56.1      585.1

memory.copy is a call into the runtime per tile row, which for a 48-byte row
costs more than the row. Run with `--variant NAME` to print any of them.

If the page's size ever nears the one-round-trip limit (docs/REMOTE.md), this
is the first place to give bytes back: shipping one module saves about 300.
"""
import base64, sys

def leb(n):
    out = bytearray()
    while True:
        b = n & 0x7f; n >>= 7
        if n == 0 and not (b & 0x40): out.append(b); return bytes(out)
        out.append(b | 0x80)

def sleb(n):
    out = bytearray()
    while True:
        b = n & 0x7f; n >>= 7
        done = (n == 0 and not (b & 0x40)) or (n == -1 and (b & 0x40))
        out.append(b | (0 if done else 0x80))
        if done: return bytes(out)

def section(id, payload): return bytes([id]) + leb(len(payload)) + payload
def vec(items): return leb(len(items)) + b"".join(items)
def name(s): return leb(len(s)) + s.encode()

I32 = 0x7f
GET, SET, TEE = 0x20, 0x21, 0x22
def get(i): return bytes([GET]) + leb(i)
def set_(i): return bytes([SET]) + leb(i)
def tee(i): return bytes([TEE]) + leb(i)
def const(n): return b"\x41" + sleb(n)
def load(off): return b"\x28" + leb(2) + leb(off)
ADD, SHL, GEU = b"\x6a", b"\x74", b"\x4f"
BLOCK, LOOP, END = b"\x02\x40", b"\x03\x40", b"\x0b"
def br(d): return b"\x0c" + leb(d)
def br_if(d): return b"\x0d" + leb(d)
MEMCOPY = b"\xfc\x0a\x00\x00"
def load64(off): return b"\x29" + leb(3) + leb(off)
def store64(off): return b"\x37" + leb(3) + leb(off)
LTU = b"\x49"

MUL = b"\x6c"
GTU = b"\x4b"
def load128(off): return b"\xfd\x00" + leb(4) + leb(off)
def store128(off): return b"\xfd\x0b" + leb(4) + leb(off)

dst, ptrs, n, rows, stride, i, r, src, w, d, p, k = range(12)

# Copying w bytes from src to d, three ways.
COPY8 = (const(0) + set_(k) + BLOCK + LOOP + get(k) + get(w) + GEU + br_if(1) +
    get(d) + get(k) + ADD + get(src) + get(k) + ADD + load64(0) + store64(0) +
    get(k) + const(8) + ADD + set_(k) + br(0) + END + END)
COPY16 = (const(0) + set_(k) +                        # 16 while 16 fit, then 8
    BLOCK + LOOP + get(k) + const(16) + ADD + get(w) + GTU + br_if(1) +
      get(d) + get(k) + ADD + get(src) + get(k) + ADD + load128(0) + store128(0) +
      get(k) + const(16) + ADD + set_(k) + br(0) + END + END +
    BLOCK + LOOP + get(k) + get(w) + GEU + br_if(1) +
      get(d) + get(k) + ADD + get(src) + get(k) + ADD + load64(0) + store64(0) +
      get(k) + const(8) + ADD + set_(k) + br(0) + END + END)
COPYCALL = get(d) + get(src) + get(w) + MEMCOPY

def tile_by_tile(copy):
    """For each tile, its rows down the framebuffer."""
    body = (BLOCK + LOOP +
      get(i) + get(n) + GEU + br_if(1) +
      get(ptrs) + get(i) + const(3) + SHL + ADD + tee(p) + load(0) + set_(src) +
      get(p) + load(4) + set_(w) + get(dst) + set_(d) + const(0) + set_(r) +
      BLOCK + LOOP + get(r) + get(rows) + GEU + br_if(1) + copy +
        get(d) + get(stride) + ADD + set_(d) + get(src) + get(w) + ADD + set_(src) +
        get(r) + const(1) + ADD + set_(r) + br(0) + END + END +
      get(dst) + get(w) + ADD + set_(dst) + get(i) + const(1) + ADD + set_(i) + br(0) +
      END + END + END)
    return vec([leb(7) + bytes([I32])]) + body

def row_by_row(copy):
    """For each pixel row, every tile's line of it, left to right."""
    body = (const(0) + set_(r) + BLOCK + LOOP + get(r) + get(rows) + GEU + br_if(1) +
      get(dst) + get(r) + get(stride) + MUL + ADD + set_(d) + const(0) + set_(i) +
      BLOCK + LOOP + get(i) + get(n) + GEU + br_if(1) +
        get(ptrs) + get(i) + const(3) + SHL + ADD + tee(p) + load(4) + set_(w) +
        get(p) + load(0) + get(r) + get(w) + MUL + ADD + set_(src) + copy +
        get(d) + get(w) + ADD + set_(d) + get(i) + const(1) + ADD + set_(i) + br(0) + END + END +
      get(r) + const(1) + ADD + set_(r) + br(0) + END + END + END)
    return vec([leb(7) + bytes([I32])]) + body

def module(func):
    code = section(10, vec([leb(len(func)) + func]))
    types = section(1, vec([b"\x60" + vec([bytes([I32])] * 5) + vec([])]))
    imports = section(2, vec([name("e") + name("m") + b"\x02" + b"\x00" + leb(1)]))
    funcs = section(3, vec([leb(0)]))
    exports = section(7, vec([name("w") + b"\x00" + leb(0)]))
    return b"\x00asm\x01\x00\x00\x00" + types + imports + funcs + exports + code

VARIANTS = {
    "simd":       row_by_row(COPY16),      # shipped: WASM_SIMD
    "plain":      tile_by_tile(COPY8),     # shipped: WASM
    "memcopy":    tile_by_tile(COPYCALL),
    "simd-tile":  tile_by_tile(COPY16),
    "plain-row":  row_by_row(COPY8),
}

if len(sys.argv) == 3 and sys.argv[1] == "--variant":
    print(base64.b64encode(module(VARIANTS[sys.argv[2]])).decode())
    sys.exit(0)
for label, key, path in (("WASM_SIMD", "simd", "web/blit-simd.wasm"), ("WASM", "plain", "web/blit.wasm")):
    m = module(VARIANTS[key])
    open(path, "wb").write(m)
    print("%s (%d bytes, %s): %s" % (label, len(m), path, base64.b64encode(m).decode()))
