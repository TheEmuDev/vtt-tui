#ifndef VTT_TURN_H
#define VTT_TURN_H

#include <stddef.h>

#include "draw.h"
#include "map.h"
#include "render.h"
#include "theme.h"
#include "undo.h"

/* The turn order. Rules-agnostic: a creature in the order has a number, the
 * highest acts first, ties go to whichever was placed on the map first, and
 * one creature at most is acting. Going past the last starts a new round.
 *
 * A game with no initiative uses the other half only: turn_take hands the
 * turn to any creature at all, in the order or not, which is all that
 * passing a spotlight round the table needs.
 *
 * The state is on the tokens (Token.turn, Token.init) and every change goes
 * through the undo log, so u takes back a turn advanced by mistake and a
 * deleted creature comes back with its place. Each call here is one undo
 * batch, or part of the caller's if one is already open. */

/* How many creatures are in the order, and who is acting (-1 for nobody). */
int turn_count(const Map *m);
int turn_acting(const Map *m);

/* The next creature in walking order from `from` (-1 to start at an end):
 * the turn order first, highest number down, then everything not in it in
 * list order -- which is plain list order when there is no fight, so t, f
 * and e walk as they always did until somebody rolls initiative. `kind` is a
 * TokenKind or TOKEN_ANY_KIND. Wraps; -1 only when nothing matches. */
int turn_walk(const Map *m, int from, int dir, int kind);

#define TURN_NO_ORDER (-1)   /* nobody is in the order */
#define TURN_AT_START (-2)   /* stepping back past the first turn of round 1 */

/* Moves the turn on by `delta` places, back for a negative one. Returns the
 * creature now acting, or one of the two codes above with nothing changed. */
int turn_advance(Map *m, Undo *u, int delta);

/* A spotlight ruleset with no order: the turn is a side, not a creature.
 * turn_flip_spotlight passes it across and lets go of whoever held it. */
int  turn_spotlight_ruleset(const Map *m);
void turn_flip_spotlight(Map *m, Undo *u);

void turn_join(Map *m, Undo *u, int idx, int init);
void turn_leave(Map *m, Undo *u, int idx);
void turn_take(Map *m, Undo *u, int idx);

/* Ends the fight: nobody in the order, nobody acting, round 0. Returns how
 * many creatures were in it. */
int  turn_clear(Map *m, Undo *u);

/* Call just before a creature is removed from the map: if the turn is its,
 * the turn passes on first, inside the same undo step. turn_settle afterwards
 * closes the fight when that removed the last creature in the order. */
void turn_before_remove(Map *m, Undo *u, int idx);
void turn_settle(Map *m, Undo *u);

/* Keeps a loaded or pasted map honest: one actor at most. */
void turn_sanitize(Map *m);

/* "Round 2 - Ogre's turn, then Aria, Bram" for the title bar; empty when
 * there is no fight and nobody holds the turn. */
void turn_status(const Map *m, char *buf, size_t bufsz);
/* The same for the players' frame when `players`: over fog, a creature the
 * party cannot see is named "?". */
void turn_status_view(const Map *m, int players, char *buf, size_t bufsz);

/* ":turns" -- the whole order on one line: "Round 2: Ogre 18*, Aria 15". */
void turn_list(const Map *m, char *buf, size_t bufsz);

/* Is there anything for the side panel to show? A fight, a held turn, or a
 * spotlight ruleset, whose two sides are always worth a glance. */
int  turn_panel_wanted(const Map *m);

/* The side panel: the order top to bottom with the actor marked, or the two
 * sides of the spotlight with the lit one marked. Draws only inside rc. */
#define TURN_PANEL_W 24
/* `counter` names the counter to show beside whoever is acting ("HP"), or
 * is NULL for the players' frame, which never shows one -- and over fog
 * names a creature the party cannot see "?". */
void turn_draw_panel(Renderer *r, const Map *m, const Theme *th, Rect rc, int ascii,
                     const char *counter);

#endif /* VTT_TURN_H */
