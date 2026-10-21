#include "maptools.h"

#include <stdarg.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "clock.h"
#include "mapio.h"
#include "dice.h"
#include "card.h"
#include "floor.h"
#include "fog.h"
#include "json.h"
#include "link.h"
#include "ruler.h"
#include "prof.h"
#include "util.h"

/* ---------------------------------------------------------------- naming */

/* A column's letters alone: "A", "AB". */
static void col_name(int x, char *buf, size_t sz)
{
    char full[MAP_COORD_MAX];
    map_coord_name(x, 0, full, sizeof full);
    size_t n = 0;
    while (full[n] >= 'A' && full[n] <= 'Z') n++;
    if (n >= sz) n = sz - 1;
    memcpy(buf, full, n);
    buf[n] = '\0';
}

static int digits(int v)
{
    int d = 1;
    while (v >= 10) { v /= 10; d++; }
    return d;
}

/* A report names an unlabeled creature as such: a reader checking a map
 * wants to know the label is missing, not which side it is on. */
static const char *label_or_unnamed(const Token *t) { return t->label[0] ? t->label : "(unnamed)"; }

/* The glyph a creature's squares show, in file order: 1-9, a-z, A-Z, then
 * '#' for the rest, which the legend still lists. */
static char token_glyph(int i)
{
    static const char g[] = "123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
    return i < (int)sizeof g - 1 ? g[i] : '#';
}

/* A square's fog as the file writes it. */
static char fog_char(const Map *m, int x, int y)
{
    static const char held[FOG_PATCH_MAX + 1] = "123456789!\"#$%&";
    uint8_t f  = fog_at(m, x, y);
    int     id = f & FOG_ID;
    if (!id || !m->fog_patches[id - 1].name[0] || m->fog_patches[id - 1].dead) return '.';
    if (f & FOG_HELD) return held[id - 1];
    return (char)((f & FOG_SEEN ? 'a' : 'A') + id - 1);
}

/* ------------------------------------------------------------------ dump */

/* The boundary characters, the file's own, with a horizontal wall as '-'. */
static char vedge_char(const Map *m, int x, int y)
{
    uint8_t k = map_vedge(m, x, y);
    return k == EDGE_NONE ? ' ' : edge_file_char(k);
}

static char hedge_char(const Map *m, int x, int y)
{
    uint8_t k = map_hedge(m, x, y);
    return k == EDGE_NONE ? ' ' : k == EDGE_WALL ? '-' : edge_file_char(k);
}

/* A lattice corner takes the line running through it, so walls read as
 * lines: '-' where a horizontal boundary meets it, else '|' where a
 * vertical one does. Never '+', which is a door. */
static char corner_char(const Map *m, int cx, int cy)
{
    int h = (cx > 0 && map_hedge(m, cx - 1, cy) != EDGE_NONE) ||
            (cx < m->w && map_hedge(m, cx, cy) != EDGE_NONE);
    if (h) return '-';
    int v = (cy > 0 && map_vedge(m, cx, cy - 1) != EDGE_NONE) ||
            (cy < m->h && map_vedge(m, cx, cy) != EDGE_NONE);
    return v ? '|' : ' ';
}

static void header_rows(FILE *out, int gutter, int x0, int x1, int prefix_pitch)
{
    /* Two rows when any column in view has two letters: the first letter
     * above, the last below, so every column is labeled at its own pitch. */
    char name[8];
    col_name(x1, name, sizeof name);
    int two = strlen(name) > 1;
    for (int row = two ? 0 : 1; row < 2; row++) {
        fprintf(out, "%*s", gutter, "");
        if (prefix_pitch == 2) fputc(' ', out);
        for (int x = x0; x <= x1; x++) {
            col_name(x, name, sizeof name);
            size_t n = strlen(name);
            char   c = row == 1 ? name[n - 1] : (n > 1 ? name[0] : ' ');
            fputc(c, out);
            if (prefix_pitch == 2 && x < x1) fputc(' ', out);
        }
        fputc('\n', out);
    }
}

static int token_touches(const Token *t, int x0, int y0, int x1, int y1)
{
    return t->x <= x1 && t->x + t->size - 1 >= x0 && t->y <= y1 && t->y + t->size - 1 >= y0;
}

void maptools_dump_key(FILE *out)
{
    fputs("key\n"
          "  .  floor   ~  water   :  rough   \"  brush   =  wood   ^  hazard   (blank) void\n"
          "  | -  wall   +  door   /  open door   %  window   S  secret door   s  open secret door\n"
          "  S and s are the GM's: the players see a wall and an open door\n", out);
}

void maptools_dump(FILE *out, const Map *m, int x0, int y0, int x1, int y1)
{
    maptools_dump_parts(out, m, x0, y0, x1, y1, DUMP_KEY | DUMP_FOOT);
}

void maptools_dump_parts(FILE *out, const Map *m, int x0, int y0, int x1, int y1, unsigned parts)
{
    x0 = iclamp(x0, 0, m->w - 1); x1 = iclamp(x1, x0, m->w - 1);
    y0 = iclamp(y0, 0, m->h - 1); y1 = iclamp(y1, y0, m->h - 1);
    int gutter = digits(y1 + 1) + 1;

    /* Which creature shows on each square in view: the newest on top, as
     * the app draws them. */
    int vw = x1 - x0 + 1, vh = y1 - y0 + 1;
    int *on = xcalloc((size_t)vw * (size_t)vh, sizeof *on);
    for (int i = 0; i < m->tokens.n; i++) {
        const Token *t = &m->tokens.v[i];
        for (int y = t->y; y < t->y + t->size; y++)
            for (int x = t->x; x < t->x + t->size; x++)
                if (x >= x0 && x <= x1 && y >= y0 && y <= y1)
                    on[(size_t)(y - y0) * (size_t)vw + (size_t)(x - x0)] = i + 1;
    }

    /* One line at a time, trailing blanks trimmed: a dump pasted into a
     * file or a prompt should not carry invisible tails. */
    char *row = xmalloc((size_t)(2 * vw + 2) + (size_t)gutter + 2);
    header_rows(out, gutter, x0, x1, 2);
    for (int ly = 2 * y0; ly <= 2 * y1 + 2; ly++) {
        int y = ly / 2;
        int n = (ly & 1) ? snprintf(row, (size_t)gutter + 1, "%*d ", gutter - 1, y + 1)
                         : snprintf(row, (size_t)gutter + 1, "%*s", gutter, "");
        for (int lx = 2 * x0; lx <= 2 * x1 + 2; lx++) {
            int  x = lx / 2;
            char c;
            if (!(ly & 1) && !(lx & 1))  c = corner_char(m, x, y);
            else if (!(ly & 1))          c = hedge_char(m, x, y);
            else if (!(lx & 1))          c = vedge_char(m, x, y);
            else {
                int t = on[(size_t)(y - y0) * (size_t)vw + (size_t)(x - x0)];
                c = t ? token_glyph(t - 1) : tile_file_char(map_tile(m, x, y));
            }
            row[n++] = c;
        }
        while (n > 0 && row[n - 1] == ' ') n--;
        fwrite(row, 1, (size_t)n, out);
        fputc('\n', out);
    }
    free(row);
    free(on);

    /* Creatures, notes, the key, fog and the map's settings: each only
     * where it touches the region shown. */
    char at[MAP_COORD_MAX], to[MAP_COORD_MAX];
    int  shown = 0;
    for (int i = 0; i < m->tokens.n; i++) {
        const Token *t = &m->tokens.v[i];
        if (!token_touches(t, x0, y0, x1, y1)) continue;
        if (!shown++) fputs("\ncreatures\n", out);
        map_coord_name(t->x, t->y, at, sizeof at);
        char where[2 * MAP_COORD_MAX + 2], kind[16];
        if (t->size > 1) {
            map_coord_name(t->x + t->size - 1, t->y + t->size - 1, to, sizeof to);
            snprintf(where, sizeof where, "%s-%s", at, to);
            snprintf(kind, sizeof kind, "%s %dx%d", t->kind == TOKEN_ENEMY ? "enemy" : "player", t->size, t->size);
        } else {
            str_lcpy(where, at, sizeof where);
            str_lcpy(kind, t->kind == TOKEN_ENEMY ? "enemy" : "player", sizeof kind);
        }
        fprintf(out, "  %c  %-16s %-12s %s%s\n", token_glyph(i), label_or_unnamed(t), kind, where,
                t->hidden ? "  hidden" : "");
        if (t->note[0]) fprintf(out, "     note: %s\n", t->note);
    }

    shown = 0;
    for (int i = 0; i < m->nnotes; i++) {
        const Note *n = &m->notes[i];
        if (n->x < x0 || n->x > x1 || n->y < y0 || n->y > y1) continue;
        if (!shown++) fputs("\nnotes\n", out);
        map_coord_name(n->x, n->y, at, sizeof at);
        fprintf(out, "  %-5s %s\n", at, n->text);
    }

    if (m->env[0]) fprintf(out, "\nenvironment %s\n", m->env);
    shown = 0;
    for (int i = 0; i < m->nareas; i++) {
        const Area *ar = &m->areas[i];
        if (ar->x1 < x0 || ar->x0 > x1 || ar->y1 < y0 || ar->y0 > y1) continue;
        char b0[MAP_COORD_MAX], b1[MAP_COORD_MAX];
        if (!shown++) fputs("\nareas\n", out);
        map_coord_name(ar->x0, ar->y0, b0, sizeof b0);
        map_coord_name(ar->x1, ar->y1, b1, sizeof b1);
        fprintf(out, "  %-16s %s:%s%s%s\n", ar->name, b0, b1, ar->env[0] ? "  environment " : "", ar->env);
    }

    shown = 0;
    for (int i = 0; i < m->nlinks; i++) {
        const Link *l = &m->links[i];
        int in = 0;
        for (int e = 0; e < 2; e++)
            in |= l->x[e] <= x1 && l->x[e] + l->size - 1 >= x0 && l->y[e] <= y1 && l->y[e] + l->size - 1 >= y0;
        if (!in) continue;
        if (!shown++) fputs("\nlinks\n", out);
        char line[128];
        link_describe(l, line, sizeof line);
        fprintf(out, "  %s\n", line);
    }

    if (parts & DUMP_KEY) { fputc('\n', out); maptools_dump_key(out); }

    int patches = 0;
    for (int i = 0; i < FOG_PATCH_MAX; i++)
        patches += m->fog_patches[i].name[0] && !m->fog_patches[i].dead;
    if (patches) {
        fprintf(out, "\nfog %s, soft edge %s\n", m->fog_on ? "on" : "off", m->fog_soft_edge ? "on" : "off");
        for (int i = 0; i < FOG_PATCH_MAX; i++) {
            const FogPatch *p = &m->fog_patches[i];
            if (!p->name[0] || p->dead) continue;
            int  seen, tiles = fog_count(m, i + 1, &seen);
            char rev[12];
            if (p->reveal == FOG_REVEAL_MANUAL) str_lcpy(rev, "manual", sizeof rev);
            else                                snprintf(rev, sizeof rev, "%d", p->reveal);
            fprintf(out, "  %c  %-16s reveal %-6s memory %-3s soft edge %-3s %d squares, %d seen%s\n",
                    'A' + i, p->name, rev, p->memory ? "on" : "off",
                    p->soft_edge < 0 ? "map" : p->soft_edge ? "on" : "off", tiles, seen,
                    p->disabled ? ", disabled" : "");
        }
        fputs("  . no fog   A-O a patch, unseen   a-o seen   1-9 !\"#$%& lit by hand\n\n", out);
        header_rows(out, gutter, x0, x1, 1);
        for (int y = y0; y <= y1; y++) {
            fprintf(out, "%*d ", gutter - 1, y + 1);
            for (int x = x0; x <= x1; x++) fputc(fog_char(m, x, y), out);
            fputc('\n', out);
        }
    }

    if (!(parts & DUMP_FOOT)) return;
    fprintf(out, "\nmap  %s  %dx%d  scale %g ft  metric %s  ruleset %s\n", m->name, m->w, m->h,
            m->scale_ft, dist_metric_name((DistMetric)m->metric), m->ruleset[0] ? m->ruleset : "none");
    if (x0 > 0 || y0 > 0 || x1 < m->w - 1 || y1 < m->h - 1) {
        map_coord_name(x0, y0, at, sizeof at);
        map_coord_name(x1, y1, to, sizeof to);
        fprintf(out, "region %s:%s\n", at, to);
    }
}

int maptools_region(const Map *m, const char *spec, int *x0, int *y0, int *x1, int *y1)
{
    char a[16] = "", b[16] = "";
    const char *colon = strchr(spec, ':');
    size_t la = colon ? (size_t)(colon - spec) : strlen(spec);
    if (!la || la >= sizeof a) return 0;
    memcpy(a, spec, la);
    a[la] = '\0';
    str_lcpy(b, colon ? colon + 1 : a, sizeof b);
    int ax = 0, ay = 0, bx = m->w - 1, by = m->h - 1;
    if (!map_coord_parse(a, &ax, &ay) || !map_coord_parse(b, &bx, &by)) return 0;
    *x0 = imin(ax, bx); *x1 = imax(ax, bx);
    *y0 = imin(ay, by); *y1 = imax(ay, by);
    return 1;
}

/* ----------------------------------------------------------------- rooms */

void maptools_edge_name(const Map *m, int vertical, int x, int y, char *buf, size_t sz)
{
    char a[MAP_COORD_MAX] = "-", b[MAP_COORD_MAX] = "-";
    int ax = vertical ? x - 1 : x, ay = vertical ? y : y - 1;
    if (map_in_bounds(m, ax, ay)) map_coord_name(ax, ay, a, sizeof a);
    if (map_in_bounds(m, x, y))   map_coord_name(x, y, b, sizeof b);
    snprintf(buf, sz, "%s%c%s", a, vertical ? '|' : '/', b);
}

/* Does a boundary join rooms for reachability? Doors of every kind do --
 * a closed one can be opened, a secret one found; a window does not. */
static int edge_passable_kind(uint8_t k)
{
    return k == EDGE_DOOR_CLOSED || k == EDGE_DOOR_OPEN || k == EDGE_SECRET_CLOSED || k == EDGE_SECRET_OPEN;
}

void rooms_build(const Map *m, Rooms *r)
{
    memset(r, 0, sizeof *r);
    size_t n = (size_t)m->w * (size_t)m->h;
    r->at    = xmalloc(n * sizeof *r->at);
    r->start = -1;
    for (size_t i = 0; i < n; i++) r->at[i] = -1;

    /* A flood fill with an explicit stack: no recursion, whatever the map.
     * It visits every square of ground, so it reads the map's arrays
     * straight -- a square is its tile and the four edge bytes round it --
     * and keeps x and y on the stack rather than dividing an index back
     * into them: through map_tile and map_vedge's bounds checks this walk
     * was 5.5 ms of a 512x512 map's describe (docs/PERFORMANCE.md). */
    PROF_ZONE("rooms.build");
    const uint8_t *T = m->tiles, *V = m->vedges, *H = m->hedges;
    const int      W = m->w, HH = m->h, VW = m->w + 1;
    int32_t *stack = xmalloc(n * sizeof *stack);
    int      cap = 0;
    for (int y = 0; y < m->h; y++)
        for (int x = 0; x < m->w; x++) {
            size_t i = (size_t)y * (size_t)m->w + (size_t)x;
            if (r->at[i] >= 0 || T[i] == TILE_VOID) continue;
            if (r->n == cap) { cap = cap ? cap * 2 : 16; r->v = xrealloc(r->v, (size_t)cap * sizeof *r->v); }
            Room *rm = &r->v[r->n];
            rm->x0 = rm->x1 = rm->fx = x;
            rm->y0 = rm->y1 = rm->fy = y;
            rm->squares = 0;
            int sp = 0;
            const int id = r->n;
            int32_t  *at = r->at;
            stack[sp++] = (int32_t)(x | y << 16);            /* MAP_MAX_DIM is well under 65536 */
            at[i] = id;
            while (sp) {
                int32_t p = stack[--sp];
                int kx = p & 0xffff, ky = p >> 16, k = ky * W + kx;
                rm->squares++;
                if (kx < rm->x0) rm->x0 = kx;
                if (kx > rm->x1) rm->x1 = kx;
                if (ky < rm->y0) rm->y0 = ky;
                if (ky > rm->y1) rm->y1 = ky;
                /* East, west, south, north: on the map, ground, not taken, and
                 * no edge between. Written out four times on purpose: one
                 * step in a helper (tried, with the review that asked for
                 * it) made this walk a third dearer, 2.2 to 3.0 ms on the
                 * largest map, the compiler no longer keeping the tests in
                 * the order that reads least. */
                if (kx + 1 < W && T[k + 1] != TILE_VOID && at[k + 1] < 0 && V[ky * VW + kx + 1] == EDGE_NONE) {
                    at[k + 1] = id;
                    stack[sp++] = (int32_t)((kx + 1) | ky << 16);
                }
                if (kx > 0 && T[k - 1] != TILE_VOID && at[k - 1] < 0 && V[ky * VW + kx] == EDGE_NONE) {
                    at[k - 1] = id;
                    stack[sp++] = (int32_t)((kx - 1) | ky << 16);
                }
                if (ky + 1 < HH && T[k + W] != TILE_VOID && at[k + W] < 0 && H[k + W] == EDGE_NONE) {
                    at[k + W] = id;
                    stack[sp++] = (int32_t)(kx | (ky + 1) << 16);
                }
                if (ky > 0 && T[k - W] != TILE_VOID && at[k - W] < 0 && H[k] == EDGE_NONE) {
                    at[k - W] = id;
                    stack[sp++] = (int32_t)(kx | (ky - 1) << 16);
                }
            }
            r->n++;
        }
    free(stack);

    /* The start: the first player creature's room, else the largest. */
    for (int i = 0; i < m->tokens.n && r->start < 0; i++)
        if (m->tokens.v[i].kind == TOKEN_PLAYER) r->start = rooms_at(r, m, m->tokens.v[i].x, m->tokens.v[i].y);
    for (int i = 0; i < r->n && r->start < 0; i++) {
        int best = 0;
        for (int j = 1; j < r->n; j++) if (r->v[j].squares > r->v[best].squares) best = j;
        r->start = best;
    }

    /* Reachability through doors: the door edges as an adjacency list, then
     * a breadth-first walk from the start. */
    r->reach = xcalloc((size_t)(r->n ? r->n : 1), 1);
    if (r->start < 0) return;
    int ne = 0, ecap = 0;
    int (*pairs)[2] = NULL;
    /* Every edge is looked at, and nearly all are none: the byte first. */
    for (int y = 0; y < m->h; y++)
        for (int x = 1; x < m->w; x++) {
            uint8_t e = V[y * VW + x];
            if (e != EDGE_NONE && edge_passable_kind(e)) {
                int a = rooms_at(r, m, x - 1, y), b = rooms_at(r, m, x, y);
                if (a >= 0 && b >= 0 && a != b) {
                    if (ne == ecap) { ecap = ecap ? ecap * 2 : 16; pairs = xrealloc(pairs, (size_t)ecap * sizeof *pairs); }
                    pairs[ne][0] = a; pairs[ne][1] = b; ne++;
                }
            }
        }
    for (int y = 1; y < m->h; y++)
        for (int x = 0; x < m->w; x++)
            if (H[y * W + x] != EDGE_NONE && edge_passable_kind(H[y * W + x])) {
                int a = rooms_at(r, m, x, y - 1), b = rooms_at(r, m, x, y);
                if (a >= 0 && b >= 0 && a != b) {
                    if (ne == ecap) { ecap = ecap ? ecap * 2 : 16; pairs = xrealloc(pairs, (size_t)ecap * sizeof *pairs); }
                    pairs[ne][0] = a; pairs[ne][1] = b; ne++;
                }
            }
    /* A two-way link joins the rooms its ends stand in as a door would; a
     * one-way one only onward, so it waits for the walk below. */
    for (int i = 0; i < m->nlinks; i++) {
        const Link *l = &m->links[i];
        if (l->oneway) continue;
        for (int k = 0; k < l->size * l->size; k++)
            for (int q = 0; q < l->size * l->size; q++) {
                int a = rooms_at(r, m, l->x[0] + k % l->size, l->y[0] + k / l->size);
                int b = rooms_at(r, m, l->x[1] + q % l->size, l->y[1] + q / l->size);
                if (a < 0 || b < 0 || a == b) continue;
                if (ne == ecap) { ecap = ecap ? ecap * 2 : 16; pairs = xrealloc(pairs, (size_t)ecap * sizeof *pairs); }
                pairs[ne][0] = a; pairs[ne][1] = b; ne++;
            }
    }
    int *deg = xcalloc((size_t)r->n + 1, sizeof *deg);
    for (int i = 0; i < ne; i++) { deg[pairs[i][0] + 1]++; deg[pairs[i][1] + 1]++; }
    for (int i = 0; i < r->n; i++) deg[i + 1] += deg[i];
    int *adj = xmalloc((size_t)(2 * ne + 1) * sizeof *adj);
    int *fill = xcalloc((size_t)r->n + 1, sizeof *fill);
    for (int i = 0; i < ne; i++) {
        adj[deg[pairs[i][0]] + fill[pairs[i][0]]++] = pairs[i][1];
        adj[deg[pairs[i][1]] + fill[pairs[i][1]]++] = pairs[i][0];
    }
    int *queue = xmalloc((size_t)r->n * sizeof *queue);
    int  qh = 0, qt = 0;
    queue[qt++] = r->start;
    r->reach[r->start] = 1;
    for (;;) {
        while (qh < qt) {
            int a = queue[qh++];
            for (int k = deg[a]; k < deg[a + 1]; k++)
                if (!r->reach[adj[k]]) { r->reach[adj[k]] = 1; queue[qt++] = adj[k]; }
        }
        /* One-way links out of what the walk reached lead on; the walk
         * carries on from each room one lands in, until nothing new. */
        int more = 0;
        for (int i = 0; i < m->nlinks; i++) {
            const Link *l = &m->links[i];
            if (!l->oneway) continue;
            int from = 0;
            for (int k = 0; k < l->size * l->size && !from; k++) {
                int a = rooms_at(r, m, l->x[0] + k % l->size, l->y[0] + k / l->size);
                from = a >= 0 && r->reach[a];
            }
            for (int k = 0; k < l->size * l->size && from; k++) {
                int b = rooms_at(r, m, l->x[1] + k % l->size, l->y[1] + k / l->size);
                if (b >= 0 && !r->reach[b]) { r->reach[b] = 1; queue[qt++] = b; more = 1; }
            }
        }
        if (!more) break;
    }
    free(queue); free(fill); free(adj); free(deg); free(pairs);
}

