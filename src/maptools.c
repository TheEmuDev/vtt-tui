#include "maptools.h"

#include <stdlib.h>
#include <string.h>

#include "clock.h"
#include "dice.h"
#include "fog.h"
#include "ruler.h"
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
     * above, the last below, so every column is labelled at its own pitch. */
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

void maptools_dump(FILE *out, const Map *m, int x0, int y0, int x1, int y1)
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
        fprintf(out, "  %c  %-16s %-12s %s\n", token_glyph(i), t->label[0] ? t->label : "(unnamed)", kind, where);
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

    fputs("\nkey\n"
          "  .  floor   ~  water   :  rough   \"  brush   =  wood   ^  hazard   (blank) void\n"
          "  | -  wall   +  door   /  open door   %  window   S  secret door   s  open secret door\n"
          "  S and s are the GM's: the players see a wall and an open door\n", out);

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

    fprintf(out, "\nmap  %s  %dx%d  scale %g ft  metric %s  ruleset %s\n", m->name, m->w, m->h,
            m->scale_ft, dist_metric_name((DistMetric)m->metric), m->ruleset[0] ? m->ruleset : "none");
    if (x0 > 0 || y0 > 0 || x1 < m->w - 1 || y1 < m->h - 1) {
        map_coord_name(x0, y0, at, sizeof at);
        map_coord_name(x1, y1, to, sizeof to);
        fprintf(out, "region %s:%s\n", at, to);
    }
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

    /* A flood fill with an explicit stack: no recursion, whatever the map. */
    int32_t *stack = xmalloc(n * sizeof *stack);
    int      cap = 0;
    for (int y = 0; y < m->h; y++)
        for (int x = 0; x < m->w; x++) {
            size_t i = (size_t)y * (size_t)m->w + (size_t)x;
            if (r->at[i] >= 0 || !map_walkable(m, x, y)) continue;
            if (r->n == cap) { cap = cap ? cap * 2 : 16; r->v = xrealloc(r->v, (size_t)cap * sizeof *r->v); }
            Room *rm = &r->v[r->n];
            rm->x0 = rm->x1 = rm->fx = x;
            rm->y0 = rm->y1 = rm->fy = y;
            rm->squares = 0;
            int sp = 0;
            stack[sp++] = (int32_t)i;
            r->at[i] = r->n;
            while (sp) {
                int32_t k = stack[--sp];
                int kx = (int)(k % m->w), ky = (int)(k / m->w);
                rm->squares++;
                if (kx < rm->x0) rm->x0 = kx;
                if (kx > rm->x1) rm->x1 = kx;
                if (ky < rm->y0) rm->y0 = ky;
                if (ky > rm->y1) rm->y1 = ky;
                static const int d[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
                for (int j = 0; j < 4; j++) {
                    int nx = kx + d[j][0], ny = ky + d[j][1];
                    if (!map_in_bounds(m, nx, ny) || !map_walkable(m, nx, ny)) continue;
                    size_t ni = (size_t)ny * (size_t)m->w + (size_t)nx;
                    if (r->at[ni] >= 0) continue;
                    uint8_t e = d[j][0] > 0 ? map_vedge(m, kx + 1, ky) : d[j][0] < 0 ? map_vedge(m, kx, ky)
                              : d[j][1] > 0 ? map_hedge(m, kx, ky + 1) : map_hedge(m, kx, ky);
                    if (e != EDGE_NONE) continue;
                    r->at[ni] = r->n;
                    stack[sp++] = (int32_t)ni;
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
    for (int y = 0; y < m->h; y++)
        for (int x = 0; x <= m->w; x++) {
            if (x > 0 && x < m->w && edge_passable_kind(map_vedge(m, x, y))) {
                int a = rooms_at(r, m, x - 1, y), b = rooms_at(r, m, x, y);
                if (a >= 0 && b >= 0 && a != b) {
                    if (ne == ecap) { ecap = ecap ? ecap * 2 : 16; pairs = xrealloc(pairs, (size_t)ecap * sizeof *pairs); }
                    pairs[ne][0] = a; pairs[ne][1] = b; ne++;
                }
            }
        }
    for (int y = 1; y < m->h; y++)
        for (int x = 0; x < m->w; x++)
            if (edge_passable_kind(map_hedge(m, x, y))) {
                int a = rooms_at(r, m, x, y - 1), b = rooms_at(r, m, x, y);
                if (a >= 0 && b >= 0 && a != b) {
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
    while (qh < qt) {
        int a = queue[qh++];
        for (int k = deg[a]; k < deg[a + 1]; k++)
            if (!r->reach[adj[k]]) { r->reach[adj[k]] = 1; queue[qt++] = adj[k]; }
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

/* ------------------------------------------------------------------ json */

/* Enough JSON to write a report: objects, arrays, strings, numbers. No
 * library, and nothing to parse -- only to write, correctly escaped. */
typedef struct {
    FILE *f;
    int   depth;
    int   first[32];      /* nothing written yet at this depth */
    int   after_key;
} Json;

static void j_sep(Json *j)
{
    if (j->after_key) { j->after_key = 0; return; }
    if (j->depth > 0) {
        if (!j->first[j->depth]) fputc(',', j->f);
        j->first[j->depth] = 0;
    }
}

static void j_open(Json *j, char c)
{
    j_sep(j);
    fputc(c, j->f);
    if (j->depth < 31) j->first[++j->depth] = 1;
}

static void j_close(Json *j, char c)
{
    fputc(c, j->f);
    if (j->depth > 0) j->depth--;
}

static void j_str_raw(FILE *f, const char *s)
{
    fputc('"', f);
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        if (*p == '"' || *p == '\\') { fputc('\\', f); fputc(*p, f); }
        else if (*p == '\n') fputs("\\n", f);
        else if (*p == '\t') fputs("\\t", f);
        else if (*p < 0x20)  fprintf(f, "\\u%04x", *p);
        else                 fputc(*p, f);
    }
    fputc('"', f);
}

static void j_key(Json *j, const char *k)
{
    j_sep(j);
    j_str_raw(j->f, k);
    fputc(':', j->f);
    j->after_key = 1;
}

static void j_str(Json *j, const char *s) { j_sep(j); j_str_raw(j->f, s); }
static void j_int(Json *j, long v)        { j_sep(j); fprintf(j->f, "%ld", v); }
static void j_num(Json *j, double v)      { j_sep(j); fprintf(j->f, "%g", v); }
static void j_bool(Json *j, int v)        { j_sep(j); fputs(v ? "true" : "false", j->f); }
static void j_null(Json *j)               { j_sep(j); fputs("null", j->f); }

static void j_kstr(Json *j, const char *k, const char *v) { j_key(j, k); j_str(j, v); }
static void j_kint(Json *j, const char *k, long v)        { j_key(j, k); j_int(j, v); }

/* -------------------------------------------------------------- describe */

static const char *kind_name(const Token *t) { return t->kind == TOKEN_ENEMY ? "enemy" : "player"; }

static void room_name(const Rooms *r, int i, char *buf, size_t sz)
{
    map_coord_name(r->v[i].fx, r->v[i].fy, buf, sz);
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
    for (int y = rm->y0; y <= rm->y1; y++)
        for (int x = rm->x0; x <= rm->x1; x++) {
            if (rooms_at(r, m, x, y) != room) continue;
            /* Its four sides; a boundary is listed from each room once. */
            const struct { int v, ex, ey, ox, oy; } side[4] = {
                { 1, x, y, x - 1, y }, { 1, x + 1, y, x + 1, y },
                { 0, x, y, x, y - 1 }, { 0, x, y + 1, x, y + 1 },
            };
            for (int s = 0; s < 4; s++) {
                uint8_t k = side[s].v ? map_vedge(m, side[s].ex, side[s].ey) : map_hedge(m, side[s].ex, side[s].ey);
                if (k == EDGE_NONE || k == EDGE_WALL) continue;
                if (n == *cap) { *cap = *cap ? *cap * 2 : 32; *buf = xrealloc(*buf, (size_t)*cap * sizeof **buf); }
                Opening *o = &(*buf)[n++];
                o->kind = k; o->vertical = side[s].v; o->x = side[s].ex; o->y = side[s].ey;
                o->to = !map_in_bounds(m, side[s].ox, side[s].oy) ? -2 : rooms_at(r, m, side[s].ox, side[s].oy);
            }
        }
    return n;
}

void maptools_describe(FILE *out, const Map *m, int json)
{
    Rooms r;
    rooms_build(m, &r);
    char buf[64], buf2[64];
    int  unreachable = 0;
    for (int i = 0; i < r.n; i++) unreachable += !r.reach[i];
    Opening *op = NULL;
    int      opcap = 0;

    if (json) {
        Json j = { out, 0, { 0 }, 0 };
        j_open(&j, '{');
        j_key(&j, "map");
        j_open(&j, '{');
        j_kstr(&j, "name", m->name);
        j_kint(&j, "width", m->w);
        j_kint(&j, "height", m->h);
        j_key(&j, "scale_ft"); j_num(&j, m->scale_ft);
        j_kstr(&j, "metric", dist_metric_name((DistMetric)m->metric));
        j_key(&j, "ruleset"); if (m->ruleset[0]) j_str(&j, m->ruleset); else j_null(&j);
        j_kint(&j, "round", m->round);
        j_key(&j, "fog"); j_bool(&j, m->fog_on);
        j_kint(&j, "creatures", m->tokens.n);
        j_kint(&j, "rooms", r.n);
        j_key(&j, "start_room"); if (r.start >= 0) j_int(&j, r.start + 1); else j_null(&j);
        j_close(&j, '}');

        j_key(&j, "rooms");
        j_open(&j, '[');
        for (int i = 0; i < r.n; i++) {
            const Room *rm = &r.v[i];
            j_open(&j, '{');
            j_kint(&j, "id", i + 1);
            room_name(&r, i, buf, sizeof buf); j_kstr(&j, "name", buf);
            j_kint(&j, "x", rm->fx); j_kint(&j, "y", rm->fy);
            bounds_name(rm->x0, rm->y0, rm->x1, rm->y1, buf, sizeof buf); j_kstr(&j, "bounds", buf);
            j_kint(&j, "squares", rm->squares);
            j_key(&j, "reachable"); j_bool(&j, r.reach[i]);
            int terrain[TILE_COUNT] = { 0 };
            for (int y = rm->y0; y <= rm->y1; y++)
                for (int x = rm->x0; x <= rm->x1; x++)
                    if (rooms_at(&r, m, x, y) == i) terrain[map_tile(m, x, y)]++;
            j_key(&j, "terrain"); j_open(&j, '{');
            for (int t = 1; t < TILE_COUNT; t++) if (terrain[t]) j_kint(&j, tile_name((uint8_t)t), terrain[t]);
            j_close(&j, '}');
            int no = room_openings(m, &r, i, &op, &opcap);
            j_key(&j, "doors"); j_open(&j, '[');
            for (int k = 0; k < no; k++) {
                j_open(&j, '{');
                j_kstr(&j, "kind", edge_name(op[k].kind));
                maptools_edge_name(m, op[k].vertical, op[k].x, op[k].y, buf, sizeof buf);
                j_kstr(&j, "where", buf);
                j_key(&j, "to");
                if (op[k].to >= 0) j_int(&j, op[k].to + 1); else j_str(&j, op[k].to == -1 ? "void" : "off-map");
                j_close(&j, '}');
            }
            j_close(&j, ']');
            j_key(&j, "creatures"); j_open(&j, '[');
            for (int t = 0; t < m->tokens.n; t++) {
                const Token *tk = &m->tokens.v[t];
                if (rooms_at(&r, m, tk->x, tk->y) != i) continue;
                j_open(&j, '{');
                j_kstr(&j, "label", tk->label);
                j_kstr(&j, "kind", kind_name(tk));
                j_kint(&j, "size", tk->size);
                map_coord_name(tk->x, tk->y, buf, sizeof buf); j_kstr(&j, "at", buf);
                j_kint(&j, "x", tk->x); j_kint(&j, "y", tk->y);
                if (tk->note[0]) j_kstr(&j, "note", tk->note);
                j_close(&j, '}');
            }
            j_close(&j, ']');
            j_key(&j, "notes"); j_open(&j, '[');
            for (int nt = 0; nt < m->nnotes; nt++) {
                const Note *n = &m->notes[nt];
                if (rooms_at(&r, m, n->x, n->y) != i) continue;
                j_open(&j, '{');
                map_coord_name(n->x, n->y, buf, sizeof buf); j_kstr(&j, "at", buf);
                j_kstr(&j, "text", n->text);
                j_close(&j, '}');
            }
            j_close(&j, ']');
            j_key(&j, "fog"); j_open(&j, '[');
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
                j_open(&j, '{');
                j_kstr(&j, "patch", fp->name);
                j_kint(&j, "painted", painted);
                j_kint(&j, "seen", seen);
                j_close(&j, '}');
            }
            j_close(&j, ']');
            j_close(&j, '}');
        }
        j_close(&j, ']');

        j_key(&j, "outside");                   /* creatures and notes on no room */
        j_open(&j, '[');
        for (int t = 0; t < m->tokens.n; t++) {
            const Token *tk = &m->tokens.v[t];
            if (rooms_at(&r, m, tk->x, tk->y) >= 0) continue;
            j_open(&j, '{');
            j_kstr(&j, "label", tk->label);
            map_coord_name(tk->x, tk->y, buf, sizeof buf); j_kstr(&j, "at", buf);
            j_close(&j, '}');
        }
        j_close(&j, ']');
        j_close(&j, '}');
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
        if (r.start >= 0) { room_name(&r, r.start, buf, sizeof buf); fprintf(out, ", starting in room %d (%s)", r.start + 1, buf); }
        if (unreachable) fprintf(out, ", %d not reachable from it", unreachable);
        fputc('\n', out);

        for (int i = 0; i < r.n; i++) {
            const Room *rm = &r.v[i];
            room_name(&r, i, buf, sizeof buf);
            bounds_name(rm->x0, rm->y0, rm->x1, rm->y1, buf2, sizeof buf2);
            fprintf(out, "\nroom %d (%s)  %s  %d square%s%s\n", i + 1, buf, buf2, rm->squares,
                    rm->squares == 1 ? "" : "s", r.reach[i] ? "" : "  NOT REACHABLE");
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
                    room_name(&r, op[k].to, buf2, sizeof buf2);
                    fprintf(out, "  %-12s %-8s to room %d (%s)\n", edge_name(op[k].kind), buf, op[k].to + 1, buf2);
                } else {
                    fprintf(out, "  %-12s %-8s to %s\n", edge_name(op[k].kind), buf, op[k].to == -1 ? "void" : "the map's edge");
                }
            }
            for (int t = 0; t < m->tokens.n; t++) {
                const Token *tk = &m->tokens.v[t];
                if (rooms_at(&r, m, tk->x, tk->y) != i) continue;
                map_coord_name(tk->x, tk->y, buf, sizeof buf);
                fprintf(out, "  %-12s %-8s %s%s%s\n", kind_name(tk), buf, tk->label,
                        tk->size > 1 ? (tk->size == 2 ? "  2x2" : "  3x3") : "", tk->note[0] ? "  (note)" : "");
            }
            for (int nt = 0; nt < m->nnotes; nt++) {
                const Note *n = &m->notes[nt];
                if (rooms_at(&r, m, n->x, n->y) != i) continue;
                map_coord_name(n->x, n->y, buf, sizeof buf);
                fprintf(out, "  %-12s %-8s %s\n", "note", buf, n->text);
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
            if (!outside++) fputs("\noutside any room\n", out);
            map_coord_name(tk->x, tk->y, buf, sizeof buf);
            fprintf(out, "  %-12s %-8s %s\n", kind_name(tk), buf, tk->label);
        }
    }
    free(op);
    rooms_free(&r);
}
