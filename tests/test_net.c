/* Tests: the wire format, the server and its clients, the players' frame, pings, :serve, the phone page. */

#include "harness.h"
#include "watch.h"

/* ---------------------------------------------------------------- wire */

static void wc_full(void *ctx, int w, int h) { WireCatch *c = ctx; c->w = w; c->h = h; c->fulls++; }

static void wc_pal(void *ctx, int i, uint32_t rgb) { ((WireCatch *)ctx)->pal[i] = rgb; }

static void wc_end(void *ctx) { ((WireCatch *)ctx)->ends++; }

static void wc_keepalive(void *ctx) { ((WireCatch *)ctx)->keepalives++; }

static void wc_run(void *ctx, int x, int y, int n, uint8_t fg, uint8_t bg, uint8_t attr,
                   const uint16_t *g)
{
    WireCatch *c = ctx;
    c->runs++;
    for (int i = 0; i < n; i++) {
        if (y < 0 || y >= 32 || x + i < 0 || x + i >= 64) continue;
        Cell *cell = &c->grid[y * 64 + x + i];
        cell->ch = g[i]; cell->fg = c->pal[fg]; cell->bg = c->pal[bg]; cell->attr = attr;
        c->glyphs++;
    }
}

/* The three text records, each counted and its last text kept. */
static void wc_text(char *dst, int *count, const char *text, size_t n)
{
    (*count)++;
    memcpy(dst, text, n);
    dst[n] = '\0';
}

static void wc_handout(void *ctx, const char *text, size_t n)
{
    WireCatch *c = ctx;
    wc_text(c->handout, &c->handouts, text, n);
    c->handout_n = n;
}

static void wc_whisper(void *ctx, const char *text, size_t n)
{
    WireCatch *c = ctx;
    wc_text(c->whisper, &c->whispers, text, n);
}

static void wc_names(void *ctx, const char *text, size_t n)
{
    WireCatch *c = ctx;
    wc_text(c->names, &c->namelists, text, n);
}

const WireSink WC_SINK = { wc_full, wc_pal, wc_run, wc_end, wc_keepalive, wc_handout, wc_whisper, wc_names };

void test_wire(void)
{
    Renderer r;
    rnd_init(&r);
    rnd_resize(&r, 40, 12);

    /* A frame with a few runs of text in two colors and a wide glyph. */
    rnd_begin(&r);
    draw_text(&r, 2, 3, "hello world", -1, style(0x112233, 0x000000, 0));
    draw_text(&r, 20, 3, "red", -1, style(0xFF0000, 0x000000, ATTR_BOLD));
    draw_text(&r, 0, 5, "中", -1, style(0x112233, 0x000000, 0));   /* wide */
    rnd_flush(&r, NULL);                                                  /* now in front */

    WireEnc e;
    wire_enc_init(&e, 65536);

    CASE("a full frame decodes to the same cells, in runs, through a palette");
    wire_enc_full(&e, &r);
    CHECK_EQ(e.overflow, 0);
    CHECK_EQ((int)e.cells, 40 * 12);
    WireCatch c;
    memset(&c, 0, sizeof c);
    WireDec d;
    wire_dec_init(&d, &WC_SINK, &c);
    uint8_t pal[2048];
    size_t  pn = wire_enc_palette(&e, 0, pal, sizeof pal);
    CHECK_EQ((int)pn, e.npal * 5);
    wire_dec_feed(&d, pal, pn);
    CHECK_EQ((int)wire_dec_feed(&d, e.buf, e.len), (int)e.len);
    CHECK_EQ(d.bad, 0);
    CHECK_EQ(c.w, 40);
    CHECK_EQ(c.h, 12);
    CHECK_EQ(c.fulls, 1);
    CHECK_EQ(c.ends, 1);
    CHECK_EQ(c.glyphs, 40 * 12);
    CHECK(c.runs < 40 * 12 / 4);                        /* runs, not cells */
    int same = 1;
    for (int y = 0; y < 12; y++)
        for (int x = 0; x < 40; x++) {
            const Cell *a = &r.front[y * 40 + x], *b = &c.grid[y * 64 + x];
            if (a->ch != b->ch || (a->fg & 0xFFFFFF) != b->fg || (a->bg & 0xFFFFFF) != b->bg || a->attr != b->attr) same = 0;
        }
    CHECK_EQ(same, 1);
    CHECK_EQ(c.grid[5 * 64 + 1].ch, 0);                 /* the wide glyph's second half */
    CHECK(e.npal >= 3 && e.npal <= 8);

    CASE("a diff frame carries only the changed cells, and no palette at all");
    rnd_begin(&r);
    draw_text(&r, 2, 3, "hello world", -1, style(0x112233, 0x000000, 0));
    draw_text(&r, 20, 3, "RED", -1, style(0xFF0000, 0x000000, ATTR_BOLD));
    draw_text(&r, 0, 5, "中", -1, style(0x112233, 0x000000, 0));
    wire_enc_begin(&e);
    rnd_set_observer(&r, wire_observe_cell, &e);
    rnd_flush(&r, NULL);
    rnd_set_observer(&r, NULL, NULL);
    wire_enc_end(&e);
    CHECK_EQ((int)e.cells, 3);
    CHECK_EQ((int)e.len, 1 + 10 + 2 * 3);              /* one run of three, then E */
    memset(&c, 0, sizeof c);
    wire_dec_init(&d, &WC_SINK, &c);
    wire_dec_feed(&d, pal, pn);
    wire_dec_feed(&d, e.buf, e.len);
    CHECK_EQ(c.runs, 1);
    CHECK_EQ(c.glyphs, 3);
    CHECK_EQ(c.grid[3 * 64 + 20].ch, 'R');

    CASE("nothing changed is nothing on the wire but the end mark");
    rnd_begin(&r);
    draw_text(&r, 2, 3, "hello world", -1, style(0x112233, 0x000000, 0));
    draw_text(&r, 20, 3, "RED", -1, style(0xFF0000, 0x000000, ATTR_BOLD));
    draw_text(&r, 0, 5, "中", -1, style(0x112233, 0x000000, 0));
    wire_enc_begin(&e);
    rnd_set_observer(&r, wire_observe_cell, &e);
    rnd_flush(&r, NULL);
    rnd_set_observer(&r, NULL, NULL);
    wire_enc_end(&e);
    CHECK_EQ((int)e.cells, 0);
    CHECK_EQ((int)e.len, 1);

    CASE("the decoder takes a stream one byte at a time, records split anywhere");
    wire_enc_full(&e, &r);
    memset(&c, 0, sizeof c);
    wire_dec_init(&d, &WC_SINK, &c);
    wire_dec_feed(&d, pal, pn);
    for (size_t i = 0; i < e.len; i++) CHECK_EQ((int)wire_dec_feed(&d, e.buf + i, 1), 1);
    CHECK_EQ(d.bad, 0);
    CHECK_EQ(c.fulls, 1);
    CHECK_EQ(c.glyphs, 40 * 12);
    /* and in odd chunks */
    memset(&c, 0, sizeof c);
    wire_dec_init(&d, &WC_SINK, &c);
    for (size_t i = 0; i < e.len; i += 7) wire_dec_feed(&d, e.buf + i, i + 7 <= e.len ? 7 : e.len - i);
    CHECK_EQ(c.glyphs, 40 * 12);
    CHECK_EQ(c.ends, 1);

    CASE("the palette is replayed from any point, and can be reset");
    CHECK_EQ((int)wire_enc_palette(&e, 1, pal, sizeof pal), (e.npal - 1) * 5);
    CHECK_EQ((int)wire_enc_palette(&e, e.npal, pal, sizeof pal), 0);
    wire_enc_reset_palette(&e);
    CHECK_EQ(e.npal, 0);
    wire_enc_full(&e, &r);
    CHECK(e.npal >= 3);

    CASE("a frame that will not fit is flagged, never overrun");
    WireEnc tiny;
    wire_enc_init(&tiny, 64);
    wire_enc_full(&tiny, &r);
    CHECK_EQ(tiny.overflow, 1);
    CHECK((int)tiny.len <= 64);
    wire_enc_free(&tiny);

    CASE("an unknown tag stops the decoder rather than guessing");
    uint8_t junk[3] = { 'Q', 1, 2 };
    wire_dec_init(&d, &WC_SINK, &c);
    wire_dec_feed(&d, junk, 3);
    CHECK_EQ(d.bad, 1);

    wire_enc_free(&e);
    rnd_free(&r);
}

/* ----------------------------------------------------------------- net */

static void hex20(const uint8_t *d, char *out)
{
    for (int i = 0; i < 20; i++) sprintf(out + 2 * i, "%02x", d[i]);
}

void test_net_primitives(void)
{
    uint8_t d[20];
    char    h[41];

    CASE("SHA-1 matches the RFC vectors, one block, empty, and two blocks");
    net_sha1((const uint8_t *)"abc", 3, d); hex20(d, h);
    CHECK_EQ(strcmp(h, "a9993e364706816aba3e25717850c26c9cd0d89d"), 0);
    net_sha1((const uint8_t *)"", 0, d); hex20(d, h);
    CHECK_EQ(strcmp(h, "da39a3ee5e6b4b0d3255bfef95601890afd80709"), 0);
    const char *two = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    net_sha1((const uint8_t *)two, strlen(two), d); hex20(d, h);
    CHECK_EQ(strcmp(h, "84983e441c3bd26ebaae4aa1f95129e5e54670f1"), 0);
    uint8_t million[1000];
    memset(million, 'a', sizeof million);
    net_sha1(million, 64, d); hex20(d, h);                    /* exactly one block of data */
    CHECK_EQ(strcmp(h, "0098ba824b5c16427bd7a1122a5a442a25ec644d"), 0);

    CASE("base64 pads the way the RFC does");
    char b[16];
    net_base64((const uint8_t *)"Man", 3, b, sizeof b); CHECK_EQ(strcmp(b, "TWFu"), 0);
    net_base64((const uint8_t *)"Ma", 2, b, sizeof b);  CHECK_EQ(strcmp(b, "TWE="), 0);
    net_base64((const uint8_t *)"M", 1, b, sizeof b);   CHECK_EQ(strcmp(b, "TQ=="), 0);

    CASE("the WebSocket accept key is the one in RFC 6455");
    char acc[32];
    net_ws_accept("dGhlIHNhbXBsZSBub25jZQ==", acc);
    CHECK_EQ(strcmp(acc, "s3pPLMBiTxaQ9kYGzzhZRbK+xOo="), 0);
}

/* Reads from a raw client into the decoder until `ends` frames have
 * arrived or the wait runs out. */
static int net_recv_until(Net *n, int fd, WireDec *d, WireCatch *c, int ends, uint64_t now_ms)
{
    uint8_t buf[8192];
    for (int tries = 0; tries < 50 && c->ends < ends; tries++) {
        net_pump(n, now_ms);
        struct pollfd p = { fd, POLLIN, 0 };
        if (poll(&p, 1, 20) <= 0) continue;
        ssize_t got = read(fd, buf, sizeof buf);
        if (got <= 0) return -1;
        wire_dec_feed(d, buf, (size_t)got);
    }
    return c->ends >= ends ? 0 : -1;
}

/* Everything the server has for a WebSocket client, unframed into the decoder. */
static int ws_recv_until(Net *n, int fd, WireDec *d, WireCatch *c, int ends, uint64_t now_ms)
{
    static uint8_t buf[65536];
    static size_t  len;
    len = 0;
    for (int tries = 0; tries < 50 && c->ends < ends; tries++) {
        net_pump(n, now_ms);
        struct pollfd p = { fd, POLLIN, 0 };
        if (poll(&p, 1, 20) <= 0) continue;
        ssize_t got = read(fd, buf + len, sizeof buf - len);
        if (got <= 0) return -1;
        len += (size_t)got;
        size_t off = 0;
        for (;;) {
            if (len - off < 2) break;
            uint8_t  op = buf[off] & 0x0F;
            uint64_t pl = buf[off + 1] & 0x7F;
            size_t   hl = 2;
            if (pl == 126) { if (len - off < 4) break; pl = ((uint64_t)buf[off + 2] << 8) | buf[off + 3]; hl = 4; }
            if (len - off < hl + pl) break;
            CHECK_EQ(buf[off] & 0x80, 0x80);
            if (op == 9) c->keepalives++;                  /* the server asking if we are there */
            else { CHECK_EQ(op, 2); wire_dec_feed(d, buf + off + hl, (size_t)pl); }
            off += hl + (size_t)pl;
        }
        memmove(buf, buf + off, len - off);
        len -= off;
    }
    return c->ends >= ends ? 0 : -1;
}

