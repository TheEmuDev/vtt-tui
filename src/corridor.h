#ifndef VTT_CORRIDOR_H
#define VTT_CORRIDOR_H

#include <stddef.h>
#include <stdint.h>
#include "map.h"
#include "undo.h"

typedef struct { int x0, y0, x1, y1; } Box;
typedef struct { int vert, x, y; } Face;

#define CORR_ENDS (2 * 3)             /* two ends, up to three squares wide */

/* A corridor: one or two legs of floor, and the faces at its ends where it
 * opens into each room. */
typedef struct {
    Box  leg[2];
    int  nleg;
    Face end[CORR_ENDS];
    int  nend;
    const Area *a, *b;                /* the rooms it joins */
} Corridor;

/* Where a corridor `width` squares wide (1-3) runs between rooms a and b:
 * straight when one is beside or above the other and they share at least
 * width rows or columns, else one bend -- out of a's side first, else its
 * top or bottom. It must run through void and no other named area (one
 * holding both rooms whole is theirs). 0, or -1 with why in err. */
int  corridor_plan(const Map *m, const Area *a, const Area *b, int width, Corridor *c,
                   char *err, size_t errsz);

/* Digs it through the undo log: the legs floored and walled along where
 * nothing is (a door or window already there stays), each end end_kind. */
void corridor_dig(Map *m, Undo *u, const Corridor *c, uint8_t end_kind);

#endif /* VTT_CORRIDOR_H */