void rooms_free(Rooms *r)
{
    free(r->at);
    free(r->v);
    free(r->reach);
    memset(r, 0, sizeof *r);
}

/* -------------------------------------------------------------- describe */


static void room_name(const Rooms *r, int i, char *buf, size_t sz)
{
    map_coord_name(r->v[i].fx, r->v[i].fy, buf, sz);
}

/* How the account refers to a room: "Crypt (B2)" when an area names it
 * (the area holding its first square), else "(B2)". */
static void room_ref(const Map *m, const Rooms *r, int i, char *buf, size_t sz)
{
    char sq[MAP_COORD_MAX];
    room_name(r, i, sq, sizeof sq);
    int ai = map_area_at(m, r->v[i].fx, r->v[i].fy);
    if (ai >= 0) snprintf(buf, sz, "%s (%s)", m->areas[ai].name, sq);
    else         snprintf(buf, sz, "(%s)", sq);
}

static void bounds_name(int x0, int y0, int x1, int y1, char *buf, size_t sz)
{
    char a[MAP_COORD_MAX], b[MAP_COORD_MAX];
    map_coord_name(x0, y0, a, sizeof a);
    map_coord_name(x1, y1, b, sizeof b);
    snprintf(buf, sz, "%s:%s", a, b);
}

/* Every door, window and secret on a room's edge, and what is across it:
 * another room, void, or the map's edge. Walls are not listed. */
typedef struct { uint8_t kind; int vertical, x, y, to; } Opening;   /* to: room, -1 void, -2 off map */

static int room_openings(const Map *m, const Rooms *r, int room, Opening **buf, int *cap)
{
    const Room *rm = &r->v[room];
    int n = 0;
    /* Every square of the room's box, and nearly all with nothing but
     * floor round them: the four edge bytes are read straight first. */
    const uint8_t *V = m->vedges, *H = m->hedges;
    const int      W = m->w, VW = m->w + 1;
    for (int y = rm->y0; y <= rm->y1; y++)
        for (int x = rm->x0; x <= rm->x1; x++) {
            if (r->at[y * W + x] != room) continue;      /* (a room's box is on the map: no bounds to ask) */
            uint8_t ew = V[y * VW + x], ee = V[y * VW + x + 1], en = H[y * W + x], es = H[(y + 1) * W + x];
            if (ew <= EDGE_WALL && ee <= EDGE_WALL && en <= EDGE_WALL && es <= EDGE_WALL) continue;
            /* Its four sides; a boundary is listed from each room once. */
            const struct { int v, ex, ey, ox, oy; } side[4] = {
                { 1, x, y, x - 1, y }, { 1, x + 1, y, x + 1, y },
                { 0, x, y, x, y - 1 }, { 0, x, y + 1, x, y + 1 },
            };
            for (int s = 0; s < 4; s++) {
                uint8_t k = side[s].v ? map_vedge(m, side[s].ex, side[s].ey) : map_hedge(m, side[s].ex, side[s].ey);
                if (k == EDGE_NONE || k == EDGE_WALL) continue;
                if (n == *cap) { *cap = *cap ? *cap * 2 : 32; *buf = xrealloc(*buf, (size_t)*cap * sizeof **buf); }
                int to = !map_in_bounds(m, side[s].ox, side[s].oy) ? -2 : rooms_at(r, m, side[s].ox, side[s].oy);
                /* A door in a stub of wall inside one room is seen from
                 * both its squares; list it once, from the first. */
                if (to == room && (side[s].oy < y || (side[s].oy == y && side[s].ox < x))) continue;
                Opening *o = &(*buf)[n++];
                o->kind = k; o->vertical = side[s].v; o->x = side[s].ex; o->y = side[s].ey;
                o->to = to;
            }
        }
    return n;
}

void maptools_describe(FILE *out, const Map *m, int json)
{
    maptools_describe_in(out, m, json, NULL);
}

void maptools_describe_in(FILE *out, const Map *m, int json, const int *box)
{
    Rooms r;
    rooms_build(m, &r);
    /* IN(i): room i is told of. */
#define IN(i) (!box || (r.v[i].x0 <= box[2] && r.v[i].x1 >= box[0] && r.v[i].y0 <= box[3] && r.v[i].y1 >= box[1]))
    int told = 0;
    for (int i = 0; i < r.n; i++) told += IN(i);
    char buf[64], buf2[64];
    int  unreachable = 0;
    for (int i = 0; i < r.n; i++) unreachable += !r.reach[i];
    Opening *op = NULL;
    int      opcap = 0;

    if (json) {
        Json j;
        json_init(&j, out);
        json_open(&j, '{');
        json_key(&j, "map");
        json_open(&j, '{');
        json_kstr(&j, "name", m->name);
        json_kint(&j, "width", m->w);
        json_kint(&j, "height", m->h);
        json_key(&j, "scale_ft"); json_num(&j, m->scale_ft);
        json_kstr(&j, "metric", dist_metric_name((DistMetric)m->metric));
        json_key(&j, "ruleset"); if (m->ruleset[0]) json_str(&j, m->ruleset); else json_null(&j);
        json_kint(&j, "round", m->round);
        json_key(&j, "fog"); json_bool(&j, m->fog_on);
        json_kint(&j, "creatures", m->tokens.n);
        json_kint(&j, "rooms", r.n);
        if (box) {
            map_region_name(box[0], box[1], box[2], box[3], buf, sizeof buf);
            json_kstr(&j, "region", buf);
            json_kint(&j, "rooms_told", told);
        }
        json_key(&j, "start_room"); if (r.start >= 0) json_int(&j, r.start + 1); else json_null(&j);
        json_close(&j, '}');

        json_key(&j, "rooms");
        json_open(&j, '[');
        for (int i = 0; i < r.n; i++) {
            if (!IN(i)) continue;
            const Room *rm = &r.v[i];
            json_open(&j, '{');
            json_kint(&j, "id", i + 1);
            room_name(&r, i, buf, sizeof buf); json_kstr(&j, "name", buf);
            {
                int ai = map_area_at(m, rm->fx, rm->fy);
                json_key(&j, "area");
                if (ai >= 0) json_str(&j, m->areas[ai].name); else json_null(&j);
                int fl = floor_at(m, rm->fx, rm->fy);
                json_key(&j, "floor");
                if (fl >= 0) json_str(&j, m->areas[fl].name); else json_null(&j);
            }
            json_kint(&j, "x", rm->fx); json_kint(&j, "y", rm->fy);
            bounds_name(rm->x0, rm->y0, rm->x1, rm->y1, buf, sizeof buf); json_kstr(&j, "bounds", buf);
            json_kint(&j, "squares", rm->squares);
            json_key(&j, "reachable"); json_bool(&j, r.reach[i]);
            int terrain[TILE_COUNT] = { 0 };
            for (int y = rm->y0; y <= rm->y1; y++)
                for (int x = rm->x0; x <= rm->x1; x++)
                    if (rooms_at(&r, m, x, y) == i) terrain[map_tile(m, x, y)]++;
            json_key(&j, "terrain"); json_open(&j, '{');
            for (int t = 1; t < TILE_COUNT; t++) if (terrain[t]) json_kint(&j, tile_name((uint8_t)t), terrain[t]);
            json_close(&j, '}');
            int no = room_openings(m, &r, i, &op, &opcap);
            json_key(&j, "doors"); json_open(&j, '[');
            for (int k = 0; k < no; k++) {
                json_open(&j, '{');
                json_kstr(&j, "kind", edge_name(op[k].kind));
                maptools_edge_name(m, op[k].vertical, op[k].x, op[k].y, buf, sizeof buf);
                json_kstr(&j, "where", buf);
                json_key(&j, "to");
                if (op[k].to >= 0) json_int(&j, op[k].to + 1); else json_str(&j, op[k].to == -1 ? "void" : "off-map");
                json_close(&j, '}');
            }
            json_close(&j, ']');
            json_key(&j, "creatures"); json_open(&j, '[');
            for (int t = 0; t < m->tokens.n; t++) {
                const Token *tk = &m->tokens.v[t];
                if (rooms_at(&r, m, tk->x, tk->y) != i) continue;
                json_open(&j, '{');
                json_kstr(&j, "label", tk->label);
                json_kstr(&j, "kind", token_kind_name(tk->kind));
                json_kint(&j, "size", tk->size);
                map_coord_name(tk->x, tk->y, buf, sizeof buf); json_kstr(&j, "at", buf);
                json_kint(&j, "x", tk->x); json_kint(&j, "y", tk->y);
                if (tk->note[0]) json_kstr(&j, "note", tk->note);
                if (tk->hidden) { json_key(&j, "hidden"); json_bool(&j, 1); }
                json_close(&j, '}');
            }
            json_close(&j, ']');
            json_key(&j, "notes"); json_open(&j, '[');
            for (int nt = 0; nt < m->nnotes; nt++) {
                const Note *n = &m->notes[nt];
                if (rooms_at(&r, m, n->x, n->y) != i) continue;
                json_open(&j, '{');
                map_coord_name(n->x, n->y, buf, sizeof buf); json_kstr(&j, "at", buf);
                json_kstr(&j, "text", n->text);
                json_close(&j, '}');
            }
            json_close(&j, ']');
            json_key(&j, "fog"); json_open(&j, '[');
            for (int p = 1; p <= FOG_PATCH_MAX; p++) {
                const FogPatch *fp = &m->fog_patches[p - 1];
                if (!fp->name[0] || fp->dead) continue;
                int painted = 0, seen = 0;
                for (int y = rm->y0; y <= rm->y1; y++)
                    for (int x = rm->x0; x <= rm->x1; x++) {
                        uint8_t f = fog_at(m, x, y);
                        if (rooms_at(&r, m, x, y) != i || (int)(f & FOG_ID) != p) continue;
                        painted++;
                        seen += (f & (FOG_SEEN | FOG_HELD)) != 0;
                    }
                if (!painted) continue;
                json_open(&j, '{');
                json_kstr(&j, "patch", fp->name);
                json_kint(&j, "painted", painted);
                json_kint(&j, "seen", seen);
                json_close(&j, '}');
            }
            json_close(&j, ']');
            json_close(&j, '}');
        }
        json_close(&j, ']');

        json_key(&j, "areas");                     /* the names given to boxes */
        json_open(&j, '[');
        for (int i = 0; i < m->nareas; i++) {
            const Area *ar = &m->areas[i];
            json_open(&j, '{');
            json_kstr(&j, "name", ar->name);
            bounds_name(ar->x0, ar->y0, ar->x1, ar->y1, buf, sizeof buf);
            json_kstr(&j, "bounds", buf);
            json_kint(&j, "x0", ar->x0); json_kint(&j, "y0", ar->y0);
            json_kint(&j, "x1", ar->x1); json_kint(&j, "y1", ar->y1);
            if (ar->env[0]) json_kstr(&j, "environment", ar->env);
            json_close(&j, '}');
        }
        json_close(&j, ']');
        if (m->env[0]) json_kstr(&j, "environment", m->env);    /* the map's own */

        json_key(&j, "floors");                    /* areas marked as floors, low level first */
        json_open(&j, '[');
        {
            int order[MAP_AREAS_MAX], nf = floor_order(m, order);
            for (int k = 0; k < nf; k++) {
                const Area *fa = &m->areas[order[k]];
                json_open(&j, '{');
                json_kstr(&j, "name", fa->name);
                json_kint(&j, "level", fa->level);
                bounds_name(fa->x0, fa->y0, fa->x1, fa->y1, buf, sizeof buf);
                json_kstr(&j, "bounds", buf);
                json_close(&j, '}');
            }
        }
        json_close(&j, ']');

        json_key(&j, "links");                     /* squares joined: stairs, portals */
        json_open(&j, '[');
        for (int i = 0; i < m->nlinks; i++) {
            const Link *l = &m->links[i];
            json_open(&j, '{');
            json_kint(&j, "num", l->num);
            json_kstr(&j, "kind", link_kind_name(l->kind));
            json_kint(&j, "size", l->size);
            json_key(&j, "oneway"); json_bool(&j, l->oneway);
            json_key(&j, "secret"); json_bool(&j, l->secret);
            if (l->to_map[0]) { json_kstr(&j, "to_map", l->to_map); json_kstr(&j, "to_place", l->to_place); }
            json_key(&j, "ends");
            json_open(&j, '[');
            for (int e = 0; e < link_ends(l); e++) {
                json_open(&j, '{');
                link_end_name(l, e, buf, sizeof buf);
                json_kstr(&j, "at", buf);
                json_kint(&j, "x", l->x[e]); json_kint(&j, "y", l->y[e]);
                int rr = rooms_at(&r, m, l->x[e], l->y[e]);
                json_key(&j, "room");
                if (rr >= 0) json_int(&j, rr + 1); else json_null(&j);
                json_close(&j, '}');
            }
            json_close(&j, ']');
            json_close(&j, '}');
        }
        json_close(&j, ']');

        json_key(&j, "outside");                   /* creatures and notes on no room */
        json_open(&j, '[');
        for (int t = 0; t < m->tokens.n; t++) {
            const Token *tk = &m->tokens.v[t];
            if (rooms_at(&r, m, tk->x, tk->y) >= 0) continue;
            if (box && (tk->x < box[0] || tk->x > box[2] || tk->y < box[1] || tk->y > box[3])) continue;
            json_open(&j, '{');
            json_kstr(&j, "label", tk->label);
            map_coord_name(tk->x, tk->y, buf, sizeof buf); json_kstr(&j, "at", buf);
            json_close(&j, '}');
        }
        json_close(&j, ']');
        json_close(&j, '}');
        fputc('\n', out);
    } else {
        fprintf(out, "map  %s  %dx%d  scale %g ft  metric %s  ruleset %s\n", m->name, m->w, m->h, m->scale_ft,
                dist_metric_name((DistMetric)m->metric), m->ruleset[0] ? m->ruleset : "none");
        if (m->round) fprintf(out, "  round %d\n", m->round);
        int nclock = 0;
        for (int i = 0; i < CLOCK_MAX; i++) {
            const Clock *c = &m->clocks[i];
            if (!c->name[0]) continue;
            fprintf(out, "%s %s %d/%d%s", nclock++ ? "," : "  clocks", c->name, c->value, c->size, c->down ? " down" : "");
        }
        if (nclock) fputc('\n', out);
        int nroll = 0;
        for (int i = 0; i < ROLL_MAX; i++)
            if (m->rolls[i].name[0])
                fprintf(out, "%s %s = %s", nroll++ ? "," : "  rolls", m->rolls[i].name, m->rolls[i].expr);
        if (nroll) fputc('\n', out);
        int patches = 0;
        for (int i = 0; i < FOG_PATCH_MAX; i++) patches += m->fog_patches[i].name[0] && !m->fog_patches[i].dead;
        if (patches) fprintf(out, "  fog %s, %d patch%s\n", m->fog_on ? "on" : "off", patches, patches == 1 ? "" : "es");
        fprintf(out, "  %d creature%s, %d room%s", m->tokens.n, m->tokens.n == 1 ? "" : "s", r.n, r.n == 1 ? "" : "s");
        if (r.start >= 0) { room_ref(m, &r, r.start, buf, sizeof buf); fprintf(out, ", starting in room %d %s", r.start + 1, buf); }
        if (unreachable) fprintf(out, ", %d not reachable from it", unreachable);
        fputc('\n', out);
        int order[MAP_AREAS_MAX], nf = floor_order(m, order);
        for (int k = 0; k < nf; k++) {
            const Area *fa = &m->areas[order[k]];
            bounds_name(fa->x0, fa->y0, fa->x1, fa->y1, buf2, sizeof buf2);
            fprintf(out, "%s %s (level %d) %s", k ? "," : "  floors", fa->name, fa->level, buf2);
        }
        if (nf) fputc('\n', out);
        if (box) {
            map_region_name(box[0], box[1], box[2], box[3], buf, sizeof buf);
            fprintf(out, "  region %s: %d of the %d room%s\n", buf, told, r.n, r.n == 1 ? "" : "s");
        }

        for (int i = 0; i < r.n; i++) {
            if (!IN(i)) continue;
            const Room *rm = &r.v[i];
            room_ref(m, &r, i, buf, sizeof buf);
            bounds_name(rm->x0, rm->y0, rm->x1, rm->y1, buf2, sizeof buf2);
            int fl = floor_at(m, rm->fx, rm->fy);
            fprintf(out, "\nroom %d %s  %s  %d square%s%s%s%s\n", i + 1, buf, buf2, rm->squares,
                    rm->squares == 1 ? "" : "s", fl >= 0 ? "  on " : "", fl >= 0 ? m->areas[fl].name : "",
                    r.reach[i] ? "" : "  NOT REACHABLE");
            int terrain[TILE_COUNT] = { 0 };
            for (int y = rm->y0; y <= rm->y1; y++)
                for (int x = rm->x0; x <= rm->x1; x++)
                    if (rooms_at(&r, m, x, y) == i) terrain[map_tile(m, x, y)]++;
            fputs("  terrain", out);
            for (int t = 1; t < TILE_COUNT; t++) if (terrain[t]) fprintf(out, "  %s %d", tile_name((uint8_t)t), terrain[t]);
            fputc('\n', out);
            int no = room_openings(m, &r, i, &op, &opcap);
            for (int k = 0; k < no; k++) {
                maptools_edge_name(m, op[k].vertical, op[k].x, op[k].y, buf, sizeof buf);
                if (op[k].to >= 0) {
                    room_ref(m, &r, op[k].to, buf2, sizeof buf2);
                    fprintf(out, "  %-12s %-8s to room %d %s\n", edge_name(op[k].kind), buf, op[k].to + 1, buf2);
                } else {
                    fprintf(out, "  %-12s %-8s to %s\n", edge_name(op[k].kind), buf, op[k].to == -1 ? "void" : "the map's edge");
                }
            }
            for (int t = 0; t < m->tokens.n; t++) {
                const Token *tk = &m->tokens.v[t];
                if (rooms_at(&r, m, tk->x, tk->y) != i) continue;
                map_coord_name(tk->x, tk->y, buf, sizeof buf);
                fprintf(out, "  %-12s %-8s %s%s%s%s\n", token_kind_name(tk->kind), buf, tk->label,
                        tk->size > 1 ? (tk->size == 2 ? "  2x2" : "  3x3") : "", tk->note[0] ? "  (note)" : "",
                        tk->hidden ? "  (hidden)" : "");
            }
            for (int nt = 0; nt < m->nnotes; nt++) {
                const Note *n = &m->notes[nt];
                if (rooms_at(&r, m, n->x, n->y) != i) continue;
                map_coord_name(n->x, n->y, buf, sizeof buf);
                fprintf(out, "  %-12s %-8s %s\n", "note", buf, n->text);
            }
            for (int li = 0; li < m->nlinks; li++) {
                const Link *l = &m->links[li];
                if (l->to_map[0]) {
                    if (rooms_at(&r, m, l->x[0], l->y[0]) != i) continue;
                    char name[32], here[2 * MAP_COORD_MAX + 2];
                    link_name(l, name, sizeof name);
                    link_end_name(l, 0, here, sizeof here);
                    fprintf(out, "  %-12s %-8s leads to %s, %s (another map)%s\n", name, here, l->to_map, l->to_place,
                            l->secret ? "  secret" : "");
                    continue;
                }
                for (int e = 0; e < 2; e++) {
                    if (rooms_at(&r, m, l->x[e], l->y[e]) != i) continue;
                    char name[32], here[2 * MAP_COORD_MAX + 2], there[2 * MAP_COORD_MAX + 2], where[80];
                    link_name(l, name, sizeof name);
                    link_end_name(l, e, here, sizeof here);
                    link_end_name(l, 1 - e, there, sizeof there);
                    int to = rooms_at(&r, m, l->x[1 - e], l->y[1 - e]);
                    if (to >= 0) { room_ref(m, &r, to, buf2, sizeof buf2); snprintf(where, sizeof where, "room %d %s", to + 1, buf2); }
                    else         str_lcpy(where, "no room", sizeof where);
                    fprintf(out, "  %-12s %-8s %s %s, %s%s\n", name, here,
                            !l->oneway ? "to" : e == 0 ? "leads to" : "comes from", there, where,
                            l->secret ? "  secret" : "");
                }
            }
            for (int p = 1; p <= FOG_PATCH_MAX; p++) {
                const FogPatch *fp = &m->fog_patches[p - 1];
                if (!fp->name[0] || fp->dead) continue;
                int painted = 0, seen = 0;
                for (int y = rm->y0; y <= rm->y1; y++)
                    for (int x = rm->x0; x <= rm->x1; x++) {
                        uint8_t f = fog_at(m, x, y);
                        if (rooms_at(&r, m, x, y) != i || (int)(f & FOG_ID) != p) continue;
                        painted++;
                        seen += (f & (FOG_SEEN | FOG_HELD)) != 0;
                    }
                if (painted) fprintf(out, "  %-12s %-8s %d square%s, %d seen\n", "fog", fp->name, painted,
                                     painted == 1 ? "" : "s", seen);
            }
        }
        int outside = 0;
        for (int t = 0; t < m->tokens.n; t++) {
            const Token *tk = &m->tokens.v[t];
            if (rooms_at(&r, m, tk->x, tk->y) >= 0) continue;
            if (box && (tk->x < box[0] || tk->x > box[2] || tk->y < box[1] || tk->y > box[3])) continue;
            if (!outside++) fputs("\noutside any room\n", out);
            map_coord_name(tk->x, tk->y, buf, sizeof buf);
            fprintf(out, "  %-12s %-8s %s\n", token_kind_name(tk->kind), buf, tk->label);
        }
    }
    free(op);
    rooms_free(&r);
#undef IN
}

