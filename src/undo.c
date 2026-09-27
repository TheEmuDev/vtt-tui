#include "undo.h"

#include <stdlib.h>
#include <string.h>

#include "link.h"
#include "prof.h"
#include "util.h"

void undo_init(Undo *u) { memset(u, 0, sizeof *u); }

void undo_free(Undo *u)
{
    free(u->ops);
    free(u->toks);
    free(u->marks);
    memset(u, 0, sizeof *u);
}

void undo_clear(Undo *u)
{
    u->nops = u->ntoks = u->nmarks = u->depth = 0;
    u->open = u->started = u->trimmed = 0;
    u->nest = u->stroke = 0;
    u->stamp++;
}

void undo_begin(Undo *u)
{
    if (u->open && u->stroke && u->nest == 0) undo_end(u);
    if (u->open) { u->nest++; return; }
    u->open    = 1;
    u->started = 0;
    u->nest    = 0;
    u->stroke  = 0;
}

void undo_stroke(Undo *u)
{
    if (u->open) return;
    undo_begin(u);
    u->stroke = 1;
}

void undo_stroke_end(Undo *u)
{
    if (u->open && u->stroke && u->nest == 0) undo_end(u);
}

/* Drops whole batches from the front until the log is under UNDO_TRIM_TO.
 * The open batch is never dropped, whatever its size: the cap bounds the
 * history, not the operation the user is in the middle of. */
static void trim(Undo *u)
{
    if (u->nops <= UNDO_MAX_OPS) return;
    PROF_ZONE("undo.trim");

    /* b = how many batches to drop: the fewest that get under the mark,
     * never the last one. */
    int b = 0;
    while (b < u->nmarks - 1 && u->nops - u->marks[b] > UNDO_TRIM_TO) b++;
    if (b == 0) return;

    int dops  = u->marks[b];
    int dtoks = u->ops[dops].tok;

    memmove(u->ops, u->ops + dops, (size_t)(u->nops - dops) * sizeof(Op));
    memmove(u->toks, u->toks + dtoks, (size_t)(u->ntoks - dtoks) * sizeof(Token));
    u->nops  -= dops;
    u->ntoks -= dtoks;
    for (int i = 0; i < u->nops; i++) u->ops[i].tok -= dtoks;

    for (int i = b; i < u->nmarks; i++) u->marks[i - b] = u->marks[i] - dops;
    u->nmarks  -= b;
    u->depth   -= b;
    u->trimmed += b;
}

void undo_end(Undo *u)
{
    if (!u->open) return;
    if (u->nest) { u->nest--; return; }
    u->open   = 0;
    u->stroke = 0;

    /* A batch that recorded nothing must not consume an undo step. */
    if (!u->started) return;

    u->nmarks++;
    u->depth = u->nmarks;
    trim(u);
}

/* Opens the batch for real, on its first op. Deferring this to here is what
 * keeps a keystroke that turns out to change nothing from throwing away the
 * redo tail: the future stops following from the present only once something
 * has actually happened. */
static void batch_start(Undo *u)
{
    if (u->started) return;

    if (u->depth < u->nmarks) {
        int keep = u->marks[u->depth];
        u->ntoks  = u->ops[keep].tok;    /* the first dropped op's slots go too */
        u->nops   = keep;
        u->nmarks = u->depth;
    }
    if (u->nmarks == u->cap_marks) {
        u->cap_marks = u->cap_marks ? u->cap_marks * 2 : 64;
        u->marks = xrealloc(u->marks, (size_t)u->cap_marks * sizeof(int));
    }
    u->marks[u->nmarks] = u->nops;
    u->started = 1;
}

static Op *push(Undo *u)
{
    batch_start(u);
    u->stamp++;

    if (u->nops == u->cap_ops) {
        u->cap_ops = u->cap_ops ? u->cap_ops * 2 : 256;
        u->ops = xrealloc(u->ops, (size_t)u->cap_ops * sizeof(Op));
    }
    Op *o = &u->ops[u->nops++];
    memset(o, 0, sizeof *o);
    o->tok = u->ntoks;
    return o;
}

/* Appends a token payload; the op that owns it was pushed just before, so
 * its slots are contiguous from op->tok. */
static void push_token(Undo *u, const Token *t)
{
    if (u->ntoks == u->cap_toks) {
        u->cap_toks = u->cap_toks ? u->cap_toks * 2 : 16;
        u->toks = xrealloc(u->toks, (size_t)u->cap_toks * sizeof(Token));
    }
    u->toks[u->ntoks++] = *t;
}

