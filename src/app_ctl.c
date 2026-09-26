/* The control channel's requests: an agent reading, and editing, the map the
 * GM has open. ctl.c moves the bytes; this reads the lines and runs them
 * against the App. The language and its rules are docs/CONTROL.md's. */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "app_priv.h"
#include "fog.h"
#include "json.h"
#include "maptools.h"
#include "prof.h"
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

/* ----------------------------------------------------------------- state */

static const char *screen_name(Screen s)
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

static const char *mode_name(EdMode m)
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
    if (a->screen == SCREEN_EDITOR) fprintf(out, "screen build, %s mode\n", mode_name(a->ed.mode));
    else                            fprintf(out, "screen %s\n", screen_name(a->screen));
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

/* --------------------------------------------------------------- marked */

/* "C3", or "B2:F6" for more than one square. */
static void region_name(int x0, int y0, int x1, int y1, char *buf, size_t sz)
{
    char a[MAP_COORD_MAX], b[MAP_COORD_MAX];
    map_coord_name(x0, y0, a, sizeof a);
    if (x0 == x1 && y0 == y1) { str_lcpy(buf, a, sz); return; }
    map_coord_name(x1, y1, b, sizeof b);
    snprintf(buf, sz, "%s:%s", a, b);
}

static void j_region(Json *j, const char *key, int x0, int y0, int x1, int y1)
{
    char r[2 * MAP_COORD_MAX + 2];
    region_name(x0, y0, x1, y1, r, sizeof r);
    json_key(j, key);
    json_open(j, '{');
    json_kstr(j, "region", r);
    json_kint(j, "x0", x0); json_kint(j, "y0", y0);
    json_kint(j, "x1", x1); json_kint(j, "y1", y1);
    json_close(j, '}');
}

/* Everything the GM is pointing at, gathered once and then written as
 * text or JSON, so the two cannot disagree. */
typedef struct {
    int      cx0, cy0, cx1, cy1;     /* the cursor, a brush's whole footprint */
    int      corner, wx, wy;         /* wall mode: the lattice corner */
    int      box;                    /* 0 none, else ED_SHAPE_RECT + 1 or ED_SHAPE_CIRCLE + 1 */
    int      bx0, by0, bx1, by1;     /* the box's squares, or the disc's bounding box */
    int      ox, oy, radius;         /* a circle's centre square and radius */
    int      box_corners;            /* the box is wall mode's, between corners */
    int      box_empty;              /* it holds no squares: a straight line of corners */
    const Ruler *ruler;              /* NULL when not measuring */
} Marked;

/* A lattice corner by the square it touches: the top left of one, or on
 * the map's east or south edge the top right, bottom left or bottom right
 * of the last. */
static void corner_name(const Map *m, int wx, int wy, char *buf, size_t sz)
{
    char at[MAP_COORD_MAX];
    int  east = wx >= m->w, south = wy >= m->h;
    map_coord_name(east ? m->w - 1 : wx, south ? m->h - 1 : wy, at, sizeof at);
    snprintf(buf, sz, "%s of %s", east && south ? "bottom right" : east ? "top right"
                                  : south ? "bottom left" : "top left", at);
}

/* The squares a shape really takes: a circle's own box is a bound, not
 * the squares inside it. Clipped to the map. */
static void shape_squares(const Map *m, const EdShape *sh, Marked *mk)
{
    int x0 = imax(sh->x0, 0), y0 = imax(sh->y0, 0);
    int x1 = imin(sh->x1, m->w - 1), y1 = imin(sh->y1, m->h - 1);
    mk->bx0 = x1; mk->by0 = y1; mk->bx1 = x0; mk->by1 = y0;
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) {
            if (!ed_shape_has(sh, x, y)) continue;
            mk->bx0 = imin(mk->bx0, x); mk->bx1 = imax(mk->bx1, x);
            mk->by0 = imin(mk->by0, y); mk->by1 = imax(mk->by1, y);
        }
    /* A wall-mode anchor on the cursor's own line holds no squares at all:
     * say so, rather than name one it does not hold. */
    mk->box_empty = mk->bx1 < mk->bx0 || mk->by1 < mk->by0;
}

