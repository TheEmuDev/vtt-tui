#include "mapio.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "ruler.h"
#include "util.h"

/* v2 added terrain kinds and door/window/secret boundaries. A v1 reader would
 * take a closed door for an opening and water for a hole, so it must refuse
 * the file rather than quietly misread a sealed room as open.
 *
 * v3 added status markers on tokens. An older reader would ignore those lines
 * and silently drop them, which loses combat state from a saved fight, so it
 * refuses too. Each version still loads everything older. */
#define FORMAT_VERSION 3

/* ------------------------------------------------------------------ save */

static void put_tile_row(FILE *f, const uint8_t *row, int n)
{
    for (int i = 0; i < n; i++) fputc(tile_file_char(row[i]), f);
    fputc('\n', f);
}

/* Horizontal walls keep their historical '-' so a map still reads as a map in
 * a text editor; every other kind writes the same character either way. */
static void put_edge_row(FILE *f, const uint8_t *row, int n, char wall_char)
{
    for (int i = 0; i < n; i++)
        fputc(row[i] == EDGE_WALL ? wall_char : edge_file_char(row[i]), f);
    fputc('\n', f);
}

int mapio_save(Map *m, const char *path, char *err, size_t errsz)
{
    char tmp[MAP_PATH_MAX + 8];
    snprintf(tmp, sizeof tmp, "%s.tmp", path);

    FILE *f = fopen(tmp, "w");
    if (!f) {
        snprintf(err, errsz, "cannot write %s: %s", tmp, strerror(errno));
        return -1;
    }

    fprintf(f, "VTT %d\n", FORMAT_VERSION);
    fprintf(f, "name %s\n", m->name);
    fprintf(f, "size %d %d\n", m->w, m->h);
    fprintf(f, "zoom %d\n", m->zoom);
    fprintf(f, "scale %g\n", m->scale_ft);
    fprintf(f, "metric %s\n", dist_metric_name((DistMetric)m->metric));
    if (m->ruleset[0]) fprintf(f, "ruleset %s\n", m->ruleset);

    fputs("tiles\n", f);
    for (int y = 0; y < m->h; y++)
        put_tile_row(f, m->tiles + (size_t)y * (size_t)m->w, m->w);

    fputs("vedges\n", f);
    for (int y = 0; y < m->h; y++)
        put_edge_row(f, m->vedges + (size_t)y * (size_t)(m->w + 1), m->w + 1, '|');

    fputs("hedges\n", f);
    for (int y = 0; y <= m->h; y++)
        put_edge_row(f, m->hedges + (size_t)y * (size_t)m->w, m->w, '-');

    for (int i = 0; i < m->tokens.n; i++) {
        const Token *t = &m->tokens.v[i];
        fprintf(f, "token %s %d %d %d \"%s\"\n",
                token_kind_name(t->kind), t->x, t->y, t->size, t->label);

        /* Markers follow the token they hang on, so a token line stays short
         * and the attachment needs no index to go wrong. */
        for (int j = 0; j < t->nstatus; j++)
            fprintf(f, "tokenstatus %s \"%s\"\n",
                    status_color_name(t->status[j].color), t->status[j].label);
    }

    int ok = (fflush(f) == 0);
    if (ok) ok = (fsync(fileno(f)) == 0) || errno == EINVAL;   /* pipes are fine */
    if (fclose(f) != 0) ok = 0;

    if (!ok) {
        snprintf(err, errsz, "write failed: %s", strerror(errno));
        unlink(tmp);
        return -1;
    }
    if (rename(tmp, path) != 0) {
        snprintf(err, errsz, "cannot replace %s: %s", path, strerror(errno));
        unlink(tmp);
        return -1;
    }

    str_lcpy(m->path, path, sizeof m->path);
    m->modified = 0;
    return 0;
}

/* ------------------------------------------------------------------ load */

/* Reads one line without its newline. Returns -1 at EOF. */
static int read_line(FILE *f, char *buf, size_t bufsz)
{
    if (!fgets(buf, (int)bufsz, f)) return -1;
    size_t n = strlen(buf);
    while (n && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) buf[--n] = '\0';
    return (int)n;
}

/* Rows are read by index and short lines are treated as trailing blanks, so
 * an editor that strips trailing whitespace cannot corrupt a map. An
 * unrecognised character reads as empty rather than aborting the load: a map
 * with one odd byte in it is still worth opening. */
static void parse_tile_row(const char *line, uint8_t *row, int n)
{
    size_t len = strlen(line);
    for (int i = 0; i < n; i++) {
        int k = (size_t)i < len ? tile_from_file_char(line[i]) : TILE_VOID;
        row[i] = (uint8_t)(k >= 0 ? k : TILE_VOID);
    }
}