static void set_cell(Undo *u, uint8_t kind, int x, int y, uint8_t before, uint8_t after)
{
    Op *o = push(u);
    o->kind   = kind;
    o->x      = (int16_t)x;
    o->y      = (int16_t)y;
    o->before = before;
    o->after  = after;
}

void undo_set_tile(Undo *u, Map *m, int x, int y, uint8_t kind)
{
    if (!map_in_bounds(m, x, y)) return;
    uint8_t before = map_tile(m, x, y);
    if (before == kind) return;

    set_cell(u, OP_TILE, x, y, before, kind);
    map_set_tile(m, x, y, kind);
}

void undo_set_vedge(Undo *u, Map *m, int x, int y, uint8_t kind)
{
    if (x < 0 || x > m->w || y < 0 || y >= m->h) return;
    uint8_t before = map_vedge(m, x, y);
    if (before == kind) return;

    set_cell(u, OP_VEDGE, x, y, before, kind);
    map_set_vedge(m, x, y, kind);
}

void undo_set_hedge(Undo *u, Map *m, int x, int y, uint8_t kind)
{
    if (x < 0 || x >= m->w || y < 0 || y > m->h) return;
    uint8_t before = map_hedge(m, x, y);
    if (before == kind) return;

    set_cell(u, OP_HEDGE, x, y, before, kind);
    map_set_hedge(m, x, y, kind);
}

int undo_add_token(Undo *u, Map *m, Token t)
{
    int idx = tokens_add(&m->tokens, t);

    Op *o = push(u);
    o->kind = OP_TOKEN_ADD;
    o->x    = (int16_t)idx;
    push_token(u, &m->tokens.v[idx]);
    map_touch(m);
    return idx;
}

void undo_del_token(Undo *u, Map *m, int idx)
{
    if (idx < 0 || idx >= m->tokens.n) return;

    Op *o = push(u);
    o->kind = OP_TOKEN_DEL;
    o->x    = (int16_t)idx;
    push_token(u, &m->tokens.v[idx]);
    tokens_remove(&m->tokens, idx);
    map_touch(m);
}

void undo_move_token(Undo *u, Map *m, int idx, int nx, int ny)
{
    if (idx < 0 || idx >= m->tokens.n) return;
    Token *t = &m->tokens.v[idx];
    if (t->x == nx && t->y == ny) return;

    Op *o = push(u);
    o->kind = OP_TOKEN_MOVE;
    o->x  = (int16_t)idx;
    o->y  = 0;
    o->ox = t->x;
    o->oy = t->y;
    o->nx = (int16_t)nx;
    o->ny = (int16_t)ny;

    t->x = (int16_t)nx;
    t->y = (int16_t)ny;
    map_touch(m);
}

void undo_edit_token(Undo *u, Map *m, int idx, Token after)
{
    if (idx < 0 || idx >= m->tokens.n) return;

    Token *t = &m->tokens.v[idx];
    if (token_equal(t, &after)) return;   /* nothing changed */

    Op *o = push(u);
    o->kind = OP_TOKEN_EDIT;
    o->x    = (int16_t)idx;
    push_token(u, t);
    push_token(u, &after);

    *t = after;
    map_touch(m);
}

void undo_set_round(Undo *u, Map *m, int round)
{
    round = iclamp(round, 0, INT16_MAX);
    if (m->round == round) return;

    Op *o = push(u);
    o->kind = OP_ROUND;
    o->x    = (int16_t)m->round;
    o->y    = (int16_t)round;
    m->round    = round;
    map_touch(m);
}

void undo_set_spotlight(Undo *u, Map *m, int side)
{
    side = side ? SPOTLIGHT_GM : SPOTLIGHT_PLAYERS;
    if (m->spotlight == side) return;

    Op *o = push(u);
    o->kind = OP_SPOTLIGHT;
    o->x    = (int16_t)m->spotlight;
    o->y    = (int16_t)side;
    m->spotlight = side;
    map_touch(m);
}

void undo_set_fog(Undo *u, Map *m, int x, int y, uint8_t f)
{
    if (!map_in_bounds(m, x, y)) return;
    /* LIT and RIM are where the creatures stand this second, rebuilt by
     * fog_recompute; they are kept out of the log so an undo can never put
     * back a light nobody is holding. */
    const uint8_t derived = FOG_LIT | FOG_RIM;
    uint8_t now = m->fog[(size_t)y * (size_t)m->w + (size_t)x];
    uint8_t was = (uint8_t)(now & ~derived);
    f = (uint8_t)(f & ~derived);
    if (was == f) { m->fog[(size_t)y * (size_t)m->w + (size_t)x] = (uint8_t)(f | (now & derived)); return; }
    Op *o = push(u);
    o->kind   = OP_FOG;
    o->x      = (int16_t)x;
    o->y      = (int16_t)y;
    o->before = was;
    o->after  = f;
    map_fog_set(m, x, y, (uint8_t)(f | (now & derived)));
    map_touch(m);
}