/* ----------------------------------------------------------------- check */

typedef struct {
    char code[8];
    char slug[24];
    /* Its place (a square, a boundary, a room; "" for a file finding) and its
     * message, each owned and as long as it is (finding_say, finding_where),
     * cut where the fixed fields they were cut (FINDING_WHERE_MAX,
     * FINDING_MSG_MAX): a map with a finding a square holds 262,145, and 336
     * inline bytes each were most of :check's memory (docs/reviews/
     * 2026-10-20-4-memory.md, M2). Never NULL once made; finding_free frees. */
    char *where;
    int  line;              /* 1-based; 0 for a map finding */
    int  col;               /* 0-based column of a file finding, -1 for none */
    int  x, y, edge;        /* 0-based square of a map finding; edge 'v', 'h' or 0 */
    char *msg;
    int  folded;            /* caused by another finding and counted in it: not listed */
    struct FindingMore *more;   /* what a finding says past its line, when it says more */
} Finding;

/* The lines under a finding's own (docs/V2.md, step 5's second piece, *The
 * standard*): the file's lines round it, the evidence, the fix, and what it
 * folded. Out of line: few findings have it, and a map can have thousands. */
typedef struct FindingMore {
    char snippet[640];      /* "     |" lines, each ending in a newline */
    char evidence[400];
    char help[720];         /* room for two readings' fixes and a long path */
    char note[200];
    char folded[200];       /* "W103 x14, E110 x1" */
    int  nfolded;
    int  fold_if;           /* folded by one reading's fix only: the note says which */
    int  joined;            /* folded in as one mistake with it (a stretch on void), not caused by it */
    int  span0, span1;      /* the file's lines a fix may be made at; 0 for none */
    int  certain;           /* 1 certain, 0 likely */
} FindingMore;

typedef struct { Finding *v; int n, cap; } Findings;

#define FINDING_MSG_MAX   256      /* what a message is cut to, with its NUL */
#define FINDING_WHERE_MAX 80

/* What a finding owns, freed; its fields NULL, so a second call is harmless. */
static void finding_free(Finding *f)
{
    free(f->more);
    free(f->msg);
    free(f->where);
    f->more = NULL;
    f->msg = f->where = NULL;
}

static char *cut_dup(const char *s, size_t max)
{
    size_t n = strnlen(s, max - 1);
    char  *p = xmalloc(n + 1);
    memcpy(p, s, n);
    p[n] = '\0';
    return p;
}

