/* The floor view (docs/FLOORS.md): which floor the GM's screen shows, [ and ]
 * to step between them, :floor to mark and show them, and the rule that the
 * view follows the cursor -- whatever put the cursor on another floor (g o,
 * a jump, the turn passing) takes the view there. What a floor is, is
 * floor.c's. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "app_priv.h"
#include "floor.h"
#include "prof.h"
#include "turn.h"

int app_floor_shown(const App *a)
{
    if (!a->map || !a->ed.floor[0]) return -1;
    int i = map_area_find(a->map, a->ed.floor);
    return i >= 0 && a->map->areas[i].floor ? i : -1;
}

/* The square the cursor names: wall mode's corner is its south-east square. */
static void cursor_square(const App *a, int *x, int *y)
{
    ed_cursor_tile(&a->ed, x, y);
    *x = iclamp(*x, 0, a->map->w - 1);
    *y = iclamp(*y, 0, a->map->h - 1);
}

static void show(App *a, int f)
{
    Editor *e = &a->ed;
    Map    *m = a->map;
    if (f >= 0) str_lcpy(e->floor, m->areas[f].name, sizeof e->floor);
    else        e->floor[0] = '\0';
    e->view.bounded = f >= 0;
    floor_box(m, f, &e->view.bx0, &e->view.by0, &e->view.bx1, &e->view.by1);
    grid_clamp_camera(&e->view, m);
}

void app_floor_sync(App *a)
{
    if (!a->map) return;
    Editor *e = &a->ed;
    Map    *m = a->map;
    (void)app_players_floor(a);     /* a turn may have started; the party may have moved */
    int f = app_floor_shown(a);
    show(a, f);                     /* the box may have moved, or the floor gone */
    if (f < 0) return;

    int x, y;
    cursor_square(a, &x, &y);
    if (floor_holds(m, f, x, y)) return;
    /* Wall mode's cursor is a corner, and the corners round a floor's far
     * edge -- where its outer wall is drawn -- belong to it. */
    if (e->mode == ED_WALL && e->wx >= m->areas[f].x0 && e->wx <= m->areas[f].x1 + 1 &&
        e->wy >= m->areas[f].y0 && e->wy <= m->areas[f].y1 + 1)
        return;

    /* Something put the cursor off the floor: the view goes with it. */
    PROF_ZONE("floor.switch");
    show(a, floor_at(m, x, y));
    grid_center_on(&e->view, m, x, y);
}

void app_floor_show(App *a, int f)
{
    Editor *e = &a->ed;
    Map    *m = a->map;
    int from = app_floor_shown(a);
    if (f == from) return;
    PROF_ZONE("floor.switch");

    /* The cursor keeps its place in the box: floors drawn the same size
     * line up, and so do the stairs between them. */
    int x, y;
    cursor_square(a, &x, &y);
    int fx0, fy0, fx1, fy1, tx0, ty0, tx1, ty1;
    floor_box(m, from, &fx0, &fy0, &fx1, &fy1);
    floor_box(m, f, &tx0, &ty0, &tx1, &ty1);
    int nx = from >= 0 && f >= 0 ? iclamp(tx0 + (x - fx0), tx0, tx1) : iclamp(x, tx0, tx1);
    int ny = from >= 0 && f >= 0 ? iclamp(ty0 + (y - fy0), ty0, ty1) : iclamp(y, ty0, ty1);
    if (a->play.grabbed) { nx = x; ny = y; }     /* never carried off by a view change */
    show(a, f);
    if (!a->play.grabbed) {
        e->cx = nx; e->cy = ny;
        e->wx = nx; e->wy = ny;
    }
    grid_center_on(&e->view, m, e->cx, e->cy);
}

