/* What a proposal costs before any of it is wired to a key (docs/CONFLICTS.md
 * step 1): the scratch copy, an agent's plan on it, the change set from its
 * log, the conflict check, the preview swapped in and out for one 80x24
 * frame's window, and the accept. Median of 51, in microseconds, for the
 * small map and the largest, with the plan of five rooms and with a fill of
 * the whole map (the worst a preview can be). tools/proposals.sh builds and
 * runs it against the release objects. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "changeset.h"
#include "map.h"
#include "token.h"
#include "undo.h"

#define RUNS 51

static double now_us(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1e6 + t.tv_nsec / 1e3;
}

static int by_value(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return x < y ? -1 : x > y;
}

static double median(double *t)
{
    qsort(t, RUNS, sizeof t[0], by_value);
    return t[RUNS / 2];
}

static Map *encounter(int w, int h, int creatures)
{
    Map *m = map_new(w, h, "bench");
    map_fill_tiles(m, 0, 0, w - 1, h - 1, TILE_FLOOR);
    /* Every array written once, as a live map's are by the time anything is
     * proposed: a calloc'd page never touched faults when the copy reads it,
     * which would time the bench's setup, not the copy. */
    memset(m->vedges, 0, (size_t)(w + 1) * (size_t)h);
    memset(m->hedges, 0, (size_t)w * (size_t)(h + 1));
    memset(m->fog, 0, (size_t)w * (size_t)h);
    for (int i = 0; i < creatures; i++) {
        Token t;
        memset(&t, 0, sizeof t);
        t.x = (int16_t)(i % w);
        t.y = (int16_t)(h - 1 - i / w);
        t.size = 1;
        t.kind = TOKEN_ENEMY;
        snprintf(t.label, sizeof t.label, "Goblin %d", i + 1);
        tokens_add(&m->tokens, t);
    }
    return m;
}

/* Five 8x6 rooms with their outlines, as `room` would: through the log. */
static void five_rooms(Map *c, Undo *u)
{
    undo_begin(u);
    for (int r = 0; r < 5; r++) {
        int x0 = 1 + r * 7, y0 = 1;
        for (int y = y0; y < y0 + 6; y++)
            for (int x = x0; x < x0 + 6; x++) undo_set_tile(u, c, x, y, TILE_WATER);
        for (int y = y0; y < y0 + 6; y++) {
            undo_set_vedge(u, c, x0, y, EDGE_WALL);
            undo_set_vedge(u, c, x0 + 6, y, EDGE_WALL);
        }
        for (int x = x0; x < x0 + 6; x++) {
            undo_set_hedge(u, c, x, y0, EDGE_WALL);
            undo_set_hedge(u, c, x, y0 + 6, EDGE_WALL);
        }
    }
    undo_end(u);
}

static void whole_fill(Map *c, Undo *u)
{
    undo_begin(u);
    for (int y = 0; y < c->h; y++)
        for (int x = 0; x < c->w; x++) undo_set_tile(u, c, x, y, TILE_ROUGH);
    undo_end(u);
}

static void row(const char *what, int w, int h, int creatures, void (*plan)(Map *, Undo *))
{
    double copy[RUNS], work[RUNS], diff[RUNS], full[RUNS], check[RUNS], show[RUNS], apply[RUNS];
    Map *c = map_new(1, 1, "scratch");          /* kept, as the app keeps it: its pages stay */
    for (int k = 0; k < RUNS; k++) {
        Map *m = encounter(w, h, creatures);
        Undo scratch, u;
        undo_init(&scratch);
        undo_init(&u);

        double t0 = now_us();
        map_copy_into(c, m);
        copy[k] = now_us() - t0;

        t0 = now_us();
        plan(c, &scratch);
        work[k] = now_us() - t0;

        ChangeSet cs, all;
        cs_init(&cs);
        cs_init(&all);
        t0 = now_us();
        cs_diff(&cs, m, c, &scratch);
        diff[k] = now_us() - t0;

        t0 = now_us();
        cs_diff(&all, m, c, NULL);
        full[k] = now_us() - t0;

        t0 = now_us();
        cs_check(&cs, m);
        check[k] = now_us() - t0;

        /* One frame of an 80x24 terminal: about 40 squares by 21. */
        t0 = now_us();
        cs_show(&cs, m, 0, 0, 39, 20);
        cs_unshow(&cs, m);
        show[k] = now_us() - t0;

        t0 = now_us();
        cs_apply(&cs, m, &u, NULL, NULL, 0);
        apply[k] = now_us() - t0;

        cs_free(&cs);
        cs_free(&all);
        map_free(m);
        undo_free(&scratch);
        undo_free(&u);
    }
    map_free(c);
    printf("| %-22s | %dx%d, %d creatures | %7.1f | %7.1f | %7.1f | %7.1f | %7.1f | %7.1f | %7.1f |\n",
           what, w, h, creatures, median(copy), median(work), median(diff), median(full),
           median(check), median(show), median(apply));
}

int main(void)
{
    printf("| plan                   | map                     |    copy |    plan | diff (log) | diff (all) |   check | preview |  accept |\n");
    printf("|------------------------|-------------------------|---------|---------|---------|---------|---------|---------|---------|\n");
    row("five rooms", 40, 25, 24, five_rooms);
    row("five rooms", 512, 512, 24, five_rooms);
    row("five rooms", 512, 512, 500, five_rooms);
    row("fill the whole map", 512, 512, 24, whole_fill);
    return 0;
}
