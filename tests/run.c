/* Minimal test harness. Zero dependencies here too: a counter, a macro, and
 * a list of functions. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "app.h"
#include "editor.h"
#include "grid.h"
#include "map.h"
#include "mapio.h"
#include "theme.h"
#include "token.h"
#include "play.h"
#include "ruler.h"
#include "undo.h"
#include "draw.h"
#include "keys.h"
#include "util.h"
#include "ui.h"
#include "input.h"
#include "prof.h"
#include "render.h"
#include "util.h"

static int g_checks;
static int g_fails;
static const char *g_case = "";

#define CHECK(cond)                                                            \
    do {                                                                       \
        g_checks++;                                                            \
        if (!(cond)) {                                                         \
            g_fails++;                                                         \
            fprintf(stderr, "  FAIL %s:%d [%s] %s\n",                          \
                    __FILE__, __LINE__, g_case, #cond);                        \
        }                                                                      \
    } while (0)

#define CHECK_EQ(a, b)                                                         \
    do {                                                                       \
        long long va = (long long)(a), vb = (long long)(b);                    \
        g_checks++;                                                            \
        if (va != vb) {                                                        \
            g_fails++;                                                         \
            fprintf(stderr, "  FAIL %s:%d [%s] %s == %s (%lld vs %lld)\n",     \
                    __FILE__, __LINE__, g_case, #a, #b, va, vb);               \
        }                                                                      \
    } while (0)

#define CASE(name) do { g_case = (name); } while (0)

/* ------------------------------------------------------------------ utf8 */

static void test_utf8(void)
{
    CASE("utf8 roundtrip");
    static const uint32_t cps[] = {
        'A', 0x00E9u, 0x2500u, 0x2503u, 0x25CFu, 0x256Du, 0x1F600u, 0x10FFFFu
    };
    for (size_t i = 0; i < sizeof cps / sizeof *cps; i++) {
        char     enc[4];
        int      n = utf8_encode(cps[i], enc);
        uint32_t back;
        int      m = utf8_decode(enc, (size_t)n, &back);
        CHECK_EQ(n, m);
        CHECK_EQ(back, cps[i]);
    }

    CASE("utf8 rejects surrogates and out-of-range");
    char tmp[4];
    CHECK_EQ(utf8_encode(0xD800u, tmp), 0);
    CHECK_EQ(utf8_encode(0x110000u, tmp), 0);

    CASE("utf8 malformed input always makes progress");
    uint32_t cp;
    CHECK_EQ(utf8_decode("\x80", 1, &cp), 1);   /* stray continuation byte */
    CHECK_EQ(cp, 0xFFFDu);
    CHECK_EQ(utf8_decode("\xC0\x80", 2, &cp), 1); /* overlong NUL */
    CHECK_EQ(cp, 0xFFFDu);

    /* utf8_width() short-circuits a whole block of codepoints to width 1 so
     * that drawing a wall segment does not cost two binary searches per cell.
     * That is only safe if neither table has an entry in the window, which is
     * checked here exhaustively rather than trusted to a comment. */
    CASE("the width fast path agrees with the tables across its whole window");
    for (uint32_t w = UTIL_WIDTH_FASTPATH_LO; w < UTIL_WIDTH_FASTPATH_HI; w++)
        if (utf8_width(w) != utf8_width_slow(w)) {
            CHECK_EQ(utf8_width(w), utf8_width_slow(w));
            break;
        }
    CHECK_EQ(utf8_width(UTIL_WIDTH_FASTPATH_LO - 1),
             utf8_width_slow(UTIL_WIDTH_FASTPATH_LO - 1));
    CHECK_EQ(utf8_width(UTIL_WIDTH_FASTPATH_HI),
             utf8_width_slow(UTIL_WIDTH_FASTPATH_HI));
    CHECK_EQ(utf8_width(0x2E80u), 2);          /* the window's upper neighbour */
    CHECK_EQ(utf8_width(0x20D0u), 0);          /* a combining mark just below */

    CASE("utf8 width");
    CHECK_EQ(utf8_width('A'), 1);
    CHECK_EQ(utf8_width(0x2500u), 1);      /* box drawing must be narrow */
    CHECK_EQ(utf8_width(0x25CFu), 1);      /* the player-token circle */
    CHECK_EQ(utf8_width(0x0301u), 0);      /* combining acute */
    CHECK_EQ(utf8_width(0x4E00u), 2);      /* CJK */
}

/* ----------------------------------------------------------------- input */

static Key next_key(InputParser *p)
{
    Key k;
    memset(&k, 0, sizeof k);
    if (!input_next(p, &k)) k.kind = KEY_NONE;
    return k;
}

static void feed(InputParser *p, const char *s)
{
    input_init(p);
    input_feed(p, s, strlen(s));
}

static void test_input(void)
{
    InputParser p;
    Key         k;

    CASE("plain characters");
    feed(&p, "hjkl");
    for (const char *c = "hjkl"; *c; c++) {
        k = next_key(&p);
        CHECK_EQ(k.kind, KEY_CHAR);
        CHECK_EQ(k.ch, (uint32_t)*c);
        CHECK_EQ(k.mods, 0);
    }

    CASE("control keys");
    feed(&p, "\x01");
    k = next_key(&p);
    CHECK_EQ(k.kind, KEY_CHAR);
    CHECK_EQ(k.ch, 'a');
    CHECK_EQ(k.mods, MOD_CTRL);

    feed(&p, "\r");
    CHECK_EQ(next_key(&p).kind, KEY_ENTER);
    feed(&p, "\t");
    CHECK_EQ(next_key(&p).kind, KEY_TAB);
    feed(&p, "\x7f");
    CHECK_EQ(next_key(&p).kind, KEY_BACKSPACE);

    CASE("arrow keys");
    feed(&p, "\x1b[A\x1b[B\x1b[C\x1b[D");
    CHECK_EQ(next_key(&p).kind, KEY_UP);
    CHECK_EQ(next_key(&p).kind, KEY_DOWN);
    CHECK_EQ(next_key(&p).kind, KEY_RIGHT);
    CHECK_EQ(next_key(&p).kind, KEY_LEFT);

    CASE("modified arrows");
    feed(&p, "\x1b[1;5A");           /* Ctrl-Up */
    k = next_key(&p);
    CHECK_EQ(k.kind, KEY_UP);
    CHECK_EQ(k.mods, MOD_CTRL);

    CASE("function keys");
    feed(&p, "\x1b[24~");
    CHECK_EQ(next_key(&p).kind, KEY_F12);
    feed(&p, "\x1bOP");
    CHECK_EQ(next_key(&p).kind, KEY_F1);
    feed(&p, "\x1b[15~");
    CHECK_EQ(next_key(&p).kind, KEY_F5);

    CASE("alt-prefixed keys");
    feed(&p, "\x1b" "x");
    k = next_key(&p);
    CHECK_EQ(k.kind, KEY_CHAR);
    CHECK_EQ(k.ch, 'x');
    CHECK_EQ(k.mods, MOD_ALT);

    CASE("CSI u codepoints");
    feed(&p, "\x1b[97;5u");          /* Ctrl-a via the kitty protocol */
    k = next_key(&p);
    CHECK_EQ(k.kind, KEY_CHAR);
    CHECK_EQ(k.ch, 'a');
    CHECK_EQ(k.mods, MOD_CTRL);

    /* This is the case that makes Escape usable at all: a lone ESC must not
     * be reported until the terminal has had a chance to send more. */
    CASE("lone ESC waits for the timeout");
    feed(&p, "\x1b");
    CHECK_EQ(next_key(&p).kind, KEY_NONE);
    CHECK(input_pending(&p));
    CHECK_EQ(input_timeout(&p, &k), 1);
    CHECK_EQ(k.kind, KEY_ESC);

    CASE("split escape sequence reassembles");
    input_init(&p);
    input_feed(&p, "\x1b[", 2);
    CHECK_EQ(next_key(&p).kind, KEY_NONE);   /* incomplete, hold */
    input_feed(&p, "A", 1);
    CHECK_EQ(next_key(&p).kind, KEY_UP);

    CASE("split UTF-8 scalar reassembles");
    input_init(&p);
    input_feed(&p, "\xC3", 1);
    CHECK_EQ(next_key(&p).kind, KEY_NONE);
    input_feed(&p, "\xA9", 1);
    k = next_key(&p);
    CHECK_EQ(k.kind, KEY_CHAR);
    CHECK_EQ(k.ch, 0x00E9u);

    CASE("held-key burst yields every keystroke");
    feed(&p, "jjjjjjjjjj");
    int n = 0;
    while (next_key(&p).kind != KEY_NONE) n++;
    CHECK_EQ(n, 10);

    CASE("garbage cannot wedge the parser");
    input_init(&p);
    for (size_t i = 0; i < sizeof p.buf; i++) input_feed(&p, "\x1b", 1);
    /* Must terminate: the parser drops a byte rather than spinning. */
    while (next_key(&p).kind != KEY_NONE) { }
    CHECK(1);
}

/* ---------------------------------------------------------------- render */

static void test_render(void)
{
    Renderer r;
    rnd_init(&r);
    rnd_resize(&r, 40, 10);

    Style s = style(COL_DEFAULT, COL_DEFAULT, 0);

    CASE("first frame paints everything");
    rnd_begin(&r);
    draw_text(&r, 0, 0, "hello", -1, s);
    rnd_flush(&r, NULL);
    CHECK_EQ(r.cells_changed, 400);      /* force_full after resize */

    CASE("an identical frame writes nothing");
    rnd_begin(&r);
    draw_text(&r, 0, 0, "hello", -1, s);
    rnd_flush(&r, NULL);
    CHECK_EQ(r.cells_changed, 0);

    /* The whole point of the diff: a one-cell edit must not repaint the
     * screen, and must cost tens of bytes rather than thousands. */
    CASE("a one-cell change emits a handful of bytes");
    rnd_begin(&r);
    draw_text(&r, 0, 0, "hellp", -1, s);
    rnd_flush(&r, NULL);
    CHECK_EQ(r.cells_changed, 1);
    CHECK(r.bytes_written < 40);

    CASE("clipping at the screen edge is silent");
    rnd_begin(&r);
    draw_text(&r, 38, 9, "overflowing", -1, s);
    draw_cell(&r, -5, -5, 'x', s);
    draw_cell(&r, 999, 999, 'x', s);
    rnd_flush(&r, NULL);
    CHECK(1);                            /* no crash, no out-of-bounds write */

    /* Regression: the map viewport scrolls, and without a clip it painted
     * over the title bar and the keybinding bar. */
    CASE("drawing outside the clip is discarded");
    rnd_begin(&r);
    ClipRect saved = rnd_clip_push(&r, 5, 2, 10, 3);
    draw_fill(&r, rect(0, 0, 40, 10), '#', s);
    rnd_clip_restore(&r, saved);
    CHECK_EQ(rnd_at(&r, 0, 0)->ch, ' ');       /* above-left of the clip */
    CHECK_EQ(rnd_at(&r, 5, 1)->ch, ' ');       /* one row above */
    CHECK_EQ(rnd_at(&r, 4, 2)->ch, ' ');       /* one column left */
    CHECK_EQ(rnd_at(&r, 5, 2)->ch, '#');       /* the clip's own corner */
    CHECK_EQ(rnd_at(&r, 14, 4)->ch, '#');      /* its far corner */
    CHECK_EQ(rnd_at(&r, 15, 4)->ch, ' ');      /* one past it */
    CHECK_EQ(rnd_at(&r, 14, 5)->ch, ' ');

    CASE("the clip is restored, and nesting only narrows");
    CHECK_EQ(r.clip_x1, 40);
    ClipRect outer = rnd_clip_push(&r, 5, 5, 10, 10);
    ClipRect inner = rnd_clip_push(&r, 0, 0, 40, 40);   /* asks for more */
    CHECK_EQ(r.clip_x0, 5);                              /* still narrowed */
    CHECK_EQ(r.clip_x1, 15);
    rnd_clip_restore(&r, inner);
    rnd_clip_restore(&r, outer);

    CASE("every frame starts unclipped");
    rnd_clip_push(&r, 1, 1, 2, 2);
    rnd_begin(&r);
    CHECK_EQ(r.clip_x0, 0);
    CHECK_EQ(r.clip_x1, 40);

    CASE("resize forces a full repaint");
    rnd_resize(&r, 20, 5);
    rnd_begin(&r);
    rnd_flush(&r, NULL);
    CHECK_EQ(r.cells_changed, 100);

    rnd_free(&r);
}

static void test_draw(void)
{
    Renderer r;
    rnd_init(&r);
    rnd_resize(&r, 20, 6);
    rnd_begin(&r);

    Style s = style(COL_DEFAULT, COL_DEFAULT, 0);

    CASE("text width and truncation");
    CHECK_EQ(text_width("abc"), 3);
    CHECK_EQ(text_width("─│┼"), 3);
    CHECK_EQ(draw_text(&r, 0, 0, "abcdef", 3, s), 3);

    CASE("ellipsis marks truncation");
    draw_text_ellipsis(&r, 0, 1, "abcdefgh", 4, s);
    CHECK_EQ(rnd_at(&r, 3, 1)->ch, 0x2026u);

    CASE("box corners land where expected");
    draw_box(&r, rect(0, 2, 5, 3), &BOX_LIGHT, s);
    CHECK_EQ(rnd_at(&r, 0, 2)->ch, BOX_LIGHT.tl);
    CHECK_EQ(rnd_at(&r, 4, 2)->ch, BOX_LIGHT.tr);
    CHECK_EQ(rnd_at(&r, 0, 4)->ch, BOX_LIGHT.bl);
    CHECK_EQ(rnd_at(&r, 4, 4)->ch, BOX_LIGHT.br);

    CASE("centering");
    Rect c = rect_center(rect(0, 0, 20, 6), 10, 2);
    CHECK_EQ(c.x, 5);
    CHECK_EQ(c.y, 2);

    rnd_free(&r);
}

/* ------------------------------------------------------------------ util */

static void test_util(void)
{
    CASE("arena alignment and reset");
    Arena a;
    arena_init(&a, 4096);
    char *p1 = ARENA_NEW(&a, char, 3);
    uint64_t *p2 = ARENA_NEW(&a, uint64_t, 4);
    CHECK(p1 != NULL);
    CHECK_EQ((uintptr_t)p2 % _Alignof(uint64_t), 0);
    size_t used = a.used;
    CHECK(used >= 3 + 32);
    arena_reset(&a);
    CHECK_EQ(a.used, 0);
    CHECK_EQ(a.peak, used);
    arena_free(&a);

    CASE("byte buffer growth and integer formatting");
    ByteBuf b;
    bb_init(&b, 4);
    bb_puts(&b, "abc");
    bb_putu(&b, 0);
    bb_putu(&b, 12345);
    bb_putc(&b, '!');
    CHECK_EQ(b.len, 3 + 1 + 5 + 1);
    CHECK_EQ(memcmp(b.data, "abc012345!", 10), 0);
    bb_free(&b);

    CASE("str_lcpy truncates and terminates");
    char dst[4];
    CHECK_EQ(str_lcpy(dst, "abcdef", sizeof dst), 6);
    CHECK_EQ(strcmp(dst, "abc"), 0);
}

/* ------------------------------------------------------------------- app */

static void test_app_smoke(void)
{
    Renderer r;
    App      a;

    rnd_init(&r);
    rnd_resize(&r, 80, 24);
    app_init(&a, NULL, &r);

    CASE("draw does not touch memory it does not own");
    rnd_begin(&r);
    app_draw(&a);
    rnd_flush(&r, NULL);
    CHECK(r.cells_changed > 0);

    CASE("q quits");
    Key k = { KEY_CHAR, 0, 'q' };
    app_key(&a, k);
    CHECK_EQ(a.running, 0);

    CASE("tiny terminals do not crash the draw path");
    for (int w = 1; w <= 6; w++) {
        rnd_resize(&r, w, w);
        rnd_begin(&r);
        app_draw(&a);
        rnd_flush(&r, NULL);
    }
    CHECK(1);

    app_free(&a);
    rnd_free(&r);
}


/* ------------------------------------------------------------------- map */

static void test_map(void)
{
    Map *m = map_new(8, 6, "test");

    CASE("a new map is empty void");
    CHECK_EQ(map_tile(m, 0, 0), TILE_VOID);
    CHECK_EQ(map_walkable(m, 0, 0), 0);

    CASE("out-of-bounds reads are void, not crashes");
    CHECK_EQ(map_tile(m, -1, 0), TILE_VOID);
    CHECK_EQ(map_tile(m, 999, 999), TILE_VOID);
    map_set_tile(m, -5, -5, TILE_FLOOR);      /* must be a no-op */

    map_fill_tiles(m, 0, 0, 7, 5, TILE_FLOOR);
    CASE("fill covers the rectangle and nothing else");
    CHECK_EQ(map_tile(m, 0, 0), TILE_FLOOR);
    CHECK_EQ(map_tile(m, 7, 5), TILE_FLOOR);

    CASE("open floor blocks nothing");
    CHECK_EQ(map_blocked(m, 3, 3, 1, 0), 0);
    CHECK_EQ(map_blocked(m, 3, 3, -1, 0), 0);
    CHECK_EQ(map_blocked(m, 3, 3, 0, 1), 0);
    CHECK_EQ(map_blocked(m, 3, 3, 0, -1), 0);

    CASE("the map border blocks");
    CHECK_EQ(map_blocked(m, 0, 0, -1, 0), 1);
    CHECK_EQ(map_blocked(m, 7, 5, 1, 0), 1);

    /* A wall on the east face of (3,3) must block eastward movement out of
     * (3,3) and westward movement out of (4,3) -- the same edge, both ways. */
    CASE("a wall blocks symmetrically");
    map_set_vedge(m, 4, 3, EDGE_WALL);
    CHECK_EQ(map_blocked(m, 3, 3, 1, 0), 1);
    CHECK_EQ(map_blocked(m, 4, 3, -1, 0), 1);
    CHECK_EQ(map_blocked(m, 3, 3, 0, 1), 0);      /* other directions unaffected */
    map_set_vedge(m, 4, 3, EDGE_NONE);

    CASE("a horizontal wall blocks symmetrically");
    map_set_hedge(m, 3, 4, EDGE_WALL);
    CHECK_EQ(map_blocked(m, 3, 3, 0, 1), 1);
    CHECK_EQ(map_blocked(m, 3, 4, 0, -1), 1);
    map_set_hedge(m, 3, 4, EDGE_NONE);

    CASE("void tiles are not enterable");
    map_set_tile(m, 4, 3, TILE_VOID);
    CHECK_EQ(map_blocked(m, 3, 3, 1, 0), 1);
    map_set_tile(m, 4, 3, TILE_FLOOR);

    /* Two walls meeting at a corner must not leave a diagonal gap a token
     * could slip through. */
    CASE("diagonals cannot cut a wall corner");
    CHECK_EQ(map_blocked(m, 3, 3, 1, 1), 0);
    map_set_vedge(m, 4, 3, EDGE_WALL);
    CHECK_EQ(map_blocked(m, 3, 3, 1, 1), 1);
    map_set_vedge(m, 4, 3, EDGE_NONE);
    map_set_hedge(m, 3, 4, EDGE_WALL);
    CHECK_EQ(map_blocked(m, 3, 3, 1, 1), 1);
    map_set_hedge(m, 3, 4, EDGE_NONE);

    CASE("rect walls enclose the rectangle");
    map_rect_walls(m, 2, 2, 4, 4, EDGE_WALL);
    CHECK_EQ(map_vedge(m, 2, 3), EDGE_WALL);      /* west face  */
    CHECK_EQ(map_vedge(m, 5, 3), EDGE_WALL);      /* east face  */
    CHECK_EQ(map_hedge(m, 3, 2), EDGE_WALL);      /* north face */
    CHECK_EQ(map_hedge(m, 3, 5), EDGE_WALL);      /* south face */
    CHECK_EQ(map_vedge(m, 3, 3), EDGE_NONE);      /* interior stays open */

    CASE("you cannot walk out of a sealed room");
    CHECK_EQ(map_blocked(m, 3, 3, -1, 0), 0);     /* inside the room */
    CHECK_EQ(map_blocked(m, 2, 3, -1, 0), 1);     /* through the west wall */
    CHECK_EQ(map_blocked(m, 3, 2, 0, -1), 1);     /* through the north wall */

    map_free(m);

    CASE("resize preserves the overlapping region");
    Map *n = map_new(6, 6, "resize");
    map_fill_tiles(n, 0, 0, 5, 5, TILE_FLOOR);
    map_set_vedge(n, 2, 2, EDGE_WALL);
    map_set_hedge(n, 3, 3, EDGE_WALL);
    CHECK_EQ(map_resize(n, 10, 10), 0);
    CHECK_EQ(n->w, 10);
    CHECK_EQ(map_tile(n, 5, 5), TILE_FLOOR);
    CHECK_EQ(map_tile(n, 9, 9), TILE_VOID);       /* new area starts empty */
    CHECK_EQ(map_vedge(n, 2, 2), EDGE_WALL);
    CHECK_EQ(map_hedge(n, 3, 3), EDGE_WALL);

    CASE("shrinking drops tokens that fall outside");
    Token t = { 8, 8, 1, TOKEN_PLAYER, "Far" };
    tokens_add(&n->tokens, t);
    CHECK_EQ(n->tokens.n, 1);
    CHECK_EQ(map_resize(n, 5, 5), 0);
    CHECK_EQ(n->tokens.n, 0);
    map_free(n);
}

/* ---------------------------------------------------------------- tokens */

static void test_tokens(void)
{
    TokenList l;
    memset(&l, 0, sizeof l);

    Token a = { 1, 1, 1, TOKEN_PLAYER, "Aria" };
    Token b = { 4, 4, 2, TOKEN_ENEMY,  "Ogre" };
    tokens_add(&l, a);
    tokens_add(&l, b);

    CASE("hit testing respects the footprint");
    CHECK_EQ(tokens_at(&l, 1, 1), 0);
    CHECK_EQ(tokens_at(&l, 2, 2), -1);
    CHECK_EQ(tokens_at(&l, 4, 4), 1);
    CHECK_EQ(tokens_at(&l, 5, 5), 1);      /* the 2x2 covers this */
    CHECK_EQ(tokens_at(&l, 6, 6), -1);

    CASE("the newest token on a tile wins");
    Token c = { 4, 4, 1, TOKEN_PLAYER, "On top" };
    tokens_add(&l, c);
    CHECK_EQ(tokens_at(&l, 4, 4), 2);

    CASE("removal keeps the rest intact");
    tokens_remove(&l, 0);
    CHECK_EQ(l.n, 2);
    CHECK_EQ(strcmp(l.v[0].label, "Ogre"), 0);

    CASE("sizes are clamped to the legal footprints");
    Token big = { 0, 0, 99, TOKEN_ENEMY, "Huge" };
    int idx = tokens_add(&l, big);
    CHECK_EQ(l.v[idx].size, TOKEN_SIZE_MAX);

    tokens_free(&l);
}

/* ----------------------------------------------------------------- mapio */

static void write_file(const char *path, const char *text)
{
    FILE *f = fopen(path, "w");
    if (f) { fputs(text, f); fclose(f); }
}

static void test_mapio(void)
{
    char path[] = "/tmp/vtt-test-XXXXXX";
    int  fd = mkstemp(path);
    if (fd >= 0) close(fd);

    Map *m = map_new(12, 7, "Goblin Ambush");
    map_fill_tiles(m, 1, 1, 10, 5, TILE_FLOOR);
    map_rect_walls(m, 1, 1, 10, 5, EDGE_WALL);
    map_set_vedge(m, 5, 3, EDGE_WALL);
    m->zoom = 2;

    Token p = { 2, 2, 1, TOKEN_PLAYER, "Aria" };
    Token e = { 7, 3, 2, TOKEN_ENEMY,  "Ogre Chief" };
    tokens_add(&m->tokens, p);
    tokens_add(&m->tokens, e);

    char err[MAPIO_ERR_MAX] = { 0 };
    CASE("save succeeds");
    CHECK_EQ(mapio_save(m, path, err, sizeof err), 0);
    CHECK_EQ(m->modified, 0);          /* saving clears the dirty flag */

    Map *l = mapio_load(path, err, sizeof err);
    CASE("load round-trips every field");
    CHECK(l != NULL);
    if (l) {
        CHECK_EQ(l->w, m->w);
        CHECK_EQ(l->h, m->h);
        CHECK_EQ(l->zoom, 2);
        CHECK_EQ(strcmp(l->name, "Goblin Ambush"), 0);

        int tiles_same = 1, v_same = 1, h_same = 1;
        for (int y = 0; y < m->h; y++)
            for (int x = 0; x < m->w; x++)
                if (map_tile(l, x, y) != map_tile(m, x, y)) tiles_same = 0;
        for (int y = 0; y < m->h; y++)
            for (int x = 0; x <= m->w; x++)
                if (map_vedge(l, x, y) != map_vedge(m, x, y)) v_same = 0;
        for (int y = 0; y <= m->h; y++)
            for (int x = 0; x < m->w; x++)
                if (map_hedge(l, x, y) != map_hedge(m, x, y)) h_same = 0;
        CHECK(tiles_same);
        CHECK(v_same);
        CHECK(h_same);

        CASE("tokens round-trip, labels with spaces included");
        CHECK_EQ(l->tokens.n, 2);
        CHECK_EQ(strcmp(l->tokens.v[0].label, "Aria"), 0);
        CHECK_EQ(strcmp(l->tokens.v[1].label, "Ogre Chief"), 0);
        CHECK_EQ(l->tokens.v[1].size, 2);
        CHECK_EQ(l->tokens.v[1].kind, TOKEN_ENEMY);
        map_free(l);
    }
    map_free(m);

    /* The format uses significant trailing blanks, so an editor that strips
     * them must not be able to corrupt a map. */
    CASE("rows short of full width load as trailing void");
    write_file(path,
               "VTT 1\nname Trimmed\nsize 4 2\nzoom 1\n"
               "tiles\n..\n.\n"
               "vedges\n|\n\n"
               "hedges\n\n\n\n");
    Map *t = mapio_load(path, err, sizeof err);
    CHECK(t != NULL);
    if (t) {
        CHECK_EQ(t->w, 4);
        CHECK_EQ(map_tile(t, 0, 0), TILE_FLOOR);
        CHECK_EQ(map_tile(t, 1, 0), TILE_FLOOR);
        CHECK_EQ(map_tile(t, 2, 0), TILE_VOID);
        CHECK_EQ(map_tile(t, 0, 1), TILE_FLOOR);
        CHECK_EQ(map_vedge(t, 0, 0), EDGE_WALL);
        map_free(t);
    }

    CASE("bad input is rejected with a reason, not a crash");
    write_file(path, "not a map at all\n");
    CHECK(mapio_load(path, err, sizeof err) == NULL);
    CHECK(err[0] != '\0');

    write_file(path, "VTT 1\nsize 0 0\ntiles\n");
    CHECK(mapio_load(path, err, sizeof err) == NULL);

    write_file(path, "VTT 99\nsize 4 4\n");
    CHECK(mapio_load(path, err, sizeof err) == NULL);

    CASE("a missing file reports rather than aborts");
    CHECK(mapio_load("/tmp/vtt-does-not-exist-xyz.vtt", err, sizeof err) == NULL);

    CASE("bare names resolve into the map directory with an extension");
    char resolved[MAP_PATH_MAX], dir[MAP_PATH_MAX];
    mapio_default_dir(dir, sizeof dir);
    mapio_resolve_path("dungeon", resolved, sizeof resolved);
    CHECK(strstr(resolved, dir) == resolved);
    CHECK(strstr(resolved, "dungeon.vtt") != NULL);
    mapio_resolve_path("./local.vtt", resolved, sizeof resolved);
    CHECK_EQ(strcmp(resolved, "./local.vtt"), 0);

    unlink(path);
}

