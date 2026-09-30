/* The control channel's requests: an agent reading, and editing, the map the
 * GM has open. ctl.c moves the bytes; this reads the lines and runs them
 * against the App. The language and its rules are docs/CONTROL.md's. */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "app_priv.h"
#include "card.h"
#include "corridor.h"
#include "fog.h"
#include "floor.h"
#include "json.h"
#include "link.h"
#include "maptools.h"
#include "prof.h"
#include "character.h"
#include "scene.h"
#include "stamp.h"
#include "util.h"

/* ----------------------------------------------------------------- words */

#define CTL_WORDS    12
#define CTL_WORD_MAX 256

/* A line split into words: spaces between, "..." one word with \" and \\
 * inside. Returns how many, or -1 with why in err. */
static int split_words(const char *line, char w[CTL_WORDS][CTL_WORD_MAX], char *err, size_t errsz)
{
    int n = 0;
    const char *p = line;
    for (;;) {
        while (*p == ' ' || *p == '\t' || *p == '\r') p++;
        if (!*p) break;
        if (n == CTL_WORDS) { snprintf(err, errsz, "more than %d words", CTL_WORDS); return -1; }
        size_t k = 0;
        if (*p == '"') {
            p++;
            while (*p && *p != '"') {
                if (*p == '\\' && (p[1] == '"' || p[1] == '\\')) p++;
                if (k + 1 == CTL_WORD_MAX) { snprintf(err, errsz, "a word over %d characters", CTL_WORD_MAX - 1); return -1; }
                w[n][k++] = *p++;
            }
            if (*p != '"') { snprintf(err, errsz, "a quote is not closed"); return -1; }
            p++;
            if (*p && *p != ' ' && *p != '\t' && *p != '\r') {
                snprintf(err, errsz, "a closing quote runs into the next word");
                return -1;
            }
        } else {
            while (*p && *p != ' ' && *p != '\t' && *p != '\r') {
                if (*p == '"') { snprintf(err, errsz, "a quote in the middle of a word"); return -1; }
                if (k + 1 == CTL_WORD_MAX) { snprintf(err, errsz, "a word over %d characters", CTL_WORD_MAX - 1); return -1; }
                w[n][k++] = *p++;
            }
        }
        w[n][k] = '\0';
        n++;
    }
    return n;
}

/* A line whose first word starts with #. */
static int is_comment(const char *line)
{
    while (*line == ' ' || *line == '\t') line++;
    return *line == '#';
}

/* ----------------------------------------------------------------- state */

const char *app_ctl_screen_name(Screen s)
{
    switch (s) {
    case SCREEN_MENU:    return "menu";
    case SCREEN_BROWSER: return "file browser";
    case SCREEN_EDITOR:  return "build";
    case SCREEN_PLAY:    return "play";
    case SCREEN_HELP:    return "help";
    }
    return "?";
}

const char *app_ctl_mode_name(EdMode m)
{
    switch (m) {
    case ED_NORMAL:  return "normal";
    case ED_WALL:    return "wall";
    case ED_VISUAL:  return "visual";
    case ED_COMMAND: return "command line";
    case ED_STAMP:   return "stamp";
    }
    return "?";
}

const char *app_ctl_busy(const App *a)
{
    if (!a->map)                         return "no map is open";
    if (a->screen == SCREEN_PLAY)        return "the GM is in play mode - edits are build mode's";
    if (a->screen != SCREEN_EDITOR)      return "the GM is not in build mode";
    if (a->modal == MODAL_PROMPT)        return "the GM is answering a prompt";
    if (a->modal == MODAL_PICKER)        return "the GM is choosing from a list";
    if (a->modal != MODAL_NONE)          return "a question is open on the GM's screen";
    if (a->ed.mode == ED_COMMAND)        return "the GM is typing a : command";
    if (a->pending || a->ed.pending_g)   return "the GM is part way through a key";
    if (a->undo.open)                    return "the GM is laying wall";
    return NULL;
}

/* ----------------------------------------------------------------- reads */

/* `own`: this request has edits in an open batch, which is not the GM's. */
static void do_status(App *a, FILE *out, int own)
{
    const Map *m = a->map;
    if (m) {
        fprintf(out, "map %s  %dx%d\n", m->name, m->w, m->h);
        fprintf(out, "file %s%s\n", m->path[0] ? m->path : "(never saved)",
                m->modified ? "  unsaved changes" : "");
    }
    else fputs("map none open\n", out);
    if (a->screen == SCREEN_EDITOR) fprintf(out, "screen build, %s mode\n", app_ctl_mode_name(a->ed.mode));
    else                            fprintf(out, "screen %s\n", app_ctl_screen_name(a->screen));
    if (m && (a->screen == SCREEN_EDITOR || a->screen == SCREEN_PLAY)) {
        int order[MAP_AREAS_MAX];
        if (floor_order(m, order))
            fprintf(out, "floor %s\n", floor_name(m, app_floor_shown(a)));
    }
    fprintf(out, "undo %d back, %d forward%s\n", a->undo.depth, a->undo.nmarks - a->undo.depth,
            own ? ", and this request's changes one more" : "");
    const char *busy = own ? NULL : app_ctl_busy(a);
    fprintf(out, "edits %s%s\n", busy ? "not now: " : "taken", busy ? busy : "");
}

/* An optional last word that must be `json`. */
static int want_json(char w[][CTL_WORD_MAX], int n, int at, char *err, size_t errsz)
{
    if (n <= at) return 0;
    if (n == at + 1 && !strcmp(w[at], "json")) return 1;
    snprintf(err, errsz, "%.20s takes nothing but json after it", w[0]);
    return -1;
}

/* ----------------------------------------------------------------- edits */

/* What a request's edits have done so far: how many lines, the area they
 * touched (for the GM's ring), and the first line, for the status. */
typedef struct {
    int      lines;
    int      x0, y0, x1, y1;          /* x1 < x0 while nothing is touched */
    char     first[48];
    unsigned ops0;                    /* the undo stamp when the first edit began */
    int      deleted;                 /* a creature went: indices behind it shifted */
    FILE    *out;                     /* the answer, for what an edit has to say */
} Edits;

static void touched(Edits *ed, int x0, int y0, int x1, int y1)
{
    if (ed->x1 < ed->x0) { ed->x0 = x0; ed->y0 = y0; ed->x1 = x1; ed->y1 = y1; return; }
    ed->x0 = imin(ed->x0, x0); ed->y0 = imin(ed->y0, y0);
    ed->x1 = imax(ed->x1, x1); ed->y1 = imax(ed->y1, y1);
}