void app_floor_step(App *a, int dir)
{
    Map *m = a->map;
    if (a->play.grabbed) { app_set_status(a, "put the creature down before changing floors"); return; }
    int order[MAP_AREAS_MAX];
    if (!floor_order(m, order)) {
        app_set_status_gm(a, "no floors - :floor NAME LEVEL makes a named area one");
        return;
    }
    int cur = app_floor_shown(a);
    int to  = floor_step(m, cur, dir);
    char msg[96];
    if (to < 0) {
        snprintf(msg, sizeof msg, "%s is the %s floor", floor_name(m, cur), dir > 0 ? "top" : "bottom");
        app_set_status_gm(a, msg);
        return;
    }
    app_floor_show(a, to);
    snprintf(msg, sizeof msg, "%s, level %d", m->areas[to].name, m->areas[to].level);
    app_set_status_gm(a, msg);
}

static void list_floors(App *a)
{
    Map *m = a->map;
    int  order[MAP_AREAS_MAX], n = floor_order(m, order);
    char msg[sizeof a->status];
    if (!n) { app_set_status_gm(a, "no floors - :floor NAME LEVEL makes a named area one"); return; }
    int off = snprintf(msg, sizeof msg, "floors:");
    for (int k = n - 1; k >= 0 && off < (int)sizeof msg; k--) {
        const Area *f = &m->areas[order[k]];
        off += snprintf(msg + off, sizeof msg - (size_t)off, "%s %s %d%s", k < n - 1 ? "," : "",
                        f->name, f->level, order[k] == app_floor_shown(a) ? " (shown)" : "");
    }
    app_set_status_gm(a, msg);
}

void app_floor_command(App *a, const char *verb, const char *rest)
{
    Map *m = a->map;
    char msg[160];
    if (!strcmp(verb, "floors") || !*rest) { list_floors(a); return; }

    char name[AREA_NAME_MAX + 16];
    str_lcpy(name, rest, sizeof name);
    size_t len = strlen(name);
    while (len && name[len - 1] == ' ') name[--len] = '\0';
    if (!strcmp(name, "all")) {
        if (a->play.grabbed) { app_set_status(a, "put the creature down before changing floors"); return; }
        app_floor_show(a, -1);
        app_set_status_gm(a, "the whole map");
        return;
    }

    /* The last word: off, a level, or part of the name. */
    int   off = str_cut_word(name, "off"), level = 0, has_level = 0;
    char *last = off ? NULL : strrchr(name, ' ');
    if (last) {
        char *end;
        long  v = strtol(last + 1, &end, 10);
        if (last[1] && !*end) { level = (int)v; has_level = 1; *last = '\0'; }
        len = strlen(name);                     /* "Ground  0": the name ends at its last letter */
        while (len && name[len - 1] == ' ') name[--len] = '\0';
    }
    int ai = map_area_find(m, name);
    if (ai < 0) {
        snprintf(msg, sizeof msg, "no area called %.40s - :area names one first", name);
        app_set_status_gm(a, msg);
        return;
    }
    if (off) {
        if (!m->areas[ai].floor) { snprintf(msg, sizeof msg, "%.40s is not a floor", name); app_set_status_gm(a, msg); return; }
        undo_begin(&a->undo);
        undo_set_floor(&a->undo, m, m->areas[ai].name, 0, 0);
        undo_end(&a->undo);
        snprintf(msg, sizeof msg, "%.40s is no longer a floor", m->areas[ai].name);
        app_note_gm(a, msg);
        app_floor_sync(a);
        return;
    }
    if (has_level) {
        if (level < FLOOR_LEVEL_MIN || level > FLOOR_LEVEL_MAX) {
            snprintf(msg, sizeof msg, "a level is %d to %d", FLOOR_LEVEL_MIN, FLOOR_LEVEL_MAX);
            app_set_status_gm(a, msg);
            return;
        }
        const char *why = floor_problem(m, ai);
        if (why) { snprintf(msg, sizeof msg, "%.40s cannot be a floor: %s", name, why); app_set_status_gm(a, msg); return; }
        undo_begin(&a->undo);
        undo_set_floor(&a->undo, m, m->areas[ai].name, 1, level);
        undo_end(&a->undo);
        snprintf(msg, sizeof msg, "%.40s is a floor, level %d - [ and ] step between floors", m->areas[ai].name, level);
        app_note_gm(a, msg);
        return;
    }
    if (!m->areas[ai].floor) {
        snprintf(msg, sizeof msg, "%.40s is not a floor - :floor %.40s 0 makes it one at level 0", name, name);
        app_set_status_gm(a, msg);
        return;
    }
    if (a->play.grabbed) { app_set_status(a, "put the creature down before changing floors"); return; }
    app_floor_show(a, ai);
    snprintf(msg, sizeof msg, "%s, level %d", m->areas[ai].name, m->areas[ai].level);
    app_set_status_gm(a, msg);
}

