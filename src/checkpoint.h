#ifndef VTT_CHECKPOINT_H
#define VTT_CHECKPOINT_H

/* What changed on the map since a moment -- since an agent last looked
 * (docs/CONFLICTS.md, "the map changed"). Copy-on-write by cell: starting one
 * copies only the small parts (creatures, areas, links, notes, rolls, clocks,
 * the round and spotlight, the fog patches) and clears a byte a 16x16 block;
 * the first change to a cell afterwards notes the cell's value at the start.
 *
 * The value comes from the undo log, not the map: every write to the live
 * map's squares is an op that carries its value before and after, so the
 * recorders note each cell as they record (its `before`), and undo and redo
 * note a whole batch before applying it (`before` forward, `after` back). The
 * map's own writers carry no test, and undo's apply loop none per op: with no
 * checkpoint running the edit paths are as they were. A rolled-back
 * request needs nothing: its cells go back to what was noted. The one writer
 * round the log, fog_delete, notes for itself; any new one must too.
 *
 * Cards and scenes are compared by Map.cards_gen and scenes_gen, so their
 * change is reported, not its detail. */

#include "changeset.h"
#include "map.h"
#include "undo.h"

#define CP_CELLS       (4 * CS_BLOCK * CS_BLOCK)    /* tiles, both boundaries, fog */
#define CP_BLOCK_BYTES (CP_CELLS + CP_CELLS / 8)    /* the values, then a bit a cell */

typedef struct Checkpoint {
    int      w, h, bw, bh;
    uint8_t *saved;            /* a byte a block: its bits are cleared; Map.cp_saved */
    uint8_t *store;            /* CP_BLOCK_BYTES a block, touched only once noted */
    int      nsaved;
    Map      parts;            /* the small parts as they were; no arrays, no cards or scenes */
    unsigned cards_gen, scenes_gen;
    int      resized, from_w, from_h;
} Checkpoint;

/* Starts one, or starts again: the map as it is now becomes what changes are
 * counted from. */
void checkpoint_start(Map *m);
void checkpoint_stop(Map *m);

/* The changes since the start, as a change set (before: the checkpoint,
 * after: the map); the elements, 0 for none. */
int  checkpoint_changes(const Map *m, ChangeSet *cs);

/* A block's cell bits are cleared the first time one of its cells is noted. */
void checkpoint_open_block(Checkpoint *cp, int b);

/* A cell (CsCellKind) about to change, and its value now (fog: the patch
 * number). Kept only for the first change since the start. Inline, because
 * while a checkpoint runs every recorded write comes through here: out of
 * line it cost 3 ns a cell, a byte and a bit should cost well under one. */
static inline void checkpoint_note(Map *m, int kind, int x, int y, uint8_t now)
{
    Checkpoint *cp = m->cp;
    int b = (y >> 4) * cp->bw + (x >> 4);
    if (!cp->saved[b]) checkpoint_open_block(cp, b);
    uint8_t *blk = cp->store + (size_t)b * CP_BLOCK_BYTES;
    int c = kind * CS_BLOCK * CS_BLOCK + (y & (CS_BLOCK - 1)) * CS_BLOCK + (x & (CS_BLOCK - 1));
    uint8_t *bit = blk + CP_CELLS + (c >> 3), mask = (uint8_t)(1u << (c & 7));
    if (*bit & mask) return;                       /* only the first change counts */
    *bit = (uint8_t)(*bit | mask);
    blk[c] = now;
}

/* The writers' test: nothing at all with no checkpoint running. */
static inline void cp_note(Map *m, int kind, int x, int y, uint8_t now)
{
    if (__builtin_expect(m->cp_saved != NULL, 0)) checkpoint_note(m, kind, x, y, now);
}
/* The ops [lo, hi) of `u` about to be applied, forward or back. */
void checkpoint_note_ops(Map *m, const Undo *u, int lo, int hi, int forward);

/* map_resize's: the noted cells no longer line up, so the changes become
 * "resized" until the next start. */
void checkpoint_resized(Map *m, int w, int h);

#endif /* VTT_CHECKPOINT_H */
