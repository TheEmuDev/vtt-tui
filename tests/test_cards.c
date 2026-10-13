/* Tests: cards -- the map's table, a creature's card, the file. */

#include "harness.h"

#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>
#include "card.h"
#include "store.h"
#include "import.h"
#include "character.h"
#include "changeset.h"
#include "floor.h"
#include "mapio.h"
#include "maptools.h"

typedef struct { const char *code; int n; } CardCodes;

static void card_code(void *ctx, int line, int col, const char *code,
                      const char *slug, const char *msg)
{
    CardCodes *c = ctx;
    (void)line; (void)col; (void)slug; (void)msg;
    if (!strcmp(code, c->code)) c->n++;
}

/* A file's text loaded through the diagnostics, counting one code. */
static Map *load_text(const char *dir, const char *text, const char *code, int *count)
{
    char path[600], err[160];
    snprintf(path, sizeof path, "%s/c.vtt", dir);
    FILE *f = fopen(path, "w");
    if (!f) return NULL;
    fputs(text, f);
    fclose(f);
    CardCodes cc = { code, 0 };
    Map *m = mapio_load_diag(path, err, sizeof err, card_code, &cc);
    if (count) *count = cc.n;
    return m;
}

void test_cards(void)
{
    Sandbox sb = sandbox_enter("cards");
    CHECK_EQ(sb.ok, 1);
    if (!sb.ok) return;
    char path[600], err[160];
    snprintf(path, sizeof path, "%s/cards.vtt", sb.dir);

    CASE("a card is set by name, found, and shown by the creatures that name it");
    Map *m = map_new(6, 4, "cards");
    CHECK_EQ(card_set(m, "goblin", "Goblin - Tier 1 Minion\r\nDifficulty: 10\n\n\n"), 0);
    CHECK_EQ(card_find(m, "goblin"), 0);
    CHECK_EQ(strcmp(m->cards[0].text, "Goblin - Tier 1 Minion\nDifficulty: 10"), 0);   /* \r\n, trailing blanks */
    CHECK_EQ(card_set(m, "goblin", "Goblin, again"), 0);                            /* replaced, not added */
    CHECK_EQ(m->ncards, 1);
    CHECK_EQ(card_set(m, "../x", "no"), -1);
    CHECK_EQ(card_set(m, "", "no"), -1);
    Token t;
    memset(&t, 0, sizeof t);
    t.x = 1; t.y = 1; t.size = 1; t.kind = TOKEN_ENEMY;
    str_lcpy(t.label, "Goblin", sizeof t.label);
    CHECK(card_of(m, &t) == NULL);                       /* names none */
    str_lcpy(t.card, "goblin", sizeof t.card);
    CHECK(card_of(m, &t) && !strcmp(card_of(m, &t), "Goblin, again"));
    str_lcpy(t.card, "ogre", sizeof t.card);
    CHECK(card_of(m, &t) == NULL);                       /* names one the map lacks */

    CASE("text past the limit is cut on a character's edge");
    {
        char big[CARD_TEXT_MAX + 16];
        memset(big, 'a', CARD_TEXT_MAX - 2);
        memcpy(big + CARD_TEXT_MAX - 2, "\xc3\xa9\xc3\xa9", 5);         /* é straddles the limit */
        CHECK(card_set(m, "big", big) >= 0);
        const char *b = m->cards[card_find(m, "big")].text;
        CHECK_EQ(strlen(b), (size_t)CARD_TEXT_MAX - 2);                  /* the é dropped whole */
        CHECK(utf8_valid(b, strlen(b)));
    }

    CASE("cards and a creature's card are saved as version 13, and read back whole");
    {
        /* A long line (split in the file), a UTF-8 one, pipes and quotes. */
        char text[1400];
        int  n = snprintf(text, sizeof text, "Ogre - Tier 2 Bruiser\n\nSmash - Action: ");
        for (int i = 0; i < 90; i++) n += snprintf(text + n, sizeof text - (size_t)n, "stomp%d ", i);
        snprintf(text + n, sizeof text - (size_t)n, "\n| not a new line | \"quoted\"\nÉlan +2");
        CHECK(card_set(m, "ogre", text) >= 0);
        str_lcpy(t.card, "ogre", sizeof t.card);
        tokens_add(&m->tokens, t);
        CHECK_EQ(mapio_save(m, path, err, sizeof err), 0);
        char *file = slurp(path);
        CHECK(file && !strncmp(file, "VTT 13\n", 7));
        CHECK(file && strstr(file, "tokencard \"ogre\"\n") != NULL);
        CHECK(file && strstr(file, "\n+ ") != NULL);                     /* the long line went in pieces */
        int longest = 0, cur = 0;
        for (const char *p = file; p && *p; p++) { if (*p == '\n') cur = 0; else if (++cur > longest) longest = cur; }
        CHECK(longest < 500);
        free(file);
        Map *back = mapio_load(path, err, sizeof err);
        CHECK(back != NULL);
        if (back) {
            CHECK_EQ(back->ncards, 3);
            int oi = card_find(back, "ogre");
            CHECK(oi >= 0 && !strcmp(back->cards[oi].text, m->cards[card_find(m, "ogre")].text));
            CHECK(oi >= 0 && strstr(back->cards[oi].text, "stomp89\n| not a new line") != NULL);   /* its trailing space trimmed */
            CHECK_EQ(strcmp(back->tokens.v[0].card, "ogre"), 0);
            map_free(back);
        }
    }

    CASE("a map with no cards stays at its version");
    {
        Map *plain = map_new(6, 4, "plain");
        CHECK_EQ(mapio_save(plain, path, err, sizeof err), 0);
        char *file = slurp(path);
        CHECK(file && strncmp(file, "VTT 13", 6) != 0);
        free(file);
        map_free(plain);
    }

    CASE("a card cut short, misnamed, twice named or stray is said, and what can be kept is");
    {
        const char *head = "VTT 13\nname x\nsize 2 2\ntiles\n..\n..\n";
        char text[512];
        int  n = 0;
        snprintf(text, sizeof text, "%scard \"a\"\n| one\nnote 0 0 \"x\"\n", head);
        Map *c = load_text(sb.dir, text, "W028", &n);
        CHECK(c && n == 1 && c->ncards == 1 && !strcmp(c->cards[0].text, "one") && c->nnotes == 1);
        map_free(c);
        snprintf(text, sizeof text, "%scard \"a b\"\n| one\nendcard\n", head);
        c = load_text(sb.dir, text, "W028", &n);
        CHECK(c && n == 1 && c->ncards == 0);
        map_free(c);
        snprintf(text, sizeof text, "%scard \"a\"\n| one\nendcard\ncard \"a\"\n| two\nendcard\n", head);
        c = load_text(sb.dir, text, "W028", &n);
        CHECK(c && n == 1 && c->ncards == 1 && !strcmp(c->cards[0].text, "one"));
        map_free(c);
        snprintf(text, sizeof text, "%sendcard\n", head);
        c = load_text(sb.dir, text, "W028", &n);
        CHECK(c && n == 1);
        map_free(c);
        snprintf(text, sizeof text, "%scard \"a\"\n| one\n|\n| three\n", head);   /* a blank line, no end */
        c = load_text(sb.dir, text, "W028", &n);
        CHECK(c && n == 1 && c->ncards == 1 && !strcmp(c->cards[0].text, "one\n\nthree"));
        map_free(c);
        snprintf(text, sizeof text, "%stokencard \"a\"\n", head);                 /* no creature */
        c = load_text(sb.dir, text, "E014", &n);
        CHECK(c && n == 1);
        map_free(c);
    }

    map_free(m);
    sandbox_leave(&sb);
}