static void parse_edge_row(const char *line, uint8_t *row, int n)
{
    size_t len = strlen(line);
    for (int i = 0; i < n; i++) {
        int k = (size_t)i < len ? edge_from_file_char(line[i]) : EDGE_NONE;
        row[i] = (uint8_t)(k >= 0 ? k : EDGE_NONE);
    }
}

/* Copies the text between the first quote and the last, so a label may hold
 * spaces. Returns 0 when there is no quoted section. */
static int parse_quoted(const char *from, char *out, size_t outsz)
{
    out[0] = '\0';
    if (!from || *from != '"') return 0;

    from++;
    const char *end = strrchr(from, '"');
    size_t      len = end && end > from ? (size_t)(end - from) : strlen(from);
    if (len >= outsz) len = outsz - 1;
    memcpy(out, from, len);
    out[len] = '\0';
    return 1;
}

/* A marker hangs on whichever token was read last. */
static int parse_status_line(Map *m, const char *line)
{
    if (m->tokens.n == 0) return -1;

    char colour[16] = { 0 };
    int  consumed = 0;
    if (sscanf(line, "tokenstatus %15s %n", colour, &consumed) < 1) return -1;

    int c = status_color_from_name(colour);
    if (c < 0) return -1;

    char label[STATUS_LABEL_MAX];
    parse_quoted(consumed > 0 ? line + consumed : NULL, label, sizeof label);

    token_add_status(&m->tokens.v[m->tokens.n - 1], (uint8_t)c, label);
    return 0;
}

static int parse_token_line(Map *m, const char *line)
{
    char kind[16] = { 0 };
    int  x, y, size;
    int  consumed = 0;

    if (sscanf(line, "token %15s %d %d %d %n", kind, &x, &y, &size, &consumed) < 4)
        return -1;

    Token t;
    memset(&t, 0, sizeof t);
    t.x    = (int16_t)x;
    t.y    = (int16_t)y;
    t.size = (uint8_t)iclamp(size, 1, TOKEN_SIZE_MAX);
    t.kind = (uint8_t)(strcmp(kind, "enemy") == 0 ? TOKEN_ENEMY : TOKEN_PLAYER);

    /* The label is quoted so it may contain spaces. */
    parse_quoted(consumed > 0 ? line + consumed : NULL, t.label, sizeof t.label);

    if (!map_in_bounds(m, t.x, t.y)) return -1;
    tokens_add(&m->tokens, t);
    return 0;
}

Map *mapio_load(const char *path, char *err, size_t errsz)
{
    FILE *f = fopen(path, "r");
    if (!f) {
        snprintf(err, errsz, "cannot open %s: %s", path, strerror(errno));
        return NULL;
    }

    char   line[MAP_MAX_DIM + 64];
    int    version = 0, w = 0, h = 0, zoom = 1;
    char   name[MAP_NAME_MAX] = "untitled";
    double scale = MAP_SCALE_DEFAULT;
    int    metric = MAP_METRIC_DEFAULT;
    char   ruleset[MAP_RULESET_MAX] = "";

    if (read_line(f, line, sizeof line) < 0 || sscanf(line, "VTT %d", &version) != 1) {
        snprintf(err, errsz, "%s is not a vtt map", path);
        fclose(f);
        return NULL;
    }
    if (version > FORMAT_VERSION) {
        snprintf(err, errsz, "map format v%d is newer than this build (v%d)",
                 version, FORMAT_VERSION);
        fclose(f);
        return NULL;
    }

    /* Header lines may appear in any order; the body sections must follow. */
    long body_start = ftell(f);
    while (read_line(f, line, sizeof line) >= 0) {
        if (!strncmp(line, "name ", 5))       str_lcpy(name, line + 5, sizeof name);
        else if (!strncmp(line, "size ", 5))  sscanf(line, "size %d %d", &w, &h);
        else if (!strncmp(line, "zoom ", 5))  sscanf(line, "zoom %d", &zoom);
        else if (!strncmp(line, "scale ", 6)) sscanf(line, "scale %lf", &scale);
        else if (!strncmp(line, "ruleset ", 8))
            str_lcpy(ruleset, line + 8, sizeof ruleset);
        else if (!strncmp(line, "metric ", 7)) {
            char mn[32] = { 0 };
            if (sscanf(line, "metric %31s", mn) == 1) {
                int got = dist_metric_from_name(mn);
                if (got >= 0) metric = got;
            }
        }
        else { fseek(f, body_start, SEEK_SET); break; }
        body_start = ftell(f);
    }

    if (w < MAP_MIN_DIM || h < MAP_MIN_DIM || w > MAP_MAX_DIM || h > MAP_MAX_DIM) {
        snprintf(err, errsz, "bad map size %dx%d", w, h);
        fclose(f);
        return NULL;
    }

    Map *m = map_new(w, h, name);
    m->zoom = iclamp(zoom, 0, 3);

    /* A nonsensical scale would make every measurement nonsense, so fall back
     * rather than trust it. */
    m->scale_ft = (scale > 0.0 && scale < 100000.0) ? scale : MAP_SCALE_DEFAULT;
    m->metric   = metric;
    if (ruleset_by_name(ruleset)) str_lcpy(m->ruleset, ruleset, sizeof m->ruleset);

    while (read_line(f, line, sizeof line) >= 0) {
        if (!strcmp(line, "tiles")) {
            for (int y = 0; y < h; y++) {
                if (read_line(f, line, sizeof line) < 0) break;
                parse_tile_row(line, m->tiles + (size_t)y * (size_t)w, w);
            }
        } else if (!strcmp(line, "vedges")) {
            for (int y = 0; y < h; y++) {
                if (read_line(f, line, sizeof line) < 0) break;
                parse_edge_row(line, m->vedges + (size_t)y * (size_t)(w + 1), w + 1);
            }
        } else if (!strcmp(line, "hedges")) {
            for (int y = 0; y <= h; y++) {
                if (read_line(f, line, sizeof line) < 0) break;
                parse_edge_row(line, m->hedges + (size_t)y * (size_t)w, w);
            }
        } else if (!strncmp(line, "token ", 6)) {
            parse_token_line(m, line);
        } else if (!strncmp(line, "tokenstatus ", 12)) {
            parse_status_line(m, line);
        }
        /* Unknown lines are ignored so a newer writer stays loadable. */
    }
    fclose(f);

    str_lcpy(m->path, path, sizeof m->path);
    m->modified = 0;
    return m;
}

