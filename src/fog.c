#include "fog.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "prof.h"
#include "ruler.h"
#include "util.h"

int fog_any(const Map *m)
{
    if (!m || !m->fog_on) return 0;
    for (int i = 0; i < FOG_PATCH_MAX; i++) {
        const FogPatch *p = &m->fog_patches[i];
        if (fog_patch_live(m, i + 1) && p->x1 >= p->x0) return 1;
    }
    return 0;
}

/* Can sight cross from (x,y) to its neighbour (x+dx,y+dy)? A diagonal needs
 * all four of the crossings round both sides of its corner clear, the rule
 * map_blocked uses for movement, so the rim never wraps round a corner. */
static int sight_step_clear(const Map *m, int x, int y, int dx, int dy)
{
    if (!dx || !dy) return !map_edge_opaque(m, x, y, dx, dy);
    return !map_edge_opaque(m, x, y, dx, 0) && !map_edge_opaque(m, x, y, 0, dy) &&
           !map_edge_opaque(m, x + dx, y, 0, dy) && !map_edge_opaque(m, x, y + dy, dx, 0);
}

/* Does any boundary strictly inside this rectangle stop sight? A line
 * between two squares of the rectangle crosses only boundaries inside it,
 * so when none of them is opaque every such line is clear -- and the per
 * square line tests can be skipped outright. One pass over the edges
 * instead of a line walk a square: the common case, an open room or a
 * hall, pays for the rectangle and nothing else. */
static int rect_opaque(const Map *m, int x0, int y0, int x1, int y1)
{
    for (int y = y0; y <= y1; y++)
        for (int x = x0 + 1; x <= x1; x++)
            if (map_edge_opaque(m, x - 1, y, 1, 0)) return 1;
    for (int y = y0 + 1; y <= y1; y++)
        for (int x = x0; x <= x1; x++)
            if (map_edge_opaque(m, x, y - 1, 0, 1)) return 1;
    return 0;
}

/* ------------------------------------------------------------ sight
 *
 * Each player creature's light is worked out alone and kept (Map.sight):
 * LIT is the union of those, RIM is worked out from LIT. A keystroke that
 * was nothing but moves relights only around the creatures that moved;
 * anything else rebuilds every entry, which is the old algorithm. The
 * proof that a keystroke was only moves is in fog_recompute below. */

typedef struct { int x0, y0, x1, y1; } Box;

/* The most moves one keystroke can be proven to be: a group carry moves at
 * most PLAY_GROUP_MAX creatures a step. More than that rebuilds everything. */
#define FOG_STEP_MAX 32

/* How many recomputes took each path, for the tests: a change that quietly
 * sent every step down the full path would pass every other check. */
static unsigned g_counts[2];

static int box_empty(Box b) { return b.x1 < b.x0 || b.y1 < b.y0; }

/* The box a creature lights in patch `id`: its reach, cut to the patch's
 * extent and the map. Empty for a patch that does not light itself. */
static Box reach_box(const Map *m, const Token *t, int id)
{
    Box b = { 0, 0, -1, -1 };
    const FogPatch *p = &m->fog_patches[id - 1];
    if (!fog_patch_live(m, id) || p->reveal < 0 || p->x1 < p->x0) return b;
    int n = p->reveal;
    b.x0 = imax(imax(t->x - n, p->x0), 0);
    b.x1 = imin(imin(t->x + t->size - 1 + n, p->x1), m->w - 1);
    b.y0 = imax(imax(t->y - n, p->y0), 0);
    b.y1 = imin(imin(t->y + t->size - 1 + n, p->y1), m->h - 1);
    return b;
}

static Box entry_box(const SightEntry *e)
{
    Box b = { e->x0, e->y0, e->x1, e->y1 };
    return b;
}

/* An entry's box with the one square round it the rim can reach. */
static Box margin(const Map *m, Box b)
{
    Box r = { imax(b.x0 - 1, 0), imax(b.y0 - 1, 0), imin(b.x1 + 1, m->w - 1), imin(b.y1 + 1, m->h - 1) };
    return r;
}

