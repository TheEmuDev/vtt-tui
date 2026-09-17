#include "slog.h"

#include <errno.h>
#include <string.h>
#include <time.h>

#include "mapio.h"
#include "prof.h"
#include "util.h"

void slog_init(SessionLog *l) { memset(l, 0, sizeof *l); }

/* The time of day, with the date in front of it for the header. */
static void stamp(char *buf, size_t bufsz, int with_date)
{
    time_t    now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    size_t n = with_date ? strftime(buf, bufsz, "%Y-%m-%d %H:%M:%S", &tm)
                         : strftime(buf, bufsz, "%H:%M:%S", &tm);
    if (n == 0) buf[0] = '\0';
}

int slog_open(SessionLog *l, const char *path, const char *mapname, char *err, size_t errsz)
{
    slog_close(l);

    FILE *f = fopen(path, "a");
    if (!f) {
        snprintf(err, errsz, "cannot open %.60s: %s", path, strerror(errno));
        return -1;
    }
    l->f = f;
    str_lcpy(l->path, path, sizeof l->path);

    char when[40];
    stamp(when, sizeof when, 1);
    fprintf(f, "--- %s  log on: %s ---\n", when, mapname && mapname[0] ? mapname : "untitled");
    fflush(f);
    return 0;
}

void slog_close(SessionLog *l)
{
    if (!l->f) return;
    char when[40];
    stamp(when, sizeof when, 0);
    fprintf(l->f, "--- %s  log off ---\n", when);
    fclose(l->f);
    l->f = NULL;
}

void slog_write(SessionLog *l, const char *msg)
{
    if (!l->f || !msg || !msg[0]) return;
    PROF_ZONE("log.write");

    char when[16];
    stamp(when, sizeof when, 0);
    fprintf(l->f, "[%s] %s\n", when, msg);
    /* One line, one flush: the log is for the crash as much as the recap. */
    if (fflush(l->f) != 0) {
        fclose(l->f);
        l->f = NULL;
    }
}

void slog_default_path(const Map *m, char *buf, size_t bufsz)
{
    if (m->path[0]) {
        size_t n = strlen(m->path);
        int    vtt = n > 4 && strcmp(m->path + n - 4, ".vtt") == 0;
        snprintf(buf, bufsz, "%.*s.log", (int)(vtt ? n - 4 : n), m->path);
        return;
    }
    char dir[MAP_PATH_MAX];
    mapio_default_dir(dir, sizeof dir);
    snprintf(buf, bufsz, "%s/%s.log", dir, m->name[0] ? m->name : "untitled");
}
