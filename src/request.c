/* The control channel's requests: an agent reading, and editing, the map the
 * GM has open. ctl.c moves the bytes; this reads the lines and runs them
 * against a World (world.h; docs/V2.md, D2), asking the world's host for
 * whatever is the GM's screen and never the app itself. The language and its
 * rules are docs/CONTROL.md's. */

#include <stdarg.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include "request.h"
#include "world_ops.h"
#include "editor.h"
#include "turn.h"
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

/* ------------------------------------------------------- a refusal's class */

/* The class of the line being run (ctl.h's CTL_E_*): `usage` until a site
 * says another, since most refusals are a line written wrong. One a
 * request at a time, and the helpers that refuse (`square`, `fits`) take a
 * map, not the app, so it is kept here and not handed down to each. */
static int g_class;

/* Returns -1, so a site passing on a lower module's refusal can return it. */
static int rq_class(int cls)
{
    g_class = cls;
    return -1;
}

int request_class(int cls) { return rq_class(cls); }

/* The squares of map this request's reads have walked, and its edits have
 * covered (ctl.h, the budget). */
static long g_read, g_cover;
static int  g_changes;                 /* `changes` lines so far */

/* An edit is about to cover this many squares: 0, with why in err, past
 * CTL_EDIT_SQUARES. */
static int rq_cover(const World *world, long squares, char *err, size_t errsz)
{
    if (world->ctl_direct) return 1;
    g_cover += squares;
    if (g_cover <= CTL_EDIT_SQUARES) return 1;
    rq_class(CTL_E_LIMIT);
    snprintf(err, errsz, "the request's edits cover more than %d squares, the largest map twice over: send them in more requests",
             CTL_EDIT_SQUARES);
    return 0;
}

int request_read(const World *world, long squares, char *err, size_t errsz)
{
    /* --apply on a file has no GM waiting on a frame. */
    if (world->ctl_direct) return 1;
    g_read += squares;
    if (g_read <= CTL_READ_SQUARES) return 1;
    rq_class(CTL_E_LIMIT);
    snprintf(err, errsz, "the request reads more than %d squares of map, which is one whole read of the largest: "
                         "send its reads one a request, or by region", CTL_READ_SQUARES);
    return 0;
}

/* The one place a refusal's first line is written: `error CLASS: why`. */
static void refuse(char *verdict, size_t sz, int cls, const char *fmt, ...) __attribute__((format(printf, 4, 5)));
static void refuse(char *verdict, size_t sz, int cls, const char *fmt, ...)
{
    int k = snprintf(verdict, sz, "error %s: ", ctl_class_name(cls));
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(verdict + k, sz - (size_t)k, fmt, ap);
    va_end(ap);
}

/* ----------------------------------------------------------------- words */

/* A line split into words: spaces between, "..." one word with \" and \\
 * inside. Returns how many, or -1 with why in err. */
