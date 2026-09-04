#include "play.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "prof.h"
#include "ruler.h"
#include "token.h"
#include "util.h"

void play_init(Play *p)
{
    memset(p, 0, sizeof *p);
    p->sel           = -1;
    p->next_size     = 1;
    p->enforce_walls = 1;
    range_clear(&p->range);
}

int token_can_move(const Map *m, const Token *t, int dx, int dy, int enforce,
                   int except)
{
    int s  = t->size;
    int nx = t->x + dx;
    int ny = t->y + dy;

    /* The whole footprint has to land on the map. */
    if (nx < 0 || ny < 0 || nx + s > m->w || ny + s > m->h) return 0;
    if (!enforce) return 1;

    /* The other side is a wall you cannot walk through; your own is not.
     * Standing on an ally is allowed on the way past, which is why this is
     * about crossing rather than about where you come to rest -- refusing to
     * be put down is what stops two creatures sharing a square. */
    uint8_t other = (t->kind == TOKEN_PLAYER) ? TOKEN_ENEMY : TOKEN_PLAYER;
    if (tokens_overlapping(&m->tokens, nx, ny, s, except, other) >= 0) return 0;

    /* Every tile along the leading face must be able to make the crossing;
     * one blocked row is enough to stop the whole token. */
    if (dx > 0) {
        for (int y = t->y; y < t->y + s; y++)
            if (map_blocked(m, t->x + s - 1, y, 1, 0)) return 0;
    } else if (dx < 0) {
        for (int y = t->y; y < t->y + s; y++)
            if (map_blocked(m, t->x, y, -1, 0)) return 0;
    } else if (dy > 0) {
        for (int x = t->x; x < t->x + s; x++)
            if (map_blocked(m, x, t->y + s - 1, 0, 1)) return 0;
    } else if (dy < 0) {
        for (int x = t->x; x < t->x + s; x++)
            if (map_blocked(m, x, t->y, 0, -1)) return 0;
    }
    return 1;
}

static void trail_push(Play *p, int x, int y)
{
    if (p->ntrail >= PLAY_TRAIL_MAX) return;
    p->trail[p->ntrail].x = (int16_t)x;
    p->trail[p->ntrail].y = (int16_t)y;
    p->ntrail++;
}

/* Tokens step one axis at a time, so the route they could actually walk does
 * too. Corners are reached by turning, not by cutting. */
static const int TRAIL_DX[4] = {  1, -1,  0,  0 };
static const int TRAIL_DY[4] = {  0,  0,  1, -1 };

/* A route can be as long as the map has tiles, and MAP_MAX_DIM squared is
 * past what 16 bits would hold, so the distances are plain ints. */
#define TRAIL_UNREACHED (-1)

int play_step(Map *m, Undo *u, Play *p, int dx, int dy)
{
    if (p->sel < 0 || p->sel >= m->tokens.n) return 0;

    Token *t = &m->tokens.v[p->sel];
    if (!token_can_move(m, t, dx, dy, p->enforce_walls, p->sel)) return 0;

    undo_move_token(u, m, p->sel, t->x + dx, t->y + dy);
    p->steps++;
    play_trail_sync(p, m);
    return 1;
}

void play_focus(Play *p, int sel)
{
    /* The overlay is anchored to a creature and follows it about, so it goes
     * when the focus moves off that creature: a highlight still sitting
     * around whoever you were looking at a moment ago is worse than none at
     * all. One anchored to a bare tile belongs to nobody and stays put. */
    if (p->range.active && p->range.token >= 0 && p->range.token != sel)
        range_clear(&p->range);

    p->sel     = sel;
    p->grabbed = 0;
    p->steps   = 0;
    p->ntrail  = 0;
}

void play_select_at(Play *p, const Map *m, int tx, int ty)
{
    play_focus(p, tokens_at(&m->tokens, tx, ty));
}

