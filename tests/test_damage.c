/* :dmg (docs/DAMAGE.md): thresholds, resistance, massive damage, Minion and
 * Horde, a group effect's catch, undo, and the GM-only messages. */

#include "harness.h"

#include "card.h"

static const char *DMG_MAP =
    "VTT 13\nname dmg\nsize 30 12\nzoom 1\nruleset daggerheart\ntiles\n";

static void write_dmg_map(const char *path)
{
    FILE *f = fopen(path, "w");
    if (!f) return;
    fputs(DMG_MAP, f);
    for (int y = 0; y < 12; y++) fputs("..............................\n", f);
    fputs("token player 1 1 1 \"Aria\"\ntokencounter HP 6 6\n"
          "token enemy 5 5 1 \"Goblin\"\ntokencounter HP 5 5\ntokencard \"goblin\"\n"
          "token enemy 10 5 1 \"Rat 1\"\ntokencounter HP 1 1\ntokencard \"rat\"\n"
          "token enemy 12 5 1 \"Rat 2\"\ntokencounter HP 1 1\ntokencard \"rat\"\n"
          "token enemy 20 5 1 \"Rat 3\"\ntokencounter HP 1 1\ntokencard \"rat\"\n"
          "token enemy 25 5 1 \"Rat 4\"\ntokencounter HP 0 1\ntokencard \"rat\"\n"
          "token player 11 9 1 \"Pet\"\ntokencounter HP 1 1\ntokencard \"rat\"\n"
          "token enemy 5 9 1 \"Raiders\"\ntokencounter HP 4 4\ntokencard \"raiders\"\n"
          "token enemy 15 1 1 \"Nocard\"\ntokencounter HP 3 3\n"
          "token enemy 6 7 1 \"Blank\"\n"
          "token enemy 15 3 1 \"Plain\"\ntokencounter HP 3 3\ntokencard \"plain\"\n"
          "card \"goblin\"\n| Goblin - Tier 1 Standard\n| Difficulty: 11   Thresholds: 8/15   HP: 5\nendcard\n"
          "card \"rat\"\n| Giant Rat - Tier 1 Minion\n| Difficulty: 10   Thresholds: None   HP: 1\n|\n"
          "| Minion (3) - Passive: The Rat is defeated when they take any damage.\nendcard\n"
          "card \"raiders\"\n| Pirate Raiders - Tier 1 Horde (3/HP)\n| Thresholds: 5/11\n|\n"
          "| Horde (1d4+1) - Passive: When the Raiders have marked half or more of their HP...\nendcard\n"
          "card \"plain\"\n| Just words\nendcard\n", f);
    fclose(f);
}

static int find(const App *a, const char *label)
{
    for (int i = 0; i < a->map->tokens.n; i++)
        if (!strcmp(a->map->tokens.v[i].label, label)) return i;
    return -1;
}

static int hp(const App *a, const char *label)
{
    const Token *t = &a->map->tokens.v[find(a, label)];
    int c = counter_find(t, "HP");
    return c < 0 ? -1 : t->counters[c].value;
}

/* :dmg on one creature, selected. */
static void dmg_on(App *a, const char *label, const char *cmd)
{
    a->play.sel = find(a, label);
    press(a, cmd);
}

