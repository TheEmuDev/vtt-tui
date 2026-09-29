/* Corridors between two named rooms (the room language's `corridor A B`):
 * where one runs -- straight when the rooms share enough rows or columns,
 * else one bend, across first, then down first -- whether it is clear, and
 * digging it through the undo log. */

#include "corridor.h"

#include <stdio.h>
#include <string.h>

#include "util.h"


static int area_holds(const Area *outer, const Area *in)
{
    return in->x0 >= outer->x0 && in->x1 <= outer->x1 && in->y0 >= outer->y0 && in->y1 <= outer->y1;
}

static int in_box(const Box *b, int x, int y) { return x >= b->x0 && x <= b->x1 && y >= b->y0 && y <= b->y1; }

static int in_legs(const Corridor *c, int x, int y)
{
    for (int i = 0; i < c->nleg; i++)
        if (in_box(&c->leg[i], x, y)) return 1;
    return 0;
}

static void add_end(Corridor *c, int vert, int x, int y)
{
    for (int i = 0; i < c->nend; i++)
        if (c->end[i].vert == vert && c->end[i].x == x && c->end[i].y == y) return;
    if (c->nend < CORR_ENDS) c->end[c->nend++] = (Face){ vert, x, y };
}

static void add_leg(Corridor *c, int x0, int y0, int x1, int y1)
{
    if (x1 < x0 || y1 < y0) return;            /* rooms already touching: no leg */
    c->leg[c->nleg++] = (Box){ x0, y0, x1, y1 };
}

/* The face of one room toward the other along a line of `w` squares from
 * (x, y) running down (vertical faces) or across (horizontal ones). */
static void add_ends(Corridor *c, int vert, int x, int y, int w)
{
    for (int i = 0; i < w; i++) add_end(c, vert, vert ? x : x + i, vert ? y + i : y);
}

/* A straight run between rooms side by side (horizontal) or one above the
 * other, centered on the stretch they share; 0 when that is narrower than
 * the corridor. */
static int straight(const Area *a, const Area *b, int w, int horiz, Corridor *c)
{
    if (horiz) {
        int lo = imax(a->y0, b->y0), hi = imin(a->y1, b->y1);
        if (hi - lo + 1 < w) return 0;
        int y = lo + (hi - lo + 1 - w) / 2;
        const Area *l = a->x1 < b->x0 ? a : b, *r = l == a ? b : a;
        add_leg(c, l->x1 + 1, y, r->x0 - 1, y + w - 1);
        add_ends(c, 1, l->x1 + 1, y, w);
        add_ends(c, 1, r->x0, y, w);
    } else {
        int lo = imax(a->x0, b->x0), hi = imin(a->x1, b->x1);
        if (hi - lo + 1 < w) return 0;
        int x = lo + (hi - lo + 1 - w) / 2;
        const Area *t = a->y1 < b->y0 ? a : b, *d = t == a ? b : a;
        add_leg(c, x, t->y1 + 1, x + w - 1, d->y0 - 1);
        add_ends(c, 0, x, t->y1 + 1, w);
        add_ends(c, 0, x, d->y0, w);
    }
    return 1;
}

/* One bend for rooms apart on both axes: out of a's side at its middle,
 * along to the middle of b, and into b. `across_first` says which side of
 * a it leaves by: its east or west, else its north or south. */
static int bent(const Area *a, const Area *b, int w, int across_first, Corridor *c)
{
    /* The side it leaves and the side it enters must each be as long as
     * the corridor is wide, or its mouth would open onto what is beside
     * the room. */
    int aw = a->x1 - a->x0 + 1, ah = a->y1 - a->y0 + 1, bw = b->x1 - b->x0 + 1, bh = b->y1 - b->y0 + 1;
    if (across_first ? (ah < w || bw < w) : (aw < w || bh < w)) return 0;
    if (across_first) {
        int hy = a->y0 + (a->y1 - a->y0 + 1 - w) / 2;          /* rows of the first leg */
        int vx = b->x0 + (b->x1 - b->x0 + 1 - w) / 2;          /* columns of the second */
        int east = b->x0 > a->x1, down = b->y0 > a->y1;
        if (east) add_leg(c, a->x1 + 1, hy, vx + w - 1, hy + w - 1);
        else      add_leg(c, vx, hy, a->x0 - 1, hy + w - 1);
        if (down) add_leg(c, vx, hy + w, vx + w - 1, b->y0 - 1);
        else      add_leg(c, vx, b->y1 + 1, vx + w - 1, hy - 1);
        add_ends(c, 1, east ? a->x1 + 1 : a->x0, hy, w);
        add_ends(c, 0, vx, down ? b->y0 : b->y1 + 1, w);
    } else {
        int vx = a->x0 + (a->x1 - a->x0 + 1 - w) / 2;
        int hy = b->y0 + (b->y1 - b->y0 + 1 - w) / 2;
        int east = b->x0 > a->x1, down = b->y0 > a->y1;
        if (down) add_leg(c, vx, a->y1 + 1, vx + w - 1, hy + w - 1);
        else      add_leg(c, vx, hy, vx + w - 1, a->y0 - 1);
        if (east) add_leg(c, vx + w, hy, b->x0 - 1, hy + w - 1);
        else      add_leg(c, b->x1 + 1, hy, vx - 1, hy + w - 1);
        add_ends(c, 0, vx, down ? a->y1 + 1 : a->y0, w);
        add_ends(c, 1, east ? b->x0 : b->x1 + 1, hy, w);
    }
    return 1;
}