/* What an entry says of a square: VIS_NO is out of reach or out of sight,
 * VIS_YES is seen, VIS_ASK is in reach but not yet looked at -- a full
 * rebuild skips the line walk for a square an earlier creature has already
 * lit, as the old algorithm did, and the step path walks it only if that
 * square's light is ever in question (see entry_apply). */
enum { VIS_NO, VIS_YES, VIS_ASK };

/* Can any square of this creature see (x,y)? The ruler's own line test. */
static int creature_sees(const Map *m, const Token *t, int x, int y)
{
    int tx1 = t->x + t->size - 1, ty1 = t->y + t->size - 1;
    int ox = iclamp(x, t->x, tx1), oy = iclamp(y, t->y, ty1);
    if (!sight_blocked(m, ox, oy, x, y)) return 1;
    for (int fy = t->y; fy <= ty1; fy++)
        for (int fx = t->x; fx <= tx1; fx++)
            if ((fx != ox || fy != oy) && !sight_blocked(m, fx, fy, x, y)) return 1;
    return 0;
}

/* What this creature alone lights: every square of a patch within that
 * patch's reveal, in the map's metric from the creature's nearest square,
 * that some square of it can see. With `fused`, the full rebuild's way, it
 * lights them as it goes and leaves VIS_ASK on squares already lit by an
 * earlier creature; without, every square is settled, which is what a
 * creature that just moved needs. Either way the entry stays true while
 * other creatures move, since what it cannot answer it says it cannot. */
static void entry_build(Map *m, const Token *t, SightEntry *e, int fused)
{
    e->x0 = e->y0 = 0;
    e->x1 = e->y1 = -1;
    if (t->kind != TOKEN_PLAYER) return;

    Box all = { m->w, m->h, -1, -1 };
    for (int id = 1; id <= FOG_PATCH_MAX; id++) {
        Box b = reach_box(m, t, id);
        if (box_empty(b)) continue;
        all.x0 = imin(all.x0, b.x0); all.y0 = imin(all.y0, b.y0);
        all.x1 = imax(all.x1, b.x1); all.y1 = imax(all.y1, b.y1);
    }
    if (box_empty(all)) return;

    size_t bw = (size_t)(all.x1 - all.x0 + 1), area = bw * (size_t)(all.y1 - all.y0 + 1);
    if (e->cap < area) {
        free(e->vis);
        e->vis = xcalloc(area, 1);
        e->cap = area;
    } else {
        memset(e->vis, 0, area);
    }
    e->x0 = (int16_t)all.x0; e->y0 = (int16_t)all.y0;
    e->x1 = (int16_t)all.x1; e->y1 = (int16_t)all.y1;

    int tx1 = t->x + t->size - 1, ty1 = t->y + t->size - 1;
    for (int id = 1; id <= FOG_PATCH_MAX; id++) {
        Box b = reach_box(m, t, id);
        if (box_empty(b)) continue;
        int n = m->fog_patches[id - 1].reveal;
        /* Every line runs inside the box that holds both the creature and
         * its reach; if nothing in there stops sight, none does. */
        int open = !rect_opaque(m, imin(b.x0, t->x), imin(b.y0, t->y), imax(b.x1, tx1), imax(b.y1, ty1));

        /* Distance from the creature's nearest square, in the map's metric,
         * so fog agrees with the ruler. Every metric grows as either offset
         * does, so a row d squares off the creature reaches a fixed number
         * of squares either side of it: worked out once a row with the
         * ruler's own dist_tiles, rather than once a square. */
        int lim[FOG_REVEAL_MAX + 1];
        for (int d = 0; d <= n; d++) {
            int l = -1;
            while (l < n && dist_tiles((DistMetric)m->metric, l + 1, d) <= (double)n + 1e-9) l++;
            lim[d] = l;
        }

        for (int y = b.y0; y <= b.y1; y++) {
            int oy = iclamp(y, t->y, ty1), dy = y > oy ? y - oy : oy - y;
            if (dy > n || lim[dy] < 0) continue;
            int xa = imax(b.x0, t->x - lim[dy]), xb = imin(b.x1, tx1 + lim[dy]);
            uint8_t *row = e->vis + (size_t)(y - all.y0) * bw - (size_t)all.x0;
            for (int x = xa; x <= xb; x++) {
                uint8_t *f = &m->fog[(size_t)y * (size_t)m->w + (size_t)x];
                if ((*f & FOG_ID) != (uint8_t)id) continue;
                if (fused && (*f & FOG_LIT)) { row[x] = VIS_ASK; continue; }
                if (!open && !creature_sees(m, t, x, y)) continue;
                row[x] = VIS_YES;
                if (fused) {
                    *f |= FOG_LIT;
                    if (m->fog_patches[id - 1].memory) *f |= FOG_SEEN;
                }
            }
        }
    }
}