/* The box beside the map, :card whole, and that neither reaches the players. */
void test_card_box(void)
{
    Renderer r;
    App      a;
    rnd_init(&r);
    rnd_resize(&r, 100, 24);
    app_init(&a, NULL, &r);
    CHECK_EQ(app_open_map(&a, "tests/fixtures/everything.vtt"), 0);
    press(&a, ":play\r");
    int ogre = -1;
    for (int i = 0; i < a.world.map->tokens.n; i++) if (!strcmp(a.world.map->tokens.v[i].label, "Ogre")) ogre = i;
    CHECK(ogre >= 0);

    CASE("the selected creature's card shows beside the map, the GM's alone");
    press(&a, ":K5\r\r\r");                                /* Ogre picked up and put down: selected */
    CHECK_EQ(a.play.sel, ogre);
    CHECK_EQ(app_card_shown(&a), ogre);
    CHECK(app_view_differs(&a));
    rnd_begin(&r);
    app_draw_view(&a, VIEW_GM);
    ByteBuf gm;
    bb_init(&gm, 65536);
    rnd_dump(&r, &gm);
    bb_putc(&gm, '\0');
    CHECK(strstr(gm.data, "Tier 2 Bruiser") != NULL);
    bb_free(&gm);
    char *pf = players_text(&a, &r);
    CHECK(strstr(pf, "Tier 2 Bruiser") == NULL);
    CHECK(strstr(pf, "Smash") == NULL);
    free(pf);

    CASE("**bold** is drawn bold and its marks are gone");
    rnd_begin(&r);
    app_draw_view(&a, VIEW_GM);
    int found = 0, bold = 0, stars = 0;
    for (int y = 0; y < r.h; y++)
        for (int x = 0; x + 4 < r.w; x++) {
            const Cell *c = &r.back[y * r.w + x];
            if (c->ch == '*') stars++;
            if (c->ch == '2' && r.back[y * r.w + x + 1].ch == 'd' && r.back[y * r.w + x + 2].ch == '1') {
                found = 1;
                bold = (c->attr & ATTR_BOLD) != 0;
            }
        }
    CHECK(found && bold);
    CHECK_EQ(stars, 0);

    CASE(":card off hides the box, :card on shows it; nothing selected, the one under the cursor");
    press(&a, ":card off\r");
    CHECK_EQ(app_card_shown(&a), -1);
    press(&a, ":card on\r");
    CHECK_EQ(app_card_shown(&a), ogre);
    press(&a, "\x1b");                                        /* the selection cleared */
    CHECK_EQ(a.play.sel, -1);
    a.ed.cx = 0; a.ed.cy = 0;
    CHECK_EQ(app_card_shown(&a), -1);
    a.ed.cx = 10; a.ed.cy = 4;
    CHECK_EQ(app_card_shown(&a), ogre);

    CASE(":card shows it whole and scrolls within it; esc puts it away");
    press(&a, ":card\r");
    CHECK_EQ(a.modal, MODAL_CARD);
    rnd_begin(&r);
    app_draw(&a);
    press(&a, "jjjjjjjjjjjj");
    rnd_begin(&r);
    app_draw(&a);
    CHECK_EQ(a.card_top, 0);                                   /* a short card does not scroll */
    press(&a, "\x1b");
    CHECK_EQ(a.modal, MODAL_NONE);

    CASE("a creature with no card says how to write one; build mode shows no box");
    a.ed.cx = 2; a.ed.cy = 2;                                  /* Aria */
    press(&a, ":card\r");
    CHECK_EQ(a.modal, MODAL_NONE);
    CHECK(strstr(a.status, "has no card - s k writes one") != NULL);
    a.ed.cx = 10; a.ed.cy = 4;
    press(&a, "\x1b[11~");
    CHECK_EQ(app_card_shown(&a), -1);

    CASE("_emphasis_ marks go, closing after punctuation too; snake_case stays");
    press(&a, "\x1b[12~");
    card_set(a.world.map, "ogre", "Ogre\n_Mana Beam._ The snake_case stays.");
    a.play.sel = ogre;
    rnd_begin(&r);
    app_draw_view(&a, VIEW_GM);
    {
        ByteBuf g;
        bb_init(&g, 65536);
        rnd_dump(&r, &g);
        bb_putc(&g, '\0');
        CHECK(strstr(g.data, "Mana Beam. The") != NULL);
        CHECK(strstr(g.data, "snake_case") != NULL);
        bb_free(&g);
    }

    CASE("the box never covers its creature");
    {
        Token *t = &a.world.map->tokens.v[ogre];
        int ox = t->x, oy = t->y;
        t->x = (int16_t)(a.world.map->w - t->size);                       /* the top right corner */
        t->y = 0;
        grid_center_on(&a.ed.view, a.world.map, t->x, t->y);
        rnd_begin(&r);
        app_draw_view(&a, VIEW_GM);
        Rect c;
        grid_token_area(&a.ed.view, t->x, t->y, t->size, &c);
        int covered = 0;
        for (int y = c.y; y < c.y + c.h; y++)
            for (int x = c.x; x < c.x + c.w; x++) {
                const Cell *cell = &r.back[y * r.w + x];
                covered |= cell->ch == 0x2502 || cell->ch == 0x256D;   /* the box's frame on it */
            }
        CHECK(!covered);
        t->x = (int16_t)ox; t->y = (int16_t)oy;
    }

    CASE(":card scrolls a long card, and not past its end");
    {
        char longcard[3000];
        int n = 0;
        for (int i = 0; i < 60; i++) n += snprintf(longcard + n, sizeof longcard - (size_t)n, "line %d\n", i);
        card_set(a.world.map, "ogre", longcard);
        a.play.sel = ogre;
        press(&a, ":card\r");
        rnd_begin(&r); app_draw(&a);
        press(&a, "jjj");
        rnd_begin(&r); app_draw(&a);
        CHECK_EQ(a.card_top, 3);
        press(&a, "G");
        rnd_begin(&r); app_draw(&a);
        CHECK(a.card_top > 3 && a.card_top < 60);
        press(&a, "\x1b");
    }

    app_free(&a);
    rnd_free(&r);
}

/* An "editor" for s k: a script doing to the file what the GM would. */
static void set_editor(const char *dir, const char *script)
{
    char path[640], cmd[700];
    snprintf(path, sizeof path, "%s/edit.sh", dir);
    FILE *f = fopen(path, "w");
    if (f) { fputs(script, f); fclose(f); }
    snprintf(cmd, sizeof cmd, "sh %s", path);
    setenv("VISUAL", cmd, 1);
}

static int leftover_card_files(const char *dir)
{
    DIR *d = opendir(dir);
    int n = 0;
    struct dirent *e;
    while (d && (e = readdir(d))) n += !strncmp(e->d_name, "vtt-card-", 9);
    if (d) closedir(d);
    return n;
}

