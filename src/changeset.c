#include "changeset.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "card.h"
#include "link.h"
#include "prof.h"
#include "turn.h"
#include "util.h"

void cs_init(ChangeSet *cs) { memset(cs, 0, sizeof *cs); }

void cs_free(ChangeSet *cs)
{
    free(cs->cells);
    free(cs->block_start);
    free(cs->toks);
    free(cs->areas);
    free(cs->links);
    free(cs->notes);
    free(cs->rolls);
    for (int i = 0; i < cs->ncards; i++) {
        free(cs->cards[i].before);
        free(cs->cards[i].after);
    }
    free(cs->cards);
    tokens_free(&cs->pv.tokens);
    free(cs->pv.saved);
    cs_init(cs);
}

/* ------------------------------------------------------------- squares */

static uint8_t cell_value(const Map *m, int kind, int x, int y)
{
    switch (kind) {
    case CS_TILE:  return map_tile(m, x, y);
    case CS_VEDGE: return map_vedge(m, x, y);
    case CS_HEDGE: return map_hedge(m, x, y);
    default:       return map_in_bounds(m, x, y) ? (uint8_t)(m->fog[(size_t)y * (size_t)m->w + (size_t)x] & FOG_ID) : 0;
    }
}

/* The byte a cell lives in, for the preview's swap: written straight, past
 * map_set_*, so nothing is touched. */
static uint8_t *cell_byte(Map *m, int kind, int x, int y)
{
    switch (kind) {
    case CS_TILE:  return &m->tiles[(size_t)y * (size_t)m->w + (size_t)x];
    case CS_VEDGE: return &m->vedges[(size_t)y * (size_t)(m->w + 1) + (size_t)x];
    case CS_HEDGE: return &m->hedges[(size_t)y * (size_t)m->w + (size_t)x];
    default:       return &m->fog[(size_t)y * (size_t)m->w + (size_t)x];
    }
}

static int cell_on_map(const Map *m, const CsCell *c)
{
    switch (c->kind) {
    case CS_VEDGE: return c->x >= 0 && c->y >= 0 && c->x <= m->w && c->y < m->h;
    case CS_HEDGE: return c->x >= 0 && c->y >= 0 && c->x < m->w && c->y <= m->h;
    default:       return map_in_bounds(m, c->x, c->y);
    }
}

static void push_cell(ChangeSet *cs, int kind, int x, int y, uint8_t before, uint8_t after)
{
    if (cs->ncells == cs->cap_cells) {
        cs->cap_cells = cs->cap_cells ? cs->cap_cells * 2 : 256;
        cs->cells = xrealloc(cs->cells, (size_t)cs->cap_cells * sizeof *cs->cells);
    }
    CsCell *c = &cs->cells[cs->ncells++];
    c->x = (int16_t)x;
    c->y = (int16_t)y;
    c->kind = (uint8_t)kind;
    c->before = before;
    c->after = after;
    c->conflict = 0;
}

static int block_of(const ChangeSet *cs, int x, int y)
{
    int bx = imin(x / CS_BLOCK, cs->bw - 1), by = imin(y / CS_BLOCK, cs->bh - 1);
    return by * cs->bw + bx;
}

/* Buckets the cells by block -- a counting sort, linear and stable -- and
 * indexes the blocks. Within a block they keep the order they came in: the
 * full diff's raster order, or the log's. */
static void cells_index(ChangeSet *cs)
{
    int nb = cs->bw * cs->bh, n = cs->ncells;
    int *start = xcalloc((size_t)nb + 1, sizeof *start);
    for (int i = 0; i < n; i++) start[block_of(cs, cs->cells[i].x, cs->cells[i].y) + 1]++;
    for (int b = 0; b < nb; b++) start[b + 1] += start[b];
    if (n) {
        CsCell *by = xmalloc((size_t)n * sizeof *by);
        int    *at = xmalloc((size_t)nb * sizeof *at);
        memcpy(at, start, (size_t)nb * sizeof *at);
        for (int i = 0; i < n; i++) by[at[block_of(cs, cs->cells[i].x, cs->cells[i].y)]++] = cs->cells[i];
        free(at);
        free(cs->cells);
        cs->cells = by;
        cs->cap_cells = n;
    }
    cs->block_start = start;
}

static void diff_cells_all(ChangeSet *cs, const Map *a, const Map *b)
{
    const struct { int kind; const uint8_t *pa, *pb; int w, h; } layers[] = {
        { CS_TILE,  a->tiles,  b->tiles,  a->w,     a->h     },
        { CS_VEDGE, a->vedges, b->vedges, a->w + 1, a->h     },
        { CS_HEDGE, a->hedges, b->hedges, a->w,     a->h + 1 },
        { CS_FOG,   a->fog,    b->fog,    a->w,     a->h     },
    };
    for (size_t k = 0; k < sizeof layers / sizeof layers[0]; k++) {
        int w = layers[k].w, kind = layers[k].kind;
        uint8_t mask = kind == CS_FOG ? FOG_ID : 0xFF;
        for (int y = 0; y < layers[k].h; y++) {
            const uint8_t *ra = layers[k].pa + (size_t)y * (size_t)w;
            const uint8_t *rb = layers[k].pb + (size_t)y * (size_t)w;
            if (!memcmp(ra, rb, (size_t)w)) continue;    /* fog too: equal bytes, equal painting */
            for (int x = 0; x < w; x++) {
                uint8_t va = (uint8_t)(ra[x] & mask), vb = (uint8_t)(rb[x] & mask);
                if (va != vb) push_cell(cs, kind, x, y, va, vb);
            }
        }
    }
}

