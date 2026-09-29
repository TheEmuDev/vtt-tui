#include "stamp.h"
#include "store.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "link.h"
#include "mapio.h"
#include "prof.h"
#include "util.h"

/* ------------------------------------------------------------- building */

/* A stamp's own creature: where it came from is not part of what it is. */
static void fresh(Token *t)
{
    token_clear_status(t);
    t->turn = 0;
    t->init = 0;
}

Map *stamp_copy(const Map *m, int x0, int y0, int x1, int y1)
{
    if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
    if (y0 > y1) { int t = y0; y0 = y1; y1 = t; }
    x0 = imax(x0, 0); y0 = imax(y0, 0);
    x1 = imin(x1, m->w - 1); y1 = imin(y1, m->h - 1);
    if (x1 < x0 || y1 < y0) return NULL;

    int  w = x1 - x0 + 1, h = y1 - y0 + 1;
    Map *s = map_new(w, h, "stamp");
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) s->tiles[(size_t)y * (size_t)w + (size_t)x] = map_tile(m, x0 + x, y0 + y);
    for (int y = 0; y < h; y++)
        for (int x = 0; x <= w; x++) s->vedges[(size_t)y * (size_t)(w + 1) + (size_t)x] = map_vedge(m, x0 + x, y0 + y);
    for (int y = 0; y <= h; y++)
        for (int x = 0; x < w; x++) s->hedges[(size_t)y * (size_t)w + (size_t)x] = map_hedge(m, x0 + x, y0 + y);

    for (int i = 0; i < m->tokens.n; i++) {
        Token t = m->tokens.v[i];
        if (t.x < x0 || t.y < y0 || t.x + t.size - 1 > x1 || t.y + t.size - 1 > y1) continue;
        t.x = (int16_t)(t.x - x0);
        t.y = (int16_t)(t.y - y0);
        fresh(&t);
        tokens_add(&s->tokens, t);
    }
    for (int i = 0; i < m->nnotes; i++) {
        const Note *n = &m->notes[i];
        if (n->x >= x0 && n->x <= x1 && n->y >= y0 && n->y <= y1)
            (void)map_note_set(s, n->x - x0, n->y - y0, n->text);
    }
    /* A link comes only with both its ends: half a staircase leads nowhere. */
    for (int i = 0; i < m->nlinks; i++) {
        Link l = m->links[i];
        int in = 1;
        for (int e = 0; e < 2; e++)
            in &= l.x[e] >= x0 && l.y[e] >= y0 && l.x[e] + l.size - 1 <= x1 && l.y[e] + l.size - 1 <= y1;
        if (!in) continue;
        for (int e = 0; e < 2; e++) { l.x[e] = (int16_t)(l.x[e] - x0); l.y[e] = (int16_t)(l.y[e] - y0); }
        (void)link_put(s, &l);
    }
    s->modified = 0;
    return s;
}

/* A new stamp the same as s but w x h, every square, boundary, creature and
 * note sent through the given maps. The four maps are the one transform
 * applied to the four kinds of thing a grid holds. */
typedef struct {
    int (*tile)(const Map *s, int x, int y, int *nx, int *ny);
    int (*vedge)(const Map *s, int x, int y, int *vert, int *nx, int *ny);
    int (*hedge)(const Map *s, int x, int y, int *vert, int *nx, int *ny);
    void (*token)(const Map *s, const Token *t, int *nx, int *ny);
} Transform;

static void put_edge(Map *d, int vert, int x, int y, uint8_t k)
{
    if (vert) d->vedges[(size_t)y * (size_t)(d->w + 1) + (size_t)x] = k;
    else      d->hedges[(size_t)y * (size_t)d->w + (size_t)x] = k;
}

