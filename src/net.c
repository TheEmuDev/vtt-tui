#include "net.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <unistd.h>

#include "prof.h"
#include "util.h"

extern const char   WEBPAGE[];
extern const size_t WEBPAGE_LEN;

/* ------------------------------------------------------------- sha1/b64 */

/* SHA-1 as RFC 3174 spells it. It exists here for one purpose, the
 * WebSocket handshake, which the RFC pins to this hash. */
static void sha1_block(uint32_t h[5], const uint8_t block[64])
{
    uint32_t w[80];
    for (int i = 0; i < 16; i++)
        w[i] = ((uint32_t)block[4 * i] << 24) | ((uint32_t)block[4 * i + 1] << 16) |
               ((uint32_t)block[4 * i + 2] << 8) | block[4 * i + 3];
    for (int i = 16; i < 80; i++) {
        uint32_t v = w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16];
        w[i] = (v << 1) | (v >> 31);
    }
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
    for (int i = 0; i < 80; i++) {
        uint32_t f, k;
        if      (i < 20) { f = (b & c) | (~b & d);          k = 0x5A827999u; }
        else if (i < 40) { f = b ^ c ^ d;                   k = 0x6ED9EBA1u; }
        else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDCu; }
        else             { f = b ^ c ^ d;                   k = 0xCA62C1D6u; }
        uint32_t t = ((a << 5) | (a >> 27)) + f + e + k + w[i];
        e = d; d = c; c = (b << 30) | (b >> 2); b = a; a = t;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
}

void net_sha1(const uint8_t *data, size_t len, uint8_t digest[20])
{
    uint32_t h[5] = { 0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u };

    size_t i = 0;
    for (; i + 64 <= len; i += 64) sha1_block(h, data + i);

    /* The tail: what is left, the 0x80, zeros, and the length in bits --
     * in one block if that fits, two if it does not. */
    uint8_t  tail[128];
    size_t   rem = len - i;
    memcpy(tail, data + i, rem);
    tail[rem++] = 0x80;
    size_t total = rem <= 56 ? 64 : 128;
    memset(tail + rem, 0, total - rem);
    uint64_t bits = (uint64_t)len * 8;
    for (size_t k = 0; k < 8; k++) tail[total - 8 + k] = (uint8_t)(bits >> (56 - 8 * k));
    sha1_block(h, tail);
    if (total == 128) sha1_block(h, tail + 64);

    for (int k = 0; k < 5; k++) {
        digest[4 * k]     = (uint8_t)(h[k] >> 24);
        digest[4 * k + 1] = (uint8_t)(h[k] >> 16);
        digest[4 * k + 2] = (uint8_t)(h[k] >> 8);
        digest[4 * k + 3] = (uint8_t)h[k];
    }
}