/* ------------------------------------------------------------- discovery */

void mapio_default_dir(char *buf, size_t bufsz)
{
    const char *xdg = getenv("XDG_DATA_HOME");
    if (xdg && xdg[0]) {
        snprintf(buf, bufsz, "%s/vtt/maps", xdg);
        return;
    }
    const char *home = getenv("HOME");
    snprintf(buf, bufsz, "%s/.local/share/vtt/maps", home && home[0] ? home : ".");
}

void mapio_resolve_path(const char *name, char *buf, size_t bufsz)
{
    char with_ext[MAP_PATH_MAX];
    size_t n = strlen(name);
    int has_ext = n > 4 && strcmp(name + n - 4, ".vtt") == 0;
    snprintf(with_ext, sizeof with_ext, "%s%s", name, has_ext ? "" : ".vtt");

    if (strchr(with_ext, '/')) {
        str_lcpy(buf, with_ext, bufsz);
        return;
    }
    char dir[MAP_PATH_MAX];
    mapio_default_dir(dir, sizeof dir);
    snprintf(buf, bufsz, "%s/%s", dir, with_ext);
}

static int entry_cmp(const void *a, const void *b)
{
    return strcmp(((const MapEntry *)a)->name, ((const MapEntry *)b)->name);
}

static void scan_dir(const char *dir, MapEntry **list, int *n, int *cap)
{
    DIR *d = opendir(dir);
    if (!d) return;

    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        size_t len = strlen(de->d_name);
        if (len < 5 || strcmp(de->d_name + len - 4, ".vtt") != 0) continue;

        char full[MAP_PATH_MAX];
        snprintf(full, sizeof full, "%s/%s", dir, de->d_name);

        struct stat st;
        if (stat(full, &st) != 0 || !S_ISREG(st.st_mode)) continue;

        /* The same file can be reachable through both scanned directories. */
        int dup = 0;
        for (int i = 0; i < *n; i++)
            if (strcmp((*list)[i].path, full) == 0) { dup = 1; break; }
        if (dup) continue;

        if (*n == *cap) {
            *cap = *cap ? *cap * 2 : 32;
            *list = xrealloc(*list, (size_t)*cap * sizeof(MapEntry));
        }
        str_lcpy((*list)[*n].name, de->d_name, sizeof (*list)[*n].name);
        str_lcpy((*list)[*n].path, full, sizeof (*list)[*n].path);
        (*n)++;
    }
    closedir(d);
}

int mapio_scan(MapEntry **out)
{
    MapEntry *list = NULL;
    int       n = 0, cap = 0;

    char dir[MAP_PATH_MAX];
    mapio_default_dir(dir, sizeof dir);

    scan_dir(".", &list, &n, &cap);
    scan_dir(dir, &list, &n, &cap);

    if (n > 1) qsort(list, (size_t)n, sizeof(MapEntry), entry_cmp);
    *out = list;
    return n;
}
