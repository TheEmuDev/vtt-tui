/* :dmg (docs/DAMAGE.md): damage the GM has rolled at the table, marked on a
 * creature's HP -- by its card's thresholds under a ruleset that has them,
 * else taken off as it is -- and said why. The dice stay physical: the
 * number comes from the table. Every message is the GM's. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_priv.h"
#include "card.h"
#include "counter.h"
#include "prof.h"

/* A sum's parts and total are capped so the arithmetic never overflows and
 * nothing silly is printed. */
#define DMG_MAX 99999

enum { HIT_OK, HIT_NO_HP, HIT_NO_THRESHOLDS, HIT_DOWN };

typedef struct {
    int         idx;
    int         why;             /* HIT_* */
    int         hp;              /* the HP counter's index */
    int         before, after, max;
    int         major, severe;   /* 0 when not by thresholds */
    int         minion;          /* its X, or 0 */
    const char *tier;            /* damage_marks's word, or NULL */
    char        horde[24];       /* the attack it now has, when this hit took it past half */
} Hit;

static void hit_one(const Map *m, const DamageRule *dr, int idx, int dmg, Hit *h)
{
    const Token *t = &m->tokens.v[idx];
    memset(h, 0, sizeof *h);
    h->idx = idx;
    h->hp  = counter_find(t, "HP");
    if (h->hp < 0) { h->why = HIT_NO_HP; return; }
    h->before = h->after = t->counters[h->hp].value;
    h->max    = t->counters[h->hp].max;
    if (h->before <= 0) { h->why = HIT_DOWN; return; }

    const char *card = card_of(m, t);
    int marks;
    if (!dr) {
        marks = dmg;
    } else if ((h->minion = damage_minion(dr, card)) > 0) {
        marks   = dmg > 0 ? h->before : 0;          /* any damage defeats it */
        h->tier = dmg > 0 ? "Minion" : "no damage";
    } else if (damage_thresholds(dr, card, &h->major, &h->severe)) {
        marks = damage_marks(dmg, h->major, h->severe, m->massive, &h->tier);
    } else {
        h->why = HIT_NO_THRESHOLDS;
        return;
    }
    h->after = h->before - marks < 0 ? 0 : h->before - marks;

    char was[24];
    if (dr && !damage_horde_attack(dr, card, h->before, h->max, was, sizeof was))
        damage_horde_attack(dr, card, h->after, h->max, h->horde, sizeof h->horde);
}

/* "6+4", "11": the total, or -1. *end is left after it. */
static int parse_amount(const char *s, const char **end)
{
    int total = 0, parts = 0;
    for (;;) {
        while (*s == ' ') s++;
        if (*s < '0' || *s > '9') return -1;
        char *e;
        long v = strtol(s, &e, 10);
        if (v > DMG_MAX || total + v > DMG_MAX) return -1;
        total += (int)v;
        parts++;
        s = e;
        while (*s == ' ') s++;
        if (*s != '+') break;
        s++;
    }
    *end = s;
    return parts ? total : -1;
}

static void cmd_massive(App *a, const char *arg)
{
    Map *m = a->map;
    if (strcmp(arg, "on") && strcmp(arg, "off") && *arg) {
        app_set_status(a, ":dmg massive on, or :dmg massive off");
        return;
    }
    if (!*arg) {
        app_set_status(a, m->massive ? "massive damage on - twice Severe marks 4 HP (:dmg massive off)"
                                     : "massive damage off (:dmg massive on: twice Severe marks 4 HP)");
        return;
    }
    int on = !strcmp(arg, "on");
    if (m->massive != on) { m->massive = on; map_touch(m); }
    app_note(a, on ? "massive damage on - damage at twice Severe marks 4 HP"
                   : "massive damage off - Severe and above marks 3 HP");
}

/* "Goblin 3 (Close)": a name and how far it is, in the band's word when the
 * map has bands. */
static int put_where(char *buf, size_t sz, const Map *m, const Token *from, const Token *t)
{
    double units = token_gap_units(m, from, t);
    const char *band = ruleset_band(ruleset_by_name(m->ruleset), units);
    char ft[24];
    dist_fmt(ft, sizeof ft, units);
    return band ? snprintf(buf, sz, "%.24s (%s)", token_name(t), band)
                : snprintf(buf, sz, "%.24s (%s ft)", token_name(t), ft);
}

typedef struct { int idx; double d; } Near;

static int near_cmp(const void *x, const void *y)
{
    const Near *p = x, *q = y;
    return p->d < q->d ? -1 : p->d > q->d ? 1 : p->idx - q->idx;
}

/* The SRD's Minion (X): every X damage defeats another Minion within the
 * attack's range. That range is the attacker's, which the GM judges, so this
 * lists who could go -- the hit Minions' side, still up, nearest the first
 * hit first -- and marks none of them. */