/* Every square of the corridor is void and in no named area: a corridor
 * is dug through nothing, never through a room or ground already there. */
static int clear_of(const Map *m, const Corridor *c, char *err, size_t errsz)
{
    for (int i = 0; i < c->nleg; i++)
        for (int y = c->leg[i].y0; y <= c->leg[i].y1; y++)
            for (int x = c->leg[i].x0; x <= c->leg[i].x1; x++) {
                char at[MAP_COORD_MAX];
                map_coord_name(x, y, at, sizeof at);
                if (!map_in_bounds(m, x, y)) { snprintf(err, errsz, "it would run off the map"); return 0; }
                /* An area holding both rooms whole (a floor, a district) is
                 * the corridor's too; any other is a room in the way. */
                for (int ai = 0; ai < m->nareas; ai++) {
                    const Area *ar = &m->areas[ai];
                    if (x < ar->x0 || x > ar->x1 || y < ar->y0 || y > ar->y1) continue;
                    if (area_holds(ar, c->a) && area_holds(ar, c->b)) continue;
                    snprintf(err, errsz, "it would cut through %.30s at %s", ar->name, at);
                    return 0;
                }
                if (map_walkable(m, x, y)) { snprintf(err, errsz, "it would cross ground already at %s", at); return 0; }
            }
    return 1;
}

void corridor_dig(Map *m, Undo *u, const Corridor *c, uint8_t end_kind)
{
    for (int i = 0; i < c->nleg; i++)
        for (int y = c->leg[i].y0; y <= c->leg[i].y1; y++)
            for (int x = c->leg[i].x0; x <= c->leg[i].x1; x++) {
                undo_set_tile(u, m, x, y, TILE_FLOOR);
                /* Each face out of the corridor is walled where it has
                 * nothing: a door or window already there -- a room the
                 * corridor runs past -- stays, and now opens onto it. */
                if (!in_legs(c, x - 1, y) && map_vedge(m, x, y) == EDGE_NONE)     undo_set_vedge(u, m, x,     y, EDGE_WALL);
                if (!in_legs(c, x + 1, y) && map_vedge(m, x + 1, y) == EDGE_NONE) undo_set_vedge(u, m, x + 1, y, EDGE_WALL);
                if (!in_legs(c, x, y - 1) && map_hedge(m, x, y) == EDGE_NONE)     undo_set_hedge(u, m, x, y,     EDGE_WALL);
                if (!in_legs(c, x, y + 1) && map_hedge(m, x, y + 1) == EDGE_NONE) undo_set_hedge(u, m, x, y + 1, EDGE_WALL);
            }
    for (int i = 0; i < c->nend; i++) {
        if (c->end[i].vert) undo_set_vedge(u, m, c->end[i].x, c->end[i].y, end_kind);
        else                undo_set_hedge(u, m, c->end[i].x, c->end[i].y, end_kind);
    }
}

#define BADP(...) do { snprintf(err, errsz, __VA_ARGS__); return -1; } while (0)

int corridor_plan(const Map *m, const Area *ra, const Area *rb, int width, Corridor *c,
                  char *err, size_t errsz)
{
    int horiz = ra->x1 < rb->x0 || rb->x1 < ra->x0, vert = ra->y1 < rb->y0 || rb->y1 < ra->y0;
    if (!horiz && !vert) BADP("%.30s and %.30s overlap", ra->name, rb->name);
    memset(c, 0, sizeof *c);
    c->a = ra; c->b = rb;
    if (horiz != vert) {
        if (!straight(ra, rb, width, horiz, c))
            BADP("%.30s and %.30s share fewer than %d %s: no straight corridor fits", ra->name, rb->name,
                 width, horiz ? "rows" : "columns");
        return clear_of(m, c, err, errsz) ? 0 : -1;
    }
    /* Across first, then down first; the first way's reason if neither
     * will do. */
    Corridor c2;
    memset(&c2, 0, sizeof c2);
    c2.a = ra; c2.b = rb;
    int ok1 = bent(ra, rb, width, 1, c), ok2 = bent(ra, rb, width, 0, &c2);
    if (!ok1 && !ok2)
        BADP("%.30s and %.30s are too narrow for a bend %d wide", ra->name, rb->name, width);
    if (ok1 && clear_of(m, c, err, errsz)) return 0;
    char e2[200];
    if (!ok2 || !clear_of(m, &c2, e2, sizeof e2)) {
        /* The first way's reason, unless there was no first way. */
        if (!ok1) str_lcpy(err, e2, errsz);
        return -1;
    }
    *c = c2;
    return 0;
}