static Map *transform(const Map *s, int w, int h, const Transform *tf)
{
    Map *d = map_new(w, h, s->name);
    int  nx, ny, vert;
    for (int y = 0; y < s->h; y++)
        for (int x = 0; x < s->w; x++) {
            tf->tile(s, x, y, &nx, &ny);
            d->tiles[(size_t)ny * (size_t)w + (size_t)nx] = map_tile(s, x, y);
        }
    for (int y = 0; y < s->h; y++)
        for (int x = 0; x <= s->w; x++) {
            tf->vedge(s, x, y, &vert, &nx, &ny);
            put_edge(d, vert, nx, ny, map_vedge(s, x, y));
        }
    for (int y = 0; y <= s->h; y++)
        for (int x = 0; x < s->w; x++) {
            tf->hedge(s, x, y, &vert, &nx, &ny);
            put_edge(d, vert, nx, ny, map_hedge(s, x, y));
        }
    for (int i = 0; i < s->tokens.n; i++) {
        Token t = s->tokens.v[i];
        tf->token(s, &t, &nx, &ny);
        t.x = (int16_t)nx;
        t.y = (int16_t)ny;
        tokens_add(&d->tokens, t);
    }
    for (int i = 0; i < s->nnotes; i++) {
        tf->tile(s, s->notes[i].x, s->notes[i].y, &nx, &ny);
        (void)map_note_set(d, nx, ny, s->notes[i].text);
    }
    /* A link's end is a block, and goes where a creature that size would. */
    for (int i = 0; i < s->nlinks; i++) {
        Link l = s->links[i];
        for (int e = 0; e < 2; e++) {
            Token t;
            memset(&t, 0, sizeof t);
            t.x = l.x[e]; t.y = l.y[e]; t.size = l.size;
            tf->token(s, &t, &nx, &ny);
            l.x[e] = (int16_t)nx;
            l.y[e] = (int16_t)ny;
        }
        (void)link_put(d, &l);
    }
    d->modified = 0;
    return d;
}

/* A quarter turn clockwise: a point (px, py) of the old w x h goes to
 * (h - py, px). A square is a unit box, a vertical boundary a unit segment
 * down a column line, a horizontal one along a row line; each is sent where
 * its corners go. */
static int cw_tile(const Map *s, int x, int y, int *nx, int *ny)
{ *nx = s->h - 1 - y; *ny = x; return 1; }
static int cw_vedge(const Map *s, int x, int y, int *vert, int *nx, int *ny)
{ *vert = 0; *nx = s->h - 1 - y; *ny = x; return 1; }
static int cw_hedge(const Map *s, int x, int y, int *vert, int *nx, int *ny)
{ *vert = 1; *nx = s->h - y; *ny = x; return 1; }
static void cw_token(const Map *s, const Token *t, int *nx, int *ny)
{ *nx = s->h - t->y - t->size; *ny = t->x; }

/* Left to right: a point (px, py) goes to (w - px, py). */
static int mi_tile(const Map *s, int x, int y, int *nx, int *ny)
{ *nx = s->w - 1 - x; *ny = y; return 1; }
static int mi_vedge(const Map *s, int x, int y, int *vert, int *nx, int *ny)
{ *vert = 1; *nx = s->w - x; *ny = y; return 1; }
static int mi_hedge(const Map *s, int x, int y, int *vert, int *nx, int *ny)
{ *vert = 0; *nx = s->w - 1 - x; *ny = y; return 1; }
static void mi_token(const Map *s, const Token *t, int *nx, int *ny)
{ *nx = s->w - t->x - t->size; *ny = t->y; }

Map *stamp_turned(const Map *s, int quarters)
{
    static const Transform CW = { cw_tile, cw_vedge, cw_hedge, cw_token };
    quarters = ((quarters % 4) + 4) % 4;
    Map *cur = stamp_copy(s, 0, 0, s->w - 1, s->h - 1);
    str_lcpy(cur->name, s->name, sizeof cur->name);
    for (int i = 0; i < quarters; i++) {
        Map *next = transform(cur, cur->h, cur->w, &CW);
        map_free(cur);
        cur = next;
    }
    return cur;
}

Map *stamp_mirrored(const Map *s)
{
    static const Transform MI = { mi_tile, mi_vedge, mi_hedge, mi_token };
    return transform(s, s->w, s->h, &MI);
}

/* --------------------------------------------------------------- placing */

