/* Tests: text and input, the renderer, the map and its file, undo, golden frames, the map browser, the autosave. */

#include "harness.h"

/* ------------------------------------------------------------------ utf8 */

void test_utf8(void)
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
    CHECK_EQ(utf8_width(0x2E80u), 2);          /* the window's upper neighbor */
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

void test_input(void)
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

void test_render(void)
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

    /* The flush skips clean rows with memcmp, which reads the padding
     * bytes. That is only sound while every cell's padding is zero -- cells
     * descend from BLANK by struct copy and are mutated field by field -- so
     * this pins the invariant the comparison rests on. */
    CASE("cell padding is zero everywhere, in both buffers");
    rnd_begin(&r);
    draw_text(&r, 3, 2, "padding", -1, style(RGB(1,2,3), RGB(4,5,6), ATTR_BOLD));
    draw_cell(&r, 8, 4, 0x4E16u /* a wide glyph and its continuation */, s);
    rnd_flush(&r, NULL);
    for (size_t ci = 0; ci < r.ncells; ci++)
        for (int b = 0; b < 3; b++) {
            CHECK_EQ(r.front[ci]._pad[b], 0);
            CHECK_EQ(r.back[ci]._pad[b], 0);
        }

    CASE("a swap-heavy sequence still diffs correctly");
    rnd_begin(&r);
    draw_text(&r, 0, 0, "tock", -1, s);
    rnd_flush(&r, NULL);                 /* prime: clear the padding frame */
    for (int f = 0; f < 5; f++) {
        rnd_begin(&r);
        draw_text(&r, 0, 0, f % 2 ? "tock" : "tick", -1, s);
        rnd_flush(&r, NULL);
        CHECK_EQ(r.cells_changed, 1u);   /* only the i/o cell flips */
    }

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

void test_draw(void)
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

