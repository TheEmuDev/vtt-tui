#include "grid.h"

#include <stdio.h>
#include <string.h>

#include "prof.h"

/* Interior sizes. Odd widths keep a true centre cell for tokens and labels.
 * Pitch is interior + 1, the extra cell being the shared boundary. */
const ZoomLevel ZOOM[ZOOM_COUNT] = {
    { 1, 1 },   /* Z0  pitch 2x2  — whole-encounter overview */
    { 3, 1 },   /* Z1  pitch 4x2  — default */
    { 5, 2 },   /* Z2  pitch 6x3  — comfortable play */
    { 7, 3 },   /* Z3  pitch 8x4  — detail, full in-token labels */
};

/* Junction glyphs indexed by a 4-bit mask: N=1, E=2, S=4, W=8. */
#define JN 1u
#define JE 2u
#define JS 4u
#define JW 8u

static const uint32_t JUNC_LIGHT[16] = {
    0x0020u, /* ....  nothing            */
    0x2575u, /* N     ╵ */
    0x2576u, /* E     ╶ */
    0x2514u, /* NE    └ */
    0x2577u, /* S     ╷ */
    0x2502u, /* NS    │ */
    0x250Cu, /* ES    ┌ */
    0x251Cu, /* NES   ├ */
    0x2574u, /* W     ╴ */
    0x2518u, /* NW    ┘ */
    0x2500u, /* EW    ─ */
    0x2534u, /* NEW   ┴ */
    0x2510u, /* SW    ┐ */
    0x2524u, /* NSW   ┤ */
    0x252Cu, /* ESW   ┬ */
    0x253Cu, /* NESW  ┼ */
};

static const uint32_t JUNC_HEAVY[16] = {
    0x0020u,
    0x2579u, /* N     ╹ */
    0x257Au, /* E     ╺ */
    0x2517u, /* NE    ┗ */
    0x257Bu, /* S     ╻ */
    0x2503u, /* NS    ┃ */
    0x250Fu, /* ES    ┏ */
    0x2523u, /* NES   ┣ */
    0x2578u, /* W     ╸ */
    0x251Bu, /* NW    ┛ */
    0x2501u, /* EW    ━ */
    0x253Bu, /* NEW   ┻ */
    0x2513u, /* SW    ┓ */
    0x252Bu, /* NSW   ┫ */
    0x2533u, /* ESW   ┳ */
    0x254Bu, /* NESW  ╋ */
};

static const uint32_t JUNC_ASCII[16] = {
    ' ', '|', '-', '+', '|', '|', '+', '+',
    '-', '+', '-', '+', '+', '+', '+', '+',
};

/* Terrain fill glyphs. Colours live in the theme; these are the shapes, and
 * the ASCII column keeps the palette distinguishable without them. */
static const struct {
    uint32_t glyph;
    uint32_t ascii;
} TERRAIN_LOOK[TILE_COUNT] = {
    [TILE_VOID]   = { ' ',     ' ' },
    [TILE_FLOOR]  = { ' ',     ' ' },
    [TILE_WATER]  = { 0x2248u, '~' },   /* approx sign, reads as ripples */
    [TILE_ROUGH]  = { 0x2237u, ':' },   /* proportion sign, reads as scree */
    [TILE_BRUSH]  = { 0x201Cu, '\'' },  /* open quotes, reads as tufts */
    [TILE_WOOD]   = { 0x2550u, '=' },   /* double line, reads as planking */
    [TILE_HAZARD] = { 0x25B2u, '^' },   /* triangle, reads as spikes */
};

/* A boundary's weight decides the junction it forms; its glyph and colour say
 * which kind it is. Weight 2 is solid, 1 is something you can step over or
 * through, which joins the grid lines rather than the walls. */
static int edge_weight(uint8_t kind)
{
    switch (kind) {
    case EDGE_NONE:        return 0;
    case EDGE_DOOR_OPEN:
    case EDGE_SECRET_OPEN:  return 1;
    default:               return 2;
    }
}

_Static_assert(TILE_COUNT == 7, "terrain palette and TileKind have drifted apart");
_Static_assert(EDGE_COUNT == 7, "edge_weight and EdgeKind have drifted apart");

int grid_cells_w(const Map *m, int zoom) { return m->w * zoom_pw(zoom) + 1; }
int grid_cells_h(const Map *m, int zoom) { return m->h * zoom_ph(zoom) + 1; }

void grid_tile_screen(const GridView *g, int tx, int ty, int *sx, int *sy)
{
    *sx = g->view.x + tx * zoom_pw(g->zoom) - g->cam_x;
    *sy = g->view.y + ty * zoom_ph(g->zoom) - g->cam_y;
}