static void finding_say(Finding *f, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void finding_say(Finding *f, const char *fmt, ...)
{
    char buf[FINDING_MSG_MAX];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    free(f->msg);
    f->msg = cut_dup(buf, sizeof buf);
}

static void finding_where(Finding *f, const char *where)
{
    free(f->where);
    f->where = cut_dup(where, FINDING_WHERE_MAX);
}

static Finding *findings_room(Findings *fs)
{
    if (fs->n == fs->cap) { fs->cap = fs->cap ? fs->cap * 2 : 16; fs->v = xrealloc(fs->v, (size_t)fs->cap * sizeof *fs->v); }
    return &fs->v[fs->n++];
}

/* Finding g moved whole into fs: what it owns goes with it, and g keeps none. */
static void take_finding(Findings *fs, Finding *g)
{
    *findings_room(fs) = *g;
    g->more = NULL;
    g->msg = g->where = NULL;
}

static Finding *add_finding(Findings *fs, const char *code, const char *slug)
{
    Finding *f = findings_room(fs);
    memset(f, 0, sizeof *f);
    str_lcpy(f->code, code, sizeof f->code);
    str_lcpy(f->slug, slug, sizeof f->slug);
    f->x = f->y = f->col = -1;
    f->msg   = cut_dup("", 1);
    f->where = cut_dup("", 1);
    return f;
}

static void file_finding(void *ctx, int line, int col, const char *code, const char *slug, const char *msg)
{
    Finding *f = add_finding(ctx, code, slug);
    f->line = line;
    f->col  = col;
    finding_say(f, "%s", msg);
}

static void map_finding(Findings *fs, const char *code, const char *slug, int x, int y, int edge,
                        const char *where, const char *msg)
{
    Finding *f = add_finding(fs, code, slug);
    f->x = x; f->y = y; f->edge = edge;
    finding_where(f, where);
    finding_say(f, "%s", msg);
}

static int void_at(const Map *m, int x, int y) { return !map_in_bounds(m, x, y) || !map_walkable(m, x, y); }

/* Does any boundary other than this one meet the corner (cx,cy)? */
static int corner_busy(const Map *m, int cx, int cy, int skip_v, int skip_x, int skip_y)
{
    const struct { int v, x, y; } at[4] = { { 1, cx, cy - 1 }, { 1, cx, cy }, { 0, cx - 1, cy }, { 0, cx, cy } };
    for (int i = 0; i < 4; i++) {
        if (at[i].v == skip_v && at[i].x == skip_x && at[i].y == skip_y) continue;
        uint8_t k = at[i].v ? (at[i].y >= 0 && at[i].y < m->h && at[i].x >= 0 && at[i].x <= m->w ? map_vedge(m, at[i].x, at[i].y) : EDGE_NONE)
                            : (at[i].x >= 0 && at[i].x < m->w && at[i].y >= 0 && at[i].y <= m->h ? map_hedge(m, at[i].x, at[i].y) : EDGE_NONE);
        if (k != EDGE_NONE) return 1;
    }
    return 0;
}

/* One boundary's findings: a door or wall with nothing either side (one
 * finding a boundary here; check_report joins them by stretch), a door with
 * nothing on one, a door on its own in open floor. */
static void check_edge(const Map *m, Findings *fs, int vertical, int x, int y)
{
    uint8_t k = vertical ? map_vedge(m, x, y) : map_hedge(m, x, y);
    if (k == EDGE_NONE) return;
    int ax = vertical ? x - 1 : x, ay = vertical ? y : y - 1;
    int va = void_at(m, ax, ay), vb = void_at(m, x, y);
    char where[32], msg[160];
    maptools_edge_name(m, vertical, x, y, where, sizeof where);
    int opening = k != EDGE_WALL;
    const char *kn = edge_name(k);
    if (va && vb) {
        snprintf(msg, sizeof msg, "%s between two squares that are not map", kn);
        if (opening) map_finding(fs, "E101", "door-in-void", x, y, vertical ? 'v' : 'h', where, msg);
        else         map_finding(fs, "W103", "wall-in-void", x, y, vertical ? 'v' : 'h', where, msg);
        return;
    }
    if (!opening) return;
    if (va || vb) {
        /* Into void on the map is a mistake; off its edge is usually a way
         * out of the dungeon, and worth a note only. */
        int onmap = map_in_bounds(m, va ? ax : x, va ? ay : y);
        if (onmap) {
            snprintf(msg, sizeof msg, "%s leads into void", kn);
            map_finding(fs, "W102", "door-to-void", x, y, vertical ? 'v' : 'h', where, msg);
        } else {
            snprintf(msg, sizeof msg, "%s leads off the map: a way out?", kn);
            map_finding(fs, "N105", "door-off-map", x, y, vertical ? 'v' : 'h', where, msg);
        }
        return;
    }
    /* Its two ends: a door meets a wall at both in any room ever drawn. */
    int c0x = x, c0y = y, c1x = vertical ? x : x + 1, c1y = vertical ? y + 1 : y;
    if (!corner_busy(m, c0x, c0y, vertical, x, y) && !corner_busy(m, c1x, c1y, vertical, x, y)) {
        snprintf(msg, sizeof msg, "%s with no wall at either end: is it in the row or column it was meant for?", kn);
        map_finding(fs, "W104", "door-loose", x, y, vertical ? 'v' : 'h', where, msg);
    }
}

/* What to do about each finding (docs/V2.md, step 5's second piece, part 3:
 * the standard's fourth point), and why where that is not obvious. The map's
 * mistakes in vtt's keys and commands (build mode unless it says play mode);
 * the file's in the file's terms. Said once a code in a report: the text
 * under its first finding, the JSON in its `codes` object. A finding with help
 * of its own (FindingMore.help: the line search's) says that instead. Every
 * code the loader or a check emits has a row: the suite `checkhelp` reads the
 * sources for them. A code with two meanings has a row a slug. */
typedef struct { const char *code, *slug, *help, *note; } CodeHelp;

static const CodeHelp CODE_HELP[] = {
    { "E001", NULL, "the first line must be VTT and a version this vtt reads (its own or older), and a size line must give the width and height; a map from a newer vtt opens in that vtt", NULL },
    { "E010", NULL, "delete the characters past the map's width, or :resize the map if it should be wider", NULL },
    { "E011", NULL, "put the missing row back where it was lost (an empty line, if it held nothing), or take it from a copy of the map (the autosave, a backup)",
      "a section that comes up short reads the next line as its last row: a header swallowed leaves the whole next section unread" },
    { "E013", NULL, "use a character the section knows: the README's File format lists them", NULL },
    { "E014", NULL, "write the line as the README's File format gives it, or delete it", NULL },
    { "N021", NULL, "nothing to do: a short row reads as if filled out with nothing, and a save writes it whole", NULL },
    { "W015", NULL, "correct the line's first word (the README's File format), or delete it; a newer vtt's line is read by that vtt", NULL },
    { "W016", NULL, ":ruleset NAME names a ruleset this vtt knows (the README's Rulesets)", NULL },
    { "W017", NULL, ":metric names one: chebyshev, euclidean, alt or manhattan", NULL },
    { "W018", NULL, "write the fogpatch line the row names, or paint those squares again: :fog NAME, then g f", NULL },
    { "W019", NULL, "if the section above has a line too many, delete it; if a header was lost, put it back on a line of its own (tiles, vedges or hedges)", NULL },
    { "W020", NULL, "write a value in range (the message names it), or shorten a card over 4 KB; vtt keeps what it used", NULL },
    { "W022", NULL, "add the row's last character, its east boundary (a space for none)", NULL },
    { "W023", NULL, "make the link again with both ends on the map, apart and off other links: g l on one end, g l on the other", NULL },
    { "W024", NULL, "name the area first (:area NAME), then :floor NAME LEVEL, once, and over no other floor", NULL },
    { "W025", NULL, "give the scene a name used once and a box on the map, and end it with endscene; a map keeps 16", NULL },
    { "W026", NULL, "move the lines above end, or delete them", NULL },
    { "W027", NULL, "nothing to do: the name is kept without them, and a save writes it clean", NULL },
    { "W028", "card-cut", "put endcard on a line of its own where the card ends; what was read before the line that ended it is kept", NULL },
    { "W028", "card-dropped", "give the card a name used once and end it with endcard; a map keeps 64 cards", NULL },
    { "W029", NULL, "write rule massive, the one rule there is, or delete the line", NULL },
    { "W030", NULL, "name an area the map has, or :env NAME again where the cursor is", NULL },
    { "W031", NULL, "delete the line too many: the finding names it when the map shows which", NULL },
    { "W032", NULL, "keep the right rows once: merge the two sections by hand and delete the second header", NULL },
    { "E101", NULL, "floor the squares beside it (space), or take it away: brush 1 (1b), t to its kind, then H J K L toward it", NULL },
    { "E110", NULL, "in play mode move it onto floor (enter picks it up, h j k l, enter puts it down; ctrl-w first lets it cross void) or remove it (d); or floor the square (space)", NULL },
    { "E111", NULL, "in play mode make it smaller (enter, enter selects it, then B or 1b), or walk it all onto the map: ctrl-w lets it cross void, then enter, h j k l, enter", NULL },
    { "E112", NULL, "in play mode move one of them: enter picks it up", NULL },
    { "W102", NULL, "floor the void side (space), or make it a wall: t to wall, then that side's key (H J K L)", NULL },
    { "W103", NULL, "floor the squares beside the wall (space, or f over the brush), or clear it: brush 1 (1b), t to wall, then H J K L toward it", NULL },
    { "W104", NULL, "a door sits in a wall: draw the wall at each end (t to wall, then H J K L), or move the door into the wall it was meant for", NULL },
    { "N105", NULL, "nothing to do if it is a way out of the map; else move it inside", NULL },
    { "W113", NULL, "in play mode c changes one's label", "the turn order, damage and agents tell creatures apart by their labels" },
    { "W120", NULL, "if the party should reach it, put a door in its wall (t to door, then H J K L on that side) or a link to it (g l); a room shut on purpose needs nothing", NULL },
    { "W121", NULL, "in play mode move it to the others (enter), or open a way: a door (t to door, then H J K L) or a link (g l)", NULL },
    { "W130", NULL, "pick it with :fog NAME and paint it (g f), or :fog NAME remove", NULL },
    { "N131", NULL, ":fog NAME enable puts it back to work", NULL },
    { "W140", NULL, "floor the square (space), or move the note: s n there, ctrl-u, enter, then s n where it belongs", NULL },
    { "W150", NULL, "floor the square under that end (space), or :link N remove and make it again with g l", NULL },
    { "W151", NULL, "save the other map beside this one, or :link N remove and make it again from here: :link to MAP PLACE", NULL },
    { "W160", NULL, "a square is on one floor: :floor NAME off on one, or redraw its area (:area)", NULL },
    { "W170", NULL, ":env edit writes the card, or :env pick takes one you keep", NULL },
};

/* The table's row for a code and slug: the codes' numbers do not repeat
 * across E, W and N, so an index a number finds one in a step. */
static const CodeHelp *code_help(const char *code, const char *slug)
{
    static short at[1000];
    static int built;
    if (!built) {
        for (int i = 0; i < 1000; i++) at[i] = -1;
        for (int i = (int)(sizeof CODE_HELP / sizeof CODE_HELP[0]) - 1; i >= 0; i--) at[atoi(CODE_HELP[i].code + 1)] = (short)i;
        built = 1;
    }
    int n = code && code[0] ? atoi(code + 1) : -1;
    if (n < 0 || n >= 1000 || at[n] < 0 || CODE_HELP[at[n]].code[0] != code[0]) return NULL;
    for (int i = at[n]; i < (int)(sizeof CODE_HELP / sizeof CODE_HELP[0]) && !strcmp(CODE_HELP[i].code, code); i++)
        if (!CODE_HELP[i].slug || (slug && !strcmp(CODE_HELP[i].slug, slug))) return &CODE_HELP[i];
    return &CODE_HELP[at[n]];
}

const char *maptools_code_help(const char *code, const char *slug)
{
    const CodeHelp *h = code_help(code, slug);
    return h && !strcmp(h->code, code) ? h->help : NULL;
}

static int finding_cmp(const void *pa, const void *pb)
{
    const Finding *a = pa, *b = pb;
    int fa = a->line > 0 || !a->where[0], fb = b->line > 0 || !b->where[0];
    if (fa != fb) return fa ? -1 : 1;                    /* the file's own first */
    if (fa && a->line != b->line) return a->line - b->line;
    if (fa) { int c = strcmp(a->code, b->code); return c ? c : strcmp(a->msg, b->msg); }
    int ra = a->code[0] == 'E' ? 0 : a->code[0] == 'W' ? 1 : 2;
    int rb = b->code[0] == 'E' ? 0 : b->code[0] == 'W' ? 1 : 2;
    if (ra != rb) return ra - rb;                        /* errors, warnings, notes */
    int c = strcmp(a->code, b->code);
    if (c) return c;
    if (a->y != b->y) return a->y - b->y;
    if (a->x != b->x) return a->x - b->x;
    return strcmp(a->msg, b->msg);          /* a total order: the report never depends on qsort */
}

static const char *severity(const Finding *f)
{
    return f->code[0] == 'E' ? "error" : f->code[0] == 'W' ? "warning" : "note";
}

/* Everything the linter finds in a map, as against in its file. */
/* Which creatures share a square, and which a label, without comparing each
 * with every other: the squares hold a chain of the creatures standing on
 * them, and the labels are sorted. */
typedef struct {
    const Map *m;
    int32_t   *head;        /* by square: the first entry of its chain, 0 for none (entries count from 1,
                               so the array is calloc'd and only a square a creature stands on is touched:
                               the cost follows the creatures, not the map) */
    int32_t   *next, *who;  /* by entry: the chain's next, and whose it is (ascending down a chain) */
    int32_t   *same;        /* by creature: the next one with its label, -1 for none */
    int32_t   *off;         /* the creatures with a square off the map: the grid cannot hold those */
    int        noff;
    int       *found;       /* pairs_after's answer: j << 1 | 1 for a label, | 0 for a square; ascending */
    int        nfound, cap;
} Pairs;

static const Map *g_sort_map;
static int by_label(const void *pa, const void *pb)
{
    int a = *(const int32_t *)pa, b = *(const int32_t *)pb;
    int c = strcmp(g_sort_map->tokens.v[a].label, g_sort_map->tokens.v[b].label);
    return c ? c : (a > b) - (a < b);
}

static int int_cmp(const void *pa, const void *pb)
{
    int a = *(const int *)pa, b = *(const int *)pb;
    return (a > b) - (a < b);
}

static void pairs_build(Pairs *p, const Map *m)
{
    PROF_ZONE("check.pairs");
    memset(p, 0, sizeof *p);
    p->m = m;
    int    n = m->tokens.n;
    size_t squares = (size_t)m->w * (size_t)m->h, entries = 0;
    for (int i = 0; i < n; i++) entries += (size_t)m->tokens.v[i].size * (size_t)m->tokens.v[i].size;
    p->head = xcalloc(squares ? squares : 1, sizeof *p->head);
    p->next = xmalloc((entries + 1) * sizeof *p->next);
    p->who  = xmalloc((entries + 1) * sizeof *p->who);
    p->same = xmalloc((size_t)(n ? n : 1) * sizeof *p->same);
    p->off  = xmalloc((size_t)(n ? n : 1) * sizeof *p->off);
    /* From the last creature to the first, each pushed on the front: a
     * chain then runs from the lowest number up. */
    int32_t e = 1;
    for (int i = n - 1; i >= 0; i--) {
        const Token *t = &m->tokens.v[i];
        int out = 0;
        for (int y = t->y; y < t->y + t->size; y++)
            for (int x = t->x; x < t->x + t->size; x++) {
                if (!map_in_bounds(m, x, y)) { out = 1; continue; }
                size_t sq = (size_t)y * (size_t)m->w + (size_t)x;
                p->who[e]  = i;
                p->next[e] = p->head[sq];
                p->head[sq] = e++;
            }
        if (out) p->off[p->noff++] = i;
    }
    /* (off[] was filled from the last down: pairs_after sorts what it finds.) */
    int32_t *order = xmalloc((size_t)(n ? n : 1) * sizeof *order);
    for (int i = 0; i < n; i++) { order[i] = i; p->same[i] = -1; }
    g_sort_map = m;
    qsort(order, (size_t)n, sizeof *order, by_label);
    for (int k = 0; k + 1 < n; k++) {
        const Token *a = &m->tokens.v[order[k]], *b = &m->tokens.v[order[k + 1]];
        if (a->label[0] && !strcmp(a->label, b->label)) p->same[order[k]] = order[k + 1];
    }
    free(order);
}

static void pairs_put(Pairs *p, int v)
{
    if (p->nfound == p->cap) { p->cap = p->cap ? p->cap * 2 : 32; p->found = xrealloc(p->found, (size_t)p->cap * sizeof *p->found); }
    p->found[p->nfound++] = v;
}

/* The creatures numbered after i that meet it or are called as it is, into
 * p->found: how many. One that does both is there twice. Sorted, so that a
 * big creature met on several squares is found beside itself and kept once. */
static int pairs_after(Pairs *p, const Map *m, int i)
{
    const Token *t = &m->tokens.v[i];
    p->nfound = 0;
    int off = 0;
    for (int y = t->y; y < t->y + t->size; y++)
        for (int x = t->x; x < t->x + t->size; x++) {
            if (!map_in_bounds(m, x, y)) { off = 1; continue; }
            for (int32_t e = p->head[(size_t)y * (size_t)m->w + (size_t)x]; e > 0; e = p->next[e])
                if (p->who[e] > i) pairs_put(p, p->who[e] << 1);
        }
    /* Two that hang off the map may meet out there, where no square keeps a chain. */
    if (off)
        for (int k = 0; k < p->noff; k++) {
            const Token *u = &m->tokens.v[p->off[k]];
            if (p->off[k] > i && token_meets(t, u->x, u->y, u->size, u->size)) pairs_put(p, p->off[k] << 1);
        }
    for (int32_t j = p->same[i]; j >= 0; j = p->same[j]) pairs_put(p, j << 1 | 1);
    if (p->nfound > 1) {
        qsort(p->found, (size_t)p->nfound, sizeof *p->found, int_cmp);
        int k = 1;                                       /* a big creature met on two of its squares is one pair */
        for (int q = 1; q < p->nfound; q++)
            if (p->found[q] != p->found[k - 1]) p->found[k++] = p->found[q];
        p->nfound = k;
    }
    return p->nfound;
}

static void pairs_free(Pairs *p)
{
    free(p->head); free(p->next); free(p->who); free(p->same); free(p->off); free(p->found);
}

static FindingMore *finding_more(Finding *f);

/* Boundaries with no map on either side, said by stretch: two meet when they
 * share a corner. A stretch with a door in it is the door's finding (a doorway
 * drawn where there is no floor), the rest joined to it; one of walls alone
 * says how many and where it runs. One mistake, one finding: a map walled on
 * every boundary with no floor is one finding, not one a boundary. Done here,
 * as the report is made, on what is left once every fold has run: the line
 * search and :check fold boundary by boundary, so a stretch keeps exactly its
 * members that are still true. */
typedef struct { long long corner; int member; } StretchCorner;

static int stretch_corner_cmp(const void *pa, const void *pb)
{
    const StretchCorner *a = pa, *b = pb;
    return a->corner < b->corner ? -1 : a->corner > b->corner ? 1 : a->member - b->member;
}

static int stretch_find(int *parent, int i)
{
    while (parent[i] != i) { parent[i] = parent[parent[i]]; i = parent[i]; }
    return i;
}

static int void_member(const Finding *f)
{
    return f->line <= 0 && f->edge && (!strcmp(f->code, "W103") || !strcmp(f->code, "E101"));
}

static int stretch_before(const Finding *a, const Finding *b)
{
    return a->y != b->y ? a->y < b->y : a->x != b->x ? a->x < b->x : a->edge > b->edge;
}

static void group_void_findings(Findings *fs)
{
    int n = 0;
    for (int i = 0; i < fs->n; i++) n += void_member(&fs->v[i]);
    if (n < 2) return;
    int *idx = xmalloc((size_t)n * sizeof *idx), *parent = xmalloc((size_t)n * sizeof *parent);
    StretchCorner *c = xmalloc((size_t)n * 2 * sizeof *c);
    int k = 0;
    for (int i = 0; i < fs->n; i++) {
        const Finding *f = &fs->v[i];
        if (!void_member(f)) continue;
        idx[k] = i; parent[k] = k;
        /* A corner as one number: a map is at most 512 squares a side. */
        long long x1 = f->edge == 'v' ? f->x : f->x + 1, y1 = f->edge == 'v' ? f->y + 1 : f->y;
        c[2 * k]     = (StretchCorner){ (long long)f->y * 1024 + f->x, k };
        c[2 * k + 1] = (StretchCorner){ y1 * 1024 + x1, k };
        k++;
    }
    qsort(c, (size_t)n * 2, sizeof *c, stretch_corner_cmp);
    for (int i = 1; i < n * 2; i++)
        if (c[i].corner == c[i - 1].corner) {
            int ra = stretch_find(parent, c[i].member), rb = stretch_find(parent, c[i - 1].member);
            if (ra != rb) parent[ra] = rb;
        }
    free(c);
    /* Per stretch, by its root: walls, doors, the first and last member, the
     * first door. */
    int *walls = xcalloc((size_t)n, sizeof *walls), *doors = xcalloc((size_t)n, sizeof *doors);
    int *first = xmalloc((size_t)n * sizeof *first), *last = xmalloc((size_t)n * sizeof *last);
    int *door1 = xmalloc((size_t)n * sizeof *door1);
    for (int i = 0; i < n; i++) first[i] = last[i] = door1[i] = -1;
    for (int i = 0; i < n; i++) {
        int r = stretch_find(parent, i);
        const Finding *f = &fs->v[idx[i]];
        if (f->code[0] == 'W') walls[r]++;
        else {
            doors[r]++;
            if (door1[r] < 0 || stretch_before(f, &fs->v[idx[door1[r]]])) door1[r] = i;
        }
        if (first[r] < 0 || stretch_before(f, &fs->v[idx[first[r]]])) first[r] = i;
        if (last[r] < 0 || stretch_before(&fs->v[idx[last[r]]], f)) last[r] = i;
    }
    /* The stretch's finding stays; its other members go. */
    char *gone = xcalloc((size_t)fs->n, 1);
    for (int i = 0; i < n; i++) {
        int r = stretch_find(parent, i), keep = doors[r] ? door1[r] : first[r];
        if (i != keep) { gone[idx[i]] = 1; continue; }
        if (walls[r] + doors[r] < 2) continue;
        Finding *f = &fs->v[idx[i]];
        if (doors[r]) {
            int more = walls[r] + doors[r] - 1;
            char kind[24];
            str_lcpy(kind, f->msg, sizeof kind);
            char *sp = strstr(kind, " between");
            if (sp) *sp = '\0';
            finding_say(f, "%s between two squares that are not map, with %d more boundar%s joined to it on void",
                     kind, more, more == 1 ? "y" : "ies");
            FindingMore *mo = finding_more(f);
            mo->nfolded = more;
            mo->joined = 1;
            if (walls[r] && doors[r] > 1) snprintf(mo->folded, sizeof mo->folded, "W103 x%d, E101 x%d", walls[r], doors[r] - 1);
            else if (walls[r])            snprintf(mo->folded, sizeof mo->folded, "W103 x%d", walls[r]);
            else                          snprintf(mo->folded, sizeof mo->folded, "E101 x%d", doors[r] - 1);
        } else {
            finding_say(f, "%d walls between squares that are not map, joined end to end from %s to %s",
                     walls[r], fs->v[idx[first[r]]].where, fs->v[idx[last[r]]].where);
        }
    }
    int kept = 0;
    for (int i = 0; i < fs->n; i++) {
        if (gone[i]) { finding_free(&fs->v[i]); continue; }
        fs->v[kept++] = fs->v[i];
    }
    fs->n = kept;
    free(gone); free(idx); free(parent); free(walls); free(doors); free(first); free(last); free(door1);
}

static void check_map(const Map *m, Findings *fs)
{
    char where[64], msg[256];
    Pairs px;
    pairs_build(&px, m);
    /* Every edge of the map, and nearly all are none: the byte is read
     * here, and check_edge called for the ones that are something. */
    for (int y = 0; y < m->h; y++)
        for (int x = 0; x <= m->w; x++)
            if (m->vedges[y * (m->w + 1) + x] != EDGE_NONE) check_edge(m, fs, 1, x, y);
    for (int y = 0; y <= m->h; y++)
        for (int x = 0; x < m->w; x++)
            if (m->hedges[y * m->w + x] != EDGE_NONE) check_edge(m, fs, 0, x, y);

    for (int i = 0; i < m->tokens.n; i++) {
        const Token *t = &m->tokens.v[i];
        const char  *label = label_or_unnamed(t);
        map_coord_name(t->x, t->y, where, sizeof where);
        int over = t->x + t->size > m->w || t->y + t->size > m->h, on_void = 0;
        for (int y = t->y; y < t->y + t->size && !on_void; y++)
            for (int x = t->x; x < t->x + t->size && !on_void; x++)
                on_void = map_in_bounds(m, x, y) && !map_walkable(m, x, y);
        /* Off the edge and on void at once is one creature out of place: one
         * finding, the other folded in. */
        if (over) {
            snprintf(msg, sizeof msg, "%s is %dx%d and hangs off the map's edge%s", label, t->size, t->size,
                     on_void ? ", and the part on the map stands on void" : "");
            map_finding(fs, "E111", "token-overhang", t->x, t->y, 0, where, msg);
            if (on_void) {
                FindingMore *mo = xcalloc(1, sizeof *mo);
                fs->v[fs->n - 1].more = mo;
                mo->nfolded = 1;
                mo->joined = 1;
                str_lcpy(mo->folded, "E110 x1", sizeof mo->folded);
            }
        } else if (on_void) {
            snprintf(msg, sizeof msg, "%s stands on void", label);
            map_finding(fs, "E110", "token-on-void", t->x, t->y, 0, where, msg);
        }
        /* The creatures after this one that share a square with it or bear its
         * label: found through the pairs' two indexes, never by trying every
         * other creature (which was the square of their number: a map of
         * 262,144 ran for minutes). Their order here does not reach the
         * report, which is sorted whole (finding_cmp). */
        int nj = pairs_after(&px, m, i);
        for (int q = 0; q < nj; q++) {
            int j = px.found[q] >> 1, labeled = px.found[q] & 1;
            const Token *u = &m->tokens.v[j];
            if (!labeled) {
                snprintf(msg, sizeof msg, "%s and %s share a square", label, label_or_unnamed(u));
                map_finding(fs, "E112", "token-overlap", t->x, t->y, 0, where, msg);
            } else {
                char w2[MAP_COORD_MAX];
                map_coord_name(u->x, u->y, w2, sizeof w2);
                snprintf(msg, sizeof msg, "two creatures called %s, here and at %s", label, w2);
                map_finding(fs, "W113", "duplicate-label", t->x, t->y, 0, where, msg);
            }
        }
    }
    pairs_free(&px);

    Rooms r;
    rooms_build(m, &r);
    char sname[MAP_COORD_MAX] = "";
    if (r.start >= 0) map_coord_name(r.v[r.start].fx, r.v[r.start].fy, sname, sizeof sname);
    for (int i = 0; i < r.n; i++) {
        if (r.reach[i]) continue;
        map_coord_name(r.v[i].fx, r.v[i].fy, where, sizeof where);
        snprintf(msg, sizeof msg, "%d square%s, no door leads to it from room %s", r.v[i].squares,
                 r.v[i].squares == 1 ? "" : "s", sname);
        char w2[80];
        snprintf(w2, sizeof w2, "room %s", where);
        map_finding(fs, "W120", "unreachable-room", r.v[i].fx, r.v[i].fy, 0, w2, msg);
    }
    for (int i = 0; i < m->tokens.n; i++) {
        const Token *t = &m->tokens.v[i];
        int room = rooms_at(&r, m, t->x, t->y);
        if (t->kind != TOKEN_PLAYER || room < 0 || r.reach[room]) continue;
        map_coord_name(t->x, t->y, where, sizeof where);
        snprintf(msg, sizeof msg, "%s cannot reach the rest of the party in room %s",
                 label_or_unnamed(t), sname);
        map_finding(fs, "W121", "party-split", t->x, t->y, 0, where, msg);
    }
    rooms_free(&r);

    for (int id = 1; id <= FOG_PATCH_MAX; id++) {
        const FogPatch *p = &m->fog_patches[id - 1];
        if (!p->name[0] || p->dead) continue;
        int seen;
        if (fog_count(m, id, &seen) == 0) {
            snprintf(msg, sizeof msg, "fog patch %s has no square painted in the fog section", p->name);
            map_finding(fs, "W130", "fog-patch-empty", -1, -1, 0, p->name, msg);
        }
        if (p->disabled) {
            snprintf(msg, sizeof msg, "fog patch %s is disabled: it hides nothing", p->name);
            map_finding(fs, "N131", "fog-patch-disabled", -1, -1, 0, p->name, msg);
        }
    }
    for (int i = 0; i < m->nnotes; i++) {
        const Note *n = &m->notes[i];
        if (map_walkable(m, n->x, n->y)) continue;
        map_coord_name(n->x, n->y, where, sizeof where);
        snprintf(msg, sizeof msg, "a note on void: %.60s", n->text);
        map_finding(fs, "W140", "note-on-void", n->x, n->y, 0, where, msg);
    }
    /* An environment naming a card the map does not hold: :env has nothing to show. */
    for (int i = -1; i < m->nareas; i++) {
        const char *env = i < 0 ? m->env : m->areas[i].env;
        if (!env[0] || card_find(m, env) >= 0) continue;
        int ex = i < 0 ? 0 : m->areas[i].x0, ey = i < 0 ? 0 : m->areas[i].y0;
        map_coord_name(ex, ey, where, sizeof where);
        snprintf(msg, sizeof msg, "%s%.31s names the environment %s, and the map has no card of that name",
                 i < 0 ? "the map" : "area ", i < 0 ? "" : m->areas[i].name, env);
        map_finding(fs, "W170", "env-no-card", ex, ey, 0, where, msg);
    }
    for (int i = 0; i < m->nareas; i++) {
        if (!m->areas[i].floor || !floor_problem(m, i)) continue;
        /* Each overlapping pair once: from its later-named floor. */
        int first = 1;
        for (int k = 0; k < i && first; k++) {
            const Area *a = &m->areas[i], *o = &m->areas[k];
            first = !(o->floor && a->x0 <= o->x1 && o->x0 <= a->x1 && a->y0 <= o->y1 && o->y0 <= a->y1);
        }
        if (first) continue;
        map_coord_name(m->areas[i].x0, m->areas[i].y0, where, sizeof where);
        snprintf(msg, sizeof msg, "floor %s overlaps another floor: a square can be on only one", m->areas[i].name);
        map_finding(fs, "W160", "floors-overlap", m->areas[i].x0, m->areas[i].y0, 0, where, msg);
    }
    for (int i = 0; i < m->nlinks; i++) {
        int vx, vy;
        if (!link_void_square(m, &m->links[i], &vx, &vy)) continue;
        char name[32];
        link_name(&m->links[i], name, sizeof name);
        map_coord_name(vx, vy, where, sizeof where);
        snprintf(msg, sizeof msg, "%s has an end on void: nobody can land there", name);
        map_finding(fs, "W150", "link-on-void", vx, vy, 0, where, msg);
    }
    /* A link to another map: the file beside this one, and its place. */
    for (int i = 0; i < m->nlinks; i++) {
        const Link *l = &m->links[i];
        char why[200];
        if (!l->to_map[0] || !link_map_check(m, l->to_map, l->to_place, why, sizeof why, NULL)) continue;
        char name[32];
        link_name(l, name, sizeof name);
        map_coord_name(l->x[0], l->y[0], where, sizeof where);
        snprintf(msg, sizeof msg, "%s to %s, %s leads nowhere: %s", name, l->to_map, l->to_place, why);
        map_finding(fs, "W151", "link-to-nowhere", l->x[0], l->y[0], 0, where, msg);
    }
}

/* ----------------------------------------------------------- the line search */

/* Where a row section's lines went wrong (docs/V2.md, step 5's second piece).
 * A line added to a section, or lost from it, by hand moves every row below
 * it; the three row sections describe the same squares, and creatures, notes
 * and links are given by coordinates no line moves, so the section fits the
 * rest of the map above the seam and is one off below it. The search reads
 * the file's text by its headers (a lost row has swallowed the next header
 * and left that section unread in the map), scores every place the fix could
 * go in one pass, and says the best when it is clearly better than leaving
 * the section as read. Fog is never searched: a patch over void is legal, so
 * nothing on the map shows a fog row out of place. */

enum { SH_WALLS, SH_ENDS, SH_THINGS, SH_OPEN, SH_PARTS };

typedef struct {
    int w, h, kind;               /* the suspect section */
    const uint8_t *T, *V, *H;     /* the other sections, as their first rows say */
    int *thing_start, *thing_x;   /* per row: the squares of creatures, notes and link ends */
    int *thing_id;                /* the creature's index, or -1 */
    int *ttrans;                  /* per true tiles row: where void meets map along it, inside the map */
    int  use_open;                /* count floor against void with no wall in the score */
    /* shift_joint: no suspect (kind -1), and a section with alt_src set
     * read from it with a row put back at alt_k (alt_extra: a line taken
     * out there); INT_MAX as read, -1 every row moved. */
    int  alt, alt_k[3], alt_extra[3], alt_n[3];
    const uint8_t *alt_src[3], *alt_put[3];
} ShiftScan;

/* The row y of section `kind`: the suspect's `prev` or `cur` when it is the
 * suspect and y is the band's row or the one above, else the true one. */
static const uint8_t *shift_row(const ShiftScan *sc, int kind, int y, int band,
                                const uint8_t *prev, const uint8_t *cur)
{
    if (kind == sc->kind) return y == band ? cur : y == band - 1 ? prev : NULL;
    int rows = mapio_section_rows(kind, sc->h), width = mapio_section_width(kind, sc->w);
    if (y < 0 || y >= rows) return NULL;
    if (sc->alt && sc->alt_src[kind]) {
        int k = sc->alt_k[kind];
        if (sc->alt_extra[kind]) { if (y >= k) y++; }
        else if (y == k) return sc->alt_put[kind];
        else if (y > k) y--;
        return y >= 0 && y < sc->alt_n[kind] ? sc->alt_src[kind] + (size_t)y * (size_t)width : NULL;
    }
    const uint8_t *a = kind == MAPIO_TILES ? sc->T : kind == MAPIO_VEDGES ? sc->V : sc->H;
    return a + (size_t)y * (size_t)width;
}

static int shift_void(const uint8_t *row, int x, int w)
{
    return x < 0 || x >= w || !row || row[x] == TILE_VOID;
}

static int shift_blank(const uint8_t *row, int width)
{
    static const uint8_t zero[MAP_MAX_DIM + 1];
    return !row || !memcmp(row, zero, (size_t)width);
}

/* Band y: the boundaries on row y (vertical ones beside its squares, the
 * horizontal ones above them), the corners on its top line, and the things
 * on its squares. Each counts what check_map calls a mistake: a wall with
 * void on both sides, a wall end that meets no other, a creature, note or
 * link on void. And, apart (SH_OPEN), floor against void inside the map
 * with no wall between: no mistake in a cave, but none of it is in a map
 * drawn with walls -- so it counts in the score only when sc->use_open
 * says (shift_find: when the fix it points to leaves none of it at all). It
 * reads the suspect's rows y-1 and y alone, which is what lets every
 * candidate be scored from two running sums and its own seam. */
static int shift_band(const ShiftScan *sc, int y, const uint8_t *prev, const uint8_t *cur, int part[SH_PARTS])
{
    int w = sc->w;
    const uint8_t *t0 = shift_row(sc, MAPIO_TILES, y - 1, y, prev, cur);
    const uint8_t *t1 = shift_row(sc, MAPIO_TILES, y, y, prev, cur);
    const uint8_t *v0 = shift_row(sc, MAPIO_VEDGES, y - 1, y, prev, cur);
    const uint8_t *v1 = shift_row(sc, MAPIO_VEDGES, y, y, prev, cur);
    const uint8_t *h1 = shift_row(sc, MAPIO_HEDGES, y, y, prev, cur);
    int walls = 0, ends = 0, open = 0, things = 0;
    /* Most bands of a big map have no boundary in them: then no wall is on
     * void and no end is loose, and what is open is where void meets map
     * along the row (kept for each true row) and between the two rows (none
     * when they are alike). */
    if (shift_blank(v0, w + 1) && shift_blank(v1, w + 1) && shift_blank(h1, w)) {
        int b0 = shift_blank(t0, w), b1 = shift_blank(t1, w);
        if (y < sc->h)
            for (int i = sc->thing_start[y]; i < sc->thing_start[y + 1]; i++)
                things += b1 || shift_void(t1, sc->thing_x[i], w);
        if (part || sc->use_open) {
            if (y < sc->h) {
                if (t1 && t1 == sc->T + (size_t)y * (size_t)w) open += sc->ttrans[y];
                else if (!b1) for (int x = 1; x < w; x++) open += shift_void(t1, x - 1, w) != shift_void(t1, x, w);
            }
            if (y > 0 && y < sc->h && !(b0 && b1) && !(t0 && t1 && !memcmp(t0, t1, (size_t)w)))
                for (int x = 0; x < w; x++) open += shift_void(t0, x, w) != shift_void(t1, x, w);
        }
        if (part) { part[SH_THINGS] += things; part[SH_OPEN] += open; }
        return things + (sc->use_open ? open : 0);
    }
    for (int x = 0; x <= w; x++) {
        if (y < sc->h) {
            int a = shift_void(t1, x - 1, w), b = shift_void(t1, x, w);
            if (v1 && v1[x] != EDGE_NONE) walls += a && b;
            else if (x > 0 && x < w)      open += a != b;
        }
        if (x < w) {
            int a = shift_void(t0, x, w), b = shift_void(t1, x, w);
            if (h1 && h1[x] != EDGE_NONE) walls += a && b;
            else if (y > 0 && y < sc->h)  open += a != b;
        }
        int meet = (v0 && v0[x] != EDGE_NONE) + (v1 && v1[x] != EDGE_NONE) +
                   (x > 0 && h1 && h1[x - 1] != EDGE_NONE) + (x < w && h1 && h1[x] != EDGE_NONE);
        ends += meet == 1;
    }
    if (y < sc->h)
        for (int i = sc->thing_start[y]; i < sc->thing_start[y + 1]; i++)
            things += shift_void(t1, sc->thing_x[i], w);
    if (part) { part[SH_WALLS] += walls; part[SH_ENDS] += ends; part[SH_THINGS] += things; part[SH_OPEN] += open; }
    return walls + ends + things + (sc->use_open ? open : 0);
}

typedef struct {
    int found, kind, extra;       /* extra 1: a line too many; -1: one lost */
    int k;                        /* the seam: the row the fix takes out, or puts one at */
    int fill;                     /* a lost row put back as: 0 empty, 1 a copy of the row above, 2 of the one below */
    int at_end;                   /* lost, and the rows read fit: the section's last row is what is missing */
    int tie, certain, more;
    int score0, score;
    int part0[SH_PARTS], part[SH_PARTS];
    int thing;                    /* a creature on void as read and not after, or -1 */
    int span0, span1;             /* the file's lines the fix may be made at */
    int fix_line;                 /* extra: the line to delete; lost: the line to add after */
    int copy_line;                /* lost, fill 1 or 2: the line to copy */
    int stray;                    /* a line too many, and what it pushed out holds something */
    int nowhere;                  /* a row lost for certain, and no place for it can be told */
    int quiet;                    /* ...because nothing on the map tells one place from another */
    int withdrawn;                /* kind + 1: a place for a line too many there, taken back by the hidden-loss check */
    int flawed;                   /* kind + 1: a place fit best there, but its fix left mistakes (the map's own?) */
    int blank_only;               /* the section's only oddity is a blank after its rows: a separator's other reading */
    int either;                   /* blank_only, and these sections (bit by kind) may each have lost a row instead: no place */
    int kfirst, klast;            /* the places (rows) the answer covers */
    int n;                        /* the section's lines searched */
} Shift;

/* The suspect's row y with candidate k applied, `put` the row put back. */
static const uint8_t *shift_cand(int extra, uint8_t *const *rows, int n, int nr, const uint8_t *put, int k, int y)
{
    if (y < 0 || y >= nr) return NULL;
    int i = extra > 0 ? (y < k ? y : y + 1) : (y < k ? y : y == k ? -1 : y - 1);
    if (i < 0) return put;
    return i < n ? rows[i] : NULL;
}

static int shift_parts(const ShiftScan *sc, int extra, uint8_t *const *rows, int n, int nr,
                       const uint8_t *put, int k, int part[SH_PARTS])
{
    memset(part, 0, SH_PARTS * sizeof *part);
    int total = 0;
    for (int y = 0; y <= sc->h; y++)
        total += shift_band(sc, y, shift_cand(extra, rows, n, nr, put, k, y - 1),
                            shift_cand(extra, rows, n, nr, put, k, y), part);
    return total;
}

#define SHIFT_TIE 2

/* The first position of candidate k's run: removing any of a run of like
 * lines (`like` NULL), or putting `like` back anywhere among lines just like
 * it, makes one file. */
static int shift_run0(uint8_t *const *rows, const int *rstart, int n, int extra, int k, const uint8_t *like, size_t wz)
{
    if (extra > 0) return k < n ? rstart[k] : k;
    return k > 0 && !memcmp(rows[k - 1], like, wz) ? rstart[k - 1] : k;
}

/* A candidate: where, and for a lost row what is put back. Two that make
 * the same file are one: removing either of two like lines, or putting a row
 * beside a line just like it on either side. `canon` is the last position of
 * that run, which with the row put back names the file. */
typedef struct { int k, fill, score, canon; } ShiftCand;

/* One section searched: sh->extra set by the caller, `n` of its lines read. */
static void shift_search(const ShiftScan *sc, const MapioSection *sec, int n, Shift *sh)
{
    int nr = mapio_section_rows(sc->kind, sc->h), width = mapio_section_width(sc->kind, sc->w);
    int h = sc->h, npos = sh->extra > 0 ? nr + 1 : nr;
    size_t wz = (size_t)width;
    uint8_t  *buf = xcalloc((size_t)(n + 1) * wz, 1);
    uint8_t **rows = xmalloc((size_t)(n + 1) * sizeof *rows);
    for (int i = 0; i < n; i++) {
        rows[i] = buf + (size_t)i * wz;
        mapio_row_parse(sc->kind, sec->v[i].text, sec->v[i].len, rows[i], width);
    }
    const uint8_t *empty = buf + (size_t)n * wz;      /* zeroed: an empty row */
    /* Runs of like rows, once: rstart[i] and rend[i] are the first and last
     * of the run row i is in, so no candidate walks a run (an empty map is
     * one run of 512). */
    int *rstart = xmalloc((size_t)(n + 1) * sizeof *rstart), *rend = xmalloc((size_t)(n + 1) * sizeof *rend);
    for (int i = 0; i < n; i++) rstart[i] = i > 0 && !memcmp(rows[i - 1], rows[i], wz) ? rstart[i - 1] : i;
    for (int i = n - 1; i >= 0; i--) rend[i] = i + 1 < n && !memcmp(rows[i + 1], rows[i], wz) ? rend[i + 1] : i;
    /* Every row alike and empty (a section of nothing): whatever is taken
     * out or put back makes the file it is, so nothing to search -- unless
     * a row is lost for certain, which is then its last (below). */
    if (n > 0 && rstart[n - 1] == 0 && !memcmp(rows[0], empty, wz) && sh->extra > 0) {
        free(rstart); free(rend); free(rows); free(buf);
        return;
    }

    /* Each band as read and one off (rows[y+1] for a line too many, rows[y-1]
     * for one lost), and running sums of both: then a candidate's score is
     * the first sum above its seam, its seam's band or two, the second below. */
    int *PA = xcalloc((size_t)h + 3, sizeof *PA), *SB = xcalloc((size_t)h + 4, sizeof *SB);
    int *A = xmalloc((size_t)(h + 1) * sizeof *A);
    for (int y = 0; y <= h; y++) {
        const uint8_t *p = y - 1 >= 0 && y - 1 < nr && y - 1 < n ? rows[y - 1] : NULL;
        const uint8_t *c = y < nr && y < n ? rows[y] : NULL;
        A[y] = shift_band(sc, y, p, c, NULL);
        PA[y + 1] = PA[y] + A[y];
    }
    for (int y = h; y >= 0; y--) {
        int ip = sh->extra > 0 ? y : y - 2, ic = sh->extra > 0 ? y + 1 : y - 1;
        const uint8_t *p = y - 1 >= 0 && y - 1 < nr && ip >= 0 && ip < n ? rows[ip] : NULL;
        const uint8_t *c = y < nr && ic >= 0 && ic < n ? rows[ic] : NULL;
        /* Deep in a run of like rows one off is the same as read. */
        int lo = sh->extra > 0 ? y - 1 : y - 2, hi = sh->extra > 0 ? y + 1 : y;
        int alike = lo >= 0 && hi < n && hi < nr && rstart[hi] <= lo;
        SB[y] = SB[y + 1] + (alike ? A[y] : shift_band(sc, y, p, c, NULL));
    }
    free(A);
#define SUM_A(i) PA[(i) <= h + 1 ? (i) : h + 1]
#define SUM_B(i) SB[(i) <= h + 1 ? (i) : h + 1]
    int nfill = sh->extra > 0 ? 1 : 3, shadow = -1;
    /* Rows with a twin elsewhere in the section (not beside them): what a
     * paste makes. Rows hashed, sorted, and like hashes compared. */
    uint8_t *twin = xcalloc((size_t)n + 1, 1);
    if (sh->extra > 0 && n > 1) {
        struct RowHash { uint64_t h; int i; } *rh = xmalloc((size_t)n * sizeof *rh);
        for (int i = 0; i < n; i++) {
            uint64_t hv = 1469598103934665603ull;
            for (size_t x = 0; x < wz; x++) hv = (hv ^ rows[i][x]) * 1099511628211ull;
            rh[i].h = hv; rh[i].i = i;
        }
        for (int i = 1; i < n; i++)                     /* insertion sort: n is a section's rows */
            for (int j = i; j > 0 && (rh[j - 1].h > rh[j].h || (rh[j - 1].h == rh[j].h && rh[j - 1].i > rh[j].i)); j--) {
                struct RowHash t = rh[j]; rh[j] = rh[j - 1]; rh[j - 1] = t;
            }
        for (int a = 0; a < n; a++)
            for (int b = a + 1; b < n && rh[b].h == rh[a].h; b++) {
                int i = rh[a].i, j = rh[b].i;
                if ((i - j > 1 || j - i > 1) && !memcmp(rows[i], rows[j], wz)) twin[i] = twin[j] = 1;
            }
        free(rh);
    }
    ShiftCand *cand = xmalloc((size_t)npos * (size_t)nfill * sizeof *cand);
    int nc = 0;
    for (int k = 0; k < npos; k++)
        for (int f = 0; f < nfill; f++) {
            const uint8_t *put = f == 0 ? empty : f == 1 ? (k > 0 && k - 1 < n ? rows[k - 1] : NULL)
                                                         : (k < n ? rows[k] : NULL);
            if (!put) continue;
            /* A copy that is the empty row, or the other copy, is that one;
             * and a candidate that makes the file the one before it makes
             * (removing the second of two like lines, putting a row back
             * after a line just like it) is that one: each file once. */
            /* A line too many is one an editor makes: an empty line, or a
             * copy of the line beside it. A row like no other (a wall) is
             * no accident's, and deleting one only ever lowers the counts
             * of a map that has some wall on void of its own. The last line
             * (leaving it as read) is always a candidate. */
            int unique = sh->extra > 0 && k < nr && k < n && memcmp(rows[k], empty, wz) &&
                         !(k > 0 && !memcmp(rows[k], rows[k - 1], wz)) && !(k + 1 < n && !memcmp(rows[k], rows[k + 1], wz));
            if (unique && !twin[k]) continue;           /* like no other row: no accident's, no paste's */
            if (f > 0 && !memcmp(put, empty, wz)) continue;
            if (f == 2 && k > 0 && k - 1 < n && !memcmp(put, rows[k - 1], wz)) continue;
            if (sh->extra > 0 ? k < n && rstart[k] < k : k > 0 && k - 1 < n && !memcmp(rows[k - 1], put, wz)) continue;
            int s = SUM_A(k), e = k;
            if (sh->extra > 0) {
                if (k <= h) s += shift_band(sc, k, shift_cand(1, rows, n, nr, NULL, k, k - 1),
                                            shift_cand(1, rows, n, nr, NULL, k, k), NULL);
                s += SUM_B(k + 1);
                if (k < n) e = rend[k];
            } else {
                if (k <= h)     s += shift_band(sc, k, shift_cand(-1, rows, n, nr, put, k, k - 1), put, NULL);
                if (k + 1 <= h) s += shift_band(sc, k + 1, put, shift_cand(-1, rows, n, nr, put, k, k + 1), NULL);
                s += SUM_B(k + 2);
                if (k < n && !memcmp(rows[k], put, wz)) e = rend[k] + 1;
            }
            /* ...but it is scored, a shadow never told: a row pasted from
             * elsewhere in the section is one too many that no rule above
             * names, and if taking one out fits as well, no place is sure. */
            if (unique) { if (shadow < 0 || s < shadow) shadow = s; continue; }
            cand[nc++] = (ShiftCand){ k, f, s, e };
        }
#undef SUM_A
#undef SUM_B
    /* The fill's row, for telling two candidates' files apart. */
#define PUT(c) ((c).fill == 0 ? empty : (c).fill == 1 ? rows[(c).k - 1] : rows[(c).k])
#define SAME(a, b) ((a).canon == (b).canon && (sh->extra > 0 || !memcmp(PUT(a), PUT(b), wz)))
    ShiftCand none = sh->extra > 0 ? (ShiftCand){ nr, 0, 0, nr } : (ShiftCand){ nr - 1, 0, 0, 0 };
    if (sh->extra < 0) none.canon = n;                 /* an empty row put at the end: n == nr - 1 */
    /* The first position of a candidate's run: back from it while the rows
     * match what it removes or puts back. */
#define RUN0(c) shift_run0(rows, rstart, n, sh->extra, (c).k, sh->extra > 0 ? NULL : PUT(c), wz)
    int bi = -1, s0 = -1;
    for (int i = 0; i < nc; i++) {
        if (SAME(cand[i], none) && (s0 < 0 || cand[i].score < s0)) s0 = cand[i].score;
        if (bi < 0 || cand[i].score < cand[bi].score) bi = i;
    }
    /* Within SHIFT_TIE of the best is as good as the best: the evidence is
     * counts of what a map drawn right has little of, and a map has a few
     * by design (a wall's stub, a door in a doorway), so a candidate that
     * lowers them by one or two over the true one is not to be told from it.
     * Every candidate that close is in the answer, which is then a span. */
    int sb = cand[bi].score, second = -1, tie = 0, best_none = SAME(cand[bi], none);
    int lo = RUN0(cand[bi]), hi = cand[bi].canon;
    for (int i = 0; i < nc; i++) {
        if (SAME(cand[i], cand[bi])) continue;
        if (cand[i].score <= sb + SHIFT_TIE) {
            tie = 1;
            best_none |= SAME(cand[i], none);
            int r0 = RUN0(cand[i]);
            if (r0 < lo) lo = r0;
            if (cand[i].canon > hi) hi = cand[i].canon;
        } else if (second < 0 || cand[i].score < second) second = cand[i].score;
    }
    int better = s0 - sb;
    int lost_for_sure = sh->extra < 0;
    /* Clearly better: by more than a tie when the count already says a line
     * is wrong; when the count looks right (a separator read as a row), by
     * enough to stand out of a map's own share of loose ends and open edges
     * -- a map drawn with no walls has its whole edge open. */
    /* ...and by more than twice what the fix still leaves: a map with
     * walls on void of its own can score better shifted than true, by
     * about what it has. */
    int clear = better > SHIFT_TIE && better >= 2 * sb;
    /* A lost row is put back as a guess (empty, or a neighbor's copy), so
     * its place is told only when the map comes out with nothing left of
     * what is counted; else, the row is said to be missing, no place. */
    /* ...and so is a line too many: on a map with flaws of its own a wrong
     * place can score better than the true one, which keeps them all. */
    if (clear && sb > 0) { clear = 0; sh->flawed = sc->kind + 1; }
    int unsure = shadow >= 0 && shadow <= sb + SHIFT_TIE;
    if (unsure) clear = 0;
    /* Loose wall ends alone place nothing: a doorway or a corridor's mouth
     * is a pair of them, and squeezing out the row between two segments
     * mends them. Walls off void, things off void, or (on a map drawn with
     * walls) open edges closed must come with them. */
    /* Each a whole pass over the map: once, kept for what is told. */
    int p0[SH_PARTS], p1[SH_PARTS], have_parts = 0;
    if (clear) {
        const uint8_t *bput = sh->extra > 0 ? NULL : PUT(cand[bi]);
        shift_parts(sc, sh->extra, rows, n, nr, sh->extra > 0 ? NULL : empty, none.k, p0);
        shift_parts(sc, sh->extra, rows, n, nr, bput, cand[bi].k, p1);
        have_parts = 1;
        if (p0[SH_WALLS] <= p1[SH_WALLS] && p0[SH_THINGS] <= p1[SH_THINGS] && !(sc->use_open && p0[SH_OPEN] > p1[SH_OPEN]))
            clear = 0;
    }
    /* A row lost is for certain (the section swallowed what followed it):
     * then something is said even when leaving the rows where they are is
     * among the best -- the row missing at the end. */

    /* And a line too many for certain when what it pushed out of the
     * section holds something (a blank there is what a hand-written file
     * puts between sections): then a tie that takes in leaving it as read
     * -- the last line being the extra one -- is still a span to tell. */
    int stray = sh->extra > 0 && nr < n && !sec->v[nr].blank && memcmp(rows[nr], empty, wz);
    if (best_none && !lost_for_sure && !(stray && tie && !unsure && sb == 0)) { /* leaving it as read is as good: nothing to say */ }
    else if (lost_for_sure && sb > 0) {
        /* A row lost, and leaving the rows where they are fits best -- but
         * the map has mistakes of its own even so, and a map with some can
         * score better shifted than true: no place is told. */
        sh->found  = 1;
        sh->nowhere = 1;
        sh->score0 = s0;
        sh->score  = sb;
        sh->thing  = -1;
    }
    else if (lost_for_sure && !clear && !best_none) {
        /* A row lost for certain, the count says so, and the map shows no
         * place: loose ends alone, or no place clearly best. Said, no place. */
        sh->found   = 1;
        sh->nowhere = 1;
        sh->quiet   = 1;
        sh->score0  = s0;
        sh->score   = sb;
        sh->thing   = -1;
    }
    else if (best_none || clear) {
        ShiftCand c = cand[bi];
        if (best_none && !tie) c = none;
        const uint8_t *put = sh->extra > 0 ? NULL : PUT(c);
        int first = tie ? lo : RUN0(c), last = tie ? hi : c.canon;
        sh->found   = 1;
        sh->stray   = stray;
        sh->at_end  = best_none && !tie;
        sh->k       = c.k;
        sh->kfirst  = first;
        sh->klast   = last;
        sh->n       = n;
        sh->fill    = c.fill;
        sh->tie     = tie;
        /* A row lost at the end is what fits, never what is seen: a row
         * that held something no wall or creature points at leaves no
         * trace. */
        sh->certain = !tie && !sh->at_end && (second < 0 || second - sb >= 4 || second - sb >= (better + 1) / 2);
        sh->score0  = s0;
        sh->score   = c.score;
        if (!have_parts) shift_parts(sc, sh->extra, rows, n, nr, sh->extra > 0 ? NULL : empty, none.k, p0);
        memcpy(sh->part0, p0, sizeof p0);
        /* Leaving it as read is the as-read pass; the best is p1. */
        if (sh->at_end)        memcpy(sh->part, p0, sizeof p0);
        else if (have_parts && SAME(c, cand[bi])) memcpy(sh->part, p1, sizeof p1);
        else shift_parts(sc, sh->extra, rows, n, nr, put, c.k, sh->part);
        if (sh->extra > 0) {
            sh->span0 = sec->v[first].line;
            sh->span1 = sec->v[last < n ? last : n - 1].line;
            sh->fix_line = sec->v[c.k].line;
        } else {
            sh->span0 = first ? sec->v[first - 1].line : sec->header;
            sh->span1 = last ? sec->v[(last < n ? last : n) - 1].line : sec->header;
            sh->fix_line = c.k ? sec->v[c.k - 1].line : sec->header;
            sh->copy_line = c.fill == 1 ? sec->v[c.k - 1].line : c.fill == 2 ? sec->v[c.k].line : 0;
        }
        /* A fix that leaves floor against void with no wall where reading
         * the file as it is has none is no fix of a map drawn right: on a
         * map whose walls stand on void by design (pillars against a wall)
         * it can score better by moving the wall off them. */
        if (!sh->at_end && sh->part[SH_OPEN] > sh->part0[SH_OPEN]) {
            int kd = sh->kind, ex = sh->extra;
            memset(sh, 0, sizeof *sh);
            sh->kind = kd;
            sh->extra = ex;
            if (lost_for_sure) { sh->found = 1; sh->nowhere = 1; sh->score0 = s0; sh->score = sb; }
        }
        /* A creature the fix takes off void, to name. */
        sh->thing = -1;
        if (sc->kind == MAPIO_TILES && sh->found && !sh->nowhere)
            for (int y = 0; y < h && sh->thing < 0; y++) {
                const uint8_t *ra = shift_cand(sh->extra, rows, n, nr, empty, none.k, y);
                const uint8_t *rb = shift_cand(sh->extra, rows, n, nr, put, c.k, y);
                for (int i = sc->thing_start[y]; i < sc->thing_start[y + 1]; i++)
                    if (sc->thing_id[i] >= 0 && shift_void(ra, sc->thing_x[i], sc->w) && !shift_void(rb, sc->thing_x[i], sc->w)) {
                        sh->thing = sc->thing_id[i];
                        break;
                    }
            }
    }
#undef RUN0
#undef PUT
#undef SAME
    free(cand);
    free(twin);
    free(rstart);
    free(rend);
    free(PA);
    free(SB);
    free(rows);
    free(buf);
}

/* Two sections each moved -- a line taken out of one or a row put back --
 * and whether some pair of places leaves the map with no more than `limit`,
 * scored as the search scores (shift_find: is there another reading as
 * good as the answer?). Every pair of places is weighed. A band reads two
 * rows of each section, so with each section as read or moved its bands are
 * running sums; a place's seam bands are read beside the other as read or
 * moved, once, when a pair first wants them, and only two places whose
 * seams meet are read together. */
typedef struct {
    int kind, extra;              /* the section; a line too many (taken out) or a row lost (put back) */
    const uint8_t *A;             /* its rows as read, n of them */
    int n;
    int stay;                     /* may it be left as read */
    int skip0, skip1;             /* places not to try (the answer's), as rows; skip0 > skip1 for none */
} JointSide;

typedef struct { int k; const uint8_t *put; int seam[2]; } JointCand;

static JointCand *joint_cands(const ShiftScan *sc, const JointSide *js, int *nout)
{
    int rows = mapio_section_rows(js->kind, sc->h), width = mapio_section_width(js->kind, sc->w);
    size_t wz = (size_t)width;
    static const uint8_t empty[MAP_MAX_DIM + 1];
    JointCand *c = xmalloc((size_t)(rows * 2 + 2) * sizeof *c);
    int n = 0;
#define ROW(i) (js->A + (size_t)(i) * wz)
    if (js->extra > 0) {
        /* Any line out of the first rows + 1, but each run of like lines once. */
        for (int k = 0; k < js->n && k <= rows; k++) {
            if (k > 0 && !memcmp(ROW(k - 1), ROW(k), wz)) continue;
            if (k >= js->skip0 && k <= js->skip1) continue;
            c[n++] = (JointCand){ k, NULL, { 0, 0 } };
        }
    } else {
        /* Empty, or a copy of the row below: a copy of the row above is
         * the row below's copy one place up. */
        for (int k = 0; k < rows; k++)
            for (int f = 0; f < 2; f++) {
                const uint8_t *put = f == 0 ? empty : k < js->n ? ROW(k) : NULL;
                if (!put || (f > 0 && !memcmp(put, empty, wz))) continue;
                /* After a row just like it: the file of the place before. */
                if (k > 0 && k - 1 < js->n && !memcmp(ROW(k - 1), put, wz)) continue;
                if (k >= js->skip0 && k <= js->skip1) continue;
                c[n++] = (JointCand){ k, put, { 0, 0 } };
            }
    }
#undef ROW
    if (js->stay) c[n++] = (JointCand){ INT_MAX, NULL, { 0, 0 } };
    *nout = n;
    return c;
}

/* A place's seam bands beside the other section as read (o 0) or moved,
 * read when first wanted. */
static int joint_seam(ShiftScan *sc, JointCand *a, int me, int other, int width, int nb, int o)
{
    if (a->seam[o] < 0) {
        sc->alt_k[me] = a->k;
        sc->alt_put[me] = a->put;
        sc->alt_k[other] = o ? -1 : INT_MAX;
        int s = 0;
        for (int y = a->k; y < a->k + width && y < nb; y++) s += shift_band(sc, y, NULL, NULL, NULL);
        a->seam[o] = s;
        sc->alt_k[me] = INT_MAX;
    }
    return a->seam[o];
}

static int shift_joint(const ShiftScan *base, const JointSide side[2], int use_open, int limit)
{
    PROF_ZONE("check.joint");
    ShiftScan sc = *base;
    sc.kind = -1;
    sc.use_open = use_open;
    sc.alt = 1;
    for (int kind = 0; kind < 3; kind++) sc.alt_src[kind] = NULL;
    int seam[2];
    for (int i = 0; i < 2; i++) {
        int kd = side[i].kind;
        sc.alt_src[kd] = side[i].A;
        sc.alt_n[kd] = side[i].n;
        sc.alt_extra[kd] = side[i].extra > 0;
        sc.alt_k[kd] = INT_MAX;
        /* The bands that read a moved row beside a row as read. */
        seam[i] = (kd == MAPIO_HEDGES ? 0 : 1) + (side[i].extra > 0 ? 0 : 1);
    }
    int nb = sc.h + 1, kx = side[0].kind, ky = side[1].kind;
    int *P[2][2];
    for (int a = 0; a < 2; a++)
        for (int b = 0; b < 2; b++) {
            P[a][b] = xcalloc((size_t)nb + 1, sizeof *P[a][b]);
            sc.alt_k[kx] = a ? -1 : INT_MAX;
            sc.alt_k[ky] = b ? -1 : INT_MAX;
            for (int y = 0; y < nb; y++) P[a][b][y + 1] = P[a][b][y] + shift_band(&sc, y, NULL, NULL, NULL);
        }
    sc.alt_k[kx] = sc.alt_k[ky] = INT_MAX;
#define RUN(a, b, lo, hi) ((lo) < (hi) ? P[a][b][(hi) < nb ? (hi) : nb] - P[a][b][(lo) > 0 ? (lo) : 0] : 0)
#define SEAM(i, cand, o) joint_seam(&sc, (cand), side[i].kind, side[1 - (i)].kind, seam[i], nb, (o))
    JointCand *c[2];
    int nc[2];
    for (int i = 0; i < 2; i++) {
        c[i] = joint_cands(&sc, &side[i], &nc[i]);
        for (int j = 0; j < nc[i]; j++) c[i][j].seam[0] = c[i][j].seam[1] = -1;
    }
    /* Only whether some pair fits within `limit` is asked: bands count
     * mistakes, never below nought, so the running sums alone bound a pair
     * from below, and a pair past the limit by them is never read further. */
    int fits = 0;
    for (int jx = 0; jx < nc[0] && !fits; jx++)
        for (int jy = 0; jy < nc[1] && !fits; jy++) {
            JointCand *x = &c[0][jx], *yc = &c[1][jy];
            int s;
            if (x->k == INT_MAX && yc->k == INT_MAX) s = RUN(0, 0, 0, nb);
            else if (yc->k == INT_MAX) {
                s = RUN(0, 0, 0, x->k) + RUN(1, 0, x->k + seam[0], nb);
                if (s <= limit) s += SEAM(0, x, 0);
            } else if (x->k == INT_MAX) {
                s = RUN(0, 0, 0, yc->k) + RUN(0, 1, yc->k + seam[1], nb);
                if (s <= limit) s += SEAM(1, yc, 0);
            } else {
                int x0 = x->k, x1 = x->k + seam[0], y0 = yc->k, y1 = yc->k + seam[1];
                if (x1 <= y0) {
                    s = RUN(0, 0, 0, x0) + RUN(1, 0, x1, y0) + RUN(1, 1, y1, nb);
                    if (s <= limit) s += SEAM(0, x, 0);
                    if (s <= limit) s += SEAM(1, yc, 1);
                } else if (y1 <= x0) {
                    s = RUN(0, 0, 0, y0) + RUN(0, 1, y1, x0) + RUN(1, 1, x1, nb);
                    if (s <= limit) s += SEAM(1, yc, 0);
                    if (s <= limit) s += SEAM(0, x, 1);
                } else {
                    /* Seams that meet: their bands read with both moved. */
                    int lo = x0 < y0 ? x0 : y0, hi = x1 > y1 ? x1 : y1;
                    s = RUN(0, 0, 0, lo) + RUN(1, 1, hi, nb);
                    if (s <= limit) {
                        sc.alt_k[kx] = x->k; sc.alt_put[kx] = x->put;
                        sc.alt_k[ky] = yc->k; sc.alt_put[ky] = yc->put;
                        for (int y = lo; y < hi && y < nb && s <= limit; y++) s += shift_band(&sc, y, NULL, NULL, NULL);
                        sc.alt_k[kx] = sc.alt_k[ky] = INT_MAX;
                    }
                }
            }
            fits = s <= limit;
        }
#undef SEAM
#undef RUN
    for (int a = 0; a < 2; a++) for (int b = 0; b < 2; b++) free(P[a][b]);
    free(c[0]); free(c[1]);
    return fits;
}

/* Is a line past the section's rows one the loader reads as a stray row?
 * Then the section has a line too many for certain: a blank there may be a
 * separator, and an unknown line is the loader's W015, not a row. */
static int rows_past(const MapioSection *sec)
{
    for (int i = sec->rows; i < sec->n; i++)
        if (sec->v[i].row) return 1;
    return 0;
}

/* Every row section of the file searched; the clearest shift found, if any,
 * into *out. `secs` are the file's sections by their headers. */
static void shift_find(const Map *m, MapioSection secs[MAPIO_SECTIONS], Shift *out)
{
    PROF_ZONE("check.shift");
    memset(out, 0, sizeof *out);
    int w = m->w, h = m->h;
    /* Which sections to search: one with a line too many or too few, or
     * one whose count is right but whose last line is a blank (a separator
     * read as its last row). None, and there is nothing to read. */
    /* Which sections to search. A blank line after a section's rows is a
     * separator in a file that keeps them, and a row pushed out of it in
     * one that does not (vtt writes none); the text alone cannot tell, so
     * the file's form is decided once, by its sections: two or more ending
     * in exactly one blank, and it keeps separators -- each section sets
     * its last blank aside, and one whose count is then one short could
     * as well be a correct section that kept none, so it is not searched;
     * fewer, and it is vtt's form -- a blank after a section is a row it
     * pushed out (the veto on new open edges keeps a separator's reading
     * from moving a wall). A header seen twice (two sections of one name:
     * a merge) leaves nothing to trust. */
    int want[3] = { 0 }, any = 0, n_of[3], seps = 0, wmask = 0, flawed = 0;
    for (int kind = MAPIO_TILES; kind <= MAPIO_HEDGES; kind++) {
        const MapioSection *sec = &secs[kind];
        if (sec->repeated) return;
        int n = sec->n;
        while (n > sec->rows + 1 && sec->v[n - 1].blank) n--;      /* blanks past one more */
        n_of[kind] = n;
        seps += sec->header && n == sec->rows + 1 && sec->v[n - 1].blank;
    }
    for (int kind = MAPIO_TILES; kind <= MAPIO_HEDGES; kind++) {
        const MapioSection *sec = &secs[kind];
        if (!sec->header) continue;
        int nr = sec->rows;
        if (seps >= 2) {
            int n = sec->n, aside = n > 0 && sec->v[n - 1].blank;
            if (aside) n--;
            n_of[kind] = n;
            /* One line more whose last is a blank too: a second separator,
             * or a row pushed out -- as in vtt's form, it yields. */
            if (n == nr + 1 && sec->v[nr].blank) want[kind] = 1;
            else if (n >= nr + 1 || (n == nr - 1 && !aside)) want[kind] = 2;
            /* Short even with its blank counted: short for certain (the
             * blank is a row it read, not a separator). */
            else if (sec->n < nr) { want[kind] = 2; n_of[kind] = sec->n; }
        } else if (n_of[kind] == nr - 1 || n_of[kind] >= nr + 1) {
            /* A line more whose last is a blank could be a separator: it
             * yields to a section whose count is wrong for certain. */
            want[kind] = n_of[kind] == nr + 1 && sec->v[nr].blank ? 1 : 2;
        }
        /* In vtt's form with one section ending in a blank, a section with
         * its count right and a blank last line may have lost a row and
         * kept its separator (a file with blanks after some sections): a
         * witness, weighed against an answer that rests on a blank (below). */
        else if (seps == 1 && n_of[kind] == nr && nr > 0 && sec->v[nr - 1].blank) wmask |= 1 << kind;
        any |= want[kind] != 0;
    }
    int definite = 0;
    for (int kind = MAPIO_TILES; kind <= MAPIO_HEDGES; kind++) definite += want[kind] == 2;
    /* Two sections wrong for certain: each is scored against the other,
     * damaged, and the best fit is a compromise between them. The loader's
     * findings say each is short or long; no place is told. */
    if (definite >= 2) return;
    if (definite) for (int kind = MAPIO_TILES; kind <= MAPIO_HEDGES; kind++) if (want[kind] == 1) want[kind] = 0;
    if (!any) return;
    /* The other sections as their first rows say: the loaded map's own,
     * unless a section came up short -- then it swallowed what followed it,
     * a whole section may be unread, and the text is read again. */
    uint8_t *arr[3];
    int short_one = 0;
    for (int kind = MAPIO_TILES; kind <= MAPIO_HEDGES; kind++) short_one |= secs[kind].n < secs[kind].rows;
    for (int kind = MAPIO_TILES; kind <= MAPIO_HEDGES; kind++) {
        if (!short_one) { arr[kind] = kind == MAPIO_TILES ? m->tiles : kind == MAPIO_VEDGES ? m->vedges : m->hedges; continue; }
        int rows = mapio_section_rows(kind, h), width = mapio_section_width(kind, w);
        arr[kind] = xcalloc((size_t)rows * (size_t)width, 1);
        for (int y = 0; y < rows && y < secs[kind].n; y++)
            mapio_row_parse(kind, secs[kind].v[y].text, secs[kind].v[y].len, arr[kind] + (size_t)y * (size_t)width, width);
    }
    /* The things given by coordinates, by row: every square of a creature,
     * each note, each end of a link on this map. */
    int cap = 64, nth = 0, *ty = xmalloc((size_t)cap * sizeof *ty), *tx = xmalloc((size_t)cap * sizeof *tx), *tid = xmalloc((size_t)cap * sizeof *tid);
#define THING(X, Y, ID) do { if ((X) >= 0 && (X) < w && (Y) >= 0 && (Y) < h) { \
        if (nth == cap) { cap *= 2; ty = xrealloc(ty, (size_t)cap * sizeof *ty); tx = xrealloc(tx, (size_t)cap * sizeof *tx); tid = xrealloc(tid, (size_t)cap * sizeof *tid); } \
        ty[nth] = (Y); tx[nth] = (X); tid[nth] = (ID); nth++; } } while (0)
    for (int i = 0; i < m->tokens.n; i++) {
        const Token *t = &m->tokens.v[i];
        for (int y = t->y; y < t->y + t->size; y++)
            for (int x = t->x; x < t->x + t->size; x++) THING(x, y, i);
    }
    for (int i = 0; i < m->nnotes; i++) THING(m->notes[i].x, m->notes[i].y, -1);
    for (int i = 0; i < m->nlinks; i++) {
        const Link *l = &m->links[i];
        for (int e = 0; e < (l->to_map[0] ? 1 : 2); e++)
            for (int y = l->y[e]; y < l->y[e] + l->size; y++)
                for (int x = l->x[e]; x < l->x[e] + l->size; x++) THING(x, y, -1);
    }
