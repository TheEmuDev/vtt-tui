#include "loop.h"

#include <errno.h>
#include <unistd.h>

#include "apikey.h"
#include "prof.h"

/* Upper bound on bytes taken from the terminal in one turn, so a flood of
 * input can never postpone the redraw indefinitely. Far larger than any
 * burst a held key produces. */
#define INPUT_DRAIN_MAX ((size_t)64 * 1024)

void loop_init(Loop *l, App *a, Term *t, Renderer *r)
{
    l->a = a;
    l->t = t;
    l->r = r;
    l->nnet = l->nctl = 0;
    input_init(&l->in);
    l->fds[0].fd = t->in_fd;
    l->fds[0].events = POLLIN;
    l->fds[0].revents = 0;
    l->fds[1].fd = term_signal_fd(t);
    l->fds[1].events = POLLIN;
    l->fds[1].revents = 0;
    l->fds[2].fd = -1;
    l->fds[2].events = POLLOUT;
    l->fds[2].revents = 0;
}

static void sooner(int *timeout, int due)
{
    if (due >= 0 && (*timeout < 0 || due < *timeout)) *timeout = due;
}

int loop_wait(Loop *l, uint64_t now_ms)
{
    App *a = l->a;
    /* Block indefinitely when there is nothing outstanding: an idle vtt
     * must cost zero CPU. An unresolved ESC needs a decision after a short
     * grace period. */
    int timeout = input_pending(&l->in) ? INPUT_ESC_TIMEOUT_MS : -1;
    /* Woken when a terminal that is behind can take more. */
    l->fds[2].fd      = term_pending(l->t) ? l->t->out_fd : -1;
    l->fds[2].revents = 0;
    /* The sockets' entries are rebuilt every turn: clients come and go. */
    l->nnet = net_pollfds(&a->net, l->fds + LOOP_FIXED, 1 + NET_MAX_CLIENTS);
    /* With clients attached, wake now and then for keep-alives. */
    if (l->nnet > 1) sooner(&timeout, 1000);
    sooner(&timeout, app_autosave_due(a, now_ms));   /* unsaved work not yet copied */
    sooner(&timeout, app_ping_due(a, now_ms));       /* a ping's ring coming down */
    sooner(&timeout, app_events_due(a, now_ms));     /* the map's changes, told to an agent */
    sooner(&timeout, app_agents_due(a, now_ms));     /* a started agent gone silent */
    sooner(&timeout, app_disk_due(a, now_ms));       /* a file someone else wrote */
    l->nctl = ctl_pollfds(&a->ctl, l->fds + LOOP_FIXED + l->nnet, 1 + CTL_SLOTS);
    sooner(&timeout, ctl_due(&a->ctl, now_ms));      /* an agent's connection gone quiet */
    return timeout;
}

void loop_frame(Loop *l, uint64_t now_ms)
{
    App *a = l->a;
    prof_frame_begin();
    app_frame(a, l->t, now_ms);
    prof_frame_end();
    prof_set_counters(l->r->cells_changed, l->r->bytes_written);
    if (net_clients(&a->net)) prof_set_net((uint32_t)net_clients(&a->net), a->net.frame_bytes);
    a->dirty = 0;
}

/* Everything the terminal has queued, dispatched as it is read. Coalescing a
 * burst of held-down keys into one frame is what keeps movement snappy: the
 * frame is still drawn once, after this.
 *
 * Reads are sized to what the parser can still hold, because input_feed()
 * drops the excess -- a long burst would otherwise lose keystrokes silently.
 * The byte cap bounds how long one turn can spend here so a flood cannot
 * starve the redraw. Returns 1 at end of input. */
static int drain_input(Loop *l)
{
    App   *a = l->a;
    size_t drained = 0;

    while (drained < INPUT_DRAIN_MAX && term_input_ready(l->t)) {
        Key    k;
        size_t room = input_room(&l->in);
        if (room == 0) {
            while (input_next(&l->in, &k)) app_key(a, k);
            room = input_room(&l->in);
            if (room == 0) break;    /* cannot happen; guard anyway */
        }

        char   buf[512];
        size_t want = room < sizeof buf ? room : sizeof buf;

        ssize_t n = read(l->t->in_fd, buf, want);
        if (n > 0) {
            input_feed(&l->in, buf, (size_t)n);
            /* What was read may be a key pasted into the hidden prompt: no
             * copy of it left on this stack once the parser has it. */
            apikey_wipe(buf, (size_t)n);
            drained += (size_t)n;
            while (input_next(&l->in, &k)) app_key(a, k);
            continue;
        }
        if (n == 0)         return 1;
        if (errno == EINTR) continue;
        break;
    }
    return 0;
}

int loop_once(Loop *l, int nready, uint64_t now_ms)
{
    App *a = l->a;

    if (l->nnet > 0) net_service(&a->net, l->fds + LOOP_FIXED, l->nnet, now_ms);
    if (l->nctl > 0) ctl_service(&a->ctl, l->fds + LOOP_FIXED + l->nnet, l->nctl, now_ms);
    app_tick(a, now_ms);

    if (nready > 0 && (l->fds[1].revents & POLLIN)) {
        if (term_drain_signals(l->t) && term_update_size(l->t)) {
            rnd_resize(l->r, l->t->w, l->t->h);
            a->dirty = 1;
        }
    }

    if (nready > 0 && (l->fds[0].revents & (POLLIN | POLLHUP)) && drain_input(l)) return 1;

    Key k;
    while (input_next(&l->in, &k)) app_key(a, k);    /* a script fed before the first turn */

    /* poll() returned with nothing readable, so the pending ESC really was
     * the Escape key rather than a slow sequence. */
    if (nready == 0 && input_pending(&l->in) && input_timeout(&l->in, &k)) app_key(a, k);

    /* A waiting agent hears of a key's verdict now, not at the next wake:
     * with nothing else due, poll would sleep on it for ever. Every turn,
     * since keys are handled in the drain above too. */
    app_events_flush(a, now_ms);

    /* The terminal's queue, and the frame held back while it was full. */
    if (term_pending(l->t)) {
        PROF_ZONE("term.flush");
        term_flush(l->t);
    }
    if (l->t->skipped && !term_pending(l->t)) {
        l->t->skipped = 0;
        a->dirty = 1;
    }

    if (a->dirty) loop_frame(l, now_ms);

    /* Only a genuine terminal failure ends the loop; one that has fallen
     * behind is neither waited for nor written past (term_write, rnd_flush). */
    return l->t->dead;
}
