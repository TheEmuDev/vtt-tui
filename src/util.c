#include "util.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------------------------------------------------------------- errors */

void die(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fputs("vtt: ", stderr);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    exit(1);
}

void *xmalloc(size_t n)
{
    void *p = malloc(n ? n : 1);
    if (!p) die("out of memory (%zu bytes)", n);
    return p;
}

void *xcalloc(size_t n, size_t sz)
{
    void *p = calloc(n ? n : 1, sz ? sz : 1);
    if (!p) die("out of memory (%zu x %zu bytes)", n, sz);
    return p;
}

void *xrealloc(void *p, size_t n)
{
    void *q = realloc(p, n ? n : 1);
    if (!q) die("out of memory (%zu bytes)", n);
    return q;
}

/* ----------------------------------------------------------------- arena */

void arena_init(Arena *a, size_t cap)
{
    a->base = xmalloc(cap);
    a->cap  = cap;
    a->used = 0;
    a->peak = 0;
}

void *arena_alloc(Arena *a, size_t n, size_t align)
{
    size_t off = (a->used + align - 1) & ~(align - 1);
    if (off + n > a->cap)
        die("arena exhausted: need %zu, have %zu", off + n, a->cap);
    a->used = off + n;
    if (a->used > a->peak) a->peak = a->used;
    return a->base + off;
}

void arena_reset(Arena *a) { a->used = 0; }

void arena_free(Arena *a)
{
    free(a->base);
    a->base = NULL;
    a->cap = a->used = a->peak = 0;
}

/* -------------------------------------------------------------- byte buf */

void bb_init(ByteBuf *b, size_t cap)
{
    b->data = xmalloc(cap);
    b->cap  = cap;
    b->len  = 0;
}

void bb_free(ByteBuf *b)
{
    free(b->data);
    b->data = NULL;
    b->cap = b->len = 0;
}

void bb_reset(ByteBuf *b) { b->len = 0; }

void bb_reserve(ByteBuf *b, size_t extra)
{
    if (b->len + extra <= b->cap) return;
    size_t cap = b->cap ? b->cap : 256;
    while (cap < b->len + extra) cap *= 2;
    b->data = xrealloc(b->data, cap);
    b->cap  = cap;
}

void bb_put(ByteBuf *b, const void *p, size_t n)
{
    bb_reserve(b, n);
    memcpy(b->data + b->len, p, n);
    b->len += n;
}

void bb_puts(ByteBuf *b, const char *s) { bb_put(b, s, strlen(s)); }

void bb_putc(ByteBuf *b, char c)
{
    bb_reserve(b, 1);
    b->data[b->len++] = c;
}

void bb_putu(ByteBuf *b, uint32_t v)
{
    char tmp[10];
    int  i = 0;
    if (v == 0) { bb_putc(b, '0'); return; }
    while (v) { tmp[i++] = (char)('0' + (v % 10)); v /= 10; }
    bb_reserve(b, (size_t)i);
    while (i--) b->data[b->len++] = tmp[i];
}

/* ------------------------------------------------------------------ utf8 */

int utf8_encode(uint32_t cp, char out[4])
{
    if (cp > 0x10FFFFu || (cp >= 0xD800u && cp <= 0xDFFFu)) return 0;
    if (cp < 0x80u) {
        out[0] = (char)cp;
        return 1;
    }
    if (cp < 0x800u) {
        out[0] = (char)(0xC0u | (cp >> 6));
        out[1] = (char)(0x80u | (cp & 0x3Fu));
        return 2;
    }
    if (cp < 0x10000u) {
        out[0] = (char)(0xE0u | (cp >> 12));
        out[1] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
        out[2] = (char)(0x80u | (cp & 0x3Fu));
        return 3;
    }
    out[0] = (char)(0xF0u | (cp >> 18));
    out[1] = (char)(0x80u | ((cp >> 12) & 0x3Fu));
    out[2] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
    out[3] = (char)(0x80u | (cp & 0x3Fu));
    return 4;
}

int utf8_decode(const char *s, size_t len, uint32_t *cp)
{
    if (len == 0) { *cp = 0; return 0; }

    unsigned char c = (unsigned char)s[0];
    int      need;
    uint32_t v;
    uint32_t lo;   /* smallest value legally encodable in this length */

    if (c < 0x80u)        { *cp = c; return 1; }
    else if ((c & 0xE0u) == 0xC0u) { need = 1; v = c & 0x1Fu; lo = 0x80u; }
    else if ((c & 0xF0u) == 0xE0u) { need = 2; v = c & 0x0Fu; lo = 0x800u; }
    else if ((c & 0xF8u) == 0xF0u) { need = 3; v = c & 0x07u; lo = 0x10000u; }
    else                  { *cp = 0xFFFDu; return 1; }   /* stray continuation */

    if (len < (size_t)need + 1) { *cp = 0xFFFDu; return 1; }

    for (int i = 1; i <= need; i++) {
        unsigned char cc = (unsigned char)s[i];
        if ((cc & 0xC0u) != 0x80u) { *cp = 0xFFFDu; return 1; }
        v = (v << 6) | (cc & 0x3Fu);
    }
    /* Reject overlongs and surrogates so they cannot round-trip through us. */
    if (v < lo || (v >= 0xD800u && v <= 0xDFFFu) || v > 0x10FFFFu) {
        *cp = 0xFFFDu;
        return 1;
    }
    *cp = v;
    return need + 1;
}

