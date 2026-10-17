/* Tests: the map tools: --dump-map, --describe, --check, the loader's diagnostics. */

#include <ctype.h>

#include "harness.h"

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

/* A scene's block that does not stand is dropped with W025, and the map's
 * own creatures are not touched by it. */
static void scene_findings(void)
{
    char path[] = "/tmp/vtt-w025-XXXXXX";
    int fd = mkstemp(path);
    if (fd < 0) return;
    const char *text =
        "VTT 11\nname W\nsize 4 3\ntiles\n....\n....\n....\n"
        "token player 0 0 1 \"Aria\"\n"
        "scene \"Good\"\ntoken enemy 1 1 1 \"Imp\"\nround 3\nendscene\n"
        "scene \"Good\"\nendscene\n"
        "scene \"Boxed\" 0 0 9 9\nendscene\n"
        "endscene\n"
        "scene \"\ntoken enemy 1 1 1 \"Lost\"\n"
        "scene \"Open\"\ntoken enemy 2 2 1 \"Rat\"\n";
    if (write(fd, text, strlen(text)) < 0) { close(fd); unlink(path); return; }
    close(fd);
    int rc;
    char *t = check_text(path, 0, &rc, NULL);
    CHECK(strstr(t, "scene Good dropped: a scene of that name came before it") != NULL);
    CHECK(strstr(t, "scene Boxed dropped: its box is not on the map") != NULL);
    CHECK(strstr(t, "endscene with no scene open") != NULL);
    CHECK(strstr(t, "scene Open dropped: the file ends before its endscene") != NULL);
    CHECK(strstr(t, "scene ? dropped: its name does not read") != NULL);
    CHECK(strstr(t, "scene  dropped") == NULL);            /* one finding for the unnamed block, not two */
    free(t);
    char err[200];
    Map *m = mapio_load(path, err, sizeof err);
    CHECK(m != NULL);
    if (m) {
        CHECK_EQ(m->nscenes, 1);
        CHECK(m->tokens.n == 1 && !strcmp(m->tokens.v[0].label, "Aria"));
        CHECK(m->scenes[0].tokens.n == 1 && m->scenes[0].round == 0);   /* no fight: the round is sanitized */
        map_free(m);
    }
    unlink(path);
}

/* The rooms and the pairs by the slow road, for the fast one to be held to
 * (HEALTH 70 and 71): a flood through map_walkable and map_vedge, as
 * rooms_build was written before it read the arrays; and every creature
 * against every other, as check_map did. */
static void rooms_slowly(const Map *m, int32_t *at, int *count)
{
    int n = 0;
    for (int i = 0; i < m->w * m->h; i++) at[i] = -1;
    int32_t *stack = malloc((size_t)(m->w * m->h + 1) * sizeof *stack);
    for (int y = 0; y < m->h; y++)
        for (int x = 0; x < m->w; x++) {
            if (at[y * m->w + x] >= 0 || !map_walkable(m, x, y)) continue;
            int sp = 0;
            stack[sp++] = y * m->w + x;
            at[y * m->w + x] = n;
            while (sp) {
                int k = stack[--sp], kx = k % m->w, ky = k / m->w;
                static const int d[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
                for (int j = 0; j < 4; j++) {
                    int nx = kx + d[j][0], ny = ky + d[j][1];
                    if (!map_in_bounds(m, nx, ny) || !map_walkable(m, nx, ny) || at[ny * m->w + nx] >= 0) continue;
                    uint8_t e = d[j][0] > 0 ? map_vedge(m, kx + 1, ky) : d[j][0] < 0 ? map_vedge(m, kx, ky)
                              : d[j][1] > 0 ? map_hedge(m, kx, ky + 1) : map_hedge(m, kx, ky);
                    if (e != EDGE_NONE) continue;
                    at[ny * m->w + nx] = n;
                    stack[sp++] = ny * m->w + nx;
                }
            }
            n++;
        }
    free(stack);
    *count = n;
}

static int count_of(const char *text, const char *needle)
{
    int n = 0;
    for (const char *p = text; (p = strstr(p, needle)) != NULL; p++) n++;
    return n;
}

static void fast_against_slow(void)
{
    CASE("rooms and creature pairs by the arrays and the index are what the slow walks find, on maps made at random");
    unsigned seed = 12345;
    #define RND(n) ((int)((seed = seed * 1103515245u + 12345u) >> 16) % (n))
    for (int round = 0; round < 60; round++) {
        int  w = 1 + RND(24), h = 1 + RND(18);
        Map *m = map_new(w, h, "rand");
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) map_set_tile(m, x, y, RND(5) ? TILE_FLOOR : TILE_VOID);
        for (int y = 0; y < h; y++)
            for (int x = 0; x <= w; x++) if (!RND(3)) map_set_vedge(m, x, y, (uint8_t)(1 + RND(4)));
        for (int y = 0; y <= h; y++)
            for (int x = 0; x < w; x++) if (!RND(3)) map_set_hedge(m, x, y, (uint8_t)(1 + RND(4)));
        /* Creatures crowded on purpose: big ones, some off the map's edge
         * and past it, three to a square, labels shared and none. */
        int nt = RND(40);
        for (int k = 0; k < nt; k++) {
            Token t = { (int16_t)(RND(w + 3) - 1), (int16_t)(RND(h + 3) - 1), (uint8_t)(1 + RND(3)), TOKEN_ENEMY, "" };
            if (RND(4)) snprintf(t.label, sizeof t.label, "L%d", RND(6));
            tokens_add(&m->tokens, t);
        }

        Rooms r;
        rooms_build(m, &r);
        int32_t *slow = malloc((size_t)(w * h) * sizeof *slow);
        int      nslow = 0, same = 1;
        rooms_slowly(m, slow, &nslow);
        for (int i = 0; i < w * h; i++) same &= slow[i] == r.at[i];
        for (int i = 0; i < r.n; i++) {
            const Room *rm = &r.v[i];
            int cnt = 0, x0 = w, y0 = h, x1 = -1, y1 = -1, first = -1;
            for (int q = 0; q < w * h; q++)
                if (slow[q] == i) {
                    cnt++;
                    if (first < 0) first = q;
                    x0 = imin(x0, q % w); x1 = imax(x1, q % w); y0 = imin(y0, q / w); y1 = imax(y1, q / w);
                }
            same &= cnt == rm->squares && x0 == rm->x0 && y0 == rm->y0 && x1 == rm->x1 && y1 == rm->y1 &&
                    first == rm->fy * w + rm->fx;
        }
        CHECK(same && nslow == r.n);
        rooms_free(&r);
        free(slow);

        int meet = 0, named = 0;
        for (int i = 0; i < m->tokens.n; i++)
            for (int j = i + 1; j < m->tokens.n; j++) {
                const Token *t = &m->tokens.v[i], *u = &m->tokens.v[j];
                meet  += token_meets(t, u->x, u->y, u->size, u->size);
                named += t->label[0] && !strcmp(t->label, u->label);
            }
        char  *text = NULL;
        size_t len = 0;
        FILE  *out = open_memstream(&text, &len);
        maptools_check_map(out, m, 0);
        fclose(out);
        CHECK_EQ(count_of(text, " share a square"), meet);
        CHECK_EQ(count_of(text, "two creatures called "), named);
        free(text);
        map_free(m);
    }
    #undef RND
}

