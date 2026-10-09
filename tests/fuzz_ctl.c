/* libFuzzer target for the control channel's requests: the other place bytes
 * from outside the program arrive, if only from this user's own agent. Each
 * input is one request run against the kinds fixture in build mode; after
 * it, everything it did is undone, so the next input starts from the same
 * map and the log cannot grow without end.
 *
 *   make fuzz-ctl             a minute, seeded from tests/fuzz-ctl
 *   make fuzz-ctl FUZZ_SECONDS=600 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app.h"
#include "app_priv.h"
#include "render.h"
#include "scene.h"
#include "stamp.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

static App      app;
static Renderer rnd;
static int      ready;

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (!ready) {
        rnd_init(&rnd);
        rnd_resize(&rnd, 80, 24);
        app_init(&app, NULL, &rnd);
        if (app_open_map(&app, "tests/fixtures/kinds.vtt") != 0 || !app.map) abort();
        app.ctl_auto = 1;           /* else every input leaves a job, and 16 fill the table */
        CsBox box = { 1, 1, 4, 4 };
        app_job_new(&app, JOB_FROM_GM, "a crypt", &box, 0);
        /* Stamps from a folder of the fuzzer's own, never the user's, with
         * one in it to put down. */
        setenv("XDG_DATA_HOME", "/tmp/vtt-fuzz-ctl-data", 1);
        Map *st = stamp_copy(app.map, 0, 0, 3, 2);
        char err[160];
        if (!st || stamp_save(st, "Piece", err, sizeof err) != 0) abort();
        map_free(st);
        ready = 1;
    }
    char *req = malloc(size + 1);
    if (!req) return 0;
    memcpy(req, data, size);
    req[size] = '\0';

    size_t len = size;
    char  *ans = app_ctl_exec(&app, req, &len);
    /* The verdict is always the first line, and says what it is. */
    if (ans && strncmp(ans, "ok\n", 3) && strncmp(ans, "error: ", 7) && strncmp(ans, "busy: ", 6)) abort();
    free(ans);
    free(req);

    while (undo_undo(&app.undo, app.map)) { }
    undo_clear(&app.undo);
    app.ctl_undoable = 0;
    app_jobs_clear(&app);
    /* A job asked by the GM, so `job 1 ...` reaches past "no job #1". */
    CsBox box = { 1, 1, 4, 4 };
    app_job_new(&app, JOB_FROM_GM, "a crypt", &box, 0);
    /* Saving a scene is no edit, so the log does not take it back. */
    while (app.map->nscenes > 0) scene_remove(app.map, app.map->nscenes - 1);
    return 0;
}
