#include "ruler.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "prof.h"

/* ---------------------------------------------------------------- metrics */

/* map.h has to spell the default as a bare number; keep the two in step. */
_Static_assert(MAP_METRIC_DEFAULT == DIST_ALT_DIAG,
               "MAP_METRIC_DEFAULT and DIST_ALT_DIAG have drifted apart");

static const char *METRIC_NAMES[DIST_COUNT] = {
    "chebyshev", "euclidean", "alt", "manhattan",
};

const char *dist_metric_name(DistMetric m)
{
    return (m >= 0 && m < DIST_COUNT) ? METRIC_NAMES[m] : "?";
}

int dist_metric_from_name(const char *name)
{
    for (int i = 0; i < DIST_COUNT; i++)
        if (strcmp(name, METRIC_NAMES[i]) == 0) return i;

    /* Friendly aliases for the systems people name instead of the maths. */
    if (!strcmp(name, "5e") || !strcmp(name, "dnd5e")) return DIST_CHEBYSHEV;
    if (!strcmp(name, "true") || !strcmp(name, "exact")) return DIST_EUCLIDEAN;
    if (!strcmp(name, "3.5") || !strcmp(name, "pf") || !strcmp(name, "5-10-5"))
        return DIST_ALT_DIAG;
    return -1;
}

double dist_tiles(DistMetric m, int dx, int dy)
{
    int ax = dx < 0 ? -dx : dx;
    int ay = dy < 0 ? -dy : dy;
    int lo = ax < ay ? ax : ay;
    int hi = ax < ay ? ay : ax;

    switch (m) {
    case DIST_EUCLIDEAN: return sqrt((double)ax * ax + (double)ay * ay);
    case DIST_ALT_DIAG:  return (double)hi + (double)(lo / 2);
    case DIST_MANHATTAN: return (double)(ax + ay);
    case DIST_CHEBYSHEV:
    default:             return (double)hi;
    }
}

/* ------------------------------------------------------------ range bands */

/* Daggerheart describes distance in bands rather than numbers, and gives two
 * different guides to each: a fiction distance in feet, and an estimate for a
 * physical battle map. They do not agree -- Far is "about 30-100 feet" in the
 * fiction but "the long edge of a piece of paper, 11-12 inches" on the map.
 *
 * These are the map estimates, converted at the book's own "1 inch represents
 * roughly 5 feet", because that is the column written for playing on a grid.
 * Keeping them in feet rather than squares means they stay right when :scale
 * changes; at the default five-foot square they come out as 1, 3, 6 and 12
 * squares, which is what the book's estimates work out to.
 *
 * The SRD is explicit that these "aren't intended to be precisely measured
 * during play" and are a quick guide for the GM, so treat the readout as the
 * same kind of aid.
 *
 * Out of Range is deliberately absent. The book defines Very Far as anything
 * beyond Far that is still "within the bounds of the conflict or scene", and
 * Out of Range as beyond that -- a fiction call about the scene, not a
 * distance. Anything the cursor can reach is on the map, so it can never be
 * Out of Range. */
static const RangeBand DAGGERHEART_BANDS[] = {
    { "Melee",       5.0 },        /* touching distance: an adjacent square */
    { "Very Close", 15.0 },        /* the short edge of a game card, 2-3 in  */
    { "Close",      30.0 },        /* a pen or pencil, 5-6 in                */
    { "Far",        60.0 },        /* the long edge of a sheet of paper, 11-12 in */
    { "Very Far",   INFINITY },    /* beyond Far, anywhere still in the scene */
};

static const Ruleset RULESETS[] = {
    { "none",        NULL, 0, 1 },
    { "daggerheart", DAGGERHEART_BANDS,
      (int)(sizeof DAGGERHEART_BANDS / sizeof *DAGGERHEART_BANDS), 1 },
};

#define NRULESETS ((int)(sizeof RULESETS / sizeof *RULESETS))

const Ruleset *ruleset_by_name(const char *name)
{
    if (!name || !name[0]) return &RULESETS[0];
    for (int i = 0; i < NRULESETS; i++)
        if (strcmp(RULESETS[i].name, name) == 0) return &RULESETS[i];
    return NULL;
}

const Ruleset *ruleset_at(int i)
{
    return (i >= 0 && i < NRULESETS) ? &RULESETS[i] : NULL;
}

const char *ruleset_band(const Ruleset *rs, double units)
{
    if (!rs || !rs->bands) return NULL;
    for (int i = 0; i < rs->nbands; i++)
        if (units <= rs->bands[i].max) return rs->bands[i].name;
    return rs->bands[rs->nbands - 1].name;
}

