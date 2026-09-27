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
    int f = app_floor_shown(a);
    show(a, f);                     /* the box may have moved, or the floor gone */
    if (f < 0) return;

    int x, y;
    cursor_square(a, &x, &y);
    if (floor_holds(m, f, x, y)) return;

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
    if (!strcmp(name, "all")) {
        if (a->play.grabbed) { app_set_status(a, "put the creature down before changing floors"); return; }
        app_floor_show(a, -1);
        app_set_status_gm(a, "the whole map");
        return;
    }

    /* The last word: off, a level, or part of the name. */
    char *last = strrchr(name, ' ');
    int   off = 0, level = 0, has_level = 0;
    if (last) {
        char *end;
        long  v = strtol(last + 1, &end, 10);
        if (!strcmp(last + 1, "off"))           { off = 1; *last = '\0'; }
        else if (last[1] && !*end)              { level = (int)v; has_level = 1; *last = '\0'; }
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