/* ------------------------------------------------------------------ grid */

static void test_grid(void)
{
    CASE("interior widths are odd so tokens have a centre cell");
    for (int z = 0; z < ZOOM_COUNT; z++) CHECK_EQ(ZOOM[z].iw % 2, 1);

    Map *m = map_new(3, 3, "grid");
    map_fill_tiles(m, 0, 0, 2, 2, TILE_FLOOR);
    map_rect_walls(m, 1, 1, 1, 1, EDGE_WALL);   /* wall the centre tile */

    Renderer r;
    rnd_init(&r);
    rnd_resize(&r, 40, 20);
    rnd_begin(&r);

    GridView g;
    memset(&g, 0, sizeof g);
    g.zoom = 0;                       /* pitch 2x2, so corner (cx,cy) is at (2cx,2cy) */
    g.view = rect(0, 0, 40, 20);

    grid_draw(&r, m, &g, &THEME_DARK, 0, 1);

    /* This is the rule that makes edge-walls legible: where a wall meets a
     * grid line, the junction belongs to the wall alone. The room's top-left
     * corner must read as ┏, not as a ╋ with two faint arms. */
    CASE("walls own the junctions they touch");
    CHECK_EQ(rnd_at(&r, 2, 2)->ch, 0x250Fu);        /* ┏ */
    CHECK_EQ(rnd_at(&r, 4, 2)->ch, 0x2513u);        /* ┓ */
    CHECK_EQ(rnd_at(&r, 2, 4)->ch, 0x2517u);        /* ┗ */
    CHECK_EQ(rnd_at(&r, 4, 4)->ch, 0x251Bu);        /* ┛ */
    CHECK_EQ(rnd_at(&r, 2, 2)->fg, THEME_DARK.wall);

    CASE("wall runs are heavy, open boundaries are thin grey");
    CHECK_EQ(rnd_at(&r, 3, 2)->ch, 0x2501u);        /* ━ along the north wall */
    CHECK_EQ(rnd_at(&r, 2, 3)->ch, 0x2503u);        /* ┃ down the west wall */
    CHECK_EQ(rnd_at(&r, 1, 2)->ch, 0x2500u);        /* ─ grid line outside it */
    CHECK_EQ(rnd_at(&r, 1, 2)->fg, THEME_DARK.grid);

    CASE("the map's outer corner is a light corner glyph");
    CHECK_EQ(rnd_at(&r, 0, 0)->ch, 0x250Cu);        /* ┌ */

    /* Grid lines exist only where a walkable tile touches the boundary. */
    CASE("void areas draw nothing at all");
    Map *v = map_new(3, 3, "void");
    rnd_begin(&r);
    grid_draw(&r, v, &g, &THEME_DARK, 0, 1);
    int drawn = 0;
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++)
            if (rnd_at(&r, x, y)->ch != ' ') drawn++;
    CHECK_EQ(drawn, 0);
    map_free(v);

    CASE("a map smaller than the viewport is centred");
    grid_clamp_camera(&g, m);
    CHECK_EQ(g.cam_x, -(40 - grid_cells_w(m, 0)) / 2);
    CHECK_EQ(g.cam_y, -(20 - grid_cells_h(m, 0)) / 2);

    CASE("a map larger than the viewport clamps to its edges");
    Map *big = map_new(200, 200, "big");
    g.cam_x = g.cam_y = -50;
    grid_clamp_camera(&g, big);
    CHECK_EQ(g.cam_x, 0);
    CHECK_EQ(g.cam_y, 0);
    g.cam_x = g.cam_y = 999999;
    grid_clamp_camera(&g, big);
    CHECK_EQ(g.cam_x, grid_cells_w(big, 0) - 40);
    CHECK_EQ(g.cam_y, grid_cells_h(big, 0) - 20);

    CASE("ensure_visible scrolls, and only as far as it must");
    g.cam_x = g.cam_y = 0;
    grid_ensure_visible(&g, big, 100, 100, 2);
    int sx, sy;
    grid_tile_screen(&g, 100, 100, &sx, &sy);
    CHECK(sx >= g.view.x && sx < g.view.x + g.view.w);
    CHECK(sy >= g.view.y && sy < g.view.y + g.view.h);

    CASE("screen-to-tile inverts tile-to-screen");
    for (int z = 0; z < ZOOM_COUNT; z++) {
        g.zoom = z;
        g.cam_x = g.cam_y = 0;
        int ix, iy, tx, ty;
        grid_tile_interior(&g, 7, 5, &ix, &iy);
        CHECK_EQ(grid_screen_to_tile(&g, big, ix, iy, &tx, &ty), 1);
        CHECK_EQ(tx, 7);
        CHECK_EQ(ty, 5);
    }

    CASE("zooming holds the anchor tile in place");
    for (int z = 1; z < ZOOM_COUNT; z++) {
        g.zoom = 0;
        g.cam_x = g.cam_y = 0;
        int bx, by, ax, ay;
        grid_tile_screen(&g, 20, 20, &bx, &by);
        grid_set_zoom(&g, big, z, 20, 20);
        grid_tile_screen(&g, 20, 20, &ax, &ay);
        CHECK_EQ(ax, bx);
        CHECK_EQ(ay, by);
    }

    map_free(big);
    map_free(m);
    rnd_free(&r);
}

/* ---------------------------------------------------------------- editor */

static void test_editor(void)
{
    Map *m = map_new(20, 15, "ed");
    map_fill_tiles(m, 0, 0, 19, 14, TILE_FLOOR);

    Editor e;
    ed_init(&e, m);
    ed_layout(&e, m, 80, 24);

    CASE("hjkl moves one tile");
    e.cx = 5; e.cy = 5;
    ed_move(&e, m, 1, 0, 1);
    CHECK_EQ(e.cx, 6);
    ed_move(&e, m, 0, 1, 1);
    CHECK_EQ(e.cy, 6);

    CASE("counts multiply the motion");
    ed_move(&e, m, 1, 0, 10);
    CHECK_EQ(e.cx, 16);

    CASE("the cursor cannot leave the map");
    ed_move(&e, m, 1, 0, 999);
    CHECK_EQ(e.cx, 19);
    ed_move(&e, m, -1, 0, 999);
    CHECK_EQ(e.cx, 0);
    ed_move(&e, m, 0, -1, 999);
    CHECK_EQ(e.cy, 0);
    ed_move(&e, m, 0, 1, 999);
    CHECK_EQ(e.cy, 14);

    CASE("the corner cursor spans one past the last tile");
    e.mode = ED_WALL;
    e.wx = 0; e.wy = 0;
    ed_move(&e, m, 1, 0, 999);
    CHECK_EQ(e.wx, m->w);          /* the far face of the last column */
    ed_move(&e, m, 0, 1, 999);
    CHECK_EQ(e.wy, m->h);
    e.mode = ED_NORMAL;

    CASE("every zoom level keeps the cursor on screen");
    for (int z = 0; z < ZOOM_COUNT; z++) {
        ed_set_zoom(&e, m, z);
        CHECK_EQ(e.view.zoom, z);
        int sx, sy;
        grid_tile_interior(&e.view, e.cx, e.cy, &sx, &sy);
        CHECK(sx >= e.view.view.x);
        CHECK(sy >= e.view.view.y);
        CHECK(sx < e.view.view.x + e.view.view.w);
        CHECK(sy < e.view.view.y + e.view.view.h);
    }

    CASE("zoom is clamped to the levels that exist");
    ed_set_zoom(&e, m, 99);
    CHECK_EQ(e.view.zoom, ZOOM_COUNT - 1);
    ed_set_zoom(&e, m, -5);
    CHECK_EQ(e.view.zoom, 0);

    map_free(m);
}


/* ------------------------------------------------------------------ undo */

static void test_undo(void)
{
    Map *m = map_new(10, 10, "undo");
    Undo u;
    undo_init(&u);

    CASE("nothing to undo on an empty log");
    CHECK_EQ(undo_undo(&u, m), 0);
    CHECK_EQ(undo_redo(&u, m), 0);
    CHECK_EQ(undo_can_undo(&u), 0);

    CASE("a single edit undoes and redoes");
    undo_begin(&u);
    undo_set_tile(&u, m, 3, 3, TILE_FLOOR);
    undo_end(&u);
    CHECK_EQ(map_tile(m, 3, 3), TILE_FLOOR);
    CHECK_EQ(undo_undo(&u, m), 1);
    CHECK_EQ(map_tile(m, 3, 3), TILE_VOID);
    CHECK_EQ(undo_redo(&u, m), 1);
    CHECK_EQ(map_tile(m, 3, 3), TILE_FLOOR);

    /* A rectangle fill is many ops but one action, so one press of u must
     * take the whole thing back. */
    CASE("a batch undoes as one step");
    undo_begin(&u);
    for (int i = 0; i < 5; i++) undo_set_tile(&u, m, i, 0, TILE_FLOOR);
    undo_end(&u);
    CHECK_EQ(undo_undo(&u, m), 1);
    for (int i = 0; i < 5; i++) CHECK_EQ(map_tile(m, i, 0), TILE_VOID);
    CHECK_EQ(undo_redo(&u, m), 1);
    for (int i = 0; i < 5; i++) CHECK_EQ(map_tile(m, i, 0), TILE_FLOOR);

    CASE("a batch that changed nothing costs no undo step");
    int before = u.nmarks;
    undo_begin(&u);
    undo_set_tile(&u, m, 3, 3, TILE_FLOOR);      /* already floor */
    undo_end(&u);
    CHECK_EQ(u.nmarks, before);

    CASE("nested begins do not split a batch");
    undo_begin(&u);
    undo_begin(&u);
    undo_set_tile(&u, m, 8, 8, TILE_FLOOR);
    undo_set_tile(&u, m, 8, 9, TILE_FLOOR);
    undo_end(&u);
    CHECK_EQ(undo_undo(&u, m), 1);
    CHECK_EQ(map_tile(m, 8, 8), TILE_VOID);
    CHECK_EQ(map_tile(m, 8, 9), TILE_VOID);

    /* Editing after an undo discards the redo tail: the future no longer
     * follows from the present. */
    CASE("a new edit truncates the redo tail");
    undo_clear(&u);
    undo_begin(&u); undo_set_tile(&u, m, 1, 1, TILE_FLOOR); undo_end(&u);
    undo_begin(&u); undo_set_tile(&u, m, 2, 2, TILE_FLOOR); undo_end(&u);
    undo_undo(&u, m);
    CHECK_EQ(undo_can_redo(&u), 1);
    undo_begin(&u); undo_set_tile(&u, m, 5, 5, TILE_FLOOR); undo_end(&u);
    CHECK_EQ(undo_can_redo(&u), 0);
    CHECK_EQ(undo_undo(&u, m), 1);
    CHECK_EQ(map_tile(m, 5, 5), TILE_VOID);
    CHECK_EQ(map_tile(m, 1, 1), TILE_FLOOR);     /* the kept history stands */

    CASE("edges undo too");
    undo_clear(&u);
    undo_begin(&u);
    undo_set_vedge(&u, m, 4, 4, EDGE_WALL);
    undo_set_hedge(&u, m, 4, 4, EDGE_WALL);
    undo_end(&u);
    CHECK_EQ(map_vedge(m, 4, 4), EDGE_WALL);
    undo_undo(&u, m);
    CHECK_EQ(map_vedge(m, 4, 4), EDGE_NONE);
    CHECK_EQ(map_hedge(m, 4, 4), EDGE_NONE);

    CASE("token add, move, and delete all undo");
    undo_clear(&u);
    Token t = { 2, 2, 1, TOKEN_PLAYER, "Aria" };
    undo_begin(&u);
    int idx = undo_add_token(&u, m, t);
    undo_end(&u);
    CHECK_EQ(m->tokens.n, 1);

    undo_begin(&u);
    undo_move_token(&u, m, idx, 6, 7);
    undo_end(&u);
    CHECK_EQ(m->tokens.v[idx].x, 6);
    CHECK_EQ(m->tokens.v[idx].y, 7);
    undo_undo(&u, m);
    CHECK_EQ(m->tokens.v[idx].x, 2);
    CHECK_EQ(m->tokens.v[idx].y, 2);

    undo_begin(&u);
    undo_del_token(&u, m, idx);
    undo_end(&u);
    CHECK_EQ(m->tokens.n, 0);
    undo_undo(&u, m);
    CHECK_EQ(m->tokens.n, 1);
    CHECK_EQ(strcmp(m->tokens.v[0].label, "Aria"), 0);
    undo_undo(&u, m);
    CHECK_EQ(m->tokens.n, 0);

    CASE("undoing a delete restores the original ordering");
    undo_clear(&u);
    Token a1 = { 0, 0, 1, TOKEN_PLAYER, "first" };
    Token b1 = { 1, 0, 1, TOKEN_PLAYER, "second" };
    Token c1 = { 2, 0, 1, TOKEN_PLAYER, "third" };
    undo_begin(&u);
    undo_add_token(&u, m, a1);
    undo_add_token(&u, m, b1);
    undo_add_token(&u, m, c1);
    undo_end(&u);
    undo_begin(&u);
    undo_del_token(&u, m, 1);          /* remove the middle one */
    undo_end(&u);
    CHECK_EQ(m->tokens.n, 2);
    undo_undo(&u, m);
    CHECK_EQ(m->tokens.n, 3);
    CHECK_EQ(strcmp(m->tokens.v[0].label, "first"), 0);
    CHECK_EQ(strcmp(m->tokens.v[1].label, "second"), 0);
    CHECK_EQ(strcmp(m->tokens.v[2].label, "third"), 0);

    CASE("undo runs to exhaustion without underflowing");
    while (undo_undo(&u, m)) { }
    CHECK_EQ(undo_can_undo(&u), 0);
    CHECK_EQ(undo_undo(&u, m), 0);

    undo_free(&u);
    map_free(m);
}

/* --------------------------------------------------------- build editing */

static void test_editing(void)
{
    Map *m = map_new(10, 8, "edit");
    map_fill_tiles(m, 0, 0, 9, 7, TILE_FLOOR);

    Undo u;
    undo_init(&u);
    Editor e;
    ed_init(&e, m);
    ed_layout(&e, m, 80, 24);

    e.cx = 3; e.cy = 3;

    CASE("Shift-HJKL toggles the matching face");
    ed_toggle_edge(&e, m, &u, 0, -1);            /* north */
    CHECK_EQ(map_hedge(m, 3, 3), EDGE_WALL);
    ed_toggle_edge(&e, m, &u, 0, 1);             /* south */
    CHECK_EQ(map_hedge(m, 3, 4), EDGE_WALL);
    ed_toggle_edge(&e, m, &u, -1, 0);            /* west */
    CHECK_EQ(map_vedge(m, 3, 3), EDGE_WALL);
    ed_toggle_edge(&e, m, &u, 1, 0);             /* east */
    CHECK_EQ(map_vedge(m, 4, 3), EDGE_WALL);

    CASE("toggling twice returns to open");
    ed_toggle_edge(&e, m, &u, 0, -1);
    CHECK_EQ(map_hedge(m, 3, 3), EDGE_NONE);

    CASE("each toggle is its own undo step");
    int marks = u.nmarks;
    CHECK_EQ(marks, 5);

    /* Walking the outline with the pen down must lay exactly the edges the
     * path crossed, and close the loop. */
    CASE("tracing a closed loop lays exactly its outline");
    undo_clear(&u);
    Map *r = map_new(8, 8, "trace");
    map_fill_tiles(r, 0, 0, 7, 7, TILE_FLOOR);
    Editor t;
    ed_init(&t, r);
    ed_layout(&t, r, 80, 24);
    t.mode = ED_WALL;
    t.wx = 1; t.wy = 1;
    t.pen = 1;
    ed_wall_step(&t, r, &u, 1, 0, 3);            /* east 3 */
    ed_wall_step(&t, r, &u, 0, 1, 2);            /* south 2 */
    ed_wall_step(&t, r, &u, -1, 0, 3);           /* west 3 */
    ed_wall_step(&t, r, &u, 0, -1, 2);           /* north 2, back to start */
    CHECK_EQ(t.wx, 1);
    CHECK_EQ(t.wy, 1);

    for (int x = 1; x < 4; x++) {
        CHECK_EQ(map_hedge(r, x, 1), EDGE_WALL);  /* north side */
        CHECK_EQ(map_hedge(r, x, 3), EDGE_WALL);  /* south side */
    }
    for (int y = 1; y < 3; y++) {
        CHECK_EQ(map_vedge(r, 1, y), EDGE_WALL);  /* west side */
        CHECK_EQ(map_vedge(r, 4, y), EDGE_WALL);  /* east side */
    }
    CHECK_EQ(map_vedge(r, 2, 1), EDGE_NONE);      /* interior stays open */

    CASE("the traced room is actually sealed");
    CHECK_EQ(map_blocked(r, 1, 1, 0, -1), 1);
    CHECK_EQ(map_blocked(r, 1, 1, -1, 0), 1);
    CHECK_EQ(map_blocked(r, 3, 2, 1, 0), 1);
    CHECK_EQ(map_blocked(r, 3, 2, 0, 1), 1);
    CHECK_EQ(map_blocked(r, 1, 1, 1, 0), 0);      /* but open inside */

    CASE("a pen-down stroke is one undo step");
    CHECK_EQ(undo_undo(&u, r), 1);
    for (int x = 1; x < 4; x++) CHECK_EQ(map_hedge(r, x, 1), EDGE_NONE);
    for (int y = 1; y < 3; y++) CHECK_EQ(map_vedge(r, 1, y), EDGE_NONE);
    CHECK_EQ(undo_can_undo(&u), 0);
    CHECK_EQ(undo_redo(&u, r), 1);
    CHECK_EQ(map_hedge(r, 1, 1), EDGE_WALL);

    CASE("pen up moves without drawing");
    undo_clear(&u);
    t.pen = 0;
    t.wx = 6; t.wy = 6;
    ed_wall_step(&t, r, &u, -1, 0, 2);
    CHECK_EQ(t.wx, 4);
    CHECK_EQ(map_hedge(r, 5, 6), EDGE_NONE);
    CHECK_EQ(undo_can_undo(&u), 0);

    CASE("erase mode clears what tracing laid");
    t.wx = 1; t.wy = 1;
    t.pen = 1;
    t.erase = 1;
    ed_wall_step(&t, r, &u, 1, 0, 3);
    for (int x = 1; x < 4; x++) CHECK_EQ(map_hedge(r, x, 1), EDGE_NONE);
    t.erase = 0;

    CASE("the rectangle tool lays a closed outline");
    undo_clear(&u);
    Map *q = map_new(8, 8, "rect");
    map_fill_tiles(q, 0, 0, 7, 7, TILE_FLOOR);
    EdShape box = ed_shape(ED_SHAPE_RECT, 2, 2, 5, 5, 1);
    ed_wall_shape(q, &u, &box, EDGE_WALL);
    for (int x = 2; x < 5; x++) {
        CHECK_EQ(map_hedge(q, x, 2), EDGE_WALL);
        CHECK_EQ(map_hedge(q, x, 5), EDGE_WALL);
    }
    for (int y = 2; y < 5; y++) {
        CHECK_EQ(map_vedge(q, 2, y), EDGE_WALL);
        CHECK_EQ(map_vedge(q, 5, y), EDGE_WALL);
    }
    CHECK_EQ(map_blocked(q, 2, 2, 0, -1), 1);
    CHECK_EQ(map_blocked(q, 4, 4, 1, 0), 1);

    CASE("a degenerate rectangle lays nothing");
    undo_clear(&u);
    EdShape flat = ed_shape(ED_SHAPE_RECT, 3, 3, 3, 6, 1);
    ed_wall_shape(q, &u, &flat, EDGE_NONE);
    CHECK_EQ(undo_can_undo(&u), 0);

    CASE("the rectangle tool also clears");
    ed_wall_shape(q, &u, &box, EDGE_NONE);
    CHECK_EQ(map_hedge(q, 3, 2), EDGE_NONE);
    CHECK_EQ(map_vedge(q, 2, 3), EDGE_NONE);

    CASE("visual fill covers the selection and undoes as one step");
    undo_clear(&u);
    Editor ve;
    ed_init(&ve, q);
    ed_layout(&ve, q, 80, 24);
    ve.mode = ED_VISUAL;
    ve.anchor_x = 1; ve.anchor_y = 1;
    ve.cx = 4; ve.cy = 3;
    ed_apply_tiles(&ve, q, &u, TILE_VOID);
    for (int y = 1; y <= 3; y++)
        for (int x = 1; x <= 4; x++)
            CHECK_EQ(map_tile(q, x, y), TILE_VOID);
    CHECK_EQ(map_tile(q, 5, 3), TILE_FLOOR);      /* just outside */
    CHECK_EQ(undo_undo(&u, q), 1);
    CHECK_EQ(map_tile(q, 1, 1), TILE_FLOOR);

    CASE("a reversed selection fills the same rectangle");
    ve.anchor_x = 4; ve.anchor_y = 3;
    ve.cx = 1; ve.cy = 1;
    ed_apply_tiles(&ve, q, &u, TILE_VOID);
    CHECK_EQ(map_tile(q, 1, 1), TILE_VOID);
    CHECK_EQ(map_tile(q, 4, 3), TILE_VOID);

    CASE("space toggles a single tile both ways");
    Editor se;
    ed_init(&se, q);
    se.cx = 6; se.cy = 6;
    CHECK_EQ(map_tile(q, 6, 6), TILE_FLOOR);
    ed_toggle_tile(&se, q, &u);
    CHECK_EQ(map_tile(q, 6, 6), TILE_VOID);
    ed_toggle_tile(&se, q, &u);
    CHECK_EQ(map_tile(q, 6, 6), TILE_FLOOR);

    map_free(q);
    map_free(r);
    map_free(m);
    undo_free(&u);
}


/* ------------------------------------------------------------------ play */

static void test_play(void)
{
    Map *m = map_new(12, 10, "play");
    map_fill_tiles(m, 0, 0, 11, 9, TILE_FLOOR);

    Undo u;
    undo_init(&u);
    Play p;
    play_init(&p);

    CASE("play starts with nothing selected and walls enforced");
    CHECK_EQ(p.sel, -1);
    CHECK_EQ(p.enforce_walls, 1);
    CHECK_EQ(p.next_size, 1);

    Token a = { 2, 2, 1, TOKEN_PLAYER, "Aria" };
    int ai = undo_add_token(&u, m, a);

    CASE("a 1x1 token moves freely on open floor");
    p.sel = ai;
    CHECK_EQ(play_step(m, &u, &p, 1, 0), 1);
    CHECK_EQ(m->tokens.v[ai].x, 3);
    CHECK_EQ(p.steps, 1);
    CHECK_EQ(play_step(m, &u, &p, 0, 1), 1);
    CHECK_EQ(p.steps, 2);

    CASE("a wall stops it, and the step is not counted");
    map_set_vedge(m, 4, 3, EDGE_WALL);
    int before = p.steps;
    CHECK_EQ(play_step(m, &u, &p, 1, 0), 0);
    CHECK_EQ(m->tokens.v[ai].x, 3);
    CHECK_EQ(p.steps, before);

    /* Rules-agnostic means the GM can always overrule the map. */
    CASE("blocking can be switched off");
    p.enforce_walls = 0;
    CHECK_EQ(play_step(m, &u, &p, 1, 0), 1);
    CHECK_EQ(m->tokens.v[ai].x, 4);
    p.enforce_walls = 1;
    map_set_vedge(m, 4, 3, EDGE_NONE);

    CASE("the map edge stops a token even with walls off");
    p.enforce_walls = 0;
    m->tokens.v[ai].x = 0;
    m->tokens.v[ai].y = 0;
    CHECK_EQ(play_step(m, &u, &p, -1, 0), 0);
    CHECK_EQ(play_step(m, &u, &p, 0, -1), 0);
    p.enforce_walls = 1;

    /* A big token has to be stopped by a wall anywhere along its leading
     * face, not only the one tile the anchor happens to sit on. */
    CASE("a 2x2 token is blocked by a wall on any part of its face");
    Token big = { 4, 4, 2, TOKEN_ENEMY, "Ogre" };
    int bi = undo_add_token(&u, m, big);
    p.sel = bi;

    CHECK_EQ(token_can_move(m, &m->tokens.v[bi], 1, 0, 1), 1);
    map_set_vedge(m, 6, 5, EDGE_WALL);        /* the token's lower-right face */
    CHECK_EQ(token_can_move(m, &m->tokens.v[bi], 1, 0, 1), 0);
    CHECK_EQ(play_step(m, &u, &p, 1, 0), 0);
    map_set_vedge(m, 6, 5, EDGE_WALL * 0);

    CHECK_EQ(token_can_move(m, &m->tokens.v[bi], 0, 1, 1), 1);
    map_set_hedge(m, 5, 6, EDGE_WALL);        /* below its right-hand column */
    CHECK_EQ(token_can_move(m, &m->tokens.v[bi], 0, 1, 1), 0);
    map_set_hedge(m, 5, 6, EDGE_NONE);

    CASE("a big token needs its whole footprint on the map");
    m->tokens.v[bi].x = 10;
    CHECK_EQ(token_can_move(m, &m->tokens.v[bi], 1, 0, 1), 0);
    m->tokens.v[bi].x = 4;

    CASE("void tiles stop a token like a wall does");
    map_set_tile(m, 6, 4, TILE_VOID);
    CHECK_EQ(token_can_move(m, &m->tokens.v[bi], 1, 0, 1), 0);
    map_set_tile(m, 6, 4, TILE_FLOOR);

    CASE("placement checks the footprint fits");
    CHECK_EQ(play_can_place(m, 0, 0, 1), 1);
    CHECK_EQ(play_can_place(m, 11, 9, 1), 1);
    CHECK_EQ(play_can_place(m, 11, 9, 2), 0);
    CHECK_EQ(play_can_place(m, 10, 8, 2), 1);
    CHECK_EQ(play_can_place(m, -1, 0, 1), 0);

    CASE("cycling wraps in both directions");
    p.sel = -1;
    play_cycle(&p, m, 1, PLAY_ANY_KIND);
    CHECK_EQ(p.sel, 0);
    play_cycle(&p, m, 1, PLAY_ANY_KIND);
    CHECK_EQ(p.sel, 1);
    play_cycle(&p, m, 1, PLAY_ANY_KIND);
    CHECK_EQ(p.sel, 0);          /* wrapped */
    play_cycle(&p, m, -1, PLAY_ANY_KIND);
    CHECK_EQ(p.sel, 1);

    CASE("selecting by tile finds the token under the cursor");
    m->tokens.v[bi].x = 4;
    m->tokens.v[bi].y = 4;
    play_select_at(&p, m, 5, 5);      /* inside the 2x2 footprint */
    CHECK_EQ(p.sel, bi);
    play_select_at(&p, m, 9, 9);
    CHECK_EQ(p.sel, -1);

    CASE("moves undo one step at a time");
    p.sel = ai;
    m->tokens.v[ai].x = 5;
    m->tokens.v[ai].y = 5;
    undo_clear(&u);
    undo_begin(&u); play_step(m, &u, &p, 1, 0); undo_end(&u);
    undo_begin(&u); play_step(m, &u, &p, 1, 0); undo_end(&u);
    CHECK_EQ(m->tokens.v[ai].x, 7);
    undo_undo(&u, m);
    CHECK_EQ(m->tokens.v[ai].x, 6);
    undo_undo(&u, m);
    CHECK_EQ(m->tokens.v[ai].x, 5);

    CASE("stepping with nothing selected does nothing");
    p.sel = -1;
    CHECK_EQ(play_step(m, &u, &p, 1, 0), 0);

    undo_free(&u);
    map_free(m);
}

