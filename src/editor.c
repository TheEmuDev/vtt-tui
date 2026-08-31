#include "editor.h"

#include <stdio.h>
#include <string.h>

#include "prof.h"
#include "util.h"

void ed_init(Editor *e, const Map *m)
{
    memset(e, 0, sizeof *e);
    e->mode = ED_NORMAL;
    e->material = EDGE_WALL;
    e->terrain  = TILE_FLOOR;
    e->view.zoom = iclamp(m->zoom, 0, ZOOM_COUNT - 1);
    e->cx = m->w / 2;
    e->cy = m->h / 2;
}

void ed_layout(Editor *e, const Map *m, int screen_w, int screen_h)
{
    /* Title row on top, status and keybinding rows at the bottom; the map
     * gets everything between. */
    e->view.view = rect(0, 1, screen_w, imax(1, screen_h - 3));
    grid_clamp_camera(&e->view, m);
    grid_ensure_visible(&e->view, m, e->cx, e->cy, ED_SCROLLOFF);
}

void ed_cursor_tile(const Editor *e, int *tx, int *ty)
{
    if (e->mode == ED_WALL) { *tx = e->wx; *ty = e->wy; }
    else                    { *tx = e->cx; *ty = e->cy; }
}

void ed_move(Editor *e, const Map *m, int dx, int dy, int times)
{
    if (times < 1) times = 1;

    if (e->mode == ED_WALL) {
        /* Corner-to-corner movement. Each step crosses exactly one edge, and
         * with the pen down that edge becomes (or stops being) a wall - so a
         * room is drawn by walking its outline. */
        for (int i = 0; i < times; i++) {
            int nx = iclamp(e->wx + dx, 0, m->w);
            int ny = iclamp(e->wy + dy, 0, m->h);
            if (nx == e->wx && ny == e->wy) break;
            e->wx = nx;
            e->wy = ny;
        }
        grid_ensure_visible(&e->view, m, iclamp(e->wx, 0, m->w - 1),
                            iclamp(e->wy, 0, m->h - 1), ED_SCROLLOFF);
        return;
    }

    e->cx = iclamp(e->cx + dx * times, 0, m->w - 1);
    e->cy = iclamp(e->cy + dy * times, 0, m->h - 1);
    grid_ensure_visible(&e->view, m, e->cx, e->cy, ED_SCROLLOFF);
}

void ed_set_zoom(Editor *e, const Map *m, int zoom)
{
    int tx, ty;
    ed_cursor_tile(e, &tx, &ty);
    grid_set_zoom(&e->view, m, zoom, iclamp(tx, 0, m->w - 1), iclamp(ty, 0, m->h - 1));
    grid_ensure_visible(&e->view, m, iclamp(tx, 0, m->w - 1),
                        iclamp(ty, 0, m->h - 1), ED_SCROLLOFF);
}

/* --------------------------------------------------------------- editing */

void ed_cycle_material(Editor *e)
{
    /* EDGE_NONE is the eraser, which the tools already have; the selector
     * only walks the things you can lay. */
    e->material = (uint8_t)(e->material + 1);
    if (e->material >= EDGE_COUNT) e->material = EDGE_WALL;
}

void ed_cycle_terrain(Editor *e)
{
    e->terrain = (uint8_t)(e->terrain + 1);
    if (e->terrain >= TILE_COUNT) e->terrain = TILE_FLOOR;
}

void ed_toggle_edge(Editor *e, Map *m, Undo *u, int dx, int dy)
{
    int     x = e->cx, y = e->cy;
    uint8_t cur, want = e->material;

    /* Pressing the same face twice takes it away again, so one key both
     * places and removes. */
    if (dx > 0)      cur = map_vedge(m, x + 1, y);
    else if (dx < 0) cur = map_vedge(m, x, y);
    else if (dy > 0) cur = map_hedge(m, x, y + 1);
    else             cur = map_hedge(m, x, y);

    uint8_t next = (cur == want) ? (uint8_t)EDGE_NONE : want;

    undo_begin(u);
    if (dx > 0)      undo_set_vedge(u, m, x + 1, y, next);
    else if (dx < 0) undo_set_vedge(u, m, x, y, next);
    else if (dy > 0) undo_set_hedge(u, m, x, y + 1, next);
    else             undo_set_hedge(u, m, x, y, next);
    undo_end(u);
}