/* Only the squares the log wrote: what the plan cost, not the map's size.
 * The maps are one size and the log wrote only squares on them, so the
 * arrays are read straight. */
static void diff_cells_hint(ChangeSet *cs, const Map *a, const Map *b, const Undo *u)
{
    size_t w = (size_t)a->w;
    /* A square the log wrote twice is one cell: a set of (kind, square) keys
     * sized to the log, so a plan of 300 writes costs a 4 KB table. */
    int cap = 16;
    while (cap < 2 * u->nops) cap *= 2;
    uint32_t *seen = xcalloc((size_t)cap, sizeof *seen);       /* key + 1; 0 empty */
    for (int i = 0; i < u->nops; i++) {
        const Op *o = &u->ops[i];
        size_t   x = (size_t)o->x, y = (size_t)o->y, at;
        const uint8_t *pa, *pb;
        int      kind;
        uint8_t  mask = 0xFF;
        switch (o->kind) {
        case OP_TILE:  kind = CS_TILE;  pa = a->tiles;  pb = b->tiles;  at = y * w + x;       break;
        case OP_VEDGE: kind = CS_VEDGE; pa = a->vedges; pb = b->vedges; at = y * (w + 1) + x; break;
        case OP_HEDGE: kind = CS_HEDGE; pa = a->hedges; pb = b->hedges; at = y * w + x;       break;
        case OP_FOG:   kind = CS_FOG;   pa = a->fog;    pb = b->fog;    at = y * w + x; mask = FOG_ID; break;
        default:       continue;
        }
        uint8_t va = (uint8_t)(pa[at] & mask), vb = (uint8_t)(pb[at] & mask);
        if (va == vb) continue;
        uint32_t key = ((uint32_t)kind << 20 | (uint32_t)y << 10 | (uint32_t)x) + 1, h = key * 2654435761u;
        int dup = 0;
        for (h &= (uint32_t)(cap - 1); seen[h] && !dup; h = (h + 1) & (uint32_t)(cap - 1)) dup = seen[h] == key;
        if (dup) continue;
        seen[h] = key;
        push_cell(cs, kind, o->x, o->y, va, vb);
    }
    free(seen);
}

/* ------------------------------------------------------- the small parts */

/* The creature in `l` that `t` names (token_same_key), the first in list
 * order. With two creatures of one label (the channel refuses them, a loaded
 * file may hold them) it is always the first: check, apply and preview all
 * ask this, so they agree with each other. */
static int token_match(const TokenList *l, const Token *t)
{
    for (int i = 0; i < l->n; i++)
        if (token_same_key(&l->v[i], t)) return i;
    return -1;
}

static int label_taken(const TokenList *l, const char *label)
{
    return tokens_find_label(l, label, -1) >= 0;
}

static int area_equal(const Area *a, const Area *b)
{
    return !strcmp(a->name, b->name) && a->x0 == b->x0 && a->y0 == b->y0 && a->x1 == b->x1 &&
           a->y1 == b->y1 && a->floor == b->floor && (!a->floor || a->level == b->level);
}

static int link_equal(const Link *a, const Link *b)
{
    for (int e = 0; e < 2; e++)
        if (a->x[e] != b->x[e] || a->y[e] != b->y[e]) return 0;
    return a->num == b->num && a->kind == b->kind && a->size == b->size && a->oneway == b->oneway &&
           a->secret == b->secret && !strcmp(a->to_map, b->to_map) && !strcmp(a->to_place, b->to_place);
}

static int roll_find(const Map *m, const char *name)
{
    if (!name[0]) return -1;
    for (int i = 0; i < ROLL_MAX; i++)
        if (!strcmp(m->rolls[i].name, name)) return i;
    return -1;
}

/* A push onto one of the lists, doubling from 8. Reserving the worst case
 * up front (every element of both maps) was tried: 1,000 creatures' worth is
 * 520 KB, which malloc gets with mmap and gives back with munmap -- two
 * system calls a diff, the most of what 500 unchanged creatures cost. */
static void *grow(void *arr, int n, int *cap, size_t sz)
{
    if (n < *cap) return arr;
    *cap = *cap ? *cap * 2 : 8;
    return xrealloc(arr, (size_t)*cap * sz);
}
#define GROW(arr, n, cap) ((arr) = grow((arr), (n), &(cap), sizeof *(arr)), &(arr)[(n)++])

/* A creature's key (token_same_key), hashed: its label, or side and square. */
static uint32_t token_key(const Token *t)
{
    uint32_t h = 2166136261u;
    if (t->label[0]) {
        for (const char *p = t->label; *p; p++) h = (h ^ (uint8_t)*p) * 16777619u;
        return h;
    }
    int v[3] = { t->kind, t->x, t->y };
    for (int i = 0; i < 3; i++) h = (h ^ (uint32_t)v[i]) * 16777619u;
    return h ^ 1u;
}

/* Pairs the two lists by key. A copy keeps the list's order -- edits in
 * place, a removal shifts what follows, an add goes on the end -- so the
 * creature at the same index, less the removals so far, is tried first and
 * nearly always is the one: a walk down both lists in step, which also pairs
 * two creatures of one label in their order. Only a miss builds a hash of
 * `b`, so the diff stays linear when the order has moved. Asking token_match
 * for each would be the square of the creatures. */