size_t net_base64(const uint8_t *in, size_t n, char *out, size_t cap)
{
    static const char T[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t o = 0;
    for (size_t i = 0; i < n; i += 3) {
        if (o + 5 > cap) break;
        uint32_t v = (uint32_t)in[i] << 16;
        if (i + 1 < n) v |= (uint32_t)in[i + 1] << 8;
        if (i + 2 < n) v |= in[i + 2];
        out[o++] = T[(v >> 18) & 63];
        out[o++] = T[(v >> 12) & 63];
        out[o++] = i + 1 < n ? T[(v >> 6) & 63] : '=';
        out[o++] = i + 2 < n ? T[v & 63] : '=';
    }
    if (o < cap) out[o] = '\0';
    return o;
}

void net_ws_accept(const char *key, char out[32])
{
    static const char GUID[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    char    joined[128];
    size_t  n = (size_t)snprintf(joined, sizeof joined, "%.60s%s", key, GUID);
    uint8_t digest[20];
    net_sha1((const uint8_t *)joined, n, digest);
    net_base64(digest, 20, out, 32);
}

/* -------------------------------------------------------------- sockets */

static int set_nonblock(int fd)
{
    int fl = fcntl(fd, F_GETFL, 0);
    if (fl < 0) return -1;
    if (fcntl(fd, F_SETFL, fl | O_NONBLOCK) < 0) return -1;
    int fd_fl = fcntl(fd, F_GETFD, 0);
    if (fd_fl >= 0) (void)fcntl(fd, F_SETFD, fd_fl | FD_CLOEXEC);
    return 0;
}

void net_init(Net *n)
{
    memset(n, 0, sizeof *n);
    n->listen_fd = -1;
    for (int i = 0; i < NET_MAX_CLIENTS; i++) n->cl[i].fd = -1;
}

static void make_code(char *code)
{
    /* Six digits from the OS, or the clock if it has none: this is a join
     * code for a living room, not a secret. */
    uint32_t v = 0;
    FILE *f = fopen("/dev/urandom", "rb");
    if (f) { if (fread(&v, sizeof v, 1, f) != 1) v = 0; fclose(f); }
    if (!v) v = (uint32_t)prof_now_ns();
    snprintf(code, NET_CODE_LEN + 1, "%06u", v % 1000000u);
}

int net_start(Net *n, uint16_t port, const Renderer *r, char *err, size_t errsz)
{
    if (net_active(n)) net_stop(n);

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { snprintf(err, errsz, "socket: %s", strerror(errno)); return -1; }

    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port        = htons(port);
    if (bind(fd, (struct sockaddr *)&addr, sizeof addr) < 0 || listen(fd, 8) < 0 ||
        set_nonblock(fd) < 0) {
        snprintf(err, errsz, "port %u: %s", (unsigned)port, strerror(errno));
        close(fd);
        return -1;
    }
    socklen_t alen = sizeof addr;
    if (getsockname(fd, (struct sockaddr *)&addr, &alen) == 0) port = ntohs(addr.sin_port);

    n->listen_fd = fd;
    n->port      = port;
    rnd_init(&n->players);
    rnd_resize(&n->players, r->w, r->h);
    n->players.clear_cell = r->clear_cell;
    n->rnd       = r;
    n->live      = 1;
    n->stale     = 0;
    n->ncl       = 0;
    n->frame_bytes = 0;
    n->total_bytes = 0;
    n->dropped     = 0;
    make_code(n->code);
    wire_enc_init(&n->enc, NET_FRAME_CAP);

    /* All the memory the server will ever use, up front. */
    for (int i = 0; i < NET_MAX_CLIENTS; i++) {
        n->cl[i].fd  = -1;
        n->cl[i].out = xmalloc(NET_SEND_CAP);
        n->cl[i].in  = xmalloc(NET_REQ_CAP);
    }
    return 0;
}

static void client_close(Net *n, int i)
{
    NetClient *c = &n->cl[i];
    if (c->fd >= 0) close(c->fd);
    c->fd = -1;
    /* Keep the array dense so poll sees no holes. */
    uint8_t *out = c->out, *in = c->in;
    if (i < n->ncl - 1) memmove(c, c + 1, (size_t)(n->ncl - 1 - i) * sizeof *c);
    n->ncl--;
    NetClient *last = &n->cl[n->ncl];
    memset(last, 0, sizeof *last);
    last->fd  = -1;
    last->out = out;
    last->in  = in;

    if (n->ncl == 0) {
        rnd_set_observer((Renderer *)n->rnd, NULL, NULL);
        wire_enc_reset_palette(&n->enc);
    }
}

void net_stop(Net *n)
{
    if (!net_active(n)) return;
    while (n->ncl > 0) client_close(n, 0);
    for (int i = 0; i < NET_MAX_CLIENTS; i++) { free(n->cl[i].out); free(n->cl[i].in); }
    close(n->listen_fd);
    wire_enc_free(&n->enc);
    rnd_set_observer((Renderer *)n->rnd, NULL, NULL);
    rnd_free(&n->players);
    net_init(n);
}

Renderer *net_players_renderer(Net *n, const Renderer *gm)
{
    if (!net_active(n)) return NULL;
    if (n->rnd != &n->players) {
        /* Handing over from the renderer given at start: the clients hold
         * that one's picture, so the first frame from this one is a FULL. */
        rnd_set_observer((Renderer *)n->rnd, NULL, NULL);
        n->rnd   = &n->players;
        n->stale = 1;
    }
    if (n->players.w != gm->w || n->players.h != gm->h) {
        rnd_resize(&n->players, gm->w, gm->h);
        n->stale = 1;                     /* a new size wants a new FULL */
    }
    n->players.clear_cell = gm->clear_cell;
    return &n->players;
}

void net_url(const Net *n, char *buf, size_t bufsz)
{
    /* The address the LAN sees: connect a UDP socket somewhere (no packet
     * is sent) and read which interface the route would use. */
    char ip[64] = "127.0.0.1";
    int  fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd >= 0) {
        struct sockaddr_in to;
        memset(&to, 0, sizeof to);
        to.sin_family = AF_INET;
        to.sin_port   = htons(53);
        inet_pton(AF_INET, "8.8.8.8", &to.sin_addr);
        struct sockaddr_in me;
        socklen_t mlen = sizeof me;
        if (connect(fd, (struct sockaddr *)&to, sizeof to) == 0 &&
            getsockname(fd, (struct sockaddr *)&me, &mlen) == 0)
            inet_ntop(AF_INET, &me.sin_addr, ip, sizeof ip);
        close(fd);
    }
    snprintf(buf, bufsz, "http://%s:%u/?k=%s", ip, (unsigned)n->port, n->code);
}

/* ------------------------------------------------------------ sending */

/* Queues bytes for a client; a client that cannot hold them is gone. */
static int client_queue(Net *n, int i, const void *p, size_t len)
{
    NetClient *c = &n->cl[i];
    if (c->out_off > 0 && c->out_off == c->out_len) c->out_off = c->out_len = 0;
    if (c->out_len + len > NET_SEND_CAP && c->out_off > 0) {
        memmove(c->out, c->out + c->out_off, c->out_len - c->out_off);
        c->out_len -= c->out_off;
        c->out_off  = 0;
    }
    if (c->out_len + len > NET_SEND_CAP) {
        n->dropped++;
        client_close(n, i);
        return -1;
    }
    memcpy(c->out + c->out_len, p, len);
    c->out_len     += len;
    n->frame_bytes += (uint32_t)len;
    n->total_bytes += len;
    return 0;
}

/* Tries to write what is pending; what will not go now waits for POLLOUT. */
static void client_flush(Net *n, int i, uint64_t now_ms)
{
    NetClient *c = &n->cl[i];
    while (c->out_off < c->out_len) {
        ssize_t w = write(c->fd, c->out + c->out_off, c->out_len - c->out_off);
        if (w > 0) { c->out_off += (size_t)w; c->last_tx_ms = now_ms; continue; }
        if (w < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return;
        if (w < 0 && errno == EINTR) continue;
        client_close(n, i);
        return;
    }
    c->out_off = c->out_len = 0;
}

/* A wire frame, in the framing this client speaks. */
static int client_send_frame(Net *n, int i, const uint8_t *p, size_t len)
{
    NetClient *c = &n->cl[i];
    if (c->kind == CL_WS) {
        uint8_t hdr[10];
        size_t  hl = 2;
        hdr[0] = 0x82;                                 /* FIN, binary */
        if (len < 126)        { hdr[1] = (uint8_t)len; }
        else if (len < 65536) { hdr[1] = 126; hdr[2] = (uint8_t)(len >> 8); hdr[3] = (uint8_t)len; hl = 4; }
        else {
            hdr[1] = 127;
            for (int k = 0; k < 8; k++) hdr[2 + k] = (uint8_t)((uint64_t)len >> (56 - 8 * k));
            hl = 10;
        }
        if (client_queue(n, i, hdr, hl) < 0) return -1;
    }
    return client_queue(n, i, p, len);
}

/* Palette entries the client lacks, then the frame. */
static int client_send_synced(Net *n, int i, const uint8_t *frame, size_t len)
{
    NetClient *c = &n->cl[i];
    if (c->pal_known < n->enc.npal) {
        uint8_t pal[WIRE_PAL_MAX * 5];
        size_t  pn = wire_enc_palette(&n->enc, c->pal_known, pal, sizeof pal);
        c->pal_known = n->enc.npal;
        if (client_send_frame(n, i, pal, pn) < 0) return -1;
    }
    return client_send_frame(n, i, frame, len);
}

/* The whole screen as it stands, for a client that has just arrived. The
 * shared encoder builds it, which may grow the palette; everyone else is
 * caught up before their next frame by client_send_synced. */
static void client_send_full(Net *n, int i, uint64_t now_ms)
{
    wire_enc_full(&n->enc, n->rnd);
    if (n->enc.overflow) { client_close(n, i); return; }
    if (client_send_synced(n, i, n->enc.buf, n->enc.len) == 0) client_flush(n, i, now_ms);
}

/* -------------------------------------------------------------- reading */

static void http_respond(Net *n, int i, int status, const char *type, const char *body, size_t len,
                         uint64_t now_ms)
{
    char hdr[256];
    int  hl = snprintf(hdr, sizeof hdr,
                       "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\n"
                       "Cache-Control: no-store\r\nConnection: close\r\n\r\n",
                       status, status == 200 ? "OK" : status == 403 ? "Forbidden" : "Not Found",
                       type, len);
    if (client_queue(n, i, hdr, (size_t)hl) < 0) return;
    if (client_queue(n, i, body, len) < 0) return;
    client_flush(n, i, now_ms);
    client_close(n, i);
}

/* The join code in a request target's query, if any. */
static int query_code(const char *target, char *code)
{
    const char *k = strstr(target, "k=");
    if (!k || (k != target && k[-1] != '?' && k[-1] != '&')) return 0;
    k += 2;
    int j = 0;
    while (j < NET_CODE_LEN && k[j] >= '0' && k[j] <= '9') { code[j] = k[j]; j++; }
    code[j] = '\0';
    return j == NET_CODE_LEN;
}

static const char *header_value(const char *req, const char *name, char *out, size_t cap)
{
    const char *p = req;
    size_t nl = strlen(name);
    while ((p = strstr(p, "\r\n")) != NULL) {
        p += 2;
        if (strncasecmp(p, name, nl) == 0 && p[nl] == ':') {
            p += nl + 1;
            while (*p == ' ') p++;
            size_t j = 0;
            while (p[j] && p[j] != '\r' && j + 1 < cap) { out[j] = p[j]; j++; }
            out[j] = '\0';
            return out;
        }
    }
    return NULL;
}

static void http_request(Net *n, int i, uint64_t now_ms)
{
    NetClient *c = &n->cl[i];
    c->in[c->in_len] = '\0';

    char method[8] = { 0 }, target[512] = { 0 };
    if (sscanf((char *)c->in, "%7s %511s", method, target) != 2 || strcmp(method, "GET") != 0) {
        http_respond(n, i, 404, "text/plain", "vtt: not found\n", 15, now_ms);
        return;
    }

    char code[NET_CODE_LEN + 1] = "";
    int  has_code = query_code(target, code) && strcmp(code, n->code) == 0;
    if (!c->local && !has_code) {
        http_respond(n, i, 403, "text/plain", "vtt: this needs the join code from the GM's screen\n", 52, now_ms);
        return;
    }

    char upgrade[32], key[64];
    if (strncmp(target, "/ws", 3) == 0 &&
        header_value((char *)c->in, "Upgrade", upgrade, sizeof upgrade) &&
        strcasecmp(upgrade, "websocket") == 0 &&
        header_value((char *)c->in, "Sec-WebSocket-Key", key, sizeof key)) {
        char accept[32];
        net_ws_accept(key, accept);
        char hdr[256];
        int  hl = snprintf(hdr, sizeof hdr,
                           "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\n"
                           "Connection: Upgrade\r\nSec-WebSocket-Accept: %s\r\n\r\n", accept);
        if (client_queue(n, i, hdr, (size_t)hl) < 0) return;
        c->kind   = CL_WS;
        c->in_len = 0;
        client_send_full(n, i, now_ms);
        return;
    }

    if (strcmp(target, "/") == 0 || strncmp(target, "/?", 2) == 0) {
        http_respond(n, i, 200, "text/html; charset=utf-8", WEBPAGE, WEBPAGE_LEN, now_ms);
        return;
    }
    http_respond(n, i, 404, "text/plain", "vtt: not found\n", 15, now_ms);
}

/* One message from a client: "P <col> <row>", a line, the only thing a
 * client may say. Anything else is counted and ignored rather than closed
 * on, so a newer page does not lose its picture to an older server. */
static int parse_num(const uint8_t **p, const uint8_t *end, int *out)
{
    int v = 0, digits = 0;
    while (*p < end && **p >= '0' && **p <= '9') {
        if (++digits > 4) return 0;
        v = v * 10 + (**p - '0');
        (*p)++;
    }
    *out = v;
    return digits > 0 && v <= NET_PING_COORD_MAX;
}

static void net_msg(Net *n, int i, const uint8_t *p, size_t len, uint64_t now_ms)
{
    PROF_ZONE("net.cmd");
    const uint8_t *end = p + len;
    if (end > p && end[-1] == '\n') end--;
    if (end > p && end[-1] == '\r') end--;
    int sx, sy;
    if (end - p < 5 || p[0] != 'P' || p[1] != ' ') { n->bad_msgs++; return; }
    p += 2;
    if (!parse_num(&p, end, &sx) || p >= end || *p++ != ' ' || !parse_num(&p, end, &sy) || p != end) {
        n->bad_msgs++;
        return;
    }

    NetClient *c = &n->cl[i];
    if (n->no_pings || now_ms < c->next_ping_ms) { n->pings_dropped++; return; }
    c->next_ping_ms = now_ms + NET_PING_RATE_MS;

    int k = 0;
    while (k < n->ninbox && n->inbox[k].who != c->id) k++;
    if (k == n->ninbox) {
        if (n->ninbox == NET_MAX_CLIENTS) { n->pings_dropped++; return; }
        n->ninbox++;
    }
    n->inbox[k].who = c->id;
    n->inbox[k].sx  = sx;
    n->inbox[k].sy  = sy;
}

int net_take_pings(Net *n, NetPing *out, int max)
{
    int k = n->ninbox < max ? n->ninbox : max;
    memcpy(out, n->inbox, (size_t)k * sizeof *out);
    n->ninbox = 0;
    return k;
}

/* WebSocket frames from the browser: close and ping are answered, a data
 * frame is a message (net_msg). Masked, as the RFC requires of clients;
 * unmasked is tolerated. Anything our page never sends -- a fragment, a
 * reserved opcode, a control frame over 125 bytes, a frame bigger than the
 * request buffer -- closes the client. */
static void ws_frames(Net *n, int i, uint64_t now_ms)
{
    NetClient *c = &n->cl[i];
    for (;;) {
        if (c->in_len < 2) return;
        int      fin = c->in[0] & 0x80;
        uint8_t  op  = c->in[0] & 0x0F;
        int      msk = c->in[1] & 0x80;
        uint64_t len = c->in[1] & 0x7F;
        size_t   hl  = 2;
        if (len == 126) { if (c->in_len < 4) return; len = ((uint64_t)c->in[2] << 8) | c->in[3]; hl = 4; }
        else if (len == 127) { client_close(n, i); return; }    /* nothing that big is expected */
        if (msk) hl += 4;
        if (hl + len > NET_REQ_CAP) { client_close(n, i); return; }
        if (!fin || op == 0 || (op > 2 && op < 8) || op > 10) { client_close(n, i); return; }
        if (op >= 8 && len > 125) { client_close(n, i); return; }
        if (c->in_len < hl + len) return;

        /* Unmasked in place: the frame is consumed below either way. */
        uint8_t *body = c->in + hl;
        if (msk)
            for (size_t k = 0; k < len; k++) body[k] ^= c->in[hl - 4 + (k & 3)];

        if (op == 8) { client_close(n, i); return; }
        if (op == 9) {
            /* pong: the payload back, unmasked */
            uint8_t hdr[2] = { 0x8A, (uint8_t)len };
            if (client_queue(n, i, hdr, 2) < 0) return;
            if (len && client_queue(n, i, body, (size_t)len) < 0) return;
            client_flush(n, i, now_ms);
        } else if (op == 1 || op == 2) {
            net_msg(n, i, body, (size_t)len, now_ms);
        }
        size_t used = hl + (size_t)len;
        memmove(c->in, c->in + used, c->in_len - used);
        c->in_len -= used;
    }
}

static void client_read(Net *n, int i, uint64_t now_ms)
{
    NetClient *c = &n->cl[i];
    if (c->in_len >= NET_REQ_CAP - 1) { client_close(n, i); return; }

    ssize_t r = read(c->fd, c->in + c->in_len, NET_REQ_CAP - 1 - c->in_len);
    if (r == 0) { client_close(n, i); return; }
    if (r < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) return;
        client_close(n, i);
        return;
    }
    c->in_len    += (size_t)r;
    c->last_rx_ms = now_ms;

    if (c->kind == CL_NEW) {
        if (c->in_len < 4) return;
        if (memcmp(c->in, "VTT1", 4) == 0) c->kind = CL_RAW;
        else if (memcmp(c->in, "GET ", 4) == 0) c->kind = CL_HTTP;
        else { client_close(n, i); return; }
    }

    switch (c->kind) {
    case CL_RAW: {
        if (!c->greeted) {
            /* "VTT1" then the code and a newline; the code may be blank
             * from this machine. */
            if (c->in_len > 4 + NET_CODE_LEN + 1 + 64) { client_close(n, i); return; }
            uint8_t *nl = memchr(c->in + 4, '\n', c->in_len - 4);
            if (!nl) return;
            size_t rest = c->in_len - (size_t)(nl + 1 - c->in);
            *nl = '\0';
            c->in[c->in_len] = '\0';
            if (!c->local && strcmp((char *)c->in + 4, n->code) != 0) { client_close(n, i); return; }
            memmove(c->in, nl + 1, rest);
            c->in_len  = rest;
            c->greeted = 1;
            /* The FULL can overflow and close this client, which shifts the
             * next one into its slot: the id says whether it is still us. */
            uint32_t id = c->id;
            if (c->pal_known == 0 && c->out_len == 0) client_send_full(n, i, now_ms);
            if (i >= n->ncl || n->cl[i].id != id) return;
        }
        /* After the hello, lines: the same messages a browser sends. A
         * line too long to be one is thrown away rather than waited on. */
        for (;;) {
            uint8_t *nl = memchr(c->in, '\n', c->in_len);
            if (!nl) {
                if (c->in_len > 64) { n->bad_msgs++; c->in_len = 0; }
                break;
            }
            size_t used = (size_t)(nl + 1 - c->in);
            net_msg(n, i, c->in, used, now_ms);
            memmove(c->in, c->in + used, c->in_len - used);
            c->in_len -= used;
        }
        break;
    }
    case CL_HTTP:
        c->in[c->in_len] = '\0';
        if (strstr((char *)c->in, "\r\n\r\n")) http_request(n, i, now_ms);
        break;
    case CL_WS:
        ws_frames(n, i, now_ms);
        break;
    default:
        break;
    }
}

static void accept_client(Net *n, uint64_t now_ms)
{
    PROF_ZONE("net.accept");
    struct sockaddr_in addr;
    socklen_t alen = sizeof addr;
    int fd = accept(n->listen_fd, (struct sockaddr *)&addr, &alen);
    if (fd < 0) return;
    if (n->ncl >= NET_MAX_CLIENTS || set_nonblock(fd) < 0) { close(fd); return; }

    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);

    NetClient *c = &n->cl[n->ncl++];
    uint8_t *out = c->out, *in = c->in;
    memset(c, 0, sizeof *c);
    c->out   = out;
    c->in    = in;
    c->fd    = fd;
    c->kind  = CL_NEW;
    c->local = (ntohl(addr.sin_addr.s_addr) >> 24) == 127;
    c->last_rx_ms = c->last_tx_ms = now_ms;
    c->id    = ++n->next_id;
}

