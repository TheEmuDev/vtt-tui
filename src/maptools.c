#include "maptools.h"

#include <stdlib.h>
#include <string.h>

#include "fog.h"
#include "ruler.h"
#include "util.h"

/* ---------------------------------------------------------------- naming */

/* A column's letters alone: "A", "AB". */
static void col_name(int x, char *buf, size_t sz)
{
    char full[MAP_COORD_MAX];
    map_coord_name(x, 0, full, sizeof full);
    size_t n = 0;
    while (full[n] >= 'A' && full[n] <= 'Z') n++;
    if (n >= sz) n = sz - 1;
    memcpy(buf, full, n);
    buf[n] = '\0';
}

static int digits(int v)
{
    int d = 1;
    while (v >= 10) { v /= 10; d++; }
    return d;
}

/* The glyph a creature's squares show, in file order: 1-9, a-z, A-Z, then
 * '#' for the rest, which the legend still lists. */
static char token_glyph(int i)
{
    static const char g[] = "123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
    return i < (int)sizeof g - 1 ? g[i] : '#';
}

/* A square's fog as the file writes it. */
static char fog_char(const Map *m, int x, int y)
{
    static const char held[FOG_PATCH_MAX + 1] = "123456789!\"#$%&";
    uint8_t f  = fog_at(m, x, y);
    int     id = f & FOG_ID;
    if (!id || !m->fog_patches[id - 1].name[0] || m->fog_patches[id - 1].dead) return '.';
    if (f & FOG_HELD) return held[id - 1];
    return (char)((f & FOG_SEEN ? 'a' : 'A') + id - 1);
}

/* ------------------------------------------------------------------ dump */

/* The boundary characters, the file's own, with a horizontal wall as '-'. */
static char vedge_char(const Map *m, int x, int y)
{
    uint8_t k = map_vedge(m, x, y);
    return k == EDGE_NONE ? ' ' : edge_file_char(k);
}

static char hedge_char(const Map *m, int x, int y)
{
    uint8_t k = map_hedge(m, x, y);
    return k == EDGE_NONE ? ' ' : k == EDGE_WALL ? '-' : edge_file_char(k);
}

/* A lattice corner takes the line running through it, so walls read as
 * lines: '-' where a horizontal boundary meets it, else '|' where a
 * vertical one does. Never '+', which is a door. */
static char corner_char(const Map *m, int cx, int cy)
{
    int h = (cx > 0 && map_hedge(m, cx - 1, cy) != EDGE_NONE) ||
            (cx < m->w && map_hedge(m, cx, cy) != EDGE_NONE);
    if (h) return '-';
    int v = (cy > 0 && map_vedge(m, cx, cy - 1) != EDGE_NONE) ||
            (cy < m->h && map_vedge(m, cx, cy) != EDGE_NONE);
    return v ? '|' : ' ';
}

static void header_rows(FILE *out, int gutter, int x0, int x1, int prefix_pitch)
{
    /* Two rows when any column in view has two letters: the first letter
     * above, the last below, so every column is labelled at its own pitch. */
    char name[8];
    col_name(x1, name, sizeof name);
    int two = strlen(name) > 1;
    for (int row = two ? 0 : 1; row < 2; row++) {
        fprintf(out, "%*s", gutter, "");
        if (prefix_pitch == 2) fputc(' ', out);
        for (int x = x0; x <= x1; x++) {
            col_name(x, name, sizeof name);
            size_t n = strlen(name);
            char   c = row == 1 ? name[n - 1] : (n > 1 ? name[0] : ' ');
            fputc(c, out);
            if (prefix_pitch == 2 && x < x1) fputc(' ', out);
        }
        fputc('\n', out);
    }
}

static int token_touches(const Token *t, int x0, int y0, int x1, int y1)
{
    return t->x <= x1 && t->x + t->size - 1 >= x0 && t->y <= y1 && t->y + t->size - 1 >= y0;
}