static void diff_tokens(ChangeSet *cs, const Map *a, const Map *b)
{
    const TokenList *bl = &b->tokens;
    uint8_t *used = xcalloc((size_t)bl->n + 1, 1);
    int ctoks = 0, cap = 0, shift = 0;
    int *slot = NULL;
    for (int i = 0; i < a->tokens.n; i++) {
        const Token *t = &a->tokens.v[i];
        int j = -1, g = i + shift;
        if (g >= 0 && g < bl->n && !used[g] && token_same_key(t, &bl->v[g])) j = g;
        else {
            if (!slot) {
                cap = 16;
                while (cap < 2 * bl->n) cap *= 2;
                slot = xmalloc((size_t)cap * sizeof *slot);
                for (int k = 0; k < cap; k++) slot[k] = -1;
                for (int k = 0; k < bl->n; k++) {
                    uint32_t h = token_key(&bl->v[k]) & (uint32_t)(cap - 1);
                    while (slot[h] >= 0) h = (h + 1) & (uint32_t)(cap - 1);
                    slot[h] = k;
                }
            }
            /* The first unused one in list order among those with the key. */
            for (uint32_t h = token_key(t) & (uint32_t)(cap - 1); slot[h] >= 0; h = (h + 1) & (uint32_t)(cap - 1)) {
                int k = slot[h];
                if (!used[k] && token_same_key(t, &bl->v[k]) && (j < 0 || k < j)) j = k;
            }
            if (j >= 0) shift = j - i;
            else shift--;                         /* removed: the rest moved up one */
        }
        if (j >= 0) {
            used[j] = 1;
            if (token_equal(t, &bl->v[j])) continue;
        }
        CsToken *c = GROW(cs->toks, cs->ntoks, ctoks);
        memset(c, 0, sizeof *c);
        c->had = 1;
        c->before = *t;
        if (j >= 0) { c->has = 1; c->after = bl->v[j]; }
    }
    for (int j = 0; j < bl->n; j++) {
        if (used[j]) continue;
        CsToken *c = GROW(cs->toks, cs->ntoks, ctoks);
        memset(c, 0, sizeof *c);
        c->has = 1;
        c->after = bl->v[j];
    }
    free(slot);
    free(used);
}

static void diff_small(ChangeSet *cs, const Map *a, const Map *b)
{
    int careas = 0, clinks = 0, cnotes = 0, crolls = 0, ccards = 0;
    for (int i = 0; i < a->nareas; i++) {
        int j = map_area_find(b, a->areas[i].name);
        if (j >= 0 && area_equal(&a->areas[i], &b->areas[j])) continue;
        CsArea *c = GROW(cs->areas, cs->nareas, careas);
        memset(c, 0, sizeof *c);
        c->had = 1;
        c->before = a->areas[i];
        if (j >= 0) { c->has = 1; c->after = b->areas[j]; }
    }
    for (int j = 0; j < b->nareas; j++) {
        if (map_area_find(a, b->areas[j].name) >= 0) continue;
        CsArea *c = GROW(cs->areas, cs->nareas, careas);
        memset(c, 0, sizeof *c);
        c->has = 1;
        c->after = b->areas[j];
    }

    for (int i = 0; i < a->nlinks; i++) {
        int j = link_find(b, a->links[i].num);
        if (j >= 0 && link_equal(&a->links[i], &b->links[j])) continue;
        CsLink *c = GROW(cs->links, cs->nlinks, clinks);
        memset(c, 0, sizeof *c);
        c->had = 1;
        c->before = a->links[i];
        if (j >= 0) { c->has = 1; c->after = b->links[j]; }
    }
    for (int j = 0; j < b->nlinks; j++) {
        if (link_find(a, b->links[j].num) >= 0) continue;
        CsLink *c = GROW(cs->links, cs->nlinks, clinks);
        memset(c, 0, sizeof *c);
        c->has = 1;
        c->after = b->links[j];
    }

    for (int i = 0; i < a->nnotes; i++) {
        const Note *n = &a->notes[i];
        const char *now = map_note_at(b, n->x, n->y);
        if (now && !strcmp(now, n->text)) continue;
        CsNote *c = GROW(cs->notes, cs->nnotes, cnotes);
        memset(c, 0, sizeof *c);
        c->x = n->x;
        c->y = n->y;
        str_lcpy(c->before, n->text, sizeof c->before);
        str_lcpy(c->after, now ? now : "", sizeof c->after);
    }
    for (int j = 0; j < b->nnotes; j++) {
        const Note *n = &b->notes[j];
        if (map_note_at(a, n->x, n->y)) continue;
        CsNote *c = GROW(cs->notes, cs->nnotes, cnotes);
        memset(c, 0, sizeof *c);
        c->x = n->x;
        c->y = n->y;
        str_lcpy(c->after, n->text, sizeof c->after);
    }

    for (int i = 0; i < ROLL_MAX; i++) {
        const NamedRoll *r = &a->rolls[i];
        if (!r->name[0]) continue;
        int j = roll_find(b, r->name);
        if (j >= 0 && !strcmp(b->rolls[j].expr, r->expr)) continue;
        CsRoll *c = GROW(cs->rolls, cs->nrolls, crolls);
        memset(c, 0, sizeof *c);
        c->before = *r;
        if (j >= 0) c->after = b->rolls[j];
    }
    for (int j = 0; j < ROLL_MAX; j++) {
        const NamedRoll *r = &b->rolls[j];
        if (!r->name[0] || roll_find(a, r->name) >= 0) continue;
        CsRoll *c = GROW(cs->rolls, cs->nrolls, crolls);
        memset(c, 0, sizeof *c);
        c->after = *r;
    }

    for (int i = 0; i < a->ncards; i++) {
        int j = card_find(b, a->cards[i].name);
        const char *was = a->cards[i].text ? a->cards[i].text : "";
        if (j >= 0 && !strcmp(was, b->cards[j].text ? b->cards[j].text : "")) continue;
        CsCard *c = GROW(cs->cards, cs->ncards, ccards);
        memset(c, 0, sizeof *c);
        str_lcpy(c->name, a->cards[i].name, sizeof c->name);
        c->before = xstrdup(was);
        if (j >= 0) c->after = xstrdup(b->cards[j].text ? b->cards[j].text : "");
    }
    for (int j = 0; j < b->ncards; j++) {
        if (card_find(a, b->cards[j].name) >= 0) continue;
        CsCard *c = GROW(cs->cards, cs->ncards, ccards);
        memset(c, 0, sizeof *c);
        str_lcpy(c->name, b->cards[j].name, sizeof c->name);
        c->after = xstrdup(b->cards[j].text ? b->cards[j].text : "");
    }

    if (a->round != b->round) {
        cs->round_changed = 1;
        cs->round_before = a->round;
        cs->round_after  = b->round;
    }
    if (a->spotlight != b->spotlight) {
        cs->spot_changed = 1;
        cs->spot_before = a->spotlight;
        cs->spot_after  = b->spotlight;
    }
    for (int i = 0; i < FOG_PATCH_MAX; i++)
        if (!b->fog_patches[i].dead) str_lcpy(cs->fog_names[i], b->fog_patches[i].name, FOG_NAME_MAX);
}

