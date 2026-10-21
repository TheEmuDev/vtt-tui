/* Tests: a phone that stops reading (docs/V2.md, step 7's plan, items 1 and 3; review 2's
 * B8). The phones' sockets go through a fake (conn.h): what vtt writes to a phone lands in a
 * store of the test's, as much as the kernel would take -- the send buffer vtt asked for, or
 * this machine's ceiling when it asked for none -- and no more until the phone reads. So
 * "how far behind is it" is exact and quick, where a real socket fills megabytes first. */

#include "harness.h"
#include "conn.h"

#include <errno.h>

/* The kernel's send buffer when vtt sets none: Linux grows it up to tcp_wmem's third
 * field, 4,194,304 on the machines this was written on (/proc/sys/net/ipv4/tcp_wmem). */
#define FK_KERNEL_DEFAULT ((size_t)4 << 20)
#define FK_PHONES 4

typedef struct {
    int     fd;                /* -1: a free slot */
    size_t  sndbuf;            /* what SO_SNDBUF was set to; 0 for never */
    ByteBuf got;               /* every byte vtt wrote, in order */
    size_t  read;              /* how much of it the phone has read */
} FakePhone;

static FakePhone fk[FK_PHONES];

static FakePhone *fk_phone(int fd)
{
    for (int i = 0; i < FK_PHONES; i++)
        if (fk[i].fd == fd) return &fk[i];
    return NULL;
}

static int fk_accept(int fd, struct sockaddr *addr, socklen_t *alen)
{
    int c = CONN_REAL.accept(fd, addr, alen);
    if (c < 0) return c;
    for (int i = 0; i < FK_PHONES; i++)
        if (fk[i].fd < 0) {
            fk[i].fd     = c;
            fk[i].sndbuf = 0;
            fk[i].read   = 0;
            fk[i].got.len = 0;
            break;
        }
    return c;
}

/* Up to what the kernel would hold unread; then EAGAIN, as a full socket says. */
static ssize_t fk_write(int fd, const void *buf, size_t n)
{
    FakePhone *p = fk_phone(fd);
    if (!p) return CONN_REAL.write(fd, buf, n);
    size_t cap    = p->sndbuf ? p->sndbuf : FK_KERNEL_DEFAULT;
    size_t unread = p->got.len - p->read;
    size_t take   = unread >= cap ? 0 : cap - unread;
    if (take > n) take = n;
    if (take == 0) { errno = EAGAIN; return -1; }
    bb_put(&p->got, buf, take);
    return (ssize_t)take;
}

static int fk_close(int fd)
{
    FakePhone *p = fk_phone(fd);
    if (p) p->fd = -1;
    return CONN_REAL.close(fd);
}

static int fk_setsockopt(int fd, int level, int name, const void *val, socklen_t len)
{
    FakePhone *p = fk_phone(fd);
    if (p && level == SOL_SOCKET && name == SO_SNDBUF && len == sizeof(int)) p->sndbuf = (size_t)*(const int *)val;
    return CONN_REAL.setsockopt(fd, level, name, val, len);
}

static const Conn FK_TABLE = { fk_accept, NULL, fk_write, fk_close, fk_setsockopt };

/* The phone reads all it has been sent, into its decoder. */
static void fk_read(FakePhone *p, WireDec *d)
{
    if (p->got.len > p->read) wire_dec_feed(d, (uint8_t *)p->got.data + p->read, p->got.len - p->read);
    p->read = p->got.len;
}

/* Every cell of a frame changed: a screen's worth of diff. */
static void lag_frame(Renderer *r, int f)
{
    rnd_begin(r);
    char line[61];
    for (int y = 0; y < 16; y++) {
        for (int x = 0; x < 60; x++) line[x] = (char)('a' + (f + x + y) % 26);
        line[60] = '\0';
        draw_text(r, 0, y, line, -1, style(0xD8D8E0, 0x0E0E12, 0));
    }
}

