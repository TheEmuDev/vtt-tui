#ifndef VTT_GRID_H
#define VTT_GRID_H

#include "draw.h"
#include "map.h"
#include "theme.h"

/* Because walls live between tiles, the screen grid uses a pitch: every tile
 * gets an `iw x ih` interior plus the 1-cell boundary row/column it shares
 * with its neighbour, which is where walls and grid lines are drawn.
 * Interior widths are odd so there is a true centre cell to anchor a circle
 * or a label on. */
typedef struct {
    int iw, ih;
} ZoomLevel;

#define ZOOM_COUNT 4
extern const ZoomLevel ZOOM[ZOOM_COUNT];

static inline int zoom_pw(int z) { return ZOOM[z].iw + 1; }
static inline int zoom_ph(int z) { return ZOOM[z].ih + 1; }

typedef struct {
    int  zoom;
    int  cam_x, cam_y;   /* top-left visible cell, in map-cell space */
    Rect view;           /* where the map is drawn on screen */
} GridView;

int  grid_cells_w(const Map *m, int zoom);
int  grid_cells_h(const Map *m, int zoom);

/* Screen position of a tile's top-left *boundary* corner. */
void grid_tile_screen(const GridView *g, int tx, int ty, int *sx, int *sy);

/* Screen position of a tile's interior origin (one cell in from the corner). */
void grid_tile_interior(const GridView *g, int tx, int ty, int *sx, int *sy);

/* Keeps the camera inside the map, or centres the map when it is smaller
 * than the viewport. */
void grid_clamp_camera(GridView *g, const Map *m);
void grid_center_on(GridView *g, const Map *m, int tx, int ty);

/* Scrolls the minimum amount to bring a tile into view, keeping `margin`
 * tiles of context around it (vim's scrolloff). */
void grid_ensure_visible(GridView *g, const Map *m, int tx, int ty, int margin);

/* Changes zoom while holding the anchor tile as close to fixed on screen as
 * the new pitch allows. */
void grid_set_zoom(GridView *g, const Map *m, int zoom, int anchor_tx, int anchor_ty);

/* The inclusive range of tiles that can appear in the viewport. Lets callers
 * that shade the map do so per visible tile rather than per map tile. */
void grid_visible_tiles(const GridView *g, const Map *m,
                        int *x0, int *y0, int *x1, int *y1);

/* Which tile is under a screen cell; returns 0 if the cell is not on a tile
 * interior or boundary within the map. */
int  grid_screen_to_tile(const GridView *g, const Map *m, int sx, int sy, int *tx, int *ty);

/* `reveal` shows what only the GM should see: secret doors are drawn as plain
 * walls without it, so play mode gives nothing away to anyone reading the
 * screen. */
void grid_draw(Renderer *r, const Map *m, const GridView *g, const Theme *th,
               int ascii, int reveal);

/* Highlights the tile the cursor is on. */
void grid_draw_tile_cursor(Renderer *r, const GridView *g, int tx, int ty, uint32_t bg);

/* Recolours a tile's four boundary corner cells, keeping their glyphs. Marks
 * the cursor on top of a token without painting over the grid underneath. */
void grid_draw_tile_marker(Renderer *r, const GridView *g, int tx, int ty,
                           uint32_t fg);

/* Highlights a lattice corner, for the wall-tracing mode. */
void grid_draw_corner_cursor(Renderer *r, const GridView *g, int cx, int cy,
                             const Theme *th, int pen_down);

/* Draws a token: players as circles, enemies as squares sitting inside the
 * grid square. Declared here rather than in token.h because it needs the
 * view transform, and token.h sits below map.h in the include order. */
void grid_draw_token(Renderer *r, const GridView *g, const Token *t,
                     const Theme *th, int selected, int ascii);

/* Status markers, drawn above the token so they never hide its label. Kept
 * separate from grid_draw_token so they can be layered over every token once
 * the tokens themselves are down. */
void grid_draw_token_status(Renderer *r, const GridView *g, const Token *t,
                            const Theme *th, int ascii);

/* A hollow outline where a token was picked up from. */
void grid_draw_token_ghost(Renderer *r, const GridView *g, int tx, int ty,
                           int size, const Theme *th);

/* Screen area a token occupies: its tiles' interiors plus the boundary cells
 * between them, so a multi-tile token reads as one solid piece. */
void grid_token_area(const GridView *g, int tx, int ty, int size, Rect *out);

/* Tints a tile rectangle, for visual-mode selection feedback. */
void grid_draw_tile_region(Renderer *r, const GridView *g, int x0, int y0,
                           int x1, int y1, uint32_t bg);

#endif /* VTT_GRID_H */