int cs_diff(ChangeSet *cs, const Map *a, const Map *b, const Undo *hint)
{
    PROF_ZONE("mapdiff");
    cs_free(cs);
    cs->w  = a->w;
    cs->h  = a->h;
    cs->bw = (a->w + 1 + CS_BLOCK - 1) / CS_BLOCK;     /* + 1: the far boundary */
    cs->bh = (a->h + 1 + CS_BLOCK - 1) / CS_BLOCK;
    if (a->w == b->w && a->h == b->h) {
        /* A log that wrote more than a quarter of the map's squares (a fill)
         * is slower to walk than the map: its dedupe table outgrows the
         * cache, where the full diff is one memcmp a row. */
        if (hint && (size_t)hint->nops < (size_t)a->w * (size_t)a->h / 4) diff_cells_hint(cs, a, b, hint);
        else diff_cells_all(cs, a, b);
    }
    cells_index(cs);
    diff_tokens(cs, a, b);
    diff_small(cs, a, b);
    return cs->ncells + cs->ntoks + cs->nareas + cs->nlinks + cs->nnotes + cs->nrolls + cs->ncards +
           cs->round_changed + cs->spot_changed;
}

/* ------------------------------------------------------------ conflicts */

static int fog_patch_gone(const ChangeSet *cs, const Map *live, int id)
{
    if (!id) return 0;
    const FogPatch *p = &live->fog_patches[id - 1];
    return p->dead || !p->name[0] || strcmp(p->name, cs->fog_names[id - 1]) != 0;
}

int cs_check(ChangeSet *cs, const Map *live)
{
    if (cs->checked && cs->checked_gen == live->gen) return cs->conflicts;
    int n = 0;
    if (live->w == cs->w && live->h == cs->h) {
        /* The map the set was made for: every cell is on it, so the arrays
         * are read straight, without map_tile's bounds and switch. */
        size_t w = (size_t)live->w;
        for (int i = 0; i < cs->ncells; i++) {
            CsCell *c = &cs->cells[i];
            size_t  x = (size_t)c->x, y = (size_t)c->y;
            uint8_t now;
            switch (c->kind) {
            case CS_TILE:  now = live->tiles[y * w + x]; break;
            case CS_VEDGE: now = live->vedges[y * (w + 1) + x]; break;
            case CS_HEDGE: now = live->hedges[y * w + x]; break;
            default:       now = (uint8_t)(live->fog[y * w + x] & FOG_ID);
                           if (fog_patch_gone(cs, live, c->after)) now = (uint8_t)~c->before;
            }
            c->conflict = now != c->before;
            n += c->conflict;
        }
    } else
        for (int i = 0; i < cs->ncells; i++) {
            CsCell *c = &cs->cells[i];
            c->conflict = (uint8_t)(!cell_on_map(live, c) || cell_value(live, c->kind, c->x, c->y) != c->before ||
                                    (c->kind == CS_FOG && fog_patch_gone(cs, live, c->after)));
            n += c->conflict;
        }
    for (int i = 0; i < cs->ntoks; i++) {
        CsToken *c = &cs->toks[i];
        if (c->had) {
            int j = token_match(&live->tokens, &c->before);
            c->conflict = (uint8_t)(j < 0 || !token_equal(&live->tokens.v[j], &c->before));
        } else
            c->conflict = (uint8_t)label_taken(&live->tokens, c->after.label);
        n += c->conflict;
    }
    for (int i = 0; i < cs->nareas; i++) {
        CsArea *c = &cs->areas[i];
        int j = map_area_find(live, c->had ? c->before.name : c->after.name);
        c->conflict = (uint8_t)(c->had ? (j < 0 || !area_equal(&live->areas[j], &c->before)) : j >= 0);
        n += c->conflict;
    }
    for (int i = 0; i < cs->nlinks; i++) {
        CsLink *c = &cs->links[i];
        if (!c->had) { c->conflict = 0; continue; }
        int j = link_find(live, c->before.num);
        c->conflict = (uint8_t)(j < 0 || !link_equal(&live->links[j], &c->before));
        n += c->conflict;
    }
    for (int i = 0; i < cs->nnotes; i++) {
        CsNote *c = &cs->notes[i];
        const char *now = map_note_at(live, c->x, c->y);
        c->conflict = (uint8_t)(strcmp(now ? now : "", c->before) != 0);
        n += c->conflict;
    }
    for (int i = 0; i < cs->nrolls; i++) {
        CsRoll *c = &cs->rolls[i];
        int j = roll_find(live, c->before.name[0] ? c->before.name : c->after.name);
        c->conflict = (uint8_t)(c->before.name[0] ? (j < 0 || strcmp(live->rolls[j].expr, c->before.expr) != 0) : j >= 0);
        n += c->conflict;
    }
    for (int i = 0; i < cs->ncards; i++) {
        CsCard *c = &cs->cards[i];
        int j = card_find(live, c->name);
        const char *now = j >= 0 ? (live->cards[j].text ? live->cards[j].text : "") : NULL;
        c->conflict = (uint8_t)(c->before ? (!now || strcmp(now, c->before) != 0) : now != NULL);
        n += c->conflict;
    }
    cs->round_conflict = (uint8_t)(cs->round_changed && live->round != cs->round_before);
    cs->spot_conflict  = (uint8_t)(cs->spot_changed && live->spotlight != cs->spot_before);
    n += cs->round_conflict + cs->spot_conflict;
    cs->conflicts   = n;
    cs->checked     = 1;
    cs->checked_gen = live->gen;
    return n;
}

