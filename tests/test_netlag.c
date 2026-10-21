/* Tests: a phone that stops reading (docs/V2.md, step 7's plan, items 1 and 3; review 2's
 * B8). The phones' sockets go through a fake (conn.h): what vtt writes to a phone lands in a
 * store of the test's, as much as the kernel would take -- the send buffer vtt asked for, or
 * this machine's ceiling when it asked for none -- and no more until the phone reads. So
 * "how far behind is it" is exact and quick, where a real socket fills megabytes first. */

#include "harness.h"
#include "app_priv.h"
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

    CASE("when it reads again it catches up to the screen as it is now, with no frame after (review, finding 1)");
    {
        /* A handout drains its queue by client_flush, not POLLOUT, and the
         * screen does not change again: the catch-up must still come. */
        fk_read(p, &d);
        net_set_handout(&n, "x", 1, now);
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

    CASE("a phone behind when frames stop for everyone takes the next live frame, a FULL, as its catch-up");
    {
        for (int f = 451; f <= 700; f++) {
            lag_frame(&r, f);
            net_frame_begin(&n);
            rnd_flush(&r, NULL);
            net_frame_end(&n, now);
            net_pump(&n, now);
            fk_read(q, &dq);
        }
        CHECK_EQ(n.cl[0].behind, 1);
        net_set_live(&n, 0);
        for (int i = 0; i < 10; i++) { fk_read(p, &d); net_pump(&n, now); }
        CHECK_EQ(n.cl[0].out_len - n.cl[0].out_off, (size_t)0);
        CHECK_EQ(n.cl[0].behind, 1);              /* nothing is sent while frames are off */
        int fulls = c.fulls;
        net_set_live(&n, 1);
        lag_frame(&r, 701);
        net_frame_begin(&n);
        rnd_flush(&r, NULL);
        net_frame_end(&n, now);
        fk_read(p, &d);
        CHECK_EQ(n.cl[0].behind, 0);
        CHECK_EQ(c.fulls, fulls + 1);
        int same = 1;
        for (int y = 0; y < 16; y++)
            for (int x = 0; x < 60; x++)
                if (c.grid[y * 64 + x].ch != r.front[y * 60 + x].ch) same = 0;
        CHECK(same);
    }

    CASE("the page's own decoder ends a stalled phone's stream -- frames, none, one FULL, more -- where the C decoder does");
    if (system("command -v node >/dev/null 2>&1") != 0) {
        CHECK(getenv("VTT_REQUIRE_NODE") == NULL);
        printf("  (skipped: no node to run the page's decoder)\n");
    } else {
        Sandbox sb = sandbox_enter("netlag");
        CHECK_EQ(sb.ok, 1);
        if (sb.ok) {
            ByteBuf want, got;
            page_feed_expect(&c, &want);
            CHECK_EQ(page_feed_run(sb.dir, p->got.data, p->got.len, &got), 0);
            CHECK(c.fulls >= 3);                   /* its first, and two catch-ups */
            CHECK_EQ(strcmp(got.data, want.data), 0);
            bb_free(&want);
            bb_free(&got);
        }
        sandbox_leave(&sb);
    }

out:
    close(w);
    close(wq);
    net_stop(&n);
    rnd_free(&r);
    conn_use(NULL);
    for (int i = 0; i < FK_PHONES; i++) bb_free(&fk[i].got);
}

/* A greeted local watcher on n, its fake phone, read up to now into d. */
static FakePhone *lag_watcher(Net *n, int *fd, WireDec *d, uint64_t now)
{
    int had = n->ncl;
    *fd = net_connect(n->port);
    if (*fd < 0 || write(*fd, "VTT1\n", 5) != 5) return NULL;
    for (int i = 0; i < 40 && !(n->ncl == had + 1 && n->cl[had].greeted); i++) net_pump(n, now);
    FakePhone *p = n->ncl == had + 1 ? fk_phone(n->cl[had].fd) : NULL;
    for (int i = 0; p && i < 400 && (p->got.len > p->read || n->cl[had].out_len > n->cl[had].out_off); i++) {
        fk_read(p, d);
        net_pump(n, now);
    }
    return p;
}