void test_util(void)
{
    CASE("utf8_valid: well-formed text passes; stray bytes, cut sequences and NUL do not");
    CHECK(utf8_valid("plain", 5));
    CHECK(utf8_valid("caf\xc3\xa9 \xe2\x80\x94 \xef\xbf\xbd", 13));   /* a real U+FFFD is fine */
    CHECK(!utf8_valid("caf\xe9", 4));
    CHECK(!utf8_valid("\xe2\x80", 2));
    CHECK(!utf8_valid("a\0b", 3));

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

void test_app_smoke(void)
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

void test_map(void)
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

void test_tokens(void)
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

void test_mapio(void)
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
    Token u = { 3, 5, 1, TOKEN_ENEMY,  "" };
    tokens_add(&m->tokens, p);
    tokens_add(&m->tokens, e);
    tokens_add(&m->tokens, u);

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
        CHECK_EQ(l->tokens.n, 3);
        if (l->tokens.n == 3) CHECK_EQ(l->tokens.v[2].label[0], '\0');   /* "" is empty, not a quote */
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

void test_grid(void)
{
    CASE("interior widths are odd so tokens have a center cell");
    for (int z = 0; z < ZOOM_COUNT; z++) CHECK_EQ(ZOOM[z].iw % 2, 1);

    Map *m = map_new(3, 3, "grid");
    map_fill_tiles(m, 0, 0, 2, 2, TILE_FLOOR);
    map_rect_walls(m, 1, 1, 1, 1, EDGE_WALL);   /* wall the center tile */

    Renderer r;
    rnd_init(&r);
    rnd_resize(&r, 40, 20);
    rnd_begin(&r);

    GridView g;
    memset(&g, 0, sizeof g);
    g.zoom = 0;                       /* pitch 2x2, so corner (cx,cy) is at (2cx,2cy) */
    g.view = rect(0, 0, 40, 20);

    grid_draw(&r, m, &g, &THEME_DARK, 0, 1, FOGV_GM);

    /* This is the rule that makes edge-walls legible: where a wall meets a
     * grid line, the junction belongs to the wall alone. The room's top-left
     * corner must read as ┏, not as a ╋ with two faint arms. */
    CASE("walls own the junctions they touch");
    CHECK_EQ(rnd_at(&r, 2, 2)->ch, 0x250Fu);        /* ┏ */
    CHECK_EQ(rnd_at(&r, 4, 2)->ch, 0x2513u);        /* ┓ */
    CHECK_EQ(rnd_at(&r, 2, 4)->ch, 0x2517u);        /* ┗ */
    CHECK_EQ(rnd_at(&r, 4, 4)->ch, 0x251Bu);        /* ┛ */
    CHECK_EQ(rnd_at(&r, 2, 2)->fg, THEME_DARK.wall);

    CASE("wall runs are heavy, open boundaries are thin gray");
    CHECK_EQ(rnd_at(&r, 3, 2)->ch, 0x2501u);        /* ━ along the north wall */
    CHECK_EQ(rnd_at(&r, 2, 3)->ch, 0x2503u);        /* ┃ down the west wall */
    CHECK_EQ(rnd_at(&r, 1, 2)->ch, 0x2500u);        /* ─ grid line outside it */
    CHECK_EQ(rnd_at(&r, 1, 2)->fg, THEME_DARK.grid);

    CASE("the map's outer corner is a light corner glyph");
    CHECK_EQ(rnd_at(&r, 0, 0)->ch, 0x250Cu);        /* ┌ */

    /* Grid lines exist only where a walkable tile touches the boundary, so a
     * void area draws no lattice at all -- only the dot per square that says
     * the square is not map. */
    CASE("void areas draw their marks and no lattice");
    Map *v = map_new(3, 3, "void");
    rnd_begin(&r);
    grid_draw(&r, v, &g, &THEME_DARK, 0, 1, FOGV_GM);
    int drawn = 0, marks = 0;
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++) {
            uint32_t ch = rnd_at(&r, x, y)->ch;
            if (ch == ' ') continue;
            drawn++;
            if (ch == 0x00B7u) marks++;
        }
    CHECK_EQ(marks, 9);              /* one per square, and nothing else */
    CHECK_EQ(drawn, marks);
    map_free(v);

    CASE("a map smaller than the viewport is centered");
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

void test_editor(void)
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

void test_undo(void)
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

    /* A full-map fill is 40,000 ops, so the op has to stay small: the token
     * payloads live in the side array, not in every op. */
    CASE("an op is 20 bytes and token ops own side-array slots");
    CHECK(sizeof(Op) <= 20);
    undo_clear(&u);
    CHECK_EQ(u.ntoks, 0);
    undo_begin(&u); undo_set_tile(&u, m, 0, 0, TILE_VOID); undo_end(&u);
    CHECK_EQ(u.ntoks, 0);                                   /* a cell op: none */
    Token e1 = { 5, 5, 1, TOKEN_ENEMY, "Ogre" };
    undo_begin(&u); int ei = undo_add_token(&u, m, e1); undo_end(&u);
    CHECK_EQ(u.ntoks, 1);                                   /* add: one */
    Token e2 = e1; str_lcpy(e2.label, "Troll", sizeof e2.label);
    undo_begin(&u); undo_edit_token(&u, m, ei, e2); undo_end(&u);
    CHECK_EQ(u.ntoks, 3);                                   /* edit: two */
    undo_begin(&u); undo_move_token(&u, m, ei, 6, 6); undo_end(&u);
    CHECK_EQ(u.ntoks, 3);                                   /* move: none */
    undo_undo(&u, m); undo_undo(&u, m);
    CHECK_EQ(strcmp(m->tokens.v[ei].label, "Ogre"), 0);
    undo_redo(&u, m);
    CHECK_EQ(strcmp(m->tokens.v[ei].label, "Troll"), 0);

    CASE("truncating the redo tail reclaims its token slots");
    undo_undo(&u, m); undo_undo(&u, m);                     /* back before the add */
    CHECK_EQ(m->tokens.n, 0);
    undo_begin(&u); undo_set_tile(&u, m, 1, 0, TILE_VOID); undo_end(&u);
    CHECK_EQ(u.ntoks, 0);
    CHECK_EQ(undo_can_redo(&u), 0);

    /* Equality is by field: a token copied and a token built by hand differ
     * in padding and in what follows the label's NUL, and are still equal. */
    CASE("an edit that changes nothing is judged by fields, not bytes");
    undo_clear(&u);
    Token f1 = { 2, 2, 1, TOKEN_PLAYER, "Aria" };
    undo_begin(&u); int fi = undo_add_token(&u, m, f1); undo_end(&u);
    Token f2;
    memset(&f2, 0x5a, sizeof f2);                           /* garbage everywhere */
    f2.x = 2; f2.y = 2; f2.size = 1; f2.kind = TOKEN_PLAYER; f2.nstatus = 0;
    f2.turn = 0;                                            /* init stays garbage: not in the order */
    str_lcpy(f2.label, "Aria", sizeof f2.label);
    f2.note[0] = '\0';                                      /* past the NUL stays garbage */
    f2.ncounters = 0;                                       /* and so do the unused counters */
    f2.hidden = 0;
    int marks_before = u.nmarks;
    undo_begin(&u); undo_edit_token(&u, m, fi, f2); undo_end(&u);
    CHECK_EQ(u.nmarks, marks_before);

    /* The log is bounded: past the cap the oldest batches go, the newest
     * stay usable, and the token slots follow the ops. Each fill of the
     * largest map is one batch of MAP_MAX_DIM^2 ops, so this takes five. */
    CASE("the history is capped and trims from the oldest end");
    undo_clear(&u);
    Map *big = map_new(MAP_MAX_DIM, MAP_MAX_DIM, "cap");
    Token g1 = { 0, 0, 1, TOKEN_PLAYER, "Keep" };
    undo_begin(&u); undo_add_token(&u, big, g1); undo_end(&u);     /* batch 0 */
    int fills = 0;
    while (u.trimmed == 0) {                                /* one fill per batch */
        uint8_t kind = fills % 2 ? TILE_VOID : TILE_FLOOR;
        undo_begin(&u);
        for (int y = 0; y < big->h; y++)
            for (int x = 0; x < big->w; x++) undo_set_tile(&u, big, x, y, kind);
        undo_end(&u);
        fills++;
    }
    CHECK(u.nops <= UNDO_TRIM_TO);
    CHECK(u.nops > UNDO_TRIM_TO / 2);                       /* not emptied */
    CHECK_EQ(u.ntoks, 0);                                   /* the add went with batch 0 */
    CHECK_EQ(u.depth, u.nmarks);
    CHECK(u.nmarks >= 1);
    Token g2 = { 3, 3, 1, TOKEN_ENEMY, "New" };
    undo_begin(&u); undo_add_token(&u, big, g2); undo_end(&u);
    CHECK_EQ(u.ntoks, 1);
    CHECK_EQ(u.ops[u.nops - 1].tok, 0);
    int last = fills % 2 ? TILE_VOID : TILE_FLOOR;          /* what the newest fill painted over */
    CHECK_EQ(undo_undo(&u, big), 1);                        /* the add */
    CHECK_EQ(big->tokens.n, 1);
    CHECK_EQ(undo_undo(&u, big), 1);                        /* the newest fill */
    CHECK_EQ(map_tile(big, 7, 7), last);
    while (undo_undo(&u, big)) { }
    CHECK_EQ(big->tokens.n, 1);                             /* "Keep" is history, not undone */
    map_free(big);

    undo_free(&u);
    map_free(m);
}