#undef THING
    ShiftScan sc = { .w = w, .h = h, .T = arr[MAPIO_TILES], .V = arr[MAPIO_VEDGES], .H = arr[MAPIO_HEDGES] };
    sc.ttrans = xcalloc((size_t)h + 1, sizeof *sc.ttrans);
    for (int y = 0; y < h; y++) {
        const uint8_t *t = sc.T + (size_t)y * (size_t)w;
        for (int x = 1; x < w; x++) sc.ttrans[y] += shift_void(t, x - 1, w) != shift_void(t, x, w);
    }
    sc.thing_start = xcalloc((size_t)h + 2, sizeof *sc.thing_start);
    sc.thing_x = xmalloc((size_t)(nth ? nth : 1) * sizeof *sc.thing_x);
    sc.thing_id = xmalloc((size_t)(nth ? nth : 1) * sizeof *sc.thing_id);
    for (int i = 0; i < nth; i++) sc.thing_start[ty[i] + 1]++;
    for (int y = 0; y < h; y++) sc.thing_start[y + 1] += sc.thing_start[y];
    {
        int *fill = xcalloc((size_t)h + 1, sizeof *fill);
        for (int i = 0; i < nth; i++) {
            int at = sc.thing_start[ty[i]] + fill[ty[i]]++;
            sc.thing_x[at] = tx[i];
            sc.thing_id[at] = tid[i];
        }
        free(fill);
    }
    free(ty); free(tx); free(tid);

    for (int kind = MAPIO_TILES; kind <= MAPIO_HEDGES; kind++) {
        const MapioSection *sec = &secs[kind];
        if (!want[kind]) continue;
        int nr = sec->rows, n = n_of[kind];
        Shift sh;
        memset(&sh, 0, sizeof sh);
        sh.kind = kind;
        if (n >= nr + 1) { sh.extra = 1; sh.more = n > nr + 1; n = nr + 1; }
        else if (n == nr - 1) sh.extra = -1;
        else continue;
        sc.kind = kind;
        /* With open edges first: they find a void row put into plain floor,
         * which nothing else shows. Kept only when the fix leaves no floor
         * against void unwalled -- a map drawn with walls; in a cave or a
         * pillared hall they would move the answer onto the cave. */
        Shift first = sh;
        sc.use_open = 1;
        shift_search(&sc, sec, n, &first);
        if (first.found && first.part[SH_OPEN] == 0) sh = first;
        else {
            sc.use_open = 0;
            shift_search(&sc, sec, n, &sh);
        }
        sh.blank_only = want[kind] == 1;
        if (!sh.found && sh.flawed) flawed = sh.flawed;
        if (sh.found && (!out->found || sh.score0 - sh.score > out->score0 - out->score)) *out = sh;
    }
    /* An answer resting on a trailing blank needs a fix that leaves the map
     * clean (a map's flaws of its own would lend a separator a meaning), and
     * is placed only where the other reading cannot hold: with a section
     * that ends in an empty row, "a line too many here" and "a row lost
     * there, the blanks separators" line the rows up alike (a file with a
     * blank after every section and two rows lost reads just so), and both
     * are said, with no place. Never a wrong line: one followed by hand
     * wrecks a map. */
    if (out->found && out->blank_only && out->score > 0) memset(out, 0, sizeof *out);
    int use_open = out->part[SH_OPEN] == 0;
    if (out->found && out->blank_only && wmask) {
        JointSide side[2];
        int ns = 0;
        for (int kind = MAPIO_TILES; kind <= MAPIO_HEDGES; kind++)
            if (wmask >> kind & 1)
                side[ns++] = (JointSide){ kind, -1, arr[kind], secs[kind].rows - 1, 1, 1, 0 };
        /* One witness: the other side is the answer's section, as read. */
        if (ns == 1) side[ns++] = (JointSide){ out->kind, 1, arr[out->kind], secs[out->kind].rows, 1, 0, INT_MAX - 1 };
        if (shift_joint(&sc, side, use_open, SHIFT_TIE)) out->either = wmask;
    }
    /* A row lost from a section whose last line is a blank leaves its
     * count right: the blank read as its last row (a separator, kept by
     * hand). Such a section's damage is unseen, and the answer was scored
     * beside it: if it and the answer's section moved together fit as well
     * with the answer's place anywhere else, no place is sure. */
    if (out->found && !out->either && !out->nowhere) {
        const MapioSection *xs = &secs[out->kind];
        int xw = mapio_section_width(out->kind, w), xn = out->n;
        uint8_t *xa = xcalloc((size_t)(xn + 1) * (size_t)xw, 1);
        for (int i = 0; i < xn && i < xs->n; i++)
            mapio_row_parse(out->kind, xs->v[i].text, xs->v[i].len, xa + (size_t)i * (size_t)xw, xw);
        int unsure = 0;
        for (int kind = MAPIO_TILES; kind <= MAPIO_HEDGES && !unsure; kind++) {
            const MapioSection *ws = &secs[kind];
            int nr = ws->rows;
            /* Its last row as loaded a blank, and only blanks after it (a
             * file may keep two). */
            int tail = ws->n >= nr && nr >= 1 && ws->v[nr - 1].blank;
            for (int i = nr; i < ws->n && tail; i++) tail = ws->v[i].blank;
            if (kind == out->kind || !ws->header || !tail) continue;
            JointSide side[2] = {
                { out->kind, out->extra, xa, xn, 0, out->kfirst, out->klast },
                { kind, -1, arr[kind], nr - 1, 0, 1, 0 },
            };
            unsure = shift_joint(&sc, side, use_open, out->score + SHIFT_TIE);
        }
        free(xa);
        if (unsure) {
            int kd = out->kind, ex = out->extra;
            memset(out, 0, sizeof *out);
            out->kind = kd;
            out->extra = ex;
            /* A row lost for certain is still said, without a place. */
            if (ex < 0) { out->found = 1; out->nowhere = 1; }
            else out->withdrawn = kd + 1;
        }
    }

    if (!out->flawed) out->flawed = flawed;
    free(sc.thing_start); free(sc.thing_x); free(sc.thing_id); free(sc.ttrans);
    if (short_one) for (int kind = MAPIO_TILES; kind <= MAPIO_HEDGES; kind++) free(arr[kind]);
}

