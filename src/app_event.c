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

void app_event(App *a, int kind, int job, const char *fmt, ...)
{
    AgentEvent *e = &a->events[++a->event_seq % EVENT_MAX];
    e->seq = a->event_seq;
    memset(&e->where, 0, sizeof e->where);
    e->other = 0;
    e->gone = kind == CTL_EV_CLOSED;
    int k = job ? snprintf(e->text, sizeof e->text, "job %d %s", job, ctl_event_name(kind))
                : snprintf(e->text, sizeof e->text, "map %s", ctl_event_name(kind));
    if (!fmt || k >= (int)sizeof e->text - 3) return;
    e->text[k++] = ':';
    e->text[k++] = ' ';
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(e->text + k, sizeof e->text - (size_t)k, fmt, ap);
    va_end(ap);
}

/* The oldest event still kept. */
static unsigned oldest(const App *a)
{
    return a->event_seq > EVENT_MAX ? a->event_seq - EVENT_MAX + 1 : 1;
}

void app_event_where(App *a, const CsWhere *where, int other)
{
    AgentEvent *e = &a->events[a->event_seq % EVENT_MAX];
    e->where = *where;
    e->other = other != 0;
}

int app_events_where(const App *a, unsigned after, CsWhere *where, int *other)
{
    memset(where, 0, sizeof *where);
    *other = 0;
    if (after > a->event_seq) after = 0;             /* another vtt's number: everything kept */
    if (after + 1 < oldest(a)) return 0;
    for (unsigned s = after + 1; s <= a->event_seq && s > after; s++) {
        /* Another map since: the pieces before it were that map's. */
        if (a->events[s % EVENT_MAX].gone) return 0;
        for (size_t i = 0; i < sizeof where->b / sizeof *where->b; i++) where->b[i] |= a->events[s % EVENT_MAX].where.b[i];
        *other |= a->events[s % EVENT_MAX].other;
    }
    return 1;
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
    /* The channel off: a wait still hears of jobs, but nothing of the map. */
    if (ctl_active(&a->ctl) && !a->agent_on) return 0;
    if (ctl_waiters(&a->ctl)) return 1;
    for (int i = 0; i < JOB_MAX; i++)
        /* Not the file's: nobody waits to hear of that one but the GM. */
        if (a->jobs[i].used && a->jobs[i].state <= JOB_READY && a->jobs[i].from != JOB_FROM_DISK) return 1;
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
        app_event(a, CTL_EV_CHANGED, 0, "%s", what);
        int     other;
        CsWhere where = { { 0 } };
        cs_where(&cs, &where, &other);
        app_event_where(a, &where, other);
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

void app_events_hear(App *a)
{
    if (!a->map || a->map->cp) return;
    if (ctl_active(&a->ctl) && !a->agent_on) return;      /* off: nothing of the map is told (listening) */
    /* Changed while nobody listened: nobody knows where, and an agent back
     * with an old number must not be told "nothing". */
    if (a->cp_off_map == a->map && a->cp_off_gen != a->map->gen) {
        CsWhere all;
        memset(&all, 0xff, sizeof all);
        app_event(a, CTL_EV_CHANGED, 0, "while no agent was listening - read the map again");
        app_event_where(a, &all, 1);
    }
    a->cp_off_map = NULL;
    checkpoint_start(a->map);
    a->cp_gen = a->map->gen;
}

void app_events_flush(App *a, uint64_t now_ms)
{
    if (a->map) {
        int want = listening(a, now_ms);
        if (want) app_events_hear(a);
        else if (a->map->cp) {
            checkpoint_stop(a->map);
            a->cp_off_map = a->map;
            a->cp_off_gen = a->map->gen;
        }
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
