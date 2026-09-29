/* Tests: saved things in play: the picker, character templates, scenes, handouts. */

#include "harness.h"

static UiPickItem *pick_fixture(int *n)
{
    static const char *const rows[][2] = {
        { "bogre",      "Bog Troll" },
        { "ghost",      "Pale Ghost" },
        { "ogre",       "Ogre" },
        { "ogre-chief", "Ogre Chief" },
        { "wolf",       "Dire Ogre-hound" },
    };
    *n = 5;
    UiPickItem *it = xmalloc(sizeof *it * 5);
    for (int i = 0; i < 5; i++) {
        str_lcpy(it[i].name, rows[i][0], sizeof it[i].name);
        str_lcpy(it[i].detail, rows[i][1], sizeof it[i].detail);
    }
    return it;
}

static Key kc(uint32_t ch, uint8_t mods) { Key k = { KEY_CHAR, mods, ch }; return k; }
static Key kk(KeyKind kind, uint8_t mods) { Key k = { kind, mods, 0 }; return k; }

static const char *pick_name(const UiPicker *pk, int i) { return pk->items[pk->match[i]].name; }

void test_picker(void)
{
    UiPicker pk;
    memset(&pk, 0, sizeof pk);
    int n;

    CASE("nothing typed: the whole list, in its own order");
    UiPickItem *fx = pick_fixture(&n);
    ui_picker_open(&pk, "Character", fx, n, "");
    CHECK_EQ(pk.nmatch, 5);
    CHECK(!strcmp(pick_name(&pk, 0), "bogre") && !strcmp(pick_name(&pk, 4), "wolf"));

    CASE("the exact name first, then names starting with it, then names holding it, then labels");
    const char *q = "ogre";
    for (const char *p = q; *p; p++) ui_picker_key(&pk, kc((uint32_t)*p, 0));
    CHECK_EQ(pk.nmatch, 4);
    CHECK(!strcmp(pick_name(&pk, 0), "ogre"));
    CHECK(!strcmp(pick_name(&pk, 1), "ogre-chief"));
    CHECK(!strcmp(pick_name(&pk, 2), "bogre"));
    CHECK(!strcmp(pick_name(&pk, 3), "wolf"));          /* by its label */
    CHECK_EQ(ui_picker_chosen(&pk), 2);                 /* the item's own index */

    CASE("case is ignored");
    ui_picker_key(&pk, kc('u', MOD_CTRL));
    ui_picker_key(&pk, kc('G', 0));
    ui_picker_key(&pk, kc('H', 0));
    CHECK_EQ(pk.nmatch, 1);
    CHECK(!strcmp(pick_name(&pk, 0), "ghost"));

    CASE("tab fills in the highlighted name, and again the next, without narrowing the list");
    ui_picker_key(&pk, kc('u', MOD_CTRL));
    ui_picker_key(&pk, kc('o', 0));
    ui_picker_key(&pk, kc('g', 0));
    CHECK_EQ(pk.nmatch, 4);                             /* ogre, ogre-chief, bogre, wolf */
    ui_picker_key(&pk, kk(KEY_TAB, 0));
    CHECK(!strcmp(pk.p.buf, "ogre"));
    CHECK_EQ(pk.nmatch, 4);
    ui_picker_key(&pk, kk(KEY_TAB, 0));
    CHECK(!strcmp(pk.p.buf, "ogre-chief"));
    CHECK_EQ(pk.sel, 1);
    ui_picker_key(&pk, kk(KEY_TAB, MOD_SHIFT));
    CHECK(!strcmp(pk.p.buf, "ogre"));
    CHECK_EQ(pk.p.cursor, 4);

    CASE("typing after a tab narrows again");
    ui_picker_key(&pk, kc('-', 0));
    CHECK_EQ(pk.nmatch, 2);                             /* ogre-chief, and wolf's Ogre-hound */
    CHECK(!strcmp(pick_name(&pk, 0), "ogre-chief"));
    CHECK_EQ(pk.cycling, 0);
    ui_picker_key(&pk, kk(KEY_BACKSPACE, 0));
    CHECK_EQ(pk.nmatch, 4);

    CASE("up and down move the highlight round the matches; ctrl-n and ctrl-p too");
    ui_picker_key(&pk, kk(KEY_UP, 0));
    CHECK_EQ(pk.sel, 3);
    ui_picker_key(&pk, kc('n', MOD_CTRL));
    CHECK_EQ(pk.sel, 0);
    ui_picker_key(&pk, kk(KEY_DOWN, 0));
    ui_picker_key(&pk, kc('p', MOD_CTRL));
    ui_picker_key(&pk, kk(KEY_DOWN, 0));
    CHECK_EQ(pk.sel, 1);
    CHECK_EQ(ui_picker_key(&pk, kk(KEY_ENTER, 0)), 1);
    CHECK(!strcmp(pk.items[ui_picker_chosen(&pk)].name, "ogre-chief"));

    CASE("with nothing matching, enter waits and tab does nothing; esc cancels");
    ui_picker_key(&pk, kc('u', MOD_CTRL));
    ui_picker_key(&pk, kc('z', 0));
    CHECK_EQ(pk.nmatch, 0);
    CHECK_EQ(ui_picker_chosen(&pk), -1);
    CHECK_EQ(ui_picker_key(&pk, kk(KEY_ENTER, 0)), 0);
    CHECK_EQ(ui_picker_key(&pk, kk(KEY_TAB, 0)), 0);
    CHECK(!strcmp(pk.p.buf, "z"));
    CHECK_EQ(ui_picker_key(&pk, kk(KEY_ESC, 0)), -1);

    CASE("opened with text typed in, the list is already narrowed");
    fx = pick_fixture(&n);
    ui_picker_open(&pk, "Character", fx, n, "gh");
    CHECK_EQ(pk.nmatch, 1);

    CASE("drawn: the title, the text, the rows, the count");
    Renderer r;
    rnd_init(&r);
    rnd_resize(&r, 80, 24);
    rnd_begin(&r);
    ui_picker_draw(&r, &THEME_DARK, &pk, &BOX_ROUND);
    ByteBuf f;
    bb_init(&f, 8192);
    rnd_dump(&r, &f);
    bb_putc(&f, '\0');
    CHECK(strstr((char *)f.data, "Character") != NULL);
    CHECK(strstr((char *)f.data, "> ghost") != NULL);
    CHECK(strstr((char *)f.data, "Pale Ghost") != NULL);
    CHECK(strstr((char *)f.data, "1 of 5") != NULL);
    bb_free(&f);

    CASE("a list longer than the box scrolls to keep the highlight on screen");
    UiPickItem *many = xmalloc(sizeof *many * 25);
    for (int i = 0; i < 25; i++) {
        snprintf(many[i].name, sizeof many[i].name, "item%02d", i);
        many[i].detail[0] = '\0';
    }
    ui_picker_open(&pk, "Stamp", many, 25, "");
    for (int i = 0; i < 23; i++) ui_picker_key(&pk, kk(KEY_DOWN, 0));
    rnd_begin(&r);
    ui_picker_draw(&r, &THEME_DARK, &pk, &BOX_ROUND);
    bb_init(&f, 8192);
    rnd_dump(&r, &f);
    bb_putc(&f, '\0');
    CHECK(strstr((char *)f.data, "> item23") != NULL);
    CHECK(strstr((char *)f.data, "item00") == NULL);
    bb_free(&f);
    rnd_free(&r);
    ui_picker_free(&pk);
}