void grid_tile_interior(const GridView *g, int tx, int ty, int *sx, int *sy)
{
    grid_tile_screen(g, tx, ty, sx, sy);
    *sx += 1;
    *sy += 1;
}

void grid_clamp_camera(GridView *g, const Map *m)
{
    int cw = grid_cells_w(m, g->zoom);
    int ch = grid_cells_h(m, g->zoom);

    /* A map smaller than its viewport is centred rather than pinned to a
     * corner, which is what "the editor is centred in the application" means
     * once the map no longer fills the space. */
    if (cw <= g->view.w) g->cam_x = -(g->view.w - cw) / 2;
    else                 g->cam_x = iclamp(g->cam_x, 0, cw - g->view.w);

    if (ch <= g->view.h) g->cam_y = -(g->view.h - ch) / 2;
    else                 g->cam_y = iclamp(g->cam_y, 0, ch - g->view.h);
}

void grid_center_on(GridView *g, const Map *m, int tx, int ty)
{
    int pw = zoom_pw(g->zoom), ph = zoom_ph(g->zoom);
    g->cam_x = tx * pw + pw / 2 - g->view.w / 2;
    g->cam_y = ty * ph + ph / 2 - g->view.h / 2;
    grid_clamp_camera(g, m);
}

void grid_ensure_visible(GridView *g, const Map *m, int tx, int ty, int margin)
{
    int pw = zoom_pw(g->zoom), ph = zoom_ph(g->zoom);

    /* The tile's full footprint, boundaries included, plus the margin. */
    int x0 = (tx - margin) * pw;
    int x1 = (tx + 1 + margin) * pw;
    int y0 = (ty - margin) * ph;
    int y1 = (ty + 1 + margin) * ph;

    if (x0 < g->cam_x)             g->cam_x = x0;
    if (x1 > g->cam_x + g->view.w) g->cam_x = x1 - g->view.w;
    if (y0 < g->cam_y)             g->cam_y = y0;
    if (y1 > g->cam_y + g->view.h) g->cam_y = y1 - g->view.h;

    grid_clamp_camera(g, m);
}

void grid_set_zoom(GridView *g, const Map *m, int zoom, int anchor_tx, int anchor_ty)
{
    zoom = iclamp(zoom, 0, ZOOM_COUNT - 1);
    if (zoom == g->zoom) return;

    /* Where the anchor sits within the viewport now; put it back there after
     * the pitch changes so zooming feels like it pivots on the cursor. */
    int sx, sy;
    grid_tile_screen(g, anchor_tx, anchor_ty, &sx, &sy);
    int off_x = sx - g->view.x;
    int off_y = sy - g->view.y;

    g->zoom  = zoom;
    g->cam_x = anchor_tx * zoom_pw(zoom) - off_x;
    g->cam_y = anchor_ty * zoom_ph(zoom) - off_y;
    grid_clamp_camera(g, m);
}

int grid_screen_to_tile(const GridView *g, const Map *m, int sx, int sy, int *tx, int *ty)
{
    int pw = zoom_pw(g->zoom), ph = zoom_ph(g->zoom);
    int mx = sx - g->view.x + g->cam_x;
    int my = sy - g->view.y + g->cam_y;
    if (mx < 0 || my < 0) return 0;

    int x = mx / pw, y = my / ph;
    if (!map_in_bounds(m, x, y)) return 0;
    *tx = x;
    *ty = y;
    return 1;
}

/* ------------------------------------------------------------- segments */

/* Level 0 = nothing to draw, 1 = thin, 2 = solid. Grid lines exist only where
 * a walkable tile touches the boundary, which is what "grid lines only show on
 * walkable tiles" means at the edge model; a boundary you can step through
 * draws thin and joins them. */
typedef struct {
    int     level;
    uint8_t kind;
} Seg;

static Seg vseg(const Map *m, int x, int y)
{
    Seg s = { 0, EDGE_NONE };
    if (x < 0 || x > m->w || y < 0 || y >= m->h) return s;

    s.kind  = map_vedge(m, x, y);
    s.level = edge_weight(s.kind);
    if (s.level == 0 && (map_walkable(m, x - 1, y) || map_walkable(m, x, y)))
        s.level = 1;
    return s;
}

static Seg hseg(const Map *m, int x, int y)
{
    Seg s = { 0, EDGE_NONE };
    if (y < 0 || y > m->h || x < 0 || x >= m->w) return s;

    s.kind  = map_hedge(m, x, y);
    s.level = edge_weight(s.kind);
    if (s.level == 0 && (map_walkable(m, x, y - 1) || map_walkable(m, x, y)))
        s.level = 1;
    return s;
}