void test_map_tools_check(void)
{
    fast_against_slow();
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

    CASE("scenes that do not stand are dropped with W025");
    scene_findings();

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

/* A version 12 map with one damaged line: what the loader says, and what it
 * keeps. The base is a 6x4 room; `tail` goes after the tiles. */
typedef struct { const char *code; int n; } Found;

static void found_code(void *ctx, int line, int col, const char *code,
                       const char *slug, const char *msg)
{
    Found *f = ctx;
    (void)line; (void)col; (void)slug; (void)msg;
    if (!strcmp(code, f->code)) f->n++;
}

static int damaged(const char *dir, const char *tail, const char *code, int *nlinks, int *nscenes, int *round)
{
    char path[600], err[160];
    snprintf(path, sizeof path, "%s/damaged.vtt", dir);
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    fprintf(f, "VTT 12\nname D\nsize 6 4\ntiles\n......\n......\n......\n......\n%s", tail);
    fclose(f);
    Found fd = { code, 0 };
    Map *m = mapio_load_diag(path, err, sizeof err, found_code, &fd);
    unlink(path);
    if (!m) return -1;
    if (nlinks) *nlinks = m->nlinks;
    if (nscenes) *nscenes = m->nscenes;
    if (round) *round = m->nscenes ? m->scenes[0].round : -1;
    map_free(m);
    return fd.n;
}

void test_loader_damage(void)
{
    Sandbox sb = sandbox_enter("damage");
    CHECK_EQ(sb.ok, 1);
    if (!sb.ok) return;
    int nl = -1, ns = -1, rd = -1;

    CASE("a link to another map that does not read is dropped, and said");
    static const char *const bad_links[] = {
        "link 1 stairs 1 0 0 to town \"Gate\"\n",              /* the map unquoted */
        "link 1 stairs 1 0 0 to \"town\" \"Gate\" oneway\n",  /* a word it does not take */
        "link 1 stairs 1 0 0 to \"town\" \"\"\n",             /* no place */
        "link 1 stairs 1 0 0 to \"to/wn\" \"Gate\"\n",        /* a path, not a name */
        "link 1 stairs 1 0 0 to \"town\n",                      /* the quote never closes */
        "link 1 stairs 9 0 0 to \"town\" \"Gate\"\n",         /* too big */
        "link 0 stairs 1 0 0 to \"town\" \"Gate\"\n",         /* no number 0 */
        "link 1 elevator 1 0 0 to \"town\" \"Gate\"\n",       /* no such kind */
    };
    for (size_t i = 0; i < sizeof bad_links / sizeof bad_links[0]; i++) {
        CHECK_EQ(damaged(sb.dir, bad_links[i], "E014", &nl, NULL, NULL), 1);
        CHECK_EQ(nl, 0);
    }
    CHECK_EQ(damaged(sb.dir, "link 1 stairs 1 0 0 to \"town\" \"Gate\" secret\n", "E014", &nl, NULL, NULL), 0);
    CHECK_EQ(nl, 1);                                         /* the good one, for contrast */
    CHECK_EQ(damaged(sb.dir, "link 1 stairs 1 0 0 to \"town\" \"Gate\"\n"
                             "link 1 ladder 1 2 2 to \"crypt\" \"Hall\"\n", "E014", &nl, NULL, NULL), 1);
    CHECK_EQ(nl, 1);                                         /* a number used twice: the first stays */

    CASE("a scene cut short, boxed off the map or upside down, or a seventeenth, is dropped");
    CHECK_EQ(damaged(sb.dir, "scene \"A\"\ntoken enemy 1 1 1 \"G\"\nscene \"B\"\nendscene\n", "W025", NULL, &ns, NULL), 1);
    CHECK_EQ(ns, 1);                                         /* A went; B stayed */
    CHECK_EQ(damaged(sb.dir, "scene \"A\" 4 3 1 1\nendscene\n", "W025", NULL, &ns, NULL), 1);
    CHECK_EQ(ns, 0);
    CHECK_EQ(damaged(sb.dir, "scene \"A\" 0 0 9 9\nendscene\n", "W025", NULL, &ns, NULL), 1);
    CHECK_EQ(ns, 0);
    CHECK_EQ(damaged(sb.dir, "scene \"A\" 0 0\nendscene\n", "W025", NULL, &ns, NULL), 1);
    CHECK_EQ(ns, 0);
    CHECK_EQ(damaged(sb.dir, "scene \"A\"\ntoken enemy 1 1 1 \"G\"\n", "W025", NULL, &ns, NULL), 1);
    CHECK_EQ(ns, 0);                                         /* the file ends inside it */
    {
        char many[17 * 32] = "";
        for (int i = 0; i < 17; i++) {
            size_t l = strlen(many);
            snprintf(many + l, sizeof many - l, "scene \"S%d\"\nendscene\n", i);
        }
        CHECK_EQ(damaged(sb.dir, many, "W025", NULL, &ns, NULL), 1);
        CHECK_EQ(ns, MAP_SCENES_MAX);
    }

    CASE("a scene's round out of range is clamped, and said; with nobody in the order it is none");
    #define FIGHTER "token enemy 1 1 1 \"G\"\ntokenturn 5\n"
    CHECK_EQ(damaged(sb.dir, "scene \"A\"\n" FIGHTER "round 99999\nendscene\n", "W020", NULL, &ns, &rd), 1);
    CHECK_EQ(ns, 1);
    CHECK_EQ(rd, INT16_MAX);
    CHECK_EQ(damaged(sb.dir, "scene \"A\"\n" FIGHTER "round -3\nendscene\n", "W020", NULL, &ns, &rd), 1);
    CHECK_EQ(rd, 0);
    CHECK_EQ(damaged(sb.dir, "scene \"A\"\n" FIGHTER "round 3\nendscene\n", "W020", NULL, &ns, &rd), 0);
    CHECK_EQ(rd, 3);
    CHECK_EQ(damaged(sb.dir, "scene \"A\"\ntoken enemy 1 1 1 \"G\"\nround 3\nendscene\n", "W020", NULL, &ns, &rd), 0);
    CHECK_EQ(rd, 0);
    #undef FIGHTER

    sandbox_leave(&sb);
}


/* ---------------------------------------------------------------- fidelity */

/* What a map tool prints of a map, as a string (freed by the caller). */
static char *printed(const Map *m, int describe)
{
    char  *text = NULL;
    size_t len = 0;
    FILE  *out = open_memstream(&text, &len);
    if (!out) return NULL;
    if (describe) maptools_describe(out, m, 0);
    else          maptools_dump(out, m, 0, 0, m->w - 1, m->h - 1);
    fclose(out);
    return text;
}

/* Saved, loaded and saved again: the map read back describes and dumps as
 * the one saved (a writer that dropped something the same way twice would
 * pass "the same bytes twice" alone), the two files are the same bytes, and
 * the loader has nothing to say of the file but what a map may say. */
static int round_trip(const Map *a, const char *dir)
{
    char p1[700], p2[700], err[256];
    snprintf(p1, sizeof p1, "%s/one.vtt", dir);
    snprintf(p2, sizeof p2, "%s/two.vtt", dir);
    if (mapio_write(a, p1, err, sizeof err) != 0) return 0;
    DiagLog d;
    memset(&d, 0, sizeof d);
    Map *b = mapio_load_diag(p1, err, sizeof err, diag_collect, &d);
    if (!b) return 0;
    int ok = mapio_write(b, p2, err, sizeof err) == 0;
    for (int k = 0; k < d.n; k++) ok &= d.codes[k][0] != 'E' && strcmp(d.codes[k], "N021") != 0;
    char *f1 = slurp(p1), *f2 = slurp(p2);
    ok &= f1 && f2 && !strcmp(f1, f2) && !strncmp(f1, "VTT 14\n", 7);
    for (int describe = 0; describe < 2; describe++) {
        char *x = printed(a, describe), *y = printed(b, describe);
        ok &= x && y && !strcmp(x, y);
        free(x);
        free(y);
    }
    ok &= a->tokens.n == b->tokens.n && a->nscenes == b->nscenes && a->ncards == b->ncards;
    for (int i = 0; ok && i < a->tokens.n; i++) ok &= token_equal(&a->tokens.v[i], &b->tokens.v[i]);
    free(f1);
    free(f2);
    map_free(b);
    return ok;
}

/* The file 2.0 writes (docs/V2.md, step 5): version 14 always, a row of
 * nothing an empty line, and what loads from it is what was saved. */
void test_map_fidelity(void)
{
    Sandbox sb = sandbox_enter("fidelity");
    CHECK_EQ(sb.ok, 1);
    if (!sb.ok) return;
    char path[700], err[256];
    snprintf(path, sizeof path, "%s/m.vtt", sb.dir);

    CASE("every fixture, oracle map and fuzz-found map loads, saves and loads again the same");
    /* The oracle's e001.vtt is not a map at all: nothing to save. */
    static const char *const files[] = {
        "tests/fixtures/areas.vtt", "tests/fixtures/broken.vtt", "tests/fixtures/crowd.vtt",
        "tests/fixtures/env.vtt", "tests/fixtures/everything.vtt", "tests/fixtures/floors.vtt",
        "tests/fixtures/kinds.vtt", "tests/fixtures/links.vtt", "tests/fixtures/two-rooms.vtt",
        "tests/oracle/maps/n105.vtt", "tests/oracle/maps/v10.vtt",
        "tests/oracle/maps/v11.vtt", "tests/oracle/maps/v12.vtt", "tests/oracle/maps/v3.vtt",
        "tests/oracle/maps/v4.vtt", "tests/oracle/maps/v5.vtt", "tests/oracle/maps/w024.vtt",
        "tests/fuzz-found/card-leading-blank.vtt", "tests/fuzz-found/roundtrip-damaged.vtt",
    };
    for (size_t i = 0; i < sizeof files / sizeof files[0]; i++) {
        char here[1024];
        snprintf(here, sizeof here, "%s/%s", sb.cwd, files[i]);
        Map *m = mapio_load(here, err, sizeof err);
        CHECK(m != NULL);
        if (!m) continue;
        int ok = round_trip(m, sb.dir);
        if (!ok) fprintf(stderr, "  not the same after a save: %s\n", files[i]);
        CHECK(ok);
        map_free(m);
    }

    CASE("maps made at random, with whole rows of nothing among them, save and load the same");
    unsigned seed = 4242;
    #define RND(n) ((int)((seed = seed * 1103515245u + 12345u) >> 16) % (n))
    int same = 0;
    for (int round = 0; round < 150; round++) {
        int  w = 1 + RND(40), h = 1 + RND(30);
        Map *m = map_new(w, h, "rand");
        for (int y = 0; y < h; y++) {
            if (RND(2)) continue;                           /* half the rows hold nothing */
            for (int x = 0; x < w; x++) if (RND(3)) map_set_tile(m, x, y, (uint8_t)RND(TILE_COUNT));
        }
        for (int y = 0; y < h; y++) {
            if (RND(2)) continue;
            for (int x = 0; x <= w; x++) if (!RND(3)) map_set_vedge(m, x, y, (uint8_t)RND(EDGE_COUNT));
        }
        for (int y = 0; y <= h; y++) {
            if (RND(2)) continue;
            for (int x = 0; x < w; x++) if (!RND(3)) map_set_hedge(m, x, y, (uint8_t)RND(EDGE_COUNT));
        }
        if (RND(2)) {
            /* Fog: two patches, the second deleted, and squares lit or on
             * the rim, none of which is written. */
            str_lcpy(m->fog_patches[0].name, "Crypt", sizeof m->fog_patches[0].name);
            str_lcpy(m->fog_patches[1].name, "Gone", sizeof m->fog_patches[1].name);
            m->fog_patches[0].reveal = m->fog_patches[1].reveal = 2;
            m->fog_patches[1].dead = 1;
            for (int y = 0; y < h; y++) {
                int kind = RND(3);
                for (int x = 0; x < w; x++) {
                    if (!RND(2)) continue;
                    int seen = RND(2), held = !RND(4), lit = RND(2);
                    uint8_t f = kind == 0 ? (uint8_t)(1 | (seen ? FOG_SEEN : 0) | (held ? FOG_HELD | FOG_SEEN : 0))
                              : kind == 1 ? (uint8_t)(2 | FOG_SEEN) : (uint8_t)(lit ? FOG_LIT : FOG_RIM);
                    map_fog_set(m, x, y, f);
                }
            }
        }
        int nt = RND(6);
        for (int k = 0; k < nt; k++) {
            Token t = { (int16_t)RND(w), (int16_t)RND(h), 1, TOKEN_ENEMY, "" };
            snprintf(t.label, sizeof t.label, "L%d", k);
            tokens_add(&m->tokens, t);
        }
        same += round_trip(m, sb.dir);
        map_free(m);
    }
    CHECK_EQ(same, 150);
    #undef RND

    CASE("an empty map is a few bytes a row: an empty line for each row of nothing");
    {
        Map *m = map_new(512, 512, "empty");
        CHECK_EQ(mapio_write(m, path, err, sizeof err), 0);
        char *f = slurp(path);
        CHECK(f && strlen(f) < 2 * 1024);
        CHECK(f && strstr(f, "tiles\n\n\n") && strstr(f, "vedges\n\n\n") && strstr(f, "hedges\n\n\n"));
        free(f);
        CHECK(round_trip(m, sb.dir));
        map_free(m);
    }

    CASE("a row with anything in it is written whole, east boundary and all");
    {
        Map *m = map_new(4, 2, "one");
        map_set_tile(m, 0, 0, TILE_FLOOR);
        map_set_vedge(m, 4, 1, EDGE_WALL);                 /* the east boundary alone */
        map_set_hedge(m, 3, 2, EDGE_DOOR_CLOSED);
        CHECK_EQ(mapio_write(m, path, err, sizeof err), 0);
        char *f = slurp(path);
        char want[64];
        snprintf(want, sizeof want, "hedges\n\n\n   %c\n", edge_file_char(EDGE_DOOR_CLOSED));
        CHECK(f && strstr(f, "tiles\n.   \n\nvedges\n\n    |\n") && strstr(f, want));
        free(f);
        map_free(m);
    }

    CASE("lit squares, the rim and a deleted patch's squares write an empty fog row");
    {
        Map *m = map_new(3, 3, "fog");
        str_lcpy(m->fog_patches[0].name, "Hall", sizeof m->fog_patches[0].name);
        str_lcpy(m->fog_patches[1].name, "Gone", sizeof m->fog_patches[1].name);
        m->fog_patches[0].reveal = m->fog_patches[1].reveal = 2;
        m->fog_patches[1].dead = 1;
        map_fog_set(m, 1, 0, 1);
        map_fog_set(m, 0, 1, FOG_LIT);
        map_fog_set(m, 1, 1, FOG_RIM);
        map_fog_set(m, 2, 1, 2 | FOG_SEEN);
        CHECK_EQ(mapio_write(m, path, err, sizeof err), 0);
        char *f = slurp(path);
        CHECK(f && strstr(f, "\nfog\n.A.\n\n\n") != NULL);
        free(f);
        map_free(m);
    }

    CASE("an empty line in a section is a row of nothing, no note; a part-short row still has one");
    {
        DiagLog d;
        Map *m = diag_load(sb.dir, "VTT 14\nsize 3 2\ntiles\n\n.\nvedges\n\n\nhedges\n\n---\n\n", &d);
        CHECK(m != NULL);
        int n021 = 0;
        for (int k = 0; k < d.n; k++) n021 += !strcmp(d.codes[k], "N021");
        CHECK_EQ(n021, 1);                                 /* the tiles' '.', alone */
        CHECK_EQ(diag_line(&d, "N021"), 5);
        CHECK(m && map_tile(m, 0, 1) == TILE_FLOOR && map_tile(m, 1, 1) == TILE_VOID && map_tile(m, 0, 0) == TILE_VOID);
        CHECK(m && map_hedge(m, 2, 1) == EDGE_WALL && map_hedge(m, 0, 0) == EDGE_NONE);
        map_free(m);
    }

    CASE("a hand-edited file stays forgiven: CRLF, trailing spaces trimmed, a short section");
    {
        DiagLog d;
        Map *m = diag_load(sb.dir, "VTT 14\r\nsize 3 2\r\ntiles\r\n...\r\n.\r\nvedges\r\n|  |\r\n", &d);
        CHECK(m && map_tile(m, 2, 0) == TILE_FLOOR && map_tile(m, 2, 1) == TILE_VOID);
        CHECK(m && map_vedge(m, 3, 0) == EDGE_WALL);
        CHECK(diag_has(&d, "N021") && diag_has(&d, "E011"));   /* tiles row 2 short; vedges ends early */
        map_free(m);
    }

    CASE("--check of a map 2.0 saved has no row-short note");
    {
        Map *m = map_new(30, 20, "check");
        for (int x = 0; x < 6; x++) map_set_tile(m, x, 3, TILE_FLOOR);
        CHECK_EQ(mapio_write(m, path, err, sizeof err), 0);
        map_free(m);
        char  *text = NULL;
        size_t len = 0;
        FILE  *out = open_memstream(&text, &len);
        maptools_check(out, path, 0);
        fclose(out);
        CHECK(text && strstr(text, "N021") == NULL);
        free(text);
    }

    sandbox_leave(&sb);
}

/* ------------------------------------------------------------ the line search */

/* A map made of rooms in walls, doors in some of them, creatures in some:
 * what a GM draws, which is what the search reads evidence from. */
static Map *rooms_at_random(unsigned *seed)
{
    #define RR(n) ((int)((*seed = *seed * 1103515245u + 12345u) >> 16) % (n))
    int  w = 20 + RR(40), h = 15 + RR(30);
    Map *m = map_new(w, h, "rooms");
    int  nrooms = 1 + RR(5);
    for (int r = 0; r < nrooms; r++) {
        int rw = 3 + RR(10), rh = 3 + RR(8);
        int x0 = RR(w - rw), y0 = RR(h - rh);
        map_fill_tiles(m, x0, y0, x0 + rw - 1, y0 + rh - 1, TILE_FLOOR);
        map_rect_walls(m, x0, y0, x0 + rw - 1, y0 + rh - 1, EDGE_WALL);
        if (RR(2)) map_set_vedge(m, x0, y0 + rh / 2, EDGE_DOOR_CLOSED);
        if (RR(2)) {
            Token t = { (int16_t)(x0 + 1), (int16_t)(y0 + 1), 1, TOKEN_ENEMY, "" };
            snprintf(t.label, sizeof t.label, "Ogre %d", r);
            tokens_add(&m->tokens, t);
        }
    }
    #undef RR
    return m;
}

/* Other shapes a GM draws, where the evidence is thinner or different: a
 * cave (floor and void, no walls), a hall of pillars (void squares in rows
 * of floor), rooms drawn against the map's edge. `shape` 0 rooms, 1 cave,
 * 2 pillars, 3 border. */
static Map *shape_at_random(unsigned *seed, int shape)
{
    #define RS(n) ((int)((*seed = *seed * 1103515245u + 12345u) >> 16) % (n))
    if (shape == 0) return rooms_at_random(seed);
    int  w = 12 + RS(30), h = 10 + RS(30);
    Map *m = map_new(w, h, "shape");
    if (shape == 1) {
        int x = w / 2, y = h / 2;
        for (int i = 0; i < w * h; i++) {
            map_set_tile(m, x, y, RS(5) ? TILE_FLOOR : TILE_WATER);
            x += RS(3) - 1; y += RS(3) - 1;
            x = x < 1 ? 1 : x > w - 2 ? w - 2 : x;
            y = y < 1 ? 1 : y > h - 2 ? h - 2 : y;
        }
    } else if (shape == 2) {
        map_fill_tiles(m, 1, 1, w - 2, h - 2, TILE_FLOOR);
        map_rect_walls(m, 1, 1, w - 2, h - 2, EDGE_WALL);
        for (int y = 3; y < h - 3; y += 2)
            for (int x = 3; x < w - 3; x += 3) map_set_tile(m, x, y, TILE_VOID);
    } else {
        map_fill_tiles(m, 0, 0, w / 2, h - 1, TILE_FLOOR);
        map_rect_walls(m, 0, 0, w / 2, h - 1, EDGE_WALL);
        map_fill_tiles(m, w / 2 + 2, 0, w - 1, h / 2, TILE_FLOOR);
        map_rect_walls(m, w / 2 + 2, 0, w - 1, h / 2, EDGE_WALL);
        map_set_vedge(m, 0, h / 2, EDGE_DOOR_CLOSED);
    }
    for (int k = RS(4); k > 0; k--) {
        int tx = RS(w), ty = RS(h);
        if (!map_walkable(m, tx, ty)) continue;
        Token t = { (int16_t)tx, (int16_t)ty, 1, TOKEN_ENEMY, "" };
        snprintf(t.label, sizeof t.label, "Bat %d", k);
        tokens_add(&m->tokens, t);
    }
    #undef RS
    return m;
}

/* The file with `after[k]` written after section k's last line. */
static char *with_after(const char *text, size_t len, int w, int h, const char *const after[MAPIO_SECTIONS], size_t *out_len)
{
    MapioSection secs[MAPIO_SECTIONS];
    mapio_sections(text, len, w, h, secs);
    int at[MAPIO_SECTIONS];
    size_t extra = 0;
    for (int k = 0; k < MAPIO_SECTIONS; k++) {
        at[k] = secs[k].n ? secs[k].v[secs[k].n - 1].line : 0;
        extra += strlen(after[k]);
    }
    mapio_sections_free(secs);
    char *out = xmalloc(len + extra + 1), *o = out;
    int line = 1;
    for (const char *p = text; *p; line++) {
        const char *e = strchr(p, '\n');
        size_t n = e ? (size_t)(e - p) + 1 : strlen(p);
        memcpy(o, p, n); o += n;
        for (int k = 0; k < MAPIO_SECTIONS; k++)
            if (at[k] == line) { size_t a = strlen(after[k]); memcpy(o, after[k], a); o += a; }
        p += n;
    }
    *o = '\0';
    *out_len = (size_t)(o - out);
    return out;
}

/* A file as a hand-writer keeps one: `blanks` blank lines after each row
 * section in `mask` (bit k for section k); negative, that many lines of
 * three spaces (an editor's blank line, which the loader reads as blank). */
static char *with_separators_in(const char *text, size_t len, int w, int h, int mask, int blanks, size_t *out_len)
{
    char sep[64] = "";
    for (int b = 0; b < (blanks < 0 ? -blanks : blanks) && b < 8; b++) strcat(sep, blanks < 0 ? "   \n" : "\n");
    const char *after[MAPIO_SECTIONS];
    for (int k = 0; k < MAPIO_SECTIONS; k++) after[k] = mask >> k & 1 ? sep : "";
    return with_after(text, len, w, h, after, out_len);
}

static char *with_separators(const char *text, size_t len, int w, int h, size_t *out_len)
{
    return with_separators_in(text, len, w, h, ~0, 1, out_len);
}

/* Does --check --json say a row section is wrong (W031, or E011 told more)? */
static int check_says_rows(const char *path)
{
    char  *text = NULL;
    size_t len = 0;
    FILE  *out = open_memstream(&text, &len);
    maptools_check(out, path, 1);
    fclose(out);
    int said = text && (strstr(text, "\"W031\"") || strstr(text, "\"E011\""));
    free(text);
    return said;
}

static int unplaced;                    /* findings that said a row is missing, or both readings, no place */

/* What --check --json says of a file: a span, and its code, when it has one. */
static int check_span(const char *path, int *first, int *last, char *code)
{
    char  *text = NULL;
    size_t len = 0;
    FILE  *out = open_memstream(&text, &len);
    maptools_check(out, path, 1);
    fclose(out);
    int got = 0;
    /* Said without a place ("the missing row is one of the N after..."). */
    if (text && (strstr(text, "the missing row is one of the") || strstr(text, "the text cannot tell which"))) unplaced++;
    const char *p = text ? strstr(text, "\"span\":{\"first\":") : NULL;
    if (p && sscanf(p, "\"span\":{\"first\":%d,\"last\":%d", first, last) == 2) {
        got = 1;
        const char *c = p;
        while (c > text && strncmp(c, "{\"code\":\"", 9)) c--;
        sscanf(c, "{\"code\":\"%7[^\"]", code);
        if (strstr(c, "somewhere")) code[0] = (char)tolower(code[0]);   /* a tie: told by the case */
    }
    free(text);
    return got;
}

static char *lines_join(char **v, int n, size_t *len)
{
    size_t total = 0;
    for (int i = 0; i < n; i++) total += strlen(v[i]) + 1;
    char *out = xmalloc(total + 1), *o = out;
    for (int i = 0; i < n; i++) { size_t k = strlen(v[i]); memcpy(o, v[i], k); o += k; *o++ = '\n'; }
    *o = '\0';
    *len = total;
    return out;
}

/* Two rooms in walls, a corridor between with a door at each end, a
 * creature in each room. */
static Map *pair_map(void)
{
    Map *m = map_new(30, 14, "pair");
    map_fill_tiles(m, 1, 1, 10, 8, TILE_FLOOR);
    map_rect_walls(m, 1, 1, 10, 8, EDGE_WALL);
    map_fill_tiles(m, 17, 2, 26, 9, TILE_FLOOR);
    map_rect_walls(m, 17, 2, 26, 9, EDGE_WALL);
    map_fill_tiles(m, 11, 4, 16, 4, TILE_FLOOR);
    map_set_vedge(m, 11, 4, EDGE_DOOR_CLOSED);
    map_set_vedge(m, 17, 4, EDGE_DOOR_OPEN);
    for (int x = 11; x <= 16; x++) { map_set_hedge(m, x, 4, EDGE_WALL); map_set_hedge(m, x, 5, EDGE_WALL); }
    Token t1 = { 3, 3, 1, TOKEN_ENEMY, "Ogre" }, t2 = { 20, 6, 1, TOKEN_PLAYER, "Aria" };
    tokens_add(&m->tokens, t1);
    tokens_add(&m->tokens, t2);
    return m;
}

/* docs/V2.md, step 5's second piece: a line added, repeated or lost in a row
 * section is found where it was, or not said at all -- never somewhere else. */
void test_map_shift(void)
{
    Sandbox sb = sandbox_enter("mapshift");
    CHECK_EQ(sb.ok, 1);
    if (!sb.ok) return;
    char path[700], bad[700], err[256];
    snprintf(path, sizeof path, "%s/s.vtt", sb.dir);
    snprintf(bad, sizeof bad, "%s/bad.vtt", sb.dir);

    CASE("a file vtt wrote has nothing to say of its rows; nor a hand-written one with blank lines between sections");
    unsigned seed = 777;
    int quiet = 1;
    for (int i = 0; i < 40; i++) {
        Map *m = rooms_at_random(&seed);
        CHECK_EQ(mapio_write(m, path, err, sizeof err), 0);
        int a, b; char code[8];
        quiet &= !check_span(path, &a, &b, code);
        map_free(m);
    }
    CHECK(quiet);
    {
        FILE *f = fopen(bad, "w");
        fputs("VTT 14\nsize 5 3\ntiles\n.....\n.....\n.....\n\nvedges\n|    |\n|    |\n|    |\n\n"
              "hedges\n-----\n\n\n-----\n\ntoken enemy 1 1 1 \"Ogre\"\n", f);
        fclose(f);
        int a, b; char code[8];
        CHECK(!check_span(bad, &a, &b, code));
    }

    CASE("what it says, as text and JSON: a row repeated, a row lost (a copy put back), a lost empty last row");
    {
        Map *m = pair_map();
        CHECK_EQ(mapio_write(m, path, err, sizeof err), 0);
        size_t len;
        char *text = file_read(path, 1 << 20, &len, NULL);
        MapioSection secs[MAPIO_SECTIONS];
        mapio_sections(text, len, m->w, m->h, secs);
        static const struct { const char *name; int kind, row, op; } cases[] = {
            { "shift-repeated",  MAPIO_TILES,  4, 0 },     /* the corridor's row, repeated */
            { "shift-lost",      MAPIO_VEDGES, 5, 2 },     /* a room row lost: a copy fits */
            { "shift-lost-end",  MAPIO_TILES, 12, 2 },     /* an empty row near the end lost */
            { "shift-added",     MAPIO_HEDGES, 3, 1 },     /* an empty line in hedges */
        };
        for (size_t c = 0; c < sizeof cases / sizeof cases[0]; c++) {
            int at = secs[cases[c].kind].v[cases[c].row].line;
            FILE *f = fopen(bad, "w");
            int line = 1;
            for (const char *p = text; *p; line++) {
                const char *e = strchr(p, '\n');
                size_t n = e ? (size_t)(e - p) + 1 : strlen(p);
                if (!(cases[c].op == 2 && line == at)) fwrite(p, 1, n, f);
                if (line == at && cases[c].op == 0) fwrite(p, 1, n, f);
                if (line == at && cases[c].op == 1) fputc('\n', f);
                p += n;
            }
            fclose(f);
            for (int json = 0; json < 2; json++) {
                char  *out = NULL;
                size_t olen = 0;
                FILE  *o = open_memstream(&out, &olen);
                maptools_check(o, bad, json);
                fclose(o);
                /* The sandbox's name changes each run: DIR for it. */
                char *t = xmalloc(olen + 1), *w = t;
                size_t dl = strlen(sb.dir);
                for (const char *p = out; *p; ) {
                    if (!strncmp(p, sb.dir, dl)) { memcpy(w, "DIR", 3); w += 3; p += dl; }
                    else *w++ = *p++;
                }
                *w = '\0';
                char gname[64];
                snprintf(gname, sizeof gname, "%s%s", cases[c].name, json ? "-json" : "");
                golden_bytes(gname, t, strlen(t));
                free(t);
                free(out);
            }
        }
        mapio_sections_free(secs);
        free(text);
        map_free(m);
    }

    CASE("separators after some sections; tiles short before its blank is set aside, a blank added to hedges' closing empty rows: hedges' line never told (it was once, wrong); tiles' row told where it was");
    {
        int a = 0, b = 0; char code[8] = "";
        char  *out = NULL;
        size_t olen = 0;
        FILE  *o = open_memstream(&out, &olen);
        maptools_check(o, "tests/fixtures/shift-short-sep.vtt", 1);
        fclose(o);
        CHECK(strstr(out, "\"W031\"") == NULL);
        free(out);
        /* The row lost is the original's line 16, after line 15 here; the
         * blank in hedges changes no square, so that fix alone restores it. */
        if (check_span("tests/fixtures/shift-short-sep.vtt", &a, &b, code)) CHECK(a <= 15 && 15 <= b);
    }

    CASE("certain without a place: a line too many where every row is alike (the count, the lines), a row lost there (missing, no place)");
    {
        size_t len;
        char *text = file_read("tests/fixtures/everything.vtt", 1 << 20, &len, NULL);
        CHECK(text != NULL);
        MapioSection secs[MAPIO_SECTIONS];
        mapio_sections(text, len, 16, 9, secs);
        int at = secs[MAPIO_VEDGES].v[0].line;
        mapio_sections_free(secs);
        for (int op = 0; op < 2 && text; op++) {
            FILE *f = fopen(bad, "w");
            int line = 1;
            for (const char *p = text; *p; line++) {
                const char *e = strchr(p, '\n');
                size_t n = e ? (size_t)(e - p) + 1 : strlen(p);
                if (line == at && op == 0) fputc('\n', f);           /* an empty line added */
                if (!(line == at && op == 1)) fwrite(p, 1, n, f);   /* a row lost */
                p += n;
            }
            fclose(f);
            char  *out = NULL;
            size_t olen = 0;
            FILE  *o = open_memstream(&out, &olen);
            maptools_check(o, bad, 0);
            fclose(o);
            CHECK(strstr(out, op ? "a 'vedges' row is missing" : "the map does not show which") != NULL);
            char *t = xmalloc(olen + 1), *w = t;
            size_t dl = strlen(sb.dir);
            for (const char *p = out; *p; ) {
                if (!strncmp(p, sb.dir, dl)) { memcpy(w, "DIR", 3); w += 3; p += dl; }
                else *w++ = *p++;
            }
            *w = '\0';
            golden_bytes(op ? "shift-lost-quiet" : "shift-count-only", t, strlen(t));
            free(t);
            free(out);
        }
        free(text);
    }

    CASE("two sections each a line too many for certain: both counts said, no place");
    {
        size_t len;
        char *text = file_read("tests/fixtures/everything.vtt", 1 << 20, &len, NULL);
        MapioSection secs[MAPIO_SECTIONS];
        mapio_sections(text, len, 16, 9, secs);
        int at_t = secs[MAPIO_TILES].v[0].line, at_v = secs[MAPIO_VEDGES].v[0].line;
        mapio_sections_free(secs);
        FILE *f = fopen(bad, "w");
        int line = 1;
        for (const char *p = text; *p; line++) {
            const char *e = strchr(p, '\n');
            size_t n = e ? (size_t)(e - p) + 1 : strlen(p);
            if (line == at_t || line == at_v) fputc('\n', f);
            fwrite(p, 1, n, f);
            p += n;
        }
        fclose(f);
        free(text);
        char  *out = NULL;
        size_t olen = 0;
        FILE  *o = open_memstream(&out, &olen);
        maptools_check(o, bad, 0);
        fclose(o);
        int said = 0;
        for (const char *p = out; (p = strstr(p, "with another section wrong too")); p++) said++;
        CHECK_EQ(said, 2);
        CHECK(strstr(out, "W019 stray-row") == NULL);          /* folded into the counts */
        free(out);
    }

    CASE("a count is said only when it is so: not with a header missing or repeated; 'at least' when a line past the rows was set aside");
    {
        size_t len;
        char *text = file_read("tests/fixtures/two-rooms.vtt", 1 << 20, &len, NULL);
        CHECK(text != NULL);
        static const struct { const char *what, *from, *to, *want, *never; } cases[] = {
            /* The vedges header gone: its rows read as tiles' surplus. */
            { "no header", "\nvedges\n", "\n", NULL, "W031" },
            /* A tiles block before the one the loader reads, a line too long. */
            { "repeated", "\ntiles\n", "\ntiles\n.\n.\n.\n.\n.\n.\n.\n.\n.\n.\ntiles\n", NULL, "W031" },
            /* Two empty lines into tiles, its last row with a stray character. */
            { "set aside", "\nvedges\n", "\n..x.\nvedges\n", "at least", "W031 row-extra          line 5    'tiles' has 1" },
        };
        for (size_t c = 0; c < sizeof cases / sizeof cases[0] && text; c++) {
            const char *at = strstr(text, cases[c].from);
            CHECK(at != NULL);
            if (!at) continue;
            FILE *f = fopen(bad, "w");
            fwrite(text, 1, (size_t)(at - text), f);
            if (c == 2) {
                /* Two empty lines after tiles' first row as well. */
                const char *t = strstr(text, "\ntiles\n") + 7, *e = strchr(t, '\n') + 1;
                fseek(f, 0, SEEK_SET);
                fwrite(text, 1, (size_t)(e - text), f);
                fputs("\n\n", f);
                fwrite(e, 1, (size_t)(at - e), f);
            }
            fputs(cases[c].to, f);
            fputs(at + strlen(cases[c].from), f);
            fclose(f);
            char  *out = NULL;
            size_t olen = 0;
            FILE  *o = open_memstream(&out, &olen);
            maptools_check(o, bad, 0);
            fclose(o);
            if (cases[c].want) CHECK(strstr(out, cases[c].want) != NULL);
            if (c < 2) CHECK(strstr(out, cases[c].never) == NULL);
            if (getenv("VTT_SHIFT_SAY")) printf("    %s:\n%s", cases[c].what, out);
            free(out);
        }
        free(text);
    }

    CASE("a map with flaws of its own: a pasted empty row is not placed where a wrong place scores better than the truth; the count says why");
    {
        char  *out = NULL;
        size_t olen = 0;
        FILE  *o = open_memstream(&out, &olen);
        maptools_check(o, "tests/fixtures/shift-flawed.vtt", 1);
        fclose(o);
        CHECK(strstr(out, "\"span\":{") == NULL);
        CHECK(strstr(out, "the map has mistakes of its own") != NULL);
        free(out);
    }

    CASE("two readings, one witness: a line too many in vedges, or hedges lost a row under a separator -- both said, no place");
    for (int json = 0; json < 2; json++) {
        char  *out = NULL;
        size_t olen = 0;
        FILE  *o = open_memstream(&out, &olen);
        maptools_check(o, "tests/fixtures/shift-either-one.vtt", json);
        fclose(o);
        CHECK(strstr(out, "the text cannot tell which") != NULL);
        CHECK(strstr(out, "\"span\":{") == NULL);
        golden_bytes(json ? "shift-either-one-json" : "shift-either-one", out, olen);
        free(out);
    }

    CASE("a separator read as a lost row's place (the count right) is never placed wrong: such a file reads like a correct one with a blank last row");
    {
        Map *m = pair_map();
        CHECK_EQ(mapio_write(m, path, err, sizeof err), 0);
        size_t len;
        char *text = file_read(path, 1 << 20, &len, NULL);
        MapioSection secs[MAPIO_SECTIONS];
        mapio_sections(text, len, m->w, m->h, secs);
        /* Separators after every section, as a hand-writer keeps them; then
         * a tiles row lost, so the separator reads as tiles' last row. */
        int lost = secs[MAPIO_TILES].v[2].line;
        size_t sl;
        char *st = with_separators(text, len, m->w, m->h, &sl);
        FILE *f = fopen(bad, "w");
        int line = 1;
        for (const char *p = st; *p; line++) {
            const char *e = strchr(p, '\n');
            size_t n = e ? (size_t)(e - p) + 1 : strlen(p);
            if (line != lost) fwrite(p, 1, n, f);
            p += n;
        }
        fclose(f);
        free(st);
        int a = 0, b = 0; char code[8] = "";
        if (check_span(bad, &a, &b, code)) CHECK(a <= lost - 1 && lost - 1 <= b);
        mapio_sections_free(secs);
        free(text);
        map_free(m);
    }

    CASE("never a false positive: a correct file with a blank after some sections only (flaws of its own or not) says nothing of its rows; a file with a blank after every section and two rows lost names no place");
    {
        unsigned fseed = 4242;
        int alarms = 0, placed = 0, files = 0, hidden_told = 0, hidden_wrong = 0;
        for (int i = 0; i < 60; i++) {
            Map *m = i % 5 == 4 ? rooms_at_random(&fseed) : shape_at_random(&fseed, i % 5);
            if (i % 2) {                       /* a flaw of its own: a wall out on the void */
                for (int k = 0; k < 40; k++) {
                    int x = (int)((unsigned)rand_r(&fseed) % (unsigned)m->w), y = (int)((unsigned)rand_r(&fseed) % (unsigned)m->h);
                    if (map_tile(m, x, y) == TILE_VOID && (x == 0 || map_tile(m, x - 1, y) == TILE_VOID)) {
                        map_set_vedge(m, x, y, EDGE_WALL);
                        break;
                    }
                }
            }
            CHECK_EQ(mapio_write(m, path, err, sizeof err), 0);
            size_t len;
            char *text = file_read(path, 1 << 22, &len, NULL);
            for (int mask = 1; mask < 32; mask++) {
                size_t sl;
                /* Bit 3: two blanks, not one; bit 4: blanks of spaces. */
                int nbl = mask & 8 ? 2 : 1;
                char *st = with_separators_in(text, len, m->w, m->h, mask & 7, mask & 16 ? -nbl : nbl, &sl);
                if (!(mask & 7)) { free(st); continue; }
                FILE *f = fopen(bad, "w");
                fwrite(st, 1, sl, f);
                fclose(f);
                free(st);
                files++;
                if (check_says_rows(bad)) { alarms++; if (getenv("VTT_SHIFT_SAY")) printf("    false alarm: map %d mask %d\n", i, mask); }
            }
            /* Each section ended its own way, mixed: nothing, one blank or
             * two, a line of spaces, a comment or a tab the loader ignores. */
            static const char *const ends[] = { "", "\n", "\n\n", "   \n", "# note\n", "\t\n", "# see the door.\n", "lights\n", "\n# wall ----\n" };
            for (int t = 0; t < 20; t++) {
                const char *after[MAPIO_SECTIONS] = { "", "", "", "" };
                for (int k = 0; k < 3; k++) after[k] = ends[(unsigned)rand_r(&fseed) % 9u];
                size_t al;
                char *st2 = with_after(text, len, m->w, m->h, after, &al);
                FILE *f = fopen(bad, "w");
                fwrite(st2, 1, al, f);
                fclose(f);
                free(st2);
                files++;
                if (check_says_rows(bad)) { alarms++; if (getenv("VTT_SHIFT_SAY")) printf("    false alarm: map %d ends %d %d %d\n", i, (int)strlen(after[0]), (int)strlen(after[1]), (int)strlen(after[2])); }
            }
            /* Separators everywhere, a row lost in each of two sections. */
            size_t sl;
            char *st = with_separators(text, len, m->w, m->h, &sl);
            MapioSection secs[MAPIO_SECTIONS];
            mapio_sections(st, sl, m->w, m->h, secs);
            for (int pair = 0; pair < 3; pair++) {
                int ka = pair == 2 ? MAPIO_VEDGES : MAPIO_TILES, kb = pair == 0 ? MAPIO_VEDGES : MAPIO_HEDGES;
                int la = secs[ka].v[(int)((unsigned)rand_r(&fseed) % (unsigned)secs[ka].rows)].line;
                int lb = secs[kb].v[(int)((unsigned)rand_r(&fseed) % (unsigned)secs[kb].rows)].line;
                FILE *f = fopen(bad, "w");
                int line = 1;
                for (const char *p = st; *p; line++) {
                    const char *e = strchr(p, '\n');
                    size_t n = e ? (size_t)(e - p) + 1 : strlen(p);
                    if (line != la && line != lb) fwrite(p, 1, n, f);
                    p += n;
                }
                fclose(f);
                int a, b; char code[8];
                if (check_span(bad, &a, &b, code)) { placed++; if (getenv("VTT_SHIFT_SAY")) printf("    placed: map %d lines %d %d span %d-%d\n", i, la, lb, a, b); }
            }
            mapio_sections_free(secs);
            free(st);
            /* Two blanks after one section, a row lost there (the count
             * then right, the loss unseen) and a row repeated in another:
             * a place told is the repeated row's run, or none. */
            for (int t = 0; t < 3; t++) {
                int kw = t, kx = (t + 1 + (int)((unsigned)rand_r(&fseed) % 2u)) % 3;
                char *s2 = with_separators_in(text, len, m->w, m->h, 1 << kw, 2, &sl);
                mapio_sections(s2, sl, m->w, m->h, secs);
                int lw = secs[kw].v[(int)((unsigned)rand_r(&fseed) % (unsigned)secs[kw].rows)].line;
                int ld = secs[kx].v[(int)((unsigned)rand_r(&fseed) % (unsigned)secs[kx].rows)].line;
                char **lines = NULL;
                int nl = 0;
                for (const char *p = s2; *p; ) {
                    const char *e = strchr(p, '\n');
                    size_t n = e ? (size_t)(e - p) : strlen(p);
                    lines = xrealloc(lines, (size_t)(nl + 2) * sizeof *lines);
                    lines[nl] = xmalloc(n + 1);
                    memcpy(lines[nl], p, n);
                    lines[nl++][n] = '\0';
                    p += n + (e ? 1 : 0);
                }
                FILE *f = fopen(bad, "w");
                for (int i2 = 1; i2 <= nl; i2++) {
                    if (i2 == lw) continue;
                    fprintf(f, "%s\n", lines[i2 - 1]);
                    if (i2 == ld) fprintf(f, "%s\n", lines[i2 - 1]);
                }
                fclose(f);
                /* The run of lines like the repeated one, as written. */
                int lo = ld, hi = ld;
                while (lo > 1 && !strcmp(lines[lo - 2], lines[ld - 1])) lo--;
                while (hi < nl && !strcmp(lines[hi], lines[ld - 1])) hi++;
                int shift = lw < ld ? -1 : 0;
                lo += shift; hi += shift + 1;
                int a, b; char code[8];
                if (check_span(bad, &a, &b, code)) {
                    hidden_told++;
                    if (a < lo || b > hi) { hidden_wrong++; if (getenv("VTT_SHIFT_SAY")) printf("    hidden loss: map %d lost %d repeated %d told %d-%d\n", i, lw, ld, a, b); }
                }
                for (int i2 = 0; i2 < nl; i2++) free(lines[i2]);
                free(lines);
                mapio_sections_free(secs);
                free(s2);
            }
            free(text);
            map_free(m);
        }
        printf("    blank after some sections: %d files, %d said; two rows lost under separators: %d placed; a loss under two blanks beside a repeated row: %d placed, %d wrong\n",
               files, alarms, placed, hidden_told, hidden_wrong);
        CHECK_EQ(alarms, 0);
        CHECK_EQ(placed, 0);
        CHECK_EQ(hidden_wrong, 0);
    }

    CASE("a line repeated, an empty line added or a line lost in tiles, vedges or hedges: found where it was, or not said");
    static char blank[1];
    int found = 0, silent = 0, silent_harm = 0, wrong = 0, trials = 0, fog_said = 0, told_unplaced = 0;
    int by_found[3][3] = { { 0 } }, by_trials[3][3] = { { 0 } }, widths[3] = { 0 };
    int rounds = getenv("VTT_SHIFT_ROUNDS") ? atoi(getenv("VTT_SHIFT_ROUNDS")) : 120;
    int false_alarms = 0, by_shape_wrong[4] = { 0 }, by_shape_found[4] = { 0 }, by_shape_harm[4] = { 0 }, by_shape_trials[4] = { 0 };
    for (int round = 0; round < rounds; round++) {
        int shape = round % 4, sep = (round / 4) % 2;
        Map *m = shape_at_random(&seed, shape);
        CHECK_EQ(mapio_write(m, path, err, sizeof err), 0);
        size_t len;
        char *text = file_read(path, 1 << 20, &len, NULL);
        if (sep) {
            size_t sl;
            char *st = with_separators(text, len, m->w, m->h, &sl);
            free(text);
            text = st;
            len = sl;
            FILE *sf = fopen(path, "w");
            fwrite(text, 1, len, sf);
            fclose(sf);
        }
        {
            int a, b; char code[8];
            if (check_span(path, &a, &b, code)) {
                false_alarms++;
                if (false_alarms <= 3) fprintf(stderr, "  a false alarm: shape %d, separators %d, said %s %d-%d\n", shape, sep, code, a, b);
            }
        }
        MapioSection secs[MAPIO_SECTIONS];
        mapio_sections(text, len, m->w, m->h, secs);
        /* The file as lines. */
        int nl = 0;
        for (size_t i = 0; i < len; i++) nl += text[i] == '\n';
        char **lines = xmalloc((size_t)(nl + 2) * sizeof *lines);
        int k = 0;
        for (char *p = text; *p; ) {
            char *e = strchr(p, '\n');
            if (!e) break;
            *e = '\0';
            lines[k++] = p;
            p = e + 1;
        }
        for (int t = 0; t < 6; t++) {
            int kind = (int)((seed = seed * 1103515245u + 12345u) >> 16) % 3;
            int op   = (int)((seed = seed * 1103515245u + 12345u) >> 16) % 3;
            const MapioSection *sec = &secs[kind];
            int r = (int)((seed = seed * 1103515245u + 12345u) >> 16) % (sec->n < sec->rows ? sec->n : sec->rows);
            int at = sec->v[r].line - 1;                    /* 0-based index into lines */
            char **mut = xmalloc((size_t)(k + 2) * sizeof *mut);
            int    mk = 0, changed = 0;
            for (int i = 0; i < k; i++) {
                if (op == 2 && i == at) continue;            /* lost */
                mut[mk++] = lines[i];
                if (i == at && op == 0) { mut[mk++] = lines[i]; changed = mk; }   /* repeated: the copy's line */
                if (i == at && op == 1) { mut[mk++] = blank; changed = mk; }      /* an empty line added */
            }
            if (op == 2) changed = at;                       /* the fix goes after the line before it */
            size_t ml;
            char *mt = lines_join(mut, mk, &ml);
            FILE *f = fopen(bad, "w");
            fwrite(mt, 1, ml, f);
            fclose(f);
            int a, b; char code[8] = "";
            trials++;
            by_trials[kind][op]++;
            by_shape_trials[shape]++;
            int before_unplaced = unplaced;
            int spanned = check_span(bad, &a, &b, code);
            if (!spanned && unplaced > before_unplaced) told_unplaced++;
            else if (!spanned) {
                silent++;
                /* Silent is right when the edit changed nothing the map
                 * holds (an empty line among empty rows at the bottom). */
                Map *mb = mapio_load(bad, err, sizeof err);
                char *d0 = printed(m, 0), *d1 = mb ? printed(mb, 0) : NULL;
                if (d1 && strcmp(d0, d1)) {
                    silent_harm++;
                    by_shape_harm[shape]++;
                    if (getenv("VTT_SHIFT_KEEP") && silent_harm <= 6) {
                        char keep[800];
                        snprintf(keep, sizeof keep, "%s/miss%d-%s-op%d-line%d.vtt", getenv("VTT_SHIFT_KEEP"), silent_harm,
                                 kind == 0 ? "tiles" : kind == 1 ? "vedges" : "hedges", op, changed);
                        FILE *kf = fopen(keep, "w");
                        if (kf) { fwrite(mt, 1, ml, kf); fclose(kf); }
                    }
                }
                free(d0); free(d1);
                map_free(mb);
            }
            else if (a <= changed && changed <= b) { found++; by_found[kind][op]++; by_shape_found[shape]++; widths[islower((unsigned char)code[0]) ? 2 : b - a < 1 ? 0 : 1]++; }
            else {
                wrong++;
                by_shape_wrong[shape]++;
                if (wrong <= 3) fprintf(stderr, "  shift misplaced: shape %d sep %d %s op %d at line %d, said %s %d-%d\n", shape, sep,
                                        kind == 0 ? "tiles" : kind == 1 ? "vedges" : "hedges", op, changed, code, a, b);
            }
            free(mt);
            free(mut);
        }
        /* A fog row: never searched, so never a span. The map gets a patch
         * over its first room, saved, then a fog line repeated or lost. */
        {
            str_lcpy(m->fog_patches[0].name, "Crypt", sizeof m->fog_patches[0].name);
            m->fog_patches[0].reveal = 2;
            for (int y = 0; y < m->h; y++)
                for (int x = 0; x < m->w / 2; x++) map_fog_set(m, x, y, 1);
            CHECK_EQ(mapio_write(m, path, err, sizeof err), 0);
            size_t fl;
            char *ft = file_read(path, 1 << 20, &fl, NULL);
            MapioSection fs2[MAPIO_SECTIONS];
            mapio_sections(ft, fl, m->w, m->h, fs2);
            const MapioSection *fsec = &fs2[MAPIO_FOG];
            if (fsec->n > 2) {
                int at = fsec->v[fsec->n / 2].line;          /* 1-based */
                FILE *f = fopen(bad, "w");
                int line = 1;
                for (char *p = ft; *p; line++) {
                    char *e = strchr(p, '\n');
                    size_t n = e ? (size_t)(e - p) + 1 : strlen(p);
                    fwrite(p, 1, n, f);
                    if (line == at) fwrite(p, 1, n, f);      /* repeated */
                    p += n;
                }
                fclose(f);
                int a, b; char code[8];
                fog_said += check_span(bad, &a, &b, code);
            }
            mapio_sections_free(fs2);
            free(ft);
        }
        free(lines);
        mapio_sections_free(secs);
        free(text);
        map_free(m);
    }
    /* The shipped maps, every line of every row section, each way. */
    static const char *const shipped[] = {
        "tests/fixtures/areas.vtt", "tests/fixtures/crowd.vtt", "tests/fixtures/env.vtt",
        "tests/fixtures/everything.vtt", "tests/fixtures/floors.vtt", "tests/fixtures/kinds.vtt",
        "tests/fixtures/links.vtt", "tests/fixtures/two-rooms.vtt", "tests/oracle/maps/v3.vtt",
        "tests/oracle/maps/v10.vtt", "tests/oracle/maps/v12.vtt", "tests/oracle/maps/w024.vtt",
    };
    int sfound = 0, strials = 0, swrong = 0, sharm = 0, sunplaced = 0;
    for (size_t f = 0; f < sizeof shipped / sizeof shipped[0]; f++) {
        char here[1024];
        snprintf(here, sizeof here, "%s/%s", sb.cwd, shipped[f]);
        Map *m = mapio_load(here, err, sizeof err);
        if (!m) continue;
        CHECK_EQ(mapio_write(m, path, err, sizeof err), 0);
        size_t len;
        char *text = file_read(path, 1 << 20, &len, NULL);
        MapioSection secs[MAPIO_SECTIONS];
        mapio_sections(text, len, m->w, m->h, secs);
        int nl = 0;
        for (size_t i = 0; i < len; i++) nl += text[i] == '\n';
        char **lines = xmalloc((size_t)(nl + 2) * sizeof *lines);
        int k = 0;
        for (char *p = text; *p; ) { char *e = strchr(p, '\n'); if (!e) break; *e = '\0'; lines[k++] = p; p = e + 1; }
        for (int kind = 0; kind < 3; kind++)
            for (int r = 0; r < secs[kind].n; r++)
                for (int op = 0; op < 3; op++) {
                    int at = secs[kind].v[r].line - 1, mk = 0, changed = 0;
                    char **mut = xmalloc((size_t)(k + 2) * sizeof *mut);
                    for (int i = 0; i < k; i++) {
                        if (op == 2 && i == at) continue;
                        mut[mk++] = lines[i];
                        if (i == at && op == 0) { mut[mk++] = lines[i]; changed = mk; }
                        if (i == at && op == 1) { mut[mk++] = blank; changed = mk; }
                    }
                    if (op == 2) changed = at;
                    size_t ml;
                    char *mt = lines_join(mut, mk, &ml);
                    FILE *bf = fopen(bad, "w");
                    fwrite(mt, 1, ml, bf);
                    fclose(bf);
                    int a, b; char code[8] = "";
                    strials++;
                    int before = unplaced;
                    int spanned = check_span(bad, &a, &b, code);
                    if (!spanned && unplaced > before) sunplaced++;
                    else if (!spanned) {
                        Map *mb = mapio_load(bad, err, sizeof err);
                        char *d0 = printed(m, 0), *d1 = mb ? printed(mb, 0) : NULL;
                        if (d1 && strcmp(d0, d1)) {
                            sharm++;
                            if (getenv("VTT_SHIFT_SAY")) fprintf(stderr, "    silent: %s %s op %d line %d\n", shipped[f],
                                                                kind == 0 ? "tiles" : kind == 1 ? "vedges" : "hedges", op, changed);
                        }
                        free(d0); free(d1);
                        map_free(mb);
                    } else {
                        if (a <= changed && changed <= b) sfound++;
                        else if (++swrong <= 30) fprintf(stderr, "  shift misplaced in %s: %s op %d at line %d, said %s %d-%d\n",
                                                        shipped[f], kind == 0 ? "tiles" : kind == 1 ? "vedges" : "hedges", op, changed, code, a, b);
                    }
                    free(mt);
                    free(mut);
                }
        free(lines);
        mapio_sections_free(secs);
        free(text);
        map_free(m);
    }
    fprintf(stderr, "  the shipped maps: %d of %d found, %d said without a place, %d not said that changed the map, %d misplaced\n",
            sfound, strials, sunplaced, sharm, swrong);
    CHECK_EQ(swrong, 0);
    /* Most edits that change the map are placed. The rest are on maps
     * whose only evidence is loose wall ends (all floor, walls drawn on it:
     * kinds.vtt, two-rooms.vtt), which place nothing alone, or with
     * mistakes of their own (two-rooms.vtt's four loose ends), or where a
     * row with a twin elsewhere in its section could have been pasted --
     * silence over a guess. */
    CHECK(sharm * 8 <= strials);
    fprintf(stderr, "  the line search: %d of %d found, %d said without a place, %d not said (%d of them changed the map), %d misplaced\n",
            found, trials, told_unplaced, silent, silent_harm, wrong);
    fprintf(stderr, "    answers: %d one line, %d a run of like lines (one row), %d a span of candidates\n", widths[0], widths[1], widths[2]);
    for (int kind = 0; kind < 3; kind++)
        fprintf(stderr, "    %-6s repeated %d/%d, an empty line added %d/%d, lost %d/%d\n",
                kind == 0 ? "tiles" : kind == 1 ? "vedges" : "hedges", by_found[kind][0], by_trials[kind][0],
                by_found[kind][1], by_trials[kind][1], by_found[kind][2], by_trials[kind][2]);
    fprintf(stderr, "    unedited files with a span: %d of %d; misplaced by shape (rooms, cave, pillars, border): %d %d %d %d\n",
            false_alarms, rounds, by_shape_wrong[0], by_shape_wrong[1], by_shape_wrong[2], by_shape_wrong[3]);
    for (int sh = 0; sh < 4; sh++)
        fprintf(stderr, "    %-7s %d of %d found, %d that changed the map not said\n",
                sh == 0 ? "rooms" : sh == 1 ? "cave" : sh == 2 ? "pillars" : "border", by_shape_found[sh], by_shape_trials[sh], by_shape_harm[sh]);
    CHECK_EQ(wrong, 0);
    CHECK_EQ(false_alarms, 0);
    CHECK(found * 10 >= trials * 3);                    /* a floor, not the aim: the aim is none misplaced */
    CHECK_EQ(fog_said, 0);

    sandbox_leave(&sb);
}