/* Compact wcwidth. Only the ranges a map/UI can realistically contain are
 * modelled: combining marks are zero-width, CJK and emoji are double-width,
 * everything else (Latin, box drawing, block elements, geometric shapes) is
 * single-width. */
struct Range { uint32_t lo, hi; };

static const struct Range zero_width[] = {
    { 0x0300u, 0x036Fu }, { 0x0483u, 0x0489u }, { 0x0591u, 0x05BDu },
    { 0x0610u, 0x061Au }, { 0x064Bu, 0x065Fu }, { 0x0670u, 0x0670u },
    { 0x06D6u, 0x06DCu }, { 0x0730u, 0x074Au }, { 0x07A6u, 0x07B0u },
    { 0x0900u, 0x0903u }, { 0x093Au, 0x094Fu }, { 0x0951u, 0x0957u },
    { 0x1AB0u, 0x1AFFu }, { 0x1DC0u, 0x1DFFu }, { 0x20D0u, 0x20F0u },
    { 0xFE00u, 0xFE0Fu }, { 0xFE20u, 0xFE2Fu },
};

static const struct Range wide[] = {
    { 0x1100u, 0x115Fu }, { 0x2E80u, 0x303Eu }, { 0x3041u, 0x33FFu },
    { 0x3400u, 0x4DBFu }, { 0x4E00u, 0x9FFFu }, { 0xA000u, 0xA4CFu },
    { 0xAC00u, 0xD7A3u }, { 0xF900u, 0xFAFFu }, { 0xFE30u, 0xFE6Fu },
    { 0xFF00u, 0xFF60u }, { 0xFFE0u, 0xFFE6u },
    { 0x1F300u, 0x1F64Fu }, { 0x1F900u, 0x1F9FFu }, { 0x20000u, 0x3FFFDu },
};

static int in_ranges(uint32_t cp, const struct Range *r, size_t n)
{
    size_t lo = 0, hi = n;
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        if (cp < r[mid].lo)      hi = mid;
        else if (cp > r[mid].hi) lo = mid + 1;
        else                     return 1;
    }
    return 0;
}

int utf8_width(uint32_t cp)
{
    if (cp == 0) return 0;
    if (cp < 32u || (cp >= 0x7Fu && cp < 0xA0u)) return 0;
    if (cp < 0x0300u) return 1;                 /* fast path: Latin */

    /* Second fast path, and the one that matters for a map: every glyph the
     * grid draws by the thousand -- box drawing, block elements, geometric
     * shapes, arrows -- lives in [0x2100, 0x2E80), and neither table has an
     * entry in that span. Without this, drawing a wall segment costs two
     * binary searches per cell. UTIL_FASTPATH_LO/HI are asserted against the
     * tables in the test suite so the shortcut cannot drift. */
    if (cp >= UTIL_WIDTH_FASTPATH_LO && cp < UTIL_WIDTH_FASTPATH_HI) return 1;

    if (in_ranges(cp, zero_width, sizeof zero_width / sizeof *zero_width)) return 0;
    if (in_ranges(cp, wide, sizeof wide / sizeof *wide)) return 2;
    return 1;
}

/* Lets the test suite prove the fast-path window really is empty in both
 * tables, rather than trusting a comment. */
int utf8_width_slow(uint32_t cp)
{
    if (cp == 0) return 0;
    if (cp < 32u || (cp >= 0x7Fu && cp < 0xA0u)) return 0;
    if (in_ranges(cp, zero_width, sizeof zero_width / sizeof *zero_width)) return 0;
    if (in_ranges(cp, wide, sizeof wide / sizeof *wide)) return 2;
    return 1;
}

/* ---------------------------------------------------------------- string */

static char lower(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c; }

size_t str_lcpy(char *dst, const char *src, size_t dstsz)
{
    size_t srclen = strlen(src);
    if (dstsz) {
        size_t n = srclen < dstsz - 1 ? srclen : dstsz - 1;
        memcpy(dst, src, n);
        dst[n] = '\0';
    }
    return srclen;
}

const char *str_casestr(const char *hay, const char *needle)
{
    if (!*needle) return hay;

    for (; *hay; hay++) {
        const char *h = hay, *n = needle;
        while (*h && *n && lower(*h) == lower(*n)) { h++; n++; }
        if (!*n) return hay;
    }
    return NULL;
}
