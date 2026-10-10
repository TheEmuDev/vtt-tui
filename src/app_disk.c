/* Other writers of the open map's file (docs/CONFLICTS.md): another vtt, a
 * text editor, git. The map remembers its file's identity and bytes as vtt
 * last read or wrote them (Map.disk, Map.base; mapio.c). One stat on a key,
 * at most once a second, says whether someone else has written it since;
 * when the keys go quiet the file is read, and what it changed against the
 * base becomes a proposal from "the file on disk", reviewed like any
 * other. Until it is reviewed, :w and a trip would write over it and are
 * refused (:w! writes anyway). Every message is the GM's alone. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_priv.h"
#include "mapio.h"
#include "prof.h"

void app_disk_reset(App *a)
{
    free(a->disk_new);
    a->disk_new = NULL;
    a->disk_new_len = 0;
    a->disk_state = DISK_NONE;
    a->disk_job = 0;
    a->disk_why[0] = '\0';
}

/* The file as it is now is the base from here: reviewed, scrapped, or
 * written over. */
static void adopt(App *a)
{
    Map *m = a->map;
    if (m && a->disk_new) {
        free(m->base);
        m->base     = a->disk_new;
        m->base_len = a->disk_new_len;
        a->disk_new = NULL;
    }
    if (m) m->disk = a->disk_seen;
    app_disk_reset(a);
}

static void stuck(App *a, const char *why)
{
    char msg[220];
    a->disk_state = DISK_STUCK;
    str_lcpy(a->disk_why, why, sizeof a->disk_why);
    snprintf(msg, sizeof msg, "%s - :e opens the file's, :w! writes yours over it", why);
    app_note_gm(a, msg);
}

/* The file changed: read it, and what it changed since the base is a
 * proposal. Parsing two files and comparing two maps, once a change. */
static void disk_read(App *a)
{
    PROF_ZONE("disk.reload");
    Map *m = a->map;
    char err[MAPIO_ERR_MAX];
    Map *now  = mapio_load_base(m->path, err, sizeof err);
    Map *base = now && m->base ? mapio_load_mem(m->base, m->base_len, err, sizeof err) : NULL;
    if (now) a->disk_seen = now->disk;
    if (!now || !base) {
        stuck(a, now ? "the file changed on disk, and what it was before cannot be read back"
                     : "the file on disk is no longer a map vtt can read");
    }
    else if (now->w != base->w || now->h != base->h || m->w != base->w || m->h != base->h) {
        stuck(a, "the file changed on disk and its size or yours is not what it was");
    }
    else {
        ChangeSet cs;
        cs_init(&cs);
        if (!cs_diff(&cs, base, now, NULL)) {
            /* Written again with nothing changed (a touch, a save of the
             * same map): nothing to review. */
            cs_free(&cs);
            free(a->disk_new);
            a->disk_new = now->base;
            a->disk_new_len = now->base_len;
            now->base = NULL;
            adopt(a);
        } else {
            int slot = a->disk_job ? app_job_find(a, a->disk_job) : -1;
            if (slot < 0 || a->jobs[slot].state != JOB_READY)
                slot = app_job_new(a, JOB_FROM_DISK, "changed by another program", NULL, 0);
            if (slot < 0) {
                cs_free(&cs);
                stuck(a, "the file changed on disk, and there is no room for another change to review");
            } else {
                if (a->review == slot) app_review_leave(a);      /* its change set is replaced */
                free(a->disk_new);
                a->disk_new = now->base;
                a->disk_new_len = now->base_len;
                now->base = NULL;
                a->disk_state = DISK_PROPOSED;
                a->disk_job = a->jobs[slot].num;
                app_job_set_proposal(a, slot, &cs, NULL);
            }
        }
    }
    map_free(now);
    map_free(base);
    a->dirty = 1;
}

void app_disk_check(App *a, int now_too)
{
    Map *m = a->map;
    if (!m || !m->path[0] || !m->disk.known) return;
    /* A change reviewed, scrapped or sent away: the file is the base now. */
    if (a->disk_state == DISK_PROPOSED) {
        int slot = app_job_find(a, a->disk_job);
        if (slot < 0 || a->jobs[slot].state != JOB_READY) adopt(a);
    }
    {
        PROF_ZONE("disk.check");
        MapDisk cur;
        if (mapio_disk_stat(m->path, &cur) < 0) {
            /* Gone: nobody's work is there to write over, and :w puts the
             * map back. */
            if (a->disk_state == DISK_NONE) m->disk.known = 0;
            return;
        }
        const MapDisk *last = a->disk_state == DISK_NONE ? &m->disk : &a->disk_seen;
        if (!mapio_disk_same(&cur, last)) {
            a->disk_seen = cur;
            if (a->disk_state != DISK_PROPOSED) a->disk_state = DISK_NOTICED;
            a->disk_reread = 1;
            if (!now_too) app_set_status_gm(a, "the file changed on disk - looking at what changed when you pause");
        }
    }
    if (a->disk_reread && now_too) { a->disk_reread = 0; disk_read(a); }
}

void app_disk_key(App *a)
{
    a->disk_key_ms = a->now_ms;
    if (!a->map) return;
    /* At most once a second, and never on a clock that is not running (the
     * bench's, the tests'): a stat is a syscall a key. */
    if (a->now_ms - a->disk_checked_ms < 1000) return;
    a->disk_checked_ms = a->now_ms;
    app_disk_check(a, 0);
}

int app_disk_due(const App *a, uint64_t now_ms)
{
    if (!a->map || !a->disk_reread) return -1;
    uint64_t at = a->disk_key_ms + AUTOSAVE_QUIET_MS;
    return now_ms >= at ? 0 : (int)(at - now_ms);
}

void app_disk_tick(App *a, uint64_t now_ms)
{
    if (app_disk_due(a, now_ms) != 0) return;
    a->disk_reread = 0;
    disk_read(a);
}

const char *app_disk_blocks(const App *a)
{
    static char why[200];
    if (a->disk_state == DISK_NONE) return NULL;
    if (a->disk_state == DISK_PROPOSED)
        snprintf(why, sizeof why, "the file changed on disk: :review %d looks at what changed", a->disk_job);
    else if (a->disk_state == DISK_STUCK) str_lcpy(why, a->disk_why, sizeof why);
    else str_lcpy(why, "the file changed on disk", sizeof why);
    return why;
}

void app_disk_overwritten(App *a)
{
    /* :w! wrote the GM's map over whatever was there: the proposal is of a
     * file that no longer is. mapio_save has made the base the GM's. */
    if (a->disk_state == DISK_PROPOSED) {
        int slot = app_job_find(a, a->disk_job);
        if (slot >= 0 && a->jobs[slot].state == JOB_READY) {
            if (a->review == slot) app_review_leave(a);
            a->jobs[slot].state = JOB_SCRAPPED;
            app_job_thread_add(&a->jobs[slot], '-', "written over by :w!");
        }
    }
    app_disk_reset(a);
    a->disk_reread = 0;
}
