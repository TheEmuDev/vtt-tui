#include "draw.h"

#include <string.h>

const BoxGlyphs BOX_LIGHT  = { 0x250Cu, 0x2510u, 0x2514u, 0x2518u, 0x2500u, 0x2502u };
const BoxGlyphs BOX_HEAVY  = { 0x250Fu, 0x2513u, 0x2517u, 0x251Bu, 0x2501u, 0x2503u };
const BoxGlyphs BOX_ROUND  = { 0x256Du, 0x256Eu, 0x2570u, 0x256Fu, 0x2500u, 0x2502u };
const BoxGlyphs BOX_DOUBLE = { 0x2554u, 0x2557u, 0x255Au, 0x255Du, 0x2550u, 0x2551u };
const BoxGlyphs BOX_ASCII  = { '+', '+', '+', '+', '-', '|' };

static int g_ascii;

void draw_set_ascii(int on) { g_ascii = on; }
int  draw_is_ascii(void)    { return g_ascii; }

Rect rect_center(Rect outer, int w, int h)
{
    if (w > outer.w) w = outer.w;
    if (h > outer.h) h = outer.h;
    return rect(outer.x + (outer.w - w) / 2, outer.y + (outer.h - h) / 2, w, h);
}

int rect_contains(Rect r, int x, int y)
{
    return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h;
}

void draw_cell(Renderer *r, int x, int y, uint32_t ch, Style s)
{
    Cell *c = rnd_at(r, x, y);
    if (!c) return;
    c->ch   = ch;
    c->fg   = s.fg;
    c->bg   = s.bg;
    c->attr = s.attr;

    /* A double-width glyph owns the next cell too; mark it as a continuation
     * so the flush loop knows not to emit anything there. The range check
     * first is not just an optimisation of the common case: nothing below
     * the first wide range can be double-width, so it is exact. */
    if (ch >= 0x1100u && utf8_width(ch) == 2) {
        Cell *n = rnd_at(r, x + 1, y);
        if (n) {
            n->ch   = 0;
            n->fg   = s.fg;
            n->bg   = s.bg;
            n->attr = s.attr;
        }
    }
}

int text_width(const char *utf8)
{
    size_t len = strlen(utf8);
    size_t i   = 0;
    int    w   = 0;
    while (i < len) {
        uint32_t cp;
        int      n = utf8_decode(utf8 + i, len - i, &cp);
        if (n <= 0) break;
        i += (size_t)n;
        w += utf8_width(cp);
    }
    return w;
}

int draw_text(Renderer *r, int x, int y, const char *utf8, int maxw, Style s)
{
    size_t len = strlen(utf8);
    size_t i   = 0;
    int    adv = 0;

    while (i < len) {
        uint32_t cp;
        int      n = utf8_decode(utf8 + i, len - i, &cp);
        if (n <= 0) break;
        i += (size_t)n;

        int cw = utf8_width(cp);
        if (cw == 0) continue;                 /* combining mark: nothing to place */
        if (maxw >= 0 && adv + cw > maxw) break;

        draw_cell(r, x + adv, y, cp, s);
        adv += cw;
    }
    return adv;
}

int draw_text_ellipsis(Renderer *r, int x, int y, const char *utf8, int maxw, Style s)
{
    if (maxw <= 0) return 0;
    if (text_width(utf8) <= maxw) return draw_text(r, x, y, utf8, maxw, s);

    int adv = draw_text(r, x, y, utf8, maxw - 1, s);
    draw_cell(r, x + adv, y, g_ascii ? '~' : 0x2026u /* … */, s);
    return adv + 1;
}

void draw_hline(Renderer *r, int x, int y, int w, uint32_t ch, Style s)
{
    for (int i = 0; i < w; i++) draw_cell(r, x + i, y, ch, s);
}

void draw_vline(Renderer *r, int x, int y, int h, uint32_t ch, Style s)
{
    for (int i = 0; i < h; i++) draw_cell(r, x, y + i, ch, s);
}

void draw_fill(Renderer *r, Rect rc, uint32_t ch, Style s)
{
    for (int y = 0; y < rc.h; y++)
        for (int x = 0; x < rc.w; x++)
            draw_cell(r, rc.x + x, rc.y + y, ch, s);
}

void draw_box(Renderer *r, Rect rc, const BoxGlyphs *g, Style s)
{
    if (rc.w < 2 || rc.h < 2) return;

    int x1 = rc.x + rc.w - 1;
    int y1 = rc.y + rc.h - 1;

    draw_hline(r, rc.x + 1, rc.y, rc.w - 2, g->h, s);
    draw_hline(r, rc.x + 1, y1,    rc.w - 2, g->h, s);
    draw_vline(r, rc.x,     rc.y + 1, rc.h - 2, g->v, s);
    draw_vline(r, x1,       rc.y + 1, rc.h - 2, g->v, s);

    draw_cell(r, rc.x, rc.y, g->tl, s);
    draw_cell(r, x1,   rc.y, g->tr, s);
    draw_cell(r, rc.x, y1,   g->bl, s);
    draw_cell(r, x1,   y1,   g->br, s);
}