/* ---------------------------------------------------------- the players */

static int floor_named(const Map *m, const char *name)
{
    if (!name[0]) return -1;
    int i = map_area_find(m, name);
    return i >= 0 && m->areas[i].floor ? i : -1;
}

static void name_floor(const Map *m, int f, char *out, size_t sz)
{
    str_lcpy(out, f >= 0 ? m->areas[f].name : "", sz);
}

void app_floor_reset(App *a)
{
    a->pfloor[0] = a->ppin[0] = a->pview_floor[0] = '\0';
    a->side_floor[0][0] = a->side_floor[1][0] = '\0';
    a->last_acting = -1;
    a->psplit = 0;
    /* A held view is a place on the map put down; party and follow carry
     * on to the next one. */
    if (a->pcam == PCAM_HOLD) a->pcam = PCAM_FOLLOW;
    a->pcam_for.map = NULL;
}

void app_floor_note_move(App *a, const Token *t)
{
    if (t->hidden) return;          /* the tie it would break is the players' to see */
    int f = floor_of_token(a->map, t);
    if (f >= 0) name_floor(a->map, f, a->side_floor[t->kind == TOKEN_ENEMY], sizeof a->side_floor[0]);
}

int app_players_floor(App *a)
{
    Map *m = a->map;
    if (!m) return -1;
    int stored = floor_named(m, a->pfloor);

    /* A player creature's turn starting takes the players there, pin or
     * not: the turn is the stronger word. */
    /* A hidden creature's turn is the GM's business: it moves nobody's
     * screens, or the players would be taken to where it is. */
    int acting = turn_acting(m);
    int player_turn = acting >= 0 && m->tokens.v[acting].kind == TOKEN_PLAYER && !m->tokens.v[acting].hidden;
    if (acting != a->last_acting) {
        a->last_acting = acting;
        if (player_turn) a->ppin[0] = '\0';
    }

    int f;
    int pin = floor_named(m, a->ppin);
    if (pin >= 0)
        f = pin;
    else if (player_turn)
        f = floor_of_token(m, &m->tokens.v[acting]);
    else if (acting >= 0 || (turn_spotlight_ruleset(m) && m->spotlight == SPOTLIGHT_GM))
        f = stored;                     /* the GM's turn: the players stay where they are */
    else {
        f = floor_pick(m, TOKEN_PLAYER, stored, floor_named(m, a->side_floor[0]));
        if (f < 0) f = stored;
    }
    name_floor(m, f, a->pfloor, sizeof a->pfloor);
    return f;
}

int app_players_split(const App *a)
{
    if (!a->map || a->screen != SCREEN_PLAY) return 0;
    int f = floor_named(a->map, a->pfloor);
    int order[MAP_AREAS_MAX];
    if (!floor_order(a->map, order)) return 0;
    return f != app_floor_shown(a);
}

int app_players_own_camera(const App *a)
{
    if (!a->map || a->screen != SCREEN_PLAY) return 0;
    return a->pcam != PCAM_FOLLOW || app_players_split(a);
}

/* The squares every player creature the players can see on floor f takes,
 * hidden ones left out; 0 when there are none. */
static int party_box(const Map *m, int f, int *x0, int *y0, int *x1, int *y1)
{
    int n = 0;
    for (int i = 0; i < m->tokens.n; i++) {
        const Token *t = &m->tokens.v[i];
        if (t->kind != TOKEN_PLAYER || t->hidden || !floor_holds(m, f, t->x, t->y)) continue;
        int tx1 = t->x + t->size - 1, ty1 = t->y + t->size - 1;
        if (!n++) { *x0 = t->x; *y0 = t->y; *x1 = tx1; *y1 = ty1; continue; }
        *x0 = imin(*x0, t->x); *y0 = imin(*y0, t->y);
        *x1 = imax(*x1, tx1);  *y1 = imax(*y1, ty1);
    }
    return n;
}