/* LIT (and SEEN, where the patch remembers) from one entry of creature t,
 * inside `clip`. A VIS_ASK square is settled here, by the line walk the
 * rebuild skipped: the creature has not moved and nothing it looks through
 * has changed since, or this would not be the step path. */
static void entry_apply(Map *m, const Token *t, SightEntry *e, Box clip)
{
    int x0 = imax(e->x0, clip.x0), x1 = imin(e->x1, clip.x1);
    int y0 = imax(e->y0, clip.y0), y1 = imin(e->y1, clip.y1);
    size_t bw = (size_t)(e->x1 - e->x0 + 1);
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) {
            uint8_t *v = &e->vis[(size_t)(y - e->y0) * bw + (size_t)(x - e->x0)];
            if (*v == VIS_NO) continue;
            if (*v == VIS_ASK) *v = creature_sees(m, t, x, y) ? VIS_YES : VIS_NO;
            if (*v == VIS_NO) continue;
            uint8_t *f = &m->fog[(size_t)y * (size_t)m->w + (size_t)x];
            *f |= FOG_LIT;
            if (m->fog_patches[(*f & FOG_ID) - 1].memory) *f |= FOG_SEEN;
        }
}

static void clear_box(Map *m, Box b)
{
    const uint8_t keep = (uint8_t)~(FOG_LIT | FOG_RIM);
    for (int y = b.y0; y <= b.y1; y++)
        for (int x = b.x0; x <= b.x1; x++)
            m->fog[(size_t)y * (size_t)m->w + (size_t)x] &= keep;
}

/* The rim: fog still hiding, beside a lit square it can see. Worked out
 * here, once a move, so drawing it is one bit. */
static void rim_box(Map *m, Box b)
{
    for (int y = b.y0; y <= b.y1; y++)
        for (int x = b.x0; x <= b.x1; x++) {
            uint8_t *f = &m->fog[(size_t)y * (size_t)m->w + (size_t)x];
            if (!fog_patch_live(m, *f & FOG_ID) || (*f & (FOG_LIT | FOG_RIM))) continue;
            for (int dy = -1; dy <= 1 && !(*f & FOG_RIM); dy++)
                for (int dx = -1; dx <= 1; dx++) {
                    if ((!dx && !dy) || !map_in_bounds(m, x + dx, y + dy)) continue;
                    if (!(fog_at(m, x + dx, y + dy) & FOG_LIT)) continue;
                    if (!sight_step_clear(m, x + dx, y + dy, -dx, -dy)) continue;
                    *f |= FOG_RIM;
                    break;
                }
        }
}

/* Boxes that overlap are cheaper done once as the box round them all,
 * when that is no bigger than doing each: a party standing together, or a
 * creature's old and new reach a step apart. Clearing, lighting and the rim
 * are each the same whichever way the squares are covered. */
