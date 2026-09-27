/* Tests: the wire format, the server, the map tools and their diagnostics, the players' frame. */

#include "harness.h"

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
const WireSink WC_SINK = { wc_full, wc_pal, wc_run, wc_end, wc_keepalive };

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
    rnd_set_observer(&r, (RndObserver)wire_enc_cell, &e);
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
    rnd_set_observer(&r, (RndObserver)wire_enc_cell, &e);
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

/* ------------------------------------------------------------ map tools */

/* What a map tool printed, as a string the caller frees. */
typedef void (*DumpFn)(FILE *out, const Map *m, int x0, int y0, int x1, int y1);

char *tool_text(const Map *m, int x0, int y0, int x1, int y1, size_t *len)
{
    char  *buf = NULL;
    size_t n   = 0;
    FILE  *f   = open_memstream(&buf, &n);
    maptools_dump(f, m, x0, y0, x1, y1);
    fclose(f);
    if (len) *len = n;
    return buf;
}

/* A small JSON validator: enough to prove what the tools write parses --
 * objects, arrays, strings with escapes, numbers, literals. */
static const char *jv_value(const char *p, int depth);

static const char *jv_ws(const char *p) { while (*p == ' ' || *p == '\n' || *p == '\t' || *p == '\r') p++; return p; }

static const char *jv_string(const char *p)
{
    if (*p++ != '"') return NULL;
    while (*p && *p != '"') {
        if ((unsigned char)*p < 0x20) return NULL;
        if (*p == '\\') {
            p++;
            if (*p == 'u') { for (int i = 1; i <= 4; i++) if (!isxdigit((unsigned char)p[i])) return NULL; p += 5; continue; }
            if (!strchr("\"\\/bfnrt", *p)) return NULL;
        }
        p++;
    }
    return *p == '"' ? p + 1 : NULL;
}

static const char *jv_value(const char *p, int depth)
{
    if (depth > 64) return NULL;
    p = jv_ws(p);
    if (*p == '{' || *p == '[') {
        char close = *p == '{' ? '}' : ']';
        int  obj = *p == '{';
        p = jv_ws(p + 1);
        if (*p == close) return p + 1;
        for (;;) {
            if (obj) {
                p = jv_string(jv_ws(p));
                if (!p) return NULL;
                p = jv_ws(p);
                if (*p++ != ':') return NULL;
            }
            p = jv_value(p, depth + 1);
            if (!p) return NULL;
            p = jv_ws(p);
            if (*p == ',') { p++; continue; }
            return *p == close ? p + 1 : NULL;
        }
    }
    if (*p == '"') return jv_string(p);
    if (!strncmp(p, "true", 4)) return p + 4;
    if (!strncmp(p, "false", 5)) return p + 5;
    if (!strncmp(p, "null", 4)) return p + 4;
    const char *q = p;
    if (*q == '-') q++;
    if (!isdigit((unsigned char)*q)) return NULL;
    while (isdigit((unsigned char)*q) || *q == '.' || *q == 'e' || *q == 'E' || *q == '+' || *q == '-') q++;
    return q;
}

int json_valid(const char *s)
{
    const char *end = jv_value(s, 0);
    return end && *jv_ws(end) == '\0';
}

char *describe_text(const Map *m, int json, size_t *len)
{
    char  *buf = NULL;
    size_t n   = 0;
    FILE  *f   = open_memstream(&buf, &n);
    maptools_describe(f, m, json);
    fclose(f);
    if (len) *len = n;
    return buf;
}