static void minion_extra(char *msg, size_t sz, int *off, const Map *m, const DamageRule *dr,
                         const Hit *hits, int nhits, const unsigned char *hit, int dmg)
{
    int more = 0, first = -1;
    for (int i = 0; i < nhits; i++)
        if (hits[i].why == HIT_OK && hits[i].minion && dmg > 0) {
            more += dmg / hits[i].minion;
            if (first < 0) first = hits[i].idx;
        }
    if (more <= 0 || *off >= (int)sz) return;

    const Token *from = &m->tokens.v[first];
    Near *near = xmalloc(sizeof *near * (size_t)(m->tokens.n ? m->tokens.n : 1));
    int n = 0;
    for (int i = 0; i < m->tokens.n; i++) {
        const Token *t = &m->tokens.v[i];
        if (hit[i] || t->kind != from->kind || !damage_minion(dr, card_of(m, t))) continue;
        int hp = counter_find(t, "HP");
        if (hp >= 0 && t->counters[hp].value <= 0) continue;
        near[n].idx = i;
        near[n].d   = token_gap_units(m, from, t);
        n++;
    }
    qsort(near, (size_t)n, sizeof *near, near_cmp);

    *off += snprintf(msg + *off, sz - (size_t)*off, " - %d damage defeats %d more Minion%s within the attack's range",
                     dmg, more, more == 1 ? "" : "s");
    if (!n && *off < (int)sz) *off += snprintf(msg + *off, sz - (size_t)*off, ", but none are left");
    for (int i = 0; i < n && *off < (int)sz - 48; i++) {
        *off += snprintf(msg + *off, sz - (size_t)*off, i ? ", " : ": ");
        *off += put_where(msg + *off, sz - (size_t)*off, m, from, &m->tokens.v[near[i].idx]);
    }
    free(near);
}

/* One creature's part of the message: "Goblin: 11 is Major (8/15) - 2 HP
 * marked, 3/5 left", or for a burst the short "Goblin 2 HP (3/5)". */
static int put_hit(char *buf, size_t sz, const Map *m, const Hit *h, const char *amount, int brief)
{
    const char *name = token_name(&m->tokens.v[h->idx]);
    int marked = h->before - h->after, off;
    if (h->minion) {
        off = !marked ? snprintf(buf, sz, brief ? "%.24s no HP" : "%.24s: %s marks no HP", name, amount)
            : snprintf(buf, sz, brief ? "%.24s defeated" : "%.24s: %s defeats it (Minion %d)", name, amount, h->minion);
        return off;
    }
    if (brief) {
        off = !marked ? snprintf(buf, sz, "%.24s no HP", name)
            : !h->after ? snprintf(buf, sz, "%.24s %d HP, defeated", name, marked)
            : snprintf(buf, sz, "%.24s %d HP (%d/%d)", name, marked, h->after, h->max);
    } else if (h->tier) {
        char thr[32];
        snprintf(thr, sizeof thr, " (%d/%d)", h->major, h->severe);
        if (!marked)
            off = snprintf(buf, sz, "%.24s: %s marks no HP", name, amount);
        else
            off = snprintf(buf, sz, "%.24s: %s is %s%s - %d HP marked, %s%d/%d%s", name, amount,
                           h->tier, thr, marked, h->after ? "" : "defeated (",
                           h->after, h->max, h->after ? " left" : ")");
    } else {
        off = !h->after ? snprintf(buf, sz, "%.24s: %s damage - defeated (HP 0/%d)", name, amount, h->max)
            : snprintf(buf, sz, "%.24s: %s damage - HP %d/%d", name, amount, h->after, h->max);
    }
    if (h->horde[0] && off < (int)sz)
        off += snprintf(buf + off, sz - (size_t)off,
                        brief ? ", attack now %s" : " - half its HP marked: its attack now deals %s", h->horde);
    return off;
}

/* Why a creature was passed over, as a hint for the one creature, or a
 * word for a burst's list. */
static void say_skip(App *a, const Map *m, const Hit *h)
{
    const Token *t = &m->tokens.v[h->idx];
    const char  *name = token_name(t);
    char msg[160];
    if (h->why == HIT_NO_HP)
        snprintf(msg, sizeof msg, "no HP on %.24s - s v sets it: HP 5", name);
    else if (h->why == HIT_DOWN)
        snprintf(msg, sizeof msg, "%.24s is already defeated (HP 0/%d)", name, h->max);
    else if (!card_of(m, t))
        snprintf(msg, sizeof msg, "%.24s has no card - s k writes one with its Thresholds: 8/15", name);
    else
        snprintf(msg, sizeof msg, "no thresholds on %.24s's card - s k adds them: Thresholds: 8/15", name);
    app_set_status_gm(a, msg);
}

static const char *skip_word(int why)
{
    return why == HIT_NO_HP ? "no HP" : why == HIT_DOWN ? "already defeated" : "no thresholds";
}

