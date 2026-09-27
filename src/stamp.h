#ifndef VTT_STAMP_H
#define VTT_STAMP_H

#include <stddef.h>

#include "map.h"
#include "undo.h"

/* Stamps: a piece of map to put down again -- a pillar, a table and its
 * benches, a stretch of cave wall. A stamp is a Map, small, with its own
 * squares, boundaries, creatures and square notes and no fog; saved, it is
 * an ordinary .vtt file in the stamps directory, so the loader, the writer
 * and the map tools all work on it unchanged.
 *
 * Placing is see-through: a void square in the stamp leaves the map's
 * square, a boundary with nothing on it leaves the map's boundary. So a
 * pillar only adds its walls. */

/* The squares x0..x1, y0..y1 (inclusive, on the map), every boundary round
 * and between them, the creatures wholly inside, and the square notes. A
 * creature copied arrives fresh: no markers, no place in a fight. */
Map *stamp_copy(const Map *m, int x0, int y0, int x1, int y1);

/* New stamps: turned a quarter clockwise `quarters` times (negative for
 * the other way), or mirrored left to right. Boundaries turn with their
 * squares; the result is the caller's to free. */
Map *stamp_turned(const Map *s, int quarters);
Map *stamp_mirrored(const Map *s);

/* Puts the stamp down with its top-left square at (x,y), through the undo
 * log (one batch, which nests in the caller's). All or nothing: checked
 * first -- it must lie on the map, every creature must land on ground and
 * on nobody, the square notes must fit -- and refused with the reason in
 * err, changing nothing. Returns 1 placed, 0 refused. */
int  stamp_place(Map *m, Undo *u, const Map *s, int x, int y, char *err, size_t errsz);

/* The directory stamps live in: $XDG_DATA_HOME/vtt/stamps, else
 * ~/.local/share/vtt/stamps. */
void stamp_dir(char *buf, size_t sz);
/* The same for another kind of saved thing: $XDG_DATA_HOME/vtt/<sub>, else
 * ~/.local/share/vtt/<sub>. Character templates live in "characters". */
void stamp_data_dir(const char *sub, char *buf, size_t sz);

/* A stamp by name: letters, digits, - and _ only, so a name is always a
 * file in the stamps directory and never a path. */
int  stamp_name_ok(const char *name);

/* Saves under <dir>/<name>.vtt (making the directory), loads it back,
 * lists the names (sorted, up to max; returns how many there are). */
int  stamp_save(const Map *s, const char *name, char *err, size_t errsz);
Map *stamp_load(const char *name, char *err, size_t errsz);
int  stamp_list(char (*names)[MAP_NAME_MAX], int max);
/* The listing of any such directory: every NAME.vtt whose NAME passes
 * stamp_name_ok. names may be NULL with max 0, to count. */
int  stamp_list_in(const char *dir, char (*names)[MAP_NAME_MAX], int max);

/* The preview: the stamp shown on the map at (x,y) for one draw, without a
 * change to the map -- no undo, no Map.gen, no sight. Swaps the stamp's
 * squares, boundaries, creatures and notes in; stamp_unshow puts the map back
 * exactly. Clipped to the map, so the preview can hang off the edge. */
typedef struct {
    uint8_t  *tiles, *vedges, *hedges;   /* what was there, over the stamp's box */
    int       x, y, w, h;                /* the box on the map, clipped */
    int       sx, sy;                    /* its first square in the stamp */
    TokenList tokens;                    /* the map's own list, set aside */
    int       nnotes;                    /* the map's notes; the stamp's go after */
    int       nlinks;                    /* the map's links; the stamp's go after */
    Token    *both;                      /* the map's creatures and the stamp's */
    int       shown;
} StampShow;

void stamp_show(Map *m, const Map *s, int x, int y, StampShow *sv);
void stamp_unshow(Map *m, StampShow *sv);

#endif /* VTT_STAMP_H */