/* ---------------------------------------------------- character templates */

static Token template_ghoul(void)
{
    Token t;
    memset(&t, 0, sizeof t);
    t.kind = TOKEN_ENEMY;
    t.size = 2;
    str_lcpy(t.label, "Crypt Ghoul", sizeof t.label);
    str_lcpy(t.note, "hates light", sizeof t.note);
    t.ncounters = 2;
    str_lcpy(t.counters[0].name, "HP", sizeof t.counters[0].name);
    t.counters[0].value = 3; t.counters[0].max = 12;
    str_lcpy(t.counters[1].name, "Stress", sizeof t.counters[1].name);
    t.counters[1].value = 0; t.counters[1].max = 3;
    t.nstatus = 1;
    str_lcpy(t.status[0].label, "burning", sizeof t.status[0].label);
    t.turn = TURN_IN | TURN_ACTING;
    t.init = 14;
    t.hidden = 1;
    return t;
}

static void set_roll(Map *m, int slot, const char *name, const char *expr)
{
    str_lcpy(m->rolls[slot].name, name, sizeof m->rolls[slot].name);
    str_lcpy(m->rolls[slot].expr, expr, sizeof m->rolls[slot].expr);
}

void test_characters(void)
{
    Sandbox sb = sandbox_enter("characters");
    CHECK_EQ(sb.ok, 1);
    if (!sb.ok) return;
    Renderer r;
    App      a;
    rnd_init(&r);
    rnd_resize(&r, 100, 30);
    app_init(&a, NULL, &r);
    CHECK(ctl_blank_map(&a, sb.dir, 12, 8));
    if (!a.map) { app_free(&a); rnd_free(&r); sandbox_leave(&sb); return; }
    Map *m = a.map;
    Key f2 = { KEY_F2, 0, 0 };
    char err[160];

    CASE("no characters yet: the picker says how to make one");
    app_key(&a, f2);
    press(&a, "ite");
    CHECK_EQ(a.modal, MODAL_NONE);
    CHECK(strstr(a.status, "no characters saved yet") != NULL);

    CASE(":character is play mode's");
    app_key(&a, (Key){ KEY_F1, 0, 0 });
    press(&a, ":character\r");
    CHECK(strstr(a.status, "play mode's") != NULL);
    app_key(&a, f2);

    CASE(":character save with no creature says so");
    a.ed.cx = 6; a.ed.cy = 6;
    press(&a, ":character save\r");
    CHECK(strstr(a.status, "no creature here") != NULL);

    CASE(":character save keeps it fresh: counters full, no markers, no turn, not hidden");
    Token g = template_ghoul();
    g.x = 1; g.y = 1;
    tokens_add(&m->tokens, g);
    set_roll(m, 0, "claw", "1d8+2");
    set_roll(m, 1, "bite", "2d6");
    set_roll(m, 2, "howl", "d4");
    a.ed.cx = 1; a.ed.cy = 1;
    a.play.sel = -1;
    press(&a, ":character save ghoul claw BITE\r");
    CHECK(strstr(a.status, "saved Crypt Ghoul as character ghoul with its rolls") != NULL);
    Map *c = character_load("ghoul", err, sizeof err);
    CHECK(c != NULL);
    if (c) {
        const Token *t = character_token(c);
        CHECK(!strcmp(t->label, "Crypt Ghoul") && t->size == 2 && t->kind == TOKEN_ENEMY);
        CHECK(!strcmp(t->note, "hates light"));
        CHECK(t->ncounters == 2 && t->counters[0].value == 12 && t->counters[1].value == 3);
        CHECK(t->nstatus == 0 && t->turn == 0 && t->init == 0 && t->hidden == 0);
        CHECK(t->x == 0 && t->y == 0 && c->w == 2 && c->h == 2);
        int nr = 0;
        for (int i = 0; i < ROLL_MAX; i++) if (c->rolls[i].name[0]) nr++;
        CHECK_EQ(nr, 2);                                /* claw and bite, not howl */
        map_free(c);
    }

    CASE("the default name is the label without its number, made a file name");
    m->tokens.v[0].turn = 0;
    str_lcpy(m->tokens.v[0].label, "Crypt Ghoul 2", sizeof m->tokens.v[0].label);
    press(&a, ":character save\r");
    CHECK(strstr(a.status, "as character Crypt-Ghoul") != NULL);
    c = character_load("Crypt-Ghoul", err, sizeof err);
    CHECK(c && !strcmp(character_token(c)->label, "Crypt Ghoul"));   /* saved without its number */
    map_free(c);
    char nm[MAP_NAME_MAX];
    character_name_from_label("  Mr. O'Neil 12", nm, sizeof nm);
    CHECK(!strcmp(nm, "Mr-O-Neil"));
    character_name_from_label("7", nm, sizeof nm);
    CHECK(!strcmp(nm, "7"));
    character_name_from_label("!!", nm, sizeof nm);
    CHECK(!strcmp(nm, ""));

    CASE("a roll the map does not have, or a bad name, saves nothing");
    press(&a, ":character save ghast spit\r");
    CHECK(strstr(a.status, "no roll called spit") != NULL);
    CHECK(character_load("ghast", err, sizeof err) == NULL);
    press(&a, ":character save ../ghast\r");
    CHECK(strstr(a.status, "letters, digits") != NULL);
    press(&a, ":character save a b c d e f g h i j\r");
    CHECK(strstr(a.status, "at most eight rolls") != NULL);
    press(&a, ":character save a b c\r");
    CHECK(strstr(a.status, "no roll called b") != NULL);

    CASE("i t e: the picker, narrowed as typed; enter places it at the cursor as an enemy");
    memset(m->rolls, 0, sizeof m->rolls);
    set_roll(m, 0, "claw", "1d8+2");                /* the same; bite is missing */
    a.ed.cx = 5; a.ed.cy = 3;
    int depth = a.undo.depth;
    press(&a, "it");
    CHECK_EQ(a.pending, PENDING_IT);
    CHECK(strstr(a.status, "p as a player") != NULL);
    press(&a, "e");
    CHECK_EQ(a.modal, MODAL_PICKER);
    CHECK(strstr(a.picker.p.title, "as an enemy") != NULL);
    CHECK_EQ(a.picker.n, 2);
    CHECK(strstr(a.picker.items[1].detail, "Crypt Ghoul  enemy 2x2  HP 12, Stress 3") != NULL);
    press(&a, "gh");
    CHECK_EQ(a.picker.nmatch, 2);                   /* ghoul by name, Crypt-Ghoul by label */
    CHECK(!strcmp(a.picker.items[a.picker.match[0]].name, "ghoul"));
    press(&a, "\r");
    CHECK_EQ(a.modal, MODAL_NONE);
    CHECK_EQ(m->tokens.n, 2);
    const Token *p = &m->tokens.v[1];
    CHECK(p->x == 5 && p->y == 3 && p->size == 2 && p->kind == TOKEN_ENEMY);
    CHECK(!strcmp(p->label, "Crypt Ghoul"));          /* the first one is Crypt Ghoul 2 now */
    CHECK(p->counters[0].value == 12 && !p->hidden && p->nstatus == 0);
    CHECK_EQ(a.play.sel, 1);
    CHECK(strstr(a.status, "placed enemy Crypt Ghoul (2x2) at F4 from ghoul") != NULL);
    CHECK_EQ(a.status_gm, 1);                          /* the template's name is the GM's */
    CHECK(!strcmp(m->rolls[1].name, "bite") && !strcmp(m->rolls[1].expr, "2d6"));
    CHECK_EQ(a.undo.depth, depth + 1);

    CASE("u takes back the creature and the roll it added; redo puts both back");
    press(&a, "u");
    CHECK_EQ(m->tokens.n, 1);
    CHECK(!m->rolls[1].name[0]);
    CHECK(!strcmp(m->rolls[0].name, "claw"));
    undo_redo(&a.undo, m);
    CHECK_EQ(m->tokens.n, 2);
    CHECK(!strcmp(m->rolls[1].name, "bite"));

    CASE("i t p places the same template as a player; labels are numbered");
    a.ed.cx = 8; a.ed.cy = 5;
    press(&a, "itpghoul\r");
    CHECK_EQ(m->tokens.n, 3);
    CHECK(m->tokens.v[2].kind == TOKEN_PLAYER && !strcmp(m->tokens.v[2].label, "Crypt Ghoul 3"));

    CASE("a roll the map has by the same name but differently is kept, and said");
    set_roll(m, 0, "claw", "1d4");
    a.ed.cx = 0; a.ed.cy = 6;
    press(&a, ":character ghoul\r");
    CHECK_EQ(a.modal, MODAL_PICKER);
    CHECK(!strcmp(a.picker.p.buf, "ghoul"));
    press(&a, "\r");
    CHECK_EQ(m->tokens.n, 4);
    CHECK_EQ(m->tokens.v[3].kind, TOKEN_ENEMY);       /* :character: the saved side */
    CHECK(strstr(a.status, "kept this map's claw = 1d4") != NULL);
    CHECK(!strcmp(m->rolls[0].expr, "1d4"));

    CASE("no room: refused by the template's size, nothing placed");
    a.ed.cx = 6; a.ed.cy = 3;                          /* the enemy at F4 covers G4 */
    press(&a, "iteghoul\r");
    CHECK_EQ(m->tokens.n, 4);
    CHECK(strstr(a.status, "no room for a 2x2 Crypt Ghoul at G4") != NULL);
    a.ed.cx = 11; a.ed.cy = 7;                         /* a 2x2 off the edge */
    press(&a, "iteghoul\r");
    CHECK_EQ(m->tokens.n, 4);
    CHECK(strstr(a.status, "no room for a 2x2 Crypt Ghoul at L8") != NULL);

    CASE("save takes the creature under the cursor over the selected one");
    a.play.sel = 3;                                    /* the last one placed */
    a.ed.cx = 8; a.ed.cy = 5;                          /* on the player at I6 */
    press(&a, ":character save pick\r");
    Map *pc = character_load("pick", err, sizeof err);
    CHECK(pc && character_token(pc)->kind == TOKEN_PLAYER);
    map_free(pc);
    a.ed.cx = 11; a.ed.cy = 0;                         /* nobody here: the selected one */
    press(&a, ":character save pick\r");
    pc = character_load("pick", err, sizeof err);
    CHECK(pc && character_token(pc)->kind == TOKEN_ENEMY);
    map_free(pc);

    CASE("a name too long is refused, not cut short; a derived name never ends in a dash");
    press(&a, ":character save aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\r");
    CHECK(strstr(a.status, "under 64 characters") != NULL);
    char nm2[8];
    character_name_from_label("abcd efgh", nm2, sizeof nm2);   /* cut at "abcd-e": no trailing dash */
    CHECK(nm2[strlen(nm2) - 1] != '-');
    character_name_from_label("abcde fg", nm2, 7);
    CHECK(!strcmp(nm2, "abcde"));

    CASE("esc cancels the picker; a wrong key after i t says what it wants");
    press(&a, "ite");
    press(&a, "\x1b");
    CHECK_EQ(a.modal, MODAL_NONE);
    CHECK(strstr(a.status, "canceled") != NULL);
    press(&a, "itx");
    CHECK(strstr(a.status, "i t wants p") != NULL);
    press(&a, "ix");
    CHECK(strstr(a.status, "t for a saved character") != NULL);

    CASE("the picker is the GM's: never in the players' frame, and the frame is drawn apart");
    press(&a, "ite");
    CHECK(app_view_differs(&a));
    rnd_begin(&r);
    app_draw_view(&a, VIEW_GM);
    ByteBuf gf;
    bb_init(&gf, 65536);
    rnd_dump(&r, &gf);
    bb_putc(&gf, '\0');
    CHECK(strstr(gf.data, "Character") != NULL);             /* on the GM's screen... */
    CHECK(strstr(gf.data, "Crypt Ghoul  enemy") != NULL);
    bb_free(&gf);
    char *pf = players_text(&a, &r);                         /* ...and not the players' */
    CHECK(strstr(pf, "Character") == NULL);
    CHECK(strstr(pf, "Crypt Ghoul  enemy") == NULL);
    free(pf);
    press(&a, "\x1b");

    CASE("a file that is not one creature is refused by name, and the picker says it cannot be read");
    char dir[MAP_PATH_MAX], path[MAP_PATH_MAX + 16];
    character_dir(dir, sizeof dir);
    snprintf(path, sizeof path, "%s/pair.vtt", dir);
    FILE *f = fopen(path, "w");
    if (f) {
        fputs("VTT 3\nname pair\nsize 2 1\ntiles\n..\ntoken enemy 0 0 1 \"A\"\ntoken enemy 1 0 1 \"B\"\n", f);
        fclose(f);
    }
    CHECK(character_load("pair", err, sizeof err) == NULL);
    CHECK(strstr(err, "holds 2 creatures") != NULL);
    press(&a, ":character\r");
    CHECK_EQ(a.picker.n, 4);                         /* Crypt-Ghoul, ghoul, pair, pick */
    CHECK(!strcmp(a.picker.items[2].name, "pair") && !strcmp(a.picker.items[2].detail, "cannot be read"));
    press(&a, "pair\r");
    CHECK_EQ(m->tokens.n, 4);
    CHECK(strstr(a.status, "holds 2 creatures") != NULL);

    CASE("the stamp picker holds the channel off while it is open");
    app_key(&a, (Key){ KEY_F1, 0, 0 });
    Map *st = stamp_copy(m, 0, 0, 1, 0);
    CHECK(stamp_save(st, "bit", err, sizeof err) == 0);
    map_free(st);
    press(&a, ":stamp\r");
    CHECK_EQ(a.modal, MODAL_PICKER);
    CHECK(app_ctl_busy(&a) && strstr(app_ctl_busy(&a), "choosing from a list") != NULL);
    press(&a, "\x1b");
    CHECK(app_ctl_busy(&a) == NULL);

    app_free(&a);
    rnd_free(&r);
    sandbox_leave(&sb);
}

