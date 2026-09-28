#ifndef VTT_LINK_H
#define VTT_LINK_H

#include <stddef.h>
#include "map.h"

/* Links: two blocks of squares joined, so a creature standing on one end can
 * be sent to the other with a key -- stairs to the floor above, a portal to a
 * room off in the void. Nothing fires by itself: the GM presses g o. The
 * Link type lives in map.h beside the other things a map holds. */

const char *link_kind_name(uint8_t kind);
int         link_kind_from_name(const char *name);    /* -1 when unknown */
uint32_t    link_glyph(uint8_t kind, int ascii);

int  link_find(const Map *m, int num);                 /* index, or -1 */

/* The link with an end meeting the w x h block at (x,y), or -1; *end (if
 * given) says which end. A square is the 1 x 1 block. */
int  link_meets(const Map *m, int x, int y, int w, int h, int *end);
static inline int link_at(const Map *m, int x, int y, int *end)
{
    return link_meets(m, x, y, 1, 1, end);
}

/* The lowest number no link has, or 0 when the map holds all it can. */
int  link_free_num(const Map *m);

/* Why this link cannot go on the map, or NULL: an end off the map or on
 * void, the two ends overlapping, or a square another link already has.
 * The link that already holds l->num is not in its own way, so a change
 * can be checked before it is made. */
const char *link_problem(const Map *m, const Link *l);
/* The same without the ground: what the loader holds a link to. Ground can
 * go from under an end after the link is made (x, tile void), and a link
 * dropped on the next load for that would be lost without a word -- so it
 * stays, and --check says so (W150). */
const char *link_misplaced(const Map *m, const Link *l);
/* 1 with the first void square under either end. */
int link_void_square(const Map *m, const Link *l, int *vx, int *vy);

/* Adds the link, or replaces the one with its number. Checks nothing but
 * room: -1 when the map is full. Returns the index. Touches the map. */
int  link_put(Map *m, const Link *l);
/* Returns 1 if there was one to remove. Touches the map. */
int  link_remove(Map *m, int num);

/* A trip through a link: every creature with a square on the near end goes,
 * keeping its place relative to the end, so a party on a 3x3 portal lands
 * in the same formation. All or nothing. */
#define LINK_TRIP_MAX 32
typedef struct {
    int  idx[LINK_TRIP_MAX];   /* token indices, low first */
    int  n;
    int  dx, dy;               /* the same shift for every one */
    char why[96];              /* when n is 0: what refused it */
} LinkTrip;

/* Plans the trip from end `from` of link `li`. With `enforce` off (ctrl-w)
 * a creature may land on void or on another creature; the map's edge still
 * holds. Returns the number going, or 0 with `why` said. */
int  link_trip(const Map *m, int li, int from, int enforce, LinkTrip *t);

/* A map a link may lead to: its file's name without .vtt, 1 to
 * LINK_MAP_MAX-1 characters, no quote or slash, not hidden. */
int  link_map_name_ok(const char *name);

/* The file a link to another map names: beside m's own file. 0 when m has
 * no file to be beside. */
int  link_map_path(const Map *m, const char *to_map, char *buf, size_t sz);

/* Could a link from m lead to PLACE in TO_MAP: the file there, and the place
 * in it an area or a square on ground? NULL, or why not in buf. It reads the
 * other file. */
const char *link_map_check(const Map *m, const char *to_map, const char *place, char *buf, size_t sz);

/* Where a party arriving through a link to another map lands in `dst`. The
 * party is n creatures at offsets (their x, y) from the near end's top-left,
 * kept in formation. `place` is a named area -- the formation inside it,
 * nearest its middle -- or a square, the formation's box nearest it. Every
 * square must be ground and free. Returns 1 with the far end's top-left in
 * *ax, *ay, or 0 with why. */
int  link_land(const Map *dst, const char *place, const Token *party, int n,
               int *ax, int *ay, char *why, size_t whysz);

/* "stairs 3" -- how a link is named in every message. */
void link_name(const Link *l, char *out, size_t outsz);
/* An end's squares: "C3", or "C3-D4" for a block. */
void link_end_name(const Link *l, int end, char *out, size_t outsz);
/* "  stairs 3 to K12" for a status line: the link at the square, or ""
 * when there is none -- or it is secret and the line is not the GM's. */
void link_status(const Map *m, int x, int y, int gm, char *out, size_t outsz);
/* "stairs 3  C3 <-> K12  2x2  one-way  secret": a line of a list. */
void link_describe(const Link *l, char *out, size_t outsz);

#endif /* VTT_LINK_H */