void test_net_big(void)
{
    Conn table = FK_TABLE;
    table.read = CONN_REAL.read;
    for (int i = 0; i < FK_PHONES; i++) { fk[i].fd = -1; bb_init(&fk[i].got, 4096); }
    conn_use(&table);
    uint64_t now = 1000;

    CASE("a phone joining a 400x170 window of a color a cell is sent its FULL, and the buffer follows it");
    {
        /* Review 3's harness (docs/reviews/, T7): every cell a run of its own,
         * 12 bytes, so the FULL is 816,006 bytes against a 128 KB buffer. */
        Renderer r;
        rnd_init(&r);
        rnd_resize(&r, 400, 170);
        rnd_begin(&r);
        for (int y = 0; y < 170; y++)
            for (int x = 0; x < 400; x++) {
                char ch[2] = { (char)('a' + (x + y) % 26), 0 };
                draw_text(&r, x, y, ch, -1, style(0x101010u * (uint32_t)((x + y) % 7 + 1), 0x0E0E12, 0));
            }
        rnd_flush(&r, NULL);
        Net n;
        net_init(&n);
        char err[128];
        CHECK_EQ(net_start(&n, 0, &r, err, sizeof err), 0);
        WireCatch c;
        memset(&c, 0, sizeof c);
        WireDec d;
        wire_dec_init(&d, &WC_SINK, &c);
        int w = -1;
        FakePhone *p = lag_watcher(&n, &w, &d, now);
        CHECK(p != NULL);
        CHECK_EQ(n.ncl, 1);
        CHECK_EQ(n.dropped, 0u);
        CHECK_EQ(c.fulls, 1);
        CHECK_EQ(c.w, 400);
        CHECK_EQ(c.h, 170);
        CHECK_EQ(c.ends, 1);
        CHECK_EQ(n.full_len, wire_full_max(400, 170));
        CHECK(n.enc.cap >= n.full_len && n.enc.cap <= 2 * n.full_len);
        if (w >= 0) close(w);
        net_stop(&n);
        rnd_free(&r);
    }

    CASE("a phone vtt has to drop is counted and named on the GM's line, which the players do not see");
    {
        Sandbox sb = sandbox_enter("netbig");
        CHECK_EQ(sb.ok, 1);
        if (sb.ok) {
            write_map_file(sb.dir, "fight.vtt");
            char path[600];
            snprintf(path, sizeof path, "%s/fight.vtt", sb.dir);
            Renderer r;
            App a;
            rnd_init(&r);
            rnd_resize(&r, 80, 24);
            app_init(&a, NULL, &r);
            CHECK_EQ(app_open_map(&a, path), 0);
            Key f2 = { KEY_F2, 0, 0 };
            app_key(&a, f2);
            press(&a, ":serve\r");
            CHECK_EQ(net_active(&a.net), 1);
            WireCatch c;
            memset(&c, 0, sizeof c);
            WireDec d;
            wire_dec_init(&d, &WC_SINK, &c);
            app_frame(&a, NULL, 0);
            int w = -1;
            FakePhone *p = lag_watcher(&a.net, &w, &d, now);
            CHECK(p != NULL);
            /* It stops reading; handouts are not frames, so nothing holds
             * them back, and its queue reaches its limit. */
            char text[WIRE_HANDOUT_MAX];
            for (int k = 0; k < 400 && a.net.ncl == 1; k++) {
                memset(text, 'a' + k % 26, sizeof text);
                net_set_handout(&a.net, text, sizeof text, now);
            }
            CHECK_EQ(a.net.ncl, 0);
            CHECK_EQ(a.net.dropped, 1u);
            app_whisper_tick(&a);
            CHECK(strstr(a.status, "a phone with no name dropped: too far behind to catch up") != NULL);
            CHECK(app_view_differs(&a));
            if (w >= 0) close(w);
            app_free(&a);
            rnd_free(&r);
        }
        sandbox_leave(&sb);
    }

    conn_use(NULL);
    for (int i = 0; i < FK_PHONES; i++) bb_free(&fk[i].got);
}
