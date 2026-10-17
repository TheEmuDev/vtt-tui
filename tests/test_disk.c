/* Other writers of the open map's file (docs/CONFLICTS.md, step 7): the base
 * and identity kept at load and save, a change behind the app noticed on a
 * key and made a proposal once the keys are quiet, :w refused until it is
 * looked at, :w!, bare :e, and being asked before a map open elsewhere is
 * opened here. */
#include "harness.h"

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
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
        if (a->world.jobs[i].used && a->world.jobs[i].from == JOB_FROM_DISK) return i;
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
    if (!a.world.map) { app_free(&a); rnd_free(&r); sandbox_leave(&sb); return; }
    Map *m = a.world.map;
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
    undo_begin(&a.world.undo);
    undo_set_tile(&a.world.undo, m, 1, 1, TILE_WATER);
    undo_end(&a.world.undo);
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
    CHECK_EQ(a.world.jobs[dj].state, JOB_READY);
    CHECK_EQ(a.disk_state, DISK_PROPOSED);
    CHECK(strstr(a.status, "ready: ground in F6") != NULL);
    CHECK_EQ(map_tile(m, 5, 5), TILE_FLOOR);               /* nothing lands by itself */
    CHECK_EQ(app_disk_due(&a, now), -1);

    CASE(":w is refused while the file's change has not been looked at; a trip would be too");
    undo_begin(&a.world.undo);
    undo_set_tile(&a.world.undo, m, 2, 2, TILE_ROUGH);            /* the GM's own, unsaved */
    undo_end(&a.world.undo);
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
    int depth = a.world.undo.depth;
    press(&a, "\r");
    CHECK_EQ(map_tile(m, 5, 5), TILE_HAZARD);
    CHECK_EQ(map_tile(m, 2, 2), TILE_ROUGH);
    CHECK_EQ(a.world.undo.depth, depth + 1);
    CHECK(undo_tag_top(&a.world.undo) == NULL);                           /* not the agent's to take back */
    press(&a, ":w\r");
    CHECK(strstr(a.status, "wrote ") != NULL);
    CHECK_EQ(a.disk_state, DISK_NONE);
    {
        char err[MAPIO_ERR_MAX];
        Map *o = mapio_load(path, err, sizeof err);
        CHECK(o && map_tile(o, 5, 5) == TILE_HAZARD && map_tile(o, 2, 2) == TILE_ROUGH);
        map_free(o);
    }
    jobs_reset(&a);

    CASE("their change where the GM has changed too is a conflict, shown as one");
    undo_begin(&a.world.undo);
    undo_set_tile(&a.world.undo, m, 7, 7, TILE_WATER);
    undo_end(&a.world.undo);
    outside_edit(path, 7, 7, TILE_BRUSH);
    key_then_quiet(&a, &now);
    dj = disk_job(&a);
    CHECK(dj >= 0);
    if (dj < 0) goto out;
    CHECK_EQ(cs_check(&a.world.jobs[dj].cs, m), 1);

    CASE("scrapped: the file's change is left out, and :w then writes over it on purpose");
    press(&a, ":review\rd");
    CHECK_EQ(a.world.jobs[dj].state, JOB_SCRAPPED);
    press(&a, ":w\r");
    CHECK(strstr(a.status, "wrote ") != NULL);
    {
        char err[MAPIO_ERR_MAX];
        Map *o = mapio_load(path, err, sizeof err);
        CHECK(o && map_tile(o, 7, 7) == TILE_WATER);
        map_free(o);
    }
    jobs_reset(&a);

    CASE(":w! writes over an unreviewed change; its proposal is scrapped with it");
    outside_edit(path, 8, 8, TILE_BRUSH);
    key_then_quiet(&a, &now);
    dj = disk_job(&a);
    CHECK(dj >= 0 && a.world.jobs[dj].state == JOB_READY);
    undo_begin(&a.world.undo);
    undo_set_tile(&a.world.undo, m, 9, 9, TILE_WATER);
    undo_end(&a.world.undo);
    press(&a, ":w!\r");
    CHECK(strstr(a.status, "wrote ") != NULL);
    CHECK(dj >= 0 && a.world.jobs[dj].state == JOB_SCRAPPED);
    CHECK_EQ(a.disk_state, DISK_NONE);
    {
        char err[MAPIO_ERR_MAX];
        Map *o = mapio_load(path, err, sizeof err);
        CHECK(o && map_tile(o, 8, 8) == TILE_FLOOR && map_tile(o, 9, 9) == TILE_WATER);
        map_free(o);
    }
    jobs_reset(&a);

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
    jobs_reset(&a);

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
    undo_begin(&a.world.undo);
    undo_set_tile(&a.world.undo, m, 4, 4, TILE_WATER);
    undo_end(&a.world.undo);
    press(&a, ":e\r");
    CHECK_EQ(a.modal, MODAL_CONFIRM_DISCARD);
    press(&a, "y");
    m = a.world.map;
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
    jobs_reset(&a);

    CASE("REVIEW 2: the file's job is the file's: an agent's job that took its number is left alone");
    outside_edit(path, 1, 5, TILE_BRUSH);
    key_then_quiet(&a, &now);
    dj = disk_job(&a);
    CHECK(dj >= 0);
    if (dj >= 0) {
        char rm[32];
        int num = a.world.jobs[dj].num;
        snprintf(rm, sizeof rm, ":ask %d remove\r", num);
        a.disk_checked_ms = a.world.now_ms;                       /* no look between: the same second */
        press(&a, rm);
        a.world.job_last = num - 1;                               /* as after the numbers have gone round */
        char *t = ctl_ask(&a, "tile A12 water\n");          /* an agent's, taking the number */
        free(t);
        int aj = -1;
        for (int i = 0; i < JOB_MAX; i++) if (a.world.jobs[i].used && a.world.jobs[i].num == num) aj = i;
        CHECK(aj >= 0 && a.world.jobs[aj].from == JOB_FROM_AGENT);
        press(&a, ":w!\r");
        CHECK(aj >= 0 && a.world.jobs[aj].state == JOB_READY);    /* not scrapped as the file's */
    }
    jobs_reset(&a);
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
        CHECK(dj >= 0 && a.world.jobs[dj].state == JOB_SCRAPPED);
        CHECK(dj >= 0 && strstr(a.world.jobs[dj].thread[a.world.jobs[dj].nthread - 1].text, "written over") == NULL);
        snprintf(other, sizeof other, ":w %s\r", path);     /* and back to the first file */
        press(&a, other);
    }
    jobs_reset(&a);
    key_then_quiet(&a, &now);
    if (a.disk_state != DISK_NONE) { press(&a, ":w!\r"); jobs_reset(&a); }

    CASE("REVIEW 6: a scrapped change brought back with :review N stands in :w's way again");
    outside_edit(path, 4, 5, TILE_BRUSH);
    key_then_quiet(&a, &now);
    dj = disk_job(&a);
    CHECK(dj >= 0);
    if (dj >= 0) {
        char rv[32];
        press(&a, ":review\rd");
        key_then_quiet(&a, &now);                           /* adopted: the file is the base */
        snprintf(rv, sizeof rv, ":review %d\r", a.world.jobs[dj].num);
        press(&a, rv);
        CHECK_EQ(a.world.jobs[dj].state, JOB_READY);
        press(&a, "\x1b");
        press(&a, ":w\r");
        CHECK(strstr(a.status, "not saved - ") != NULL);
        press(&a, ":w!\r");
    }
    jobs_reset(&a);

    CASE("pinned: :wq is refused too and the map stays open; a part accepted by box leaves :w refused; two changes are one job");
    outside_edit(path, 5, 1, TILE_BRUSH);
    key_then_quiet(&a, &now);
    outside_edit(path, 6, 1, TILE_BRUSH);
    key_then_quiet(&a, &now);
    int njobs = 0;
    for (int i = 0; i < JOB_MAX; i++) njobs += a.world.jobs[i].used && a.world.jobs[i].from == JOB_FROM_DISK;
    CHECK_EQ(njobs, 1);
    dj = disk_job(&a);
    CHECK(dj >= 0 && a.world.jobs[dj].cs.ncells == 2);
    press(&a, ":wq\r");
    CHECK(a.world.map != NULL && a.screen == SCREEN_EDITOR);
    CHECK(strstr(a.status, "not saved - ") != NULL);
    a.ed.cx = 5; a.ed.cy = 1;
    press(&a, ":review\rv\r");                             /* the box is F2 alone */
    CHECK_EQ(map_tile(a.world.map, 5, 1), TILE_BRUSH);
    CHECK_EQ(map_tile(a.world.map, 6, 1), TILE_FLOOR);
    press(&a, "\x1b");
    press(&a, ":w\r");
    CHECK(strstr(a.status, "not saved - ") != NULL);        /* the rest still waits */
    press(&a, ":w!\r");
    jobs_reset(&a);
    m = a.world.map;

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
                int asked = c.modal == MODAL_CONFIRM_HELD && !c.world.map && strstr(c.modal_body, "is open in another vtt") != NULL;
                press(&c, yes ? "y" : "n");
                int opened = c.world.map != NULL && c.modal == MODAL_NONE;
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
            int asked = c.modal == MODAL_CONFIRM_HELD && !c.world.map && strstr(c.modal_body, "did not say whether it has") != NULL;
            press(&c, "y");
            _exit(asked && c.world.map ? 9 : 1);
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