/* ------------------------------------------------------------------ ruler */

void ruler_reset(Ruler *r) { memset(r, 0, sizeof *r); }

void ruler_start(Ruler *r, int x, int y)
{
    memset(r, 0, sizeof *r);
    r->active = 1;
    r->n      = 1;
    r->pts[0].x = (int16_t)x;
    r->pts[0].y = (int16_t)y;
    r->cx = (int16_t)x;
    r->cy = (int16_t)y;
}

void ruler_set_cursor(Ruler *r, int x, int y)
{
    r->cx = (int16_t)x;
    r->cy = (int16_t)y;
}

int ruler_add_waypoint(Ruler *r)
{
    if (!r->active || r->n >= RULER_MAX_POINTS) return 0;
    r->pts[r->n].x = r->cx;
    r->pts[r->n].y = r->cy;
    r->n++;
    return 1;
}

int ruler_drop_waypoint(Ruler *r)
{
    if (!r->active || r->n <= 1) return 0;
    r->n--;
    return 1;
}

double ruler_tiles(const Ruler *r, DistMetric m)
{
    if (!r->active || r->n < 1) return 0.0;

    double total = 0.0;
    for (int i = 1; i < r->n; i++)
        total += dist_tiles(m, r->pts[i].x - r->pts[i - 1].x,
                               r->pts[i].y - r->pts[i - 1].y);

    total += dist_tiles(m, r->cx - r->pts[r->n - 1].x,
                           r->cy - r->pts[r->n - 1].y);
    return total;
}

/* Bresenham, taking the diagonal where both axes advance. Appends to out,
 * skipping the first tile when it would repeat the previous segment's end. */
static int line_tiles(int x0, int y0, int x1, int y1,
                      RulerPt *out, int max, int n, int skip_first)
{
    int dx = x1 > x0 ? x1 - x0 : x0 - x1;
    int dy = y1 > y0 ? y1 - y0 : y0 - y1;
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx - dy;

    int x = x0, y = y0;
    for (;;) {
        if (!(skip_first && x == x0 && y == y0)) {
            if (n < max) {
                out[n].x = (int16_t)x;
                out[n].y = (int16_t)y;
            }
            n++;
        }
        skip_first = 0;
        if (x == x1 && y == y1) break;

        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x += sx; }
        if (e2 <  dx) { err += dx; y += sy; }
    }
    return n;
}

int ruler_trace(const Ruler *r, RulerPt *out, int max)
{
    if (!r->active || r->n < 1) return 0;

    int n = 0;
    for (int i = 1; i < r->n; i++)
        n = line_tiles(r->pts[i - 1].x, r->pts[i - 1].y,
                       r->pts[i].x, r->pts[i].y, out, max, n, i > 1);

    n = line_tiles(r->pts[r->n - 1].x, r->pts[r->n - 1].y, r->cx, r->cy,
                   out, max, n, r->n > 1);
    return n < max ? n : max;
}

/* Sight follows the straight line between two tiles. A step that moves both
 * axes can pass if either of the two ways round the corner is open; only a
 * corner walled on both sides stops it. */
int sight_blocked(const Map *m, int x0, int y0, int x1, int y1)
{
    int x = x0, y = y0;

    int dx = x1 > x ? x1 - x : x - x1;
    int dy = y1 > y ? y1 - y : y - y1;
    int sx = x < x1 ? 1 : -1;
    int sy = y < y1 ? 1 : -1;
    int err = dx - dy;

    while (x != x1 || y != y1) {
        int e2 = 2 * err;
        int stepx = 0, stepy = 0;
        if (e2 > -dy) { err -= dy; stepx = sx; }
        if (e2 <  dx) { err += dx; stepy = sy; }

        if (stepx && stepy) {
            int via_x = map_edge_opaque(m, x, y, stepx, 0) ||
                        map_edge_opaque(m, x + stepx, y, 0, stepy);
            int via_y = map_edge_opaque(m, x, y, 0, stepy) ||
                        map_edge_opaque(m, x, y + stepy, stepx, 0);
            if (via_x && via_y) return 1;
        } else if (map_edge_opaque(m, x, y, stepx, stepy)) {
            return 1;
        }

        x += stepx;
        y += stepy;
    }
    return 0;
}

int ruler_sight_blocked(const Ruler *r, const Map *m)
{
    if (!r->active) return 0;
    return sight_blocked(m, r->pts[0].x, r->pts[0].y, r->cx, r->cy);
}

