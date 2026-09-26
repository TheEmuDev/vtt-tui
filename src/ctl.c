#include "ctl.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
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
 * that nobody else can list or enter. Made if it is not there. */
static int dir_ready(const char *dir, char *err, size_t errsz)
{
    if (mkdir(dir, 0700) < 0 && errno != EEXIST) {
        snprintf(err, errsz, "cannot make %s: %s", dir, strerror(errno));
        return -1;
    }
    struct stat st;
    if (lstat(dir, &st) < 0) {
        snprintf(err, errsz, "cannot read %s: %s", dir, strerror(errno));
        return -1;
    }
    if (!S_ISDIR(st.st_mode) || st.st_uid != getuid() || (st.st_mode & 077)) {
        snprintf(err, errsz, "%s is not this user's alone - not listening there", dir);
        return -1;
    }
    return 0;
}

static int sock_path(char *buf, size_t sz, const char *dir, long pid)
{
    int n = snprintf(buf, sz, "%s/%ld.sock", dir, pid);
    return n < 0 || (size_t)n >= sz ? -1 : 0;
}

static int set_nonblock(int fd)
{
    int fl = fcntl(fd, F_GETFL, 0);
    if (fl < 0 || fcntl(fd, F_SETFL, fl | O_NONBLOCK) < 0) return -1;
    int fd_fl = fcntl(fd, F_GETFD, 0);
    if (fd_fl >= 0) (void)fcntl(fd, F_SETFD, fd_fl | FD_CLOEXEC);
    return 0;
}

/* ---------------------------------------------------------------- server */

void ctl_init(Ctl *c)
{
    memset(c, 0, sizeof *c);
    c->listen_fd = -1;
    for (int i = 0; i < CTL_MAX_CONN; i++) c->c[i].fd = -1;
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
    if (set_nonblock(fd) < 0) {
        snprintf(err, errsz, "socket: %s", strerror(errno));
        close(fd);
        return -1;
    }

    struct sockaddr_un sa;
    memset(&sa, 0, sizeof sa);
    sa.sun_family = AF_UNIX;
    str_lcpy(sa.sun_path, path, sizeof sa.sun_path);
    /* A file of this name is a vtt that had this pid and did not tidy up. */
    unlink(path);
    mode_t old = umask(077);
    int rc = bind(fd, (struct sockaddr *)&sa, sizeof sa);
    umask(old);
    if (rc < 0 || listen(fd, CTL_MAX_CONN) < 0) {
        snprintf(err, errsz, "cannot listen at %s: %s", path, strerror(errno));
        close(fd);
        unlink(path);
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
        /* A READY connection is waiting on the app, not on its socket. */
        fds[n].events = k->state == CTL_READING ? POLLIN : k->state == CTL_WRITING ? POLLOUT : 0;
        fds[n].revents = 0;
        n++;
    }
    return n;
}

static void answer_text(Ctl *c, int i, const char *text)
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
            /* Full, and still more coming: say why and stop listening. */
            char probe;
            ssize_t r = recv(k->fd, &probe, 1, 0);
            if (r == 0) break;
            if (r < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return 0;
            if (r < 0 && errno == EINTR) continue;
            if (r < 0) { conn_close(c, i); return -1; }
            c->dropped++;
            answer_text(c, i, "error: the request is over 64 KB\n");
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
    }

    if (fds[0].revents & POLLIN) {
        for (;;) {
            int fd = accept(c->listen_fd, NULL, NULL);
            if (fd < 0) break;
            if (c->nc == CTL_MAX_CONN || set_nonblock(fd) < 0) {
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

    /* Too slow to ask, or to take the answer. A READY one is the app's. */
    for (int i = c->nc - 1; i >= 0; i--) {
        if (c->c[i].state == CTL_READY) continue;
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
        uint64_t end = c->c[i].since_ms + CTL_TIMEOUT_MS;
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
    if (k->state == CTL_READY) c->requests++;
    k->state = CTL_WRITING;
    /* Most answers go at once; a big one finishes under poll. */
    (void)conn_write(c, i);
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

/* Sends req on fd and reads the whole answer. NULL on failure. */
static char *exchange(int fd, const char *req, size_t len, size_t *out_len)
{
    size_t off = 0;
    while (off < len) {
        ssize_t w = send(fd, req + off, len - off, MSG_NOSIGNAL);
        if (w > 0) { off += (size_t)w; continue; }
        if (w < 0 && errno == EINTR) continue;
        return NULL;
    }
    shutdown(fd, SHUT_WR);

    size_t cap = 4096, n = 0;
    char  *buf = malloc(cap);
    if (!buf) return NULL;
    for (;;) {
        struct pollfd p = { fd, POLLIN, 0 };
        int pr = poll(&p, 1, CTL_TIMEOUT_MS + 5000);
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

/* Every vtt taking requests, by pid. Stale sockets are removed on the way. */
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
        char path[CTL_PATH_MAX];
        if (sock_path(path, sizeof path, dir, pid) < 0) continue;
        int fd = ctl_connect(path);
        if (fd < 0) continue;
        close(fd);
        pids[n++] = pid;
    }
    closedir(d);
    return n;
}

int ctl_client_main(const char *req, long pid)
{
    char dir[CTL_PATH_MAX];
    if (ctl_dir(dir, sizeof dir) < 0) { fputs("vtt: no socket directory\n", stderr); return 2; }

    if (!pid) {
        long pids[CTL_LIST_MAX];
        int  n = find_live(dir, pids, CTL_LIST_MAX);
        if (n == 0) {
            fputs("vtt: no vtt is taking requests - type :agent on in the one you mean\n", stderr);
            return 2;
        }
        if (n > 1) {
            fputs("vtt: more than one vtt is taking requests; pick one with --ctl-pid:\n", stderr);
            for (int i = 0; i < n; i++) {
                char path[CTL_PATH_MAX], line[160] = "";
                if (sock_path(path, sizeof path, dir, pids[i]) < 0) continue;
                int fd = ctl_connect(path);
                if (fd >= 0) {
                    size_t len;
                    char  *ans = exchange(fd, "status\n", 7, &len);
                    close(fd);
                    /* The line after "ok" is the map's. */
                    const char *nl = ans ? strchr(ans, '\n') : NULL;
                    if (nl) {
                        size_t k = strcspn(nl + 1, "\n");
                        snprintf(line, sizeof line, "%.*s", (int)(k < 150 ? k : 150), nl + 1);
                    }
                    free(ans);
                }
                fprintf(stderr, "  --ctl-pid %-8ld %s\n", pids[i], line);
            }
            return 2;
        }
        pid = pids[0];
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
    char  *ans = exchange(fd, req, len, &alen);
    close(fd);
    free(mine);
    if (!ans) { fputs("vtt: no answer from the vtt\n", stderr); return 2; }

    /* The first line is the verdict; the rest is the report. */
    size_t first = strcspn(ans, "\n");
    int    ok = first == 2 && !strncmp(ans, "ok", 2);
    if (!ok) fprintf(stderr, "vtt: %.*s\n", (int)first, ans);
    const char *body = ans[first] ? ans + first + 1 : ans + first;
    fwrite(body, 1, strlen(body), stdout);
    free(ans);
    return ok ? 0 : 1;
}
