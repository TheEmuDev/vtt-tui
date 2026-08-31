#ifndef VTT_MAP_H
#define VTT_MAP_H

#include <stdint.h>
#include "token.h"

#define MAP_MIN_DIM 1
#define MAP_MAX_DIM 512
#define MAP_NAME_MAX 64
#define MAP_RULESET_MAX 32

/* Feet represented by one tile. Five is the tabletop convention; :scale
 * changes it per map. */
#define MAP_SCALE_DEFAULT 5.0

/* Default distance metric for a new map: DIST_ALT_DIAG, the alternating
 * 5-10-5 diagonal. Spelled as a number because ruler.h includes this header
 * and so cannot be included back; ruler.c carries a static assertion that the
 * two still agree. */
#define MAP_METRIC_DEFAULT 2
#define MAP_PATH_MAX 512

/* Terrain is decoration: everything except VOID is part of the map and
 * behaves identically. What difficult ground costs is a ruling between the GM
 * and the players, not something the tool decides. */
typedef enum {
    TILE_VOID = 0,      /* not part of the map; renders as nothing */
    TILE_FLOOR,         /* plain ground */
    TILE_WATER,
    TILE_ROUGH,         /* rubble, scree, debris */
    TILE_BRUSH,         /* grass, undergrowth */
    TILE_WOOD,          /* planking, bridge, deck */
    TILE_HAZARD,        /* fire, acid, spikes */
    TILE_COUNT,
} TileKind;

/* What sits on the boundary between two tiles. Movement and sight are
 * separate questions: a window stops one and not the other. */
typedef enum {
    EDGE_NONE = 0,
    EDGE_WALL,            /* stops both */
    EDGE_DOOR_CLOSED,     /* stops both, until opened */
    EDGE_DOOR_OPEN,       /* stops neither; still drawn, so you see the door */
    EDGE_WINDOW,          /* stops movement, not sight */
    EDGE_SECRET_CLOSED,   /* a wall to anyone reading the screen in play mode */
    EDGE_SECRET_OPEN,
    EDGE_COUNT,
} EdgeKind;

/* Names and the single character each takes in a saved map. */
const char *tile_name(uint8_t kind);
char        tile_file_char(uint8_t kind);
int         tile_from_file_char(char c);      /* -1 when unrecognised */

const char *edge_name(uint8_t kind);
char        edge_file_char(uint8_t kind);
int         edge_from_file_char(char c);      /* -1 when unrecognised */

/* Doors and secret doors toggle; everything else does not. */
int     edge_is_door(uint8_t kind);
uint8_t edge_toggled(uint8_t kind);

/* Walls live on the boundary *between* tiles, not on tiles themselves, so a
 * wall costs no floor space and blocking is an exact per-crossing question.
 *
 *   vedges[(w+1) * h]  vertical   walls: vedges(x,y) separates tile (x-1,y) | (x,y)
 *   hedges[w * (h+1)]  horizontal walls: hedges(x,y) separates tile (x,y-1) / (x,y)
 *
 * The +1 in each array is what makes the far edge of the last row/column
 * representable without special-casing. */
typedef struct {
    int      w, h;
    uint8_t *tiles;
    uint8_t *vedges;
    uint8_t *hedges;

    char name[MAP_NAME_MAX];
    char path[MAP_PATH_MAX];
    int  zoom;              /* preferred zoom level, persisted with the map */
    int  modified;          /* unsaved changes */

    /* Measurement settings travel with the encounter, since they belong to
     * the game being played rather than to the session. */
    double scale_ft;                      /* feet per tile */
    char   ruleset[MAP_RULESET_MAX];      /* range-band table, "" for none */
    int    metric;                        /* DistMetric */

    TokenList tokens;
} Map;

Map *map_new(int w, int h, const char *name);
void map_free(Map *m);

/* Resizes in place, preserving the overlapping region. */
int  map_resize(Map *m, int w, int h);

static inline int map_in_bounds(const Map *m, int x, int y)
{
    return x >= 0 && y >= 0 && x < m->w && y < m->h;
}

uint8_t map_tile(const Map *m, int x, int y);
void    map_set_tile(Map *m, int x, int y, uint8_t kind);

/* x in [0, w], y in [0, h) */
uint8_t map_vedge(const Map *m, int x, int y);
void    map_set_vedge(Map *m, int x, int y, uint8_t kind);

/* x in [0, w), y in [0, h] */
uint8_t map_hedge(const Map *m, int x, int y);
void    map_set_hedge(Map *m, int x, int y, uint8_t kind);

int map_walkable(const Map *m, int x, int y);

/* Does the boundary crossed by stepping (dx,dy) from (x,y) stop movement?
 * This asks only about the boundary, not about what is on the far side.
 * Orthogonal steps only. */
int map_edge_blocked(const Map *m, int x, int y, int dx, int dy);

/* Does that same boundary stop sight? Not the same question: a window stops
 * movement and not sight, an open door stops neither, and you can see across
 * a chasm you cannot walk over. */
int map_edge_opaque(const Map *m, int x, int y, int dx, int dy);

/* Can a token step from (x,y) by (dx,dy)? Adds to the wall test the
 * requirements that the destination exists and is walkable. Handles the four
 * diagonals too; a diagonal is blocked if either of the orthogonal crossings
 * it is made of is blocked, so you cannot slip through a corner. */
int map_blocked(const Map *m, int x, int y, int dx, int dy);

/* Fills a tile rectangle, clipped to the map. */
void map_fill_tiles(Map *m, int x0, int y0, int x1, int y1, uint8_t kind);

/* Lays or clears the wall outline of a tile rectangle. */
void map_rect_walls(Map *m, int x0, int y0, int x1, int y1, uint8_t kind);

#endif /* VTT_MAP_H */
