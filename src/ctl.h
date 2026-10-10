#ifndef VTT_CTL_H
#define VTT_CTL_H

#include <poll.h>
#include <stddef.h>
#include <stdint.h>

/* The control channel's socket: a Unix socket only this user can reach,
 * one request a connection. The client writes its request and shuts its
 * side; the request is handed to the app whole, the answer queued, written
 * without blocking, and the connection closed. What a request says is
 * app_ctl.c's business; this file only moves bytes. docs/CONTROL.md. */

#define CTL_MAX_CONN   4            /* requests being read or answered */
#define CTL_MAX_WAIT   4            /* and `wait`s held beside them, so a waiting
                                       agent never locks a request out */
#define CTL_SLOTS      (CTL_MAX_CONN + CTL_MAX_WAIT)
#define CTL_REQ_CAP    (64 * 1024)
#define CTL_TIMEOUT_MS 10000
#define CTL_PATH_MAX   108          /* sun_path */
#define CTL_WAIT_DEFAULT_S 60       /* `wait` with no time named */
#define CTL_WAIT_MAX_S     600

typedef enum {
    CTL_READING = 0,    /* the request is still coming */
    CTL_READY,          /* read to the end, waiting for the app */
    CTL_WRITING,        /* the answer is going out */
    CTL_WAITING,        /* a `wait` held until an event or its own deadline */
} CtlState;

typedef struct {
    int       fd;
    CtlState  state;
    char     *in;               /* CTL_REQ_CAP + 1, nul-terminated when READY */
    size_t    in_len;
    int       over;             /* more than CTL_REQ_CAP came: being drained */
    char     *out;              /* the answer, owned; sent from out_off */
    size_t    out_len, out_off;
    uint64_t  since_ms;         /* connected; the deadline runs from here */
    unsigned  wait_seq;         /* WAITING: events after this one answer it, */
    uint64_t  wait_until_ms;    /* or this moment does */
} CtlConn;

typedef struct {
    int      listen_fd;
    char     path[CTL_PATH_MAX];
    CtlConn  c[CTL_SLOTS];
    int      nc;
    uint64_t now_ms;            /* the last ctl_service's clock */
    uint32_t requests;          /* answered, over the channel's life */
    uint32_t dropped;           /* too big, too slow, or refused for want of a slot */
} Ctl;

void ctl_init(Ctl *c);

/* Makes the directory if it has to, refuses one that is not ours alone,
 * and listens at <dir>/<pid>.sock. Returns 0, or -1 with why in err. */
int  ctl_start(Ctl *c, char *err, size_t errsz);
/* Closes every connection and removes the socket file. */
void ctl_stop(Ctl *c);

static inline int ctl_active(const Ctl *c) { return c->listen_fd >= 0; }

/* The directory the sockets live in; 0, or -1 when there is none to use. */
int  ctl_dir(char *buf, size_t sz);

/* Event loop integration, as the remote view's: append the listener and
 * the connections, then service what poll said is ready, plus deadlines. */
int  ctl_pollfds(Ctl *c, struct pollfd *fds, int max);
void ctl_service(Ctl *c, const struct pollfd *fds, int count, uint64_t now_ms);

/* Milliseconds until the next deadline, or -1 with nothing open. */
int  ctl_due(const Ctl *c, uint64_t now_ms);

/* The next request read to the end: its connection's index, the text in
 * *req (nul-terminated, length *len). -1 when none is waiting. */
int  ctl_next(Ctl *c, const char **req, size_t *len);

/* The answer to connection i, taken over (malloc'd; freed here). The
 * connection closes once it is written. */
void ctl_answer(Ctl *c, int i, char *out, size_t len);

/* Holds connection i's request (a `wait`) instead of answering it: until
 * ctl_answer, or the caller hanging up. 0, or -1 when CTL_MAX_WAIT are held
 * already (the caller answers it then). A held connection is outside the
 * ten-second deadline, and ctl_due counts only its own. */
int  ctl_hold(Ctl *c, int i, unsigned seq, uint64_t until_ms);
int  ctl_waiters(const Ctl *c);
/* 1 when connection i (0..nc-1) is a held wait, with what it waits for. */
int  ctl_waiter(const Ctl *c, int i, unsigned *seq, uint64_t *until_ms);

/* A line of text as connection i's answer (copied). */
void ctl_answer_text(Ctl *c, int i, const char *text);

/* How long the server may hold this request, in ms: a `wait` names its own
 * time; anything else is answered at once (0). The client waits that much
 * longer for its answer. */
int  ctl_held_ms(const char *req);

/* `vtt MAP --apply PLAN` when a running vtt has MAP open: the plan goes
 * there as a proposal and the file is not touched (docs/CONFLICTS.md, step
 * 6). -1 when no vtt holds the file (the caller applies it to the file);
 * else the exit status: 0, 1 the plan failed, 2 no answer. With wait_s >= 0
 * it waits that long for the GM's verdict: 0 accepted, 3 none in time, 4
 * scrapped, 5 sent back (the feedback on stdout). */
int  ctl_apply_open(const char *map_path, const char *plan_name, const char *plan, size_t len, int wait_s);

/* `vtt --ctl`: sends `req` (NULL reads stdin) to the vtt of `pid` (0: the
 * only one running), prints the answer. Returns the exit status. */
int  ctl_client_main(const char *req, long pid);

#endif /* VTT_CTL_H */
