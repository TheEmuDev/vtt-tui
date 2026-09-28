#include "mapio.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "clock.h"
#include "counter.h"
#include "floor.h"
#include "fog.h"
#include "link.h"
#include "ruler.h"
#include "scene.h"
#include "turn.h"
#include "util.h"

/* v2 added terrain kinds and door/window/secret boundaries. A v1 reader would
 * take a closed door for an opening and water for a hole, so it must refuse
 * the file rather than quietly misread a sealed room as open.
 *
 * v3 added status markers on tokens. An older reader would ignore those lines
 * and silently drop them, which loses combat state from a saved fight, so it
 * refuses too. Each version still loads everything older. */
#define FORMAT_VERSION 11

/* Version 4 added the turn order. A map with no fight in it is still written
 * as version 3, which says everything it needs and stays loadable by the
 * builds that came before; one with a fight says 4, so that an older reader
 * refuses it rather than dropping whose turn it is on the floor.
 *
 * Version 5 added clocks, named rolls and notes, on the same terms: a map
 * carrying none of them is written as whatever version it needs. The
 * writer always picks the lowest version that says everything. */
#define FORMAT_BEFORE_TURNS    3
#define FORMAT_BEFORE_CLOCKS   4
#define FORMAT_BEFORE_COUNTERS 5
/* Version 6 added counters and fog; 7 named areas; 8 links; 9 floors; 10
 * hidden creatures -- an older reader would ignore the line and show the
 * ambusher to the table, so it must refuse the file. */
#define FORMAT_BEFORE_AREAS    6
#define FORMAT_BEFORE_LINKS    7
#define FORMAT_BEFORE_FLOORS   8
#define FORMAT_BEFORE_HIDDEN   9
/* Version 11 added scenes: an older reader would read a scene's creatures
 * as the map's own. */
#define FORMAT_BEFORE_SCENES   10

/* Fog rows: a held tile of patch 1..15 is one of these, in order. */
static const char FOG_HELD_CHARS[FOG_PATCH_MAX + 1] = "123456789!\"#$%&";

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

/* Creatures, each followed by the lines that hang on it. The map's and each
 * scene's are written the same way. */
static void put_tokens(FILE *f, const TokenList *l)
{
    for (int i = 0; i < l->n; i++) {
        const Token *t = &l->v[i];
        fprintf(f, "token %s %d %d %d \"%s\"\n",
                token_kind_name(t->kind), t->x, t->y, t->size, t->label);

        /* Markers follow the token they hang on, so a token line stays short
         * and the attachment needs no index to go wrong. */
        for (int j = 0; j < t->nstatus; j++)
            fprintf(f, "tokenstatus %s \"%s\"\n",
                    status_color_name(t->status[j].color), t->status[j].label);

        if (t->note[0]) fprintf(f, "tokennote \"%s\"\n", t->note);
        if (t->hidden)  fputs("tokenhidden\n", f);
        for (int j = 0; j < t->ncounters; j++)
            fprintf(f, "tokencounter %s %d %d\n", t->counters[j].name,
                    t->counters[j].value, t->counters[j].max);

        /* Its place in the turn order, the same way: "tokenturn 15",
         * "tokenturn 15 acting", or "tokenturn - acting" for a creature
         * holding the turn from outside the order. */
        if (t->turn) {
            if (t->turn & TURN_IN) fprintf(f, "tokenturn %d", t->init);
            else                   fputs("tokenturn -", f);
            fputs((t->turn & TURN_ACTING) ? " acting\n" : "\n", f);
        }
    }
}

