#include "map.h"

#include <stdlib.h>
#include <string.h>

#include "util.h"

/* One row per kind, so the name, the saved character and the behaviour cannot
 * drift apart. grid.c holds a parallel table of appearances, checked against
 * these counts at compile time. */
static const struct {
    const char *name;
    char        ch;
} TILE_INFO[TILE_COUNT] = {
    { "void",   ' ' },
    { "floor",  '.' },
    { "water",  '~' },
    { "rough",  ':' },
    { "brush",  '"' },
    { "wood",   '=' },
    { "hazard", '^' },
};

static const struct {
    const char *name;
    char        ch;
    uint8_t     blocks_move;
    uint8_t     blocks_sight;
} EDGE_INFO[EDGE_COUNT] = {
    { "none",        ' ', 0, 0 },
    { "wall",        '|', 1, 1 },
    { "door",        '+', 1, 1 },
    { "open door",   '/', 0, 0 },
    { "window",      '%', 1, 0 },
    { "secret door", 'S', 1, 1 },
    { "open secret", 's', 0, 0 },
};

const char *tile_name(uint8_t k) { return k < TILE_COUNT ? TILE_INFO[k].name : "?"; }
char tile_file_char(uint8_t k)   { return k < TILE_COUNT ? TILE_INFO[k].ch : ' '; }

int tile_from_file_char(char c)
{
    for (int i = 0; i < TILE_COUNT; i++)
        if (TILE_INFO[i].ch == c) return i;
    return -1;
}

const char *edge_name(uint8_t k) { return k < EDGE_COUNT ? EDGE_INFO[k].name : "?"; }
char edge_file_char(uint8_t k)   { return k < EDGE_COUNT ? EDGE_INFO[k].ch : ' '; }

int edge_from_file_char(char c)
{
    /* Horizontal walls were written as '-' before doors existed, so both
     * spellings still load as a wall. */
    if (c == '-') return EDGE_WALL;
    for (int i = 0; i < EDGE_COUNT; i++)
        if (EDGE_INFO[i].ch == c) return i;
    return -1;
}

int edge_is_door(uint8_t k)
{
    return k == EDGE_DOOR_CLOSED || k == EDGE_DOOR_OPEN ||
           k == EDGE_SECRET_CLOSED || k == EDGE_SECRET_OPEN;
}

uint8_t edge_toggled(uint8_t k)
{
    switch (k) {
    case EDGE_DOOR_CLOSED:   return EDGE_DOOR_OPEN;
    case EDGE_DOOR_OPEN:     return EDGE_DOOR_CLOSED;
    case EDGE_SECRET_CLOSED: return EDGE_SECRET_OPEN;
    case EDGE_SECRET_OPEN:   return EDGE_SECRET_CLOSED;
    default:                 return k;
    }
}

static int edge_stops_move(uint8_t k)  { return k < EDGE_COUNT && EDGE_INFO[k].blocks_move; }
static int edge_stops_sight(uint8_t k) { return k < EDGE_COUNT && EDGE_INFO[k].blocks_sight; }

Map *map_new(int w, int h, const char *name)
{
    if (w < MAP_MIN_DIM) w = MAP_MIN_DIM;
    if (h < MAP_MIN_DIM) h = MAP_MIN_DIM;
    if (w > MAP_MAX_DIM) w = MAP_MAX_DIM;
    if (h > MAP_MAX_DIM) h = MAP_MAX_DIM;

    Map *m = xcalloc(1, sizeof *m);
    m->w = w;
    m->h = h;
    m->tiles  = xcalloc((size_t)w * (size_t)h, 1);
    m->vedges = xcalloc((size_t)(w + 1) * (size_t)h, 1);
    m->hedges = xcalloc((size_t)w * (size_t)(h + 1), 1);
    m->zoom     = 1;
    m->scale_ft = MAP_SCALE_DEFAULT;
    m->metric   = MAP_METRIC_DEFAULT;
    str_lcpy(m->name, name && name[0] ? name : "untitled", sizeof m->name);
    return m;
}

void map_free(Map *m)
{
    if (!m) return;
    free(m->tiles);
    free(m->vedges);
    free(m->hedges);
    tokens_free(&m->tokens);
    free(m);
}

int map_resize(Map *m, int w, int h)
{
    if (w < MAP_MIN_DIM || h < MAP_MIN_DIM || w > MAP_MAX_DIM || h > MAP_MAX_DIM)
        return -1;
    if (w == m->w && h == m->h) return 0;

    uint8_t *tiles  = xcalloc((size_t)w * (size_t)h, 1);
    uint8_t *vedges = xcalloc((size_t)(w + 1) * (size_t)h, 1);
    uint8_t *hedges = xcalloc((size_t)w * (size_t)(h + 1), 1);

    int cw = imin(w, m->w), ch = imin(h, m->h);

    for (int y = 0; y < ch; y++)
        memcpy(tiles + (size_t)y * (size_t)w,
               m->tiles + (size_t)y * (size_t)m->w, (size_t)cw);

    /* Edge arrays are one wider/taller than the tile grid, so the copied span
     * includes the boundary that closes the preserved region. */
    for (int y = 0; y < ch; y++)
        memcpy(vedges + (size_t)y * (size_t)(w + 1),
               m->vedges + (size_t)y * (size_t)(m->w + 1), (size_t)imin(w + 1, m->w + 1));

    for (int y = 0; y < imin(h + 1, m->h + 1); y++)
        memcpy(hedges + (size_t)y * (size_t)w,
               m->hedges + (size_t)y * (size_t)m->w, (size_t)cw);

    free(m->tiles);
    free(m->vedges);
    free(m->hedges);
    m->tiles  = tiles;
    m->vedges = vedges;
    m->hedges = hedges;
    m->w = w;
    m->h = h;

    /* Drop tokens that the shrink left outside the map. */
    for (int i = m->tokens.n - 1; i >= 0; i--) {
        const Token *t = &m->tokens.v[i];
        if (t->x + t->size > w || t->y + t->size > h) tokens_remove(&m->tokens, i);
    }
    m->modified = 1;
    return 0;
}