/* A map built by hand: rooms and what joins them. */
static Map *rooms_fixture(void)
{
    /* Three rooms in a row, 3 wide each, walls between: A|B through a door,
     * B|C through a window only. And a sealed D below A, and a void gap. */
    Map *m = map_new(11, 5, "rooms");
    for (int y = 0; y < 5; y++)
        for (int x = 0; x < 11; x++) map_set_tile(m, x, y, x == 3 || x == 7 ? TILE_VOID : TILE_FLOOR);
    map_set_tile(m, 3, 1, TILE_FLOOR);                      /* the door squares across the gaps */
    map_set_tile(m, 7, 1, TILE_FLOOR);
    for (int y = 0; y < 5; y++) { map_set_vedge(m, 3, y, EDGE_WALL); map_set_vedge(m, 4, y, EDGE_WALL); }
    map_set_vedge(m, 3, 1, EDGE_DOOR_CLOSED);               /* A <-> corridor square */
    map_set_vedge(m, 4, 1, EDGE_DOOR_OPEN);                 /* corridor square <-> B */
    for (int y = 0; y < 5; y++) { map_set_vedge(m, 7, y, EDGE_WALL); map_set_vedge(m, 8, y, EDGE_WALL); }
    map_set_vedge(m, 7, 1, EDGE_WINDOW);
    map_set_vedge(m, 8, 1, EDGE_WINDOW);
    for (int x = 0; x < 3; x++) map_set_hedge(m, x, 3, EDGE_WALL);   /* D: rows 3-4 of A's column */
    Token t = { 0 };
    t.x = 1; t.y = 1; t.size = 1; t.kind = TOKEN_PLAYER;
    str_lcpy(t.label, "Aria \"the Bold\"", sizeof t.label);         /* a quote to escape */
    tokens_add(&m->tokens, t);
    return m;
}

void test_map_tools_describe(void)
{
    char err[256];

    CASE("rooms: doors of any kind join them for reaching, windows and walls do not, every door splits");
    Map *m = rooms_fixture();
    Rooms r;
    rooms_build(m, &r);
    /* A (rows 0-2 of x 0-2), the corridor square D2... names in reading order. */
    CHECK_EQ(r.n, 6);
    int a = rooms_at(&r, m, 0, 0), cor1 = rooms_at(&r, m, 3, 1), b = rooms_at(&r, m, 5, 0);
    int cor2 = rooms_at(&r, m, 7, 1), c = rooms_at(&r, m, 9, 0), d = rooms_at(&r, m, 0, 4);
    CHECK(a != cor1 && cor1 != b && b != cor2 && cor2 != c && d != a);
    CHECK_EQ(r.start, a);                                   /* Aria's room */
    CHECK(r.reach[a] && r.reach[cor1] && r.reach[b]);       /* a closed door and an open one */
    CHECK(!r.reach[cor2] && !r.reach[c]);                   /* windows only */
    CHECK(!r.reach[d]);                                     /* sealed by a wall */
    CHECK_EQ(r.v[a].fx, 0);
    CHECK_EQ(r.v[a].squares, 9);
    rooms_free(&r);

    CASE("describe names rooms by their first square and says where each door leads; JSON parses");
    size_t n;
    char *t = describe_text(m, 0, &n);
    CHECK(strstr(t, "room 1 (A1)") != NULL);
    CHECK(strstr(t, "not reachable") != NULL);
    CHECK(strstr(t, "NOT REACHABLE") != NULL);
    CHECK(strstr(t, "door         C2|D2    to room") != NULL);
    free(t);
    t = describe_text(m, 1, &n);
    CHECK(json_valid(t));
    CHECK(strstr(t, "\"label\":\"Aria \\\"the Bold\\\"\"") != NULL);   /* escaped */
    CHECK(strstr(t, "\"reachable\":false") != NULL);
    free(t);
    map_free(m);

    CASE("the goldens: the fixture with every boundary kind, as text and as JSON");
    m = mapio_load("tests/fixtures/kinds.vtt", err, sizeof err);
    CHECK(m != NULL);
    if (m) {
        t = describe_text(m, 0, &n);
        golden_bytes("describe-kinds", t, n);
        free(t);
        t = describe_text(m, 1, &n);
        CHECK(json_valid(t));
        golden_bytes("describe-kinds-json", t, n);
        free(t);
        map_free(m);
    }

    CASE("the worst case: 512x512 with a wall on every boundary is 262,144 rooms, linearly");
    m = map_new(512, 512, "cells");
    for (int y = 0; y < 512; y++)
        for (int x = 0; x < 512; x++) {
            map_set_tile(m, x, y, TILE_FLOOR);
            map_set_vedge(m, x, y, EDGE_WALL);
            map_set_hedge(m, x, y, EDGE_WALL);
        }
    uint64_t t0 = prof_now_ns();
    rooms_build(m, &r);
    uint64_t took = prof_now_ns() - t0;
    CHECK_EQ(r.n, 512 * 512);
    CHECK(took < 2000000000ull);                            /* generous: ASan, a busy machine */
    rooms_free(&r);
    map_free(m);

    CASE("the account of the worst case is written in full");
    m = map_new(512, 512, "cells");
    for (int y = 0; y < 512; y++)
        for (int x = 0; x < 512; x++) {
            map_set_tile(m, x, y, TILE_FLOOR);
            map_set_vedge(m, x, y, EDGE_WALL);
            map_set_hedge(m, x, y, EDGE_WALL);
        }
    t = describe_text(m, 0, &n);
    CHECK(strstr(t, "room 262144 (SR512)") != NULL);
    free(t);
    map_free(m);

    CASE("a door in a stub of wall inside one room is listed once");
    m = map_new(3, 3, "stub");
    for (int y = 0; y < 3; y++) for (int x = 0; x < 3; x++) map_set_tile(m, x, y, TILE_FLOOR);
    map_set_vedge(m, 1, 1, EDGE_DOOR_CLOSED);             /* a door standing alone in the room */
    t = describe_text(m, 0, &n);
    const char *first = strstr(t, "B2|");
    CHECK(first == NULL || strstr(first + 1, "B2|") == NULL);
    const char *door = strstr(t, "A2|B2");
    CHECK(door != NULL && strstr(door + 1, "A2|B2") == NULL);
    free(t);
    map_free(m);

    CASE("one open 512x512 room is one room, filled without recursion");
    m = map_new(512, 512, "open");
    for (int y = 0; y < 512; y++)
        for (int x = 0; x < 512; x++) map_set_tile(m, x, y, TILE_FLOOR);
    rooms_build(m, &r);
    CHECK_EQ(r.n, 1);
    CHECK_EQ(r.v[0].squares, 512 * 512);
    rooms_free(&r);
    map_free(m);
}