/* The file's text with the shift undone: the line deleted, or the lost
 * row put back after fix_line (empty, or a copy of copy_line). */
static void text_line(const char *text, size_t len, int want, char *buf, size_t cap);

static char *shift_fixed_text(const char *text, size_t len, const Shift *sh, size_t *out_len)
{
    char   copy[MAP_MAX_DIM + 64] = "";
    if (sh->copy_line) text_line(text, len, sh->copy_line, copy, sizeof copy);
    size_t copy_n = strlen(copy);
    char  *out = xmalloc(len + copy_n + 3);
    size_t o = 0, at = 0;
    int    line = 0;
    while (at < len) {
        const char *nl = memchr(text + at, '\n', len - at);
        size_t      n  = nl ? (size_t)(nl - (text + at)) + 1 : len - at;
        line++;
        if (!(sh->extra > 0 && line == sh->fix_line)) { memcpy(out + o, text + at, n); o += n; }
        if (sh->extra < 0 && line == sh->fix_line) {
            if (!nl) out[o++] = '\n';
            memcpy(out + o, copy, copy_n);
            o += copy_n;
            out[o++] = '\n';
        }
        at += n;
    }
    out[o] = '\0';
    *out_len = o;
    return out;
}

/* A finding's place in the file with the shift undone: lines past the fix
 * move by one, and so do the suspect section's own boundaries below the seam
 * (and a room's first square, when the tiles moved). 0 when it has no place
 * there: it was on the very line taken out. */
