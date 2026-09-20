#ifndef VTT_CLOCK_H
#define VTT_CLOCK_H

#include <stddef.h>
#include <stdint.h>

#include "draw.h"
#include "map.h"
#include "render.h"
#include "theme.h"
#include "undo.h"

/* A clock is a name and a count of segments, some of them filled: the
 * countdown a Daggerheart GM ticks when the party dawdles, the progress
 * clock a heist game fills, the "three more rounds until the roof comes in"
 * a GM writes on a sticky note. It runs one of two ways -- up from empty to
 * full, or down from full to nothing -- and a tick is always a step towards
 * the end. Nothing ticks by itself: the tool serves a table that rolls its
 * own dice and decides for itself when time has passed. It attaches no
 * meaning to a clock reaching its end either; it lights the row and says so.
 *
 * Clocks live in fixed slots on the map. A dropped clock leaves its slot
 * empty rather than shifting the ones after it, so an undo op that names a
 * slot still names the clock it was recorded against, or nothing. */
#define CLOCK_SIZE_MAX 24

int clock_count(const Map *m);

/* The slot whose name starts with `prefix`, case-insensitively; an exact
 * match wins over a longer one. -1 for none, -2 when several match. */
#define CLOCK_NONE      (-1)
#define CLOCK_AMBIGUOUS (-2)
int clock_find(const Map *m, const char *prefix);

/* Starts a clock of `size` segments at its start -- full for a countdown,
 * empty otherwise -- or resizes the one already called `name`, keeping its
 * value clamped, unless the direction changes, when it starts over. Returns
 * the slot, or -1 when every slot is taken or the name does not start with
 * a letter. The name is one word, cut to fit. */
int  clock_start(Map *m, const char *name, int size, int down);
void clock_drop(Map *m, int idx);

/* The value a clock starts at, and whether it has reached its end. */
static inline int clock_start_value(const Clock *c) { return c->down ? c->size : 0; }
static inline int clock_done(const Clock *c) { return c->down ? c->value == 0 : c->value >= c->size; }

/* Sets the value outright, through the undo log so a tick made by mistake
 * is one u away. Clamped to the clock; returns the new value. */
int  clock_set(Map *m, Undo *u, int idx, int value);
/* Steps `delta` towards the end (back for a negative one); the value that
 * would result, unclamped, is returned through *want so the caller can say
 * why a tick did nothing. */
int  clock_tick(Map *m, Undo *u, int idx, int delta, int *want);

/* "Dragon 3/6" */
void clock_format(const Clock *c, char *buf, size_t bufsz);

/* Rows the side panel needs for the clocks: 0 when there are none. */
int  clock_panel_rows(const Map *m);
void clock_draw_panel(Renderer *r, const Map *m, const Theme *th, Rect rc, int ascii);

#endif /* VTT_CLOCK_H */
