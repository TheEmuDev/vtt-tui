#include "link.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include "floor.h"
#include "mapio.h"
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

int link_map_name_ok(const char *name)
{
    size_t n = strlen(name);
    if (n == 0 || n >= LINK_MAP_MAX || name[0] == '.' || name[0] == ' ' || name[n - 1] == ' ') return 0;
    return strpbrk(name, "\"/\\") == NULL;
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
    /* Another map: the GM hears where it goes; the players only its name,
     * since a map's file name and an area's are the GM's. */
    if (l->to_map[0]) {
        if (gm) snprintf(out, outsz, "  %s%s to %s, %s", l->secret ? "secret " : "", name, l->to_map, l->to_place);
        else    snprintf(out, outsz, "  %s", name);    /* a file's name can spoil: "dragon-lair" */
        return;
    }
    /* Where it leads is a square's name, and the players are not told the
     * names of squares fog hides. */
    if (!gm && fog_ground_hidden(m, l->x[1 - end], l->y[1 - end])) {
        snprintf(out, outsz, "  %s", name);
        return;
    }
    link_end_name(l, 1 - end, there, sizeof there);
    /* Another floor is named, on the GM's line (an area's name is the GM's):
     * under stacked layers a square's name alone would not say where. */
    int ff = floor_at(m, l->x[1 - end], l->y[1 - end]);
    char fl[AREA_NAME_MAX + 2] = "";
    if (gm && ff >= 0 && ff != floor_at(m, l->x[end], l->y[end])) snprintf(fl, sizeof fl, "%s ", m->areas[ff].name);
    snprintf(out, outsz, "  %s%s %s %s%s", l->secret ? "secret " : "", name,
             !l->oneway ? "to" : end == 0 ? "one-way to" : "one-way from", fl, there);
}