typedef struct { char code[8]; int line, x, y, edge; } FKey;

static int fkey_cmp(const void *pa, const void *pb)
{
    const FKey *a = pa, *b = pb;
    int c = strcmp(a->code, b->code);
    if (c) return c;
    if (a->line != b->line) return a->line - b->line;
    if (a->y != b->y) return a->y - b->y;
    if (a->x != b->x) return a->x - b->x;
    return a->edge - b->edge;
}

static int shift_key(const Shift *sh, const Finding *f, int moved, FKey *k)
{
    memset(k, 0, sizeof *k);
    str_lcpy(k->code, f->code, sizeof k->code);
    k->x = f->x; k->y = f->y; k->edge = f->edge;
    if (f->line > 0) {
        k->line = f->line;
        if (!moved) return 1;
        if (sh->extra > 0) {
            if (f->line == sh->fix_line) return 0;
            if (f->line > sh->fix_line) k->line--;
        } else if (f->line > sh->fix_line) k->line++;
        return 1;
    }
    int own = (sh->kind == MAPIO_VEDGES && f->edge == 'v') || (sh->kind == MAPIO_HEDGES && f->edge == 'h') ||
              (sh->kind == MAPIO_TILES && !strcmp(f->code, "W120"));
    if (!moved || !own || f->y < sh->k) return 1;
    if (sh->extra > 0) {
        if (f->y == sh->k) return 0;
        k->y--;
    } else k->y++;
    return 1;
}

static FindingMore *finding_more(Finding *f)
{
    if (!f->more) f->more = xcalloc(1, sizeof *f->more);
    return f->more;
}

/* Line `want` of the text, at most `cap`-1 bytes of it. */
static void text_line(const char *text, size_t len, int want, char *buf, size_t cap)
{
    buf[0] = '\0';
    int    line = 1;
    size_t at = 0;
    while (at < len && line < want) {
        const char *nl = memchr(text + at, '\n', len - at);
        if (!nl) return;
        at = (size_t)(nl - text) + 1;
        line++;
    }
    if (line != want || at > len) return;
    size_t n = 0;
    while (at + n < len && text[at + n] != '\n' && text[at + n] != '\r' && n + 1 < cap) n++;
    memcpy(buf, text + at, n);
    buf[n] = '\0';
}

static void snip_line(char *out, size_t cap, const char *text, size_t len, int line, const char *mark)
{
    char row[64];
    text_line(text, len, line, row, sizeof row);
    size_t n = strlen(row);
    if (n > 40) { row[37] = '.'; row[38] = '.'; row[39] = '.'; row[40] = '\0'; }
    size_t at = strlen(out);
    if (mark) snprintf(out + at, cap - at, " %4d | %-40s  <- %s\n", line, row, mark);
    else      snprintf(out + at, cap - at, " %4d | %s\n", line, row);
}

static const char *section_name(int kind)
{
    return kind == MAPIO_TILES ? "tiles" : kind == MAPIO_VEDGES ? "vedges" : kind == MAPIO_HEDGES ? "hedges" : "fog";
}

/* A section with a line too many for certain -- a line past its rows the
 * loader reads as a stray row -- and no place told: its count and its
 * lines, where an added line usually is, and the loader's stray-row
 * findings past its rows folded in. `why`: 0 nothing on the map tells the
 * places apart, 1 a hidden loss fits as well as the place, 2 another
 * section is wrong too, 3 the best place's fix leaves mistakes. */
static void say_count(const MapioSection *sec, const char *name, int why, const char *path, Findings *fs)
{
    int nr = sec->rows, n = sec->n;
    while (n > nr && !sec->v[n - 1].row) n--;          /* through the last stray row */
    int first = sec->v[0].line, last = sec->v[n - 1].line;
    /* Exact only when every line past the rows reads as a row: a blank
     * there may be a separator or an added empty line, and a line set
     * aside an ignored note or a row with a stray character. */
    int strays = 0, exact = !sec->dropped;
    for (int i = nr; i < n; i++) { strays += sec->v[i].row; exact &= sec->v[i].row; }
    if (sec->dropped && sec->last > last) last = sec->last;
    Finding *cf = add_finding(fs, "W031", "row-extra");
    cf->line = sec->header;
    const char *tail = why == 2 ? "with another section wrong too no place is told" : "the map does not show which";
    if (exact)
        finding_say(cf, "'%s' has %d lines where the map has %d rows: %s of lines %d-%d %s too many, and %s",
                 name, n, nr, n - nr == 1 ? "one" : "some", first, last, n - nr == 1 ? "is one" : "are", tail);
    else if (strays == 1)
        finding_say(cf, "'%s' has more lines than its %d rows: at least one of lines %d-%d is too many, and %s",
                 name, nr, first, last, tail);
    else
        finding_say(cf, "'%s' has more lines than its %d rows: at least %d of lines %d-%d are too many, and %s",
                 name, nr, strays, first, last, tail);
    FindingMore *mo = finding_more(cf);
    static const char *const ev[] = {
        "no place to take a line out of fits the rest of the map better than another (rows alike, or only wall ends that meet nothing differ, which a doorway has by design)",
        "a place fits, but a row lost before a blank line in another section, with the line taken out elsewhere, fits as well",
        "another section has more or fewer lines than rows too, and each would be scored beside the other's damage",
        "a place fits best, but its fix leaves walls on void or loose ends still: the map has mistakes of its own, and a wrong place can fit them better than the true one",
    };
    snprintf(mo->evidence, sizeof mo->evidence, "the count is certain (a line past its rows reads as a row); %s", ev[why]);
    /* Where an editor's added line usually is: an empty line, or one that
     * repeats the line above. Named, not chosen; runs grouped. */
    char look[240] = "";
    int said = 0;
    /* The lines past the rows: a ruler or a note there is itself the line
     * too many, if it is no row of the map. */
    if (n - nr > 1) snprintf(look, sizeof look, "lines %d-%d past its rows (themselves too many, if no rows of the map)", sec->v[nr].line, sec->v[n - 1].line);
    else            snprintf(look, sizeof look, "line %d past its rows (itself the line too many, if no row of the map)", sec->v[nr].line);
    said = 1;
    for (int i = 0; i < nr && said < 5; i++) {
        int j = i;
        while (j + 1 < nr && sec->v[j + 1].blank == sec->v[i].blank &&
               (sec->v[i].blank || (sec->v[j + 1].len == sec->v[i].len && !memcmp(sec->v[j + 1].text, sec->v[i].text, (size_t)sec->v[i].len)))) j++;
        size_t at = strlen(look);
        if (sec->v[i].blank && j > i) snprintf(look + at, sizeof look - at, "%slines %d-%d (empty)", said++ ? ", " : "", sec->v[i].line, sec->v[j].line);
        else if (sec->v[i].blank)     snprintf(look + at, sizeof look - at, "%sline %d (empty)", said++ ? ", " : "", sec->v[i].line);
        else if (j > i)               snprintf(look + at, sizeof look - at, "%slines %d-%d (alike: any one of them)", said++ ? ", " : "", sec->v[i].line, sec->v[j].line);
        i = j;
    }
    if (said)
        snprintf(mo->help, sizeof mo->help, "compare lines %d-%d with a copy of the map (the autosave, a backup) and delete what is too many, then vtt %s --check; look first at %s -- an added line is most often empty or a repeat of the line above",
                 first, last, path, look);
    else
        snprintf(mo->help, sizeof mo->help, "compare lines %d-%d with a copy of the map (the autosave, a backup) and delete what is not in it, then vtt %s --check",
                 first, last, path);
    /* The loader's stray rows past the section's rows are these lines. */
    int ci = (int)(cf - fs->v);
    for (int i = 0; i < fs->n; i++) {
        Finding *f = &fs->v[i];
        if (strcmp(f->code, "W019") || f->line < sec->v[nr].line || f->line > last) continue;
        f->folded = 1;
        mo = fs->v[ci].more;
        mo->nfolded++;
        snprintf(mo->folded, sizeof mo->folded, "W019 x%d", mo->nfolded);
    }
}

/* The search, and when it finds a shift, its finding and the fold: the file
 * read again and checked with the shift undone, and every finding of this
 * one that check does not have counted into it instead of listed. */
static void check_shift(const Map *m, const char *text, size_t len, const char *path, Findings *fs)
{
    MapioSection secs[MAPIO_SECTIONS];
    mapio_sections(text, len, m->w, m->h, secs);
    Shift sh;
    shift_find(m, secs, &sh);
    /* Lines set aside past its rows may be rows too (a stray character):
     * more may be wrong than the answer's line. */
    if (sh.found && sh.extra > 0 && secs[sh.kind].dropped) sh.more = 1;
    /* What the count makes certain and no place told says -- when the file
     * has each row section once: with one missing, a surplus may be its
     * rows under a lost header; with one twice, the loader reads the other. */
    int wrong = 0, whole = 1;
    for (int kind = MAPIO_TILES; kind <= MAPIO_HEDGES; kind++) {
        wrong += secs[kind].header && (rows_past(&secs[kind]) || secs[kind].n < secs[kind].rows);
        whole &= secs[kind].header && !secs[kind].repeated;
    }
    for (int kind = MAPIO_TILES; kind <= MAPIO_HEDGES && whole; kind++) {
        if (!secs[kind].header || !rows_past(&secs[kind])) continue;
        if (sh.found && sh.kind == kind) continue;
        say_count(&secs[kind], section_name(kind), wrong >= 2 ? 2 : sh.withdrawn == kind + 1 ? 1 : sh.flawed == kind + 1 ? 3 : 0, path, fs);
    }
    if (!sh.found) { mapio_sections_free(secs); return; }
    const MapioSection *sec = &secs[sh.kind];
    const char *name = section_name(sh.kind);


    /* Its finding: a lost row's is the loader's E011 told more, a line too
     * many's is new. */
    Finding *pf = NULL;
    char msg[256];
    if (sh.extra < 0) {
        /* The loader's E011 for this section: the record it swallowed read
         * "as NAME row", or the file ending "inside 'NAME'". */
        char as_row[32], inside[32];
        snprintf(as_row, sizeof as_row, "read as %s row", name);
        snprintf(inside, sizeof inside, "inside '%s'", name);
        for (int i = 0; i < fs->n && !pf; i++)
            if (!strcmp(fs->v[i].code, "E011") && fs->v[i].line > 0 &&
                (strstr(fs->v[i].msg, as_row) || strstr(fs->v[i].msg, inside))) pf = &fs->v[i];
        if (pf) {
            char *cut = strstr(pf->msg, ": the section is short");
            if (cut) {
                char swallowed[16] = "";
                text_line(text, len, sec->next, swallowed, sizeof swallowed);
                int header = !strcmp(swallowed, "tiles") || !strcmp(swallowed, "vedges") ||
                             !strcmp(swallowed, "hedges") || !strcmp(swallowed, "fog");
                finding_say(pf, "%.*s: a '%s' row is missing%s%s%s", (int)(cut - pf->msg), pf->msg, name,
                            header ? ", and '" : "", header ? swallowed : "", header ? "' was not read" : "");
            }
        } else if (sec->n > 0) {
            pf = add_finding(fs, "E011", "section-short");
            pf->line = sec->v[sec->n - 1].line;
            finding_say(pf, "'%s' is one row short: the blank line %d after it was read as its last row",
                     name, pf->line);
        } else {
            pf = add_finding(fs, "E011", "section-short");
            pf->line = sec->header;
            finding_say(pf, "'%s' has none of its rows", name);
        }
    } else {
        pf = add_finding(fs, "W031", "row-extra");
        pf->line = sh.fix_line;
        if (sh.blank_only && sh.span1 > sh.span0)
            snprintf(msg, sizeof msg, "'%s' ends in a blank line after its rows: unless that is a separator, one of lines %d-%d is one too many",
                     name, sh.span0, sh.span1);
        else if (sh.blank_only)
            snprintf(msg, sizeof msg, "'%s' ends in a blank line after its rows: unless that is a separator, line %d is one too many",
                     name, sh.fix_line);
        else if (sh.tie && sh.stray) snprintf(msg, sizeof msg, "'%s' has one line more than its rows: one of lines %d-%d is not a row of it",
                                         name, sh.span0, sh.span1);
        else if (sh.tie) snprintf(msg, sizeof msg, "'%s' has one row too many, somewhere in lines %d-%d: the rows below it are one lower than the rest of the map",
                             name, sh.span0, sh.span1);
        else        snprintf(msg, sizeof msg, "'%s' has one row too many: from line %d down its rows are one lower than the rest of the map",
                             name, sh.fix_line);
        finding_say(pf, "%s", msg);
    }
    int pi = (int)(pf - fs->v);

    if (sh.either) {
        /* Two readings that line the rows up alike: both said, no place. */
        char others[48] = "", which[64] = "";
        int nothers = 0;
        for (int kind = MAPIO_TILES; kind <= MAPIO_HEDGES; kind++) {
            if (!(sh.either >> kind & 1)) continue;
            size_t at = strlen(others), wt = strlen(which);
            snprintf(others + at, sizeof others - at, "%s'%s'", nothers ? " and " : "", section_name(kind));
            snprintf(which + wt, sizeof which - wt, "%s'%s'", nothers ? ", " : "", section_name(kind));
            nothers++;
        }
        if (nothers > 1) str_lcpy(which + strlen(which), " or both", sizeof which - strlen(which));
        pf->line = sec->header;
        finding_say(pf, "'%s' ends in a blank line after its rows and %s end%s in an empty row: either '%s' has one line too many, or the blank is a separator and %s lost a row; the text cannot tell which",
                 name, others, nothers == 1 ? "s" : "", name, which);
        FindingMore *mo = finding_more(pf);
        snprintf(mo->evidence, sizeof mo->evidence,
                 "a line taken out of '%s' lines its rows up with the rest of the map, and a row put back into %s, with the blank lines read as separators, would as well or within %d of the walls on void and loose ends counted",
                 name, which, SHIFT_TIE);
        /* Which holds is the writer's to know: whether they keep blank
         * lines between sections by hand. Each reading's fix, said. */
        char fix[160];
        if (sh.tie)                   snprintf(fix, sizeof fix, "the extra line is one of lines %d to %d: delete the one that repeats the line beside it or does not belong", sh.span0, sh.span1);
        else if (sh.span1 > sh.span0) snprintf(fix, sizeof fix, "delete line %d (or any of lines %d-%d: they are the same row)", sh.fix_line, sh.span0, sh.span1);
        else                          snprintf(fix, sizeof fix, "delete line %d", sh.fix_line);
        snprintf(mo->help, sizeof mo->help,
                 "if this file keeps a blank line after its sections (written by hand), %s lost a row: compare %s with a copy of the map (the autosave, a backup) and put it back; if not (vtt writes none), %s; then vtt %s --check",
                 which, others, fix, path);
        /* The fold below is the first reading's: said so. */
    }

    else if (sh.nowhere) {
        /* A row is missing for certain, and no place for it stands out. */
        FindingMore *mo = finding_more(pf);
        snprintf(mo->evidence, sizeof mo->evidence, "%s", sh.quiet
                 ? "nothing on the map tells one place from another (rows alike, or only wall ends that meet nothing differ, which a doorway has by design), so no place is told"
                 : "the map has walls on void or loose ends of its own, and a row put back in the wrong place can seem to mend them, so no place is told");
        snprintf(mo->help, sizeof mo->help,
                 "the missing row is one of the %d after line %d: compare them with a copy of the map (the autosave, a backup), put it back (an empty line, if it held nothing), then vtt %s --check",
                 sec->rows, sec->header, path);
        /* The fold, as for a tie: a row put back anywhere in the section
         * gives the same file below it, so what the file's own lines said
         * of the swallowed header and the rows after it goes, wherever. */
        sh.tie = 1;
        sh.fill = 0;
        sh.copy_line = 0;
        sh.k = mapio_section_rows(sh.kind, m->h) - 1;
        sh.fix_line = sec->n ? sec->v[sec->n - 1].line : sec->header;
    }

    /* The fold. */
    {
        PROF_ZONE("check.fold");
        size_t flen;
        char  *fixed = shift_fixed_text(text, len, &sh, &flen);
        Findings f2 = { 0 };
        char err[256];
        Map *m2 = mapio_load_text_diag(path, fixed, flen, err, sizeof err, file_finding, &f2);
        if (m2) {
            check_map(m2, &f2);
            map_free(m2);
            FKey *keys = xmalloc((size_t)(f2.n ? f2.n : 1) * sizeof *keys);
            for (int i = 0; i < f2.n; i++) shift_key(&sh, &f2.v[i], 0, &keys[i]);
            qsort(keys, (size_t)f2.n, sizeof *keys, fkey_cmp);
            struct { char code[8]; int n; } tally[16];
            int ntally = 0, nfold = 0;
            for (int i = 0; i < fs->n; i++) {
                if (i == pi) continue;
                /* A tie is a span of fixes, so only what the file's form
                 * said (a stray row, a short section) is surely its doing;
                 * the map's findings stay listed. */
                /* The stray row may be the extra line itself: its W019 says
                 * so exactly, and stays. */
                if (sh.stray && sh.tie && !strcmp(fs->v[i].code, "W019")) continue;
                if (sh.tie && fs->v[i].line <= 0) {
                    /* ...but for one thing: a lost row's section read what
                     * followed it (a header, a record) as its last row, and
                     * whatever stands on that row is the shift's doing. */
                    const Finding *f = &fs->v[i];
                    int own = (sh.kind == MAPIO_VEDGES && f->edge == 'v') || (sh.kind == MAPIO_HEDGES && f->edge == 'h');
                    if (!(sh.extra < 0 && own && f->y == mapio_section_rows(sh.kind, m->h) - 1 && sec->next)) continue;
                }
                FKey k;
                int  there = shift_key(&sh, &fs->v[i], 1, &k) && f2.n &&
                             bsearch(&k, keys, (size_t)f2.n, sizeof *keys, fkey_cmp);
                if (there) continue;
                fs->v[i].folded = 1;
                nfold++;
                int t = 0;
                while (t < ntally && strcmp(tally[t].code, fs->v[i].code)) t++;
                if (t == ntally && ntally < 16) { str_lcpy(tally[ntally].code, fs->v[i].code, sizeof tally[0].code); tally[ntally++].n = 0; }
                if (t < ntally) tally[t].n++;
            }
            pf = &fs->v[pi];
            if (nfold) {
                FindingMore *mo = finding_more(pf);
                mo->nfolded = nfold;
                for (int t = 0; t < ntally; t++) {
                    size_t at = strlen(mo->folded);
                    snprintf(mo->folded + at, sizeof mo->folded - at, "%s%s x%d", t ? ", " : "", tally[t].code, tally[t].n);
                }
            }
            free(keys);
        }
        for (int i = 0; i < f2.n; i++) finding_free(&f2.v[i]);
        free(f2.v);
        free(fixed);
    }

    if (sh.nowhere) {
        mapio_sections_free(secs);
        return;
    }
    if (sh.either) {
        FindingMore *mo = finding_more(pf);
        if (mo->nfolded) {
            mo->fold_if = 1;
            if (sh.tie) snprintf(mo->note, sizeof mo->note, "the findings folded in go away with the extra line among lines %d-%d deleted (the reading without separators); with separators, check again once the rows are back", sh.span0, sh.span1);
            else        snprintf(mo->note, sizeof mo->note, "the findings folded in go away with line %d deleted (the reading without separators); with separators, check again once the rows are back", sh.fix_line);
        }
        mapio_sections_free(secs);
        return;
    }

    /* What it says. */
    FindingMore *mo = finding_more(pf);
    mo->span0   = sh.span0;
    mo->span1   = sh.span1;
    mo->certain = sh.certain && !sh.blank_only;
    char *ev = mo->evidence;
    size_t cap = sizeof mo->evidence;
    if (sh.extra > 0) snprintf(ev, cap, sh.tie ? "with the extra line found and taken out" : "with line %d gone", sh.fix_line);
    else if (sh.at_end) snprintf(ev, cap, "its rows as read fit the walls, creatures and notes of the rest of the map, so as far as the map shows, what is missing is its last row, and it held nothing");
    else if (sh.copy_line) snprintf(ev, cap, "with a copy of line %d put in after line %d", sh.copy_line, sh.fix_line);
    else                   snprintf(ev, cap, "with an empty line put in after line %d", sh.fix_line);
    int parts = 0;
#define EV(...) do { size_t at_ = strlen(ev); snprintf(ev + at_, cap - at_, "%s", parts++ ? ", " : ": "); \
                     at_ = strlen(ev); snprintf(ev + at_, cap - at_, __VA_ARGS__); } while (0)
    int dw = sh.part0[SH_WALLS] - sh.part[SH_WALLS], de = sh.part0[SH_ENDS] - sh.part[SH_ENDS];
    int dt = sh.part0[SH_THINGS] - sh.part[SH_THINGS];
    if (dw > 0) EV("%d wall%s and door%s that stand on void stand on map", dw, dw == 1 ? "" : "s", dw == 1 ? "" : "s");
    if (de > 0) EV("%d loose wall end%s meet%s another wall", de, de == 1 ? "" : "s", de == 1 ? "s" : "");
    int dopen = sh.part0[SH_OPEN] - sh.part[SH_OPEN];
    if (dopen > 0 && sh.part[SH_OPEN] == 0)
        EV("%d place%s where floor met void with no wall between %s gone", dopen, dopen == 1 ? "" : "s", dopen == 1 ? "is" : "are");
    if (dt > 0 && sh.thing >= 0)
        EV("%s stands on map instead of void%s", label_or_unnamed(&m->tokens.v[sh.thing]), dt > 1 ? ", and more creatures, notes or links with it" : "");
    else if (dt > 0) EV("%d square%s of creatures, notes or links stand%s on map instead of void", dt, dt == 1 ? "" : "s", dt == 1 ? "s" : "");
