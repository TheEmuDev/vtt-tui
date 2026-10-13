#include "term.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "util.h"

#define ESC_ALT_ON    "\x1b[?1049h"
#define ESC_ALT_OFF   "\x1b[?1049l"
#define ESC_CURS_HIDE "\x1b[?25l"
#define ESC_CURS_SHOW "\x1b[?25h"
#define ESC_SGR_RESET "\x1b[0m"
#define ESC_CLEAR     "\x1b[2J\x1b[H"

/* The signal handler can only reach state through a file-scope pointer, and
 * only the self-pipe write fd is safe to touch from it. */
static volatile sig_atomic_t g_winch_fd = -1;

/* Set for the duration of raw mode so the fatal-signal handler can restore
 * the terminal before the process dies. Without this a SIGTERM leaves the
 * user's shell in raw mode with no cursor. */
static Term *g_term = NULL;

static void on_winch(int sig)
{
    (void)sig;
    if (g_winch_fd >= 0) {
        int saved = errno;               /* handlers must not clobber errno */
        char b = 1;
        ssize_t r = write((int)g_winch_fd, &b, 1);
        (void)r;                         /* a full pipe already means "pending" */
        errno = saved;
    }
}

/* Set by the fatal handler: term_shutdown is then inside a signal handler,
 * where free() is not safe. */
static volatile sig_atomic_t g_in_fatal = 0;

static void on_fatal(int sig)
{
    g_in_fatal = 1;
    if (g_term) term_shutdown(g_term);
    /* Re-raise with the default disposition so the exit status is honest. */
    signal(sig, SIG_DFL);
    raise(sig);
}

/* Raw mode over the saved settings. */
static int make_raw(Term *t)
{
    struct termios raw = t->saved;
    /* Disable canonical mode, echo, and signal generation so every keystroke
     * (Ctrl-C included) arrives as a plain byte for the input parser. */
    raw.c_iflag &= (tcflag_t)~(IXON | ICRNL | BRKINT | INPCK | ISTRIP);
    raw.c_oflag &= (tcflag_t)~(OPOST);
    raw.c_lflag &= (tcflag_t)~(ECHO | ICANON | ISIG | IEXTEN);
    raw.c_cflag |= (tcflag_t)CS8;
    /* Blocking reads are fine: poll() decides when there is input to take. */
    raw.c_cc[VMIN]  = 1;
    raw.c_cc[VTIME] = 0;

    if (tcsetattr(t->in_fd, TCSAFLUSH, &raw) < 0) return -1;
    t->raw = 1;
    return 0;
}

/* How long leaving the screen waits for a terminal that is behind to take
 * what is queued (the sequences that give the shell its screen back among
 * it), before giving up on one that never will. */
#define DRAIN_MS 1000

/* As much of buf as the terminal takes now. Stops at the first write it
 * would have to wait for; anything but that is the terminal gone. No
 * allocation: the fatal handler comes through here. */
static size_t write_some(Term *t, const char *buf, size_t n)
{
    size_t off = 0;
    while (off < n && !t->dead) {
        ssize_t w = write(t->out_fd, buf + off, n - off);
        if (w > 0) { off += (size_t)w; continue; }
        if (w < 0 && errno == EINTR) continue;
        if (w < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
        t->dead = 1;
    }
    return off;
}

/* Waits up to *ms for the terminal to take more; what is left of *ms after. */
static int wait_writable(Term *t, int *ms)
{
    struct pollfd pfd = { t->out_fd, POLLOUT, 0 };
    int step = *ms < 0 ? -1 : *ms < 50 ? *ms : 50;
    if (poll(&pfd, 1, step) < 0 && errno != EINTR) return -1;
    if (*ms > 0) *ms -= step;
    return 0;
}

size_t term_flush(Term *t)
{
    t->pend_off += write_some(t, (const char *)t->pend.data + t->pend_off, term_pending(t));
    if (t->pend_off == t->pend.len) t->pend_off = t->pend.len = 0;
    return term_pending(t);
}

/* Leaving the screen: what is queued, then the sequences that give the shell
 * its screen back, each waited for until `ms` runs out -- a terminal that
 * never reads again is given up on. Nothing is queued and nothing allocated:
 * term_shutdown runs inside the fatal handler too. */
static void term_leave(Term *t, const char *const *seq, int nseq, int ms)
{
    while (term_pending(t) && !t->dead && ms > 0) {
        if (wait_writable(t, &ms) < 0) break;
        term_flush(t);
    }
    for (int i = 0; i < nseq && !term_pending(t) && !t->dead; i++) {
        size_t n = strlen(seq[i]), off = 0;
        while (off < n && !t->dead) {
            off += write_some(t, seq[i] + off, n - off);
            if (off < n && (!t->async || ms <= 0 || wait_writable(t, &ms) < 0)) break;
        }
        if (off < n) break;
    }
}

int term_async(Term *t)
{
    const char *name = ttyname(t->out_fd);
    if (!name) return -1;
    int fd = open(name, O_WRONLY | O_NONBLOCK | O_NOCTTY | O_CLOEXEC);
    if (fd < 0) return -1;
    t->out_fd = fd;
    t->async  = 1;
    return 0;
}

int term_init(Term *t)
{
    memset(t, 0, sizeof *t);
    t->in_fd  = STDIN_FILENO;
    t->out_fd = STDOUT_FILENO;
    t->sig_pipe[0] = t->sig_pipe[1] = -1;

    if (!isatty(t->in_fd) || !isatty(t->out_fd)) {
        errno = ENOTTY;
        return -1;
    }
    if (tcgetattr(t->in_fd, &t->saved) < 0) return -1;

    if (make_raw(t) < 0) return -1;

    /* These two descriptors are deliberately left blocking. In a terminal
     * stdin and stdout are the same open file description, so setting
     * O_NONBLOCK on stdin sets it on stdout too. The event loop polls before
     * each read instead, which drains input just as well; and output that
     * must not wait goes through a description of its own (term_async). */

    g_term = t;

    if (pipe(t->sig_pipe) < 0) {
        term_shutdown(t);
        return -1;
    }
    (void)fd_nonblock_cloexec(t->sig_pipe[0]);
    (void)fd_nonblock_cloexec(t->sig_pipe[1]);
    g_winch_fd = t->sig_pipe[1];

    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_winch;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGWINCH, &sa, NULL);
    /* A started agent that stops (app_agent.c) wakes the loop the same way:
     * the tick reaps it. The pipe has no lost wake-up to race with poll. */
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    sigaction(SIGCHLD, &sa, NULL);

    sa.sa_handler = on_fatal;
    sa.sa_flags   = 0;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGHUP,  &sa, NULL);

    /* A write to a closed pty must not kill us mid-frame. */
    signal(SIGPIPE, SIG_IGN);

    term_update_size(t);

    term_write(t, ESC_ALT_ON, sizeof ESC_ALT_ON - 1);
    term_write(t, ESC_CURS_HIDE, sizeof ESC_CURS_HIDE - 1);
    term_write(t, ESC_CLEAR, sizeof ESC_CLEAR - 1);
    t->altscreen = 1;

    return 0;
}

