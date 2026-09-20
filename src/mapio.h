#ifndef VTT_MAPIO_H
#define VTT_MAPIO_H

#include <stddef.h>
#include "map.h"

#define MAPIO_ERR_MAX 160

/* Writes via a temporary file and rename(), so an interrupted save can never
 * leave a half-written map where the original was. mapio_write leaves the
 * map's own path and modified flag alone, for a copy that is not "the
 * save"; mapio_save is the save. */
int  mapio_write(const Map *m, const char *path, char *err, size_t errsz);
int  mapio_save(Map *m, const char *path, char *err, size_t errsz);

/* The recovery autosave that goes beside a map: path + ".autosave". A map
 * never saved gets one under its name in the map directory. */
void mapio_autosave_path(const Map *m, char *buf, size_t bufsz);

/* Is there an autosave newer than the file at `path` (or any at all, when
 * there is no file)? Returns the autosave's modification time through
 * *when, for the question that follows. */
int  mapio_autosave_newer(const char *path, const char *autosave, long *when);

/* Returns NULL on failure with a human-readable reason in err. */
Map *mapio_load(const char *path, char *err, size_t errsz);

typedef struct {
    char name[128];
    char path[MAP_PATH_MAX];
} MapEntry;

/* Where new maps go when the user gives only a name:
 * $XDG_DATA_HOME/vtt/maps, else ~/.local/share/vtt/maps. */
void mapio_default_dir(char *buf, size_t bufsz);

/* Lists *.vtt in the current directory and the default map directory,
 * de-duplicated and sorted. Caller frees the returned array. */
int  mapio_scan(MapEntry **out);

/* Appends ".vtt" when the name has no extension, and resolves a bare name to
 * the default map directory. */
void mapio_resolve_path(const char *name, char *buf, size_t bufsz);

#endif /* VTT_MAPIO_H */