/* ---------------------------------------------------------------- display */

void dist_fmt(char *buf, size_t n, double v)
{
    if (fabs(v - floor(v + 0.5)) < 0.05) snprintf(buf, n, "%.0f", floor(v + 0.5));
    else                                 snprintf(buf, n, "%.1f", v);
}

void ruler_status(const Ruler *r, const Map *m, char *buf, size_t bufsz)
{
    if (!r->active) { buf[0] = '\0'; return; }

    DistMetric metric = (DistMetric)m->metric;
    double tiles = ruler_tiles(r, metric);
    double units = tiles * m->scale_ft;

    char t[24], u[24];
    dist_fmt(t, sizeof t, tiles);
    dist_fmt(u, sizeof u, units);

    char extra[96];
    extra[0] = '\0';

    const Ruleset *rs = ruleset_by_name(m->ruleset);
    const char    *band = ruleset_band(rs, units);
    if (band)
        snprintf(extra, sizeof extra, "  %s%s", band, rs->verified ? "" : " (unverified)");

    int legs = r->n - 1;
    char legbuf[32];
    legbuf[0] = '\0';
    if (legs > 0) snprintf(legbuf, sizeof legbuf, "  %d leg%s", legs + 1, legs ? "s" : "");

    snprintf(buf, bufsz, "RULER   %s tiles  %s ft%s%s  %s  [%s]",
             t, u, extra, legbuf,
             ruler_sight_blocked(r, m) ? "sight blocked" : "sight clear",
             dist_metric_name(metric));
}

void ruler_draw(Renderer *rn, const Map *m, const GridView *g, const Ruler *r,
                const Theme *th, int show_label)
{
    PROF_ZONE("ruler.draw");

    if (!r->active) return;

    RulerPt trace[512];
    int     n = ruler_trace(r, trace, (int)(sizeof trace / sizeof *trace));

    int      blocked = ruler_sight_blocked(r, m);
    uint32_t tint    = blocked ? th->ruler_bad : th->ruler_bg;

    /* The traced tiles, so the path is visible on the map and not only as a
     * number at the bottom of the screen. */
    for (int i = 0; i < n; i++)
        grid_draw_tile_cursor(rn, g, trace[i].x, trace[i].y, tint);

    uint32_t mark = blocked ? th->ruler_bad : th->ruler;
    Style    ms   = style(mark, tint, ATTR_BOLD);

    /* Anchor, then each committed waypoint. */
    for (int i = 0; i < r->n; i++) {
        int sx, sy;
        grid_tile_interior(g, r->pts[i].x, r->pts[i].y, &sx, &sy);
        draw_cell(rn, sx + ZOOM[g->zoom].iw / 2, sy + ZOOM[g->zoom].ih / 2,
                  i == 0 ? 0x25C6u /* ◆ */ : 0x25CBu /* ○ */, ms);
    }

    int ex, ey;
    grid_tile_interior(g, r->cx, r->cy, &ex, &ey);
    draw_cell(rn, ex + ZOOM[g->zoom].iw / 2, ey + ZOOM[g->zoom].ih / 2,
              0x25C7u /* ◇ */, ms);

    if (!show_label) return;

    /* A compact readout beside the cursor, so the eye does not have to travel
     * to the status line while measuring. */
    DistMetric metric = (DistMetric)m->metric;
    double tiles = ruler_tiles(r, metric);
    double units = tiles * m->scale_ft;

    char t[24], u[24], label[64];
    dist_fmt(t, sizeof t, tiles);
    dist_fmt(u, sizeof u, units);

    const Ruleset *rs   = ruleset_by_name(m->ruleset);
    const char    *band = ruleset_band(rs, units);
    if (band) snprintf(label, sizeof label, " %s ft  %s ", u, band);
    else      snprintf(label, sizeof label, " %s tiles  %s ft ", t, u);

    int w  = text_width(label);
    int lx = ex + ZOOM[g->zoom].iw + 1;
    int ly = ey;

    /* Keep it inside the viewport, flipping to the other side when there is
     * no room rather than letting the clip eat it. */
    if (lx + w > g->view.x + g->view.w) lx = ex - w - 1;
    if (lx < g->view.x)                 lx = g->view.x;
    if (ly < g->view.y)                 ly = g->view.y;
    if (ly >= g->view.y + g->view.h)    ly = g->view.y + g->view.h - 1;

    draw_text(rn, lx, ly, label, w, style(th->bg, mark, ATTR_BOLD));
}