void test_card_edit(void)
{
    Sandbox sb = sandbox_enter("cardedit");
    CHECK_EQ(sb.ok, 1);
    if (!sb.ok) return;
    char path[600];
    snprintf(path, sizeof path, "%s/c.vtt", sb.dir);
    FILE *f = fopen(path, "w");
    if (f) {
        fputs("VTT 2\nname c\nsize 10 4\nzoom 1\nruleset daggerheart\ntiles\n"
              "..........\n..........\n..........\n..........\n"
              "token enemy 1 1 1 \"Goblin\"\ntoken enemy 3 1 1 \"Goblin 2\"\ntoken player 5 1 1 \"Aria\"\n", f);
        fclose(f);
    }
    const char *old_visual = getenv("VISUAL"), *old_tmp = getenv("TMPDIR");
    char keep_visual[256] = "", keep_tmp[256] = "";
    if (old_visual) str_lcpy(keep_visual, old_visual, sizeof keep_visual);
    if (old_tmp)    str_lcpy(keep_tmp, old_tmp, sizeof keep_tmp);
    setenv("TMPDIR", sb.dir, 1);

    Renderer r;
    App      a;
    rnd_init(&r);
    rnd_resize(&r, 100, 24);
    app_init(&a, NULL, &r);
    CHECK_EQ(app_open_map(&a, path), 0);
    press(&a, ":play\r");

    CASE("s k on a creature with no card opens the ruleset's stat block to fill in");
    set_editor(sb.dir, "sed -i 's/Difficulty: 11/Difficulty: 14/' \"$1\"\n");
    a.ed.cx = 1; a.ed.cy = 1;
    press(&a, "sk");
    int gi = card_find(a.world.map, "Goblin");
    CHECK(gi >= 0);
    if (gi >= 0) {
        const char *t = a.world.map->cards[gi].text;
        CHECK(!strncmp(t, "Goblin - Tier 1 Standard\n", 25));
        CHECK(strstr(t, "Difficulty: 14   Thresholds: 5/10") != NULL);
        CHECK(strchr(t, '#') == NULL);                          /* the help lines left out */
    }
    CHECK_EQ(strcmp(a.world.map->tokens.v[0].card, "Goblin"), 0);
    CHECK_EQ(strcmp(a.world.map->tokens.v[1].card, "Goblin"), 0);   /* Goblin 2 has it too */
    CHECK_EQ(a.world.map->tokens.v[2].card[0], '\0');               /* Aria does not */
    CHECK(strstr(a.status, "given to 1 more Goblin") != NULL);
    CHECK_EQ(a.world.map->modified, 1);
    CHECK_EQ(leftover_card_files(sb.dir), 0);

    CASE("an existing card opens as it is; an addition is kept");
    set_editor(sb.dir, "sed -i '1a Motives: steal, run' \"$1\"\n");
    press(&a, "sk");
    CHECK(strstr(a.world.map->cards[gi].text, "Standard\nMotives: steal, run\n") != NULL);
    CHECK(strstr(a.status, "card Goblin saved") != NULL);

    CASE("quitting untouched, emptying it, or the editor failing changes nothing");
    set_editor(sb.dir, "true\n");
    char before[CARD_TEXT_MAX];
    str_lcpy(before, a.world.map->cards[gi].text, sizeof before);
    press(&a, "sk");
    CHECK(strstr(a.status, "card unchanged") != NULL);
    set_editor(sb.dir, ": > \"$1\"\n");
    press(&a, "sk");
    CHECK(strstr(a.status, "empty card changes nothing") != NULL);
    set_editor(sb.dir, "exit 3\n");
    press(&a, "sk");
    CHECK(strstr(a.status, "did not finish") != NULL);
    CHECK_EQ(strcmp(a.world.map->cards[gi].text, before), 0);
    CHECK_EQ(leftover_card_files(sb.dir), 0);

    CASE("a new card left as it started is not written");
    set_editor(sb.dir, "true\n");
    a.ed.cx = 5; a.ed.cy = 1;
    press(&a, "sk");
    CHECK(strstr(a.status, "no card written") != NULL);
    CHECK_EQ(card_find(a.world.map, "Aria"), -1);
    CHECK_EQ(a.world.map->tokens.v[2].card[0], '\0');

    CASE("without a ruleset a new card starts with the label alone");
    press(&a, ":ruleset none\r");
    set_editor(sb.dir, "printf 'Aria\\nThe party\\x27s scout\\n' > \"$1\"\n");
    press(&a, "sk");
    int ai = card_find(a.world.map, "Aria");
    CHECK(ai >= 0 && !strcmp(a.world.map->cards[ai].text, "Aria\nThe party's scout"));

    CASE("an undo of an edit made before s k gave the card keeps the card");
    press(&a, ":ruleset daggerheart\r");
    a.ed.cx = 1; a.ed.cy = 1;
    press(&a, "\x1b");
    a.world.map->tokens.v[0].card[0] = '\0';                        /* Goblin with no card yet */
    press(&a, "sh");                                          /* an edit, logged with no card */
    CHECK(a.world.map->tokens.v[0].hidden);
    set_editor(sb.dir, "sed -i '1a Hides well.' \"$1\"\n");
    press(&a, "sk");                                          /* the card, given after it */
    CHECK_EQ(strcmp(a.world.map->tokens.v[0].card, "Goblin"), 0);
    press(&a, "u");                                           /* takes the hiding back */
    CHECK(!a.world.map->tokens.v[0].hidden);
    CHECK_EQ(strcmp(a.world.map->tokens.v[0].card, "Goblin"), 0);   /* not the card */

    CASE("the card's own # lines are kept; only what is below the scissors goes");
    card_set(a.world.map, "Goblin", "Goblin\n# Tactics\nhit and run");
    set_editor(sb.dir, "true\n");
    press(&a, "sk");
    CHECK(strstr(a.status, "card unchanged") != NULL);
    CHECK_EQ(strcmp(a.world.map->cards[card_find(a.world.map, "Goblin")].text, "Goblin\n# Tactics\nhit and run"), 0);

    CASE("an editor that trims line ends leaves a new card's start unwritten");
    {
        Token w;
        memset(&w, 0, sizeof w);
        w.x = 0; w.y = 3; w.size = 1; w.kind = TOKEN_ENEMY;
        str_lcpy(w.label, "Wight", sizeof w.label);
        tokens_add(&a.world.map->tokens, w);
        w.x = 2;
        str_lcpy(w.label, "Bat", sizeof w.label);
        tokens_add(&a.world.map->tokens, w);
    }
    a.play.sel = -1;
    a.ed.cx = 0; a.ed.cy = 3;
    set_editor(sb.dir, "sed -i 's/ *$//' \"$1\"\n");
    press(&a, "sk");
    CHECK(strstr(a.status, "no card written") != NULL);

    CASE("an editor named with quotes and a space runs; $EDITOR serves without $VISUAL");
    {
        char dir2[700], ed[760], cmd[800];
        snprintf(dir2, sizeof dir2, "%s/my editors", sb.dir);
        mkdir(dir2, 0755);
        snprintf(ed, sizeof ed, "%s/ed.sh", dir2);
        FILE *ef = fopen(ed, "w");
        if (ef) { fputs("sed -i 's/Difficulty: 11/Difficulty: 12/' \"$1\"\n", ef); fclose(ef); }
        snprintf(cmd, sizeof cmd, "sh \"%s\"", ed);
        setenv("VISUAL", cmd, 1);
        a.ed.cx = 2; a.ed.cy = 3;                               /* Bat, no card yet */
        press(&a, "sk");
        CHECK(card_find(a.world.map, "Bat") >= 0 && strstr(a.world.map->cards[card_find(a.world.map, "Bat")].text, "Difficulty: 12"));
        unsetenv("VISUAL");
        setenv("EDITOR", "true", 1);
        press(&a, "sk");
        CHECK(strstr(a.status, "card unchanged") != NULL);
        setenv("VISUAL", "no-such-editor-anywhere", 1);
        press(&a, "sk");
        CHECK(strstr(a.status, "no editor to run") != NULL);
        unsetenv("EDITOR");
    }

    CASE("a new card goes only to creatures on the same side");
    {
        Token pl;
        memset(&pl, 0, sizeof pl);
        pl.x = 7; pl.y = 1; pl.size = 1; pl.kind = TOKEN_PLAYER;
        str_lcpy(pl.label, "Kobold", sizeof pl.label);
        tokens_add(&a.world.map->tokens, pl);
        Token en = pl;
        en.x = 8; en.kind = TOKEN_ENEMY;
        str_lcpy(en.label, "Kobold 2", sizeof en.label);
        tokens_add(&a.world.map->tokens, en);
        set_editor(sb.dir, "sed -i '1a Sneaky.' \"$1\"\n");
        a.play.sel = -1;
        a.ed.cx = 8; a.ed.cy = 1;
        press(&a, "sk");
        CHECK_EQ(strcmp(a.world.map->tokens.v[a.world.map->tokens.n - 1].card, "Kobold"), 0);
        CHECK_EQ(a.world.map->tokens.v[a.world.map->tokens.n - 2].card[0], '\0');  /* the player Kobold */
    }

    app_free(&a);
    rnd_free(&r);
    if (keep_visual[0]) setenv("VISUAL", keep_visual, 1); else unsetenv("VISUAL");
    if (keep_tmp[0]) setenv("TMPDIR", keep_tmp, 1); else unsetenv("TMPDIR");
    sandbox_leave(&sb);
}