/* Glyph and colour for a run of one boundary kind. A secret door is a wall in
 * every respect until `reveal` says otherwise. */
static void seg_look(uint8_t kind, int vertical, int ascii, int reveal,
                     const Theme *th, uint32_t *glyph, uint32_t *fg)
{
    if (!reveal && kind == EDGE_SECRET_CLOSED) kind = EDGE_WALL;
    if (!reveal && kind == EDGE_SECRET_OPEN)   kind = EDGE_DOOR_OPEN;

    switch (kind) {
    case EDGE_DOOR_CLOSED:
        *glyph = ascii ? '+' : (vertical ? 0x2551u : 0x2550u);
        *fg    = th->edge_door;
        break;
    case EDGE_DOOR_OPEN:
        *glyph = ascii ? 0x27u : (vertical ? 0x2502u : 0x2500u);
        *fg    = th->edge_door;
        break;
    case EDGE_WINDOW:
        *glyph = ascii ? '"' : (vertical ? 0x2507u : 0x2505u);
        *fg    = th->edge_window;
        break;
    case EDGE_SECRET_CLOSED:
        *glyph = ascii ? 'S' : (vertical ? 0x2503u : 0x2501u);
        *fg    = th->edge_secret;
        break;
    case EDGE_SECRET_OPEN:
        *glyph = ascii ? 's' : (vertical ? 0x2502u : 0x2500u);
        *fg    = th->edge_secret;
        break;
    case EDGE_WALL:
        *glyph = ascii ? (vertical ? '|' : '=') : (vertical ? 0x2503u : 0x2501u);
        *fg    = th->wall;
        break;
    default:
        *glyph = ascii ? (vertical ? '|' : '-') : (vertical ? 0x2502u : 0x2500u);
        *fg    = th->grid;
        break;
    }
}

static int fdiv(int a, int b) { return a >= 0 ? a / b : -(((-a) + b - 1) / b); }

void grid_visible_tiles(const GridView *g, const Map *m,
                        int *x0, int *y0, int *x1, int *y1)
{
    int pw = zoom_pw(g->zoom), ph = zoom_ph(g->zoom);

    *x0 = iclamp(fdiv(g->cam_x, pw), 0, m->w - 1);
    *y0 = iclamp(fdiv(g->cam_y, ph), 0, m->h - 1);
    *x1 = iclamp(fdiv(g->cam_x + g->view.w, pw), 0, m->w - 1);
    *y1 = iclamp(fdiv(g->cam_y + g->view.h, ph), 0, m->h - 1);
}

