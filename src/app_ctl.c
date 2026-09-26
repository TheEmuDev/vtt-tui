/* The control channel's requests: an agent reading, and editing, the map the
 * GM has open. ctl.c moves the bytes; this reads the lines and runs them
 * against the App. The language and its rules are docs/CONTROL.md's. */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_priv.h"
#include "json.h"
#include "maptools.h"
#include "prof.h"
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
    return NULL;
}

/* ----------------------------------------------------------------- reads */

static void do_status(App *a, FILE *out)
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
    fprintf(out, "undo %d back, %d forward\n", a->undo.depth, a->undo.nmarks - a->undo.depth);
    const char *busy = app_ctl_busy(a);
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
    const Ruler *ruler;              /* NULL when not measuring */
} Marked;

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
    if (mk->bx1 < mk->bx0) { mk->bx0 = mk->bx1 = x0; mk->by0 = mk->by1 = y0; }
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
            map_coord_name(mk.wx, mk.wy, at, sizeof at);
            json_kstr(&j, "top_left_of", at);
            json_close(&j, '}');
        } else json_null(&j);
        json_key(&j, "box");
        if (mk.box) {
            json_open(&j, '{');
            json_kstr(&j, "shape", mk.box == ED_SHAPE_CIRCLE + 1 ? "circle" : "rect");
            json_key(&j, "between");
            json_str(&j, mk.box_corners ? "corners" : "squares");
            j_region(&j, "squares", mk.bx0, mk.by0, mk.bx1, mk.by1);
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
                json_kstr(&j, "kind", t->kind == TOKEN_ENEMY ? "enemy" : "player");
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
        map_coord_name(mk.wx, mk.wy, at, sizeof at);
        fprintf(out, "corner at the top left of %s\n", at);
    }
    if (mk.box) {
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

/* ------------------------------------------------------------------- run */

/* Runs one line. 0, or -1 with why in err. */
static int run_line(App *a, char w[][CTL_WORD_MAX], int n, FILE *out, char *err, size_t errsz)
{
    Map *m = a->map;
    const char *v = w[0];

    if (!strcmp(v, "status")) {
        if (n > 1) { snprintf(err, errsz, "status takes nothing after it"); return -1; }
        do_status(a, out);
        return 0;
    }
    if (!strcmp(v, "dump")) {
        int x0 = 0, y0 = 0, x1 = m->w - 1, y1 = m->h - 1;
        if (n > 2) { snprintf(err, errsz, "dump takes one region, like B2:K12"); return -1; }
        if (n == 2 && !maptools_region(m, w[1], &x0, &y0, &x1, &y1)) {
            snprintf(err, errsz, "%.40s is not a region on this map, like B2:K12", w[1]);
            return -1;
        }
        maptools_dump(out, m, x0, y0, x1, y1);
        return 0;
    }
    if (!strcmp(v, "describe")) {
        int j = want_json(w, n, 1, err, errsz);
        if (j < 0) return -1;
        maptools_describe(out, m, j);
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

char *app_ctl_exec(App *a, const char *req, size_t *len)
{
    PROF_ZONE("ctl");
    char  *body = NULL;
    size_t blen = 0;
    FILE  *out  = open_memstream(&body, &blen);
    if (!out) return NULL;

    char verdict[320] = "ok";
    int  lineno = 0;
    const char *p = req;
    if (strlen(req) != *len) {
        snprintf(verdict, sizeof verdict, "error: a nul byte in the request");
        p = "";
    }
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
        if (!err[0] && run_line(a, w, n, out, err, sizeof err) == 0) continue;
        snprintf(verdict, sizeof verdict, "error: line %d: %s", lineno, err);
        break;
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