/* A character template keeps its card, and placing it brings the card. */
void test_card_templates(void)
{
    Sandbox sb = sandbox_enter("cardtpl");
    CHECK_EQ(sb.ok, 1);
    if (!sb.ok) return;
    char err[160], said[160];

    CASE("saving a creature saves its card; loading the template has it");
    Map *m = map_new(8, 4, "src");
    map_fill_tiles(m, 0, 0, 7, 3, TILE_FLOOR);
    card_set(m, "Ogre", "Ogre - Tier 2 Bruiser\nDifficulty: 14");
    Token t;
    memset(&t, 0, sizeof t);
    t.size = 2; t.kind = TOKEN_ENEMY;
    str_lcpy(t.label, "Ogre", sizeof t.label);
    str_lcpy(t.card, "Ogre", sizeof t.card);
    tokens_add(&m->tokens, t);
    CHECK_EQ(character_save(m, 0, "ogre", NULL, 0, err, sizeof err), 0);
    Map *tpl = character_load("ogre", err, sizeof err);
    CHECK(tpl != NULL);
    if (tpl) CHECK(card_of(tpl, character_token(tpl)) && !strncmp(card_of(tpl, character_token(tpl)), "Ogre - Tier 2", 13));

    CASE("placing it into a map without the card brings the card");
    Map *d = map_new(8, 4, "dest");
    map_fill_tiles(d, 0, 0, 7, 3, TILE_FLOOR);
    Undo u;
    undo_init(&u);
    int idx = tpl ? character_place(d, &u, tpl, -1, 1, 1, 0, said, sizeof said, err, sizeof err) : -1;
    CHECK(idx >= 0);
    CHECK(card_find(d, "Ogre") >= 0);
    if (idx >= 0) CHECK(card_of(d, &d->tokens.v[idx]) != NULL);

    CASE("a map with its own card of that name keeps it, and says so");
    card_set(d, "Ogre", "Our ogre is different");
    idx = tpl ? character_place(d, &u, tpl, -1, 4, 1, 0, said, sizeof said, err, sizeof err) : -1;
    CHECK(idx >= 0);
    CHECK_EQ(strcmp(d->cards[card_find(d, "Ogre")].text, "Our ogre is different"), 0);
    CHECK(strstr(said, "kept this map's card Ogre") != NULL);

    CASE("a creature naming a card its map lacks is saved without the name");
    Token lone = t;
    str_lcpy(lone.card, "Nobody", sizeof lone.card);
    lone.x = 4;
    tokens_add(&m->tokens, lone);
    CHECK_EQ(character_save(m, 1, "lone", NULL, 0, err, sizeof err), 0);
    Map *ltpl = character_load("lone", err, sizeof err);
    CHECK(ltpl && character_token(ltpl)->card[0] == '\0');
    map_free(ltpl);

    CASE("the channel's characters read gives the card's first line");
    {
        Renderer r;
        App      a;
        rnd_init(&r);
        rnd_resize(&r, 80, 24);
        app_init(&a, NULL, &r);
        a.world.ctl_auto = 1;                       /* edits land at once (decision 6) */
        char path[640];
        snprintf(path, sizeof path, "%s/d.vtt", sb.dir);
        CHECK_EQ(mapio_save(d, path, err, sizeof err), 0);
        CHECK_EQ(app_open_map(&a, path), 0);
        char *ans = ctl_ask(&a, "characters");
        CHECK(ans && strstr(ans, "ogre  \"Ogre\" enemy 2x2  card: Ogre - Tier 2 Bruiser") != NULL);
        free(ans);
        app_free(&a);
        rnd_free(&r);
    }

    undo_free(&u);
    map_free(d);
    map_free(tpl);
    map_free(m);
    sandbox_leave(&sb);
}

/* The JSON reader, which imports read other people's files through. */
void test_json_read(void)
{
    char err[160];
    #define PARSE(txt) json_parse(txt, strlen(txt), err, sizeof err)

    CASE("objects, arrays, strings, numbers, true, false and null");
    JsonVal *v = PARSE("\xef\xbb\xbf [ {\"a\": 1.5e2, \"b\": [true, false, null], \"c\": \"x\"}, -3, \"\" ]");
    CHECK(v && v->kind == JSON_ARR && v->n == 3);
    if (v) {
        const JsonVal *o = &v->kids[0];
        CHECK(o->kind == JSON_OBJ && o->n == 3);
        CHECK(json_get(o, "a") && json_get(o, "a")->num == 150.0);
        CHECK(json_get(o, "b") && json_get(o, "b")->n == 3 && json_get(o, "b")->kids[0].b == 1);
        CHECK(json_text(json_get(o, "c")) && !strcmp(json_text(json_get(o, "c")), "x"));
        CHECK(json_get(o, "missing") == NULL);
        CHECK(v->kids[1].num == -3.0);
        CHECK(json_text(&v->kids[2]) && !json_text(&v->kids[2])[0]);
    }
    json_free(v);

    CASE("escapes are decoded, \\u as UTF-8, a surrogate pair as one character");
    v = PARSE("\"a\\\"b\\\\c\\/d\\ne\\u00e9\\ud83d\\ude00\"");
    CHECK(v && json_text(v) && !strcmp(json_text(v), "a\"b\\c/d\ne\xc3\xa9\xf0\x9f\x98\x80"));
    json_free(v);

    CASE("anything that is not JSON is refused, saying where");
    static const char *const bad[] = {
        "", "[1,]", "{\"a\" 1}", "[1 2]", "\"open", "{\"a\":}", "tru", "01x", "-", "1.", "1e",
        "\"\\q\"", "\"\\ud800\"", "\"\\udc00\"", "\"\\u12\"", "\"a\tb\"", "[1] 2", "{1:2}", "\"\\u0000\"",
        "\"\xff\"",
    };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        v = PARSE(bad[i]);
        CHECK(v == NULL);
        CHECK(strstr(err, "not JSON at line") != NULL);
        json_free(v);
    }
    v = PARSE("[1,\n2,\n]");
    CHECK(v == NULL && strstr(err, "line 3") != NULL);

    CASE("nesting past 64 is refused, not recursed into");
    {
        char deep[200];
        memset(deep, '[', 100);
        memset(deep + 100, ']', 100);
        v = json_parse(deep, sizeof deep, err, sizeof err);
        CHECK(v == NULL && strstr(err, "64") != NULL);
        memset(deep, '[', 60);
        memset(deep + 60, ']', 60);
        v = json_parse(deep, 120, err, sizeof err);
        CHECK(v != NULL);
        json_free(v);
    }
    #undef PARSE
}

/* --import-adversaries: a list made templates with cards. A made-up list:
 * vtt ships no SRD data. */
void test_import(void)
{
    Sandbox sb = sandbox_enter("import");
    CHECK_EQ(sb.ok, 1);
    if (!sb.ok) return;
    char path[640], err[320];
    snprintf(path, sizeof path, "%s/adv.json", sb.dir);
    FILE *f = fopen(path, "w");
    if (f) {
        fputs("\xef\xbb\xbf[\n"
              " {\"name\": \"Cave Bat\", \"tier\": \"1\", \"type\": \"Minion\", \"difficulty\": 10,"
              "  \"thresholds\": \"None\", \"hp\": \"1\", \"stress\": \"1\", \"atk\": \"-1\", \"attack\": \"Bite\","
              "  \"range\": \"Melee\", \"damage\": \"2 phy\", \"description\": \"A bat.\","
              "  \"feature\": [{\"name\": \"Minion (3) - Passive\", \"text\": \"Defeated by **any** damage.\"}]},\n"
              " {\"name\": \"Stone Troll\", \"tier\": 2, \"type\": \"Bruiser\", \"hp\": 9},\n"
              " {\"name\": \"???\"},\n"
              " {\"tier\": \"1\"}\n"
              "]\n", f);
        fclose(f);
    }
    FILE *out = tmpfile();

    CASE("each adversary becomes a template: an enemy, 1x1, full, with its stat block as its card");
    CHECK_EQ(import_adversaries(path, 0, out, err, sizeof err), 2);
    Map *bat = character_load("Cave-Bat", err, sizeof err);
    CHECK(bat != NULL);
    if (bat) {
        const Token *t = character_token(bat);
        CHECK(t->kind == TOKEN_ENEMY && t->size == 1 && !strcmp(t->label, "Cave Bat"));
        CHECK(t->ncounters == 2 && t->counters[0].value == 1 && t->counters[1].max == 1);
        const char *c = card_of(bat, t);
        CHECK(c && !strncmp(c, "Cave Bat - Tier 1 Minion\nA bat.\n", 32));
        CHECK(c && strstr(c, "Difficulty: 10   Thresholds: None   HP: 1   Stress: 1\n") != NULL);
        CHECK(c && strstr(c, "Attack: -1   Bite (Melee) 2 phy\n") != NULL);
        CHECK(c && strstr(c, "\n\nMinion (3) - Passive: Defeated by **any** damage.") != NULL);
        map_free(bat);
    }
    Map *troll = character_load("Stone-Troll", err, sizeof err);
    CHECK(troll != NULL);                                    /* a sparse one: what it has */
    if (troll) {
        const char *c = card_of(troll, character_token(troll));
        CHECK(c && !strcmp(c, "Stone Troll - Tier 2 Bruiser\nHP: 9"));
        map_free(troll);
    }

    CASE("again, the ones there are kept; --force replaces them");
    CHECK_EQ(import_adversaries(path, 0, out, err, sizeof err), 0);
    CHECK_EQ(import_adversaries(path, 1, out, err, sizeof err), 2);
    rewind(out);
    char said[2048] = "";
    size_t n = fread(said, 1, sizeof said - 1, out);
    said[n] = '\0';
    CHECK(strstr(said, "imported 2 adversaries") != NULL);
    CHECK(strstr(said, "2 already there, kept (--force replaces them)") != NULL);
    CHECK(strstr(said, "skipped ???: its name makes no file name") != NULL);
    fclose(out);

    CASE("a file that is not a list, or not JSON, is refused with why");
    f = fopen(path, "w");
    if (f) { fputs("{\"name\": \"x\"}", f); fclose(f); }
    CHECK_EQ(import_adversaries(path, 0, stdout, err, sizeof err), -1);
    CHECK(strstr(err, "not a list of adversaries") != NULL);
    f = fopen(path, "w");
    if (f) { fputs("[{\"name\": ]", f); fclose(f); }
    CHECK_EQ(import_adversaries(path, 0, stdout, err, sizeof err), -1);
    CHECK(strstr(err, "not JSON at line 1") != NULL);

    sandbox_leave(&sb);
}