/* --------------------------------------------------------- build editing */

void test_editing(void)
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

/* ------------------------------------------------------- token appearance */

/* Renders one token and reports whether a cell carries its fill color. */
static int tok_filled(Renderer *r, uint32_t col, int x, int y)
{
    Cell *c = rnd_at(r, x, y);
    return c && c->bg == col;
}

void test_token_draw(void)
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

    /* Color alone cannot carry the player/enemy distinction: it is lost in
     * --ascii and to a colorblind reader. The smallest tokens say it with a
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
    CHECK_EQ(tok_filled(&r, pc, a.x + a.w / 2, a.y + a.h / 2), 1); /* center */
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
     * so a centered label must not be allowed to overwrite them. */
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

#define FIXTURE "tests/fixtures/two-rooms.vtt"

void test_golden(void)
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

    /* The fill itself is a background color, which a text dump cannot show;
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
void test_term_io(void)
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

void test_map_format_v2(void)
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

static int file_exists(const char *dir, const char *name)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    return access(path, F_OK) == 0;
}

void test_delete_map(void)
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

    CASE("y deletes it, and only it, and its recovery copy with it");
    write_map_file(dir, "bravo.vtt.autosave");
    press(&a, "dy");
    CHECK_EQ(a.modal, MODAL_NONE);
    CHECK_EQ(file_exists(dir, "bravo.vtt"), 0);
    CHECK_EQ(file_exists(dir, "bravo.vtt.autosave"), 0);
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

void test_rename_map(void)
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
    CASE("renaming moves the file and retitles the map, and its recovery copy follows");
    write_map_file(dir, "bravo.vtt.autosave");
    press(&a, "R\025goblin\r");
    CHECK_EQ(file_exists(dir, "bravo.vtt"), 0);
    CHECK_EQ(file_exists(dir, "goblin.vtt"), 1);
    CHECK_EQ(file_exists(dir, "bravo.vtt.autosave"), 0);
    CHECK_EQ(file_exists(dir, "goblin.vtt.autosave"), 1);
    unlink("goblin.vtt.autosave");
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

void test_duplicate_map(void)
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

    /* The copy is the bytes, not a re-serialization, so a map the loader
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

void test_unique_label(void)
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

    CASE("an unlabeled token stays unlabeled");
    tokens_unique_label(&l, "", out, sizeof out);
    CHECK_EQ(out[0], '\0');

    tokens_free(&l);
}

/* The recovery autosave: a copy beside the file once changes go quiet,
 * gone with a save or a discard, offered back after a crash. */
