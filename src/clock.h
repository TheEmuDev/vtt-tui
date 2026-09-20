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
 * a GM writes on a sticky note. The tool attaches no meaning to a clock
 * filling; it lights the row and says so, and the table decides what that
 * means.
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

/* Starts a clock of `size` segments, or resizes the one already called
 * `name`, keeping what was filled up to the new size. Returns the slot, or
 * -1 when every slot is taken or the name does not start with a letter.
 * The name is one word, cut to fit. */
int  clock_start(Map *m, const char *name, int size);
void clock_drop(Map *m, int idx);

/* Fills or empties segments, through the undo log so a tick made by
 * mistake is one u away. Clamped to the clock; returns the new value. */
int  clock_set(Map *m, Undo *u, int idx, int value);

/* "Dragon 3/6" */
void clock_format(const Clock *c, char *buf, size_t bufsz);

/* Rows the side panel needs for the clocks: 0 when there are none. */
int  clock_panel_rows(const Map *m);
void clock_draw_panel(Renderer *r, const Map *m, const Theme *th, Rect rc, int ascii);

#endif /* VTT_CLOCK_H */
