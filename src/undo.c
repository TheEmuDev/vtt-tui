#include "undo.h"

#include <stdlib.h>
#include <string.h>

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
}

void undo_begin(Undo *u)
{
    if (u->open) return;
    u->open    = 1;
    u->started = 0;
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
    u->open = 0;

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
    m->modified = 1;
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
    m->modified = 1;
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
    m->modified = 1;
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
    m->modified = 1;
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
    m->modified = 1;
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
    m->modified = 1;
}

static int batch_end(const Undo *u, int batch)
{
    return batch + 1 < u->nmarks ? u->marks[batch + 1] : u->nops;
}

int undo_undo(Undo *u, Map *m)
{
    if (u->open) undo_end(u);
    if (u->depth == 0) return 0;
    PROF_ZONE("undo.step");

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
    if (u->open) undo_end(u);
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
    if (u->open) undo_end(u);
    if (u->depth >= u->nmarks) return 0;
    PROF_ZONE("undo.step");

    int lo = u->marks[u->depth];
    int hi = batch_end(u, u->depth);
    for (int i = lo; i < hi; i++) apply(u, m, &u->ops[i], 1);

    u->depth++;
    return 1;
}
