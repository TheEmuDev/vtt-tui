/* What a proposal costs before any of it is wired to a key (docs/CONFLICTS.md
 * step 1): the scratch copy, an agent's plan on it, the change set from its
 * log, the conflict check, the preview swapped in and out for one 80x24
 * frame's window (the first frame and each one after), and the accept. Median of 51, in microseconds, for the
 * small map and the largest, with the plan of five rooms and with a fill of
 * the whole map (the worst a preview can be). tools/proposals.sh builds and
 * runs it against the release objects. */
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "changeset.h"
#include "mapio.h"
#include "checkpoint.h"
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
    double copy[RUNS], work[RUNS], diff[RUNS], full[RUNS], check[RUNS], first[RUNS], frame[RUNS], apply[RUNS];
    /* The live map is copied from a base each run, into one kept map, so a
     * row times the work and not fresh pages for the setup; the scratch map
     * is kept as the app keeps it. */
    Map *base = encounter(w, h, creatures);
    Map *m = map_new(1, 1, "live");
    Map *c = map_new(1, 1, "scratch");
    for (int k = 0; k < RUNS; k++) {
        map_copy_into(m, base);
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

        /* One frame of an 80x24 terminal, about 40 squares by 21: the first
         * builds the preview's creatures, links and notes; every one after is
         * the swap alone. */
        t0 = now_us();
        cs_show(&cs, m, 0, 0, 39, 20);
        cs_unshow(&cs, m);
        first[k] = now_us() - t0;
        t0 = now_us();
        cs_show(&cs, m, 0, 0, 39, 20);
        cs_unshow(&cs, m);
        frame[k] = now_us() - t0;

        t0 = now_us();
        cs_apply(&cs, m, &u, NULL, NULL, 0);
        apply[k] = now_us() - t0;

        cs_free(&cs);
        cs_free(&all);
        undo_free(&scratch);
        undo_free(&u);
    }
    map_free(c);
    map_free(m);
    map_free(base);
    char label[64];
    snprintf(label, sizeof label, "%s, %dx%d, %d creatures", what, w, h, creatures);
    printf("| %-41s | %7.1fus | %7.1fus | %7.1fus | %7.1fus | %7.1fus | %7.1fus | %7.1fus | %7.1fus |\n",
           label, median(copy), median(work), median(diff), median(full),
           median(check), median(first), median(frame), median(apply));
}

/* The checkpoint (step 2): starting one, and what it costs the edits made
 * while it runs -- recording a fill of the whole map and an undo step, with
 * it off and on -- and reading the changes after a dozen edits and after a
 * fill. */
static void checkpoint_row(int w, int h, int creatures)
{
    double start[RUNS], fill_off[RUNS], fill_on[RUNS], step_off[RUNS], step_on[RUNS], dozen[RUNS], all[RUNS];
    Map *base = encounter(w, h, creatures);
    Map *m = map_new(1, 1, "live");
    for (int k = 0; k < RUNS; k++) {
        for (int on = 0; on <= 1; on++) {
            map_copy_into(m, base);
            Undo u;
            undo_init(&u);
            if (on) {
                double t0 = now_us();
                checkpoint_start(m);
                start[k] = now_us() - t0;
                undo_begin(&u);                       /* a dozen edits, read back */
                for (int i = 0; i < 12; i++) undo_set_tile(&u, m, 3 + i, 3, TILE_WATER);
                undo_end(&u);
                ChangeSet cs;
                cs_init(&cs);
                t0 = now_us();
                checkpoint_changes(m, &cs);
                dozen[k] = now_us() - t0;
                cs_free(&cs);
                checkpoint_start(m);
            }
            double t0 = now_us();
            undo_begin(&u);
            for (int y = 0; y < h; y++)
                for (int x = 0; x < w; x++) undo_set_tile(&u, m, x, y, TILE_ROUGH);
            undo_end(&u);
            (on ? fill_on : fill_off)[k] = now_us() - t0;
            t0 = now_us();
            undo_undo(&u, m);
            (on ? step_on : step_off)[k] = now_us() - t0;
            if (on) {
                undo_redo(&u, m);
                ChangeSet cs;
                cs_init(&cs);
                t0 = now_us();
                checkpoint_changes(m, &cs);
                all[k] = now_us() - t0;
                cs_free(&cs);
                checkpoint_stop(m);
            }
            undo_free(&u);
        }
    }
    map_free(m);
    map_free(base);
    char label[64];
    snprintf(label, sizeof label, "checkpoint, %dx%d, %d creatures", w, h, creatures);
    printf("| %-41s | %7.1fus | %7.1fus | %7.1fus | %7.1fus | %7.1fus | %7.1fus | %7.1fus |\n",
           label, median(start), median(fill_off), median(fill_on), median(step_off), median(step_on),
           median(dozen), median(all));
}