void link_describe(const Link *l, char *out, size_t outsz)
{
    char name[32], a[2 * MAP_COORD_MAX + 2], b[2 * MAP_COORD_MAX + 2];
    link_name(l, name, sizeof name);
    link_end_name(l, 0, a, sizeof a);
    if (l->to_map[0]) {
        snprintf(out, outsz, "%-12s %s -> %s, %s%s%s", name, a, l->to_map, l->to_place,
                 l->size == 2 ? "  2x2" : l->size == 3 ? "  3x3" : "", l->secret ? "  secret" : "");
        return;
    }
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
    if (l->to_map[0] && (!link_map_name_ok(l->to_map) || !l->to_place[0]))
        return "a link to another map names the map and a place in it";
    int s = l->size, ends = link_ends(l);
    for (int e = 0; e < ends; e++) {
        if (l->x[e] < 0 || l->y[e] < 0 || l->x[e] + s > m->w || l->y[e] + s > m->h)
            return "an end is off the map";
    }
    if (ends == 2 && blocks_meet(l->x[0], l->y[0], s, s, l->x[1], l->y[1], s, s))
        return "the two ends overlap";
    for (int i = 0; i < m->nlinks; i++) {
        const Link *o = &m->links[i];
        if (o->num == l->num) continue;
        for (int e = 0; e < ends; e++)
            for (int f = 0; f < link_ends(o); f++)
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
    if (l->to_map[0]) {
        snprintf(t->why, sizeof t->why, "%s leads to another map", name);
        return 0;
    }
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

/* ------------------------------------------------------- another map */

/* The file a link to another map names: beside this map's own. 0 when this
 * map has no file to be beside. */
int link_map_path(const Map *m, const char *to_map, char *buf, size_t sz)
{
    /* A map that has never been written has nothing to be beside yet. */
    struct stat st;
    if (!m->path[0] || stat(m->path, &st) != 0) return 0;
    if (sz > MAP_PATH_MAX) sz = MAP_PATH_MAX;         /* what a loaded map can hold as its path */
    const char *slash = strrchr(m->path, '/');
    int dl = slash ? (int)(slash - m->path) : 0;
    int n = slash ? snprintf(buf, sz, "%.*s/%s.vtt", dl, m->path, to_map) : snprintf(buf, sz, "%s.vtt", to_map);
    return n > 0 && (size_t)n < sz;
}

/* Checks a link to another map could be made: the file there, and the place
 * in it an area or a square on ground. NULL, or why not in buf. */
const char *link_map_check(const Map *m, const char *to_map, const char *place, char *buf, size_t sz)
{
    char path[MAP_PATH_MAX + 32], err[MAPIO_ERR_MAX];
    if (!link_map_name_ok(to_map)) {
        snprintf(buf, sz, "%.30s: a map's name is its file's without .vtt, under %d characters", to_map, LINK_MAP_MAX);
        return buf;
    }
    if (!link_map_path(m, to_map, path, sizeof path)) {
        snprintf(buf, sz, "save this map first (:w NAME) - the other map is found beside it");
        return buf;
    }
    Map *d = mapio_load(path, err, sizeof err);
    if (!d) { snprintf(buf, sz, "no map %.30s beside this one", to_map); return buf; }
    const char *why = NULL;
    int x, y, ai = map_area_find(d, place);
    if (ai < 0) {
        if (!map_coord_parse(place, &x, &y) || !map_in_bounds(d, x, y))
            { snprintf(buf, sz, "%.30s has no area or square called %.31s", to_map, place); why = buf; }
        else if (map_tile(d, x, y) == TILE_VOID)
            { snprintf(buf, sz, "%.31s in %.30s is void", place, to_map); why = buf; }
    }
    map_free(d);
    return why;
}


static int lands(const Map *m, const Token *party, int n, int ax, int ay)
{
    for (int i = 0; i < n; i++) {
        int x0 = ax + party[i].x, y0 = ay + party[i].y, s = party[i].size;
        for (int y = y0; y < y0 + s; y++)
            for (int x = x0; x < x0 + s; x++)
                if (!map_in_bounds(m, x, y) || map_tile(m, x, y) == TILE_VOID) return 0;
        if (tokens_overlapping(&m->tokens, x0, y0, s, -1, TOKEN_ANY_KIND) >= 0) return 0;
    }
    return 1;
}

int link_land(const Map *dst, const char *place, const Token *party, int n,
              int *ax, int *ay, char *why, size_t whysz)
{
    /* The formation's box, from the end's corner. */
    int fx0 = 0, fy0 = 0, fx1 = 0, fy1 = 0;
    for (int i = 0; i < n; i++) {
        if (i == 0 || party[i].x < fx0) fx0 = party[i].x;
        if (i == 0 || party[i].y < fy0) fy0 = party[i].y;
        if (i == 0 || party[i].x + party[i].size - 1 > fx1) fx1 = party[i].x + party[i].size - 1;
        if (i == 0 || party[i].y + party[i].size - 1 > fy1) fy1 = party[i].y + party[i].size - 1;
    }
    int bx0, by0, bx1, by1, inside;
    int ai = map_area_find(dst, place), px, py;
    if (ai >= 0) {
        const Area *ar = &dst->areas[ai];
        bx0 = ar->x0; by0 = ar->y0; bx1 = ar->x1; by1 = ar->y1;
        inside = 1;
    } else if (map_coord_parse(place, &px, &py) && map_in_bounds(dst, px, py)) {
        bx0 = bx1 = px; by0 = by1 = py;
        inside = 0;
    } else {
        snprintf(why, whysz, "%.24s has no area or square called %.31s", dst->name, place);
        return 0;
    }
    /* Twice the middles, so a box of even width has a middle between squares. */
    long mx = bx0 + bx1, my = by0 + by1, best = -1;
    int  lo_x = inside ? bx0 - fx0 : 0 - fx0, hi_x = inside ? bx1 - fx1 : dst->w - 1 - fx1;
    int  lo_y = inside ? by0 - fy0 : 0 - fy0, hi_y = inside ? by1 - fy1 : dst->h - 1 - fy1;
    for (int y = lo_y; y <= hi_y; y++)
        for (int x = lo_x; x <= hi_x; x++) {
            long cx = 2L * x + fx0 + fx1 - mx, cy = 2L * y + fy0 + fy1 - my;
            long d = cx * cx + cy * cy;
            if (best >= 0 && d >= best) continue;
            if (!lands(dst, party, n, x, y)) continue;
            best = d; *ax = x; *ay = y;
        }
    if (best < 0) {
        if (inside) snprintf(why, whysz, "no room in %.24s's %.31s for %d creature%s as they stand",
                             dst->name, place, n, n == 1 ? "" : "s");
        else        snprintf(why, whysz, "no room in %.24s for %d creature%s as they stand",
                             dst->name, n, n == 1 ? "" : "s");
        return 0;
    }
    return 1;
}