int undo_set_note(Undo *u, Map *m, int x, int y, const char *text)
{
    if (!map_in_bounds(m, x, y)) return 0;
    while (*text == ' ') text++;
    const char *was = map_note_at(m, x, y);
    if (!strcmp(was ? was : "", text)) return 1;
    Token before, after;
    memset(&before, 0, sizeof before);
    memset(&after, 0, sizeof after);
    str_lcpy(before.note, was ? was : "", sizeof before.note);
    str_lcpy(after.note, text, sizeof after.note);
    if (!map_note_set(m, x, y, text)) return 0;

    Op *o = push(u);
    o->kind = OP_NOTE;
    o->x    = (int16_t)x;
    o->y    = (int16_t)y;
    push_token(u, &before);
    push_token(u, &after);
    return 1;
}

/* An area in a token slot: the undo log's side array holds tokens, and an
 * area is a name and four numbers, which a token has room for. */
static Token area_slot(const Map *m, const Area *ar)
{
    Token t;
    memset(&t, 0, sizeof t);
    if (!ar) return t;
    t.size = (uint8_t)(ar - m->areas);     /* its place in naming order */
    str_lcpy(t.label, ar->name, sizeof t.label);
    t.x = ar->x0;
    t.y = ar->y0;
    t.counters[0].value = ar->x1;
    t.counters[0].max   = ar->y1;
    t.counters[1].value = ar->level;
    t.counters[1].max   = ar->floor;
    return t;
}

static void area_from_slot(Map *m, const Token *want, const Token *other)
{
    if (!want->label[0]) { (void)map_area_remove(m, other->label); return; }
    int i = map_area_set(m, want->label, want->x, want->y, want->counters[0].value, want->counters[0].max);
    if (i < 0) return;
    m->areas[i].level = (int8_t)want->counters[1].value;
    m->areas[i].floor = (uint8_t)want->counters[1].max;
    /* Put back where it was: naming order is precedence among equals. */
    int at = want->size;
    if (i > at && at < m->nareas) {
        Area keep = m->areas[i];
        memmove(&m->areas[at + 1], &m->areas[at], (size_t)(i - at) * sizeof keep);
        m->areas[at] = keep;
    }
}

static void record_area(Undo *u, const Token *before, const Token *after)
{
    Op *o = push(u);
    o->kind = OP_AREA;
    push_token(u, before);
    push_token(u, after);
}

int undo_set_area(Undo *u, Map *m, const char *name, int x0, int y0, int x1, int y1)
{
    int   i = map_area_find(m, name);
    Token before = area_slot(m, i >= 0 ? &m->areas[i] : NULL);
    /* A rename by case keeps the old spelling's slot: say it as removed and
     * added, so undo puts the old spelling back. */
    int   j = map_area_set(m, name, x0, y0, x1, y1);
    if (j < 0) return 0;
    Token after = area_slot(m, &m->areas[j]);
    if (i >= 0 && !memcmp(&before, &after, sizeof before)) return 1;
    record_area(u, &before, &after);
    return 1;
}

int undo_set_floor(Undo *u, Map *m, const char *name, int on, int level)
{
    int i = map_area_find(m, name);
    if (i < 0) return 0;
    Area *ar = &m->areas[i];
    if (ar->floor == (on != 0) && (!on || ar->level == level)) return 1;
    Token before = area_slot(m, ar);
    ar->floor = (uint8_t)(on != 0);
    ar->level = (int8_t)(on ? level : 0);
    map_touch(m);
    Token after = area_slot(m, ar);
    record_area(u, &before, &after);
    return 1;
}

int undo_remove_area(Undo *u, Map *m, const char *name)
{
    int i = map_area_find(m, name);
    if (i < 0) return 0;
    Token before = area_slot(m, &m->areas[i]), after = area_slot(m, NULL);
    map_area_remove(m, name);
    record_area(u, &before, &after);
    return 1;
}

/* A link in a token slot, carried whole in the note: size 1 says there is
 * one, 0 that there is none. */
_Static_assert(sizeof(Link) <= TOKEN_NOTE_MAX, "a link fits in a token's note");

