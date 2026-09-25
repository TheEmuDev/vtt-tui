#include "watch.h"

#include <errno.h>
#include <netdb.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "net.h"
#include "render.h"
#include "term.h"
#include "util.h"
#include "wire.h"

typedef struct {
    Renderer *r;
    Term     *t;
    Cell     *grid;              /* the GM's frame, gw x gh */
    int       gw, gh, cap;
    uint32_t  pal[WIRE_PAL_MAX];
    int       frames;
    int       ascii;
} Watch;

static void on_full(void *ctx, int w, int h)
{
    Watch *wt = ctx;
    if (w < 1 || h < 1 || w > 1024 || h > 1024) return;
    int need = w * h;
    if (need > wt->cap) {
        wt->grid = xrealloc(wt->grid, (size_t)need * sizeof(Cell));
        wt->cap  = need;
    }
    wt->gw = w;
    wt->gh = h;
    for (int i = 0; i < need; i++) wt->grid[i] = wt->r->clear_cell;
}

static void on_pal(void *ctx, int index, uint32_t rgb)
{
    ((Watch *)ctx)->pal[index] = rgb;
}

static void on_run(void *ctx, int x, int y, int n, uint8_t fg, uint8_t bg, uint8_t attr,
                   const uint16_t *glyphs)
{
    Watch *wt = ctx;
    if (y < 0 || y >= wt->gh) return;
    for (int i = 0; i < n; i++) {
        int cx = x + i;
        if (cx < 0 || cx >= wt->gw) continue;
        Cell *c = &wt->grid[y * wt->gw + cx];
        c->ch   = glyphs[i];
        c->fg   = wt->pal[fg];
        c->bg   = wt->pal[bg];
        c->attr = attr;
    }
}

/* The GM's frame onto this terminal: centred with room to spare, clipped
 * at the top left without. Every frame is a full repaint of the back
 * buffer, and the renderer's diff makes that cost only what changed. */
static void paint(Watch *wt)
{
    Renderer *r = wt->r;
    rnd_begin(r);
    if (wt->gw > 0) {
        int ox = r->w > wt->gw ? (r->w - wt->gw) / 2 : 0;
        int oy = r->h > wt->gh ? (r->h - wt->gh) / 2 : 0;
        int w  = imin(wt->gw, r->w - ox);
        int h  = imin(wt->gh, r->h - oy);
        for (int y = 0; y < h; y++)
            memcpy(&r->back[(size_t)(oy + y) * (size_t)r->w + (size_t)ox],
                   &wt->grid[(size_t)y * (size_t)wt->gw], (size_t)w * sizeof(Cell));
    }
    rnd_flush(r, wt->t);
}

static void on_end(void *ctx)
{
    Watch *wt = ctx;
    wt->frames++;
    paint(wt);
}

static void on_keepalive(void *ctx) { (void)ctx; }

static const WireSink SINK = { on_full, on_pal, on_run, on_end, on_keepalive };

/* host:port, host:port?k=CODE, or host:port/CODE. */
static int parse_target(const char *target, char *host, size_t hs, char *port, size_t ps,
                        char *code, size_t cs)
{
    const char *colon = strrchr(target, ':');
    if (!colon || colon == target) return -1;
    size_t hl = (size_t)(colon - target);
    if (hl + 1 > hs) return -1;
    memcpy(host, target, hl);
    host[hl] = '\0';

    const char *p = colon + 1;
    size_t pl = 0;
    while (p[pl] >= '0' && p[pl] <= '9' && pl + 1 < ps) { port[pl] = p[pl]; pl++; }
    port[pl] = '\0';
    if (pl == 0) return -1;

    code[0] = '\0';
    const char *k = strstr(p + pl, "k=");
    if (!k && p[pl] == '/') k = p + pl - 1;      /* /CODE: point at the slash */
    if (k) {
        k += 2;
        size_t cl = 0;
        while (k[cl] >= '0' && k[cl] <= '9' && cl + 1 < cs) { code[cl] = k[cl]; cl++; }
        code[cl] = '\0';
    }
    return 0;
}

int watch_main(const char *target, int ascii)
{
    char host[256], port[8], code[16];
    if (parse_target(target, host, sizeof host, port, sizeof port, code, sizeof code) < 0) {
        fprintf(stderr, "vtt: --watch wants host:port, as the GM's :serve shows it\n");
        return 2;
    }

    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof hints);
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    int rc = getaddrinfo(host, port, &hints, &res);
    if (rc != 0 || !res) {
        fprintf(stderr, "vtt: cannot resolve %s: %s\n", host, gai_strerror(rc));
        return 2;
    }
    int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd < 0 || connect(fd, res->ai_addr, res->ai_addrlen) < 0) {
        fprintf(stderr, "vtt: cannot reach %s:%s: %s\n", host, port, strerror(errno));
        freeaddrinfo(res);
        return 2;
    }
    freeaddrinfo(res);

    char hello[32];
    int  hl = snprintf(hello, sizeof hello, "VTT1%s\n", code);
    if (write(fd, hello, (size_t)hl) != hl) {
        fprintf(stderr, "vtt: the server hung up\n");
        close(fd);
        return 2;
    }

    Term t;
    if (term_init(&t) < 0) {
        fprintf(stderr, "vtt: --watch needs a terminal to mirror into\n");
        close(fd);
        return 2;
    }
    Renderer r;
    rnd_init(&r);
    rnd_resize(&r, t.w, t.h);

    Watch wt;
    memset(&wt, 0, sizeof wt);
    wt.r     = &r;
    wt.t     = &t;
    wt.ascii = ascii;

    WireDec dec;
    wire_dec_init(&dec, &SINK, &wt);

    const char *why = "the GM stopped serving";
    struct pollfd fds[3];
    fds[0].fd = fd;                 fds[0].events = POLLIN;
    fds[1].fd = t.in_fd;            fds[1].events = POLLIN;
    fds[2].fd = term_signal_fd(&t); fds[2].events = POLLIN;

    uint8_t buf[16384];
    for (;;) {
        int n = poll(fds, 3, -1);
        if (n < 0) { if (errno == EINTR) continue; why = strerror(errno); break; }

        if (fds[2].revents & POLLIN) {
            if (term_drain_signals(&t) && term_update_size(&t)) {
                rnd_resize(&r, t.w, t.h);
                paint(&wt);
            }
        }
        if (fds[1].revents & (POLLIN | POLLHUP)) {
            char k[16];
            ssize_t got = read(t.in_fd, k, sizeof k);
            int quit = 0;
            for (ssize_t i = 0; i < got; i++)
                if (k[i] == 'q' || k[i] == 'Q' || k[i] == 3 || k[i] == 27) quit = 1;
            if (quit) { why = NULL; break; }
        }
        if (fds[0].revents & (POLLIN | POLLHUP | POLLERR)) {
            ssize_t got = read(fd, buf, sizeof buf);
            if (got == 0) break;
            if (got < 0) { if (errno == EINTR || errno == EAGAIN) continue; why = strerror(errno); break; }
            wire_dec_feed(&dec, buf, (size_t)got);
            if (dec.bad) { why = "the stream was not a vtt stream"; break; }
        }
        if (t.dead) { why = "the terminal went away"; break; }
    }

    term_shutdown(&t);
    rnd_free(&r);
    free(wt.grid);
    close(fd);
    if (why) fprintf(stderr, "vtt: mirror closed: %s\n", why);
    return 0;
}
