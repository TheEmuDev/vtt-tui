/* The channel's `marked` read: what the GM is pointing at -- the cursor,
 * a v box (rect or circle), wall mode's corner, the ruler, pings --
 * gathered once and written as text or JSON, so the two cannot disagree. */

#include <stdio.h>
#include <string.h>

#include "app_priv.h"
#include "json.h"
#include "util.h"

static void j_region(Json *j, const char *key, int x0, int y0, int x1, int y1)
{
    char r[2 * MAP_COORD_MAX + 2];
    map_region_name(x0, y0, x1, y1, r, sizeof r);
    json_key(j, key);
    json_open(j, '{');
    json_kstr(j, "region", r);
    json_kint(j, "x0", x0); json_kint(j, "y0", y0);
    json_kint(j, "x1", x1); json_kint(j, "y1", y1);
    json_close(j, '}');
}

/* Everything the GM is pointing at, gathered once and then written as
 * text or JSON, so the two cannot disagree. */
typedef struct {
    int      cx0, cy0, cx1, cy1;     /* the cursor, a brush's whole footprint */
    int      corner, wx, wy;         /* wall mode: the lattice corner */
    int      box;                    /* 0 none, else ED_SHAPE_RECT + 1 or ED_SHAPE_CIRCLE + 1 */
    int      bx0, by0, bx1, by1;     /* the box's squares, or the disc's bounding box */
    int      ox, oy, radius;         /* a circle's center square and radius */
    int      box_corners;            /* the box is wall mode's, between corners */
    int      box_empty;              /* it holds no squares: a straight line of corners */
    const Ruler *ruler;              /* NULL when not measuring */
} Marked;

/* A lattice corner by the square it touches: the top left of one, or on
 * the map's east or south edge the top right, bottom left or bottom right
 * of the last. */
static void corner_name(const Map *m, int wx, int wy, char *buf, size_t sz)
{
    char at[MAP_COORD_MAX];
    int  east = wx >= m->w, south = wy >= m->h;
    map_coord_name(east ? m->w - 1 : wx, south ? m->h - 1 : wy, at, sizeof at);
    snprintf(buf, sz, "%s of %s", east && south ? "bottom right" : east ? "top right"
                                  : south ? "bottom left" : "top left", at);
}

/* The squares a shape really takes: a circle's own box is a bound, not
 * the squares inside it. Clipped to the map. */
static void shape_squares(const Map *m, const EdShape *sh, Marked *mk)
{
    int x0 = imax(sh->x0, 0), y0 = imax(sh->y0, 0);
    int x1 = imin(sh->x1, m->w - 1), y1 = imin(sh->y1, m->h - 1);
    mk->bx0 = x1; mk->by0 = y1; mk->bx1 = x0; mk->by1 = y0;
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) {
            if (!ed_shape_has(sh, x, y)) continue;
            mk->bx0 = imin(mk->bx0, x); mk->bx1 = imax(mk->bx1, x);
            mk->by0 = imin(mk->by0, y); mk->by1 = imax(mk->by1, y);
        }
    /* A wall-mode anchor on the cursor's own line holds no squares at all:
     * say so, rather than name one it does not hold. */
    mk->box_empty = mk->bx1 < mk->bx0 || mk->by1 < mk->by0;
}

static void gather_marked(const App *a, Marked *mk)
{
    const Map    *m = a->map;
    const Editor *e = &a->ed;
    memset(mk, 0, sizeof *mk);
    int b = a->screen == SCREEN_EDITOR ? e->brush : 1, tx, ty;
    ed_cursor_tile(e, &tx, &ty);
    mk->cx0 = imin(tx, m->w - 1); mk->cy0 = imin(ty, m->h - 1);
    mk->cx1 = imin(tx + b - 1, m->w - 1);
    mk->cy1 = imin(ty + b - 1, m->h - 1);

    if (a->screen == SCREEN_EDITOR && e->mode == ED_WALL) {
        mk->corner = 1;
        mk->wx = e->wx; mk->wy = e->wy;
        if (e->has_anchor) {
            EdShape sh = ed_shape(e->shape, e->ax, e->ay, e->wx, e->wy, 1);
            mk->box = sh.kind + 1;
            mk->box_corners = 1;
            shape_squares(m, &sh, mk);
            mk->radius = ed_shape_radius(&sh);
        }
    }
    else if (a->screen == SCREEN_EDITOR && e->mode == ED_VISUAL) {
        EdShape sh = ed_shape(e->shape, e->anchor_x, e->anchor_y, e->cx, e->cy, 0);
        mk->box = sh.kind + 1;
        shape_squares(m, &sh, mk);
        mk->ox = e->anchor_x; mk->oy = e->anchor_y;
        mk->radius = ed_shape_radius(&sh);
    }
    else if (a->screen == SCREEN_PLAY && a->play.visual) {
        mk->box = ED_SHAPE_RECT + 1;
        mk->bx0 = imin(a->play.anchor_x, e->cx); mk->bx1 = imax(a->play.anchor_x, e->cx);
        mk->by0 = imin(a->play.anchor_y, e->cy); mk->by1 = imax(a->play.anchor_y, e->cy);
    }
    mk->ruler = a->ruler.active ? &a->ruler : NULL;
}