/* -------------------------------------------------------------- polling */

int net_pollfds(Net *n, struct pollfd *fds, int max)
{
    if (!net_active(n) || max < 1) return 0;
    int k = 0;
    fds[k].fd = n->listen_fd; fds[k].events = POLLIN; fds[k].revents = 0; k++;
    for (int i = 0; i < n->ncl && k < max; i++, k++) {
        fds[k].fd      = n->cl[i].fd;
        fds[k].events  = POLLIN | (n->cl[i].out_off < n->cl[i].out_len ? POLLOUT : 0);
        fds[k].revents = 0;
    }
    return k;
}

void net_service(Net *n, const struct pollfd *fds, int count, uint64_t now_ms)
{
    if (!net_active(n) || count < 1) return;
    if (fds[0].revents & POLLIN) accept_client(n, now_ms);

    /* Walk by descriptor rather than index: a close shifts the array. */
    for (int k = 1; k < count; k++) {
        int fd = fds[k].fd;
        int i  = -1;
        for (int j = 0; j < n->ncl; j++) if (n->cl[j].fd == fd) { i = j; break; }
        if (i < 0) continue;
        if (fds[k].revents & (POLLERR | POLLHUP | POLLNVAL)) { client_close(n, i); continue; }
        if (fds[k].revents & POLLOUT) client_flush(n, i, now_ms);
        if (i < n->ncl && n->cl[i].fd == fd && (fds[k].revents & POLLIN)) client_read(n, i, now_ms);
    }

    /* Keep-alives out, and the silent gone. Only stream clients get them;
     * an HTTP request still being read has its own short life. */
    for (int i = 0; i < n->ncl; i++) {
        NetClient *c = &n->cl[i];
        int stream = c->kind == CL_WS || c->kind == CL_RAW;
        if (stream && now_ms - c->last_tx_ms >= NET_KEEPALIVE_MS) {
            uint8_t z = 'Z';
            if (client_send_frame(n, i, &z, 1) == 0) { client_flush(n, i, now_ms); c->last_tx_ms = now_ms; }
            else continue;
        }
        if (i < n->ncl && n->cl[i].fd >= 0 && now_ms - n->cl[i].last_rx_ms >= NET_IDLE_MS &&
            n->cl[i].kind != CL_RAW)          /* a watcher never speaks; only its socket can die */
            client_close(n, i);
    }
}

