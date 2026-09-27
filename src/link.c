#include "link.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "fog.h"
#include "util.h"

static const struct {
    const char *name;
    uint32_t    glyph;
    char        ascii;
} LINK_INFO[LINK_KIND_COUNT] = {
    { "stairs",   0x2261u, '=' },   /* ≡ */
    { "ladder",   0x2021u, 'H' },   /* ‡ */
    { "trapdoor", 0x25A1u, '#' },   /* □ */
    { "portal",   0x25CEu, '@' },   /* ◎ */
};

const char *link_kind_name(uint8_t kind)
{
    return kind < LINK_KIND_COUNT ? LINK_INFO[kind].name : "link";
}

int link_kind_from_name(const char *name)
{
    for (int i = 0; i < LINK_KIND_COUNT; i++)
        if (!strcasecmp(name, LINK_INFO[i].name)) return i;
    return -1;
}

uint32_t link_glyph(uint8_t kind, int ascii)
{
    if (kind >= LINK_KIND_COUNT) return '?';
    return ascii ? (uint32_t)LINK_INFO[kind].ascii : LINK_INFO[kind].glyph;
}

void link_name(const Link *l, char *out, size_t outsz)
{
    snprintf(out, outsz, "%s %d", link_kind_name(l->kind), l->num);
}

void link_end_name(const Link *l, int end, char *out, size_t outsz)
{
    char a[MAP_COORD_MAX], b[MAP_COORD_MAX];
    map_coord_name(l->x[end], l->y[end], a, sizeof a);
    if (l->size == 1) { snprintf(out, outsz, "%s", a); return; }
    map_coord_name(l->x[end] + l->size - 1, l->y[end] + l->size - 1, b, sizeof b);
    snprintf(out, outsz, "%s-%s", a, b);
}

void link_status(const Map *m, int x, int y, int gm, char *out, size_t outsz)
{
    out[0] = '\0';
    int end, i = link_at(m, x, y, &end);
    if (i < 0) return;
    const Link *l = &m->links[i];
    if (l->secret && !gm) return;
    char name[32], there[2 * MAP_COORD_MAX + 2];
    link_name(l, name, sizeof name);
    /* Where it leads is a square's name, and the players are not told the
     * names of squares fog hides. */
    if (!gm && fog_ground_hidden(m, l->x[1 - end], l->y[1 - end])) {
        snprintf(out, outsz, "  %s", name);
        return;
    }
    link_end_name(l, 1 - end, there, sizeof there);
    snprintf(out, outsz, "  %s%s %s %s", l->secret ? "secret " : "", name,
             !l->oneway ? "to" : end == 0 ? "one-way to" : "one-way from", there);
}

void link_describe(const Link *l, char *out, size_t outsz)
{
    char name[32], a[2 * MAP_COORD_MAX + 2], b[2 * MAP_COORD_MAX + 2];
    link_name(l, name, sizeof name);
    link_end_name(l, 0, a, sizeof a);
    link_end_name(l, 1, b, sizeof b);
    snprintf(out, outsz, "%-12s %s %s %s%s%s%s", name, a, l->oneway ? "->" : "<->", b,
             l->size == 2 ? "  2x2" : l->size == 3 ? "  3x3" : "",
             l->oneway ? "  one-way" : "", l->secret ? "  secret" : "");
}

int link_find(const Map *m, int num)
{
    for (int i = 0; i < m->nlinks; i++)
        if (m->links[i].num == num) return i;
    return -1;
}

static int blocks_meet(int ax, int ay, int aw, int ah, int bx, int by, int bw, int bh)
{
    return ax < bx + bw && bx < ax + aw && ay < by + bh && by < ay + ah;
}

int link_meets(const Map *m, int x, int y, int w, int h, int *end)
{
    for (int i = 0; i < m->nlinks; i++) {
        const Link *l = &m->links[i];
        for (int e = 0; e < 2; e++) {
            if (blocks_meet(x, y, w, h, l->x[e], l->y[e], l->size, l->size)) {
                if (end) *end = e;
                return i;
            }
        }
    }
    return -1;
}

int link_free_num(const Map *m)
{
    if (m->nlinks >= MAP_LINKS_MAX) return 0;
    /* Not trusting the order: a stamp's preview appends its links unsorted. */
    uint8_t used[LINK_NUM_MAX + 1] = { 0 };
    for (int i = 0; i < m->nlinks; i++)
        if (m->links[i].num <= LINK_NUM_MAX) used[m->links[i].num] = 1;
    for (int n = 1; n <= LINK_NUM_MAX; n++)
        if (!used[n]) return n;
    return 0;
}

int link_void_square(const Map *m, const Link *l, int *vx, int *vy)
{
    for (int e = 0; e < 2; e++)
        for (int y = l->y[e]; y < l->y[e] + l->size; y++)
            for (int x = l->x[e]; x < l->x[e] + l->size; x++)
                if (map_tile(m, x, y) == TILE_VOID) { *vx = x; *vy = y; return 1; }
    return 0;
}

