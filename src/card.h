#ifndef VTT_CARD_H
#define VTT_CARD_H

/* Cards: the map's table of them by name, and a creature's (docs/CARDS.md). */

#include <stddef.h>
#include "map.h"

/* Letters, digits, - and _, as a template's name (store_name_ok). */
int         card_name_ok(const char *name);

/* The card of that name, or -1. */
int         card_find(const Map *m, const char *name);

/* Sets a card's text, adding the card when the map has none by that name.
 * The text is copied, cut at CARD_TEXT_MAX-1 bytes on a character's edge,
 * its line ends made \n and trailing blank lines dropped. Returns its index,
 * or -1: a bad name, or the table full. */
int         card_set(Map *m, const char *name, const char *text);

/* The text a creature shows: its card's, or NULL when it names none or one
 * the map does not have. */
const char *card_of(const Map *m, const Token *t);

#endif /* VTT_CARD_H */