/* Walks the list in ring order from `from`, returning the first index that
 * answers to both filters: the kind, and a label containing the needle.
 * PLAY_ANY_KIND and a NULL needle each match anything. -1 when none does.
 * Starting one past `from` and going all the way round means the token
 * already selected is the last candidate rather than the first, so a repeat
 * moves on instead of sitting still. */
static int scan_tokens(const Map *m, int from, int delta, int kind,
                       const char *needle)
{
    int n = m->tokens.n;
    if (n == 0) return -1;
    if (delta == 0) delta = 1;
    if (from < 0 || from >= n) from = delta > 0 ? -1 : n;

    for (int i = 1; i <= n; i++) {
        int idx = ((from + delta * i) % n + n) % n;
        const Token *t = &m->tokens.v[idx];

        if (kind != PLAY_ANY_KIND && t->kind != kind) continue;
        if (needle && !str_casestr(t->label, needle)) continue;
        return idx;
    }
    return -1;
}

int play_cycle(Play *p, const Map *m, int delta, int kind)
{
    if (m->tokens.n == 0) { play_focus(p, -1); return 0; }

    int idx = scan_tokens(m, p->sel, delta, kind, NULL);
    if (idx < 0) return 0;

    play_focus(p, idx);
    return 1;
}

int play_find(Play *p, const Map *m, const char *needle, int delta)
{
    if (needle && needle[0]) str_lcpy(p->search, needle, sizeof p->search);
    if (!p->search[0]) return 0;

    int idx = scan_tokens(m, p->sel, delta, PLAY_ANY_KIND, p->search);
    if (idx < 0) return 0;

    play_focus(p, idx);
    return 1;
}

void play_grab(Play *p, const Map *m, int undo_depth)
{
    if (p->sel < 0 || p->sel >= m->tokens.n) return;

    const Token *t = &m->tokens.v[p->sel];
    p->grabbed    = 1;
    p->grab_depth = undo_depth;
    p->origin_x = t->x;
    p->origin_y = t->y;
    p->steps    = 0;
    p->ntrail   = 0;
    play_trail_sync(p, m);
}

/* Breadth-first out from where the creature stands, over every tile its whole
 * footprint could occupy, until the tile it set out from is reached. Searching
 * from the destination rather than the origin is what lets the route then be
 * traced forward: at each tile the next step is any neighbour one closer, and
 * having a choice is the point -- across open floor there are many equally
 * short routes, and the one that hugs the straight line is the one a player
 * would actually walk. Picking arbitrarily gives an L. */