int ed_toggle_doors(Editor *e, Map *m, Undo *u, int secret)
{
    int x = e->cx, y = e->cy;
    struct { int v, ex, ey; } faces[4] = {
        { 1, x, y }, { 1, x + 1, y },        /* west and east */
        { 0, x, y }, { 0, x, y + 1 },        /* north and south */
    };

    int n = 0;
    undo_begin(u);
    for (int i = 0; i < 4; i++) {
        uint8_t cur = faces[i].v ? map_vedge(m, faces[i].ex, faces[i].ey)
                                 : map_hedge(m, faces[i].ex, faces[i].ey);
        if (!edge_is_door(cur)) continue;

        int is_secret = (cur == EDGE_SECRET_CLOSED || cur == EDGE_SECRET_OPEN);
        if (is_secret != (secret != 0)) continue;

        uint8_t next = edge_toggled(cur);
        if (faces[i].v) undo_set_vedge(u, m, faces[i].ex, faces[i].ey, next);
        else            undo_set_hedge(u, m, faces[i].ex, faces[i].ey, next);
        n++;
    }
    undo_end(u);
    return n;
}

void ed_wall_step(Editor *e, Map *m, Undo *u, int dx, int dy, int times)
{
    if (times < 1) times = 1;

    /* The batch is opened here but deliberately not closed: a pen-down
     * stroke stays one undo step until the pen lifts, so `u` takes back the
     * whole run you just drew rather than one segment of it. */
    if (e->pen) undo_begin(u);

    uint8_t kind = e->erase ? (uint8_t)EDGE_NONE : e->material;

    for (int i = 0; i < times; i++) {
        int nx = iclamp(e->wx + dx, 0, m->w);
        int ny = iclamp(e->wy + dy, 0, m->h);
        if (nx == e->wx && ny == e->wy) break;

        if (e->pen) {
            /* The edge crossed by this step is the segment between the two
             * corners: horizontal for a sideways step, vertical for a
             * vertical one. */
            if (dx > 0)      undo_set_hedge(u, m, e->wx, e->wy, kind);
            else if (dx < 0) undo_set_hedge(u, m, nx, e->wy, kind);
            else if (dy > 0) undo_set_vedge(u, m, e->wx, e->wy, kind);
            else             undo_set_vedge(u, m, e->wx, ny, kind);
        }
        e->wx = nx;
        e->wy = ny;
    }

    grid_ensure_visible(&e->view, m, iclamp(e->wx, 0, m->w - 1),
                        iclamp(e->wy, 0, m->h - 1), ED_SCROLLOFF);
}

/* ---------------------------------------------------------------- shapes */

EdShape ed_shape(uint8_t kind, int ax, int ay, int bx, int by, int corners)
{
    EdShape s;
    memset(&s, 0, sizeof s);
    s.kind = kind;

    if (kind != ED_SHAPE_CIRCLE) {
        s.x0 = imin(ax, bx); s.x1 = imax(ax, bx);
        s.y0 = imin(ay, by); s.y1 = imax(ay, by);
        /* Between two corners, not including both: corner 2 to corner 5 spans
         * the three tiles they enclose, not four. */
        if (corners) { s.x1--; s.y1--; }
        return s;
    }

    /* Half-tiles throughout: a square's middle is odd, a lattice corner even,
     * so both anchors and the tile centres they are measured against live in
     * the same integers. */
    s.ccx = corners ? 2L * ax : 2L * ax + 1;
    s.ccy = corners ? 2L * ay : 2L * ay + 1;

    long ex = (corners ? 2L * bx : 2L * bx + 1) - s.ccx;
    long ey = (corners ? 2L * by : 2L * by + 1) - s.ccy;
    s.cr2 = ex * ex + ey * ey;

    /* The box only bounds the walk; ed_shape_has is what decides, so a tile
     * of slack costs a few tests and saves getting the rounding exactly
     * right in two places. */
    long rh = 0;
    while ((rh + 1) * (rh + 1) <= s.cr2) rh++;

    int rt = (int)(rh / 2) + 1;
    int cx = (int)(s.ccx / 2), cy = (int)(s.ccy / 2);
    s.x0 = cx - rt; s.x1 = cx + rt;
    s.y0 = cy - rt; s.y1 = cy + rt;
    return s;
}