/* ------------------------------------------------------- token appearance */

/* Renders one token and reports whether a cell carries its fill colour. */
static int tok_filled(Renderer *r, uint32_t col, int x, int y)
{
    Cell *c = rnd_at(r, x, y);
    return c && c->bg == col;
}

static void test_token_draw(void)
{
    Map *m = map_new(8, 8, "draw");
    map_fill_tiles(m, 0, 0, 7, 7, TILE_FLOOR);

    Renderer r;
    rnd_init(&r);
    rnd_resize(&r, 80, 40);

    GridView g;
    memset(&g, 0, sizeof g);
    g.view = rect(0, 0, 80, 40);

    Token t;
    memset(&t, 0, sizeof t);
    t.x = 1; t.y = 1; t.size = 1;

    /* Colour alone cannot carry the player/enemy distinction: it is lost in
     * --ascii and to a colourblind reader. The smallest tokens say it with a
     * glyph instead. */
    CASE("a one-cell token is a filled circle or square glyph");
    g.zoom = 0;
    Rect a;
    grid_token_area(&g, 1, 1, 1, &a);
    CHECK_EQ(a.w, 1);
    CHECK_EQ(a.h, 1);

    rnd_begin(&r);
    t.kind = TOKEN_PLAYER;
    grid_draw_token(&r, &g, &t, &THEME_DARK, 0, 0);
    CHECK_EQ(rnd_at(&r, a.x, a.y)->ch, 0x25CFu);      /* ● */

    rnd_begin(&r);
    t.kind = TOKEN_ENEMY;
    grid_draw_token(&r, &g, &t, &THEME_DARK, 0, 0);
    CHECK_EQ(rnd_at(&r, a.x, a.y)->ch, 0x25A0u);      /* ■ */

    CASE("a single-row token brackets its label by shape");
    g.zoom = 1;                        /* interior 3x1 */
    grid_token_area(&g, 1, 1, 1, &a);
    CHECK_EQ(a.h, 1);

    rnd_begin(&r);
    t.kind = TOKEN_PLAYER;
    str_lcpy(t.label, "A", sizeof t.label);
    grid_draw_token(&r, &g, &t, &THEME_DARK, 0, 0);
    CHECK_EQ(rnd_at(&r, a.x, a.y)->ch, '(');
    CHECK_EQ(rnd_at(&r, a.x + 1, a.y)->ch, 'A');
    CHECK_EQ(rnd_at(&r, a.x + 2, a.y)->ch, ')');

    rnd_begin(&r);
    t.kind = TOKEN_ENEMY;
    grid_draw_token(&r, &g, &t, &THEME_DARK, 0, 0);
    CHECK_EQ(rnd_at(&r, a.x, a.y)->ch, '[');
    CHECK_EQ(rnd_at(&r, a.x + 2, a.y)->ch, ']');

    /* At larger sizes the shape is the fill, and a circle must actually lose
     * its corners. */
    CASE("a player token is a circle: corners are not filled");
    g.zoom = 1;
    t.size = 2;                        /* 7x3 area */
    t.kind = TOKEN_PLAYER;
    t.label[0] = '\0';
    grid_token_area(&g, 1, 1, 2, &a);
    CHECK_EQ(a.w, 7);
    CHECK_EQ(a.h, 3);

    rnd_begin(&r);
    grid_draw_token(&r, &g, &t, &THEME_DARK, 0, 0);
    uint32_t pc = THEME_DARK.player;
    CHECK_EQ(tok_filled(&r, pc, a.x, a.y), 0);                    /* corner */
    CHECK_EQ(tok_filled(&r, pc, a.x + a.w - 1, a.y), 0);
    CHECK_EQ(tok_filled(&r, pc, a.x, a.y + a.h - 1), 0);
    CHECK_EQ(tok_filled(&r, pc, a.x + a.w - 1, a.y + a.h - 1), 0);
    CHECK_EQ(tok_filled(&r, pc, a.x + a.w / 2, a.y + a.h / 2), 1); /* centre */
    CHECK_EQ(tok_filled(&r, pc, a.x, a.y + a.h / 2), 1);           /* widest row */

    CASE("an enemy token is a square: every cell of its body is filled");
    rnd_begin(&r);
    t.kind = TOKEN_ENEMY;
    grid_draw_token(&r, &g, &t, &THEME_DARK, 0, 0);
    uint32_t ec = THEME_DARK.enemy;
    /* 7 wide, so it insets by one to sit inside the grid square. */
    CHECK_EQ(tok_filled(&r, ec, a.x, a.y), 0);
    CHECK_EQ(tok_filled(&r, ec, a.x + 1, a.y), 1);                 /* square corner */
    CHECK_EQ(tok_filled(&r, ec, a.x + a.w - 2, a.y), 1);
    CHECK_EQ(tok_filled(&r, ec, a.x + 1, a.y + a.h - 1), 1);
    CHECK_EQ(tok_filled(&r, ec, a.x + a.w - 2, a.y + a.h - 1), 1);

    /* Insetting a 5-wide enemy would make it exactly the size of the circle
     * inscribed beside it, which is the one thing it must not look like. */
    CASE("a narrow enemy keeps its full width so it cannot mimic a circle");
    g.zoom = 2;                        /* interior 5x2 */
    t.size = 1;
    grid_token_area(&g, 1, 1, 1, &a);
    CHECK_EQ(a.w, 5);

    rnd_begin(&r);
    t.kind = TOKEN_ENEMY;
    grid_draw_token(&r, &g, &t, &THEME_DARK, 0, 0);
    int enemy_w = 0;
    for (int i = 0; i < a.w; i++) if (tok_filled(&r, ec, a.x + i, a.y)) enemy_w++;

    rnd_begin(&r);
    t.kind = TOKEN_PLAYER;
    grid_draw_token(&r, &g, &t, &THEME_DARK, 0, 0);
    int player_w = 0;
    for (int i = 0; i < a.w; i++) if (tok_filled(&r, pc, a.x + i, a.y)) player_w++;

    CHECK_EQ(enemy_w, 5);
    CHECK(player_w < enemy_w);

    /* Three cells cannot hold "Aria", and "(…)" names nothing at the table. */
    CASE("an oversized label truncates to initials, not an ellipsis");
    g.zoom = 1;
    t.size = 1;
    t.kind = TOKEN_PLAYER;
    str_lcpy(t.label, "Aria", sizeof t.label);
    grid_token_area(&g, 1, 1, 1, &a);
    rnd_begin(&r);
    grid_draw_token(&r, &g, &t, &THEME_DARK, 0, 0);
    CHECK_EQ(rnd_at(&r, a.x + 1, a.y)->ch, 'A');
    CHECK(rnd_at(&r, a.x + 1, a.y)->ch != 0x2026u);

    g.zoom = 2;
    t.label[0] = '\0';
    grid_token_area(&g, 1, 1, 1, &a);

    CASE("selection brightens the fill rather than moving anything");
    rnd_begin(&r);
    t.kind = TOKEN_PLAYER;
    grid_draw_token(&r, &g, &t, &THEME_DARK, 1, 0);
    CHECK_EQ(tok_filled(&r, pc, a.x + a.w / 2, a.y), 0);   /* no longer the base */
    CHECK(rnd_at(&r, a.x + a.w / 2, a.y)->bg != THEME_DARK.bg);

    /* In --ascii the brackets are the only thing distinguishing the shapes,
     * so a centred label must not be allowed to overwrite them. */
    CASE("an ascii label never eats its own brackets");
    g.zoom = 1;
    t.size = 2;
    t.kind = TOKEN_ENEMY;
    str_lcpy(t.label, "Ogre", sizeof t.label);
    grid_token_area(&g, 1, 1, 2, &a);
    rnd_begin(&r);
    grid_draw_token(&r, &g, &t, &THEME_DARK, 0, 1);
    {
        Rect b = a;
        if (b.w >= 7) { b.x += 1; b.w -= 2; }      /* the enemy inset */
        int mid = b.y + b.h / 2;
        CHECK_EQ(rnd_at(&r, b.x, mid)->ch, '[');
        CHECK_EQ(rnd_at(&r, b.x + b.w - 1, mid)->ch, ']');
    }

    t.kind = TOKEN_PLAYER;
    rnd_begin(&r);
    grid_draw_token(&r, &g, &t, &THEME_DARK, 0, 1);
    {
        int mid = a.y + a.h / 2;
        CHECK_EQ(rnd_at(&r, a.x, mid)->ch, '(');
        CHECK_EQ(rnd_at(&r, a.x + a.w - 1, mid)->ch, ')');
    }
    t.label[0] = '\0';

    CASE("ascii mode swaps the ellipsis for a plain marker");
    draw_set_ascii(1);
    rnd_begin(&r);
    draw_text_ellipsis(&r, 0, 0, "abcdefgh", 4, style(COL_DEFAULT, COL_DEFAULT, 0));
    CHECK_EQ(rnd_at(&r, 3, 0)->ch, '~');
    draw_set_ascii(0);
    rnd_begin(&r);
    draw_text_ellipsis(&r, 0, 0, "abcdefgh", 4, style(COL_DEFAULT, COL_DEFAULT, 0));
    CHECK_EQ(rnd_at(&r, 3, 0)->ch, 0x2026u);

    CASE("a multi-tile token covers the boundaries inside its own footprint");
    g.zoom = 1;
    grid_token_area(&g, 1, 1, 3, &a);
    CHECK_EQ(a.w, 3 * zoom_pw(1) - 1);
    CHECK_EQ(a.h, 3 * zoom_ph(1) - 1);

    CASE("a token drawn off the viewport is clipped, not crashed");
    rnd_begin(&r);
    ClipRect saved = rnd_clip_push(&r, 0, 0, 4, 4);
    t.size = 3;
    grid_draw_token(&r, &g, &t, &THEME_DARK, 0, 0);
    rnd_clip_restore(&r, saved);
    CHECK(1);

    rnd_free(&r);
    map_free(m);
}


/* ---------------------------------------------------------------- golden */

/* Drives a real App through a scripted session, renders one frame, and
 * compares the plain-text dump with a stored file. Segments are fed one at a
 * time with the pending-ESC timeout resolved between them, exactly as the
 * event loop does, so `esc` followed by a command is expressible.
 *
 * Run with VTT_UPDATE_GOLDEN=1 to rewrite the expectations. */
static void golden(const char *name, int w, int h, const char *map_path,
                   const char *const *segments, int nsegments, int ascii)
{
    Renderer r;
    App      a;

    rnd_init(&r);
    rnd_resize(&r, w, h);
    app_init(&a, NULL, &r);
    a.ascii = ascii;
    draw_set_ascii(ascii);

    if (map_path && app_open_map(&a, map_path) != 0) {
        g_fails++;
        fprintf(stderr, "  FAIL [%s] could not open fixture %s\n", name, map_path);
        rnd_free(&r);
        return;
    }

    InputParser p;
    input_init(&p);
    for (int i = 0; i < nsegments; i++) {
        input_feed(&p, segments[i], strlen(segments[i]));
        Key k;
        while (input_next(&p, &k)) app_key(&a, k);
        while (input_pending(&p) && input_timeout(&p, &k)) app_key(&a, k);
    }

    rnd_begin(&r);
    app_draw(&a);

    ByteBuf out;
    bb_init(&out, 16384);
    rnd_dump(&r, &out);

    char path[256];
    snprintf(path, sizeof path, "tests/golden/%s.txt", name);

    if (getenv("VTT_UPDATE_GOLDEN")) {
        FILE *f = fopen(path, "w");
        if (f) { fwrite(out.data, 1, out.len, f); fclose(f); }
        fprintf(stderr, "  wrote %s\n", path);
    } else {
        FILE *f = fopen(path, "rb");
        g_checks++;
        if (!f) {
            g_fails++;
            fprintf(stderr, "  FAIL [%s] missing golden %s "
                            "(VTT_UPDATE_GOLDEN=1 make test to create)\n", name, path);
        } else {
            char  *want = xmalloc(out.len + 4096);
            size_t n    = fread(want, 1, out.len + 4096, f);
            fclose(f);
            if (n != out.len || memcmp(want, out.data, n) != 0) {
                g_fails++;
                fprintf(stderr, "  FAIL [%s] frame differs from %s\n", name, path);
                /* Show the first differing line, which is usually enough to
                 * see what moved. */
                size_t i = 0, line = 1, ls = 0;
                while (i < n && i < out.len && want[i] == out.data[i]) {
                    if (want[i] == '\n') { line++; ls = i + 1; }
                    i++;
                }
                size_t le = ls;
                while (le < out.len && out.data[le] != '\n') le++;
                fprintf(stderr, "    line %zu\n      want: %.*s\n      got : %.*s\n",
                        line, (int)(le - ls), want + ls, (int)(le - ls), out.data + ls);
            }
            free(want);
        }
    }

    bb_free(&out);
    app_free(&a);
    rnd_free(&r);
    draw_set_ascii(0);
}

#define FIXTURE "tests/fixtures/two-rooms.vtt"

static void test_golden(void)
{
    CASE("menu");
    {
        static const char *const seg[] = { "" };
        golden("menu", 72, 20, NULL, seg, 1, 0);
    }

    CASE("build mode on a loaded map");
    {
        static const char *const seg[] = { "" };
        golden("build", 72, 20, FIXTURE, seg, 1, 0);
    }

    /* The whole point of the wall tool: walking an outline with the pen down
     * should leave a sealed room. */
    CASE("a room traced with the wall tool");
    {
        static const char *const seg[] = { "gg0jjjjjjllllllllllllw lljjhhkk" };
        golden("traced-room", 72, 20, FIXTURE, seg, 1, 0);
    }

    CASE("visual selection cleared to void");
    {
        static const char *const seg[] = { "gg0vlljj", "x" };
        golden("cleared", 72, 20, FIXTURE, seg, 2, 0);
    }

    CASE("play mode with a token picked up and moved");
    {
        /* F2 into play, tab to the first token, grab it, walk east. */
        static const char *const seg[] = { "\x1b[12~\t\r", "lll" };
        golden("play-moving", 72, 20, FIXTURE, seg, 2, 0);
    }

    CASE("play mode in ascii");
    {
        static const char *const seg[] = { "\x1b[12~" };
        golden("play-ascii", 72, 20, FIXTURE, seg, 1, 1);
    }

    CASE("the new-map prompt");
    {
        static const char *const seg[] = { "j\r", "Ambush" };
        golden("prompt", 72, 20, NULL, seg, 2, 0);
    }

    CASE("the ruler measuring across a room");
    {
        /* Anchor inside the west room, then measure out through its wall so
         * the readout has to report sight as broken. */
        static const char *const seg[] = { "gg0jjll", "m", "llllll" };
        golden("ruler", 72, 20, FIXTURE, seg, 3, 0);
    }

    CASE("the ruler with several legs");
    {
        static const char *const seg[] = { "gg0jjll", "m", "lll", "\r", "jjj", "\r", "ll" };
        golden("ruler-legs", 72, 20, FIXTURE, seg, 7, 0);
    }

    /* The fill itself is a background colour, which a text dump cannot show;
     * this pins the readout, which is the part that names names. */
    CASE("the range overlay's readout");
    {
        static const char *const seg[] = { ":ruleset daggerheart\r", "\x1b[12~", "\t", "rrrr" };
        golden("range", 84, 20, FIXTURE, seg, 4, 0);
    }

    /* Doors, windows and terrain all carry their own glyph, so a
     * text dump pins them. */
    CASE("every boundary kind and terrain, in build mode");
    {
        static const char *const seg[] = { "" };
        golden("kinds-build", 72, 16, "tests/fixtures/kinds.vtt", seg, 1, 0);
    }

    /* The same map in play mode: the secret door must be a wall. */
    CASE("the same map in play mode, with the secret door hidden");
    {
        static const char *const seg[] = { "\x1b[12~" };
        golden("kinds-play", 72, 16, "tests/fixtures/kinds.vtt", seg, 1, 0);
    }

    CASE("a narrow terminal still lays out");
    {
        static const char *const seg[] = { "" };
        golden("narrow", 34, 12, FIXTURE, seg, 1, 0);
    }
}


/* ---------------------------------------------------------------- term io */

/* These cover the failure that produced visible artifacts: the terminal falls
 * behind, part of a frame never arrives, and the renderer goes on believing
 * the screen shows what it drew. */
static void test_term_io(void)
{
    signal(SIGPIPE, SIG_IGN);

    /* A frame that does not fully arrive must not advance `front`. Otherwise
     * the cells that were dropped are diffed away on every later frame and
     * stay wrong on screen forever. */
    CASE("a failed write forces a full repaint instead of trusting front");
    {
        Renderer r;
        rnd_init(&r);
        rnd_resize(&r, 20, 5);

        Term t;
        memset(&t, 0, sizeof t);

        int devnull = open("/dev/null", O_WRONLY);
        CHECK(devnull >= 0);
        t.out_fd = devnull;

        Style st = style(COL_DEFAULT, COL_DEFAULT, 0);

        /* A clean frame first, so front is in sync and force_full is clear. */
        rnd_begin(&r);
        draw_text(&r, 0, 0, "hello", -1, st);
        rnd_flush(&r, &t);
        CHECK_EQ(r.force_full, 0);
        CHECK_EQ(t.dead, 0);

        /* Now break the destination and change one cell. */
        int fds[2];
        CHECK_EQ(pipe(fds), 0);
        close(fds[0]);                       /* reader gone: writes get EPIPE */
        t.out_fd = fds[1];

        rnd_begin(&r);
        draw_text(&r, 0, 0, "hellp", -1, st);
        rnd_flush(&r, &t);

        CHECK_EQ(t.dead, 1);                 /* a real failure, not backpressure */
        CHECK_EQ(r.bytes_written, 0);        /* reports what arrived, not what we hoped */
        CHECK_EQ(r.force_full, 1);           /* the next frame must repaint everything */

        /* Redrawing the same content must now emit the whole screen, which is
         * only true if front was left alone. */
        close(fds[1]);
        t.out_fd = devnull;
        t.dead   = 0;
        rnd_begin(&r);
        draw_text(&r, 0, 0, "hellp", -1, st);
        rnd_flush(&r, &t);
        CHECK_EQ(r.cells_changed, 100);      /* 20 x 5, every cell */
        CHECK_EQ(r.force_full, 0);

        close(devnull);
        rnd_free(&r);
    }

    /* The original bug: stdout was non-blocking, so a terminal that fell
     * behind made write() return EAGAIN and the rest of the frame was thrown
     * away. Backpressure must be waited out instead. */
    CASE("term_write delivers everything even when the reader is slow");
    {
        int fds[2];
        CHECK_EQ(pipe(fds), 0);

        int fl = fcntl(fds[1], F_GETFL, 0);
        fcntl(fds[1], F_SETFL, fl | O_NONBLOCK);   /* force the EAGAIN path */

        const size_t N = 512 * 1024;              /* far beyond any pipe buffer */
        char *buf = xmalloc(N);
        memset(buf, 'x', N);

        pid_t pid = fork();
        CHECK(pid >= 0);
        if (pid == 0) {
            /* Child: drain slowly, so the writer really does hit EAGAIN. */
            close(fds[1]);
            char   sink[8192];
            size_t total = 0;
            for (;;) {
                ssize_t n = read(fds[0], sink, sizeof sink);
                if (n <= 0) break;
                total += (size_t)n;
                if ((total / sizeof sink) % 4 == 0) {
                    struct timespec ts = { 0, 1000000 };   /* 1ms */
                    nanosleep(&ts, NULL);
                }
            }
            close(fds[0]);
            _exit(total == N ? 0 : 1);
        }

        close(fds[0]);
        Term t;
        memset(&t, 0, sizeof t);
        t.out_fd = fds[1];

        size_t wrote = term_write(&t, buf, N);
        CHECK_EQ(wrote, N);                       /* nothing dropped */
        CHECK_EQ(t.dead, 0);                      /* slow is not dead */

        close(fds[1]);
        int status = 0;
        waitpid(pid, &status, 0);
        CHECK(WIFEXITED(status));
        CHECK_EQ(WEXITSTATUS(status), 0);         /* the child saw every byte */

        free(buf);
    }

    /* The drain loop reads only what the parser can hold, because input_feed
     * discards the rest; without that a long burst loses keystrokes. */
    CASE("input_room bounds what input_feed can accept");
    {
        InputParser p;
        input_init(&p);
        size_t cap = input_room(&p);
        CHECK(cap > 0);

        char *big = xmalloc(cap + 64);
        memset(big, 'j', cap + 64);

        input_feed(&p, big, cap);
        CHECK_EQ(input_room(&p), 0);

        int n = 0;
        Key k;
        while (input_next(&p, &k)) n++;
        CHECK_EQ((size_t)n, cap);                 /* every byte became a key */
        CHECK_EQ(input_room(&p), cap);

        /* Offering more than the room silently drops the excess, which is
         * exactly why the caller must ask first. */
        input_feed(&p, big, cap + 64);
        CHECK_EQ(input_room(&p), 0);

        free(big);
    }
}


/* ----------------------------------------------------------------- ruler */

static void test_dist(void)
{
    CASE("a zero offset is zero under every metric");
    for (int m = 0; m < DIST_COUNT; m++) CHECK_EQ((int)(dist_tiles(m, 0, 0) * 10), 0);

    /* The 3-4-5 triangle makes the metrics tell their differences plainly. */
    CASE("the metrics differ as documented on a 4x3 offset");
    CHECK_EQ((int)(dist_tiles(DIST_CHEBYSHEV, 4, 3) * 10), 40);
    CHECK_EQ((int)(dist_tiles(DIST_EUCLIDEAN, 4, 3) * 10), 50);
    CHECK_EQ((int)(dist_tiles(DIST_ALT_DIAG,  4, 3) * 10), 50);
    CHECK_EQ((int)(dist_tiles(DIST_MANHATTAN, 4, 3) * 10), 70);

    CASE("a pure diagonal shows the diagonal rule");
    CHECK_EQ((int)(dist_tiles(DIST_CHEBYSHEV, 4, 4) * 10), 40);   /* free diagonals */
    CHECK_EQ((int)(dist_tiles(DIST_ALT_DIAG,  4, 4) * 10), 60);   /* 5-10-5 */
    CHECK_EQ((int)(dist_tiles(DIST_MANHATTAN, 4, 4) * 10), 80);

    CASE("orthogonal offsets agree across all metrics");
    for (int m = 0; m < DIST_COUNT; m++) {
        CHECK_EQ((int)(dist_tiles(m, 6, 0) * 10), 60);
        CHECK_EQ((int)(dist_tiles(m, 0, 6) * 10), 60);
    }

    CASE("distance does not depend on direction");
    for (int m = 0; m < DIST_COUNT; m++) {
        double a = dist_tiles(m, 5, 3), b = dist_tiles(m, -5, -3);
        double c = dist_tiles(m, -5, 3), d = dist_tiles(m, 5, -3);
        CHECK_EQ((int)(a * 100), (int)(b * 100));
        CHECK_EQ((int)(a * 100), (int)(c * 100));
        CHECK_EQ((int)(a * 100), (int)(d * 100));
    }

    CASE("metric names round-trip, with aliases");
    for (int m = 0; m < DIST_COUNT; m++)
        CHECK_EQ(dist_metric_from_name(dist_metric_name(m)), m);
    CHECK_EQ(dist_metric_from_name("5e"), DIST_CHEBYSHEV);
    CHECK_EQ(dist_metric_from_name("5-10-5"), DIST_ALT_DIAG);
    CHECK_EQ(dist_metric_from_name("nonsense"), -1);
}

