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
    OP_ROUND,            /* the fight's round counter: x before, y after */
    OP_SPOTLIGHT,        /* which side has the spotlight: x before, y after */
    OP_CLOCK,            /* a clock's filled segments: x is the slot, y its generation, before/after */
    OP_FOG,              /* a tile's fog byte: before/after */
} OpKind;

/* One op is one cell or one token changing. Tile ops dominate -- a brush
 * fill across a 200x200 map records 40,000 of them -- so the op is kept to
 * 20 bytes and token payloads live in a side array the log owns: an add or
 * delete uses one slot, an edit two (before, after), a move none. */
typedef struct {
    uint8_t kind;
    int16_t x, y;        /* cell coords; for token ops, x is the token index */
    union {
        struct { uint8_t before, after; };       /* tile and edge ops */
        struct { int16_t ox, oy, nx, ny; };      /* OP_TOKEN_MOVE: from, to */
    };
    int32_t tok;         /* first slot in Undo.toks this op owns; every op
                            records it so truncating the ops truncates the
                            tokens too */
} Op;

/* The history is bounded. Past UNDO_MAX_OPS the oldest batches are dropped
 * until the log is back under UNDO_TRIM_TO, so the memmove that closes the
 * gap runs once per quarter-log of new ops rather than once per keystroke.
 * The cap is four fills of the largest map (512x512 is 262,144 ops a fill),
 * a little over 20 MB at the very worst and nothing at all in normal use;
 * before the payloads moved out of the op, one such fill alone was 60 MB. */
#define UNDO_MAX_OPS (4 * MAP_MAX_DIM * MAP_MAX_DIM)
#define UNDO_TRIM_TO (UNDO_MAX_OPS - UNDO_MAX_OPS / 4)

/* A flat op array plus batch boundaries: filling a rectangle or tracing a
 * wall run records many ops but undoes as one step, which is what a user
 * means by "undo that". */
typedef struct {
    Op  *ops;
    int  nops, cap_ops;

    Token *toks;         /* token payloads, in op order */
    int    ntoks, cap_toks;

    int *marks;          /* marks[i] = index of the first op of batch i */
    int  nmarks, cap_marks;

    int  depth;          /* batches currently applied; the redo boundary */
    int  open;           /* a batch is being built */
    int  started;        /* the open batch has recorded at least one op */
    int  trimmed;        /* batches dropped from the front so far (for tests
                            and the profiler; never consulted by the log) */
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

/* The round counter, so stepping a turn back with u puts the round back too. */
void undo_set_round(Undo *u, Map *m, int round);
void undo_set_spotlight(Undo *u, Map *m, int side);
/* A tile's fog byte: which patch it belongs to and what is lit by hand. */
void undo_set_fog(Undo *u, Map *m, int x, int y, uint8_t f);
/* A clock's value; the slot must hold a clock. */
void undo_set_clock(Undo *u, Map *m, int slot, int value);

int  undo_undo(Undo *u, Map *m);

/* Unwinds back to `depth`, but only through batches that are nothing but
 * moves of the tokens in `idx`. Stops at anything else, so cancelling a move cannot
 * quietly swallow an edit made part way through it. Returns how many batches
 * were undone. This is what makes a cancel leave no trace: the steps are
 * taken back out of the history rather than answered with a step back. */
int  undo_rewind_moves(Undo *u, Map *m, int depth, const int *idx, int nidx);
int  undo_redo(Undo *u, Map *m);

static inline int undo_can_undo(const Undo *u) { return u->depth > 0; }
static inline int undo_can_redo(const Undo *u) { return u->depth < u->nmarks; }

#endif /* VTT_UNDO_H */