void play_trail_sync(Play *p, const Map *m)
{
    PROF_ZONE("trail.path");

    p->ntrail = 0;
    if (!p->grabbed || p->sel < 0 || p->sel >= m->tokens.n) return;

    const Token *tok = &m->tokens.v[p->sel];
    int gx = tok->x, gy = tok->y;
    int ox = p->origin_x, oy = p->origin_y;

    if (!map_in_bounds(m, ox, oy)) return;
    if (ox == gx && oy == gy) { trail_push(p, ox, oy); p->steps = 0; return; }

    size_t n     = (size_t)m->w * (size_t)m->h;
    int   *dist  = xmalloc(n * sizeof *dist);
    int   *queue = xmalloc(n * sizeof *queue);

    for (size_t i = 0; i < n; i++) dist[i] = TRAIL_UNREACHED;

    int head = 0, tail = 0;
    dist[(size_t)gy * (size_t)m->w + (size_t)gx] = 0;
    queue[tail++] = gy * m->w + gx;

    Token probe = *tok;
    int   found = 0;

    while (head < tail && !found) {
        int cur = queue[head++];
        int cx  = cur % m->w, cy = cur / m->w;

        for (int d = 0; d < 4; d++) {
            int nx = cx + TRAIL_DX[d], ny = cy + TRAIL_DY[d];
            if (nx < 0 || ny < 0 ||
                nx + probe.size > m->w || ny + probe.size > m->h) continue;

            size_t ni = (size_t)ny * (size_t)m->w + (size_t)nx;
            if (dist[ni] != TRAIL_UNREACHED) continue;

            /* The search runs backwards, so the question is whether a token
             * standing on the neighbour could step to here. A boundary stops
             * both ways, so either phrasing gives the same answer. */
            probe.x = (int16_t)nx;
            probe.y = (int16_t)ny;
            if (!token_can_move(m, &probe, -TRAIL_DX[d], -TRAIL_DY[d],
                                p->enforce_walls, p->sel))
                continue;

            dist[ni] = dist[cur] + 1;
            queue[tail++] = (int)ni;
            if (nx == ox && ny == oy) { found = 1; break; }
        }
    }

    /* Downhill from the origin, preferring at every fork the tile nearest the
     * straight line between the two ends. */
    int reach = dist[(size_t)oy * (size_t)m->w + (size_t)ox];
    if (reach != TRAIL_UNREACHED) {
        int cx = ox, cy = oy;
        trail_push(p, cx, cy);

        while ((cx != gx || cy != gy) && p->ntrail < PLAY_TRAIL_MAX) {
            int want = dist[(size_t)cy * (size_t)m->w + (size_t)cx] - 1;
            int  bx = -1, by = -1;
            long best = 0;

            for (int d = 0; d < 4; d++) {
                int nx = cx + TRAIL_DX[d], ny = cy + TRAIL_DY[d];
                if (nx < 0 || ny < 0 || nx >= m->w || ny >= m->h) continue;
                if (dist[(size_t)ny * (size_t)m->w + (size_t)nx] != want) continue;

                /* Being one closer is not the same as being reachable from
                 * here: the far side of a wall can be a step nearer the goal
                 * by going the long way round, and the trace would otherwise
                 * hop straight through the wall to reach it. */
                probe.x = (int16_t)cx;
                probe.y = (int16_t)cy;
                if (!token_can_move(m, &probe, TRAIL_DX[d], TRAIL_DY[d],
                                    p->enforce_walls, p->sel))
                    continue;

                long dev = labs((long)(gx - ox) * (ny - oy) -
                                (long)(gy - oy) * (nx - ox));
                if (bx < 0 || dev < best) { best = dev; bx = nx; by = ny; }
            }
            if (bx < 0) break;

            cx = bx; cy = by;
            trail_push(p, cx, cy);
        }
    }

    /* The route is what the move cost, so it is the number worth reporting --
     * taken from the search rather than the ribbon, which stops at its cap
     * while the cost carries on being true. With no route to price at all --
     * a creature carried through a wall while blocking was off, say -- the
     * keystroke count is the only honest number left, so it stands. */
    if (reach != TRAIL_UNREACHED) p->steps = reach;

    free(dist);
    free(queue);
}

void play_trail_draw(Renderer *r, const Map *m, const GridView *g,
                     const Play *p, const Theme *th, int ascii)
{
    PROF_ZONE("trail.draw");

    if (!p->grabbed || p->ntrail < 1) return;
    if (p->sel < 0 || p->sel >= m->tokens.n) return;

    int size = m->tokens.v[p->sel].size;

    int x0, y0, x1, y1;
    grid_visible_tiles(g, m, &x0, &y0, &x1, &y1);

    /* The whole footprint at every step, so a big creature shows the ground
     * it actually covered rather than a thread along its top-left corner.
     * Tiles off screen are skipped: a long walk should cost the window, not
     * the walk. */
    for (int i = 0; i < p->ntrail; i++)
        for (int dy = 0; dy < size; dy++)
            for (int dx = 0; dx < size; dx++) {
                int tx = p->trail[i].x + dx, ty = p->trail[i].y + dy;
                if (tx < x0 || tx > x1 || ty < y0 || ty > y1) continue;
                grid_draw_tile_cursor(r, g, tx, ty, th->trail_bg);
            }

    /* The tile it started from gets a mark of its own, not just the tint:
     * "where did this creature come from" is the question the trail exists to
     * answer, and a ribbon alone does not say which end is which. It lands
     * over the ghost outline, which is drawn first -- a one-tile footprint at
     * the closest zoom is a single cell, and one mark there beats two. */
    Rect a;
    grid_token_area(g, p->trail[0].x, p->trail[0].y, size, &a);

    int tiny = (a.w < 2 || a.h < 2);   /* the ghost's own fallback condition */
    int mx   = tiny ? a.x : a.x + a.w / 2;
    int my   = tiny ? a.y : a.y + a.h / 2;
    draw_cell(r, mx, my, ascii ? (uint32_t)'X' : 0x25C6u /* diamond */,
              style(th->trail, th->trail_bg, ATTR_BOLD));
}

