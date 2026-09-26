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

#endif /* VTT_MAPTOOLS_H */
