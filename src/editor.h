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

/* ---------------------------------------------------------------- shapes */

typedef enum {
    ED_SHAPE_RECT = 0,
    ED_SHAPE_CIRCLE,
} EdShapeKind;

/* The tiles an anchor and a cursor describe: the box between them, or a disc
 * centred on the anchor and reaching the cursor.
 *
 * Held as a test rather than a list. A circle has no corners to walk between,
 * and the preview, the terrain fill and the wall outline all have to agree
 * about which tiles are in -- one predicate is how they cannot disagree.
 *
 * The centre and radius are in half-tiles, because the two modes anchor in
 * different places: build mode's cursor sits on a square, wall mode's on a
 * lattice corner, and doubling lets one test serve both. */
typedef struct {
    uint8_t kind;
    int     x0, y0, x1, y1;   /* bounding box of tiles, inclusive */
    long    ccx, ccy, cr2;    /* circle centre and radius squared, half-tiles */
} EdShape;

/* `corners` says the two points are lattice corners rather than squares. A
 * rectangle then spans the tiles between them rather than including both
 * ends, and a circle is centred on the corner itself. */
EdShape ed_shape(uint8_t kind, int ax, int ay, int bx, int by, int corners);
int     ed_shape_has(const EdShape *s, int x, int y);

/* Radius in whole tiles, for the readout. Zero for a rectangle. */
int     ed_shape_radius(const EdShape *s);

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

    int    has_anchor;         /* wall mode: an anchor is set */
    int    ax, ay;             /* wall-mode anchor, in corner coords */

    /* What the anchor draws: a box, or a disc centred on the anchor. Shared
     * by visual mode and wall mode, since both are anchor-plus-cursor. */
    uint8_t shape;

    /* The brush: how much ground the cursor is. f, space and x paint its
     * whole footprint and H J K L wall its whole face, so at 3 the cursor is
     * a 3x3 stamp. A view-and-tool preference like `labels`, deliberately
     * separate from play mode's token size: a big paint brush should not
     * make the next creature Large. */
    uint8_t brush;

    /* Column letters above the map and row numbers down its left. On by
     * default: a coordinate you cannot read is a coordinate you cannot jump
     * to. A view preference, so it is not saved with the map. */
    int    labels;

    TextPrompt cmd;            /* the `:` line */
} Editor;

void ed_init(Editor *e, const Map *m);

/* Recomputes the viewport rect and re-clamps the camera. Call on resize, and
 * whenever the labels are switched, since they take a row and a gutter. */
void ed_layout(Editor *e, const Map *m, int screen_w, int screen_h);

/* Columns reserved down the left for row numbers; 0 with the labels off. */
int  ed_gutter(const Editor *e, const Map *m);

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

/* Lays or clears the boundary of a shape: every face a covered tile shares
 * with one the shape does not cover. For a rectangle that is exactly its
 * outline, and it is the only sensible reading of a circle of wall. */
void ed_wall_shape(Map *m, Undo *u, const EdShape *s, uint8_t kind);

/* Applies a tile kind to the cursor tile, or to the whole selection when
 * visual mode is active. */
void ed_apply_tiles(Editor *e, Map *m, Undo *u, uint8_t kind);
void ed_toggle_tile(Editor *e, Map *m, Undo *u);

void ed_draw(Renderer *r, const Map *m, const Editor *e, const Theme *th, int ascii);

/* Fills `buf` with the mode/position/zoom status line. */
void ed_status(const Editor *e, const Map *m, char *buf, size_t bufsz);

const char *ed_mode_name(EdMode m);

#endif /* VTT_EDITOR_H */