/* What the review of the cards found, each kept from coming back. */
void test_card_fixes(void)
{
    Sandbox sb = sandbox_enter("cardfix");
    CHECK_EQ(sb.ok, 1);
    if (!sb.ok) return;
    char path[640], err[320];
    snprintf(path, sizeof path, "%s/f.vtt", sb.dir);

    CASE("a card of bytes that are not text loads as text, and saves without stalling");
    {
        FILE *f = fopen(path, "w");
        if (f) {
            fputs("VTT 13\nname f\nsize 2 2\ntiles\n..\n..\ncard \"a\"\n| ", f);
            for (int i = 0; i < 401; i++) fputc(0x80, f);
            fputs("\x01ok\tthere\nendcard\n", f);
            fclose(f);
        }
        Map *m = mapio_load(path, err, sizeof err);
        CHECK(m != NULL);
        if (m) {
            const char *t = m->cards[0].text;
            CHECK(utf8_valid(t, strlen(t)));
            CHECK(strstr(t, "\xef\xbf\xbd") != NULL);           /* U+FFFD in their place */
            CHECK(strstr(t, "ok there") != NULL);                 /* the control byte gone, the tab a space */
            CHECK_EQ(mapio_save(m, path, err, sizeof err), 0);    /* returns: no stall */
            map_free(m);
        }
    }

    CASE("past the size limit the rest of a card goes, later short lines too");
    {
        FILE *f = fopen(path, "w");
        if (f) {
            fputs("VTT 13\nname f\nsize 2 2\ntiles\n..\n..\ncard \"a\"\n", f);
            for (int i = 0; i < 12; i++) { fputs("| ", f); for (int k = 0; k < 390; k++) fputc('a' + i, f); fputc('\n', f); }
            fputs("| tail\nendcard\n", f);
            fclose(f);
        }
        CardCodes cc = { "W020", 0 };
        Map *m = mapio_load_diag(path, err, sizeof err, card_code, &cc);
        CHECK(m && cc.n == 1);
        if (m) {
            CHECK(strlen(m->cards[0].text) < CARD_TEXT_MAX);
            CHECK(strstr(m->cards[0].text, "tail") == NULL);
            map_free(m);
        }
    }

    CASE("a stamp keeps no card names: it carries no cards");
    {
        Map *m = map_new(4, 4, "s");
        map_fill_tiles(m, 0, 0, 3, 3, TILE_FLOOR);
        card_set(m, "Ogre", "Ogre");
        Token t;
        memset(&t, 0, sizeof t);
        t.size = 1; t.kind = TOKEN_ENEMY;
        str_lcpy(t.card, "Ogre", sizeof t.card);
        tokens_add(&m->tokens, t);
        Map *st = stamp_copy(m, 0, 0, 3, 3);
        CHECK(st && st->tokens.n == 1 && st->tokens.v[0].card[0] == '\0');
        map_free(st);
        map_free(m);
    }

    CASE("the import says when it cuts a long name, and two cut to one are not one");
    {
        char jpath[640];
        snprintf(jpath, sizeof jpath, "%s/long.json", sb.dir);
        FILE *f = fopen(jpath, "w");
        if (f) {
            fputs("[{\"name\": \"Fallen Warlord: Undefeated Champion\"},"
                  " {\"name\": \"Fallen Warlord: Undefeated Champions All\"}]", f);
            fclose(f);
        }
        FILE *out = tmpfile();
        CHECK_EQ(import_adversaries(jpath, 0, out, err, sizeof err), 1);
        rewind(out);
        char said[1024] = "";
        size_t n = fread(said, 1, sizeof said - 1, out);
        said[n] = '\0';
        fclose(out);
        CHECK(strstr(said, "its label is cut to \"Fallen Warlord: Undefeated Cham\"") != NULL);
        CHECK(strstr(said, "the name of one before it") != NULL);
    }

    sandbox_leave(&sb);
}


/* ------------------------------------------------ environments (:env) */

static int first_line_is(const char *path, const char *want)
{
    char line[64] = "";
    FILE *f = fopen(path, "r");
    if (f) { if (!fgets(line, sizeof line, f)) line[0] = '\0'; fclose(f); }
    return !strcmp(line, want);
}