static char *check_text(const char *path, int json, int *rc, size_t *len)
{
    char  *buf = NULL;
    size_t n   = 0;
    FILE  *f   = open_memstream(&buf, &n);
    *rc = maptools_check(f, path, json);
    fclose(f);
    if (len) *len = n;
    return buf;
}

void test_map_tools_check(void)
{
    int    rc;
    size_t n;

    CASE("the shipped fixtures are clean: exit 0");
    static const char *const clean[] = { "tests/fixtures/two-rooms.vtt", "tests/fixtures/kinds.vtt",
                                         "tests/fixtures/crowd.vtt" };
    for (int i = 0; i < 3; i++) {
        char *t = check_text(clean[i], 0, &rc, NULL);
        CHECK_EQ(rc, 0);
        CHECK(strstr(t, "no findings") != NULL);
        free(t);
    }

    CASE("the broken fixture: one of every mistake, each found, in a stable order; exit 1");
    char *t = check_text("tests/fixtures/broken.vtt", 0, &rc, &n);
    CHECK_EQ(rc, 1);
    golden_bytes("check-broken", t, n);
    static const char *const codes[] = {
        "E010", "E011", "E013", "E014", "E101", "E110", "E111", "E112", "W015", "W016", "W018", "W019",
        "W020", "W102", "W103", "W104", "W113", "W120", "W121", "W130", "W140", "N021", "N131",
    };
    for (size_t i = 0; i < sizeof codes / sizeof *codes; i++) {
        char want[16];
        snprintf(want, sizeof want, "\n%s ", codes[i]);
        if (!strstr(t, want) && strncmp(t, want + 1, 5) != 0) {
            CHECK(!"a code the broken fixture should raise");
            fprintf(stderr, "    missing %s\n", codes[i]);
        }
    }
    CHECK(strstr(t, "8 errors, 15 warnings, 2 notes") != NULL);
    free(t);

    CASE("--json: the same findings, valid, with coordinates beside the names");
    t = check_text("tests/fixtures/broken.vtt", 1, &rc, &n);
    CHECK_EQ(rc, 1);
    CHECK(json_valid(t));
    CHECK(strstr(t, "\"code\":\"E101\"") != NULL);
    CHECK(strstr(t, "\"edge\":\"v\"") != NULL);
    golden_bytes("check-broken-json", t, n);
    free(t);

    CASE("the dump and the account read the broken map without fault (ASan is the check)");
    {
        char err[256];
        Map *m = mapio_load("tests/fixtures/broken.vtt", err, sizeof err);
        CHECK(m != NULL);
        if (m) {
            FILE *sink = fopen("/dev/null", "w");
            maptools_dump(sink, m, 0, 0, m->w - 1, m->h - 1);
            maptools_describe(sink, m, 0);
            maptools_describe(sink, m, 1);
            fclose(sink);
            map_free(m);
        }
    }

    CASE("rules that must not fire: an outer wall on the map's edge, a door with a wall at one end");
    {
        Sandbox sb = sandbox_enter("checkok");
        char path[600];
        snprintf(path, sizeof path, "%s/ok.vtt", sb.dir);
        FILE *f = fopen(path, "w");
        /* A room walled on the map's edge, an inner wall with a door at its
         * open end (one wall end), a door off the map (a way out), and a
         * disabled fog patch: only notes. */
        fputs("VTT 6\nsize 5 3\ntiles\n.....\n.....\n.....\n"
              "vedges\n|  | |\n|  + +\n|    |\nhedges\n-----\n     \n     \n-----\n"
              "fog on\nfogpatch 1 Mist disabled\nfog\nA....\n.....\n.....\n", f);
        fclose(f);
        t = check_text(path, 0, &rc, NULL);
        CHECK_EQ(rc, 0);                                   /* notes only: clean */
        CHECK(strstr(t, "W103") == NULL && strstr(t, "W102") == NULL && strstr(t, "W104") == NULL);
        CHECK(strstr(t, "N105 door-off-map") != NULL);
        CHECK(strstr(t, "N131") != NULL);
        free(t);
        sandbox_leave(&sb);
    }

    CASE("--region: either order, one square, a row alone, and what is not a square");
    {
        Map *m = map_new(10, 8, "r");
        int x0, y0, x1, y1;
        CHECK_EQ(maptools_region(m, "C4:B2", &x0, &y0, &x1, &y1), 1);
        CHECK(x0 == 1 && y0 == 1 && x1 == 2 && y1 == 3);
        CHECK_EQ(maptools_region(m, "d5", &x0, &y0, &x1, &y1), 1);
        CHECK(x0 == 3 && x1 == 3 && y0 == 4 && y1 == 4);
        CHECK_EQ(maptools_region(m, "5:6", &x0, &y0, &x1, &y1), 1);
        CHECK(x0 == 0 && x1 == 9 && y0 == 4 && y1 == 5);
        CHECK_EQ(maptools_region(m, "B2:", &x0, &y0, &x1, &y1), 0);
        CHECK_EQ(maptools_region(m, "hello", &x0, &y0, &x1, &y1), 0);
        CHECK_EQ(maptools_region(m, ":B2", &x0, &y0, &x1, &y1), 0);
        map_free(m);
    }

    CASE("a file that is not a map: E001 and exit 2");
    t = check_text("/nonexistent/map.vtt", 0, &rc, NULL);
    CHECK_EQ(rc, 2);
    CHECK(strncmp(t, "E001 unreadable", 15) == 0);
    free(t);
    t = check_text("/nonexistent/map.vtt", 1, &rc, NULL);
    CHECK_EQ(rc, 2);
    CHECK(json_valid(t));
    free(t);
}