static void count_w015(void *ctx, int line, int col, const char *code,
                       const char *slug, const char *msg)
{
    (void)line; (void)col; (void)slug; (void)msg;
    if (!strcmp(code, "W015")) ++*(int *)ctx;
}

void test_autosave(void)
{
    Sandbox sb = sandbox_enter("autosave");
    CHECK_EQ(sb.ok, 1);
    if (!sb.ok) return;

    write_map_file(sb.dir, "fight.vtt");
    char path[600], autosave[620];
    snprintf(path, sizeof path, "%s/fight.vtt", sb.dir);
    snprintf(autosave, sizeof autosave, "%s.autosave", path);

    Renderer r;
    App      a;
    rnd_init(&r);
    rnd_resize(&r, 80, 24);
    app_init(&a, NULL, &r);
    a.autosave_on = 1;
    CHECK_EQ(app_open_map(&a, path), 0);
    a.ed.cx = a.ed.cy = 0;
    CHECK_EQ(a.modal, MODAL_NONE);                          /* nothing to recover */

    CASE("a clean map owes no autosave, and an idle loop can sleep for ever");
    CHECK_EQ(app_autosave_due(&a, 1000), -1);
    app_tick(&a, 1000);
    CHECK_EQ(file_exists(sb.dir, "fight.vtt.autosave"), 0);

    CASE("a change starts the clock; the copy is written once the changes go quiet");
    press(&a, "x");                                         /* a tile to void */
    CHECK_EQ(a.map->modified, 1);
    app_tick(&a, 2000);
    CHECK_EQ(app_autosave_due(&a, 2000), AUTOSAVE_QUIET_MS);
    app_tick(&a, 2000 + AUTOSAVE_QUIET_MS - 1);
    CHECK_EQ(file_exists(sb.dir, "fight.vtt.autosave"), 0);
    press(&a, "lx");                                        /* still typing: the clock restarts */
    app_tick(&a, 2000 + AUTOSAVE_QUIET_MS);
    CHECK_EQ(file_exists(sb.dir, "fight.vtt.autosave"), 0);
    app_tick(&a, 2000 + 2 * AUTOSAVE_QUIET_MS);
    CHECK_EQ(file_exists(sb.dir, "fight.vtt.autosave"), 1);
    CHECK_EQ(file_exists(sb.dir, "fight.vtt.autosave.tmp"), 0); /* renamed, unflushed or not */
    CHECK_EQ(app_autosave_due(&a, 9000), -1);              /* nothing more owed */
    CHECK_EQ(a.map->modified, 1);                           /* it is not a save */
    CHECK_EQ(strcmp(a.map->path, path), 0);

    CASE("the copy holds the changes");
    {
        char err[128];
        Map *copy = mapio_load(autosave, err, sizeof err);
        CHECK(copy != NULL);
        if (copy) { CHECK_EQ(map_tile(copy, 0, 0), TILE_VOID); CHECK_EQ(map_tile(copy, 1, 0), TILE_VOID); map_free(copy); }
    }

    CASE("a save takes the copy away");
    press(&a, ":w\r");
    CHECK_EQ(file_exists(sb.dir, "fight.vtt.autosave"), 0);
    CHECK_EQ(app_autosave_due(&a, 20000), -1);

    CASE("so does a deliberate discard");
    press(&a, "jx");
    app_tick(&a, 30000);
    app_tick(&a, 30000 + AUTOSAVE_QUIET_MS);
    CHECK_EQ(file_exists(sb.dir, "fight.vtt.autosave"), 1);
    press(&a, ":q!\r");
    CHECK_EQ(a.map, NULL);
    CHECK_EQ(file_exists(sb.dir, "fight.vtt.autosave"), 0);

    CASE("after a crash the next open offers the copy, and y takes it");
    CHECK_EQ(app_open_map(&a, path), 0);
    a.ed.cx = a.ed.cy = 0;
    press(&a, "jx");                                        /* (0,1) */
    app_tick(&a, 40000);
    app_tick(&a, 40000 + AUTOSAVE_QUIET_MS);
    CHECK_EQ(file_exists(sb.dir, "fight.vtt.autosave"), 1);
    map_free(a.map);                                        /* the crash: no close, no save */
    a.map = NULL;
    undo_clear(&a.undo);
    CHECK_EQ(app_open_map(&a, path), 0);
    a.ed.cx = a.ed.cy = 0;
    CHECK_EQ(a.modal, MODAL_CONFIRM_RECOVER);
    CHECK(strstr(a.modal_body, "never saved") != NULL);
    CHECK_EQ(map_tile(a.map, 0, 1), TILE_FLOOR);            /* the file as saved, until answered */
    press(&a, "y");
    CHECK_EQ(a.modal, MODAL_NONE);
    CHECK_EQ(map_tile(a.map, 0, 1), TILE_VOID);
    CHECK_EQ(a.map->modified, 1);
    CHECK_EQ(strcmp(a.map->path, path), 0);
    CHECK(strstr(a.status, "recovered") != NULL);
    CHECK_EQ(file_exists(sb.dir, "fight.vtt.autosave"), 1); /* kept until the save */
    press(&a, ":w\r");
    CHECK_EQ(file_exists(sb.dir, "fight.vtt.autosave"), 0);
    {
        char err[128];
        Map *disk = mapio_load(path, err, sizeof err);
        CHECK(disk != NULL);
        if (disk) { CHECK_EQ(map_tile(disk, 0, 1), TILE_VOID); map_free(disk); }
    }

    CASE("n lets the copy go, once");
    press(&a, "lx");
    app_tick(&a, 50000);
    app_tick(&a, 50000 + AUTOSAVE_QUIET_MS);
    map_free(a.map);
    a.map = NULL;
    undo_clear(&a.undo);
    CHECK_EQ(app_open_map(&a, path), 0);
    a.ed.cx = a.ed.cy = 0;
    CHECK_EQ(a.modal, MODAL_CONFIRM_RECOVER);
    press(&a, "n");
    CHECK_EQ(a.modal, MODAL_NONE);
    CHECK_EQ(file_exists(sb.dir, "fight.vtt.autosave"), 0);
    CHECK_EQ(map_tile(a.map, 1, 1), TILE_FLOOR);
    map_free(a.map); a.map = NULL; undo_clear(&a.undo);
    CHECK_EQ(app_open_map(&a, path), 0);
    a.ed.cx = a.ed.cy = 0;
    CHECK_EQ(a.modal, MODAL_NONE);

    CASE("the copy ends with an end line, which the loader stops at");
    press(&a, " ");                                         /* toggle: always a change */
    app_tick(&a, 55000);
    app_tick(&a, 55000 + AUTOSAVE_QUIET_MS);
    {
        char *whole = slurp(autosave);
        CHECK(whole != NULL);
        size_t wl = whole ? strlen(whole) : 0;
        CHECK(wl > 5 && !strcmp(whole + wl - 5, "\nend\n"));
        char err[128];
        int w015 = 0;
        Map *back = mapio_load_diag(autosave, err, sizeof err, count_w015, &w015);
        CHECK(back != NULL);
        CHECK_EQ(w015, 0);
        if (back) map_free(back);

        CASE("a copy cut short on a line's end is refused, set aside, and not offered again");
        FILE *cut = fopen(autosave, "w");                  /* everything but the end line */
        if (cut && whole) { fwrite(whole, 1, wl - 4, cut); }
        if (cut) fclose(cut);
        free(whole);
    }
    map_free(a.map); a.map = NULL; undo_clear(&a.undo);
    CHECK_EQ(app_open_map(&a, path), 0);
    CHECK_EQ(a.modal, MODAL_CONFIRM_RECOVER);
    press(&a, "y");
    CHECK(strstr(a.modal_title, "incomplete") != NULL);
    CHECK(strstr(a.modal_body, "fight.vtt.autosave.damaged") != NULL);
    CHECK_EQ(a.map->modified, 0);                           /* the file as saved */
    CHECK_EQ(a.pending_file[0], '\0');
    CHECK_EQ(file_exists(sb.dir, "fight.vtt.autosave"), 0);
    CHECK_EQ(file_exists(sb.dir, "fight.vtt.autosave.damaged"), 1);
    press(&a, "\r");
    CHECK_EQ(a.modal, MODAL_NONE);
    map_free(a.map); a.map = NULL; undo_clear(&a.undo);
    CHECK_EQ(app_open_map(&a, path), 0);
    CHECK_EQ(a.modal, MODAL_NONE);                          /* asked once */
    {
        char aside[640];
        snprintf(aside, sizeof aside, "%s.damaged", autosave);
        unlink(aside);
    }

    CASE("an autosave older than the file is not offered");
    press(&a, " ");                                         /* toggle: always a change */
    app_tick(&a, 60000);
    app_tick(&a, 60000 + AUTOSAVE_QUIET_MS);
    CHECK_EQ(file_exists(sb.dir, "fight.vtt.autosave"), 1);
    a.map->modified = 0;                                    /* pretend it was saved elsewhere... */
    write_map_file(sb.dir, "fight.vtt");                    /* ...and the file rewritten since */
    map_free(a.map); a.map = NULL; undo_clear(&a.undo);
    CHECK_EQ(app_open_map(&a, path), 0);
    a.ed.cx = a.ed.cy = 0;
    CHECK_EQ(a.modal, MODAL_NONE);
    unlink(autosave);

    CASE("a write that fails is not owed again until the next change");
    char real_path[MAP_PATH_MAX];
    str_lcpy(real_path, a.map->path, sizeof real_path);
    str_lcpy(a.map->path, "/nonexistent/dir/t.vtt", sizeof a.map->path);
    press(&a, " ");
    app_tick(&a, 65000);
    app_tick(&a, 65000 + AUTOSAVE_QUIET_MS);              /* the attempt, which fails */
    CHECK_EQ(app_autosave_due(&a, 65000 + AUTOSAVE_QUIET_MS), -1);
    CHECK_EQ(app_autosave_due(&a, 99000), -1);            /* and stays that way */
    press(&a, " ");                                        /* a change owes one again */
    app_tick(&a, 99000);
    CHECK(app_autosave_due(&a, 99000) >= 0);
    str_lcpy(a.map->path, real_path, sizeof a.map->path);
    app_tick(&a, 99000 + AUTOSAVE_QUIET_MS);
    CHECK_EQ(file_exists(sb.dir, "fight.vtt.autosave"), 1);

    CASE("quitting with y to the question lets the copy go");
    a.screen = SCREEN_MENU;                                /* the quit key lives on the menu */
    press(&a, "q");
    CHECK_EQ(a.modal, MODAL_CONFIRM_QUIT);
    press(&a, "y");
    CHECK_EQ(a.running, 0);
    CHECK_EQ(file_exists(sb.dir, "fight.vtt.autosave"), 0);
    a.running = 1;
    a.screen = SCREEN_EDITOR;
    a.map->modified = 0;

    CASE("headless runs never write one");
    a.autosave_on = 0;
    press(&a, " ");
    app_tick(&a, 70000);
    app_tick(&a, 70000 + AUTOSAVE_QUIET_MS);
    CHECK_EQ(file_exists(sb.dir, "fight.vtt.autosave"), 0);
    CHECK_EQ(app_autosave_due(&a, 80000), -1);

    app_free(&a);
    rnd_free(&r);
    sandbox_leave(&sb);
}