int stamp_place(Map *m, Undo *u, const Map *s, int x, int y, char *err, size_t errsz)
{
    PROF_ZONE("stamp.place");
    char at[MAP_COORD_MAX];
    map_coord_name(x, y, at, sizeof at);
    if (x < 0 || y < 0 || x + s->w > m->w || y + s->h > m->h) {
        snprintf(err, errsz, "a %dx%d stamp at %s runs off the map", s->w, s->h, at);
        return 0;
    }

    /* Every creature on ground -- the stamp's where it has some, the map's
     * where it is see-through -- and on nobody already there. */
    for (int i = 0; i < s->tokens.n; i++) {
        const Token *t = &s->tokens.v[i];
        for (int yy = t->y; yy < t->y + t->size; yy++)
            for (int xx = t->x; xx < t->x + t->size; xx++)
                if (map_tile(s, xx, yy) == TILE_VOID && !map_walkable(m, x + xx, y + yy)) {
                    char sq[MAP_COORD_MAX];
                    map_coord_name(x + xx, y + yy, sq, sizeof sq);
                    snprintf(err, errsz, "%.30s would stand on void at %s", token_name(t), sq);
                    return 0;
                }
        int other = tokens_overlapping(&m->tokens, x + t->x, y + t->y, t->size, -1, TOKEN_ANY_KIND);
        if (other >= 0) {
            snprintf(err, errsz, "%.30s would land on %.30s", token_name(t), token_name(&m->tokens.v[other]));
            return 0;
        }
    }
    /* Its links: every square of an end on ground, clear of the map's own
     * links, and a number free for each. */
    if (s->nlinks > MAP_LINKS_MAX - m->nlinks) {
        snprintf(err, errsz, "no room: a map holds %d links", MAP_LINKS_MAX);
        return 0;
    }
    for (int i = 0; i < s->nlinks; i++) {
        const Link *l = &s->links[i];
        for (int e = 0; e < 2; e++) {
            for (int yy = l->y[e]; yy < l->y[e] + l->size; yy++)
                for (int xx = l->x[e]; xx < l->x[e] + l->size; xx++)
                    if (map_tile(s, xx, yy) == TILE_VOID && !map_walkable(m, x + xx, y + yy)) {
                        char sq[MAP_COORD_MAX];
                        map_coord_name(x + xx, y + yy, sq, sizeof sq);
                        snprintf(err, errsz, "a link would end on void at %s", sq);
                        return 0;
                    }
            int o = link_meets(m, x + l->x[e], y + l->y[e], l->size, l->size, NULL);
            if (o >= 0) {
                char name[32];
                link_name(&m->links[o], name, sizeof name);
                snprintf(err, errsz, "a link would end on %s", name);
                return 0;
            }
        }
    }
    int fresh_notes = 0;
    for (int i = 0; i < s->nnotes; i++)
        fresh_notes += map_note_at(m, x + s->notes[i].x, y + s->notes[i].y) == NULL;
    if (m->nnotes + fresh_notes > MAP_NOTES_MAX) {
        snprintf(err, errsz, "no room: a map holds %d notes on squares", MAP_NOTES_MAX);
        return 0;
    }

    undo_begin(u);
    for (int yy = 0; yy < s->h; yy++)
        for (int xx = 0; xx < s->w; xx++) {
            uint8_t k = map_tile(s, xx, yy);
            if (k != TILE_VOID) undo_set_tile(u, m, x + xx, y + yy, k);
        }
    for (int yy = 0; yy < s->h; yy++)
        for (int xx = 0; xx <= s->w; xx++) {
            uint8_t k = map_vedge(s, xx, yy);
            if (k != EDGE_NONE) undo_set_vedge(u, m, x + xx, y + yy, k);
        }
    for (int yy = 0; yy <= s->h; yy++)
        for (int xx = 0; xx < s->w; xx++) {
            uint8_t k = map_hedge(s, xx, yy);
            if (k != EDGE_NONE) undo_set_hedge(u, m, x + xx, y + yy, k);
        }
    for (int i = 0; i < s->tokens.n; i++) {
        Token t = s->tokens.v[i];
        t.x = (int16_t)(x + t.x);
        t.y = (int16_t)(y + t.y);
        tokens_unique_label(&m->tokens, s->tokens.v[i].label, t.label, sizeof t.label);
        undo_add_token(u, m, t);
    }
    for (int i = 0; i < s->nnotes; i++)
        (void)undo_set_note(u, m, x + s->notes[i].x, y + s->notes[i].y, s->notes[i].text);
    for (int i = 0; i < s->nlinks; i++) {
        Link l = s->links[i];
        l.num = (uint8_t)link_free_num(m);
        for (int e = 0; e < 2; e++) { l.x[e] = (int16_t)(x + l.x[e]); l.y[e] = (int16_t)(y + l.y[e]); }
        (void)undo_set_link(u, m, &l);
    }
    undo_end(u);
    return 1;
}

