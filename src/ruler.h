#ifndef VTT_RULER_H
#define VTT_RULER_H

#include <stddef.h>
#include <stdint.h>

#include "draw.h"
#include "grid.h"
#include "map.h"
#include "theme.h"

/* ---------------------------------------------------------------- metrics */

typedef enum {
    DIST_CHEBYSHEV = 0,   /* every step costs one tile, diagonals included */
    DIST_EUCLIDEAN,       /* true straight line, what a tape measure reads */
    DIST_ALT_DIAG,        /* diagonals alternate 1 and 2 (D&D 3.5 / PF) */
    DIST_MANHATTAN,       /* no diagonals at all */
    DIST_COUNT,
} DistMetric;

const char *dist_metric_name(DistMetric m);

/* A distance printed the way every readout prints one: trailing ".0" trimmed,
 * so whole numbers read as whole numbers. Shared so the ruler, the range
 * overlay and a creature being carried all say a number the same way. */
void dist_fmt(char *buf, size_t n, double v);

/* Returns -1 when the name matches nothing. */
int    dist_metric_from_name(const char *name);
double dist_tiles(DistMetric m, int dx, int dy);

/* ------------------------------------------------------------ range bands */

/* Systems that describe distance in bands rather than numbers. `max` is the
 * inclusive upper bound in the map's own units; the last band uses INFINITY. */
typedef struct {
    const char *name;
    double      max;
} RangeBand;

typedef struct {
    const char      *name;
    const RangeBand *bands;
    int              nbands;
    /* Cleared while the thresholds are placeholders, so the readout can say
     * so rather than quietly reporting a number nobody checked. */
    int              verified;
} Ruleset;

const Ruleset *ruleset_by_name(const char *name);   /* NULL if unknown */
const Ruleset *ruleset_at(int i);                   /* for listing */
const char    *ruleset_band(const Ruleset *rs, double units);

/* ------------------------------------------------------------------ ruler */

#define RULER_MAX_POINTS 16

typedef struct { int16_t x, y; } RulerPt;

typedef struct {
    int     active;
    RulerPt pts[RULER_MAX_POINTS];   /* pts[0] is the anchor */
    int     n;                       /* committed points, >= 1 when active */
    int16_t cx, cy;                  /* the live end, following the cursor */
} Ruler;

void ruler_reset(Ruler *r);
void ruler_start(Ruler *r, int x, int y);
void ruler_set_cursor(Ruler *r, int x, int y);

/* Commits the live end as a waypoint. Returns 0 when full. */
int  ruler_add_waypoint(Ruler *r);
/* Removes the last waypoint. Returns 0 when only the anchor remains. */
int  ruler_drop_waypoint(Ruler *r);

/* Total length of the polyline, in tiles. */
double ruler_tiles(const Ruler *r, DistMetric m);

/* The tiles the line passes through, for highlighting. Returns the number
 * written, which is capped at `max`. */
int ruler_trace(const Ruler *r, RulerPt *out, int max);

/* Is the straight line between two tiles broken by a wall? This asks about
 * sight, so it ignores whether the tiles are walkable: you can see across a
 * pit you cannot walk over. A step that moves both axes passes if either way
 * round the corner is open. */
int sight_blocked(const Map *m, int x0, int y0, int x1, int y1);

/* The same question, from the ruler's anchor to its live end. */
int ruler_sight_blocked(const Ruler *r, const Map *m);

void ruler_draw(Renderer *rn, const Map *m, const GridView *g, const Ruler *r,
                const Theme *th, int show_label);

/* One-line readout: length, distance, band, and whether sight is clear. */
void ruler_status(const Ruler *r, const Map *m, char *buf, size_t bufsz);

#endif /* VTT_RULER_H */