/* ------------------------------------------------------------ :scene keys */

void test_scene_keys(void)
{
    Sandbox sb = sandbox_enter("scenekeys");
    CHECK_EQ(sb.ok, 1);
    if (!sb.ok) return;
    Renderer r;
    App      a;
    rnd_init(&r);
    rnd_resize(&r, 100, 30);
    app_init(&a, NULL, &r);
    CHECK(ctl_blank_map(&a, sb.dir, 12, 8));
    if (!a.map) { app_free(&a); rnd_free(&r); sandbox_leave(&sb); return; }
    Map *m = a.map;
    Key f2 = { KEY_F2, 0, 0 };
    app_key(&a, f2);
    a.ed.cx = 1; a.ed.cy = 1; press(&a, "ipAria\r");
    a.ed.cx = 5; a.ed.cy = 5; press(&a, "ieOgre\r");

    CASE("no scenes: :scene and :scenes say how to make one");
    press(&a, ":scene\r");
    CHECK_EQ(a.modal, MODAL_NONE);
    CHECK(strstr(a.status, "no scenes") != NULL);
    press(&a, ":scenes\r");
    CHECK(strstr(a.status, "no scenes") != NULL);

    CASE(":scene save NAME keeps them; the message is the GM's alone");
    press(&a, ":scene save before the ambush\r");
    CHECK_EQ(m->nscenes, 1);
    CHECK(strstr(a.status, "scene before the ambush saved - 2 creatures") != NULL);
    CHECK_EQ(a.status_gm, 1);
    CHECK(app_view_differs(&a));
    char *pf = players_text(&a, &r);
    CHECK(strstr(pf, "ambush") == NULL);
    free(pf);

    CASE("move them, then :scene NAME puts them back, one u away; the selection is cleared");
    a.ed.cx = 1; a.ed.cy = 1;
    press(&a, "\rlll\r");
    CHECK_EQ(m->tokens.v[0].x, 4);
    press(&a, "t");
    CHECK(a.play.sel >= 0);
    press(&a, ":scene before the ambush\r");
    CHECK_EQ(m->tokens.v[0].x, 1);
    CHECK_EQ(a.play.sel, -1);
    CHECK(strstr(a.status, "scene before the ambush is back - 2 creatures") != NULL);
    CHECK_EQ(a.status_gm, 1);
    press(&a, "u");
    int moved = 0;
    for (int i = 0; i < m->tokens.n; i++) moved |= !strcmp(m->tokens.v[i].label, "Aria") && m->tokens.v[i].x == 4;
    CHECK(moved);

    CASE(":scene alone is the picker; enter puts the highlighted one back");
    press(&a, ":scene\r");
    CHECK_EQ(a.modal, MODAL_PICKER);
    CHECK(!strcmp(a.picker.items[0].name, "before the ambush"));
    CHECK(!strcmp(a.picker.items[0].detail, "2 creatures"));
    press(&a, "amb\r");
    CHECK_EQ(a.modal, MODAL_NONE);
    int back = 0;
    for (int i = 0; i < m->tokens.n; i++) back |= !strcmp(m->tokens.v[i].label, "Aria") && m->tokens.v[i].x == 1;
    CHECK(back);

    CASE("a v box saves only what is in it, and says the box");
    a.ed.cx = 4; a.ed.cy = 4;
    press(&a, "vll");
    CHECK(a.play.visual);
    press(&a, "jj:scene save Ogre corner\r");
    CHECK_EQ(a.play.visual, 0);
    int oc = scene_find(m, "ogre corner");
    CHECK(oc >= 0 && m->scenes[oc].boxed && m->scenes[oc].tokens.n == 1);
    CHECK(strstr(a.status, "1 creature, E5:G7") != NULL);

    CASE(":scenes lists them");
    press(&a, ":scenes\r");
    CHECK(strstr(a.status, "scenes: before the ambush (2 creatures), Ogre corner (1 creature, E5:G7)") != NULL);

    CASE("refused while a creature is carried");
    a.ed.cx = 1; a.ed.cy = 1;
    press(&a, "\r");
    CHECK(a.play.grabbed);
    press(&a, ":scene before the ambush\r");
    CHECK(strstr(a.status, "put the creature down first") != NULL);
    press(&a, ":scene save mid walk\r");
    CHECK(strstr(a.status, "put the creature down first") != NULL);
    CHECK_EQ(scene_find(m, "mid walk"), -1);
    press(&a, "\x1b");

    CASE("spaces round and between the words do not matter");
    press(&a, ":scene   before   the ambush  \r");
    CHECK(strstr(a.status, "scene before the ambush is back") != NULL);

    CASE("remove throws one away; off only says what to type; an unknown name says so");
    press(&a, ":scene Ogre corner off\r");
    CHECK_EQ(m->nscenes, 2);
    CHECK(strstr(a.status, ":scene Ogre corner remove") != NULL);
    press(&a, ":scene Ogre corner remove\r");
    CHECK_EQ(m->nscenes, 1);
    CHECK(strstr(a.status, "scene Ogre corner removed") != NULL);
    press(&a, ":scene nowhere\r");
    CHECK(strstr(a.status, "no scene called nowhere") != NULL);
    press(&a, ":scene save\r");
    CHECK(strstr(a.status, ":scene save NAME") != NULL);

    CASE("build mode too, with its v box");
    app_key(&a, (Key){ KEY_F1, 0, 0 });
    a.ed.cx = 0; a.ed.cy = 0;
    press(&a, "vjj:scene save west\r");
    int w = scene_find(m, "west");
    CHECK(w >= 0 && m->scenes[w].boxed && m->scenes[w].x1 == 0 && m->scenes[w].y1 == 2);
    CHECK_EQ(a.ed.mode, ED_NORMAL);

    app_free(&a);
    rnd_free(&r);
    sandbox_leave(&sb);
}