void test_net_lag(void)
{
    Conn table = FK_TABLE;
    table.read = CONN_REAL.read;
    for (int i = 0; i < FK_PHONES; i++) { fk[i].fd = -1; bb_init(&fk[i].got, 4096); }
    conn_use(&table);

    Renderer r;
    rnd_init(&r);
    rnd_resize(&r, 60, 16);
    lag_frame(&r, 0);
    rnd_flush(&r, NULL);

    Net n;
    net_init(&n);
    char err[128];
    uint64_t now = 1000;
    CHECK_EQ(net_start(&n, 0, &r, err, sizeof err), 0);

    /* Two watchers (local: no code), greeted and sent their FULLs: p will
     * stop reading, q reads every frame. */
    int w = net_connect(n.port), wq = net_connect(n.port);
    CHECK(w >= 0 && wq >= 0);
    CHECK_EQ((int)write(w, "VTT1\n", 5), 5);
    CHECK_EQ((int)write(wq, "VTT1\n", 5), 5);
    for (int i = 0; i < 40 && !(n.ncl == 2 && n.cl[0].greeted && n.cl[1].greeted); i++) net_pump(&n, now);
    FakePhone *p = n.ncl == 2 ? fk_phone(n.cl[0].fd) : NULL;
    FakePhone *q = n.ncl == 2 ? fk_phone(n.cl[1].fd) : NULL;
    CHECK(p != NULL && q != NULL);
    if (!p || !q) goto out;

    WireCatch c, cq;
    memset(&c, 0, sizeof c);
    memset(&cq, 0, sizeof cq);
    WireDec d, dq;
    wire_dec_init(&d, &WC_SINK, &c);
    wire_dec_init(&dq, &WC_SINK, &cq);
    fk_read(p, &d);
    fk_read(q, &dq);

    CASE("vtt asks the kernel for a send buffer of 128 KB a phone");
    CHECK_EQ(p->sndbuf, (size_t)128 * 1024);

    CASE("a phone that stops reading for 450 frames is never more than a FULL behind vtt's side of the socket");
    {
        /* A FULL of this window, from an encoder of the test's own. */
        WireEnc e;
        wire_enc_init(&e, 1 << 20);
        wire_enc_full(&e, &r);
        size_t full = e.len;
        wire_enc_free(&e);

        uint32_t dropped = n.dropped;
        for (int f = 1; f <= 450; f++) {
            lag_frame(&r, f);
            net_frame_begin(&n);
            rnd_flush(&r, NULL);
            net_frame_end(&n, now);
            net_pump(&n, now);
            fk_read(q, &dq);
        }
        CHECK_EQ(n.ncl, 2);
        CHECK_EQ(n.dropped, dropped);
        size_t unread = p->got.len - p->read;
        size_t queued = n.cl[0].out_len - n.cl[0].out_off;
        /* The kernel's buffer, then at most a FULL in vtt's queue. */
        printf("    a FULL %zu bytes; behind: %zu in the kernel, %zu queued\n", full, unread, queued);
        CHECK(queued <= full);
        CHECK(unread + queued <= p->sndbuf + full);
    }

    CASE("a phone that reads is sent every frame meanwhile, and never a FULL past its first");
    {
        int same = 1;
        for (int y = 0; y < 16; y++)
            for (int x = 0; x < 60; x++)
                if (cq.grid[y * 64 + x].ch != r.front[y * 60 + x].ch) same = 0;
        CHECK(same);
        CHECK_EQ(cq.fulls, 1);
        CHECK_EQ(cq.ends, 451);
        CHECK_EQ(n.cl[1].behind, 0);
    }

    CASE("when it reads again it catches up to the screen as it is now");
    {
        for (int i = 0; i < 10; i++) {
            fk_read(p, &d);
            net_pump(&n, now);
        }
        fk_read(p, &d);
        int same = 1;
        for (int y = 0; y < 16; y++)
            for (int x = 0; x < 60; x++)
                if (c.grid[y * 64 + x].ch != r.front[y * 60 + x].ch) same = 0;
        CHECK(same);
        CHECK_EQ(c.fulls, 2);              /* its first, and the one catch-up */
        CHECK_EQ(n.cl[0].behind, 0);
        CHECK_EQ(n.ncl, 2);
    }

out:
    close(w);
    close(wq);
    net_stop(&n);
    rnd_free(&r);
    conn_use(NULL);
    for (int i = 0; i < FK_PHONES; i++) bb_free(&fk[i].got);
}
