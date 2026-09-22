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
#define NET_PING_MS     15000
#define NET_IDLE_MS     60000     /* a client silent this long is gone */

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
} NetClient;

typedef struct {
    int       listen_fd;
    uint16_t  port;
    char      code[NET_CODE_LEN + 1];

    NetClient cl[NET_MAX_CLIENTS];
    int       ncl;

    WireEnc         enc;
    const Renderer *rnd;
    int             live;       /* frames are being broadcast (play mode) */
    /* Set by :serve --stay-alive. The server belongs to the encounter and
     * goes down with the map unless this says otherwise; it never outlives
     * the process, which the operating system sees to. */
    int             stay;
    int             stale;      /* a frame was withheld: next live frame is FULL */

    /* Counters for the profiler: per frame, and over the server's life. */
    uint32_t frame_bytes;
    uint64_t total_bytes;
    uint32_t dropped;
} Net;

void net_init(Net *n);

/* Opens the listener on `port` (0 for any free one) and makes up a join
 * code. Returns 0, or -1 with the reason in err. */
int  net_start(Net *n, uint16_t port, const Renderer *r, char *err, size_t errsz);
void net_stop(Net *n);

static inline int net_active(const Net *n)  { return n->listen_fd >= 0; }
static inline int net_clients(const Net *n) { return n->ncl; }
static inline int net_stays(const Net *n)   { return n->stay; }
static inline void net_set_stay(Net *n, int stay) { n->stay = stay != 0; }

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

/* Exposed for the tests: the WebSocket accept key for a client key, and the
 * primitives behind it. */
void net_ws_accept(const char *key, char out[32]);
void net_sha1(const uint8_t *data, size_t len, uint8_t digest[20]);
size_t net_base64(const uint8_t *in, size_t n, char *out, size_t cap);

#endif /* VTT_NET_H */
