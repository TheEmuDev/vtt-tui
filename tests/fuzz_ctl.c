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
#include "render.h"

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
    return 0;
}
