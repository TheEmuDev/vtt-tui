/* The world a request acts on (world.h; docs/V2.md, D2): the events told to
 * agents, and the jobs -- asked, proposed, landed. Nothing here knows the
 * GM's screen: what must reach it goes through the host. */

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "world.h"
#include "checkpoint.h"
#include "util.h"

/* ------------------------------------------------------------ the events */

void world_event(World *w, int kind, int job, const char *fmt, ...)
{
    AgentEvent *e = &w->events[++w->event_seq % EVENT_MAX];
    e->seq = w->event_seq;
    memset(&e->where, 0, sizeof e->where);
    e->other = 0;
    e->gone = kind == CTL_EV_CLOSED;
    e->by[0] = '\0';
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
static unsigned oldest(const World *w)
{
    return w->event_seq > EVENT_MAX ? w->event_seq - EVENT_MAX + 1 : 1;
}

void world_event_where(World *w, const CsWhere *where, int other)
{
    AgentEvent *e = &w->events[w->event_seq % EVENT_MAX];
    e->where = *where;
    e->other = other != 0;
}

void world_event_by(World *w, const char *who)
{
    str_lcpy(w->events[w->event_seq % EVENT_MAX].by, who, sizeof w->events[0].by);
}

/* Is event e one `who` is told of? Not its own. */
static int heard(const AgentEvent *e, const char *who)
{
    return !who || !e->by[0] || strcmp(e->by, who) != 0;
}

int world_events_where(const World *w, unsigned after, CsWhere *where, int *other, const char *who)
{
    memset(where, 0, sizeof *where);
    *other = 0;
    if (after > w->event_seq) after = 0;             /* another vtt's number: everything kept */
    if (after + 1 < oldest(w)) return 0;
    for (unsigned s = after + 1; s <= w->event_seq && s > after; s++) {
        /* Another map since: the pieces before it were that map's. */
        if (w->events[s % EVENT_MAX].gone) return 0;
        if (!heard(&w->events[s % EVENT_MAX], who)) continue;
        for (size_t i = 0; i < sizeof where->b / sizeof *where->b; i++) where->b[i] |= w->events[s % EVENT_MAX].where.b[i];
        *other |= w->events[s % EVENT_MAX].other;
    }
    return 1;
}

/* A number past the newest event is another vtt's (this one was started
 * since): there is something to say at once -- that, and everything kept. */
int world_events_after(const World *w, unsigned after, const char *who)
{
    if (after > w->event_seq) return 1;
    if (after + 1 < oldest(w)) return 1;             /* `lost` is something to say */
    int n = 0;
    for (unsigned s = after + 1; s <= w->event_seq && s > after; s++) n += heard(&w->events[s % EVENT_MAX], who);
    return n;
}

void world_events_write(const World *w, FILE *out, unsigned after, const char *who)
{
    fprintf(out, "seq %u\n", w->event_seq);
    if (after > w->event_seq) {
        fprintf(out, "reset %u: that number is not this vtt's (it was started since) - "
                     "everything kept follows; read jobs and the map again\n", after);
        after = 0;
    }
    unsigned from = after + 1;
    if (from < oldest(w)) {
        fprintf(out, "lost %u-%u: more happened than is kept - read jobs and the map again\n", from, oldest(w) - 1);
        from = oldest(w);
    }
    for (unsigned s = from; s <= w->event_seq && s >= from; s++)
        if (heard(&w->events[s % EVENT_MAX], who)) fprintf(out, "%u %s\n", s, w->events[s % EVENT_MAX].text);
}

void world_events_map_restart(World *w)
{
    if (!w->map || !w->map->cp) return;
    checkpoint_start(w->map);
    w->cp_gen = w->map->gen;
}

void world_events_map_flush(World *w)
{
    Map *m = w->map;
    if (!m || !m->cp || m->gen == w->cp_gen) return;
    ChangeSet cs;
    cs_init(&cs);
    if (checkpoint_changes(m, &cs)) {
        char what[EVENT_TEXT_MAX - 32];
        cs_summary(&cs, NULL, what, sizeof what);
        world_event(w, CTL_EV_CHANGED, 0, "%s", what);
        int     other;
        CsWhere where = { { 0 } };
        cs_where(&cs, &where, &other);
        world_event_where(w, &where, other);
    }
    cs_free(&cs);
    world_events_map_restart(w);
}

void world_event_of_change(World *w, int kind, int job, const char *who, const char *text)
{
    Map *m = w->map;
    ChangeSet cs;
    cs_init(&cs);
    int     other = 0, known = m->cp && checkpoint_changes(m, &cs);
    CsWhere where = { { 0 } };
    if (known) cs_where(&cs, &where, &other);
    cs_free(&cs);
    world_event(w, kind, job, "%s", text);
    world_event_where(w, &where, other);
    world_event_by(w, who);
    world_events_map_restart(w);
}

void world_events_hear(World *w)
{
    if (!w->map || w->map->cp) return;
    if (w->host->deaf(w->gm)) return;      /* off: nothing of the map is told (listening) */
    /* Changed while nobody listened: nobody knows where, and an agent back
     * with an old number must not be told "nothing". */
    if (w->cp_off_map == w->map && w->cp_off_gen != w->map->gen) {
        CsWhere all;
        memset(&all, 0xff, sizeof all);
        world_event(w, CTL_EV_CHANGED, 0, "while no agent was listening - read the map again");
        world_event_where(w, &all, 1);
    }
    w->cp_off_map = NULL;
    checkpoint_start(w->map);
    w->cp_gen = w->map->gen;
}

/* ------------------------------------------------------------- the jobs */

static const char *const STATE_NAME[] = { "asked", "working", "ready", "accepted", "scrapped" };

static const char *const FROM_NAME[] = { "gm", "agent", "apply", "disk" };

int world_job_find(const World *w, int num)
{
    for (int i = 0; i < JOB_MAX; i++)
        if (w->jobs[i].used && w->jobs[i].num == num) return i;
    return -1;
}

const char *world_job_state_name(int state) { return STATE_NAME[state]; }

const char *world_job_from_name(int from) { return FROM_NAME[from]; }

const char *world_job_who_name(char who) { return who == 'G' ? "gm" : who == 'A' ? "agent" : "-"; }

/* The next number after the last one given, round to 1 after the cap: a
 * number is not given out again while another is free. A program started
 * for a job (app_agent.c) or holding its number may outlive the job, and
 * must not find a different job under it. */
static int job_free_num(World *w)
{
    for (int k = 1; k <= JOB_NUM_MAX; k++) {
        int n = (w->job_last + k - 1) % JOB_NUM_MAX + 1;
        if (world_job_find(w, n) < 0) return w->job_last = n;
    }
    return 0;
}

/* A job that is not an agent's to change: the file's (another program's
 * work, which only the GM may take or leave) and an --apply's (its sender
 * waits for the verdict on what it sent). */
int world_job_gm_only(const Job *j) { return j->from == JOB_FROM_DISK || j->from == JOB_FROM_APPLY; }

/* The job goes, and its number is free to be given again: a landing of its
 * that the log still holds keeps its author and not the number. */
void world_job_clear(World *w, Job *j)
{
    if (j->used) { undo_tag_forget(&w->undo, j->num); w->host->job_gone(w->gm, j->num); }
    if (j->has_cs) cs_free(&j->cs);
    memset(j, 0, sizeof *j);
}

void world_job_thread_add(Job *j, char who, const char *text)
{
    if (j->nthread == JOB_THREAD_MAX) {                    /* oldest goes */
        memmove(&j->thread[0], &j->thread[1], (JOB_THREAD_MAX - 1) * sizeof j->thread[0]);
        j->nthread--;
    }
    j->thread[j->nthread].who = who;
    str_lcpy(j->thread[j->nthread].text, text, sizeof j->thread[0].text);
    j->nthread++;
}

void world_jobs_clear(World *w)
{
    for (int i = 0; i < JOB_MAX; i++) world_job_clear(w, &w->jobs[i]);
    w->host->review_leave(w->gm);
}

int world_job_slot_for_new(const World *w)
{
    for (int i = 0; i < JOB_MAX; i++)
        if (!w->jobs[i].used) return i;
    /* Full: the oldest finished job makes room, as a log would. */
    for (int i = 0; i < JOB_MAX; i++)
        if (w->jobs[i].state == JOB_ACCEPTED || w->jobs[i].state == JOB_SCRAPPED) return i;
    return -1;
}

int world_job_new(World *w, int from, const char *text, const CsBox *box, int at_once)
{
    int slot = world_job_slot_for_new(w);
    if (slot < 0) return -1;
    if (w->host->reviewing(w->gm) == slot) w->host->review_leave(w->gm);
    Job *j = &w->jobs[slot];
    world_job_clear(w, j);                   /* first: its number is free again */
    int num = job_free_num(w);
    if (!num) return -1;
    j->used    = 1;
    j->num     = num;
    j->from    = (uint8_t)from;
    j->state   = JOB_ASKED;
    j->at_once = (uint8_t)(at_once != 0);
    str_lcpy(j->text, text, sizeof j->text);
    if (box) { j->has_box = 1; j->box = *box; }
    if (text[0]) world_job_thread_add(j, from == JOB_FROM_GM ? 'G' : from == JOB_FROM_DISK ? '-' : 'A', text);
    return slot;
}

/* Where #N goes for a change set: the corner of what it touches, found once. */
static void corner_from_set(Job *j)
{
    int x1, y1;
    if (!cs_bounds(&j->cs, &j->cx, &j->cy, &x1, &y1)) j->cx = j->cy = -1;
}

/* Accepts job j's change set, or the part `box` holds, as one undo step. */
void world_job_land(World *w, Job *j, const CsBox *box)
{
    char left[160], msg[256], what[200];
    cs_summary(&j->cs, box, what, sizeof what);
    int creatures = j->cs.ntoks > 0;
    world_events_map_flush(w);                 /* the GM's own changes so far, told apart */
    unsigned logged = w->undo.stamp;
    int n = cs_apply(&j->cs, w->map, &w->undo, box, left, sizeof left);
    world_events_map_restart(w);               /* the accept is its own event, below */
    if (n) {
        w->host->landed(w->gm, creatures);
        /* Whose step it is, written on the step: the agent's `undo` and
         * `job N undo` take it back while it is the last thing done. Not the
         * file's nor an --apply's, the GM's alone to take back; and not a landing that
         * wrote nothing to the log (cards alone), or the tag would fall on
         * the GM's own step under it. */
        if (!world_job_gm_only(j) && w->undo.stamp != logged)
            undo_tag(&w->undo, j->num, j->taker[0] ? j->taker : CTL_NAME_NONE);
    }
    if (box) {
        char where[2 * MAP_COORD_MAX + 2];
        cs_box_name(box, where, sizeof where);
        cs_drop(&j->cs, box);
        cs_summary(&j->cs, NULL, j->summary, sizeof j->summary);
        corner_from_set(j);
        snprintf(msg, sizeof msg, "accepted the part in %s: %s", where, what);
        world_job_thread_add(j, '-', msg);
        if (cs_empty(&j->cs)) j->state = JOB_ACCEPTED;
        if (n) {
            world_event(w, CTL_EV_PART, j->num, "%s: %.120s%s", where, what,
                      j->state == JOB_READY ? " - the rest waits" : "");
            /* What landed lies wholly in the box. */
            CsWhere in = { { 0 } };
            cs_pieces(w->map->w, w->map->h, box->x0, box->y0, box->x1, box->y1, &in);
            world_event_where(w, &in, 0);
        }
        if (!n) snprintf(msg, sizeof msg, "#%d: nothing of it is in %s - the box takes what lies wholly inside", j->num, where);
        else snprintf(msg, sizeof msg, "#%d: accepted %s (%d)%s%.120s - u takes it back%s", j->num, where, n,
                      left[0] ? "; " : "", left, j->state == JOB_READY ? "; the rest waits" : "");
    } else {
        j->state = JOB_ACCEPTED;
        snprintf(msg, sizeof msg, "accepted: %s", what);
        world_job_thread_add(j, '-', msg);
        world_event(w, CTL_EV_ACCEPTED, j->num, "%.150s%s%.50s", what, left[0] ? "; " : "", left);
        int     other;
        CsWhere pieces = { { 0 } };
        cs_where(&j->cs, &pieces, &other);
        world_event_where(w, &pieces, other);
        snprintf(msg, sizeof msg, "#%d accepted: %.120s%s%.80s - u takes it back", j->num, what,
                 left[0] ? "; " : "", left);
    }
    w->host->note(w->gm, msg, 1);
}

/* Can a change land now, at once? Not into a prompt, a stroke, play: the
 * same reasons the channel's edits wait (app_ctl_busy). */
static int can_land(const World *w)
{
    return w->host->busy(w->gm) == NULL;
}

void world_job_set_proposal(World *w, int slot, ChangeSet *cs, const char *line)
{
    Job *j = &w->jobs[slot];
    if (j->has_cs) cs_free(&j->cs);
    j->cs = *cs;                       /* the set is the job's now */
    cs_init(cs);
    j->has_cs = 1;
    j->state  = JOB_READY;
    cs_summary(&j->cs, NULL, j->summary, sizeof j->summary);
    corner_from_set(j);
    char msg[256];
    snprintf(msg, sizeof msg, "proposed: %s", j->summary);
    world_job_thread_add(j, '-', msg);
    if (line && line[0]) world_job_thread_add(j, 'A', line);
    /* News to every agent but the one that made it; the file's change is
     * nobody's to hear of but the GM (app_event.c, `listening`). */
    if (j->from != JOB_FROM_DISK) {
        world_event(w, CTL_EV_PROPOSED, j->num, "%s", j->summary);
        world_event_by(w, j->taker[0] ? j->taker : CTL_NAME_NONE);
    }
    /* Never under the GM's eyes: a job being reviewed waits for enter. */
    if (j->at_once && can_land(w) && w->host->reviewing(w->gm) != slot) { world_job_land(w, j, NULL); return; }
    snprintf(msg, sizeof msg, "#%d ready: %.150s - :review %d%s%.60s", j->num, j->summary, j->num,
             line && line[0] ? " -- " : "", line ? line : "");
    w->host->note(w->gm, msg, 1);
}

long world_jobs_cells(const World *w, int skip)
{
    long n = 0;
    for (int i = 0; i < JOB_MAX; i++)
        if (i != skip && w->jobs[i].used && w->jobs[i].has_cs) n += w->jobs[i].cs.ncells;
    return n;
}

Map *world_scratch_copy(World *w)
{
    if (!w->ctl_scratch) w->ctl_scratch = map_new(1, 1, "scratch");
    map_copy_into(w->ctl_scratch, w->map);
    undo_clear(&w->ctl_sundo);
    return w->ctl_scratch;
}

Map *world_job_result(World *w, int slot)
{
    Job *j = &w->jobs[slot];
    /* A set made for another size of map would put its squares and
     * creatures off this one: the review refuses to show it too. */
    if (!j->has_cs || j->cs.w != w->map->w || j->cs.h != w->map->h) return NULL;
    Map *m = world_scratch_copy(w);
    cs_apply(&j->cs, m, &w->ctl_sundo, NULL, NULL, 0);
    return m;
}

/* What waited to land at once, landed when the GM is back (after each key). */
void world_jobs_land_waiting(World *w)
{
    if (!w->map || !can_land(w)) return;
    for (int i = 0; i < JOB_MAX; i++) {
        Job *j = &w->jobs[i];
        if (j->used && j->at_once && j->state == JOB_READY && j->has_cs && w->host->reviewing(w->gm) != i) world_job_land(w, j, NULL);
    }
}