int play_can_place(const Map *m, int tx, int ty, int size, int except)
{
    if (tx < 0 || ty < 0 || tx + size > m->w || ty + size > m->h) return 0;

    /* Coming to rest is the strict one. Creatures step through each other on
     * the way past, but two of them cannot end up sharing a square: a stack
     * of tokens is a stack you cannot see into. */
    return tokens_overlapping(&m->tokens, tx, ty, size, except,
                              TOKEN_ANY_KIND) < 0;
}

void play_move_label(Renderer *r, const Map *m, const GridView *g,
                     const Play *p, const Theme *th)
{
    PROF_ZONE("move.label");

    if (!p->grabbed || p->sel < 0 || p->sel >= m->tokens.n) return;

    const Token *t = &m->tokens.v[p->sel];
    int dx = t->x - p->origin_x, dy = t->y - p->origin_y;
    if (!dx && !dy) return;              /* nothing to say about no distance */

    double tiles = dist_tiles((DistMetric)m->metric, dx, dy);
    double units = tiles * m->scale_ft;

    char ft[24], sq[24], label[72];
    dist_fmt(ft, sizeof ft, units);
    dist_fmt(sq, sizeof sq, tiles);

    const Ruleset *rs   = ruleset_by_name(m->ruleset);
    const char    *band = ruleset_band(rs, units);
    if (band) snprintf(label, sizeof label, " %s ft  %s ", ft, band);
    else      snprintf(label, sizeof label, " %s sq  %s ft ", sq, ft);

    /* Beside the creature rather than above or below it: those two rows
     * belong to its status markers, and a distance covering a condition
     * would be a worse trade than one sitting out to the side. */
    Rect a;
    grid_token_area(g, t->x, t->y, t->size, &a);

    int w  = text_width(label);
    int lx = a.x + a.w + 1;
    int ly = a.y + a.h / 2;

    /* Keep it inside the viewport, flipping to the other side when there is
     * no room rather than letting the clip eat it. */
    if (lx + w > g->view.x + g->view.w) lx = a.x - w - 1;
    if (lx < g->view.x)                 lx = g->view.x;
    if (ly < g->view.y)                 ly = g->view.y;
    if (ly >= g->view.y + g->view.h)    ly = g->view.y + g->view.h - 1;

    draw_text(r, lx, ly, label, w, style(th->bg, th->trail, ATTR_BOLD));
}