/* A raw client past its hello, its FULL drained. */
static int netmsg_raw(Net *n, uint64_t now)
{
    int fd = net_connect(n->port);
    if (fd < 0) return -1;
    if (write(fd, "VTT1\n", 5) != 5) { close(fd); return -1; }
    WireCatch c;
    memset(&c, 0, sizeof c);
    WireDec d;
    wire_dec_init(&d, &WC_SINK, &c);
    net_recv_until(n, fd, &d, &c, 1, now);
    return fd;
}

/* A browser past its upgrade, whatever it was sent read and thrown away. */
static int netmsg_ws(Net *n, uint64_t now)
{
    int fd = ws_connect(n, "");
    if (fd < 0) return -1;
    char buf[65536];
    for (int i = 0; i < 20; i++) {
        net_pump(n, now);
        struct pollfd p = { fd, POLLIN, 0 };
        if (poll(&p, 1, 20) > 0 && read(fd, buf, sizeof buf) <= 0) break;
    }
    return fd;
}

/* One WebSocket frame from a client: masked unless told otherwise. */
static void ws_send(int fd, uint8_t b0, const void *payload, size_t len, int masked)
{
    uint8_t f[300];
    size_t  k = 0;
    f[k++] = b0;
    if (len < 126) f[k++] = (uint8_t)((masked ? 0x80 : 0) | len);
    else { f[k++] = (uint8_t)((masked ? 0x80 : 0) | 126); f[k++] = (uint8_t)(len >> 8); f[k++] = (uint8_t)len; }
    static const uint8_t key[4] = { 0x37, 0xFA, 0x21, 0x3D };
    if (masked) { memcpy(f + k, key, 4); k += 4; }
    for (size_t i = 0; i < len && k < sizeof f; i++)
        f[k++] = masked ? ((const uint8_t *)payload)[i] ^ key[i & 3] : ((const uint8_t *)payload)[i];
    CHECK_EQ((size_t)write(fd, f, k), k);
}

static int netmsg_client_open(const Net *n, uint32_t id)
{
    for (int i = 0; i < n->ncl; i++) if (n->cl[i].id == id) return 1;
    return 0;
}

/* What arrived on a browser's socket: WebSocket pings, and 'Z' records in
 * binary frames. Reads what is there now, without waiting long. */
static void ws_count(int fd, int *pings, int *zs)
{
    uint8_t buf[65536];
    ssize_t len = 0;
    struct pollfd p = { fd, POLLIN, 0 };
    while (poll(&p, 1, 30) > 0) {
        ssize_t got = read(fd, buf + len, sizeof buf - (size_t)len);
        if (got <= 0) break;
        len += got;
        if ((size_t)len == sizeof buf) break;
    }
    for (ssize_t off = 0; off + 2 <= len; ) {
        uint8_t op = buf[off] & 0x0F;
        size_t  pl = buf[off + 1] & 0x7F, hl = 2;
        if (pl == 126) {
            if (off + 4 > len) break;
            pl = ((size_t)buf[off + 2] << 8) | buf[off + 3];
            hl = 4;
        }
        if ((size_t)off + hl + pl > (size_t)len) break;
        if (op == 9) (*pings)++;
        if (op == 2 && pl == 1 && buf[(size_t)off + hl] == 'Z') (*zs)++;
        off += (ssize_t)(hl + pl);
    }
}

static const NetClient *net_client_by_id(const Net *n, uint32_t id)
{
    for (int i = 0; i < n->ncl; i++) if (n->cl[i].id == id) return &n->cl[i];
    return NULL;
}

/* A quiet phone stays: the server asks a silent browser whether it is
 * there, the browser answers by itself, and only one that stops answering
 * is dropped. */
void test_net_live(void)
{
    Renderer r;
    rnd_init(&r);
    rnd_resize(&r, 60, 16);
    rnd_begin(&r);
    rnd_flush(&r, NULL);
    Net n;
    net_init(&n);
    char err[128];
    CHECK_EQ(net_start(&n, 0, &r, err, sizeof err), 0);
    uint64_t t0 = 100000;
    int pings = 0, zs = 0;

    int w = netmsg_raw(&n, t0);                              /* a watcher alongside, silent throughout */
    uint32_t wid = n.cl[0].id;
    int b = netmsg_ws(&n, t0);
    uint32_t bid = n.cl[n.ncl - 1].id;
    CHECK(net_client_by_id(&n, bid) != NULL);
    ws_count(b, &pings, &zs);                                /* whatever came with the upgrade */
    pings = zs = 0;

    CASE("a silent browser is asked at fifteen seconds, not before, and never sent a 'Z'");
    net_pump(&n, t0 + NET_KEEPALIVE_MS - 1);
    ws_count(b, &pings, &zs);
    CHECK_EQ(pings, 0);
    net_pump(&n, t0 + NET_KEEPALIVE_MS);
    ws_count(b, &pings, &zs);
    CHECK_EQ(pings, 1);
    CHECK_EQ(zs, 0);

    CASE("a pong is life, and the next question waits another fifteen seconds");
    uint64_t t1 = t0 + NET_KEEPALIVE_MS + 100;
    ws_send(b, 0x8A, "", 0, 1);
    for (int i = 0; i < 10 && net_client_by_id(&n, bid)->last_rx_ms != t1; i++) net_pump(&n, t1);
    CHECK_EQ(net_client_by_id(&n, bid)->last_rx_ms, t1);
    pings = 0;
    net_pump(&n, t1 + NET_KEEPALIVE_MS - 1);
    ws_count(b, &pings, &zs);
    CHECK_EQ(pings, 0);
    net_pump(&n, t1 + NET_KEEPALIVE_MS);
    ws_count(b, &pings, &zs);
    CHECK_EQ(pings, 1);
    net_pump(&n, t0 + NET_IDLE_MS + 1);                      /* past a minute from the start: answered, so still here */
    CHECK(net_client_by_id(&n, bid) != NULL);

    CASE("frames going out are not the browser speaking: a busy map's silent phone is still asked");
    uint64_t tb = t0 + NET_IDLE_MS + 1;                      /* asked just now, above */
    ws_count(b, &pings, &zs);
    pings = 0;
    for (uint64_t t = tb + 5000; t <= tb + NET_KEEPALIVE_MS; t += 5000) {
        rnd_begin(&r);
        char txt[16];
        snprintf(txt, sizeof txt, "t %d", (int)(t % 1000000));
        draw_text(&r, 1, 1, txt, -1, style(0xD8D8E0, 0x0E0E12, 0));
        net_frame_begin(&n); rnd_flush(&r, NULL); net_frame_end(&n, t);
        net_pump(&n, t);
    }
    ws_count(b, &pings, &zs);
    CHECK_EQ(pings, 1);

    CASE("a tap resets the clock too");
    uint64_t t2 = tb + NET_KEEPALIVE_MS + 1000;
    ws_send(b, 0x81, "P 1 1", 5, 1);
    for (int i = 0; i < 10 && net_client_by_id(&n, bid)->last_rx_ms != t2; i++) net_pump(&n, t2);
    net_take_pings(&n, (NetPing[NET_MAX_CLIENTS]){ 0 }, NET_MAX_CLIENTS);
    pings = 0;
    net_pump(&n, t2 + NET_KEEPALIVE_MS - 1);
    ws_count(b, &pings, &zs);
    CHECK_EQ(pings, 0);

    CASE("unanswered, a browser is dropped at a minute of silence, counted as idle, not as slow");
    uint32_t dropped0 = n.dropped;
    net_pump(&n, t2 + NET_IDLE_MS - 1);
    CHECK(net_client_by_id(&n, bid) != NULL);
    net_pump(&n, t2 + NET_IDLE_MS);
    CHECK(net_client_by_id(&n, bid) == NULL);
    CHECK_EQ(n.idle_dropped, 1u);
    CHECK_EQ(n.dropped, dropped0);

    CASE("the last minute's question is not asked of a browser being dropped");
    /* Covered above: at t2 + NET_IDLE_MS it was closed, and no ping was
     * written first -- counted by what a fresh browser receives below. */
    {
        int q = netmsg_ws(&n, t2 + NET_IDLE_MS);
        uint32_t qid = n.cl[n.ncl - 1].id;
        ws_count(q, &pings, &zs);
        pings = 0;
        for (uint64_t t = t2 + NET_IDLE_MS + NET_KEEPALIVE_MS; t <= t2 + 2 * NET_IDLE_MS; t += NET_KEEPALIVE_MS)
            net_pump(&n, t);
        ws_count(q, &pings, &zs);
        CHECK_EQ(pings, 3);                                 /* at 15, 30 and 45 s; at 60 it is dropped */
        CHECK(net_client_by_id(&n, qid) == NULL);
        close(q);
    }

    CASE("a hello never finished is dropped at a minute, so eight of them cannot fill the server");
    {
        uint64_t th = t2 + 3 * NET_IDLE_MS;
        int h = net_connect(n.port);
        CHECK(write(h, "VTT1", 4) == 4);
        for (int i = 0; i < 10; i++) net_pump(&n, th);
        uint32_t hid = n.cl[n.ncl - 1].id;
        CHECK_EQ(n.cl[n.ncl - 1].greeted, 0);
        net_pump(&n, th + NET_IDLE_MS - 1);
        CHECK(net_client_by_id(&n, hid) != NULL);
        net_pump(&n, th + NET_IDLE_MS);
        CHECK(net_client_by_id(&n, hid) == NULL);
        close(h);
    }

    CASE("the watcher beside it was never dropped for silence, and got its 'Z's");
    CHECK(net_client_by_id(&n, wid) != NULL);
    {
        WireCatch c;
        memset(&c, 0, sizeof c);
        WireDec d;
        wire_dec_init(&d, &WC_SINK, &c);
        uint8_t buf[65536];
        struct pollfd p = { w, POLLIN, 0 };
        while (poll(&p, 1, 30) > 0) {
            ssize_t got = read(w, buf, sizeof buf);
            if (got <= 0) break;
            wire_dec_feed(&d, buf, (size_t)got);
        }
        CHECK(c.keepalives >= 2);
        CHECK_EQ(d.bad, 0);
    }

    close(b);
    close(w);
    net_stop(&n);
    rnd_free(&r);
}