static void test_damage_rule(void)
{
    CASE("the SRD's thresholds: below Major 1, Major 2, Severe 3, twice Severe 4 with massive damage");
    const char *tier = NULL;
    CHECK_EQ(damage_marks(0, 8, 15, 0, &tier), 0);
    CHECK(!strcmp(tier, "no damage"));
    CHECK_EQ(damage_marks(-3, 8, 15, 0, NULL), 0);
    CHECK_EQ(damage_marks(1, 8, 15, 0, NULL), 1);
    CHECK_EQ(damage_marks(7, 8, 15, 0, &tier), 1);
    CHECK(!strcmp(tier, "below Major"));
    CHECK_EQ(damage_marks(8, 8, 15, 0, &tier), 2);
    CHECK(!strcmp(tier, "Major"));
    CHECK_EQ(damage_marks(14, 8, 15, 0, NULL), 2);
    CHECK_EQ(damage_marks(15, 8, 15, 0, &tier), 3);
    CHECK(!strcmp(tier, "Severe"));
    CHECK_EQ(damage_marks(30, 8, 15, 0, NULL), 3);
    CHECK_EQ(damage_marks(29, 8, 15, 1, NULL), 3);
    CHECK_EQ(damage_marks(30, 8, 15, 1, &tier), 4);
    CHECK(!strcmp(tier, "twice Severe"));

    CASE("a card's value by its label: case aside, up to two spaces, never inside a longer word");
    char v[32];
    const char *card = "Goblin\nDifficulty: 11   Thresholds: 8/15   HP: 5\nMotives: steal, run away";
    CHECK(card_value(card, "Thresholds", v, sizeof v) && !strcmp(v, "8/15"));
    CHECK(card_value(card, "thresholds", v, sizeof v) && !strcmp(v, "8/15"));
    CHECK(card_value(card, "Difficulty", v, sizeof v) && !strcmp(v, "11"));
    CHECK(card_value(card, "Motives", v, sizeof v) && !strcmp(v, "steal, run away"));
    CHECK(!card_value(card, "Holds", v, sizeof v) && !v[0]);
    CHECK(!card_value("MyThresholds: 1/2", "Thresholds", v, sizeof v));
    CHECK(!card_value(NULL, "Thresholds", v, sizeof v));
    CHECK(card_value("Thresholds: 123456789", "Thresholds", v, 5) && !strcmp(v, "1234"));

    CASE("a feature's X, bold or not; a word that only starts the same is not it");
    CHECK(card_feature("x\nMinion (3) - Passive: ...", "Minion", v, sizeof v) && !strcmp(v, "3"));
    CHECK(card_feature("**Horde (1d4+1)** - Passive", "Horde", v, sizeof v) && !strcmp(v, "1d4+1"));
    CHECK(!card_feature("Minions (3)", "Minion", v, sizeof v));
    CHECK(!card_feature("Minion (3", "Minion", v, sizeof v));
    CHECK(!card_feature("A Minion (3) mid-line", "Minion", v, sizeof v));

    CASE("thresholds read as Major/Severe; None, backwards or junk are none");
    const DamageRule *dr = ruleset_by_name("daggerheart")->damage;
    int major = 0, severe = 0;
    CHECK(damage_thresholds(dr, card, &major, &severe));
    CHECK(major == 8 && severe == 15);
    CHECK(damage_thresholds(dr, "Thresholds: 8 / 15", &major, &severe) && severe == 15);
    CHECK(!damage_thresholds(dr, "Thresholds: None", &major, &severe));
    CHECK(!damage_thresholds(dr, "Thresholds: 15/8", &major, &severe));
    CHECK(!damage_thresholds(dr, "Thresholds: 8/15x", &major, &severe));
    CHECK(!damage_thresholds(NULL, card, &major, &severe));
    CHECK(ruleset_by_name("none")->damage == NULL);

    CASE("a Minion's X; a Horde's attack only while half or more is marked and it is up");
    CHECK_EQ(damage_minion(dr, "Minion (4) - Passive"), 4);
    CHECK_EQ(damage_minion(dr, "Minion (0) - Passive"), 0);
    CHECK_EQ(damage_minion(dr, card), 0);
    const char *horde = "Horde (2d4+1) - Passive: ...";
    CHECK(!damage_horde_attack(dr, horde, 4, 4, v, sizeof v));
    CHECK(!damage_horde_attack(dr, horde, 3, 4, v, sizeof v));
    CHECK(damage_horde_attack(dr, horde, 2, 4, v, sizeof v) && !strcmp(v, "2d4+1"));
    CHECK(damage_horde_attack(dr, horde, 2, 5, v, sizeof v));    /* 3 of 5 marked */
    CHECK(!damage_horde_attack(dr, horde, 3, 5, v, sizeof v));   /* 2 of 5 */
    CHECK(!damage_horde_attack(dr, horde, 0, 4, v, sizeof v));
}

