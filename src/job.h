#ifndef VTT_JOB_H
#define VTT_JOB_H

/* Jobs (docs/CONFLICTS.md): a change asked of an agent, or offered by one,
 * and what came back. The GM asks with :ask over a v box; the agent takes it,
 * may declare where it will work, and proposes a change set; the GM reviews
 * it in place and accepts it (whole or by box), scraps it, or sends it back
 * with a line of feedback. Nobody's change lands without the GM, unless the
 * GM asked for it at once (:ask!). Jobs belong to the map they were asked
 * on: they close with it. */

#include "changeset.h"

#define JOB_MAX         16
#define JOB_NUM_MAX     99
#define JOB_TEXT_MAX    160
#define JOB_THREAD_MAX  8

typedef enum {
    JOB_ASKED,         /* waiting for an agent */
    JOB_WORKING,       /* an agent has it (or has it back, with feedback) */
    JOB_READY,         /* a change set came in: the GM's to review */
    JOB_ACCEPTED,
    JOB_SCRAPPED,      /* kept until the map closes: :review N brings it back */
} JobState;

typedef enum {
    JOB_FROM_GM,       /* :ask */
    JOB_FROM_AGENT,    /* an agent's own idea */
    JOB_FROM_APPLY,    /* --apply to the open map */
    JOB_FROM_DISK,     /* the file changed under the open map */
} JobFrom;

/* One line of a job's history (counter-suggestion C): the GM's words, the
 * agent's, or what came of a proposal. Oldest dropped when full. */
typedef struct {
    char who;                      /* 'G' the GM, 'A' the agent, '-' what happened */
    char text[JOB_TEXT_MAX];
} JobLine;

typedef struct {
    uint8_t  used;
    uint8_t  state;                /* JobState */
    uint8_t  from;                 /* JobFrom */
    uint8_t  at_once;              /* :ask! -- the result lands when it comes */
    int      num;                  /* 1..JOB_NUM_MAX, the lowest free when made */
    char     text[JOB_TEXT_MAX];   /* what was asked */
    int      has_box;              /* the GM's v box when asked */
    CsBox    box;
    int      has_area;             /* where the agent said it would work */
    CsBox    area;
    JobLine  thread[JOB_THREAD_MAX];
    int      nthread;
    char     summary[200];         /* the change set's, when ready */
    int      has_cs;
    ChangeSet cs;
} Job;

#endif /* VTT_JOB_H */