int ed_shape_has(const EdShape *s, int x, int y)
{
    if (x < s->x0 || x > s->x1 || y < s->y0 || y > s->y1) return 0;
    if (s->kind != ED_SHAPE_CIRCLE) return 1;

    long dx = (2L * x + 1) - s->ccx;
    long dy = (2L * y + 1) - s->ccy;
    return dx * dx + dy * dy <= s->cr2;
}

int ed_shape_radius(const EdShape *s)
{
    if (s->kind != ED_SHAPE_CIRCLE) return 0;

    long rh = 0;
    while ((rh + 1) * (rh + 1) <= s->cr2) rh++;
    return (int)(rh / 2);
}

void ed_wall_shape(Map *m, Undo *u, const EdShape *s, uint8_t kind)
{
    undo_begin(u);
    for (int y = s->y0; y <= s->y1; y++) {
        for (int x = s->x0; x <= s->x1; x++) {
            if (!ed_shape_has(s, x, y)) continue;

            /* Every face this tile shares with one the shape does not cover.
             * Walking the region rather than its corners is what lets a
             * circle be walled at all, and it gives a rectangle exactly the
             * outline it had before. */
            if (!ed_shape_has(s, x - 1, y)) undo_set_vedge(u, m, x,     y,     kind);
            if (!ed_shape_has(s, x + 1, y)) undo_set_vedge(u, m, x + 1, y,     kind);
            if (!ed_shape_has(s, x, y - 1)) undo_set_hedge(u, m, x,     y,     kind);
            if (!ed_shape_has(s, x, y + 1)) undo_set_hedge(u, m, x,     y + 1, kind);
        }
    }
    undo_end(u);
}

void ed_apply_tiles(Editor *e, Map *m, Undo *u, uint8_t kind)
{
    EdShape s = ed_shape(ED_SHAPE_RECT, e->cx, e->cy, e->cx, e->cy, 0);
    if (e->mode == ED_VISUAL)
        s = ed_shape(e->shape, e->anchor_x, e->anchor_y, e->cx, e->cy, 0);

    undo_begin(u);
    for (int y = s.y0; y <= s.y1; y++)
        for (int x = s.x0; x <= s.x1; x++)
            if (ed_shape_has(&s, x, y)) undo_set_tile(u, m, x, y, kind);
    undo_end(u);
}

void ed_toggle_tile(Editor *e, Map *m, Undo *u)
{
    uint8_t next = map_tile(m, e->cx, e->cy) == TILE_VOID ? e->terrain
                                                          : (uint8_t)TILE_VOID;
    ed_apply_tiles(e, m, u, next);
}

const char *ed_mode_name(EdMode m)
{
    switch (m) {
    case ED_WALL:    return "WALL";
    case ED_VISUAL:  return "VISUAL";
    case ED_COMMAND: return "COMMAND";
    case ED_NORMAL:
    default:         return "NORMAL";
    }
}

/* Tints the tiles a shape covers. Clipped to what is on screen first, so a
 * circle with a big radius costs the window rather than the radius. */
