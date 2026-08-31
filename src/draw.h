#ifndef VTT_DRAW_H
#define VTT_DRAW_H

#include "render.h"

typedef struct { int x, y, w, h; } Rect;

typedef struct {
    uint32_t fg;
    uint32_t bg;
    uint8_t  attr;
} Style;

/* Six glyphs are enough to draw any simple frame. */
typedef struct {
    uint32_t tl, tr, bl, br, h, v;
} BoxGlyphs;

extern const BoxGlyphs BOX_LIGHT;
extern const BoxGlyphs BOX_HEAVY;
extern const BoxGlyphs BOX_ROUND;
extern const BoxGlyphs BOX_DOUBLE;
extern const BoxGlyphs BOX_ASCII;

/* --ascii is a property of the output device, not of any one call site, so
 * it lives here rather than being threaded through every draw. It only
 * affects glyph choices that have no caller-supplied alternative, such as
 * the ellipsis used when text is truncated. */
void draw_set_ascii(int on);
int  draw_is_ascii(void);

static inline Style style(uint32_t fg, uint32_t bg, uint8_t attr)
{
    Style s = { fg, bg, attr };
    return s;
}

static inline Rect rect(int x, int y, int w, int h)
{
    Rect r = { x, y, w, h };
    return r;
}

/* Centers a w x h box inside outer; clamps rather than going negative. */
Rect rect_center(Rect outer, int w, int h);
int  rect_contains(Rect r, int x, int y);

void draw_cell(Renderer *r, int x, int y, uint32_t ch, Style s);

/* Draws UTF-8 text, stopping at maxw cells (maxw < 0 means unlimited).
 * Returns the number of cells advanced. */
int  draw_text(Renderer *r, int x, int y, const char *utf8, int maxw, Style s);

/* Like draw_text but appends an ellipsis when the string had to be cut. */
int  draw_text_ellipsis(Renderer *r, int x, int y, const char *utf8, int maxw, Style s);

/* Cell width of a UTF-8 string, for centering and layout. */
int  text_width(const char *utf8);

void draw_hline(Renderer *r, int x, int y, int w, uint32_t ch, Style s);
void draw_vline(Renderer *r, int x, int y, int h, uint32_t ch, Style s);
void draw_fill(Renderer *r, Rect rc, uint32_t ch, Style s);
void draw_box(Renderer *r, Rect rc, const BoxGlyphs *g, Style s);

#endif /* VTT_DRAW_H */
