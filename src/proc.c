#include "proc.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

extern char **environ;

static long now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void close_pair(int p[2])
{
    if (p[0] >= 0) close(p[0]);
    if (p[1] >= 0) close(p[1]);
    p[0] = p[1] = -1;
}

int proc_run(char *const argv[], const ProcIn *pi, ByteBuf *out, int *status, char *err, size_t errsz)
{
    int in[2] = { -1, -1 }, o[2] = { -1, -1 }, f3[2] = { -1, -1 };
    *status = -1;
    if ((pi->in && pipe(in) < 0) || pipe(o) < 0 || (pi->fd3 && pipe(f3) < 0)) {
        snprintf(err, errsz, "no pipe: %s", strerror(errno));
        close_pair(in); close_pair(o); close_pair(f3);
        return -1;
    }
    pid_t pid = fork();
    if (pid < 0) {
        snprintf(err, errsz, "cannot start %s: %s", argv[0], strerror(errno));
        close_pair(in); close_pair(o); close_pair(f3);
        return -1;
    }
    if (pid == 0) {
        int null = open("/dev/null", O_RDWR);
        dup2(pi->in ? in[0] : null, 0);
        dup2(o[1], 1);
        if (pi->quiet) dup2(null, 2);
        if (pi->fd3) dup2(f3[0], 3);
        /* Nothing else of ours goes with it: the app's sockets and the
         * terminal are not the program's business. */
        for (int fd = pi->fd3 ? 4 : 3; fd < 1024; fd++) close(fd);
        signal(SIGPIPE, SIG_DFL);
        if (pi->env) {
            /* An environment of its own: only the names kept, as they are
             * here (curl: no SSLKEYLOGFILE, nothing a config could steer). */
            static char *keep[64];
            int n = 0;
            for (int i = 0; pi->env[i] && n < 63; i++) {
                const char *v = getenv(pi->env[i]);
                if (!v) continue;
                size_t len = strlen(pi->env[i]) + strlen(v) + 2;
                char *kv = malloc(len);
                if (!kv) continue;
                snprintf(kv, len, "%s=%s", pi->env[i], v);
                keep[n++] = kv;
            }
            keep[n] = NULL;
            environ = keep;
        }
        execvp(argv[0], argv);
        _exit(127);
    }
    if (in[0] >= 0) close(in[0]);
    close(o[1]);
    if (f3[0] >= 0) close(f3[0]);
    o[1] = -1;

    /* fd 3 carries a few lines (the key's): a pipe holds them whole. */
    if (pi->fd3) {
        void (*was)(int) = signal(SIGPIPE, SIG_IGN);
        for (size_t off = 0; off < pi->fd3len; ) {
            ssize_t w = write(f3[1], pi->fd3 + off, pi->fd3len - off);
            if (w > 0) off += (size_t)w;
            else if (w < 0 && errno == EINTR) continue;
            else break;
        }
        signal(SIGPIPE, was);
        close(f3[1]);
    }

    /* stdin written and stdout read together: a program may answer before
     * it has read all it is sent, and either pipe can fill. */
    void (*was)(int) = signal(SIGPIPE, SIG_IGN);
    int    wfd = in[1];
    size_t woff = 0;
    if (wfd >= 0) fcntl(wfd, F_SETFL, fcntl(wfd, F_GETFL) | O_NONBLOCK);
    if (wfd >= 0 && pi->inlen == 0) { close(wfd); wfd = -1; }
    long until = pi->timeout_ms ? now_ms() + pi->timeout_ms : 0;
    int  timed_out = 0;
    char buf[16384];
    for (;;) {
        struct pollfd p[2];
        int np = 0;
        p[np].fd = o[0]; p[np].events = POLLIN; np++;
        if (wfd >= 0) { p[np].fd = wfd; p[np].events = POLLOUT; np++; }
        int wait = -1;
        if (until) { long left = until - now_ms(); if (left <= 0) { timed_out = 1; break; } wait = (int)left; }
        int r = poll(p, (nfds_t)np, wait);
        if (r < 0) { if (errno == EINTR) continue; break; }
        if (r == 0) { timed_out = 1; break; }
        if (np > 1 && p[1].revents) {
            ssize_t w = write(wfd, pi->in + woff, pi->inlen - woff);
            if (w > 0) woff += (size_t)w;
            if ((w < 0 && errno != EAGAIN && errno != EINTR) || woff == pi->inlen) { close(wfd); wfd = -1; }
        }
        if (p[0].revents) {
            ssize_t n = read(o[0], buf, sizeof buf);
            if (n > 0) { bb_put(out, buf, (size_t)n); continue; }
            if (n < 0 && (errno == EINTR || errno == EAGAIN)) continue;
            break;                                  /* its end of the pipe closed */
        }
    }
    signal(SIGPIPE, was);
    if (wfd >= 0) close(wfd);
    close(o[0]);
    if (timed_out) kill(pid, SIGKILL);
    int st = 0;
    while (waitpid(pid, &st, 0) < 0 && errno == EINTR) {}
    *status = timed_out ? -1 : WIFEXITED(st) ? WEXITSTATUS(st) : -1;
    return 0;
}

int proc_found(const char *name)
{
    if (strchr(name, '/')) return access(name, X_OK) == 0;
    const char *path = getenv("PATH");
    if (!path || !*path) path = "/usr/bin:/bin";
    for (const char *p = path; ; ) {
        size_t n = strcspn(p, ":");
        if (n > 0 && n < 4096) {
            char full[8192];
            snprintf(full, sizeof full, "%.*s/%s", (int)n, p, name);
            struct stat st;
            if (stat(full, &st) == 0 && S_ISREG(st.st_mode) && access(full, X_OK) == 0) return 1;
        }
        if (!p[n]) break;
        p += n + 1;
    }
    return 0;
}