void test_env(void)
{
    Sandbox sb = sandbox_enter("env");
    CHECK_EQ(sb.ok, 1);
    if (!sb.ok) return;
    const char *old_visual = getenv("VISUAL"), *old_tmp = getenv("TMPDIR");
    char keep_visual[256] = "", keep_tmp[256] = "";
    if (old_visual) str_lcpy(keep_visual, old_visual, sizeof keep_visual);
    if (old_tmp)    str_lcpy(keep_tmp, old_tmp, sizeof keep_tmp);
    setenv("TMPDIR", sb.dir, 1);

    Renderer r;
    App      a;
    rnd_init(&r);
    rnd_resize(&r, 100, 30);
    app_init(&a, NULL, &r);
    CHECK(ctl_blank_map(&a, sb.dir, 20, 12));
    if (!a.world.map) { app_free(&a); rnd_free(&r); sandbox_leave(&sb); return; }
    a.world.ctl_auto = 1;
    Map *m = a.world.map;
    char err[160], path[700];
    snprintf(path, sizeof path, "%s/env.vtt", sb.dir);
    str_lcpy(m->ruleset, "daggerheart", sizeof m->ruleset);
    CHECK(map_area_set(m, "Crypt", 1, 1, 6, 6) >= 0);
    CHECK(map_area_set(m, "Upper", 0, 0, 19, 11) >= 0);
    m->areas[map_area_find(m, "Upper")].floor = 1;
    Area *crypt = &m->areas[map_area_find(m, "Crypt")], *upper = &m->areas[map_area_find(m, "Upper")];
    app_key(&a, (Key){ KEY_F2, 0, 0 });

    CASE("no environment yet; a card's name must be one");
    a.ed.cx = 3; a.ed.cy = 3;
    press(&a, ":env\r");
    CHECK(strstr(a.status, "no environment here") != NULL);
    press(&a, ":env bad/name\r");
    CHECK(strstr(a.status, "letters, digits") != NULL && !crypt->env[0]);
    press(&a, ":env a b\r");
    CHECK(strstr(a.status, ":env, :env NAME") != NULL);

    CASE(":env NAME goes to the smallest area under the cursor, one undo step; outside any, to the map");
    int d0 = a.world.undo.depth;
    press(&a, ":env haunted-crypt\r");
    CHECK(!strcmp(crypt->env, "haunted-crypt") && !upper->env[0] && !m->env[0]);
    CHECK(strstr(a.status, "Crypt: environment haunted-crypt - no card of that name yet") != NULL);
    CHECK_EQ(a.status_gm, 1);
    CHECK_EQ(a.world.undo.depth, d0 + 1);
    press(&a, "u");
    CHECK(!crypt->env[0]);
    press(&a, "\x12");
    CHECK(!strcmp(crypt->env, "haunted-crypt"));
    a.ed.cx = 15; a.ed.cy = 9;                             /* on the floor, in no room */
    unsigned cg = m->cards_gen;
    press(&a, ":env old-keep\r");
    CHECK(!strcmp(m->env, "old-keep") && !upper->env[0]);
    CHECK(m->cards_gen != cg && m->modified);
    press(&a, ":env old-keep\r");
    CHECK(strstr(a.status, "the map already has old-keep") != NULL);

    CASE(":env map NAME and :env area \"AREA\" NAME say which outright; a floor is named that way");
    press(&a, ":env map the-keep\r");
    CHECK(!strcmp(m->env, "the-keep"));
    press(&a, ":env area \"Upper\" windy-hall\r");
    CHECK(!strcmp(upper->env, "windy-hall"));
    press(&a, ":env area \"Nowhere\" x\r");
    CHECK(strstr(a.status, "no area called Nowhere") != NULL);

    CASE("the environment here: the room's, else its floor's, else the map's");
    press(&a, ":env\r");                                   /* (15,9): the floor's */
    CHECK(strstr(a.status, "Upper: environment windy-hall has no card") != NULL);
    CHECK_EQ(a.modal, MODAL_NONE);

    CASE(":env edit starts a new one from the ruleset's block, its labels only");
    a.ed.cx = 3; a.ed.cy = 3;
    set_editor(sb.dir, "sed -i 's/What the place is like./Cold, and the dead do not rest./' \"$1\"\n");
    press(&a, ":env edit\r");
    int ci = card_find(m, "haunted-crypt");
    CHECK(ci >= 0);
    if (ci >= 0) {
        const char *t = m->cards[ci].text;
        CHECK(!strncmp(t, "haunted-crypt - Tier 1 Exploration\nCold, and the dead do not rest.\n", 66));
        CHECK(strstr(t, "Potential Adversaries:") != NULL && strstr(t, ">8") == NULL);
    }
    CHECK(strstr(a.status, "card haunted-crypt written") != NULL);
    set_editor(sb.dir, "true\n");
    press(&a, ":env edit\r");
    CHECK(strstr(a.status, "card unchanged") != NULL);
    CHECK_EQ(leftover_card_files(sb.dir), 0);
    a.ed.cx = 19; a.ed.cy = 0;
    upper->env[0] = '\0';
    m->env[0] = '\0';
    press(&a, ":env edit\r");
    CHECK(strstr(a.status, "has no environment - :env NAME") != NULL);
    str_lcpy(m->env, "the-keep", sizeof m->env);
    str_lcpy(upper->env, "windy-hall", sizeof upper->env);

    CASE(":env reads it whole, in the card's view: the GM's alone, and its own copy of the text");
    a.ed.cx = 3; a.ed.cy = 3;
    press(&a, ":env\r");
    CHECK_EQ(a.modal, MODAL_CARD);
    CHECK(a.card_text && a.card_text != m->cards[ci].text && strstr(a.card_text, "the dead do not rest") != NULL);
    CHECK(!strcmp(a.card_title, "Crypt: haunted-crypt"));
    CHECK(app_view_differs(&a));
    rnd_begin(&r);
    app_draw_view(&a, VIEW_GM);
    ByteBuf gf;
    bb_init(&gf, 65536);
    rnd_dump(&r, &gf);
    bb_putc(&gf, '\0');
    CHECK(strstr(gf.data, "the dead do not rest") != NULL);
    bb_free(&gf);
    char *pf = players_text(&a, &r);
    CHECK(strstr(pf, "the dead do not rest") == NULL && strstr(pf, "haunted-crypt") == NULL);
    free(pf);
    press(&a, "\x1b");
    CHECK_EQ(a.modal, MODAL_NONE);

    CASE("the file: version 14, both lines, and back again; without one it is written as before");
    CHECK_EQ(mapio_save(m, path, err, sizeof err), 0);
    CHECK(first_line_is(path, "VTT 14\n"));
    Map *back = mapio_load(path, err, sizeof err);
    CHECK(back != NULL);
    if (back) {
        CHECK(!strcmp(back->env, "the-keep"));
        CHECK(!strcmp(back->areas[map_area_find(back, "Crypt")].env, "haunted-crypt"));
        CHECK(!strcmp(back->areas[map_area_find(back, "Upper")].env, "windy-hall"));
        CHECK(card_find(back, "haunted-crypt") >= 0);
        back->env[0] = '\0';
        for (int i = 0; i < back->nareas; i++) back->areas[i].env[0] = '\0';
        char p2[700];
        snprintf(p2, sizeof p2, "%s/plain.vtt", sb.dir);
        CHECK_EQ(mapio_save(back, p2, err, sizeof err), 0);
        CHECK(first_line_is(p2, "VTT 13\n"));               /* a card is still there */
        map_free(back);
    }

    CASE("a line naming no area, or no card's name, is dropped with its code");
    {
        char p3[700];
        snprintf(p3, sizeof p3, "%s/bad.vtt", sb.dir);
        FILE *f = fopen(p3, "w");
        if (f) {
            fputs("VTT 14\nname bad\nsize 4 4\nzoom 1\ntiles\n....\n....\n....\n....\n"
                  "area 0 0 1 1 \"Nook\"\nareaenv \"Nook\" \"damp\"\nareaenv \"Gone\" \"damp\"\n"
                  "areaenv \"Nook\" \"bad name\"\nenv \"bad name\"\nareaenv Nook damp\n", f);
            fclose(f);
        }
        FILE *out = tmpfile();
        int rc = maptools_check(out, p3, 0);
        char said[4096] = "";
        rewind(out);
        size_t got = fread(said, 1, sizeof said - 1, out);
        said[got] = '\0';
        fclose(out);
        CHECK_EQ(rc, 1);
        int n30 = 0;
        for (const char *q = said; (q = strstr(q, "W030")); q++) n30++;
        CHECK_EQ(n30, 4);
        CHECK(strstr(said, "one came before it") == NULL);
        CHECK(strstr(said, "no area called Gone") != NULL);
        CHECK(strstr(said, "W170") != NULL && strstr(said, "Nook names the environment damp") != NULL);
        Map *b = mapio_load(p3, err, sizeof err);
        CHECK(b && !strcmp(b->areas[0].env, "damp") && !b->env[0]);
        map_free(b);
    }

    CASE("a second line for the map, or for an area, is dropped and said; the first stands");
    {
        char p4[700];
        snprintf(p4, sizeof p4, "%s/twice.vtt", sb.dir);
        FILE *f = fopen(p4, "w");
        if (f) {
            fputs("VTT 14\nname twice\nsize 4 4\nzoom 1\ntiles\n....\n....\n....\n....\n"
                  "area 0 0 1 1 \"Nook\"\nenv \"first\"\nenv \"second\"\n"
                  "areaenv \"Nook\" \"one\"\nareaenv \"nook\" \"two\"\n", f);
            fclose(f);
        }
        Map *b = mapio_load(p4, err, sizeof err);
        CHECK(b && !strcmp(b->env, "first") && !strcmp(b->areas[0].env, "one"));
        map_free(b);
        FILE *out = tmpfile();
        maptools_check(out, p4, 0);
        char said[4096] = "";
        rewind(out);
        size_t got = fread(said, 1, sizeof said - 1, out);
        said[got] = '\0';
        fclose(out);
        CHECK(strstr(said, "a second environment for the map dropped") != NULL);
        CHECK(strstr(said, "a second environment for Nook dropped") != NULL);
    }

    CASE("a change set carries both: the map's own and an area's, each seen, each put on");
    {
        Map *base = map_new(20, 12, "base"), *want = map_new(20, 12, "base");
        CHECK(map_area_set(base, "Crypt", 1, 1, 6, 6) >= 0 && map_area_set(want, "Crypt", 1, 1, 6, 6) >= 0);
        ChangeSet cs;
        memset(&cs, 0, sizeof cs);
        cs_diff(&cs, base, want, NULL);
        CHECK(cs_empty(&cs));
        str_lcpy(want->env, "moor", sizeof want->env);
        cs_diff(&cs, base, want, NULL);
        CHECK(cs.env_changed && !cs.nareas && !strcmp(cs.env_after, "moor"));
        char sum[256] = "";
        cs_summary(&cs, NULL, sum, sizeof sum);
        CHECK(strstr(sum, "the map's environment moor") != NULL);
        Undo u;
        undo_init(&u);
        cs_check(&cs, base);
        CHECK_EQ(cs.conflicts, 0);
        CHECK(cs_apply(&cs, base, &u, NULL, sum, sizeof sum) > 0);
        CHECK(!strcmp(base->env, "moor"));
        str_lcpy(want->areas[0].env, "bog", sizeof want->areas[0].env);
        cs_diff(&cs, base, want, NULL);
        CHECK(!cs.env_changed && cs.nareas == 1);
        CHECK(cs_apply(&cs, base, &u, NULL, sum, sizeof sum) > 0);
        CHECK(!strcmp(base->areas[0].env, "bog"));
        undo_undo(&u, base);
        CHECK(!base->areas[0].env[0] && !strcmp(base->env, "moor"));   /* the area's is in the log, the map's is not */
        cs_free(&cs);
        undo_free(&u);
        map_free(base);
        map_free(want);
    }

    CASE(":env area \"X\" remove takes that area's off");
    press(&a, ":env area \"Upper\" remove\r");
    CHECK(!upper->env[0]);
    press(&a, "u");
    CHECK(!strcmp(upper->env, "windy-hall"));

    CASE("--describe and the dump name them; --check is quiet once the cards exist");
    char *t = ctl_ask(&a, "dump\n");
    CHECK(strstr(t, "\nenvironment the-keep\n") != NULL);
    CHECK(strstr(t, "B2:G7  environment haunted-crypt\n") != NULL);
    free(t);
    t = ctl_ask(&a, "describe json\n");
    CHECK(t && strstr(t, "\"environment\":\"haunted-crypt\"") != NULL && strstr(t, "\"environment\":\"the-keep\"") != NULL);
    free(t);

    CASE("the channel: env AREA CARD and env AREA remove, one undo step; the map's is not an agent's");
    app_key(&a, (Key){ KEY_F1, 0, 0 });
    t = ctl_ask(&a, "env Crypt sunken-crypt\n");
    CHECK(strncmp(t, "ok\n", 3) == 0);
    free(t);
    CHECK(!strcmp(crypt->env, "sunken-crypt"));
    press(&a, "u");
    CHECK(!strcmp(crypt->env, "haunted-crypt"));
    t = ctl_ask(&a, "env Nowhere x\n");
    CHECK(strstr(t, "no area called Nowhere") != NULL);
    free(t);
    t = ctl_ask(&a, "env Crypt bad/name\n");
    CHECK(strstr(t, "a card's name") != NULL);
    free(t);
    t = ctl_ask(&a, "env Upper remove\n");
    CHECK(strncmp(t, "ok\n", 3) == 0 && !upper->env[0]);
    free(t);
    t = ctl_ask(&a, "env Upper remove\n");
    CHECK(strstr(t, "has no environment") != NULL);
    free(t);

    CASE(":env adversaries: the picker holds the card's creatures that are saved characters, and names the rest");
    app_key(&a, (Key){ KEY_F2, 0, 0 });
    a.ed.cx = 3; a.ed.cy = 3;
    press(&a, ":env adversaries\r");                       /* the skeleton's line is empty */
    CHECK(a.modal == MODAL_NONE && strstr(a.status, "no \"Potential Adversaries:\" line") != NULL);
    CHECK(card_set(m, "haunted-crypt", "Haunted Crypt - Tier 1 Exploration\n"
                   "**Potential Adversaries:** Beasts (Giant Rat, Bat), Ghoul, Undead (Skeleton Archer)\n") >= 0);
    press(&a, ":env adversaries\r");
    CHECK(a.modal == MODAL_NONE && strstr(a.status, "none of haunted-crypt's potential adversaries") != NULL);
    {
        Token t;
        memset(&t, 0, sizeof t);
        t.size = 1; t.kind = TOKEN_ENEMY; t.x = 10; t.y = 10;
        const char *who[] = { "Giant Rat", "Bat", "Wolf" };
        for (int k = 0; k < 3; k++) {
            str_lcpy(t.label, who[k], sizeof t.label);
            int idx = tokens_add(&m->tokens, t);
            char nm[MAP_NAME_MAX];
            character_name_from_label(who[k], nm, sizeof nm);
            CHECK(character_save(m, idx, nm, NULL, 0, err, sizeof err) == 0);
            tokens_remove(&m->tokens, idx);
        }
    }
    int n0 = m->tokens.n;
    press(&a, ":env adversaries\r");
    CHECK_EQ(a.modal, MODAL_PICKER);
    CHECK_EQ(a.picker.n, 2);                               /* Giant-Rat and Bat; not Wolf, not the missing two */
    CHECK(!strcmp(a.picker.items[0].name, "Giant-Rat") || !strcmp(a.picker.items[1].name, "Giant-Rat"));
    CHECK(strstr(a.status, "2 of 4 are saved characters; not saved: Ghoul, Skeleton-Archer") != NULL);
    CHECK_EQ(a.status_gm, 1);
    pf = players_text(&a, &r);                             /* the picker and the names are the GM's */
    CHECK(strstr(pf, "Giant-Rat") == NULL && strstr(pf, "Ghoul") == NULL && strstr(pf, "Character") == NULL);
    free(pf);
    CHECK(a.pick_only == NULL);
    a.ed.cx = 4; a.ed.cy = 4;
    press(&a, "bat\r");
    CHECK_EQ(a.modal, MODAL_NONE);
    CHECK_EQ(m->tokens.n, n0 + 1);
    CHECK(!strcmp(m->tokens.v[n0].label, "Bat") && m->tokens.v[n0].x == 4);
    press(&a, "u");
    CHECK_EQ(m->tokens.n, n0);
    /* The whole line is the list: two spaces in it end nothing. */
    CHECK(card_set(m, "haunted-crypt", "Haunted Crypt\nPotential Adversaries: Beasts (Giant Rat,  Bat), Wolf\n") >= 0);
    press(&a, ":env adversaries\r");
    CHECK_EQ(a.modal, MODAL_PICKER);
    CHECK_EQ(a.picker.n, 3);
    press(&a, "\x1b");
    press(&a, "ite");                                      /* the picker at large still holds them all */
    CHECK_EQ(a.picker.n, 3);
    press(&a, "\x1b");
    app_key(&a, (Key){ KEY_F1, 0, 0 });
    press(&a, ":env adversaries\r");
    CHECK(a.modal == MODAL_NONE && strstr(a.status, "play mode's") != NULL);

    CASE(":env remove takes the name off and leaves the card; an area removed and undone has its own back");
    app_key(&a, (Key){ KEY_F2, 0, 0 });
    a.ed.cx = 3; a.ed.cy = 3;
    press(&a, ":env remove\r");
    CHECK(!crypt->env[0] && card_find(m, "haunted-crypt") >= 0);
    CHECK(strstr(a.status, "Crypt has no environment now") != NULL);
    press(&a, "u");
    CHECK(!strcmp(crypt->env, "haunted-crypt"));
    undo_begin(&a.world.undo);
    undo_remove_area(&a.world.undo, m, "Crypt");
    undo_end(&a.world.undo);
    CHECK_EQ(map_area_find(m, "Crypt"), -1);
    press(&a, "u");
    CHECK(map_area_find(m, "Crypt") >= 0 && !strcmp(m->areas[map_area_find(m, "Crypt")].env, "haunted-crypt"));
    CHECK(undo_balanced(&a.world.undo));

    if (keep_visual[0]) setenv("VISUAL", keep_visual, 1); else unsetenv("VISUAL");
    if (keep_tmp[0])    setenv("TMPDIR", keep_tmp, 1);    else unsetenv("TMPDIR");
    app_free(&a);
    rnd_free(&r);
    sandbox_leave(&sb);
}

