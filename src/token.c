#include "token.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"

#include "grid.h"
#include "render.h"

void tokens_free(TokenList *l)
{
    free(l->v);
    l->v = NULL;
    l->n = l->cap = 0;
}

int tokens_add(TokenList *l, Token t)
{
    if (l->n == l->cap) {
        l->cap = l->cap ? l->cap * 2 : 16;
        l->v   = xrealloc(l->v, (size_t)l->cap * sizeof(Token));
    }
    if (t.size < 1) t.size = 1;
    if (t.size > TOKEN_SIZE_MAX) t.size = TOKEN_SIZE_MAX;
    l->v[l->n] = t;
    return l->n++;
}

void tokens_remove(TokenList *l, int idx)
{
    if (idx < 0 || idx >= l->n) return;
    memmove(&l->v[idx], &l->v[idx + 1], (size_t)(l->n - idx - 1) * sizeof(Token));
    l->n--;
}

int tokens_at(const TokenList *l, int x, int y)
{
    /* Newest first: a token dropped on top of another is the one you grab. */
    for (int i = l->n - 1; i >= 0; i--) {
        const Token *t = &l->v[i];
        if (x >= t->x && y >= t->y && x < t->x + t->size && y < t->y + t->size)
            return i;
    }
    return -1;
}

int tokens_covered_next(const TokenList *l, int x, int y, int size, int after)
{
    if (l->n <= 0) return -1;

    /* From just past the current one, wrapping, so the walk ends back where
     * it started rather than stopping at the end of the list. */
    int start = (after >= 0 && after < l->n) ? after + 1 : 0;

    for (int k = 0; k < l->n; k++) {
        int i = (start + k) % l->n;
        const Token *t = &l->v[i];

        if (x + size <= t->x || t->x + t->size <= x) continue;
        if (y + size <= t->y || t->y + t->size <= y) continue;
        return i;
    }
    return -1;
}

int tokens_overlapping(const TokenList *l, int x, int y, int size,
                       int except, int kind)
{
    for (int i = l->n - 1; i >= 0; i--) {
        if (i == except) continue;

        const Token *t = &l->v[i];
        if (kind != TOKEN_ANY_KIND && t->kind != kind) continue;

        /* Two blocks miss each other when either axis does. */
        if (x + size <= t->x || t->x + t->size <= x) continue;
        if (y + size <= t->y || t->y + t->size <= y) continue;
        return i;
    }
    return -1;
}

const char *token_kind_name(uint8_t kind)
{
    return kind == TOKEN_ENEMY ? "enemy" : "player";
}

void tokens_unique_label(const TokenList *l, const char *base,
                         char *out, size_t outsz)
{
    if (!base || !base[0]) { str_lcpy(out, "", outsz); return; }

    char root[TOKEN_LABEL_MAX];
    str_lcpy(root, base, sizeof root);

    /* Continue an existing run instead of stacking numbers: a copy of
     * "Goblin 2" should look for "Goblin 3", not "Goblin 2 2". */
    size_t n = strlen(root), end = n;
    while (end > 0 && root[end - 1] >= '0' && root[end - 1] <= '9') end--;
    if (end < n && end > 0 && root[end - 1] == ' ') root[end - 1] = '\0';

    for (int i = 1; i < 1000; i++) {
        char cand[TOKEN_LABEL_MAX];
        /* i % 1000 is i, given the loop bound -- spelled that way so the
         * compiler can see the suffix is three digits and stop warning that
         * the buffer might not hold it. */
        if (i == 1) str_lcpy(cand, root, sizeof cand);
        else        snprintf(cand, sizeof cand, "%.24s %d", root, i % 1000);

        int taken = 0;
        for (int j = 0; j < l->n; j++)
            if (strcmp(l->v[j].label, cand) == 0) { taken = 1; break; }

        if (!taken) { str_lcpy(out, cand, outsz); return; }
    }
    str_lcpy(out, base, outsz);
}

static const char *STATUS_COLOR_NAMES[STATUS_COLOR_COUNT] = {
    "red", "orange", "yellow", "green", "cyan", "blue", "violet", "grey",
};

const char *status_color_name(uint8_t c)
{
    return c < STATUS_COLOR_COUNT ? STATUS_COLOR_NAMES[c] : "?";
}

int status_color_from_name(const char *name)
{
    for (int i = 0; i < STATUS_COLOR_COUNT; i++)
        if (strcmp(STATUS_COLOR_NAMES[i], name) == 0) return i;
    return -1;
}

