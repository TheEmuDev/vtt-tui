#ifndef VTT_MAPIO_H
#define VTT_MAPIO_H

#include <stddef.h>
#include "map.h"

#define MAPIO_ERR_MAX 160

/* Writes via a temporary file and rename(), so an interrupted save can never
 * leave a half-written map where the original was. */
int  mapio_save(Map *m, const char *path, char *err, size_t errsz);

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
