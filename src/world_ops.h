#ifndef VTT_WORLD_OPS_H
#define VTT_WORLD_OPS_H

/* The operations the GM's keys and the channel's requests both make
 * (docs/V2.md, *Step 6*): each one checks, records through the world's log
 * and says why it refused, once, so a limit or a check has one wording on
 * the GM's line and in a refusal. Parsing stays with each side: the GM's
 * command line and keys, the channel's words. None opens or closes an undo
 * batch; the caller has one open. */

#include "world.h"

/* Why an operation refused: the words, and the class a request's refusal
 * takes (ctl.h, CTL_E_*). */
typedef struct {
    int  cls;
    char text[200];
} WorldErr;

/* Areas. `name` is whether NAME may name an area at all; `fits`, whether it
 * may name the box (a valid name; a floor keeps its creatures inside it);
 * `set` is that and the naming, refused when the map holds all it can;
 * `remove` takes a name off, its box in `box` (x0, y0, x1, y1). Each returns
 * 0, or -1 with why in `e`. */
int world_area_name(const char *name, WorldErr *e);
int world_area_fits(const World *w, const char *name, int x0, int y0, int x1, int y1, WorldErr *e);
int world_area_set(World *w, const char *name, int x0, int y0, int x1, int y1, WorldErr *e);
int world_area_remove(World *w, const char *name, int box[4], WorldErr *e);

/* A note on square x,y (on the map), or "" to take it off: refused when the
 * text is too long or the map holds all the notes it can. */
int world_note_set(World *w, int x, int y, const char *text, WorldErr *e);

/* Links. `num` is the number a new link takes, or -1 when the map holds all
 * it can; `set` puts down a new link or a change to one, refused when an end
 * is misplaced or on void (link_problem); `remove` takes link `num` away. */
int world_link_num(const World *w, WorldErr *e);
int world_link_set(World *w, const Link *l, WorldErr *e);
int world_link_remove(World *w, int num, WorldErr *e);

/* Floors (floor.h): area `ai` made a floor at `level`, refused out of range
 * or where it would overlap another floor; or made a floor no longer,
 * refused when it is not one. */
int world_floor_set(World *w, int ai, int level, WorldErr *e);
int world_floor_off(World *w, int ai, WorldErr *e);

/* Environments (docs/ENVIRONMENTS.md): `card_name` is whether CARD may name
 * a card at all; `area_env` makes card CARD area `ai`'s environment, or ""
 * takes it off, refused when it has none to take. */
int world_card_name(const char *card, WorldErr *e);
int world_area_env(World *w, int ai, const char *card, WorldErr *e);

#endif /* VTT_WORLD_OPS_H */