int token_add_status(Token *t, uint8_t colour, const char *label)
{
    if (t->nstatus >= TOKEN_STATUS_MAX) return 0;

    Status *st = &t->status[t->nstatus];
    memset(st, 0, sizeof *st);
    st->color = (uint8_t)(colour % STATUS_COLOR_COUNT);
    str_lcpy(st->label, label ? label : "", sizeof st->label);
    t->nstatus++;
    return 1;
}

void token_clear_status(Token *t)
{
    memset(t->status, 0, sizeof t->status);
    t->nstatus = 0;
}

void token_remove_status(Token *t, int idx)
{
    if (idx < 0 || idx >= t->nstatus) return;

    /* Close the gap rather than leaving a hole: the markers are drawn and
     * numbered by position, so a hole would make the second of three answer
     * to "3" on the next pass. */
    for (int i = idx; i < t->nstatus - 1; i++) t->status[i] = t->status[i + 1];
    t->nstatus--;
    memset(&t->status[t->nstatus], 0, sizeof t->status[0]);
}

uint32_t status_glyph(const Status *st)
{
    /* A letter says more than a dot, and the colour still separates two that
     * happen to share one. */
    if (!st->label[0]) return 0x25CFu;   /* filled circle */

    unsigned char c = (unsigned char)st->label[0];
    if (c >= 'a' && c <= 'z') c = (unsigned char)(c - 'a' + 'A');
    if (c < 0x20u || c >= 0x7Fu) return 0x25CFu;   /* not a plain letter */
    return c;
}


/* ---------------------------------------------------------------- drawing */

void grid_token_area(const GridView *g, int tx, int ty, int size, Rect *out)
{
    int sx, sy;
    grid_tile_interior(g, tx, ty, &sx, &sy);

    /* A size-s token spans s interiors plus the s-1 boundaries between them,
     * which is exactly s * pitch - 1. */
    out->x = sx;
    out->y = sy;
    out->w = size * zoom_pw(g->zoom) - 1;
    out->h = size * zoom_ph(g->zoom) - 1;
}

/* Brightens a colour towards white by roughly a third, for the selected
 * token, so selection is legible without a second colour to learn. */
/* Ring on the lattice around a selected creature, in its own selected colour.
 *
 * The fill alone cannot carry selection. A token is a solid patch of colour,
 * and telling two solid patches apart is a matter of luminance -- but the
 * player green already sits near the top of that range, so the brightest
 * green still only reads 1.6:1 against the plain one, well under the 3:1 a
 * UI element needs. Contrast has to come from somewhere other than the fill,
 * and the grid lines around the creature are free: they are already drawn, so
 * this recolours them rather than painting anything new, and against the page
 * the ring reads at better than 16:1.
 *
 * The creature's own colour rather than the cursor's blue, so "what is
 * selected" and "where the cursor is" stay two different questions. */
static void draw_select_ring(Renderer *r, const Rect *a, uint32_t fg)
{
    int x0 = a->x - 1, x1 = a->x + a->w;
    int y0 = a->y - 1, y1 = a->y + a->h;

    for (int x = x0; x <= x1; x++) {
        for (int i = 0; i < 2; i++) {
            Cell *c = rnd_at(r, x, i ? y1 : y0);
            if (!c) continue;
            c->fg    = fg;
            c->attr |= ATTR_BOLD;
        }
    }
    for (int y = a->y; y < y1; y++) {
        for (int i = 0; i < 2; i++) {
            Cell *c = rnd_at(r, i ? x1 : x0, y);
            if (!c) continue;
            c->fg    = fg;
            c->attr |= ATTR_BOLD;
        }
    }
}

/* Is cell (i,j) inside the ellipse inscribed in a w x h box? The threshold is
 * below 1 so that a 3x3 token loses its four corners and reads as a circle
 * rather than as a square. */
static int in_ellipse(int i, int j, int w, int h)
{
    double dx = ((double)i + 0.5 - (double)w / 2.0) / ((double)w / 2.0);
    double dy = ((double)j + 0.5 - (double)h / 2.0) / ((double)h / 2.0);
    return dx * dx + dy * dy <= 0.82;
}