static const char *ping_source(uint32_t who, char *buf, size_t sz)
{
    if (who == PING_GM) return "the GM";
    snprintf(buf, sz, "phone %u", who);
    return buf;
}

/* A record's until_ms is when it was made. */
static uint64_t ping_age_ms(const App *a, const Ping *p)
{
    return a->now_ms > p->until_ms ? a->now_ms - p->until_ms : 0;
}

void app_ctl_marked(App *a, FILE *out, int json)
{
    const Map *m = a->map;
    Marked mk;
    gather_marked(a, &mk);
    const char *screen = app_ctl_screen_name(a->screen);
    const char *mode   = a->screen == SCREEN_EDITOR ? app_ctl_mode_name(a->ed.mode) : NULL;
    char r[2 * MAP_COORD_MAX + 2], at[MAP_COORD_MAX];
    double ruler_ft = mk.ruler ? ruler_tiles(mk.ruler, (DistMetric)m->metric) * m->scale_ft : 0;
    char dist[32] = "";
    if (mk.ruler) dist_fmt(dist, sizeof dist, ruler_ft);

    if (json) {
        Json j;
        json_init(&j, out);
        json_open(&j, '{');
        json_kstr(&j, "screen", screen);
        json_key(&j, "mode");
        if (mode) json_str(&j, mode); else json_null(&j);
        j_region(&j, "cursor", mk.cx0, mk.cy0, mk.cx1, mk.cy1);
        {
            int ai = map_area_at(m, mk.cx0, mk.cy0);
            json_key(&j, "in");
            if (ai >= 0) json_str(&j, m->areas[ai].name); else json_null(&j);
            int fl = app_floor_shown(a);
            json_key(&j, "floor");                 /* the floor the GM is looking at */
            if (fl >= 0) json_str(&j, m->areas[fl].name); else json_null(&j);
        }
        json_key(&j, "corner");
        if (mk.corner) {
            json_open(&j, '{');
            json_kint(&j, "x", mk.wx);
            json_kint(&j, "y", mk.wy);
            char cn[48];
            corner_name(m, mk.wx, mk.wy, cn, sizeof cn);
            json_kstr(&j, "at", cn);
            json_close(&j, '}');
        } else json_null(&j);
        json_key(&j, "box");
        if (mk.box) {
            json_open(&j, '{');
            json_kstr(&j, "shape", mk.box == ED_SHAPE_CIRCLE + 1 ? "circle" : "rect");
            json_key(&j, "between");
            json_str(&j, mk.box_corners ? "corners" : "squares");
            if (mk.box_empty) { json_key(&j, "squares"); json_null(&j); }
            else j_region(&j, "squares", mk.bx0, mk.by0, mk.bx1, mk.by1);
            if (mk.box == ED_SHAPE_CIRCLE + 1) {
                json_kint(&j, "radius", mk.radius);
                if (!mk.box_corners) {
                    map_coord_name(mk.ox, mk.oy, at, sizeof at);
                    json_kstr(&j, "center", at);
                }
            }
            json_close(&j, '}');
        } else json_null(&j);
        json_key(&j, "selected");
        json_open(&j, '[');
        if (a->screen == SCREEN_PLAY)
            for (int i = 0; i < a->play.ngroup; i++) {
                const Token *t = &m->tokens.v[a->play.group[i]];
                json_open(&j, '{');
                json_kstr(&j, "label", t->label);
                json_kstr(&j, "kind", token_kind_name(t->kind));
                j_region(&j, "at", t->x, t->y, t->x + t->size - 1, t->y + t->size - 1);
                if (t->hidden) { json_key(&j, "hidden"); json_bool(&j, 1); }
                json_close(&j, '}');
            }
        json_close(&j, ']');
        json_key(&j, "ruler");
        if (mk.ruler) {
            json_open(&j, '{');
            json_key(&j, "points");
            json_open(&j, '[');
            for (int i = 0; i < mk.ruler->n; i++) {
                map_coord_name(mk.ruler->pts[i].x, mk.ruler->pts[i].y, at, sizeof at);
                json_str(&j, at);
            }
            json_close(&j, ']');
            map_coord_name(mk.ruler->cx, mk.ruler->cy, at, sizeof at);
            json_kstr(&j, "end", at);
            json_key(&j, "feet");
            json_num(&j, ruler_ft);
            json_close(&j, '}');
        } else json_null(&j);
        json_key(&j, "pings");
        json_open(&j, '[');
        for (int i = 0; i < a->npinged; i++) {
            const Ping *p = &a->pinged[i];
            json_open(&j, '{');
            json_kstr(&j, "by", p->who == PING_GM ? "gm" : "phone");
            json_key(&j, "phone");
            if (p->who == PING_GM) json_null(&j); else json_int(&j, (long)p->who);
            j_region(&j, "at", p->x0, p->y0, p->x1, p->y1);
            json_kint(&j, "seconds_ago", (long)(ping_age_ms(a, p) / 1000));
            json_close(&j, '}');
        }
        json_close(&j, ']');
        json_close(&j, '}');
        fputc('\n', out);
        return;
    }

    if (mode) fprintf(out, "screen %s, %s mode\n", screen, mode);
    else      fprintf(out, "screen %s\n", screen);
    map_region_name(mk.cx0, mk.cy0, mk.cx1, mk.cy1, r, sizeof r);
    int in_area = map_area_at(m, mk.cx0, mk.cy0);
    if (in_area >= 0) fprintf(out, "cursor %s, in %s\n", r, m->areas[in_area].name);
    else              fprintf(out, "cursor %s\n", r);
    if (app_floor_shown(a) >= 0) fprintf(out, "floor %s\n", m->areas[app_floor_shown(a)].name);
    if (mk.corner) {
        char cn[48];
        corner_name(m, mk.wx, mk.wy, cn, sizeof cn);
        fprintf(out, "corner at the %s\n", cn);
    }
    if (mk.box && mk.box_empty)
        fputs("box a line of corners, no squares inside it\n", out);
    else if (mk.box) {
        map_region_name(mk.bx0, mk.by0, mk.bx1, mk.by1, r, sizeof r);
        if (mk.box == ED_SHAPE_CIRCLE + 1 && !mk.box_corners) {
            map_coord_name(mk.ox, mk.oy, at, sizeof at);
            fprintf(out, "box circle round %s, radius %d, over %s\n", at, mk.radius, r);
        }
        else if (mk.box == ED_SHAPE_CIRCLE + 1)
            fprintf(out, "box circle, radius %d, over %s\n", mk.radius, r);
        else
            fprintf(out, "box %s, %dx%d\n", r, mk.bx1 - mk.bx0 + 1, mk.by1 - mk.by0 + 1);
    }
    if (a->screen == SCREEN_PLAY && a->play.ngroup) {
        fputs("selected", out);
        for (int i = 0; i < a->play.ngroup; i++) {
            const Token *t = &m->tokens.v[a->play.group[i]];
            map_region_name(t->x, t->y, t->x + t->size - 1, t->y + t->size - 1, r, sizeof r);
            fprintf(out, "%s %s %s%s", i ? ";" : "", t->label[0] ? t->label : "(unnamed)", r,
                    t->hidden ? " (hidden)" : "");
        }
        fputc('\n', out);
    }
    if (mk.ruler) {
        fputs("ruler", out);
        for (int i = 0; i < mk.ruler->n; i++) {
            map_coord_name(mk.ruler->pts[i].x, mk.ruler->pts[i].y, at, sizeof at);
            fprintf(out, " %s", at);
        }
        map_coord_name(mk.ruler->cx, mk.ruler->cy, at, sizeof at);
        fprintf(out, " to %s, %s ft\n", at, dist);
    }
    for (int i = 0; i < a->npinged; i++) {
        const Ping *p = &a->pinged[i];
        char who[24];
        map_region_name(p->x0, p->y0, p->x1, p->y1, r, sizeof r);
        fprintf(out, "pinged by %s at %s, %lu s ago\n", ping_source(p->who, who, sizeof who), r,
                (unsigned long)(ping_age_ms(a, p) / 1000));
    }
}