void test_map_tools_dump(void)
{
    char err[256];

    CASE("the dump: whole fixtures and a region, as the goldens have them");
    static const struct { const char *file, *golden; int x0, y0, x1, y1; } cases[] = {
        { "tests/fixtures/two-rooms.vtt", "dump-two-rooms", 0, 0, 99, 99 },
        { "tests/fixtures/kinds.vtt",     "dump-kinds",     0, 0, 99, 99 },
        { "tests/fixtures/two-rooms.vtt", "dump-region",    1, 1, 6, 5 },
    };
    for (size_t i = 0; i < sizeof cases / sizeof *cases; i++) {
        Map *m = mapio_load(cases[i].file, err, sizeof err);
        CHECK(m != NULL);
        if (!m) continue;
        size_t n;
        char *t = tool_text(m, cases[i].x0, cases[i].y0, cases[i].x1, cases[i].y1, &n);
        golden_bytes(cases[i].golden, t, n);
        CHECK(strstr(t, " \n") == NULL);                    /* no trailing blanks */
        free(t);
        map_free(m);
    }

    CASE("fog, notes and a creature's note get their own sections");
    {
        Sandbox sb = sandbox_enter("dumpfog");
        CHECK_EQ(sb.ok, 1);
        char path[600];
        snprintf(path, sizeof path, "%s/f.vtt", sb.dir);
        FILE *f = fopen(path, "w");
        fputs("VTT 6\nname Cellar\nsize 5 3\nscale 5\nmetric chebyshev\ntiles\n.....\n.~~..\n.....\n"
              "vedges\n|  S |\n|  | |\n|  + |\nhedges\n-----\n     \n     \n-----\n"
              "token player 0 0 1 \"Aria\"\ntokennote \"wants the amulet\"\n"
              "token enemy 3 1 2 \"Ogre\"\nnote 1 2 \"pressure plate\"\n"
              "fog on\nfogpatch 1 Cellar reveal 2 memory on\nfogpatch 2 Pit reveal manual memory off\n"
              "fog\n..AAA\n.aAAB\n..1BB\n", f);
        fclose(f);
        Map *m = mapio_load(path, err, sizeof err);
        CHECK(m != NULL);
        if (m) {
            size_t n;
            char *t = tool_text(m, 0, 0, 99, 99, &n);
            golden_bytes("dump-fog", t, n);
            CHECK(strstr(t, "wants the amulet") != NULL);
            CHECK(strstr(t, "pressure plate") != NULL);
            CHECK(strstr(t, "reveal manual") != NULL);
            free(t);
            map_free(m);
        }
        sandbox_leave(&sb);
    }

    CASE("past Z the columns take two header rows, and every one is labeled");
    {
        Map *m = map_new(30, 2, "wide");
        char *t = tool_text(m, 0, 0, 99, 99, NULL);
        const char *nl = strchr(t, '\n');
        CHECK(nl != NULL);
        if (nl) {
            char first[256];
            size_t fl = (size_t)(nl - t) < sizeof first - 1 ? (size_t)(nl - t) : sizeof first - 1;
            memcpy(first, t, fl);
            first[fl] = '\0';
            CHECK(strstr(first, "A A A A") != NULL);                      /* AA..AD's first letters */
            CHECK(strstr(nl, "Y Z A B C D") != NULL);                     /* ...and their last */
        }
        free(t);
        map_free(m);
    }
}