/* What the phones may say: "P col row", read, limited and handed over. */
void test_net_msg(void)
{
    Renderer r;
    rnd_init(&r);
    rnd_resize(&r, 60, 16);
    rnd_begin(&r);
    rnd_flush(&r, NULL);
    Net n;
    net_init(&n);
    char err[128];
    CHECK_EQ(net_start(&n, 0, &r, err, sizeof err), 0);
    uint64_t now = 0;
    NetPing got[NET_MAX_CLIENTS];

    CASE("a watcher's line after its hello is a ping, handed over once, with the client's id");
    int w = netmsg_raw(&n, now);
    CHECK(w >= 0);
    CHECK_EQ(n.ncl, 1);
    uint32_t wid = n.cl[0].id;
    CHECK(write(w, "P 5 3\n", 6) == 6);
    for (int i = 0; i < 10 && n.ninbox == 0; i++) net_pump(&n, now);
    CHECK_EQ(net_take_pings(&n, got, NET_MAX_CLIENTS), 1);
    CHECK_EQ(got[0].who, wid);
    CHECK_EQ(got[0].sx, 5);
    CHECK_EQ(got[0].sy, 3);
    CHECK_EQ(net_take_pings(&n, got, NET_MAX_CLIENTS), 0);

    CASE("one a second: the next within it is dropped and counted, and taken at the second");
    CHECK(write(w, "P 6 3\n", 6) == 6);
    for (int i = 0; i < 5; i++) net_pump(&n, now + 999);
    CHECK_EQ(net_take_pings(&n, got, NET_MAX_CLIENTS), 0);
    CHECK_EQ(n.pings_dropped, 1u);
    CHECK(write(w, "P 7 3\r\n", 7) == 7);
    for (int i = 0; i < 10 && n.ninbox == 0; i++) net_pump(&n, now + 1000);
    CHECK_EQ(net_take_pings(&n, got, NET_MAX_CLIENTS), 1);
    CHECK_EQ(got[0].sx, 7);
    now = 5000;

    CASE("a browser's text, binary and unmasked frames all arrive");
    int b = netmsg_ws(&n, now);
    CHECK(b >= 0);
    CHECK_EQ(n.ncl, 2);
    uint32_t bid = n.cl[1].id;
    CHECK(bid != wid);
    ws_send(b, 0x81, "P 10 4", 6, 1);
    for (int i = 0; i < 10 && n.ninbox == 0; i++) net_pump(&n, now);
    CHECK_EQ(net_take_pings(&n, got, NET_MAX_CLIENTS), 1);
    CHECK_EQ(got[0].who, bid);
    CHECK_EQ(got[0].sx, 10);
    ws_send(b, 0x82, "P 11 4", 6, 1);
    for (int i = 0; i < 10 && n.ninbox == 0; i++) net_pump(&n, now + 1000);
    CHECK_EQ(net_take_pings(&n, got, NET_MAX_CLIENTS), 1);
    ws_send(b, 0x81, "P 12 4", 6, 0);
    for (int i = 0; i < 10 && n.ninbox == 0; i++) net_pump(&n, now + 2000);
    CHECK_EQ(net_take_pings(&n, got, NET_MAX_CLIENTS), 1);
    CHECK_EQ(got[0].sx, 12);
    now = 10000;

    CASE("what does not parse is counted and ignored; the client stays");
    static const char *junk[] = { "P x y", "P 5", "Q 1 2", "P 99999 1", "", "P 1 2 3", "P  1 2", "P 1 -2" };
    uint32_t bad0 = n.bad_msgs;
    for (size_t j = 0; j < sizeof junk / sizeof *junk; j++) {
        ws_send(b, 0x81, junk[j], strlen(junk[j]), 1);
        for (int i = 0; i < 5; i++) net_pump(&n, now);
    }
    char big[200];
    memset(big, 'z', sizeof big);
    ws_send(b, 0x81, big, sizeof big, 1);
    CHECK(write(w, "garbage\n", 8) == 8);
    for (int i = 0; i < 5; i++) net_pump(&n, now);
    CHECK_EQ(n.bad_msgs - bad0, (uint32_t)(sizeof junk / sizeof *junk) + 2);
    CHECK_EQ(net_take_pings(&n, got, NET_MAX_CLIENTS), 0);
    CHECK(netmsg_client_open(&n, bid));
    CHECK(netmsg_client_open(&n, wid));

    CASE("a watcher that sends one byte after its hello is still served, and a line too long is dropped");
    CHECK(write(w, "P", 1) == 1);
    for (int i = 0; i < 5; i++) net_pump(&n, now);
    CHECK(netmsg_client_open(&n, wid));
    CHECK(write(w, " 2 2\n", 5) == 5);                      /* the rest of the line */
    for (int i = 0; i < 10 && n.ninbox == 0; i++) net_pump(&n, now);
    CHECK_EQ(net_take_pings(&n, got, NET_MAX_CLIENTS), 1);
    CHECK_EQ(got[0].sx, 2);
    char longline[100];
    memset(longline, 'P', sizeof longline);
    CHECK(write(w, longline, sizeof longline) == (ssize_t)sizeof longline);
    for (int i = 0; i < 5; i++) net_pump(&n, now);
    CHECK(netmsg_client_open(&n, wid));
    CHECK_EQ(n.cl[0].in_len, 0u);
    now = 20000;

    CASE("a flood: five hundred pings in one write, one taken, nothing grows, the server lives");
    {
        static char flood[500 * 6];
        for (int i = 0; i < 500; i++) memcpy(flood + i * 6, "P 1 1\n", 6);
        size_t off = 0;
        for (int tries = 0; tries < 200 && off < sizeof flood; tries++) {
            ssize_t put = write(w, flood + off, sizeof flood - off);
            if (put > 0) off += (size_t)put;
            net_pump(&n, now);
        }
        for (int i = 0; i < 20; i++) net_pump(&n, now);
        CHECK_EQ(net_take_pings(&n, got, NET_MAX_CLIENTS), 1);
        CHECK(n.ninbox == 0);
        CHECK(netmsg_client_open(&n, wid));
    }
    now = 30000;

    CASE("switched off, pings are dropped and counted; a WebSocket ping is still answered");
    net_set_pings(&n, 0);
    CHECK_EQ(net_pings_on(&n), 0);
    uint32_t dropped0 = n.pings_dropped;
    ws_send(b, 0x81, "P 3 3", 5, 1);
    ws_send(b, 0x89, "hi", 2, 1);
    int pong = 0;
    for (int i = 0; i < 20 && !pong; i++) {
        net_pump(&n, now);
        uint8_t buf[256];
        struct pollfd p = { b, POLLIN, 0 };
        if (poll(&p, 1, 20) > 0) {
            ssize_t got_n = read(b, buf, sizeof buf);
            for (ssize_t k = 0; k + 3 < got_n; k++)
                if (buf[k] == 0x8A && buf[k + 1] == 2 && buf[k + 2] == 'h' && buf[k + 3] == 'i') pong = 1;
        }
    }
    CHECK(pong);
    CHECK_EQ(n.pings_dropped - dropped0, 1u);
    CHECK_EQ(net_take_pings(&n, got, NET_MAX_CLIENTS), 0);
    net_set_pings(&n, 1);

    CASE("framing a page never sends closes the client: a control frame over 125 bytes, a fragment, a reserved op, 64-bit length");
    static const uint8_t b0s[] = { 0x89, 0x01, 0x83 };
    for (size_t j = 0; j < 4; j++) {
        int c = netmsg_ws(&n, now);
        CHECK(c >= 0);
        uint32_t cid = n.cl[n.ncl - 1].id;
        if (j < 3) ws_send(c, b0s[j], big, j == 0 ? 200 : 5, 1);
        else { uint8_t f[10] = { 0x81, 0x80 | 127, 0, 0, 0, 0, 0, 0, 0, 1 }; CHECK(write(c, f, 10) == 10); }
        for (int i = 0; i < 10 && netmsg_client_open(&n, cid); i++) net_pump(&n, now);
        CHECK(!netmsg_client_open(&n, cid));
        close(c);
    }
    CHECK(netmsg_client_open(&n, bid));

    CASE("a frame split across two reads is read whole");
    now = 40000;
    {
        uint8_t f[12] = { 0x81, 0x80 | 6, 1, 2, 3, 4 };
        const char *msg = "P 8 8";
        for (int k = 0; k < 5; k++) f[6 + k] = (uint8_t)(msg[k] ^ f[2 + (k & 3)]);
        f[11] = (uint8_t)('\n' ^ f[2 + (5 & 3)]);
        CHECK(write(b, f, 7) == 7);
        for (int i = 0; i < 5; i++) net_pump(&n, now);
        CHECK_EQ(n.ninbox, 0);
        CHECK(write(b, f + 7, 5) == 5);
        for (int i = 0; i < 10 && n.ninbox == 0; i++) net_pump(&n, now);
        CHECK_EQ(net_take_pings(&n, got, NET_MAX_CLIENTS), 1);
        CHECK_EQ(got[0].sx, 8);
    }

    CASE("a client whose pong fails to send is closed, and the next client in its slot is left alone");
    {
        /* A ping, then a reset, seen by a poll that reported only the data:
         * the pong's write fails and closes the client, and the next one
         * slides into its slot while the frame loop is still running. */
        int x = netmsg_ws(&n, now);
        uint32_t xid = n.cl[n.ncl - 1].id;
        int y = netmsg_ws(&n, now);
        CHECK(x >= 0 && y >= 0);
        uint32_t yid = n.cl[n.ncl - 1].id;
        uint8_t f[] = { 0x89, 0x84, 1, 2, 3, 4, 'p' ^ 1, 'i' ^ 2, 'n' ^ 3, 'g' ^ 4 };
        CHECK(write(x, f, sizeof f) == (ssize_t)sizeof f);
        struct linger l = { 1, 0 };
        setsockopt(x, SOL_SOCKET, SO_LINGER, &l, sizeof l);
        close(x);
        /* Until the server's end has the ping and the reset, not a fixed
         * sleep: a slow machine must not turn this into a test of nothing. */
        struct pollfd fds[1 + NET_MAX_CLIENTS];
        int k = 0, seen = 0;
        for (int t = 0; t < 200 && !seen; t++) {
            k = net_pollfds(&n, fds, 1 + NET_MAX_CLIENTS);
            poll(fds, (nfds_t)k, 10);
            for (int i = 1; i < k; i++) seen |= (fds[i].revents & (POLLIN | POLLHUP | POLLERR)) != 0;
        }
        CHECK(seen);
        for (int i = 1; i < k; i++) if (fds[i].revents & POLLIN) fds[i].revents = POLLIN;
        net_service(&n, fds, k, now);                       /* and ASan */
        for (int i = 0; i < 5; i++) net_pump(&n, now);
        CHECK(!netmsg_client_open(&n, xid));                /* the one whose pong failed */
        CHECK(netmsg_client_open(&n, yid));
        CHECK(netmsg_client_open(&n, bid));
        close(y);
    }

    close(b);
    close(w);
    net_stop(&n);
    rnd_free(&r);
}