void test_damage(void)
{
    test_damage_rule();

    Sandbox sb = sandbox_enter("damage");
    CHECK_EQ(sb.ok, 1);
    if (!sb.ok) return;
    char path[600];
    snprintf(path, sizeof path, "%s/dmg.vtt", sb.dir);
    write_dmg_map(path);
    Renderer r;
    App      a;
    rnd_init(&r);
    rnd_resize(&r, 140, 30);
    app_init(&a, NULL, &r);
    CHECK_EQ(app_open_map(&a, path), 0);
    press(&a, ":play\r");
    a.ed.cx = 29; a.ed.cy = 11;                     /* on no one */

    CASE(":dmg 11 on the selected creature: Major by its card, why, one undo");
    dmg_on(&a, "Goblin", ":dmg 11\r");
    CHECK_EQ(hp(&a, "Goblin"), 3);
    CHECK(strstr(a.status, "Goblin: 11 is Major (8/15) - 2 HP marked, 3/5 left") != NULL);
    CHECK(a.status_gm);
    char *pf = players_text(&a, &r);
    CHECK(strstr(pf, "Major") == NULL);
    free(pf);
    press(&a, "u");
    CHECK_EQ(hp(&a, "Goblin"), 5);

    CASE("nothing selected: the creature under the cursor");
    a.play.sel = -1;
    a.ed.cx = 5; a.ed.cy = 5;
    press(&a, ":dmg 3\r");
    CHECK_EQ(hp(&a, "Goblin"), 4);
    press(&a, "u");
    a.ed.cx = 29; a.ed.cy = 11;
    a.play.sel = -1;
    press(&a, ":dmg 3\r");
    CHECK(strstr(a.status, "no creature here") != NULL);

    CASE("a sum, and resistance halving it before the thresholds, rounded up");
    dmg_on(&a, "Goblin", ":dmg 6+4\r");
    CHECK(strstr(a.status, "Goblin: 10 is Major") != NULL);
    press(&a, "u");
    dmg_on(&a, "Goblin", ":dmg 15 half \r");
    CHECK(strstr(a.status, "15 halved to 8 is Major") != NULL);
    CHECK_EQ(hp(&a, "Goblin"), 3);
    press(&a, "u");
    dmg_on(&a, "Goblin", ":dmg 0\r");
    CHECK(strstr(a.status, "marks no HP") != NULL);
    CHECK_EQ(hp(&a, "Goblin"), 5);

    CASE("enough to drop it says so; one already down is not marked again");
    dmg_on(&a, "Goblin", ":dmg 15+15\r");
    CHECK_EQ(hp(&a, "Goblin"), 2);
    dmg_on(&a, "Goblin", ":dmg 20\r");
    CHECK_EQ(hp(&a, "Goblin"), 0);
    CHECK(strstr(a.status, "2 HP marked, defeated (0/5)") != NULL);
    int stamp = (int)a.undo.stamp;
    dmg_on(&a, "Goblin", ":dmg 20\r");
    CHECK(strstr(a.status, "Goblin is already defeated (HP 0/5)") != NULL);
    CHECK_EQ((int)a.undo.stamp, stamp);
    press(&a, "uu");
    CHECK_EQ(hp(&a, "Goblin"), 5);

    CASE("massive damage: off by default, :dmg massive on makes twice Severe 4 HP, saved with the map");
    CHECK_EQ(a.map->massive, 0);
    dmg_on(&a, "Goblin", ":dmg 30\r");
    CHECK_EQ(hp(&a, "Goblin"), 2);
    press(&a, "u");
    press(&a, ":dmg massive on\r");
    CHECK_EQ(a.map->massive, 1);
    CHECK(a.map->modified);
    dmg_on(&a, "Goblin", ":dmg 30\r");
    CHECK_EQ(hp(&a, "Goblin"), 1);
    CHECK(strstr(a.status, "30 is twice Severe (8/15) - 4 HP marked") != NULL);
    press(&a, "u");
    press(&a, ":w\r");
    {
        char *text = slurp(path);
        CHECK(text && strstr(text, "\nrule massive\n") && !strncmp(text, "VTT 13\n", 7));
        free(text);
        char err[160];
        Map *m = mapio_load(path, err, sizeof err);
        CHECK(m && m->massive == 1);
        if (m) map_free(m);
    }
    press(&a, ":dmg massive\r");
    CHECK(strstr(a.status, "massive damage on") != NULL);
    press(&a, ":dmg massive off\r");
    CHECK_EQ(a.map->massive, 0);
    press(&a, ":dmg massive maybe\r");
    CHECK(strstr(a.status, ":dmg massive on, or :dmg massive off") != NULL);

    CASE("a Minion: any damage defeats it; every X more lists who could go, nearest first, its side only");
    dmg_on(&a, "Rat 1", ":dmg 7\r");
    CHECK_EQ(hp(&a, "Rat 1"), 0);
    CHECK(strstr(a.status, "Rat 1: 7 defeats it (Minion 3) - 7 damage defeats 2 more Minions "
                           "within the attack's range: Rat 2 (Very Close), Rat 3 (Far)") != NULL);
    CHECK(strstr(a.status, "Pet") == NULL);                 /* a player's */
    CHECK(strstr(a.status, "Rat 4") == NULL);               /* already down */
    CHECK_EQ(hp(&a, "Rat 2"), 1);                           /* listed, not marked */
    press(&a, "u");
    dmg_on(&a, "Rat 1", ":dmg 2\r");
    CHECK_EQ(hp(&a, "Rat 1"), 0);
    CHECK(strstr(a.status, "more Minion") == NULL);
    press(&a, "u");

    CASE("a Horde past half says its attack now, in the message and the card box's title, once");
    dmg_on(&a, "Raiders", ":dmg 6\r");
    CHECK_EQ(hp(&a, "Raiders"), 2);
    CHECK(strstr(a.status, "half its HP marked: its attack now deals 1d4+1") != NULL);
    {
        char note[24];
        CHECK(app_horde_note(&a, find(&a, "Raiders"), note, sizeof note) && !strcmp(note, "1d4+1"));
        CHECK(!app_horde_note(&a, find(&a, "Goblin"), note, sizeof note));
        rnd_begin(&r);
        app_draw_view(&a, VIEW_GM);
        ByteBuf gm;
        bb_init(&gm, 65536);
        rnd_dump(&r, &gm);
        bb_putc(&gm, '\0');
        CHECK(strstr(gm.data, "Raiders - attack now 1d4+1") != NULL);
        bb_free(&gm);
    }
    dmg_on(&a, "Raiders", ":dmg 1\r");
    CHECK_EQ(hp(&a, "Raiders"), 1);
    CHECK(strstr(a.status, "half its HP") == NULL);
    press(&a, "uu");
    CHECK_EQ(hp(&a, "Raiders"), 4);

    CASE("what is missing says how to add it, and changes nothing");
    stamp = (int)a.undo.stamp;
    dmg_on(&a, "Blank", ":dmg 5\r");
    CHECK(strstr(a.status, "no HP on Blank - s v sets it") != NULL);
    dmg_on(&a, "Nocard", ":dmg 5\r");
    CHECK(strstr(a.status, "Nocard has no card - s k writes one") != NULL);
    dmg_on(&a, "Plain", ":dmg 5\r");
    CHECK(strstr(a.status, "no thresholds on Plain's card - s k adds them") != NULL);
    CHECK(a.status_gm);
    CHECK_EQ((int)a.undo.stamp, stamp);
    CHECK_EQ(hp(&a, "Plain"), 3);

    CASE("words it does not know are refused with the forms it takes");
    static const char *const bad[] = { ":dmg\r", ":dmg x\r", ":dmg 5 fire\r", ":dmg 5+\r", ":dmg -3\r",
                                       ":dmg 100000\r", ":dmg 99999+1\r", ":dmg halfway\r", NULL };
    for (int i = 0; bad[i]; i++) {
        dmg_on(&a, "Goblin", bad[i]);
        CHECK(strstr(a.status, ":dmg 11, :dmg 6+4") != NULL);
    }
    CHECK_EQ((int)a.undo.stamp, stamp);
    dmg_on(&a, "Goblin", ":damage 8\r");
    CHECK_EQ(hp(&a, "Goblin"), 3);
    press(&a, "u");

    CASE("while g e is up: everyone caught, each by their own card, one message, one undo");
    a.play.sel = -1;
    a.ed.cx = 5; a.ed.cy = 7;
    press(&a, "ge");
    CHECK(a.play.range.burst);
    press(&a, ":dmg 9\r");
    CHECK_EQ(hp(&a, "Goblin"), 3);
    CHECK_EQ(hp(&a, "Raiders"), 2);
    CHECK_EQ(hp(&a, "Aria"), 6);
    CHECK(strstr(a.status, "9 damage to 3 caught: Goblin 2 HP (3/5), Raiders 2 HP (2/4), attack now 1d4+1; "
                           "skipped Blank (no HP)") != NULL);
    pf = players_text(&a, &r);
    CHECK(strstr(pf, "caught: Goblin 2 HP") == NULL);
    free(pf);
    press(&a, "u");
    CHECK_EQ(hp(&a, "Goblin"), 5);
    CHECK_EQ(hp(&a, "Raiders"), 4);

    CASE("a burst over Minions defeats them all and lists the ones beyond it");
    a.ed.cx = 11; a.ed.cy = 5;
    press(&a, ":dmg 3\r");
    CHECK_EQ(hp(&a, "Rat 1"), 0);
    CHECK_EQ(hp(&a, "Rat 2"), 0);
    CHECK_EQ(hp(&a, "Pet"), 1);                             /* 3 squares away: outside */
    CHECK(strstr(a.status, "Rat 1 defeated, Rat 2 defeated - 3 damage defeats 2 more Minions "
                           "within the attack's range: Rat 3 (Far)") != NULL);
    press(&a, "u");

    CASE("a burst over no one says so");
    a.ed.cx = 25; a.ed.cy = 10;
    press(&a, ":dmg 3\r");
    CHECK(strstr(a.status, "the group effect catches no one") != NULL);
    press(&a, "\x1b");
    CHECK(!a.play.range.active);

    CASE("without a ruleset with thresholds, damage comes off HP as it is");
    press(&a, ":ruleset none\r");
    dmg_on(&a, "Goblin", ":dmg 3\r");
    CHECK_EQ(hp(&a, "Goblin"), 2);
    CHECK(strstr(a.status, "Goblin: 3 damage - HP 2/5") != NULL);
    dmg_on(&a, "Rat 1", ":dmg 1\r");                        /* no Minion rule either */
    CHECK(strstr(a.status, "Rat 1: 1 damage - defeated (HP 0/1)") != NULL);
    dmg_on(&a, "Goblin", ":dmg 9\r");
    CHECK_EQ(hp(&a, "Goblin"), 0);

    app_free(&a);
    rnd_free(&r);
    sandbox_leave(&sb);
}