/* An outside change (step 7): the open map's file written by someone else.
 * The stat a key costs; then, once a change: the file read and parsed, the
 * base parsed from memory, and the two maps compared whole. */
static void disk_row(int w, int h, int creatures)
{
    double stat_t[RUNS], file_t[RUNS], base_t[RUNS], diff_t[RUNS];
    char path[] = "/tmp/vtt-proposals-XXXXXX", err[MAPIO_ERR_MAX];
    int fd = mkstemp(path);
    if (fd < 0) return;
    close(fd);
    Map *m = encounter(w, h, creatures);
    mapio_save(m, path, err, sizeof err);                  /* m has its base now */
    Map *theirs = mapio_load(path, err, sizeof err);
    map_set_tile(theirs, 3, 3, TILE_WATER);
    mapio_write(theirs, path, err, sizeof err);
    map_free(theirs);
    for (int k = 0; k < RUNS; k++) {
        MapDisk id;
        double t0 = now_us();
        for (int i = 0; i < 100; i++) mapio_disk_stat(path, &id);
        stat_t[k] = (now_us() - t0) / 100;

        t0 = now_us();
        Map *now = mapio_load_base(path, err, sizeof err);
        file_t[k] = now_us() - t0;

        t0 = now_us();
        Map *base = mapio_load_mem(m->base, m->base_len, err, sizeof err);
        base_t[k] = now_us() - t0;

        ChangeSet cs;
        cs_init(&cs);
        t0 = now_us();
        cs_diff(&cs, base, now, NULL);
        diff_t[k] = now_us() - t0;
        cs_free(&cs);
        map_free(now);
        map_free(base);
    }
    char what[64];
    snprintf(what, sizeof what, "%dx%d, %d creatures, %zu KB", w, h, creatures, m->base_len / 1024);
    double f = median(file_t), b = median(base_t), d = median(diff_t);
    printf("| %-41s | %7.2fus | %7.1fus | %7.1fus | %7.1fus | %7.1fus |\n", what, median(stat_t), f, b, d, f + b + d);
    map_free(m);
    unlink(path);
}

int main(void)
{
    printf("| plan and map                              |      copy |      plan |      diff | diff (all) |     check | preview, first | preview, frame |    accept |\n");
    printf("|-------------------------------------------|-----------|-----------|-----------|-----------|-----------|-----------|-----------|-----------|\n");
    row("five rooms", 40, 25, 24, five_rooms);
    row("five rooms", 512, 512, 24, five_rooms);
    row("five rooms", 512, 512, 500, five_rooms);
    row("fill the whole map", 512, 512, 24, whole_fill);
    printf("\n| checkpoint and map                        |     start | fill, off |  fill, on | undo, off |  undo, on | read, a dozen | read, a fill |\n");
    printf("|-------------------------------------------|-----------|-----------|-----------|-----------|-----------|-----------|-----------|\n");
    checkpoint_row(40, 25, 24);
    checkpoint_row(512, 512, 24);
    checkpoint_row(512, 512, 500);
    printf("\n| an outside change to the file             | stat, a key | read + parse the file | parse the base | compare | all, once a change |\n");
    printf("|-------------------------------------------|-----------|-----------|-----------|-----------|-----------|\n");
    disk_row(40, 25, 24);
    disk_row(512, 512, 24);
    disk_row(512, 512, 500);
    return 0;
}