void grid_draw(Renderer *r, const Map *m, const GridView *g, const Theme *th,
               int ascii, int reveal)
{
    PROF_ZONE("grid.draw");

    int pw = zoom_pw(g->zoom), ph = zoom_ph(g->zoom);
    int iw = ZOOM[g->zoom].iw, ih = ZOOM[g->zoom].ih;

    const uint32_t *light = ascii ? JUNC_ASCII : JUNC_LIGHT;
    const uint32_t *heavy = ascii ? JUNC_ASCII : JUNC_HEAVY;

    Style s_grid = style(th->grid, th->bg, 0);
    Style s_wall = style(th->wall, th->bg, 0);

    /* Terrain first, underneath the lines, so a boundary is never broken by
     * the ground it separates. */
    int tx0, ty0, tx1, ty1;
    grid_visible_tiles(g, m, &tx0, &ty0, &tx1, &ty1);

    for (int ty = ty0; ty <= ty1; ty++) {
        for (int tx = tx0; tx <= tx1; tx++) {
            uint8_t t = map_tile(m, tx, ty);
            if (t == TILE_VOID || t >= TILE_COUNT) continue;

            uint32_t glyph = ascii ? TERRAIN_LOOK[t].ascii : TERRAIN_LOOK[t].glyph;
            uint32_t bg    = th->terrain_bg[t];
            if (glyph == ' ' && bg == th->bg) continue;   /* nothing to show */

            Style ts = style(th->terrain_fg[t], bg, 0);
            int   sx, sy;
            grid_tile_interior(g, tx, ty, &sx, &sy);
            for (int j = 0; j < ih; j++)
                for (int i = 0; i < iw; i++)
                    draw_cell(r, sx + i, sy + j, glyph, ts);
        }
    }

    /* Only the lattice corners that can land inside the viewport. On a
     * 512x512 map that is the difference between 263k iterations and a few
     * hundred. */
    int cx0 = iclamp(fdiv(g->cam_x, pw) - 1, 0, m->w);
    int cx1 = iclamp(fdiv(g->cam_x + g->view.w, pw) + 1, 0, m->w);
    int cy0 = iclamp(fdiv(g->cam_y, ph) - 1, 0, m->h);
    int cy1 = iclamp(fdiv(g->cam_y + g->view.h, ph) + 1, 0, m->h);

    for (int cy = cy0; cy <= cy1; cy++) {
        /* The segment west of a corner is the one east of its neighbour, so
         * carrying it across the row halves the horizontal lookups. */
        Seg w = hseg(m, cx0 - 1, cy);

        for (int cx = cx0; cx <= cx1; cx++) {
            int sx, sy;
            grid_tile_screen(g, cx, cy, &sx, &sy);

            Seg n = vseg(m, cx, cy - 1);
            Seg sg = vseg(m, cx, cy);
            Seg e = hseg(m, cx, cy);

            unsigned any = 0, solid = 0;
            if (n.level)  { any |= JN; if (n.level == 2) solid |= JN; }
            if (e.level)  { any |= JE; if (e.level == 2) solid |= JE; }
            if (sg.level) { any |= JS; if (sg.level == 2) solid |= JS; }
            if (w.level)  { any |= JW; if (w.level == 2) solid |= JW; }

            /* A junction between two panels of the same kind takes their
             * colour, so a run of doors reads as one door rather than as
             * panels stitched together by wall. Mixed junctions stay wall,
             * which is what a doorway in a wall should look like. */
            Style s_junc = s_wall;
            if (solid) {
                uint8_t k0 = 0;
                int     uniform = 1;
                const Seg *segs[4] = { &n, &e, &sg, &w };
                for (int i = 0; i < 4; i++) {
                    if (segs[i]->level != 2) continue;
                    if (!k0) k0 = segs[i]->kind;
                    else if (segs[i]->kind != k0) uniform = 0;
                }
                if (uniform && k0 && k0 != EDGE_WALL) {
                    uint32_t g_unused, fg;
                    seg_look(k0, 0, ascii, reveal, th, &g_unused, &fg);
                    s_junc = style(fg, th->bg, 0);
                }
            }

            /* Where a solid boundary meets a thin one, the junction belongs
             * to the solid alone: a room corner must read as a corner, not as
             * a cross with two faint arms. That is also what puts the jambs
             * either side of an open doorway. */
            if (solid)    draw_cell(r, sx, sy, heavy[solid], s_junc);
            else if (any) draw_cell(r, sx, sy, light[any], s_grid);

            /* The run of cells east of this corner, and south of it. */
            if (e.level) {
                uint32_t glyph, fg;
                seg_look(e.kind, 0, ascii, reveal, th, &glyph, &fg);
                draw_hline(r, sx + 1, sy, iw, glyph, style(fg, th->bg, 0));
                if (reveal && e.kind == EDGE_SECRET_CLOSED)
                    draw_cell(r, sx + 1 + iw / 2, sy, ascii ? 'S' : 0x2573u,
                              style(th->edge_secret, th->bg, ATTR_BOLD));
            }
            if (sg.level) {
                uint32_t glyph, fg;
                seg_look(sg.kind, 1, ascii, reveal, th, &glyph, &fg);
                draw_vline(r, sx, sy + 1, ih, glyph, style(fg, th->bg, 0));
                if (reveal && sg.kind == EDGE_SECRET_CLOSED)
                    draw_cell(r, sx, sy + 1 + ih / 2, ascii ? 'S' : 0x2573u,
                              style(th->edge_secret, th->bg, ATTR_BOLD));
            }

            w = e;
        }
    }
}

void grid_draw_tile_cursor(Renderer *r, const GridView *g, int tx, int ty, uint32_t bg)
{
    int sx, sy;
    grid_tile_interior(g, tx, ty, &sx, &sy);

    for (int y = 0; y < ZOOM[g->zoom].ih; y++) {
        for (int x = 0; x < ZOOM[g->zoom].iw; x++) {
            Cell *c = rnd_at(r, sx + x, sy + y);
            if (c) c->bg = bg;
        }
    }
}

void grid_draw_tile_region(Renderer *r, const GridView *g, int x0, int y0,
                           int x1, int y1, uint32_t bg)
{
    if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
    if (y0 > y1) { int t = y0; y0 = y1; y1 = t; }
    for (int ty = y0; ty <= y1; ty++)
        for (int tx = x0; tx <= x1; tx++)
            grid_draw_tile_cursor(r, g, tx, ty, bg);
}

