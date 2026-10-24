#ifndef VTT_REQUEST_H
#define VTT_REQUEST_H

/* The control channel's language (docs/CONTROL.md, docs/AGENTS.md): a request
 * run against a World (docs/V2.md, D2). Nothing here or in request.c knows
 * the app: what a request needs of the GM's screen it asks the world's host. */

#include <stddef.h>
#include <stdio.h>

#include "world.h"

/* Runs one request and returns the answer, verdict line first, malloc'd, its
 * length in *len. The app's tick hands it every request the socket has
 * read; --apply and the tests call it straight. */
char *request_exec(World *world, const char *req, size_t *len);

/* The channel's words (request.c splits a line into them), and what its
 * other files share: a region by name (an area's name, or B2:K12), refused
 * off the map; the requests about jobs (request_job.c): 0 done, -1 with why
 * in err, 1 not one of them. */
#define CTL_WORDS    12
#define CTL_WORD_MAX 256
int  request_region(const Map *m, const char *w, int *x0, int *y0, int *x1, int *y1,
                    char *err, size_t errsz);
int  request_job(World *world, char w[][CTL_WORD_MAX], int n, FILE *out, char *err, size_t errsz);
/* Whether the request's name may act on a job: nobody has it, or it has. 0
 * with why in err (class `state`) when it is another's. */
int  request_job_mine(World *world, const Job *j, char *err, size_t errsz);
/* The request's name takes a job nobody has: the one rule for `take`, `area`
 * and a proposal. 1 when it did (the job was nobody's). */
int  request_job_claim(World *world, Job *j);
/* The channel's step back, for `undo` and `job N undo` alike: the newest
 * `steps` batches, job `job`'s (0: a change that had none), told to the
 * other names as one event with where. 0 with why in err when the GM is
 * busy (app_ctl_busy, or reviewing that very job): nothing was done. */
int  request_undo(World *world, int steps, int job, char *err, size_t errsz);
/* `dump`'s words after the verb, for the live map and a job's alike: none
 * (the whole map, refused above CTL_DUMP_SQUARES: an agent that forgot the
 * region should not be sent half a megabyte), `all`, or a region. 0 with why
 * in err. */
int  request_dump(const World *world, const Map *m, char w[][CTL_WORD_MAX], int n, int at, int count, FILE *out, char *err, size_t errsz);
/* A read is about to walk this many squares of map: 0, with why in err,
 * when the request has then read more than CTL_READ_SQUARES (`count` above
 * is whether the dump counts its own: a job's result is counted where it is
 * made). */
int  request_read(const World *world, long squares, char *err, size_t errsz);
/* The class of the refusal being written (ctl.h's CTL_E_*; `usage` unless
 * said). Returns -1, to be returned with it. */
int  request_class(int cls);
/* A word that is a whole number from lo to hi, into *out; 0 when it is not. */
int  request_int(const char *w, int lo, int hi, int *out);

/* What a request's edits have done so far: how many lines, the area they
 * touched (for the GM's ring), and the first line, for the status. */
typedef struct {
    int      lines;
    int      x0, y0, x1, y1;          /* x1 < x0 while nothing is touched */
    char     first[48];
    unsigned ops0;                    /* the undo stamp when the first edit began */
    int      deleted;                 /* a creature went: indices behind it shifted */
    FILE    *out;                     /* the answer, for what an edit has to say */

    /* A proposal: the edits run on the scratch map, with the live map and
     * its log set aside here until the request ends. */
    int      proposal;
    Map     *live;
    Undo     live_undo;
    int      header;                  /* a `propose` or `job N propose` line came */
    int      from;                    /* JobFrom of a new job: an agent, or --apply */
    int      job;                     /* the job it answers, or -1 for a new one */
    char     say[JOB_TEXT_MAX];       /* the line it came with */
} Edits;

/* The proposal's side of a request (request_propose.c). request_propose
 * reads a `propose` or `job N propose` line into ed (0, or -1 with why in
 * err); begin and end set the live map and its log aside round the edits
 * and put them back; live_swap swaps them in and out for a read of the GM's
 * side mid-proposal (call it twice); say_changed is what a request that
 * landed says, with the ring round it; finish_proposal makes the change set
 * a job's proposal (-1 with why in err when there is no room for it). */
int  request_propose(World *world, char w[][CTL_WORD_MAX], int n, Edits *ed, char *err, size_t errsz);
void request_proposal_begin(World *world, Edits *ed);
void request_proposal_end(World *world, Edits *ed);
void request_live_swap(World *world, Edits *ed);
void request_say_changed(World *world, const Edits *ed, FILE *out);
int  request_finish_proposal(World *world, Edits *ed, FILE *out, char *err, size_t errsz);

/* The channel's requests, from request.c's one table: the i-th's name (NULL
 * past the last) and what kind it is; and the edit verbs as "room, area, ..."
 * for the prompt an agent is started with. */
enum {
    CTL_RQ_EDIT  = 1,       /* changes the map: its request is a proposal */
    CTL_RQ_OFF   = 2,       /* taken over the socket with the channel off */
    CTL_RQ_ALONE = 4,       /* must be the only line of its request */
    CTL_RQ_LIVE  = 8,       /* means something only to a running vtt */
    CTL_RQ_NOMAP = 16,      /* answered with no map open */
    CTL_RQ_PLAN  = 32,      /* in the agents' guide for a plan (`vtt --agent-guide plan`) */
    CTL_RQ_FORLIVE = 64,    /* ... and for live work (`live`) */
};
const char *request_verb(int i, unsigned *flags);
void        request_edit_verbs(FILE *out);
/* How a request is written and its rules, as `help VERB` says it: 0, or -1
 * when there is no such request. */
int  request_help_verb(FILE *out, const char *verb);
/* Every request whose flags hold any of `topic` (CTL_RQ_PLAN, CTL_RQ_FORLIVE),
 * in the table's order, each as `help VERB` says it: the guide's reference,
 * written once, here. */
void request_reference(FILE *out, unsigned topic);

#endif /* VTT_REQUEST_H */
