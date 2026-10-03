/* Tests: the keys: keymaps, the key bar, the ? page, remapping, cycling and focus. */

#include "harness.h"

/* The bar and the ? page read the same tables, so the tables themselves are
 * what has to be right. */
void test_keymaps(void)
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

void test_keybar_fits(void)
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

void test_help_page(void)
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
    CHECK(strstr(f.data, "cycle the bands") == NULL);   /* below the fold at 24 rows */
    bb_free(&f);

    rnd_resize(&r, 90, 120);                   /* the play page has grown past a hundred rows */
    rnd_begin(&r);
    app_draw(&a);
    bb_init(&f, 65536);
    rnd_dump(&r, &f);
    bb_putc(&f, '\0');
    CHECK(strstr(f.data, "cycle the bands") != NULL);
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
    CHECK(strstr(top.data, "yank -- copy it") == NULL);      /* below the first fold */
    CHECK(strstr(f.data, "yank -- copy it") != NULL);
    bb_free(&top);
    bb_free(&f);
    press(&a, "g");

    CASE("a key wider than its column is shown whole, on a line of its own");
    {
        int seen_link = 0, seen_scene = 0, seen_serve = 0;
        for (int t = 0; t < a.help_lines; t += 10) {
            a.help_top = t;
            rnd_begin(&r);
            app_draw(&a);
            bb_init(&f, 32768);
            rnd_dump(&r, &f);
            bb_putc(&f, '\0');
            seen_link  |= strstr(f.data, ":link to crypt Entrance") != NULL;
            seen_scene |= strstr(f.data, ":scene save Ambush") != NULL;
            seen_serve |= strstr(f.data, ":serve --stay-alive") != NULL;
            bb_free(&f);
        }
        CHECK(seen_link);
        CHECK(seen_scene);
        CHECK(seen_serve);
        a.help_top = 0;
    }

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

void test_play_remap(void)
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
    CHECK(strstr(a.status, "canceled") != NULL);

    /* The prefix must swallow the next key rather than let it act: a
     * half-typed command turning into a different whole one is the worst
     * thing a prefix can do. */
    CASE("a prefix swallows a key that means something on its own");
    play_focus(&a.play, -1);
    press(&a, "if");
    CHECK_EQ(a.play.sel, -1);              /* f did not cycle */
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
    CHECK_EQ(a.play.range.active, 0);

    /* R came back, as v did: the capital of a tool's key changes the tool's
     * variant, the way M changes the ruler's metric. */
    CASE("R cycles the range shape, a count names one, and esc keeps it");
    press(&a, "R");
    CHECK_EQ(a.play.range.shape, RANGE_CONE);
    CHECK(strstr(a.status, "range shape: cone") != NULL);
    CHECK_EQ(a.play.range.active, 0);           /* a setting: it shows nothing itself */
    press(&a, "4R");
    CHECK_EQ(a.play.range.shape, RANGE_SQUARE);
    press(&a, "R");
    CHECK_EQ(a.play.range.shape, RANGE_CIRCLE);  /* wraps */
    press(&a, "3R");
    press(&a, "r");
    CHECK_EQ(a.play.range.active, 1);
    CHECK_EQ(a.play.range.shape, RANGE_LINE);
    press(&a, "\x1b");
    CHECK_EQ(a.play.range.active, 0);
    CHECK_EQ(a.play.range.shape, RANGE_LINE);
    press(&a, "1R");

    /* The overlay's band index or radius only means something against the
     * ruleset it was set under, so changing the ruleset takes it off rather
     * than leaving a highlight that reads as something else. */
    CASE("2r names the second band, and :ruleset takes the overlay off");
    press(&a, "2r");
    CHECK_EQ(a.play.range.active, 1);
    CHECK_EQ(a.play.range.band, 1);
    press(&a, ":ruleset none\r");
    CHECK_EQ(a.play.range.active, 0);
    press(&a, "20r");
    CHECK_EQ(a.play.range.active, 1);
    CHECK_EQ(a.play.range.radius, 20);
    press(&a, ":ruleset daggerheart\r");
    CHECK_EQ(a.play.range.active, 0);

    CASE("s a adds a marker, s c colors, s d drops");
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

