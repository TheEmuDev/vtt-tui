/* The operations both the GM and the channel make (world_ops.h). */

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "world_ops.h"
#include "card.h"
#include "floor.h"
#include "link.h"
#include "scene.h"
#include "stamp.h"

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
    undo_begin(&w->undo);
    int ok = undo_set_area(&w->undo, w->map, name, x0, y0, x1, y1);
    undo_end(&w->undo);
    if (!ok) return refuse(e, CTL_E_LIMIT, MAP_FULL_FMT, MAP_AREAS_MAX, "named areas");
    return 0;
}

int world_area_remove(World *w, const char *name, int box[4], WorldErr *e)
{
    if (world_area_name(name, e) < 0) return -1;
    int ai = map_area_find(w->map, name);
    if (ai < 0) return refuse(e, CTL_E_MISSING, "no area called %.40s", name);
    const Area *ar = &w->map->areas[ai];
    box[0] = ar->x0; box[1] = ar->y0; box[2] = ar->x1; box[3] = ar->y1;
    undo_begin(&w->undo);
    undo_remove_area(&w->undo, w->map, name);
    undo_end(&w->undo);
    return 0;
}

/* ------------------------------------------------------------------ notes */

int world_note_set(World *w, int x, int y, const char *text, WorldErr *e)
{
    if (strlen(text) >= NOTE_MAX) return refuse(e, CTL_E_LIMIT, "the note is over %d characters", NOTE_MAX - 1);
    undo_begin(&w->undo);
    int ok = undo_set_note(&w->undo, w->map, x, y, text);
    undo_end(&w->undo);
    if (!ok) return refuse(e, CTL_E_LIMIT, MAP_FULL_FMT, MAP_NOTES_MAX, "notes on squares");
    return 0;
}

/* ------------------------------------------------------------------ links */

int world_link_num(const World *w, WorldErr *e)
{
    int num = link_free_num(w->map);
    if (!num) return refuse(e, CTL_E_LIMIT, MAP_FULL_FMT, MAP_LINKS_MAX, "links");
    return num;
}

int world_link_set(World *w, const Link *l, WorldErr *e)
{
    const char *why = link_problem(w->map, l);
    if (why) return refuse(e, CTL_E_CONFLICT, "%s", why);
    undo_begin(&w->undo);
    (void)undo_set_link(&w->undo, w->map, l);
    undo_end(&w->undo);
    return 0;
}

int world_link_remove(World *w, int num, WorldErr *e)
{
    if (link_find(w->map, num) < 0) return refuse(e, CTL_E_MISSING, "there is no link %d", num);
    undo_begin(&w->undo);
    undo_remove_link(&w->undo, w->map, num);
    undo_end(&w->undo);
    return 0;
}

/* ----------------------------------------------------------------- floors */

int world_floor_set(World *w, int ai, int level, WorldErr *e)
{
    const Area *ar = &w->map->areas[ai];
    if (level < FLOOR_LEVEL_MIN || level > FLOOR_LEVEL_MAX)
        return refuse(e, CTL_E_USAGE, "a level is %d to %d", FLOOR_LEVEL_MIN, FLOOR_LEVEL_MAX);
    const char *why = floor_problem(w->map, ai);
    if (why) return refuse(e, CTL_E_CONFLICT, "%.40s cannot be a floor: %s", ar->name, why);
    undo_begin(&w->undo);
    undo_set_floor(&w->undo, w->map, ar->name, 1, level);
    undo_end(&w->undo);
    return 0;
}

int world_floor_off(World *w, int ai, WorldErr *e)
{
    const Area *ar = &w->map->areas[ai];
    if (!ar->floor) return refuse(e, CTL_E_STATE, "%.40s is not a floor", ar->name);
    undo_begin(&w->undo);
    undo_set_floor(&w->undo, w->map, ar->name, 0, 0);
    undo_end(&w->undo);
    return 0;
}

/* ----------------------------------------------------------- environments */

int world_card_name(const char *card, WorldErr *e)
{
    if (card_name_ok(card)) return 0;
    return refuse(e, CTL_E_USAGE, "%.40s: a card's name is letters, digits, - and _", card);
}

int world_area_env(World *w, int ai, const char *card, WorldErr *e)
{
    const Area *ar = &w->map->areas[ai];
    if (card[0] && world_card_name(card, e) < 0) return -1;
    if (!card[0] && !ar->env[0]) return refuse(e, CTL_E_STATE, "%.40s has no environment", ar->name);
    undo_begin(&w->undo);
    undo_set_area_env(&w->undo, w->map, ar->name, card);
    undo_end(&w->undo);
    return 0;
}

/* ----------------------------------------------------------------- stamps */

int world_stamp_place(World *w, const Map *s, int x, int y, WorldErr *e)
{
    int full;
    if (stamp_place(w->map, &w->undo, s, x, y, &full, e->text, sizeof e->text)) return 0;
    e->cls = full ? CTL_E_LIMIT : CTL_E_CONFLICT;
    return -1;
}

/* ----------------------------------------------------------------- scenes */

int world_scene_find(const World *w, const char *name, WorldErr *e)
{
    int i = scene_find(w->map, name);
    if (i < 0) return refuse(e, CTL_E_MISSING, "no scene called %.31s", name);
    return i;
}

int world_scene_restore(World *w, int i, WorldErr *e)
{
    int n = scene_restore(w->map, &w->undo, i, e->text, sizeof e->text);
    if (n < 0) e->cls = CTL_E_CONFLICT;
    return n;
}

int world_scene_save(World *w, const char *name, const int box[4], WorldErr *e)
{
    int i = scene_save(w->map, name, box, e->text, sizeof e->text);
    if (i < 0) e->cls = scene_name_ok(name) ? CTL_E_LIMIT : CTL_E_USAGE;     /* its two refusals: the name, or no room */
    return i;
}