static void test_ruleset(void)
{
    CASE("an unset ruleset means no bands, not a crash");
    const Ruleset *none = ruleset_by_name("");
    CHECK(none != NULL);
    CHECK(ruleset_band(none, 25.0) == NULL);
    CHECK(ruleset_band(NULL, 25.0) == NULL);

    CASE("an unknown ruleset is reported, not guessed at");
    CHECK(ruleset_by_name("pathfinder") == NULL);

    const Ruleset *dh = ruleset_by_name("daggerheart");
    CHECK(dh != NULL);
    if (!dh) return;

    CASE("every band resolves and none is skipped");
    for (int i = 0; i < dh->nbands; i++) {
        const char *got = ruleset_band(dh, dh->bands[i].max);
        CHECK(got != NULL);
        CHECK_EQ(strcmp(got, dh->bands[i].name), 0);
    }

    CASE("band thresholds are ordered, so lookup is unambiguous");
    for (int i = 1; i < dh->nbands; i++) CHECK(dh->bands[i].max > dh->bands[i - 1].max);

    CASE("the last band catches everything beyond it");
    const char *far = ruleset_band(dh, 1e9);
    CHECK(far != NULL);
    CHECK_EQ(strcmp(far, dh->bands[dh->nbands - 1].name), 0);

    CASE("zero distance lands in the first band");
    CHECK_EQ(strcmp(ruleset_band(dh, 0.0), dh->bands[0].name), 0);

    CASE("daggerheart thresholds come from the book, so they are verified");
    CHECK_EQ(dh->verified, 1);

    /* The SRD gives each band twice: a fiction distance and an estimate for a
     * physical map. These are the map estimates converted at the book's own
     * "1 inch is roughly 5 feet", so at a five-foot square they must land on
     * the square counts the book's estimates work out to. */
    CASE("the bands land on the book's square counts at five-foot squares");
    const struct { int squares; const char *band; } EXPECT[] = {
        {  0, "Melee"      },   /* the same square */
        {  1, "Melee"      },   /* touching: adjacent */
        {  2, "Very Close" },
        {  3, "Very Close" },   /* game card, 2-3 in */
        {  4, "Close"      },
        {  6, "Close"      },   /* pen or pencil, 5-6 in */
        {  7, "Far"        },
        { 12, "Far"        },   /* sheet of paper long edge, 11-12 in */
        { 13, "Very Far"   },
        { 99, "Very Far"   },
    };
    for (size_t i = 0; i < sizeof EXPECT / sizeof *EXPECT; i++) {
        const char *got = ruleset_band(dh, EXPECT[i].squares * 5.0);
        CHECK(got != NULL);
        if (got) CHECK_EQ(strcmp(got, EXPECT[i].band), 0);
    }

    /* Thresholds are held in feet rather than squares so that changing the
     * scale keeps them describing the same fictional distance. */
    CASE("a ten-foot square puts three squares in Close, not Very Close");
    CHECK_EQ(strcmp(ruleset_band(dh, 3 * 10.0), "Close"), 0);
    CHECK_EQ(strcmp(ruleset_band(dh, 1 * 10.0), "Very Close"), 0);

    /* Out of Range is a call about the scene, not a distance: anything the
     * cursor can reach is by definition on the map. */
    CASE("no band is Out of Range, because nothing on the map can be");
    for (int i = 0; i < dh->nbands; i++)
        CHECK(strcmp(dh->bands[i].name, "Out of Range") != 0);
}

static void test_ruler(void)
{
    Ruler r;
    ruler_reset(&r);

    CASE("an inactive ruler measures nothing");
    CHECK_EQ(r.active, 0);
    CHECK_EQ((int)ruler_tiles(&r, DIST_CHEBYSHEV), 0);

    CASE("anchor and cursor give a single segment");
    ruler_start(&r, 2, 2);
    CHECK_EQ(r.active, 1);
    CHECK_EQ(r.n, 1);
    CHECK_EQ((int)ruler_tiles(&r, DIST_CHEBYSHEV), 0);
    ruler_set_cursor(&r, 6, 5);
    CHECK_EQ((int)ruler_tiles(&r, DIST_CHEBYSHEV), 4);
    CHECK_EQ((int)ruler_tiles(&r, DIST_MANHATTAN), 7);

    /* A bent path is what waypoints are for: each leg measured, then summed. */
    CASE("waypoints accumulate leg by leg");
    ruler_start(&r, 0, 0);
    ruler_set_cursor(&r, 3, 0);
    CHECK_EQ(ruler_add_waypoint(&r), 1);
    CHECK_EQ(r.n, 2);
    ruler_set_cursor(&r, 3, 4);
    CHECK_EQ((int)ruler_tiles(&r, DIST_CHEBYSHEV), 7);      /* 3 across, 4 down */
    CHECK_EQ(ruler_add_waypoint(&r), 1);
    ruler_set_cursor(&r, 5, 4);
    CHECK_EQ((int)ruler_tiles(&r, DIST_CHEBYSHEV), 9);

    /* Dropping a leg removes the waypoint but leaves the cursor where it is,
     * so the last leg now runs from the previous waypoint to the cursor. */
    CASE("dropping a leg re-measures from the previous waypoint");
    CHECK_EQ(ruler_drop_waypoint(&r), 1);
    CHECK_EQ(r.n, 2);
    CHECK_EQ((int)ruler_tiles(&r, DIST_CHEBYSHEV), 7);      /* 3 across, then 4 */
    CHECK_EQ(ruler_drop_waypoint(&r), 1);
    CHECK_EQ(r.n, 1);
    CHECK_EQ(ruler_drop_waypoint(&r), 0);                   /* the anchor stays */

    CASE("waypoints stop at the cap without corrupting the ruler");
    ruler_start(&r, 0, 0);
    int added = 0;
    for (int i = 0; i < RULER_MAX_POINTS + 8; i++) {
        ruler_set_cursor(&r, i + 1, 0);
        if (ruler_add_waypoint(&r)) added++;
    }
    CHECK_EQ(r.n, RULER_MAX_POINTS);
    CHECK_EQ(added, RULER_MAX_POINTS - 1);
    CHECK(ruler_tiles(&r, DIST_CHEBYSHEV) > 0);

    CASE("the traced line runs from anchor to cursor without gaps");
    ruler_start(&r, 1, 1);
    ruler_set_cursor(&r, 6, 4);
    RulerPt pts[64];
    int n = ruler_trace(&r, pts, 64);
    CHECK(n >= 6);
    CHECK_EQ(pts[0].x, 1);
    CHECK_EQ(pts[0].y, 1);
    CHECK_EQ(pts[n - 1].x, 6);
    CHECK_EQ(pts[n - 1].y, 4);
    for (int i = 1; i < n; i++) {
        int dx = pts[i].x - pts[i - 1].x, dy = pts[i].y - pts[i - 1].y;
        if (dx < 0) dx = -dx;
        if (dy < 0) dy = -dy;
        CHECK(dx <= 1 && dy <= 1 && (dx || dy));    /* contiguous, no repeats */
    }

    CASE("a waypoint join is not traced twice");
    ruler_start(&r, 0, 0);
    ruler_set_cursor(&r, 3, 0);
    ruler_add_waypoint(&r);
    ruler_set_cursor(&r, 3, 3);
    n = ruler_trace(&r, pts, 64);
    for (int i = 1; i < n; i++)
        CHECK(!(pts[i].x == pts[i - 1].x && pts[i].y == pts[i - 1].y));

    CASE("trace reports the full length even when the buffer is small");
    RulerPt few[3];
    ruler_start(&r, 0, 0);
    ruler_set_cursor(&r, 20, 0);
    CHECK_EQ(ruler_trace(&r, few, 3), 3);           /* capped, never overrun */
}

static void test_sight(void)
{
    Map *m = map_new(12, 12, "sight");
    map_fill_tiles(m, 0, 0, 11, 11, TILE_FLOOR);

    Ruler r;
    ruler_start(&r, 1, 1);

    CASE("open floor never blocks sight");
    ruler_set_cursor(&r, 9, 7);
    CHECK_EQ(ruler_sight_blocked(&r, m), 0);

    CASE("a wall across the line blocks it");
    for (int y = 0; y < 12; y++) map_set_vedge(m, 5, y, EDGE_WALL);
    CHECK_EQ(ruler_sight_blocked(&r, m), 1);

    CASE("a gap in that wall lets sight through");
    ruler_set_cursor(&r, 9, 1);
    map_set_vedge(m, 5, 1, EDGE_NONE);
    CHECK_EQ(ruler_sight_blocked(&r, m), 0);
    map_set_vedge(m, 5, 1, EDGE_WALL);
    CHECK_EQ(ruler_sight_blocked(&r, m), 1);

    CASE("sight is symmetric");
    Ruler back;
    ruler_start(&back, 9, 1);
    ruler_set_cursor(&back, 1, 1);
    CHECK_EQ(ruler_sight_blocked(&back, m), 1);

    for (int y = 0; y < 12; y++) map_set_vedge(m, 5, y, EDGE_NONE);

    /* Sight is not movement: you can see over a pit you cannot walk across. */
    CASE("void tiles do not block sight the way they block movement");
    map_set_tile(m, 5, 4, TILE_VOID);
    ruler_start(&r, 4, 4);
    ruler_set_cursor(&r, 7, 4);
    CHECK_EQ(ruler_sight_blocked(&r, m), 0);
    CHECK_EQ(map_blocked(m, 4, 4, 1, 0), 1);        /* but you cannot step there */
    map_set_tile(m, 5, 4, TILE_FLOOR);

    /* Looking diagonally past a corner: one wall still leaves a way round,
     * two walls meeting at the corner do not. */
    CASE("a diagonal past a single wall is still visible");
    ruler_start(&r, 2, 2);
    ruler_set_cursor(&r, 3, 3);
    CHECK_EQ(ruler_sight_blocked(&r, m), 0);
    map_set_vedge(m, 3, 2, EDGE_WALL);
    CHECK_EQ(ruler_sight_blocked(&r, m), 0);

    CASE("a diagonal into a sealed corner is not");
    map_set_hedge(m, 2, 3, EDGE_WALL);
    CHECK_EQ(ruler_sight_blocked(&r, m), 1);
    map_set_vedge(m, 3, 2, EDGE_NONE);
    map_set_hedge(m, 2, 3, EDGE_NONE);

    CASE("measuring to where you stand is always clear");
    ruler_start(&r, 5, 5);
    CHECK_EQ(ruler_sight_blocked(&r, m), 0);

    map_free(m);
}

static void test_measure_settings(void)
{
    char path[] = "/tmp/vtt-scale-XXXXXX";
    int  fd = mkstemp(path);
    if (fd >= 0) close(fd);

    Map *m = map_new(8, 8, "scaled");
    map_fill_tiles(m, 0, 0, 7, 7, TILE_FLOOR);

    CASE("a new map defaults to five-foot squares, 5-10-5, and no ruleset");
    CHECK_EQ((int)m->scale_ft, 5);
    CHECK_EQ(m->metric, DIST_ALT_DIAG);
    CHECK_EQ(m->metric, MAP_METRIC_DEFAULT);
    CHECK_EQ(m->ruleset[0], '\0');

    m->scale_ft = 10.0;
    m->metric   = DIST_EUCLIDEAN;
    str_lcpy(m->ruleset, "daggerheart", sizeof m->ruleset);

    char err[MAPIO_ERR_MAX] = { 0 };
    CASE("measurement settings travel with the map");
    CHECK_EQ(mapio_save(m, path, err, sizeof err), 0);

    Map *l = mapio_load(path, err, sizeof err);
    CHECK(l != NULL);
    if (l) {
        CHECK_EQ((int)l->scale_ft, 10);
        CHECK_EQ(l->metric, DIST_EUCLIDEAN);
        CHECK_EQ(strcmp(l->ruleset, "daggerheart"), 0);
        map_free(l);
    }

    /* An older map has none of these lines, and must still load. */
    CASE("a map without measurement settings gets the defaults");
    FILE *f = fopen(path, "w");
    if (f) {
        fputs("VTT 1\nname Old\nsize 4 4\nzoom 1\ntiles\n....\n....\n....\n....\n", f);
        fclose(f);
    }
    Map *old = mapio_load(path, err, sizeof err);
    CHECK(old != NULL);
    if (old) {
        CHECK_EQ((int)old->scale_ft, (int)MAP_SCALE_DEFAULT);
        CHECK_EQ(old->metric, MAP_METRIC_DEFAULT);
        CHECK_EQ(old->ruleset[0], '\0');
        map_free(old);
    }

    CASE("a nonsense scale falls back rather than poisoning every measurement");
    f = fopen(path, "w");
    if (f) {
        fputs("VTT 1\nname Bad\nsize 4 4\nscale -3\nmetric wat\nruleset nope\n"
              "tiles\n....\n....\n....\n....\n", f);
        fclose(f);
    }
    Map *bad = mapio_load(path, err, sizeof err);
    CHECK(bad != NULL);
    if (bad) {
        CHECK_EQ((int)bad->scale_ft, (int)MAP_SCALE_DEFAULT);
        CHECK_EQ(bad->metric, MAP_METRIC_DEFAULT);
        CHECK_EQ(bad->ruleset[0], '\0');
        map_free(bad);
    }

    map_free(m);
    unlink(path);
}


/* --------------------------------------------------------- range overlay */

static void test_range(void)
{
    Map *m = map_new(21, 15, "range");
    map_fill_tiles(m, 0, 0, 20, 14, TILE_FLOOR);
    str_lcpy(m->ruleset, "daggerheart", sizeof m->ruleset);

    RangeOverlay ro;
    range_clear(&ro);

    CASE("the overlay starts off and highlights nothing");
    CHECK_EQ(ro.active, 0);
    CHECK_EQ(range_contains(&ro, m, 5, 5), 0);

    CASE("cycling walks the bands then switches off");
    const Ruleset *rs = ruleset_by_name("daggerheart");
    CHECK(rs != NULL);
    for (int i = 0; i < rs->nbands; i++) {
        CHECK_EQ(range_cycle(&ro, m, -1, 10, 7), i);
        CHECK_EQ(ro.active, 1);
        CHECK_EQ(ro.band, i);
    }
    CHECK_EQ(range_cycle(&ro, m, -1, 10, 7), -1);
    CHECK_EQ(ro.active, 0);

    /* The anchor is taken once, on the way in, so walking the cursor away
     * while flipping through bands does not drag the highlight with it. */
    CASE("cycling does not move the anchor");
    range_clear(&ro);
    range_cycle(&ro, m, -1, 10, 7);
    range_cycle(&ro, m, -1, 2, 2);
    range_cycle(&ro, m, -1, 18, 13);
    int ax, ay, as;
    range_anchor(&ro, m, &ax, &ay, &as);
    CHECK_EQ(ax, 10);
    CHECK_EQ(ay, 7);
    CHECK_EQ(as, 1);

    CASE("without a ruleset there are no bands to show");
    Map *plain = map_new(10, 10, "plain");
    RangeOverlay bare;
    range_clear(&bare);
    CHECK_EQ(range_cycle(&bare, plain, -1, 5, 5), -1);
    CHECK_EQ(bare.active, 0);
    map_free(plain);

    /* Melee is one square, so exactly the eight neighbours and the anchor. */
    CASE("Melee covers the anchor and its neighbours, and nothing else");
    range_clear(&ro);
    range_cycle(&ro, m, -1, 10, 7);          /* band 0: Melee */
    int inside = 0;
    for (int y = 0; y < m->h; y++)
        for (int x = 0; x < m->w; x++)
            if (range_contains(&ro, m, x, y)) inside++;
    CHECK_EQ(inside, 9);
    CHECK_EQ(range_contains(&ro, m, 10, 7), 1);
    CHECK_EQ(range_contains(&ro, m, 11, 8), 1);
    CHECK_EQ(range_contains(&ro, m, 12, 7), 0);

    CASE("the highlight matches the band's reach exactly");
    ro.band = 2;                              /* Close: 30 ft, 6 squares */
    CHECK_EQ(range_contains(&ro, m, 16, 7), 1);   /* 6 squares east */
    CHECK_EQ(range_contains(&ro, m, 17, 7), 0);   /* 7 squares east */
    CHECK_EQ(range_contains(&ro, m, 10, 13), 1);  /* 6 squares south */

    /* Under 5-10-5 a diagonal costs more, so the region is an octagon rather
     * than the square Chebyshev would give. */
    CASE("the shape follows the metric");
    CHECK_EQ(m->metric, DIST_ALT_DIAG);
    CHECK_EQ(range_contains(&ro, m, 14, 11), 1);  /* 4 diagonal: 6 tiles */
    CHECK_EQ(range_contains(&ro, m, 15, 12), 0);  /* 5 diagonal: 7 tiles */
    int alt_count = 0;
    for (int y = 0; y < m->h; y++)
        for (int x = 0; x < m->w; x++)
            if (range_contains(&ro, m, x, y)) alt_count++;

    m->metric = DIST_CHEBYSHEV;
    CHECK_EQ(range_contains(&ro, m, 15, 12), 1);  /* free diagonals reach further */
    int cheb_count = 0;
    for (int y = 0; y < m->h; y++)
        for (int x = 0; x < m->w; x++)
            if (range_contains(&ro, m, x, y)) cheb_count++;
    CHECK(cheb_count > alt_count);
    m->metric = DIST_ALT_DIAG;

    CASE("the highlight is symmetric about its anchor");
    for (int d = 1; d <= 6; d++) {
        CHECK_EQ(range_contains(&ro, m, 10 + d, 7), range_contains(&ro, m, 10 - d, 7));
        CHECK_EQ(range_contains(&ro, m, 10, 7 + d), range_contains(&ro, m, 10, 7 - d));
    }

    CASE("scale changes what a band reaches");
    m->scale_ft = 10.0;
    CHECK_EQ(range_contains(&ro, m, 13, 7), 1);   /* 3 squares = 30 ft */
    CHECK_EQ(range_contains(&ro, m, 14, 7), 0);   /* 4 squares = 40 ft */
    m->scale_ft = 5.0;

    /* A creature bigger than one square reaches from its nearest square, not
     * from a corner. */
    CASE("a large anchor measures from its nearest square");
    Token big = { 10, 7, 3, TOKEN_ENEMY, "Troll" };
    int bi = tokens_add(&m->tokens, big);
    range_clear(&ro);
    range_cycle(&ro, m, bi, 0, 0);            /* Melee, anchored to the troll */
    range_anchor(&ro, m, &ax, &ay, &as);
    CHECK_EQ(ax, 10);
    CHECK_EQ(as, 3);
    CHECK_EQ(range_contains(&ro, m, 13, 9), 1);   /* touching its east face */
    CHECK_EQ(range_contains(&ro, m, 14, 9), 0);
    CHECK_EQ(range_contains(&ro, m, 9, 7), 1);    /* and its west face */

    CASE("the highlight follows the creature it is anchored to");
    m->tokens.v[bi].x = 4;
    range_anchor(&ro, m, &ax, &ay, &as);
    CHECK_EQ(ax, 4);
    CHECK_EQ(range_contains(&ro, m, 3, 7), 1);
    CHECK_EQ(range_contains(&ro, m, 13, 9), 0);

    /* Indices shift when a token is removed, so an anchor holding one must
     * not silently start following whoever inherits it. */
    CASE("removing the anchor's creature leaves the highlight where it stood");
    range_token_removed(&ro, bi, 4, 7);
    CHECK_EQ(ro.token, -1);
    range_anchor(&ro, m, &ax, &ay, &as);
    CHECK_EQ(ax, 4);
    CHECK_EQ(ay, 7);

    CASE("removing an earlier token keeps the anchor on the same creature");
    range_clear(&ro);
    range_cycle(&ro, m, 3, 0, 0);
    range_token_removed(&ro, 1, 0, 0);
    CHECK_EQ(ro.token, 2);
    range_token_removed(&ro, 5, 0, 0);        /* a later one changes nothing */
    CHECK_EQ(ro.token, 2);

    map_free(m);
}

static void test_range_sight(void)
{
    Map *m = map_new(21, 11, "rsight");
    map_fill_tiles(m, 0, 0, 20, 10, TILE_FLOOR);
    str_lcpy(m->ruleset, "daggerheart", sizeof m->ruleset);

    RangeOverlay ro;
    range_clear(&ro);
    range_cycle(&ro, m, -1, 5, 5);
    ro.band = 2;                               /* Close, 6 squares */

    CASE("open ground is all in range and all visible");
    CHECK_EQ(range_contains(&ro, m, 11, 5), 1);
    CHECK_EQ(sight_blocked(m, 5, 5, 11, 5), 0);

    /* A wall does not shrink the band -- distance is distance -- it only
     * changes which of those squares can actually be targeted. */
    CASE("a wall leaves squares in range but out of sight");
    for (int y = 0; y < 11; y++) map_set_vedge(m, 8, y, EDGE_WALL);
    CHECK_EQ(range_contains(&ro, m, 11, 5), 1);
    CHECK_EQ(sight_blocked(m, 5, 5, 11, 5), 1);

    CASE("a gap in the wall restores sight along that line");
    map_set_vedge(m, 8, 5, EDGE_NONE);
    CHECK_EQ(sight_blocked(m, 5, 5, 11, 5), 0);
    CHECK_EQ(sight_blocked(m, 5, 5, 11, 1), 1);

    /* The overlay and the ruler must never disagree about the same line. */
    CASE("the overlay and the ruler agree about sight");
    for (int y = 0; y < 11; y++) {
        for (int x = 0; x < 21; x++) {
            Ruler r;
            ruler_start(&r, 5, 5);
            ruler_set_cursor(&r, x, y);
            CHECK_EQ(ruler_sight_blocked(&r, m), sight_blocked(m, 5, 5, x, y));
        }
    }

    map_free(m);
}


/* ------------------------------------------------------- doors and terrain */

static void test_edges(void)
{
    Map *m = map_new(9, 9, "edges");
    map_fill_tiles(m, 0, 0, 8, 8, TILE_FLOOR);

    /* Movement and sight are separate questions, and each kind answers them
     * differently. This is the whole point of having kinds at all. */
    CASE("each boundary kind stops what it should");
    const struct { uint8_t kind; int stops_move; int stops_sight; } K[] = {
        { EDGE_NONE,          0, 0 },
        { EDGE_WALL,          1, 1 },
        { EDGE_DOOR_CLOSED,   1, 1 },
        { EDGE_DOOR_OPEN,     0, 0 },
        { EDGE_WINDOW,        1, 0 },   /* see through, cannot walk through */
        { EDGE_SECRET_CLOSED, 1, 1 },
        { EDGE_SECRET_OPEN,   0, 0 },
    };
    for (size_t i = 0; i < sizeof K / sizeof *K; i++) {
        map_set_vedge(m, 5, 4, K[i].kind);
        CHECK_EQ(map_edge_blocked(m, 4, 4, 1, 0), K[i].stops_move);
        CHECK_EQ(map_edge_opaque(m, 4, 4, 1, 0), K[i].stops_sight);
        CHECK_EQ(map_blocked(m, 4, 4, 1, 0), K[i].stops_move);
        CHECK_EQ(sight_blocked(m, 4, 4, 6, 4), K[i].stops_sight);
    }
    map_set_vedge(m, 5, 4, EDGE_NONE);

    CASE("a window is the one you can shoot through but not walk through");
    map_set_vedge(m, 5, 4, EDGE_WINDOW);
    CHECK_EQ(map_blocked(m, 4, 4, 1, 0), 1);
    CHECK_EQ(sight_blocked(m, 4, 4, 8, 4), 0);
    map_set_vedge(m, 5, 4, EDGE_NONE);

    CASE("only doors toggle");
    CHECK_EQ(edge_is_door(EDGE_DOOR_CLOSED), 1);
    CHECK_EQ(edge_is_door(EDGE_SECRET_OPEN), 1);
    CHECK_EQ(edge_is_door(EDGE_WALL), 0);
    CHECK_EQ(edge_is_door(EDGE_WINDOW), 0);
    CHECK_EQ(edge_toggled(EDGE_DOOR_CLOSED), EDGE_DOOR_OPEN);
    CHECK_EQ(edge_toggled(EDGE_DOOR_OPEN), EDGE_DOOR_CLOSED);
    CHECK_EQ(edge_toggled(EDGE_SECRET_CLOSED), EDGE_SECRET_OPEN);
    CHECK_EQ(edge_toggled(EDGE_WALL), EDGE_WALL);      /* a wall is a wall */

    CASE("opening a door opens the way through it");
    map_set_vedge(m, 5, 4, EDGE_DOOR_CLOSED);
    CHECK_EQ(map_blocked(m, 4, 4, 1, 0), 1);
    map_set_vedge(m, 5, 4, edge_toggled(map_vedge(m, 5, 4)));
    CHECK_EQ(map_vedge(m, 5, 4), EDGE_DOOR_OPEN);
    CHECK_EQ(map_blocked(m, 4, 4, 1, 0), 0);
    CHECK_EQ(sight_blocked(m, 4, 4, 6, 4), 0);

    CASE("every kind survives a round trip through its file character");
    for (int k = 0; k < EDGE_COUNT; k++)
        CHECK_EQ(edge_from_file_char(edge_file_char((uint8_t)k)), k);
    for (int k = 0; k < TILE_COUNT; k++)
        CHECK_EQ(tile_from_file_char(tile_file_char((uint8_t)k)), k);

    CASE("horizontal walls written before doors existed still load");
    CHECK_EQ(edge_from_file_char('-'), EDGE_WALL);
    CHECK_EQ(edge_from_file_char('?'), -1);
    CHECK_EQ(tile_from_file_char('?'), -1);

    map_free(m);
}

static void test_terrain(void)
{
    Map *m = map_new(8, 8, "terrain");

    /* Terrain is decoration. Anything that is not void is map, and the map
     * does not decide what difficult ground costs. */
    CASE("every terrain is walkable; only void is not");
    for (int k = 0; k < TILE_COUNT; k++) {
        map_set_tile(m, 3, 3, (uint8_t)k);
        CHECK_EQ(map_walkable(m, 3, 3), k != TILE_VOID);
    }

    CASE("terrain does not affect movement or sight");
    map_fill_tiles(m, 0, 0, 7, 7, TILE_HAZARD);
    CHECK_EQ(map_blocked(m, 3, 3, 1, 0), 0);
    CHECK_EQ(sight_blocked(m, 0, 3, 7, 3), 0);
    map_fill_tiles(m, 0, 0, 7, 7, TILE_WATER);
    CHECK_EQ(map_blocked(m, 3, 3, 1, 0), 0);

    /* Grid lines mark walkable ground, so they must follow terrain and not
     * just plain floor. */
    CASE("grid lines show on every terrain, not only on floor");
    Renderer r;
    rnd_init(&r);
    rnd_resize(&r, 40, 20);
    GridView g;
    memset(&g, 0, sizeof g);
    g.zoom = 0;
    g.view = rect(0, 0, 40, 20);

    for (int k = TILE_FLOOR; k < TILE_COUNT; k++) {
        Map *t = map_new(3, 3, "t");
        map_fill_tiles(t, 0, 0, 2, 2, (uint8_t)k);
        rnd_begin(&r);
        grid_draw(&r, t, &g, &THEME_DARK, 0, 1);
        CHECK_EQ(rnd_at(&r, 0, 0)->ch, 0x250Cu);      /* the map's outer corner */
        map_free(t);
    }

    CASE("void draws nothing at all, whatever the palette");
    Map *v = map_new(3, 3, "v");
    rnd_begin(&r);
    grid_draw(&r, v, &g, &THEME_DARK, 0, 1);
    int drawn = 0;
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++)
            if (rnd_at(&r, x, y)->ch != ' ') drawn++;
    CHECK_EQ(drawn, 0);
    map_free(v);

    rnd_free(&r);
    map_free(m);
}

