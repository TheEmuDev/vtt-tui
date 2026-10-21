#ifndef VTT_WORLD_H
#define VTT_WORLD_H

/* The world a request acts on (docs/V2.md, D2): the map and its undo log, the
 * jobs, the events told to agents, and the state of the request being run.
 * It is what a request handler is given, in place of the whole App, so that
 * the protocol can be run and tested with no editor, renderer or terminal
 * behind it. The App holds one, by value, as `world`. */

#include <stdint.h>

#include "ctl.h"
#include "job.h"
#include "map.h"
#include "undo.h"
#include "changeset.h"
#include <stdio.h>

#define EVENT_MAX      32
#define EVENT_TEXT_MAX 240
typedef struct {
    unsigned seq;
    char     text[EVENT_TEXT_MAX];
    CsWhere  where;         /* the pieces of the map it changed (cs_where), for `changes` */
    uint8_t  other;         /* and something on no square */
    uint8_t  gone;          /* the map itself went (closed): nothing before this says where anything is */
    char     by[CTL_NAME_MAX];  /* the agent whose doing it was, who is not told of it; "" the GM's, told to all */
} AgentEvent;

/* The GM's side of the world, as the world needs it: a seam (docs/V2.md,
 * D2 and D3), one real implementation in app_ctl.c and a recording one in the
 * tests. Every call is made for a request or a job, never for a frame. */
typedef struct WorldHost {
    /* A line for the GM alone -- never the players -- on the status line;
     * `logged` for what happened (the session log has it), 0 for a hint. */
    void (*note)(void *gm, const char *text, int logged);
    /* Why a change cannot land now (a prompt open, play mode, a key half
     * typed), or NULL. */
    const char *(*busy)(void *gm);
    /* A change set landed on the live map: the screen follows (`creatures`:
     * the list was renumbered, so what named creatures by index is void). */
    void (*landed)(void *gm, int creatures);
    /* The slot of the job on the GM's screen for review, or -1; and the
     * review put away, because its job is going. */
    int  (*reviewing)(void *gm);
    void (*review_leave)(void *gm);
    /* Job `num` is gone: whatever was started for it answers to no job. */
    void (*job_gone)(void *gm, int num);
    /* Nothing of the map is told to agents now (the channel is up and off). */
    int  (*deaf)(void *gm);

    /* What a request asks (request.c). The frame is owed a redraw. */
    void (*redraw)(void *gm);
    /* The live map changed under the screen in one of the WORLD_* ways below;
     * i, x, y say which creature where one is meant. */
    void (*changed)(void *gm, int what, int i, int x, int y);
    /* A ring round a change that landed, for the GM to see where. */
    void (*ring)(void *gm, int x0, int y0, int x1, int y1);
    /* `status`'s lines about the screen (which, its mode, the floor on it);
     * `marked`, whole; and the floor the GM is looking at (an area's index). */
    void (*screen_lines)(void *gm, FILE *out);
    void (*marked)(void *gm, FILE *out, int json);
    int  (*floor_shown)(void *gm);
    /* A request was refused with this verdict: for the session log. */
    void (*refused)(void *gm, const char *verdict);
    /* A request came in under this name: an agent the GM started is alive. */
    void (*heard)(void *gm, const char *who);
} WorldHost;

/* WorldHost.changed's `what`. */
enum {
    WORLD_EDITED,       /* a request's edits landed straight (--apply): sight follows */
    WORLD_UNDONE,       /* the channel stepped the log back */
    WORLD_RENUMBERED,   /* the creature list was replaced: indices are void */
    WORLD_TOKEN_GONE,   /* creature i, which stood at x,y, was deleted */
    WORLD_ROLLED_BACK,  /* a request failed and its edits were taken back */
};

typedef struct World {
    Map     *map;               /* the open map; during a request's edits, its scratch copy (request.c) */
    /* One log for the whole session: token moves in play mode undo through
     * the same history as wall edits in build mode. A landing's batch says
     * whose it is (undo_tag). */
    Undo     undo;

    /* Jobs (job.h, world.c): asked of agents or offered by them, kept
     * with the map. */
    Job      jobs[JOB_MAX];
    int      job_last;          /* the last job number given out; the next is after it */

    /* Events for agents (world.c): a numbered ring `wait` reads. The
     * newest is `event_seq`, kept at seq % EVENT_MAX. `cp_gen` is Map.gen
     * when the map's checkpoint was started; the checkpoint stops when no
     * agent listens, and `cp_off_*` is what the map was then, so that a
     * change nobody was told of is said when one comes back. */
    AgentEvent events[EVENT_MAX];
    unsigned event_seq;
    unsigned cp_gen;
    const Map *cp_off_map;
    unsigned   cp_off_gen;
    uint64_t agent_seen_ms;     /* the last `wait`: keeps the checkpoint running between an agent's waits */

    /* The clock, as the tick was last given it, and when the map last moved. */
    uint64_t now_ms;
    unsigned seen_gen;          /* the last Map.gen the tick saw */
    uint64_t change_ms;         /* when seen_gen last moved */

    /* The request being run. Its edits are made on a copy of the map, never
     * the live one, and become a proposal (docs/CONFLICTS.md); the copy and
     * its log are kept for their buffers. */
    char     ctl_who[CTL_NAME_MAX];   /* its name (`agent NAME`, or cli) */
    Map     *ctl_scratch;
    Undo     ctl_sundo;
    int      ctl_auto;          /* :agent accept auto: each proposal lands at once */
    int      ctl_direct;        /* --apply with no GM: the map itself is edited */
    int      ctl_can_hold;      /* set round a request the socket can hold */
    int      ctl_held;          /* a `wait` with nothing to say asked to be held, */
    unsigned ctl_hold_seq;      /* from this event, */
    int      ctl_hold_ms;       /* this long */
    /* :agent on. The socket listens whenever vtt runs interactively, so
     * --apply can find a map that is open; off, a request over it is refused
     * with a line saying to turn it on (docs/V2.md, ruling 9), all but
     * --apply's `holds`. Requests not from the socket (the tests, the bench)
     * are not asked. */
    int      agent_on;

    const WorldHost *host;      /* the GM's side, and what its calls are given */
    void    *gm;
} World;

