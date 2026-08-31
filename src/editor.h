#ifndef VTT_EDITOR_H
#define VTT_EDITOR_H

#include "grid.h"
#include "input.h"
#include "map.h"
#include "theme.h"
#include "ui.h"
#include "undo.h"

typedef enum {
    ED_NORMAL,     /* cursor on tiles */
    ED_WALL,       /* cursor on lattice corners; movement lays wall */
    ED_VISUAL,     /* rectangular tile selection */
    ED_COMMAND,    /* the `:` line */
} EdMode;

#define ED_SCROLLOFF 2

typedef struct {
    GridView view;

    EdMode mode;
    int    cx, cy;             /* tile cursor */
    int    wx, wy;             /* lattice-corner cursor, in [0,w] x [0,h] */
    int    pen;                /* wall mode: laying wall while moving */
    int    erase;              /* wall mode: clearing instead of laying */
    int    anchor_x, anchor_y; /* visual-mode anchor tile */

    int    count;              /* pending numeric prefix, 0 when none */
    int    pending_g;          /* a `g` was typed and is awaiting its pair */

    /* What the pen lays and what the brush paints. Held here rather than
     * passed around so every tool agrees on the current choice. */
    uint8_t material;          /* EdgeKind laid by the pen and Shift-HJKL */
    uint8_t terrain;           /* TileKind painted by f and space */

    int    has_anchor;         /* wall mode: a rectangle anchor is set */
    int    ax, ay;             /* wall-mode rectangle anchor, in corner coords */

    TextPrompt cmd;            /* the `:` line */
} Editor;

void ed_init(Editor *e, const Map *m);

/* Recomputes the viewport rect and re-clamps the camera. Call on resize. */
void ed_layout(Editor *e, const Map *m, int screen_w, int screen_h);

void ed_move(Editor *e, const Map *m, int dx, int dy, int times);
void ed_set_zoom(Editor *e, const Map *m, int zoom);

/* The tile the user is acting on, which in wall mode is the corner's
 * south-east tile. */
void ed_cursor_tile(const Editor *e, int *tx, int *ty);

/* --------------------------------------------------------------- editing */

/* Sets one face of the cursor tile to the current material, or clears it when
 * it already is that material: (dx,dy) picks the face, so Shift-K is the
 * north face and Shift-L the east one. */
void ed_toggle_edge(Editor *e, Map *m, Undo *u, int dx, int dy);

/* Opens or closes every door on the four faces of the cursor tile. Secret
 * doors are only touched when asked for, so opening an ordinary door beside
 * one cannot give it away. Returns how many were toggled. */
int ed_toggle_doors(Editor *e, Map *m, Undo *u, int secret);

/* Steps the material and terrain selections. */
void ed_cycle_material(Editor *e);
void ed_cycle_terrain(Editor *e);

/* Wall-mode movement. Each corner-to-corner step crosses exactly one edge,
 * and with the pen down that edge is laid (or erased), so a room is drawn by
 * walking its outline. */
void ed_wall_step(Editor *e, Map *m, Undo *u, int dx, int dy, int times);

/* Lays or clears the wall outline between two lattice corners. */
void ed_wall_rect(Editor *e, Map *m, Undo *u, int x0, int y0, int x1, int y1,
                  uint8_t kind);

/* Applies a tile kind to the cursor tile, or to the whole selection when
 * visual mode is active. */
void ed_apply_tiles(Editor *e, Map *m, Undo *u, uint8_t kind);
void ed_toggle_tile(Editor *e, Map *m, Undo *u);

void ed_draw(Renderer *r, const Map *m, const Editor *e, const Theme *th, int ascii);

/* Fills `buf` with the mode/position/zoom status line. */
void ed_status(const Editor *e, const Map *m, char *buf, size_t bufsz);

const char *ed_mode_name(EdMode m);

#endif /* VTT_EDITOR_H */