/* ----------------------------------------------------------------- files */

void stamp_dir(char *buf, size_t sz)
{
    store_dir("stamps", buf, sz);
}

int stamp_save(const Map *s, const char *name, char *err, size_t errsz)
{
    char path[MAP_PATH_MAX], dir[MAP_PATH_MAX];
    if (!store_name_ok(name)) {
        snprintf(err, errsz, "a stamp's name is letters, digits, - and _");
        return -1;
    }
    if (!store_path("stamps", name, ".vtt", path, sizeof path)) { snprintf(err, errsz, "the stamp's path is too long"); return -1; }
    stamp_dir(dir, sizeof dir);
    dir_make(dir);
    Map copy = *s;                       /* the file says its name */
    str_lcpy(copy.name, name, sizeof copy.name);
    return mapio_write(&copy, path, err, errsz);
}

Map *stamp_load(const char *name, char *err, size_t errsz)
{
    char path[MAP_PATH_MAX];
    if (!store_name_ok(name)) { snprintf(err, errsz, "no stamp called %.40s", name); return NULL; }
    if (!store_path("stamps", name, ".vtt", path, sizeof path)) { snprintf(err, errsz, "the stamp's path is too long"); return NULL; }
    Map *s = mapio_load(path, err, errsz);
    if (!s) { snprintf(err, errsz, "no stamp called %.40s", name); return NULL; }
    /* Fog is a map's, never a stamp's, whatever the file was edited to say. */
    memset(s->fog, 0, (size_t)s->w * (size_t)s->h);
    s->fog_on = 0;
    for (int i = 0; i < s->tokens.n; i++) fresh(&s->tokens.v[i]);
    return s;
}

int stamp_list(char (*names)[MAP_NAME_MAX], int max)
{
    char dir[MAP_PATH_MAX];
    stamp_dir(dir, sizeof dir);
    return store_list(dir, ".vtt", names, max);
}

/* --------------------------------------------------------------- preview */

