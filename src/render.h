#ifndef VTT_RENDER_H
#define VTT_RENDER_H

#include <stdint.h>
#include "term.h"
#include "util.h"

/* Colors are 0x00RRGGBB, or COL_DEFAULT for the terminal's own default. */
#define COL_DEFAULT 0x80000000u
#define RGB(r, g, b) ((uint32_t)(((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b)))

#define ATTR_BOLD      0x01u
#define ATTR_DIM       0x02u
#define ATTR_UNDERLINE 0x04u
#define ATTR_REVERSE   0x08u

typedef struct {
    uint32_t ch;      /* Unicode scalar; 0 marks the second half of a wide glyph */
    uint32_t fg;
    uint32_t bg;
    uint8_t  attr;
    uint8_t  _pad[3];
} Cell;

typedef struct {
    int    w, h;
    Cell  *front;     /* what the terminal is currently showing */
    Cell  *back;      /* what this frame wants it to show */
    size_t ncells;

    ByteBuf out;      /* one frame's worth of escape sequences */
    int     force_full;

    /* Drawing is rejected outside this rectangle. Panels set it so a map
     * that has scrolled past its viewport cannot paint over the title bar or
     * the keybinding bar. Reset to the full screen by rnd_begin(). */
    int clip_x0, clip_y0, clip_x1, clip_y1;   /* x1/y1 exclusive */

    /* Per-frame counters. These, not wall-clock alone, are what predict
     * perceived latency in a TUI. */
    uint32_t cells_changed;
    uint32_t bytes_written;

    /* What rnd_begin() clears to. Setting it to the theme background saves a
     * full-screen fill every frame -- on a large terminal that is thousands
     * of writes that the clear was about to make anyway. */
    Cell clear_cell;
} Renderer;

void rnd_init(Renderer *r);
void rnd_free(Renderer *r);

/* Reallocates for a new size and forces a full repaint. No-op if unchanged. */
void rnd_resize(Renderer *r, int w, int h);

/* Sets the colour rnd_begin() clears to. */
void rnd_set_clear(Renderer *r, uint32_t fg, uint32_t bg);

/* Clears the back buffer. Call once at the top of each frame. */
void rnd_begin(Renderer *r);

/* Diffs back against front, emits the minimal escape sequence run, and
 * writes it in a single write(). */
void rnd_flush(Renderer *r, Term *t);

/* Bounds- and clip-checked cell access; returns NULL when not drawable. */
static inline Cell *rnd_at(Renderer *r, int x, int y)
{
    if (x < r->clip_x0 || y < r->clip_y0 || x >= r->clip_x1 || y >= r->clip_y1)
        return NULL;
    return &r->back[(size_t)y * (size_t)r->w + (size_t)x];
}

typedef struct { int x0, y0, x1, y1; } ClipRect;

/* Narrows the clip to the intersection with the given rectangle and returns
 * the previous one, to be handed back to rnd_clip_restore(). */
ClipRect rnd_clip_push(Renderer *r, int x, int y, int w, int h);
void     rnd_clip_restore(Renderer *r, ClipRect saved);

/* Renders the back buffer as plain UTF-8 text, colors stripped, one line per
 * row. Backs --dump-frame and the golden tests. */
void rnd_dump(const Renderer *r, ByteBuf *out);

#endif /* VTT_RENDER_H */
