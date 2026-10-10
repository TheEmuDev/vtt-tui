#include "ctl.h"

#include <dirent.h>
#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#include "util.h"

/* ------------------------------------------------------------ the place */

int ctl_dir(char *buf, size_t sz)
{
    const char *rt = getenv("XDG_RUNTIME_DIR");
    int n = rt && rt[0] == '/' ? snprintf(buf, sz, "%s/vtt", rt)
                               : snprintf(buf, sz, "/tmp/vtt-%u", (unsigned)getuid());
    if (n < 0 || (size_t)n >= sz) return -1;
    return 0;
}

/* Ours alone: a real directory (not a link to one), owned by this user,
 * that nobody else can list or enter. The server makes it if it is not
 * there; the client only checks, since a socket in a directory anyone else
 * could write to may be anyone's -- and whatever answers is printed as
 * the vtt's answer to an agent that believes it. */
static int dir_ours(const char *dir, char *err, size_t errsz)
{
    struct stat st;
    if (lstat(dir, &st) < 0) {
        snprintf(err, errsz, "cannot read %s: %s", dir, strerror(errno));
        return -1;
    }
    if (!S_ISDIR(st.st_mode) || st.st_uid != getuid() || (st.st_mode & 077)) {
        snprintf(err, errsz, "%s is not this user's alone - not using it", dir);
        return -1;
    }
    return 0;
}

static int dir_ready(const char *dir, char *err, size_t errsz)
{
    if (mkdir(dir, 0700) < 0 && errno != EEXIST) {
        snprintf(err, errsz, "cannot make %s: %s", dir, strerror(errno));
        return -1;
    }
    return dir_ours(dir, err, errsz);
}

static int sock_path(char *buf, size_t sz, const char *dir, long pid)
{
    int n = snprintf(buf, sz, "%s/%ld.sock", dir, pid);
    return n < 0 || (size_t)n >= sz ? -1 : 0;
}

/* ---------------------------------------------------------------- server */

void ctl_init(Ctl *c)
{
    memset(c, 0, sizeof *c);
    c->listen_fd = -1;
    for (int i = 0; i < CTL_SLOTS; i++) c->c[i].fd = -1;
}

int ctl_start(Ctl *c, char *err, size_t errsz)
{
    if (ctl_active(c)) return 0;

    char dir[CTL_PATH_MAX];
    if (ctl_dir(dir, sizeof dir) < 0) { snprintf(err, errsz, "no room for a socket path"); return -1; }
    if (dir_ready(dir, err, errsz) < 0) return -1;
    char path[CTL_PATH_MAX];
    if (sock_path(path, sizeof path, dir, (long)getpid()) < 0) {
        snprintf(err, errsz, "the socket path is too long");
        return -1;
    }

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) { snprintf(err, errsz, "socket: %s", strerror(errno)); return -1; }
    if (fd_nonblock_cloexec(fd) < 0) {
        snprintf(err, errsz, "socket: %s", strerror(errno));
        close(fd);
        return -1;
    }

    struct sockaddr_un sa;
    memset(&sa, 0, sizeof sa);
    sa.sun_family = AF_UNIX;
    if (strlen(path) + 4 >= sizeof sa.sun_path) {      /* ".new" is as long as ".sock" less one */
        snprintf(err, errsz, "the socket path is too long: %.60s...", path);
        close(fd);
        return -1;
    }
    str_lcpy(sa.sun_path, path, sizeof sa.sun_path);
    /* Bound and listening under a name --ctl does not look at, then moved
     * into place: a client finding it between bind and listen would be
     * refused, take it for a crashed vtt's, and remove it. A file of the
     * final name is a vtt that had this pid and did not tidy up; rename
     * replaces it. */
    char tmp[CTL_PATH_MAX];
    if (snprintf(tmp, sizeof tmp, "%s/%ld.new", dir, (long)getpid()) >= (int)sizeof tmp) {
        snprintf(err, errsz, "the socket path is too long");
        close(fd);
        return -1;
    }
    str_lcpy(sa.sun_path, tmp, sizeof sa.sun_path);
    unlink(tmp);
    mode_t old = umask(077);
    int rc = bind(fd, (struct sockaddr *)&sa, sizeof sa);
    umask(old);
    if (rc < 0 || listen(fd, CTL_SLOTS) < 0 || rename(tmp, path) < 0) {
        snprintf(err, errsz, "cannot listen at %s: %s", path, strerror(errno));
        close(fd);
        unlink(tmp);
        return -1;
    }
    c->listen_fd = fd;
    str_lcpy(c->path, path, sizeof c->path);
    return 0;
}

