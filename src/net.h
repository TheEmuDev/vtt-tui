#ifndef VTT_NET_H
#define VTT_NET_H

#include <poll.h>
#include <stddef.h>
#include <stdint.h>

#include "render.h"
#include "wire.h"

/* The remote view's server: one listening socket, up to NET_MAX_CLIENTS
 * clients, every one of them fed the renderer's diff as it is flushed. Two
 * kinds of client come through the same door, told apart by their first
 * bytes: a browser (GET, then the page or an upgrade to WebSocket) and a
 * watcher (`vtt --watch`, the magic "VTT1" on a raw socket). Both receive
 * identical wire frames; a WebSocket client gets them in binary messages.
 *
 * Memory is fixed at net_start: one send buffer per client, one request
 * buffer per client, one frame buffer. A client that cannot take a frame is
 * dropped, never waited for; it reconnects and is resynced with a FULL. */

#define NET_MAX_CLIENTS 8
#define NET_SEND_CAP    (64 * 1024)
#define NET_REQ_CAP     4096
#define NET_FRAME_CAP   (128 * 1024)
#define NET_CODE_LEN    6
/* A browser silent this long is sent a WebSocket ping, which browsers answer
 * by themselves, hidden tab or not; a watcher whose line has been quiet this
 * long is sent a 'Z' record. A browser silent for NET_IDLE_MS -- three
 * unanswered pings -- is gone, and so is a request or a watcher's hello not
 * finished by then; a greeted watcher never speaks, so only its socket
 * failing drops it. */
#define NET_KEEPALIVE_MS     15000
#define NET_IDLE_MS     60000
#define NET_PING_RATE_MS 1000     /* a phone's pings: one a second, the rest dropped */
#define NET_PING_COORD_MAX 4095   /* the largest screen cell a ping may name */
/* Names (docs/WHISPER.md): a phone says who it is as it connects -- `n=` on
 * /ws, or after the code in a raw hello -- and is that until it reconnects.
 * Self-declared: the join code is the only door. */
#define NET_NAME_MAX 25           /* 24 bytes of UTF-8 and the NUL */
#define NET_KEPT_MAX 16           /* whispers waiting for a phone, one a name */
#define NET_SEEN_MAX 16           /* names that have been here, for :whisper to a sleeper */

typedef enum {
    CL_NEW = 0,     /* connected; first bytes not yet seen */
    CL_HTTP,        /* reading an HTTP request */
    CL_WS,          /* a browser, upgraded */
    CL_RAW,         /* a watcher */
} ClientKind;

typedef struct {
    int        fd;
    ClientKind kind;
    int        local;         /* connected from this machine: no code asked */

    uint8_t   *out;           /* NET_SEND_CAP; bytes [off, len) are pending */
    size_t     out_len, out_off;

    uint8_t   *in;            /* NET_REQ_CAP; the request, or WebSocket frames */
    size_t     in_len;

    int        pal_known;     /* palette entries this client has been told */
    uint64_t   last_rx_ms, last_tx_ms;

    uint32_t   id;            /* this connection, for as long as it lasts */
    int        greeted;       /* a watcher's hello has been read */
    uint64_t   next_ping_ms;  /* the earliest its next ping is taken */
    uint64_t   probed_ms;     /* a browser: when we last asked if it was there */
    char       name[NET_NAME_MAX];   /* who it says it is; "" for none */
} NetClient;

/* A whisper for a name with no phone here: given to the first that comes. */
typedef struct {
    char   name[NET_NAME_MAX];
    char   text[WIRE_TEXT_MAX];
    size_t len;
} NetKept;

/* A ping: a client pointing at a cell of the frame it is shown. The app
 * decides what square that is; the server only reads, limits and hands
 * over. One per client at a time -- a newer one replaces it. */
typedef struct {
    uint32_t who;
    int      sx, sy;
} NetPing;

typedef struct {
    int       listen_fd;
    uint16_t  port;
    char      code[NET_CODE_LEN + 1];

    NetClient cl[NET_MAX_CLIENTS];
    int       ncl;

    WireEnc         enc;
    /* The players' frame. The app sends the clients this renderer's diff,
     * never the GM's terminal's: each frame it either draws the players'
     * view into it or, when the two views cannot differ, copies the GM's
     * back buffer across, so its front buffer is exactly what every client
     * is showing, which is what makes a FULL on resync right. Until
     * net_players_renderer is first asked for, `rnd` is the renderer given
     * to net_start, which is how the server is used bare in the tests. */
    Renderer        players;
    const Renderer *rnd;        /* the source of frames: start's, then &players */
    int             live;       /* frames are being broadcast (play mode) */
    /* Set by :serve --stay-alive. The server belongs to the encounter and
     * goes down with the map unless this says otherwise; it never outlives
     * the process, which the operating system sees to. */
    int             stay;
    int             stale;      /* a frame was withheld: next live frame is FULL */
    int             no_pings;   /* :serve --no-pings: taps are read and dropped */

    /* What the clients have said since the app last looked. Fixed: one
     * entry a client, so nothing a client sends can grow it. */
    NetPing         inbox[NET_MAX_CLIENTS];
    int             ninbox;
    uint32_t        next_id;
    int             joined;     /* a client was sent its first FULL: the app redraws */
    /* The handout up on the players' screens, as its record's text (title,
     * newline, body); length 0 for none. Sent when it changes and to every
     * client that joins while it is up. */
    char            handout[WIRE_HANDOUT_MAX];
    size_t          handout_len;
    /* Like the handout, these are the table's and outlive a restart: the
     * names a phone is offered ('N', newline between), the whispers
     * waiting, the names seen, and the arrivals the app has yet to say. */
    char            offer[WIRE_TEXT_MAX];
    size_t          offer_len;
    NetKept         kept[NET_KEPT_MAX];
    char            seen[NET_SEEN_MAX][NET_NAME_MAX];
    int             nseen;
    char            arrived[NET_MAX_CLIENTS][NET_NAME_MAX];
    int             narrived;

    /* Counters for the profiler: per frame, and over the server's life. */
    uint32_t frame_bytes;
    uint64_t total_bytes;
    uint32_t dropped;
    uint32_t idle_dropped;      /* browsers gone silent: asleep, or off the Wi-Fi */
    uint32_t pings_dropped;     /* over the rate, or switched off */
    uint32_t bad_msgs;          /* upstream messages that did not parse */
} Net;