/* --------------------------------------------------------------- boxes */

static int sq_in(const CsBox *b, int x, int y)
{
    return !b || (x >= b->x0 && x <= b->x1 && y >= b->y0 && y <= b->y1);
}

static int block_in(const CsBox *b, int x, int y, int size)
{
    return !b || (sq_in(b, x, y) && sq_in(b, x + size - 1, y + size - 1));
}

static int cell_in(const CsBox *b, const CsCell *c)
{
    if (!b) return 1;
    switch (c->kind) {
    case CS_VEDGE: return c->x >= b->x0 && c->x <= b->x1 + 1 && c->y >= b->y0 && c->y <= b->y1;
    case CS_HEDGE: return c->x >= b->x0 && c->x <= b->x1 && c->y >= b->y0 && c->y <= b->y1 + 1;
    default:       return sq_in(b, c->x, c->y);
    }
}

static int tok_in(const CsBox *b, const CsToken *c)
{
    return (!c->had || block_in(b, c->before.x, c->before.y, c->before.size)) &&
           (!c->has || block_in(b, c->after.x, c->after.y, c->after.size));
}

static int link_in(const CsBox *b, const Link *l)
{
    for (int e = 0; e < link_ends(l); e++)
        if (!block_in(b, l->x[e], l->y[e], l->size)) return 0;
    return 1;
}

static int lnk_in(const CsBox *b, const CsLink *c)
{
    return (!c->had || link_in(b, &c->before)) && (!c->has || link_in(b, &c->after));
}

static int area_in(const CsBox *b, const Area *a)
{
    return sq_in(b, a->x0, a->y0) && sq_in(b, a->x1, a->y1);
}

static int ar_in(const CsBox *b, const CsArea *c)
{
    return (!c->had || area_in(b, &c->before)) && (!c->has || area_in(b, &c->after));
}

/* --------------------------------------------------------------- apply */

/* The creature holding the turn, other than `skip`; -1 for none. */
static int acting_other(const TokenList *l, int skip)
{
    for (int i = 0; i < l->n; i++)
        if (i != skip && (l->v[i].turn & TURN_ACTING)) return i;
    return -1;
}

static void note_left_out(char *out, size_t outsz, const char *what)
{
    if (!out || !outsz) return;
    size_t n = strlen(out);
    snprintf(out + n, outsz - n, "%s%s", n ? ", " : "left out: ", what);
}

