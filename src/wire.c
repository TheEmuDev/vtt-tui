#include "wire.h"

#include <stdlib.h>
#include <string.h>

#include "util.h"

#define PAL_BUCKETS 4096

/* ---------------------------------------------------------------- bytes */

static void put8(WireEnc *e, uint8_t v)
{
    if (e->len < e->cap) e->buf[e->len++] = v;
    else                 e->overflow = 1;
}

static void put16(WireEnc *e, uint32_t v)
{
    put8(e, (uint8_t)v);
    put8(e, (uint8_t)(v >> 8));
}

static uint32_t get16(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8); }

/* -------------------------------------------------------------- palette */

/* Colours are 24-bit but a session meets a few dozen. The hash is a small
 * open-addressed table of index+1 so the lookup per cell is a couple of
 * probes and never a scan. */
static uint32_t pal_slot(uint32_t rgb)
{
    uint32_t h = rgb * 2654435761u;
    return (h >> 20) & (PAL_BUCKETS - 1);
}

static int pal_find(const WireEnc *e, uint32_t rgb)
{
    uint32_t s = pal_slot(rgb);
    for (int probe = 0; probe < PAL_BUCKETS; probe++) {
        uint8_t v = e->pal_hash[s];
        if (v == 0) return -1;
        if (e->pal[v - 1] == rgb) return v - 1;
        s = (s + 1) & (PAL_BUCKETS - 1);
    }
    return -1;
}

static void pal_insert(WireEnc *e, uint32_t rgb, int index)
{
    uint32_t s = pal_slot(rgb);
    while (e->pal_hash[s]) s = (s + 1) & (PAL_BUCKETS - 1);
    e->pal_hash[s] = (uint8_t)(index + 1);
    e->pal[index]  = rgb;
}

static uint8_t pal_index(WireEnc *e, uint32_t rgb)
{
    rgb &= 0xFFFFFFu;
    int i = pal_find(e, rgb);
    if (i >= 0) return (uint8_t)i;

    /* Past 256 colours the table would need evicting and clients told; a
     * frame of that many colours is not one this program draws. Reuse the
     * last index and let that cell be a shade off rather than grow. */
    if (e->npal >= WIRE_PAL_MAX) return (uint8_t)(WIRE_PAL_MAX - 1);

    i = e->npal++;
    pal_insert(e, rgb, i);
    return (uint8_t)i;
}

/* -------------------------------------------------------------- encoder */

void wire_enc_init(WireEnc *e, size_t cap)
{
    memset(e, 0, sizeof *e);
    e->buf      = xmalloc(cap);
    e->cap      = cap;
    e->pal_hash = xcalloc(PAL_BUCKETS, 1);
    e->run_n    = 0;
}

void wire_enc_free(WireEnc *e)
{
    free(e->buf);
    free(e->pal_hash);
    memset(e, 0, sizeof *e);
}

void wire_enc_reset_palette(WireEnc *e)
{
    memset(e->pal_hash, 0, PAL_BUCKETS);
    e->npal = 0;
}

void wire_enc_begin(WireEnc *e)
{
    e->len      = 0;
    e->overflow = 0;
    e->run_n    = 0;
    e->cells    = 0;
}

static void run_close(WireEnc *e)
{
    if (e->run_n == 0) return;
    /* The count was left blank when the header went out. */
    if (e->run_at + 5 <= e->cap) {
        e->buf[e->run_at + 5] = (uint8_t)e->run_n;
        e->buf[e->run_at + 6] = (uint8_t)(e->run_n >> 8);
    }
    e->run_n = 0;
}

static void run_open(WireEnc *e, int x, int y, uint8_t fg, uint8_t bg, uint8_t attr)
{
    e->run_at   = e->len;
    e->run_x    = x;
    e->run_y    = y;
    e->run_fg   = fg;
    e->run_bg   = bg;
    e->run_attr = attr;
    put8(e, 'R');
    put16(e, (uint32_t)x);
    put16(e, (uint32_t)y);
    put16(e, 0);                   /* n, filled in by run_close */
    put8(e, fg);
    put8(e, bg);
    put8(e, attr);
}

void wire_enc_cell(WireEnc *e, int x, int y, const Cell *c)
{
    uint8_t fg   = pal_index(e, c->fg);
    uint8_t bg   = pal_index(e, c->bg);
    uint8_t attr = c->attr;

    int continues = e->run_n > 0 && y == e->run_y && x == e->run_x + e->run_n &&
                    fg == e->run_fg && bg == e->run_bg && attr == e->run_attr &&
                    e->run_n < WIRE_RUN_MAX;
    if (!continues) {
        run_close(e);
        run_open(e, x, y, fg, bg, attr);
    }

    uint32_t ch = c->ch;
    if (ch > 0xFFFF) ch = 0xFFFD;
    put16(e, ch);
    e->run_n++;
    e->cells++;
}