static void gather_marked(const App *a, Marked *mk)
{
    const Map    *m = a->map;
    const Editor *e = &a->ed;
    memset(mk, 0, sizeof *mk);
    int b = a->screen == SCREEN_EDITOR ? e->brush : 1, tx, ty;
    ed_cursor_tile(e, &tx, &ty);
    mk->cx0 = imin(tx, m->w - 1); mk->cy0 = imin(ty, m->h - 1);
    mk->cx1 = imin(tx + b - 1, m->w - 1);
    mk->cy1 = imin(ty + b - 1, m->h - 1);

    if (a->screen == SCREEN_EDITOR && e->mode == ED_WALL) {
        mk->corner = 1;
        mk->wx = e->wx; mk->wy = e->wy;
        if (e->has_anchor) {
            EdShape sh = ed_shape(e->shape, e->ax, e->ay, e->wx, e->wy, 1);
            mk->box = sh.kind + 1;
            mk->box_corners = 1;
            shape_squares(m, &sh, mk);
            mk->radius = ed_shape_radius(&sh);
        }
    }
    else if (a->screen == SCREEN_EDITOR && e->mode == ED_VISUAL) {
        EdShape sh = ed_shape(e->shape, e->anchor_x, e->anchor_y, e->cx, e->cy, 0);
        mk->box = sh.kind + 1;
        shape_squares(m, &sh, mk);
        mk->ox = e->anchor_x; mk->oy = e->anchor_y;
        mk->radius = ed_shape_radius(&sh);
    }
    else if (a->screen == SCREEN_PLAY && a->play.visual) {
        mk->box = ED_SHAPE_RECT + 1;
        mk->bx0 = imin(a->play.anchor_x, e->cx); mk->bx1 = imax(a->play.anchor_x, e->cx);
        mk->by0 = imin(a->play.anchor_y, e->cy); mk->by1 = imax(a->play.anchor_y, e->cy);
    }
    mk->ruler = a->ruler.active ? &a->ruler : NULL;
}

static const char *ping_source(uint32_t who, char *buf, size_t sz)
{
    if (who == PING_GM) return "the GM";
    snprintf(buf, sz, "phone %u", who);
    return buf;
}

/* A record's until_ms is when it was made. */
static uint64_t ping_age_ms(const App *a, const Ping *p)
{
    return a->now_ms > p->until_ms ? a->now_ms - p->until_ms : 0;
}