int cs_apply(const ChangeSet *cs, Map *live, Undo *u, const CsBox *box, char *out, size_t outsz)
{
    PROF_ZONE("job.accept");
    if (out && outsz) out[0] = '\0';
    int  n = 0;
    char what[96];
    undo_begin(u);

    for (int i = 0; i < cs->ncells; i++) {
        const CsCell *c = &cs->cells[i];
        if (!cell_in(box, c) || !cell_on_map(live, c)) continue;
        switch (c->kind) {
        case CS_TILE:  undo_set_tile(u, live, c->x, c->y, c->after);  break;
        case CS_VEDGE: undo_set_vedge(u, live, c->x, c->y, c->after); break;
        case CS_HEDGE: undo_set_hedge(u, live, c->x, c->y, c->after); break;
        default: {
            if (fog_patch_gone(cs, live, c->after)) {
                snprintf(what, sizeof what, "fog patch %d's painting", c->after);
                note_left_out(out, outsz, what);
                continue;
            }
            uint8_t now = live->fog[(size_t)c->y * (size_t)live->w + (size_t)c->x];
            undo_set_fog(u, live, c->x, c->y, (uint8_t)((now & ~FOG_ID) | c->after));
        }
        }
        n++;
    }

    /* Creatures: the ones going first, so a label they free can be taken. */
    for (int i = 0; i < cs->ntoks; i++) {
        const CsToken *c = &cs->toks[i];
        if (!c->had || c->has || !tok_in(box, c)) continue;
        int j = token_match(&live->tokens, &c->before);
        if (j < 0) continue;
        turn_before_remove(live, u, j);
        undo_del_token(u, live, j);
        n++;
    }
    turn_settle(live, u);
    for (int i = 0; i < cs->ntoks; i++) {
        const CsToken *c = &cs->toks[i];
        if (!c->has || !tok_in(box, c)) continue;
        int j = c->had ? token_match(&live->tokens, &c->before) : -1;
        if (j < 0 && label_taken(&live->tokens, c->after.label)) {
            snprintf(what, sizeof what, "\"%.40s\" (the map has one)", c->after.label);
            note_left_out(out, outsz, what);
            continue;
        }
        /* One creature holds the turn (token.h). The plan's turn lands only
         * where nobody else on the live map has it -- the GM may have passed
         * it since, or a box may hold the plan's new actor and not the old. */
        Token t = c->after;
        if ((t.turn & TURN_ACTING) && acting_other(&live->tokens, j) >= 0) t.turn &= (uint8_t)~TURN_ACTING;
        if (j >= 0) undo_edit_token(u, live, j, t);
        else        undo_add_token(u, live, t);
        n++;
    }

    for (int i = 0; i < cs->nareas; i++) {
        const CsArea *c = &cs->areas[i];
        if (!ar_in(box, c)) continue;
        if (!c->has) { n += undo_remove_area(u, live, c->before.name); continue; }
        const Area *a = &c->after;
        if (!undo_set_area(u, live, a->name, a->x0, a->y0, a->x1, a->y1)) {
            snprintf(what, sizeof what, "area %.31s (no room)", a->name);
            note_left_out(out, outsz, what);
            continue;
        }
        undo_set_floor(u, live, a->name, a->floor, a->level);
        n++;
    }

    for (int i = 0; i < cs->nlinks; i++) {
        const CsLink *c = &cs->links[i];
        if (!lnk_in(box, c)) continue;
        if (!c->has) { n += undo_remove_link(u, live, c->before.num); continue; }
        Link l = c->after;
        if (!c->had) l.num = (uint8_t)link_free_num(live);
        if (!l.num || !undo_set_link(u, live, &l)) {
            note_left_out(out, outsz, "a link (no room)");
            continue;
        }
        n++;
    }

    for (int i = 0; i < cs->nnotes; i++) {
        const CsNote *c = &cs->notes[i];
        if (!sq_in(box, c->x, c->y)) continue;
        if (!undo_set_note(u, live, c->x, c->y, c->after)) {
            note_left_out(out, outsz, "a note (no room)");
            continue;
        }
        n++;
    }

    /* Rolls, cards and the fight are no square's: only a whole accept takes them. */
    if (!box) {
        for (int i = 0; i < cs->nrolls; i++) {
            const CsRoll *c = &cs->rolls[i];
            int slot = roll_find(live, c->before.name[0] ? c->before.name : c->after.name);
            if (slot < 0 && c->after.name[0]) slot = roll_find(live, c->after.name);
            if (slot < 0 && c->after.name[0])
                for (int k = 0; k < ROLL_MAX && slot < 0; k++)
                    if (!live->rolls[k].name[0]) slot = k;
            if (slot < 0) {
                if (c->after.name[0]) note_left_out(out, outsz, "a named roll (no room)");
                continue;
            }
            undo_set_roll(u, live, slot, &c->after);
            n++;
        }
        for (int i = 0; i < cs->ncards; i++) {
            const CsCard *c = &cs->cards[i];
            if (!c->after) continue;                    /* no request takes a card away */
            if (card_set(live, c->name, c->after) < 0) {
                snprintf(what, sizeof what, "card %.31s (no room)", c->name);
                note_left_out(out, outsz, what);
                continue;
            }
            n++;
        }
        if (cs->round_changed) { undo_set_round(u, live, cs->round_after); n++; }
        if (cs->spot_changed)  { undo_set_spotlight(u, live, cs->spot_after); n++; }
    }

    undo_end(u);
    return n;
}

/* ------------------------------------------------------------- summary */

typedef struct {
    int n, x0, y0, x1, y1;
} Tally;

static void tally(Tally *t, int x, int y)
{
    if (!t->n++) { t->x0 = t->x1 = x; t->y0 = t->y1 = y; return; }
    t->x0 = imin(t->x0, x); t->x1 = imax(t->x1, x);
    t->y0 = imin(t->y0, y); t->y1 = imax(t->y1, y);
}

static void say(char *out, size_t outsz, const char *fmt, ...)
{
    size_t n = strlen(out);
    if (n + 3 >= outsz) return;
    if (n) { memcpy(out + n, ", ", 3); n += 2; }
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(out + n, outsz - n, fmt, ap);
    va_end(ap);
}

static void say_tally(char *out, size_t outsz, const char *what, const Tally *t)
{
    if (!t->n) return;
    char where[2 * MAP_COORD_MAX + 2];
    map_region_name(t->x0, t->y0, t->x1, t->y1, where, sizeof where);
    say(out, outsz, "%s in %s", what, where);
}

/* One group of creatures: "Ghoul added", "3 creatures added (Ghoul, Ghoul 2,
 * Ghoul 3)", the names cut after three. */
static void say_tokens(char *out, size_t outsz, const ChangeSet *cs, const CsBox *box, int had, int has, const char *verb)
{
    char names[96] = "";
    int  n = 0;
    for (int i = 0; i < cs->ntoks; i++) {
        const CsToken *c = &cs->toks[i];
        if (c->had != had || c->has != has || !tok_in(box, c)) continue;
        size_t k = strlen(names);
        if (n < 3) snprintf(names + k, sizeof names - k, "%s%.24s", n ? ", " : "", token_name(has ? &c->after : &c->before));
        n++;
    }
    if (n == 1) say(out, outsz, "%s %s", names, verb);
    else if (n) say(out, outsz, "%d creatures %s (%s%s)", n, verb, names, n > 3 ? ", ..." : "");
}

