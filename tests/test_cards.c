/* Tests: cards -- the map's table, a creature's card, the file. */

#include "harness.h"
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

