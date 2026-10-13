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

typedef struct World {
    Map     *map;               /* the open map; during a request's edits, its scratch copy (app_ctl.c) */
    Undo     undo;              /* its log; a landing's batch says whose it is (undo_tag) */

    /* Jobs (job.h, app_job.c): asked of agents or offered by them, kept
     * with the map. */
    Job      jobs[JOB_MAX];
    int      job_last;          /* the last job number given out; the next is after it */

    /* Events for agents (app_event.c): a numbered ring `wait` reads. The
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
    unsigned ctl_refused;       /* the last refusal logged, hashed: a caller repeating itself is one line */
    Map     *ctl_scratch;
    Undo     ctl_sundo;
    int      ctl_auto;          /* :agent accept auto: each proposal lands at once */
    int      ctl_direct;        /* --apply with no GM: the map itself is edited */
    int      ctl_can_hold;      /* set round a request the socket can hold */
    int      ctl_held;          /* a `wait` with nothing to say asked to be held, */
    unsigned ctl_hold_seq;      /* from this event, */
    int      ctl_hold_ms;       /* this long */
    int      agent_on;          /* :agent on: off, a request over the socket is refused (docs/V2.md, ruling 9) */
} World;

#endif /* VTT_WORLD_H */
