#ifndef VTT_PLAY_H
#define VTT_PLAY_H

#include "editor.h"
#include "grid.h"
#include "map.h"
#include "theme.h"
#include "ruler.h"
#include "undo.h"

/* Highlights every tile within one range band of an anchor, for effects that
 * catch everything in range rather than a single target. */
typedef struct {
    int active;
    int band;          /* index into the current ruleset's band list */
    int token;         /* token the anchor follows, or -1 for a bare tile */
    int ax, ay;        /* anchor tile, used when token is -1 */
} RangeOverlay;

void range_clear(RangeOverlay *ro);

/* Keeps the anchor pointing at the same creature after a token is removed,
 * since the list is an array and later indices shift down. The overlay stays
 * put as a bare tile when the creature it followed is the one that went. */
void range_token_removed(RangeOverlay *ro, int removed, int x, int y);

/* Cycles off -> first band -> ... -> last band -> off. The anchor is fixed
 * when the overlay is switched on, so later cycling only changes the reach.
 * Returns the new band index, or -1 when it switched off or the map has no
 * ruleset to take bands from. */
int  range_cycle(RangeOverlay *ro, const Map *m, int anchor_token, int cx, int cy);

/* Where the overlay measures from. Follows the token when it has one, so the
 * highlight moves with the creature. */
void range_anchor(const RangeOverlay *ro, const Map *m,
                  int *ax, int *ay, int *asize);

/* Distance from the anchor to a tile, in the map's units, measured from the
 * nearest tile of the anchor's footprint. */
double range_units_to(const RangeOverlay *ro, const Map *m, int tx, int ty);
int    range_contains(const RangeOverlay *ro, const Map *m, int tx, int ty);

void range_draw(Renderer *r, const Map *m, const GridView *g,
                const RangeOverlay *ro, const Theme *th);
void range_status(const RangeOverlay *ro, const Map *m, char *buf, size_t bufsz);

/* Longest route the ribbon will draw. Far past any move a table would make in
 * one pickup, and a route longer than this is not one anybody is reading off
 * the screen anyway. */
#define PLAY_TRAIL_MAX 256

typedef struct {
    int sel;             /* selected token index, -1 for none */
    int grabbed;         /* the selection is being moved */
    int origin_x, origin_y;
    int steps;           /* squares the route costs, or keystrokes with no route */

    /* How deep the undo log was when the creature was picked up, so esc can
     * take the walk back out of the history rather than answering it with a
     * step back. A cancelled move should leave no trace. */
    int grab_depth;

    /* Set when a creature has been picked up but the cursor has not committed
     * to it. An enlarged cursor can cover several creatures at once, and which
     * one you meant is a question the cursor cannot answer by itself -- so
     * enter walks the ones it covers while the cursor holds still, and the
     * first movement key is what settles it. Until then the cursor keeps its
     * own size and place, or cycling would drag it around the map. */
    int choosing;

    /* The shortest walkable route from where the creature set out to where it
     * stands now -- not the wandering the cursor did to get there. Recut every
     * time the token lands somewhere new. RulerPt is reused because it is just
     * a tile coordinate. Empty when no route exists at all. */
    RulerPt trail[PLAY_TRAIL_MAX];
    int     ntrail;

    /* Rules-agnostic means never fighting the GM: blocking can be switched
     * off to drop a token anywhere. */
    int enforce_walls;

    uint8_t next_size;   /* footprint for the next token placed */

    /* The yank buffer, so a creature can be stamped down repeatedly. */
    Token   yank;
    int     has_yank;

    uint8_t status_color;   /* colour the next marker will use */

    /* The last thing searched for, so n and N can walk the matches without
     * making you type it again. */
    char    search[TOKEN_LABEL_MAX];

    RangeOverlay range;
} Play;

void play_init(Play *p);

/* Can the whole footprint cross in this direction? Every row (or column) of
 * a multi-tile token has to be able to make the crossing, not just one.
 *
 * Creatures of the same side step through each other -- an ally is somebody
 * you squeeze past, not a wall -- but the other side is a wall. `except` is
 * the mover's own index, since a token always overlaps where it already is;
 * -1 for a token that is not in the list. */
int  token_can_move(const Map *m, const Token *t, int dx, int dy, int enforce,
                    int except);

/* Moves the selected token one step, recording it for undo. Returns 1 if it
 * moved. */
int  play_step(Map *m, Undo *u, Play *p, int dx, int dy);

/* Moves the focus to a token, or to nothing with -1. Every change of
 * selection goes through here so the state that hangs off the focus -- the
 * grab, the trail, the range overlay -- cannot be left behind pointing at
 * whoever you were looking at a moment ago. */
void play_focus(Play *p, int sel);

/* Selects a token the cursor covers, or clears the selection. `size` is the
 * cursor's footprint: an enlarged cursor reaches every creature under it, not
 * only the one on its top-left square. */
void play_select_at(Play *p, const Map *m, int tx, int ty, int size);

/* Cycles the selection through one track: TOKEN_PLAYER, TOKEN_ENEMY, or
 * PLAY_ANY_KIND for the whole list. `delta` is 1 forwards, -1 back. Walking
 * on from whatever is selected now means switching tracks picks up near where
 * you were looking rather than at the top of the list. Returns 0 when the
 * track is empty, leaving the selection alone: pressing the wrong one of
 * three keys should cost nothing. */
#define PLAY_ANY_KIND (-1)
int play_cycle(Play *p, const Map *m, int delta, int kind);

/* Selects the next token whose label contains `needle`, ignoring case and
 * searching on from the current selection so repeating walks the matches.
 * A NULL or empty needle repeats the last search. Returns 0 for no match. */
int play_find(Play *p, const Map *m, const char *needle, int delta);

/* Picks up the selected token, marking the tile it set out from. `undo_depth`
 * is the history's depth at that moment, which is where a cancel unwinds to. */
void play_grab(Play *p, const Map *m, int undo_depth);

/* The footprint the cursor covers. What is in hand beats what is set: while a
 * creature is being carried the cursor is that creature's own size, whatever
 * the size keys were last left on, so where it will land is never a guess.
 * The setting is untouched by a pickup, so putting the creature down brings
 * the cursor back to the size chosen before. */
uint8_t play_cursor_size(const Play *p, const Map *m);

/* Recuts the route from the origin to wherever the held token now stands, and
 * prices the move by its length. The trail is derived rather than recorded, so
 * anything that moves the token -- a step, an undo, a redo -- is answered by
 * calling this again. */
void play_trail_sync(Play *p, const Map *m);

void play_trail_draw(Renderer *r, const Map *m, const GridView *g,
                     const Play *p, const Theme *th, int ascii);

/* How far a carried creature has come, beside the creature rather than at the
 * bottom of the screen: while moving, the eye is on the token, and a number
 * it has to travel for is a number it reads late. Names the range band too
 * when the map has a ruleset, since that is what the distance is for. */
void play_move_label(Renderer *r, const Map *m, const GridView *g,
                     const Play *p, const Theme *th);

/* Is there room for a size x size token anchored here -- on the map, and on
 * a square nothing already stands on? `except` is a token to ignore, for
 * asking whether one can grow where it already is. */
int  play_can_place(const Map *m, int tx, int ty, int size, int except);

void play_draw(Renderer *r, const Map *m, const Editor *e, const Play *p,
               const Theme *th, int ascii);
void play_status(const Play *p, const Map *m, const Editor *e, char *buf, size_t bufsz);

#endif /* VTT_PLAY_H */
