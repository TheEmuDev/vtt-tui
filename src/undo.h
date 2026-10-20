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
    OP_NOTE,             /* a square's note: two token slots carry the text before
                            and after in .note ("" for none); x, y the square */
    OP_AREA,             /* a named area: two token slots carry it before and
                            after (.label the name, "" for none; the box in
                            x, y and counters[0].value/max) */
    OP_LINK,             /* a link: two token slots carry it before and after
                            (the Link in .note, size 0 for none) */
    OP_ROLL,             /* a named roll: x is the slot; two token slots carry
                            the NamedRoll before and after in .note */
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

/* The history is bounded twice. Past UNDO_MAX_OPS ops, or past UNDO_MAX_BYTES
 * of ops and their token slots, the oldest batches are dropped until the log
 * is back under three quarters of the cap it crossed, so the memmove that
 * closes the gap runs once per quarter-log rather than once per keystroke.
 * The ops cap is four fills of the largest map (512x512 is 262,144 ops a fill,
 * 20 MB of ops); the bytes cap is what a creature's op makes needed, since it
 * carries one or two 256-byte slots (docs/reviews/2026-10-20-4-memory.md, M1:
 * 1,500 scene restores of 500 creatures held 287 MB before it). With both the
 * log holds at most UNDO_MAX_BYTES and one batch: four fills of cells, about
 * 63,000 creature edits, or 121,000 creatures added or removed. A batch is
 * never dropped while it is the only one. */
#define UNDO_MAX_OPS   (4 * MAP_MAX_DIM * MAP_MAX_DIM)
#define UNDO_TRIM_TO   (UNDO_MAX_OPS - UNDO_MAX_OPS / 4)
#define UNDO_MAX_BYTES ((size_t)32 << 20)
#define UNDO_TRIM_BYTES (UNDO_MAX_BYTES - UNDO_MAX_BYTES / 4)

/* Whose batch one is, when it is not the GM's own (docs/V2.md, D8): a job's
 * landing carries the job's number and the name of the agent that had it, so
 * the channel's `undo` and `job N undo` find it. Kept apart from the marks,
 * a few among thousands: a log no agent wrote to carries none. */
#define UNDO_WHO_MAX 32
typedef struct {
    int  batch;          /* which batch, as marks[] counts them */
    int  job;            /* 0 for a change with no job (--apply on a file) */
    char who[UNDO_WHO_MAX];
} UndoTag;

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

    UndoTag *tags;       /* in batch order; a batch dropped takes its tag along */
    int      ntags, cap_tags;

    int  depth;          /* batches currently applied; the redo boundary */
    int  open;           /* a batch is being built */
    int  nest;           /* undo_begin calls inside it still to be ended */
    int  stroke;         /* the open batch is a stroke (undo_stroke) */
    int  started;        /* the open batch has recorded at least one op */
    int  trimmed;        /* batches dropped from the front so far (for tests
                            and the profiler; never consulted by the log) */
    unsigned stamp;      /* bumped by anything that changes the log: a record,
                            an undo, a redo, a clear. Equal stamps mean the log
                            is as it was, which is how the control channel
                            knows its batch is still the last thing done. */
} Undo;

void undo_init(Undo *u);
void undo_free(Undo *u);
void undo_clear(Undo *u);

/* Opens a batch. Calls nest: a helper that opens and ends its own batch can
 * be called from inside a larger operation, and only the outermost end
 * closes it, so the operation stays one step. Opened while a stroke is open,
 * it ends the stroke first -- another tool is a break in the drawing. An end
 * with nothing nested closes whatever is open. */
void undo_begin(Undo *u);
void undo_end(Undo *u);

/* A stroke: one batch held open across keystrokes, wall mode's pen-down
 * run, so u takes back the whole run. undo_stroke opens one if nothing is
 * open and may be called on every step; undo_stroke_end closes it. */
void undo_stroke(Undo *u);
void undo_stroke_end(Undo *u);

/* Between keystrokes nothing may be open but a stroke: an operation that
 * began a batch and never ended it would swallow the next one. The tests
 * check this after every key. */
static inline int undo_balanced(const Undo *u)
{
    return !u->open || (u->stroke && u->nest == 0);
}

/* Takes back everything the open batch has recorded and closes it, as if
 * it had never been begun: for an operation that fails part way. The redo
 * tail its first op let go of does not come back. */
void undo_abort(Undo *u, Map *m);

