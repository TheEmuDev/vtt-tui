/* The interactive loop (src/loop.c), driven from where poll stands: a pipe
 * for the keyboard, time as an argument, and the order of a turn checked
 * rather than re-enacted by hand in each test that needs it. */
#include <fcntl.h>
#include <poll.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "harness.h"
#include "loop.h"

typedef struct {
    Sandbox  sb;
    Term     t;
    Renderer r;
    App      a;
    Loop     l;
    int      keys;          /* write end: what the GM types */
} Rig;

static void rig_up(Rig *g, const char *name)
{
    memset(g, 0, sizeof *g);
    g->sb = sandbox_enter(name);
    int in[2], sig[2];
    CHECK(pipe(in) == 0 && pipe(sig) == 0);
    fcntl(sig[0], F_SETFL, O_NONBLOCK);
    g->keys          = in[1];
    g->t.in_fd       = in[0];
    g->t.out_fd      = open("/dev/null", O_WRONLY);
    g->t.w           = 80;
    g->t.h           = 24;
    g->t.sig_pipe[0] = sig[0];
    g->t.sig_pipe[1] = sig[1];
    rnd_init(&g->r);
    rnd_resize(&g->r, 80, 24);
    app_init(&g->a, &g->t, &g->r);
    app_open_map(&g->a, "tests/fixtures/two-rooms.vtt");
    loop_init(&g->l, &g->a, &g->t, &g->r);
    loop_frame(&g->l, 1000);
}

static void rig_down(Rig *g)
{
    app_free(&g->a);
    rnd_free(&g->r);
    if (g->keys >= 0) close(g->keys);
    close(g->t.in_fd);
    close(g->t.out_fd);
    close(g->t.sig_pipe[0]);
    close(g->t.sig_pipe[1]);
    sandbox_leave(&g->sb);
}

/* One turn as main runs it, but never sleeping: what is ready now. */
static int turn(Rig *g, uint64_t now_ms)
{
    loop_wait(&g->l, now_ms);
    int n = poll(g->l.fds, (nfds_t)loop_nfds(&g->l), 0);
    return loop_once(&g->l, n, now_ms);
}

static void type(Rig *g, const char *s)
{
    CHECK(write(g->keys, s, strlen(s)) == (ssize_t)strlen(s));
}