static int jobs_used(const App *a)
{
    int n = 0;
    for (int i = 0; i < JOB_MAX; i++) n += a->world.jobs[i].used;
    return n;
}

/* What the review of the whole feature found between the steps: jobs across
 * :e, an agent's requests on a job that is the file's, a table that was full. */
void test_disk_whole(void)
{
    Sandbox sb = sandbox_enter("diskwhole");
    Renderer r;
    App a;
    rnd_init(&r);
    rnd_resize(&r, 100, 30);
    app_init(&a, NULL, &r);
    CHECK(ctl_blank_map(&a, sb.dir, 20, 12));
    if (!a.world.map) { app_free(&a); rnd_free(&r); sandbox_leave(&sb); return; }
    char path[700], other[700], cmd[800];
    str_lcpy(path, a.world.map->path, sizeof path);
    write_map_file(sb.dir, "other.vtt");
    snprintf(other, sizeof other, "%s/other.vtt", sb.dir);
    uint64_t now = 10000;
    char *t;

    CASE(":e NAME puts the map down: its jobs go with it, an agent is told, a review typed over is left");
    t = ctl_ask(&a, "tile D4 water\n");
    free(t);
    CHECK_EQ(jobs_used(&a), 1);
    press(&a, ":review\r");
    CHECK_EQ(a.ed.mode, ED_REVIEW);
    unsigned seq = a.world.event_seq;
    snprintf(cmd, sizeof cmd, ":e %s\r", other);
    press(&a, cmd);
    CHECK(a.world.map && a.world.map->w == 2);
    CHECK_EQ(jobs_used(&a), 0);
    CHECK_EQ(a.review, -1);
    CHECK(a.ed.mode != ED_REVIEW);
    CHECK(a.world.event_seq > seq && strstr(a.world.events[a.world.event_seq % EVENT_MAX].text, "map closed: ") != NULL);
    snprintf(cmd, sizeof cmd, ":e %s\r", path);
    press(&a, cmd);
    CHECK(a.world.map && a.world.map->w == 20);

    CASE("bare :e over the file's change: the file is the map now, its job is gone, and :w passes");
    outside_edit(path, 5, 5, TILE_HAZARD);
    key_then_quiet(&a, &now);
    CHECK(disk_job(&a) >= 0);
    press(&a, ":e\r");
    CHECK(a.world.map && map_tile(a.world.map, 5, 5) == TILE_HAZARD);
    CHECK_EQ(jobs_used(&a), 0);
    key_then_quiet(&a, &now);
    CHECK_EQ(a.disk_state, DISK_NONE);
    press(&a, ":w\r");
    CHECK(strstr(a.status, "wrote ") != NULL);
    key_then_quiet(&a, &now);

    CASE("the file's change is the GM's to decide: an agent can read it, never take, replace, answer or drop it");
    outside_edit(path, 7, 7, TILE_WATER);
    key_then_quiet(&a, &now);
    int dj = disk_job(&a);
    CHECK(dj >= 0);
    if (dj >= 0) {
        int num = a.world.jobs[dj].num;
        char sum[sizeof a.world.jobs[0].summary];
        str_lcpy(sum, a.world.jobs[dj].summary, sizeof sum);
        static const char *const TRY[] = { "job %d propose \"mine\"\ntile B2 water\n", "job %d take\n", "job %d area B2:C3\n",
                                           "job %d say \"hello\"\n", "job %d drop\n" };
        for (int i = 0; i < 5; i++) {
            snprintf(cmd, sizeof cmd, TRY[i], num);
            t = ctl_ask(&a, cmd);
            CHECK(t && ctl_refused(t, "state", 1) && strstr(t, "the GM's to decide") != NULL);
            free(t);
        }
        CHECK(a.world.jobs[dj].used && a.world.jobs[dj].state == JOB_READY && a.world.jobs[dj].from == JOB_FROM_DISK);
        CHECK(!strcmp(a.world.jobs[dj].summary, sum));
        snprintf(cmd, sizeof cmd, "job %d dump\n", num);
        t = ctl_ask(&a, cmd);
        CHECK(t && !strncmp(t, "ok\n", 3));
        free(t);
        CHECK_EQ(a.disk_state, DISK_PROPOSED);
        /* The file's job alone is no agent listening: no checkpoint runs for it. */
        a.world.agent_seen_ms = 0;
        app_events_flush(&a, now);
        CHECK(a.world.map->cp == NULL);
    }
    press(&a, ":w!\r");
    jobs_reset(&a);
    key_then_quiet(&a, &now);

    CASE("no room for the file's change: once a job is gone, the next quiet key reads the file and proposes it");
    for (int i = 0; i < JOB_MAX; i++) { t = ctl_ask(&a, "tile A1 water\n"); free(t); }
    CHECK_EQ(jobs_used(&a), JOB_MAX);
    outside_edit(path, 9, 9, TILE_BRUSH);
    key_then_quiet(&a, &now);
    CHECK_EQ(a.disk_state, DISK_STUCK);
    CHECK_EQ(disk_job(&a), -1);
    snprintf(cmd, sizeof cmd, ":ask %d remove\r", a.world.jobs[0].num);
    press(&a, cmd);
    key_then_quiet(&a, &now);
    CHECK(disk_job(&a) >= 0);
    CHECK_EQ(a.disk_state, DISK_PROPOSED);
    press(&a, ":w!\r");
    jobs_reset(&a);

    CASE("a job's number is not given out again while another is free: a run started for the old job cannot take the new");
    press(&a, ":ask a crypt\r");
    int first = -1, second = -1;
    for (int i = 0; i < JOB_MAX; i++) if (a.world.jobs[i].used) first = a.world.jobs[i].num;
    snprintf(cmd, sizeof cmd, ":ask %d remove\r", first);
    press(&a, cmd);
    press(&a, ":ask a tavern\r");
    for (int i = 0; i < JOB_MAX; i++) if (a.world.jobs[i].used) second = a.world.jobs[i].num;
    CHECK(first > 0 && second > 0 && second != first);
    snprintf(cmd, sizeof cmd, "job %d take\n", first);
    t = ctl_ask(&a, cmd);
    CHECK(t && ctl_refused(t, "missing", 1));
    free(t);

    app_free(&a);
    rnd_free(&r);
    sandbox_leave(&sb);
}

