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

/* The channel's requests, from request.c's one table: the i-th's name (NULL
 * past the last) and what kind it is; and the edit verbs as "room, area, ..."
 * for the prompt an agent is started with. */
enum {
    CTL_RQ_EDIT  = 1,       /* changes the map: its request is a proposal */
    CTL_RQ_OFF   = 2,       /* taken over the socket with the channel off */
    CTL_RQ_ALONE = 4,       /* must be the only line of its request */
    CTL_RQ_LIVE  = 8,       /* means something only to a running vtt */
    CTL_RQ_NOMAP = 16,      /* answered with no map open */
};
const char *request_verb(int i, unsigned *flags);
void        request_edit_verbs(FILE *out);

#endif /* VTT_REQUEST_H */
