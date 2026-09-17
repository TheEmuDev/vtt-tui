#ifndef VTT_SLOG_H
#define VTT_SLOG_H

#include <stddef.h>
#include <stdio.h>

#include "map.h"

/* The session log: one timestamped line per thing that happened at the
 * table -- a creature put down, a marker added, a roll -- appended to a text
 * file for reading back after the game. Off until :log turns it on. Every
 * line is flushed as it is written, so a crash loses nothing. */
typedef struct {
    FILE *f;
    char  path[MAP_PATH_MAX];
} SessionLog;

void slog_init(SessionLog *l);

/* Opens (appending) and writes a header naming the map. Returns 0, or -1
 * with the reason in err. Opening while open closes the old file first. */
int  slog_open(SessionLog *l, const char *path, const char *mapname, char *err, size_t errsz);
void slog_close(SessionLog *l);

static inline int slog_on(const SessionLog *l) { return l->f != NULL; }

/* Appends "[HH:MM:SS] msg". A no-op when the log is off, so callers need not
 * check. */
void slog_write(SessionLog *l, const char *msg);

/* Where the log goes when :log names no file: beside the map as name.log,
 * or in the map directory when the map has never been saved. */
void slog_default_path(const Map *m, char *buf, size_t bufsz);

#endif /* VTT_SLOG_H */