static void test_secret_doors(void)
{
    Map *m = map_new(5, 5, "secret");
    map_fill_tiles(m, 0, 0, 4, 4, TILE_FLOOR);
    map_set_vedge(m, 2, 2, EDGE_SECRET_CLOSED);

    Map *w = map_new(5, 5, "wall");
    map_fill_tiles(w, 0, 0, 4, 4, TILE_FLOOR);
    map_set_vedge(w, 2, 2, EDGE_WALL);

    Renderer r;
    rnd_init(&r);
    rnd_resize(&r, 40, 20);
    GridView g;
    memset(&g, 0, sizeof g);
    g.zoom = 1;
    g.view = rect(0, 0, 40, 20);

    /* The point of a secret door is that nobody reading the screen in play
     * can tell it from a wall. Not by glyph, and not by colour. */
    CASE("in play mode a secret door is pixel for pixel a wall");
    rnd_begin(&r);
    grid_draw(&r, m, &g, &THEME_DARK, 0, 0);
    Cell secret[40 * 20];
    memcpy(secret, r.back, sizeof secret);

    rnd_begin(&r);
    grid_draw(&r, w, &g, &THEME_DARK, 0, 0);
    int same = 1;
    for (int i = 0; i < 40 * 20; i++)
        if (secret[i].ch != r.back[i].ch || secret[i].fg != r.back[i].fg ||
            secret[i].bg != r.back[i].bg || secret[i].attr != r.back[i].attr)
            same = 0;
    CHECK_EQ(same, 1);

    CASE("in build mode it is marked, so the GM can see their own door");
    rnd_begin(&r);
    grid_draw(&r, m, &g, &THEME_DARK, 0, 1);
    int differs = 0;
    for (int i = 0; i < 40 * 20; i++)
        if (secret[i].ch != r.back[i].ch || secret[i].fg != r.back[i].fg)
            differs++;
    CHECK(differs > 0);

    CASE("but it blocks exactly like a wall either way");
    CHECK_EQ(map_blocked(m, 1, 2, 1, 0), map_blocked(w, 1, 2, 1, 0));
    CHECK_EQ(sight_blocked(m, 1, 2, 3, 2), sight_blocked(w, 1, 2, 3, 2));

    rnd_free(&r);
    map_free(m);
    map_free(w);
}

static void test_map_format_v2(void)
{
    char path[] = "/tmp/vtt-v2-XXXXXX";
    int  fd = mkstemp(path);
    if (fd >= 0) close(fd);

    Map *m = map_new(10, 6, "kinds");
    for (int k = TILE_FLOOR, x = 0; x < 10; x++, k++) {
        if (k >= TILE_COUNT) k = TILE_FLOOR;
        for (int y = 0; y < 6; y++) map_set_tile(m, x, y, (uint8_t)k);
    }
    for (int k = EDGE_WALL, y = 0; y < 6; y++, k++) {
        if (k >= EDGE_COUNT) k = EDGE_WALL;
        map_set_vedge(m, 3, y, (uint8_t)k);
        map_set_hedge(m, y, 2, (uint8_t)k);
    }

    char err[MAPIO_ERR_MAX] = { 0 };
    CASE("every terrain and boundary kind survives a save and load");
    CHECK_EQ(mapio_save(m, path, err, sizeof err), 0);

    Map *l = mapio_load(path, err, sizeof err);
    CHECK(l != NULL);
    if (l) {
        int tiles_ok = 1, v_ok = 1, h_ok = 1;
        for (int y = 0; y < 6; y++)
            for (int x = 0; x < 10; x++)
                if (map_tile(l, x, y) != map_tile(m, x, y)) tiles_ok = 0;
        for (int y = 0; y < 6; y++)
            for (int x = 0; x <= 10; x++)
                if (map_vedge(l, x, y) != map_vedge(m, x, y)) v_ok = 0;
        for (int y = 0; y <= 6; y++)
            for (int x = 0; x < 10; x++)
                if (map_hedge(l, x, y) != map_hedge(m, x, y)) h_ok = 0;
        CHECK(tiles_ok);
        CHECK(v_ok);
        CHECK(h_ok);
        map_free(l);
    }

    /* A v1 map predates doors and terrain, and must still open. */
    CASE("a version 1 map still loads, as walls and plain floor");
    FILE *f = fopen(path, "w");
    if (f) {
        fputs("VTT 1\nname Old\nsize 4 3\nzoom 1\n"
              "tiles\n....\n....\n....\n"
              "vedges\n|   |\n|   |\n|   |\n"
              "hedges\n----\n    \n    \n----\n", f);
        fclose(f);
    }
    Map *old = mapio_load(path, err, sizeof err);
    CHECK(old != NULL);
    if (old) {
        CHECK_EQ(map_tile(old, 0, 0), TILE_FLOOR);
        CHECK_EQ(map_vedge(old, 0, 0), EDGE_WALL);
        CHECK_EQ(map_vedge(old, 4, 0), EDGE_WALL);
        CHECK_EQ(map_hedge(old, 0, 0), EDGE_WALL);      /* written as '-' */
        CHECK_EQ(map_hedge(old, 0, 1), EDGE_NONE);
        map_free(old);
    }

    CASE("an unreadable character reads as empty rather than failing the load");
    f = fopen(path, "w");
    if (f) {
        fputs("VTT 2\nname Odd\nsize 3 2\ntiles\n.@.\n...\n"
              "vedges\n|@ |\n    \nhedges\n-@-\n   \n   \n", f);
        fclose(f);
    }
    Map *odd = mapio_load(path, err, sizeof err);
    CHECK(odd != NULL);
    if (odd) {
        CHECK_EQ(map_tile(odd, 1, 0), TILE_VOID);
        CHECK_EQ(map_vedge(odd, 0, 0), EDGE_WALL);
        CHECK_EQ(map_vedge(odd, 1, 0), EDGE_NONE);
        map_free(odd);
    }

    map_free(m);
    unlink(path);
}

static void test_edge_tools(void)
{
    Map *m = map_new(9, 9, "tools");
    map_fill_tiles(m, 0, 0, 8, 8, TILE_FLOOR);

    Undo u;
    undo_init(&u);
    Editor e;
    ed_init(&e, m);
    ed_layout(&e, m, 80, 24);

    CASE("a fresh editor lays walls on plain floor");
    CHECK_EQ(e.material, EDGE_WALL);
    CHECK_EQ(e.terrain, TILE_FLOOR);

    CASE("the selectors cycle and wrap past the eraser");
    for (int i = 0; i < EDGE_COUNT * 2; i++) {
        ed_cycle_material(&e);
        CHECK(e.material != EDGE_NONE);
        CHECK(e.material < EDGE_COUNT);
    }
    for (int i = 0; i < TILE_COUNT * 2; i++) {
        ed_cycle_terrain(&e);
        CHECK(e.terrain != TILE_VOID);
        CHECK(e.terrain < TILE_COUNT);
    }

    CASE("a face takes the selected material, and drops it when pressed again");
    e.material = EDGE_WINDOW;
    e.cx = 4; e.cy = 4;
    ed_toggle_edge(&e, m, &u, 1, 0);
    CHECK_EQ(map_vedge(m, 5, 4), EDGE_WINDOW);
    ed_toggle_edge(&e, m, &u, 1, 0);
    CHECK_EQ(map_vedge(m, 5, 4), EDGE_NONE);

    CASE("a different material replaces rather than clears");
    e.material = EDGE_WALL;
    ed_toggle_edge(&e, m, &u, 1, 0);
    CHECK_EQ(map_vedge(m, 5, 4), EDGE_WALL);
    e.material = EDGE_DOOR_CLOSED;
    ed_toggle_edge(&e, m, &u, 1, 0);
    CHECK_EQ(map_vedge(m, 5, 4), EDGE_DOOR_CLOSED);

    CASE("the pen lays whatever is selected");
    e.material = EDGE_WINDOW;
    e.mode = ED_WALL;
    e.wx = 1; e.wy = 1;
    e.pen = 1;
    ed_wall_step(&e, m, &u, 1, 0, 3);
    for (int x = 1; x < 4; x++) CHECK_EQ(map_hedge(m, x, 1), EDGE_WINDOW);
    e.mode = ED_NORMAL;
    e.pen = 0;

    CASE("painting uses the selected terrain");
    e.terrain = TILE_WATER;
    e.cx = 6; e.cy = 6;
    ed_apply_tiles(&e, m, &u, e.terrain);
    CHECK_EQ(map_tile(m, 6, 6), TILE_WATER);
    ed_toggle_tile(&e, m, &u);
    CHECK_EQ(map_tile(m, 6, 6), TILE_VOID);
    ed_toggle_tile(&e, m, &u);
    CHECK_EQ(map_tile(m, 6, 6), TILE_WATER);

    /* Opening an ordinary door beside a secret one must not give the secret
     * away, so the two are toggled by separate requests. */
    CASE("doors and secret doors toggle separately");
    e.cx = 4; e.cy = 4;
    map_set_vedge(m, 4, 4, EDGE_DOOR_CLOSED);      /* west face */
    map_set_vedge(m, 5, 4, EDGE_SECRET_CLOSED);    /* east face */

    CHECK_EQ(ed_toggle_doors(&e, m, &u, 0), 1);
    CHECK_EQ(map_vedge(m, 4, 4), EDGE_DOOR_OPEN);
    CHECK_EQ(map_vedge(m, 5, 4), EDGE_SECRET_CLOSED);   /* untouched */

    CHECK_EQ(ed_toggle_doors(&e, m, &u, 1), 1);
    CHECK_EQ(map_vedge(m, 5, 4), EDGE_SECRET_OPEN);

    CASE("toggling a door is undoable");
    CHECK_EQ(undo_undo(&u, m), 1);
    CHECK_EQ(map_vedge(m, 5, 4), EDGE_SECRET_CLOSED);

    CASE("a tile with no doors reports none, and changes nothing");
    e.cx = 8; e.cy = 8;
    CHECK_EQ(ed_toggle_doors(&e, m, &u, 0), 0);
    CHECK_EQ(ed_toggle_doors(&e, m, &u, 1), 0);

    undo_free(&u);
    map_free(m);
}


/* -------------------------------------------------------- deleting maps */

static void write_map_file(const char *dir, const char *name)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE *f = fopen(path, "w");
    if (!f) return;
    fputs("VTT 2\nname x\nsize 2 2\nzoom 1\ntiles\n..\n..\n"
          "vedges\n   \n   \nhedges\n  \n  \n  \n", f);
    fclose(f);
}

static int file_exists(const char *dir, const char *name)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    return access(path, F_OK) == 0;
}

/* Drives the app through the real input parser rather than a hand-rolled
 * translation, so a test types what a terminal would send: Ctrl-U arrives as
 * 0x15 and becomes MOD_CTRL 'u', and a trailing ESC resolves on the timeout
 * exactly as the event loop resolves it. */
static void press(App *a, const char *keys)
{
    InputParser p;
    input_init(&p);
    input_feed(&p, keys, strlen(keys));

    Key k;
    while (input_next(&p, &k)) app_key(a, k);
    while (input_pending(&p) && input_timeout(&p, &k)) app_key(a, k);
}

/* The browser reads the working directory AND the user's map directory, so a
 * test that deletes or renames has to pin both. Without this it would list --
 * and then act on -- somebody's real map. */
typedef struct {
    char dir[1024];
    char datadir[1100];
    char cwd[1024];
    char saved_xdg[1024];
    int  ok;
} Sandbox;

static Sandbox sandbox_enter(const char *tag)
{
    Sandbox s;
    memset(&s, 0, sizeof s);

    snprintf(s.dir, sizeof s.dir, "/tmp/vtt-%s-XXXXXX", tag);
    if (!mkdtemp(s.dir)) return s;
    if (!getcwd(s.cwd, sizeof s.cwd)) return s;

    snprintf(s.datadir, sizeof s.datadir, "%s/xdg", s.dir);
    mkdir(s.datadir, 0755);

    const char *old = getenv("XDG_DATA_HOME");
    if (old) str_lcpy(s.saved_xdg, old, sizeof s.saved_xdg);
    setenv("XDG_DATA_HOME", s.datadir, 1);

    s.ok = 1;
    return s;
}

static void sandbox_leave(Sandbox *s)
{
    if (!s->ok) return;
    if (chdir(s->cwd) != 0) { }
    if (s->saved_xdg[0]) setenv("XDG_DATA_HOME", s->saved_xdg, 1);
    else                 unsetenv("XDG_DATA_HOME");
    rmdir(s->datadir);
}

static void test_delete_map(void)
{
    Sandbox sb = sandbox_enter("del");
    CHECK_EQ(sb.ok, 1);
    if (!sb.ok) return;
    const char *dir = sb.dir;

    write_map_file(dir, "alpha.vtt");
    write_map_file(dir, "bravo.vtt");
    write_map_file(dir, "charlie.vtt");
    if (chdir(dir) != 0) { CHECK(0); return; }

    Renderer r;
    App      a;
    rnd_init(&r);
    rnd_resize(&r, 80, 24);
    app_init(&a, NULL, &r);

    press(&a, "\r");                       /* menu -> Open Map */
    CASE("the browser finds the maps");
    CHECK_EQ(a.screen, SCREEN_BROWSER);
    CHECK_EQ(a.nentries, 3);

    CASE("d asks before it deletes anything");
    press(&a, "jd");                       /* select bravo, then delete */
    CHECK_EQ(a.modal, MODAL_CONFIRM_DELETE);
    CHECK(strstr(a.modal_body, "bravo.vtt") != NULL);
    CHECK_EQ(file_exists(dir, "bravo.vtt"), 1);   /* nothing gone yet */

    /* While the question is up, nothing else may act -- least of all the
     * keys that would move the selection out from under it. */
    CASE("the confirmation swallows every other key");
    int sel_before = a.browser.sel;
    press(&a, "jkgGr");
    CHECK_EQ(a.modal, MODAL_CONFIRM_DELETE);
    CHECK_EQ(a.browser.sel, sel_before);
    CHECK_EQ(a.nentries, 3);

    CASE("n keeps the file");
    press(&a, "n");
    CHECK_EQ(a.modal, MODAL_NONE);
    CHECK_EQ(file_exists(dir, "bravo.vtt"), 1);
    CHECK_EQ(a.nentries, 3);

    CASE("esc keeps it too");
    press(&a, "d\x1b");
    CHECK_EQ(a.modal, MODAL_NONE);
    CHECK_EQ(file_exists(dir, "bravo.vtt"), 1);

    CASE("y deletes it, and only it");
    press(&a, "dy");
    CHECK_EQ(a.modal, MODAL_NONE);
    CHECK_EQ(file_exists(dir, "bravo.vtt"), 0);
    CHECK_EQ(file_exists(dir, "alpha.vtt"), 1);
    CHECK_EQ(file_exists(dir, "charlie.vtt"), 1);

    CASE("the list refreshes without being asked");
    CHECK_EQ(a.nentries, 2);

    /* Deleting several in a row should not send you back to the top. */
    CASE("the caret keeps its place");
    CHECK_EQ(a.browser.sel, 1);
    CHECK(strstr(a.entries[a.browser.sel].name, "charlie") != NULL);

    CASE("deleting the last entry clamps the caret rather than running off");
    press(&a, "dy");
    CHECK_EQ(a.nentries, 1);
    CHECK_EQ(a.browser.sel, 0);
    CHECK(strstr(a.entries[0].name, "alpha") != NULL);

    CASE("an empty list has nothing to delete and says so");
    press(&a, "dy");
    CHECK_EQ(a.nentries, 0);
    press(&a, "d");
    CHECK_EQ(a.modal, MODAL_NONE);
    CHECK(strstr(a.status, "nothing") != NULL);

    /* A file that will not unlink must report, not pretend. */
    CASE("a delete that fails reports instead of lying");
    write_map_file(dir, "guard.vtt");
    char sub[1200];
    snprintf(sub, sizeof sub, "%.1000s/locked", dir);
    if (mkdir(sub, 0755) == 0) {
        write_map_file(sub, "inner.vtt");
        chmod(sub, 0500);                  /* readable, not writable */
    }
    press(&a, "r");
    CHECK(a.nentries >= 1);

    app_free(&a);
    rnd_free(&r);

    sandbox_leave(&sb);

    /* Tidy up whatever survived. */
    chmod(sub, 0700);
    char p2[1400];
    snprintf(p2, sizeof p2, "%.1200s/inner.vtt", sub); unlink(p2);
    rmdir(sub);
    snprintf(p2, sizeof p2, "%.1200s/alpha.vtt", dir); unlink(p2);
    snprintf(p2, sizeof p2, "%.1200s/guard.vtt", dir); unlink(p2);
    rmdir(dir);
}

/* Reads the map's title straight out of the file, to check the rename reached
 * inside and not only the directory entry. */
static void read_title(const char *dir, const char *name, char *out, size_t n)
{
    out[0] = '\0';
    char path[512];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE *f = fopen(path, "r");
    if (!f) return;
    char line[256];
    while (fgets(line, sizeof line, f))
        if (!strncmp(line, "name ", 5)) {
            size_t l = strlen(line);
            while (l && (line[l - 1] == '\n' || line[l - 1] == '\r')) line[--l] = '\0';
            str_lcpy(out, line + 5, n);
            break;
        }
    fclose(f);
}

static void test_rename_map(void)
{
    Sandbox sb = sandbox_enter("ren");
    CHECK_EQ(sb.ok, 1);
    if (!sb.ok) return;
    const char *dir = sb.dir;

    write_map_file(dir, "alpha.vtt");
    write_map_file(dir, "bravo.vtt");
    if (chdir(dir) != 0) { CHECK(0); sandbox_leave(&sb); return; }

    Renderer r;
    App      a;
    rnd_init(&r);
    rnd_resize(&r, 80, 24);
    app_init(&a, NULL, &r);

    press(&a, "\r");
    CHECK_EQ(a.nentries, 2);

    CASE("R offers the current name, without its extension");
    press(&a, "jR");
    CHECK_EQ(a.modal, MODAL_PROMPT);
    CHECK_EQ(strcmp(a.prompt.buf, "bravo"), 0);

    CASE("esc leaves the file alone");
    press(&a, "\x1b");
    CHECK_EQ(file_exists(dir, "bravo.vtt"), 1);

    /* The name in the browser and the title in the editor should not drift
     * apart, so a rename reaches inside the file too. */
    CASE("renaming moves the file and retitles the map");
    press(&a, "R\025goblin\r");
    CHECK_EQ(file_exists(dir, "bravo.vtt"), 0);
    CHECK_EQ(file_exists(dir, "goblin.vtt"), 1);
    char title[128];
    read_title(dir, "goblin.vtt", title, sizeof title);
    CHECK_EQ(strcmp(title, "goblin"), 0);

    CASE("the caret follows the file to wherever it now sorts");
    CHECK_EQ(a.nentries, 2);
    CHECK(strstr(a.entries[a.browser.sel].name, "goblin") != NULL);

    /* rename(2) would silently destroy the other map; it must refuse. */
    CASE("renaming onto an existing map refuses instead of clobbering it");
    press(&a, "R\025alpha\r");
    CHECK_EQ(a.modal, MODAL_MESSAGE);
    CHECK(strstr(a.modal_body, "already exists") != NULL);
    CHECK_EQ(file_exists(dir, "goblin.vtt"), 1);
    CHECK_EQ(file_exists(dir, "alpha.vtt"), 1);
    read_title(dir, "alpha.vtt", title, sizeof title);
    CHECK_EQ(strcmp(title, "x"), 0);        /* the other map is untouched */
    press(&a, " ");                         /* dismiss */

    CASE("a name with a slash is refused: this renames, it does not move");
    press(&a, "R\025../escaped\r");
    CHECK_EQ(file_exists(dir, "goblin.vtt"), 1);
    CHECK(strstr(a.status, "cannot contain") != NULL);

    CASE("an empty name is refused");
    press(&a, "R\025\r");
    CHECK_EQ(file_exists(dir, "goblin.vtt"), 1);
    CHECK_EQ(a.nentries, 2);

    CASE("a typed extension is not doubled up");
    press(&a, "R\025ogre.vtt\r");
    CHECK_EQ(file_exists(dir, "ogre.vtt"), 1);
    CHECK_EQ(file_exists(dir, "ogre.vtt.vtt"), 0);

    CASE("renaming to the same name is a no-op, not a self-destruct");
    press(&a, "R\r");
    CHECK_EQ(file_exists(dir, "ogre.vtt"), 1);

    /* A map too damaged to parse is exactly when you want to move it out of
     * the way, so the file rename must not depend on the load. */
    CASE("a map that will not load still renames, keeping its old title");
    char broken[1200];
    snprintf(broken, sizeof broken, "%.1000s/broken.vtt", dir);
    FILE *bf = fopen(broken, "w");
    if (bf) { fputs("VTT 2\nname keep\nsize 0 0\ngarbage\n", bf); fclose(bf); }
    press(&a, "r");
    int found = -1;
    for (int i = 0; i < a.nentries; i++)
        if (strstr(a.entries[i].name, "broken")) found = i;
    CHECK(found >= 0);
    if (found >= 0) {
        a.browser.sel = found;
        press(&a, "R\025salvaged\r");
        CHECK_EQ(file_exists(dir, "salvaged.vtt"), 1);
        CHECK_EQ(file_exists(dir, "broken.vtt"), 0);
        read_title(dir, "salvaged.vtt", title, sizeof title);
        CHECK_EQ(strcmp(title, "keep"), 0);          /* contents preserved */
        CHECK(strstr(a.status, "title unchanged") != NULL);
    }

    /* The browser used to swallow its own confirmations: the message was set
     * but never drawn, so a delete reported nothing at all. */
    CASE("the browser actually draws its status message");
    app_set_status(&a, "a distinctive message");
    rnd_begin(&r);
    app_draw(&a);
    ByteBuf frame;
    bb_init(&frame, 8192);
    rnd_dump(&r, &frame);
    bb_putc(&frame, '\0');
    CHECK(strstr(frame.data, "a distinctive message") != NULL);
    bb_free(&frame);

    app_free(&a);
    rnd_free(&r);
    sandbox_leave(&sb);

    char p2[1400];
    const char *leftovers[] = { "alpha.vtt", "ogre.vtt", "salvaged.vtt", "goblin.vtt" };
    for (size_t i = 0; i < sizeof leftovers / sizeof *leftovers; i++) {
        snprintf(p2, sizeof p2, "%.1200s/%.40s", dir, leftovers[i]);
        unlink(p2);
    }
    rmdir(dir);
}


/* ----------------------------------------------------- duplicating maps */

static int files_identical(const char *dir, const char *a, const char *b)
{
    char pa[1200], pb[1200];
    snprintf(pa, sizeof pa, "%.1000s/%.60s", dir, a);
    snprintf(pb, sizeof pb, "%.1000s/%.60s", dir, b);

    FILE *fa = fopen(pa, "rb"), *fb = fopen(pb, "rb");
    if (!fa || !fb) { if (fa) fclose(fa); if (fb) fclose(fb); return 0; }

    int same = 1, ca, cb;
    do { ca = fgetc(fa); cb = fgetc(fb); if (ca != cb) same = 0; }
    while (same && ca != EOF && cb != EOF);

    fclose(fa);
    fclose(fb);
    return same;
}