static void do_marked(App *a, FILE *out, int json)
{
    const Map *m = a->map;
    Marked mk;
    gather_marked(a, &mk);
    const char *screen = screen_name(a->screen);
    const char *mode   = a->screen == SCREEN_EDITOR ? mode_name(a->ed.mode) : NULL;
    char r[2 * MAP_COORD_MAX + 2], at[MAP_COORD_MAX];
    double ruler_ft = mk.ruler ? ruler_tiles(mk.ruler, (DistMetric)m->metric) * m->scale_ft : 0;
    char dist[32] = "";
    if (mk.ruler) dist_fmt(dist, sizeof dist, ruler_ft);

    if (json) {
        Json j;
        json_init(&j, out);
        json_open(&j, '{');
        json_kstr(&j, "screen", screen);
        json_key(&j, "mode");
        if (mode) json_str(&j, mode); else json_null(&j);
        j_region(&j, "cursor", mk.cx0, mk.cy0, mk.cx1, mk.cy1);
        json_key(&j, "corner");
        if (mk.corner) {
            json_open(&j, '{');
            json_kint(&j, "x", mk.wx);
            json_kint(&j, "y", mk.wy);
            char cn[48];
            corner_name(m, mk.wx, mk.wy, cn, sizeof cn);
            json_kstr(&j, "at", cn);
            json_close(&j, '}');
        } else json_null(&j);
        json_key(&j, "box");
        if (mk.box) {
            json_open(&j, '{');
            json_kstr(&j, "shape", mk.box == ED_SHAPE_CIRCLE + 1 ? "circle" : "rect");
            json_key(&j, "between");
            json_str(&j, mk.box_corners ? "corners" : "squares");
            if (mk.box_empty) { json_key(&j, "squares"); json_null(&j); }
            else j_region(&j, "squares", mk.bx0, mk.by0, mk.bx1, mk.by1);
            if (mk.box == ED_SHAPE_CIRCLE + 1) {
                json_kint(&j, "radius", mk.radius);
                if (!mk.box_corners) {
                    map_coord_name(mk.ox, mk.oy, at, sizeof at);
                    json_kstr(&j, "centre", at);
                }
            }
            json_close(&j, '}');
        } else json_null(&j);
        json_key(&j, "selected");
        json_open(&j, '[');
        if (a->screen == SCREEN_PLAY)
            for (int i = 0; i < a->play.ngroup; i++) {
                const Token *t = &m->tokens.v[a->play.group[i]];
                json_open(&j, '{');
                json_kstr(&j, "label", t->label);
                json_kstr(&j, "kind", token_kind_name(t->kind));
                j_region(&j, "at", t->x, t->y, t->x + t->size - 1, t->y + t->size - 1);
                json_close(&j, '}');
            }
        json_close(&j, ']');
        json_key(&j, "ruler");
        if (mk.ruler) {
            json_open(&j, '{');
            json_key(&j, "points");
            json_open(&j, '[');
            for (int i = 0; i < mk.ruler->n; i++) {
                map_coord_name(mk.ruler->pts[i].x, mk.ruler->pts[i].y, at, sizeof at);
                json_str(&j, at);
            }
            json_close(&j, ']');
            map_coord_name(mk.ruler->cx, mk.ruler->cy, at, sizeof at);
            json_kstr(&j, "end", at);
            json_key(&j, "feet");
            json_num(&j, ruler_ft);
            json_close(&j, '}');
        } else json_null(&j);
        json_key(&j, "pings");
        json_open(&j, '[');
        for (int i = 0; i < a->npinged; i++) {
            const Ping *p = &a->pinged[i];
            json_open(&j, '{');
            json_kstr(&j, "by", p->who == PING_GM ? "gm" : "phone");
            json_key(&j, "phone");
            if (p->who == PING_GM) json_null(&j); else json_int(&j, (long)p->who);
            j_region(&j, "at", p->x0, p->y0, p->x1, p->y1);
            json_kint(&j, "seconds_ago", (long)(ping_age_ms(a, p) / 1000));
            json_close(&j, '}');
        }
        json_close(&j, ']');
        json_close(&j, '}');
        fputc('\n', out);
        return;
    }

    if (mode) fprintf(out, "screen %s, %s mode\n", screen, mode);
    else      fprintf(out, "screen %s\n", screen);
    region_name(mk.cx0, mk.cy0, mk.cx1, mk.cy1, r, sizeof r);
    fprintf(out, "cursor %s\n", r);
    if (mk.corner) {
        char cn[48];
        corner_name(m, mk.wx, mk.wy, cn, sizeof cn);
        fprintf(out, "corner at the %s\n", cn);
    }
    if (mk.box && mk.box_empty)
        fputs("box a line of corners, no squares inside it\n", out);
    else if (mk.box) {
        region_name(mk.bx0, mk.by0, mk.bx1, mk.by1, r, sizeof r);
        if (mk.box == ED_SHAPE_CIRCLE + 1 && !mk.box_corners) {
            map_coord_name(mk.ox, mk.oy, at, sizeof at);
            fprintf(out, "box circle round %s, radius %d, over %s\n", at, mk.radius, r);
        }
        else if (mk.box == ED_SHAPE_CIRCLE + 1)
            fprintf(out, "box circle, radius %d, over %s\n", mk.radius, r);
        else
            fprintf(out, "box %s, %dx%d\n", r, mk.bx1 - mk.bx0 + 1, mk.by1 - mk.by0 + 1);
    }
    if (a->screen == SCREEN_PLAY && a->play.ngroup) {
        fputs("selected", out);
        for (int i = 0; i < a->play.ngroup; i++) {
            const Token *t = &m->tokens.v[a->play.group[i]];
            region_name(t->x, t->y, t->x + t->size - 1, t->y + t->size - 1, r, sizeof r);
            fprintf(out, "%s %s %s", i ? ";" : "", t->label[0] ? t->label : "(unnamed)", r);
        }
        fputc('\n', out);
    }
    if (mk.ruler) {
        fputs("ruler", out);
        for (int i = 0; i < mk.ruler->n; i++) {
            map_coord_name(mk.ruler->pts[i].x, mk.ruler->pts[i].y, at, sizeof at);
            fprintf(out, " %s", at);
        }
        map_coord_name(mk.ruler->cx, mk.ruler->cy, at, sizeof at);
        fprintf(out, " to %s, %s ft\n", at, dist);
    }
    for (int i = 0; i < a->npinged; i++) {
        const Ping *p = &a->pinged[i];
        char who[24];
        region_name(p->x0, p->y0, p->x1, p->y1, r, sizeof r);
        fprintf(out, "pinged by %s at %s, %lu s ago\n", ping_source(p->who, who, sizeof who), r,
                (unsigned long)(ping_age_ms(a, p) / 1000));
    }
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
    if (map_coord_parse(w, x, y) && map_in_bounds(m, *x, *y)) return 1;
    char edge[MAP_COORD_MAX];
    map_coord_name(m->w - 1, m->h - 1, edge, sizeof edge);
    snprintf(err, errsz, "%.40s is not a square on this map (A1 to %s)", w, edge);
    return 0;
}

/* A region for an edit: both ends on the map, never clipped -- an edit that
 * runs off the edge is a mistake to report, not to half do. */