#undef EV
    if (!parts && !sh.at_end)
        snprintf(ev, cap, "the section has %s, and nothing on the map shows which place it was: the span is every place that fits as well",
                 sh.extra > 0 ? "one line more than its rows" : "one row fewer than the map's size says");

    if (sh.extra > 0) {
        if (sh.tie)
            snprintf(mo->help, sizeof mo->help, "the extra line is one of lines %d to %d: look for one that repeats the line beside it or does not belong, delete it, then vtt %s --check",
                     sh.span0, sh.span1, path);
        else if (sh.span1 > sh.span0)
            snprintf(mo->help, sizeof mo->help, "delete line %d (or any of lines %d-%d: they are the same row); then vtt %s --check",
                     sh.fix_line, sh.span0, sh.span1, path);
        else
            snprintf(mo->help, sizeof mo->help, "delete line %d; then vtt %s --check", sh.fix_line, path);
        if (sh.more) str_lcpy(mo->note, "there are more lines than rows past this one too: check again once it is fixed", sizeof mo->note);
        if (sh.blank_only) {
            /* Both readings, said: the help as it stands is for one. */
            char h2[sizeof mo->help];
            snprintf(h2, sizeof h2, "if this file keeps a blank line after some of its sections (by hand), that blank is a separator and nothing is wrong here; otherwise: %s", mo->help);
            str_lcpy(mo->help, h2, sizeof mo->help);
        }
    } else {
        char where[64];
        if (sh.tie)                   snprintf(where, sizeof where, "somewhere after line %d and before line %d", sh.span0, sh.span1 + 1);
        else if (sh.span1 > sh.span0) snprintf(where, sizeof where, "after any of lines %d-%d (the same place)", sh.span0, sh.span1);
        else                          snprintf(where, sizeof where, "after line %d", sh.fix_line);
        if (sh.at_end)
            snprintf(mo->help, sizeof mo->help, "add an empty line after line %d, the section's last%s, then vtt %s --check; if a row that held something was lost, take it from a copy of the map (the autosave, a backup)",
                     sh.fix_line, sh.span1 > sh.span0 ? " (or after any of the empty lines before it: the same)" : "", path);
        else if (sh.copy_line)
            snprintf(mo->help, sizeof mo->help, "the rows beside it are alike, so the missing one most likely was too: add a copy of line %d %s, then vtt %s --check; if it was not, take it from a copy of the map (the autosave, a backup)",
                     sh.copy_line, where, path);
        else
            snprintf(mo->help, sizeof mo->help, "if the missing row held nothing, add an empty line %s; if it held something, take it from a copy of the map (the autosave, a backup)",
                     where);
    }

    /* The file's lines round it. */
    char *sn = mo->snippet;
    size_t scap = sizeof mo->snippet;
    snprintf(sn, scap, "      |\n");
    if (sh.extra > 0) {
        int a = sh.span0, b = sh.span1;
        if (a - 1 >= 1) snip_line(sn, scap, text, len, a - 1, NULL);
        char row[64], above[64];
        text_line(text, len, sh.fix_line, row, sizeof row);
        text_line(text, len, sh.fix_line - 1, above, sizeof above);
        const char *mark = sh.tie ? "the extra line is from here..."
                         : b > a ? "these lines are one row, written"
                         : row[0] && !strcmp(row, above) ? "this line repeats the one above it"
                         : sh.certain ? "this line" : "this line, most likely";
        snip_line(sn, scap, text, len, a, mark);
        if (b > a) {
            if (b - a > 1) { size_t at = strlen(sn); snprintf(sn + at, scap - at, "  ... |\n"); }
            char last[64];
            snprintf(last, sizeof last, "%d times where the map has %d: delete one", b - a + 1, b - a);
            snip_line(sn, scap, text, len, b, sh.tie ? "...to here" : last);
        }
        snip_line(sn, scap, text, len, b + 1, NULL);
    } else {
        snip_line(sn, scap, text, len, sh.fix_line, NULL);
        size_t at = strlen(sn);
        snprintf(sn + at, scap - at, "      + %-40s  <- a row is missing here%s\n", "",
                 sh.tie ? ", or near it" : sh.span1 > sh.span0 ? " (or anywhere among the lines like it round it)" : "");
        snip_line(sn, scap, text, len, sh.fix_line + 1, NULL);
    }
    (void)sec;
    mapio_sections_free(secs);
}

/* Sorts and prints the findings, `file` naming what was checked (NULL for
 * none), and frees them. Returns the exit status. */
/* A report lists at most this many findings of one code (and slug): a map
 * with a finding a square has 262,145, and a list nobody can read helps no
 * one (the user, 2026-10-20; docs/V2.md, *Memory: the plan*, 5). The rest are
 * counted, after every fold has run, so the count is exact, and the totals
 * count them all. */
#define FINDINGS_LISTED_MAX 10000

typedef struct { char code[8], slug[24]; int listed, unlisted; } Listed;

/* Whether finding f is listed, counting it against its code; once the table
 * of codes is full (it never is: there are fewer codes) everything is. */
static int listed(Listed *t, int *nt, int cap, const Finding *f)
{
    int i = 0;
    while (i < *nt && (strcmp(t[i].code, f->code) || strcmp(t[i].slug, f->slug))) i++;
    if (i == *nt) {
        if (*nt == cap) return 1;
        str_lcpy(t[i].code, f->code, sizeof t[i].code);
        str_lcpy(t[i].slug, f->slug, sizeof t[i].slug);
        t[i].listed = t[i].unlisted = 0;
        (*nt)++;
    }
    if (t[i].listed < FINDINGS_LISTED_MAX) { t[i].listed++; return 1; }
    t[i].unlisted++;
    return 0;
}

static int check_report(FILE *out, Findings *pfs, const char *file, int json)
{
    Findings fs = *pfs;
    /* What a finding folded is counted in it, not listed, nor counted again. */
    int kept = 0;
    for (int i = 0; i < fs.n; i++) {
        if (fs.v[i].folded) { finding_free(&fs.v[i]); continue; }
        fs.v[kept++] = fs.v[i];
    }
    fs.n = kept;
    group_void_findings(&fs);
    if (fs.n) qsort(fs.v, (size_t)fs.n, sizeof *fs.v, finding_cmp);
    int errors = 0, warnings = 0, notes = 0;
    for (int i = 0; i < fs.n; i++) {
        errors   += fs.v[i].code[0] == 'E';
        warnings += fs.v[i].code[0] == 'W';
        notes    += fs.v[i].code[0] == 'N';
    }
    /* Each code's help said once: which rows of the table this report has. */
    enum { NHELP = sizeof CODE_HELP / sizeof CODE_HELP[0] };
    char said[NHELP] = { 0 };
    Listed lt[64];
    int    nlt = 0, unlisted = 0;

    if (json) {
        Json j;
        json_init(&j, out);
        json_open(&j, '{');
        json_key(&j, "file");
        if (file) json_str(&j, file); else json_null(&j);
        json_kint(&j, "errors", errors);
        json_kint(&j, "warnings", warnings);
        json_kint(&j, "notes", notes);
        json_key(&j, "findings");
        json_open(&j, '[');
        for (int i = 0; i < fs.n; i++) {
            const Finding *f = &fs.v[i];
            if (!listed(lt, &nlt, 64, f)) { unlisted++; continue; }
            json_open(&j, '{');
            json_kstr(&j, "code", f->code);
            json_kstr(&j, "slug", f->slug);
            json_kstr(&j, "severity", severity(f));
            json_key(&j, "line");  if (f->line > 0) json_int(&j, f->line); else json_null(&j);
            json_key(&j, "column"); if (f->col >= 0) json_int(&j, f->col); else json_null(&j);
            json_key(&j, "where"); if (f->where[0]) json_str(&j, f->where); else json_null(&j);
            json_key(&j, "x");     if (f->x >= 0) json_int(&j, f->x); else json_null(&j);
            json_key(&j, "y");     if (f->y >= 0) json_int(&j, f->y); else json_null(&j);
            json_key(&j, "edge");
            if (f->edge) { char e[2] = { (char)f->edge, 0 }; json_str(&j, e); } else json_null(&j);
            json_kstr(&j, "message", f->msg);
            const FindingMore *mo = f->more;
            json_key(&j, "help");  if (mo && mo->help[0]) json_str(&j, mo->help); else json_null(&j);
            json_key(&j, "notes");
            json_open(&j, '[');
            if (mo && mo->evidence[0]) json_str(&j, mo->evidence);
            if (mo && mo->note[0]) json_str(&j, mo->note);
            json_close(&j, ']');
            json_key(&j, "span");
            if (mo && mo->span0) {
                json_open(&j, '{');
                json_kint(&j, "first", mo->span0);
                json_kint(&j, "last", mo->span1);
                json_close(&j, '}');
            } else json_null(&j);
            json_key(&j, "folded");
            if (mo && mo->nfolded) json_str(&j, mo->folded); else json_null(&j);
            json_key(&j, "confidence");
            if (mo && mo->span0) json_str(&j, mo->certain ? "certain" : "likely"); else json_null(&j);
            json_close(&j, '}');
            const CodeHelp *h = code_help(f->code, f->slug);
            if (h) said[h - CODE_HELP] = 1;
        }
        json_close(&j, ']');
        if (unlisted) {                  /* only when there are: a usual report is as it was */
            json_key(&j, "unlisted");
            json_open(&j, '{');
            for (int k = 0; k < nlt; k++) {
                if (!lt[k].unlisted) continue;
                char key[40];
                snprintf(key, sizeof key, "%.7s/%.23s", lt[k].code, lt[k].slug);
                json_kint(&j, key, lt[k].unlisted);
            }
            json_close(&j, '}');
        }
        /* What to do about each code in it, once: "CODE", or "CODE/slug" for a
         * code with two meanings. A finding's own help is in the finding. */
        json_key(&j, "codes");
        json_open(&j, '{');
        for (int k = 0; k < NHELP; k++) {
            if (!said[k]) continue;
            char key[40];
            if (CODE_HELP[k].slug) snprintf(key, sizeof key, "%s/%s", CODE_HELP[k].code, CODE_HELP[k].slug);
            else                   str_lcpy(key, CODE_HELP[k].code, sizeof key);
            json_key(&j, key);
            json_open(&j, '{');
            json_kstr(&j, "help", CODE_HELP[k].help);
            json_key(&j, "note");
            if (CODE_HELP[k].note) json_str(&j, CODE_HELP[k].note); else json_null(&j);
            json_close(&j, '}');
        }
        json_close(&j, '}');
        json_close(&j, '}');
        fputc('\n', out);
    } else {
        for (int i = 0; i < fs.n; i++) {
            const Finding *f = &fs.v[i];
            if (!listed(lt, &nlt, 64, f)) { unlisted++; continue; }
            char loc[40];
            if (f->line > 0) snprintf(loc, sizeof loc, "line %d", f->line);
            else             str_lcpy(loc, f->where, sizeof loc);
            fprintf(out, "%s %-18s %-9s %s\n", f->code, f->slug, loc, f->msg);
            const FindingMore *mo = f->more;
            /* The code's help under its first finding, unless this one has
             * help of its own. */
            const CodeHelp *h = mo && mo->help[0] ? NULL : code_help(f->code, f->slug);
            if (h && said[h - CODE_HELP]) h = NULL;
            if (h) said[h - CODE_HELP] = 1;
            if (!mo) {
                if (h && h->note) fprintf(out, "      = note: %s\n", h->note);
                if (h)            fprintf(out, "      = help: %s\n", h->help);
                continue;
            }
            fputs(mo->snippet, out);
            if (mo->evidence[0]) fprintf(out, "      = evidence: %s\n", mo->evidence);
            if (mo->note[0])     fprintf(out, "      = note: %s\n", mo->note);
            else if (h && h->note) fprintf(out, "      = note: %s\n", h->note);
            if (mo->help[0])     fprintf(out, "      = help: %s\n", mo->help);
            else if (h)          fprintf(out, "      = help: %s\n", h->help);
            if (mo->nfolded && mo->fold_if) fprintf(out, "      = %d finding%s folded in: %s\n", mo->nfolded,
                                                    mo->nfolded == 1 ? " is" : "s are", mo->folded);
            else if (mo->nfolded && mo->joined) fprintf(out, "      = %d more folded in with it: %s\n", mo->nfolded, mo->folded);
            else if (mo->nfolded) fprintf(out, "      = %d finding%s caused by it, folded in: %s\n", mo->nfolded,
                                          mo->nfolded == 1 ? " is" : "s are", mo->folded);
        }
        for (int k = 0; k < nlt && unlisted; k++)
            if (lt[k].unlisted)
                fprintf(out, "%s %-18s and %d more, not listed: the first %d are\n", lt[k].code, lt[k].slug,
                        lt[k].unlisted, FINDINGS_LISTED_MAX);
        if (!fs.n) fputs("no findings\n", out);
        else {
            fprintf(out, "%d error%s, %d warning%s", errors, errors == 1 ? "" : "s", warnings, warnings == 1 ? "" : "s");
            if (notes) fprintf(out, ", %d note%s", notes, notes == 1 ? "" : "s");
            fputc('\n', out);
        }
    }
    int unreadable = fs.n && !strcmp(fs.v[0].code, "E001");
    for (int i = 0; i < fs.n; i++) finding_free(&fs.v[i]);
    free(fs.v);
    return unreadable ? 2 : errors || warnings ? 1 : 0;
}

int maptools_check(FILE *out, const char *path, int json)
{
    Findings fs = { 0 };
    char   err[256];
    size_t len = 0;
    char  *text = mapio_file_read(path, &len, err, sizeof err);
    Map   *m = text ? mapio_load_text_diag(path, text, len, err, sizeof err, file_finding, &fs) : NULL;
    if (!m) {
        Finding *f = add_finding(&fs, "E001", "unreadable");
        finding_say(f, "%s", err);
    } else {
        check_map(m, &fs);
        check_shift(m, text, len, path, &fs);
        map_free(m);
    }
    free(text);
    return check_report(out, &fs, path, json);
}

int maptools_check_map(FILE *out, const Map *m, int json)
{
    Findings fs = { 0 };
    check_map(m, &fs);
    return check_report(out, &fs, m->path[0] ? m->path : NULL, json);
}

/* ------------------------------------------------- the file's findings, kept */

/* What an open says of its file (docs/V2.md, step 5's second piece, part
 * 2): the loader's findings, told in its one pass, then the line search on
 * the bytes it kept (Map.base) -- which costs a scan of the line starts
 * unless a section's count is off. Nothing is kept past the open: the
 * report is made again from Map.base when asked (:check, the channel). */
struct MaptoolsFile { Findings fs; };

MaptoolsFile *maptools_file_new(void)
{
    return xcalloc(1, sizeof(MaptoolsFile));
}

void maptools_file_sink(void *ctx, int line, int col, const char *code, const char *slug, const char *msg)
{
    file_finding(&((MaptoolsFile *)ctx)->fs, line, col, code, slug, msg);
}

/* The line search on the bytes `m` was read from, then the count and the
 * first finding (errors before warnings, then by line). */
static int file_sum(Findings *fs, const Map *m, const char *text, size_t len, char *summary, size_t cap, int *rows, int *errors)
{
    if (m && text) check_shift(m, text, len, m->path, fs);
    if (summary && cap) summary[0] = '\0';
    if (rows) *rows = 0;
    if (errors) *errors = 0;
    int n = 0;
    const Finding *first = NULL;
    for (int i = 0; i < fs->n; i++) {
        const Finding *g = &fs->v[i];
        if (g->folded || (g->code[0] != 'E' && g->code[0] != 'W')) continue;
        n++;
        if (errors && g->code[0] == 'E') (*errors)++;
        if (rows && (!strcmp(g->code, "W031") || !strcmp(g->code, "E011"))) *rows = g->line > 0 ? g->line : 1;
        if (!first || (g->code[0] == 'E' && first->code[0] != 'E') ||
            (g->code[0] == first->code[0] && g->line < first->line)) first = g;
    }
    if (first && summary && cap) {
        if (first->line > 0) snprintf(summary, cap, "line %d, %s", first->line, first->msg);
        else                 snprintf(summary, cap, "%s", first->msg);
    }
    return n;
}

int maptools_file_finish(MaptoolsFile *f, const Map *m, char *summary, size_t cap, int *rows, int *errors)
{
    return file_sum(&f->fs, m, m ? m->base : NULL, m ? m->base_len : 0, summary, cap, rows, errors);
}

int maptools_file_of(const Map *m, char *summary, size_t cap, int *rows, int *errors)
{
    Findings fs = { 0 };
    char err[256];
    Map *bm = m->base ? mapio_load_text_diag(m->path, m->base, m->base_len, err, sizeof err, file_finding, &fs) : NULL;
    int n = file_sum(&fs, bm, m->base, m->base_len, summary, cap, rows, errors);
    map_free(bm);
    for (int i = 0; i < fs.n; i++) finding_free(&fs.v[i]);
    free(fs.v);
    return n;
}

void maptools_file_free(MaptoolsFile *f)
{
    if (!f) return;
    for (int i = 0; i < f->fs.n; i++) finding_free(&f->fs.v[i]);
    free(f->fs.v);
    free(f);
}

/* Does the line search find anything in this file -- a place, the two
 * readings, a row missing, or a count -- before the fold is paid for? */
static int shift_any(const Map *m, const char *text, size_t len)
{
    MapioSection secs[MAPIO_SECTIONS];
    mapio_sections(text, len, m->w, m->h, secs);
    Shift sh;
    shift_find(m, secs, &sh);
    int any = sh.found;
    for (int kind = MAPIO_TILES; kind <= MAPIO_HEDGES && !any; kind++) any = rows_past(&secs[kind]);
    mapio_sections_free(secs);
    return any;
}

/* The map findings the file's check folded, by what names one: its code,
 * square and boundary. Sorted once, then each live finding looked up -- a
 * map walled on every edge has a finding a square. */
typedef struct { char code[8]; int x, y, edge; } FoldKey;

static int foldkey_cmp(const void *pa, const void *pb)
{
    const FoldKey *a = pa, *b = pb;
    int c = strcmp(a->code, b->code);
    if (c) return c;
    if (a->y != b->y) return a->y - b->y;
    if (a->x != b->x) return a->x - b->x;
    return a->edge - b->edge;
}

static FoldKey fold_key(const Finding *f)
{
    FoldKey k;
    memset(&k, 0, sizeof k);
    str_lcpy(k.code, f->code, sizeof k.code);
    k.x = f->x; k.y = f->y; k.edge = f->edge;
    return k;
}

int maptools_check_open(FILE *out, const Map *m, int json)
{
    /* The file as vtt last read or wrote it, with its line numbers: the
     * loader's findings, the line search and its fold, checked on the map
     * the bytes make; then the live map's own findings, less those the
     * file's fold explains. A map never saved has only the second. */
    Findings fs = { 0 };
    if (m->base) {
        Findings fb = { 0 };
        char err[256];
        Map *bm = mapio_load_text_diag(m->path, m->base, m->base_len, err, sizeof err, file_finding, &fb);
        if (bm) {
            /* The map the bytes make is checked only for the fold, which a
             * shift found needs: else the live map's findings say it all. */
            if (shift_any(bm, m->base, m->base_len)) {
                check_map(bm, &fb);
                check_shift(bm, m->base, m->base_len, m->path, &fb);
            }
            map_free(bm);
        }
        Findings live = { 0 };
        check_map(m, &live);
        FoldKey *keys = xmalloc((size_t)(fb.n ? fb.n : 1) * sizeof *keys);
        int nk = 0;
        for (int i = 0; i < fb.n; i++)
            if (fb.v[i].folded && fb.v[i].line <= 0) keys[nk++] = fold_key(&fb.v[i]);
        qsort(keys, (size_t)nk, sizeof *keys, foldkey_cmp);
        for (int i = 0; i < fb.n; i++) {
            Finding *g = &fb.v[i];
            if (g->line <= 0 || g->folded) continue;        /* the base's map findings: the live map's say */
            take_finding(&fs, g);
        }
        for (int i = 0; i < live.n; i++) {
            Finding *g = &live.v[i];
            FoldKey k = fold_key(g);
            if (nk && bsearch(&k, keys, (size_t)nk, sizeof *keys, foldkey_cmp)) { finding_free(g); continue; }
            take_finding(&fs, g);
        }
        for (int i = 0; i < fb.n; i++) finding_free(&fb.v[i]);
        free(fb.v);
        free(live.v);
        free(keys);
    } else {
        check_map(m, &fs);
    }
    return check_report(out, &fs, m->path[0] ? m->path : NULL, json);
}