/* Squares round the party a party camera keeps on screen: one inside it
 * moves the camera. */
#define PARTY_MARGIN 2

/* Party: the GM's zoom, or the closest out at which the party fits with its
 * margin. The camera stays put until a creature comes inside the margin,
 * then centers on them all: a TV that swims with every step is worse than
 * one that jumps now and then. Too spread out even at the farthest zoom, it
 * frames the creature whose turn it is, else the party's middle, kept the
 * same way. */
static void party_frame(App *a, int f)
{
    PROF_ZONE("camera.party");
    const Map *m = a->map;
    GridView  *g = &a->pview;
    int x0, y0, x1, y1;
    if (!party_box(m, f, &x0, &y0, &x1, &y1)) { g->zoom = a->ed.view.zoom; return; }

    /* The box with its margin, cut to what can be shown: a creature at the
     * floor's edge has no margin beyond it to keep. */
    int bx0, by0, bx1, by1;
    grid_bounds(g, m, &bx0, &by0, &bx1, &by1);
    int mx0 = imax(x0 - PARTY_MARGIN, bx0), my0 = imax(y0 - PARTY_MARGIN, by0);
    int mx1 = imin(x1 + PARTY_MARGIN, bx1), my1 = imin(y1 + PARTY_MARGIN, by1);
    #define FITS(z) ((mx1 - mx0 + 1) * zoom_pw(z) + 1 <= g->view.w && (my1 - my0 + 1) * zoom_ph(z) + 1 <= g->view.h)
    int z = a->ed.view.zoom;
    while (z > 0 && !FITS(z)) z--;
    int cx = (x0 + x1) / 2, cy = (y0 + y1) / 2;
    if (!FITS(z)) {
        /* The one square framed has the same margin, so this does not
         * swim either. */
        int acting = turn_acting(m);
        const Token *t = acting >= 0 ? &m->tokens.v[acting] : NULL;
        if (t && t->kind == TOKEN_PLAYER && !t->hidden && floor_holds(m, f, t->x, t->y)) {
            cx = t->x + (t->size - 1) / 2;
            cy = t->y + (t->size - 1) / 2;
        }
        mx0 = imax(cx - PARTY_MARGIN, bx0); my0 = imax(cy - PARTY_MARGIN, by0);
        mx1 = imin(cx + PARTY_MARGIN, bx1); my1 = imin(cy + PARTY_MARGIN, by1);
    }
    #undef FITS
    int pw = zoom_pw(z), ph = zoom_ph(z);
    int inside = mx0 * pw >= g->cam_x && (mx1 + 1) * pw + 1 <= g->cam_x + g->view.w &&
                 my0 * ph >= g->cam_y && (my1 + 1) * ph + 1 <= g->cam_y + g->view.h;
    if (z != g->zoom || !inside) {
        g->zoom = z;
        grid_center_on(g, m, cx, cy);
    }
}

/* On a floor of theirs: where the party stands, on average, else the
 * floor's middle. */
static void center_on_party(App *a, int f)
{
    const Map *m = a->map;
    GridView  *g = &a->pview;
    long sx = 0, sy = 0, n = 0;
    for (int i = 0; i < m->tokens.n; i++) {
        const Token *t = &m->tokens.v[i];
        if (t->kind != TOKEN_PLAYER || t->hidden || !floor_holds(m, f, t->x, t->y)) continue;
        sx += t->x; sy += t->y; n++;
    }
    if (n) grid_center_on(g, m, (int)(sx / n), (int)(sy / n));
    else   grid_center_on(g, m, (g->bx0 + g->bx1) / 2, (g->by0 + g->by1) / 2);
}