static int merge(Box *u, int nu)
{
    if (nu < 2) return nu;
    Box all = u[0];
    long sum = 0;
    for (int i = 0; i < nu; i++) {
        all.x0 = imin(all.x0, u[i].x0); all.y0 = imin(all.y0, u[i].y0);
        all.x1 = imax(all.x1, u[i].x1); all.y1 = imax(all.y1, u[i].y1);
        sum += (long)(u[i].x1 - u[i].x0 + 1) * (long)(u[i].y1 - u[i].y0 + 1);
    }
    if ((long)(all.x1 - all.x0 + 1) * (long)(all.y1 - all.y0 + 1) > sum) return nu;
    u[0] = all;
    return 1;
}

static void snapshot(Map *m)
{
    Sight *s = &m->sight;
    for (int i = 0; i < m->tokens.n; i++) {
        const Token *t = &m->tokens.v[i];
        s->tok[i].x = t->x; s->tok[i].y = t->y;
        s->tok[i].size = t->size; s->tok[i].kind = t->kind;
    }
    s->n      = m->tokens.n;
    s->gen    = m->gen;
    s->fog_on = m->fog_on;
    s->metric = m->metric;
    s->w      = m->w;
    s->h      = m->h;
    memcpy(s->patches, m->fog_patches, sizeof s->patches);
    s->valid  = 1;
}

/* The margin boxes of every non-empty entry, merged when that is cheaper.
 * Static scratch: one map is worked out at a time. */
static Box *g_boxes;
static int  g_boxcap;

static int margins(Map *m, Box **out)
{
    Sight *s = &m->sight;
    if (g_boxcap < s->n) {
        g_boxcap = imax(s->n, 16);
        g_boxes  = xrealloc(g_boxes, (size_t)g_boxcap * sizeof *g_boxes);
    }
    int nb = 0;
    for (int i = 0; i < s->n; i++)
        if (!box_empty(entry_box(&s->e[i]))) g_boxes[nb++] = margin(m, entry_box(&s->e[i]));
    *out = g_boxes;
    return merge(g_boxes, nb);
}

static void recompute_full(Map *m)
{
    Sight *s = &m->sight;
    Box *b;
    int  nb;
    {
        PROF_ZONE("fog.sight.union");
        nb = margins(m, &b);
        for (int i = 0; i < nb; i++) clear_box(m, b[i]);
    }

    if (s->cap < m->tokens.n) {
        int cap = imax(m->tokens.n, 8);
        s->e   = xrealloc(s->e, (size_t)cap * sizeof *s->e);
        s->tok = xrealloc(s->tok, (size_t)cap * sizeof *s->tok);
        memset(s->e + s->cap, 0, (size_t)(cap - s->cap) * sizeof *s->e);
        s->cap = cap;
    }
    Token none = { 0 };
    none.kind = TOKEN_ENEMY;
    {
        PROF_ZONE("fog.sight.build");
        for (int i = 0; i < m->tokens.n; i++)
            entry_build(m, m->fog_on ? &m->tokens.v[i] : &none, &s->e[i], 1);
    }
    s->n = m->tokens.n;
    {
        PROF_ZONE("fog.sight.rim");
        nb = margins(m, &b);
        for (int i = 0; i < nb; i++) rim_box(m, b[i]);
    }
    snapshot(m);
}

/* Only the creatures in moved[] have changed, and only where they stand.
 * Their old and new boxes, with the rim's margin, hold every square whose
 * LIT or RIM could have changed: clear those, rebuild the movers, relight
 * those boxes from every entry that reaches into them, and redo the rim
 * there. Everywhere else is as it was. */