/* --------------------------------- --import-environments, :env pick */

void test_env_import(void)
{
    Sandbox sb = sandbox_enter("envimport");
    CHECK_EQ(sb.ok, 1);
    if (!sb.ok) return;
    char path[700], err[320], lib[700];
    snprintf(path, sizeof path, "%s/environments.json", sb.dir);
    /* Made-up places: vtt ships none of the SRD's. */
    FILE *f = fopen(path, "w");
    if (f) {
        fputs("[{\"name\": \"Misty Bog\", \"tier\": 1, \"type\": \"Traversal\", \"description\": \"Wet to the knee.\","
              " \"impulses\": \"Swallow, mislead\", \"difficulty\": \"12\","
              " \"potential_adversaries\": \"Beasts (Bog Rat, Heron), Lost Soul\","
              " \"feature\": [{\"name\": \"Sucking Mud - Passive\", \"text\": \"Moving costs more.\", \"question\": \"What was lost here?\"},"
              " {\"name\": \"Fog - Action\", \"text\": \"It closes in.\"}]},"
              " {\"name\": \"Empty Hall\"}, {\"name\": \"???\"}, {\"tier\": 2}]", f);
        fclose(f);
    }
    FILE *out = tmpfile();

    Renderer r;
    App      a;
    rnd_init(&r);
    rnd_resize(&r, 100, 30);
    app_init(&a, NULL, &r);
    CHECK(ctl_blank_map(&a, sb.dir, 12, 8));
    if (!a.world.map) { app_free(&a); rnd_free(&r); fclose(out); sandbox_leave(&sb); return; }
    Map *m = a.world.map;
    app_key(&a, (Key){ KEY_F2, 0, 0 });

    CASE("no environments kept yet: :env pick says how to get some");
    press(&a, ":env pick\r");
    CHECK(a.modal == MODAL_NONE && strstr(a.status, "no environments kept") != NULL);

    CASE("each environment becomes a text file, laid out as a card; a sparse one has what it has");
    CHECK_EQ(import_environments(path, 0, out, err, sizeof err), 2);
    char *bog = environment_load("Misty-Bog");
    CHECK(bog != NULL);
    if (bog) {
        CHECK(!strncmp(bog, "Misty Bog - Tier 1 Traversal\nWet to the knee.\nImpulses: Swallow, mislead\n"
                            "Difficulty: 12\nPotential Adversaries: Beasts (Bog Rat, Heron), Lost Soul\n\n"
                            "Sucking Mud - Passive: Moving costs more.\nWhat was lost here?\n\n"
                            "Fog - Action: It closes in.\n", 220));
        free(bog);
    }
    char *hall = environment_load("Empty-Hall");
    CHECK(hall && !strcmp(hall, "Empty Hall\n"));
    free(hall);
    CHECK(environment_load("Nowhere") == NULL && environment_load("../x") == NULL);

    CASE("again, the ones there are kept; --force replaces them");
    CHECK_EQ(import_environments(path, 0, out, err, sizeof err), 0);
    CHECK_EQ(import_environments(path, 1, out, err, sizeof err), 2);
    rewind(out);
    char said[2048] = "";
    size_t n = fread(said, 1, sizeof said - 1, out);
    said[n] = '\0';
    fclose(out);
    CHECK(strstr(said, "imported 2 environments") != NULL);
    CHECK(strstr(said, "2 already there, kept (--force replaces them)") != NULL);
    CHECK(strstr(said, "skipped ???: its name makes no file name") != NULL);

    CASE("a file that is not a list, or not JSON, is refused with why");
    snprintf(lib, sizeof lib, "%s/bad.json", sb.dir);
    f = fopen(lib, "w");
    if (f) { fputs("{\"name\": \"x\"}", f); fclose(f); }
    CHECK_EQ(import_environments(lib, 0, stdout, err, sizeof err), -1);
    CHECK(strstr(err, "not a list of environments") != NULL);
    CHECK_EQ(import_environments("/nonexistent/e.json", 0, stdout, err, sizeof err), -1);

    CASE(":env NAME copies a kept one into a map that has no card of that name, once");
    press(&a, ":env Misty-Bog\r");
    CHECK(!strcmp(m->env, "Misty-Bog"));
    int ci = card_find(m, "Misty-Bog");
    CHECK(ci >= 0 && !strncmp(m->cards[ci].text, "Misty Bog - Tier 1 Traversal\n", 29));
    CHECK(strstr(a.status, "copied from your environments") != NULL);
    CHECK_EQ(a.status_gm, 1);
    CHECK(card_set(m, "Misty-Bog", "Mine now.") >= 0);     /* the map's own copy from here on */
    press(&a, ":env remove\r");
    press(&a, ":env Misty-Bog\r");
    ci = card_find(m, "Misty-Bog");
    CHECK(ci >= 0 && !strcmp(m->cards[ci].text, "Mine now."));
    CHECK(strstr(a.status, "copied") == NULL);

    CASE("one called like a word of :env's is still a name when the picker chooses it; a name no card can have is not offered");
    {
        char p5[700], dir[600];
        store_dir("environments", dir, sizeof dir);
        snprintf(p5, sizeof p5, "%s/remove.txt", dir);
        FILE *g = fopen(p5, "w");
        if (g) { fputs("Remove - Tier 1 Event\n", g); fclose(g); }
        snprintf(p5, sizeof p5, "%s/a-name-far-too-long-to-be-the-name-of-a-card.txt", dir);
        g = fopen(p5, "w");
        if (g) { fputs("Too long\n", g); fclose(g); }
        press(&a, ":env Misty-Bog\r");
        press(&a, ":env pick\r");
        CHECK_EQ(a.picker.n, 3);                          /* the long one is not there */
        press(&a, "remove\r");
        CHECK(!strcmp(m->env, "remove") && card_find(m, "remove") >= 0);
        snprintf(p5, sizeof p5, "%s/remove.txt", dir);
        unlink(p5);
        snprintf(p5, sizeof p5, "%s/a-name-far-too-long-to-be-the-name-of-a-card.txt", dir);
        unlink(p5);
    }

    CASE("an environment over what a card holds is cut, and the import says so; a short one says nothing");
    {
        char p6[700];
        snprintf(p6, sizeof p6, "%s/long.json", sb.dir);
        FILE *g = fopen(p6, "w");
        if (g) {
            fputs("[{\"name\": \"Long Road\", \"description\": \"", g);
            for (int k = 0; k < 5000; k++) fputc('x', g);
            fputs("\"}, {\"name\": \"Short Road\", \"description\": \"ctl\\u0001\\u0001 gone\"}]", g);
            fclose(g);
        }
        FILE *o2 = tmpfile();
        CHECK_EQ(import_environments(p6, 0, o2, err, sizeof err), 2);
        rewind(o2);
        char s2[1024] = "";
        size_t got = fread(s2, 1, sizeof s2 - 1, o2);
        s2[got] = '\0';
        fclose(o2);
        CHECK(strstr(s2, "Long Road: cut to what a card holds") != NULL);
        CHECK(strstr(s2, "Short Road: cut") == NULL);
        char *lr = environment_load("Long-Road");
        CHECK(lr && strlen(lr) <= CARD_TEXT_MAX);
        free(lr);
        char dir[600], p7[700];
        store_dir("environments", dir, sizeof dir);
        snprintf(p7, sizeof p7, "%s/Long-Road.txt", dir);  unlink(p7);
        snprintf(p7, sizeof p7, "%s/Short-Road.txt", dir); unlink(p7);
        snprintf(p7, sizeof p7, "%s/Long-Road.txt.tmp", dir);
        CHECK(access(p7, F_OK) != 0);                      /* nothing half-written left */
    }

    CASE(":env pick is the picker over what is kept, its first line beside each; choosing names it here");
    press(&a, ":env remove\r");
    CHECK(map_area_set(m, "Hall", 0, 0, 3, 3) >= 0);
    a.ed.cx = 1; a.ed.cy = 1;
    press(&a, ":env pick\r");
    CHECK_EQ(a.modal, MODAL_PICKER);
    CHECK_EQ(a.picker.n, 2);
    CHECK(!strcmp(a.picker.items[0].name, "Empty-Hall") && !strcmp(a.picker.items[0].detail, "Empty Hall"));
    CHECK(!strcmp(a.picker.items[1].detail, "Misty Bog - Tier 1 Traversal"));
    CHECK(app_view_differs(&a));
    press(&a, "empty\r");
    CHECK_EQ(a.modal, MODAL_NONE);
    CHECK(!strcmp(m->areas[map_area_find(m, "Hall")].env, "Empty-Hall") && !m->env[0]);
    CHECK(card_find(m, "Empty-Hall") >= 0);
    press(&a, "u");                                        /* the area's name comes off; the card stays */
    CHECK(!m->areas[map_area_find(m, "Hall")].env[0] && card_find(m, "Empty-Hall") >= 0);
    CHECK(undo_balanced(&a.world.undo));

    app_free(&a);
    rnd_free(&r);
    sandbox_leave(&sb);
}