void app_players_camera(App *a)
{
    Map *m = a->map;
    int  f = floor_named(m, a->pfloor);
    GridView *g = &a->pview;
    if (a->pcam == PCAM_FOLLOW) g->zoom = a->ed.view.zoom;    /* party picks its own; hold keeps it */
    g->view = a->ed.view.view;
    g->bounded = f >= 0;
    floor_box(m, f, &g->bx0, &g->by0, &g->bx1, &g->by1);

    if (a->pcam == PCAM_PARTY) {
        /* Worked out again only when the map, their floor, the GM's zoom or
         * the screen changed: nothing else moves the party. */
        if (a->pcam_for.map != m || a->pcam_for.gen != m->gen || a->pcam_for.floor != f ||
            a->pcam_for.zoom != a->ed.view.zoom || memcmp(&a->pcam_for.view, &g->view, sizeof g->view)) {
            a->pcam_for.map  = m;
            a->pcam_for.gen  = m->gen;
            a->pcam_for.floor = f;
            a->pcam_for.zoom = a->ed.view.zoom;
            a->pcam_for.view = g->view;
            party_frame(a, f);
        }
        str_lcpy(a->pview_floor, a->pfloor, sizeof a->pview_floor);
    } else if (strcmp(a->pview_floor, a->pfloor) != 0) {
        /* Follow on a split floor, and hold: centered on the party when
         * their floor changes, held after that, so their screens do not
         * swim with every step. */
        str_lcpy(a->pview_floor, a->pfloor, sizeof a->pview_floor);
        center_on_party(a, f);
    }
    grid_clamp_camera(g, m);
}

/* :player camera follow|party|hold. Hold takes the GM's view as it is now
 * -- given again, it shows the table wherever the GM has got to -- and
 * the GM's floor with it, pinned as :player floor pins. */
void app_players_camera_command(App *a, const char *rest)
{
    static const char *const NAME[] = { "follow", "party", "hold" };
    Map *m = a->map;
    int mode = -1;
    for (int k = 0; k < 3; k++) if (!strcmp(rest, NAME[k])) mode = k;
    if (mode < 0) {
        char msg[128];
        snprintf(msg, sizeof msg, "the players' camera is on %s - :player camera follow|party|hold", NAME[a->pcam]);
        app_set_status_gm(a, msg);
        return;
    }
    a->pcam = (PlayersCamera)mode;
    a->pcam_for.map = NULL;
    if (mode == PCAM_HOLD) {
        int shown = app_floor_shown(a);
        if (shown >= 0) {
            str_lcpy(a->ppin, m->areas[shown].name, sizeof a->ppin);
            str_lcpy(a->pfloor, m->areas[shown].name, sizeof a->pfloor);
        }
        a->pview = a->ed.view;
        str_lcpy(a->pview_floor, a->pfloor, sizeof a->pview_floor);
    }
    app_set_status_gm(a, mode == PCAM_FOLLOW ? "the players' screens follow yours"
                       : mode == PCAM_PARTY  ? "the players' screens frame the party - :player camera follow returns"
                       :                       "the players' screens hold this view - :player camera hold again moves it here");
}

void app_floor_spotlight(App *a)
{
    Map *m = a->map;
    int  f;
    if (m->spotlight == SPOTLIGHT_PLAYERS) f = app_players_floor(a);
    else f = floor_pick(m, TOKEN_ENEMY, app_floor_shown(a), floor_named(m, a->side_floor[1]));
    if (f >= 0) app_floor_show(a, f);
}

void app_players_pin(App *a, const char *rest)
{
    Map *m = a->map;
    char msg[96];
    if (!strcmp(rest, "auto")) {
        /* A fresh pick: staying put would keep them where the pin held. */
        a->ppin[0] = a->pfloor[0] = '\0';
        app_set_status_gm(a, "the players' screens follow the party");
        return;
    }
    int f = floor_named(m, rest);
    if (f < 0) {
        snprintf(msg, sizeof msg, "no floor called %.40s - :floors lists them", rest);
        app_set_status_gm(a, msg);
        return;
    }
    str_lcpy(a->ppin, m->areas[f].name, sizeof a->ppin);
    snprintf(msg, sizeof msg, "the players' screens stay on %s until a player's turn - :player floor auto",
             m->areas[f].name);
    app_set_status_gm(a, msg);
}