int mapio_write(const Map *m, const char *path, char *err, size_t errsz)
{
    char tmp[MAP_PATH_MAX + 8];
    snprintf(tmp, sizeof tmp, "%s.tmp", path);

    FILE *f = fopen(tmp, "w");
    if (!f) {
        snprintf(err, errsz, "cannot write %s: %s", tmp, strerror(errno));
        return -1;
    }

    int fight = m->round > 0 || m->spotlight != SPOTLIGHT_PLAYERS;
    for (int i = 0; i < m->tokens.n && !fight; i++) fight = m->tokens.v[i].turn != 0;
    int v5 = clock_count(m) > 0;
    for (int i = 0; i < ROLL_MAX && !v5; i++) v5 = m->rolls[i].name[0] != '\0';
    for (int i = 0; i < m->tokens.n && !v5; i++) v5 = m->tokens.v[i].note[0] != '\0';
    if (m->nnotes) v5 = 1;
    int v6 = 0, patches = 0;
    for (int i = 0; i < m->tokens.n && !v6; i++) v6 = m->tokens.v[i].ncounters > 0;
    for (int i = 0; i < FOG_PATCH_MAX; i++)
        patches += m->fog_patches[i].name[0] && !m->fog_patches[i].dead;
    if (patches) v6 = 1;
    int v7 = m->nareas > 0;
    int v8 = m->nlinks > 0;
    int v9 = 0;
    for (int i = 0; i < m->nareas; i++) v9 |= m->areas[i].floor;
    int v10 = tokens_any_hidden(&m->tokens);
    int v11 = m->nscenes > 0;
    fprintf(f, "VTT %d\n", v11 ? FORMAT_VERSION : v10 ? FORMAT_BEFORE_SCENES : v9 ? FORMAT_BEFORE_HIDDEN : v8 ? FORMAT_BEFORE_FLOORS : v7 ? FORMAT_BEFORE_LINKS : v6 ? FORMAT_BEFORE_AREAS : v5 ? FORMAT_BEFORE_COUNTERS
                          : fight ? FORMAT_BEFORE_CLOCKS : FORMAT_BEFORE_TURNS);
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

    put_tokens(f, &m->tokens);
    if (m->round > 0) fprintf(f, "round %d\n", m->round);
    if (m->spotlight == SPOTLIGHT_GM) fputs("spotlight gm\n", f);
    for (int i = 0; i < CLOCK_MAX; i++)
        if (m->clocks[i].name[0])
            fprintf(f, "clock %s %d %d%s\n", m->clocks[i].name, m->clocks[i].value,
                    m->clocks[i].size, m->clocks[i].down ? " down" : "");
    for (int i = 0; i < ROLL_MAX; i++)
        if (m->rolls[i].name[0])
            fprintf(f, "roll %s \"%s\"\n", m->rolls[i].name, m->rolls[i].expr);
    for (int i = 0; i < m->nnotes; i++)
        fprintf(f, "note %d %d \"%s\"\n", m->notes[i].x, m->notes[i].y, m->notes[i].text);
    for (int i = 0; i < m->nareas; i++)
        fprintf(f, "area %d %d %d %d \"%s\"\n", m->areas[i].x0, m->areas[i].y0,
                m->areas[i].x1, m->areas[i].y1, m->areas[i].name);
    for (int i = 0; i < m->nareas; i++)
        if (m->areas[i].floor) fprintf(f, "floor \"%s\" %d\n", m->areas[i].name, m->areas[i].level);
    for (int i = 0; i < m->nlinks; i++) {
        const Link *l = &m->links[i];
        fprintf(f, "link %d %s %d %d %d %d %d%s%s\n", l->num, link_kind_name(l->kind), l->size,
                l->x[0], l->y[0], l->x[1], l->y[1],
                l->oneway ? " oneway" : "", l->secret ? " secret" : "");
    }

    /* Scenes: a scene line, its creatures and fight as the map's are
     * written, and an end. */
    for (int i = 0; i < m->nscenes; i++) {
        const Scene *sc = &m->scenes[i];
        fprintf(f, "scene \"%s\"", sc->name);
        if (sc->boxed) fprintf(f, " %d %d %d %d", sc->x0, sc->y0, sc->x1, sc->y1);
        fputc('\n', f);
        put_tokens(f, &sc->tokens);
        if (sc->round > 0) fprintf(f, "round %d\n", sc->round);
        if (sc->spotlight == SPOTLIGHT_GM) fputs("spotlight gm\n", f);
        fputs("endscene\n", f);
    }

    /* Fog: the switches, the patches, then one row a map row, a character a
     * tile. Lit and rim are not written: they are where the party stands
     * this second, and are rebuilt from the creatures when the map opens. */
    if (patches) {
        if (m->fog_on)        fputs("fog on\n", f);
        if (m->fog_soft_edge) fputs("fog soft-edge\n", f);
        for (int i = 0; i < FOG_PATCH_MAX; i++) {
            const FogPatch *p = &m->fog_patches[i];
            if (!p->name[0] || p->dead) continue;
            fprintf(f, "fogpatch %d %s", i + 1, p->name);
            if (p->reveal == FOG_REVEAL_MANUAL) fputs(" reveal manual", f);
            else                                fprintf(f, " reveal %d", p->reveal);
            fprintf(f, " memory %s", p->memory ? "on" : "off");
            if (p->soft_edge >= 0) fprintf(f, " soft-edge %s", p->soft_edge ? "on" : "off");
            if (p->disabled) fputs(" disabled", f);
            fputc('\n', f);
        }
        fputs("fog\n", f);
        for (int y = 0; y < m->h; y++) {
            for (int x = 0; x < m->w; x++) {
                uint8_t fb = m->fog[(size_t)y * (size_t)m->w + (size_t)x];
                int     id = fb & FOG_ID;
                const FogPatch *p = id ? &m->fog_patches[id - 1] : NULL;
                char c = '.';
                if (p && p->name[0] && !p->dead) {
                    if (fb & FOG_HELD)      c = FOG_HELD_CHARS[id - 1];
                    else if (fb & FOG_SEEN) c = (char)('a' + id - 1);
                    else                    c = (char)('A' + id - 1);
                }
                fputc(c, f);
            }
            fputc('\n', f);
        }
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
    return 0;
}

int mapio_save(Map *m, const char *path, char *err, size_t errsz)
{
    if (mapio_write(m, path, err, errsz) != 0) return -1;
    str_lcpy(m->path, path, sizeof m->path);
    m->modified = 0;
    return 0;
}

void mapio_autosave_path(const Map *m, char *buf, size_t bufsz)
{
    char base[MAP_PATH_MAX];
    if (m->path[0]) str_lcpy(base, m->path, sizeof base);
    else            mapio_resolve_path(m->name[0] ? m->name : "untitled", base, sizeof base);
    snprintf(buf, bufsz, "%s.autosave", base);
}

int mapio_autosave_newer(const char *path, const char *autosave, long *when)
{
    struct stat sa, sp;
    if (stat(autosave, &sa) != 0 || !S_ISREG(sa.st_mode)) return 0;
    if (when) *when = (long)sa.st_mtime;
    if (stat(path, &sp) != 0) return 1;
    /* To the nanosecond: a save and its autosave can fall in one second. */
    return sa.st_mtim.tv_sec > sp.st_mtim.tv_sec ||
           (sa.st_mtim.tv_sec == sp.st_mtim.tv_sec && sa.st_mtim.tv_nsec > sp.st_mtim.tv_nsec);
}

/* ------------------------------------------------------------------ load */

/* A file being read, and who to tell about what it forgives. The line
 * number is the one just read. */
typedef struct {
    FILE     *f;
    int       line;
    MapioDiag sink;
    void     *ctx;
} Loader;

static void diag(Loader *ld, int line, int col, const char *code, const char *slug,
                 const char *fmt, ...) __attribute__((format(printf, 6, 7)));

static void diag(Loader *ld, int line, int col, const char *code, const char *slug,
                 const char *fmt, ...)
{
    if (!ld->sink) return;
    char msg[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    ld->sink(ld->ctx, line, col, code, slug, msg);
}

/* Reads one line without its newline. Returns -1 at EOF.
 * One line per call, however long it is: what does not fit the buffer is
 * discarded rather than handed back as a line of its own, so an oversized
 * record cannot desynchronize the ones after it. */
static int read_line(Loader *ld, char *buf, size_t bufsz)
{
    FILE *f = ld->f;
    if (!fgets(buf, (int)bufsz, f)) return -1;
    ld->line++;
    size_t n = strlen(buf);
    if (n && buf[n - 1] != '\n') {
        int c;
        while ((c = fgetc(f)) != EOF && c != '\n') { }
    }
    while (n && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) buf[--n] = '\0';
    return (int)n;
}

/* Rows are read by index and short lines are treated as trailing blanks, so
 * an editor that strips trailing whitespace cannot corrupt a map. An
 * unrecognized character reads as empty rather than aborting the load: a map
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

/* The words a record line starts with. A section's row that begins with
 * one is a record the section swallowed because it was short. */
static int looks_like_record(const char *line)
{
    static const char *const words[] = {
        "tiles", "vedges", "hedges", "fog", "fogpatch", "token", "tokenstatus",
        "tokenturn", "tokencounter", "tokennote", "tokenhidden", "note", "area", "floor", "link", "spotlight", "clock",
        "roll", "round", "name", "size", "zoom", "scale", "ruleset", "metric", NULL,
    };
    size_t n = 0;
    while (line[n] >= 'a' && line[n] <= 'z') n++;
    if (!n || (line[n] && line[n] != ' ')) return 0;
    for (int i = 0; words[i]; i++)
        if (strlen(words[i]) == n && !strncmp(words[i], line, n)) return 1;
    return 0;
}

/* Is every character one a row of some section could hold? For an unknown
 * line that is really a row one too many. */
static int looks_like_row(const char *line)
{
    if (!*line) return 0;
    for (const char *p = line; *p; p++)
        if (tile_from_file_char(*p) < 0 && edge_from_file_char(*p) < 0 && *p != '-') return 0;
    return 1;
}

/* One section of rows, read by index as it always is, with what it
 * forgives told: rows too long, characters it does not know, a record it
 * swallowed, the file ending inside it. Short rows are the format's own
 * leniency, counted once for the section. `kind` 0 tiles, 1 edges, 2 fog. */
typedef struct { const char *name; int rows, width, kind; int shorts, first_short; int rows_w; } Section;

static int section_row(Loader *ld, Section *sec, int y, char *line, size_t cap)
{
    if (read_line(ld, line, cap) < 0) {
        diag(ld, ld->line, -1, "E011", "section-short",
             "the file ends inside '%s' after %d of %d rows", sec->name, y, sec->rows);
        return -1;
    }
    if (!ld->sink) return 0;
    /* A fog row's seen patches are lowercase letters, so a real one can
     * spell "fog" or "name"; only a line a fog row could not be is a
     * swallowed record there. */
    int fog_row = sec->kind == 2 && (int)strlen(line) <= sec->width;
    for (const char *p = line; fog_row && *p; p++)
        fog_row = *p == '.' || *p == ' ' || (*p >= 'A' && *p <= 'O') || (*p >= 'a' && *p <= 'o') ||
                  (*p && strchr("123456789!\"#$%&", *p));
    if (!fog_row && looks_like_record(line)) {
        diag(ld, ld->line, -1, "E011", "section-short",
             "'%.24s' read as %s row %d of %d: the section is short", line, sec->name, y + 1, sec->rows);
        return 0;
    }
    int len = (int)strlen(line);
    /* The easy mistake: a vedges row written w long, as if it were a row
     * of squares -- its last boundary is the east edge's neighbor, and
     * the east edge itself is missing. */
    if (sec->kind == 1 && sec->width == sec->rows_w + 1 && len == sec->rows_w && line[len - 1] != ' ')
        diag(ld, ld->line, len, "W022", "edge-row-short",
             "vedges row %d is %d characters: a vedges row is %d, one more than the map is wide, "
             "and the east boundary is the last; is it missing?", y + 1, len, sec->width);
    if (len > sec->width)
        diag(ld, ld->line, sec->width, "E010", "row-long",
             "%s row %d is %d characters, the map needs %d; the rest is ignored",
             sec->name, y + 1, len, sec->width);
    else if (len < sec->width && !sec->shorts++)
        sec->first_short = ld->line;
    for (int x = 0; x < len && x < sec->width; x++) {
        char c = line[x];
        int  ok = sec->kind == 0 ? tile_from_file_char(c) >= 0
                : sec->kind == 1 ? edge_from_file_char(c) >= 0 || c == '-'
                : 1;
        if (!ok) {
            diag(ld, ld->line, x, "E013", "bad-char", "'%c' in %s row %d, column %d, reads as %s",
                 c, sec->name, y + 1, x + 1, sec->kind == 0 ? "void" : "no boundary");
            break;
        }
    }
    return 0;
}

static void section_done(Loader *ld, const Section *sec)
{
    if (sec->shorts)
        diag(ld, sec->first_short, -1, "N021", "row-short",
             "%d %s row%s shorter than %d; the rest reads as %s", sec->shorts, sec->name,
             sec->shorts == 1 ? " is" : "s are", sec->width, sec->kind == 0 ? "void" : "empty");
}

/* Copies the text between the first quote and the last, so a label may hold
 * spaces. Returns 0 when there is no quoted section. */
static int parse_quoted(const char *from, char *out, size_t outsz)
{
    out[0] = '\0';
    if (!from || *from != '"') return 0;

    from++;
    /* The last quote closes it; "" is empty, not a lone quote. */
    const char *end = strrchr(from, '"');
    size_t      len = end ? (size_t)(end - from) : strlen(from);
    if (len >= outsz) len = outsz - 1;
    memcpy(out, from, len);
    out[len] = '\0';
    return 1;
}

/* A marker hangs on whichever token was read last. */
static int parse_status_line(Map *m, const char *line)
{
    if (m->tokens.n == 0) return -1;

    char color[16] = { 0 };
    int  consumed = 0;
    if (sscanf(line, "tokenstatus %15s %n", color, &consumed) < 1) return -1;

    int c = status_color_from_name(color);
    if (c < 0) return -1;

    char label[STATUS_LABEL_MAX];
    parse_quoted(consumed > 0 ? line + consumed : NULL, label, sizeof label);

    token_add_status(&m->tokens.v[m->tokens.n - 1], (uint8_t)c, label);
    return 0;
}

/* "tokencounter HP 4 6": a counter on the token above it. */
/* "fogpatch 2 Crypt reveal 1 memory off soft-edge on disabled". */
static int parse_fogpatch_line(Map *m, const char *line)
{
    int  id = 0, consumed = 0;
    char name[FOG_NAME_MAX + 1] = { 0 };
    if (sscanf(line, "fogpatch %d %16s %n", &id, name, &consumed) < 2) return -1;
    if (id < 1 || id > FOG_PATCH_MAX || strlen(name) >= FOG_NAME_MAX || !isalpha((unsigned char)name[0]))
        return -1;
    FogPatch *p = &m->fog_patches[id - 1];
    memset(p, 0, sizeof *p);
    str_lcpy(p->name, name, sizeof p->name);
    p->reveal = 2; p->memory = 1; p->soft_edge = -1; p->x1 = -1;

    const char *s = consumed > 0 ? line + consumed : "";
    char key[16], val[16];
    int  n;
    while (*s) {
        while (*s == ' ') s++;
        if (!*s) break;
        if (sscanf(s, "%15s %n", key, &n) < 1) break;
        s += n;
        if (!strcmp(key, "disabled")) { p->disabled = 1; continue; }
        if (sscanf(s, "%15s %n", val, &n) < 1) break;
        s += n;
        if (!strcmp(key, "reveal")) {
            if (!strcmp(val, "manual")) p->reveal = FOG_REVEAL_MANUAL;
            else p->reveal = (int8_t)iclamp(atoi(val), 0, 99);
        } else if (!strcmp(key, "memory"))    p->memory    = !strcmp(val, "on");
        else if (!strcmp(key, "soft-edge"))   p->soft_edge = (int8_t)!strcmp(val, "on");
    }
    return 0;
}

/* One row of the fog section. An unknown character is no fog, the same
 * forgiveness the tile rows get. */
static void parse_fog_row(Map *m, int y, const char *line)
{
    size_t len = strlen(line);
    for (int x = 0; x < m->w && (size_t)x < len; x++) {
        char c = line[x];
        uint8_t f = 0;
        if (c >= 'A' && c < 'A' + FOG_PATCH_MAX)      f = (uint8_t)(c - 'A' + 1);
        else if (c >= 'a' && c < 'a' + FOG_PATCH_MAX) f = (uint8_t)((unsigned)(c - 'a' + 1) | FOG_SEEN);
        else {
            const char *h = strchr(FOG_HELD_CHARS, c);
            if (c && h) f = (uint8_t)((h - FOG_HELD_CHARS + 1) | FOG_HELD | FOG_SEEN);
        }
        if (f) map_fog_set(m, x, y, f);
    }
}

static int parse_counter_line(Map *m, const char *line)
{
    if (m->tokens.n == 0) return -1;
    /* Scanned one wider than a name may be, so an overlong one is refused
     * rather than cut short with its tail read as the value. */
    char name[COUNTER_NAME_MAX + 1] = { 0 };
    int  value = 0, max = 0;
    if (sscanf(line, "tokencounter %8s %d %d", name, &value, &max) != 3) return -1;
    if (strlen(name) >= COUNTER_NAME_MAX) return -1;
    return counter_set(&m->tokens.v[m->tokens.n - 1], name, value, max) >= 0 ? 0 : -1;
}

static int parse_token_note_line(Map *m, const char *line)
{
    if (m->tokens.n == 0 || strlen(line) < 10) return -1;
    Token *t = &m->tokens.v[m->tokens.n - 1];
    parse_quoted(line + 10, t->note, sizeof t->note);
    return 0;
}

static int parse_note_line(Map *m, const char *line)
{
    int x, y, consumed = 0;
    if (sscanf(line, "note %d %d %n", &x, &y, &consumed) < 2) return -1;
    char text[NOTE_MAX];
    parse_quoted(consumed > 0 ? line + consumed : NULL, text, sizeof text);
    return map_in_bounds(m, x, y) && map_note_set(m, x, y, text) ? 0 : -1;
}

/* "area X0 Y0 X1 Y1 "Name"": refused for a bad name, a box off the map, a
 * name already used, or a full list. */
static int parse_area_line(Map *m, const char *line)
{
    int x0, y0, x1, y1, consumed = 0;
    if (sscanf(line, "area %d %d %d %d %n", &x0, &y0, &x1, &y1, &consumed) < 4) return -1;
    char name[AREA_NAME_MAX];
    parse_quoted(consumed > 0 ? line + consumed : NULL, name, sizeof name);
    if (map_area_find(m, name) >= 0) return -1;
    return map_area_set(m, name, x0, y0, x1, y1) >= 0 ? 0 : -1;
}

/* "link N KIND SIZE X0 Y0 X1 Y1 [oneway] [secret]": refused for a word it
 * does not know or a number already used. Where the ends are is checked
 * once the whole file is in (link_misplaced wants the size), see the end of
 * mapio_load_diag. */
static int parse_link_line(Map *m, const char *line)
{
    int num, size, x0, y0, x1, y1, consumed = 0;
    char kind[16];
    if (sscanf(line, "link %d %15s %d %d %d %d %d%n", &num, kind, &size, &x0, &y0, &x1, &y1, &consumed) < 7)
        return -1;
    int k = link_kind_from_name(kind);
    if (k < 0 || num < 1 || num > LINK_NUM_MAX || size < 1 || size > LINK_SIZE_MAX) return -1;
    if (x0 < 0 || y0 < 0 || x1 < 0 || y1 < 0 || x0 > INT16_MAX || y0 > INT16_MAX ||
        x1 > INT16_MAX || y1 > INT16_MAX)
        return -1;
    if (link_find(m, num) >= 0) return -1;
    Link l;
    memset(&l, 0, sizeof l);
    l.num = (uint8_t)num; l.kind = (uint8_t)k; l.size = (uint8_t)size;
    l.x[0] = (int16_t)x0; l.y[0] = (int16_t)y0; l.x[1] = (int16_t)x1; l.y[1] = (int16_t)y1;
    for (const char *p = line + consumed; *p; ) {
        while (*p == ' ') p++;
        size_t n = strcspn(p, " ");
        if (!n) break;
        if      (n == 6 && !strncmp(p, "oneway", 6)) l.oneway = 1;
        else if (n == 6 && !strncmp(p, "secret", 6)) l.secret = 1;
        else return -1;
        p += n;
    }
    return link_put(m, &l) >= 0 ? 0 : -1;
}

static int parse_turn_line(Map *m, const char *line)
{
    if (m->tokens.n == 0) return -1;

    char init[16] = { 0 }, flag[16] = { 0 };
    if (sscanf(line, "tokenturn %15s %15s", init, flag) < 1) return -1;

    Token *t = &m->tokens.v[m->tokens.n - 1];
    if (strcmp(init, "-") != 0) {
        char *end;
        long  v = strtol(init, &end, 10);
        if (end == init || *end) return -1;
        t->init  = (int16_t)(v < -999 ? -999 : v > 999 ? 999 : v);
        t->turn |= TURN_IN;
    }
    if (!strcmp(flag, "acting")) t->turn |= TURN_ACTING;
    return 0;
}

/* "clock Dragon 3 6 down": the name, filled and total segments, and the
 * direction when it counts down. Slots are taken in file order, so a saved
 * map reads back in the order it was written. */
static int parse_clock_line(Map *m, const char *line)
{
    char name[CLOCK_NAME_MAX] = { 0 }, dir[8] = { 0 };
    int  value = 0, size = 0;
    if (sscanf(line, "clock %19s %d %d %7s", name, &value, &size, dir) < 3) return -1;
    int idx = clock_start(m, name, size, !strcmp(dir, "down"));
    if (idx < 0) return -1;
    m->clocks[idx].value = (uint8_t)iclamp(value, 0, m->clocks[idx].size);
    return 0;
}

/* "roll attack "2d12+3"": a named roll. The expression is not checked
 * here; :roll says what is wrong with it when it is used. */
static int parse_roll_line(Map *m, const char *line)
{
    char name[ROLL_NAME_MAX] = { 0 };
    int  consumed = 0;
    if (sscanf(line, "roll %15s %n", name, &consumed) < 1) return -1;
    int idx = -1;
    for (int i = 0; i < ROLL_MAX && idx < 0; i++)
        if (!m->rolls[i].name[0]) idx = i;
    if (idx < 0) return -1;
    str_lcpy(m->rolls[idx].name, name, sizeof m->rolls[idx].name);
    parse_quoted(consumed > 0 ? line + consumed : NULL, m->rolls[idx].expr, sizeof m->rolls[idx].expr);
    if (!m->rolls[idx].expr[0]) m->rolls[idx].name[0] = '\0';
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
    return mapio_load_diag(path, err, errsz, NULL, NULL);
}

Map *mapio_load_diag(const char *path, char *err, size_t errsz, MapioDiag sink, void *ctx)
{
    FILE *f = fopen(path, "r");
    if (!f) {
        snprintf(err, errsz, "cannot open %s: %s", path, strerror(errno));
        return NULL;
    }
    Loader  L  = { f, 0, sink, ctx };
    Loader *ld = &L;

    char   line[MAP_MAX_DIM + 64];
    int    version = 0, w = 0, h = 0, zoom = 1;
    char   name[MAP_NAME_MAX] = "untitled";
    double scale = MAP_SCALE_DEFAULT;
    int    metric = MAP_METRIC_DEFAULT;
    char   ruleset[MAP_RULESET_MAX] = "";

    if (read_line(ld, line, sizeof line) < 0 || sscanf(line, "VTT %d", &version) != 1) {
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
    int  body_line  = ld->line;
    int  zoom_line = 0, scale_line = 0, ruleset_line = 0;
    while (read_line(ld, line, sizeof line) >= 0) {
        if (!strncmp(line, "name ", 5))       str_lcpy(name, line + 5, sizeof name);
        else if (!strncmp(line, "size ", 5))  sscanf(line, "size %d %d", &w, &h);
        else if (!strncmp(line, "zoom ", 5))  { sscanf(line, "zoom %d", &zoom); zoom_line = ld->line; }
        else if (!strncmp(line, "scale ", 6)) { sscanf(line, "scale %lf", &scale); scale_line = ld->line; }
        else if (!strncmp(line, "ruleset ", 8)) {
            str_lcpy(ruleset, line + 8, sizeof ruleset);
            ruleset_line = ld->line;
        }
        else if (!strncmp(line, "metric ", 7)) {
            char mn[32] = { 0 };
            int  got = sscanf(line, "metric %31s", mn) == 1 ? dist_metric_from_name(mn) : -1;
            if (got >= 0) metric = got;
            else diag(ld, ld->line, -1, "W017", "unknown-metric",
                      "'%.40s': chebyshev, euclidean, alt or manhattan; chebyshev is used", line + 7);
        }
        else { fseek(f, body_start, SEEK_SET); ld->line = body_line; break; }
        body_start = ftell(f);
        body_line  = ld->line;
    }

    if (w == 0 && h == 0) {
        snprintf(err, errsz, "no size line before the sections (header lines -- name, size, "
                             "scale... -- come first)");
        fclose(f);
        return NULL;
    }
    if (w < MAP_MIN_DIM || h < MAP_MIN_DIM || w > MAP_MAX_DIM || h > MAP_MAX_DIM) {
        snprintf(err, errsz, "bad map size %dx%d", w, h);
        fclose(f);
        return NULL;
    }

    Map *m = map_new(w, h, name);
    m->zoom = iclamp(zoom, 0, 3);
    if (m->zoom != zoom) diag(ld, zoom_line, -1, "W020", "clamped", "zoom %d is not 0-3; %d is used", zoom, m->zoom);

    /* A nonsensical scale would make every measurement nonsense, so fall back
     * rather than trust it. */
    m->scale_ft = (scale > 0.0 && scale < 100000.0) ? scale : MAP_SCALE_DEFAULT;
    if (m->scale_ft != scale)
        diag(ld, scale_line, -1, "W020", "clamped", "scale %g is not a length; %g ft is used", scale, m->scale_ft);
    m->metric   = metric;
    if (ruleset_by_name(ruleset)) str_lcpy(m->ruleset, ruleset, sizeof m->ruleset);
    else if (ruleset[0])
        diag(ld, ruleset_line, -1, "W016", "unknown-ruleset", "no ruleset called '%.40s'; the map has none", ruleset);
    int fog_line = 0;

    /* A record that did not parse is dropped, as ever; told, it names the
     * line. */
#define RECORD(call, what) do { if ((call) < 0) \
        diag(ld, ld->line, -1, "E014", "bad-record", "%s dropped: '%.60s'", (what), line); } while (0)

    int stray_at = -1;
    int link_line[LINK_NUM_MAX + 1] = { 0 };   /* where each link was read, for its finding */
    struct { char name[AREA_NAME_MAX]; int level, line; } floors[MAP_AREAS_MAX];
    int last_token_read = 0;       /* the last token line made a creature */
    /* Inside a scene block its creature lines go to the scene: its list is
     * swapped in for the map's, so every creature parser serves both, and the
     * map's is held here until endscene. */
    int       in_scene = 0, scene_keep = 0, scene_line = 0, scene_round = 0, scene_spot = SPOTLIGHT_PLAYERS;
    Scene     scene_new;
    TokenList map_tokens;
    memset(&scene_new, 0, sizeof scene_new);
    memset(&map_tokens, 0, sizeof map_tokens);
    int nfloors = 0;
    while (read_line(ld, line, sizeof line) >= 0) {
        if (!strncmp(line, "scene ", 6) || !strcmp(line, "endscene")) {
            if (in_scene) {
                /* The block closes: kept, or thrown away. */
                if (!strcmp(line, "endscene") && scene_keep) {
                    scene_new.tokens    = m->tokens;
                    scene_new.round     = scene_round;
                    scene_new.spotlight = scene_spot;
                    m->scenes[m->nscenes++] = scene_new;
                } else {
                    tokens_free(&m->tokens);
                    if (strcmp(line, "endscene") != 0)
                        diag(ld, scene_line, -1, "W025", "scene-dropped",
                             "scene %.31s dropped: another began before its endscene", scene_new.name);
                }
                m->tokens = map_tokens;
                in_scene  = 0;
            } else if (!strcmp(line, "endscene")) {
                diag(ld, ld->line, -1, "W025", "scene-dropped", "endscene with no scene open, ignored");
            }
            if (!strcmp(line, "endscene")) continue;

            /* scene "Name" [x0 y0 x1 y1] */
            memset(&scene_new, 0, sizeof scene_new);
            const char *q = parse_quoted(line + 6, scene_new.name, sizeof scene_new.name) ? strrchr(line + 7, '"') : NULL;
            int b[4], nb = q ? sscanf(q + 1, "%d %d %d %d", &b[0], &b[1], &b[2], &b[3]) : 0;
            const char *why = NULL;
            if (!q || q <= line + 6 || !scene_name_ok(scene_new.name)) why = "its name does not read";
            else if (nb != 0 && nb != 4 && nb != EOF) why = "its box does not read";
            else if (nb == 4 && (!map_in_bounds(m, b[0], b[1]) || !map_in_bounds(m, b[2], b[3]) ||
                                 b[2] < b[0] || b[3] < b[1])) why = "its box is not on the map";
            else if (scene_find(m, scene_new.name) >= 0) why = "a scene of that name came before it";
            else if (m->nscenes >= MAP_SCENES_MAX) why = "a map holds 16 scenes";
            if (why) diag(ld, ld->line, -1, "W025", "scene-dropped", "scene %.31s dropped: %s",
                          scene_new.name[0] ? scene_new.name : "?", why);
            if (nb == 4) {
                scene_new.boxed = 1;
                scene_new.x0 = (int16_t)b[0]; scene_new.y0 = (int16_t)b[1];
                scene_new.x1 = (int16_t)b[2]; scene_new.y1 = (int16_t)b[3];
            }
            in_scene    = 1;
            scene_keep  = !why;
            scene_line  = ld->line;
            scene_round = 0;
            scene_spot  = SPOTLIGHT_PLAYERS;
            map_tokens  = m->tokens;
            memset(&m->tokens, 0, sizeof m->tokens);
            last_token_read = 0;
        } else if (in_scene && !strcmp(line, "spotlight gm")) {
            scene_spot = SPOTLIGHT_GM;
        } else if (in_scene && !strncmp(line, "round ", 6)) {
            int round = 0;
            if (sscanf(line, "round %d", &round) == 1) scene_round = iclamp(round, 0, INT16_MAX);
        } else if (!strcmp(line, "fog")) {
            Section sec = { "fog", h, w, 2, 0, 0, w };
            fog_line = ld->line;
            for (int y = 0; y < h; y++) {
                if (section_row(ld, &sec, y, line, sizeof line) < 0) break;
                parse_fog_row(m, y, line);
            }
            section_done(ld, &sec);
        } else if (!strcmp(line, "fog on")) {
            m->fog_on = 1;
        } else if (!strcmp(line, "fog soft-edge")) {
            m->fog_soft_edge = 1;
        } else if (!strncmp(line, "fogpatch ", 9)) {
            RECORD(parse_fogpatch_line(m, line), "fog patch");
        } else if (!strcmp(line, "tiles")) {
            Section sec = { "tiles", h, w, 0, 0, 0, w };
            for (int y = 0; y < h; y++) {
                if (section_row(ld, &sec, y, line, sizeof line) < 0) break;
                parse_tile_row(line, m->tiles + (size_t)y * (size_t)w, w);
            }
            section_done(ld, &sec);
        } else if (!strcmp(line, "vedges")) {
            Section sec = { "vedges", h, w + 1, 1, 0, 0, w };
            for (int y = 0; y < h; y++) {
                if (section_row(ld, &sec, y, line, sizeof line) < 0) break;
                parse_edge_row(line, m->vedges + (size_t)y * (size_t)(w + 1), w + 1);
            }
            section_done(ld, &sec);
        } else if (!strcmp(line, "hedges")) {
            Section sec = { "hedges", h + 1, w, 1, 0, 0, w };
            for (int y = 0; y <= h; y++) {
                if (section_row(ld, &sec, y, line, sizeof line) < 0) break;
                parse_edge_row(line, m->hedges + (size_t)y * (size_t)w, w);
            }
            section_done(ld, &sec);
        } else if (!strncmp(line, "token ", 6)) {
            int before = m->tokens.n;
            RECORD(parse_token_line(m, line), "token");
            last_token_read = m->tokens.n > before;
            if (m->tokens.n > before) {
                int size = 0;
                if (sscanf(line, "token %*s %*d %*d %d", &size) == 1 && size != m->tokens.v[before].size)
                    diag(ld, ld->line, -1, "W020", "clamped", "token size %d is not 1-%d; %d is used",
                         size, TOKEN_SIZE_MAX, m->tokens.v[before].size);
            }
        } else if (!strncmp(line, "tokenstatus ", 12)) {
            RECORD(parse_status_line(m, line), "marker");
        } else if (!strncmp(line, "tokenturn ", 10)) {
            RECORD(parse_turn_line(m, line), "turn");
        } else if (!strncmp(line, "tokencounter ", 13)) {
            RECORD(parse_counter_line(m, line), "counter");
        } else if (!strcmp(line, "tokenhidden")) {
            /* On the creature read last -- and only if its line was read: after a
             * dropped one it would hide the creature before, which the table
             * would then not see. */
            if (m->tokens.n && last_token_read) m->tokens.v[m->tokens.n - 1].hidden = 1;
            else diag(ld, ld->line, -1, "E014", "bad-record", "hidden marker dropped: its creature's line was not read");
        } else if (!strncmp(line, "tokennote ", 10)) {
            RECORD(parse_token_note_line(m, line), "creature note");
        } else if (!strncmp(line, "note ", 5)) {
            RECORD(parse_note_line(m, line), "note");
        } else if (!strncmp(line, "area ", 5)) {
            RECORD(parse_area_line(m, line), "area");
        } else if (!strncmp(line, "floor ", 6)) {
            /* Held until every area is in: a floor names one. */
            char fname[AREA_NAME_MAX];
            const char *q = parse_quoted(line + 6, fname, sizeof fname) ? strrchr(line + 7, '"') : NULL;
            int level = 0;
            if (!q || q <= line + 6 || nfloors >= MAP_AREAS_MAX || sscanf(q + 1, "%d", &level) != 1 ||
                level < FLOOR_LEVEL_MIN || level > FLOOR_LEVEL_MAX) {
                diag(ld, ld->line, -1, "E014", "bad-record", "floor dropped: '%.60s'", line);
            } else {
                str_lcpy(floors[nfloors].name, fname, sizeof floors[nfloors].name);
                floors[nfloors].level = level;
                floors[nfloors].line  = ld->line;
                nfloors++;
            }
        } else if (!strncmp(line, "link ", 5)) {
            RECORD(parse_link_line(m, line), "link");
            int num = 0;
            if (sscanf(line, "link %d", &num) == 1 && num >= 1 && num <= LINK_NUM_MAX && !link_line[num])
                link_line[num] = ld->line;
        } else if (!strcmp(line, "spotlight gm")) {
            m->spotlight = SPOTLIGHT_GM;
        } else if (!strncmp(line, "clock ", 6)) {
            RECORD(parse_clock_line(m, line), "clock");
        } else if (!strncmp(line, "roll ", 5)) {
            RECORD(parse_roll_line(m, line), "named roll");
        } else if (!strncmp(line, "round ", 6)) {
            int round = 0;
            if (sscanf(line, "round %d", &round) == 1) {
                m->round = iclamp(round, 0, INT16_MAX);
                if (m->round != round)
                    diag(ld, ld->line, -1, "W020", "clamped", "round %d is out of range; %d is used", round, m->round);
            }
        } else if (ld->sink && !line[strspn(line, " ")]) {
            /* Only spaces: a blank line, whatever an editor left in it --
             * though inside a run of stranded rows it is one of them. */
            if (stray_at == ld->line - 1) stray_at = ld->line;
        } else if (ld->sink && looks_like_row(line)) {
            /* The first of a run: a swallowed header strands every row of
             * the section it began, and one finding says so. */
            if (stray_at != ld->line - 1)
                diag(ld, ld->line, -1, "W019", "stray-row",
                     "rows outside any section, ignored: is the section before them one row too long, "
                     "or its header swallowed by a short one?");
            stray_at = ld->line;
        } else if (ld->sink && line[0]) {
            /* Unknown lines are ignored so a newer writer stays loadable. */
            static const char *const header[] = { "name ", "size ", "zoom ", "scale ", "ruleset ", "metric ", NULL };
            int is_header = 0;
            for (int k = 0; header[k] && !is_header; k++) is_header = !strncmp(line, header[k], strlen(header[k]));
            if (is_header)
                diag(ld, ld->line, -1, "W015", "unknown-line",
                     "ignored: '%.40s' is a header line, and header lines come before the sections", line);
            else
                diag(ld, ld->line, -1, "W015", "unknown-line", "ignored: '%.60s'", line);
        }
    }
#undef RECORD
    fclose(f);
    if (in_scene) {
        tokens_free(&m->tokens);
        m->tokens = map_tokens;
        diag(ld, scene_line, -1, "W025", "scene-dropped", "scene %.31s dropped: the file ends before its endscene",
             scene_new.name);
    }
    turn_sanitize(m);
    /* Each scene's order the same way, its list swapped in for the call. */
    for (int i = 0; i < m->nscenes; i++) {
        TokenList keep = m->tokens;
        int round = m->round;
        m->tokens = m->scenes[i].tokens;
        m->round  = m->scenes[i].round;
        turn_sanitize(m);
        m->scenes[i].tokens = m->tokens;
        m->scenes[i].round  = m->round;
        m->tokens = keep;
        m->round  = round;
    }
    /* A fog row that names a patch no fogpatch line created is no fog; and
     * the extents are rebuilt from the rows, whatever order the lines came
     * in -- a fogpatch line after the section, or twice, would otherwise
     * leave painted ground under an empty extent, and hide nothing. */
    for (int i = 0; i < FOG_PATCH_MAX; i++) { m->fog_patches[i].x0 = 0; m->fog_patches[i].x1 = -1; }
    unsigned told = 0;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            uint8_t fb = m->fog[(size_t)y * (size_t)w + (size_t)x];
            int id = fb & FOG_ID;
            if (!id) continue;
            if (!m->fog_patches[id - 1].name[0]) {
                m->fog[(size_t)y * (size_t)w + (size_t)x] = 0;
                if (!(told & (1u << id))) {
                    told |= 1u << id;
                    diag(ld, fog_line ? fog_line + 1 + y : 0, x, "W018", "fog-unknown-patch",
                         "fog row %d names patch %d ('%c'), which no fogpatch line creates; read as no fog",
                         y + 1, id, 'A' + id - 1);
                }
            }
            else map_fog_set(m, x, y, fb);
        }

    /* Floors, now that every area is in. One naming no area, or overlapping
     * a floor already marked, goes with a finding. */
    for (int i = 0; i < nfloors; i++) {
        int ai = map_area_find(m, floors[i].name);
        const char *why = ai < 0 ? "no area has that name" : m->areas[ai].floor ? "it is already a floor" : NULL;
        if (!why) {
            m->areas[ai].floor = 1;
            m->areas[ai].level = (int8_t)floors[i].level;
            why = floor_problem(m, ai);
            if (why) m->areas[ai].floor = 0, m->areas[ai].level = 0;
        }
        if (why) diag(ld, floors[i].line, -1, "W024", "floor-dropped", "floor %.31s dropped: %s", floors[i].name, why);
    }

    /* Links are checked now that the size is certain. One that cannot stand
     * -- an end off the map, its ends overlapping, two links on one square --
     * goes, the later-numbered of a clashing pair first. One over void stays:
     * --check reports it (W150), and a trip onto void is refused anyway. */
    for (int i = m->nlinks - 1; i >= 0; i--) {
        const char *why = link_misplaced(m, &m->links[i]);
        if (!why) continue;
        diag(ld, link_line[m->links[i].num], -1, "W023", "link-dropped", "link %d dropped: %s",
             m->links[i].num, why);
        link_remove(m, m->links[i].num);
    }

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