void test_loop(void)
{
    Rig g;
    rig_up(&g, "loop");
    CHECK_EQ(g.sb.ok, 1);
    if (!g.sb.ok) { rig_down(&g); return; }

    CASE("idle, with nothing owed, the loop asks to sleep without a timeout");
    CHECK_EQ(loop_wait(&g.l, 1000), -1);
    CHECK_EQ(loop_nfds(&g.l), 2);                  /* the keyboard and the signal pipe: no sockets */

    CASE("a turn with nothing ready changes nothing and draws nothing");
    g.r.cells_changed = 7;                         /* rnd_begin would zero it: a mark no frame leaves */
    CHECK_EQ(turn(&g, 1001), 0);
    CHECK_EQ(g.r.cells_changed, 7);

    CASE("keys already in the parser (--script) are handled by a turn with nothing ready");
    int cy0 = g.a.ed.cy;
    input_feed(&g.l.in, "jj", 2);
    CHECK_EQ(turn(&g, 1001), 0);
    CHECK_EQ(g.a.ed.cy, cy0 + 2);
    type(&g, "kk");
    turn(&g, 1001);
    CHECK_EQ(g.a.ed.cy, cy0);

    CASE("keys typed are handled and drawn in the one turn");
    int cy = g.a.ed.cy;
    type(&g, "jj");
    CHECK_EQ(turn(&g, 1002), 0);
    CHECK_EQ(g.a.ed.cy, cy + 2);
    CHECK_EQ(g.a.dirty, 0);                        /* the frame went out */
    CHECK(g.r.cells_changed > 0);

    CASE("a burst longer than one read is all handled in the turn, with one frame after it");
    {
        char burst[4097];
        for (int i = 0; i < 4096; i += 2) { burst[i] = 'j'; burst[i + 1] = 'k'; }
        burst[4096] = 'j';
        cy = g.a.ed.cy;
        CHECK(write(g.keys, burst, sizeof burst) == (ssize_t)sizeof burst);
        CHECK_EQ(turn(&g, 1002), 0);
        CHECK_EQ(g.a.ed.cy, cy + 1);
        CHECK_EQ(g.a.dirty, 0);
        g.r.cells_changed = 7;
        CHECK_EQ(turn(&g, 1002), 0);               /* nothing was left behind for a second turn */
        CHECK_EQ(g.r.cells_changed, 7);
    }

    CASE("a lone ESC waits its grace period, then is the Escape key");
    type(&g, ":");
    turn(&g, 1003);
    CHECK_EQ(g.a.ed.mode, ED_COMMAND);
    type(&g, "\x1b");
    CHECK_EQ(turn(&g, 1004), 0);
    CHECK_EQ(g.a.ed.mode, ED_COMMAND);             /* it may yet be the start of a sequence */
    CHECK_EQ(loop_wait(&g.l, 1004), INPUT_ESC_TIMEOUT_MS);
    CHECK_EQ(loop_once(&g.l, 0, 1004 + INPUT_ESC_TIMEOUT_MS), 0);     /* poll timed out */
    CHECK(g.a.ed.mode != ED_COMMAND);
    CHECK_EQ(loop_wait(&g.l, 1100), -1);

    CASE("a resize signal resizes the renderer and repaints");
    rnd_resize(&g.r, 100, 30);                     /* as if the window had been larger: */
    g.t.w = 100;
    g.t.h = 30;
    CHECK(write(g.t.sig_pipe[1], "w", 1) == 1);
    g.a.dirty = 0;
    CHECK_EQ(turn(&g, 1101), 0);
    CHECK_EQ(g.r.w, 80);                           /* /dev/null has no size: the 80x24 fallback */
    CHECK_EQ(g.r.h, 24);
    CHECK_EQ(g.r.cells_changed, 80 * 24);          /* and the whole of it painted again */
    CHECK(write(g.t.sig_pipe[1], "w", 1) == 1);    /* a signal with the size as it was: */
    g.r.cells_changed = 7;
    CHECK_EQ(turn(&g, 1102), 0);
    CHECK_EQ(g.r.cells_changed, 7);                /* nothing drawn */

    /* Step 5's bug (docs/CONFLICTS.md): keys handled in the drain never
     * reached the flush, so a waiting agent slept on a verdict until the
     * next wake. The order is loop_once's now, and this holds it. */
    CASE("an agent's held wait is answered in the turn of the key that made the event");
    type(&g, ":agent on\r");
    turn(&g, 1200);
    CHECK_EQ(g.a.agent_on, 1);
    loop_wait(&g.l, 1200);
    CHECK_EQ(loop_nfds(&g.l), 3);                  /* the channel's listener beside the two */
    CtlReader rd = { ctl_raw_connect(g.a.ctl.path), "", 0, 0 };
    CHECK(rd.fd >= 0);
    CHECK(write(rd.fd, "wait for 600\n", 13) == 13);
    shutdown(rd.fd, SHUT_WR);
    uint64_t now = 1300;
    for (int k = 0; k < 200 && ctl_waiters(&g.a.ctl) < 1; k++) { turn(&g, now++); poll(NULL, 0, 1); }
    CHECK_EQ(ctl_waiters(&g.a.ctl), 1);
    CHECK_EQ(ctl_read_some(&rd), 0);               /* held: no answer yet */
    CHECK(loop_wait(&g.l, now) > 500000);          /* sleeping on the wait's own deadline, not spinning */
    type(&g, ":ask a well\r");
    CHECK_EQ(turn(&g, now++), 0);                  /* one turn: the key, the event, the answer */
    CHECK_EQ(ctl_waiters(&g.a.ctl), 0);
    for (int k = 0; k < 200 && !ctl_read_some(&rd); k++) { turn(&g, now++); poll(NULL, 0, 1); }
    CHECK_EQ(rd.done, 1);
    CHECK(strstr(rd.buf, "job 1 asked: a well") != NULL);
    close(rd.fd);

    CASE("a held wait whose time runs out is answered by the turn its deadline wakes");
    {
        CtlReader rw = { ctl_raw_connect(g.a.ctl.path), "", 0, 0 };
        CHECK(write(rw.fd, "wait 1 for 600\n", 15) == 15);
        shutdown(rw.fd, SHUT_WR);
        for (int k = 0; k < 200 && ctl_waiters(&g.a.ctl) < 1; k++) { turn(&g, now++); poll(NULL, 0, 1); }
        CHECK_EQ(ctl_waiters(&g.a.ctl), 1);
        int left = loop_wait(&g.l, now);
        CHECK(left > 500000 && left <= 600000);
        now += (uint64_t)left + 1;
        CHECK_EQ(loop_once(&g.l, 0, now), 0);      /* poll timed out */
        CHECK_EQ(ctl_waiters(&g.a.ctl), 0);
        for (int k = 0; k < 200 && !ctl_read_some(&rw); k++) { turn(&g, now++); poll(NULL, 0, 1); }
        CHECK_EQ(rw.done, 1);
        CHECK(!strncmp(rw.buf, "ok\nseq 1\n", 9));
        close(rw.fd);
    }

    CASE("the keyboard closing ends the loop");
    close(g.keys);
    g.keys = -1;
    CHECK_EQ(turn(&g, now++), 1);

    rig_down(&g);

    CASE("a terminal that has gone ends the loop");
    rig_up(&g, "loop-dead");
    g.t.dead = 1;
    CHECK_EQ(turn(&g, 1000), 1);
    rig_down(&g);
}