static void draw_shape(Renderer *r, const Map *m, const Editor *e,
                       const EdShape *s, uint32_t bg)
{
    int vx0, vy0, vx1, vy1;
    grid_visible_tiles(&e->view, m, &vx0, &vy0, &vx1, &vy1);

    int x0 = imax(s->x0, vx0), x1 = imin(s->x1, vx1);
    int y0 = imax(s->y0, vy0), y1 = imin(s->y1, vy1);

    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++)
            if (ed_shape_has(s, x, y))
                grid_draw_tile_cursor(r, &e->view, x, y, bg);
}

void ed_draw(Renderer *r, const Map *m, const Editor *e, const Theme *th, int ascii)
{
    PROF_ZONE("editor.draw");

    /* The map scrolls; the chrome around it must not. Everything the grid
     * paints is confined to the viewport rectangle. */
    ClipRect saved = rnd_clip_push(r, e->view.view.x, e->view.view.y,
                                   e->view.view.w, e->view.view.h);

    grid_draw(r, m, &e->view, th, ascii, 1);   /* build mode sees secrets */

    if (e->mode == ED_VISUAL) {
        EdShape s = ed_shape(e->shape, e->anchor_x, e->anchor_y, e->cx, e->cy, 0);
        draw_shape(r, m, e, &s, th->sel_bg);
    }

    if (e->mode == ED_WALL) {
        /* Show the ground Enter would wall around, so the anchor is not an
         * invisible piece of state. */
        if (e->has_anchor) {
            EdShape s = ed_shape(e->shape, e->ax, e->ay, e->wx, e->wy, 1);
            draw_shape(r, m, e, &s, th->sel_bg);
            grid_draw_corner_cursor(r, &e->view, e->ax, e->ay, th, 0);
        }
        grid_draw_corner_cursor(r, &e->view, e->wx, e->wy, th, e->pen);
    }
    else
        grid_draw_tile_cursor(r, &e->view, e->cx, e->cy, th->cursor_bg);

    rnd_clip_restore(r, saved);
}

/* What the live anchor is drawing. A circle's radius is worth saying: a room
 * "four across" is the thing being aimed at, and counting tinted squares off
 * the screen to find it is no way to build a map. */
static void shape_note(const Editor *e, char *buf, size_t bufsz)
{
    buf[0] = '\0';

    int live = (e->mode == ED_VISUAL) || (e->mode == ED_WALL && e->has_anchor);
    if (!live) return;

    if (e->shape != ED_SHAPE_CIRCLE) { str_lcpy(buf, "  box", bufsz); return; }

    EdShape s = (e->mode == ED_WALL)
              ? ed_shape(ED_SHAPE_CIRCLE, e->ax, e->ay, e->wx, e->wy, 1)
              : ed_shape(ED_SHAPE_CIRCLE, e->anchor_x, e->anchor_y, e->cx, e->cy, 0);
    snprintf(buf, bufsz, "  circle r%d", ed_shape_radius(&s));
}

void ed_status(const Editor *e, const Map *m, char *buf, size_t bufsz)
{
    const ZoomLevel *z = &ZOOM[e->view.zoom];

    char shape[24];
    shape_note(e, shape, sizeof shape);

    if (e->mode == ED_WALL) {
        snprintf(buf, bufsz, "%-7s corner %d,%d  %s  [%s]%s  zoom %d  map %dx%d",
                 ed_mode_name(e->mode), e->wx, e->wy,
                 e->erase ? "ERASE" : (e->pen ? "PEN DOWN" : "pen up"),
                 edge_name(e->material), shape, e->view.zoom, m->w, m->h);
        return;
    }

    snprintf(buf, bufsz, "%-7s tile %d,%d  %s  [%s/%s]%s  zoom %d  map %dx%d",
             ed_mode_name(e->mode), e->cx, e->cy,
             tile_name(map_tile(m, e->cx, e->cy)),
             edge_name(e->material), tile_name(e->terrain), shape,
             e->view.zoom, m->w, m->h);
    (void)z;
}