void grid_draw_tile_marker(Renderer *r, const GridView *g, int tx, int ty,
                           uint32_t fg)
{
    int sx, sy;
    grid_tile_screen(g, tx, ty, &sx, &sy);
    int pw = zoom_pw(g->zoom), ph = zoom_ph(g->zoom);

    const int cx[4] = { sx, sx + pw, sx, sx + pw };
    const int cy[4] = { sy, sy, sy + ph, sy + ph };

    for (int i = 0; i < 4; i++) {
        Cell *c = rnd_at(r, cx[i], cy[i]);
        if (!c) continue;
        c->fg    = fg;
        c->attr |= ATTR_BOLD;
    }
}

void grid_draw_corner_cursor(Renderer *r, const GridView *g, int cx, int cy,
                             const Theme *th, int pen_down)
{
    int sx, sy;
    grid_tile_screen(g, cx, cy, &sx, &sy);

    Cell *c = rnd_at(r, sx, sy);
    if (!c) return;

    /* Pen down is what distinguishes "moving to a start point" from "laying
     * wall as I go", so it has to be unmistakable at a glance. */
    c->ch   = pen_down ? 0x25CFu : 0x25CBu;    /* ● filled / ○ hollow */
    c->fg   = pen_down ? th->warn : th->accent;
    c->bg   = th->cursor_bg;
    c->attr = ATTR_BOLD;
}

void grid_draw_labels(Renderer *r, const Map *m, const GridView *g,
                      const Theme *th, int gutter, int cx, int cy)
{
    PROF_ZONE("grid.labels");

    if (gutter <= 0) return;

    int x0, y0, x1, y1;
    grid_visible_tiles(g, m, &x0, &y0, &x1, &y1);

    Style dim = style(th->dim, th->bg, 0);
    Style hot = style(th->accent, th->bg, ATTR_BOLD);

    /* Whichever column and row the cursor is on gets the bright label, so
     * finding where you are is a glance rather than a count. */

    int pw = zoom_pw(g->zoom), ph = zoom_ph(g->zoom);
    int iw = ZOOM[g->zoom].iw;

    /* One label per column only when one fits. Otherwise every second or
     * third, the way an axis thins its ticks rather than overprinting. */
    int widest = 1;
    for (int tx = x0; tx <= x1; tx++) {
        char lbl[MAP_COORD_MAX];
        map_coord_name(tx, 0, lbl, sizeof lbl);
        int w = (int)strlen(lbl) - 1;              /* drop the row digit */
        if (w > widest) widest = w;
    }
    int step = 1;
    while (widest + 1 > step * pw) step++;

    int labelrow = g->view.y - 1;
    ClipRect saved = rnd_clip_push(r, g->view.x, labelrow, g->view.w, 1);

    for (int tx = x0; tx <= x1; tx++) {
        if (tx % step) continue;

        char full[MAP_COORD_MAX], lbl[MAP_COORD_MAX];
        map_coord_name(tx, 0, full, sizeof full);
        size_t n = strlen(full) - 1;               /* the row digit is not wanted */
        memcpy(lbl, full, n);
        lbl[n] = '\0';

        int sx, sy;
        grid_tile_interior(g, tx, 0, &sx, &sy);
        draw_text(r, sx + (iw - (int)n + 1) / 2, labelrow, lbl, -1,
                  tx == cx ? hot : dim);
    }
    rnd_clip_restore(r, saved);

    /* Row numbers hug the grid rather than the edge of the screen: a map
     * narrower than the window is centred, and a column of numbers stranded
     * out on the left belongs to nothing the eye can see. */
    int gx, gy;
    grid_tile_interior(g, x0, y0, &gx, &gy);
    int right = imax(gutter - 2, gx - 3);   /* a space clear of the grid */

    /* Rows are a line tall at every zoom, so they never have to be thinned. */
    saved = rnd_clip_push(r, 0, g->view.y, right + 1, g->view.h);
    for (int ty = y0; ty <= y1; ty++) {
        char num[MAP_COORD_MAX];
        snprintf(num, sizeof num, "%d", ty + 1);

        int sx, sy;
        grid_tile_interior(g, x0, ty, &sx, &sy);
        int w = (int)strlen(num);
        draw_text(r, right - w + 1, sy + (ZOOM[g->zoom].ih - 1) / 2, num, -1,
                  ty == cy ? hot : dim);
    }
    rnd_clip_restore(r, saved);

    (void)pw; (void)ph;
}
