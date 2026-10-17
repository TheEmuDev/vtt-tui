#ifndef VTT_MAPTOOLS_H
#define VTT_MAPTOOLS_H

#include <stdio.h>

#include "map.h"

/* Headless tools for reading a map file: what an agent writing maps by
 * hand, or a GM checking one, needs that the app's screen does not give.
 * Everything here reads a Map and writes text; nothing draws, nothing
 * changes the map. Documented in README *Map tools* and docs/AGENTS.md.
 *
 * Cost: the room fill is linear in the map's squares with no recursion;
 * the account and the linter then pass over each room's bounding box, so
 * many rooms with large overlapping boxes cost more than linear. A 512x512
 * map of 262,144 rooms is about a third of a second. One-shot tools, so
 * no profiler zones. */

/* The whole map, or the region x0..x1, y0..y1 (inclusive, clipped), as a
 * text lattice in the file format's own alphabet: one character a square,
 * one a boundary, column letters and row numbers round it, then a legend
 * for creatures, notes and fog. */
void maptools_dump(FILE *out, const Map *m, int x0, int y0, int x1, int y1);
/* The same with parts left off: the channel's `dump` has no key (`dump key`
 * is its own request: 330 bytes an agent needs once, not with every read),
 * and `changes` prints several regions under its own headings, so each
 * without the map's line and the region's. */
enum { DUMP_KEY = 1, DUMP_FOOT = 2 };
void maptools_dump_parts(FILE *out, const Map *m, int x0, int y0, int x1, int y1, unsigned parts);
void maptools_dump_key(FILE *out);

/* "B2:K12" (either order, case aside; one square alone is that square)
 * into squares. A row without a column -- "5:6" -- keeps every column, as
 * :6 keeps the column. Returns 0 when it is not two squares. */
int  maptools_region(const Map *m, const char *spec, int *x0, int *y0, int *x1, int *y1);

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
/* The same for the rooms that meet a box (x0, y0, x1, y1; NULL for all): the
 * map's own lines are always there, and a line says how many rooms of how
 * many. A room is whole or absent, never cut. */
void maptools_describe_in(FILE *out, const Map *m, int json, const int *box);

/* A linter. Loads `path` itself, so the file's own mistakes come with
 * their line numbers (mapio_load_diag), then checks the map: doors and
 * walls in void, loose doors, creatures on void, off the map or on top of
 * each other, repeated names, rooms the party cannot reach, fog patches
 * with nothing painted, notes on void. One line a finding, a stable code
 * first (E error, W warning, N note); JSON with `json`. Returns the exit
 * status: 0 clean, 1 an error or warning, 2 the file cannot be read. */
int  maptools_check(FILE *out, const char *path, int json);

/* The same checks on a map already in memory -- the live one, for the
 * control channel -- so without the file's own findings and line numbers.
 * JSON's "file" is the map's path, or null for one never saved. */
int  maptools_check_map(FILE *out, const Map *m, int json);

/* The open map's report (:check, the channel's check): its file's findings
 * from Map.base as vtt last read or wrote it -- with line numbers, the line
 * search and its fold -- then the live map's, less those the fold explains. */
int  maptools_check_open(FILE *out, const Map *m, int json);

/* What an open says of its file: a sink for mapio_load_base_diag, then
 * maptools_file_finish -- the line search on Map.base -- which returns the
 * file's warnings and errors (`errors` of them errors), `summary` the first
 * ("line 214, ..."), and `rows` the line of a row added or lost (W031,
 * E011), else 0. */
typedef struct MaptoolsFile MaptoolsFile;
MaptoolsFile *maptools_file_new(void);
void maptools_file_sink(void *ctx, int line, int col, const char *code, const char *slug, const char *msg);
int  maptools_file_finish(MaptoolsFile *f, const Map *m, char *summary, size_t cap, int *rows, int *errors);
/* The same for a map already read, from its Map.base read again. */
int  maptools_file_of(const Map *m, char *summary, size_t cap, int *rows, int *errors);
void maptools_file_free(MaptoolsFile *f);

#endif /* VTT_MAPTOOLS_H */
