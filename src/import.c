/* Imports: other people's files made vtt's own (docs/CARDS.md). */

#include "import.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "card.h"
#include "character.h"
#include "counter.h"
#include "json.h"
#include "map.h"
#include "mapio.h"
#include "store.h"
#include "util.h"

/* A field as text, whether the file wrote it as a string or a number. */
static const char *field(const JsonVal *o, const char *key, char *buf, size_t sz)
{
    const JsonVal *v = json_get(o, key);
    if (!v) return "";
    if (v->kind == JSON_STR) return v->str;
    if (v->kind == JSON_NUM) { snprintf(buf, sz, "%g", v->num); return buf; }
    return "";
}

/* The stat block as a card, in the order the SRD prints it; a line whose
 * fields are all missing is left out. */
static void card_text(const JsonVal *o, ByteBuf *b)
{
    char t1[32], t2[32], t3[32], t4[32], t5[32], t6[32], line[1024];
    const char *name = field(o, "name", t1, sizeof t1), *tier = field(o, "tier", t2, sizeof t2);
    const char *type = field(o, "type", t3, sizeof t3);
    /* "Acid Burrower - Tier 1 Solo" */
    snprintf(line, sizeof line, "%s%s%s%s%s%s\n", name, tier[0] || type[0] ? " -" : "",
             tier[0] ? " Tier " : "", tier, type[0] ? " " : "", type);
    bb_puts(b, line);
    const char *desc = field(o, "description", t4, sizeof t4);
    if (desc[0]) { bb_puts(b, desc); bb_putc(b, '\n'); }
    const char *mot = field(o, "motives_and_tactics", t4, sizeof t4);
    if (mot[0]) { bb_puts(b, "Motives: "); bb_puts(b, mot); bb_putc(b, '\n'); }

    const char *diff = field(o, "difficulty", t1, sizeof t1), *thr = field(o, "thresholds", t2, sizeof t2);
    const char *hp = field(o, "hp", t3, sizeof t3), *st = field(o, "stress", t5, sizeof t5);
    int off = 0;
    line[0] = '\0';
    if (diff[0]) off += snprintf(line + off, sizeof line - (size_t)off, "%sDifficulty: %s", off ? "   " : "", diff);
    if (thr[0])  off += snprintf(line + off, sizeof line - (size_t)off, "%sThresholds: %s", off ? "   " : "", thr);
    if (hp[0])   off += snprintf(line + off, sizeof line - (size_t)off, "%sHP: %s", off ? "   " : "", hp);
    if (st[0])   off += snprintf(line + off, sizeof line - (size_t)off, "%sStress: %s", off ? "   " : "", st);
    if (off) { bb_puts(b, line); bb_putc(b, '\n'); }

    const char *atk = field(o, "atk", t1, sizeof t1), *weap = field(o, "attack", t2, sizeof t2);
    const char *rng = field(o, "range", t3, sizeof t3), *dmg = field(o, "damage", t6, sizeof t6);
    if (atk[0] || weap[0]) {
        snprintf(line, sizeof line, "Attack: %s   %s%s%s%s%s%s\n", atk, weap, rng[0] ? " (" : "", rng,
                 rng[0] ? ")" : "", dmg[0] ? " " : "", dmg);
        bb_puts(b, line);
    }
    const char *exp = field(o, "experience", t4, sizeof t4);
    if (exp[0]) { bb_puts(b, "Experience: "); bb_puts(b, exp); bb_putc(b, '\n'); }

    const JsonVal *feats = json_get(o, "feature");
    for (int i = 0; feats && feats->kind == JSON_ARR && i < feats->n; i++) {
        const char *fn = json_text(json_get(&feats->kids[i], "name"));
        const char *ft = json_text(json_get(&feats->kids[i], "text"));
        if (!fn && !ft) continue;
        bb_putc(b, '\n');
        if (fn) bb_puts(b, fn);
        if (fn && ft) bb_puts(b, ": ");
        if (ft) bb_puts(b, ft);
    }
    bb_putc(b, '\0');
}

int import_adversaries(const char *path, int force, FILE *out, char *err, size_t errsz)
{
    size_t len = 0;
    int    big = 0;
    char  *text = file_read(path, (size_t)16 << 20, &len, &big);
    if (!text) { snprintf(err, errsz, big ? "%.200s is over 16 MB" : "cannot read %.200s", path); return -1; }
    char why[160];
    JsonVal *doc = json_parse(text, len, why, sizeof why);
    free(text);
    if (!doc) { snprintf(err, errsz, "%.200s: %s", path, why); return -1; }
    if (doc->kind != JSON_ARR) { json_free(doc); snprintf(err, errsz, "%.200s is not a list of adversaries", path); return -1; }

    int written = 0, kept = 0, skipped = 0;
    char dir[MAP_PATH_MAX];
    character_dir(dir, sizeof dir);
    for (int i = 0; i < doc->n; i++) {
        const JsonVal *o = &doc->kids[i];
        const char *label = json_text(json_get(o, "name"));
        char name[CARD_NAME_MAX];
        if (!label || !label[0]) { skipped++; continue; }
        character_name_from_label(label, name, sizeof name);
        if (!card_name_ok(name)) { fprintf(out, "skipped %s: its name makes no file name\n", label); skipped++; continue; }

        char path_out[MAP_PATH_MAX];
        if (!store_path("characters", name, ".vtt", path_out, sizeof path_out)) { skipped++; continue; }
        if (!force && access(path_out, F_OK) == 0) { kept++; continue; }

        /* One creature, 1x1, full, naming its card. */
        Map *m = map_new(1, 1, name);
        m->tiles[0] = TILE_FLOOR;
        Token t;
        memset(&t, 0, sizeof t);
        t.size = 1;
        t.kind = TOKEN_ENEMY;
        str_lcpy(t.label, label, sizeof t.label);
        char nb[32];
        int hp = atoi(field(o, "hp", nb, sizeof nb)), st = atoi(field(o, "stress", nb, sizeof nb));
        if (hp > 0) counter_set(&t, "HP", hp, hp);
        if (st > 0) counter_set(&t, "Stress", st, st);
        ByteBuf b;
        bb_init(&b, 2048);
        card_text(o, &b);
        card_set(m, name, (const char *)b.data);
        bb_free(&b);
        str_lcpy(t.card, name, sizeof t.card);
        tokens_add(&m->tokens, t);
        char e2[MAPIO_ERR_MAX];
        if (character_save(m, 0, name, NULL, 0, e2, sizeof e2) == 0) written++;
        else { fprintf(out, "skipped %s: %s\n", label, e2); skipped++; }
        map_free(m);
    }
    json_free(doc);
    fprintf(out, "imported %d adversar%s into %s", written, written == 1 ? "y" : "ies", dir);
    if (kept) fprintf(out, " - %d already there, kept (--force replaces them)", kept);
    if (skipped) fprintf(out, " - %d skipped", skipped);
    fputc('\n', out);
    return written;
}
