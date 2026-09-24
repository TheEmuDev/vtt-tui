#ifndef VTT_COUNTER_H
#define VTT_COUNTER_H

#include <stddef.h>

#include "token.h"

/* Counters on a creature: HP 4/6, Stress 2/6. The tool attaches no meaning
 * to one reaching zero or its maximum; it keeps the number for the GM, who
 * decides what it means. A ruleset may name the counters its game uses
 * ("HP Stress Armor" for Daggerheart); those spellings are offered by the
 * prompt and win over the GM's capitalisation, and nothing else follows from
 * them -- a creature has only the counters it has been given numbers for. */

/* Index of the counter called `name`, case aside, or -1. */
int  counter_find(const Token *t, const char *name);

/* Sets a counter, creating it when absent. `max` is clamped to
 * 1..COUNTER_VALUE_MAX and `value` to 0..max. Returns its index, -1 when
 * the creature already holds TOKEN_COUNTER_MAX, -2 for a bad name. */
int  counter_set(Token *t, const char *name, int value, int max);
void counter_remove(Token *t, int idx);

/* "HP 4/6  Stress 2/6", or "" for none. */
void counter_format(const Token *t, char *buf, size_t bufsz);

/* What s v's prompt means. Clauses separated by commas:
 *
 *   hp 6/8    set value and maximum, creating it
 *   hp 6      set the value -- or, for a new counter, value and maximum
 *   hp -2     two off;  hp +1 one on
 *   hp        make it the counter < and > step
 *   -hp       take it off the creature
 *
 * Works on *t in place, so the caller passes a copy and commits it through
 * the undo log. `current` is the counter < and > step, updated as clauses
 * name one. `names` is the ruleset's spellings, space-separated, or NULL.
 * Returns 0 with a summary in msg, or -1 with the complaint in msg and *t
 * in an unspecified state. */
int  counter_apply(Token *t, const char *text, const char *names,
                   char *current, size_t cursz, char *msg, size_t msgsz);

/* The counter < and > step when none has been named yet: the ruleset's
 * first, or HP. */
void counter_default(const char *names, char *buf, size_t bufsz);

#endif /* VTT_COUNTER_H */