/* Events for agents (world.c): `job N KIND[: text]`, or `map KIND[:
 * text]` with job 0; a NULL fmt for a kind that says it all. The kinds are
 * ctl.h's, where --wait reads them back. */
void world_event(World *w, int kind, int job, const char *fmt, ...) __attribute__((format(printf, 4, 5)));
/* Where on the map the newest event's change is (cs_where's pieces), said
 * straight after the world_event that made it. */
void world_event_where(World *w, const CsWhere *where, int other);
/* Whose doing the newest event is, said straight after it: that name is not
 * told of it (its own doing is no news to it), every other is. Callers with
 * no name are one name, `cli`, as everywhere. */
void world_event_by(World *w, const char *who);
/* In every reader below `who` is the name asking, whose own events are
 * passed over; NULL hears all. */
/* The pieces changed by every event after `after`, and whether something on
 * no square changed. 0 when events older than are kept would be among them:
 * then nobody knows. */
int  world_events_where(const World *w, unsigned after, CsWhere *where, int *other, const char *who);
/* An agent is listening from now: the checkpoint starts if it was stopped,
 * and a change made while it was is an event before anything is answered
 * (`wait` and `changes` call it; the tick does for a held wait or a job). */
void world_events_hear(World *w);
int  world_events_after(const World *w, unsigned after, const char *who);
/* "seq N", then every event after `after`, one a line. */
void world_events_write(const World *w, FILE *out, unsigned after, const char *who);
/* A change to the map that is an agent's own (its undo), made into one event
 * with where it was: called after the change, the caller having told the
 * GM's changes so far before it (world_events_map_flush). */
void world_event_of_change(World *w, int kind, int job, const char *who, const char *text);
/* Round a change that is not the GM's (an accept, the agent's undo): the
 * GM's changes so far become their event first, and the checkpoint starts
 * again after, so the change is not told twice. */
void world_events_map_flush(World *w);
void world_events_map_restart(World *w);

/* The squares the jobs' change sets may hold together, 8 bytes each: 32 MB
 * (docs/V2.md, *Memory: the plan*, 6; HEALTH 44). A plan of rooms is a few
 * thousand. A whole-512x512 tile fill is 262,144, so sixteen (JOB_MAX) come
 * exactly to it; one that changes both edge arrays too is 787,456, and the
 * sixth such set is refused to an agent. The file's own change (another
 * writer's) is never refused: it is counted, and told to the GM as it comes. */
#define JOB_CELLS_MAX (4L << 20)

/* Jobs (job.h): the list, a proposal coming in, a landing. */
void world_jobs_clear(World *w);
int  world_job_new(World *w, int from, const char *text, const CsBox *box, int at_once);
/* The slot world_job_new would take: a free one, else the oldest finished job,
 * which it clears (-1: none). */
int  world_job_slot_for_new(const World *w);
void world_job_set_proposal(World *w, int slot, ChangeSet *cs, const char *line);
void world_jobs_land_waiting(World *w);
int  world_job_find(const World *w, int num);
/* The squares every job's change set holds, but slot `skip`'s (-1: none). */
long world_jobs_cells(const World *w, int skip);
int  world_job_gm_only(const Job *j);     /* the file's or an --apply's: not an agent's to take, replace, answer or drop */
void world_job_thread_add(Job *j, char who, const char *text);
const char *world_job_state_name(int state);
const char *world_job_from_name(int from);
/* A thread line's speaker as the channel and a started agent read it. */
const char *world_job_who_name(char who);
/* The scratch map (ctl_scratch) as a fresh copy of the live map, its log
 * (ctl_sundo) cleared: a proposal is made in it, and a job's result read. */
Map *world_scratch_copy(World *w);
/* The map a job's proposal would make, accepted now: the scratch map, valid
 * until the next request. NULL when the job has none, or one made for
 * another size of map. */
Map *world_job_result(World *w, int slot);
/* A job goes, its number free again; and its change set lands, whole or the
 * part `box` holds, as one undo step tagged with whose it is. */
void world_job_clear(World *w, Job *j);
void world_job_land(World *w, Job *j, const CsBox *box);

#endif /* VTT_WORLD_H */