static void recompute_step(Map *m, const int *moved, int k)
{
    Sight *s = &m->sight;
    Box u[2 * FOG_STEP_MAX];
    int nu = 0;
    for (int j = 0; j < k; j++) {
        Box b = entry_box(&s->e[moved[j]]);
        if (!box_empty(b)) u[nu++] = margin(m, b);
    }
    {
        PROF_ZONE("fog.sight.build");
        for (int j = 0; j < k; j++) {
            entry_build(m, &m->tokens.v[moved[j]], &s->e[moved[j]], 0);
            Box b = entry_box(&s->e[moved[j]]);
            if (!box_empty(b)) u[nu++] = margin(m, b);
        }
    }
    nu = merge(u, nu);
    {
        PROF_ZONE("fog.sight.union");
        for (int r = 0; r < nu; r++) clear_box(m, u[r]);
        for (int i = 0; i < s->n; i++) {
            Box b = entry_box(&s->e[i]);
            if (box_empty(b)) continue;
            for (int r = 0; r < nu; r++)
                if (b.x0 <= u[r].x1 && u[r].x0 <= b.x1 && b.y0 <= u[r].y1 && u[r].y0 <= b.y1)
                    entry_apply(m, &m->tokens.v[i], &s->e[i], u[r]);
        }
    }
    {
        PROF_ZONE("fog.sight.rim");
        for (int r = 0; r < nu; r++) rim_box(m, u[r]);
    }
    snapshot(m);
}

void fog_recompute(Map *m)
{
    PROF_ZONE("fog.sight");
    Sight *s = &m->sight;

    /* The step path's proof. map_touch is the only writer of Map.gen, every
     * change to a map touches it, and a move touches exactly once and does
     * nothing else. So if gen moved by k, and exactly k creatures stand
     * somewhere else -- same list, same sizes and sides, every setting sight
     * reads unchanged -- then those k touches were those k moves and nothing
     * else happened. Anything short of that proof rebuilds everything. */
    unsigned k = m->gen - s->gen;
    int moved[FOG_STEP_MAX], nmoved = 0, step = 0;
    if (s->valid && m->fog_on && s->fog_on && k >= 1 && k <= FOG_STEP_MAX &&
        s->n == m->tokens.n && s->metric == m->metric && s->w == m->w && s->h == m->h &&
        !memcmp(s->patches, m->fog_patches, sizeof s->patches)) {
        step = 1;
        for (int i = 0; i < m->tokens.n && step; i++) {
            const Token *t = &m->tokens.v[i];
            const SightTok *o = &s->tok[i];
            if (o->size != t->size || o->kind != t->kind) step = 0;
            else if (o->x != t->x || o->y != t->y) {
                if (nmoved == (int)k) step = 0;
                else moved[nmoved++] = i;
            }
        }
        step = step && nmoved == (int)k;
    }
    g_counts[step]++;
    if (step) recompute_step(m, moved, nmoved);
    else      recompute_full(m);
}

void fog_sight_counts(unsigned *steps, unsigned *fulls)
{
    if (steps) *steps = g_counts[1];
    if (fulls) *fulls = g_counts[0];
}

int fog_token_hidden(const Map *m, const Token *t)
{
    for (int y = t->y; y < t->y + t->size; y++)
        for (int x = t->x; x < t->x + t->size; x++)
            if (!fog_creature_hidden(m, x, y)) return 0;
    return 1;
}

int fog_token_silhouette(const Map *m, const Token *t)
{
    for (int y = t->y; y < t->y + t->size; y++)
        for (int x = t->x; x < t->x + t->size; x++)
            if (fog_rim_shown(m, x, y)) return 1;
    return 0;
}

int fog_find(const Map *m, const char *prefix)
{
    if (!prefix || !*prefix) return 0;
    size_t len = strlen(prefix);
    int found = 0, hits = 0;
    for (int i = 0; i < FOG_PATCH_MAX; i++) {
        const FogPatch *p = &m->fog_patches[i];
        if (!p->name[0] || p->dead || strncasecmp(p->name, prefix, len) != 0) continue;
        if (strlen(p->name) == len) return i + 1;
        found = i + 1;
        hits++;
    }
    return hits > 1 ? -1 : found;
}

