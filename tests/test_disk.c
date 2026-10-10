/* Other writers of the open map's file (docs/CONFLICTS.md, step 7): the base
 * and identity kept at load and save, a change behind the app noticed on a
 * key and made a proposal once the keys are quiet, :w refused until it is
 * looked at, :w!, bare :e, and being asked before a map open elsewhere is
 * opened here. */
#include "harness.h"

#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "app_priv.h"
#include "mapio.h"

/* Someone else writes the file: the map as it is on disk, one square
 * changed, written back the way a save writes. */
static void outside_edit(const char *path, int x, int y, uint8_t kind)
{
    char err[MAPIO_ERR_MAX];
    Map *o = mapio_load(path, err, sizeof err);
    if (!o) return;
    map_set_tile(o, x, y, kind);
    mapio_write(o, path, err, sizeof err);
    map_free(o);
}

static char *file_bytes(const char *path, size_t *len)
{
    int big = 0;
    return file_read(path, (size_t)8 << 20, len, &big);
}

/* A key at `now`, then the tick once the keys have been quiet. */
static void key_then_quiet(App *a, uint64_t *now)
{
    *now += 2000;
    app_tick(a, *now);
    press(a, "l");
    *now += AUTOSAVE_QUIET_MS;
    app_tick(a, *now);
}

static int disk_job(const App *a)
{
    for (int i = 0; i < JOB_MAX; i++)
        if (a->jobs[i].used && a->jobs[i].from == JOB_FROM_DISK) return i;
    return -1;
}