void app_damage_command(App *a, const char *rest)
{
    PROF_ZONE("dmg.apply");
    Map *m = a->map;
    if (!strncmp(rest, "massive", 7) && (!rest[7] || rest[7] == ' ')) {
        const char *arg = rest + 7;
        while (*arg == ' ') arg++;
        cmd_massive(a, arg);
        return;
    }

    const char *end;
    int raw = parse_amount(rest, &end), half = 0;
    if (raw >= 0 && !strncmp(end, "half", 4) && (!end[4] || end[4] == ' ')) { half = 1; end += 4; }
    if (raw >= 0) while (*end == ' ') end++;
    if (raw < 0 || *end) {
        app_set_status(a, ":dmg 11, :dmg 6+4, :dmg 11 half (resistance), or :dmg massive on|off");
        return;
    }
    /* Resistance halves before the thresholds; the SRD rounds up. */
    int  dmg = half ? (raw + 1) / 2 : raw;
    char amount[48];
    if (half) snprintf(amount, sizeof amount, "%d halved to %d", raw, dmg);
    else      snprintf(amount, sizeof amount, "%d", dmg);

    const Ruleset    *rs = ruleset_by_name(m->ruleset);
    const DamageRule *dr = rs ? rs->damage : NULL;
    const RangeOverlay *ro = &a->play.range;
    int burst = a->screen == SCREEN_PLAY && ro->active && ro->burst;

    /* Who it hits: everyone the group effect catches (one damage roll, each
     * against their own thresholds, as the SRD has a multi-target attack),
     * else the one creature. */
    unsigned char *hit = xmalloc((size_t)(m->tokens.n ? m->tokens.n : 1));
    memset(hit, 0, (size_t)(m->tokens.n ? m->tokens.n : 1));
    int nhit;
    if (burst) {
        nhit = range_caught(ro, m, hit);
        if (!nhit) { free(hit); app_set_status(a, "the group effect catches no one - move it over them, or esc for one creature"); return; }
    } else {
        int i = app_target_token(a);
        if (i < 0) { free(hit); app_set_status(a, "no creature here - :dmg marks the selected one's HP"); return; }
        hit[i] = 1;
        nhit = 1;
    }

    Hit *hits = xmalloc(sizeof *hits * (size_t)nhit);
    int  n = 0, changed = 0;
    for (int i = 0; i < m->tokens.n; i++) {
        if (!hit[i]) continue;
        hit_one(m, dr, i, dmg, &hits[n]);
        changed += hits[n].why == HIT_OK && hits[n].after != hits[n].before;
        n++;
    }

    if (!burst && hits[0].why != HIT_OK) {
        say_skip(a, m, &hits[0]);
        free(hits);
        free(hit);
        return;
    }

    /* One undo step for the lot, as one damage roll is one thing. */
    if (changed) {
        undo_begin(&a->undo);
        for (int k = 0; k < n; k++) {
            const Hit *h = &hits[k];
            if (h->why != HIT_OK || h->after == h->before) continue;
            Token t = m->tokens.v[h->idx];
            counter_set(&t, t.counters[h->hp].name, h->after, h->max);
            undo_edit_token(&a->undo, m, h->idx, t);
        }
        undo_end(&a->undo);
    }

    char msg[640];
    int  off = 0;
    if (!burst) {
        off = put_hit(msg, sizeof msg, m, &hits[0], amount, 0);
    } else {
        off = snprintf(msg, sizeof msg, "%s damage to %d caught: ", amount, n);
        int put = 0;
        for (int k = 0; k < n && off < (int)sizeof msg - 2; k++) {
            if (hits[k].why != HIT_OK) continue;
            if (put++) off += snprintf(msg + off, sizeof msg - (size_t)off, ", ");
            if (off < (int)sizeof msg) off += put_hit(msg + off, sizeof msg - (size_t)off, m, &hits[k], amount, 1);
        }
        if (!put && off < (int)sizeof msg) off += snprintf(msg + off, sizeof msg - (size_t)off, "no one marked");
        int skipped = 0;
        for (int k = 0; k < n && off < (int)sizeof msg - 2; k++) {
            if (hits[k].why == HIT_OK) continue;
            off += snprintf(msg + off, sizeof msg - (size_t)off, "%s%.24s (%s)",
                            skipped++ ? ", " : "; skipped ", token_name(&m->tokens.v[hits[k].idx]),
                            skip_word(hits[k].why));
        }
    }
    if (dr && off < (int)sizeof msg) minion_extra(msg, sizeof msg, &off, m, dr, hits, n, hit, dmg);
    app_note_gm(a, msg);
    free(hits);
    free(hit);
}

int app_horde_note(const App *a, int idx, char *buf, size_t sz)
{
    const Map *m = a->map;
    const Ruleset *rs = ruleset_by_name(m->ruleset);
    const Token *t = &m->tokens.v[idx];
    int hp = counter_find(t, "HP");
    buf[0] = '\0';
    if (!rs || !rs->damage || hp < 0) return 0;
    return damage_horde_attack(rs->damage, card_of(m, t), t->counters[hp].value, t->counters[hp].max, buf, sz);
}