int fog_create(Map *m, const char *name)
{
    size_t n = strlen(name);
    if (n == 0 || n >= FOG_NAME_MAX || !isalpha((unsigned char)name[0])) return -1;
    for (size_t i = 0; i < n; i++)
        if (!isalnum((unsigned char)name[i])) return -1;

    for (int i = 0; i < FOG_PATCH_MAX; i++) {
        const FogPatch *p = &m->fog_patches[i];
        if (p->name[0] && !p->dead && !strcasecmp(p->name, name)) return i + 1;
    }
    for (int i = 0; i < FOG_PATCH_MAX; i++) {
        FogPatch *p = &m->fog_patches[i];
        if (p->name[0] || p->dead) continue;
        memset(p, 0, sizeof *p);
        str_lcpy(p->name, name, sizeof p->name);
        p->reveal    = 2;
        p->memory    = 1;
        p->soft_edge = -1;
        p->x1        = -1;
        map_touch(m);
        return i + 1;
    }
    return 0;
}

void fog_delete(Map *m, int id)
{
    if (id < 1 || id > FOG_PATCH_MAX) return;
    FogPatch *p = &m->fog_patches[id - 1];
    if (p->x1 >= p->x0)
        for (int y = p->y0; y <= p->y1; y++)
            for (int x = p->x0; x <= p->x1; x++)
                if ((fog_at(m, x, y) & FOG_ID) == (uint8_t)id)
                    m->fog[(size_t)y * (size_t)m->w + (size_t)x] = 0;
    memset(p, 0, sizeof *p);
    p->x1   = -1;
    p->dead = 1;
    map_touch(m);
}

void fog_paint(Map *m, Undo *u, int x, int y, int id)
{
    if (!map_in_bounds(m, x, y)) return;
    uint8_t want = (uint8_t)((unsigned)id & FOG_ID);
    if (fog_at(m, x, y) == want) return;
    undo_set_fog(u, m, x, y, want);
}

void fog_light(Map *m, Undo *u, int x, int y, int on)
{
    uint8_t f = fog_at(m, x, y);
    if (!(f & FOG_ID)) return;
    uint8_t want = on ? (uint8_t)(f | FOG_HELD | FOG_SEEN)
                      : (uint8_t)(f & ~(FOG_HELD | FOG_SEEN | FOG_LIT | FOG_RIM));
    if (want != f) undo_set_fog(u, m, x, y, want);
}

int fog_light_patch(Map *m, Undo *u, int id, int on)
{
    if (id < 1 || id > FOG_PATCH_MAX) return 0;
    const FogPatch *p = &m->fog_patches[id - 1];
    int changed = 0;
    if (p->x1 < p->x0) return 0;
    undo_begin(u);
    for (int y = p->y0; y <= p->y1; y++)
        for (int x = p->x0; x <= p->x1; x++) {
            uint8_t f = fog_at(m, x, y);
            if ((f & FOG_ID) != (uint8_t)id) continue;
            fog_light(m, u, x, y, on);
            changed += fog_at(m, x, y) != f;
        }
    undo_end(u);
    return changed;
}

void fog_forget(Map *m, int id)
{
    if (id < 1 || id > FOG_PATCH_MAX) return;
    const FogPatch *p = &m->fog_patches[id - 1];
    if (p->x1 < p->x0) return;
    for (int y = p->y0; y <= p->y1; y++)
        for (int x = p->x0; x <= p->x1; x++) {
            uint8_t *f = &m->fog[(size_t)y * (size_t)m->w + (size_t)x];
            if ((*f & FOG_ID) == (uint8_t)id && !(*f & FOG_HELD)) *f &= (uint8_t)~FOG_SEEN;
        }
    map_touch(m);
}

int fog_count(const Map *m, int id, int *seen)
{
    int n = 0, s = 0;
    const FogPatch *p = &m->fog_patches[id - 1];
    if (p->x1 >= p->x0)
        for (int y = p->y0; y <= p->y1; y++)
            for (int x = p->x0; x <= p->x1; x++) {
                uint8_t f = fog_at(m, x, y);
                if ((f & FOG_ID) != (uint8_t)id) continue;
                n++;
                s += (f & (FOG_SEEN | FOG_HELD | FOG_LIT)) != 0;
            }
    if (seen) *seen = s;
    return n;
}
