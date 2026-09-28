/* A save's cost, flushed (:w, a trip) and unflushed (the recovery autosave),
 * for three map sizes, median and worst of nine: tools/saves.sh builds and
 * runs it on the disk the maps are on. perf.sh cannot show the autosave --
 * a bench never writes one. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <unistd.h>

#include "map.h"
#include "mapio.h"
#include "token.h"

#define RUNS 9

static double now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}

static int by_value(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return x < y ? -1 : x > y;
}

int main(int argc, char **argv)
{
    static const int sizes[3][3] = { { 40, 25, 24 }, { 200, 200, 0 }, { 512, 512, 0 } };
    if (argc != 2) { fprintf(stderr, "usage: saves DIR\n"); return 2; }

    printf("| map | bytes | autosave (unflushed) | `:w` (flushed) |\n|---|---|---|---|\n");
    for (int s = 0; s < 3; s++) {
        int w = sizes[s][0], h = sizes[s][1], n = sizes[s][2];
        Map *m = map_new(w, h, "saves");
        map_fill_tiles(m, 0, 0, w - 1, h - 1, TILE_FLOOR);
        map_rect_walls(m, 0, 0, w - 1, h - 1, EDGE_WALL);
        for (int i = 0; i < n; i++) {
            Token t;
            memset(&t, 0, sizeof t);
            t.x = (int16_t)(i % 20 + 1);
            t.y = (int16_t)(i / 20 + 1);
            t.size = 1;
            snprintf(t.label, sizeof t.label, "Goblin %d", i + 1);
            tokens_add(&m->tokens, t);
        }

        char path[1024], err[MAPIO_ERR_MAX], cols[2][48];
        snprintf(path, sizeof path, "%s/saves-%d.vtt", argv[1], s);
        for (int flush = 0; flush < 2; flush++) {
            double t[RUNS];
            for (int k = 0; k < RUNS; k++) {
                double t0 = now_ms();
                int rc = flush ? mapio_write(m, path, err, sizeof err)
                               : mapio_write_unflushed(m, path, err, sizeof err);
                t[k] = now_ms() - t0;
                if (rc) { fprintf(stderr, "%s\n", err); return 1; }
            }
            qsort(t, RUNS, sizeof t[0], by_value);
            snprintf(cols[flush], sizeof cols[flush], "%.2f ms (worst %.1f)", t[RUNS / 2], t[RUNS - 1]);
        }
        struct stat st;
        long bytes = stat(path, &st) == 0 ? (long)st.st_size : 0;
        printf("| %d×%d%s | %.1f KB | %s | %s |\n", w, h, n ? ", 24 creatures" : "",
               bytes / 1000.0, cols[0], cols[1]);
        unlink(path);
        map_free(m);
    }
    return 0;
}