static const char *const LEAVE[] = { ESC_SGR_RESET, ESC_CURS_SHOW, ESC_ALT_OFF };

void term_shutdown(Term *t)
{
    if (!t) return;

    term_leave(t, LEAVE, t->altscreen ? 3 : 0, DRAIN_MS);
    t->altscreen = 0;
    if (t->async) {
        close(t->out_fd);
        t->out_fd = STDOUT_FILENO;
        t->async  = 0;
        t->pend_off = t->pend.len = 0;
        if (!g_in_fatal) bb_free(&t->pend);
    }
    if (t->raw) {
        tcsetattr(t->in_fd, TCSAFLUSH, &t->saved);
        t->raw = 0;
    }
    g_winch_fd = -1;
    if (t->sig_pipe[0] >= 0) { close(t->sig_pipe[0]); t->sig_pipe[0] = -1; }
    if (t->sig_pipe[1] >= 0) { close(t->sig_pipe[1]); t->sig_pipe[1] = -1; }
    g_term = NULL;
}

void term_suspend(Term *t)
{
    if (!t) return;
    term_leave(t, LEAVE, t->altscreen ? 3 : 0, DRAIN_MS);   /* the terminal is about to be someone else's */
    t->altscreen = 0;
    t->pend_off = t->pend.len = 0;      /* what it never took belongs to a screen that is gone */
    if (t->raw) {
        tcsetattr(t->in_fd, TCSAFLUSH, &t->saved);
        t->raw = 0;
    }
}

int term_resume(Term *t)
{
    if (!t) return -1;
    if (!t->raw && make_raw(t) < 0) return -1;
    term_update_size(t);
    term_write(t, ESC_ALT_ON, sizeof ESC_ALT_ON - 1);
    term_write(t, ESC_CURS_HIDE, sizeof ESC_CURS_HIDE - 1);
    term_write(t, ESC_CLEAR, sizeof ESC_CLEAR - 1);
    t->altscreen = 1;
    return 0;
}

int term_update_size(Term *t)
{
    struct winsize ws;
    int w = 80, h = 24;      /* conservative fallback */

    if (ioctl(t->out_fd, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0 && ws.ws_row > 0) {
        w = ws.ws_col;
        h = ws.ws_row;
    }
    if (w == t->w && h == t->h) return 0;
    t->w = w;
    t->h = h;
    return 1;
}

int term_drain_signals(Term *t)
{
    char    buf[64];
    int     got = 0;
    ssize_t n;

    while ((n = read(t->sig_pipe[0], buf, sizeof buf)) > 0) got = 1;
    if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) return 0;
    return got;
}

size_t term_write(Term *t, const char *buf, size_t n)
{
    /* Async: nothing waits. What is queued goes first, to keep the order;
     * what the terminal will not take now joins the queue, and counts as
     * written -- it will be, in order, and the renderer's `front` may go on
     * meaning what the terminal has been sent. */
    if (t->async) {
        if (t->dead) return 0;
        size_t off = term_pending(t) ? 0 : write_some(t, buf, n);
        if (t->dead) return off;
        if (off < n) bb_put(&t->pend, buf + off, n - off);
        return n;
    }

    /* Blocking: the terminal behind is waited for, not given up on. Dropping
     * the tail of a frame would leave cells on screen that the renderer
     * believes it has painted, and nothing repaints them afterwards. */
    size_t off = 0;
    int    forever = -1;
    while (off < n && !t->dead) {
        off += write_some(t, buf + off, n - off);
        if (off < n && !t->dead && wait_writable(t, &forever) < 0) t->dead = 1;
    }
    return off;
}

int term_input_ready(const Term *t)
{
    struct pollfd pfd;
    pfd.fd      = t->in_fd;
    pfd.events  = POLLIN;
    pfd.revents = 0;

    int r = poll(&pfd, 1, 0);
    return r > 0 && (pfd.revents & (POLLIN | POLLHUP)) != 0;
}
