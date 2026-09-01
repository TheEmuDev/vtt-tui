#include "undo.h"

#include <stdlib.h>
#include <string.h>

#include "util.h"

void undo_init(Undo *u) { memset(u, 0, sizeof *u); }

void undo_free(Undo *u)
{
    free(u->ops);
    free(u->marks);
    memset(u, 0, sizeof *u);
}

void undo_clear(Undo *u)
{
    u->nops = u->nmarks = u->depth = 0;
    u->open = u->started = 0;
}

void undo_begin(Undo *u)
{
    if (u->open) return;
    u->open    = 1;
    u->started = 0;
}

void undo_end(Undo *u)
{
    if (!u->open) return;
    u->open = 0;

    /* A batch that recorded nothing must not consume an undo step. */
    if (!u->started) return;

    u->nmarks++;
    u->depth = u->nmarks;
}

/* Opens the batch for real, on its first op. Deferring this to here is what
 * keeps a keystroke that turns out to change nothing from throwing away the
 * redo tail: the future stops following from the present only once something
 * has actually happened. */
static void batch_start(Undo *u)
{
    if (u->started) return;

    if (u->depth < u->nmarks) {
        u->nops   = u->marks[u->depth];
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
    return o;
}

void undo_set_tile(Undo *u, Map *m, int x, int y, uint8_t kind)
{
    if (!map_in_bounds(m, x, y)) return;
    uint8_t before = map_tile(m, x, y);
    if (before == kind) return;

    Op *o = push(u);
    o->kind = OP_TILE;
    o->x = (int16_t)x;
    o->y = (int16_t)y;
    o->before = before;
    o->after  = kind;
    map_set_tile(m, x, y, kind);
}

void undo_set_vedge(Undo *u, Map *m, int x, int y, uint8_t kind)
{
    if (x < 0 || x > m->w || y < 0 || y >= m->h) return;
    uint8_t before = map_vedge(m, x, y);
    if (before == kind) return;

    Op *o = push(u);
    o->kind = OP_VEDGE;
    o->x = (int16_t)x;
    o->y = (int16_t)y;
    o->before = before;
    o->after  = kind;
    map_set_vedge(m, x, y, kind);
}

void undo_set_hedge(Undo *u, Map *m, int x, int y, uint8_t kind)
{
    if (x < 0 || x >= m->w || y < 0 || y > m->h) return;
    uint8_t before = map_hedge(m, x, y);
    if (before == kind) return;

    Op *o = push(u);
    o->kind = OP_HEDGE;
    o->x = (int16_t)x;
    o->y = (int16_t)y;
    o->before = before;
    o->after  = kind;
    map_set_hedge(m, x, y, kind);
}

int undo_add_token(Undo *u, Map *m, Token t)
{
    int idx = tokens_add(&m->tokens, t);

    Op *o = push(u);
    o->kind  = OP_TOKEN_ADD;
    o->x     = (int16_t)idx;
    o->token = m->tokens.v[idx];
    m->modified = 1;
    return idx;
}

void undo_del_token(Undo *u, Map *m, int idx)
{
    if (idx < 0 || idx >= m->tokens.n) return;

    Op *o = push(u);
    o->kind  = OP_TOKEN_DEL;
    o->x     = (int16_t)idx;
    o->token = m->tokens.v[idx];
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
    o->nx = (int16_t)nx;
    o->ny = (int16_t)ny;
    /* The origin is stashed in the token payload so the inverse needs no
     * extra fields. */
    o->token = *t;

    t->x = (int16_t)nx;
    t->y = (int16_t)ny;
    m->modified = 1;
}

void undo_edit_token(Undo *u, Map *m, int idx, Token after)
{
    if (idx < 0 || idx >= m->tokens.n) return;

    Token *t = &m->tokens.v[idx];
    if (memcmp(t, &after, sizeof after) == 0) return;   /* nothing changed */

    Op *o = push(u);
    o->kind   = OP_TOKEN_EDIT;
    o->x      = (int16_t)idx;
    o->token  = *t;
    o->token2 = after;

    *t = after;
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

static void apply(Map *m, const Op *o, int forward)
{
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
        if (forward) token_insert_at(&m->tokens, o->x, o->token);
        else         tokens_remove(&m->tokens, o->x);
        break;
    case OP_TOKEN_DEL:
        if (forward) tokens_remove(&m->tokens, o->x);
        else         token_insert_at(&m->tokens, o->x, o->token);
        break;
    case OP_TOKEN_EDIT:
        if (o->x >= 0 && o->x < m->tokens.n)
            m->tokens.v[o->x] = forward ? o->token2 : o->token;
        break;
    case OP_TOKEN_MOVE:
        if (o->x >= 0 && o->x < m->tokens.n) {
            Token *t = &m->tokens.v[o->x];
            t->x = forward ? o->nx : o->token.x;
            t->y = forward ? o->ny : o->token.y;
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

    u->depth--;
    int lo = u->marks[u->depth];
    int hi = batch_end(u, u->depth);

    /* Reverse order, so overlapping edits within one batch unwind correctly. */
    for (int i = hi - 1; i >= lo; i--) apply(m, &u->ops[i], 0);
    return 1;
}

/* Is every op in this batch a move of that one token? */
static int batch_is_moves_of(const Undo *u, int b, int idx)
{
    int lo = u->marks[b], hi = batch_end((Undo *)u, b);
    if (lo >= hi) return 0;

    for (int i = lo; i < hi; i++)
        if (u->ops[i].kind != OP_TOKEN_MOVE || u->ops[i].x != idx) return 0;
    return 1;
}

int undo_rewind_moves(Undo *u, Map *m, int depth, int idx)
{
    if (u->open) undo_end(u);
    if (depth < 0) depth = 0;

    int undone = 0;
    while (u->depth > depth && batch_is_moves_of(u, u->depth - 1, idx)) {
        undo_undo(u, m);
        undone++;
    }
    return undone;
}

int undo_redo(Undo *u, Map *m)
{
    if (u->open) undo_end(u);
    if (u->depth >= u->nmarks) return 0;

    int lo = u->marks[u->depth];
    int hi = batch_end(u, u->depth);
    for (int i = lo; i < hi; i++) apply(m, &u->ops[i], 1);

    u->depth++;
    return 1;
}