void play_draw(Renderer *r, const Map *m, const Editor *e, const Play *p,
               const Theme *th, int ascii)
{
    PROF_ZONE("play.draw");

    grid_draw_labels(r, m, &e->view, th, ed_gutter(e, m), e->cx, e->cy);

    ClipRect saved = rnd_clip_push(r, e->view.view.x, e->view.view.y,
                                   e->view.view.w, e->view.view.h);

    grid_draw(r, m, &e->view, th, ascii, 0);   /* play mode gives nothing away */

    /* Under everything else, so tokens standing in it stay readable. */
    range_draw(r, m, &e->view, &p->range, th);

    /* The outline first, then the trail over it: the trail tints backgrounds
     * without touching glyphs, so the outline survives, and its own origin
     * mark lands on top where the two would otherwise both claim one cell. */
    if (p->grabbed && p->sel >= 0 && p->sel < m->tokens.n)
        grid_draw_token_ghost(r, &e->view, p->origin_x, p->origin_y,
                              m->tokens.v[p->sel].size, th);

    /* Over the range wash but under the cursor: where this creature has been
     * is a more specific fact than what is merely in range of it. */
    play_trail_draw(r, m, &e->view, p, th, ascii);

    /* The cursor tint goes under the tokens so a token is never hidden by
     * it; the corner marks below put the cursor back on top. */
    grid_draw_tile_cursor(r, &e->view, e->cx, e->cy, th->cursor_bg);

    for (int i = 0; i < m->tokens.n; i++)
        grid_draw_token(r, &e->view, &m->tokens.v[i], th, i == p->sel, ascii);

    /* After every token, so a marker is never buried under the next one. */
    for (int i = 0; i < m->tokens.n; i++)
        grid_draw_token_status(r, &e->view, &m->tokens.v[i], th, ascii);

    /* Recolouring the tile's four boundary corners keeps the cursor visible
     * on top of a token without painting over the box-drawing underneath. */
    grid_draw_tile_marker(r, &e->view, e->cx, e->cy, th->accent);

    /* Over everything, since it is the one thing being read right now. */
    play_move_label(r, m, &e->view, p, th);

    rnd_clip_restore(r, saved);
}

void play_status(const Play *p, const Map *m, const Editor *e, char *buf, size_t bufsz)
{
    const char *walls = p->enforce_walls ? "walls on" : "walls OFF";

    if (p->sel >= 0 && p->sel < m->tokens.n) {
        const Token *t = &m->tokens.v[p->sel];
        if (p->grabbed) {
            /* What the move costs and how far it went answer different
             * questions: one is the route walked around the walls, the other
             * is the straight line a range band cares about. Show both rather
             * than making the GM convert. */
            double tiles = dist_tiles((DistMetric)m->metric,
                                      t->x - p->origin_x, t->y - p->origin_y);
            double units = tiles * m->scale_ft;

            /* With no walkable route the count is keystrokes rather than the
             * cost of a move, and the ribbon is missing; say so rather than
             * letting a blank map read as a bug. */
            const char *route = p->ntrail ? "" : "  no route";

            char from[MAP_COORD_MAX];
            map_coord_name(p->origin_x, p->origin_y, from, sizeof from);

            /* The distance and its band ride beside the creature now, where
             * the eye already is; this line keeps what there is no room for
             * out there. */
            (void)units;
            snprintf(buf, bufsz,
                     "MOVING  %.20s %dx%d  %d step%s%s  from %s  %s",
                     t->label[0] ? t->label : token_kind_name(t->kind),
                     t->size, t->size, p->steps, p->steps == 1 ? "" : "s", route,
                     from, walls);
            return;
        }
        /* Spell the markers out here: the map shows their initials, and the
         * initial alone does not say which condition it stands for. */
        char marks[96];
        int  off = 0;
        marks[0] = '\0';
        for (int i = 0; i < t->nstatus && off < (int)sizeof marks - 20; i++)
            off += snprintf(marks + off, sizeof marks - (size_t)off, "%s%s %.14s",
                            i ? ", " : "  [",
                            status_color_name(t->status[i].color),
                            t->status[i].label);
        if (t->nstatus && off < (int)sizeof marks - 2)
            snprintf(marks + off, sizeof marks - (size_t)off, "]");

        char at[MAP_COORD_MAX];
        map_coord_name(t->x, t->y, at, sizeof at);

        snprintf(buf, bufsz, "PLAY    %.20s (%s %dx%d) at %s%s  %s",
                 t->label[0] ? t->label : "unlabelled",
                 token_kind_name(t->kind), t->size, t->size, at,
                 marks, walls);
        return;
    }

    char at[MAP_COORD_MAX];
    map_coord_name(e->cx, e->cy, at, sizeof at);

    snprintf(buf, bufsz, "PLAY    %s  %s  %d token%s  next size %d  %s",
             at, map_walkable(m, e->cx, e->cy) ? "floor" : "void",
             m->tokens.n, m->tokens.n == 1 ? "" : "s", p->next_size, walls);
}