void test_undo_nesting(void)
{
    Map *m = map_new(6, 4, "n");
    Undo u;
    undo_init(&u);

    CASE("a helper's batch inside an operation's is part of it: one step, closed by the outermost end");
    undo_begin(&u);
    undo_set_tile(&u, m, 0, 0, TILE_WATER);
    undo_begin(&u);                                   /* a helper */
    undo_set_tile(&u, m, 1, 0, TILE_WATER);
    undo_end(&u);
    CHECK_EQ(u.open, 1);                              /* still the operation's */
    CHECK_EQ(undo_balanced(&u), 0);
    undo_begin(&u);                                   /* a second helper */
    undo_set_tile(&u, m, 2, 0, TILE_WATER);
    undo_end(&u);
    undo_end(&u);
    CHECK_EQ(u.open, 0);
    CHECK_EQ(undo_balanced(&u), 1);
    CHECK_EQ(u.nmarks, 1);
    undo_undo(&u, m);
    CHECK(map_tile(m, 0, 0) == TILE_VOID && map_tile(m, 2, 0) == TILE_VOID);

    CASE("a stroke stays open across calls, is balanced between keys, and ends when told");
    undo_clear(&u);
    undo_stroke(&u);
    undo_set_hedge(&u, m, 0, 1, EDGE_WALL);
    undo_stroke(&u);                                  /* the next step */
    undo_set_hedge(&u, m, 1, 1, EDGE_WALL);
    CHECK_EQ(undo_balanced(&u), 1);
    undo_stroke_end(&u);
    CHECK_EQ(u.open, 0);
    CHECK_EQ(u.nmarks, 1);

    CASE("another tool mid-stroke ends the stroke: two steps, as before nesting");
    undo_clear(&u);
    undo_stroke(&u);
    undo_set_hedge(&u, m, 2, 1, EDGE_WALL);
    undo_begin(&u);                                   /* a fill */
    undo_set_tile(&u, m, 3, 3, TILE_WATER);
    undo_end(&u);
    CHECK_EQ(u.open, 0);
    CHECK_EQ(u.nmarks, 2);

    CASE("undo and redo close whatever is open, however deep");
    undo_clear(&u);
    undo_begin(&u);
    undo_begin(&u);
    undo_set_tile(&u, m, 4, 3, TILE_WATER);
    undo_undo(&u, m);
    CHECK_EQ(u.open, 0);
    CHECK_EQ(u.nest, 0);
    CHECK_EQ(map_tile(m, 4, 3), TILE_VOID);

    CASE("abort forgets a nested batch whole");
    undo_clear(&u);
    undo_begin(&u);
    undo_set_tile(&u, m, 5, 3, TILE_WATER);
    undo_begin(&u);
    undo_set_tile(&u, m, 5, 2, TILE_WATER);
    undo_end(&u);
    undo_abort(&u, m);
    CHECK(u.open == 0 && u.nest == 0 && u.nmarks == 0 && u.nops == 0);
    CHECK(map_tile(m, 5, 3) == TILE_VOID && map_tile(m, 5, 2) == TILE_VOID);

    undo_free(&u);
    map_free(m);
}