static int split_words(const char *line, char w[CTL_WORDS][CTL_WORD_MAX], char *err, size_t errsz)
{
    int n = 0;
    const char *p = line;
    for (;;) {
        while (*p == ' ' || *p == '\t' || *p == '\r') p++;
        if (!*p) break;
        if (n == CTL_WORDS) { rq_class(CTL_E_LIMIT), snprintf(err, errsz, "more than %d words", CTL_WORDS); return -1; }
        size_t k = 0;
        if (*p == '"') {
            p++;
            while (*p && *p != '"') {
                if (*p == '\\' && (p[1] == '"' || p[1] == '\\')) p++;
                if (k + 1 == CTL_WORD_MAX) { rq_class(CTL_E_LIMIT), snprintf(err, errsz, "a word over %d characters", CTL_WORD_MAX - 1); return -1; }
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
                if (k + 1 == CTL_WORD_MAX) { rq_class(CTL_E_LIMIT), snprintf(err, errsz, "a word over %d characters", CTL_WORD_MAX - 1); return -1; }
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

/* ----------------------------------------------------------------- reads */

/* `own`: this request has edits of its own so far. Asked of the live map
 * and log (live_swap, mid-proposal). */
static void do_status(World *world, FILE *out, int own)
{
    const Map  *m = world->map;
    const Undo *u = &world->undo;
    if (m) {
        fprintf(out, "map %s  %dx%d\n", m->name, m->w, m->h);
        fprintf(out, "file %s%s\n", m->path[0] ? m->path : "(never saved)",
                m->modified ? "  unsaved changes" : "");
    }
    else fputs("map none open\n", out);
    world->host->screen_lines(world->gm, out);       /* the screen and mode, and the floor on it */
    fprintf(out, "undo %d back, %d forward%s\n", u->depth, u->nmarks - u->depth,
            !own ? "" : world->ctl_direct ? ", and this request's changes one more" : ", and this request's changes proposed");
    const char *busy = world->host->busy(world->gm);
    if (world->ctl_direct || !m)
        fprintf(out, "edits %s%s\n", busy && !own ? "not now: " : "taken", busy && !own ? busy : "");
    else if (!world->ctl_auto) fputs("edits proposed, for the GM to review\n", out);
    else if (busy)         fprintf(out, "edits proposed, landing when the GM is back: %s\n", busy);
    else                   fputs("edits land at once (:agent accept auto)\n", out);
    /* The caller's name as vtt took it: a mistyped one shows here, not in whose job a change became. */
    fprintf(out, "you %s\n", world->ctl_who[0] ? world->ctl_who : CTL_NAME_NONE);
    fprintf(out, "agent %s\n", world->agent_on ? "on" : "off");
    /* The newest event's number: what a first `wait` is given. */
    fprintf(out, "seq %u\n", world->event_seq);
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

    /* A proposal: the edits run on the scratch map, with the live map and
     * its log set aside here until the request ends. */
    int      proposal;
    Map     *live;
    Undo     live_undo;
    int      header;                  /* a `propose` or `job N propose` line came */
    int      from;                    /* JobFrom of a new job: an agent, or --apply */
    int      job;                     /* the job it answers, or -1 for a new one */
    char     say[JOB_TEXT_MAX];       /* the line it came with */
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

int request_region(const Map *m, const char *w, int *x0, int *y0, int *x1, int *y1,
                   char *err, size_t errsz)
{
    return region(m, w, x0, y0, x1, y1, err, errsz);
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
        if (i < 0) rq_class(CTL_E_MISSING), snprintf(err, errsz, "no creature stands on %.40s", w);
        return i;
    }
    rq_class(nloose > 1 ? CTL_E_USAGE : CTL_E_MISSING), snprintf(err, errsz, nloose > 1 ? "more than one creature is called %.40s" : "no creature called %.40s", w);
    return -1;
}

/* Can a creature `size` wide stand at (x,y): on the map, on ground, and on
 * nobody else (`skip` is the creature itself, when it is moving)? */
static int fits(const Map *m, int x, int y, int size, int skip, char *err, size_t errsz)
{
    char at[MAP_COORD_MAX];
    map_coord_name(x, y, at, sizeof at);
    if (x + size > m->w || y + size > m->h) {
        rq_class(CTL_E_CONFLICT), snprintf(err, errsz, "%dx%d at %s hangs off the map", size, size, at);
        return 0;
    }
    for (int yy = y; yy < y + size; yy++)
        for (int xx = x; xx < x + size; xx++)
            if (!map_walkable(m, xx, yy)) {
                char v[MAP_COORD_MAX];
                map_coord_name(xx, yy, v, sizeof v);
                rq_class(CTL_E_CONFLICT), snprintf(err, errsz, "%s is void - a creature needs ground", v);
                return 0;
            }
    int other = tokens_overlapping(&m->tokens, x, y, size, skip, TOKEN_ANY_KIND);
    if (other >= 0) {
        const Token *t = &m->tokens.v[other];
        rq_class(CTL_E_CONFLICT), snprintf(err, errsz, "%s is taken by %.30s", at, t->label[0] ? t->label : "a creature");
        return 0;
    }
    return 1;
}

static int label_ok(const Map *m, const char *label, int skip, char *err, size_t errsz)
{
    if (!label[0]) { snprintf(err, errsz, "a creature needs a label"); return 0; }
    if (strlen(label) >= TOKEN_LABEL_MAX) {
        rq_class(CTL_E_LIMIT), snprintf(err, errsz, "the label is over %d characters", TOKEN_LABEL_MAX - 1);
        return 0;
    }
    if (tokens_find_label(&m->tokens, label, skip) >= 0) {
        rq_class(CTL_E_CONFLICT), snprintf(err, errsz, "there is already a creature called %.30s", label);
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
    int  cls = g_class;                 /* `fits` classes each square it turns down: none of them is the answer */
    for (int yy = ar->y0; yy + size - 1 <= ar->y1; yy++)
        for (int xx = ar->x0; xx + size - 1 <= ar->x1; xx++) {
            long d = (2L * xx - mx) * (2L * xx - mx) + (2L * yy - my) * (2L * yy - my);
            if (best >= 0 && d >= best) continue;
            if (!fits(m, xx, yy, size, skip, e2, sizeof e2)) continue;
            best = d; *x = xx; *y = yy;
        }
    g_class = cls;
    if (best < 0) rq_class(CTL_E_CONFLICT), snprintf(err, errsz, "no room in %.30s for a %dx%d creature", ar->name, size, size);
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
    if (best < 0) rq_class(CTL_E_CONFLICT), snprintf(err, errsz, "no room in %.30s for a %dx%d link end", ar->name, size, size);
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
#define BADC(c, ...) do { rq_class(c); snprintf(err, errsz, __VA_ARGS__); return -1; } while (0)
#define COVER(sq) do { if (!rq_cover(world, (sq), err, errsz)) return -1; } while (0)

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
        if (oi < 0) { rq_class(CTL_E_MISSING), snprintf(err, errsz, "no room called %.40s", w[5]); return 0; }
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
        rq_class(CTL_E_CONFLICT), snprintf(err, errsz, "%.30s, %dx%d, would run off the %s side of the map", *name,
                 *x1 - *x0 + 1, *y1 - *y0 + 1,
                 *x0 < 0 ? "west" : *y0 < 0 ? "north" : *x1 >= m->w ? "east" : "south");
        return 0;
    }
    return 1;
}

static int token_line(World *world, char w[][CTL_WORD_MAX], int n, Edits *ed, char *err, size_t errsz)
{
    Map  *m = world->map;
    Undo *u = &world->undo;
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
        if (!request_read(world, CTL_FILE_CHARGE, err, errsz)) return -1;      /* a file read */
        Map *c = character_load(w[5], err, errsz);
        if (!c) return rq_class(CTL_E_MISSING);
        const Token *ct = character_token(c);
        char label[TOKEN_LABEL_MAX], said[160];
        tokens_unique_label(&m->tokens, ct->label, label, sizeof label);
        int x, y, idx = -1;
        if (!label[0]) rq_class(CTL_E_STATE), snprintf(err, errsz, "%.40s has no label, and the channel names creatures by label", w[5]);
        else if (spot(m, w[3], ct->size, -1, &x, &y, err, errsz) &&
                 (idx = character_place(m, u, c, kind, x, y, n == 7, said, sizeof said, err, errsz)) < 0)
            rq_class(CTL_E_CONFLICT);
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
        if (t.turn & TURN_ACTING) BADC(CTL_E_STATE, "%.30s holds the turn - the GM passes it on first", t.label);
        undo_del_token(u, m, i);
        turn_settle(m, u);
        if (!ed->proposal) {                 /* the live map's: an accept does this */
            world->host->changed(world->gm, WORLD_TOKEN_GONE, i, t.x, t.y);
            ed->deleted = 1;
        }
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
            if (strlen(w[4]) >= TOKEN_NOTE_MAX) BADC(CTL_E_LIMIT, "the note is over %d characters", TOKEN_NOTE_MAX - 1);
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
static int edit_line(World *world, char w[][CTL_WORD_MAX], int n, Edits *ed, char *err, size_t errsz)
{
    Map  *m = world->map;
    Undo *u = &world->undo;
    const char *v = w[0];
    int x0, y0, x1, y1;
    WorldErr we;

    if (!strcmp(v, "room")) {
        const char *name = NULL;
        if (!room_box(m, w, n, &name, &x0, &y0, &x1, &y1, err, errsz)) return -1;
        for (int y = y0; y <= y1; y++)
            for (int x = x0; x <= x1; x++) undo_set_tile(u, m, x, y, TILE_FLOOR);
        if (name && world_area_fits(world, name, x0, y0, x1, y1, &we) < 0) BADC(we.cls, "%s", we.text);
        COVER((long)(x1 - x0 + 1) * (y1 - y0 + 1));
        outline(u, m, x0, y0, x1, y1, EDGE_WALL);
        if (name && world_area_set(world, name, x0, y0, x1, y1, &we) < 0) BADC(we.cls, "%s", we.text);
    }
    else if (!strcmp(v, "area")) {
        /* area NAME REGION, area NAME remove: a name, nothing drawn. */
        if (n != 3) BAD("area NAME REGION, or area NAME remove");
        if (!strcmp(w[2], "off")) BAD("area %.40s remove takes the name off", w[1]);
        if (world_area_name(w[1], &we) < 0) BADC(we.cls, "%s", we.text);
        if (!strcmp(w[2], "remove")) {
            int b[4];
            if (world_area_remove(world, w[1], b, &we) < 0) BADC(we.cls, "%s", we.text);
            x0 = b[0]; y0 = b[1]; x1 = b[2]; y1 = b[3];
        } else {
            if (!region(m, w[2], &x0, &y0, &x1, &y1, err, errsz)) return -1;
            if (world_area_set(world, w[1], x0, y0, x1, y1, &we) < 0) BADC(we.cls, "%s", we.text);
        }
    }
    else if (!strcmp(v, "corridor")) {
        /* corridor A B [width N] [KIND]: dug through void between two named
         * rooms, walled along, KIND at both ends (a door when one wide,
         * open when wider). */
        int width = 1, k = -1;
        if (n < 3) BAD("corridor ROOM ROOM [width 1-3] [KIND]");
        int ia = map_area_find(m, w[1]), ib = map_area_find(m, w[2]);
        if (ia < 0) BADC(CTL_E_MISSING, "no room called %.40s", w[1]);
        if (ib < 0) BADC(CTL_E_MISSING, "no room called %.40s", w[2]);
        if (ia == ib) BAD("a corridor joins two rooms");
        for (int i = 3; i < n; i++) {
            if (!strcmp(w[i], "width") && i + 1 < n && word_int(w[i + 1], 1, 3, &width)) i++;
            else if ((k = edge_kind(w[i])) < 0) BAD("corridor ROOM ROOM [width 1-3] [KIND]");
        }
        if (k < 0) k = width == 1 ? EDGE_DOOR_CLOSED : EDGE_NONE;
        Corridor c;
        if (corridor_plan(m, &m->areas[ia], &m->areas[ib], width, &c, err, errsz) < 0) return rq_class(CTL_E_CONFLICT);
        for (int i = 0; i < c.nleg; i++) COVER((long)(c.leg[i].x1 - c.leg[i].x0 + 1) * (c.leg[i].y1 - c.leg[i].y0 + 1));
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
        if (ai < 0) BADC(CTL_E_MISSING, "no room called %.40s - room NAME ... names one", w[1]);
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
        COVER((long)(x1 - x0 + 1) * (y1 - y0 + 1));
        for (int y = y0; y <= y1; y++)
            for (int x = x0; x <= x1; x++) undo_set_tile(u, m, x, y, (uint8_t)k);
    }
    else if (!strcmp(v, "wall")) {
        int k = EDGE_WALL;
        if (n != 2 && n != 3) BAD("wall REGION [KIND]");
        if (!region(m, w[1], &x0, &y0, &x1, &y1, err, errsz)) return -1;
        if (n == 3 && (k = edge_kind(w[2])) < 0)
            BAD("%.20s: a boundary is none, wall, door, open, window, secret or opensecret", w[2]);
        COVER(2L * (x1 - x0 + 1) + 2L * (y1 - y0 + 1));      /* its outline */
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
        if (strlen(text) >= NOTE_MAX) BADC(CTL_E_LIMIT, "the note is over %d characters", NOTE_MAX - 1);
        if (!undo_set_note(u, m, x, y, text)) BADC(CTL_E_LIMIT, "no room: a map holds %d notes on squares", MAP_NOTES_MAX);
        x0 = x1 = x; y0 = y1 = y;
    }
    else if (!strcmp(v, "fog")) {
        int id;
        if (n != 4 || strcmp(w[1], "paint") != 0) BAD("fog paint REGION N (0 scrubs)");
        if (!region(m, w[2], &x0, &y0, &x1, &y1, err, errsz)) return -1;
        if (!word_int(w[3], 0, FOG_PATCH_MAX, &id)) BAD("a fog patch is 1 to %d, or 0 to scrub", FOG_PATCH_MAX);
        if (id && (!m->fog_patches[id - 1].name[0] || m->fog_patches[id - 1].dead))
            BADC(CTL_E_MISSING, "there is no fog patch %d - :fog makes one", id);
        COVER((long)(x1 - x0 + 1) * (y1 - y0 + 1));
        for (int y = y0; y <= y1; y++)
            for (int x = x0; x <= x1; x++) fog_paint(m, u, x, y, id);
    }
    else if (!strcmp(v, "floor")) {
        /* floor NAME LEVEL, floor NAME off */
        if (n != 3) BAD("floor NAME LEVEL, or floor NAME off");
        int ai = map_area_find(m, w[1]);
        if (ai < 0) BADC(CTL_E_MISSING, "no area called %.40s - area NAME REGION names one", w[1]);
        const Area *ar = &m->areas[ai];
        if (!strcmp(w[2], "off")) {
            if (!ar->floor) BADC(CTL_E_STATE, "%.40s is not a floor", w[1]);
            undo_set_floor(u, m, ar->name, 0, 0);
        } else {
            int level;
            if (!word_int(w[2], FLOOR_LEVEL_MIN, FLOOR_LEVEL_MAX, &level))
                BAD("%.20s: a level is %d to %d, or off", w[2], FLOOR_LEVEL_MIN, FLOOR_LEVEL_MAX);
            const char *why = floor_problem(m, ai);
            if (why) BADC(CTL_E_CONFLICT, "%.40s cannot be a floor: %s", w[1], why);
            undo_set_floor(u, m, ar->name, 1, level);
        }
        x0 = ar->x0; y0 = ar->y0; x1 = ar->x1; y1 = ar->y1;
    }
    else if (!strcmp(v, "env")) {
        /* env AREA CARD, env AREA remove: the area's environment, a card's
         * name (docs/ENVIRONMENTS.md). The map's own is the GM's (:env map). */
        if (n != 3) BAD("env AREA CARD, or env AREA remove");
        int ai = map_area_find(m, w[1]);
        if (ai < 0) BADC(CTL_E_MISSING, "no area called %.40s - area NAME REGION names one", w[1]);
        const Area *ar = &m->areas[ai];
        if (!strcmp(w[2], "remove")) {
            if (!ar->env[0]) BADC(CTL_E_STATE, "%.40s has no environment", w[1]);
            undo_set_area_env(u, m, ar->name, "");
        } else {
            if (!card_name_ok(w[2])) BAD("%.40s: a card's name is letters, digits, - and _", w[2]);
            undo_set_area_env(u, m, ar->name, w[2]);
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
                BADC(CTL_E_MISSING, "there is no link %.10s", w[1]);
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
            if (!l.num) BADC(CTL_E_LIMIT, "the map holds %d links", MAP_LINKS_MAX);
            if (n < 5) BAD("link SQUARE to MAP PLACE [KIND] [size N] [secret] - PLACE an area or a square in MAP");
            l.size = 1;
            for (int i = 5; i < n; i++) {
                int k = link_kind_from_name(w[i]), sz;
                if (k >= 0) l.kind = (uint8_t)k;
                else if (!strcmp(w[i], "secret")) l.secret = 1;
                else if (!strcmp(w[i], "size") && i + 1 < n && word_int(w[i + 1], 1, LINK_SIZE_MAX, &sz)) { l.size = (uint8_t)sz; i++; }
                else BAD("%.20s: after the place goes a kind, size N or secret", w[i]);
            }
            if (strlen(w[4]) >= AREA_NAME_MAX) BADC(CTL_E_LIMIT, "no area has a name that long");
            char why[200];
            int  kind;
            if (link_map_check(m, w[3], w[4], why, sizeof why, &kind))
                BADC(kind == LINK_MAP_NAME ? CTL_E_USAGE : kind == LINK_MAP_UNSAVED ? CTL_E_STATE :
                     kind == LINK_MAP_VOID ? CTL_E_CONFLICT : CTL_E_MISSING, "%s", why);
            str_lcpy(l.to_map, w[3], sizeof l.to_map);
            str_lcpy(l.to_place, w[4], sizeof l.to_place);
            int ax, ay;
            if (!link_spot(m, w[1], l.size, -1, -1, &ax, &ay, err, errsz)) return -1;
            l.x[0] = l.x[1] = (int16_t)ax; l.y[0] = l.y[1] = (int16_t)ay;
            first = n;
        } else {
            l.num = (uint8_t)link_free_num(m);
            if (!l.num) BADC(CTL_E_LIMIT, "the map holds %d links", MAP_LINKS_MAX);
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
                BADC(CTL_E_STATE, "link %d leads to another map: it has one end here, and goes one way", l.num);
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
        if (why) BADC(CTL_E_CONFLICT, "%s", why);
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
        if (!request_read(world, CTL_FILE_CHARGE, err, errsz)) return -1;      /* a file read */
        Map *st = stamp_load(w[1], err, errsz);
        if (!st) return rq_class(CTL_E_MISSING);
        if (!rq_cover(world, (long)st->w * st->h, err, errsz)) { map_free(st); return -1; }
        if (quarters) { Map *t = stamp_turned(st, quarters); map_free(st); st = t; }
        if (mirror)   { Map *t = stamp_mirrored(st); map_free(st); st = t; }
        int full, ok = stamp_place(m, u, st, x, y, &full, err, errsz);
        x0 = x; y0 = y; x1 = x + st->w - 1; y1 = y + st->h - 1;
        map_free(st);
        /* Refused: something in the way, or the map full of links or notes. */
        if (!ok) return rq_class(full ? CTL_E_LIMIT : CTL_E_CONFLICT);
    }
    else if (!strcmp(v, "scene")) {
        /* scene NAME: put it back, one step with the rest of the request. */
        int i = scene_find(m, w[1]);
        if (i < 0) BADC(CTL_E_MISSING, "no scene called %.31s", w[1]);
        if (scene_restore(m, u, i, err, errsz) < 0) return rq_class(CTL_E_CONFLICT);
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
        if (!ed->proposal) {
            world->host->changed(world->gm, WORLD_RENUMBERED, 0, 0, 0);
            ed->deleted = 1;                 /* the indices behind are new */
        }
    }
    else if (!strcmp(v, "token")) {
        if (token_line(world, w, n, ed, err, errsz) < 0) return -1;
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

/* A scene put back is an edit; the scene's other lines are not. */
static int scene_is_edit(char w[][CTL_WORD_MAX], int n)
{
    return n == 2 && !strcmp(w[0], "scene") && strcmp(w[1], "save") != 0 && strcmp(w[1], "diff") != 0;
}

/* A proposal's edits run on a copy of the live map with a log of their
 * own: the live map, its log (the GM's redo tail too), `modified` and
 * Map.gen never move for a change nobody has accepted (docs/CONFLICTS.md,
 * "Why not run it live and roll it back"). */
static void proposal_begin(World *world, Edits *ed)
{
    world_scratch_copy(world);
    ed->proposal  = 1;
    ed->live      = world->map;
    ed->live_undo = world->undo;
    world->undo = world->ctl_sundo;
    world->map  = world->ctl_scratch;
    undo_begin(&world->undo);
    ed->ops0 = world->undo.stamp;
}

/* A read about the GM's side (status, marked) in the middle of a proposal:
 * the live map and log in for its length, and out again (call it twice). */
static void live_swap(World *world, Edits *ed)
{
    if (!ed->proposal) return;
    Map *m = world->map;   world->map  = ed->live;      ed->live      = m;
    Undo u = world->undo;  world->undo = ed->live_undo; ed->live_undo = u;
}

/* What a request that landed says, and the ring round it on the GM's screen. */
static void say_changed(World *world, const Edits *ed, FILE *out)
{
    char area[2 * MAP_COORD_MAX + 2] = "";
    if (ed->x1 >= ed->x0) map_region_name(ed->x0, ed->y0, ed->x1, ed->y1, area, sizeof area);
    fprintf(out, "changed %s: %d line%s, one undo step\n", area, ed->lines, ed->lines == 1 ? "" : "s");
    if (ed->x1 < ed->x0) return;
    world->host->ring(world->gm, ed->x0, ed->y0, ed->x1, ed->y1);
}

/* The live map back, the scratch log kept beside it for the diff. */
static void proposal_end(World *world, Edits *ed)
{
    undo_end(&world->undo);
    world->ctl_sundo = world->undo;
    world->undo = ed->live_undo;
    world->map  = ed->live;
}

/* The request's edits are in, on the scratch map: their change set becomes a
 * job's proposal, landing at once under :agent accept auto. -1 with why in
 * err when there is no room for it. */
static int finish_proposal(World *world, Edits *ed, FILE *out, char *err, size_t errsz)
{
    ChangeSet cs;
    cs_init(&cs);
    int slot = ed->job;
    {
        PROF_ZONE("job.propose");
        if (!cs_diff(&cs, world->map, world->ctl_scratch, &world->ctl_sundo)) {
            cs_free(&cs);
            fputs("no change: the map already looked like that\n", out);
            return 0;
        }
        /* At once is :agent on's (a request not from the socket is not asked). */
        int at_once = world->ctl_auto && (world->agent_on || !world->ctl_can_hold);
        if (slot < 0) slot = world_job_new(world, ed->from, ed->say, NULL, at_once);
        if (slot < 0) {
            cs_free(&cs);
            rq_class(CTL_E_LIMIT), snprintf(err, errsz, "%d changes are waiting for the GM - nothing more until some are reviewed", JOB_MAX);
            return -1;
        }
        Job *j = &world->jobs[slot];
        if (j->from != JOB_FROM_APPLY) request_job_claim(world, j);    /* --apply's is nobody's: the GM's alone to decide */
        if (at_once) j->at_once = 1;               /* everything that comes in */
        world_job_set_proposal(world, slot, &cs, ed->job >= 0 ? ed->say : NULL);
    }
    Job *j = &world->jobs[slot];
    fprintf(out, "proposal #%d: %s\n", j->num, j->summary);
    if (j->state == JOB_ACCEPTED) say_changed(world, ed, out);
    else if (j->at_once) fprintf(out, "lands when the GM is back: %s\n", world->host->busy(world->gm));
    else fprintf(out, "waiting for the GM's review - :review %d\n", j->num);
    return 0;
}

/* What every request's handler is given: the app, the line as written and in
 * words, where its report goes, the request's edits so far, and where to say
 * why not. A handler returns 0; -1 with why in err; -2 when an edit is
 * refused because the GM is busy, the reason in err. */
#define RQ_ARGS World *world, const char *line, char w[][CTL_WORD_MAX], int n, FILE *out, Edits *ed, char *err, size_t errsz
#define RQ_PASS world, line, w, n, out, ed, err, errsz
/* Not every handler reads all of them. */
#define RQ_LOCALS Map *m = world->map; const char *v = w[0]; (void)m; (void)v; (void)line; (void)n; (void)out; (void)ed; (void)err; (void)errsz

static int rq_scene_other(RQ_ARGS)
{
    RQ_LOCALS;
    /* scene diff NAME, a read; scene save NAME [REGION] and scene NAME
     * remove, each alone in its request. */
    if (n < 2 || (n != 3 && !strcmp(w[1], "diff"))) {
        snprintf(err, errsz, "scene NAME, scene save NAME [REGION], scene NAME remove, or scene diff NAME");
        return -1;
    }
    if (n == 3 && !strcmp(w[1], "diff")) {
        int i = scene_find(m, w[2]);
        if (i < 0) { rq_class(CTL_E_MISSING), snprintf(err, errsz, "no scene called %.31s", w[2]); return -1; }
        /* Every creature of the scene against every one of the map's. */
        if (!request_read(world, (long)m->scenes[i].tokens.n * m->tokens.n / 8 + 1, err, errsz)) return -1;
        if (!scene_diff(out, m, i)) fputs("no changes\n", out);
        return 0;
    }
    if (n == 3 && !strcmp(w[2], "off")) { snprintf(err, errsz, "scene %.31s remove throws a scene away", w[1]); return -1; }
    int save = !strcmp(w[1], "save");
    if (save ? (n < 3 || n > 4) : !(n == 3 && !strcmp(w[2], "remove"))) {
        snprintf(err, errsz, "scene NAME, scene save NAME [REGION], scene NAME remove, or scene diff NAME");
        return -1;
    }
    const char *busy = world->host->busy(world->gm);
    if (busy) { str_lcpy(err, busy, errsz); return -2; }
    char msg[160];
    if (save) {
        int box[4], boxed = n == 4;
        if (boxed && !region(m, w[3], &box[0], &box[1], &box[2], &box[3], err, errsz)) return -1;
        int i = scene_save(m, w[2], boxed ? box : NULL, err, errsz);
        if (i < 0) return rq_class(scene_name_ok(w[2]) ? CTL_E_LIMIT : CTL_E_USAGE);     /* its two refusals: the name, or no room */
        char what[64];
        scene_describe(m, i, what, sizeof what);
        fprintf(out, "saved scene \"%s\": %s\n", m->scenes[i].name, what);
        snprintf(msg, sizeof msg, "agent: saved scene %.31s (%s)", m->scenes[i].name, what);
    } else {
        int i = scene_find(m, w[1]);
        if (i < 0) { rq_class(CTL_E_MISSING), snprintf(err, errsz, "no scene called %.31s", w[1]); return -1; }
        snprintf(msg, sizeof msg, "agent: removed scene %.31s", m->scenes[i].name);
        fprintf(out, "removed scene \"%s\"\n", m->scenes[i].name);
        scene_remove(m, i);
    }
    world->host->note(world->gm, msg, 1);
    world->host->redraw(world->gm);
    return 0;
}

static int rq_propose(RQ_ARGS)
{
    RQ_LOCALS;
    /* propose ["a line for the GM"], or job N propose [...]: what the
     * request's edits are. Without one, they are an agent's own idea. */
    int at = v[0] == 'p' ? 1 : 3, slot = -1, num;
    /* propose apply "plan.txt": what `vtt map --apply` sends to the vtt
     * that has the map open. */
    int from_apply = at == 1 && n == 3 && !strcmp(w[1], "apply");
    if (from_apply) at = 2;
    if (n > at + 1) { snprintf(err, errsz, "%s takes one line for the GM after it, in quotes", at == 1 ? "propose" : "job N propose"); return -1; }
    if (ed->header || ed->lines) { snprintf(err, errsz, "propose comes once, before the request's edits"); return -1; }
    if (world->ctl_direct) { rq_class(CTL_E_STATE), snprintf(err, errsz, "there is no GM to propose to: --apply changes the file"); return -1; }
    if (at == 3) {
        if (!word_int(w[1], 1, JOB_NUM_MAX, &num) || (slot = world_job_find(world, num)) < 0) {
            rq_class(CTL_E_MISSING), snprintf(err, errsz, "no job #%.10s - jobs lists them", w[1]);
            return -1;
        }
        const Job *j = &world->jobs[slot];
        if (j->state == JOB_ACCEPTED || j->state == JOB_SCRAPPED) {
            rq_class(CTL_E_STATE), snprintf(err, errsz, "#%d was %s - propose alone offers a new change", j->num, world_job_state_name(j->state));
            return -1;
        }
        if (world_job_gm_only(j)) {
            rq_class(CTL_E_STATE), snprintf(err, errsz, "#%d is %s: the GM's to decide - propose alone offers a change of your own", j->num,
                     j->from == JOB_FROM_DISK ? "what another program wrote to the file" : "an --apply's");
            return -1;
        }
        if (!request_job_mine(world, j, err, errsz)) return -1;
    }
    ed->header = 1;
    ed->job = slot;
    if (from_apply) {
        ed->from = JOB_FROM_APPLY;
        snprintf(ed->say, sizeof ed->say, "--apply %.140s", w[at]);
    }
    else str_lcpy(ed->say, n > at ? w[at] : "", sizeof ed->say);
    return 0;
}

static int rq_job(RQ_ARGS)
{
    RQ_LOCALS;
    /* The scratch map holds this request's edits: a job's own result
     * would be made in it. */
    if (ed->lines || ed->header) { snprintf(err, errsz, "%.4s comes before the request's propose line and edits", v); return -1; }
    return request_job(world, w, n, out, err, errsz);
}

static int rq_edit(RQ_ARGS)
{
    RQ_LOCALS;
    if (!ed->lines && !world->ctl_direct) proposal_begin(world, ed);
    else if (!ed->lines) {
        /* Asked once, at the first edit: after it the open batch is
         * this request's own. */
        const char *busy = world->host->busy(world->gm);
        if (busy) { str_lcpy(err, busy, errsz); return -2; }
        undo_begin(&world->undo);
        ed->ops0 = world->undo.stamp;
    }
    if (ed->lines == 0) {
        while (*line == ' ' || *line == '\t') line++;
        str_lcpy(ed->first, line, sizeof ed->first);
    }
    ed->lines++;
    ed->out = out;
    if (edit_line(world, w, n, ed, err, errsz) < 0) return -1;
    if ((unsigned long)(world->undo.stamp - ed->ops0) > CTL_OPS_MAX) {
        rq_class(CTL_E_LIMIT), snprintf(err, errsz, "the request changes more than %lu things at once", CTL_OPS_MAX);
        return -1;
    }
    return 0;
}

static int rq_undo(RQ_ARGS)
{
    RQ_LOCALS;
    if (n > 1) { snprintf(err, errsz, "undo takes nothing after it"); return -1; }
    if (ed->lines) { snprintf(err, errsz, "undo comes before any edit in a request"); return -1; }
    /* Whose a step is, the log says (undo_tag). `undo` takes back the
     * caller's newest landing and no other: only while it is the last step
     * applied, and once -- a caller repeating itself must not walk back
     * through everything the GM accepted of it (`job N undo` names one). */
    const UndoTag *top = undo_tag_top(&world->undo), *t = undo_tag_last(&world->undo, world->ctl_who);
    if (top && strcmp(top->who, world->ctl_who) != 0) {
        rq_class(CTL_E_STATE);
        snprintf(err, errsz, "the last change to land is %s's, and you are %s - undo takes back your own", top->who, world->ctl_who);
        return -1;
    }
    if (!t || t->batch >= world->undo.depth) {
        rq_class(CTL_E_STATE);
        /* Its newest was taken back already (by it, or the GM's u), and an
         * older one of its is the last step: said, with what reaches it. */
        if (t && top && top->job)
            snprintf(err, errsz, "your newest change was taken back already, and undo is once - job %d undo takes back the one before it", top->job);
        else snprintf(err, errsz, "there is no change of the agent's to take back");
        return -1;
    }
    if (t != top) {
        rq_class(CTL_E_STATE), snprintf(err, errsz, "something has happened since the agent's last change - only the GM's u takes it back now");
        return -1;
    }
    if (!request_undo(world, 1, t->job, err, errsz)) return -2;
    world->host->note(world->gm, "the agent took back its last change - ctrl-r puts it back", 1);
    fputs("took back the last change\n", out);
    return 0;
}

int request_undo(World *world, int steps, int job, char *err, size_t errsz)
{
    int         slot = job ? world_job_find(world, job) : -1;
    const char *busy = world->host->busy(world->gm);
    /* Not from under the GM's eyes: the rest of that job is on screen, and
     * what it shows was made against the map with the part landed. */
    if (!busy && slot >= 0 && world->host->reviewing(world->gm) == slot) busy = "the GM is reviewing the rest of that job";
    if (busy) { str_lcpy(err, busy, errsz); return 0; }
    char msg[120];
    world_events_map_flush(world);             /* the GM's changes so far are their own event */
    for (int k = 0; k < steps; k++) undo_undo(&world->undo, world->map);
    /* News to every name but the caller's: a job's step, or a change that
     * had none (--apply on a file, where nobody listens). */
    snprintf(msg, sizeof msg, "taken back by %s", world->ctl_who);
    world_event_of_change(world, job ? CTL_EV_UNDONE : CTL_EV_CHANGED, job, world->ctl_who, msg);
    if (slot >= 0) world_job_thread_add(&world->jobs[slot], '-', msg);
    world->host->changed(world->gm, WORLD_UNDONE, 0, 0, 0);
    return 1;
}

static int rq_holds(RQ_ARGS)
{
    RQ_LOCALS;
    /* holds DEV INO: is that file (as stat names it) the map open here?
     * --apply asks every vtt before it touches a file (docs/CONFLICTS.md,
     * step 6). By identity, not by name: the two were started in
     * different directories, and a link is the same file. */
    char *e1, *e2;
    unsigned long long dev = n == 3 ? strtoull(w[1], &e1, 10) : 0, ino = n == 3 ? strtoull(w[2], &e2, 10) : 0;
    if (n != 3 || *e1 || *e2) { snprintf(err, errsz, "holds DEVICE INODE, as stat gives them"); return -1; }
    if (!request_read(world, CTL_FILE_CHARGE, err, errsz)) return -1;          /* a stat a line */
    struct stat st;
    int same = m && m->path[0] && stat(m->path, &st) == 0 &&
               (unsigned long long)st.st_dev == dev && (unsigned long long)st.st_ino == ino;
    fputs(same ? "holds\n" : "no\n", out);
    return 0;
}

static int rq_wait(RQ_ARGS)
{
    RQ_LOCALS;
    /* wait [SEQ] [for SECONDS]: every event after SEQ (unsaid: from
     * now), held until there is one or the time is up. */
    unsigned seq = world->event_seq;
    int      secs = CTL_WAIT_DEFAULT_S, at = 1, k, said = 0;
    if (at < n && strcmp(w[at], "for") != 0) {
        said = 1;
        if (!word_int(w[at], 0, 0x7fffffff, &k)) { snprintf(err, errsz, "wait [SEQ] [for SECONDS]: %.20s is not an event's number", w[at]); return -1; }
        seq = (unsigned)k;
        at++;
    }
    int timed = at < n;
    if (at < n) {
        if (at + 2 != n || strcmp(w[at], "for") != 0 || !word_int(w[at + 1], 0, 0x7fffffff, &secs)) {
            snprintf(err, errsz, "wait [SEQ] [for SECONDS], at most %d seconds", CTL_WAIT_MAX_S);
            return -1;
        }
        if (secs > CTL_WAIT_MAX_S) {
            rq_class(CTL_E_LIMIT), snprintf(err, errsz, "wait [SEQ] [for SECONDS], at most %d seconds", CTL_WAIT_MAX_S);
            return -1;
        }
    }
    world->agent_seen_ms = world->now_ms ? world->now_ms : 1;
    world_events_hear(world);
    /* Held only where there is a socket to hold it on (app_tick):
     * --apply, the bench and the fuzzer are answered at once. */
    /* `wait 0` alone asks for everything kept: an answer, not a wait, even
     * when nothing is. With a time it is a wait like any other -- `--apply
     * --wait` asks `wait SEQ for N` with the SEQ it was given, 0 on a vtt
     * where nothing has happened yet, and must be held, not answered in a
     * loop. */
    if (world_events_after(world, seq, world->ctl_who) || !world->ctl_can_hold || secs == 0 || (said && seq == 0 && !timed)) world_events_write(world, out, seq, world->ctl_who);
    else {
        world->ctl_held     = 1;
        world->ctl_hold_seq = seq;
        world->ctl_hold_ms  = secs * 1000;
    }
    return 0;
}

static int rq_status(RQ_ARGS)
{
    RQ_LOCALS;
    if (n > 1) { snprintf(err, errsz, "status takes nothing after it"); return -1; }
    live_swap(world, ed);
    do_status(world, out, ed->lines > 0);
    live_swap(world, ed);
    return 0;
}

int request_dump(const World *world, const Map *m, char w[][CTL_WORD_MAX], int n, int at, int count, FILE *out, char *err, size_t errsz)
{
    int x0 = 0, y0 = 0, x1 = m->w - 1, y1 = m->h - 1;
    if (n > at + 1) { snprintf(err, errsz, "dump takes one region, like B2:K12, or all, or key"); return 0; }
    /* An area the GM has called `all` or `key` is still an area. */
    int word = n == at + 1 && map_area_find(m, w[at]) < 0;
    if (word && !strcmp(w[at], "key")) { maptools_dump_key(out); return 1; }
    if (n == at && (long)m->w * m->h > CTL_DUMP_SQUARES) {
        char far[MAP_COORD_MAX], eg[MAP_COORD_MAX];
        map_coord_name(m->w - 1, m->h - 1, far, sizeof far);
        map_coord_name(imin(31, m->w - 1), imin(31, m->h - 1), eg, sizeof eg);
        rq_class(CTL_E_LIMIT);
        snprintf(err, errsz, "the map is %dx%d, A1 to %s: dump a region of it, like A1:%s, or dump all for its %ld squares",
                 m->w, m->h, far, eg, (long)m->w * m->h);
        return 0;
    }
    /* Refused off the map, as an edit is: clipped, a mistyped region
     * would quietly read the wrong squares. */
    if (n == at + 1 && !(word && !strcmp(w[at], "all")) && !region(m, w[at], &x0, &y0, &x1, &y1, err, errsz)) return 0;
    if (count && !request_read(world, (long)(x1 - x0 + 1) * (y1 - y0 + 1), err, errsz)) return 0;
    maptools_dump_parts(out, m, x0, y0, x1, y1, DUMP_FOOT);
    return 1;
}

static int rq_dump(RQ_ARGS)
{
    RQ_LOCALS;
    return request_dump(world, m, w, n, 1, 1, out, err, errsz) ? 0 : -1;
}

static int rq_describe(RQ_ARGS)
{
    RQ_LOCALS;
    /* describe [REGION] [json] */
    int json = n > 1 && !strcmp(w[n - 1], "json"), box[4];
    if (n - json > 2) { snprintf(err, errsz, "describe [REGION] [json]"); return -1; }
    if (n - json == 2 && !region(m, w[1], &box[0], &box[1], &box[2], &box[3], err, errsz)) return -1;
    /* The rooms are found by walking every square, whatever region is told. */
    if (!request_read(world, (long)m->w * m->h, err, errsz)) return -1;
    maptools_describe_in(out, m, json, n - json == 2 ? box : NULL);
    return 0;
}

/* changes SEQ [REGION]: the events after SEQ, as `wait` gives them, and then
 * the map as it is now in the pieces they changed -- so an agent told the
 * map changed reads those squares and not the map. Never a diff of two
 * maps: each event kept where its change was when it was made. */
static int rq_changes(RQ_ARGS)
{
    RQ_LOCALS;
    int k, box[4] = { 0, 0, m->w - 1, m->h - 1 };
    if (n < 2 || n > 3 || !word_int(w[1], 0, 0x7fffffff, &k)) {
        snprintf(err, errsz, "changes SEQ [REGION], SEQ the number wait or status last gave");
        return -1;
    }
    /* After an edit `world->map` is the request's own copy: "the map as it is
     * now" would be the proposal, where dump means that and this does not. */
    if (ed->lines || ed->header) { snprintf(err, errsz, "changes comes before the request's propose line and edits"); return -1; }
    if (n == 3 && !region(m, w[2], &box[0], &box[1], &box[2], &box[3], err, errsz)) return -1;
    /* Each writes its regions aside before it sends them, and one tells
     * all there is to tell: three a request is two to spare. Its own count,
     * since the squares it may not read it lists and does not refuse. */
    if (!world->ctl_direct && ++g_changes > 3) BADC(CTL_E_LIMIT, "changes is asked at most three times in a request");
    world->agent_seen_ms = world->now_ms ? world->now_ms : 1;       /* someone is listening: the checkpoint stays */
    world_events_hear(world);                                 /* and a change nobody heard of is said first */
    world_events_write(world, out, (unsigned)k, world->ctl_who);
    CsWhere where, asked = { { 0 } };
    int     other;
    if (!world_events_where(world, (unsigned)k, &where, &other, world->ctl_who)) return 0;   /* `lost` has said to read the map again */
    cs_pieces(m->w, m->h, box[0], box[1], box[2], box[3], &asked);
    char more[CS_GRID * CS_GRID * 16] = "";              /* room for every piece's name */
    fflush(out);
    long base = ftell(out);                              /* the budget is this line's, not the request's */
    int  full = 0;
    for (int r = 0; r < CS_GRID; r++)
        for (int c = 0; c < CS_GRID; c++) {
            if (!cs_where_has(&where, c, r) || !cs_where_has(&asked, c, r)) continue;
            /* The widest run of pieces from here, then as far down as it
             * goes whole: a wall across the map is one region, not sixteen.
             * A region is never grown past half an answer, so each can be
             * sent in one. */
            CsBox b;
            int nc = 1, nr = 1;
#define COST(nc_, nr_) (cs_piece_box(m->w, m->h, c, r, (nc_), (nr_), &b), \
                        (2L * (b.x1 - b.x0 + 1) + 8) * (2L * (b.y1 - b.y0 + 1) + 3))   /* a lattice: two characters a square each way */
            while (c + nc < CS_GRID && cs_where_has(&where, c + nc, r) && cs_where_has(&asked, c + nc, r) &&
                   COST(nc + 1, 1) <= CTL_CHANGES_BYTES / 2) nc++;
            for (int whole = 1; r + nr < CS_GRID; nr++) {
                for (int i = 0; i < nc && whole; i++)
                    whole = cs_where_has(&where, c + i, r + nr) && cs_where_has(&asked, c + i, r + nr);
                if (!whole || COST(nc, nr + 1) > CTL_CHANGES_BYTES / 2) break;
            }
            for (int j = 0; j < nr; j++)
                for (int i = 0; i < nc; i++) cs_where_put(&where, c + i, r + j, 0);
            (void)COST(nc, nr);
#undef COST
            b.x0 = imax(b.x0, box[0]); b.y0 = imax(b.y0, box[1]);
            b.x1 = imin(b.x1, box[2]); b.y1 = imin(b.y1, box[3]);
            char name[2 * MAP_COORD_MAX + 2];
            cs_box_name(&b, name, sizeof name);
            /* Its real bytes decide: the lattice is the least of a region
             * full of creatures and fog. Once one is turned away the rest
             * are listed unread, so at most one dump is thrown away. */
            char  *reg = NULL;
            size_t rl = 0;
            /* A region's squares count toward the request's reads; past them it is listed too. */
            char   e2[200];
            int    cls = g_class;
            if (!full && !request_read(world, (long)(b.x1 - b.x0 + 1) * (b.y1 - b.y0 + 1), e2, sizeof e2)) { full = 1; g_class = cls; }
            FILE  *rf = full ? NULL : open_memstream(&reg, &rl);
            if (rf) {
                maptools_dump_parts(rf, m, b.x0, b.y0, b.x1, b.y1, 0);
                fclose(rf);
            }
            fflush(out);
            if (!rf || ftell(out) - base + (long)rl + 32 > CTL_CHANGES_BYTES) {
                size_t ml = strlen(more);
                if (ml + strlen(name) + 2 < sizeof more) snprintf(more + ml, sizeof more - ml, " %s", name);
                free(reg);
                full = 1;
                continue;
            }
            fprintf(out, "changed %s\n", name);
            fwrite(reg, 1, rl, out);
            free(reg);
        }
    if (more[0]) fprintf(out, "more:%s - too much for one answer; dump each\n", more);
    if (other) fputs("also: something on no square changed (rolls, cards, scenes, clocks, the fight, fog's settings, "
                     "the map's size) - describe reads them\n", out);
    return 0;
}

static int rq_stamps(RQ_ARGS)
{
    RQ_LOCALS;
    /* The saved stamps, one a line with its size: what `stamp` can put down. */
    if (n > 1) { snprintf(err, errsz, "stamps takes nothing after it"); return -1; }
    if (!request_read(world, CTL_LIST_CHARGE, err, errsz)) return -1;          /* every stamp's file is opened */
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

static int rq_scenes(RQ_ARGS)
{
    RQ_LOCALS;
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

static int rq_characters(RQ_ARGS)
{
    RQ_LOCALS;
    /* The saved character templates: what `token add ... from NAME` places. */
    if (n > 1) { snprintf(err, errsz, "characters takes nothing after it"); return -1; }
    if (!request_read(world, CTL_LIST_CHARGE, err, errsz)) return -1;          /* every character's file is opened */
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

static int rq_floors(RQ_ARGS)
{
    RQ_LOCALS;
    /* The floors, highest first, as a building reads top to bottom. */
    if (n > 1) { snprintf(err, errsz, "floors takes nothing after it"); return -1; }
    int order[MAP_AREAS_MAX], nf = floor_order(m, order);
    for (int k = nf - 1; k >= 0; k--) {
        const Area *fa = &m->areas[order[k]];
        char b0[MAP_COORD_MAX], b1[MAP_COORD_MAX];
        map_coord_name(fa->x0, fa->y0, b0, sizeof b0);
        map_coord_name(fa->x1, fa->y1, b1, sizeof b1);
        fprintf(out, "%-16s level %3d  %s:%s%s\n", fa->name, fa->level, b0, b1,
                order[k] == world->host->floor_shown(world->gm) ? "  (the GM is looking at it)" : "");
    }
    if (!nf) fputs("no floors\n", out);
    return 0;
}

static int rq_links(RQ_ARGS)
{
    RQ_LOCALS;
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

static int rq_marked(RQ_ARGS)
{
    RQ_LOCALS;
    int j = want_json(w, n, 1, err, errsz);
    if (j < 0) return -1;
    live_swap(world, ed);                    /* what the GM points at is on the GM's map */
    world->host->marked(world->gm, out, j);
    live_swap(world, ed);
    return 0;
}

static int rq_check(RQ_ARGS)
{
    RQ_LOCALS;
    int j = want_json(w, n, 1, err, errsz);
    if (j < 0) return -1;
    /* The file's findings from its bytes as vtt last read them, then the
     * live map's: one map, counted as its squares or as the file's bytes a
     * third of a square each (three row sections a square), whichever is
     * more -- a sparse file is small, a full one about the squares again. */
    long sq = (long)m->w * m->h, fb = m->base ? (long)(m->base_len / 3) : 0;
    if (!request_read(world, sq > fb ? sq : fb, err, errsz)) return -1;
    maptools_check_open(out, m, j);
    return 0;
}

static int rq_scene(RQ_ARGS)
{
    /* A scene put back is an edit; saving, removing and diffing one are not. */
    return scene_is_edit(w, n) ? rq_edit(RQ_PASS) : rq_scene_other(RQ_PASS);
}

static int rq_job_any(RQ_ARGS)
{
    if (n >= 3 && !strcmp(w[2], "propose")) return rq_propose(RQ_PASS);
    return rq_job(RQ_PASS);
}

static int rq_agent(RQ_ARGS)
{
    RQ_LOCALS;
    /* The first line's is taken where requests are run; this is one further down. */
    snprintf(err, errsz, "agent NAME goes first in a request, and once");
    return -1;
}

static int rq_help(RQ_ARGS);

/* Every request the channel takes, one row each: the one place a verb is
 * named. The dispatcher, what an off channel takes, what must go alone in
 * its request, `help`, the agent's prompt (request_edit_verbs) and the test
 * that docs/AGENTS.md lists exactly these all read it. `use` is how the line
 * is written, one form a line; `doc` one line of what it does. Nothing longer
 * belongs here: the page is AGENTS.md. */
typedef struct {
    const char *name;
    unsigned    flags;          /* CTL_RQ_* (request.h) */
    int       (*run)(RQ_ARGS);
    const char *use;
    const char *doc;
} Request;

#define E CTL_RQ_EDIT
static const Request REQUESTS[] = {
    { "room",     E, rq_edit, "room [NAME] REGION\nroom NAME SQUARE WxH\nroom NAME WxH east|west|north|south of ROOM [gap N] [top|middle|bottom|left|right]",
      "a room: floor with a wall round it, named as an area when NAME is given" },
    { "area",     E, rq_edit, "area NAME REGION\narea NAME remove", "name a box without drawing anything, or take the name off" },
    { "door",     E, rq_edit, "door ROOM north|south|east|west [N|middle] [KIND]", "a door (or window, secret door, opening) in a room's side" },
    { "corridor", E, rq_edit, "corridor ROOM ROOM [width 1-3] [KIND]", "dug between two named rooms through void, with an opening at each end" },
    { "tile",     E, rq_edit, "tile REGION void|floor|water|rough|brush|wood|hazard", "the ground of every square in the region" },
    { "wall",     E, rq_edit, "wall REGION [wall|door|open|window|secret|opensecret|none]", "the region's outline" },
    { "edge",     E, rq_edit, "edge BOUNDARY KIND", "one boundary between two squares" },
    { "stamp",    E, rq_edit, "stamp NAME SQUARE [rotate 90|180|270] [mirror]", "one of the GM's saved stamps, its top-left on SQUARE" },
    { "note",     E, rq_edit, "note SQUARE \"text\"\nnote SQUARE", "a note only the GM sees on a square, or take it off" },
    { "fog",      E, rq_edit, "fog paint REGION N", "paint the region into fog patch N (0 scrubs)" },
    { "floor",    E, rq_edit, "floor AREA LEVEL\nfloor AREA off", "the named area is a floor at that level (-99 to 99), or no longer one" },
    { "env",      E, rq_edit, "env AREA CARD\nenv AREA remove", "the named area's environment is the card of that name, or it has none" },
    { "link",     E, rq_edit, "link FROM TO [KIND] [size 2|3] [oneway] [secret]\nlink FROM to MAP PLACE [KIND] [size N] [secret]\nlink N KIND|oneway|twoway|reverse|secret|seen|remove",
      "stairs, a ladder, a trapdoor or a portal joining two places, or a change to link N" },
    { "token",    E, rq_edit, "token add player|enemy PLACE [size 1-3] [hidden] \"LABEL\"\ntoken add player|enemy PLACE from CHARACTER [hidden]\ntoken move WHO SQUARE\ntoken del WHO\ntoken set WHO label \"...\"|size N|note \"...\"|hidden on|off",
      "put down, move, remove or change a creature" },
    { "scene",    E, rq_scene, "scene \"NAME\"\nscene save \"NAME\" [REGION]\nscene \"NAME\" remove\nscene diff \"NAME\"",
      "put a saved scene's creatures back (an edit); save or remove one (alone in its request); or say what changed since" },

    { "dump",       0, rq_dump, "dump [REGION|all|key]", "the map as text: a region of it, or all of it; key says what its characters mean" },
    { "changes",    0, rq_changes, "changes SEQ [REGION]", "the events after SEQ, then the map as it is now where they changed it: read this, not the map again" },
    { "describe",   0, rq_describe, "describe [REGION] [json]", "the rooms walls make, their doors and links, and what is in each" },
    { "check",      0, rq_check, "check [json]", "mistakes in the file (line numbers, a row added or lost) and on the map: loose doors, creatures on void, rooms nothing reaches" },
    { "stamps",     0, rq_stamps, "stamps", "the GM's saved stamps and their sizes" },
    { "characters", 0, rq_characters, "characters", "the GM's saved characters" },
    { "scenes",     0, rq_scenes, "scenes", "the map's scenes" },
    { "links",      0, rq_links, "links [json]", "every link: number, kind, ends, size, one-way, secret" },
    { "floors",     0, rq_floors, "floors", "the floors, top first, and which the GM is looking at" },
    { "agent",      CTL_RQ_NOMAP, rq_agent, "agent NAME", "who is asking, as a request's first line (vtt --ctl writes it from $VTT_AGENT): a job is its taker's, and undo is your own" },
    { "status",     CTL_RQ_LIVE | CTL_RQ_NOMAP, rq_status, "status", "the map, the GM's screen and floor, whether edits are taken now" },
    { "marked",     CTL_RQ_LIVE, rq_marked, "marked [json]", "what the GM is pointing at: cursor, box, selection, ruler, pings" },

    { "propose",  CTL_RQ_LIVE, rq_propose, "propose [\"a line for the GM\"]", "first line of a request: its edits are a proposal for the GM to review" },
    { "job",      0, rq_job_any, "job N take|drop\njob N area REGION\njob N say \"a line for the GM\"\njob N dump [REGION|all|key]|check|describe\njob N conflicts\njob N undo\njob N propose [\"a line for the GM\"]",
      "one job: take it or give it up, show the GM where you work, tell the GM a line, read its proposal back, list what changed under it, take its landing back, or answer it with this request's edits" },
    { "jobs",     0, rq_job, "jobs [json]", "every job and where it stands" },
    { "wait",     CTL_RQ_ALONE | CTL_RQ_NOMAP, rq_wait, "wait [SEQ] [for SECONDS]", "every event after SEQ; a running vtt holds the answer until there is one or the time is up" },
    { "undo",     CTL_RQ_ALONE | CTL_RQ_LIVE, rq_undo, "undo", "take back the last change to land, when it is yours and nothing has happened since (job N undo: that job's)" },
    { "holds",    CTL_RQ_OFF | CTL_RQ_NOMAP, rq_holds, "holds DEVICE INODE", "whether that file is the map open here (--apply asks)" },
    { "help",     CTL_RQ_NOMAP, rq_help, "help [VERB|json]", "these requests, or how one is written" },
};
#undef E
#define NREQUESTS ((int)(sizeof REQUESTS / sizeof *REQUESTS))

static const Request *rq_find(const char *v)
{
    for (int i = 0; i < NREQUESTS; i++)
        if (!strcmp(v, REQUESTS[i].name)) return &REQUESTS[i];
    return NULL;
}

const char *request_verb(int i, unsigned *flags)
{
    if (i < 0 || i >= NREQUESTS) return NULL;
    if (flags) *flags = REQUESTS[i].flags;
    return REQUESTS[i].name;
}

void request_edit_verbs(FILE *out)
{
    for (int i = 0, k = 0; i < NREQUESTS; i++)
        if (REQUESTS[i].flags & CTL_RQ_EDIT) fprintf(out, "%s%s", k++ ? ", " : "", REQUESTS[i].name);
}

static const char *rq_kind(const Request *r)
{
    return r->flags & CTL_RQ_EDIT ? "edit" : "read";
}

static int rq_help(RQ_ARGS)
{
    RQ_LOCALS;
    if (n > 2) { snprintf(err, errsz, "help, help VERB, or help json"); return -1; }
    if (n == 2 && !strcmp(w[1], "json")) {
        Json js;
        json_init(&js, out);
        json_open(&js, '[');
        for (int i = 0; i < NREQUESTS; i++) {
            const Request *r = &REQUESTS[i];
            json_open(&js, '{');
            json_kstr(&js, "name", r->name);
            json_kstr(&js, "kind", rq_kind(r));
            json_kstr(&js, "use", r->use);
            json_kstr(&js, "does", r->doc);
            json_key(&js, "alone"); json_bool(&js, (r->flags & CTL_RQ_ALONE) != 0);
            json_key(&js, "live");  json_bool(&js, (r->flags & CTL_RQ_LIVE) != 0);
            json_close(&js, '}');
        }
        json_close(&js, ']');
        fputc('\n', out);
        return 0;
    }
    if (n == 2) {
        const Request *r = rq_find(w[1]);
        if (!r) { rq_class(CTL_E_MISSING), snprintf(err, errsz, "no request called %.40s - help lists them", w[1]); return -1; }
        fprintf(out, "%s\n  %s%s%s\n", r->use, r->doc,
                r->flags & CTL_RQ_ALONE ? "; alone in its request" : "",
                r->flags & CTL_RQ_LIVE ? "; of a running vtt only" : "");
        return 0;
    }
    for (int i = 0; i < NREQUESTS; i++) fprintf(out, "%-10s  %s\n", REQUESTS[i].name, REQUESTS[i].doc);
    fputs("help VERB says how one is written; edits in one request are all or nothing\n", out);
    return 0;
}

/* Lines that change nothing the undo log can take back, so they cannot roll
 * back with the lines around them and go in a request of their own: the
 * table's (the agent's undo, a wait), and saving or removing a scene. The
 * verb, or NULL. */
static const char *lonely(char w[][CTL_WORD_MAX], int n)
{
    const Request *r = n >= 1 ? rq_find(w[0]) : NULL;
    if (r && (r->flags & CTL_RQ_ALONE)) return r->name;
    if (n >= 2 && !strcmp(w[0], "scene") && !strcmp(w[1], "save")) return "scene save";
    if (n == 3 && !strcmp(w[0], "scene") && !strcmp(w[2], "remove")) return "scene NAME remove";
    if (n == 3 && !strcmp(w[0], "job") && !strcmp(w[2], "undo")) return "job N undo";
    return NULL;
}

/* What a request over the socket may say with the channel off: nothing but
 * --apply's own question, whether the map it was given is open here. The
 * rest is refused whole (docs/V2.md, ruling 9; it reverses docs/CONFLICTS.md's
 * decision 5, under which proposals, status and wait were taken). */
static int off_takes(char w[][CTL_WORD_MAX], int n)
{
    const Request *r = n > 0 ? rq_find(w[0]) : NULL;
    return r && (r->flags & CTL_RQ_OFF);
}

/* Runs one line through its row. */
static int run_line(RQ_ARGS)
{
    const Request *r = rq_find(w[0]);
    if (!r) {
        snprintf(err, errsz, "unknown request %.40s", w[0]);
        return -1;
    }
    return r->run(RQ_PASS);
}

/* 0 when the request holds `undo` and any other line. */
/* The first line that must go alone, when the request holds more than one
 * line; NULL when it is fine. */
static const char *not_alone(const char *p)
{
    int lines = 0, named = 0;                /* the first line met, when it is the name's, is not one of the request's */
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
        if (n != 0 && !(n > 0 && w[0][0] == '#') && !(n > 0 && !lines && !named++ && !strcmp(w[0], "agent"))) {
            lines++;                                   /* the name above a request is not a line of it */
            if (n > 0 && !verb) verb = lonely(w, n);
        }
        p = end ? end + 1 : p + ll;
    }
    return lines > 1 ? verb : NULL;
}

/* A request's edits are in: close the batch and tell both sides. */
static void finish_edits(World *world, Edits *ed, FILE *out)
{
    int changed = world->undo.stamp != ed->ops0;     /* a stamp a recorded op */
    undo_end(&world->undo);
    if (!changed) {
        fprintf(out, "no change: the map already looked like that\n");
        return;
    }
    undo_tag(&world->undo, 0, world->ctl_who);           /* whose step: `undo` takes back the caller's own */
    say_changed(world, ed, out);
    char msg[160];
    if (ed->lines == 1) snprintf(msg, sizeof msg, "agent: %s - u takes it back", ed->first);
    else snprintf(msg, sizeof msg, "agent: %s and %d more - u takes them back", ed->first, ed->lines - 1);
    world->host->note(world->gm, msg, 1);
}

char *request_exec(World *world, const char *req, size_t *len)
{
    PROF_ZONE("ctl");
    char  *body = NULL;
    size_t blen = 0;
    FILE  *out  = open_memstream(&body, &blen);
    if (!out) return NULL;

    char  verdict[320] = "ok";
    int   lineno = 0;
    g_read = g_cover = 0;
    g_changes = 0;
    int ran = 0;                         /* a line has been met: the name's must be the first */
    str_lcpy(world->ctl_who, CTL_NAME_NONE, sizeof world->ctl_who);
    Edits ed;
    memset(&ed, 0, sizeof ed);
    ed.x1 = -1;
    ed.job = -1;
    ed.from = JOB_FROM_AGENT;
    const char *p = req;
    if (strlen(req) != *len) {
        refuse(verdict, sizeof verdict, CTL_E_USAGE, "a nul byte in the request");
        p = "";
    }
    /* `undo`, and saving or removing a scene, go alone: they cannot be
     * rolled back with the lines around them, so a request that holds one
     * holds nothing else. */
    const char *lone = strcmp(verdict, "ok") == 0 ? not_alone(p) : NULL;
    if (lone) refuse(verdict, sizeof verdict, CTL_E_USAGE, "%s goes in a request of its own", lone);
    if (strcmp(verdict, "ok") != 0) p = "";
    while (*p) {
        const char *end = strchr(p, '\n');
        size_t      ll  = end ? (size_t)(end - p) : strlen(p);
        lineno++;

        char line[1024], err[200] = "";
        char w[CTL_WORDS][CTL_WORD_MAX];
        int  n = -1;
        g_class = CTL_E_USAGE;
        if (ll >= sizeof line) rq_class(CTL_E_LIMIT), snprintf(err, sizeof err, "the line is over %zu characters", sizeof line - 1);
        else {
            memcpy(line, p, ll);
            line[ll] = '\0';
            /* A comment is prose: never split, so its length and its
             * quotes are nobody's business. */
            n = is_comment(line) ? 0 : split_words(line, w, err, sizeof err);
        }
        p = end ? end + 1 : p + ll;

        if (n == 0 || (n > 0 && w[0][0] == '#')) continue;
        /* `agent NAME`, above everything: who is asking. Taken before the
         * channel's state is, so a refusal can still say whose it was. */
        if (n > 0 && !strcmp(w[0], "agent") && !ran) {
            ran = 1;
            /* Lines are numbered from the one after it: `vtt --ctl` wrote
             * it, and the agent counts its own. */
            if (n == 2 && ctl_name_ok(w[1])) {
                str_lcpy(world->ctl_who, w[1], sizeof world->ctl_who);
                world->host->heard(world->gm, world->ctl_who);           /* one vtt started is alive */
                lineno--;
                continue;
            }
            snprintf(err, sizeof err, "agent NAME: a name is 1-%d letters, digits, - or _", CTL_NAME_MAX - 1);
        }
        ran = 1;
        /* With no map: what the table says needs none (a wait among them --
         * the map closing is an event). */
        if (err[0]) { }                 /* the name's line, written wrong: said as it is */
        else if (n > 0 && !world->map && !(rq_find(w[0]) && (rq_find(w[0])->flags & CTL_RQ_NOMAP)))
            rq_class(CTL_E_STATE), snprintf(err, sizeof err, "no map is open");
        /* The channel off: over the socket, nothing is taken. The map is
         * named so that a caller with several vtts open knows which. */
        else if (n > 0 && world->ctl_can_hold && !world->agent_on && !off_takes(w, n))
            rq_class(CTL_E_STATE), snprintf(err, sizeof err, "the agent channel is off%s%.63s - type :agent on in that vtt, then ask again",
                     world->map ? " in the vtt with " : "", world->map ? world->map->name : "");
        /* The answer so far (ctl.h, the budget): once it is past the cap no
         * further line is run. One line's answer is never refused for its
         * size -- a whole read of a crowded map is what was asked, and its
         * squares were counted -- only what is piled on top of it. Not under
         * --apply on a file, whose answer is a terminal's or a pipe's. */
        if (n > 0 && !err[0] && !world->ctl_direct && (fflush(out), blen > CTL_ANSWER_CAP)) {
            rq_class(CTL_E_LIMIT);
            snprintf(err, sizeof err, "the answer was over %d MB before this line: send the rest in another request",
                     CTL_ANSWER_CAP >> 20);
        }
        int rc = err[0] ? -1 : run_line(world, line, w, n, out, &ed, err, sizeof err);
        if (rc == 0) continue;
        if (rc == -2) snprintf(verdict, sizeof verdict, "busy: %s", err);
        else          refuse(verdict, sizeof verdict, g_class, "line %d: %s", lineno, err);
        break;
    }

    if (ed.proposal) {
        /* Anything that failed leaves the scratch map to be copied over. */
        proposal_end(world, &ed);
        char err[200];
        if (!strcmp(verdict, "ok") && finish_proposal(world, &ed, out, err, sizeof err) < 0)
            refuse(verdict, sizeof verdict, CTL_E_LIMIT, "%s", err);
        world->host->redraw(world->gm);
    }
    else if (ed.header && !strcmp(verdict, "ok"))
        refuse(verdict, sizeof verdict, CTL_E_USAGE, "propose wants edit lines after it");
    else if (ed.lines) {
        if (strcmp(verdict, "ok") != 0) {
            undo_abort(&world->undo, world->map);                 /* all or nothing */
            /* The overlay followed the deletion; the creature is back. */
            if (ed.deleted) world->host->changed(world->gm, WORLD_ROLLED_BACK, 0, 0, 0);
        }
        else finish_edits(world, &ed, out);
        world->host->changed(world->gm, WORLD_EDITED, 0, 0, 0);
    }
    fclose(out);

    /* The verdict, then the report; an error sends only what it says. */
    int    ok = !strcmp(verdict, "ok");
    if (!ok) world->host->refused(world->gm, verdict);
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