void maptools_dump(FILE *out, const Map *m, int x0, int y0, int x1, int y1)
{
    x0 = iclamp(x0, 0, m->w - 1); x1 = iclamp(x1, x0, m->w - 1);
    y0 = iclamp(y0, 0, m->h - 1); y1 = iclamp(y1, y0, m->h - 1);
    int gutter = digits(y1 + 1) + 1;

    /* Which creature shows on each square in view: the newest on top, as
     * the app draws them. */
    int vw = x1 - x0 + 1, vh = y1 - y0 + 1;
    int *on = xcalloc((size_t)vw * (size_t)vh, sizeof *on);
    for (int i = 0; i < m->tokens.n; i++) {
        const Token *t = &m->tokens.v[i];
        for (int y = t->y; y < t->y + t->size; y++)
            for (int x = t->x; x < t->x + t->size; x++)
                if (x >= x0 && x <= x1 && y >= y0 && y <= y1)
                    on[(size_t)(y - y0) * (size_t)vw + (size_t)(x - x0)] = i + 1;
    }

    /* One line at a time, trailing blanks trimmed: a dump pasted into a
     * file or a prompt should not carry invisible tails. */
    char *row = xmalloc((size_t)(2 * vw + 2) + (size_t)gutter + 2);
    header_rows(out, gutter, x0, x1, 2);
    for (int ly = 2 * y0; ly <= 2 * y1 + 2; ly++) {
        int y = ly / 2;
        int n = (ly & 1) ? snprintf(row, (size_t)gutter + 1, "%*d ", gutter - 1, y + 1)
                         : snprintf(row, (size_t)gutter + 1, "%*s", gutter, "");
        for (int lx = 2 * x0; lx <= 2 * x1 + 2; lx++) {
            int  x = lx / 2;
            char c;
            if (!(ly & 1) && !(lx & 1))  c = corner_char(m, x, y);
            else if (!(ly & 1))          c = hedge_char(m, x, y);
            else if (!(lx & 1))          c = vedge_char(m, x, y);
            else {
                int t = on[(size_t)(y - y0) * (size_t)vw + (size_t)(x - x0)];
                c = t ? token_glyph(t - 1) : tile_file_char(map_tile(m, x, y));
            }
            row[n++] = c;
        }
        while (n > 0 && row[n - 1] == ' ') n--;
        fwrite(row, 1, (size_t)n, out);
        fputc('\n', out);
    }
    free(row);
    free(on);

    /* Creatures, notes, the key, fog and the map's settings: each only
     * where it touches the region shown. */
    char at[MAP_COORD_MAX], to[MAP_COORD_MAX];
    int  shown = 0;
    for (int i = 0; i < m->tokens.n; i++) {
        const Token *t = &m->tokens.v[i];
        if (!token_touches(t, x0, y0, x1, y1)) continue;
        if (!shown++) fputs("\ncreatures\n", out);
        map_coord_name(t->x, t->y, at, sizeof at);
        char where[2 * MAP_COORD_MAX + 2], kind[16];
        if (t->size > 1) {
            map_coord_name(t->x + t->size - 1, t->y + t->size - 1, to, sizeof to);
            snprintf(where, sizeof where, "%s-%s", at, to);
            snprintf(kind, sizeof kind, "%s %dx%d", t->kind == TOKEN_ENEMY ? "enemy" : "player", t->size, t->size);
        } else {
            str_lcpy(where, at, sizeof where);
            str_lcpy(kind, t->kind == TOKEN_ENEMY ? "enemy" : "player", sizeof kind);
        }
        fprintf(out, "  %c  %-16s %-12s %s\n", token_glyph(i), t->label[0] ? t->label : "(unnamed)", kind, where);
        if (t->note[0]) fprintf(out, "     note: %s\n", t->note);
    }

    shown = 0;
    for (int i = 0; i < m->nnotes; i++) {
        const Note *n = &m->notes[i];
        if (n->x < x0 || n->x > x1 || n->y < y0 || n->y > y1) continue;
        if (!shown++) fputs("\nnotes\n", out);
        map_coord_name(n->x, n->y, at, sizeof at);
        fprintf(out, "  %-5s %s\n", at, n->text);
    }

    fputs("\nkey\n"
          "  .  floor   ~  water   :  rough   \"  brush   =  wood   ^  hazard   (blank) void\n"
          "  | -  wall   +  door   /  open door   %  window   S  secret door   s  open secret door\n"
          "  S and s are the GM's: the players see a wall and an open door\n", out);

    int patches = 0;
    for (int i = 0; i < FOG_PATCH_MAX; i++)
        patches += m->fog_patches[i].name[0] && !m->fog_patches[i].dead;
    if (patches) {
        fprintf(out, "\nfog %s, soft edge %s\n", m->fog_on ? "on" : "off", m->fog_soft_edge ? "on" : "off");
        for (int i = 0; i < FOG_PATCH_MAX; i++) {
            const FogPatch *p = &m->fog_patches[i];
            if (!p->name[0] || p->dead) continue;
            int  seen, tiles = fog_count(m, i + 1, &seen);
            char rev[12];
            if (p->reveal == FOG_REVEAL_MANUAL) str_lcpy(rev, "manual", sizeof rev);
            else                                snprintf(rev, sizeof rev, "%d", p->reveal);
            fprintf(out, "  %c  %-16s reveal %-6s memory %-3s soft edge %-3s %d squares, %d seen%s\n",
                    'A' + i, p->name, rev, p->memory ? "on" : "off",
                    p->soft_edge < 0 ? "map" : p->soft_edge ? "on" : "off", tiles, seen,
                    p->disabled ? ", disabled" : "");
        }
        fputs("  . no fog   A-O a patch, unseen   a-o seen   1-9 !\"#$%& lit by hand\n\n", out);
        header_rows(out, gutter, x0, x1, 1);
        for (int y = y0; y <= y1; y++) {
            fprintf(out, "%*d ", gutter - 1, y + 1);
            for (int x = x0; x <= x1; x++) fputc(fog_char(m, x, y), out);
            fputc('\n', out);
        }
    }

    fprintf(out, "\nmap  %s  %dx%d  scale %g ft  metric %s  ruleset %s\n", m->name, m->w, m->h,
            m->scale_ft, dist_metric_name((DistMetric)m->metric), m->ruleset[0] ? m->ruleset : "none");
    if (x0 > 0 || y0 > 0 || x1 < m->w - 1 || y1 < m->h - 1) {
        map_coord_name(x0, y0, at, sizeof at);
        map_coord_name(x1, y1, to, sizeof to);
        fprintf(out, "region %s:%s\n", at, to);
    }
}
