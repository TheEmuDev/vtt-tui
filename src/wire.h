#ifndef VTT_WIRE_H
#define VTT_WIRE_H

#include <stddef.h>
#include <stdint.h>

#include "render.h"

/* The frame stream a remote view receives. Records, each one byte of tag
 * and a fixed header, no text anywhere:
 *
 *   'F' u16 w, u16 h            a full frame follows; the grid is this size
 *   'P' u8 index, u8 r,g,b      a palette entry, sent the first time a colour is met
 *   'R' u16 x,y, u16 n,          n consecutive cells in row y from column x,
 *       u8 fg,bg,attr,           sharing colours, then n x u16 glyph; a 0 glyph is
 *       u16 glyph[n]             the second half of the wide one before it
 *   'E'                          end of frame: present it
 *   'Z'                          keep-alive to a watcher; nothing to draw (a browser gets
 *                                a WebSocket ping instead)
 *
 * All integers little-endian. Glyphs are sixteen-bit: everything the grid
 * draws is in the basic plane, and a rare astral glyph goes out as the
 * replacement character rather than widening every record for it.
 *
 * One encoder serves every client: the frame is encoded once per flush and
 * the bytes are copied to each. The palette belongs to the encoder and is
 * never written inline: each client remembers how many entries it has been
 * told, and the server sends it the ones it lacks ahead of its next frame,
 * so a client that joined late is told the same colours as one that did
 * not. */

#define WIRE_PAL_MAX  256
#define WIRE_RUN_MAX  4096       /* glyphs in one run; a row is never wider */

typedef struct {
    uint8_t  *buf;               /* the frame being built */
    size_t    len, cap;
    int       overflow;          /* the frame did not fit: drop it, send FULL */

    uint32_t  pal[WIRE_PAL_MAX]; /* colour by index */
    int       npal;
    uint8_t  *pal_hash;          /* 4096 buckets of index+1, 0 for empty */

    /* The run being extended, flushed when the next cell does not continue it. */
    int       run_x, run_y, run_n;
    uint8_t   run_fg, run_bg, run_attr;
    size_t    run_at;            /* offset of the run header in buf */
    uint32_t  cells;             /* cells encoded this frame */
} WireEnc;

/* cap is the largest frame the encoder will build; a 200x50 full frame is
 * about 24 KB of runs. Allocates once. */
void wire_enc_init(WireEnc *e, size_t cap);
void wire_enc_free(WireEnc *e);

/* A frame is: begin, then cells in row-major order (any subset), then end.
 * Cells that continue a run cost two bytes; a break costs a header. */
void wire_enc_begin(WireEnc *e);
void wire_enc_cell(WireEnc *e, int x, int y, const Cell *c);
void wire_enc_end(WireEnc *e);

/* The whole front buffer as one FULL frame, for a client that just joined
 * or fell behind. */
void wire_enc_full(WireEnc *e, const Renderer *r);

/* Palette entries from index `from` up, as 'P' records into out (up to cap
 * bytes). Returns the bytes written; a client then knows e->npal entries. */
size_t wire_enc_palette(const WireEnc *e, int from, uint8_t *out, size_t cap);

/* Forgets the palette; every colour is re-announced as it is next met. Used
 * when the last client leaves, so the table cannot grow across a session. */
void wire_enc_reset_palette(WireEnc *e);

/* ------------------------------------------------------------- decoder */

typedef struct {
    void (*full)(void *ctx, int w, int h);
    void (*pal)(void *ctx, int index, uint32_t rgb);
    void (*run)(void *ctx, int x, int y, int n, uint8_t fg, uint8_t bg, uint8_t attr,
                const uint16_t *glyphs);
    void (*end)(void *ctx);
    void (*keepalive)(void *ctx);
} WireSink;

/* Consumes bytes as they arrive; a record split across reads is held until
 * the rest comes. Fixed memory: the largest record is a full-width run. */
typedef struct {
    const WireSink *sink;
    void           *ctx;
    uint8_t         hold[16 + 2 * WIRE_RUN_MAX];
    size_t          held;
    int             bad;         /* a record the decoder cannot read; stop */
} WireDec;

void   wire_dec_init(WireDec *d, const WireSink *sink, void *ctx);
/* Returns the bytes consumed (all of them unless the stream is bad). */
size_t wire_dec_feed(WireDec *d, const uint8_t *p, size_t n);

#endif /* VTT_WIRE_H */