static void test_duplicate_map(void)
{
    Sandbox sb = sandbox_enter("dup");
    CHECK_EQ(sb.ok, 1);
    if (!sb.ok) return;
    const char *dir = sb.dir;

    write_map_file(dir, "goblin.vtt");
    if (chdir(dir) != 0) { CHECK(0); sandbox_leave(&sb); return; }

    Renderer r;
    App      a;
    rnd_init(&r);
    rnd_resize(&r, 80, 24);
    app_init(&a, NULL, &r);

    press(&a, "\r");
    CHECK_EQ(a.nentries, 1);

    CASE("c offers a name that is already free");
    press(&a, "c");
    CHECK_EQ(a.modal, MODAL_PROMPT);
    CHECK_EQ(strcmp(a.prompt.buf, "goblin copy"), 0);

    CASE("esc leaves nothing behind");
    press(&a, "\x1b");
    CHECK_EQ(a.nentries, 1);
    CHECK_EQ(file_exists(dir, "goblin copy.vtt"), 0);

    CASE("accepting it copies the file and titles the copy");
    press(&a, "c\r");
    CHECK_EQ(file_exists(dir, "goblin.vtt"), 1);       /* original untouched */
    CHECK_EQ(file_exists(dir, "goblin copy.vtt"), 1);
    char title[128];
    read_title(dir, "goblin.vtt", title, sizeof title);
    CHECK_EQ(strcmp(title, "x"), 0);
    read_title(dir, "goblin copy.vtt", title, sizeof title);
    CHECK_EQ(strcmp(title, "goblin copy"), 0);

    CASE("the caret moves to the copy");
    CHECK_EQ(a.nentries, 2);
    CHECK(strstr(a.entries[a.browser.sel].name, "goblin copy") != NULL);

    /* Duplicating a duplicate should count up from the original rather than
     * stacking the word. */
    CASE("a copy of a copy is offered the next number");
    press(&a, "c");
    CHECK_EQ(strcmp(a.prompt.buf, "goblin copy 2"), 0);
    press(&a, "\r");
    CHECK_EQ(file_exists(dir, "goblin copy 2.vtt"), 1);
    CHECK_EQ(file_exists(dir, "goblin copy copy.vtt"), 0);

    press(&a, "c");
    CHECK_EQ(strcmp(a.prompt.buf, "goblin copy 3"), 0);
    press(&a, "\x1b");

    CASE("duplicating onto an existing map refuses, leaving it alone");
    press(&a, "c\025goblin\r");
    CHECK_EQ(a.modal, MODAL_MESSAGE);
    CHECK(strstr(a.modal_body, "already exists") != NULL);
    read_title(dir, "goblin.vtt", title, sizeof title);
    CHECK_EQ(strcmp(title, "x"), 0);                   /* not overwritten */
    press(&a, " ");

    CASE("a copy needs a name of its own");
    press(&a, "g");                                    /* first entry */
    press(&a, "c\025goblin\r");
    CHECK(a.modal == MODAL_MESSAGE || strstr(a.status, "name of its own") != NULL);
    if (a.modal == MODAL_MESSAGE) press(&a, " ");

    CASE("a name with a slash is refused");
    press(&a, "c\025../escaped\r");
    CHECK_EQ(file_exists(dir, "escaped.vtt"), 0);
    CHECK(strstr(a.status, "cannot contain") != NULL);

    CASE("an empty name is refused");
    int before = a.nentries;
    press(&a, "c\025\r");
    CHECK_EQ(a.nentries, before);

    /* The copy is the bytes, not a re-serialisation, so a map the loader
     * would choke on still duplicates exactly. */
    CASE("a map that will not load copies byte for byte, title untouched");
    char broken[1200];
    snprintf(broken, sizeof broken, "%.1000s/broken.vtt", dir);
    FILE *bf = fopen(broken, "w");
    if (bf) { fputs("VTT 2\nname keep\nsize 0 0\ngarbage here\n", bf); fclose(bf); }
    press(&a, "r");

    int found = -1;
    for (int i = 0; i < a.nentries; i++)
        if (strstr(a.entries[i].name, "broken")) found = i;
    CHECK(found >= 0);
    if (found >= 0) {
        a.browser.sel = found;
        press(&a, "c\025salvage\r");
        CHECK_EQ(file_exists(dir, "salvage.vtt"), 1);
        CHECK_EQ(files_identical(dir, "broken.vtt", "salvage.vtt"), 1);
        read_title(dir, "salvage.vtt", title, sizeof title);
        CHECK_EQ(strcmp(title, "keep"), 0);
        CHECK(strstr(a.status, "title unchanged") != NULL);
    }

    CASE("an empty list has nothing to duplicate");
    while (a.nentries > 0) press(&a, "dy");
    press(&a, "c");
    CHECK_EQ(a.modal, MODAL_NONE);
    CHECK(strstr(a.status, "nothing") != NULL);

    app_free(&a);
    rnd_free(&r);
    sandbox_leave(&sb);
    rmdir(dir);
}


/* ------------------------------------------------------- status markers */

static void test_status(void)
{
    Token t;
    memset(&t, 0, sizeof t);
    t.size = 1;
    str_lcpy(t.label, "Goblin", sizeof t.label);

    CASE("a token starts unmarked");
    CHECK_EQ(t.nstatus, 0);

    CASE("markers accumulate up to the cap, then refuse");
    for (int i = 0; i < TOKEN_STATUS_MAX; i++)
        CHECK_EQ(token_add_status(&t, (uint8_t)i, "Poisoned"), 1);
    CHECK_EQ(t.nstatus, TOKEN_STATUS_MAX);
    CHECK_EQ(token_add_status(&t, 0, "Marked"), 0);
    CHECK_EQ(t.nstatus, TOKEN_STATUS_MAX);

    CASE("clearing removes all of them");
    token_clear_status(&t);
    CHECK_EQ(t.nstatus, 0);
    CHECK_EQ(token_add_status(&t, 0, "Poisoned"), 1);

    /* The map shows an initial rather than a dot, so a glance says which
     * condition it is and not merely that there is one. */
    CASE("a marker draws as the first letter of its word");
    CHECK_EQ(status_glyph(&t.status[0]), 'P');
    token_clear_status(&t);
    token_add_status(&t, 0, "burning");
    CHECK_EQ(status_glyph(&t.status[0]), 'B');       /* upper-cased */
    token_clear_status(&t);
    token_add_status(&t, 0, "");
    CHECK_EQ(status_glyph(&t.status[0]), 0x25CFu);   /* a dot, with no word */

    CASE("a colour out of range wraps rather than reading past the palette");
    token_clear_status(&t);
    token_add_status(&t, 200, "X");
    CHECK(t.status[0].color < STATUS_COLOR_COUNT);

    CASE("colour names round-trip");
    for (int i = 0; i < STATUS_COLOR_COUNT; i++)
        CHECK_EQ(status_color_from_name(status_color_name((uint8_t)i)), i);
    CHECK_EQ(status_color_from_name("chartreuse"), -1);

    CASE("a long word is truncated, not overrun");
    token_clear_status(&t);
    token_add_status(&t, 0, "an extremely long condition name indeed");
    CHECK(strlen(t.status[0].label) < STATUS_LABEL_MAX);

    /* A condition ends on its own schedule, so the one that ended has to be
     * the one that goes -- and the rest have to keep the order they are drawn
     * and numbered in, or the next question would answer about the wrong one. */
    CASE("one marker can be taken off, leaving the rest in order");
    token_clear_status(&t);
    token_add_status(&t, 0, "Poisoned");
    token_add_status(&t, 1, "Marked");
    token_add_status(&t, 2, "Burning");
    token_remove_status(&t, 1);
    CHECK_EQ(t.nstatus, 2);
    CHECK_EQ(strcmp(t.status[0].label, "Poisoned"), 0);
    CHECK_EQ(strcmp(t.status[1].label, "Burning"), 0);
    CHECK_EQ(t.status[1].color, 2);

    CASE("the vacated slot is wiped, not left holding the old word");
    CHECK_EQ(t.status[2].label[0], '\0');

    CASE("removing the first and the last both work");
    token_remove_status(&t, 1);
    CHECK_EQ(t.nstatus, 1);
    CHECK_EQ(strcmp(t.status[0].label, "Poisoned"), 0);
    token_remove_status(&t, 0);
    CHECK_EQ(t.nstatus, 0);

    CASE("an index nobody holds is a no-op, not a corruption");
    token_add_status(&t, 0, "Poisoned");
    token_remove_status(&t, -1);
    token_remove_status(&t, 1);
    token_remove_status(&t, TOKEN_STATUS_MAX + 5);
    CHECK_EQ(t.nstatus, 1);
    CHECK_EQ(strcmp(t.status[0].label, "Poisoned"), 0);
}

static void test_unique_label(void)
{
    TokenList l;
    memset(&l, 0, sizeof l);
    char out[TOKEN_LABEL_MAX];

    CASE("an unused label is left alone");
    tokens_unique_label(&l, "Goblin", out, sizeof out);
    CHECK_EQ(strcmp(out, "Goblin"), 0);

    Token g;
    memset(&g, 0, sizeof g);
    g.size = 1;
    str_lcpy(g.label, "Goblin", sizeof g.label);
    tokens_add(&l, g);

    CASE("a taken one gets the next number");
    tokens_unique_label(&l, "Goblin", out, sizeof out);
    CHECK_EQ(strcmp(out, "Goblin 2"), 0);

    str_lcpy(g.label, "Goblin 2", sizeof g.label);
    tokens_add(&l, g);
    tokens_unique_label(&l, "Goblin", out, sizeof out);
    CHECK_EQ(strcmp(out, "Goblin 3"), 0);

    /* Copying a copy should continue the run rather than stack numbers. */
    CASE("a numbered label continues the run");
    tokens_unique_label(&l, "Goblin 2", out, sizeof out);
    CHECK_EQ(strcmp(out, "Goblin 3"), 0);
    CHECK(strstr(out, "2 2") == NULL);

    CASE("an unlabelled token stays unlabelled");
    tokens_unique_label(&l, "", out, sizeof out);
    CHECK_EQ(out[0], '\0');

    tokens_free(&l);
}

static void test_status_io(void)
{
    char path[] = "/tmp/vtt-status-XXXXXX";
    int  fd = mkstemp(path);
    if (fd >= 0) close(fd);

    Map *m = map_new(8, 8, "marked");
    map_fill_tiles(m, 0, 0, 7, 7, TILE_FLOOR);

    Token a = { 1, 1, 1, TOKEN_PLAYER, "Aria", { { 0, "" } }, 0 };
    Token b = { 4, 4, 2, TOKEN_ENEMY, "Ogre Chief", { { 0, "" } }, 0 };
    token_add_status(&a, 0, "Poisoned");
    token_add_status(&a, 3, "Blessed by Fate");
    token_add_status(&b, 6, "Marked");
    tokens_add(&m->tokens, a);
    tokens_add(&m->tokens, b);

    char err[MAPIO_ERR_MAX] = { 0 };
    CASE("markers travel with the map");
    CHECK_EQ(mapio_save(m, path, err, sizeof err), 0);

    Map *l = mapio_load(path, err, sizeof err);
    CHECK(l != NULL);
    if (l) {
        CHECK_EQ(l->tokens.n, 2);
        CHECK_EQ(l->tokens.v[0].nstatus, 2);
        CHECK_EQ(l->tokens.v[1].nstatus, 1);
        CHECK_EQ(strcmp(l->tokens.v[0].status[0].label, "Poisoned"), 0);
        CHECK_EQ(l->tokens.v[0].status[0].color, 0);
        CHECK_EQ(strcmp(l->tokens.v[0].status[1].label, "Blessed by Fate"), 0);
        CHECK_EQ(l->tokens.v[0].status[1].color, 3);
        CHECK_EQ(strcmp(l->tokens.v[1].status[0].label, "Marked"), 0);
        CHECK_EQ(l->tokens.v[1].status[0].color, 6);
        map_free(l);
    }

    /* A marker line must attach to the token above it and nothing else. */
    CASE("a stray marker line with no token before it is ignored");
    FILE *f = fopen(path, "w");
    if (f) {
        fputs("VTT 3\nname Stray\nsize 3 3\ntiles\n...\n...\n...\n"
              "tokenstatus red \"Orphan\"\n"
              "token enemy 1 1 1 \"Real\"\n"
              "tokenstatus blue \"Mine\"\n", f);
        fclose(f);
    }
    Map *stray = mapio_load(path, err, sizeof err);
    CHECK(stray != NULL);
    if (stray) {
        CHECK_EQ(stray->tokens.n, 1);
        CHECK_EQ(stray->tokens.v[0].nstatus, 1);
        CHECK_EQ(strcmp(stray->tokens.v[0].status[0].label, "Mine"), 0);
        map_free(stray);
    }

    CASE("an unknown colour name drops the marker rather than the map");
    f = fopen(path, "w");
    if (f) {
        fputs("VTT 3\nname Odd\nsize 3 3\ntiles\n...\n...\n...\n"
              "token enemy 1 1 1 \"Real\"\ntokenstatus chartreuse \"Nope\"\n", f);
        fclose(f);
    }
    Map *odd = mapio_load(path, err, sizeof err);
    CHECK(odd != NULL);
    if (odd) {
        CHECK_EQ(odd->tokens.n, 1);
        CHECK_EQ(odd->tokens.v[0].nstatus, 0);
        map_free(odd);
    }

    map_free(m);
    unlink(path);
}

static void test_token_edit_undo(void)
{
    Map *m = map_new(8, 8, "edit");
    map_fill_tiles(m, 0, 0, 7, 7, TILE_FLOOR);

    Undo u;
    undo_init(&u);

    Token g = { 2, 2, 1, TOKEN_ENEMY, "Goblin", { { 0, "" } }, 0 };
    undo_begin(&u);
    int idx = undo_add_token(&u, m, g);
    undo_end(&u);

    /* Marking, relabelling and resizing all edit a token in place, and all
     * three should be one u away. */
    CASE("adding a marker undoes");
    Token t = m->tokens.v[idx];
    token_add_status(&t, 0, "Poisoned");
    undo_begin(&u);
    undo_edit_token(&u, m, idx, t);
    undo_end(&u);
    CHECK_EQ(m->tokens.v[idx].nstatus, 1);
    CHECK_EQ(undo_undo(&u, m), 1);
    CHECK_EQ(m->tokens.v[idx].nstatus, 0);
    CHECK_EQ(undo_redo(&u, m), 1);
    CHECK_EQ(m->tokens.v[idx].nstatus, 1);

    CASE("clearing markers undoes, restoring every one");
    t = m->tokens.v[idx];
    token_add_status(&t, 2, "Marked");
    undo_begin(&u); undo_edit_token(&u, m, idx, t); undo_end(&u);
    CHECK_EQ(m->tokens.v[idx].nstatus, 2);

    t = m->tokens.v[idx];
    token_clear_status(&t);
    undo_begin(&u); undo_edit_token(&u, m, idx, t); undo_end(&u);
    CHECK_EQ(m->tokens.v[idx].nstatus, 0);
    CHECK_EQ(undo_undo(&u, m), 1);
    CHECK_EQ(m->tokens.v[idx].nstatus, 2);
    CHECK_EQ(strcmp(m->tokens.v[idx].status[1].label, "Marked"), 0);

    CASE("relabelling undoes");
    t = m->tokens.v[idx];
    str_lcpy(t.label, "Hobgoblin", sizeof t.label);
    undo_begin(&u); undo_edit_token(&u, m, idx, t); undo_end(&u);
    CHECK_EQ(strcmp(m->tokens.v[idx].label, "Hobgoblin"), 0);
    CHECK_EQ(undo_undo(&u, m), 1);
    CHECK_EQ(strcmp(m->tokens.v[idx].label, "Goblin"), 0);

    CASE("resizing undoes");
    t = m->tokens.v[idx];
    t.size = 3;
    undo_begin(&u); undo_edit_token(&u, m, idx, t); undo_end(&u);
    CHECK_EQ(m->tokens.v[idx].size, 3);
    CHECK_EQ(undo_undo(&u, m), 1);
    CHECK_EQ(m->tokens.v[idx].size, 1);

    /* A keystroke that turns out to change nothing should cost neither an
     * undo step nor the redo tail waiting behind it. */
    CASE("an edit that changes nothing costs no undo step, and keeps redo");
    CHECK_EQ(undo_can_redo(&u), 1);
    int before = u.nmarks;
    undo_begin(&u);
    undo_edit_token(&u, m, idx, m->tokens.v[idx]);
    undo_end(&u);
    CHECK_EQ(u.nmarks, before);
    CHECK_EQ(undo_can_redo(&u), 1);
    CHECK_EQ(undo_redo(&u, m), 1);
    CHECK_EQ(m->tokens.v[idx].size, 3);

    undo_free(&u);
    map_free(m);
}

/* Driving the whole app rather than the model: the point of the chooser is
 * the keystrokes, and a test that called clear_token_status directly would
 * not notice if `c` never reached it. */
static void test_clear_status_keys(void)
{
    Sandbox sb = sandbox_enter("clr");
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
    CHECK_EQ(a.screen, SCREEN_PLAY);

    Token g = { 0, 0, 1, TOKEN_ENEMY, "Goblin", { { 0, "" } }, 0 };
    token_add_status(&g, 0, "Poisoned");
    token_add_status(&g, 3, "Marked");
    token_add_status(&g, 5, "Burning");
    int idx = tokens_add(&a.map->tokens, g);
    a.ed.cx = 0; a.ed.cy = 0;

    CASE("s d on a token wearing several markers asks which one");
    press(&a, "sd");
    CHECK_EQ(a.modal, MODAL_CLEAR_STATUS);
    CHECK_EQ(a.map->tokens.v[idx].nstatus, 3);

    /* The map only ever shows initials, and two conditions can share one, so
     * the question has to spell the words out. */
    CASE("the chooser names every marker in full");
    rnd_begin(&r);
    app_draw(&a);
    ByteBuf frame;
    bb_init(&frame, 16384);
    rnd_dump(&r, &frame);
    bb_putc(&frame, '\0');
    CHECK(strstr(frame.data, "Poisoned") != NULL);
    CHECK(strstr(frame.data, "Marked") != NULL);
    CHECK(strstr(frame.data, "Burning") != NULL);
    CHECK(strstr(frame.data, "Goblin") != NULL);
    CHECK(strstr(frame.data, "1-3") != NULL);
    bb_free(&frame);

    CASE("esc leaves every marker where it was");
    press(&a, "\x1b");
    CHECK_EQ(a.modal, MODAL_NONE);
    CHECK_EQ(a.map->tokens.v[idx].nstatus, 3);

    CASE("a number takes off that marker and only that one");
    press(&a, "sd2");
    CHECK_EQ(a.modal, MODAL_NONE);
    CHECK_EQ(a.map->tokens.v[idx].nstatus, 2);
    CHECK_EQ(strcmp(a.map->tokens.v[idx].status[0].label, "Poisoned"), 0);
    CHECK_EQ(strcmp(a.map->tokens.v[idx].status[1].label, "Burning"), 0);
    CHECK(strstr(a.status, "Marked") != NULL);

    CASE("clearing one marker undoes");
    press(&a, "u");
    CHECK_EQ(a.map->tokens.v[idx].nstatus, 3);
    CHECK_EQ(strcmp(a.map->tokens.v[idx].status[1].label, "Marked"), 0);

    CASE("a number past the last row is ignored, and the question stays up");
    press(&a, "sd4");
    CHECK_EQ(a.modal, MODAL_CLEAR_STATUS);
    CHECK_EQ(a.map->tokens.v[idx].nstatus, 3);

    CASE("a clears them all at once");
    press(&a, "a");
    CHECK_EQ(a.modal, MODAL_NONE);
    CHECK_EQ(a.map->tokens.v[idx].nstatus, 0);
    CHECK(strstr(a.status, "3 markers") != NULL);
    press(&a, "u");
    CHECK_EQ(a.map->tokens.v[idx].nstatus, 3);

    /* A chooser with one row is a keystroke that asks nothing. */
    CASE("a single marker clears without a question");
    press(&a, "sda");
    press(&a, "sa");
    CHECK_EQ(a.modal, MODAL_PROMPT);
    press(&a, "Stunned\r");
    CHECK_EQ(a.map->tokens.v[idx].nstatus, 1);
    press(&a, "sd");
    CHECK_EQ(a.modal, MODAL_NONE);
    CHECK_EQ(a.map->tokens.v[idx].nstatus, 0);
    CHECK(strstr(a.status, "Stunned") != NULL);

    CASE("s d on a bare token says so rather than opening an empty question");
    press(&a, "sd");
    CHECK_EQ(a.modal, MODAL_NONE);
    CHECK(strstr(a.status, "no markers") != NULL);

    CASE("s d away from any token says so");
    a.play.sel = -1;
    a.ed.cx = 1; a.ed.cy = 1;
    press(&a, "sd");
    CHECK_EQ(a.modal, MODAL_NONE);
    CHECK(strstr(a.status, "no token here") != NULL);

    app_free(&a);
    rnd_free(&r);
    unlink(path);
    sandbox_leave(&sb);
}

/* --------------------------------------------------------- movement trail */

static void test_trail(void)
{
    Map *m = map_new(12, 10, "trail");
    map_fill_tiles(m, 0, 0, 11, 9, TILE_FLOOR);

    Undo u;
    undo_init(&u);
    Play p;
    play_init(&p);

    Token g = { 2, 2, 1, TOKEN_ENEMY, "Goblin", { { 0, "" } }, 0 };
    int idx = undo_add_token(&u, m, g);
    p.sel = idx;

    CASE("picking a token up marks the tile it stood on");
    play_grab(&p, m);
    CHECK_EQ(p.grabbed, 1);
    CHECK_EQ(p.ntrail, 1);
    CHECK_EQ(p.trail[0].x, 2);
    CHECK_EQ(p.trail[0].y, 2);
    CHECK_EQ(p.origin_x, 2);
    CHECK_EQ(p.origin_y, 2);
    CHECK_EQ(p.steps, 0);

    /* Walk east then back west: the route from where it set out is one square,
     * however much the cursor wandered getting there. */
    CASE("the ribbon is the route from the origin, not the squares walked");
    undo_begin(&u); play_step(m, &u, &p, 1, 0); undo_end(&u);
    undo_begin(&u); play_step(m, &u, &p, 1, 0); undo_end(&u);
    undo_begin(&u); play_step(m, &u, &p, -1, 0); undo_end(&u);
    CHECK_EQ(m->tokens.v[idx].x, 3);
    CHECK_EQ(p.ntrail, 2);
    CHECK_EQ(p.trail[0].x, 2);
    CHECK_EQ(p.trail[1].x, 3);

    CASE("the step count is what the route costs, not the keys pressed");
    CHECK_EQ(p.steps, 1);

    /* Across open floor a great many routes are equally short. The one drawn
     * should hug the straight line rather than turning a single corner. */
    CASE("an open diagonal comes out as a staircase, not an L");
    play_focus(&p, idx);
    m->tokens.v[idx].x = 2;
    m->tokens.v[idx].y = 2;
    play_grab(&p, m);
    for (int i = 0; i < 3; i++) {
        undo_begin(&u); play_step(m, &u, &p, 1, 0); undo_end(&u);
        undo_begin(&u); play_step(m, &u, &p, 0, 1); undo_end(&u);
    }
    CHECK_EQ(p.ntrail, 7);
    CHECK_EQ(p.steps, 6);
    int corners = 0;
    for (int i = 1; i + 1 < p.ntrail; i++) {
        int ax = p.trail[i].x - p.trail[i - 1].x, ay = p.trail[i].y - p.trail[i - 1].y;
        int bx = p.trail[i + 1].x - p.trail[i].x, by = p.trail[i + 1].y - p.trail[i].y;
        if (ax != bx || ay != by) corners++;
    }
    CHECK(corners > 1);                     /* an L would turn exactly once */

    CASE("every tile on the route is a step from the one before it");
    for (int i = 1; i < p.ntrail; i++) {
        int d = abs(p.trail[i].x - p.trail[i - 1].x) +
                abs(p.trail[i].y - p.trail[i - 1].y);
        CHECK_EQ(d, 1);
    }

    /* A route has to be one the creature could actually walk, so a wall in
     * the way lengthens it rather than being cut through. */
    CASE("a wall in the way makes the route go round it");
    Map *w = map_new(9, 9, "wall");
    map_fill_tiles(w, 0, 0, 8, 8, TILE_FLOOR);
    for (int y = 0; y <= 3; y++) map_set_vedge(w, 4, y, EDGE_WALL);

    Undo wu;
    undo_init(&wu);
    Play wp;
    play_init(&wp);
    Token t2 = { 3, 0, 1, TOKEN_ENEMY, "W", { { 0, "" } }, 0 };
    int wi = undo_add_token(&wu, w, t2);
    wp.sel = wi;
    play_grab(&wp, w);

    /* Down the near side, round the end of the wall, back up the far side. */
    for (int i = 0; i < 4; i++) { undo_begin(&wu); play_step(w, &wu, &wp, 0, 1); undo_end(&wu); }
    undo_begin(&wu); play_step(w, &wu, &wp, 1, 0); undo_end(&wu);
    CHECK_EQ(w->tokens.v[wi].x, 4);
    CHECK_EQ(w->tokens.v[wi].y, 4);

    CHECK_EQ(wp.ntrail, 6);                 /* five steps: straight is only two */
    CHECK_EQ(wp.steps, 5);
    for (int i = 0; i < wp.ntrail; i++)
        CHECK(!(wp.trail[i].x == 4 && wp.trail[i].y <= 3));   /* never through it */

    CASE("no route at all leaves no ribbon and the keystrokes standing");
    map_fill_tiles(w, 0, 0, 8, 8, TILE_VOID);
    map_set_tile(w, 0, 0, TILE_FLOOR);
    map_set_tile(w, 8, 8, TILE_FLOOR);
    wp.origin_x = 0; wp.origin_y = 0;
    w->tokens.v[wi].x = 8; w->tokens.v[wi].y = 8;
    wp.steps = 7;
    play_trail_sync(&wp, w);
    CHECK_EQ(wp.ntrail, 0);
    CHECK_EQ(wp.steps, 7);

    undo_free(&wu);
    map_free(w);

    /* Undo walks the token back the way it came, so the route shortens with
     * it and the cost comes down: a step that has been undone was not spent. */
    CASE("undo shortens the route and gives the cost back");
    CHECK_EQ(undo_undo(&u, m), 1);
    play_trail_sync(&p, m);
    CHECK_EQ(p.ntrail, 6);
    CHECK_EQ(p.steps, 5);

    CASE("redo lengthens it again");
    CHECK_EQ(undo_redo(&u, m), 1);
    play_trail_sync(&p, m);
    CHECK_EQ(p.ntrail, 7);
    CHECK_EQ(p.steps, 6);

    CASE("undoing back to the start leaves just the origin");
    for (int i = 0; i < 6; i++) { CHECK_EQ(undo_undo(&u, m), 1); play_trail_sync(&p, m); }
    CHECK_EQ(p.ntrail, 1);
    CHECK_EQ(p.steps, 0);
    CHECK_EQ(p.trail[0].x, 2);
    CHECK_EQ(p.trail[0].y, 2);

    CASE("sync does nothing at all when no token is held");
    p.grabbed = 0;
    play_trail_sync(&p, m);
    CHECK_EQ(p.ntrail, 0);

    undo_free(&u);
    map_free(m);

    /* The biggest map the format allows has more tiles than 16 bits can
     * count, so a route across it has to be measured in something wider. */
    CASE("a route across the largest allowed map is measured, not wrapped");
    Map *big = map_new(MAP_MAX_DIM, 4, "big");
    map_fill_tiles(big, 0, 0, MAP_MAX_DIM - 1, 3, TILE_FLOOR);

    Undo bu;
    undo_init(&bu);
    Play bp;
    play_init(&bp);
    Token bt = { 0, 0, 1, TOKEN_ENEMY, "B", { { 0, "" } }, 0 };
    bp.sel = undo_add_token(&bu, big, bt);
    play_grab(&bp, big);
    big->tokens.v[bp.sel].x = (int16_t)(MAP_MAX_DIM - 1);
    play_trail_sync(&bp, big);
    CHECK_EQ(bp.steps, MAP_MAX_DIM - 1);
    CHECK_EQ(bp.ntrail, PLAY_TRAIL_MAX);       /* the ribbon stops at its cap */

    undo_free(&bu);
    map_free(big);
}


