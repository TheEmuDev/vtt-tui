#include "fog.h"

#include <ctype.h>
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

static void lit_rect(Map *m, int x0, int y0, int x1, int y1)
{
    if (m->fog_nlit < FOG_LIT_RECTS) {
        int16_t *r = m->fog_lit[m->fog_nlit++];
        r[0] = (int16_t)x0; r[1] = (int16_t)y0; r[2] = (int16_t)x1; r[3] = (int16_t)y1;
        return;
    }
    /* Out of room: fold into the last, which only ever makes it bigger. */
    int16_t *r = m->fog_lit[FOG_LIT_RECTS - 1];
    if (x0 < r[0]) r[0] = (int16_t)x0;
    if (y0 < r[1]) r[1] = (int16_t)y0;
    if (x1 > r[2]) r[2] = (int16_t)x1;
    if (y1 > r[3]) r[3] = (int16_t)y1;
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

void fog_recompute(Map *m)
{
    PROF_ZONE("fog.sight");
    const uint8_t keep = (uint8_t)~(FOG_LIT | FOG_RIM);
    for (int i = 0; i < m->fog_nlit; i++) {
        const int16_t *r = m->fog_lit[i];
        for (int y = r[1]; y <= r[3]; y++)
            for (int x = r[0]; x <= r[2]; x++)
                m->fog[(size_t)y * (size_t)m->w + (size_t)x] &= keep;
    }
    m->fog_nlit = 0;
    if (!m->fog_on) return;

    /* Light. For each player creature and each patch that lights itself,
     * the box its reveal can reach, cut to the patch's extent first. */
    for (int ti = 0; ti < m->tokens.n; ti++) {
        const Token *t = &m->tokens.v[ti];
        if (t->kind != TOKEN_PLAYER) continue;
        int tx1 = t->x + t->size - 1, ty1 = t->y + t->size - 1;

        for (int id = 1; id <= FOG_PATCH_MAX; id++) {
            const FogPatch *p = &m->fog_patches[id - 1];
            if (!fog_patch_live(m, id) || p->reveal < 0 || p->x1 < p->x0) continue;
            int n = p->reveal;
            int x0 = imax(imax(t->x - n, p->x0), 0), x1 = imin(imin(tx1 + n, p->x1), m->w - 1);
            int y0 = imax(imax(t->y - n, p->y0), 0), y1 = imin(imin(ty1 + n, p->y1), m->h - 1);
            if (x0 > x1 || y0 > y1) continue;
            lit_rect(m, imax(x0 - 1, 0), imax(y0 - 1, 0), imin(x1 + 1, m->w - 1), imin(y1 + 1, m->h - 1));
            /* Every line runs inside the box that holds both the creature
             * and its reach; if nothing in there stops sight, none does. */
            int open = !rect_opaque(m, imin(x0, t->x), imin(y0, t->y), imax(x1, tx1), imax(y1, ty1));

            for (int y = y0; y <= y1; y++)
                for (int x = x0; x <= x1; x++) {
                    uint8_t *f = &m->fog[(size_t)y * (size_t)m->w + (size_t)x];
                    if ((*f & FOG_ID) != (uint8_t)id || (*f & FOG_LIT)) continue;
                    /* Distance from the creature's nearest square, in the
                     * map's metric, so fog agrees with the ruler. */
                    int ox = iclamp(x, t->x, tx1), oy = iclamp(y, t->y, ty1);
                    if (dist_tiles((DistMetric)m->metric, x - ox, y - oy) > (double)n + 1e-9) continue;
                    int seen = open || !sight_blocked(m, ox, oy, x, y);
                    for (int fy = t->y; fy <= ty1 && !seen; fy++)
                        for (int fx = t->x; fx <= tx1 && !seen; fx++)
                            if (fx != ox || fy != oy) seen = !sight_blocked(m, fx, fy, x, y);
                    if (!seen) continue;
                    *f |= FOG_LIT;
                    if (p->memory) *f |= FOG_SEEN;
                }
        }
    }

    /* The rim: fog still hiding, beside a lit square it can see. Worked out
     * here, once a move, so drawing it is one bit. */
    for (int i = 0; i < m->fog_nlit; i++) {
        const int16_t *r = m->fog_lit[i];
        for (int y = r[1]; y <= r[3]; y++)
            for (int x = r[0]; x <= r[2]; x++) {
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