static Token link_slot(const Link *l)
{
    Token t;
    memset(&t, 0, sizeof t);
    if (!l) return t;
    t.size = 1;
    memcpy(t.note, l, sizeof *l);
    return t;
}

static void link_from_slot(Map *m, const Token *want, const Token *other)
{
    Link l;
    if (want->size) { memcpy(&l, want->note, sizeof l); (void)link_put(m, &l); return; }
    memcpy(&l, other->note, sizeof l);
    (void)link_remove(m, l.num);
}

static void record_link(Undo *u, const Token *before, const Token *after)
{
    Op *o = push(u);
    o->kind = OP_LINK;
    push_token(u, before);
    push_token(u, after);
}

int undo_set_link(Undo *u, Map *m, const Link *l)
{
    int   i = link_find(m, l->num);
    Token before = link_slot(i >= 0 ? &m->links[i] : NULL);
    if (i >= 0 && !memcmp(&m->links[i], l, sizeof *l)) return 1;
    if (link_put(m, l) < 0) return 0;
    Token after = link_slot(l);
    record_link(u, &before, &after);
    return 1;
}

int undo_remove_link(Undo *u, Map *m, int num)
{
    int i = link_find(m, num);
    if (i < 0) return 0;
    Token before = link_slot(&m->links[i]), after = link_slot(NULL);
    link_remove(m, num);
    record_link(u, &before, &after);
    return 1;
}

_Static_assert(sizeof(NamedRoll) <= TOKEN_NOTE_MAX, "a named roll fits in a token's note");

void undo_set_roll(Undo *u, Map *m, int slot, const NamedRoll *r)
{
    if (slot < 0 || slot >= ROLL_MAX) return;
    NamedRoll now = *r;
    if (!now.name[0]) memset(&now, 0, sizeof now);
    if (!memcmp(&m->rolls[slot], &now, sizeof now)) return;
    Token before, after;
    memset(&before, 0, sizeof before);
    memset(&after, 0, sizeof after);
    memcpy(before.note, &m->rolls[slot], sizeof now);
    memcpy(after.note, &now, sizeof now);
    m->rolls[slot] = now;
    map_touch(m);
    Op *o = push(u);
    o->kind = OP_ROLL;
    o->x = (int16_t)slot;
    push_token(u, &before);
    push_token(u, &after);
}

void undo_set_clock(Undo *u, Map *m, int slot, int value)
{
    if (slot < 0 || slot >= CLOCK_MAX || !m->clocks[slot].name[0]) return;
    Clock *c = &m->clocks[slot];
    value = iclamp(value, 0, c->size);
    if (c->value == value) return;

    Op *o = push(u);
    o->kind   = OP_CLOCK;
    o->x      = (int16_t)slot;
    o->y      = (int16_t)c->gen;
    o->before = c->value;
    o->after  = (uint8_t)value;
    c->value    = (uint8_t)value;
    map_touch(m);
}

/* Re-inserts a token at a specific index so undoing a delete restores the
 * ordering that hit-testing depends on. */
static void token_insert_at(TokenList *l, int idx, Token t)
{
    tokens_add(l, t);                       /* grows the array */
    if (idx < 0 || idx >= l->n) return;
    memmove(&l->v[idx + 1], &l->v[idx], (size_t)(l->n - 1 - idx) * sizeof(Token));
    l->v[idx] = t;
}