/* A square on the map, by name. */
static int square(const Map *m, const char *w, int *x, int *y, char *err, size_t errsz)
{
    /* A bare row ("5") sets only y: it is not a square. */
    *x = -1;
    if (map_coord_parse(w, x, y) && map_in_bounds(m, *x, *y)) return 1;
    char edge[MAP_COORD_MAX];
    map_coord_name(m->w - 1, m->h - 1, edge, sizeof edge);
    int letters = 0;
    for (const char *p = w; *p; p++) letters |= (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z');
    snprintf(err, errsz, "%.40s is not a square on this map (A1 to %s)%s", w, edge,
             letters && m->nareas ? ", nor a room's name" : "");
    return 0;
}

/* A region for an edit: both ends on the map, never clipped -- an edit that
 * runs off the edge is a mistake to report, not to half do. */
static int region(const Map *m, const char *w, int *x0, int *y0, int *x1, int *y1,
                  char *err, size_t errsz)
{
    /* A named area is its box. */
    int ai = map_area_find(m, w);
    if (ai >= 0) {
        const Area *ar = &m->areas[ai];
        *x0 = ar->x0; *y0 = ar->y0; *x1 = ar->x1; *y1 = ar->y1;
        return 1;
    }
    char a[CTL_WORD_MAX];
    str_lcpy(a, w, sizeof a);
    char *colon = strchr(a, ':');
    const char *b = a;
    if (colon) { *colon = '\0'; b = colon + 1; }
    if (!a[0] || !b[0]) {
        snprintf(err, errsz, "%.40s: a region is two squares, like B2:K12, or one", w);
        return 0;
    }
    int ax, ay, bx, by;
    if (!square(m, a, &ax, &ay, err, errsz) || !square(m, b, &bx, &by, err, errsz)) return 0;
    *x0 = imin(ax, bx); *x1 = imax(ax, bx);
    *y0 = imin(ay, by); *y1 = imax(ay, by);
    return 1;
}

static int tile_kind(const char *w)
{
    for (int k = 0; k < TILE_COUNT; k++)
        if (!strcmp(w, tile_name((uint8_t)k))) return k;
    return -1;
}

/* The words for boundaries, in EdgeKind order: the file's names with the
 * spaces taken out, so each is one word. */
static const char *const EDGE_WORDS[EDGE_COUNT] = {
    "none", "wall", "door", "open", "window", "secret", "opensecret",
};

static int edge_kind(const char *w)
{
    for (int k = 0; k < EDGE_COUNT; k++)
        if (!strcmp(w, EDGE_WORDS[k])) return k;
    return -1;
}

/* "G5|H5" or "C3/C4", '-' for off the map on that side, as
 * maptools_edge_name writes them. Sets the lattice position and which
 * array. */
static int boundary(const Map *m, const char *w, int *vertical, int *x, int *y,
                    char *err, size_t errsz)
{
    char a[CTL_WORD_MAX];
    str_lcpy(a, w, sizeof a);
    char *sep = strpbrk(a, "|/");
    if (!sep) {
        snprintf(err, errsz, "%.40s is not a boundary: G5|H5 across a vertical one, C3/C4 a horizontal", w);
        return 0;
    }
    int v = *sep == '|';
    *sep = '\0';
    const char *l = a, *r = sep + 1;
    int lx = -1, ly = -1, rx = -1, ry = -1;
    int loff = !strcmp(l, "-"), roff = !strcmp(r, "-");
    if ((loff && roff) || (!loff && !square(m, l, &lx, &ly, err, errsz)) ||
        (!roff && !square(m, r, &rx, &ry, err, errsz))) {
        if (loff && roff) snprintf(err, errsz, "a boundary needs a square on one side");
        return 0;
    }
    /* The far side of an edge off the map is implied by the near one. */
    if (loff) { lx = v ? rx - 1 : rx; ly = v ? ry : ry - 1; }
    if (roff) { rx = v ? lx + 1 : lx; ry = v ? ly : ly + 1; }
    int ok = v ? (ly == ry && rx == lx + 1) : (lx == rx && ry == ly + 1);
    if (!ok || (loff && map_in_bounds(m, lx, ly)) || (roff && map_in_bounds(m, rx, ry))) {
        snprintf(err, errsz, "%.40s: the squares are not side by side%s", w,
                 v ? " (| is between a square and the one east of it)"
                   : " (/ is between a square and the one south of it)");
        return 0;
    }
    *vertical = v;
    *x = rx;
    *y = ry;
    return 1;
}

static int word_int(const char *w, int lo, int hi, int *out)
{
    char *end;
    long  v = strtol(w, &end, 10);
    if (!*w || *end || v < lo || v > hi) return 0;
    *out = (int)v;
    return 1;
}

/* A creature by label (exact, else the one label that matches ignoring
 * case), or by a square it stands on. */
static int who(const Map *m, const char *w, char *err, size_t errsz)
{
    int found = -1, loose = -1, nloose = 0;
    for (int i = 0; i < m->tokens.n; i++) {
        if (!strcmp(m->tokens.v[i].label, w)) { found = i; break; }
        if (!strcasecmp(m->tokens.v[i].label, w)) { loose = i; nloose++; }
    }
    if (found < 0 && nloose == 1) found = loose;
    if (found >= 0) return found;
    int x, y;
    if (map_coord_parse(w, &x, &y) && map_in_bounds(m, x, y)) {
        int i = tokens_at(&m->tokens, x, y);
        if (i < 0) snprintf(err, errsz, "no creature stands on %.40s", w);
        return i;
    }
    snprintf(err, errsz, nloose > 1 ? "more than one creature is called %.40s" : "no creature called %.40s", w);
    return -1;
}

/* Can a creature `size` wide stand at (x,y): on the map, on ground, and on
 * nobody else (`skip` is the creature itself, when it is moving)? */
static int fits(const Map *m, int x, int y, int size, int skip, char *err, size_t errsz)
{
    char at[MAP_COORD_MAX];
    map_coord_name(x, y, at, sizeof at);
    if (x + size > m->w || y + size > m->h) {
        snprintf(err, errsz, "%dx%d at %s hangs off the map", size, size, at);
        return 0;
    }
    for (int yy = y; yy < y + size; yy++)
        for (int xx = x; xx < x + size; xx++)
            if (!map_walkable(m, xx, yy)) {
                char v[MAP_COORD_MAX];
                map_coord_name(xx, yy, v, sizeof v);
                snprintf(err, errsz, "%s is void - a creature needs ground", v);
                return 0;
            }
    int other = tokens_overlapping(&m->tokens, x, y, size, skip, TOKEN_ANY_KIND);
    if (other >= 0) {
        const Token *t = &m->tokens.v[other];
        snprintf(err, errsz, "%s is taken by %.30s", at, t->label[0] ? t->label : "a creature");
        return 0;
    }
    return 1;
}

static int label_ok(const Map *m, const char *label, int skip, char *err, size_t errsz)
{
    if (!label[0]) { snprintf(err, errsz, "a creature needs a label"); return 0; }
    if (strlen(label) >= TOKEN_LABEL_MAX) {
        snprintf(err, errsz, "the label is over %d characters", TOKEN_LABEL_MAX - 1);
        return 0;
    }
    for (int i = 0; i < m->tokens.n; i++)
        if (i != skip && !strcmp(m->tokens.v[i].label, label)) {
            snprintf(err, errsz, "there is already a creature called %.30s", label);
            return 0;
        }
    return 1;
}

/* Where a creature goes: a square it must fit on, or a named area, where
 * it takes the free square nearest the area's middle (reading order breaks
 * a tie). */
static int spot(const Map *m, const char *w, int size, int skip, int *x, int *y, char *err, size_t errsz)
{
    int ai = map_area_find(m, w);
    if (ai < 0) return square(m, w, x, y, err, errsz) && fits(m, *x, *y, size, skip, err, errsz);
    const Area *ar = &m->areas[ai];
    /* Twice the middle, so a box of even width has a middle between squares. */
    long mx = ar->x0 + ar->x1 + 1 - size, my = ar->y0 + ar->y1 + 1 - size, best = -1;
    char e2[160];
    for (int yy = ar->y0; yy + size - 1 <= ar->y1; yy++)
        for (int xx = ar->x0; xx + size - 1 <= ar->x1; xx++) {
            long d = (2L * xx - mx) * (2L * xx - mx) + (2L * yy - my) * (2L * yy - my);
            if (best >= 0 && d >= best) continue;
            if (!fits(m, xx, yy, size, skip, e2, sizeof e2)) continue;
            best = d; *x = xx; *y = yy;
        }
    if (best < 0) snprintf(err, errsz, "no room in %.30s for a %dx%d creature", ar->name, size, size);
    return best >= 0;
}

/* Where a link's end goes: its top-left square, or a named area, where the
 * block takes the ground nearest the area's middle that no link has -- nor
 * the link's other end, when `ox` is not -1 (the block of that size there). */
static int link_spot(const Map *m, const char *w, int size, int ox, int oy, int *x, int *y,
                     char *err, size_t errsz)
{
    int ai = map_area_find(m, w);
    if (ai < 0) return square(m, w, x, y, err, errsz);
    const Area *ar = &m->areas[ai];
    long mx = ar->x0 + ar->x1 + 1 - size, my = ar->y0 + ar->y1 + 1 - size, best = -1;
    for (int yy = ar->y0; yy + size - 1 <= ar->y1; yy++)
        for (int xx = ar->x0; xx + size - 1 <= ar->x1; xx++) {
            long d = (2L * xx - mx) * (2L * xx - mx) + (2L * yy - my) * (2L * yy - my);
            if (best >= 0 && d >= best) continue;
            int ground = 1;
            for (int k = 0; k < size * size && ground; k++)
                ground = map_tile(m, xx + k % size, yy + k / size) != TILE_VOID;
            if (!ground || link_meets(m, xx, yy, size, size, NULL) >= 0) continue;
            if (ox >= 0 && xx < ox + size && ox < xx + size && yy < oy + size && oy < yy + size) continue;
            best = d; *x = xx; *y = yy;
        }
    if (best < 0) snprintf(err, errsz, "no room in %.30s for a %dx%d link end", ar->name, size, size);
    return best >= 0;
}

/* The outline of a box of squares, as build mode walls a shape. */
static void outline(Undo *u, Map *m, int x0, int y0, int x1, int y1, uint8_t kind)
{
    EdShape sh = ed_shape(ED_SHAPE_RECT, x0, y0, x1, y1, 0);
    ed_wall_shape(m, u, &sh, kind);
}

enum { SIDE_NORTH, SIDE_SOUTH, SIDE_EAST, SIDE_WEST };

static int side_word(const char *w)
{
    static const char *const SIDES[] = { "north", "south", "east", "west" };
    for (int i = 0; i < 4; i++)
        if (!strcmp(w, SIDES[i])) return i;
    return -1;
}

/* "8x6" */
static int size_word(const char *w, int *sw, int *sh)
{
    char tail;
    return sscanf(w, "%dx%d%c", sw, sh, &tail) == 2 && *sw >= 1 && *sh >= 1 &&
           *sw <= MAP_MAX_DIM && *sh <= MAP_MAX_DIM;
}

#define BAD(...) do { snprintf(err, errsz, __VA_ARGS__); return -1; } while (0)

/* A room's box, from any of its forms:
 *   room REGION
 *   room NAME REGION
 *   room NAME SQUARE WxH
 *   room NAME WxH east|west|north|south of OTHER [gap N] [top|middle|bottom|left|right]
 * The box must lie on the map; the name, when there is one, comes back. */
static int room_box(const Map *m, char w[][CTL_WORD_MAX], int n, const char **name,
                    int *x0, int *y0, int *x1, int *y1, char *err, size_t errsz)
{
    static const char USE[] = "room REGION, room NAME REGION, room NAME C3 8x6, or room NAME 8x6 east of OTHER [gap N] [middle]";
    int sw, sh;
    if (n == 2) return region(m, w[1], x0, y0, x1, y1, err, errsz);
    if (n < 3) { snprintf(err, errsz, "%s", USE); return 0; }
    if (!map_area_name_ok(w[1])) {
        snprintf(err, errsz, "%.40s: a room's name is 1-%d characters, no quote or colon, and not a square",
                 w[1], AREA_NAME_MAX - 1);
        return 0;
    }
    *name = w[1];
    if (n == 3) return region(m, w[2], x0, y0, x1, y1, err, errsz);
    if (n == 4 && size_word(w[3], &sw, &sh)) {
        int x, y;
        if (!square(m, w[2], &x, &y, err, errsz)) return 0;
        *x0 = x; *y0 = y; *x1 = x + sw - 1; *y1 = y + sh - 1;
    }
    else if (n >= 6 && size_word(w[2], &sw, &sh) && !strcmp(w[4], "of")) {
        int side = side_word(w[3]), oi = map_area_find(m, w[5]), gap = 0;
        const char *align = "middle";
        if (side < 0) { snprintf(err, errsz, "%.20s: a side is north, south, east or west", w[3]); return 0; }
        if (oi < 0) { snprintf(err, errsz, "no room called %.40s", w[5]); return 0; }
        for (int i = 6; i < n; i++) {
            if (!strcmp(w[i], "gap") && i + 1 < n && word_int(w[i + 1], 0, MAP_MAX_DIM, &gap)) i++;
            else if (!strcmp(w[i], "top") || !strcmp(w[i], "bottom") || !strcmp(w[i], "left") ||
                     !strcmp(w[i], "right") || !strcmp(w[i], "middle")) align = w[i];
            else { snprintf(err, errsz, "%s", USE); return 0; }
        }
        const Area *o = &m->areas[oi];
        int horiz = side == SIDE_EAST || side == SIDE_WEST;
        int bad = horiz ? (!strcmp(align, "left") || !strcmp(align, "right"))
                        : (!strcmp(align, "top") || !strcmp(align, "bottom"));
        if (bad) {
            snprintf(err, errsz, "%s of a room lines up %s", w[3], horiz ? "top, middle or bottom" : "left, middle or right");
            return 0;
        }
        if (side == SIDE_EAST)  { *x0 = o->x1 + 1 + gap; *x1 = *x0 + sw - 1; }
        if (side == SIDE_WEST)  { *x1 = o->x0 - 1 - gap; *x0 = *x1 - sw + 1; }
        if (side == SIDE_SOUTH) { *y0 = o->y1 + 1 + gap; *y1 = *y0 + sh - 1; }
        if (side == SIDE_NORTH) { *y1 = o->y0 - 1 - gap; *y0 = *y1 - sh + 1; }
        if (horiz) {
            if (!strcmp(align, "top"))         *y0 = o->y0;
            else if (!strcmp(align, "bottom")) *y0 = o->y1 - sh + 1;
            else                               *y0 = o->y0 + ((o->y1 - o->y0 + 1) - sh) / 2;
            *y1 = *y0 + sh - 1;
        } else {
            if (!strcmp(align, "left"))        *x0 = o->x0;
            else if (!strcmp(align, "right"))  *x0 = o->x1 - sw + 1;
            else                               *x0 = o->x0 + ((o->x1 - o->x0 + 1) - sw) / 2;
            *x1 = *x0 + sw - 1;
        }
    }
    else { snprintf(err, errsz, "%s", USE); return 0; }

    if (*x0 < 0 || *y0 < 0 || *x1 >= m->w || *y1 >= m->h) {
        snprintf(err, errsz, "%.30s, %dx%d, would run off the %s side of the map", *name,
                 *x1 - *x0 + 1, *y1 - *y0 + 1,
                 *x0 < 0 ? "west" : *y0 < 0 ? "north" : *x1 >= m->w ? "east" : "south");
        return 0;
    }
    return 1;
}

static int token_line(App *a, char w[][CTL_WORD_MAX], int n, Edits *ed, char *err, size_t errsz)
{
    Map  *m = a->map;
    Undo *u = &a->undo;
    if (n < 2) BAD("token add, token move, token del or token set");
    const char *sub = w[1];

    if (!strcmp(sub, "add") && n >= 6 && !strcmp(w[4], "from")) {
        /* token add player|enemy SQ from NAME [hidden]: a character template,
         * its size and label (numbered to stay unique), its rolls the map lacks. */
        int kind;
        if (!strcmp(w[2], "player"))     kind = TOKEN_PLAYER;
        else if (!strcmp(w[2], "enemy")) kind = TOKEN_ENEMY;
        else BAD("%.20s: a creature is a player or an enemy", w[2]);
        if (n > 7 || (n == 7 && strcmp(w[6], "hidden") != 0))
            BAD("token add player|enemy SQUARE from NAME [hidden]");
        Map *c = character_load(w[5], err, errsz);
        if (!c) return -1;
        const Token *ct = character_token(c);
        char label[TOKEN_LABEL_MAX], said[160];
        tokens_unique_label(&m->tokens, ct->label, label, sizeof label);
        int x, y, idx = -1;
        if (!label[0]) snprintf(err, errsz, "%.40s has no label, and the channel names creatures by label", w[5]);
        else if (spot(m, w[3], ct->size, -1, &x, &y, err, errsz))
            idx = character_place(m, u, c, kind, x, y, n == 7, said, sizeof said, err, errsz);
        map_free(c);
        if (idx < 0) return -1;
        const Token *t = &m->tokens.v[idx];
        char at[MAP_COORD_MAX];
        map_coord_name(t->x, t->y, at, sizeof at);
        fprintf(ed->out, "placed \"%s\" at %s%s%s\n", t->label, at, said[0] ? " - " : "", said);
        touched(ed, t->x, t->y, t->x + t->size - 1, t->y + t->size - 1);
        return 0;
    }

    if (!strcmp(sub, "add")) {
        /* token add player|enemy SQ [size N] [hidden] "Label" */
        if (n < 5 || n > 8) BAD("token add player|enemy SQUARE [size N] [hidden] \"Label\"");
        Token t;
        memset(&t, 0, sizeof t);
        if (!strcmp(w[2], "player"))     t.kind = TOKEN_PLAYER;
        else if (!strcmp(w[2], "enemy")) t.kind = TOKEN_ENEMY;
        else BAD("%.20s: a creature is a player or an enemy", w[2]);
        int x, y, size = 1;
        /* "hidden" last is a label forgotten, not a creature named hidden. */
        if (n >= 5 && !strcmp(w[n - 1], "hidden") && strcmp(w[n - 2], "hidden") != 0)
            BAD("the label goes last: token add enemy C3 hidden \"Ghoul\"");
        if (n > 5 && !strcmp(w[n - 2], "size")) BAD("the size goes before the label: token add enemy C3 size 2 \"Ogre\"");
        /* The words between the square and the label, in any order. */
        for (int k = 4; k < n - 1; k++) {
            if (!strcmp(w[k], "hidden")) t.hidden = 1;
            else if (!strcmp(w[k], "size") && k + 1 < n - 1 && word_int(w[k + 1], 1, 3, &size)) k++;
            else if (!strcmp(w[k], "size")) BAD("size is 1, 2 or 3 squares wide, as: size 2");
            else BAD("%.20s: before the label goes size N or hidden", w[k]);
        }
        const char *label = w[n - 1];
        if (!label_ok(m, label, -1, err, errsz) || !spot(m, w[3], size, -1, &x, &y, err, errsz)) return -1;
        t.x = (int16_t)x;
        t.y = (int16_t)y;
        t.size = (uint8_t)size;
        str_lcpy(t.label, label, sizeof t.label);
        undo_add_token(u, m, t);
        touched(ed, x, y, x + size - 1, y + size - 1);
        return 0;
    }

    if (n < 3) BAD("token %.10s wants a creature: its label, or a square it stands on", sub);
    int i = who(m, w[2], err, errsz);
    if (i < 0) return -1;
    Token t = m->tokens.v[i];
    touched(ed, t.x, t.y, t.x + t.size - 1, t.y + t.size - 1);

    if (!strcmp(sub, "move")) {
        if (n != 4) BAD("token move WHO SQUARE");
        int x, y;
        if (!spot(m, w[3], t.size, i, &x, &y, err, errsz)) return -1;
        undo_move_token(u, m, i, x, y);
        touched(ed, x, y, x + t.size - 1, y + t.size - 1);
        return 0;
    }
    if (!strcmp(sub, "del")) {
        if (n != 3) BAD("token del WHO");
        /* Passing the turn on moves the fight, and the fight is the GM's:
         * an edit to the map must not advance it as a side effect. */
        if (t.turn & TURN_ACTING) BAD("%.30s holds the turn - the GM passes it on first", t.label);
        undo_del_token(u, m, i);
        range_token_removed(&a->play.range, i, t.x, t.y);
        ed->deleted = 1;
        turn_settle(m, u);
        play_focus(&a->play, -1);
        a->play.visual = 0;
        return 0;
    }
    if (!strcmp(sub, "set")) {
        if (n != 5) BAD("token set WHO label \"...\", size N, note \"...\", or hidden on|off");
        if (!strcmp(w[3], "label")) {
            if (!label_ok(m, w[4], i, err, errsz)) return -1;
            str_lcpy(t.label, w[4], sizeof t.label);
        }
        else if (!strcmp(w[3], "size")) {
            int size;
            if (!word_int(w[4], 1, 3, &size)) BAD("size is 1, 2 or 3");
            if (!fits(m, t.x, t.y, size, i, err, errsz)) return -1;
            t.size = (uint8_t)size;
            touched(ed, t.x, t.y, t.x + size - 1, t.y + size - 1);
        }
        else if (!strcmp(w[3], "note")) {
            if (strlen(w[4]) >= TOKEN_NOTE_MAX) BAD("the note is over %d characters", TOKEN_NOTE_MAX - 1);
            str_lcpy(t.note, w[4], sizeof t.note);
        }
        else if (!strcmp(w[3], "hidden")) {
            if (strcmp(w[4], "on") != 0 && strcmp(w[4], "off") != 0) BAD("hidden on, or hidden off");
            t.hidden = !strcmp(w[4], "on");
        }
        else BAD("token set changes a label, a size, a note or hidden");
        undo_edit_token(u, m, i, t);
        return 0;
    }
    BAD("token %.20s: add, move, del or set", sub);
}

/* One edit line: 0, or -1 with why. The caller has the batch open. */
static int edit_line(App *a, char w[][CTL_WORD_MAX], int n, Edits *ed, char *err, size_t errsz)
{
    Map  *m = a->map;
    Undo *u = &a->undo;
    const char *v = w[0];
    int x0, y0, x1, y1;

    if (!strcmp(v, "room")) {
        const char *name = NULL;
        if (!room_box(m, w, n, &name, &x0, &y0, &x1, &y1, err, errsz)) return -1;
        for (int y = y0; y <= y1; y++)
            for (int x = x0; x <= x1; x++) undo_set_tile(u, m, x, y, TILE_FLOOR);
        const char *fwhy = name ? floor_problem_box(m, map_area_find(m, name), x0, y0, x1, y1) : NULL;
        if (fwhy) BAD("%.40s is a floor, and %s", name, fwhy);
        outline(u, m, x0, y0, x1, y1, EDGE_WALL);
        if (name && !undo_set_area(u, m, name, x0, y0, x1, y1))
            BAD("the map holds %d named areas", MAP_AREAS_MAX);
    }
    else if (!strcmp(v, "area")) {
        /* area NAME REGION, area NAME remove: a name, nothing drawn. */
        if (n != 3) BAD("area NAME REGION, or area NAME remove");
        if (!strcmp(w[2], "off")) BAD("area %.40s remove takes the name off", w[1]);
        if (!map_area_name_ok(w[1]))
            BAD("%.40s: an area's name is 1-%d characters, no quote or colon, and not a square",
                w[1], AREA_NAME_MAX - 1);
        if (!strcmp(w[2], "remove")) {
            int ai = map_area_find(m, w[1]);
            if (ai < 0) BAD("no area called %.40s", w[1]);
            const Area *ar = &m->areas[ai];
            x0 = ar->x0; y0 = ar->y0; x1 = ar->x1; y1 = ar->y1;
            undo_remove_area(u, m, w[1]);
        } else {
            if (!region(m, w[2], &x0, &y0, &x1, &y1, err, errsz)) return -1;
            const char *fwhy = floor_problem_box(m, map_area_find(m, w[1]), x0, y0, x1, y1);
            if (fwhy) BAD("%.40s is a floor, and %s", w[1], fwhy);
            if (!undo_set_area(u, m, w[1], x0, y0, x1, y1)) BAD("the map holds %d named areas", MAP_AREAS_MAX);
        }
    }
    else if (!strcmp(v, "corridor")) {
        /* corridor A B [width N] [KIND]: dug through void between two named
         * rooms, walled along, KIND at both ends (a door when one wide,
         * open when wider). */
        int width = 1, k = -1;
        if (n < 3) BAD("corridor ROOM ROOM [width 1-3] [KIND]");
        int ia = map_area_find(m, w[1]), ib = map_area_find(m, w[2]);
        if (ia < 0) BAD("no room called %.40s", w[1]);
        if (ib < 0) BAD("no room called %.40s", w[2]);
        if (ia == ib) BAD("a corridor joins two rooms");
        for (int i = 3; i < n; i++) {
            if (!strcmp(w[i], "width") && i + 1 < n && word_int(w[i + 1], 1, 3, &width)) i++;
            else if ((k = edge_kind(w[i])) < 0) BAD("corridor ROOM ROOM [width 1-3] [KIND]");
        }
        if (k < 0) k = width == 1 ? EDGE_DOOR_CLOSED : EDGE_NONE;
        Corridor c;
        if (corridor_plan(m, &m->areas[ia], &m->areas[ib], width, &c, err, errsz) < 0) return -1;
        corridor_dig(m, u, &c, (uint8_t)k);
        /* What changed: the legs, and the doors at their ends. */
        x1 = -1; x0 = y0 = y1 = 0;
        for (int i = 0; i < c.nleg; i++) touched(ed, c.leg[i].x0, c.leg[i].y0, c.leg[i].x1, c.leg[i].y1);
        for (int i = 0; i < c.nend; i++) {
            int ex = imin(c.end[i].x, m->w - 1), ey = imin(c.end[i].y, m->h - 1);
            touched(ed, imax(ex - c.end[i].vert, 0), imax(ey - !c.end[i].vert, 0), ex, ey);
        }
    }
    else if (!strcmp(v, "door")) {
        /* door ROOM SIDE [N|middle] [KIND]: on the boundary of a side. */
        int side, at = -1, k = EDGE_DOOR_CLOSED;
        if (n < 3 || n > 5) BAD("door ROOM north|south|east|west [N|middle] [KIND]");
        int ai = map_area_find(m, w[1]);
        if (ai < 0) BAD("no room called %.40s - room NAME ... names one", w[1]);
        const Area *ar = &m->areas[ai];
        if ((side = side_word(w[2])) < 0) BAD("%.20s: a side is north, south, east or west", w[2]);
        int along = side == SIDE_EAST || side == SIDE_WEST ? ar->y1 - ar->y0 + 1 : ar->x1 - ar->x0 + 1;
        for (int i = 3; i < n; i++) {
            int num;
            if (!strcmp(w[i], "middle")) at = (along - 1) / 2;
            else if (word_int(w[i], 1, along, &num)) at = num - 1;
            else if ((num = edge_kind(w[i])) >= 0) k = num;
            else BAD("%.20s: a square along the side (1-%d, from the %s), middle, or a kind", w[i], along,
                     side == SIDE_EAST || side == SIDE_WEST ? "top" : "left");
        }
        if (at < 0) at = (along - 1) / 2;
        if (side == SIDE_EAST || side == SIDE_WEST) {
            int x = side == SIDE_EAST ? ar->x1 + 1 : ar->x0, y = ar->y0 + at;
            undo_set_vedge(u, m, x, y, (uint8_t)k);
            x0 = imax(x - 1, 0); x1 = imin(x, m->w - 1); y0 = y1 = y;
        } else {
            int x = ar->x0 + at, y = side == SIDE_SOUTH ? ar->y1 + 1 : ar->y0;
            undo_set_hedge(u, m, x, y, (uint8_t)k);
            y0 = imax(y - 1, 0); y1 = imin(y, m->h - 1); x0 = x1 = x;
        }
    }
    else if (!strcmp(v, "tile")) {
        int k;
        if (n != 3) BAD("tile REGION KIND");
        if (!region(m, w[1], &x0, &y0, &x1, &y1, err, errsz)) return -1;
        if ((k = tile_kind(w[2])) < 0) BAD("%.20s: a tile is void, floor, water, rough, brush, wood or hazard", w[2]);
        for (int y = y0; y <= y1; y++)
            for (int x = x0; x <= x1; x++) undo_set_tile(u, m, x, y, (uint8_t)k);
    }
    else if (!strcmp(v, "wall")) {
        int k = EDGE_WALL;
        if (n != 2 && n != 3) BAD("wall REGION [KIND]");
        if (!region(m, w[1], &x0, &y0, &x1, &y1, err, errsz)) return -1;
        if (n == 3 && (k = edge_kind(w[2])) < 0)
            BAD("%.20s: a boundary is none, wall, door, open, window, secret or opensecret", w[2]);
        outline(u, m, x0, y0, x1, y1, (uint8_t)k);
    }
    else if (!strcmp(v, "edge")) {
        int vert, x, y, k;
        if (n != 3) BAD("edge BOUNDARY KIND, like edge G5|H5 door");
        if (!boundary(m, w[1], &vert, &x, &y, err, errsz)) return -1;
        if ((k = edge_kind(w[2])) < 0)
            BAD("%.20s: a boundary is none, wall, door, open, window, secret or opensecret", w[2]);
        if (vert) undo_set_vedge(u, m, x, y, (uint8_t)k);
        else      undo_set_hedge(u, m, x, y, (uint8_t)k);
        x0 = imin(imax(vert ? x - 1 : x, 0), m->w - 1); x1 = imin(x, m->w - 1);
        y0 = imin(imax(vert ? y : y - 1, 0), m->h - 1); y1 = imin(y, m->h - 1);
    }
    else if (!strcmp(v, "note")) {
        int x, y;
        if (n != 2 && n != 3) BAD("note SQUARE \"text\", or note SQUARE to take it off");
        /* A room's note goes on its middle square. */
        int ai = map_area_find(m, w[1]);
        if (ai >= 0) {
            x = (m->areas[ai].x0 + m->areas[ai].x1) / 2;
            y = (m->areas[ai].y0 + m->areas[ai].y1) / 2;
        }
        else if (!square(m, w[1], &x, &y, err, errsz)) return -1;
        const char *text = n == 3 ? w[2] : "";
        if (strlen(text) >= NOTE_MAX) BAD("the note is over %d characters", NOTE_MAX - 1);
        if (!undo_set_note(u, m, x, y, text)) BAD("no room: a map holds %d notes on squares", MAP_NOTES_MAX);
        x0 = x1 = x; y0 = y1 = y;
    }
    else if (!strcmp(v, "fog")) {
        int id;
        if (n != 4 || strcmp(w[1], "paint") != 0) BAD("fog paint REGION N (0 scrubs)");
        if (!region(m, w[2], &x0, &y0, &x1, &y1, err, errsz)) return -1;
        if (!word_int(w[3], 0, FOG_PATCH_MAX, &id)) BAD("a fog patch is 1 to %d, or 0 to scrub", FOG_PATCH_MAX);
        if (id && (!m->fog_patches[id - 1].name[0] || m->fog_patches[id - 1].dead))
            BAD("there is no fog patch %d - :fog makes one", id);
        for (int y = y0; y <= y1; y++)
            for (int x = x0; x <= x1; x++) fog_paint(m, u, x, y, id);
    }
    else if (!strcmp(v, "floor")) {
        /* floor NAME LEVEL, floor NAME off */
        if (n != 3) BAD("floor NAME LEVEL, or floor NAME off");
        int ai = map_area_find(m, w[1]);
        if (ai < 0) BAD("no area called %.40s - area NAME REGION names one", w[1]);
        const Area *ar = &m->areas[ai];
        if (!strcmp(w[2], "off")) {
            if (!ar->floor) BAD("%.40s is not a floor", w[1]);
            undo_set_floor(u, m, ar->name, 0, 0);
        } else {
            int level;
            if (!word_int(w[2], FLOOR_LEVEL_MIN, FLOOR_LEVEL_MAX, &level))
                BAD("%.20s: a level is %d to %d, or off", w[2], FLOOR_LEVEL_MIN, FLOOR_LEVEL_MAX);
            const char *why = floor_problem(m, ai);
            if (why) BAD("%.40s cannot be a floor: %s", w[1], why);
            undo_set_floor(u, m, ar->name, 1, level);
        }
        x0 = ar->x0; y0 = ar->y0; x1 = ar->x1; y1 = ar->y1;
    }
    else if (!strcmp(v, "link")) {
        /* link A B [KIND] [size 2|3] [oneway] [secret]: a new one, numbered
         * the lowest free. link N remove, or link N and what changes. */
        if (n < 3) BAD("link A B [KIND] [size 2|3] [oneway] [secret], or link N remove");
        Link l;
        memset(&l, 0, sizeof l);
        int first = 1, isnum = w[1][0] != '\0';
        for (const char *p = w[1]; *p; p++) isnum &= *p >= '0' && *p <= '9';
        if (isnum) {
            int num, li;
            if (!word_int(w[1], 1, LINK_NUM_MAX, &num) || (li = link_find(m, num)) < 0)
                BAD("there is no link %.10s", w[1]);
            l = m->links[li];
            if (n == 3 && !strcmp(w[2], "off")) BAD("link %d remove takes a link away", num);
            if (n == 3 && !strcmp(w[2], "remove")) {
                for (int e = 0; e < 2; e++) touched(ed, l.x[e], l.y[e], l.x[e] + l.size - 1, l.y[e] + l.size - 1);
                undo_remove_link(u, m, num);
                return 0;
            }
            first = 2;
        } else if (n >= 4 && !strcmp(w[2], "to")) {
            /* link A to MAP PLACE [KIND] [size N] [secret]: to another map. */
            l.num = (uint8_t)link_free_num(m);
            if (!l.num) BAD("the map holds %d links", MAP_LINKS_MAX);
            if (n < 5) BAD("link SQUARE to MAP PLACE [KIND] [size N] [secret] - PLACE an area or a square in MAP");
            l.size = 1;
            for (int i = 5; i < n; i++) {
                int k = link_kind_from_name(w[i]), sz;
                if (k >= 0) l.kind = (uint8_t)k;
                else if (!strcmp(w[i], "secret")) l.secret = 1;
                else if (!strcmp(w[i], "size") && i + 1 < n && word_int(w[i + 1], 1, LINK_SIZE_MAX, &sz)) { l.size = (uint8_t)sz; i++; }
                else BAD("%.20s: after the place goes a kind, size N or secret", w[i]);
            }
            if (strlen(w[4]) >= AREA_NAME_MAX) BAD("no area has a name that long");
            char why[200];
            if (link_map_check(m, w[3], w[4], why, sizeof why)) BAD("%s", why);
            str_lcpy(l.to_map, w[3], sizeof l.to_map);
            str_lcpy(l.to_place, w[4], sizeof l.to_place);
            int ax, ay;
            if (!link_spot(m, w[1], l.size, -1, -1, &ax, &ay, err, errsz)) return -1;
            l.x[0] = l.x[1] = (int16_t)ax; l.y[0] = l.y[1] = (int16_t)ay;
            first = n;
        } else {
            l.num = (uint8_t)link_free_num(m);
            if (!l.num) BAD("the map holds %d links", MAP_LINKS_MAX);
            l.size = 1;
            /* The size first: where an end goes in a named area depends on it. */
            for (int i = 3; i < n; i++) {
                if (strcmp(w[i], "size") != 0) continue;
                int sz;
                if (i + 1 >= n || !word_int(w[i + 1], 1, LINK_SIZE_MAX, &sz))
                    BAD("size is 1, 2 or 3 squares across, as: size 2");
                l.size = (uint8_t)sz;
            }
            int ax, ay, bx, by;
            if (!link_spot(m, w[1], l.size, -1, -1, &ax, &ay, err, errsz)) return -1;
            if (!link_spot(m, w[2], l.size, ax, ay, &bx, &by, err, errsz)) return -1;
            l.x[0] = (int16_t)ax; l.y[0] = (int16_t)ay; l.x[1] = (int16_t)bx; l.y[1] = (int16_t)by;
            first = 3;
        }
        for (int i = first; i < n; i++) {
            int k = link_kind_from_name(w[i]);
            if (l.to_map[0] && (!strcmp(w[i], "oneway") || !strcmp(w[i], "twoway") || !strcmp(w[i], "reverse")))
                BAD("link %d leads to another map: it has one end here, and goes one way", l.num);
            if (k >= 0)                                l.kind = (uint8_t)k;
            else if (!strcmp(w[i], "oneway"))          l.oneway = 1;
            else if (!strcmp(w[i], "twoway"))          l.oneway = 0;
            else if (!strcmp(w[i], "secret"))          l.secret = 1;
            else if (!strcmp(w[i], "seen"))            l.secret = 0;
            else if (!strcmp(w[i], "size") && !isnum)  i++;
            else if (!strcmp(w[i], "reverse") && isnum) {
                int16_t tx = l.x[0], ty = l.y[0];
                l.x[0] = l.x[1]; l.y[0] = l.y[1]; l.x[1] = tx; l.y[1] = ty;
            }
            else BAD("%.20s: a link is stairs, ladder, trapdoor or portal; oneway, twoway, secret, seen%s",
                     w[i], isnum ? ", reverse or remove" : " or size N");
        }
        const char *why = link_problem(m, &l);
        if (why) BAD("%s", why);
        (void)undo_set_link(u, m, &l);
        for (int e = 0; e < link_ends(&l); e++) touched(ed, l.x[e], l.y[e], l.x[e] + l.size - 1, l.y[e] + l.size - 1);
        return 0;
    }
    else if (!strcmp(v, "stamp")) {
        /* stamp NAME SQUARE [rotate 90|180|270] [mirror] */
        int x, y, quarters = 0, mirror = 0;
        if (n < 3) BAD("stamp NAME SQUARE [rotate 90|180|270] [mirror]");
        if (!square(m, w[2], &x, &y, err, errsz)) return -1;
        for (int i = 3; i < n; i++) {
            int deg;
            if (!strcmp(w[i], "mirror")) mirror = 1;
            else if (!strcmp(w[i], "rotate") && i + 1 < n && word_int(w[i + 1], 0, 270, &deg) && deg % 90 == 0) {
                quarters = deg / 90;
                i++;
            }
            else BAD("stamp NAME SQUARE [rotate 90|180|270] [mirror]");
        }
        Map *st = stamp_load(w[1], err, errsz);
        if (!st) return -1;
        if (quarters) { Map *t = stamp_turned(st, quarters); map_free(st); st = t; }
        if (mirror)   { Map *t = stamp_mirrored(st); map_free(st); st = t; }
        int ok = stamp_place(m, u, st, x, y, err, errsz);
        x0 = x; y0 = y; x1 = x + st->w - 1; y1 = y + st->h - 1;
        map_free(st);
        if (!ok) return -1;
    }
    else if (!strcmp(v, "scene")) {
        /* scene NAME: put it back, one step with the rest of the request. */
        int i = scene_find(m, w[1]);
        if (i < 0) BAD("no scene called %.31s", w[1]);
        if (scene_restore(m, u, i, err, errsz) < 0) return -1;
        const Scene *sc = &m->scenes[i];
        if (sc->boxed) {
            /* The box, and any creature of the scene hanging over its edge. */
            x0 = sc->x0; y0 = sc->y0; x1 = sc->x1; y1 = sc->y1;
            for (int k = 0; k < sc->tokens.n; k++) {
                const Token *t = &sc->tokens.v[k];
                x0 = imin(x0, t->x); y0 = imin(y0, t->y);
                x1 = imax(x1, t->x + t->size - 1); y1 = imax(y1, t->y + t->size - 1);
            }
        }
        else { x0 = 0; y0 = 0; x1 = m->w - 1; y1 = m->h - 1; }
        a->last_acting = turn_acting(m);     /* the index is a new creature's: no turn started */
        ed->deleted = 1;                     /* the indices behind are new */
        play_focus(&a->play, -1);
        a->play.visual = 0;
        range_clear(&a->play.range);
    }
    else if (!strcmp(v, "token")) {
        if (token_line(a, w, n, ed, err, errsz) < 0) return -1;
        x1 = -1; x0 = y0 = y1 = 0;           /* token_line has said where */
    }
    else return 1;                           /* not an edit */

    if (x1 >= x0) touched(ed, x0, y0, x1, y1);
    return 0;
}

#undef BAD

/* ------------------------------------------------------------------- run */

/* A request may change this much and no more: twice the largest map's
 * squares. Past it the request is refused and rolled back; it is what
 * bounds the memory one 64 KB request can make the undo log take. */
#define CTL_OPS_MAX (2UL * MAP_MAX_DIM * MAP_MAX_DIM)

/* Lines that change nothing the undo log can take back, so they cannot roll
 * back with the lines around them and go in a request of their own: the
 * agent's undo, and saving or removing a scene. The verb, or NULL. */
static const char *lonely(char w[][CTL_WORD_MAX], int n)
{
    if (n >= 1 && !strcmp(w[0], "undo")) return "undo";
    if (n >= 2 && !strcmp(w[0], "scene") && !strcmp(w[1], "save")) return "scene save";
    if (n == 3 && !strcmp(w[0], "scene") && !strcmp(w[2], "remove")) return "scene NAME remove";
    return NULL;
}

/* A scene put back is an edit; the scene's other lines are not. */
static int scene_is_edit(char w[][CTL_WORD_MAX], int n)
{
    return n == 2 && !strcmp(w[0], "scene") && strcmp(w[1], "save") != 0 && strcmp(w[1], "diff") != 0;
}

static int is_edit(const char *v)
{
    static const char *const EDITS[] = { "room", "area", "door", "corridor", "tile", "wall", "edge", "note", "fog", "token", "stamp", "link", "floor", "scene" };
    for (size_t i = 0; i < sizeof EDITS / sizeof *EDITS; i++)
        if (!strcmp(v, EDITS[i])) return 1;
    return 0;
}

/* Runs one line. 0; -1 with why in err; -2 when an edit is refused because
 * the GM is busy, the reason in err. */
static int run_line(App *a, const char *line, char w[][CTL_WORD_MAX], int n, FILE *out,
                    Edits *ed, char *err, size_t errsz)
{
    Map *m = a->map;
    const char *v = w[0];

    if (!strcmp(v, "scene") && !scene_is_edit(w, n)) {
        /* scene diff NAME, a read; scene save NAME [REGION] and scene NAME
         * remove, each alone in its request. */
        if (n < 2 || (n != 3 && !strcmp(w[1], "diff"))) {
            snprintf(err, errsz, "scene NAME, scene save NAME [REGION], scene NAME remove, or scene diff NAME");
            return -1;
        }
        if (n == 3 && !strcmp(w[1], "diff")) {
            int i = scene_find(m, w[2]);
            if (i < 0) { snprintf(err, errsz, "no scene called %.31s", w[2]); return -1; }
            if (!scene_diff(out, m, i)) fputs("no changes\n", out);
            return 0;
        }
        if (n == 3 && !strcmp(w[2], "off")) { snprintf(err, errsz, "scene %.31s remove throws a scene away", w[1]); return -1; }
        int save = !strcmp(w[1], "save");
        if (save ? (n < 3 || n > 4) : !(n == 3 && !strcmp(w[2], "remove"))) {
            snprintf(err, errsz, "scene NAME, scene save NAME [REGION], scene NAME remove, or scene diff NAME");
            return -1;
        }
        const char *busy = app_ctl_busy(a);
        if (busy) { str_lcpy(err, busy, errsz); return -2; }
        char msg[160];
        if (save) {
            int box[4], boxed = n == 4;
            if (boxed && !region(m, w[3], &box[0], &box[1], &box[2], &box[3], err, errsz)) return -1;
            int i = scene_save(m, w[2], boxed ? box : NULL, err, errsz);
            if (i < 0) return -1;
            char what[64];
            scene_describe(m, i, what, sizeof what);
            fprintf(out, "saved scene \"%s\": %s\n", m->scenes[i].name, what);
            snprintf(msg, sizeof msg, "agent: saved scene %.31s (%s)", m->scenes[i].name, what);
        } else {
            int i = scene_find(m, w[1]);
            if (i < 0) { snprintf(err, errsz, "no scene called %.31s", w[1]); return -1; }
            snprintf(msg, sizeof msg, "agent: removed scene %.31s", m->scenes[i].name);
            fprintf(out, "removed scene \"%s\"\n", m->scenes[i].name);
            scene_remove(m, i);
        }
        app_note_gm(a, msg);
        a->dirty = 1;
        return 0;
    }

    if (is_edit(v)) {
        if (!ed->lines) {
            /* Asked once, at the first edit: after it the open batch is
             * this request's own. */
            const char *busy = app_ctl_busy(a);
            if (busy) { str_lcpy(err, busy, errsz); return -2; }
            undo_begin(&a->undo);
            ed->ops0 = a->undo.stamp;
            while (*line == ' ' || *line == '\t') line++;
            str_lcpy(ed->first, line, sizeof ed->first);
        }
        ed->lines++;
        ed->out = out;
        if (edit_line(a, w, n, ed, err, errsz) < 0) return -1;
        if ((unsigned long)(a->undo.stamp - ed->ops0) > CTL_OPS_MAX) {
            snprintf(err, errsz, "the request changes more than %lu things at once", CTL_OPS_MAX);
            return -1;
        }
        return 0;
    }
    if (!strcmp(v, "undo")) {
        if (n > 1) { snprintf(err, errsz, "undo takes nothing after it"); return -1; }
        if (ed->lines) { snprintf(err, errsz, "undo comes before any edit in a request"); return -1; }
        if (!a->ctl_undoable) { snprintf(err, errsz, "there is no change of the agent's to take back"); return -1; }
        /* The log as the edit left it, and the map: a note or a fog setting
         * the GM changes outside the log still moves Map.gen. */
        if (a->undo.stamp != a->ctl_stamp || m->gen != a->ctl_gen) {
            snprintf(err, errsz, "something has happened since the agent's last change - only the GM's u takes it back now");
            return -1;
        }
        const char *busy = app_ctl_busy(a);
        if (busy) { str_lcpy(err, busy, errsz); return -2; }
        undo_undo(&a->undo, m);
        range_history_changed(&a->play.range, m);
        a->ctl_undoable = 0;
        app_fog_sync(a);
        a->dirty = 1;
        app_note_gm(a, "the agent took back its last change - ctrl-r puts it back");
        fputs("took back the last change\n", out);
        return 0;
    }

    if (!strcmp(v, "status")) {
        if (n > 1) { snprintf(err, errsz, "status takes nothing after it"); return -1; }
        do_status(a, out, ed->lines > 0);
        return 0;
    }
    if (!strcmp(v, "dump")) {
        int x0 = 0, y0 = 0, x1 = m->w - 1, y1 = m->h - 1;
        if (n > 2) { snprintf(err, errsz, "dump takes one region, like B2:K12"); return -1; }
        /* Refused off the map, as an edit is: clipped, a mistyped region
         * would quietly read the wrong squares. */
        if (n == 2 && !region(m, w[1], &x0, &y0, &x1, &y1, err, errsz)) return -1;
        maptools_dump(out, m, x0, y0, x1, y1);
        return 0;
    }
    if (!strcmp(v, "describe")) {
        int j = want_json(w, n, 1, err, errsz);
        if (j < 0) return -1;
        maptools_describe(out, m, j);
        return 0;
    }
    if (!strcmp(v, "stamps")) {
        /* The saved stamps, one a line with its size: what `stamp` can put down. */
        if (n > 1) { snprintf(err, errsz, "stamps takes nothing after it"); return -1; }
        char names[64][MAP_NAME_MAX], e2[160];
        int  k = stamp_list(names, 64);
        for (int i = 0; i < k && i < 64; i++) {
            Map *st = stamp_load(names[i], e2, sizeof e2);
            if (!st) continue;
            fprintf(out, "%s  %dx%d%s\n", names[i], st->w, st->h, st->tokens.n ? "  with creatures" : "");
            map_free(st);
        }
        if (!k) fputs("no stamps\n", out);
        else if (k > 64) fprintf(out, "... and %d more\n", k - 64);
        return 0;
    }
    if (!strcmp(v, "scenes")) {
        /* The map's scenes: what `scene NAME` puts back. */
        if (n > 1) { snprintf(err, errsz, "scenes takes nothing after it"); return -1; }
        char what[64];
        for (int i = 0; i < m->nscenes; i++) {
            scene_describe(m, i, what, sizeof what);
            fprintf(out, "\"%s\"  %s\n", m->scenes[i].name, what);
        }
        if (!m->nscenes) fputs("no scenes\n", out);
        return 0;
    }
    if (!strcmp(v, "characters")) {
        /* The saved character templates: what `token add ... from NAME` places. */
        if (n > 1) { snprintf(err, errsz, "characters takes nothing after it"); return -1; }
        char names[64][MAP_NAME_MAX], e2[160];
        int  k = character_list(names, 64);
        for (int i = 0; i < k && i < 64; i++) {
            Map *c = character_load(names[i], e2, sizeof e2);
            if (!c) { fprintf(out, "%s  (%s)\n", names[i], e2); continue; }
            const Token *t = character_token(c);
            fprintf(out, "%s  \"%s\" %s %dx%d", names[i], t->label, token_kind_name(t->kind), t->size, t->size);
            for (int j = 0; j < t->ncounters; j++)
                fprintf(out, "%s%s %d", j ? ", " : "  ", t->counters[j].name, t->counters[j].max);
            for (int j = 0, r = 0; j < ROLL_MAX; j++)
                if (c->rolls[j].name[0]) fprintf(out, "%s%s = %s", r++ ? ", " : "  rolls ", c->rolls[j].name, c->rolls[j].expr);
            const char *card = card_of(c, t);
            if (card) {
                char first[128];
                card_first_line(card, first, sizeof first);
                fprintf(out, "  card: %s", first);
            }
            fputc('\n', out);
            map_free(c);
        }
        if (!k) fputs("no characters\n", out);
        else if (k > 64) fprintf(out, "... and %d more\n", k - 64);
        return 0;
    }
    if (!strcmp(v, "floors")) {
        /* The floors, highest first, as a building reads top to bottom. */
        if (n > 1) { snprintf(err, errsz, "floors takes nothing after it"); return -1; }
        int order[MAP_AREAS_MAX], nf = floor_order(m, order);
        for (int k = nf - 1; k >= 0; k--) {
            const Area *fa = &m->areas[order[k]];
            char b0[MAP_COORD_MAX], b1[MAP_COORD_MAX];
            map_coord_name(fa->x0, fa->y0, b0, sizeof b0);
            map_coord_name(fa->x1, fa->y1, b1, sizeof b1);
            fprintf(out, "%-16s level %3d  %s:%s%s\n", fa->name, fa->level, b0, b1,
                    order[k] == app_floor_shown(a) ? "  (the GM is looking at it)" : "");
        }
        if (!nf) fputs("no floors\n", out);
        return 0;
    }
    if (!strcmp(v, "links")) {
        /* Every link, one a line, or as JSON: what `link N ...` changes. */
        int j = want_json(w, n, 1, err, errsz);
        if (j < 0) return -1;
        if (j) {
            Json js;
            json_init(&js, out);
            json_open(&js, '[');
            for (int i = 0; i < m->nlinks; i++) {
                const Link *l = &m->links[i];
                char at[2 * MAP_COORD_MAX + 2];
                json_open(&js, '{');
                json_kint(&js, "num", l->num);
                json_kstr(&js, "kind", link_kind_name(l->kind));
                json_kint(&js, "size", l->size);
                link_end_name(l, 0, at, sizeof at); json_kstr(&js, "from", at);
                link_end_name(l, 1, at, sizeof at); json_kstr(&js, "to", at);
                json_key(&js, "oneway"); json_bool(&js, l->oneway);
                json_key(&js, "secret"); json_bool(&js, l->secret);
                json_close(&js, '}');
            }
            json_close(&js, ']');
            fputc('\n', out);
            return 0;
        }
        for (int i = 0; i < m->nlinks; i++) {
            char row[128];
            link_describe(&m->links[i], row, sizeof row);
            fprintf(out, "%s\n", row);
        }
        if (!m->nlinks) fputs("no links\n", out);
        return 0;
    }
    if (!strcmp(v, "marked")) {
        int j = want_json(w, n, 1, err, errsz);
        if (j < 0) return -1;
        app_ctl_marked(a, out, j);
        return 0;
    }
    if (!strcmp(v, "check")) {
        int j = want_json(w, n, 1, err, errsz);
        if (j < 0) return -1;
        maptools_check_map(out, m, j);
        return 0;
    }
    snprintf(err, errsz, "unknown request %.40s", v);
    return -1;
}

/* 0 when the request holds `undo` and any other line. */
/* The first line that must go alone, when the request holds more than one
 * line; NULL when it is fine. */
static const char *not_alone(const char *p)
{
    int lines = 0;
    const char *verb = NULL;
    while (*p) {
        const char *end = strchr(p, '\n');
        size_t      ll  = end ? (size_t)(end - p) : strlen(p);
        char line[1024], w[CTL_WORDS][CTL_WORD_MAX], e[200];
        int  n = -1;
        if (ll < sizeof line) {
            memcpy(line, p, ll);
            line[ll] = '\0';
            n = is_comment(line) ? 0 : split_words(line, w, e, sizeof e);
        }
        if (n != 0 && !(n > 0 && w[0][0] == '#')) {
            lines++;
            if (n > 0 && !verb) verb = lonely(w, n);
        }
        p = end ? end + 1 : p + ll;
    }
    return lines > 1 ? verb : NULL;
}

/* A request's edits are in: close the batch and tell both sides. */
static void finish_edits(App *a, Edits *ed, FILE *out)
{
    int changed = a->undo.stamp != ed->ops0;     /* a stamp a recorded op */
    undo_end(&a->undo);
    char area[2 * MAP_COORD_MAX + 2] = "";
    if (ed->x1 >= ed->x0) map_region_name(ed->x0, ed->y0, ed->x1, ed->y1, area, sizeof area);
    if (!changed) {
        fprintf(out, "no change: the map already looked like that\n");
        return;
    }
    a->ctl_stamp    = a->undo.stamp;
    a->ctl_gen      = a->map->gen;
    a->ctl_undoable = 1;
    fprintf(out, "changed %s: %d line%s, one undo step\n", area, ed->lines, ed->lines == 1 ? "" : "s");

    char msg[160];
    if (ed->lines == 1) snprintf(msg, sizeof msg, "agent: %s - u takes it back", ed->first);
    else snprintf(msg, sizeof msg, "agent: %s and %d more - u takes them back", ed->first, ed->lines - 1);
    app_note_gm(a, msg);
    if (ed->x1 >= ed->x0) {
        Ping *r = &a->agent_ring;
        r->who = PING_GM;
        r->x0 = ed->x0; r->y0 = ed->y0; r->x1 = ed->x1; r->y1 = ed->y1;
        r->until_ms = a->now_ms + PING_SHOW_MS;
        if (!r->until_ms) r->until_ms = 1;
    }
}

char *app_ctl_exec(App *a, const char *req, size_t *len)
{
    PROF_ZONE("ctl");
    char  *body = NULL;
    size_t blen = 0;
    FILE  *out  = open_memstream(&body, &blen);
    if (!out) return NULL;

    char  verdict[320] = "ok";
    int   lineno = 0;
    Edits ed;
    memset(&ed, 0, sizeof ed);
    ed.x1 = -1;
    const char *p = req;
    if (strlen(req) != *len) {
        snprintf(verdict, sizeof verdict, "error: a nul byte in the request");
        p = "";
    }
    /* `undo`, and saving or removing a scene, go alone: they cannot be
     * rolled back with the lines around them, so a request that holds one
     * holds nothing else. */
    const char *lone = strcmp(verdict, "ok") == 0 ? not_alone(p) : NULL;
    if (lone) snprintf(verdict, sizeof verdict, "error: %s goes in a request of its own", lone);
    if (strcmp(verdict, "ok") != 0) p = "";
    while (*p) {
        const char *end = strchr(p, '\n');
        size_t      ll  = end ? (size_t)(end - p) : strlen(p);
        lineno++;

        char line[1024], err[200] = "";
        char w[CTL_WORDS][CTL_WORD_MAX];
        int  n = -1;
        if (ll >= sizeof line) snprintf(err, sizeof err, "the line is over %zu characters", sizeof line - 1);
        else {
            memcpy(line, p, ll);
            line[ll] = '\0';
            /* A comment is prose: never split, so its length and its
             * quotes are nobody's business. */
            n = is_comment(line) ? 0 : split_words(line, w, err, sizeof err);
        }
        p = end ? end + 1 : p + ll;

        if (n == 0 || (n > 0 && w[0][0] == '#')) continue;
        if (n > 0 && !a->map && strcmp(w[0], "status") != 0) snprintf(err, sizeof err, "no map is open");
        int rc = err[0] ? -1 : run_line(a, line, w, n, out, &ed, err, sizeof err);
        if (rc == 0) continue;
        if (rc == -2) snprintf(verdict, sizeof verdict, "busy: %s", err);
        else          snprintf(verdict, sizeof verdict, "error: line %d: %s", lineno, err);
        break;
    }

    if (ed.lines) {
        if (strcmp(verdict, "ok") != 0) {
            undo_abort(&a->undo, a->map);                 /* all or nothing */
            /* The overlay followed the deletion; the creature is back. */
            if (ed.deleted) range_clear(&a->play.range);
        }
        else finish_edits(a, &ed, out);
        app_fog_sync(a);
        a->dirty = 1;
    }
    fclose(out);

    /* The verdict, then the report; an error sends only what it says. */
    int    ok = !strcmp(verdict, "ok");
    size_t vl = strlen(verdict);
    size_t total = vl + 1 + (ok ? blen : 0);
    char  *ans = malloc(total + 1);
    if (!ans) { free(body); return NULL; }
    memcpy(ans, verdict, vl);
    ans[vl] = '\n';
    if (ok && blen) memcpy(ans + vl + 1, body, blen);
    ans[total] = '\0';
    free(body);
    *len = total;
    return ans;
}

/* ------------------------------------------------------------- :agent */

void app_agent_command(App *a, const char *rest)
{
    Ctl *c = &a->ctl;
    char msg[CTL_PATH_MAX + 64];
    if (!*rest) {
        if (!ctl_active(c)) app_set_status_gm(a, "the agent channel is off - :agent on opens it");
        else {
            snprintf(msg, sizeof msg, "agent channel on at %s - %u request%s so far", c->path,
                     c->requests, c->requests == 1 ? "" : "s");
            app_set_status_gm(a, msg);
        }
        return;
    }
    if (!strcmp(rest, "off")) {
        if (!ctl_active(c)) { app_set_status_gm(a, "the agent channel is already off"); return; }
        ctl_stop(c);
        app_note_gm(a, "agent channel off");
        return;
    }
    if (strcmp(rest, "on") != 0) { app_set_status_gm(a, ":agent on, :agent off, or :agent to ask"); return; }
    if (ctl_active(c)) {
        snprintf(msg, sizeof msg, "the agent channel is already on at %s", c->path);
        app_set_status_gm(a, msg);
        return;
    }
    char err[CTL_PATH_MAX + 64];
    if (ctl_start(c, err, sizeof err) < 0) { app_set_status_gm(a, err); return; }
    snprintf(msg, sizeof msg, "agent channel on - vtt --ctl talks to this map; u undoes what it does");
    app_note_gm(a, msg);
}