void test_disk(void)
{
    Sandbox sb = sandbox_enter("disk");
    Renderer r;
    App a;
    rnd_init(&r);
    rnd_resize(&r, 100, 30);
    app_init(&a, NULL, &r);
    CHECK(ctl_blank_map(&a, sb.dir, 20, 12));
    if (!a.map) { app_free(&a); rnd_free(&r); sandbox_leave(&sb); return; }
    Map *m = a.map;
    char path[700];
    str_lcpy(path, m->path, sizeof path);
    uint64_t now = 10000;
    size_t n;
    char *bytes;

    CASE("opened: the file's bytes are the map's base, and its identity is known");
    bytes = file_bytes(path, &n);
    CHECK(m->base && m->base_len == n && !memcmp(m->base, bytes, n));
    MapDisk id;
    CHECK(mapio_disk_stat(path, &id) == 0 && mapio_disk_same(&id, &m->disk));
    free(bytes);

    CASE("saved: the base is what was written, the identity the new file's; a key then finds nothing changed");
    undo_begin(&a.undo);
    undo_set_tile(&a.undo, m, 1, 1, TILE_WATER);
    undo_end(&a.undo);
    press(&a, ":w\r");
    CHECK(strstr(a.status, "wrote ") != NULL);
    bytes = file_bytes(path, &n);
    CHECK(m->base && m->base_len == n && !memcmp(m->base, bytes, n));
    CHECK(mapio_disk_stat(path, &id) == 0 && mapio_disk_same(&id, &m->disk));
    free(bytes);
    key_then_quiet(&a, &now);
    CHECK_EQ(a.disk_state, DISK_NONE);
    CHECK_EQ(disk_job(&a), -1);

    CASE("the file written behind the app: noticed on a key, a proposal from the file once the keys are quiet");
    outside_edit(path, 5, 5, TILE_HAZARD);
    now += 2000;
    app_tick(&a, now);
    press(&a, "l");
    CHECK_EQ(a.disk_state, DISK_NOTICED);
    CHECK(strstr(a.status, "the file changed on disk") != NULL && a.status_gm);
    CHECK_EQ(disk_job(&a), -1);                            /* not read on the key */
    CHECK_EQ(app_disk_due(&a, now), AUTOSAVE_QUIET_MS);
    app_tick(&a, now + AUTOSAVE_QUIET_MS - 1);
    CHECK_EQ(disk_job(&a), -1);
    now += AUTOSAVE_QUIET_MS;
    app_tick(&a, now);
    int dj = disk_job(&a);
    CHECK(dj >= 0);
    if (dj < 0) goto out;
    CHECK_EQ(a.jobs[dj].state, JOB_READY);
    CHECK_EQ(a.disk_state, DISK_PROPOSED);
    CHECK(strstr(a.status, "ready: ground in F6") != NULL);
    CHECK_EQ(map_tile(m, 5, 5), TILE_FLOOR);               /* nothing lands by itself */
    CHECK_EQ(app_disk_due(&a, now), -1);

    CASE(":w is refused while the file's change has not been looked at; a trip would be too");
    undo_begin(&a.undo);
    undo_set_tile(&a.undo, m, 2, 2, TILE_ROUGH);            /* the GM's own, unsaved */
    undo_end(&a.undo);
    press(&a, ":w\r");
    CHECK(strstr(a.status, "not saved - the file changed on disk: :review") != NULL);
    CHECK(strstr(a.status, ":w! writes yours over it") != NULL);
    CHECK(app_disk_blocks(&a) != NULL);
    {
        char err[MAPIO_ERR_MAX];
        Map *o = mapio_load(path, err, sizeof err);        /* theirs, still */
        CHECK(o && map_tile(o, 5, 5) == TILE_HAZARD && map_tile(o, 2, 2) == TILE_FLOOR);
        map_free(o);
    }

    CASE("a file cannot be sent back with feedback");
    press(&a, ":review\r");
    CHECK_EQ(a.ed.mode, ED_REVIEW);
    press(&a, "c");
    CHECK_EQ(a.modal, MODAL_NONE);
    CHECK(strstr(a.status, "a file cannot be asked") != NULL);

    CASE("accepted: their change and the GM's are both here, one undo step; :w passes, and the file has both");
    int depth = a.undo.depth;
    press(&a, "\r");
    CHECK_EQ(map_tile(m, 5, 5), TILE_HAZARD);
    CHECK_EQ(map_tile(m, 2, 2), TILE_ROUGH);
    CHECK_EQ(a.undo.depth, depth + 1);
    CHECK_EQ(a.ctl_undoable, 0);                            /* not the agent's to take back */
    press(&a, ":w\r");
    CHECK(strstr(a.status, "wrote ") != NULL);
    CHECK_EQ(a.disk_state, DISK_NONE);
    {
        char err[MAPIO_ERR_MAX];
        Map *o = mapio_load(path, err, sizeof err);
        CHECK(o && map_tile(o, 5, 5) == TILE_HAZARD && map_tile(o, 2, 2) == TILE_ROUGH);
        map_free(o);
    }
    app_jobs_clear(&a);

    CASE("their change where the GM has changed too is a conflict, shown as one");
    undo_begin(&a.undo);
    undo_set_tile(&a.undo, m, 7, 7, TILE_WATER);
    undo_end(&a.undo);
    outside_edit(path, 7, 7, TILE_BRUSH);
    key_then_quiet(&a, &now);
    dj = disk_job(&a);
    CHECK(dj >= 0);
    if (dj < 0) goto out;
    CHECK_EQ(cs_check(&a.jobs[dj].cs, m), 1);

    CASE("scrapped: the file's change is left out, and :w then writes over it on purpose");
    press(&a, ":review\rd");
    CHECK_EQ(a.jobs[dj].state, JOB_SCRAPPED);
    press(&a, ":w\r");
    CHECK(strstr(a.status, "wrote ") != NULL);
    {
        char err[MAPIO_ERR_MAX];
        Map *o = mapio_load(path, err, sizeof err);
        CHECK(o && map_tile(o, 7, 7) == TILE_WATER);
        map_free(o);
    }
    app_jobs_clear(&a);

    CASE(":w! writes over an unreviewed change; its proposal is scrapped with it");
    outside_edit(path, 8, 8, TILE_BRUSH);
    key_then_quiet(&a, &now);
    dj = disk_job(&a);
    CHECK(dj >= 0 && a.jobs[dj].state == JOB_READY);
    undo_begin(&a.undo);
    undo_set_tile(&a.undo, m, 9, 9, TILE_WATER);
    undo_end(&a.undo);
    press(&a, ":w!\r");
    CHECK(strstr(a.status, "wrote ") != NULL);
    CHECK(dj >= 0 && a.jobs[dj].state == JOB_SCRAPPED);
    CHECK_EQ(a.disk_state, DISK_NONE);
    {
        char err[MAPIO_ERR_MAX];
        Map *o = mapio_load(path, err, sizeof err);
        CHECK(o && map_tile(o, 8, 8) == TILE_FLOOR && map_tile(o, 9, 9) == TILE_WATER);
        map_free(o);
    }
    app_jobs_clear(&a);

    CASE("at most one look a second: a second key in the same second does not stat again");
    key_then_quiet(&a, &now);
    press(&a, "l");                                         /* a look, at this clock */
    outside_edit(path, 3, 3, TILE_BRUSH);
    press(&a, "h");                                         /* the same clock: no second look */
    CHECK_EQ(a.disk_state, DISK_NONE);
    now += 999;
    app_tick(&a, now);
    press(&a, "l");
    CHECK_EQ(a.disk_state, DISK_NONE);
    now += 1;
    app_tick(&a, now);
    press(&a, "h");
    CHECK_EQ(a.disk_state, DISK_NOTICED);

    CASE("a save looks now, not at the last key: the change it would write over is found and it is refused");
    press(&a, ":w\r");
    CHECK(strstr(a.status, "not saved - the file changed on disk") != NULL);
    CHECK_EQ(a.disk_state, DISK_PROPOSED);
    press(&a, ":review\r\r");                               /* take theirs */
    CHECK_EQ(map_tile(m, 3, 3), TILE_BRUSH);
    press(&a, ":w\r");
    CHECK(strstr(a.status, "wrote ") != NULL);
    app_jobs_clear(&a);

    CASE("written again with nothing changed: nothing to review, and :w passes");
    {
        char err[MAPIO_ERR_MAX];
        Map *o = mapio_load(path, err, sizeof err);
        mapio_write(o, path, err, sizeof err);              /* a new file, the same map */
        map_free(o);
    }
    key_then_quiet(&a, &now);
    CHECK_EQ(a.disk_state, DISK_NONE);
    CHECK_EQ(disk_job(&a), -1);
    CHECK(mapio_disk_stat(path, &id) == 0 && mapio_disk_same(&id, &m->disk));

    CASE("the file no longer a map: no proposal can say that; :w is refused, :w! puts the map back");
    {
        FILE *f = fopen(path, "w");
        fputs("not a map at all\n", f);
        fclose(f);
    }
    key_then_quiet(&a, &now);
    CHECK_EQ(a.disk_state, DISK_STUCK);
    CHECK(strstr(a.status, "no longer a map vtt can read") != NULL);
    press(&a, ":w\r");
    CHECK(strstr(a.status, "not saved - the file on disk is no longer a map") != NULL);
    press(&a, ":w!\r");
    CHECK(strstr(a.status, "wrote ") != NULL);
    CHECK_EQ(a.disk_state, DISK_NONE);

    CASE("the file resized by someone else: said, not proposed");
    {
        char err[MAPIO_ERR_MAX];
        Map *o = mapio_load(path, err, sizeof err);
        map_resize(o, 22, 12);
        mapio_write(o, path, err, sizeof err);
        map_free(o);
    }
    key_then_quiet(&a, &now);
    CHECK_EQ(a.disk_state, DISK_STUCK);
    CHECK(strstr(a.status, "its size or yours is not what it was") != NULL);
    CHECK_EQ(disk_job(&a), -1);

    CASE("bare :e reads the file again, asking first when there is unsaved work");
    undo_begin(&a.undo);
    undo_set_tile(&a.undo, m, 4, 4, TILE_WATER);
    undo_end(&a.undo);
    press(&a, ":e\r");
    CHECK_EQ(a.modal, MODAL_CONFIRM_DISCARD);
    press(&a, "y");
    m = a.map;
    CHECK(m && m->w == 22 && map_tile(m, 4, 4) == TILE_FLOOR);
    CHECK_EQ(a.disk_state, DISK_NONE);
    CHECK(m && m->base != NULL);

    CASE("the file deleted behind the app: nothing to write over, and :w puts it back");
    unlink(path);
    key_then_quiet(&a, &now);
    CHECK_EQ(a.disk_state, DISK_NONE);
    press(&a, ":w\r");
    CHECK(strstr(a.status, "wrote ") != NULL);
    CHECK(access(path, F_OK) == 0);
    CHECK(mapio_disk_stat(path, &id) == 0 && mapio_disk_same(&id, &m->disk));

    CASE("REVIEW 1: deleted, then another program puts a different map there: a change to review, not written over");
    unlink(path);
    key_then_quiet(&a, &now);                               /* a look finds it gone */
    {
        char err[MAPIO_ERR_MAX];
        Map *o = map_copy(m);
        map_set_tile(o, 6, 6, TILE_HAZARD);
        mapio_write(o, path, err, sizeof err);              /* theirs, where ours was */
        map_free(o);
    }
    key_then_quiet(&a, &now);
    CHECK(disk_job(&a) >= 0);
    press(&a, ":w\r");
    CHECK(strstr(a.status, "not saved - the file changed on disk") != NULL);
    {
        char err[MAPIO_ERR_MAX];
        Map *o = mapio_load(path, err, sizeof err);
        CHECK(o && map_tile(o, 6, 6) == TILE_HAZARD);       /* theirs is still there */
        map_free(o);
    }
    press(&a, ":w!\r");
    app_jobs_clear(&a);

    CASE("REVIEW 2: the file's job is the file's: an agent's job that took its number is left alone");
    outside_edit(path, 1, 5, TILE_BRUSH);
    key_then_quiet(&a, &now);
    dj = disk_job(&a);
    CHECK(dj >= 0);
    if (dj >= 0) {
        char rm[32];
        int num = a.jobs[dj].num;
        snprintf(rm, sizeof rm, ":ask %d remove\r", num);
        a.disk_checked_ms = a.now_ms;                       /* no look between: the same second */
        press(&a, rm);
        char *t = ctl_ask(&a, "tile A12 water\n");          /* an agent's, taking the number */
        free(t);
        int aj = -1;
        for (int i = 0; i < JOB_MAX; i++) if (a.jobs[i].used && a.jobs[i].num == num) aj = i;
        CHECK(aj >= 0 && a.jobs[aj].from == JOB_FROM_AGENT);
        press(&a, ":w!\r");
        CHECK(aj >= 0 && a.jobs[aj].state == JOB_READY);    /* not scrapped as the file's */
    }
    app_jobs_clear(&a);
    key_then_quiet(&a, &now);

    CASE("REVIEW 4: a change noticed, then the file deleted: nothing to review, and :w puts the map back");
    outside_edit(path, 2, 5, TILE_BRUSH);
    now += 2000;
    app_tick(&a, now);
    press(&a, "l");
    CHECK_EQ(a.disk_state, DISK_NOTICED);
    unlink(path);
    now += AUTOSAVE_QUIET_MS;
    app_tick(&a, now);
    CHECK_EQ(a.disk_state, DISK_NONE);
    press(&a, ":w\r");
    CHECK(strstr(a.status, "wrote ") != NULL);
    {
        FILE *f = fopen(path, "w");                         /* stuck, then gone */
        fputs("garbage\n", f);
        fclose(f);
    }
    key_then_quiet(&a, &now);
    CHECK_EQ(a.disk_state, DISK_STUCK);
    unlink(path);
    key_then_quiet(&a, &now);
    CHECK_EQ(a.disk_state, DISK_NONE);
    press(&a, ":w\r");
    CHECK(strstr(a.status, "wrote ") != NULL);

    CASE("REVIEW 5: saved under another name: the file's change is left behind, and its thread says that");
    outside_edit(path, 3, 5, TILE_BRUSH);
    key_then_quiet(&a, &now);
    dj = disk_job(&a);
    CHECK(dj >= 0);
    {
        char other[720];
        snprintf(other, sizeof other, ":w %s/other.vtt\r", sb.dir);
        press(&a, other);
        CHECK(strstr(a.status, "wrote ") != NULL);
        CHECK(dj >= 0 && a.jobs[dj].state == JOB_SCRAPPED);
        CHECK(dj >= 0 && strstr(a.jobs[dj].thread[a.jobs[dj].nthread - 1].text, "written over") == NULL);
        snprintf(other, sizeof other, ":w %s\r", path);     /* and back to the first file */
        press(&a, other);
    }
    app_jobs_clear(&a);
    key_then_quiet(&a, &now);
    if (a.disk_state != DISK_NONE) { press(&a, ":w!\r"); app_jobs_clear(&a); }

    CASE("REVIEW 6: a scrapped change brought back with :review N stands in :w's way again");
    outside_edit(path, 4, 5, TILE_BRUSH);
    key_then_quiet(&a, &now);
    dj = disk_job(&a);
    CHECK(dj >= 0);
    if (dj >= 0) {
        char rv[32];
        press(&a, ":review\rd");
        key_then_quiet(&a, &now);                           /* adopted: the file is the base */
        snprintf(rv, sizeof rv, ":review %d\r", a.jobs[dj].num);
        press(&a, rv);
        CHECK_EQ(a.jobs[dj].state, JOB_READY);
        press(&a, "\x1b");
        press(&a, ":w\r");
        CHECK(strstr(a.status, "not saved - ") != NULL);
        press(&a, ":w!\r");
    }
    app_jobs_clear(&a);

    CASE("pinned: :wq is refused too and the map stays open; a part accepted by box leaves :w refused; two changes are one job");
    outside_edit(path, 5, 1, TILE_BRUSH);
    key_then_quiet(&a, &now);
    outside_edit(path, 6, 1, TILE_BRUSH);
    key_then_quiet(&a, &now);
    int njobs = 0;
    for (int i = 0; i < JOB_MAX; i++) njobs += a.jobs[i].used && a.jobs[i].from == JOB_FROM_DISK;
    CHECK_EQ(njobs, 1);
    dj = disk_job(&a);
    CHECK(dj >= 0 && a.jobs[dj].cs.ncells == 2);
    press(&a, ":wq\r");
    CHECK(a.map != NULL && a.screen == SCREEN_EDITOR);
    CHECK(strstr(a.status, "not saved - ") != NULL);
    a.ed.cx = 5; a.ed.cy = 1;
    press(&a, ":review\rv\r");                             /* the box is F2 alone */
    CHECK_EQ(map_tile(a.map, 5, 1), TILE_BRUSH);
    CHECK_EQ(map_tile(a.map, 6, 1), TILE_FLOOR);
    press(&a, "\x1b");
    press(&a, ":w\r");
    CHECK(strstr(a.status, "not saved - ") != NULL);        /* the rest still waits */
    press(&a, ":w!\r");
    app_jobs_clear(&a);
    m = a.map;

    CASE("a map another vtt has open: asked first; y opens it here too, n leaves it");
    {
        /* The other vtt is this process, listening and pumped; the one that
         * opens is a child, which says through its exit what it saw. */
        App b;
        Renderer rb;
        rnd_init(&rb);
        rnd_resize(&rb, 80, 24);
        app_init(&b, NULL, &rb);
        app_open_map(&b, path);
        char err[200];
        CHECK(ctl_start(&b.ctl, err, sizeof err) == 0);
        for (int yes = 0; yes < 2; yes++) {
            fflush(stdout);
            pid_t pid = fork();
            if (pid == 0) {
                App c;
                Renderer rc;
                rnd_init(&rc);
                rnd_resize(&rc, 80, 24);
                app_init(&c, NULL, &rc);
                c.ask_holders = 1;
                app_open_map(&c, path);
                int asked = c.modal == MODAL_CONFIRM_HELD && !c.map && strstr(c.modal_body, "is open in another vtt") != NULL;
                press(&c, yes ? "y" : "n");
                int opened = c.map != NULL && c.modal == MODAL_NONE;
                _exit(asked ? (opened ? 8 : 7) : 1);
            }
            int st = 0, done = 0;
            for (int spin = 0; spin < 600 && !done; spin++) {
                struct pollfd fds[1 + CTL_SLOTS];
                int k = ctl_pollfds(&b.ctl, fds, 1 + CTL_SLOTS);
                poll(fds, (nfds_t)k, 5);
                uint64_t t = prof_now_ns() / 1000000u;
                ctl_service(&b.ctl, fds, k, t);
                app_tick(&b, t);
                done = waitpid(pid, &st, WNOHANG) > 0;
            }
            CHECK(done && WIFEXITED(st) && WEXITSTATUS(st) == (yes ? 8 : 7));
        }

        CASE("REVIEW 3: a vtt that is there and does not answer may have the map: asked about too, never silently opened");
        fflush(stdout);
        pid_t pid = fork();                                /* b listens and nobody services it */
        if (pid == 0) {
            App c;
            Renderer rc;
            rnd_init(&rc);
            rnd_resize(&rc, 80, 24);
            app_init(&c, NULL, &rc);
            c.ask_holders = 1;
            app_open_map(&c, path);
            int asked = c.modal == MODAL_CONFIRM_HELD && !c.map && strstr(c.modal_body, "did not say whether it has") != NULL;
            press(&c, "y");
            _exit(asked && c.map ? 9 : 1);
        }
        int st = 0;
        waitpid(pid, &st, 0);
        CHECK(WIFEXITED(st) && WEXITSTATUS(st) == 9);
        app_free(&b);
        rnd_free(&rb);
    }

out:
    app_free(&a);
    rnd_free(&r);
    sandbox_leave(&sb);
}
