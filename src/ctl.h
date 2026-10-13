#ifndef VTT_CTL_H
#define VTT_CTL_H

#include <poll.h>
#include <stddef.h>
#include <stdint.h>

/* The control channel's socket: a Unix socket only this user can reach,
 * one request a connection. The client writes its request and shuts its
 * side; the request is handed to the app whole, the answer queued, written
 * without blocking, and the connection closed. What a request says is
 * request.c's business: the server side here only moves bytes. The clients
 * (`vtt --ctl`, `vtt MAP --apply`) know the few words they must: `agent
 * on` in status, `holds`, a proposal's number, a job's verdict in the
 * events. docs/CONTROL.md. */

#define CTL_MAX_CONN   4            /* requests being read or answered */
#define CTL_MAX_WAIT   8            /* and `wait`s held beside them, so a waiting
                                       agent never locks a request out */
#define CTL_SLOTS      (CTL_MAX_CONN + CTL_MAX_WAIT)
#define CTL_REQ_CAP    (64 * 1024)
/* An agent's name (docs/V2.md, D8): the request's first line may be `agent
 * NAME`, which `vtt --ctl` writes from $VTT_AGENT so an agent never types
 * it; with none the caller is `cli`. Letters, digits, - and _. */
#define CTL_NAME_MAX   32
#define CTL_NAME_NONE  "cli"
int  ctl_name_ok(const char *name);
/* A `dump` that names no region is taken up to this many squares (16 KB of
 * lattice; a map that full of creatures and notes was measured at 40 KB);
 * past it the caller says `dump all` or names a region. And what `changes`
 * prints of the map, by the bytes really written, before it lists the rest. */
#define CTL_DUMP_SQUARES  4096
/* The budget (docs/V2.md, D5). A turn of the loop runs requests until this
 * much time is spent, then goes on to the keys and the frame; what is ready
 * waits a turn. A request is never cut short, so one must be bounded by
 * itself, and by counts that do not depend on the machine as a time would:
 *  - the squares of map its reads walk (a region's for a dump of one, the
 *    map's for describe, check and a job read back, which is the map copied
 *    and then read: up to two walks for the one count). One whole read of
 *    the largest map, about 6 ms;
 *  - the squares its edits cover, whether or not they change them (a fill of
 *    what is already there records nothing, so the cap on recorded changes
 *    never sees it): the largest map twice, as that cap is;
 *  - work that is no square of map counts as squares read: a file opened
 *    for a `stamp` or a `from NAME`, a `stat` for `holds`, a line written to
 *    the session log by `job N say`, each CTL_FILE_CHARGE; a listing of the
 *    stamps or characters, which opens every one, a third of the request's
 *    reads; a `scene diff` its creatures against the map's. `changes`, which
 *    writes its regions aside, is asked three times a request at most. */
#define CTL_TURN_NS       2000000
#define CTL_READ_SQUARES  300000
#define CTL_EDIT_SQUARES  (2 * 512 * 512)
#define CTL_FILE_CHARGE   500
#define CTL_LIST_CHARGE   (CTL_READ_SQUARES / 3)
/* `job N conflicts` lists this many and counts the rest: an agent proposes
 * again from the first few, never from a thousand. */
#define CTL_CONFLICT_LINES 64
/* And by what it answers: a read that walks no map and opens no file can
 * still be written 12,800 times in 64 KB (`help` so was 43 ms and tens of
 * megabytes). Once the answer is past this, no further line is run. A
 * single line's answer is never refused for its size: a whole dump of the
 * largest map crowded with creatures was counted at 2.7 MB, and is what was
 * asked. */
#define CTL_ANSWER_CAP    (2 * 1024 * 1024)
#define CTL_CHANGES_BYTES (16 * 1024)
/* What `--apply` takes: the cap less the `propose apply "NAME"` line it
 * writes above a plan sent to an open map (a file's name twice over, each
 * byte escaped), so the same plans are taken on a file and on an open map. */
#define CTL_APPLY_HEAD 640
#define CTL_PLAN_CAP   (CTL_REQ_CAP - CTL_APPLY_HEAD)
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
    char      who[CTL_NAME_MAX];  /* whose wait it is: a newer one from the same name takes its place */
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