void stamp_show(Map *m, const Map *s, int x, int y, StampShow *sv)
{
    memset(sv, 0, sizeof *sv);
    int x0 = imax(x, 0), y0 = imax(y, 0);
    int x1 = imin(x + s->w - 1, m->w - 1), y1 = imin(y + s->h - 1, m->h - 1);
    if (x1 < x0 || y1 < y0) return;
    sv->x = x0; sv->y = y0; sv->w = x1 - x0 + 1; sv->h = y1 - y0 + 1;
    sv->sx = x0 - x; sv->sy = y0 - y;

    size_t nt = (size_t)sv->w * (size_t)sv->h;
    sv->tiles  = malloc(nt);
    sv->vedges = malloc((size_t)(sv->w + 1) * (size_t)sv->h);
    sv->hedges = malloc((size_t)sv->w * (size_t)(sv->h + 1));
    sv->both   = malloc(sizeof(Token) * (size_t)(m->tokens.n + s->tokens.n + 1));
    if (!sv->tiles || !sv->vedges || !sv->hedges || !sv->both) {
        free(sv->tiles); free(sv->vedges); free(sv->hedges); free(sv->both);
        memset(sv, 0, sizeof *sv);
        return;
    }

    /* Straight into the arrays: map_set_* would touch the map. */
    for (int yy = 0; yy < sv->h; yy++)
        for (int xx = 0; xx < sv->w; xx++) {
            uint8_t *t = &m->tiles[(size_t)(y0 + yy) * (size_t)m->w + (size_t)(x0 + xx)];
            sv->tiles[(size_t)yy * (size_t)sv->w + (size_t)xx] = *t;
            uint8_t k = map_tile(s, sv->sx + xx, sv->sy + yy);
            if (k != TILE_VOID) *t = k;
        }
    for (int yy = 0; yy < sv->h; yy++)
        for (int xx = 0; xx <= sv->w; xx++) {
            uint8_t *e = &m->vedges[(size_t)(y0 + yy) * (size_t)(m->w + 1) + (size_t)(x0 + xx)];
            sv->vedges[(size_t)yy * (size_t)(sv->w + 1) + (size_t)xx] = *e;
            uint8_t k = map_vedge(s, sv->sx + xx, sv->sy + yy);
            if (k != EDGE_NONE) *e = k;
        }
    for (int yy = 0; yy <= sv->h; yy++)
        for (int xx = 0; xx < sv->w; xx++) {
            uint8_t *e = &m->hedges[(size_t)(y0 + yy) * (size_t)m->w + (size_t)(x0 + xx)];
            sv->hedges[(size_t)yy * (size_t)sv->w + (size_t)xx] = *e;
            uint8_t k = map_hedge(s, sv->sx + xx, sv->sy + yy);
            if (k != EDGE_NONE) *e = k;
        }

    /* The creatures: the map's list set aside whole, a combined one in its
     * place for the draw. No tokens_add, so TokenList.shape is untouched. */
    sv->tokens = m->tokens;
    if (m->tokens.n) memcpy(sv->both, m->tokens.v, sizeof(Token) * (size_t)m->tokens.n);
    int n = m->tokens.n;
    for (int i = 0; i < s->tokens.n; i++) {
        Token t = s->tokens.v[i];
        t.x = (int16_t)(x + t.x);
        t.y = (int16_t)(y + t.y);
        if (t.x < 0 || t.y < 0 || t.x + t.size > m->w || t.y + t.size > m->h) continue;
        sv->both[n++] = t;
    }
    m->tokens.v = sv->both;
    m->tokens.n = n;
    m->tokens.cap = n;

    /* Its notes, after the map's while there is room: the marks are drawn
     * by square, so one shown twice on a square is one mark. */
    sv->nnotes = m->nnotes;
    for (int i = 0; i < s->nnotes && m->nnotes < MAP_NOTES_MAX; i++) {
        int nx = x + s->notes[i].x, ny = y + s->notes[i].y;
        if (!map_in_bounds(m, nx, ny)) continue;
        m->notes[m->nnotes] = s->notes[i];
        m->notes[m->nnotes].x = (int16_t)nx;
        m->notes[m->nnotes].y = (int16_t)ny;
        m->nnotes++;
    }
    /* Its links the same way, numbered as placing would number them. */
    sv->nlinks = m->nlinks;
    for (int i = 0; i < s->nlinks && m->nlinks < MAP_LINKS_MAX; i++) {
        Link l = s->links[i];
        int on = 1;
        for (int e = 0; e < 2; e++) {
            l.x[e] = (int16_t)(x + l.x[e]);
            l.y[e] = (int16_t)(y + l.y[e]);
            on &= l.x[e] >= 0 && l.y[e] >= 0 && l.x[e] + l.size <= m->w && l.y[e] + l.size <= m->h;
        }
        int num = link_free_num(m);
        if (!on || !num) continue;
        l.num = (uint8_t)num;
        m->links[m->nlinks++] = l;
    }
    sv->shown = 1;
}

void stamp_unshow(Map *m, StampShow *sv)
{
    if (!sv->shown) return;
    for (int yy = 0; yy < sv->h; yy++)
        for (int xx = 0; xx < sv->w; xx++)
            m->tiles[(size_t)(sv->y + yy) * (size_t)m->w + (size_t)(sv->x + xx)] =
                sv->tiles[(size_t)yy * (size_t)sv->w + (size_t)xx];
    for (int yy = 0; yy < sv->h; yy++)
        for (int xx = 0; xx <= sv->w; xx++)
            m->vedges[(size_t)(sv->y + yy) * (size_t)(m->w + 1) + (size_t)(sv->x + xx)] =
                sv->vedges[(size_t)yy * (size_t)(sv->w + 1) + (size_t)xx];
    for (int yy = 0; yy <= sv->h; yy++)
        for (int xx = 0; xx < sv->w; xx++)
            m->hedges[(size_t)(sv->y + yy) * (size_t)m->w + (size_t)(sv->x + xx)] =
                sv->hedges[(size_t)yy * (size_t)sv->w + (size_t)xx];
    m->tokens = sv->tokens;
    m->nnotes = sv->nnotes;
    m->nlinks = sv->nlinks;
    free(sv->tiles); free(sv->vedges); free(sv->hedges); free(sv->both);
    memset(sv, 0, sizeof *sv);
}