/* ---------------------------------------------------------- range overlay */

void range_clear(RangeOverlay *ro) { memset(ro, 0, sizeof *ro); ro->token = -1; }

void range_token_removed(RangeOverlay *ro, int removed, int x, int y)
{
    if (!ro->active || ro->token < 0) return;

    if (ro->token == removed) {
        /* Fall back to where it stood, so the highlight does not silently
         * jump to whichever token inherited the index. */
        ro->token = -1;
        ro->ax    = x;
        ro->ay    = y;
    } else if (ro->token > removed) {
        ro->token--;
    }
}

int range_cycle(RangeOverlay *ro, const Map *m, int anchor_token, int cx, int cy)
{
    const Ruleset *rs = ruleset_by_name(m->ruleset);
    if (!rs || !rs->bands || rs->nbands == 0) {
        range_clear(ro);
        return -1;
    }

    if (!ro->active) {
        /* The anchor is fixed on the way in, so cycling through the bands
         * afterwards does not drag it along with the cursor. */
        ro->active = 1;
        ro->band   = 0;
        ro->token  = anchor_token;
        ro->ax     = cx;
        ro->ay     = cy;
        return ro->band;
    }

    ro->band++;
    if (ro->band >= rs->nbands) {
        range_clear(ro);
        return -1;
    }
    return ro->band;
}

void range_anchor(const RangeOverlay *ro, const Map *m, int *ax, int *ay, int *asize)
{
    if (ro->token >= 0 && ro->token < m->tokens.n) {
        const Token *t = &m->tokens.v[ro->token];
        *ax = t->x;
        *ay = t->y;
        *asize = t->size;
        return;
    }
    *ax = ro->ax;
    *ay = ro->ay;
    *asize = 1;
}

/* Distance from a footprint to a tile is the distance from its nearest
 * square, which is what reach means for a creature bigger than one tile.
 * Reports that nearest square too, so sight can be traced from it. */
static double footprint_dist(DistMetric metric, int ax, int ay, int asize,
                             int tx, int ty, int *nx, int *ny)
{
    double best = 1e30;
    for (int y = ay; y < ay + asize; y++) {
        for (int x = ax; x < ax + asize; x++) {
            double d = dist_tiles(metric, tx - x, ty - y);
            if (d < best) {
                best = d;
                if (nx) *nx = x;
                if (ny) *ny = y;
            }
        }
    }
    return best;
}

double range_units_to(const RangeOverlay *ro, const Map *m, int tx, int ty)
{
    int ax, ay, asize;
    range_anchor(ro, m, &ax, &ay, &asize);
    return footprint_dist((DistMetric)m->metric, ax, ay, asize, tx, ty, NULL, NULL)
           * m->scale_ft;
}

int range_contains(const RangeOverlay *ro, const Map *m, int tx, int ty)
{
    if (!ro->active) return 0;
    const Ruleset *rs = ruleset_by_name(m->ruleset);
    if (!rs || !rs->bands || ro->band >= rs->nbands) return 0;
    return range_units_to(ro, m, tx, ty) <= rs->bands[ro->band].max;
}

/* Distance between two footprints, plus the pair of squares that achieves it,
 * so a token's reach is measured edge to edge rather than corner to corner. */
static double token_dist(DistMetric metric, int ax, int ay, int asize,
                         const Token *t, int *sx, int *sy, int *dx, int *dy)
{
    double best = 1e30;
    for (int y = t->y; y < t->y + t->size; y++) {
        for (int x = t->x; x < t->x + t->size; x++) {
            int    fx = ax, fy = ay;
            double d  = footprint_dist(metric, ax, ay, asize, x, y, &fx, &fy);
            if (d < best) {
                best = d;
                if (sx) *sx = fx;
                if (sy) *sy = fy;
                if (dx) *dx = x;
                if (dy) *dy = y;
            }
        }
    }
    return best;
}