static void test_trail_draw(void)
{
    Map *m = map_new(12, 10, "trail");
    map_fill_tiles(m, 0, 0, 11, 9, TILE_FLOOR);

    Undo u;
    undo_init(&u);
    Play p;
    play_init(&p);

    Renderer r;
    rnd_init(&r);
    rnd_resize(&r, 80, 24);

    GridView g;
    memset(&g, 0, sizeof g);
    g.zoom = 1;
    g.view = rect(0, 0, 80, 24);

    Token t = { 2, 2, 1, TOKEN_ENEMY, "G", { { 0, "" } }, 0 };
    int idx = undo_add_token(&u, m, t);
    p.sel = idx;

    CASE("nothing is drawn while no token is held");
    rnd_begin(&r);
    play_trail_draw(&r, m, &g, &p, &THEME_DARK, 0);
    int sx, sy;
    grid_tile_interior(&g, 2, 2, &sx, &sy);
    CHECK(rnd_at(&r, sx, sy)->bg != THEME_DARK.trail_bg);

    play_grab(&p, m);
    undo_begin(&u); play_step(m, &u, &p, 1, 0); undo_end(&u);
    undo_begin(&u); play_step(m, &u, &p, 0, 1); undo_end(&u);

    CASE("every tile walked over is tinted");
    rnd_begin(&r);
    play_trail_draw(&r, m, &g, &p, &THEME_DARK, 0);
    const int walked[3][2] = { { 2, 2 }, { 3, 2 }, { 3, 3 } };
    for (int i = 0; i < 3; i++) {
        grid_tile_interior(&g, walked[i][0], walked[i][1], &sx, &sy);
        CHECK_EQ(rnd_at(&r, sx, sy)->bg, THEME_DARK.trail_bg);
    }

    /* The corner it did not cut: the ribbon follows the route, so the tile
     * on the diagonal stays untouched. */
    CASE("a tile beside the route is left alone");
    grid_tile_interior(&g, 2, 3, &sx, &sy);
    CHECK(rnd_at(&r, sx, sy)->bg != THEME_DARK.trail_bg);

    /* The whole point of drawing the route rather than the walk: fumbling the
     * cursor out and back should leave nothing behind. */
    CASE("squares only wandered over are not tinted");
    undo_begin(&u); play_step(m, &u, &p, 1, 0); undo_end(&u);   /* out to x=4 */
    undo_begin(&u); play_step(m, &u, &p, 1, 0); undo_end(&u);   /* and x=5 */
    undo_begin(&u); play_step(m, &u, &p, -1, 0); undo_end(&u);  /* back to x=4 */
    undo_begin(&u); play_step(m, &u, &p, -1, 0); undo_end(&u);  /* back to x=3 */
    CHECK_EQ(m->tokens.v[idx].x, 3);
    rnd_begin(&r);
    play_trail_draw(&r, m, &g, &p, &THEME_DARK, 0);
    grid_tile_interior(&g, 5, 3, &sx, &sy);
    CHECK(rnd_at(&r, sx, sy)->bg != THEME_DARK.trail_bg);
    grid_tile_interior(&g, 4, 3, &sx, &sy);
    CHECK(rnd_at(&r, sx, sy)->bg != THEME_DARK.trail_bg);
    grid_tile_interior(&g, 3, 3, &sx, &sy);
    CHECK_EQ(rnd_at(&r, sx, sy)->bg, THEME_DARK.trail_bg);

    CASE("the tile it set out from carries a mark of its own");
    Rect a;
    grid_token_area(&g, 2, 2, 1, &a);
    CHECK_EQ(rnd_at(&r, a.x, a.y)->ch, 0x25C6u);
    CHECK_EQ(rnd_at(&r, a.x, a.y)->fg, THEME_DARK.trail);

    CASE("ascii mode marks it with a letter instead");
    rnd_begin(&r);
    play_trail_draw(&r, m, &g, &p, &THEME_DARK, 1);
    CHECK_EQ(rnd_at(&r, a.x, a.y)->ch, (uint32_t)'X');

    /* A big creature covers ground, not a thread along its top-left corner. */
    CASE("a 2x2 token tints its whole footprint at every step");
    m->tokens.v[idx].size = 2;
    m->tokens.v[idx].x = 5;
    m->tokens.v[idx].y = 5;
    play_grab(&p, m);
    undo_begin(&u); play_step(m, &u, &p, 1, 0); undo_end(&u);
    rnd_begin(&r);
    play_trail_draw(&r, m, &g, &p, &THEME_DARK, 0);
    const int covered[6][2] = {
        { 5, 5 }, { 6, 5 }, { 5, 6 }, { 6, 6 }, { 7, 5 }, { 7, 6 },
    };
    for (int i = 0; i < 6; i++) {
        grid_tile_interior(&g, covered[i][0], covered[i][1], &sx, &sy);
        CHECK_EQ(rnd_at(&r, sx, sy)->bg, THEME_DARK.trail_bg);
    }

    /* A long walk should cost the size of the window, not the size of the
     * walk: tiles off screen are never drawn. */
    CASE("tiles scrolled out of view are skipped");
    g.view = rect(0, 0, 20, 10);
    rnd_begin(&r);
    play_trail_draw(&r, m, &g, &p, &THEME_DARK, 0);
    CHECK(1);

    rnd_free(&r);
    undo_free(&u);
    map_free(m);
}

/* ------------------------------------------------------------------ shapes */

static void test_shapes(void)
{
    CASE("a rectangle between two tiles includes both ends");
    EdShape b = ed_shape(ED_SHAPE_RECT, 2, 3, 5, 4, 0);
    CHECK_EQ(b.x0, 2); CHECK_EQ(b.x1, 5);
    CHECK_EQ(b.y0, 3); CHECK_EQ(b.y1, 4);
    CHECK_EQ(ed_shape_has(&b, 2, 3), 1);
    CHECK_EQ(ed_shape_has(&b, 5, 4), 1);
    CHECK_EQ(ed_shape_has(&b, 6, 4), 0);

    CASE("a reversed rectangle is the same rectangle");
    EdShape rev = ed_shape(ED_SHAPE_RECT, 5, 4, 2, 3, 0);
    CHECK_EQ(rev.x0, b.x0); CHECK_EQ(rev.x1, b.x1);
    CHECK_EQ(rev.y0, b.y0); CHECK_EQ(rev.y1, b.y1);

    /* Between two corners it spans what they enclose, which is one fewer
     * tile than a box drawn between two squares. */
    CASE("a rectangle between two corners spans the tiles they enclose");
    EdShape c = ed_shape(ED_SHAPE_RECT, 2, 2, 5, 5, 1);
    CHECK_EQ(c.x0, 2); CHECK_EQ(c.x1, 4);
    CHECK_EQ(c.y0, 2); CHECK_EQ(c.y1, 4);

    CASE("a circle holds its centre and reaches its cursor");
    EdShape d = ed_shape(ED_SHAPE_CIRCLE, 10, 10, 14, 10, 0);
    CHECK_EQ(ed_shape_has(&d, 10, 10), 1);
    CHECK_EQ(ed_shape_has(&d, 14, 10), 1);      /* the tile that set the radius */
    CHECK_EQ(ed_shape_has(&d, 15, 10), 0);
    CHECK_EQ(ed_shape_radius(&d), 4);

    CASE("a circle is round, not the box around it");
    CHECK_EQ(ed_shape_has(&d, 13, 13), 0);      /* the corner of the box */
    CHECK_EQ(ed_shape_has(&d, 12, 12), 1);      /* inside the arc */

    CASE("a circle is symmetric about its centre");
    for (int dy = -5; dy <= 5; dy++)
        for (int dx = -5; dx <= 5; dx++) {
            int in = ed_shape_has(&d, 10 + dx, 10 + dy);
            CHECK_EQ(ed_shape_has(&d, 10 - dx, 10 + dy), in);
            CHECK_EQ(ed_shape_has(&d, 10 + dx, 10 - dy), in);
        }

    CASE("a circle of no radius is the one tile");
    EdShape dot = ed_shape(ED_SHAPE_CIRCLE, 4, 4, 4, 4, 0);
    CHECK_EQ(ed_shape_has(&dot, 4, 4), 1);
    CHECK_EQ(ed_shape_has(&dot, 5, 4), 0);
    CHECK_EQ(ed_shape_radius(&dot), 0);

    /* Wall mode anchors on a lattice corner, so its circles sit between
     * squares and come out even across rather than odd. */
    CASE("a circle anchored on a corner is centred on the corner");
    EdShape w = ed_shape(ED_SHAPE_CIRCLE, 5, 5, 8, 5, 1);
    CHECK_EQ(ed_shape_has(&w, 4, 4), 1);        /* the four tiles round it */
    CHECK_EQ(ed_shape_has(&w, 5, 4), 1);
    CHECK_EQ(ed_shape_has(&w, 4, 5), 1);
    CHECK_EQ(ed_shape_has(&w, 5, 5), 1);
    CHECK_EQ(ed_shape_has(&w, 4, 4), ed_shape_has(&w, 5, 5));

    CASE("a circle reaching off the map is clipped, not clamped");
    EdShape edge = ed_shape(ED_SHAPE_CIRCLE, 1, 1, 6, 1, 0);
    CHECK_EQ(ed_shape_has(&edge, -3, 1), 1);    /* the shape itself is unbounded */
    CHECK(edge.x0 < 0);                          /* the map bounds it on use */
}

static void test_circle_fill(void)
{
    Map *m = map_new(20, 20, "circle");
    Undo u;
    undo_init(&u);

    Editor e;
    ed_init(&e, m);
    ed_layout(&e, m, 80, 24);

    CASE("a visual circle fills a disc, not its bounding box");
    e.mode = ED_VISUAL;
    e.shape = ED_SHAPE_CIRCLE;
    e.anchor_x = 10; e.anchor_y = 10;
    e.cx = 14; e.cy = 10;
    ed_apply_tiles(&e, m, &u, TILE_FLOOR);
    CHECK_EQ(map_tile(m, 10, 10), TILE_FLOOR);
    CHECK_EQ(map_tile(m, 14, 10), TILE_FLOOR);
    CHECK_EQ(map_tile(m, 13, 13), TILE_VOID);       /* the box corner */
    CHECK_EQ(map_tile(m, 15, 10), TILE_VOID);

    CASE("it undoes as one step");
    CHECK_EQ(undo_undo(&u, m), 1);
    CHECK_EQ(map_tile(m, 10, 10), TILE_VOID);
    CHECK_EQ(undo_redo(&u, m), 1);

    /* A circle reaching past the edge should paint what fits rather than
     * refusing or wrapping. */
    CASE("a circle overhanging the map paints only what is on it");
    e.anchor_x = 1; e.anchor_y = 1;
    e.cx = 5; e.cy = 1;
    ed_apply_tiles(&e, m, &u, TILE_WATER);
    CHECK_EQ(map_tile(m, 1, 1), TILE_WATER);
    CHECK_EQ(map_tile(m, 0, 0), TILE_WATER);
    CHECK_EQ(map_tile(m, 5, 1), TILE_WATER);

    CASE("a box selection still fills its box");
    e.shape = ED_SHAPE_RECT;
    e.anchor_x = 15; e.anchor_y = 15;
    e.cx = 17; e.cy = 17;
    ed_apply_tiles(&e, m, &u, TILE_ROUGH);
    for (int y = 15; y <= 17; y++)
        for (int x = 15; x <= 17; x++)
            CHECK_EQ(map_tile(m, x, y), TILE_ROUGH);

    undo_free(&u);
    map_free(m);
}

/* The point of a ring of wall is that it encloses. Flooding out from the
 * middle and finding no way past it is the only test that says so. */
static int flood_escapes(const Map *m, int sx, int sy, const EdShape *s)
{
    int  n    = m->w * m->h;
    char *seen = xcalloc((size_t)n, 1);
    int  *q    = xmalloc((size_t)n * sizeof *q);
    int   head = 0, tail = 0, escaped = 0;

    seen[sy * m->w + sx] = 1;
    q[tail++] = sy * m->w + sx;

    static const int DX[4] = { 1, -1, 0, 0 };
    static const int DY[4] = { 0, 0, 1, -1 };

    while (head < tail) {
        int cur = q[head++];
        int cx = cur % m->w, cy = cur / m->w;
        if (!ed_shape_has(s, cx, cy)) { escaped = 1; break; }

        for (int d = 0; d < 4; d++) {
            int nx = cx + DX[d], ny = cy + DY[d];
            if (!map_in_bounds(m, nx, ny)) continue;
            if (seen[ny * m->w + nx]) continue;
            if (map_blocked(m, cx, cy, DX[d], DY[d])) continue;
            seen[ny * m->w + nx] = 1;
            q[tail++] = ny * m->w + nx;
        }
    }

    free(seen);
    free(q);
    return escaped;
}

static void test_circle_walls(void)
{
    Map *m = map_new(24, 24, "ring");
    map_fill_tiles(m, 0, 0, 23, 23, TILE_FLOOR);

    Undo u;
    undo_init(&u);

    CASE("a circle of wall closes all the way round");
    EdShape s = ed_shape(ED_SHAPE_CIRCLE, 12, 12, 18, 12, 1);
    ed_wall_shape(m, &u, &s, EDGE_WALL);
    CHECK_EQ(flood_escapes(m, 11, 11, &s), 0);

    CASE("it walls the boundary and nothing inside it");
    CHECK_EQ(map_blocked(m, 11, 11, 1, 0), 0);      /* the middle is open */
    CHECK_EQ(map_blocked(m, 11, 11, 0, 1), 0);

    CASE("a radius of one is still a closed ring");
    Map *tiny = map_new(9, 9, "tiny");
    map_fill_tiles(tiny, 0, 0, 8, 8, TILE_FLOOR);
    Undo tu;
    undo_init(&tu);
    EdShape one = ed_shape(ED_SHAPE_CIRCLE, 4, 4, 5, 4, 1);
    ed_wall_shape(tiny, &tu, &one, EDGE_WALL);
    CHECK_EQ(flood_escapes(tiny, 3, 3, &one), 0);
    undo_free(&tu);
    map_free(tiny);

    CASE("the whole ring undoes as one step");
    CHECK_EQ(undo_undo(&u, m), 1);
    CHECK_EQ(flood_escapes(m, 11, 11, &s), 1);      /* open again */

    /* Half a circle drawn off the corner of the map: the arc that fits gets
     * laid and the rest is dropped, rather than writing past the edge. */
    CASE("a circle overhanging the map lays the arc that fits");
    Map *corner = map_new(10, 10, "corner");
    map_fill_tiles(corner, 0, 0, 9, 9, TILE_FLOOR);
    Undo cu;
    undo_init(&cu);
    EdShape off = ed_shape(ED_SHAPE_CIRCLE, 1, 1, 6, 1, 1);
    ed_wall_shape(corner, &cu, &off, EDGE_WALL);
    CHECK_EQ(undo_can_undo(&cu), 1);
    CHECK_EQ(map_vedge(corner, 6, 1), EDGE_WALL);   /* the east arc is there */
    undo_free(&cu);
    map_free(corner);

    CASE("a rectangle of wall closes too, through the same path");
    undo_clear(&u);
    EdShape box = ed_shape(ED_SHAPE_RECT, 3, 3, 8, 8, 1);
    ed_wall_shape(m, &u, &box, EDGE_WALL);
    CHECK_EQ(flood_escapes(m, 4, 4, &box), 0);

    undo_free(&u);
    map_free(m);
}

static void test_shape_keys(void)
{
    Sandbox sb = sandbox_enter("shape");
    CHECK_EQ(sb.ok, 1);
    if (!sb.ok) return;

    write_map_file(sb.dir, "m.vtt");
    char path[600];
    snprintf(path, sizeof path, "%s/m.vtt", sb.dir);

    Renderer r;
    App      a;
    rnd_init(&r);
    rnd_resize(&r, 80, 24);
    app_init(&a, NULL, &r);
    CHECK_EQ(app_open_map(&a, path), 0);

    CASE("v selects a box, V a circle");
    press(&a, "v");
    CHECK_EQ(a.ed.mode, ED_VISUAL);
    CHECK_EQ(a.ed.shape, ED_SHAPE_RECT);
    press(&a, "\x1b");

    press(&a, "V");
    CHECK_EQ(a.ed.mode, ED_VISUAL);
    CHECK_EQ(a.ed.shape, ED_SHAPE_CIRCLE);
    CHECK(strstr(a.status, "circle") != NULL);

    /* The way v and V swap between vim's two visual modes: the other key
     * changes the shape, the same key leaves. */
    CASE("the other key swaps the shape and keeps the anchor");
    a.ed.anchor_x = 0; a.ed.anchor_y = 0;
    press(&a, "v");
    CHECK_EQ(a.ed.mode, ED_VISUAL);
    CHECK_EQ(a.ed.shape, ED_SHAPE_RECT);
    CHECK_EQ(a.ed.anchor_x, 0);
    CHECK_EQ(a.ed.anchor_y, 0);

    CASE("the same key twice leaves visual mode");
    press(&a, "v");
    CHECK_EQ(a.ed.mode, ED_NORMAL);
    press(&a, "VV");
    CHECK_EQ(a.ed.mode, ED_NORMAL);

    CASE("the readout names the shape, and a circle's radius");
    press(&a, "V");
    a.ed.anchor_x = 0; a.ed.anchor_y = 0;
    a.ed.cx = 1; a.ed.cy = 0;
    char st[256];
    ed_status(&a.ed, a.map, st, sizeof st);
    CHECK(strstr(st, "circle r1") != NULL);
    press(&a, "\x1b");

    CASE("wall mode anchors both shapes too");
    press(&a, "w");
    CHECK_EQ(a.ed.mode, ED_WALL);
    press(&a, "V");
    CHECK_EQ(a.ed.has_anchor, 1);
    CHECK_EQ(a.ed.shape, ED_SHAPE_CIRCLE);
    press(&a, "v");
    CHECK_EQ(a.ed.has_anchor, 1);
    CHECK_EQ(a.ed.shape, ED_SHAPE_RECT);
    press(&a, "v");
    CHECK_EQ(a.ed.has_anchor, 0);

    CASE("enter lays the shape the anchor was dropped with");
    press(&a, "V");
    press(&a, "\r");
    CHECK_EQ(a.ed.has_anchor, 0);
    CHECK(strstr(a.status, "circle") != NULL);

    CASE("enter with no anchor says which keys set one");
    press(&a, "\r");
    CHECK(strstr(a.status, "v or V") != NULL);

    /* The bar has to name the shape too: v and V chose it a while ago, and
     * the anchor on screen does not spell out which one it is. */
    CASE("the trace bar names the shape enter would lay");
    press(&a, "V");
    rnd_begin(&r);
    app_draw(&a);
    ByteBuf f;
    bb_init(&f, 16384);
    rnd_dump(&r, &f);
    bb_putc(&f, '\0');
    CHECK(strstr(f.data, "enter circle") != NULL);
    CHECK(strstr(f.data, "enter rect") == NULL);
    bb_free(&f);

    press(&a, "v");
    rnd_begin(&r);
    app_draw(&a);
    bb_init(&f, 16384);
    rnd_dump(&r, &f);
    bb_putc(&f, '\0');
    CHECK(strstr(f.data, "enter rect") != NULL);
    CHECK(strstr(f.data, "enter circle") == NULL);
    bb_free(&f);

    app_free(&a);
    rnd_free(&r);
    unlink(path);
    sandbox_leave(&sb);
}

/* -------------------------------------------------- key tables and the ? page */

/* The bar and the ? page read the same tables, so the tables themselves are
 * what has to be right. */
static void test_keymaps(void)
{
    for (int i = 0; i < KEYS_COUNT; i++) {
        const KeyMap *km = keys_map((KeyMapId)i);

        CASE("every map has a name and rows");
        CHECK(km->name != NULL && km->name[0] != '\0');
        CHECK(km->n > 0);

        CASE("every row says what it does");
        int groups = 0, bars = 0, help = 0;
        for (int j = 0; j < km->n; j++) {
            const KeyDoc *d = &km->rows[j];
            CHECK(d->what != NULL && d->what[0] != '\0');
            if (!d->keys) { groups++; CHECK(d->bar == NULL); continue; }
            if (d->bar) bars++;
            if (strcmp(d->keys, "?") == 0) help++;
        }

        CASE("every map opens with a group heading");
        CHECK_EQ(km->rows[0].keys == NULL, 1);
        CHECK(groups > 0);

        /* Six is the cap, and the last one is ? -- ui_keybar pins the final
         * hint to the right so the way to everything else cannot be the hint
         * a narrow terminal drops. */
        CASE("no bar carries more than six hints");
        CHECK(bars <= 6);

        CASE("every map documents ?, and it is the last hint on the bar");
        CHECK_EQ(help, 1);
        CHECK_EQ(strcmp(km->rows[km->n - 1].keys, "?"), 0);
        CHECK(km->rows[km->n - 1].bar != NULL);

        /* Two rows claiming the same key in one mode means one of them is a
         * lie, and neither the bar nor the page would show which. */
        CASE("no key is documented twice in one map");
        for (int j = 0; j < km->n; j++) {
            if (!km->rows[j].keys) continue;
            for (int l = j + 1; l < km->n; l++) {
                if (!km->rows[l].keys) continue;
                CHECK(strcmp(km->rows[j].keys, km->rows[l].keys) != 0);
            }
        }
    }
}

static void test_keybar_fits(void)
{
    Renderer r;
    rnd_init(&r);

    /* The bar must never overflow, and the ? hint must survive every width a
     * terminal might be, because it is the way to everything the bar dropped. */
    for (int i = 0; i < KEYS_COUNT; i++) {
        const KeyMap *km = keys_map((KeyMapId)i);

        for (int w = 20; w <= 200; w += 3) {
            rnd_resize(&r, w, 6);
            rnd_begin(&r);
            ui_keybar(&r, &THEME_DARK, km);

            ByteBuf f;
            bb_init(&f, 4096);
            rnd_dump(&r, &f);
            bb_putc(&f, '\0');

            const char *bar = strrchr(f.data, '\n');
            CHECK(bar != NULL);
            if (bar) {
                CASE("the bar never runs past the edge");
                CHECK((int)strlen(bar + 1) <= w);
                CASE("the ? hint is there at every width");
                CHECK(strstr(f.data, "? keys") != NULL);
            }
            bb_free(&f);
        }
    }

    CASE("at eighty columns every bar keeps at least four hints");
    for (int i = 0; i < KEYS_COUNT; i++) {
        rnd_resize(&r, 80, 6);
        rnd_begin(&r);
        ui_keybar(&r, &THEME_DARK, keys_map((KeyMapId)i));

        ByteBuf f;
        bb_init(&f, 4096);
        rnd_dump(&r, &f);
        bb_putc(&f, '\0');

        int shown = 0;
        for (int j = 0; j < keys_map((KeyMapId)i)->n; j++) {
            const KeyDoc *d = &keys_map((KeyMapId)i)->rows[j];
            if (d->bar && strstr(f.data, d->bar)) shown++;
        }
        CHECK(shown >= 4);
        bb_free(&f);
    }

    rnd_free(&r);
}

static void test_help_page(void)
{
    Sandbox sb = sandbox_enter("help");
    CHECK_EQ(sb.ok, 1);
    if (!sb.ok) return;

    write_map_file(sb.dir, "fight.vtt");
    char path[600];
    snprintf(path, sizeof path, "%s/fight.vtt", sb.dir);

    Renderer r;
    App      a;
    rnd_init(&r);
    rnd_resize(&r, 90, 24);
    app_init(&a, NULL, &r);

    CHECK_EQ(app_open_map(&a, path), 0);
    Key f2 = { KEY_F2, 0, 0 };
    app_key(&a, f2);

    CASE("? opens the page and remembers where it came from");
    press(&a, "?");
    CHECK_EQ(a.screen, SCREEN_HELP);
    CHECK_EQ(a.help_from, SCREEN_PLAY);
    CHECK_EQ(a.help_id, KEYS_PLAY);

    CASE("the page leads with the mode you asked from");
    rnd_begin(&r);
    app_draw(&a);
    ByteBuf f;
    bb_init(&f, 32768);
    rnd_dump(&r, &f);
    bb_putc(&f, '\0');
    const char *play  = strstr(f.data, "Play mode");
    const char *build = strstr(f.data, "Build mode");
    CHECK(play != NULL);
    CHECK(build == NULL || play < build);      /* build is further down, if visible */

    CASE("keys the bar had no room for are on the page");
    CHECK(strstr(f.data, "range-band") == NULL);   /* below the fold at 24 rows */
    bb_free(&f);

    rnd_resize(&r, 90, 60);
    rnd_begin(&r);
    app_draw(&a);
    bb_init(&f, 65536);
    rnd_dump(&r, &f);
    bb_putc(&f, '\0');
    CHECK(strstr(f.data, "range-band") != NULL);
    CHECK(strstr(f.data, "s a") != NULL);
    CHECK(strstr(f.data, "s d") != NULL);
    bb_free(&f);
    rnd_resize(&r, 90, 24);

    /* Scrolling has to actually move the page, not just the number. */
    CASE("ctrl-d shows something the top of the page did not");
    rnd_begin(&r);
    app_draw(&a);
    ByteBuf top;
    bb_init(&top, 32768);
    rnd_dump(&r, &top);
    bb_putc(&top, '\0');

    press(&a, "\x04");                             /* ctrl-d */
    CHECK(a.help_top > 0);
    rnd_begin(&r);
    app_draw(&a);
    bb_init(&f, 32768);
    rnd_dump(&r, &f);
    bb_putc(&f, '\0');
    CHECK(strcmp(top.data, f.data) != 0);
    CHECK(strstr(f.data, "range-band") != NULL);
    bb_free(&top);
    bb_free(&f);
    press(&a, "g");

    CASE("j and k scroll, and the top does not go negative");
    CHECK_EQ(a.help_top, 0);
    press(&a, "k");
    CHECK_EQ(a.help_top, 0);
    press(&a, "jjj");
    CHECK_EQ(a.help_top, 3);
    press(&a, "k");
    CHECK_EQ(a.help_top, 2);

    /* A scroll is a request until the page lays out: only the draw knows how
     * long the page is, which is what makes G right on the first keypress. */
    CASE("G goes to the end and stops there");
    press(&a, "G");
    rnd_begin(&r); app_draw(&a);
    int end = a.help_top;
    CHECK(end > 0);
    CHECK(end < a.help_lines);                 /* the last line stays on screen */

    press(&a, "jjjjj");
    rnd_begin(&r); app_draw(&a);
    CHECK_EQ(a.help_top, end);

    CASE("g goes back to the top");
    press(&a, "g");
    CHECK_EQ(a.help_top, 0);

    CASE("q closes it, back to where it was called from");
    press(&a, "q");
    CHECK_EQ(a.screen, SCREEN_PLAY);

    CASE("esc closes it too, and so does a second ?");
    press(&a, "?");
    CHECK_EQ(a.screen, SCREEN_HELP);
    press(&a, "\x1b");
    CHECK_EQ(a.screen, SCREEN_PLAY);
    press(&a, "?");
    press(&a, "?");
    CHECK_EQ(a.screen, SCREEN_PLAY);

    /* Carrying a creature changes enough of the keyboard to be its own page. */
    CASE("the page follows the mode, not just the screen");
    tokens_add(&a.map->tokens, (Token){ 0, 0, 1, TOKEN_ENEMY, "Ogre", { { 0, "" } }, 0 });
    a.ed.cx = 0; a.ed.cy = 0;
    press(&a, "\r");
    CHECK_EQ(a.play.grabbed, 1);
    press(&a, "?");
    CHECK_EQ(a.help_id, KEYS_PLAY_GRABBED);
    press(&a, "q");

    Key f1 = { KEY_F1, 0, 0 };
    app_key(&a, f1);
    press(&a, "?");
    CHECK_EQ(a.help_id, KEYS_BUILD);
    press(&a, "q");
    CHECK_EQ(a.screen, SCREEN_EDITOR);

    /* A question mark is a character you might want in a map name. */
    CASE("? on the command line is typed, not swallowed");
    press(&a, ":name a?b\r");
    CHECK_EQ(a.screen, SCREEN_EDITOR);
    CHECK_EQ(strcmp(a.map->name, "a?b"), 0);

    CASE("the page lays out on a small terminal without crashing");
    press(&a, "?");
    for (int w = 20; w <= 120; w += 7) {
        rnd_resize(&r, w, w > 40 ? 30 : 8);
        rnd_begin(&r);
        app_draw(&a);
        rnd_flush(&r, NULL);
    }
    CHECK(1);

    app_free(&a);
    rnd_free(&r);
    unlink(path);
    sandbox_leave(&sb);
}