void grid_draw_token(Renderer *r, const GridView *g, const Token *t,
                     const Theme *th, int selected, int ascii)
{
    Rect a;
    grid_token_area(g, t->x, t->y, t->size, &a);
    if (a.w < 1 || a.h < 1) return;

    int      player = (t->kind != TOKEN_ENEMY);
    uint32_t base   = player ? th->player : th->enemy;
    if (selected) base = player ? th->player_sel : th->enemy_sel;

    if (selected) draw_select_ring(r, &a, base);

    /* A single-row token has no room for a shape, and colour alone is a poor
     * way to tell a player from an enemy — it fails in --ascii and for a
     * colourblind reader. So the smallest tokens carry their shape as a
     * glyph: round brackets for circles, square ones for squares. */
    if (a.h == 1) {
        Style s = style(base, th->bg, ATTR_BOLD);
        if (a.w < 3) {
            draw_cell(r, a.x, a.y, player ? 0x25CFu /* ● */ : 0x25A0u /* ■ */, s);
            return;
        }
        draw_cell(r, a.x, a.y, (uint32_t)(player ? '(' : '['), s);
        draw_cell(r, a.x + a.w - 1, a.y, (uint32_t)(player ? ')' : ']'), s);

        /* A token label is truncated to its leading characters, never to an
         * ellipsis: in three cells "Aria" has to read as A, and "(…)" names
         * nothing at the table. The status line carries the full label. */
        int avail = a.w - 2;
        int lw    = imin(text_width(t->label), avail);
        draw_text(r, a.x + 1 + (avail - lw) / 2, a.y, t->label, avail, s);
        return;
    }

    Rect body = a;

    /* "Slightly smaller than the grid square": the enemy pulls in a cell on
     * each side, but only where that still leaves it visibly wider and
     * flatter-edged than the circle it sits next to. Insetting a 5-wide
     * token makes it exactly the size of the inscribed circle, which is the
     * one thing it must not look like. */
    if (!player && body.w >= 7) { body.x += 1; body.w -= 2; }
    if (!player && body.h >= 5) { body.y += 1; body.h -= 2; }

    Style fill = style(th->bg, base, selected ? ATTR_BOLD : 0);

    for (int j = 0; j < body.h; j++) {
        for (int i = 0; i < body.w; i++) {
            /* Circles drop the cells outside the inscribed ellipse; squares
             * keep every cell. */
            if (player && !in_ellipse(i, j, body.w, body.h)) continue;
            draw_cell(r, body.x + i, body.y + j, ' ', fill);
        }
    }

    /* Without colour the fill alone is ambiguous, so ASCII mode marks the
     * shape on the middle row the same way the single-row form does. */
    if (ascii && body.w >= 3) {
        Style edge = style(th->bg, base, ATTR_BOLD);
        int   mid  = body.y + body.h / 2;
        draw_cell(r, body.x, mid, (uint32_t)(player ? '(' : '['), edge);
        draw_cell(r, body.x + body.w - 1, mid, (uint32_t)(player ? ')' : ']'), edge);
    }

    if (!t->label[0]) return;

    /* The label sits on the middle row, centred, trimmed to what fits.
     * It has to stay clear of the ASCII brackets when those are drawn, and
     * off the curve of a circle when they are not. */
    int row   = body.y + body.h / 2;
    int inset = 0;
    if (ascii && body.w >= 3)        inset = 1;
    else if (player && body.w >= 5)  inset = 1;

    int avail = body.w - 2 * inset;
    if (avail < 1) return;

    int lw = imin(text_width(t->label), avail);
    int lx = body.x + inset + (avail - lw) / 2;

    Style ls = style(th->bg, base, ATTR_BOLD);
    draw_text(r, lx, row, t->label, avail, ls);
}

void grid_draw_token_ghost(Renderer *r, const GridView *g, int tx, int ty,
                           int size, const Theme *th)
{
    Rect a;
    grid_token_area(g, tx, ty, size, &a);

    Style s = style(th->dim, th->bg, ATTR_DIM);
    if (a.w < 2 || a.h < 2) {
        draw_cell(r, a.x, a.y, 0x00B7u /* · */, s);
        return;
    }
    draw_box(r, a, &BOX_LIGHT, s);
}

void grid_draw_token_status(Renderer *r, const GridView *g, const Token *t,
                            const Theme *th, int ascii)
{
    if (!t->nstatus) return;

    Rect a;
    grid_token_area(g, t->x, t->y, t->size, &a);

    /* On the boundary rows around the token rather than on the token itself,
     * so a marker never covers the name it belongs to. Those rows exist at
     * every zoom, which a spare row inside a one-cell token would not.
     *
     * The top row fills first and the bottom takes the overflow: a one-tile
     * token at the default zoom is only three cells wide, which is fewer than
     * a token can carry. */
    if (a.w < 1) return;

    for (int i = 0; i < t->nstatus; i++) {
        int col = (i < a.w) ? i : i - a.w;
        int row = (i < a.w) ? a.y - 1 : a.y + a.h;
        if (col >= a.w) break;          /* more markers than the token has edge */

        const Status *st = &t->status[i];
        uint32_t      ch = status_glyph(st);
        if (ascii && ch > 0x7Fu) ch = '*';

        draw_cell(r, a.x + col, row, ch,
                  style(th->status[st->color % STATUS_COLOR_COUNT], th->bg, ATTR_BOLD));
    }
}