/* Records and applies. Each is a no-op when nothing would change. */
void undo_set_tile(Undo *u, Map *m, int x, int y, uint8_t kind);
void undo_set_vedge(Undo *u, Map *m, int x, int y, uint8_t kind);
void undo_set_hedge(Undo *u, Map *m, int x, int y, uint8_t kind);
int  undo_add_token(Undo *u, Map *m, Token t);
void undo_del_token(Undo *u, Map *m, int idx);
void undo_move_token(Undo *u, Map *m, int idx, int nx, int ny);

/* Replaces a token wholesale, which is how relabeling, resizing and status
 * markers become undoable without an op per field. */
void undo_edit_token(Undo *u, Map *m, int idx, Token after);

/* The round counter, so stepping a turn back with u puts the round back too. */
void undo_set_round(Undo *u, Map *m, int round);
void undo_set_spotlight(Undo *u, Map *m, int side);
/* A tile's fog byte: which patch it belongs to and what is lit by hand. */
void undo_set_fog(Undo *u, Map *m, int x, int y, uint8_t f);
/* A clock's value; the slot must hold a clock. */
void undo_set_clock(Undo *u, Map *m, int slot, int value);
/* A square's note ("" takes it off). Returns 0, recording nothing, when
 * the map holds all the notes it can. */
int  undo_set_note(Undo *u, Map *m, int x, int y, const char *text);
/* Names a box, or with remove set takes the name off. Returns 0, recording
 * nothing, when map_area_set would refuse (bad name, full, off the map). */
int  undo_set_area(Undo *u, Map *m, const char *name, int x0, int y0, int x1, int y1);
int  undo_remove_area(Undo *u, Map *m, const char *name);
/* Marks the named area a floor at `level`, or with on 0 unmarks it; floor.c's
 * floor_problem is the caller's. Returns 0 when there is no such area. */
int  undo_set_floor(Undo *u, Map *m, const char *name, int on, int level);
/* Adds a link, or changes the one with its number; checks nothing but room
 * (link_problem is the caller's). Returns 0, recording nothing, when full. */
/* An area's environment, a card's name or "" (docs/ENVIRONMENTS.md): rides
 * OP_AREA, as a floor's mark does. 0 when no area has that name. */
int  undo_set_area_env(Undo *u, Map *m, const char *name, const char *card);
int  undo_set_link(Undo *u, Map *m, const Link *l);
int  undo_remove_link(Undo *u, Map *m, int num);

/* A named roll's slot, set to `r` (an empty name clears it). Placing a
 * character template adds its rolls through this, so u takes back the whole
 * placement; :roll's own saving stays outside the log. */
void undo_set_roll(Undo *u, Map *m, int slot, const NamedRoll *r);
int  undo_undo(Undo *u, Map *m);

/* Unwinds back to `depth`, but only through batches that are nothing but
 * moves of the tokens in `idx`. Stops at anything else, so canceling a move cannot
 * quietly swallow an edit made part way through it. Returns how many batches
 * were undone. This is what makes a cancel leave no trace: the steps are
 * taken back out of the history rather than answered with a step back. */
int  undo_rewind_moves(Undo *u, Map *m, int depth, const int *idx, int nidx);
int  undo_redo(Undo *u, Map *m);

/* Says whose the newest applied batch is: called once a landing's batch is
 * closed. Nothing when the log is empty. */
void undo_tag(Undo *u, int job, const char *who);
/* The newest applied batch's tag; NULL when it is the GM's own, or none. */
const UndoTag *undo_tag_top(const Undo *u);
/* The newest batch of `who`'s the log holds, applied or taken back (its
 * `batch` against `depth` says which); NULL for none. */
const UndoTag *undo_tag_last(const Undo *u, const char *who);
/* How many of the newest applied batches in a row are job `job`'s and
 * `who`'s (a job accepted in parts landed in several; given back and taken
 * by another name, its steps are two names'); *under is set to how many
 * more of them lie below a batch that is not, out of the count's reach. */
int  undo_tag_run(const Undo *u, int job, const char *who, int *under);
/* The job is gone and its number free to be given again: its batches keep
 * their author and lose the number. */
void undo_tag_forget(Undo *u, int job);

static inline int undo_can_undo(const Undo *u) { return u->depth > 0; }
static inline int undo_can_redo(const Undo *u) { return u->depth < u->nmarks; }

/* The bytes the log holds: its ops and their token slots (the cap is
 * UNDO_MAX_BYTES, above). */
size_t undo_bytes(const Undo *u);

#endif /* VTT_UNDO_H */