const char *link_problem(const Map *m, const Link *l)
{
    const char *why = link_misplaced(m, l);
    if (why) return why;
    int vx, vy;
    return link_void_square(m, l, &vx, &vy) ? "an end is on void" : NULL;
}

const char *link_misplaced(const Map *m, const Link *l)
{
    if (l->size < 1 || l->size > LINK_SIZE_MAX) return "a link's ends are 1, 2 or 3 squares across";
    if (l->kind >= LINK_KIND_COUNT)             return "not a kind of link";
    if (l->num < 1 || l->num > LINK_NUM_MAX)    return "links are numbered 1 to 99";
    int s = l->size;
    for (int e = 0; e < 2; e++) {
        if (l->x[e] < 0 || l->y[e] < 0 || l->x[e] + s > m->w || l->y[e] + s > m->h)
            return "an end is off the map";
    }
    if (blocks_meet(l->x[0], l->y[0], s, s, l->x[1], l->y[1], s, s))
        return "the two ends overlap";
    for (int i = 0; i < m->nlinks; i++) {
        const Link *o = &m->links[i];
        if (o->num == l->num) continue;
        for (int e = 0; e < 2; e++)
            for (int f = 0; f < 2; f++)
                if (blocks_meet(l->x[e], l->y[e], s, s, o->x[f], o->y[f], o->size, o->size))
                    return "another link is already there";
    }
    return NULL;
}

int link_put(Map *m, const Link *l)
{
    int i = link_find(m, l->num);
    if (i < 0) {
        if (m->nlinks >= MAP_LINKS_MAX) return -1;
        i = 0;
        while (i < m->nlinks && m->links[i].num < l->num) i++;
        memmove(&m->links[i + 1], &m->links[i], (size_t)(m->nlinks - i) * sizeof *l);
        m->nlinks++;
    }
    m->links[i] = *l;
    map_touch(m);
    return i;
}

int link_remove(Map *m, int num)
{
    int i = link_find(m, num);
    if (i < 0) return 0;
    memmove(&m->links[i], &m->links[i + 1], (size_t)(m->nlinks - i - 1) * sizeof m->links[0]);
    m->nlinks--;
    map_touch(m);
    return 1;
}

static int going(const LinkTrip *t, int idx)
{
    for (int i = 0; i < t->n; i++)
        if (t->idx[i] == idx) return 1;
    return 0;
}

int link_trip(const Map *m, int li, int from, int enforce, LinkTrip *t)
{
    memset(t, 0, sizeof *t);
    const Link *l = &m->links[li];
    char name[32];
    link_name(l, name, sizeof name);
    if (l->oneway && from != 0) {
        char at[MAP_COORD_MAX];
        map_coord_name(l->x[0], l->y[0], at, sizeof at);
        snprintf(t->why, sizeof t->why, "%s is one-way: it is taken from %s", name, at);
        return 0;
    }

    int s = l->size, to = 1 - from;
    for (int i = 0; i < m->tokens.n; i++) {
        if (!token_meets(&m->tokens.v[i], l->x[from], l->y[from], s, s)) continue;
        if (t->n == LINK_TRIP_MAX) {
            snprintf(t->why, sizeof t->why, "more than %d creatures on %s", LINK_TRIP_MAX, name);
            t->n = 0;
            return 0;
        }
        t->idx[t->n++] = i;
    }
    if (!t->n) {
        snprintf(t->why, sizeof t->why, "nobody on this end of %s", name);
        return 0;
    }
    t->dx = l->x[to] - l->x[from];
    t->dy = l->y[to] - l->y[from];

    for (int k = 0; k < t->n; k++) {
        const Token *tok = &m->tokens.v[t->idx[k]];
        int nx = tok->x + t->dx, ny = tok->y + t->dy, ts = tok->size;
        for (int y = ny; y < ny + ts; y++) {
            for (int x = nx; x < nx + ts; x++) {
                char at[MAP_COORD_MAX];
                map_coord_name(x, y, at, sizeof at);
                if (!map_in_bounds(m, x, y)) {
                    snprintf(t->why, sizeof t->why, "%.30s would land off the map", token_name(tok));
                    t->n = 0;
                    return 0;
                }
                if (!enforce) continue;
                if (map_tile(m, x, y) == TILE_VOID) {
                    snprintf(t->why, sizeof t->why, "%.30s would land on void at %s", token_name(tok), at);
                    t->n = 0;
                    return 0;
                }
                int o = -1;
                for (int j = 0; j < m->tokens.n && o < 0; j++)
                    if (!going(t, j) && token_meets(&m->tokens.v[j], x, y, 1, 1)) o = j;
                if (o >= 0) {
                    snprintf(t->why, sizeof t->why, "%s is taken by %.30s",
                             at, token_name(&m->tokens.v[o]));
                    t->n = 0;
                    return 0;
                }
            }
        }
    }
    return t->n;
}