void test_net_server(void)
{
    Renderer r;
    rnd_init(&r);
    rnd_resize(&r, 60, 16);
    rnd_begin(&r);
    draw_text(&r, 1, 1, "the GM's screen", -1, style(0xD8D8E0, 0x0E0E12, 0));
    rnd_flush(&r, NULL);

    Net n;
    net_init(&n);
    char err[128];
    uint64_t now = 1000;

    CASE("the server opens on any free port and knows its address");
    CHECK_EQ(net_start(&n, 0, &r, err, sizeof err), 0);
    CHECK(n.port > 0);
    CHECK_EQ(strlen(n.code), (size_t)NET_CODE_LEN);
    char url[160];
    net_url(&n, url, sizeof url);
    CHECK(strstr(url, "http://") != NULL && strstr(url, "/?k=") != NULL);
    CHECK_EQ(net_clients(&n), 0);

    CASE("a watcher says hello and is sent the whole screen");
    int w = net_connect(n.port);
    CHECK(w >= 0);
    CHECK_EQ((int)write(w, "VTT1\n", 5), 5);          /* local: no code needed */
    WireCatch c;
    memset(&c, 0, sizeof c);
    WireDec d;
    wire_dec_init(&d, &WC_SINK, &c);
    CHECK_EQ(net_recv_until(&n, w, &d, &c, 1, now), 0);
    CHECK_EQ(net_clients(&n), 1);
    CHECK_EQ(c.fulls, 1);
    CHECK_EQ(c.w, 60);
    CHECK_EQ(c.h, 16);
    CHECK_EQ(c.glyphs, 60 * 16);
    CHECK_EQ(c.grid[1 * 64 + 5].ch, r.front[1 * 60 + 5].ch);   /* 'G' of GM, same cell */
    CHECK_EQ(c.grid[1 * 64 + 5].fg, 0xD8D8E0u);

    CASE("a frame with no change sends nothing; a change sends only the diff");
    uint64_t before = n.total_bytes;
    rnd_begin(&r);
    draw_text(&r, 1, 1, "the GM's screen", -1, style(0xD8D8E0, 0x0E0E12, 0));
    net_frame_begin(&n);
    rnd_flush(&r, NULL);
    net_frame_end(&n, now);
    CHECK_EQ((int)(n.total_bytes - before), 0);
    rnd_begin(&r);
    draw_text(&r, 1, 1, "the GM's SCREEN", -1, style(0xD8D8E0, 0x0E0E12, 0));
    net_frame_begin(&n);
    rnd_flush(&r, NULL);
    net_frame_end(&n, now);
    CHECK((int)(n.total_bytes - before) < 40);         /* one run of six, and E */
    CHECK_EQ(net_recv_until(&n, w, &d, &c, 2, now), 0);
    CHECK_EQ(c.grid[1 * 64 + 10].ch, 'S');
    CHECK_EQ(c.runs, c.runs);                          /* decoded, nothing bad */
    CHECK_EQ(d.bad, 0);

    CASE("while not live nothing goes out; going live again sends a full frame");
    net_set_live(&n, 0);
    rnd_begin(&r);
    draw_text(&r, 1, 3, "GM only", -1, style(0xFF0000, 0x0E0E12, 0));
    net_frame_begin(&n); rnd_flush(&r, NULL); net_frame_end(&n, now);
    before = n.total_bytes;
    net_pump(&n, now);
    CHECK_EQ((int)(n.total_bytes - before), 0);
    net_set_live(&n, 1);
    rnd_begin(&r);
    draw_text(&r, 1, 3, "back in play", -1, style(0xD8D8E0, 0x0E0E12, 0));
    net_frame_begin(&n); rnd_flush(&r, NULL); net_frame_end(&n, now);
    CHECK_EQ(net_recv_until(&n, w, &d, &c, 3, now), 0);
    CHECK_EQ(c.fulls, 2);
    CHECK_EQ(c.grid[3 * 64 + 1].ch, 'b');

    CASE("a keep-alive goes out when the line has been quiet");
    CHECK_EQ(net_recv_until(&n, w, &d, &c, 3, now + NET_KEEPALIVE_MS + 1), 0);
    for (int i = 0; i < 5 && c.keepalives == 0; i++) {
        net_pump(&n, now + NET_KEEPALIVE_MS + 1);
        uint8_t z;
        if (read(w, &z, 1) == 1) wire_dec_feed(&d, &z, 1);
    }
    CHECK_EQ(c.keepalives, 1);

    CASE("a browser gets the page, or a refusal without the code once it is not local");
    int b = net_connect(n.port);
    const char *req = "GET /?k=000000 HTTP/1.1\r\nHost: x\r\n\r\n";
    CHECK_EQ((int)write(b, req, strlen(req)), (int)strlen(req));
    char resp[4096] = { 0 };
    size_t rl = 0;
    for (int i = 0; i < 20 && !strstr(resp, "</body>") && !strstr(resp, "vtt: "); i++) {
        net_pump(&n, now);
        ssize_t got = read(b, resp + rl, sizeof resp - 1 - rl);
        if (got > 0) rl += (size_t)got;
    }
    CHECK(strncmp(resp, "HTTP/1.1 200", 12) == 0);
    CHECK(strstr(resp, "text/html") != NULL);
    close(b);
    b = net_connect(n.port);
    net_pump(&n, now);                                  /* accepted */
    for (int i = 0; i < n.ncl; i++) if (n.cl[i].kind == CL_NEW) n.cl[i].local = 0;
    const char *bad = "GET / HTTP/1.1\r\nHost: x\r\n\r\n";
    CHECK_EQ((int)write(b, bad, strlen(bad)), (int)strlen(bad));
    memset(resp, 0, sizeof resp); rl = 0;
    for (int i = 0; i < 20 && !strstr(resp, "code"); i++) {
        net_pump(&n, now);
        ssize_t got = read(b, resp + rl, sizeof resp - 1 - rl);
        if (got > 0) rl += (size_t)got;
    }
    CHECK(strncmp(resp, "HTTP/1.1 403", 12) == 0);
    close(b);

    CASE("a WebSocket upgrade is answered with the right key and a full frame in binary messages");
    int s = ws_connect(&n, "");
    CHECK(s >= 0);
    memset(resp, 0, sizeof resp); rl = 0;
    for (int i = 0; i < 20 && !strstr(resp, "\r\n\r\n"); i++) {
        net_pump(&n, now);
        ssize_t got = read(s, resp + rl, sizeof resp - 1 - rl);
        if (got > 0) rl += (size_t)got;
    }
    CHECK(strncmp(resp, "HTTP/1.1 101", 12) == 0);
    CHECK(strstr(resp, "Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=") != NULL);
    /* Whatever followed the headers is the first message. */
    WireCatch wc;
    memset(&wc, 0, sizeof wc);
    WireDec wd;
    wire_dec_init(&wd, &WC_SINK, &wc);
    char *body = strstr(resp, "\r\n\r\n") + 4;
    size_t bl = rl - (size_t)(body - resp);
    /* feed via the unframer: put the bytes back where ws_recv_until reads */
    if (bl) {
        /* the simplest way: a tiny inline unframe of what we already hold */
        size_t off = 0;
        while (bl - off >= 2) {
            uint64_t pl = (uint8_t)body[off + 1] & 0x7F; size_t hl = 2;
            if (pl == 126) { pl = ((uint64_t)(uint8_t)body[off + 2] << 8) | (uint8_t)body[off + 3]; hl = 4; }
            if (bl - off < hl + pl) break;
            wire_dec_feed(&wd, (uint8_t *)body + off + hl, (size_t)pl);
            off += hl + (size_t)pl;
        }
    }
    CHECK_EQ(ws_recv_until(&n, s, &wd, &wc, 1, now), 0);
    CHECK_EQ(wc.fulls, 1);
    CHECK_EQ(wc.glyphs, 60 * 16);
    CHECK_EQ(wc.grid[3 * 64 + 1].ch, 'b');
    CHECK_EQ(net_clients(&n), 2);

    CASE("a client that cannot take a frame is dropped, and the GM is never waited for");
    int ncl = n.ncl;
    for (int i = 0; i < n.ncl; i++) if (n.cl[i].kind == CL_RAW) n.cl[i].out_len = NET_SEND_CAP - 4;
    n.stale = 1;                                        /* force a full frame out */
    rnd_begin(&r);
    draw_text(&r, 1, 5, "a big change", -1, style(0xD8D8E0, 0x0E0E12, 0));
    net_frame_begin(&n); rnd_flush(&r, NULL); net_frame_end(&n, now);
    CHECK_EQ(n.ncl, ncl - 1);
    CHECK_EQ((int)n.dropped, 1);
    CHECK_EQ(ws_recv_until(&n, s, &wd, &wc, 2, now), 0);   /* the other still gets it */
    CHECK_EQ(wc.grid[5 * 64 + 1].ch, 'a');

    CASE("a client closing is noticed, and the last one out resets the palette");
    close(s);
    close(w);
    for (int i = 0; i < 10 && n.ncl > 0; i++) net_pump(&n, now);
    CHECK_EQ(n.ncl, 0);
    CHECK_EQ(n.enc.npal, 0);
    CHECK(r.observer == NULL);

    CASE("stop closes everything and can start again");
    net_stop(&n);
    CHECK_EQ(net_active(&n), 0);
    CHECK_EQ(net_start(&n, 0, &r, err, sizeof err), 0);
    net_stop(&n);
    rnd_free(&r);
}

/* The server's lifetime: it belongs to the encounter, so closing the map
 * takes it down unless :serve --stay-alive said otherwise. Quitting the
 * application always takes it down, which the operating system guarantees
 * in any case. */
