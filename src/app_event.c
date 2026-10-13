/* Events for agents, the loop's side (docs/CONFLICTS.md, "Telling the
 * agent"): the ring itself and what makes an event are world.c's. Here is
 * when the map's changes become one -- the map's checkpoint (checkpoint.c)
 * runs only while an agent is listening: a wait held, a job open, or a wait
 * in the last ten minutes -- and the answering of the waits the socket holds
 * (ctl.c's CTL_WAITING) once there is an event after the number each named,
 * or its time is up. */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_priv.h"
#include "checkpoint.h"
#include "prof.h"









/* --------------------------------------------------------- the map changed */

static int listening(const App *a, uint64_t now_ms)
{
    /* The channel off: a wait still hears of jobs, but nothing of the map. */
    if (ctl_active(&a->ctl) && !a->world.agent_on) return 0;
    if (ctl_waiters(&a->ctl)) return 1;
    for (int i = 0; i < JOB_MAX; i++)
        /* Not the file's: nobody waits to hear of that one but the GM. */
        if (a->world.jobs[i].used && a->world.jobs[i].state <= JOB_READY && a->world.jobs[i].from != JOB_FROM_DISK) return 1;
    return a->world.agent_seen_ms && now_ms - a->world.agent_seen_ms < (uint64_t)CTL_WAIT_MAX_S * 1000;
}




/* When the map's changes become an event: once it has been quiet as long as
 * an autosave waits. -1 for nothing owed; the tick must have seen the
 * change first (`seen_gen`), or `change_ms` is an older change's. */
static int map_due(const App *a, uint64_t now_ms)
{
    const Map *m = a->world.map;
    if (!m || !m->cp || m->gen == a->world.cp_gen || m->gen != a->world.seen_gen) return -1;
    uint64_t at = a->world.change_ms + AUTOSAVE_QUIET_MS;
    return now_ms >= at ? 0 : (int)(at - now_ms);
}

int app_events_due(const App *a, uint64_t now_ms)
{
    /* A change the tick has not stamped yet: wake for it, so the quiet time
     * is counted from now and not from whatever else wakes the loop. */
    const Map *m = a->world.map;
    if (m && m->cp && m->gen != a->world.cp_gen && m->gen != a->world.seen_gen) return 0;
    return map_due(a, now_ms);          /* a held wait's deadline is ctl_due's */
}


void app_events_flush(App *a, uint64_t now_ms)
{
    if (a->world.map) {
        int want = listening(a, now_ms);
        if (want) world_events_hear(&a->world);
        else if (a->world.map->cp) {
            checkpoint_stop(a->world.map);
            a->world.cp_off_map = a->world.map;
            a->world.cp_off_gen = a->world.map->gen;
        }
        if (map_due(a, now_ms) == 0) world_events_map_flush(&a->world);
    }
    if (!ctl_waiters(&a->ctl)) return;
    PROF_ZONE("ctl.wait");
    /* Backwards: an answer written whole closes its connection, and the
     * ones after it shift down. */
    for (int i = a->ctl.nc - 1; i >= 0; i--) {
        unsigned    seq;
        uint64_t    until;
        const char *who;
        if (!ctl_waiter(&a->ctl, i, &seq, &until, &who)) continue;
        if (!world_events_after(&a->world, seq, who) && now_ms < until) continue;
        char  *body = NULL;
        size_t len = 0;
        FILE  *out = open_memstream(&body, &len);
        if (!out) continue;
        fputs("ok\n", out);
        world_events_write(&a->world, out, seq, who);
        fclose(out);
        ctl_answer(&a->ctl, i, body, len);
    }
}