/* docs/V2.md, step 5's second piece, part 2: what an open says of its
 * file, :check, the channel's check, and a save over a row added by hand. */
static const char ROOM[] =
    "VTT 14\nname room\nsize 10 6\ntiles\n"
    "..........\n..........\n..........\n..........\n..........\n..........\n"
    "vedges\n|         |\n|         |\n|         |\n|         |\n|         |\n|         |\n"
    "hedges\n----------\n\n\n\n\n\n----------\ntoken enemy 2 2 1 \"Ogre\"\n";

static void write_text(const char *path, const char *text, int dup_line)
{
    FILE *f = fopen(path, "w");
    int line = 1;
    for (const char *p = text; *p; line++) {
        const char *e = strchr(p, '\n');
        size_t n = e ? (size_t)(e - p) + 1 : strlen(p);
        fwrite(p, 1, n, f);
        if (line == dup_line) fwrite(p, 1, n, f);
        p += n;
    }
    fclose(f);
}

void test_file_findings(void)
{
    Sandbox sb = sandbox_enter("filefind");
    Renderer r;
    App a;
    rnd_init(&r);
    rnd_resize(&r, 120, 40);
    app_init(&a, NULL, &r);
    char clean[700], bad[700];
    snprintf(clean, sizeof clean, "%s/room.vtt", sb.dir);
    snprintf(bad, sizeof bad, "%s/hand.vtt", sb.dir);
    write_text(clean, ROOM, 0);
    write_text(bad, ROOM, 6);                  /* a tiles row repeated by hand */

    CASE("a clean file opens with the plain line, and nothing to ask at a save");
    CHECK_EQ(app_open_map(&a, clean), 0);
    CHECK(strstr(a.status, "opened room") != NULL && strstr(a.status, "in the file") == NULL);
    CHECK_EQ(a.status_gm, 0);
    CHECK_EQ(a.file_rows, 0);

    CASE("a file with a row added by hand: the open says so, the GM's alone");
    CHECK_EQ(app_open_map(&a, bad), 0);
    CHECK(strstr(a.status, "opened room (10x6, 1 token); 1 warning in the file (:check): line 4, 'tiles' has 7 lines") != NULL);
    CHECK_EQ(a.status_gm, 1);
    CHECK_EQ(a.file_rows, 4);

    CASE(":check shows the report in the card's view: the file's finding, its stray row folded in");
    press(&a, ":check\r");
    CHECK_EQ(a.modal, MODAL_CARD);
    CHECK(a.card_text && strstr(a.card_text, "W031 row-extra") != NULL);
    CHECK(a.card_text && strstr(a.card_text, "W019 stray-row") == NULL);
    CHECK(a.card_text && strstr(a.card_text, "folded in: W019 x1") != NULL);
    CHECK(app_view_differs(&a));
    press(&a, "\x1b");
    CHECK_EQ(a.modal, MODAL_NONE);

    CASE("the channel's check says the file's findings too");
    {
        char *u = ctl_ask(&a, "check");
        CHECK(u && !strncmp(u, "ok\n", 3) && strstr(u, "W031 row-extra") != NULL);
        free(u);
    }

    CASE(":w over it asks first; n leaves the file as it is");
    size_t n0, n1;
    char *before = file_bytes(bad, &n0);
    press(&a, ":w\r");
    CHECK_EQ(a.modal, MODAL_CONFIRM_ROWS);
    CHECK(strstr(a.modal_body, "Near line 4") != NULL);
    press(&a, "n");
    CHECK_EQ(a.modal, MODAL_NONE);
    CHECK(strstr(a.status, "not saved") != NULL);
    char *after = file_bytes(bad, &n1);
    CHECK(before && after && n0 == n1 && !memcmp(before, after, n0));
    free(after);

    CASE("y saves the rows as read, once: the next :w does not ask");
    press(&a, ":w\r");
    press(&a, "y");
    CHECK_EQ(a.modal, MODAL_NONE);
    CHECK(strstr(a.status, "wrote ") != NULL);
    CHECK_EQ(a.file_rows, 0);
    after = file_bytes(bad, &n1);
    CHECK(after && (n0 != n1 || memcmp(before, after, n0)));
    free(after);
    free(before);
    press(&a, ":w\r");
    CHECK_EQ(a.modal, MODAL_NONE);

    CASE(":w! asks too; :w to another file asks nothing, and the map is that file's now");
    write_text(bad, ROOM, 6);
    CHECK_EQ(app_open_map(&a, bad), 0);
    press(&a, ":w!\r");
    CHECK_EQ(a.modal, MODAL_CONFIRM_ROWS);
    press(&a, "n");
    char other[720];
    snprintf(other, sizeof other, "%s/copy.vtt", sb.dir);
    char cmd[760];
    snprintf(cmd, sizeof cmd, ":w %s\r", other);
    press(&a, cmd);
    CHECK_EQ(a.modal, MODAL_NONE);
    CHECK_EQ(a.file_rows, 0);
    CHECK(access(other, F_OK) == 0);

    CASE("another program writes the file with a line vtt does not know: taken in, its finding said, the GM's");
    {
        CHECK_EQ(app_open_map(&a, clean), 0);
        uint64_t now = 50000;
        FILE *f = fopen(clean, "a");
        fputs("# a note by hand\n", f);
        fclose(f);
        struct timespec later[2] = { { 0, UTIME_NOW }, { time(NULL) + 60, 0 } };
        utimensat(AT_FDCWD, clean, later, 0);
        key_then_quiet(&a, &now);
        CHECK(strstr(a.status, "the file as another program wrote it; 1 warning in the file (:check): line") != NULL);
        CHECK_EQ(a.status_gm, 1);
        write_text(clean, ROOM, 0);
    }

    CASE(":wq over it asks, and y saves and closes");
    write_text(bad, ROOM, 6);
    CHECK_EQ(app_open_map(&a, bad), 0);
    press(&a, ":wq\r");
    CHECK_EQ(a.modal, MODAL_CONFIRM_ROWS);
    press(&a, "y");
    CHECK(a.world.map == NULL);

    CASE("--apply over it saves nothing, and says why; the browser's rename keeps its title");
    {
        write_text(bad, ROOM, 6);
        char plan[720], line[2400];
        snprintf(plan, sizeof plan, "%s/plan.txt", sb.dir);
        FILE *pf = fopen(plan, "w");
        fputs("room Z B2 3x2\n", pf);
        fclose(pf);
        size_t n2;
        char *was = file_bytes(bad, &n2);
        snprintf(line, sizeof line, "./vtt %s --apply %s 2>&1", bad, plan);
        FILE *p = popen(line, "r");
        char said[600] = "";
        size_t got = p ? fread(said, 1, sizeof said - 1, p) : 0;
        said[got] = '\0';
        int rc = p ? pclose(p) : -1;
        CHECK(strstr(said, "has a row added or lost by hand near line 4") != NULL && strstr(said, "nothing saved") != NULL);
        CHECK(rc != 0);
        size_t n3;
        char *now_bytes = file_bytes(bad, &n3);
        CHECK(was && now_bytes && n2 == n3 && !memcmp(was, now_bytes, n2));
        free(was);
        free(now_bytes);
        /* A copy made by the browser keeps the file's title: its save would
         * write the rows as read. */
        app_duplicate_map(&a, bad, "twin");
        CHECK(strstr(a.status, "title unchanged: its file has a row added or lost") != NULL);
        char twin[720];
        snprintf(twin, sizeof twin, "%s/twin.vtt", sb.dir);
        size_t n4;
        char *tb = file_bytes(twin, &n4), *hb = file_bytes(bad, &n2);
        CHECK(tb && hb && n4 == n2 && !memcmp(tb, hb, n2));      /* copied byte for byte, not rewritten */
        free(tb);
        free(hb);
    }

    app_free(&a);
    rnd_free(&r);
    sandbox_leave(&sb);
}