void net_init(Net *n);

/* Opens the listener on `port` (0 for any free one) and makes up a join
 * code. Returns 0, or -1 with the reason in err. */
int  net_start(Net *n, uint16_t port, const Renderer *r, char *err, size_t errsz);
void net_stop(Net *n);

static inline int net_active(const Net *n)  { return n->listen_fd >= 0; }
static inline int net_clients(const Net *n) { return n->ncl; }
static inline int net_stays(const Net *n)   { return n->stay; }
static inline int net_is_live(const Net *n) { return n->live; }

/* The players' renderer, sized to match the GM's (a resize resyncs every
 * client with a FULL). NULL when not serving. */
Renderer *net_players_renderer(Net *n, const Renderer *gm);
static inline void net_set_stay(Net *n, int stay) { n->stay = stay != 0; }
static inline void net_set_pings(Net *n, int on)   { n->no_pings = !on; }
static inline int  net_pings_on(const Net *n)      { return !n->no_pings; }

/* Hands over the pings read since the last call, oldest client first, and
 * empties the inbox. Returns how many were written to out. */
int net_take_pings(Net *n, NetPing *out, int max);

/* The address to hand players: http://<lan ip>:<port>/?k=<code>. */
void net_url(const Net *n, char *buf, size_t bufsz);

/* Event loop integration. net_pollfds appends the listener and every client
 * to fds (up to max) and returns how many it added; after poll returns,
 * net_service handles what is ready in those same entries, plus timeouts. */
int  net_pollfds(Net *n, struct pollfd *fds, int max);
void net_service(Net *n, const struct pollfd *fds, int count, uint64_t now_ms);

/* Around each flush of the renderer: begin hooks the encoder up as the
 * renderer's observer; end broadcasts what the flush changed. With no
 * clients both are a compare and a return. Frames are broadcast only while
 * live; while not, clients keep their last frame and the next live frame
 * is sent in full. */
void net_frame_begin(Net *n);
void net_frame_end(Net *n, uint64_t now_ms);
void net_set_live(Net *n, int live);
/* Puts the handout up on every screen, or takes it down (n 0), and keeps it
 * for clients that join later. Sent at once, live or not: a handout is not a
 * frame and does not wait for play mode. */
void net_set_handout(Net *n, const char *text, size_t len, uint64_t now_ms);

/* The names a phone may choose from (player creatures' labels, a newline
 * between), sent to browsers when it changes and after each one's FULL. */
void net_set_offer(Net *n, const char *text, size_t len, uint64_t now_ms);

/* A whisper to every phone called `name` (case aside). Returns how many got
 * it; with none here and `keep`, it waits for the first to come (the last
 * one a name; the oldest name gives way when all are taken). */
int  net_whisper(Net *n, const char *name, const char *text, size_t len, int keep, uint64_t now_ms);

/* Drops every whisper waiting: they were the encounter's, as a handout is. */
void net_clear_kept(Net *n);

/* Has a phone called `name` been here, now or before (case aside)? */
int  net_name_seen(const Net *n, const char *name);

/* Hands over the next name to have arrived since the last call: 1, or 0
 * when there are none. */
int  net_take_arrival(Net *n, char *out, size_t sz);

/* Who is watching, for :players: "Aria, Brin (2), 1 unnamed, 1 terminal",
 * or "" for no one. */
void net_who(const Net *n, char *buf, size_t sz);

/* A name as a phone gave it, trimmed and checked: 1 with it in out, 0 for
 * one that is empty, too long, not UTF-8 or has a control character. `len`
 * bytes of `in`, %XX and + decoded first when `url` is set. */
int  net_name_clean(const char *in, size_t len, int url, char out[NET_NAME_MAX]);

/* Exposed for the tests: the WebSocket accept key for a client key, and the
 * primitives behind it. */
void net_ws_accept(const char *key, char out[32]);
void net_sha1(const uint8_t *data, size_t len, uint8_t digest[20]);
size_t net_base64(const uint8_t *in, size_t n, char *out, size_t cap);

#endif /* VTT_NET_H */
