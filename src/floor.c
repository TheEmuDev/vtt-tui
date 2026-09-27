#include "floor.h"

#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "prof.h"

int floor_at(const Map *m, int x, int y)
{
    for (int i = 0; i < m->nareas; i++)
        if (m->areas[i].floor && floor_holds(m, i, x, y)) return i;
    return -1;
}

int floor_of_token(const Map *m, const Token *t)
{
    return floor_at(m, t->x, t->y);
}

static const Map *g_order_map;   /* qsort has no context argument */

static int by_level(const void *pa, const void *pb)
{
    const Area *a = &g_order_map->areas[*(const int *)pa];
    const Area *b = &g_order_map->areas[*(const int *)pb];
    if (a->level != b->level) return a->level - b->level;
    return strcasecmp(a->name, b->name);
}

int floor_order(const Map *m, int out[MAP_AREAS_MAX])
{
    int n = 0;
    for (int i = 0; i < m->nareas; i++)
        if (m->areas[i].floor) out[n++] = i;
    g_order_map = m;
    qsort(out, (size_t)n, sizeof *out, by_level);
    return n;
}

int floor_step(const Map *m, int cur, int dir)
{
    int order[MAP_AREAS_MAX];
    int n = floor_order(m, order);
    if (!n) return -1;
    if (cur < 0) return dir > 0 ? order[0] : order[n - 1];
    for (int k = 0; k < n; k++)
        if (order[k] == cur) {
            int to = k + (dir > 0 ? 1 : -1);
            return to >= 0 && to < n ? order[to] : -1;
        }
    return -1;
}

const char *floor_problem(const Map *m, int ai)
{
    const Area *a = &m->areas[ai];
    for (int i = 0; i < m->nareas; i++) {
        const Area *o = &m->areas[i];
        if (i == ai || !o->floor) continue;
        if (a->x0 <= o->x1 && o->x0 <= a->x1 && a->y0 <= o->y1 && o->y0 <= a->y1)
            return "it would overlap another floor";
    }
    return NULL;
}

void floor_box(const Map *m, int f, int *x0, int *y0, int *x1, int *y1)
{
    if (f < 0 || f >= m->nareas) { *x0 = 0; *y0 = 0; *x1 = m->w - 1; *y1 = m->h - 1; return; }
    const Area *a = &m->areas[f];
    *x0 = a->x0; *y0 = a->y0; *x1 = a->x1; *y1 = a->y1;
}

int floor_pick(const Map *m, int kind, int shown, int last)
{
    PROF_ZONE("floor.pick");
    int count[MAP_AREAS_MAX] = { 0 }, any = 0;
    for (int i = 0; i < m->tokens.n; i++) {
        const Token *t = &m->tokens.v[i];
        if (t->kind != kind) continue;
        int f = floor_of_token(m, t);
        if (f >= 0) { count[f]++; any = 1; }
    }
    if (!any) return -1;
    if (shown >= 0 && shown < m->nareas && count[shown]) return shown;

    int best = -1;
    for (int i = 0; i < m->nareas; i++)
        if (count[i] && (best < 0 || count[i] > count[best])) best = i;
    int tied = 0;
    for (int i = 0; i < m->nareas; i++) tied += count[i] == count[best];
    if (tied == 1) return best;
    if (last >= 0 && last < m->nareas && count[last] == count[best]) return last;

    /* The lowest of the tied, in level order so the answer never depends on
     * the order areas were named in. */
    int order[MAP_AREAS_MAX];
    int n = floor_order(m, order);
    for (int k = 0; k < n; k++)
        if (count[order[k]] == count[best]) return order[k];
    return best;
}

const char *floor_name(const Map *m, int f)
{
    return f >= 0 && f < m->nareas ? m->areas[f].name : "the whole map";
}
