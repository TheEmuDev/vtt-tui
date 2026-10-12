#ifndef VTT_LOOP_H
#define VTT_LOOP_H

#include <poll.h>
#include <stdint.h>

#include "app.h"
#include "input.h"
#include "render.h"
#include "term.h"

/* The interactive loop as three calls, so that a test can stand where poll
 * stands: loop_wait says what to sleep on and for how long, the caller polls
 * (or a test fills in `revents` by hand), and loop_once does one turn with
 * what was ready. Time comes in as an argument everywhere: nothing here reads
 * a clock, so a test's time is whatever it says. docs/V2.md, D2. */

/* stdin, the signal pipe, the remote view's listener and clients, the
 * channel's listener and connections. */
#define LOOP_FDS (2 + 1 + NET_MAX_CLIENTS + 1 + CTL_SLOTS)

typedef struct {
    App        *a;
    Term       *t;
    Renderer   *r;
    InputParser in;
    struct pollfd fds[LOOP_FDS];
    int         nnet, nctl;         /* how many of fds are the remote view's, the channel's */
} Loop;

void loop_init(Loop *l, App *a, Term *t, Renderer *r);

/* Fills l->fds for a poll of loop_nfds() entries and returns its timeout in
 * milliseconds: the nearest thing due, or -1 when nothing is -- an idle vtt
 * sleeps until something is readable. */
int loop_wait(Loop *l, uint64_t now_ms);
static inline int loop_nfds(const Loop *l) { return 2 + l->nnet + l->nctl; }

/* One turn, in the order the rest relies on: the sockets, the app's own
 * timers and the agents' requests, a resize, every key the terminal holds,
 * then what the keys made is told to waiting agents, then one frame if
 * anything changed. `nready` is poll's answer over l->fds. Returns 1 when the
 * loop should end (stdin closed, or the terminal gone), else 0. */
int loop_once(Loop *l, int nready, uint64_t now_ms);

/* A frame now, whether or not anything is dirty: the first paint. */
void loop_frame(Loop *l, uint64_t now_ms);

#endif /* VTT_LOOP_H */