void cs_summary(const ChangeSet *cs, const CsBox *box, char *out, size_t outsz)
{
    if (!outsz) return;
    out[0] = '\0';
    Tally ground = { 0 }, walls = { 0 }, fog = { 0 };
    for (int i = 0; i < cs->ncells; i++) {
        const CsCell *c = &cs->cells[i];
        if (!cell_in(box, c)) continue;
        int x = imin(c->x, cs->w - 1), y = imin(c->y, cs->h - 1);
        tally(c->kind == CS_TILE ? &ground : c->kind == CS_FOG ? &fog : &walls, x, y);
    }
    say_tally(out, outsz, "ground", &ground);
    say_tally(out, outsz, "walls and doors", &walls);
    say_tally(out, outsz, "fog", &fog);
    say_tokens(out, outsz, cs, box, 0, 1, "added");
    say_tokens(out, outsz, cs, box, 1, 1, "changed");
    say_tokens(out, outsz, cs, box, 1, 0, "removed");
    for (int i = 0; i < cs->nareas; i++) {
        const CsArea *c = &cs->areas[i];
        if (!ar_in(box, c)) continue;
        say(out, outsz, "area %s %s", c->has ? c->after.name : c->before.name,
            !c->had ? "added" : !c->has ? "removed" : "changed");
    }
    for (int i = 0; i < cs->nlinks; i++) {
        const CsLink *c = &cs->links[i];
        if (!lnk_in(box, c)) continue;
        if (!c->had) say(out, outsz, "a %s link added", link_kind_name(c->after.kind));   /* numbered at accept */
        else say(out, outsz, "link %d %s", c->before.num, !c->has ? "removed" : "changed");
    }
    Tally notes = { 0 };
    for (int i = 0; i < cs->nnotes; i++)
        if (sq_in(box, cs->notes[i].x, cs->notes[i].y)) tally(&notes, cs->notes[i].x, cs->notes[i].y);
    if (notes.n == 1) {
        char sq[MAP_COORD_MAX];
        map_coord_name(notes.x0, notes.y0, sq, sizeof sq);
        say(out, outsz, "a note at %s", sq);
    } else say_tally(out, outsz, "notes", &notes);
    if (!box) {
        for (int i = 0; i < cs->nrolls; i++)
            say(out, outsz, "roll %s%s", cs->rolls[i].after.name[0] ? cs->rolls[i].after.name : cs->rolls[i].before.name,
                cs->rolls[i].after.name[0] ? "" : " removed");
        for (int i = 0; i < cs->ncards; i++) say(out, outsz, "card %s", cs->cards[i].name);
        if (cs->round_changed) say(out, outsz, "round %d -> %d", cs->round_before, cs->round_after);
        if (cs->spot_changed) say(out, outsz, "spotlight to the %s", cs->spot_after ? "GM" : "players");
    }
    if (!out[0]) snprintf(out, outsz, "no changes");
}

int cs_bounds(const ChangeSet *cs, int *x0, int *y0, int *x1, int *y1)
{
    Tally t = { 0 };
    for (int i = 0; i < cs->ncells; i++)
        tally(&t, imin(cs->cells[i].x, cs->w - 1), imin(cs->cells[i].y, cs->h - 1));
    for (int i = 0; i < cs->ntoks; i++) {
        const CsToken *c = &cs->toks[i];
        for (int k = 0; k < 2; k++) {
            if (!(k ? c->has : c->had)) continue;
            const Token *s = k ? &c->after : &c->before;
            tally(&t, s->x, s->y);
            tally(&t, s->x + s->size - 1, s->y + s->size - 1);
        }
    }
    for (int i = 0; i < cs->nnotes; i++) tally(&t, cs->notes[i].x, cs->notes[i].y);
    for (int i = 0; i < cs->nlinks; i++) {
        const CsLink *c = &cs->links[i];
        for (int k = 0; k < 2; k++) {
            if (!(k ? c->has : c->had)) continue;
            const Link *l = k ? &c->after : &c->before;
            for (int e = 0; e < link_ends(l); e++) {
                tally(&t, l->x[e], l->y[e]);
                tally(&t, l->x[e] + l->size - 1, l->y[e] + l->size - 1);
            }
        }
    }
    for (int i = 0; i < cs->nareas; i++) {
        const CsArea *c = &cs->areas[i];
        for (int k = 0; k < 2; k++) {
            if (!(k ? c->has : c->had)) continue;
            const Area *a = k ? &c->after : &c->before;
            tally(&t, a->x0, a->y0);
            tally(&t, a->x1, a->y1);
        }
    }
    if (!t.n) return 0;
    *x0 = t.x0; *y0 = t.y0; *x1 = t.x1; *y1 = t.y1;
    return 1;
}

/* ------------------------------------------------------------- preview */

/* The live creatures, links and notes with the set applied, straight into
 * the copies: what the preview swaps in. Built once per live Map.gen. */
