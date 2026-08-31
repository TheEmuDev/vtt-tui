#include "term.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

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

static void on_fatal(int sig)
{
    if (g_term) term_shutdown(g_term);
    /* Re-raise with the default disposition so the exit status is honest. */
    signal(sig, SIG_DFL);
    raise(sig);
}

static void set_nonblock_cloexec(int fd)
{
    int fl = fcntl(fd, F_GETFL, 0);
    if (fl >= 0) (void)fcntl(fd, F_SETFL, fl | O_NONBLOCK);
    int fd_fl = fcntl(fd, F_GETFD, 0);
    if (fd_fl >= 0) (void)fcntl(fd, F_SETFD, fd_fl | FD_CLOEXEC);
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

    /* The terminal descriptors are deliberately left blocking. In a terminal
     * stdin and stdout are the same open file description, so setting
     * O_NONBLOCK on stdin sets it on stdout too, and writes then fail with
     * EAGAIN as soon as the terminal falls behind -- which is exactly what
     * holding a movement key does. The event loop polls before each read
     * instead, which drains input just as well without that side effect. */

    g_term = t;

    if (pipe(t->sig_pipe) < 0) {
        term_shutdown(t);
        return -1;
    }
    set_nonblock_cloexec(t->sig_pipe[0]);
    set_nonblock_cloexec(t->sig_pipe[1]);
    g_winch_fd = t->sig_pipe[1];

    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_winch;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGWINCH, &sa, NULL);

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

void term_shutdown(Term *t)
{
    if (!t) return;

    if (t->altscreen) {
        term_write(t, ESC_SGR_RESET, sizeof ESC_SGR_RESET - 1);
        term_write(t, ESC_CURS_SHOW, sizeof ESC_CURS_SHOW - 1);
        term_write(t, ESC_ALT_OFF, sizeof ESC_ALT_OFF - 1);
        t->altscreen = 0;
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
    size_t off = 0;

    while (off < n) {
        ssize_t w = write(t->out_fd, buf + off, n - off);
        if (w > 0) {
            off += (size_t)w;
            continue;
        }
        if (w < 0 && errno == EINTR) continue;

        if (w < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            /* The terminal is behind, not gone. Waiting is the whole point:
             * dropping the tail of a frame leaves cells on screen that the
             * renderer believes it has already painted, and nothing repaints
             * them afterwards. Blocking here is what a blocking write would
             * do anyway, without the spin. */
            struct pollfd pfd;
            pfd.fd      = t->out_fd;
            pfd.events  = POLLOUT;
            pfd.revents = 0;
            if (poll(&pfd, 1, -1) < 0 && errno != EINTR) {
                t->dead = 1;
                break;
            }
            continue;
        }

        /* Anything else is a real failure: the terminal has gone away. */
        t->dead = 1;
        break;
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