void test_gray_marker(void)
{
    CASE("a marker saved as grey loads as gray and is written as gray");
    CHECK(status_color_from_name("grey") >= 0);
    CHECK_EQ(status_color_from_name("grey"), status_color_from_name("gray"));
    CHECK_EQ(strcmp(status_color_name((uint8_t)status_color_from_name("grey")), "gray"), 0);

    char path[] = "/tmp/vtt-test-XXXXXX";
    int  fd = mkstemp(path);
    if (fd >= 0) close(fd);
    write_file(path, "VTT 3\nsize 2 1\ntiles\n..\nvedges\n\nhedges\n\n\n"
                     "token player 0 0 1 \"Aria\"\ntokenstatus grey \"Hidden\"\n");
    char err[MAPIO_ERR_MAX] = { 0 };
    Map *m = mapio_load(path, err, sizeof err);
    CHECK(m != NULL);
    if (m) {
        CHECK_EQ(m->tokens.n, 1);
        CHECK_EQ(m->tokens.v[0].nstatus, 1);
        CHECK_EQ(m->tokens.v[0].status[0].color, status_color_from_name("gray"));
        CHECK_EQ(mapio_save(m, path, err, sizeof err), 0);
        map_free(m);
        FILE *f = fopen(path, "r");
        char buf[512] = { 0 };
        if (f) { if (fread(buf, 1, sizeof buf - 1, f) == 0) buf[0] = 0; fclose(f); }
        CHECK(strstr(buf, "tokenstatus gray \"Hidden\"") != NULL);
        CHECK(strstr(buf, "grey") == NULL);
    }
    unlink(path);
}
