#ifndef VTT_CARD_H
#define VTT_CARD_H

/* Cards: the map's table of them by name, and a creature's (docs/CARDS.md). */

#include <stddef.h>
#include "map.h"

/* Letters, digits, - and _, as a template's name (store_name_ok). */
int         card_name_ok(const char *name);

/* The card of that name, or -1. */
int         card_find(const Map *m, const char *name);

/* Sets a card's text (card_clean's copy of it), adding the card when the
 * map has none by that name. Returns its index, or -1: a bad name, or the
 * table full. */
int         card_set(Map *m, const char *name, const char *text);

/* The text as a card keeps it, on the heap: line ends made \n, cut at
 * CARD_TEXT_MAX-1 bytes on a character's edge, no blank lines at either end. */
char       *card_clean(const char *text);

/* A card's first line, "**" marks dropped, into buf: what a listing says of
 * it ("Acid Burrower - Tier 1 Solo"). */
void        card_first_line(const char *text, char *buf, size_t sz);

/* The text a creature shows: its card's, or NULL when it names none or one
 * the map does not have. */
const char *card_of(const Map *m, const Token *t);

#endif /* VTT_CARD_H */
