#ifndef VTT_SCENE_H
#define VTT_SCENE_H

#include <stddef.h>
#include <stdio.h>

#include "map.h"
#include "undo.h"

/* Scenes (docs/SCENES.md): the creatures and the fight saved under a name on
 * the map (Map.scenes) and put back. The storage is map.h's Scene; this is
 * what is done with it. Saving and removing one are not undo steps; putting
 * one back is. */

/* 1 to SCENE_NAME_MAX-1 characters, no quote, not starting or ending in a
 * space, and not starting with the word save or diff. */
int  scene_name_ok(const char *name);

/* The scene of that name (ignoring case), or -1. */
int  scene_find(const Map *m, const char *name);

/* Saves the creatures as they stand under `name`, replacing a scene of that
 * name: every creature and the round and spotlight, or with `box` (x0, y0,
 * x1, y1, inclusive) the creatures meeting it. Returns the scene's index, or
 * -1 with the reason in err (a bad name, sixteen scenes already). */
int  scene_save(Map *m, const char *name, const int *box, char *err, size_t errsz);

/* Removes scene i. */
void scene_remove(Map *m, int i);

/* Puts scene i back, one undo batch (nesting in the caller's): the creatures
 * it replaces go -- every one, or those meeting its box -- and its own come
 * back as they were saved. A whole scene sets the round and spotlight; a
 * boxed one leaves them, and a creature of its keeps its place in the order
 * but not the turn while a creature outside holds it. Checked first: a scene
 * creature landing on one the scene does not replace refuses the lot,
 * changing nothing. Returns how many creatures it put down, or -1 with the
 * reason in err. */
int  scene_restore(Map *m, Undo *u, int i, char *err, size_t errsz);

/* What changed since scene i was saved, a line a difference: creatures
 * matched by label (unlabeled ones by side and square), then the round and
 * spotlight for a whole scene. Returns how many lines it wrote; none means
 * nothing changed. */
int  scene_diff(FILE *out, const Map *m, int i);

/* "5 creatures, round 2, C3:H8" -- what the picker and :scenes say of one. */
void scene_describe(const Map *m, int i, char *buf, size_t sz);

#endif /* VTT_SCENE_H */