void test_serve_lifetime(void)
{
    Sandbox sb = sandbox_enter("servelife");
    CHECK_EQ(sb.ok, 1);
    if (!sb.ok) return;

    write_map_file(sb.dir, "fight.vtt");
    write_map_file(sb.dir, "second.vtt");
    char path[600], other[600];
    snprintf(path, sizeof path, "%s/fight.vtt", sb.dir);
    snprintf(other, sizeof other, "%s/second.vtt", sb.dir);

    Renderer r;
    App      a;
    rnd_init(&r);
    rnd_resize(&r, 80, 24);
    app_init(&a, NULL, &r);
    CHECK_EQ(app_open_map(&a, path), 0);
    Key f2 = { KEY_F2, 0, 0 };
    app_key(&a, f2);

    CASE("by default the server goes down with the map, and the clients with it");
    press(&a, ":serve\r");
    CHECK_EQ(net_active(&a.net), 1);
    CHECK_EQ(net_stays(&a.net), 0);
    uint16_t port = a.net.port;
    int w = net_connect(port);
    CHECK(w >= 0);
    CHECK_EQ((int)write(w, "VTT1\n", 5), 5);
    net_pump(&a.net, 0);

    /* Let it actually watch: a player mid-encounter, not a bare socket. */
    WireCatch c;
    memset(&c, 0, sizeof c);
    WireDec d;
    wire_dec_init(&d, &WC_SINK, &c);
    app_frame(&a, NULL, 0);
    CHECK_EQ(net_recv_until(&a.net, w, &d, &c, 1, 0), 0);
    CHECK_EQ(c.w, 80);
    press(&a, ":serve\r");
    CHECK(strstr(a.status, "1 client") != NULL);

    CASE(":serve with the port it is already on answers, keeping the code and the watcher");
    {
        char code[NET_CODE_LEN + 1], cmd[32];
        str_lcpy(code, a.net.code, sizeof code);
        snprintf(cmd, sizeof cmd, ":serve %u\r", (unsigned)a.net.port);
        press(&a, cmd);
        CHECK_EQ(strcmp(a.net.code, code), 0);
        CHECK_EQ(net_clients(&a.net), 1);
        CHECK(strstr(a.status, "serving at http://") != NULL && strstr(a.status, "1 client") != NULL);

        CASE("a move to a port that is taken leaves the server and its watcher alone");
        int hog = socket(AF_INET, SOCK_STREAM, 0);
        struct sockaddr_in in4;
        memset(&in4, 0, sizeof in4);
        in4.sin_family      = AF_INET;
        in4.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        socklen_t len = sizeof in4;
        CHECK(hog >= 0 && bind(hog, (struct sockaddr *)&in4, sizeof in4) == 0 && listen(hog, 1) == 0 &&
              getsockname(hog, (struct sockaddr *)&in4, &len) == 0);
        uint16_t was = a.net.port;
        snprintf(cmd, sizeof cmd, ":serve %u\r", (unsigned)ntohs(in4.sin_port));
        press(&a, cmd);
        CHECK(strstr(a.status, "port ") != NULL);
        CHECK_EQ(net_active(&a.net), 1);
        CHECK_EQ(a.net.port, was);
        CHECK_EQ(strcmp(a.net.code, code), 0);
        CHECK_EQ(net_clients(&a.net), 1);
        if (hog >= 0) close(hog);
    }

    press(&a, ":q!\r");
    CHECK_EQ(a.map, NULL);
    CHECK_EQ(net_active(&a.net), 0);
    /* Both pieces of news, neither eating the other. */
    CHECK(strstr(a.status, "closed without saving") != NULL);
    CHECK(strstr(a.status, "1 client dropped") != NULL);

    struct timeval tv = { 1, 0 };
    setsockopt(w, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    uint8_t  z;
    ssize_t  got;
    do got = read(w, &z, 1); while (got > 0);
    CHECK_EQ((int)got, 0);                       /* the server hung up on it */
    close(w);

    CASE("the port is free again, so the next serve may have it back");
    CHECK_EQ(app_open_map(&a, path), 0);
    app_key(&a, f2);
    char cmd[64];
    snprintf(cmd, sizeof cmd, ":serve %u\r", (unsigned)port);
    press(&a, cmd);
    CHECK_EQ(net_active(&a.net), 1);
    CHECK_EQ((int)a.net.port, (int)port);

    CASE("--stay-alive keeps it up across a map close");
    press(&a, ":serve --stay-alive\r");
    CHECK_EQ(net_stays(&a.net), 1);
    CHECK_EQ((int)a.net.port, (int)port);        /* the flag alone never restarts it */
    CHECK(strstr(a.status, "staying up when the map closes") != NULL);
    press(&a, ":q!\r");
    CHECK_EQ(a.map, NULL);
    CHECK_EQ(net_active(&a.net), 1);
    CHECK(strstr(a.status, "remote view off") == NULL);

    CASE("and the same server carries over to the next map");
    CHECK_EQ(app_open_map(&a, other), 0);
    app_key(&a, f2);
    CHECK_EQ(net_active(&a.net), 1);
    CHECK_EQ((int)a.net.port, (int)port);
    CHECK_EQ(net_stays(&a.net), 1);

    CASE("--no-stay-alive takes the flag off without dropping anyone");
    int w2 = net_connect(a.net.port);
    CHECK(w2 >= 0);
    CHECK_EQ((int)write(w2, "VTT1\n", 5), 5);
    net_pump(&a.net, 0);
    press(&a, ":serve --no-stay-alive\r");
    CHECK_EQ(net_stays(&a.net), 0);
    CHECK_EQ(net_active(&a.net), 1);
    CHECK_EQ((int)a.net.port, (int)port);
    CHECK(strstr(a.status, "staying up") == NULL);
    press(&a, "q");                               /* the plain close does it too */
    CHECK_EQ(a.map, NULL);
    CHECK_EQ(net_active(&a.net), 0);
    CHECK(strstr(a.status, "remote view off") != NULL);
    close(w2);

    CASE(":e with unsaved work asks, and y discards then opens; n keeps the map");
    CHECK_EQ(app_open_map(&a, path), 0);                   /* build mode, where space edits ground */
    a.ed.cx = a.ed.cy = 0;
    press(&a, " ");                                        /* an unsaved change */
    CHECK_EQ(a.map->modified, 1);
    char e_other[700];
    snprintf(e_other, sizeof e_other, ":e %s\r", other);
    press(&a, e_other);
    CHECK_EQ(a.modal, MODAL_CONFIRM_DISCARD);
    CHECK(strstr(a.modal_body, "open the other map") != NULL);
    press(&a, "n");
    CHECK_EQ(a.modal, MODAL_NONE);
    CHECK_EQ(strcmp(a.map->path, path), 0);                /* still the first map, still unsaved */
    CHECK_EQ(a.map->modified, 1);
    press(&a, e_other);
    press(&a, "y");
    CHECK_EQ(a.modal, MODAL_NONE);
    CHECK_EQ(strcmp(a.map->path, other), 0);
    CHECK_EQ(a.map->modified, 0);
    CHECK_EQ(a.screen, SCREEN_EDITOR);
    app_key(&a, f2);
    press(&a, ":q!\r");
    CHECK_EQ(app_open_map(&a, path), 0);
    app_key(&a, f2);

    CASE("switching maps with :e is not a close, so the players keep watching");
    CHECK_EQ(app_open_map(&a, path), 0);
    app_key(&a, f2);
    press(&a, ":serve\r");
    CHECK_EQ(net_active(&a.net), 1);
    uint16_t kept = a.net.port;
    char open_other[700];
    snprintf(open_other, sizeof open_other, ":e %s\r", other);
    press(&a, open_other);
    CHECK(a.map != NULL);
    CHECK_EQ(net_active(&a.net), 1);
    CHECK_EQ((int)a.net.port, (int)kept);

    CASE("a saved close says both what was written and what went down");
    app_key(&a, f2);
    press(&a, ":wq\r");
    CHECK_EQ(a.map, NULL);
    CHECK_EQ(net_active(&a.net), 0);
    CHECK(strstr(a.status, "wrote") != NULL);
    CHECK(strstr(a.status, "remote view off") != NULL);

    CASE("nonsense arguments are refused, and refuse nothing else");
    CHECK_EQ(app_open_map(&a, path), 0);
    app_key(&a, f2);
    press(&a, ":serve --stay\r");
    CHECK(strstr(a.status, ":serve [PORT]") != NULL);
    CHECK_EQ(net_active(&a.net), 0);
    press(&a, ":serve off --stay-alive\r");
    CHECK(strstr(a.status, ":serve [PORT]") != NULL);
    press(&a, ":serve 99999\r");
    CHECK(strstr(a.status, ":serve [PORT]") != NULL);
    CHECK_EQ(net_active(&a.net), 0);

    CASE("a port and the flag together, in either order");
    press(&a, ":serve 0 --stay-alive\r");
    CHECK_EQ(net_active(&a.net), 1);
    CHECK_EQ(net_stays(&a.net), 1);
    press(&a, ":serve off\r");
    press(&a, ":serve --stay-alive 0\r");
    CHECK_EQ(net_active(&a.net), 1);
    CHECK_EQ(net_stays(&a.net), 1);

    CASE("a restart on another port starts again without the flag");
    press(&a, ":serve 0\r");
    CHECK_EQ(net_active(&a.net), 1);
    CHECK_EQ(net_stays(&a.net), 0);

    CASE("quitting the application takes it down whatever the flag says");
    press(&a, ":serve --stay-alive\r");
    CHECK_EQ(net_stays(&a.net), 1);
    app_free(&a);
    CHECK_EQ(net_active(&a.net), 0);

    rnd_free(&r);
    sandbox_leave(&sb);
}

/* The players' frame: what the clients receive is a second renderer's diff,
 * drawn the players' way when it could differ from the GM's and copied from
 * the GM's when it cannot. */
void test_players_frame(void)
{
    Sandbox sb = sandbox_enter("pframe");
    CHECK_EQ(sb.ok, 1);
    if (!sb.ok) return;

    write_map_file(sb.dir, "fight.vtt");
    char path[600];
    snprintf(path, sizeof path, "%s/fight.vtt", sb.dir);

    Renderer r;
    App      a;
    rnd_init(&r);
    rnd_resize(&r, 80, 24);
    app_init(&a, NULL, &r);
    CHECK_EQ(app_open_map(&a, path), 0);
    Key f2 = { KEY_F2, 0, 0 };
    app_key(&a, f2);
    a.ed.cx = a.ed.cy = 0;
    press(&a, "ipAria\r");

    press(&a, ":serve\r");
    CHECK_EQ(net_active(&a.net), 1);
    int w = net_connect(a.net.port);
    CHECK(w >= 0);
    CHECK_EQ((int)write(w, "VTT1\n", 5), 5);
    net_pump(&a.net, 0);
    WireCatch c;
    memset(&c, 0, sizeof c);
    WireDec d;
    wire_dec_init(&d, &WC_SINK, &c);

    CASE("with nothing GM-only on screen the two frames are the same bytes, and nothing is drawn twice");
    CHECK_EQ(app_view_differs(&a), 0);
    app_frame(&a, NULL, 0);
    CHECK_EQ(net_recv_until(&a.net, w, &d, &c, 1, 0), 0);
    const Renderer *pr = &a.net.players;
    CHECK_EQ(pr->w, r.w);
    CHECK_EQ(memcmp(pr->front, r.front, r.ncells * sizeof(Cell)), 0);
    CHECK_EQ(a.view, VIEW_GM);                      /* the last draw was the GM's: nothing drawn twice */

    CASE("a note prompt is in the GM's frame and not in the players'");
    press(&a, "sn");
    CHECK_EQ(app_view_differs(&a), 1);
    app_frame(&a, NULL, 0);
    CHECK_EQ(net_recv_until(&a.net, w, &d, &c, 2, 0), 0);
    CHECK_EQ(a.view, VIEW_PLAYERS);                 /* the last draw was the players' */
    ByteBuf gm, pl;
    bb_init(&gm, 65536); front_text(&r, &gm); bb_putc(&gm, '\0');
    bb_init(&pl, 65536); front_text(pr, &pl); bb_putc(&pl, '\0');
    CHECK(strstr(gm.data, "note on Aria") != NULL);
    CHECK(strstr(pl.data, "note on Aria") == NULL);
    CHECK(strstr(pl.data, "PLAY") != NULL);          /* still a play frame */
    bb_free(&gm); bb_free(&pl);
    press(&a, "the amulet\r");

    CASE("the (note) hint is the GM's alone, and the client's picture says so");
    app_frame(&a, NULL, 0);
    CHECK_EQ(net_recv_until(&a.net, w, &d, &c, 3, 0), 0);
    bb_init(&gm, 65536); front_text(&r, &gm); bb_putc(&gm, '\0');
    bb_init(&pl, 65536); front_text(pr, &pl); bb_putc(&pl, '\0');
    CHECK(strstr(gm.data, "(note)") != NULL);
    CHECK(strstr(pl.data, "(note)") == NULL);
    /* and what the watcher decoded is the players' frame, cell for cell */
    int same = 1;                                     /* the catch keeps 64x32 of it */
    for (int y = 0; y < c.h && y < 32 && same; y++)
        for (int x = 0; x < c.w && x < 64; x++)
            if (c.grid[y * 64 + x].ch != pr->front[(size_t)y * (size_t)pr->w + (size_t)x].ch) { same = 0; break; }
    CHECK_EQ(same, 1);
    bb_free(&gm); bb_free(&pl);

    CASE("the profiler overlay never reaches a phone");
    press(&a, "\x1b");                                /* deselect: no note in view */
    a.ed.cx = 1; a.ed.cy = 1;
    CHECK_EQ(app_view_differs(&a), 0);
    Key f12 = { KEY_F12, 0, 0 };
    app_key(&a, f12);
    CHECK_EQ(app_view_differs(&a), 1);
    app_frame(&a, NULL, 0);
    CHECK_EQ(net_recv_until(&a.net, w, &d, &c, 4, 0), 0);
    bb_init(&pl, 65536); front_text(pr, &pl); bb_putc(&pl, '\0');
    CHECK(strstr(pl.data, "frame") == NULL);         /* the overlay's own word */
    bb_free(&pl);
    app_key(&a, f12);

    CASE(":player preview shows the GM the players' frame; q comes back, and does not close the map");
    press(&a, "t");                                   /* select Aria, who has a note */
    press(&a, ":player preview\r");
    CHECK_EQ(a.preview, 1);
    CHECK(strstr(a.status, "previewing") != NULL);
    rnd_begin(&r); app_draw(&a);
    CHECK_EQ(a.view, VIEW_PLAYERS);
    bb_init(&gm, 65536); rnd_dump(&r, &gm); bb_putc(&gm, '\0');
    CHECK(strstr(gm.data, "(note)") == NULL);
    bb_free(&gm);
    CHECK_EQ(app_view_differs(&a), 0);                /* both frames are the players' now */
    press(&a, "q");
    CHECK_EQ(a.preview, 0);
    CHECK(a.map != NULL);
    rnd_begin(&r); app_draw(&a);
    CHECK_EQ(a.view, VIEW_GM);
    press(&a, ":player preview\r");
    press(&a, ":player preview\r");                   /* a second one toggles it off */
    CHECK_EQ(a.preview, 0);
    press(&a, ":player\r");
    CHECK(strstr(a.status, ":player preview") != NULL);

    CASE("the players' renderer follows the terminal's size, and a resize resyncs the client whole");
    rnd_resize(&r, 100, 30);
    press(&a, "\x1b");
    a.ed.cx = 1; a.ed.cy = 1;
    int fulls = c.fulls;
    app_frame(&a, NULL, 0);
    CHECK_EQ(net_recv_until(&a.net, w, &d, &c, 5, 0), 0);
    CHECK_EQ(a.net.players.w, 100);
    CHECK_EQ(c.w, 100);
    CHECK_EQ(c.fulls, fulls + 1);

    CASE("out of play mode nothing is sent, and no players' frame is drawn");
    Key f1 = { KEY_F1, 0, 0 };
    app_key(&a, f1);
    uint64_t before = a.net.total_bytes;
    app_frame(&a, NULL, 0);
    CHECK_EQ((int)(a.net.total_bytes - before), 0);
    CHECK_EQ(a.view, VIEW_GM);

    close(w);
    app_free(&a);
    rnd_free(&r);
    sandbox_leave(&sb);
}

/* The ring round tile (tx,ty): true when the cell just north-west of its
 * interior -- a corner of the ring -- has the ping's color. */
static int ring_at(const Renderer *r, const App *a, int tx, int ty)
{
    int sx, sy;
    grid_tile_interior(&a->ed.view, tx, ty, &sx, &sy);
    const Cell *c = rnd_at((Renderer *)r, sx - 1, sy - 1);
    return c && c->bg == a->th->ping_bg;
}

/* Pings: a phone's tap or the GM's g p rings a square on every screen. */
void test_pings(void)
{
    Sandbox sb = sandbox_enter("pings");
    CHECK_EQ(sb.ok, 1);
    if (!sb.ok) return;
    write_sight_map(sb.dir, "p.vtt", 2, 1);
    char path[600];
    snprintf(path, sizeof path, "%s/p.vtt", sb.dir);

    Renderer r;
    App      a;
    rnd_init(&r);
    rnd_resize(&r, 80, 16);
    app_init(&a, NULL, &r);
    CHECK_EQ(app_open_map(&a, path), 0);
    Map *m = a.map;
    Key f1 = { KEY_F1, 0, 0 }, f2 = { KEY_F2, 0, 0 };
    app_key(&a, f2);
    press(&a, ":fog off\r");
    uint64_t now = 10000;
    app_tick(&a, now);
    rnd_begin(&r); app_draw(&a);                            /* lay the view out */

    CASE("a phone's tap on a square rings it, says where, and is taken down two seconds later");
    int sx, sy;
    grid_tile_interior(&a.ed.view, 4, 2, &sx, &sy);
    CHECK_EQ(app_ping_cell(&a, 7, sx, sy), 1);
    CHECK_EQ(a.npings, 1);
    CHECK_EQ(a.pings[0].x0, 4);
    CHECK_EQ(a.pings[0].y0, 2);
    CHECK_EQ(a.pings[0].who, 7u);
    CHECK(strstr(a.status, "ping at E3") != NULL);
    CHECK_EQ(app_ping_due(&a, now), PING_SHOW_MS);
    rnd_begin(&r); app_draw(&a);
    CHECK(ring_at(&r, &a, 4, 2));
    CHECK(!ring_at(&r, &a, 6, 2));

    CASE("without fog the players' frame can be the GM's copied: a ring changes nothing there");
    CHECK_EQ(app_view_differs(&a), 0);
    rnd_begin(&r); app_draw_view(&a, VIEW_PLAYERS);
    CHECK(ring_at(&r, &a, 4, 2));

    CASE("expiry: due counts down, and at two seconds the ring is gone");
    CHECK_EQ(app_ping_due(&a, now + 1999), 1);
    app_tick(&a, now + 1999);
    CHECK_EQ(a.npings, 1);
    a.dirty = 0;
    app_tick(&a, now + 2000);
    CHECK_EQ(a.npings, 0);
    CHECK_EQ(a.dirty, 1);
    CHECK_EQ(app_ping_due(&a, now + 2000), -1);
    rnd_begin(&r); app_draw(&a);
    CHECK(!ring_at(&r, &a, 4, 2));
    now += 3000;
    app_tick(&a, now);

    CASE("a tap off the map -- the gutter, the title, the status line, the bar -- names nothing");
    CHECK_EQ(app_ping_cell(&a, 7, 0, sy), 0);               /* the row labels */
    CHECK_EQ(app_ping_cell(&a, 7, sx, 0), 0);               /* the title bar */
    CHECK_EQ(app_ping_cell(&a, 7, sx, r.h - 2), 0);         /* the status line */
    CHECK_EQ(app_ping_cell(&a, 7, sx, r.h - 1), 0);         /* the key bar */
    CHECK_EQ(app_ping_cell(&a, 7, r.w - 1, sy), 0);         /* past the map's east edge */
    CHECK_EQ(a.npings, 0);

    CASE("the same phone moves its ring; another phone adds one; the GM's is its own");
    app_ping_cell(&a, 7, sx, sy);
    grid_tile_interior(&a.ed.view, 1, 1, &sx, &sy);
    app_ping_cell(&a, 7, sx, sy);
    CHECK_EQ(a.npings, 1);
    CHECK_EQ(a.pings[0].x0, 1);
    app_ping_cell(&a, 8, sx, sy);
    CHECK_EQ(a.npings, 2);
    a.ed.cx = 9; a.ed.cy = 3;
    press(&a, "gp");
    CHECK_EQ(a.npings, 3);
    CHECK(strstr(a.status, "ping at J4") != NULL);
    press(&a, "lgp");
    CHECK_EQ(a.npings, 3);
    int gm = -1;
    for (int i = 0; i < a.npings; i++) if (a.pings[i].who == PING_GM) gm = i;
    CHECK(gm >= 0);
    CHECK_EQ(a.pings[gm].x0, 10);

    CASE("the GM's ping rings the cursor's footprint, or the box");
    press(&a, "2b");
    a.ed.cx = 1; a.ed.cy = 1;
    press(&a, "gp");
    CHECK_EQ(a.pings[gm].x1 - a.pings[gm].x0, 1);
    CHECK_EQ(a.pings[gm].y1 - a.pings[gm].y0, 1);
    CHECK(strstr(a.status, "ping at B2-C3") != NULL);
    press(&a, "1b");
    a.ed.cx = 2; a.ed.cy = 0;
    press(&a, "vlljgp");
    CHECK_EQ(a.pings[gm].x0, 2);
    CHECK_EQ(a.pings[gm].x1, 4);
    CHECK_EQ(a.pings[gm].y1, 1);
    CHECK_EQ(a.play.visual, 0);

    CASE("in build mode g p is not a ping, and the rings wait for play");
    int before = a.npings;
    app_key(&a, f1);
    press(&a, "gp");
    CHECK_EQ(a.npings, before);
    rnd_begin(&r); app_draw(&a);
    CHECK(!ring_at(&r, &a, 2, 0));
    CHECK_EQ(app_ping_cell(&a, 7, sx, sy), 0);              /* the phones' frame is play mode's */
    app_key(&a, f2);

    CASE("fog: a ping into the dark rings for the GM, not for the players, and says nothing to them");
    now += 5000;
    app_tick(&a, now);
    CHECK_EQ(a.npings, 0);
    press(&a, ":fog on\r");
    a.ed.cx = 9; a.ed.cy = 3;                                /* nobody lights it */
    CHECK_EQ(fog_ground_hidden(m, 9, 3), 1);
    press(&a, "gp");
    CHECK_EQ(app_view_differs(&a), 1);
    rnd_begin(&r); app_draw_view(&a, VIEW_GM);
    CHECK(ring_at(&r, &a, 9, 3));
    rnd_begin(&r); app_draw_view(&a, VIEW_PLAYERS);
    CHECK(!ring_at(&r, &a, 9, 3));
    ByteBuf fr;
    bb_init(&fr, 65536); rnd_dump(&r, &fr); bb_putc(&fr, '\0');
    CHECK(strstr(fr.data, "ping") == NULL);
    bb_free(&fr);
    int amber = 0;
    for (size_t i = 0; i < (size_t)r.w * (size_t)r.h; i++) amber += r.back[i].bg == a.th->ping_bg;
    CHECK_EQ(amber, 0);

    CASE("a box half in the light is ringed only round the part the players can see");
    a.ed.cx = 1; a.ed.cy = 1;
    press(&a, "ipAria\r\x1b");                              /* she lights x 0..3 */
    a.ed.cx = 2; a.ed.cy = 1;
    press(&a, "vllllgp");                                   /* 2..6: 5 and 6 are dark */
    CHECK_EQ(fog_ground_hidden(m, 2, 1), 0);
    CHECK_EQ(fog_ground_hidden(m, 6, 1), 1);
    rnd_begin(&r); app_draw_view(&a, VIEW_PLAYERS);
    CHECK(ring_at(&r, &a, 2, 1));
    int tx6, ty6;
    grid_tile_interior(&a.ed.view, 6, 1, &tx6, &ty6);
    CHECK(rnd_at(&r, tx6, ty6 - 1)->bg != a.th->ping_bg);   /* the ring's top over a dark square */
    rnd_begin(&r); app_draw_view(&a, VIEW_GM);
    CHECK(rnd_at(&r, tx6, ty6 - 1)->bg == a.th->ping_bg);

    CASE("the server's taps come through app_tick; :serve --no-pings stops them and says so");
    press(&a, ":fog off\r");
    now += 5000;
    app_tick(&a, now);
    press(&a, ":serve 0\r");
    CHECK(net_active(&a.net));
    int w = net_connect(a.net.port);
    CHECK(w >= 0);
    CHECK(write(w, "VTT1\n", 5) == 5);
    for (int i = 0; i < 10; i++) net_pump(&a.net, now);
    grid_tile_interior(&a.ed.view, 3, 3, &sx, &sy);
    char line[32];
    snprintf(line, sizeof line, "P %d %d\n", sx, sy);
    CHECK(write(w, line, strlen(line)) == (ssize_t)strlen(line));
    for (int i = 0; i < 20 && a.npings == 0; i++) { net_pump(&a.net, now); app_tick(&a, now); }
    CHECK_EQ(a.npings, 1);
    CHECK(a.pings[0].who != PING_GM);
    CHECK_EQ(a.pings[0].x0, 3);
    press(&a, ":serve --no-pings\r");
    CHECK(strstr(a.status, "pings off") != NULL);
    CHECK_EQ(net_pings_on(&a.net), 0);
    press(&a, "gp");                                        /* the GM's own is not a phone's */
    CHECK_EQ(a.npings, 2);
    press(&a, ":serve --pings\r");
    CHECK_EQ(net_pings_on(&a.net), 1);
    close(w);

    CASE("a phone's tap into the dark sends the phones nothing at all");
    press(&a, ":fog on\r");
    app_frame(&a, NULL, now);
    app_frame(&a, NULL, now);
    CHECK_EQ(fog_ground_hidden(m, 9, 3), 1);
    grid_tile_interior(&a.ed.view, 9, 3, &sx, &sy);
    now += 2000;
    snprintf(line, sizeof line, "P %d %d\n", sx, sy);
    int w2 = net_connect(a.net.port);
    CHECK(w2 >= 0);
    CHECK(write(w2, "VTT1\n", 5) == 5);
    for (int i = 0; i < 10; i++) net_pump(&a.net, now);
    app_tick(&a, now);                                      /* earlier rings come down first */
    app_frame(&a, NULL, now);
    app_frame(&a, NULL, now);
    CHECK(write(w2, line, strlen(line)) == (ssize_t)strlen(line));
    int dark = 0;
    for (int i = 0; i < 20 && !dark; i++) {
        net_pump(&a.net, now);
        app_tick(&a, now);
        for (int j = 0; j < a.npings; j++)
            dark |= a.pings[j].who != PING_GM && a.pings[j].x0 == 9 && a.pings[j].y0 == 3;
    }
    CHECK(dark);
    app_frame(&a, NULL, now);
    CHECK_EQ(a.net.frame_bytes, 0u);                        /* the GM's frame changed; theirs did not */
    close(w2);
    press(&a, ":fog off\r");

    CASE("with every slot taken, a new source takes the ring closest to going");
    now += 10000;
    app_tick(&a, now);
    for (uint32_t i = 1; i <= PING_MAX; i++) { a.now_ms = now + i; app_ping(&a, 100 + i, 1, 1, 1, 1); }
    CHECK_EQ(a.npings, PING_MAX);
    a.now_ms = now + 50;
    app_ping(&a, 999, 2, 2, 2, 2);
    CHECK_EQ(a.npings, PING_MAX);
    int evicted = 1;
    for (int i = 0; i < a.npings; i++) if (a.pings[i].who == 101) evicted = 0;
    CHECK(evicted);

    CASE("closing the map, or opening another, takes every ring down");
    press(&a, ":q!\r");                                    /* app_close_map */
    CHECK(a.map == NULL);
    CHECK_EQ(a.npings, 0);
    CHECK_EQ(app_open_map(&a, path), 0);
    app_key(&a, f2);
    press(&a, "gp");
    CHECK_EQ(a.npings, 1);
    CHECK_EQ(app_open_map(&a, path), 0);
    CHECK_EQ(a.npings, 0);

    app_free(&a);
    rnd_free(&r);
    sandbox_leave(&sb);
}

void test_serve_commands(void)
{
    Sandbox sb = sandbox_enter("serve");
    CHECK_EQ(sb.ok, 1);
    if (!sb.ok) return;

    write_map_file(sb.dir, "fight.vtt");
    char path[600];
    snprintf(path, sizeof path, "%s/fight.vtt", sb.dir);

    Renderer r;
    App      a;
    rnd_init(&r);
    rnd_resize(&r, 80, 24);
    app_init(&a, NULL, &r);
    CHECK_EQ(app_open_map(&a, path), 0);
    Key f2 = { KEY_F2, 0, 0 };
    app_key(&a, f2);

    CASE(":serve opens the remote view and says where");
    CHECK_EQ(net_active(&a.net), 0);
    press(&a, ":serve\r");
    CHECK_EQ(net_active(&a.net), 1);
    CHECK(strstr(a.status, "serving at http://") != NULL);
    CHECK(strstr(a.status, "/?k=") != NULL);
    press(&a, ":serve\r");
    CHECK(strstr(a.status, "0 clients") != NULL);

    CASE("a watcher that connects is counted, and sees the play frame");
    int w = net_connect(a.net.port);
    CHECK(w >= 0);
    CHECK_EQ((int)write(w, "VTT1\n", 5), 5);
    WireCatch c;
    memset(&c, 0, sizeof c);
    WireDec d;
    wire_dec_init(&d, &WC_SINK, &c);
    /* the app draws its frame through the same hooks main uses */
    app_frame(&a, NULL, 0);
    CHECK_EQ(net_recv_until(&a.net, w, &d, &c, 1, 0), 0);
    CHECK_EQ(c.w, 80);
    press(&a, ":serve\r");
    CHECK(strstr(a.status, "1 client") != NULL);

    CASE("leaving play mode freezes the mirror; coming back sends it whole");
    Key f1 = { KEY_F1, 0, 0 };
    app_key(&a, f1);
    app_frame(&a, NULL, 0);
    uint64_t before = a.net.total_bytes;
    CHECK_EQ((int)(a.net.total_bytes - before), 0);
    app_key(&a, f2);
    app_frame(&a, NULL, 0);
    CHECK_EQ(net_recv_until(&a.net, w, &d, &c, 2, 0), 0);
    CHECK_EQ(c.fulls, 2);

    CASE(":mirror with no terminal to open says so, and how to do it by hand");
    const char *had_term = getenv("TERMINAL");
    char saved_term[512] = "";
    if (had_term) str_lcpy(saved_term, had_term, sizeof saved_term);
    const char *had_path = getenv("PATH");
    char saved_path[2048] = "";
    if (had_path) str_lcpy(saved_path, had_path, sizeof saved_path);
    setenv("TERMINAL", "/nonexistent/terminal", 1);
    setenv("PATH", "/nonexistent", 1);
    press(&a, ":mirror\r");
    CHECK(strstr(a.status, "no terminal found") != NULL);
    CHECK(strstr(a.status, "--watch 127.0.0.1:") != NULL);
    if (had_term) setenv("TERMINAL", saved_term, 1); else unsetenv("TERMINAL");
    if (had_path) setenv("PATH", saved_path, 1);

    CASE(":serve off drops everyone");
    press(&a, ":serve off\r");
    CHECK_EQ(net_active(&a.net), 0);
    CHECK(strstr(a.status, "1 client dropped") != NULL);
    uint8_t z;
    struct timeval tv = { 1, 0 };
    setsockopt(w, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    ssize_t got;
    do got = read(w, &z, 1); while (got > 0);
    CHECK_EQ((int)got, 0);                              /* the server closed it */
    close(w);
    press(&a, ":serve off\r");
    CHECK(strstr(a.status, "not on") != NULL);

    CASE(":mirror starts the server itself when it has to");
    setenv("TERMINAL", "/nonexistent/terminal", 1);
    setenv("PATH", "/nonexistent", 1);
    press(&a, ":mirror\r");
    CHECK_EQ(net_active(&a.net), 1);
    if (had_term) setenv("TERMINAL", saved_term, 1); else unsetenv("TERMINAL");
    if (had_path) setenv("PATH", saved_path, 1);

    app_free(&a);
    rnd_free(&r);
    sandbox_leave(&sb);
}

void test_webpage(void)
{
    extern const char   WEBPAGE[];
    extern const size_t WEBPAGE_LEN;

    CASE("the page is one request under 12 KB, with nothing fetched from anywhere");
    CHECK((int)WEBPAGE_LEN < 12288);
    CHECK(strstr(WEBPAGE, "<!doctype html") != NULL);
    CHECK(strstr(WEBPAGE, "new WebSocket(") != NULL);
    CHECK(strstr(WEBPAGE, "WebAssembly.Module") != NULL);
    CHECK(strstr(WEBPAGE, "src=\"http") == NULL);
    CHECK(strstr(WEBPAGE, "href=\"http") == NULL);
    CHECK(strstr(WEBPAGE, "@import") == NULL);

    CASE("a tap sends the cell under it, a line the server reads as a ping");
    CHECK(strstr(WEBPAGE, "addEventListener('click'") != NULL);
    CHECK(strstr(WEBPAGE, "ws.send('P '+x+' '+y)") != NULL);

    CASE("the embedded wasm module is the one tools/blit_wasm.py assembles");
    const char *w = strstr(WEBPAGE, "const WASM='");
    CHECK(w != NULL);
    if (w) CHECK(strncmp(w + 12, "AGFzbQEAAAAB", 12) == 0);   /* \0asm, version 1, a type section */
}

/* ---------------------------------------------------------------- handouts */

/* Reads until the client has seen `want` handout records. */
static int recv_handouts(Net *n, int fd, WireDec *d, WireCatch *c, int want, uint64_t now_ms)
{
    uint8_t buf[8192];
    for (int tries = 0; tries < 50 && c->handouts < want; tries++) {
        net_pump(n, now_ms);
        struct pollfd p = { fd, POLLIN, 0 };
        if (poll(&p, 1, 10) <= 0) continue;
        ssize_t got = read(fd, buf, sizeof buf);
        if (got <= 0) break;
        wire_dec_feed(d, buf, (size_t)got);
    }
    return c->handouts >= want ? 0 : -1;
}

void test_handouts(void)
{
    CASE("wire: an H record carries the text whole, byte by byte too; n 0 takes it down");
    {
        const char *text = "Tomb\nHere lies Aldric.";
        uint8_t rec[3 + WIRE_HANDOUT_MAX];
        size_t  rl = wire_handout(rec, text, strlen(text));
        CHECK_EQ((int)rl, 3 + (int)strlen(text));
        WireCatch c;
        memset(&c, 0, sizeof c);
        WireDec d;
        wire_dec_init(&d, &WC_SINK, &c);
        for (size_t i = 0; i < rl; i++) wire_dec_feed(&d, rec + i, 1);
        CHECK_EQ(c.handouts, 1);
        CHECK(!strcmp(c.handout, text));
        rl = wire_handout(rec, "", 0);
        wire_dec_feed(&d, rec, rl);
        CHECK(c.handouts == 2 && c.handout_n == 0);
        uint8_t bad[3] = { 'H', 0xFF, 0xFF };                /* longer than any handout */
        wire_dec_feed(&d, bad, 3);
        CHECK_EQ(d.bad, 1);
    }

    Renderer r;
    rnd_init(&r);
    rnd_resize(&r, 40, 12);
    rnd_flush(&r, NULL);
    Net n;
    net_init(&n);
    char err[128];
    CHECK_EQ(net_start(&n, 0, &r, err, sizeof err), 0);
    net_set_live(&n, 1);

    CASE("put up, a watcher already there gets it at once, live or not");
    int w1 = net_connect(n.port);
    CHECK_EQ((int)write(w1, "VTT1\n", 5), 5);
    WireCatch c1;
    memset(&c1, 0, sizeof c1);
    WireDec d1;
    wire_dec_init(&d1, &WC_SINK, &c1);
    net_recv_until(&n, w1, &d1, &c1, 1, 0);
    CHECK_EQ(recv_handouts(&n, w1, &d1, &c1, 1, 0), 0);
    CHECK_EQ((int)c1.handout_n, 0);                        /* a FULL is always followed by one, empty */
    net_set_live(&n, 0);
    const char *tomb = "Tomb\nHere lies Aldric.\n\nDo not open the door.";
    net_set_handout(&n, tomb, strlen(tomb), 0);
    CHECK_EQ(recv_handouts(&n, w1, &d1, &c1, 2, 0), 0);
    CHECK(!strcmp(c1.handout, tomb));

    CASE("a watcher that joins while it is up gets it after its FULL");
    net_set_live(&n, 1);
    int w2 = net_connect(n.port);
    CHECK_EQ((int)write(w2, "VTT1\n", 5), 5);
    WireCatch c2;
    memset(&c2, 0, sizeof c2);
    WireDec d2;
    wire_dec_init(&d2, &WC_SINK, &c2);
    CHECK_EQ(recv_handouts(&n, w2, &d2, &c2, 1, 0), 0);
    CHECK(c2.fulls >= 1 && !strcmp(c2.handout, tomb));
    CHECK_EQ(n.joined, 1);                                  /* the app is asked for a fresh frame */

    CASE("taken down: every client gets the empty record, and a newcomer gets none");
    net_set_handout(&n, "", 0, 0);
    CHECK_EQ(recv_handouts(&n, w1, &d1, &c1, 3, 0), 0);
    CHECK_EQ(recv_handouts(&n, w2, &d2, &c2, 2, 0), 0);
    CHECK(c1.handout_n == 0 && c2.handout_n == 0);
    close(w1);
    close(w2);

    CASE("a restarted server keeps the handout for the phones that come back");
    net_set_handout(&n, tomb, strlen(tomb), 0);
    net_stop(&n);
    CHECK_EQ(net_start(&n, 0, &r, err, sizeof err), 0);
    CHECK_EQ((int)n.handout_len, (int)strlen(tomb));
    int w3 = net_connect(n.port);
    CHECK_EQ((int)write(w3, "VTT1\n", 5), 5);
    WireCatch c3;
    memset(&c3, 0, sizeof c3);
    WireDec d3;
    wire_dec_init(&d3, &WC_SINK, &c3);
    CHECK_EQ(recv_handouts(&n, w3, &d3, &c3, 1, 0), 0);
    CHECK(!strcmp(c3.handout, tomb));
    close(w3);

    CASE("a watcher that has not finished its hello -- no join code yet -- is sent no frames");
    net_set_live(&n, 1);
    int half = net_connect(n.port);
    CHECK_EQ((int)write(half, "VTT1", 4), 4);
    for (int k = 0; k < 5; k++) net_pump(&n, 0);
    rnd_begin(&r);
    draw_text(&r, 0, 0, "a frame", -1, style(0xFFFFFF, 0, 0));
    net_frame_begin(&n);
    rnd_flush(&r, NULL);
    net_frame_end(&n, 0);
    net_pump(&n, 0);
    struct pollfd hp = { half, POLLIN, 0 };
    CHECK_EQ(poll(&hp, 1, 50), 0);                          /* nothing arrived */
    close(half);
    net_stop(&n);
    rnd_free(&r);
}

/* A phone joining before the first players' frame was once sent the GM's own
 * screen. Anything GM-only on it would reach the table. */
void test_join_frame(void)
{
    Sandbox sb = sandbox_enter("joinframe");
    CHECK_EQ(sb.ok, 1);
    if (!sb.ok) return;
    write_map_file(sb.dir, "fight.vtt");
    char path[600];
    snprintf(path, sizeof path, "%s/fight.vtt", sb.dir);
    Renderer r;
    App      a;
    rnd_init(&r);
    rnd_resize(&r, 80, 24);
    app_init(&a, NULL, &r);
    CHECK_EQ(app_open_map(&a, path), 0);
    app_key(&a, (Key){ KEY_F2, 0, 0 });

    CASE("the first FULL a joining watcher gets is the players' picture -- blank before one is drawn -- never the GM's screen");
    press(&a, ":serve\r");
    app_note_gm(&a, "SECRET the ogre is a mimic");
    app_frame(&a, NULL, 0);                                /* the loop draws after every key */
    int w = net_connect(a.net.port);
    CHECK_EQ((int)write(w, "VTT1\n", 5), 5);
    WireCatch c;
    memset(&c, 0, sizeof c);
    WireDec d;
    wire_dec_init(&d, &WC_SINK, &c);
    CHECK_EQ(net_recv_until(&a.net, w, &d, &c, 1, 0), 0);
    int leaked = 0;
    for (int y = 0; y < 24 && y < 32; y++) {
        char row[81];
        for (int x = 0; x < 64; x++) row[x] = c.grid[y * 64 + x].ch < 128 && c.grid[y * 64 + x].ch ? (char)c.grid[y * 64 + x].ch : ' ';
        row[64] = '\0';
        if (strstr(row, "SECRET")) leaked = 1;
    }
    CHECK_EQ(leaked, 0);
    close(w);

    CASE("in build mode a joining watcher gets a blank screen, not the editor");
    app_key(&a, (Key){ KEY_F1, 0, 0 });
    net_stop(&a.net);
    press(&a, ":serve\r");
    app_frame(&a, NULL, 0);
    w = net_connect(a.net.port);
    CHECK_EQ((int)write(w, "VTT1\n", 5), 5);
    memset(&c, 0, sizeof c);
    wire_dec_init(&d, &WC_SINK, &c);
    CHECK_EQ(net_recv_until(&a.net, w, &d, &c, 1, 0), 0);
    int drawn = 0;
    for (int i = 0; i < 64 * 24; i++) drawn |= c.grid[i].ch > ' ';
    CHECK_EQ(drawn, 0);
    close(w);

    app_free(&a);
    rnd_free(&r);
    sandbox_leave(&sb);
}

/* --watch takes the address as :serve shows it, pasted whole, or less. */
void test_watch_target(void)
{
    char host[64], port[8], code[16];
    #define PARSES(in, h, p, c) do {                                              \
        CHECK_EQ(watch_parse_target(in, host, sizeof host, port, sizeof port,    \
                                    code, sizeof code), 0);                      \
        CHECK_EQ(strcmp(host, h), 0);                                            \
        CHECK_EQ(strcmp(port, p), 0);                                            \
        CHECK_EQ(strcmp(code, c), 0);                                            \
    } while (0)

    CASE("the address :serve shows, with or without http://");
    PARSES("http://192.168.1.10:41873/?k=482913", "192.168.1.10", "41873", "482913");
    PARSES("192.168.1.10:41873/?k=482913", "192.168.1.10", "41873", "482913");
    PARSES("192.168.1.10:41873?k=482913", "192.168.1.10", "41873", "482913");

    CASE("the code as a path, or none at all");
    PARSES("table.local:7777/482913", "table.local", "7777", "482913");
    PARSES("table.local:7777", "table.local", "7777", "");

    CASE("no host or no port is refused");
    CHECK_EQ(watch_parse_target(":7777", host, sizeof host, port, sizeof port, code, sizeof code), -1);
    CHECK_EQ(watch_parse_target("table.local", host, sizeof host, port, sizeof port, code, sizeof code), -1);
    CHECK_EQ(watch_parse_target("table.local:", host, sizeof host, port, sizeof port, code, sizeof code), -1);
    CHECK_EQ(watch_parse_target("http://:7777", host, sizeof host, port, sizeof port, code, sizeof code), -1);
    #undef PARSES
}

/* The page decodes the wire with its own code, feed() in web/index.html. It
 * runs under node here (tests/page_feed.js) over a stream the C encoder
 * wrote -- a full frame of a real map in play mode, a diff after some keys,
 * a handout -- and must end with the cells the C decoder ends with. Skipped
 * where there is no node. */
extern const char   WEBPAGE[];
extern const size_t WEBPAGE_LEN;

/* tools/embed.sh's cut, in C: block comments out, then every line left
 * with nothing but white space. */
static char *embed_cut(const char *html)
{
    size_t n = strlen(html);
    char *nc = malloc(n + 1), *out = malloc(n + 1);
    size_t k = 0, o = 0;
    for (size_t i = 0; i < n; i++) {
        if (html[i] == '/' && html[i + 1] == '*') {
            const char *end = strstr(html + i + 2, "*/");
            if (!end) break;
            i = (size_t)(end - html) + 1;
            continue;
        }
        nc[k++] = html[i];
    }
    nc[k] = '\0';
    for (char *line = nc; *line; ) {
        char *nl = strchr(line, '\n');
        size_t len = nl ? (size_t)(nl - line) + 1 : strlen(line);
        int blank = 1;
        for (size_t j = 0; j < len; j++) if (!isspace((unsigned char)line[j])) blank = 0;
        if (!blank) { memcpy(out + o, line, len); o += len; }
        line += len;
    }
    out[o] = '\0';
    free(nc);
    return out;
}

void test_page_feed(void)
{
    CASE("the page served is web/index.html as tools/embed.sh cuts it: webpage.c is in step");
    {
        char *html = slurp("web/index.html");
        CHECK(html != NULL);
        if (html) {
            char *cut = embed_cut(html);
            CHECK_EQ(strlen(cut), WEBPAGE_LEN);
            CHECK(strcmp(cut, WEBPAGE) == 0);       /* else: run tools/embed.sh */
            free(cut);
            free(html);
        }
    }

    if (system("command -v node >/dev/null 2>&1") != 0) {
        /* VTT_REQUIRE_NODE=1 makes a missing node a failure, for a run that
         * must not quietly lose this. */
        CHECK(getenv("VTT_REQUIRE_NODE") == NULL);
        printf("  (skipped: no node to run the page's decoder)\n");
        return;
    }
    Sandbox sb = sandbox_enter("pagefeed");
    CHECK_EQ(sb.ok, 1);
    if (!sb.ok) return;

    enum { W = 60, H = 20 };
    Renderer r;
    App      a;
    rnd_init(&r);
    rnd_resize(&r, W, H);
    app_init(&a, NULL, &r);
    CHECK_EQ(app_open_map(&a, "tests/fixtures/everything.vtt"), 0);
    press(&a, ":play\r");

    WireEnc e;
    wire_enc_init(&e, 1 << 16);
    ByteBuf stream;
    bb_init(&stream, 1 << 16);
    uint8_t pal[2048];

    /* The first frame whole, its palette first, as the server sends it. */
    rnd_begin(&r);
    app_draw(&a);
    rnd_flush(&r, NULL);
    wire_enc_full(&e, &r);
    size_t pn = wire_enc_palette(&e, 0, pal, sizeof pal);
    bb_put(&stream, pal, pn);
    bb_put(&stream, e.buf, e.len);
    int had = e.npal;

    /* Then what changed after some keys: a creature picked up and moved. */
    press(&a, "tt\rll\r");
    rnd_begin(&r);
    app_draw(&a);
    wire_enc_begin(&e);
    rnd_set_observer(&r, wire_observe_cell, &e);
    rnd_flush(&r, NULL);
    rnd_set_observer(&r, NULL, NULL);
    wire_enc_end(&e);
    pn = wire_enc_palette(&e, had, pal, sizeof pal);
    bb_put(&stream, pal, pn);
    bb_put(&stream, e.buf, e.len);
    CHECK(e.cells > 0);

    uint8_t ho[64];
    const char *text = "Tomb\nHere lies \xc3\x89lan";
    size_t hn = wire_handout(ho, text, strlen(text));
    bb_put(&stream, ho, hn);
    hn = wire_text(ho, 'N', "Aria\nBrin", 9);
    bb_put(&stream, ho, hn);
    hn = wire_text(ho, 'W', "The floor is warm", 17);
    bb_put(&stream, ho, hn);

    /* The page as the binary serves it, not the source beside it. */
    char page[640], spath[640], cmd[1600];
    snprintf(page, sizeof page, "%s/page.html", sb.dir);
    FILE *pg = fopen(page, "w");
    if (pg) { fwrite(WEBPAGE, 1, WEBPAGE_LEN, pg); fclose(pg); }
    snprintf(spath, sizeof spath, "%s/stream.bin", sb.dir);
    FILE *f = fopen(spath, "wb");
    if (f) { fwrite(stream.data, 1, stream.len, f); fclose(f); }

    /* What the C decoder makes of the same bytes, in the script's form. */
    WireCatch c;
    memset(&c, 0, sizeof c);
    WireDec d;
    wire_dec_init(&d, &WC_SINK, &c);
    CHECK_EQ((int)wire_dec_feed(&d, (const uint8_t *)stream.data, stream.len), (int)stream.len);
    CHECK_EQ(d.bad, 0);
    ByteBuf want;
    bb_init(&want, 1 << 16);
    char cell[64];
    snprintf(cell, sizeof cell, "%dx%d\n", c.w, c.h);
    bb_puts(&want, cell);
    for (int y = 0; y < c.h; y++) {
        for (int x = 0; x < c.w; x++) {
            const Cell *g = &c.grid[y * 64 + x];
            snprintf(cell, sizeof cell, "%s%u.%06x.%06x.%u", x ? " " : "", (unsigned)g->ch,
                     (unsigned)(g->fg & 0xFFFFFF), (unsigned)(g->bg & 0xFFFFFF), (unsigned)g->attr);
            bb_puts(&want, cell);
        }
        bb_putc(&want, '\n');
    }
    bb_puts(&want, "handout:");
    bb_puts(&want, c.handout);
    bb_puts(&want, "\nnames:");
    bb_puts(&want, c.names);
    bb_puts(&want, "\nwhisper:");
    bb_puts(&want, c.whisper);
    bb_putc(&want, '\n');
    bb_putc(&want, '\0');

    CASE("the page's feed() decodes the stream to the cells the C decoder does");
    snprintf(cmd, sizeof cmd, "node tests/page_feed.js '%s' '%s'", page, spath);
    FILE *pp = popen(cmd, "r");
    CHECK(pp != NULL);
    ByteBuf got;
    bb_init(&got, 1 << 16);
    if (pp) {
        char buf[4096];
        size_t n;
        while ((n = fread(buf, 1, sizeof buf, pp)) > 0) bb_put(&got, buf, n);
        CHECK_EQ(pclose(pp), 0);
    }
    bb_putc(&got, '\0');
    CHECK_EQ(c.w, W);
    CHECK_EQ(c.h, H);
    CHECK(strstr(got.data, "handout:Tomb\nHere lies \xc3\x89lan") != NULL);
    CHECK(strstr(got.data, "names:Aria\nBrin\nwhisper:The floor is warm\n") != NULL);
    CHECK_EQ(strcmp(got.data, want.data), 0);

    bb_free(&got);
    bb_free(&want);
    bb_free(&stream);
    wire_enc_free(&e);
    app_free(&a);
    rnd_free(&r);
    sandbox_leave(&sb);
}

/* Read what is there, waiting up to ms for it; 0 when the other end has
 * closed, -1 when nothing came. */
static int drain_ms(int fd, char *buf, size_t cap, int ms)
{
    struct pollfd p = { fd, POLLIN, 0 };
    if (poll(&p, 1, ms) <= 0) return -1;
    return (int)read(fd, buf, cap);
}

static int drain(int fd, char *buf, size_t cap) { return drain_ms(fd, buf, cap, 20); }

/* The server's edges: what it refuses and what it drops, each checked by
 * what the client sees and by the server's own count. */
void test_net_edges(void)
{
    Renderer r;
    rnd_init(&r);
    rnd_resize(&r, 60, 16);
    rnd_begin(&r);
    draw_text(&r, 1, 1, "the table", -1, style(0xD8D8E0, 0x0E0E12, 0));
    rnd_flush(&r, NULL);
    Net n;
    net_init(&n);
    char err[128], buf[4096];
    uint64_t now = 1000;
    CHECK_EQ(net_start(&n, 0, &r, err, sizeof err), 0);

    CASE("a path it does not serve is a 404");
    {
        int b = net_connect(n.port);
        char req[200];
        snprintf(req, sizeof req, "GET /nothing?k=%s HTTP/1.1\r\nHost: x\r\n\r\n", n.code);
        CHECK_EQ((int)write(b, req, strlen(req)), (int)strlen(req));
        char resp[512] = { 0 };
        size_t rl = 0;
        for (int i = 0; i < 20 && !strstr(resp, "not found"); i++) {
            net_pump(&n, now);
            int got = drain(b, resp + rl, sizeof resp - 1 - rl);
            if (got > 0) rl += (size_t)got;
        }
        CHECK(strncmp(resp, "HTTP/1.1 404", 12) == 0);
        close(b);
        for (int i = 0; i < 5; i++) net_pump(&n, now);
    }

    CASE("a watcher from another machine with the wrong code is closed");
    {
        int b = net_connect(n.port);
        net_pump(&n, now);                                 /* accepted */
        for (int i = 0; i < n.ncl; i++) if (n.cl[i].kind == CL_NEW) n.cl[i].local = 0;
        char hello[32];
        snprintf(hello, sizeof hello, "VTT1%s\n", strcmp(n.code, "000000") ? "000000" : "111111");
        CHECK_EQ((int)write(b, hello, strlen(hello)), (int)strlen(hello));
        int got = -1;
        for (int i = 0; i < 20 && got != 0; i++) { net_pump(&n, now); got = drain(b, buf, sizeof buf); }
        CHECK_EQ(got, 0);                                  /* closed, nothing sent */
        CHECK_EQ(net_clients(&n), 0);
        close(b);
    }

    CASE("a browser's close frame closes it");
    {
        int s = netmsg_ws(&n, now);
        CHECK(s >= 0);
        CHECK_EQ(n.ncl, 1);
        ws_send(s, 0x88, "", 0, 1);
        for (int i = 0; i < 20 && n.ncl; i++) net_pump(&n, now);
        CHECK_EQ(n.ncl, 0);
        close(s);
    }

    CASE("with every slot taken, the ninth connection is turned away and the eight kept");
    int w[NET_MAX_CLIENTS];
    for (int k = 0; k < NET_MAX_CLIENTS; k++) {
        w[k] = net_connect(n.port);
        CHECK_EQ((int)write(w[k], "VTT1\n", 5), 5);
        for (int i = 0; i < 5; i++) net_pump(&n, now);
    }
    CHECK_EQ(n.ncl, NET_MAX_CLIENTS);
    {
        int extra = net_connect(n.port);
        int got = -1;
        for (int i = 0; i < 20 && got != 0; i++) { net_pump(&n, now); got = drain(extra, buf, sizeof buf); }
        CHECK_EQ(got, 0);
        CHECK_EQ(n.ncl, NET_MAX_CLIENTS);
        close(extra);
    }

    CASE("a handout over the cap is cut to it");
    {
        char big[WIRE_HANDOUT_MAX + 500];
        memset(big, 'x', sizeof big);
        net_set_handout(&n, big, sizeof big, now);
        CHECK_EQ(n.handout_len, (size_t)WIRE_HANDOUT_MAX);
        WireCatch c;
        memset(&c, 0, sizeof c);
        WireDec d;
        wire_dec_init(&d, &WC_SINK, &c);
        /* Its greeting's empty handout first, then this one. */
        for (int i = 0; i < 40 && c.handout_n == 0; i++) {
            net_pump(&n, now);
            int got = drain(w[0], buf, sizeof buf);
            if (got > 0) wire_dec_feed(&d, (uint8_t *)buf, (size_t)got);
        }
        CHECK_EQ(c.handout_n, (size_t)WIRE_HANDOUT_MAX);
        net_set_handout(&n, "", 0, now);
    }

    CASE("a watcher that never reads is dropped once its queue is full, and counted; the rest are not");
    {
        uint32_t before = n.dropped;
        net_set_live(&n, 1);
        /* w[7] never reads; the others are read as they go. Every frame
         * changes every cell, so each is a whole screen's worth. The queue
         * overflows only once the kernel's buffers between the two are full,
         * and those are the kernel's size (megabytes): the bound is bytes
         * sent, far past any of them, not a count of frames. */
        uint64_t start = n.total_bytes;
        for (int f = 0; n.dropped == before && n.total_bytes - start < (uint64_t)512 << 20; f++) {
            rnd_begin(&r);
            char line[61];
            for (int y = 0; y < 16; y++) {
                for (int x = 0; x < 60; x++) line[x] = (char)('a' + (f + x + y) % 26);
                line[60] = '\0';
                draw_text(&r, 0, y, line, -1, style(0xD8D8E0, 0x0E0E12, 0));
            }
            net_frame_begin(&n);
            rnd_flush(&r, NULL);
            net_frame_end(&n, now);
            net_pump(&n, now);
            for (int k = 0; k < NET_MAX_CLIENTS - 1; k++) while (drain_ms(w[k], buf, sizeof buf, 0) > 0) { }
        }
        CHECK_EQ(n.dropped, before + 1);
        CHECK_EQ(n.ncl, NET_MAX_CLIENTS - 1);
    }

    for (int k = 0; k < NET_MAX_CLIENTS; k++) close(w[k]);
    net_stop(&n);
    rnd_free(&r);
}