static void conn_close(Ctl *c, int i)
{
    CtlConn *k = &c->c[i];
    if (k->fd >= 0) close(k->fd);
    free(k->in);
    free(k->out);
    /* Shift down, so the live ones are always [0, nc). */
    memmove(&c->c[i], &c->c[i + 1], (size_t)(c->nc - i - 1) * sizeof *c->c);
    c->nc--;
    memset(&c->c[c->nc], 0, sizeof *c->c);
    c->c[c->nc].fd = -1;
}

void ctl_stop(Ctl *c)
{
    while (c->nc) conn_close(c, c->nc - 1);
    if (c->listen_fd >= 0) {
        close(c->listen_fd);
        unlink(c->path);
    }
    c->listen_fd = -1;
    c->path[0] = '\0';
}

int ctl_pollfds(Ctl *c, struct pollfd *fds, int max)
{
    if (!ctl_active(c) || max < 1) return 0;
    int n = 0;
    fds[n].fd = c->listen_fd;
    fds[n].events = POLLIN;
    fds[n].revents = 0;
    n++;
    for (int i = 0; i < c->nc && n < max; i++) {
        const CtlConn *k = &c->c[i];
        fds[n].fd = k->fd;
        /* A READY connection is waiting on the app, not on its socket; a
         * WAITING one on an event, and only its hang-up (POLLHUP, which
         * poll reports unasked) is news: its caller shut its writing side
         * long ago, so POLLIN would be ready for ever. */
        fds[n].events = k->state == CTL_READING ? POLLIN : k->state == CTL_WRITING ? POLLOUT : 0;
        fds[n].revents = 0;
        n++;
    }
    return n;
}

void ctl_answer_text(Ctl *c, int i, const char *text)
{
    size_t n = strlen(text);
    char  *o = malloc(n);
    if (!o) { conn_close(c, i); return; }
    memcpy(o, text, n);
    ctl_answer(c, i, o, n);
}

