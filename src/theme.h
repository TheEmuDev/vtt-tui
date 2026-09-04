#ifndef VTT_THEME_H
#define VTT_THEME_H

#include "map.h"
#include "render.h"

/* All color choices in one place so a theme switch is a data change. */
typedef struct {
    uint32_t bg;
    uint32_t fg;
    uint32_t dim;
    uint32_t accent;
    uint32_t wall;        /* white lines */
    uint32_t grid;        /* thin grey lines, walkable tiles only */
    uint32_t void_mark;   /* the dot that says a square is not map */
    uint32_t cursor_bg;
    uint32_t sel_bg;
    uint32_t player;
    uint32_t enemy;

    /* Selection is a colour of its own rather than the base lightened. Green
     * sits near the top of the luminance range already, so lightening it can
     * only move it a few percent -- which is exactly how the selected player
     * came to be almost indistinguishable from an unselected one. */
    uint32_t player_sel;
    uint32_t enemy_sel;
    uint32_t bar_bg;
    uint32_t bar_fg;
    uint32_t bar_key;
    uint32_t warn;
    uint32_t ruler;       /* the measured line */
    uint32_t ruler_bg;
    uint32_t ruler_bad;   /* the line when sight is broken */
    uint32_t range_bg;    /* ground within the chosen range band */
    uint32_t range_dim;   /* in range, but with no line to it */
    uint32_t trail;       /* the mark on the tile a held token set out from */
    uint32_t trail_bg;    /* ground a held token has walked over */

    /* Boundary kinds. Secret is only ever used in build mode; in play a
     * secret door is drawn exactly as a wall. */
    uint32_t edge_door;
    uint32_t edge_window;
    uint32_t edge_secret;

    /* Indexed by TileKind, so a theme carries the whole terrain palette. */
    /* Status marker palette, indexed by Status.color. */
    uint32_t status[STATUS_COLOR_COUNT];

    uint32_t terrain_fg[TILE_COUNT];
    uint32_t terrain_bg[TILE_COUNT];
} Theme;

extern const Theme THEME_DARK;

#endif /* VTT_THEME_H */