/* Milliseconds until the next deadline, or -1 with nothing open; 0 while a
 * request is ready, which is what brings the loop straight back for the
 * ones a turn's slice left waiting (app_tick). */
int  ctl_due(const Ctl *c, uint64_t now_ms);

/* The next request read to the end: its connection's index, the text in
 * *req (nul-terminated, length *len). -1 when none is waiting. */
int  ctl_next(Ctl *c, const char **req, size_t *len);

/* The answer to connection i, taken over (malloc'd; freed here). The
 * connection closes once it is written. */
void ctl_answer(Ctl *c, int i, char *out, size_t len);

/* Holds connection i's request (a `wait`) for `who` instead of answering
 * it: until ctl_answer, or the caller hanging up. 0, or -1 when CTL_MAX_WAIT
 * are held already (the caller answers it then). A held connection is
 * outside the ten-second deadline, and ctl_due counts only its own.
 * One wait a name: when `who` (not `cli`) holds one already, that one is
 * answered with `older` (its events and why, written by the app; copied)
 * and this one takes its place, whatever the count. ctl_waiter_of finds it:
 * its index and the number it waits from, or -1. */
int  ctl_hold(Ctl *c, int i, unsigned seq, uint64_t until_ms, const char *who, const char *older);
int  ctl_waiter_of(const Ctl *c, const char *who, unsigned *seq);
/* The line that names the caller, when a request has one where the server
 * looks for it (its first line that is not blank or a comment): where it
 * starts, or NULL. The one reading of that for the client and ctl_held_ms. */
const char *ctl_name_line(const char *req);

int  ctl_waiters(const Ctl *c);
/* 1 when connection i (0..nc-1) is a held wait, with what it waits for and
 * whose it is. */
int  ctl_waiter(const Ctl *c, int i, unsigned *seq, uint64_t *until_ms, const char **who);

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
/* A refusal's class: the word after `error`, one for each thing a caller
 * should do next (docs/V2.md, D6; docs/AGENTS.md, *Refusals*). USAGE is the
 * default: a site names its class only when it is another. */
enum { CTL_E_USAGE, CTL_E_MISSING, CTL_E_CONFLICT, CTL_E_STATE, CTL_E_LIMIT, CTL_E_INTERNAL, CTL_E_COUNT };
const char *ctl_class_name(int cls);

/* An event's kind: the word after its subject (`job N` or `map`), ended by
 * a colon or the line. Emitters (app_event) and --wait read the one table. */
enum {
    CTL_EV_ASKED, CTL_EV_ACCEPTED, CTL_EV_PART, CTL_EV_FEEDBACK, CTL_EV_SCRAPPED,
    CTL_EV_REMOVED, CTL_EV_BACK, CTL_EV_CHANGED, CTL_EV_CLOSED,
    /* What an agent did, told to the other names (docs/V2.md, D8). */
    CTL_EV_TAKEN, CTL_EV_PROPOSED, CTL_EV_UNDONE, CTL_EV_COUNT
};
const char *ctl_event_name(int kind);
/* One line of an events answer, `SEQ job N KIND[: text]` or `SEQ map
 * KIND[: text]`: the kind, with *num the job's number (0 for the map) and
 * *text the words after the colon ("" for none; it points into `line`).
 * -1 for a line that is not an event (`seq`, `lost`, `reset`). */
int  ctl_event_parse(const char *line, size_t len, int *num, const char **text, size_t *tlen);

/* --wait's reading of an events answer for job `num`:
 * 0 accepted, whole or in part, 4 scrapped, removed or the map closed, 5
 * sent back (the line, or the feedback, into `line`); -1 nothing of it
 * yet. *seq is the answer's "seq N". */
int  ctl_verdict_in(const char *ans, int num, unsigned *seq, char *line, size_t sz);

/* The pid of another running vtt that has this file open, or 0: asked before
 * a map is opened, so a GM is told it is open twice. *silent is a vtt that
 * was there and did not answer in two seconds (stopped, its GM in an
 * editor), or 0: it may have the map open, and the GM is told that too. */
long ctl_who_holds(const char *map_path, long *silent);

/* `vtt --ctl`: sends `req` (NULL reads stdin) to the vtt of `pid` (0: the
 * only one running), prints the answer. Returns the exit status. */
int  ctl_client_main(const char *req, long pid);

#endif /* VTT_CTL_H */