/* ------------------------------------------------------------ map diagnostics */

typedef struct { char codes[64][8]; int lines[64]; int n; } DiagLog;

static void diag_collect(void *ctx, int line, int col, const char *code, const char *slug, const char *msg)
{
    (void)col; (void)slug; (void)msg;
    DiagLog *d = ctx;
    if (d->n < 64) { str_lcpy(d->codes[d->n], code, sizeof d->codes[0]); d->lines[d->n] = line; d->n++; }
}

static int diag_has(const DiagLog *d, const char *code)
{
    for (int i = 0; i < d->n; i++) if (!strcmp(d->codes[i], code)) return 1;
    return 0;
}

static int diag_line(const DiagLog *d, const char *code)
{
    for (int i = 0; i < d->n; i++) if (!strcmp(d->codes[i], code)) return d->lines[i];
    return -1;
}

/* Loads `text` through the diagnostic loader; returns the map (freed by
 * the caller) and what it said. */
static Map *diag_load(const char *dir, const char *text, DiagLog *d)
{
    char path[600], err[256];
    snprintf(path, sizeof path, "%s/d.vtt", dir);
    FILE *f = fopen(path, "w");
    if (!f) return NULL;
    fputs(text, f);
    fclose(f);
    memset(d, 0, sizeof *d);
    return mapio_load_diag(path, err, sizeof err, diag_collect, d);
}

/* The loader forgives a damaged map and, asked, says what it forgave: with
 * the line, and without changing what loads. */
