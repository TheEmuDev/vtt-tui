#ifndef VTT_IMPORT_H
#define VTT_IMPORT_H

#include <stdio.h>
#include <stddef.h>

/* vtt --import-adversaries FILE: the Daggerheart SRD's adversary list as
 * JSON (a list of objects: name, tier, type, description,
 * motives_and_tactics, difficulty, thresholds, hp, stress, atk, attack,
 * range, damage, experience, feature[{name, text}]) made character
 * templates -- an enemy, 1x1, HP and Stress full, a card laid out as the
 * stat block. A template already there is kept unless force. Says what it
 * did on out; returns how many it wrote, or -1 with why in err. */
int import_adversaries(const char *path, int force, FILE *out, char *err, size_t errsz);

/* vtt --import-environments FILE: the SRD's environment list as JSON (a list
 * of objects: name, tier, type, description, impulses, difficulty,
 * potential_adversaries, feature[{name, text, question}]) made cards kept as
 * text files in the environments folder, for :env to copy into a map
 * (docs/ENVIRONMENTS.md). One already there is kept unless force. Says what
 * it did on out; returns how many it wrote, or -1 with why in err. */
int import_environments(const char *path, int force, FILE *out, char *err, size_t errsz);

/* An environment kept in that folder: its text for the caller to free, or
 * NULL when there is none of that name. */
char *environment_load(const char *name);

#endif /* VTT_IMPORT_H */