void wire_enc_end(WireEnc *e)
{
    run_close(e);
    put8(e, 'E');
}

void wire_enc_full(WireEnc *e, const Renderer *r)
{
    wire_enc_begin(e);
    put8(e, 'F');
    put16(e, (uint32_t)r->w);
    put16(e, (uint32_t)r->h);
    for (int y = 0; y < r->h; y++)
        for (int x = 0; x < r->w; x++)
            wire_enc_cell(e, x, y, &r->front[(size_t)y * (size_t)r->w + (size_t)x]);
    wire_enc_end(e);
}

size_t wire_enc_palette(const WireEnc *e, int from, uint8_t *out, size_t cap)
{
    size_t n = 0;
    for (int i = from < 0 ? 0 : from; i < e->npal && n + 5 <= cap; i++) {
        out[n++] = 'P';
        out[n++] = (uint8_t)i;
        out[n++] = (uint8_t)(e->pal[i] >> 16);
        out[n++] = (uint8_t)(e->pal[i] >> 8);
        out[n++] = (uint8_t)e->pal[i];
    }
    return n;
}

/* -------------------------------------------------------------- decoder */

void wire_dec_init(WireDec *d, const WireSink *sink, void *ctx)
{
    memset(d, 0, sizeof *d);
    d->sink = sink;
    d->ctx  = ctx;
}

/* How many bytes the record starting at p needs, given `have` of them so
 * far; 0 when the tag is unknown. A run's length is not known until its
 * header is in. */
static size_t record_len(const uint8_t *p, size_t have)
{
    switch (p[0]) {
    case 'F': return 5;
    case 'P': return 5;
    case 'E': return 1;
    case 'Z': return 1;
    case 'R':
        if (have < 10) return 10;                     /* enough to read n */
        return 10 + 2 * get16(p + 5);
    default:  return 0;
    }
}

static void dispatch(WireDec *d, const uint8_t *p)
{
    const WireSink *s = d->sink;
    switch (p[0]) {
    case 'F': if (s->full) s->full(d->ctx, (int)get16(p + 1), (int)get16(p + 3)); break;
    case 'P': if (s->pal)  s->pal(d->ctx, p[1], ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 8) | p[4]); break;
    case 'E': if (s->end)  s->end(d->ctx);  break;
    case 'Z': if (s->ping) s->ping(d->ctx); break;
    case 'R': {
        int n = (int)get16(p + 5);
        uint16_t glyphs[WIRE_RUN_MAX];
        for (int i = 0; i < n; i++) glyphs[i] = (uint16_t)get16(p + 10 + 2 * i);
        if (s->run) s->run(d->ctx, (int)get16(p + 1), (int)get16(p + 3), n, p[7], p[8], p[9], glyphs);
        break;
    }
    default: break;
    }
}

size_t wire_dec_feed(WireDec *d, const uint8_t *p, size_t n)
{
    size_t used = 0;
    /* Whole records left in the hold after the input runs out are still
     * records; keep going until the hold is short of one. */
    while ((used < n || d->held) && !d->bad) {
        /* Top the hold up to a whole record, then dispatch it. Records that
         * arrive whole and alone never touch the hold. */
        const uint8_t *rec;
        size_t         avail;
        if (d->held) {
            size_t take = n - used;
            if (d->held + take > sizeof d->hold) take = sizeof d->hold - d->held;
            memcpy(d->hold + d->held, p + used, take);
            d->held += take;
            used    += take;
            rec = d->hold; avail = d->held;
        } else {
            rec = p + used; avail = n - used;
        }

        size_t need = record_len(rec, avail);
        if (need == 0 || need > sizeof d->hold) { d->bad = 1; break; }
        if (avail < need) {
            if (rec != d->hold) {
                memcpy(d->hold, rec, avail);
                d->held = avail;
                used    = n;
            }
            break;                                     /* wait for the rest */
        }
        dispatch(d, rec);
        if (rec == d->hold) {
            /* Anything beyond the record was over-read into the hold. */
            size_t extra = d->held - need;
            memmove(d->hold, d->hold + need, extra);
            d->held = extra;
        } else {
            used += need;
        }
    }
    return used;
}
