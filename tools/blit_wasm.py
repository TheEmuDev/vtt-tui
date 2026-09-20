#!/usr/bin/env python3
"""Assembles web/blit.wasm: one function that copies a row of tiles into a
framebuffer, eight bytes at a time. Hand-assembled so the page needs no
toolchain; the bytes are pasted into web/index.html as base64. (A variant
using memory.copy measured three times slower: each copy is a call into the
runtime, which for a 64-byte row costs more than the row.)

  w(dst, ptrs, n, rows, stride)
    for i in 0..n:  src, w = ptrs[i]      (8 bytes each: byte offset, row bytes)
                    for r in 0..rows: copy(dst + r*stride, src + r*w, w)
                    dst += w
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

dst, ptrs, n, rows, stride, i, r, src, w, d, p = range(11)
body = (
    BLOCK + LOOP +
      get(i) + get(n) + GEU + br_if(1) +
      get(ptrs) + get(i) + const(3) + SHL + ADD + tee(p) +
      load(0) + set_(src) +
      get(p) + load(4) + set_(w) +
      get(dst) + set_(d) +
      const(0) + set_(r) +
      BLOCK + LOOP +
        get(r) + get(rows) + GEU + br_if(1) +
        get(d) + get(src) + get(w) + MEMCOPY +
        get(d) + get(stride) + ADD + set_(d) +
        get(src) + get(w) + ADD + set_(src) +
        get(r) + const(1) + ADD + set_(r) +
        br(0) +
      END + END +
      get(dst) + get(w) + ADD + set_(dst) +
      get(i) + const(1) + ADD + set_(i) +
      br(0) +
    END + END + END
)
locals_ = vec([leb(6) + bytes([I32])])
func = locals_ + body

# The same, with each row copied as 8-byte words in a loop: a memory.copy
# is a call into the runtime, which for a 64-byte row costs more than the
# row. k is a byte offset within the row; w must be a multiple of 8.
k = 11
body2 = (
    BLOCK + LOOP +
      get(i) + get(n) + GEU + br_if(1) +
      get(ptrs) + get(i) + const(3) + SHL + ADD + tee(p) +
      load(0) + set_(src) +
      get(p) + load(4) + set_(w) +
      get(dst) + set_(d) +
      const(0) + set_(r) +
      BLOCK + LOOP +
        get(r) + get(rows) + GEU + br_if(1) +
        const(0) + set_(k) +
        BLOCK + LOOP +
          get(k) + get(w) + GEU + br_if(1) +
          get(d) + get(k) + ADD + get(src) + get(k) + ADD + load64(0) + store64(0) +
          get(k) + const(8) + ADD + set_(k) +
          br(0) +
        END + END +
        get(d) + get(stride) + ADD + set_(d) +
        get(src) + get(w) + ADD + set_(src) +
        get(r) + const(1) + ADD + set_(r) +
        br(0) +
      END + END +
      get(dst) + get(w) + ADD + set_(dst) +
      get(i) + const(1) + ADD + set_(i) +
      br(0) +
    END + END + END
)
func2 = vec([leb(7) + bytes([I32])]) + body2
code = section(10, vec([leb(len(func2)) + func2]))
types = section(1, vec([b"\x60" + vec([bytes([I32])] * 5) + vec([])]))
imports = section(2, vec([name("e") + name("m") + b"\x02" + b"\x00" + leb(1)]))
funcs = section(3, vec([leb(0)]))
exports = section(7, vec([name("w") + b"\x00" + leb(0)]))
module = b"\x00asm\x01\x00\x00\x00" + types + imports + funcs + exports + code

open("web/blit.wasm", "wb").write(module)
print(len(module), "bytes")
print(base64.b64encode(module).decode())