static void apply(const Undo *u, Map *m, const Op *o, int forward)
{
    /* Token slots are only reached for token ops: a cell op's tok is a
     * position, not a payload, and may be one past the end. */
    const Token *tok = u->toks + o->tok;

    switch (o->kind) {
    case OP_TILE:
        map_set_tile(m, o->x, o->y, forward ? o->after : o->before);
        break;
    case OP_VEDGE:
        map_set_vedge(m, o->x, o->y, forward ? o->after : o->before);
        break;
    case OP_HEDGE:
        map_set_hedge(m, o->x, o->y, forward ? o->after : o->before);
        break;
    case OP_TOKEN_ADD:
        if (forward) token_insert_at(&m->tokens, o->x, *tok);
        else         tokens_remove(&m->tokens, o->x);
        break;
    case OP_TOKEN_DEL:
        if (forward) tokens_remove(&m->tokens, o->x);
        else         token_insert_at(&m->tokens, o->x, *tok);
        break;
    case OP_TOKEN_EDIT:
        if (o->x >= 0 && o->x < m->tokens.n)
            m->tokens.v[o->x] = forward ? tok[1] : tok[0];
        break;
    case OP_ROUND:
        m->round = forward ? o->y : o->x;
        break;
    case OP_SPOTLIGHT:
        m->spotlight = forward ? o->y : o->x;
        break;
    case OP_FOG: {
        uint8_t now = map_in_bounds(m, o->x, o->y) ? m->fog[(size_t)o->y * (size_t)m->w + (size_t)o->x] : 0;
        map_fog_set(m, o->x, o->y, (uint8_t)((forward ? o->after : o->before) |
                                             (now & (FOG_LIT | FOG_RIM))));
        break;
    }
    case OP_CLOCK:
        /* The slot may have been dropped and started again since. The op
         * carries the generation it was recorded against, so a new clock
         * in the same slot is left alone rather than handed the old one's
         * history. */
        if (o->x >= 0 && o->x < CLOCK_MAX && m->clocks[o->x].name[0] &&
            m->clocks[o->x].gen == (uint8_t)o->y) {
            uint8_t v = forward ? o->after : o->before;
            m->clocks[o->x].value = v > m->clocks[o->x].size ? m->clocks[o->x].size : v;
        }
        break;
    case OP_AREA:
        if (forward) area_from_slot(m, &tok[1], &tok[0]);
        else         area_from_slot(m, &tok[0], &tok[1]);
        break;
    case OP_LINK:
        if (forward) link_from_slot(m, &tok[1], &tok[0]);
        else         link_from_slot(m, &tok[0], &tok[1]);
        break;
    case OP_ROLL:
        if (o->x >= 0 && o->x < ROLL_MAX)
            memcpy(&m->rolls[o->x], (forward ? tok[1] : tok[0]).note, sizeof(NamedRoll));
        break;
    case OP_NOTE:
        /* Putting a note back takes the slot its removal freed. */
        (void)map_note_set(m, o->x, o->y, forward ? tok[1].note : tok[0].note);
        break;
    case OP_TOKEN_MOVE:
        if (o->x >= 0 && o->x < m->tokens.n) {
            Token *t = &m->tokens.v[o->x];
            t->x = forward ? o->nx : o->ox;
            t->y = forward ? o->ny : o->oy;
        }
        break;
    default:
        break;
    }
    map_touch(m);
}

static int batch_end(const Undo *u, int batch)
{
    return batch + 1 < u->nmarks ? u->marks[batch + 1] : u->nops;
}

void undo_abort(Undo *u, Map *m)
{
    if (!u->open) return;
    u->open = u->nest = u->stroke = 0;
    if (!u->started) return;
    int lo = u->marks[u->nmarks];
    for (int i = u->nops - 1; i >= lo; i--) apply(u, m, &u->ops[i], 0);
    u->ntoks = u->ops[lo].tok;
    u->nops  = lo;
    u->started = 0;
    u->stamp++;
}

int undo_undo(Undo *u, Map *m)
{
    if (u->open) { u->nest = 0; undo_end(u); }
    if (u->depth == 0) return 0;
    PROF_ZONE("undo.step");
    u->stamp++;

    u->depth--;
    int lo = u->marks[u->depth];
    int hi = batch_end(u, u->depth);

    /* Reverse order, so overlapping edits within one batch unwind correctly. */
    for (int i = hi - 1; i >= lo; i--) apply(u, m, &u->ops[i], 0);
    return 1;
}

/* Is every op in this batch a move of one of those tokens? */
static int batch_is_moves_of(const Undo *u, int b, const int *idx, int nidx)
{
    int lo = u->marks[b], hi = batch_end(u, b);
    if (lo >= hi) return 0;

    for (int i = lo; i < hi; i++) {
        if (u->ops[i].kind != OP_TOKEN_MOVE) return 0;

        int mine = 0;
        for (int j = 0; j < nidx; j++)
            if (u->ops[i].x == idx[j]) { mine = 1; break; }
        if (!mine) return 0;
    }
    return 1;
}

int undo_rewind_moves(Undo *u, Map *m, int depth, const int *idx, int nidx)
{
    if (u->open) { u->nest = 0; undo_end(u); }
    if (depth < 0) depth = 0;

    int undone = 0;
    while (u->depth > depth && batch_is_moves_of(u, u->depth - 1, idx, nidx)) {
        undo_undo(u, m);
        undone++;
    }
    return undone;
}

int undo_redo(Undo *u, Map *m)
{
    if (u->open) { u->nest = 0; undo_end(u); }
    if (u->depth >= u->nmarks) return 0;
    PROF_ZONE("undo.step");
    u->stamp++;

    int lo = u->marks[u->depth];
    int hi = batch_end(u, u->depth);
    for (int i = lo; i < hi; i++) apply(u, m, &u->ops[i], 1);

    u->depth++;
    return 1;
}