/* -------------------------------------------------- the remapped play keys */

static void test_play_remap(void)
{
    Sandbox sb = sandbox_enter("remap");
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

    /* i is insert, so p is free to mean paste the way it does everywhere. */
    CASE("i p places a player, i e an enemy");
    a.ed.cx = 0; a.ed.cy = 0;
    press(&a, "ip");
    CHECK_EQ(a.modal, MODAL_PROMPT);
    press(&a, "Aria\r");
    CHECK_EQ(a.map->tokens.n, 1);
    CHECK_EQ(a.map->tokens.v[0].kind, TOKEN_PLAYER);

    a.ed.cx = 1; a.ed.cy = 0;
    press(&a, "ieOgre\r");
    CHECK_EQ(a.map->tokens.n, 2);
    CHECK_EQ(a.map->tokens.v[1].kind, TOKEN_ENEMY);

    CASE("a bare i does nothing until the next key says what");
    press(&a, "i");
    CHECK_EQ(a.modal, MODAL_NONE);
    CHECK(strstr(a.status, "p player") != NULL);
    CHECK_EQ(a.map->tokens.n, 2);

    CASE("esc abandons a half-typed prefix");
    press(&a, "\x1b");
    CHECK_EQ(a.map->tokens.n, 2);
    CHECK(strstr(a.status, "cancelled") != NULL);

    /* The prefix must swallow the next key rather than let it act: a
     * half-typed command turning into a different whole one is the worst
     * thing a prefix can do. */
    CASE("a prefix swallows a key that means something on its own");
    a.play.sel = -1;
    press(&a, "it");
    CHECK_EQ(a.play.sel, -1);              /* t did not cycle */
    CHECK(strstr(a.status, "i wants") != NULL);

    CASE("p pastes now, and P says where it went");
    press(&a, "t");                        /* select the player */
    CHECK_EQ(a.play.sel, 0);
    press(&a, "y");
    a.ed.cx = 1; a.ed.cy = 1;
    press(&a, "p");
    CHECK_EQ(a.map->tokens.n, 3);
    press(&a, "P");
    CHECK_EQ(a.map->tokens.n, 3);
    CHECK(strstr(a.status, "now p") != NULL);

    CASE("c changes the label, where r used to");
    press(&a, "t");
    press(&a, "c");
    CHECK_EQ(a.modal, MODAL_PROMPT);
    press(&a, "\025Renamed\r");
    CHECK_EQ(strcmp(a.map->tokens.v[a.play.sel].label, "Renamed"), 0);

    CASE("r cycles the range bands, where R used to");
    str_lcpy(a.map->ruleset, "daggerheart", sizeof a.map->ruleset);
    press(&a, "r");
    CHECK_EQ(a.play.range.active, 1);
    press(&a, "\x1b");
    press(&a, "R");
    CHECK_EQ(a.play.range.active, 0);
    CHECK(strstr(a.status, "now r") != NULL);

    CASE("s a adds a marker, s c colours, s d drops");
    press(&a, "t");
    int sel = a.play.sel;
    press(&a, "saPoisoned\r");
    CHECK_EQ(a.map->tokens.v[sel].nstatus, 1);
    uint8_t before = a.play.status_color;
    press(&a, "sc");
    CHECK(a.play.status_color != before);
    press(&a, "sd");
    CHECK_EQ(a.map->tokens.v[sel].nstatus, 0);

    CASE("s with a key it does not know says what it wanted");
    press(&a, "sz");
    CHECK(strstr(a.status, "s wants") != NULL);

    CASE("the retired keys name where they went");
    const char *gone = "aAvVPRS";
    for (const char *c = gone; *c; c++) {
        char keys[2] = { *c, '\0' };
        app_set_status(&a, "");
        press(&a, keys);
        CHECK(a.status[0] != '\0');
    }

    /* A count typed in build must not arrive in play as a multiplier. */
    CASE("a half-typed count does not survive the mode switch");
    Key f1 = { KEY_F1, 0, 0 };
    app_key(&a, f1);
    press(&a, "12");
    CHECK(a.ed.count > 0);
    app_key(&a, f2);
    CHECK_EQ(a.ed.count, 0);

    app_free(&a);
    rnd_free(&r);
    unlink(path);
    sandbox_leave(&sb);
}

/* ------------------------------------------------- cycling and searching */

static void test_cycle_tracks(void)
{
    Map *m = map_new(12, 6, "cycle");
    map_fill_tiles(m, 0, 0, 11, 5, TILE_FLOOR);

    Play p;
    play_init(&p);

    CASE("cycling an empty map selects nothing and says so");
    CHECK_EQ(play_cycle(&p, m, 1, PLAY_ANY_KIND), 0);
    CHECK_EQ(p.sel, -1);

    /* Interleaved on purpose: a track has to skip over the other kind rather
     * than stop at it. */
    Token a = { 0, 0, 1, TOKEN_PLAYER, "Aria",  { { 0, "" } }, 0 };
    Token b = { 2, 0, 1, TOKEN_ENEMY,  "Ogre",  { { 0, "" } }, 0 };
    Token c = { 4, 0, 1, TOKEN_PLAYER, "Bram",  { { 0, "" } }, 0 };
    Token d = { 6, 0, 1, TOKEN_ENEMY,  "Goblin",{ { 0, "" } }, 0 };
    tokens_add(&m->tokens, a);
    tokens_add(&m->tokens, b);
    tokens_add(&m->tokens, c);
    tokens_add(&m->tokens, d);

    CASE("the all track visits every token in order and wraps");
    p.sel = -1;
    const int all[] = { 0, 1, 2, 3, 0 };
    for (int i = 0; i < 5; i++) {
        CHECK_EQ(play_cycle(&p, m, 1, PLAY_ANY_KIND), 1);
        CHECK_EQ(p.sel, all[i]);
    }

    CASE("the player track skips the enemies");
    p.sel = -1;
    CHECK_EQ(play_cycle(&p, m, 1, TOKEN_PLAYER), 1);
    CHECK_EQ(p.sel, 0);
    CHECK_EQ(play_cycle(&p, m, 1, TOKEN_PLAYER), 1);
    CHECK_EQ(p.sel, 2);
    CHECK_EQ(play_cycle(&p, m, 1, TOKEN_PLAYER), 1);
    CHECK_EQ(p.sel, 0);          /* wrapped past both enemies */

    CASE("the enemy track skips the players");
    p.sel = -1;
    CHECK_EQ(play_cycle(&p, m, 1, TOKEN_ENEMY), 1);
    CHECK_EQ(p.sel, 1);
    CHECK_EQ(play_cycle(&p, m, 1, TOKEN_ENEMY), 1);
    CHECK_EQ(p.sel, 3);

    CASE("shift runs a track backwards");
    p.sel = -1;
    CHECK_EQ(play_cycle(&p, m, -1, TOKEN_ENEMY), 1);
    CHECK_EQ(p.sel, 3);
    CHECK_EQ(play_cycle(&p, m, -1, TOKEN_ENEMY), 1);
    CHECK_EQ(p.sel, 1);
    CHECK_EQ(play_cycle(&p, m, -1, PLAY_ANY_KIND), 1);
    CHECK_EQ(p.sel, 0);

    /* Switching tracks should pick up near where you were looking, not at the
     * top of the list. */
    CASE("a track switch carries on from where the selection is");
    p.sel = 2;                                /* Bram, a player */
    CHECK_EQ(play_cycle(&p, m, 1, TOKEN_ENEMY), 1);
    CHECK_EQ(p.sel, 3);                       /* the enemy just after him */

    CASE("an empty track leaves the selection where it was");
    Map *only = map_new(6, 6, "only");
    map_fill_tiles(only, 0, 0, 5, 5, TILE_FLOOR);
    tokens_add(&only->tokens, b);
    Play q;
    play_init(&q);
    q.sel = 0;
    CHECK_EQ(play_cycle(&q, only, 1, TOKEN_PLAYER), 0);
    CHECK_EQ(q.sel, 0);
    map_free(only);

    CASE("a track of one comes back round to itself");
    p.sel = 0;
    CHECK_EQ(play_cycle(&p, m, 1, TOKEN_PLAYER), 1);
    CHECK_EQ(p.sel, 2);

    /* ------------------------------------------------------------ search */

    CASE("a search finds a label by any part of it, in any case");
    p.sel = -1;
    CHECK_EQ(play_find(&p, m, "gob", 1), 1);
    CHECK_EQ(p.sel, 3);
    p.sel = -1;
    CHECK_EQ(play_find(&p, m, "ARI", 1), 1);
    CHECK_EQ(p.sel, 0);
    p.sel = -1;
    CHECK_EQ(play_find(&p, m, "ra", 1), 1);   /* Bram, mid-label */
    CHECK_EQ(p.sel, 2);

    CASE("a search that matches nothing leaves the selection alone");
    p.sel = 1;
    CHECK_EQ(play_find(&p, m, "dragon", 1), 0);
    CHECK_EQ(p.sel, 1);

    CASE("repeating walks the matches and wraps, both ways");
    Token e2 = { 8, 0, 1, TOKEN_ENEMY, "Goblin 2", { { 0, "" } }, 0 };
    tokens_add(&m->tokens, e2);
    p.sel = -1;
    CHECK_EQ(play_find(&p, m, "goblin", 1), 1);
    CHECK_EQ(p.sel, 3);
    CHECK_EQ(play_find(&p, m, NULL, 1), 1);
    CHECK_EQ(p.sel, 4);
    CHECK_EQ(play_find(&p, m, NULL, 1), 1);
    CHECK_EQ(p.sel, 3);                       /* wrapped */
    CHECK_EQ(play_find(&p, m, NULL, -1), 1);
    CHECK_EQ(p.sel, 4);

    CASE("an empty needle repeats the last search rather than matching all");
    p.sel = -1;
    CHECK_EQ(play_find(&p, m, "", 1), 1);
    CHECK_EQ(p.sel, 3);
    CHECK_EQ(strcmp(p.search, "goblin"), 0);

    /* An unlabelled token has nothing to match, and must not be swept up by
     * an empty-looking search. */
    CASE("an unlabelled token matches nothing");
    Token bare = { 10, 0, 1, TOKEN_ENEMY, "", { { 0, "" } }, 0 };
    tokens_add(&m->tokens, bare);
    p.sel = -1;
    CHECK_EQ(play_find(&p, m, "z", 1), 0);

    CASE("searching with nothing ever searched for finds nothing");
    Play fresh;
    play_init(&fresh);
    CHECK_EQ(play_find(&fresh, m, NULL, 1), 0);
    CHECK_EQ(fresh.sel, -1);

    map_free(m);
}

static void test_cycle_keys(void)
{
    Sandbox sb = sandbox_enter("cyc");
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

    Token p1 = { 0, 0, 1, TOKEN_PLAYER, "Aria", { { 0, "" } }, 0 };
    Token e1 = { 1, 0, 1, TOKEN_ENEMY,  "Ogre", { { 0, "" } }, 0 };
    Token p2 = { 0, 1, 1, TOKEN_PLAYER, "Bram", { { 0, "" } }, 0 };
    tokens_add(&a.map->tokens, p1);
    tokens_add(&a.map->tokens, e1);
    tokens_add(&a.map->tokens, p2);

    CASE("t walks every token, T walks back");
    press(&a, "t"); CHECK_EQ(a.play.sel, 0);
    press(&a, "t"); CHECK_EQ(a.play.sel, 1);
    press(&a, "T"); CHECK_EQ(a.play.sel, 0);

    CASE("f walks the friendlies, e the enemies");
    press(&a, "\x1b");
    press(&a, "f"); CHECK_EQ(a.play.sel, 0);
    press(&a, "f"); CHECK_EQ(a.play.sel, 2);
    press(&a, "e"); CHECK_EQ(a.play.sel, 1);
    press(&a, "E"); CHECK_EQ(a.play.sel, 1);   /* only one enemy: back to itself */

    CASE("the cursor follows the selection so the creature can be seen");
    CHECK_EQ(a.ed.cx, a.map->tokens.v[a.play.sel].x);
    CHECK_EQ(a.ed.cy, a.map->tokens.v[a.play.sel].y);

    CASE("the status line names what was landed on");
    CHECK(strstr(a.status, "Ogre") != NULL);

    CASE("/ opens a prompt and finds by part of a label");
    press(&a, "/");
    CHECK_EQ(a.modal, MODAL_PROMPT);
    press(&a, "bra\r");
    CHECK_EQ(a.modal, MODAL_NONE);
    CHECK_EQ(a.play.sel, 2);
    CHECK(strstr(a.status, "Bram") != NULL);

    CASE("a search with no match says so and keeps the selection");
    press(&a, "/dragon\r");
    CHECK_EQ(a.play.sel, 2);
    CHECK(strstr(a.status, "no token matching") != NULL);

    CASE("n and N repeat the last search without retyping it");
    press(&a, "/o\r");                          /* Ogre */
    CHECK_EQ(a.play.sel, 1);
    press(&a, "n");
    CHECK_EQ(a.play.sel, 1);                    /* the only match, wrapped */
    press(&a, "N");
    CHECK_EQ(a.play.sel, 1);

    CASE("n before any search says what to press");
    Play saved = a.play;
    a.play.search[0] = '\0';
    press(&a, "n");
    CHECK(strstr(a.status, "/ finds") != NULL);
    a.play = saved;

    /* The three keys are only cycles in play mode; build mode has its own use
     * for the letters and must not lose it. */
    CASE("the cycle keys stay out of build mode");
    Key f1 = { KEY_F1, 0, 0 };
    app_key(&a, f1);
    CHECK_EQ(a.screen, SCREEN_EDITOR);
    int before = a.play.sel;
    press(&a, "v");
    CHECK_EQ(a.play.sel, before);
    CHECK_EQ(a.ed.mode, ED_VISUAL);
    press(&a, "\x1b");

    app_free(&a);
    rnd_free(&r);
    unlink(path);
    sandbox_leave(&sb);
}

/* Esc backs out of one thing at a time, and the range overlay is anchored to
 * a creature, so it goes when the focus does. */
static void test_play_focus(void)
{
    Sandbox sb = sandbox_enter("focus");
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
    str_lcpy(a.map->ruleset, "daggerheart", sizeof a.map->ruleset);
    Key f2 = { KEY_F2, 0, 0 };
    app_key(&a, f2);
    CHECK_EQ(a.screen, SCREEN_PLAY);

    Token one = { 0, 0, 1, TOKEN_ENEMY, "One", { { 0, "" } }, 0 };
    Token two = { 1, 1, 1, TOKEN_ENEMY, "Two", { { 0, "" } }, 0 };
    tokens_add(&a.map->tokens, one);
    tokens_add(&a.map->tokens, two);

    press(&a, "\t");                        /* focus the first token */
    CHECK_EQ(a.play.sel, 0);
    press(&a, "r");
    CASE("r anchors the overlay to the token in focus");
    CHECK_EQ(a.play.range.active, 1);
    CHECK_EQ(a.play.range.token, 0);

    CASE("esc takes the overlay off without dropping the selection");
    press(&a, "\x1b");
    CHECK_EQ(a.play.range.active, 0);
    CHECK_EQ(a.play.sel, 0);

    CASE("a second esc then lets the token go");
    press(&a, "\x1b");
    CHECK_EQ(a.play.sel, -1);

    CASE("tabbing to another creature resets the overlay");
    press(&a, "\tr");
    CHECK_EQ(a.play.range.active, 1);
    CHECK_EQ(a.play.range.token, 0);
    press(&a, "\t");
    CHECK_EQ(a.play.sel, 1);
    CHECK_EQ(a.play.range.active, 0);

    /* An overlay dropped on bare ground belongs to nobody, so moving the
     * focus about should leave it alone. */
    CASE("an overlay anchored to a tile survives a change of focus");
    press(&a, "\x1b");                      /* deselect */
    CHECK_EQ(a.play.sel, -1);
    a.ed.cx = 5; a.ed.cy = 5;
    press(&a, "r");
    CHECK_EQ(a.play.range.active, 1);
    CHECK_EQ(a.play.range.token, -1);
    press(&a, "\t");
    CHECK_EQ(a.play.sel, 0);
    CHECK_EQ(a.play.range.active, 1);

    CASE("esc still cancels a tile overlay");
    press(&a, "\x1b");
    CHECK_EQ(a.play.range.active, 0);

    /* Held first, overlay second, selection last: esc unwinds one at a time. */
    CASE("esc puts a held creature down before touching the overlay");
    a.ed.cx = a.map->tokens.v[0].x;
    a.ed.cy = a.map->tokens.v[0].y;
    play_select_at(&a.play, a.map, a.ed.cx, a.ed.cy);
    CHECK_EQ(a.play.sel, 0);
    press(&a, "r");
    CHECK_EQ(a.play.range.active, 1);
    press(&a, "\r");                        /* pick it up */
    CHECK_EQ(a.play.grabbed, 1);
    CHECK_EQ(a.play.ntrail, 1);
    press(&a, "\x1b");
    CHECK_EQ(a.play.grabbed, 0);
    CHECK_EQ(a.play.ntrail, 0);
    CHECK_EQ(a.play.range.active, 1);

    CASE("dropping with enter clears the trail too");
    press(&a, "\rl");
    CHECK_EQ(a.play.grabbed, 1);
    CHECK_EQ(a.play.ntrail, 2);
    press(&a, "\r");
    CHECK_EQ(a.play.grabbed, 0);
    CHECK_EQ(a.play.ntrail, 0);

    /* Steps used to be pushed with no mark closing them, so u reached past
     * them to the batch underneath and took the whole token off the map. */
    CASE("undo after a move takes back the step, not the creature");
    int ntok = a.map->tokens.n;
    a.ed.cx = a.map->tokens.v[0].x;
    a.ed.cy = a.map->tokens.v[0].y;
    play_select_at(&a.play, a.map, a.ed.cx, a.ed.cy);
    press(&a, "\r");
    int ox = a.map->tokens.v[0].x;
    int dir = ox > 0 ? -1 : 1;               /* the fixture map is only 2 wide */
    press(&a, dir < 0 ? "h" : "l");
    CHECK_EQ(a.map->tokens.v[0].x, ox + dir);
    CHECK_EQ(a.play.steps, 1);
    press(&a, "u");
    CHECK_EQ(a.map->tokens.n, ntok);
    CHECK_EQ(a.map->tokens.v[0].x, ox);

    CASE("and the trail retreats with it");
    CHECK_EQ(a.play.ntrail, 1);
    CHECK_EQ(a.play.steps, 0);

    app_free(&a);
    rnd_free(&r);
    unlink(path);
    sandbox_leave(&sb);
}

static void test_status_draw(void)
{
    Map *m = map_new(8, 8, "draw");
    map_fill_tiles(m, 0, 0, 7, 7, TILE_FLOOR);

    Renderer r;
    rnd_init(&r);
    rnd_resize(&r, 60, 24);

    GridView g;
    memset(&g, 0, sizeof g);
    g.zoom = 1;                            /* a 1x1 token is three cells wide */
    g.view = rect(0, 0, 60, 24);

    Token t;
    memset(&t, 0, sizeof t);
    t.x = 2; t.y = 2; t.size = 1; t.kind = TOKEN_ENEMY;
    str_lcpy(t.label, "G", sizeof t.label);

    Rect a;
    grid_token_area(&g, t.x, t.y, t.size, &a);

    CASE("an unmarked token draws no markers");
    rnd_begin(&r);
    grid_draw_token_status(&r, &g, &t, &THEME_DARK, 0);
    CHECK_EQ(rnd_at(&r, a.x, a.y - 1)->ch, ' ');

    /* Above the token, never on it: the label has to stay readable. */
    CASE("markers sit on the boundary row, not on the token");
    token_add_status(&t, 0, "Poisoned");
    token_add_status(&t, 3, "Marked");
    rnd_begin(&r);
    grid_draw_token_status(&r, &g, &t, &THEME_DARK, 0);
    CHECK_EQ(rnd_at(&r, a.x, a.y - 1)->ch, 'P');
    CHECK_EQ(rnd_at(&r, a.x + 1, a.y - 1)->ch, 'M');
    CHECK_EQ(rnd_at(&r, a.x, a.y)->ch, ' ');        /* the token row is untouched */

    CASE("each marker takes its own colour from the palette");
    CHECK_EQ(rnd_at(&r, a.x, a.y - 1)->fg, THEME_DARK.status[0]);
    CHECK_EQ(rnd_at(&r, a.x + 1, a.y - 1)->fg, THEME_DARK.status[3]);

    /* Three cells is fewer than a token can carry, so the fourth has to go
     * somewhere: the row below. */
    CASE("markers past the top edge continue underneath");
    token_add_status(&t, 4, "Burning");
    token_add_status(&t, 5, "Slowed");
    CHECK_EQ(t.nstatus, 4);
    rnd_begin(&r);
    grid_draw_token_status(&r, &g, &t, &THEME_DARK, 0);
    CHECK_EQ(rnd_at(&r, a.x + 2, a.y - 1)->ch, 'B');
    CHECK_EQ(rnd_at(&r, a.x, a.y + a.h)->ch, 'S');

    CASE("a wider token fits them all on one row");
    g.zoom = 2;
    grid_token_area(&g, t.x, t.y, t.size, &a);
    rnd_begin(&r);
    grid_draw_token_status(&r, &g, &t, &THEME_DARK, 0);
    CHECK_EQ(rnd_at(&r, a.x + 3, a.y - 1)->ch, 'S');

    CASE("ascii mode keeps letters and swaps the dot");
    g.zoom = 1;
    token_clear_status(&t);
    token_add_status(&t, 0, "Poisoned");
    token_add_status(&t, 1, "");
    grid_token_area(&g, t.x, t.y, t.size, &a);
    rnd_begin(&r);
    grid_draw_token_status(&r, &g, &t, &THEME_DARK, 1);
    CHECK_EQ(rnd_at(&r, a.x, a.y - 1)->ch, 'P');
    CHECK_EQ(rnd_at(&r, a.x + 1, a.y - 1)->ch, '*');

    rnd_free(&r);
    map_free(m);
}

int main(void)
{
    prof_init();

    struct { const char *name; void (*fn)(void); } suites[] = {
        { "util",   test_util },
        { "utf8",   test_utf8 },
        { "input",  test_input },
        { "render", test_render },
        { "draw",   test_draw },
        { "map",    test_map },
        { "tokens", test_tokens },
        { "mapio",  test_mapio },
        { "grid",   test_grid },
        { "editor", test_editor },
        { "undo",   test_undo },
        { "editing", test_editing },
        { "play",   test_play },
        { "tokendraw", test_token_draw },
        { "app",    test_app_smoke },
        { "dist",     test_dist },
        { "ruleset",  test_ruleset },
        { "ruler",    test_ruler },
        { "sight",    test_sight },
        { "measure",  test_measure_settings },
        { "edges",    test_edges },
        { "terrain",  test_terrain },
        { "secret",   test_secret_doors },
        { "formatv2", test_map_format_v2 },
        { "edgetools", test_edge_tools },
        { "range",    test_range },
        { "rangelos", test_range_sight },
        { "termio", test_term_io },
        { "delmap", test_delete_map },
        { "renmap", test_rename_map },
        { "dupmap", test_duplicate_map },
        { "status",   test_status },
        { "uniqlbl",  test_unique_label },
        { "statusio", test_status_io },
        { "tokedit",  test_token_edit_undo },
        { "statusdraw", test_status_draw },
        { "clearstatus", test_clear_status_keys },
        { "trail",    test_trail },
        { "traildraw", test_trail_draw },
        { "focus",    test_play_focus },
        { "tracks",   test_cycle_tracks },
        { "cyclekeys", test_cycle_keys },
        { "shapes",   test_shapes },
        { "circlefill", test_circle_fill },
        { "circlewall", test_circle_walls },
        { "shapekeys", test_shape_keys },
        { "keymaps",  test_keymaps },
        { "keybar",   test_keybar_fits },
        { "help",     test_help_page },
        { "remap",    test_play_remap },
        { "golden", test_golden },
    };

    for (size_t i = 0; i < sizeof suites / sizeof *suites; i++) {
        int before = g_fails;
        suites[i].fn();
        printf("  %-8s %s\n", suites[i].name, g_fails == before ? "ok" : "FAILED");
    }

    prof_shutdown();

    printf("\n%d checks, %d failures\n", g_checks, g_fails);
    return g_fails ? 1 : 0;
}