/* --------------------------------------------------------------- frames */

void net_set_live(Net *n, int live)
{
    if (!net_active(n)) return;
    if (!n->live && live) n->stale = 1;
    n->live = live;
}

void net_frame_begin(Net *n)
{
    if (!net_active(n) || n->ncl == 0) return;
    n->frame_bytes = 0;
    wire_enc_begin(&n->enc);
    rnd_set_observer((Renderer *)n->rnd, (RndObserver)wire_enc_cell, &n->enc);
}

void net_frame_end(Net *n, uint64_t now_ms)
{
    if (!net_active(n) || n->ncl == 0) return;
    PROF_ZONE("net.frame");
    rnd_set_observer((Renderer *)n->rnd, NULL, NULL);
    wire_enc_end(&n->enc);

    if (!n->live) { n->stale = 1; return; }

    int full = n->stale || n->enc.overflow;
    if (!full && n->enc.cells == 0) return;           /* nothing changed: nothing sent */
    if (full) { wire_enc_full(&n->enc, n->rnd); n->stale = 0; }
    if (n->enc.overflow) return;

    /* Only stream clients get frames; one still shaking hands gets a FULL
     * when it finishes. */
    for (int i = 0; i < n->ncl; i++) {
        NetClient *c = &n->cl[i];
        if (c->kind != CL_WS && c->kind != CL_RAW) continue;
        if (client_send_synced(n, i, n->enc.buf, n->enc.len) < 0) { i--; continue; }
        client_flush(n, i, now_ms);
        if (i < n->ncl && n->cl[i].fd < 0) i--;
    }
}
