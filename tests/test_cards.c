/* Tests: cards -- the map's table, a creature's card, the file. */

#include "harness.h"

#include <dirent.h>
#include "card.h"

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
            CHECK(oi >= 0 && !strcmp(back->cards[oi].text, text));
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
    for (int i = 0; i < a.map->tokens.n; i++) if (!strcmp(a.map->tokens.v[i].label, "Ogre")) ogre = i;
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
    int gi = card_find(a.map, "Goblin");
    CHECK(gi >= 0);
    if (gi >= 0) {
        const char *t = a.map->cards[gi].text;
        CHECK(!strncmp(t, "Goblin - Tier 1 Standard\n", 25));
        CHECK(strstr(t, "Difficulty: 14   Thresholds: 5/10") != NULL);
        CHECK(strchr(t, '#') == NULL);                          /* the help lines left out */
    }
    CHECK_EQ(strcmp(a.map->tokens.v[0].card, "Goblin"), 0);
    CHECK_EQ(strcmp(a.map->tokens.v[1].card, "Goblin"), 0);   /* Goblin 2 has it too */
    CHECK_EQ(a.map->tokens.v[2].card[0], '\0');               /* Aria does not */
    CHECK(strstr(a.status, "given to 1 more Goblin") != NULL);
    CHECK_EQ(a.map->modified, 1);
    CHECK_EQ(leftover_card_files(sb.dir), 0);

    CASE("an existing card opens as it is; an addition is kept");
    set_editor(sb.dir, "sed -i '1a Motives: steal, run' \"$1\"\n");
    press(&a, "sk");
    CHECK(strstr(a.map->cards[gi].text, "Standard\nMotives: steal, run\n") != NULL);
    CHECK(strstr(a.status, "card Goblin saved") != NULL);

    CASE("quitting untouched, emptying it, or the editor failing changes nothing");
    set_editor(sb.dir, "true\n");
    char before[CARD_TEXT_MAX];
    str_lcpy(before, a.map->cards[gi].text, sizeof before);
    press(&a, "sk");
    CHECK(strstr(a.status, "card unchanged") != NULL);
    set_editor(sb.dir, ": > \"$1\"\n");
    press(&a, "sk");
    CHECK(strstr(a.status, "empty card changes nothing") != NULL);
    set_editor(sb.dir, "exit 3\n");
    press(&a, "sk");
    CHECK(strstr(a.status, "did not finish") != NULL);
    CHECK_EQ(strcmp(a.map->cards[gi].text, before), 0);
    CHECK_EQ(leftover_card_files(sb.dir), 0);

    CASE("a new card left as it started is not written");
    set_editor(sb.dir, "true\n");
    a.ed.cx = 5; a.ed.cy = 1;
    press(&a, "sk");
    CHECK(strstr(a.status, "no card written") != NULL);
    CHECK_EQ(card_find(a.map, "Aria"), -1);
    CHECK_EQ(a.map->tokens.v[2].card[0], '\0');

    CASE("without a ruleset a new card starts with the label alone");
    press(&a, ":ruleset none\r");
    set_editor(sb.dir, "printf 'Aria\\nThe party\\x27s scout\\n' > \"$1\"\n");
    press(&a, "sk");
    int ai = card_find(a.map, "Aria");
    CHECK(ai >= 0 && !strcmp(a.map->cards[ai].text, "Aria\nThe party's scout"));

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

