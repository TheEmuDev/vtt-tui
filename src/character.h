#ifndef VTT_CHARACTER_H
#define VTT_CHARACTER_H

#include <stddef.h>

#include "map.h"
#include "undo.h"

/* Character templates (docs/CHARACTERS.md): a creature saved once -- its
 * label, size, counters, note and the named rolls it uses -- and placed
 * again by name on any map. A template is a map file in the characters
 * directory, the creature's size square, holding the one creature at its
 * corner and the saved rolls; so the loader, the writer and the map tools
 * work on it unchanged. Names follow the stamp rules (stamp_name_ok). */

/* $XDG_DATA_HOME/vtt/characters, else ~/.local/share/vtt/characters. */
void character_dir(char *buf, size_t sz);

/* A template's name from a label: the copy number dropped ("Ghoul 2" is
 * Ghoul), every run of anything but a letter or digit made one '-'. ""
 * when nothing is left. */
void character_name_from_label(const char *label, char *out, size_t outsz);

/* Saves the creature at index `idx` of `m` under `name`, fresh: counters
 * full, no markers, no place in a fight, not hidden, and its label without
 * a copy number ("Ghoul 2" saves as Ghoul). `rolls` names map rolls
 * to carry along, each of which must be on the map (ignoring case). Returns
 * 0, or -1 with the reason in err. */
int  character_save(const Map *m, int idx, const char *name,
                    const char *const *rolls, int nrolls, char *err, size_t errsz);

/* Loads one, checked: exactly one creature, made fresh whatever the file was
 * edited to say. NULL with the reason in err. */
Map *character_load(const char *name, char *err, size_t errsz);

/* The template's creature. */
static inline const Token *character_token(const Map *tpl) { return &tpl->tokens.v[0]; }

/* The names, sorted; returns how many there are (see stamp_list_in). */
int  character_list(char (*names)[MAP_NAME_MAX], int max);

/* Puts the template's creature down with its top-left square at (x,y) as
 * `kind` (TOKEN_PLAYER, TOKEN_ENEMY, or -1 for the side it was saved on),
 * labeled as a paste is (Ghoul, Ghoul 2 ...), and adds the rolls the map
 * lacks -- one undo batch, nesting in the caller's. Refused, changing
 * nothing, off the map or on another creature. `said` gets what the status
 * line should add about rolls ("" for nothing): a roll kept because the map's
 * differs, or one with no slot. Returns the new creature's index, or -1 with
 * the reason in err. */
int  character_place(Map *m, Undo *u, const Map *tpl, int kind, int x, int y, int hidden,
                     char *said, size_t saidsz, char *err, size_t errsz);

#endif /* VTT_CHARACTER_H */