/* ---------------------------------------------------------------- handouts */

static void write_text(const char *path, const char *text, size_t n)
{
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fwrite(text, 1, n, f);
    fclose(f);
}

void test_handout_keys(void)
{
    Sandbox sb = sandbox_enter("handoutkeys");
    CHECK_EQ(sb.ok, 1);
    if (!sb.ok) return;
    Renderer r;
    App      a;
    rnd_init(&r);
    rnd_resize(&r, 100, 30);
    app_init(&a, NULL, &r);
    CHECK(ctl_blank_map(&a, sb.dir, 12, 8));
    if (!a.map) { app_free(&a); rnd_free(&r); sandbox_leave(&sb); return; }
    app_key(&a, (Key){ KEY_F2, 0, 0 });
    char dir[MAP_PATH_MAX], path[MAP_PATH_MAX + 32];
    store_dir("handouts", dir, sizeof dir);

    CASE("none yet: :handout says where to write one");
    press(&a, ":handout\r");
    CHECK_EQ(a.modal, MODAL_NONE);
    CHECK(strstr(a.status, "no handouts - write NAME.txt in") != NULL);
    press(&a, ":handout on\r");
    CHECK(strstr(a.status, "no handout yet") != NULL);
    press(&a, ":handout off\r");
    CHECK(strstr(a.status, "no handout is up") != NULL);

    CASE(":handout NAME puts the file up: the name its title, CRLF and tabs cleaned, blank tail dropped");
    dir_make(dir);
    snprintf(path, sizeof path, "%s/tomb.txt", dir);
    const char *tomb = "Here lies Aldric.\r\n\r\n\tDo not open the door.\r\n\r\n";
    write_text(path, tomb, strlen(tomb));
    press(&a, ":handout tomb\r");
    CHECK_EQ(a.handout_up, 1);
    CHECK(!strcmp(a.handout_title, "tomb"));
    CHECK(!strcmp(a.handout_body, "Here lies Aldric.\n\n Do not open the door."));
    CHECK(strstr(a.status, "handout up: tomb") != NULL);
    CHECK_EQ(a.status_gm, 1);
    CHECK((int)a.net.handout_len == (int)strlen("tomb\nHere lies Aldric.\n\n Do not open the door."));

    CASE("the title bar says so, in both views");
    rnd_begin(&r);
    app_draw(&a);
    ByteBuf f;
    bb_init(&f, 65536);
    rnd_dump(&r, &f);
    bb_putc(&f, '\0');
    CHECK(strstr((char *)f.data, "HANDOUT  PLAY") != NULL);
    CHECK(strstr((char *)f.data, "Here lies") == NULL);        /* the GM's own screen has no card */
    bb_free(&f);
    char *pf = players_text(&a, &r);
    CHECK(strstr(pf, "HANDOUT  PLAY") != NULL);
    CHECK(strstr(pf, "Here lies") == NULL);                  /* the phones draw their own */
    free(pf);

    CASE(":player preview shows the card, and the players' frame is then drawn apart");
    press(&a, ":player preview\r");
    CHECK(a.preview);
    CHECK(app_view_differs(&a));
    rnd_begin(&r);
    app_draw(&a);
    bb_init(&f, 65536);
    rnd_dump(&r, &f);
    bb_putc(&f, '\0');
    CHECK(strstr((char *)f.data, "Here lies Aldric.") != NULL);
    CHECK(strstr((char *)f.data, "tomb") != NULL);
    bb_free(&f);
    press(&a, ":player preview\r");
    CHECK(!a.preview);

    CASE("off takes it down and keeps it; on puts it back");
    press(&a, ":handout off\r");
    CHECK_EQ(a.handout_up, 0);
    CHECK_EQ((int)a.net.handout_len, 0);
    CHECK(strstr(a.status, ":handout on puts it back") != NULL);
    press(&a, ":handout on\r");
    CHECK_EQ(a.handout_up, 1);
    CHECK(strstr(a.status, "handout up again: tomb") != NULL);

    CASE(":handout say puts a line up with no title");
    press(&a, ":handout say SPEAK, FRIEND\r");
    CHECK(a.handout_up && !a.handout_title[0] && !strcmp(a.handout_body, "SPEAK, FRIEND"));
    CHECK(a.net.handout_len == strlen("\nSPEAK, FRIEND"));
    press(&a, ":handout say\r");
    CHECK(strstr(a.status, ":handout say TEXT") != NULL);

    CASE("refused: no such file, too long, not UTF-8, empty, a path");
    press(&a, ":handout nope\r");
    CHECK(strstr(a.status, "no handout called nope") != NULL);
    char *big = xmalloc(3000);
    memset(big, 'x', 3000);
    snprintf(path, sizeof path, "%s/big.txt", dir);
    write_text(path, big, 3000);
    free(big);
    press(&a, ":handout big\r");
    CHECK(strstr(a.status, "over 2048 bytes") != NULL);
    snprintf(path, sizeof path, "%s/latin.txt", dir);
    write_text(path, "caf\xe9", 4);
    press(&a, ":handout latin\r");
    CHECK(strstr(a.status, "not UTF-8") != NULL);
    snprintf(path, sizeof path, "%s/blank.txt", dir);
    write_text(path, "\n\n", 2);
    press(&a, ":handout blank\r");
    CHECK(strstr(a.status, "is empty") != NULL);
    press(&a, ":handout ../tomb\r");
    CHECK(strstr(a.status, "no handout called") != NULL);
    CHECK(!strcmp(a.handout_body, "SPEAK, FRIEND"));          /* none of those replaced it */

    CASE("what the GM types after : is never in the players' frame");
    press(&a, ":handout say SECRET WORDS");
    CHECK_EQ(a.ed.mode, ED_COMMAND);
    CHECK(app_view_differs(&a));
    pf = players_text(&a, &r);
    CHECK(strstr(pf, "SECRET") == NULL);
    free(pf);
    press(&a, "\x1b");

    CASE("the picker: every file, its first line beside it; enter puts it up");
    press(&a, ":handout\r");
    CHECK_EQ(a.modal, MODAL_PICKER);
    CHECK_EQ(a.picker.n, 4);
    int ti = -1;
    for (int i = 0; i < a.picker.n; i++) if (!strcmp(a.picker.items[i].name, "tomb")) ti = i;
    CHECK(ti >= 0 && !strcmp(a.picker.items[ti].detail, "Here lies Aldric."));
    press(&a, "tom\r");
    CHECK(!strcmp(a.handout_title, "tomb"));

    CASE("the card wraps to its box, keeps line breaks, and says when it is cut short");
    Renderer small;
    rnd_init(&small);
    rnd_resize(&small, 40, 12);
    rnd_begin(&small);
    ui_handout_draw(&small, &THEME_DARK, "Letter",
                    "a b c d e f g h i j k l m n o p q r s t u v w x y z aa bb cc dd\n"
                    "Supercalifragilisticexpialidocious-and-more\nline\nline\nline\nline\nline\nline\nline",
                    &BOX_ROUND);
    bb_init(&f, 8192);
    rnd_dump(&small, &f);
    bb_putc(&f, '\0');
    CHECK(strstr((char *)f.data, "Letter") != NULL);
    CHECK(strstr((char *)f.data, "a b c d") != NULL);
    CHECK(strstr((char *)f.data, "…") != NULL);               /* nine lines do not fit in twelve rows' box */
    CHECK(strstr((char *)f.data, "Supercalifragilistic") != NULL);   /* a long word is cut, not lost: */
    CHECK(strstr((char *)f.data, "-and-more") != NULL);              /* its end is on the next line */
    bb_free(&f);
    rnd_free(&small);

    CASE("closing the map takes the handout down and forgets it");
    CHECK(a.handout_up);
    press(&a, ":q!\r");
    CHECK(a.map == NULL);
    CHECK(!a.handout_up && !a.handout_body[0] && a.net.handout_len == 0);

    app_free(&a);
    rnd_free(&r);
    sandbox_leave(&sb);
}