void test_cycle_tracks(void)
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
    play_focus(&p, -1);
    const int all[] = { 0, 1, 2, 3, 0 };
    for (int i = 0; i < 5; i++) {
        CHECK_EQ(play_cycle(&p, m, 1, PLAY_ANY_KIND), 1);
        CHECK_EQ(p.sel, all[i]);
    }

    CASE("the player track skips the enemies");
    play_focus(&p, -1);
    CHECK_EQ(play_cycle(&p, m, 1, TOKEN_PLAYER), 1);
    CHECK_EQ(p.sel, 0);
    CHECK_EQ(play_cycle(&p, m, 1, TOKEN_PLAYER), 1);
    CHECK_EQ(p.sel, 2);
    CHECK_EQ(play_cycle(&p, m, 1, TOKEN_PLAYER), 1);
    CHECK_EQ(p.sel, 0);          /* wrapped past both enemies */

    CASE("the enemy track skips the players");
    play_focus(&p, -1);
    CHECK_EQ(play_cycle(&p, m, 1, TOKEN_ENEMY), 1);
    CHECK_EQ(p.sel, 1);
    CHECK_EQ(play_cycle(&p, m, 1, TOKEN_ENEMY), 1);
    CHECK_EQ(p.sel, 3);

    CASE("shift runs a track backwards");
    play_focus(&p, -1);
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
    play_focus(&q, 0);
    CHECK_EQ(play_cycle(&q, only, 1, TOKEN_PLAYER), 0);
    CHECK_EQ(q.sel, 0);
    map_free(only);

    CASE("a track of one comes back round to itself");
    play_focus(&p, 0);
    CHECK_EQ(play_cycle(&p, m, 1, TOKEN_PLAYER), 1);
    CHECK_EQ(p.sel, 2);

    /* ------------------------------------------------------------ search */

    CASE("a search finds a label by any part of it, in any case");
    play_focus(&p, -1);
    CHECK_EQ(play_find(&p, m, "gob", 1), 1);
    CHECK_EQ(p.sel, 3);
    play_focus(&p, -1);
    CHECK_EQ(play_find(&p, m, "ARI", 1), 1);
    CHECK_EQ(p.sel, 0);
    play_focus(&p, -1);
    CHECK_EQ(play_find(&p, m, "ra", 1), 1);   /* Bram, mid-label */
    CHECK_EQ(p.sel, 2);

    CASE("a search that matches nothing leaves the selection alone");
    play_focus(&p, 1);
    CHECK_EQ(play_find(&p, m, "dragon", 1), 0);
    CHECK_EQ(p.sel, 1);

    CASE("repeating walks the matches and wraps, both ways");
    Token e2 = { 8, 0, 1, TOKEN_ENEMY, "Goblin 2", { { 0, "" } }, 0 };
    tokens_add(&m->tokens, e2);
    play_focus(&p, -1);
    CHECK_EQ(play_find(&p, m, "goblin", 1), 1);
    CHECK_EQ(p.sel, 3);
    CHECK_EQ(play_find(&p, m, NULL, 1), 1);
    CHECK_EQ(p.sel, 4);
    CHECK_EQ(play_find(&p, m, NULL, 1), 1);
    CHECK_EQ(p.sel, 3);                       /* wrapped */
    CHECK_EQ(play_find(&p, m, NULL, -1), 1);
    CHECK_EQ(p.sel, 4);

    CASE("an empty needle repeats the last search rather than matching all");
    play_focus(&p, -1);
    CHECK_EQ(play_find(&p, m, "", 1), 1);
    CHECK_EQ(p.sel, 3);
    CHECK_EQ(strcmp(p.search, "goblin"), 0);

    /* An unlabeled token has nothing to match, and must not be swept up by
     * an empty-looking search. */
    CASE("an unlabeled token matches nothing");
    Token bare = { 10, 0, 1, TOKEN_ENEMY, "", { { 0, "" } }, 0 };
    tokens_add(&m->tokens, bare);
    play_focus(&p, -1);
    CHECK_EQ(play_find(&p, m, "z", 1), 0);

    CASE("searching with nothing ever searched for finds nothing");
    Play fresh;
    play_init(&fresh);
    CHECK_EQ(play_find(&fresh, m, NULL, 1), 0);
    CHECK_EQ(fresh.sel, -1);

    map_free(m);
}

void test_cycle_keys(void)
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
void test_play_focus(void)
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
    play_select_at(&a.play, a.map, a.ed.cx, a.ed.cy, 1);
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
    play_select_at(&a.play, a.map, a.ed.cx, a.ed.cy, 1);
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
