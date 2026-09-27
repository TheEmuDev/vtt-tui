#ifndef VTT_FLOOR_H
#define VTT_FLOOR_H

#include "map.h"

/* Floors: named areas marked with a level, an elevation (-1 a basement, 0 the
 * ground, 1 upstairs). The screen can show one at a time. docs/FLOORS.md.
 *
 * Every question about floors is asked here, never of the area boxes
 * directly: stacked layers (floors sharing coordinates, each its own grid)
 * would answer the same questions from different storage, and only this
 * file would change. */

#define FLOOR_LEVEL_MIN (-99)
#define FLOOR_LEVEL_MAX 99

/* The floor holding a square, as an area index, or -1. Floors never overlap,
 * so there is at most one. */
int  floor_at(const Map *m, int x, int y);
/* The floor holding a creature: the one under its top-left square. */
int  floor_of_token(const Map *m, const Token *t);

/* The floors low level first (the name breaks a tie, ignoring case), as
 * area indices. Returns how many. */
int  floor_order(const Map *m, int out[MAP_AREAS_MAX]);
/* The floor above (dir 1) or below (-1) `cur`, or -1 at the end. From the
 * whole map (cur -1), up is the lowest floor and down the highest. */
int  floor_step(const Map *m, int cur, int dir);

/* Why area `ai` cannot be a floor, or NULL: it would overlap another. */
const char *floor_problem(const Map *m, int ai);

/* The shown floor's box, or the whole map for -1. */
void floor_box(const Map *m, int f, int *x0, int *y0, int *x1, int *y1);

/* Is floor `f` (-1, the whole map) holding the square? */
static inline int floor_holds(const Map *m, int f, int x, int y)
{
    if (f < 0) return map_in_bounds(m, x, y);
    const Area *a = &m->areas[f];
    return x >= a->x0 && x <= a->x1 && y >= a->y0 && y <= a->y1;
}

/* The floor to show a side (TOKEN_PLAYER or TOKEN_ENEMY) on, when nothing
 * names a creature: stay on `shown` if one of the side is there; else the
 * floor with the most of them; else `last`, the floor one of them last moved
 * on; else the lowest. -1 when none of them is on any floor. */
int  floor_pick(const Map *m, int kind, int shown, int last);

/* "Upper" for a floor, "the whole map" for -1. */
const char *floor_name(const Map *m, int f);

#endif /* VTT_FLOOR_H */
