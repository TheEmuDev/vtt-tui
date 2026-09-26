#ifndef VTT_MAPTOOLS_H
#define VTT_MAPTOOLS_H

#include <stdio.h>

#include "map.h"

/* Headless tools for reading a map file: what an agent writing maps by
 * hand, or a GM checking one, needs that the app's screen does not give.
 * Everything here reads a Map and writes text; nothing draws, nothing
 * changes the map. Documented in README *Map tools* and docs/AGENTS.md.
 *
 * Cost: linear in the map's squares, no recursion; a 512x512 map is well
 * under 50 ms. One-shot tools, so no profiler zones. */

/* The whole map, or the region x0..x1, y0..y1 (inclusive, clipped), as a
 * text lattice in the file format's own alphabet: one character a square,
 * one a boundary, column letters and row numbers round it, then a legend
 * for creatures, notes and fog. */
void maptools_dump(FILE *out, const Map *m, int x0, int y0, int x1, int y1);

/* Rooms: the 4-connected areas of walkable squares that nothing but open
 * ground joins -- every wall, window and door (open or closed, secret or
 * not) is a room's edge, so opening a door in play renumbers nothing.
 * Numbered in reading order of their first square, which is also their
 * name: "room 2 (K2)". Doors of every kind join rooms for reachability,
 * windows do not. The start room is the first player creature's, else the
 * largest, else the first. */
typedef struct {
    int x0, y0, x1, y1;     /* bounding box */
    int fx, fy;             /* its first square in reading order: the name */
    int squares;
} Room;

typedef struct {
    int32_t *at;            /* w*h: the room a square is in, -1 for void */
    Room    *v;
    int      n;
    int      start;         /* -1 on a map with no rooms */
    uint8_t *reach;         /* n: reachable from the start through doors */
} Rooms;

void rooms_build(const Map *m, Rooms *r);
void rooms_free(Rooms *r);

/* The room a square belongs to, or -1. */
static inline int rooms_at(const Rooms *r, const Map *m, int x, int y)
{
    return map_in_bounds(m, x, y) ? r->at[(size_t)y * (size_t)m->w + (size_t)x] : -1;
}

/* A boundary named by the squares either side of it: "G5|H5" across a
 * vertical one, "C3/C4" across a horizontal one, '-' for off the map. */
void maptools_edge_name(const Map *m, int vertical, int x, int y, char *buf, size_t sz);

/* A structured account of the map: its settings, then every room with its
 * extent, terrain, doors and where they lead, creatures, fog and notes;
 * text, or JSON with `json`. */
void maptools_describe(FILE *out, const Map *m, int json);

/* A linter. Loads `path` itself, so the file's own mistakes come with
 * their line numbers (mapio_load_diag), then checks the map: doors and
 * walls in void, loose doors, creatures on void, off the map or on top of
 * each other, repeated names, rooms the party cannot reach, fog patches
 * with nothing painted, notes on void. One line a finding, a stable code
 * first (E error, W warning, N note); JSON with `json`. Returns the exit
 * status: 0 clean, 1 an error or warning, 2 the file cannot be read. */
int  maptools_check(FILE *out, const char *path, int json);

#endif /* VTT_MAPTOOLS_H */