/* Returns -1 when it closed connection i (the array shifted). */
static int conn_read(Ctl *c, int i)
{
    CtlConn *k = &c->c[i];
    for (;;) {
        if (k->in_len == CTL_REQ_CAP) {
            /* Full. Whatever else comes is read and thrown away until the
             * client shuts its side, then the answer says why: closing with
             * bytes unread would reset the connection, and the client would
             * lose the answer with it. The deadline still stands. */
            char sink[4096];
            ssize_t r = recv(k->fd, sink, sizeof sink, 0);
            if (r > 0) { k->over = 1; continue; }
            if (r < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return 0;
            if (r < 0 && errno == EINTR) continue;
            if (r < 0) { conn_close(c, i); return -1; }
            if (!k->over) break;               /* exactly the cap: a request */
            c->dropped++;
            ctl_answer_text(c, i, "error: the request is over 64 KB\n");
            return 0;
        }
        ssize_t r = recv(k->fd, k->in + k->in_len, CTL_REQ_CAP - k->in_len, 0);
        if (r > 0) { k->in_len += (size_t)r; continue; }
        if (r == 0) break;
        if (errno == EINTR) continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
        conn_close(c, i);
        return -1;
    }
    k->in[k->in_len] = '\0';
    k->state = CTL_READY;
    return 0;
}

static int conn_write(Ctl *c, int i)
{
    CtlConn *k = &c->c[i];
    while (k->out_off < k->out_len) {
        ssize_t w = send(k->fd, k->out + k->out_off, k->out_len - k->out_off, MSG_NOSIGNAL);
        if (w > 0) { k->out_off += (size_t)w; continue; }
        if (w < 0 && errno == EINTR) continue;
        if (w < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return 0;
        break;
    }
    conn_close(c, i);
    return -1;
}

void ctl_service(Ctl *c, const struct pollfd *fds, int count, uint64_t now_ms)
{
    if (!ctl_active(c) || count < 1) return;
    c->now_ms = now_ms;

    /* The connections first, by fd -- the array shifts as they close, so
     * poll's entries are matched by descriptor, not by position. */
    for (int f = 1; f < count; f++) {
        if (!fds[f].revents) continue;
        int i = 0;
        while (i < c->nc && c->c[i].fd != fds[f].fd) i++;
        if (i == c->nc) continue;
        CtlConn *k = &c->c[i];
        if (k->state == CTL_READING && (fds[f].revents & (POLLIN | POLLHUP | POLLERR))) {
            if (conn_read(c, i) < 0) continue;
        }
        else if (k->state == CTL_WRITING && (fds[f].revents & (POLLOUT | POLLHUP | POLLERR))) {
            if (conn_write(c, i) < 0) continue;
        }
        else if (k->state == CTL_WAITING && (fds[f].revents & (POLLHUP | POLLERR))) {
            conn_close(c, i);                  /* the waiter went away */
            continue;
        }
    }

    if (fds[0].revents & POLLIN) {
        for (;;) {
            int fd = accept(c->listen_fd, NULL, NULL);
            if (fd < 0) break;
            if (c->nc - ctl_waiters(c) == CTL_MAX_CONN || fd_nonblock_cloexec(fd) < 0) {
                close(fd);
                c->dropped++;
                continue;
            }
            CtlConn *k = &c->c[c->nc];
            memset(k, 0, sizeof *k);
            k->fd = fd;
            k->in = malloc(CTL_REQ_CAP + 1);
            if (!k->in) { close(fd); k->fd = -1; c->dropped++; continue; }
            k->since_ms = now_ms;
            c->nc++;
        }
    }

    /* Too slow to ask, or to take the answer. A READY one is the app's, and
     * a WAITING one has its own deadline, which the app keeps. */
    for (int i = c->nc - 1; i >= 0; i--) {
        if (c->c[i].state == CTL_READY || c->c[i].state == CTL_WAITING) continue;
        if (now_ms - c->c[i].since_ms >= CTL_TIMEOUT_MS) {
            c->dropped++;
            conn_close(c, i);
        }
    }
}

int ctl_due(const Ctl *c, uint64_t now_ms)
{
    int due = -1;
    for (int i = 0; i < c->nc; i++) {
        if (c->c[i].state == CTL_READY) return 0;
        uint64_t end = c->c[i].state == CTL_WAITING ? c->c[i].wait_until_ms
                                                    : c->c[i].since_ms + CTL_TIMEOUT_MS;
        int left = end > now_ms ? (int)(end - now_ms) : 0;
        if (due < 0 || left < due) due = left;
    }
    return due;
}

int ctl_next(Ctl *c, const char **req, size_t *len)
{
    for (int i = 0; i < c->nc; i++) {
        if (c->c[i].state != CTL_READY) continue;
        *req = c->c[i].in;
        *len = c->c[i].in_len;
        return i;
    }
    return -1;
}

void ctl_answer(Ctl *c, int i, char *out, size_t len)
{
    CtlConn *k = &c->c[i];
    free(k->out);
    k->out = out;
    k->out_len = len;
    k->out_off = 0;
    if (k->state == CTL_READY && k->in_len) c->requests++;    /* not an empty connection */
    /* A wait held for minutes is long past the deadline it connected
     * under: taking its answer gets ten seconds from now. */
    if (k->state == CTL_WAITING) k->since_ms = c->now_ms;
    k->state = CTL_WRITING;
    /* Most answers go at once; a big one finishes under poll. */
    (void)conn_write(c, i);
}

int ctl_waiters(const Ctl *c)
{
    int n = 0;
    for (int i = 0; i < c->nc; i++) n += c->c[i].state == CTL_WAITING;
    return n;
}

int ctl_hold(Ctl *c, int i, unsigned seq, uint64_t until_ms)
{
    if (ctl_waiters(c) == CTL_MAX_WAIT) return -1;
    CtlConn *k = &c->c[i];
    free(k->in);                       /* 64 KB nobody reads again, for up to ten minutes */
    k->in            = NULL;
    k->state         = CTL_WAITING;
    k->wait_seq      = seq;
    k->wait_until_ms = until_ms;
    c->requests++;
    return 0;
}

int ctl_waiter(const Ctl *c, int i, unsigned *seq, uint64_t *until_ms)
{
    if (i < 0 || i >= c->nc || c->c[i].state != CTL_WAITING) return 0;
    *seq      = c->c[i].wait_seq;
    *until_ms = c->c[i].wait_until_ms;
    return 1;
}

/* ---------------------------------------------------------------- client */

/* Connects to the socket at path: the fd, -1 when nobody is there (a
 * stale file is removed), -2 on any other failure. */
static int ctl_connect(const char *path)
{
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return -2;
    struct sockaddr_un sa;
    memset(&sa, 0, sizeof sa);
    sa.sun_family = AF_UNIX;
    str_lcpy(sa.sun_path, path, sizeof sa.sun_path);
    if (connect(fd, (struct sockaddr *)&sa, sizeof sa) == 0) return fd;
    int e = errno;
    close(fd);
    if (e == ECONNREFUSED) { unlink(path); return -1; }
    return e == ENOENT ? -1 : -2;
}

/* How long the server may hold this request, in ms: a `wait` names its own
 * time (`wait [SEQ] for SECONDS`, a minute unsaid, ten at most); anything
 * else is answered at once. */
int ctl_held_ms(const char *req)
{
    /* Past what the server skips: blank lines and comments. */
    for (;;) {
        while (*req == ' ' || *req == '\t' || *req == '\n' || *req == '\r') req++;
        if (*req != '#') break;
        req += strcspn(req, "\n");
    }
    if (strncmp(req, "wait", 4) != 0 || (req[4] && !strchr(" \t\r\n", req[4]))) return 0;
    size_t      line = strcspn(req, "\n");
    const char *f = strstr(req, " for ");
    long        s = f && (size_t)(f - req) < line ? strtol(f + 5, NULL, 10) : CTL_WAIT_DEFAULT_S;
    if (s < 0) s = 0;
    if (s > CTL_WAIT_MAX_S) s = CTL_WAIT_MAX_S;
    return (int)(s * 1000);
}

/* Sends req on fd and reads the whole answer, waiting `wait_ms` between
 * reads. NULL on failure. */
static char *exchange(int fd, const char *req, size_t len, size_t *out_len, int wait_ms)
{
    size_t off = 0;
    while (off < len) {
        ssize_t w = send(fd, req + off, len - off, MSG_NOSIGNAL);
        if (w > 0) { off += (size_t)w; continue; }
        if (w < 0 && errno == EINTR) continue;
        break;                     /* the server has stopped reading: its answer says why */
    }
    shutdown(fd, SHUT_WR);

    size_t cap = 4096, n = 0;
    char  *buf = malloc(cap);
    if (!buf) return NULL;
    for (;;) {
        struct pollfd p = { fd, POLLIN, 0 };
        int pr = poll(&p, 1, wait_ms);
        if (pr < 0 && errno == EINTR) continue;
        if (pr <= 0) { free(buf); return NULL; }
        if (n + 4096 > cap) {
            char *nb = realloc(buf, cap * 2);
            if (!nb) { free(buf); return NULL; }
            buf = nb;
            cap *= 2;
        }
        ssize_t r = read(fd, buf + n, cap - n - 1);
        if (r > 0) { n += (size_t)r; continue; }
        if (r == 0) break;
        if (errno == EINTR) continue;
        if (n > 0) break;          /* a reset after the answer: keep what came */
        free(buf);
        return NULL;
    }
    buf[n] = '\0';
    *out_len = n;
    return buf;
}

static char *read_all(FILE *f, size_t *len)
{
    size_t cap = 4096, n = 0;
    char  *buf = malloc(cap);
    if (!buf) return NULL;
    for (;;) {
        if (n + 1 >= cap) {
            if (cap > CTL_REQ_CAP * 2) break;       /* the server says no past 64 KB anyway */
            char *nb = realloc(buf, cap * 2);
            if (!nb) { free(buf); return NULL; }
            buf = nb;
            cap *= 2;
        }
        size_t r = fread(buf + n, 1, cap - n - 1, f);
        if (!r) break;
        n += r;
    }
    buf[n] = '\0';
    *len = n;
    return buf;
}

#define CTL_LIST_MAX 16

/* Every socket in the directory, by pid. Whether a vtt is behind each is
 * found by asking it something (`ask`): a probe first would wake every vtt
 * twice, and waking a sleeping process is the whole cost of asking. A stale
 * one is removed when the connect is refused. */
static int find_live(const char *dir, long *pids, int max)
{
    DIR *d = opendir(dir);
    if (!d) return 0;
    int n = 0;
    struct dirent *e;
    while ((e = readdir(d)) && n < max) {
        char *end;
        long pid = strtol(e->d_name, &end, 10);
        if (pid <= 0 || strcmp(end, ".sock") != 0) continue;
        pids[n++] = pid;
    }
    closedir(d);
    return n;
}

/* Finding which vtt to talk to asks each a question it answers between
 * keystrokes; one that does not in this long is stopped, or its GM is in
 * $EDITOR. The request itself keeps the longer wait. */
#define CTL_FIND_MS 2000

/* One request to the vtt of `pid`: the whole answer (malloc'd), or NULL with
 * *gone set when nobody is at the socket (a vtt that has quit) and clear
 * when one is there and did not answer -- which is not the same thing to
 * --apply: a silent vtt may have the map open. */
static char *ask(const char *dir, long pid, const char *req, size_t len, size_t *alen, int wait_ms, int *gone)
{
    char path[CTL_PATH_MAX];
    *gone = 1;
    if (sock_path(path, sizeof path, dir, pid) < 0) return NULL;
    int fd = ctl_connect(path);
    if (fd < 0) return NULL;
    *gone = 0;
    char *ans = exchange(fd, req, len, alen, wait_ms);
    close(fd);
    if (ans && !*alen) { free(ans); ans = NULL; }
    return ans;
}

int ctl_client_main(const char *req, long pid)
{
    char dir[CTL_PATH_MAX], err[CTL_PATH_MAX + 64];
    if (ctl_dir(dir, sizeof dir) < 0) { fputs("vtt: no socket directory\n", stderr); return 2; }
    if (access(dir, F_OK) == 0 && dir_ours(dir, err, sizeof err) < 0) {
        fprintf(stderr, "vtt: %s\n", err);
        return 2;
    }

    if (!pid) {
        /* Every interactive vtt listens (for --apply); --ctl talks to the
         * one whose GM typed :agent on. Each says which it is in status. */
        long pids[CTL_LIST_MAX], on[CTL_LIST_MAX];
        char lines[CTL_LIST_MAX][160];
        int  n = find_live(dir, pids, CTL_LIST_MAX), non = 0;
        for (int i = 0; i < n; i++) {
            size_t len;
            int    gone;
            char  *ans = ask(dir, pids[i], "status\n", 7, &len, CTL_FIND_MS, &gone);
            if (!ans && !gone) fprintf(stderr, "vtt: the vtt with pid %ld is not answering - passed over\n", pids[i]);
            if (ans && strstr(ans, "\nagent on\n")) {
                /* The line after "ok" is the map's. */
                const char *nl = strchr(ans, '\n');
                size_t k = strcspn(nl + 1, "\n");
                snprintf(lines[non], sizeof lines[non], "%.*s", (int)(k < 150 ? k : 150), nl + 1);
                on[non++] = pids[i];
            }
            free(ans);
        }
        if (non == 0) {
            fputs("vtt: no vtt is taking requests - type :agent on in the one you mean\n", stderr);
            return 2;
        }
        if (non > 1) {
            fputs("vtt: more than one vtt is taking requests; pick one with --ctl-pid:\n", stderr);
            for (int i = 0; i < non; i++) fprintf(stderr, "  --ctl-pid %-8ld %s\n", on[i], lines[i]);
            return 2;
        }
        pid = on[0];
    }

    char path[CTL_PATH_MAX];
    if (sock_path(path, sizeof path, dir, pid) < 0) { fputs("vtt: socket path too long\n", stderr); return 2; }
    int fd = ctl_connect(path);
    if (fd < 0) {
        fprintf(stderr, "vtt: no vtt with pid %ld is taking requests\n", pid);
        return 2;
    }

    size_t len;
    char  *mine = NULL;
    if (!req) {
        mine = read_all(stdin, &len);
        if (!mine) { close(fd); fputs("vtt: out of memory\n", stderr); return 2; }
        req = mine;
    }
    else len = strlen(req);

    size_t alen;
    char  *ans = exchange(fd, req, len, &alen, ctl_held_ms(req) + CTL_TIMEOUT_MS + 5000);
    close(fd);
    free(mine);
    if (!ans) { fputs("vtt: no answer from the vtt\n", stderr); return 2; }
    /* Closed with nothing said: :agent off, or the vtt quit, on a held wait. */
    if (!alen) { free(ans); fputs("vtt: the vtt closed the channel\n", stderr); return 2; }

    /* The first line is the verdict; the rest is the report. */
    size_t first = strcspn(ans, "\n");
    int    ok = first == 2 && !strncmp(ans, "ok", 2);
    if (!ok) fprintf(stderr, "vtt: %.*s\n", (int)first, ans);
    const char *body = ans[first] ? ans + first + 1 : ans + first;
    fwrite(body, 1, strlen(body), stdout);
    free(ans);
    return ok ? 0 : 1;
}

/* ---------------------------------------------------- --apply to an open map */

/* The lines of an events answer that are about job `num`: 0 accepted (the
 * line into *line), 4 scrapped or gone, 5 sent back with feedback (its text
 * into *line), -1 none of them yet. *seq is the answer's "seq N". */
int ctl_verdict_in(const char *ans, int num, unsigned *seq, char *line, size_t sz)
{
    char acc[32], scr[32], fb[32], rm[48];
    snprintf(acc, sizeof acc, " job %d accepted", num);
    snprintf(scr, sizeof scr, " job %d scrapped", num);
    snprintf(fb,  sizeof fb,  " job %d feedback: ", num);
    snprintf(rm,  sizeof rm,  " job %d removed by the GM", num);
    for (const char *p = ans; *p; ) {
        size_t      ll = strcspn(p, "\n");
        const char *sp = memchr(p, ' ', ll);
        if (!strncmp(p, "seq ", 4)) *seq = (unsigned)strtoul(p + 4, NULL, 10);
        else if (sp) {
            size_t rest = ll - (size_t)(sp - p);
            if (!strncmp(sp, acc, strlen(acc)))    { snprintf(line, sz, "%.*s", (int)rest - 1, sp + 1); return 0; }
            if (!strncmp(sp, fb, strlen(fb)))      { snprintf(line, sz, "%.*s", (int)(rest - strlen(fb)), sp + strlen(fb)); return 5; }
            if (!strncmp(sp, scr, strlen(scr)) || !strncmp(sp, rm, strlen(rm)) || !strncmp(sp, " map closed", 11)) {
                snprintf(line, sz, "%.*s", (int)rest - 1, sp + 1);
                return 4;
            }
        }
        p += ll + (p[ll] == '\n');
    }
    return -1;
}

int ctl_apply_open(const char *map_path, const char *plan_name, const char *plan, size_t len, int wait_s)
{
    char dir[CTL_PATH_MAX], err[CTL_PATH_MAX + 64];
    struct stat st;
    /* No file, or no directory of sockets: no vtt has it open. */
    if (stat(map_path, &st) < 0 || ctl_dir(dir, sizeof dir) < 0 || access(dir, F_OK) != 0) return -1;
    /* A directory that is not ours alone: whatever answers in it is not to
     * be believed, and silence there is not a "no". */
    if (dir_ours(dir, err, sizeof err) < 0) {
        fprintf(stderr, "vtt: %s - cannot tell whether a vtt has %s open; nothing done\n", err, map_path);
        return 2;
    }

    /* Which vtt has this file open, if any: by identity, since each was
     * started in a directory of its own. Every one is asked: a vtt that is
     * there and does not say no may hold it, and then the file is left
     * alone -- writing it under an open map is what this is here to stop. */
    char holds[96];
    int  hl = snprintf(holds, sizeof holds, "holds %llu %llu\n", (unsigned long long)st.st_dev, (unsigned long long)st.st_ino);
    long pids[CTL_LIST_MAX], pid = 0, silent = 0;
    int  n = find_live(dir, pids, CTL_LIST_MAX), others = 0;
    for (int i = 0; i < n; i++) {
        size_t al;
        int    gone;
        char  *ans = ask(dir, pids[i], holds, (size_t)hl, &al, CTL_FIND_MS, &gone);
        if (ans && !strcmp(ans, "ok\nholds\n")) { if (!pid) pid = pids[i]; else others++; }
        else if (!gone && !(ans && !strcmp(ans, "ok\nno\n"))) silent = pids[i];
        free(ans);
    }
    if (!pid && silent) {
        fprintf(stderr, "vtt: the vtt with pid %ld did not say whether it has %s open (stopped, or its GM is in an "
                        "editor) - nothing done; try again\n", silent, map_path);
        return 2;
    }
    if (!pid) return -1;
    if (others)
        fprintf(stderr, "vtt: %d more vtt%s %s open too - the proposal goes to pid %ld only\n", others,
                others == 1 ? " has" : "s have", map_path, pid);
    int gone;

    /* Where its events stand, before the proposal: the verdict comes after. */
    unsigned seq = 0;
    size_t   al;
    char    *ans = wait_s >= 0 ? ask(dir, pid, "wait for 0\n", 11, &al, CTL_FIND_MS, &gone) : NULL;
    if (ans && !strncmp(ans, "ok\nseq ", 7)) seq = (unsigned)strtoul(ans + 7, NULL, 10);
    free(ans);

    /* The plan, named: a proposal from --apply, never a write to the file. */
    size_t cap = len + strlen(plan_name) * 2 + 32, k = 0;
    char  *req = malloc(cap);
    if (!req) { fputs("vtt: out of memory\n", stderr); return 2; }
    k += (size_t)snprintf(req, cap, "propose apply \"");
    for (const char *p = plan_name; *p; p++) {
        if (*p == '"' || *p == '\\') req[k++] = '\\';
        req[k++] = *p == '\n' ? ' ' : *p;
    }
    req[k++] = '"';
    req[k++] = '\n';
    memcpy(req + k, plan, len);
    ans = ask(dir, pid, req, k + len, &al, CTL_TIMEOUT_MS + 5000, &gone);
    free(req);
    if (!ans) { fputs("vtt: no answer from the vtt that has the map open\n", stderr); return 2; }

    size_t first = strcspn(ans, "\n");
    int    ok = first == 2 && !strncmp(ans, "ok", 2);
    const char *body = ans[first] ? ans + first + 1 : ans + first;
    if (!ok) {
        fprintf(stderr, "vtt: %.*s - nothing proposed\n", (int)first, ans);
        free(ans);
        return 1;
    }
    fprintf(stderr, "vtt: %s is open in vtt %ld - the plan went there as a proposal; the file is untouched\n", map_path, pid);
    fputs(body, stdout);
    int num = 0;
    const char *hash = strstr(body, "proposal #");
    if (hash) num = atoi(hash + 10);
    free(ans);
    if (wait_s < 0 || !num) return 0;       /* not asked to wait, or nothing to wait for */

    /* --wait: the GM's verdict, from the vtt's events. */
    struct timespec t0;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (;;) {
        struct timespec t;
        clock_gettime(CLOCK_MONOTONIC, &t);
        long left = wait_s - (long)(t.tv_sec - t0.tv_sec);
        if (left < 0) left = 0;
        char wreq[64];
        int  wl = snprintf(wreq, sizeof wreq, "wait %u for %ld\n", seq, left > CTL_WAIT_MAX_S ? CTL_WAIT_MAX_S : left);
        ans = ask(dir, pid, wreq, (size_t)wl, &al, ctl_held_ms(wreq) + CTL_TIMEOUT_MS + 5000, &gone);
        if (!ans) { fputs("vtt: the vtt closed before a verdict\n", stderr); return 2; }
        char line[300] = "";
        unsigned was = seq;
        int v = ctl_verdict_in(ans, num, &seq, line, sizeof line);
        free(ans);
        if (seq < was) seq = 0;             /* a reset: hear everything kept */
        if (v == 0) { printf("%s, not saved\n", line); return 0; }
        if (v == 4) { printf("%s\n", line); return 4; }
        if (v == 5) { printf("%s\n", line); return 5; }
        if (left == 0) {
            printf("no verdict in %d seconds - proposal #%d still waits for the GM\n", wait_s, num);
            return 3;
        }
    }
}

long ctl_who_holds(const char *map_path)
{
    char dir[CTL_PATH_MAX], err[CTL_PATH_MAX + 64], holds[96];
    struct stat st;
    if (stat(map_path, &st) < 0 || ctl_dir(dir, sizeof dir) < 0 || access(dir, F_OK) != 0 ||
        dir_ours(dir, err, sizeof err) < 0)
        return 0;
    int  hl = snprintf(holds, sizeof holds, "holds %llu %llu\n", (unsigned long long)st.st_dev, (unsigned long long)st.st_ino);
    long pids[CTL_LIST_MAX], self = (long)getpid(), found = 0;
    int  n = find_live(dir, pids, CTL_LIST_MAX);
    for (int i = 0; i < n && !found; i++) {
        if (pids[i] == self) continue;          /* this vtt is not answering itself */
        size_t al;
        int    gone;
        char  *ans = ask(dir, pids[i], holds, (size_t)hl, &al, CTL_FIND_MS, &gone);
        if (ans && !strcmp(ans, "ok\nholds\n")) found = pids[i];
        free(ans);
    }
    return found;
}