uint8_t map_tile(const Map *m, int x, int y)
{
    if (!map_in_bounds(m, x, y)) return TILE_VOID;
    return m->tiles[(size_t)y * (size_t)m->w + (size_t)x];
}

void map_set_tile(Map *m, int x, int y, uint8_t kind)
{
    if (!map_in_bounds(m, x, y)) return;
    m->tiles[(size_t)y * (size_t)m->w + (size_t)x] = kind;
    m->modified = 1;
}

uint8_t map_vedge(const Map *m, int x, int y)
{
    if (x < 0 || y < 0 || x > m->w || y >= m->h) return EDGE_NONE;
    return m->vedges[(size_t)y * (size_t)(m->w + 1) + (size_t)x];
}

void map_set_vedge(Map *m, int x, int y, uint8_t kind)
{
    if (x < 0 || y < 0 || x > m->w || y >= m->h) return;
    m->vedges[(size_t)y * (size_t)(m->w + 1) + (size_t)x] = kind;
    m->modified = 1;
}

uint8_t map_hedge(const Map *m, int x, int y)
{
    if (x < 0 || y < 0 || x >= m->w || y > m->h) return EDGE_NONE;
    return m->hedges[(size_t)y * (size_t)m->w + (size_t)x];
}

void map_set_hedge(Map *m, int x, int y, uint8_t kind)
{
    if (x < 0 || y < 0 || x >= m->w || y > m->h) return;
    m->hedges[(size_t)y * (size_t)m->w + (size_t)x] = kind;
    m->modified = 1;
}

int map_walkable(const Map *m, int x, int y)
{
    /* Every terrain is walkable; only the absence of map is not. */
    return map_tile(m, x, y) != TILE_VOID;
}

static uint8_t edge_between(const Map *m, int x, int y, int dx, int dy)
{
    if (dx > 0) return map_vedge(m, x + 1, y);
    if (dx < 0) return map_vedge(m, x, y);
    if (dy > 0) return map_hedge(m, x, y + 1);
    if (dy < 0) return map_hedge(m, x, y);
    return EDGE_NONE;
}

int map_edge_blocked(const Map *m, int x, int y, int dx, int dy)
{
    return edge_stops_move(edge_between(m, x, y, dx, dy));
}

int map_edge_opaque(const Map *m, int x, int y, int dx, int dy)
{
    return edge_stops_sight(edge_between(m, x, y, dx, dy));
}

int map_blocked(const Map *m, int x, int y, int dx, int dy)
{
    if (dx == 0 && dy == 0) return 0;

    int nx = x + dx, ny = y + dy;
    if (!map_in_bounds(m, nx, ny)) return 1;
    if (!map_walkable(m, nx, ny))  return 1;

    if (dx && dy) {
        /* A diagonal is two crossings. Blocking it when either is walled is
         * what stops a token slipping through the corner where two walls
         * meet, which every table ruling treats as solid. */
        return map_blocked(m, x, y, dx, 0) || map_blocked(m, x, y, 0, dy) ||
               map_blocked(m, x + dx, y, 0, dy) || map_blocked(m, x, y + dy, dx, 0);
    }

    return map_edge_blocked(m, x, y, dx, dy);
}

static void order(int *a, int *b) { if (*a > *b) { int t = *a; *a = *b; *b = t; } }

void map_fill_tiles(Map *m, int x0, int y0, int x1, int y1, uint8_t kind)
{
    order(&x0, &x1);
    order(&y0, &y1);
    x0 = imax(x0, 0); y0 = imax(y0, 0);
    x1 = imin(x1, m->w - 1); y1 = imin(y1, m->h - 1);

    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++)
            m->tiles[(size_t)y * (size_t)m->w + (size_t)x] = kind;
    m->modified = 1;
}

void map_rect_walls(Map *m, int x0, int y0, int x1, int y1, uint8_t kind)
{
    order(&x0, &x1);
    order(&y0, &y1);

    /* The outline sits on the boundary just outside the tile rectangle: the
     * west face of column x0 and the east face of column x1, and likewise for
     * the rows. */
    for (int y = y0; y <= y1; y++) {
        map_set_vedge(m, x0, y, kind);
        map_set_vedge(m, x1 + 1, y, kind);
    }
    for (int x = x0; x <= x1; x++) {
        map_set_hedge(m, x, y0, kind);
        map_set_hedge(m, x, y1 + 1, kind);
    }
}