void test_map_diag(void)
{
    Sandbox sb = sandbox_enter("mapdiag");
    CHECK_EQ(sb.ok, 1);
    if (!sb.ok) return;
    DiagLog d;
    char err[256];

    CASE("the shipped fixtures load without a word");
    static const char *const clean[] = { "tests/fixtures/two-rooms.vtt", "tests/fixtures/kinds.vtt",
                                         "tests/fixtures/crowd.vtt" };
    char here[1024];
    for (size_t i = 0; i < 3; i++) {
        snprintf(here, sizeof here, "%s/%s", sb.cwd, clean[i]);
        memset(&d, 0, sizeof d);
        Map *m = mapio_load_diag(here, err, sizeof err, diag_collect, &d);
        CHECK(m != NULL);
        int loud = 0;
        for (int k = 0; k < d.n; k++) loud += d.codes[k][0] != 'N';
        CHECK_EQ(loud, 0);
        map_free(m);
    }

    CASE("a vedges section one row short swallows the hedges header, and says so at that line");
    Map *m = diag_load(sb.dir,
        "VTT 2\nsize 3 2\ntiles\n...\n...\n"
        "vedges\n|  |\n"                                   /* one row, not two */
        "hedges\n---\n   \n---\n", &d);
    CHECK(m != NULL);
    CHECK(diag_has(&d, "E011"));
    CHECK_EQ(diag_line(&d, "E011"), 8);                    /* 'hedges' is line 8 */
    CHECK(diag_has(&d, "W019"));                           /* the stranded hedges rows */
    int strays = 0;
    for (int k = 0; k < d.n; k++) strays += !strcmp(d.codes[k], "W019");
    CHECK_EQ(strays, 1);                                   /* one finding for the run */
    map_free(m);

    CASE("a section one row long leaves a stray row; a row too long is cut, and says where");
    m = diag_load(sb.dir,
        "VTT 2\nsize 3 2\ntiles\n...\n....\n...\n"         /* second row long, then one too many */
        "vedges\n|  |\n|  |\nhedges\n---\n   \n---\n", &d);
    CHECK(diag_has(&d, "E010"));
    CHECK_EQ(diag_line(&d, "E010"), 5);
    CHECK(diag_has(&d, "W019"));
    CHECK_EQ(diag_line(&d, "W019"), 6);
    map_free(m);

    CASE("a character no row knows reads as empty and is named with its column");
    m = diag_load(sb.dir, "VTT 2\nsize 3 1\ntiles\n.X.\nvedges\n| Q|\nhedges\n---\n---\n", &d);
    CHECK(diag_has(&d, "E013"));
    CHECK_EQ(map_tile(m, 1, 0), TILE_VOID);                /* loaded exactly as before */
    int bad = 0;
    for (int k = 0; k < d.n; k++) bad += !strcmp(d.codes[k], "E013");
    CHECK_EQ(bad, 2);
    map_free(m);

    CASE("records that do not parse are named, dropped as ever");
    m = diag_load(sb.dir,
        "VTT 6\nsize 3 2\ntiles\n...\n...\n"
        "tokenstatus red \"Early\"\n"                      /* no token yet */
        "token player 9 9 1 \"Off\"\n"                     /* off the map */
        "token enemy 0 0 7 \"Big\"\n"                      /* size clamped */
        "note 5 5 \"nowhere\"\n"
        "clock 3 4\n"
        "fogpatch 99 Bad reveal 1\n"
        "mystery line\n", &d);
    CHECK(m != NULL);
    int records = 0;
    for (int k = 0; k < d.n; k++) records += !strcmp(d.codes[k], "E014");
    CHECK_EQ(records, 5);
    CHECK_EQ(diag_line(&d, "E014"), 6);
    CHECK(diag_has(&d, "W020"));
    CHECK(diag_has(&d, "W015"));
    CHECK_EQ(m->tokens.n, 1);
    map_free(m);

    CASE("header settings it does not know or cannot use");
    m = diag_load(sb.dir, "VTT 5\nsize 2 1\nzoom 9\nscale -3\nruleset nosuch\nmetric bent\ntiles\n..\n", &d);
    CHECK(diag_has(&d, "W016"));
    CHECK(diag_has(&d, "W017"));
    CHECK_EQ(diag_line(&d, "W017"), 6);
    int clamps = 0;
    for (int k = 0; k < d.n; k++) clamps += !strcmp(d.codes[k], "W020");
    CHECK_EQ(clamps, 2);
    CHECK_EQ(m->zoom, 3);
    map_free(m);

    CASE("short rows are the format's leniency: one note a section, not a failure");
    m = diag_load(sb.dir, "VTT 2\nsize 4 3\ntiles\n..\n.\n....\n", &d);
    CHECK(diag_has(&d, "N021"));
    int notes = 0;
    for (int k = 0; k < d.n; k++) notes += !strcmp(d.codes[k], "N021");
    CHECK_EQ(notes, 1);
    CHECK_EQ(diag_line(&d, "N021"), 4);
    map_free(m);

    CASE("a fog row naming a patch no line creates, told once for that patch");
    m = diag_load(sb.dir, "VTT 6\nsize 3 2\ntiles\n...\n...\nfog on\nfogpatch 1 Crypt\nfog\nAC.\n.CC\n", &d);
    int unknown = 0;
    for (int k = 0; k < d.n; k++) unknown += !strcmp(d.codes[k], "W018");
    CHECK_EQ(unknown, 1);
    CHECK_EQ(diag_line(&d, "W018"), 9);
    CHECK_EQ(fog_at(m, 1, 0) & FOG_ID, 0);
    map_free(m);

    CASE("a file that ends inside a section says so");
    m = diag_load(sb.dir, "VTT 2\nsize 2 3\ntiles\n..\n", &d);
    CHECK(diag_has(&d, "E011"));
    map_free(m);

    CASE("a fog row spelling a word is a fog row, not a swallowed record");
    m = diag_load(sb.dir, "VTT 6\nsize 4 2\ntiles\n....\n....\nfog on\n"
                          "fogpatch 6 Six\nfogpatch 7 Seven\nfogpatch 15 Last\nfogpatch 14 Four\n"
                          "fog\nfog.\nname\n", &d);
    CHECK(!diag_has(&d, "E011"));
    map_free(m);

    CASE("short wall and fog rows are noted too, and a vedges row as wide as the map is a warning");
    m = diag_load(sb.dir, "VTT 2\nsize 4 2\ntiles\n....\n....\nvedges\n|  |\n|   |\nhedges\n---\n    \n----\n", &d);
    CHECK(diag_has(&d, "W022"));
    CHECK_EQ(diag_line(&d, "W022"), 7);
    int shorts = 0;
    for (int k = 0; k < d.n; k++) shorts += !strcmp(d.codes[k], "N021");
    CHECK_EQ(shorts, 2);                                   /* vedges row 1, hedges row 1 */
    map_free(m);

    CASE("a line of spaces between sections is blank, not a stray row");
    m = diag_load(sb.dir, "VTT 2\nsize 2 1\ntiles\n..\n   \nvedges\n| |\n", &d);
    CHECK(!diag_has(&d, "W019"));
    map_free(m);

    CASE("a header line after the sections says why it is ignored; no size at all says so");
    m = diag_load(sb.dir, "VTT 2\nsize 2 1\ntiles\n..\nname Late\n", &d);
    CHECK(diag_has(&d, "W015"));
    map_free(m);
    m = diag_load(sb.dir, "VTT 2\ntiles\n..\nsize 2 1\n", &d);
    CHECK(m == NULL);

    CASE("the broken fixture loads byte-for-byte the same through either loader");
    {
        char here2[1024], o1[700], o2[700];
        snprintf(here2, sizeof here2, "%s/tests/fixtures/broken.vtt", sb.cwd);
        Map *b1 = mapio_load(here2, err, sizeof err);
        Map *b2 = mapio_load_diag(here2, err, sizeof err, diag_collect, &d);
        CHECK(b1 && b2);
        snprintf(o1, sizeof o1, "%s/o1.vtt", sb.dir);
        snprintf(o2, sizeof o2, "%s/o2.vtt", sb.dir);
        if (b1 && b2) {
            CHECK_EQ(mapio_write(b1, o1, err, sizeof err), 0);
            CHECK_EQ(mapio_write(b2, o2, err, sizeof err), 0);
            FILE *f1 = fopen(o1, "rb"), *f2 = fopen(o2, "rb");
            char  x1[8192], x2[8192];
            size_t n1 = f1 ? fread(x1, 1, sizeof x1, f1) : 0, n2 = f2 ? fread(x2, 1, sizeof x2, f2) : 1;
            if (f1) fclose(f1);
            if (f2) fclose(f2);
            CHECK(n1 > 0);
            CHECK(n1 == n2 && !memcmp(x1, x2, n1));
        }
        map_free(b1);
        map_free(b2);
    }

    CASE("without a sink the loader is silent and loads the same map");
    map_free(diag_load(sb.dir, "VTT 2\nsize 3 2\ntiles\n.X.\n...\nvedges\n|  |\n", &d));
    char path[600];
    snprintf(path, sizeof path, "%s/d.vtt", sb.dir);
    Map *a1 = mapio_load(path, err, sizeof err);
    Map *a2 = mapio_load_diag(path, err, sizeof err, diag_collect, &d);
    CHECK(a1 && a2);
    if (a1 && a2) CHECK_EQ(memcmp(a1->tiles, a2->tiles, (size_t)a1->w * (size_t)a1->h), 0);
    map_free(a1);
    map_free(a2);

    sandbox_leave(&sb);
}