static void preview_build(ChangeSet *cs, const Map *live)
{
    TokenList *l = &cs->pv.tokens;
    if (l->cap < live->tokens.n) {
        l->cap = live->tokens.n;
        l->v = xrealloc(l->v, (size_t)l->cap * sizeof *l->v);
    }
    if (live->tokens.n) memcpy(l->v, live->tokens.v, (size_t)live->tokens.n * sizeof *l->v);
    l->n = live->tokens.n;
    for (int i = 0; i < cs->ntoks; i++) {
        const CsToken *c = &cs->toks[i];
        int j = c->had ? token_match(l, &c->before) : -1;
        if (c->had && !c->has) { if (j >= 0) tokens_remove(l, j); continue; }
        if (j >= 0) { l->v[j] = c->after; continue; }
        if (!label_taken(l, c->after.label)) tokens_add(l, c->after);
    }

    memcpy(cs->pv.links, live->links, sizeof cs->pv.links);
    cs->pv.nlinks = live->nlinks;
    for (int i = 0; i < cs->nlinks; i++) {
        const CsLink *c = &cs->links[i];
        int j = -1;
        for (int k = 0; k < cs->pv.nlinks; k++)
            if (c->had && cs->pv.links[k].num == c->before.num) j = k;
        if (c->had && !c->has) {
            if (j >= 0) cs->pv.links[j] = cs->pv.links[--cs->pv.nlinks];
            continue;
        }
        if (j >= 0) { cs->pv.links[j] = c->after; continue; }
        if (cs->pv.nlinks >= MAP_LINKS_MAX) continue;
        /* A new link: the number the accept will give it (cs_apply). */
        uint8_t used[LINK_NUM_MAX + 1] = { 0 };
        for (int k = 0; k < cs->pv.nlinks; k++)
            if (cs->pv.links[k].num <= LINK_NUM_MAX) used[cs->pv.links[k].num] = 1;
        Link nl = c->after;
        nl.num = 0;
        for (int k = 1; k <= LINK_NUM_MAX && !nl.num; k++)
            if (!used[k]) nl.num = (uint8_t)k;
        if (nl.num) cs->pv.links[cs->pv.nlinks++] = nl;
    }

    memcpy(cs->pv.notes, live->notes, sizeof cs->pv.notes);
    cs->pv.nnotes = live->nnotes;
    for (int i = 0; i < cs->nnotes; i++) {
        const CsNote *c = &cs->notes[i];
        int j = -1;
        for (int k = 0; k < cs->pv.nnotes; k++)
            if (cs->pv.notes[k].x == c->x && cs->pv.notes[k].y == c->y) j = k;
        if (!c->after[0]) {
            if (j >= 0) cs->pv.notes[j] = cs->pv.notes[--cs->pv.nnotes];
            continue;
        }
        if (j < 0) {
            if (cs->pv.nnotes >= MAP_NOTES_MAX) continue;
            j = cs->pv.nnotes++;
            cs->pv.notes[j].x = c->x;
            cs->pv.notes[j].y = c->y;
        }
        str_lcpy(cs->pv.notes[j].text, c->after, sizeof cs->pv.notes[j].text);
    }
    cs->pv.gen   = live->gen;
    cs->pv.valid = 1;
}

static void swap_bytes(void *a, void *b, size_t n)
{
    unsigned char *p = a, *q = b, t[256];
    while (n) {
        size_t k = n < sizeof t ? n : sizeof t;
        memcpy(t, p, k);
        memcpy(p, q, k);
        memcpy(q, t, k);
        p += k; q += k; n -= k;
    }
}

/* The used slots only: the whole fixed arrays are 9 KB a side, three copies
 * each way, most of a frame's swap for a map with a few links and notes. */
static void swap_small(ChangeSet *cs, Map *live)
{
    TokenList t = live->tokens;
    live->tokens = cs->pv.tokens;
    cs->pv.tokens = t;
    int n = imax(live->nlinks, cs->pv.nlinks);
    swap_bytes(live->links, cs->pv.links, (size_t)n * sizeof *live->links);
    n = live->nlinks; live->nlinks = cs->pv.nlinks; cs->pv.nlinks = n;
    n = imax(live->nnotes, cs->pv.nnotes);
    swap_bytes(live->notes, cs->pv.notes, (size_t)n * sizeof *live->notes);
    n = live->nnotes; live->nnotes = cs->pv.nnotes; cs->pv.nnotes = n;
}

void cs_show(ChangeSet *cs, Map *live, int x0, int y0, int x1, int y1)
{
    PROF_ZONE("job.preview");
    if (cs->pv.shown) return;
    if (!cs->pv.valid || cs->pv.gen != live->gen) preview_build(cs, live);
    if (!cs->pv.saved && cs->ncells) cs->pv.saved = xmalloc((size_t)cs->ncells);
    cs->pv.x0 = x0; cs->pv.y0 = y0; cs->pv.x1 = x1; cs->pv.y1 = y1;
    if (cs->ncells && live->w == cs->w && live->h == cs->h) {
        int bx0 = imax(x0, 0) / CS_BLOCK, by0 = imax(y0, 0) / CS_BLOCK;
        int bx1 = imin((x1 + 1) / CS_BLOCK, cs->bw - 1), by1 = imin((y1 + 1) / CS_BLOCK, cs->bh - 1);
        for (int by = by0; by <= by1; by++)
            for (int bx = bx0; bx <= bx1; bx++) {
                int b = by * cs->bw + bx;
                for (int i = cs->block_start[b]; i < cs->block_start[b + 1]; i++) {
                    const CsCell *c = &cs->cells[i];
                    uint8_t *p = cell_byte(live, c->kind, c->x, c->y);
                    cs->pv.saved[i] = *p;
                    *p = c->kind == CS_FOG ? (uint8_t)((*p & ~FOG_ID) | c->after) : c->after;
                }
            }
    }
    swap_small(cs, live);
    cs->pv.shown = 1;
}

void cs_unshow(ChangeSet *cs, Map *live)
{
    if (!cs->pv.shown) return;
    swap_small(cs, live);
    if (cs->ncells && live->w == cs->w && live->h == cs->h) {
        int bx0 = imax(cs->pv.x0, 0) / CS_BLOCK, by0 = imax(cs->pv.y0, 0) / CS_BLOCK;
        int bx1 = imin((cs->pv.x1 + 1) / CS_BLOCK, cs->bw - 1), by1 = imin((cs->pv.y1 + 1) / CS_BLOCK, cs->bh - 1);
        for (int by = by0; by <= by1; by++)
            for (int bx = bx0; bx <= bx1; bx++) {
                int b = by * cs->bw + bx;
                for (int i = cs->block_start[b]; i < cs->block_start[b + 1]; i++) {
                    const CsCell *c = &cs->cells[i];
                    *cell_byte(live, c->kind, c->x, c->y) = cs->pv.saved[i];
                }
            }
    }
    cs->pv.shown = 0;
}
