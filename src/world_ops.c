/* The operations both the GM and the channel make (world_ops.h). */

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "world_ops.h"
#include "floor.h"
#include "link.h"

static int refuse(WorldErr *e, int cls, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
static int refuse(WorldErr *e, int cls, const char *fmt, ...)
{
    e->cls = cls;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(e->text, sizeof e->text, fmt, ap);
    va_end(ap);
    return -1;
}

/* ------------------------------------------------------------------ areas */

int world_area_name(const char *name, WorldErr *e)
{
    if (map_area_name_ok(name)) return 0;
    return refuse(e, CTL_E_USAGE, "%.40s: an area's name is 1-%d characters, no quote or colon, and not a square",
                  name, AREA_NAME_MAX - 1);
}

int world_area_fits(const World *w, const char *name, int x0, int y0, int x1, int y1, WorldErr *e)
{
    if (world_area_name(name, e) < 0) return -1;
    int ai = map_area_find(w->map, name);
    const char *why = floor_problem_box(w->map, ai, x0, y0, x1, y1);
    if (why) return refuse(e, CTL_E_CONFLICT, "%.40s is a floor, and %s", w->map->areas[ai].name, why);
    return 0;
}

int world_area_set(World *w, const char *name, int x0, int y0, int x1, int y1, WorldErr *e)
{
    if (world_area_fits(w, name, x0, y0, x1, y1, e) < 0) return -1;
    if (!undo_set_area(&w->undo, w->map, name, x0, y0, x1, y1))
        return refuse(e, CTL_E_LIMIT, "no room: a map holds %d named areas", MAP_AREAS_MAX);
    return 0;
}

int world_area_remove(World *w, const char *name, int box[4], WorldErr *e)
{
    if (world_area_name(name, e) < 0) return -1;
    int ai = map_area_find(w->map, name);
    if (ai < 0) return refuse(e, CTL_E_MISSING, "no area called %.40s", name);
    const Area *ar = &w->map->areas[ai];
    box[0] = ar->x0; box[1] = ar->y0; box[2] = ar->x1; box[3] = ar->y1;
    undo_remove_area(&w->undo, w->map, name);
    return 0;
}

/* ------------------------------------------------------------------ notes */

int world_note_set(World *w, int x, int y, const char *text, WorldErr *e)
{
    if (strlen(text) >= NOTE_MAX) return refuse(e, CTL_E_LIMIT, "the note is over %d characters", NOTE_MAX - 1);
    if (!undo_set_note(&w->undo, w->map, x, y, text))
        return refuse(e, CTL_E_LIMIT, "no room: a map holds %d notes on squares", MAP_NOTES_MAX);
    return 0;
}

/* ------------------------------------------------------------------ links */

int world_link_num(const World *w, WorldErr *e)
{
    int num = link_free_num(w->map);
    if (!num) return refuse(e, CTL_E_LIMIT, "no room: a map holds %d links", MAP_LINKS_MAX);
    return num;
}

int world_link_set(World *w, const Link *l, WorldErr *e)
{
    const char *why = link_problem(w->map, l);
    if (why) return refuse(e, CTL_E_CONFLICT, "%s", why);
    (void)undo_set_link(&w->undo, w->map, l);
    return 0;
}

int world_link_remove(World *w, int num, WorldErr *e)
{
    if (link_find(w->map, num) < 0) return refuse(e, CTL_E_MISSING, "there is no link %d", num);
    undo_remove_link(&w->undo, w->map, num);
    return 0;
}
