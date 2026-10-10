/* Events for agents (docs/CONFLICTS.md, "Telling the agent"): a numbered
 * ring of what happened that an agent cares about, read by the channel's
 * `wait` -- a long poll the socket holds (ctl.c's CTL_WAITING) until there
 * is an event after the number the agent names, or its time is up. The
 * map-changed event comes from the map's checkpoint (checkpoint.c), which
 * runs only while an agent is listening: a wait held, a job open, or a wait
 * in the last ten minutes. */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_priv.h"
#include "checkpoint.h"
#include "prof.h"

void app_event(App *a, const char *fmt, ...)
{
    AgentEvent *e = &a->events[++a->event_seq % EVENT_MAX];
    e->seq = a->event_seq;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(e->text, sizeof e->text, fmt, ap);
    va_end(ap);
}

/* The oldest event still kept. */
static unsigned oldest(const App *a)
{
    return a->event_seq > EVENT_MAX ? a->event_seq - EVENT_MAX + 1 : 1;
}

/* A number past the newest event is another vtt's (this one was started
 * since): there is something to say at once -- that, and everything kept. */
int app_events_after(const App *a, unsigned after)
{
    if (after > a->event_seq) return 1;
    return (int)(a->event_seq - after);
}

void app_events_write(const App *a, FILE *out, unsigned after)
{
    fprintf(out, "seq %u\n", a->event_seq);
    if (after > a->event_seq) {
        fprintf(out, "reset %u: that number is not this vtt's (it was started since) - "
                     "everything kept follows; read jobs and the map again\n", after);
        after = 0;
    }
    unsigned from = after + 1;
    if (from < oldest(a)) {
        fprintf(out, "lost %u-%u: more happened than is kept - read jobs and the map again\n", from, oldest(a) - 1);
        from = oldest(a);
    }
    for (unsigned s = from; s <= a->event_seq && s >= from; s++)
        fprintf(out, "%u %s\n", s, a->events[s % EVENT_MAX].text);
}

/* --------------------------------------------------------- the map changed */

static int listening(const App *a, uint64_t now_ms)
{
    if (ctl_waiters(&a->ctl)) return 1;
    for (int i = 0; i < JOB_MAX; i++)
        if (a->jobs[i].used && a->jobs[i].state <= JOB_READY) return 1;
    return a->agent_seen_ms && now_ms - a->agent_seen_ms < (uint64_t)CTL_WAIT_MAX_S * 1000;
}

void app_events_map_restart(App *a)
{
    if (!a->map || !a->map->cp) return;
    checkpoint_start(a->map);
    a->cp_gen = a->map->gen;
}

void app_events_map_flush(App *a)
{
    Map *m = a->map;
    if (!m || !m->cp || m->gen == a->cp_gen) return;
    ChangeSet cs;
    cs_init(&cs);
    if (checkpoint_changes(m, &cs)) {
        char what[EVENT_TEXT_MAX - 32];
        cs_summary(&cs, NULL, what, sizeof what);
        app_event(a, "map changed: %s", what);
    }
    cs_free(&cs);
    app_events_map_restart(a);
}

/* When the map's changes become an event: once it has been quiet as long as
 * an autosave waits. -1 for nothing owed; the tick must have seen the
 * change first (`seen_gen`), or `change_ms` is an older change's. */
static int map_due(const App *a, uint64_t now_ms)
{
    const Map *m = a->map;
    if (!m || !m->cp || m->gen == a->cp_gen || m->gen != a->seen_gen) return -1;
    uint64_t at = a->change_ms + AUTOSAVE_QUIET_MS;
    return now_ms >= at ? 0 : (int)(at - now_ms);
}

int app_events_due(const App *a, uint64_t now_ms)
{
    /* A change the tick has not stamped yet: wake for it, so the quiet time
     * is counted from now and not from whatever else wakes the loop. */
    const Map *m = a->map;
    if (m && m->cp && m->gen != a->cp_gen && m->gen != a->seen_gen) return 0;
    return map_due(a, now_ms);          /* a held wait's deadline is ctl_due's */
}

void app_events_flush(App *a, uint64_t now_ms)
{
    if (a->map) {
        int want = listening(a, now_ms);
        if (want && !a->map->cp) { checkpoint_start(a->map); a->cp_gen = a->map->gen; }
        else if (!want && a->map->cp) checkpoint_stop(a->map);
        if (map_due(a, now_ms) == 0) app_events_map_flush(a);
    }
    if (!ctl_waiters(&a->ctl)) return;
    PROF_ZONE("ctl.wait");
    /* Backwards: an answer written whole closes its connection, and the
     * ones after it shift down. */
    for (int i = a->ctl.nc - 1; i >= 0; i--) {
        unsigned seq;
        uint64_t until;
        if (!ctl_waiter(&a->ctl, i, &seq, &until)) continue;
        if (!app_events_after(a, seq) && now_ms < until) continue;
        char  *body = NULL;
        size_t len = 0;
        FILE  *out = open_memstream(&body, &len);
        if (!out) continue;
        fputs("ok\n", out);
        app_events_write(a, out, seq);
        fclose(out);
        ctl_answer(&a->ctl, i, body, len);
    }
}