static int region(const Map *m, const char *w, int *x0, int *y0, int *x1, int *y1,
                  char *err, size_t errsz)
{
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

/* The outline of a box of squares, as build mode walls a shape. */
static void outline(Undo *u, Map *m, int x0, int y0, int x1, int y1, uint8_t kind)
{
    EdShape sh = ed_shape(ED_SHAPE_RECT, x0, y0, x1, y1, 0);
    ed_wall_shape(m, u, &sh, kind);
}

#define BAD(...) do { snprintf(err, errsz, __VA_ARGS__); return -1; } while (0)

static int token_line(App *a, char w[][CTL_WORD_MAX], int n, Edits *ed, char *err, size_t errsz)
{
    Map  *m = a->map;
    Undo *u = &a->undo;
    if (n < 2) BAD("token add, token move, token del or token set");
    const char *sub = w[1];

    if (!strcmp(sub, "add")) {
        /* token add player|enemy SQ [size N] "Label" */
        if (n != 5 && n != 7) BAD("token add player|enemy SQUARE [size N] \"Label\"");
        Token t;
        memset(&t, 0, sizeof t);
        if (!strcmp(w[2], "player"))     t.kind = TOKEN_PLAYER;
        else if (!strcmp(w[2], "enemy")) t.kind = TOKEN_ENEMY;
        else BAD("%.20s: a creature is a player or an enemy", w[2]);
        int x, y, size = 1;
        if (!square(m, w[3], &x, &y, err, errsz)) return -1;
        if (n == 7) {
            if (!strcmp(w[5], "size")) BAD("the size goes before the label: token add enemy C3 size 2 \"Ogre\"");
            if (strcmp(w[4], "size") != 0 || !word_int(w[5], 1, 3, &size))
                BAD("size is 1, 2 or 3 squares wide, as: size 2");
        }
        const char *label = w[n - 1];
        if (!label_ok(m, label, -1, err, errsz) || !fits(m, x, y, size, -1, err, errsz)) return -1;
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
        if (!square(m, w[3], &x, &y, err, errsz) || !fits(m, x, y, t.size, i, err, errsz)) return -1;
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
        if (n != 5) BAD("token set WHO label \"...\", size N, or note \"...\"");
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
        else BAD("token set changes a label, a size or a note");
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
        if (n != 2) BAD("room REGION, like room B2:K12");
        if (!region(m, w[1], &x0, &y0, &x1, &y1, err, errsz)) return -1;
        for (int y = y0; y <= y1; y++)
            for (int x = x0; x <= x1; x++) undo_set_tile(u, m, x, y, TILE_FLOOR);
        outline(u, m, x0, y0, x1, y1, EDGE_WALL);
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
        if (!square(m, w[1], &x, &y, err, errsz)) return -1;
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

static int is_edit(const char *v)
{
    static const char *const EDITS[] = { "room", "tile", "wall", "edge", "note", "fog", "token", "stamp" };
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
        return 0;
    }
    if (!strcmp(v, "marked")) {
        int j = want_json(w, n, 1, err, errsz);
        if (j < 0) return -1;
        do_marked(a, out, j);
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
static int undo_alone(const char *p)
{
    int lines = 0, undo = 0;
    while (*p) {
        const char *end = strchr(p, '\n');
        size_t      ll  = end ? (size_t)(end - p) : strlen(p);
        char line[1024], w[CTL_WORDS][CTL_WORD_MAX], e[200];
        int  n = -1;
        if (ll < sizeof line) {
            memcpy(line, p, ll);
            line[ll] = '\0';
            n = split_words(line, w, e, sizeof e);
        }
        if (n != 0 && !(n > 0 && w[0][0] == '#')) {
            lines++;
            undo |= n > 0 && !strcmp(w[0], "undo");
        }
        p = end ? end + 1 : p + ll;
    }
    return !undo || lines == 1;
}

/* A request's edits are in: close the batch and tell both sides. */
static void finish_edits(App *a, Edits *ed, FILE *out)
{
    int changed = a->undo.stamp != ed->ops0;     /* a stamp a recorded op */
    undo_end(&a->undo);
    char area[2 * MAP_COORD_MAX + 2] = "";
    if (ed->x1 >= ed->x0) region_name(ed->x0, ed->y0, ed->x1, ed->y1, area, sizeof area);
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
    /* `undo` goes alone: it cannot be rolled back with the lines around it,
     * so a request that holds it holds nothing else. */
    if (strcmp(verdict, "ok") == 0 && !undo_alone(p))
        snprintf(verdict, sizeof verdict, "error: undo goes in a request of its own");
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
            n = split_words(line, w, err, sizeof err);
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