void range_draw(Renderer *r, const Map *m, const GridView *g,
                const RangeOverlay *ro, const Theme *th)
{
    PROF_ZONE("range.draw");

    if (!ro->active) return;

    const Ruleset *rs = ruleset_by_name(m->ruleset);
    if (!rs || !rs->bands || ro->band >= rs->nbands) return;
    double reach = rs->bands[ro->band].max;

    int ax, ay, asize;
    range_anchor(ro, m, &ax, &ay, &asize);

    /* Only the tiles that can actually appear on screen are considered. A
     * band with no upper bound covers the whole map, and shading it tile by
     * tile would otherwise scale with the map rather than the window. */
    int x0, y0, x1, y1;
    grid_visible_tiles(g, m, &x0, &y0, &x1, &y1);

    DistMetric metric = (DistMetric)m->metric;

    for (int ty = y0; ty <= y1; ty++) {
        for (int tx = x0; tx <= x1; tx++) {
            int    nx = ax, ny = ay;
            double d  = footprint_dist(metric, ax, ay, asize, tx, ty, &nx, &ny)
                        * m->scale_ft;
            if (d > reach) continue;

            /* In range but with no line to it: you cannot target what you
             * cannot see, so it gets the dimmer shade. */
            int blocked = sight_blocked(m, nx, ny, tx, ty);
            grid_draw_tile_cursor(r, g, tx, ty, blocked ? th->range_dim : th->range_bg);
        }
    }
}

void range_status(const RangeOverlay *ro, const Map *m, char *buf, size_t bufsz)
{
    if (!ro->active) { buf[0] = '\0'; return; }

    const Ruleset *rs = ruleset_by_name(m->ruleset);
    if (!rs || !rs->bands || ro->band >= rs->nbands) { buf[0] = '\0'; return; }

    const RangeBand *band = &rs->bands[ro->band];
    int ax, ay, asize;
    range_anchor(ro, m, &ax, &ay, &asize);

    /* What the highlight is measuring, in both the units and the squares. */
    char reach[48];
    if (band->max >= 1e30)
        snprintf(reach, sizeof reach, "beyond %s",
                 ro->band > 0 ? rs->bands[ro->band - 1].name : "melee");
    else
        snprintf(reach, sizeof reach, "%g ft, %g sq", band->max,
                 m->scale_ft > 0 ? band->max / m->scale_ft : 0.0);

    char from[40] = "here";
    if (ro->token >= 0 && ro->token < m->tokens.n) {
        const Token *t = &m->tokens.v[ro->token];
        snprintf(from, sizeof from, "%.24s",
                 t->label[0] ? t->label : token_kind_name(t->kind));
    }

    /* Naming them catches the ones scrolled off screen, which the highlight
     * cannot. A trailing * marks a target with no line of sight. */
    char names[128];
    int  off = 0, count = 0, hidden = 0;
    names[0] = '\0';

    for (int i = 0; i < m->tokens.n; i++) {
        if (i == ro->token) continue;
        const Token *t = &m->tokens.v[i];

        int    sx = ax, sy = ay, dx = t->x, dy = t->y;
        double d  = token_dist((DistMetric)m->metric, ax, ay, asize, t,
                               &sx, &sy, &dx, &dy) * m->scale_ft;
        if (d > band->max) continue;

        count++;
        int blocked = sight_blocked(m, sx, sy, dx, dy);
        if (blocked) hidden++;

        if (off < (int)sizeof names - 24)
            off += snprintf(names + off, sizeof names - (size_t)off, "%s%.16s%s",
                            off ? ", " : "",
                            t->label[0] ? t->label : token_kind_name(t->kind),
                            blocked ? "*" : "");
    }

    snprintf(buf, bufsz, "%s (%s) from %s - %d in range%s%s%s",
             band->name, reach, from, count,
             count ? ": " : "", names,
             hidden ? "   * no line of sight" : "");
}
