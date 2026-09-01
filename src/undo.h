#ifndef VTT_UNDO_H
#define VTT_UNDO_H

#include "map.h"

typedef enum {
    OP_TILE,
    OP_VEDGE,
    OP_HEDGE,
    OP_TOKEN_ADD,
    OP_TOKEN_DEL,
    OP_TOKEN_MOVE,
    OP_TOKEN_EDIT,
} OpKind;

typedef struct {
    uint8_t kind;
    int16_t x, y;        /* cell coords; for token ops, x is the token index */
    uint8_t before, after;
    int16_t nx, ny;      /* destination for OP_TOKEN_MOVE */
    Token   token;       /* payload for add/delete; the 'before' of an edit */
    Token   token2;      /* the 'after' of an edit */
} Op;

/* A flat op array plus batch boundaries: filling a rectangle or tracing a
 * wall run records many ops but undoes as one step, which is what a user
 * means by "undo that". */
typedef struct {
    Op  *ops;
    int  nops, cap_ops;

    int *marks;          /* marks[i] = index of the first op of batch i */
    int  nmarks, cap_marks;

    int  depth;          /* batches currently applied; the redo boundary */
    int  open;           /* a batch is being built */
    int  started;        /* the open batch has recorded at least one op */
} Undo;

void undo_init(Undo *u);
void undo_free(Undo *u);
void undo_clear(Undo *u);

/* Opens a batch. Nested calls are ignored, so a helper that records can be
 * called from inside a larger operation without splitting it. */
void undo_begin(Undo *u);
void undo_end(Undo *u);

/* Records and applies. Each is a no-op when nothing would change. */
void undo_set_tile(Undo *u, Map *m, int x, int y, uint8_t kind);
void undo_set_vedge(Undo *u, Map *m, int x, int y, uint8_t kind);
void undo_set_hedge(Undo *u, Map *m, int x, int y, uint8_t kind);
int  undo_add_token(Undo *u, Map *m, Token t);
void undo_del_token(Undo *u, Map *m, int idx);
void undo_move_token(Undo *u, Map *m, int idx, int nx, int ny);

/* Replaces a token wholesale, which is how relabelling, resizing and status
 * markers become undoable without an op per field. */
void undo_edit_token(Undo *u, Map *m, int idx, Token after);

int  undo_undo(Undo *u, Map *m);

/* Unwinds back to `depth`, but only through batches that are nothing but
 * moves of token `idx`. Stops at anything else, so cancelling a move cannot
 * quietly swallow an edit made part way through it. Returns how many batches
 * were undone. This is what makes a cancel leave no trace: the steps are
 * taken back out of the history rather than answered with a step back. */
int  undo_rewind_moves(Undo *u, Map *m, int depth, int idx);   /* 1 if a batch was reverted */
int  undo_redo(Undo *u, Map *m);

static inline int undo_can_undo(const Undo *u) { return u->depth > 0; }
static inline int undo_can_redo(const Undo *u) { return u->depth < u->nmarks; }

#endif /* VTT_UNDO_H */