/* A loopback client of the server under test. */
int net_connect(uint16_t port)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port   = htons(port);
    a.sin_addr.s_addr = htonl(0x7F000001);
    if (connect(fd, (struct sockaddr *)&a, sizeof a) < 0) { close(fd); return -1; }
    struct timeval tv = { 2, 0 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    return fd;
}

/* One turn of the server's event loop, as main would run it. */
void net_pump(Net *n, uint64_t now_ms)
{
    struct pollfd fds[1 + NET_MAX_CLIENTS];
    int k = net_pollfds(n, fds, 1 + NET_MAX_CLIENTS);
    if (k == 0) return;
    poll(fds, (nfds_t)k, 20);
    net_service(n, fds, k, now_ms);
}

/* Reads from a raw client into the decoder until `ends` frames have
 * arrived or the wait runs out. */
int net_recv_until(Net *n, int fd, WireDec *d, WireCatch *c, int ends, uint64_t now_ms)
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
    int fd = net_connect(n->port);
    if (fd < 0) return -1;
    char up[300];
    snprintf(up, sizeof up,
             "GET /ws?k=%s HTTP/1.1\r\nHost: x\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
             "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n\r\n", n->code);
    if (write(fd, up, strlen(up)) != (ssize_t)strlen(up)) { close(fd); return -1; }
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
        int x = netmsg_ws(&n, now), y = netmsg_ws(&n, now);
        CHECK(x >= 0 && y >= 0);
        uint32_t yid = n.cl[n.ncl - 1].id;
        uint8_t f[] = { 0x89, 0x84, 1, 2, 3, 4, 'p' ^ 1, 'i' ^ 2, 'n' ^ 3, 'g' ^ 4 };
        CHECK(write(x, f, sizeof f) == (ssize_t)sizeof f);
        struct linger l = { 1, 0 };
        setsockopt(x, SOL_SOCKET, SO_LINGER, &l, sizeof l);
        close(x);
        struct timespec ts = { 0, 50000000 };
        nanosleep(&ts, NULL);
        struct pollfd fds[1 + NET_MAX_CLIENTS];
        int k = net_pollfds(&n, fds, 1 + NET_MAX_CLIENTS);
        poll(fds, (nfds_t)k, 20);
        for (int i = 1; i < k; i++) if (fds[i].revents & POLLIN) fds[i].revents = POLLIN;
        net_service(&n, fds, k, now);                       /* ASan is the check */
        for (int i = 0; i < 5; i++) net_pump(&n, now);
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
    int s = net_connect(n.port);
    char up[300];
    snprintf(up, sizeof up,
             "GET /ws?k=%s HTTP/1.1\r\nHost: x\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
             "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n\r\n", n.code);
    CHECK_EQ((int)write(s, up, strlen(up)), (int)strlen(up));
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

/* rnd_dump reads the back buffer, which after a flush is the previous frame;
 * this reads what was actually shown or sent. */
void front_text(const Renderer *r, ByteBuf *out)
{
    for (int y = 0; y < r->h; y++) {
        for (int x = 0; x < r->w; x++) {
            const Cell *c = &r->front[(size_t)y * (size_t)r->w + (size_t)x];
            if (c->ch == 0) continue;
            char enc[4];
            int  n = utf8_encode(c->ch, enc);
            if (n > 0) bb_put(out, enc, (size_t)n); else bb_putc(out, ' ');
        }
        bb_putc(out, '\n');
    }
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

/* Counters on creatures: s v reads and writes them, < and > step the
 * current one, undo takes a hit back, the file keeps them, and the
 * players' frame never shows a number. */
