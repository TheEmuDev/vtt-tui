#include "checkpoint.h"

#include <stdlib.h>
#include <string.h>

#include "prof.h"
#include "util.h"

static uint8_t *block_store(const Checkpoint *cp, int b)
{
    return cp->store + (size_t)b * CP_BLOCK_BYTES;
}

void checkpoint_open_block(Checkpoint *cp, int b)
{
    memset(block_store(cp, b) + CP_CELLS, 0, CP_CELLS / 8);
    cp->saved[b] = 1;
    cp->nsaved++;
}

void checkpoint_note_ops(Map *m, const Undo *u, int lo, int hi, int forward)
{
    for (int k = 0; k < hi - lo; k++) {
        const Op *o = &u->ops[forward ? lo + k : hi - 1 - k];   /* the order they apply in */
        uint8_t now = forward ? o->before : o->after;
        switch (o->kind) {
        case OP_TILE:  checkpoint_note(m, CS_TILE, o->x, o->y, now);  break;
        case OP_VEDGE: checkpoint_note(m, CS_VEDGE, o->x, o->y, now); break;
        case OP_HEDGE: checkpoint_note(m, CS_HEDGE, o->x, o->y, now); break;
        case OP_FOG:   checkpoint_note(m, CS_FOG, o->x, o->y, (uint8_t)(now & FOG_ID)); break;
        default:       break;
        }
    }
}

static void size_for(Checkpoint *cp, int w, int h)
{
    if (cp->saved && cp->w == w && cp->h == h) {
        if (cp->nsaved) memset(cp->saved, 0, (size_t)cp->bw * (size_t)cp->bh);
        return;
    }
    free(cp->saved);
    free(cp->store);
    cp->w  = w;
    cp->h  = h;
    cp->bw = (w + 1 + CS_BLOCK - 1) / CS_BLOCK;
    cp->bh = (h + 1 + CS_BLOCK - 1) / CS_BLOCK;
    size_t nb = (size_t)cp->bw * (size_t)cp->bh;
    cp->saved = xcalloc(nb, 1);
    /* Not zeroed: a block's bits are cleared when it is first noted, and a
     * page no block was noted into is never touched. */
    cp->store = xmalloc(nb * CP_BLOCK_BYTES);
}

void checkpoint_start(Map *m)
{
    PROF_ZONE("ctl.checkpoint");
    Checkpoint *cp = m->cp;
    if (!cp) cp = m->cp = xcalloc(1, sizeof *cp);
    size_for(cp, m->w, m->h);
    cp->nsaved  = 0;
    cp->resized = 0;

    /* The small parts, copied whole: what a few dozen writes a request could
     * change, read without a hook. */
    Map *p = &cp->parts;
    p->w = m->w;
    p->h = m->h;
    tokens_copy(&p->tokens, &m->tokens);
    memcpy(p->areas, m->areas, (size_t)m->nareas * sizeof *p->areas);
    p->nareas = m->nareas;
    memcpy(p->links, m->links, (size_t)m->nlinks * sizeof *p->links);
    p->nlinks = m->nlinks;
    memcpy(p->notes, m->notes, (size_t)m->nnotes * sizeof *p->notes);
    p->nnotes = m->nnotes;
    memcpy(p->rolls, m->rolls, sizeof p->rolls);
    memcpy(p->clocks, m->clocks, sizeof p->clocks);
    memcpy(p->fog_patches, m->fog_patches, sizeof p->fog_patches);
    p->round     = m->round;
    p->spotlight = m->spotlight;
    cp->cards_gen  = m->cards_gen;
    cp->scenes_gen = m->scenes_gen;

    m->cp_saved = cp->saved;
    m->cp_bw    = cp->bw;
}

void checkpoint_stop(Map *m)
{
    Checkpoint *cp = m->cp;
    if (!cp) return;
    free(cp->saved);
    free(cp->store);
    tokens_free(&cp->parts.tokens);
    free(cp);
    m->cp = NULL;
    m->cp_saved = NULL;
}

void checkpoint_resized(Map *m, int w, int h)
{
    Checkpoint *cp = m->cp;
    (void)w;
    (void)h;
    if (!cp) return;
    if (!cp->resized) {
        cp->resized = 1;
        cp->from_w  = m->w;
        cp->from_h  = m->h;
    }
    m->cp_saved = NULL;             /* the blocks no longer line up: nothing more to save */
}

static int clock_same(const Clock *a, const Clock *b)
{
    return !strcmp(a->name, b->name) && (!a->name[0] ||
           (a->value == b->value && a->size == b->size && a->down == b->down));
}

int checkpoint_changes(const Map *m, ChangeSet *cs)
{
    PROF_ZONE("mapdiff");
    const Checkpoint *cp = m->cp;
    cs_begin(cs, m->w, m->h);
    if (!cp) return 0;
    if (cp->resized) {
        cs->resized = 1;
        cs->old_w   = cp->from_w;
        cs->old_h   = cp->from_h;
    } else if (cp->nsaved) {
        /* The map is the size the cells were noted on (a resize says so
         * above), so its arrays are read straight, a byte of noted bits at a
         * time, skipping a byte with none. */
        size_t w = (size_t)m->w;
        const uint8_t *layer[4]  = { m->tiles, m->vedges, m->hedges, m->fog };
        const size_t   stride[4] = { w, w + 1, w, w };
        int nb = cp->bw * cp->bh;
        for (int b = 0; b < nb; b++) {
            if (!cp->saved[b]) continue;
            const uint8_t *blk = block_store(cp, b), *bits = blk + CP_CELLS;
            int x0 = (b % cp->bw) * CS_BLOCK, y0 = (b / cp->bw) * CS_BLOCK;
            for (int byte = 0; byte < CP_CELLS / 8; byte++) {
                unsigned set = bits[byte];
                while (set) {
                    int c = byte * 8 + __builtin_ctz(set);
                    set &= set - 1;
                    int kind = c / (CS_BLOCK * CS_BLOCK), r = c % (CS_BLOCK * CS_BLOCK);
                    int x = x0 + r % CS_BLOCK, y = y0 + r / CS_BLOCK;
                    uint8_t now = layer[kind][(size_t)y * stride[kind] + (size_t)x];
                    if (kind == CS_FOG) now &= FOG_ID;
                    if (blk[c] != now) cs_push_cell(cs, kind, x, y, blk[c], now);
                }
            }
        }
    }
    int n = cs_finish(cs, &cp->parts, m, 0);
    cs->cards_changed  = m->cards_gen != cp->cards_gen;
    cs->scenes_changed = m->scenes_gen != cp->scenes_gen;
    for (int i = 0; i < CLOCK_MAX; i++)
        if (!clock_same(&cp->parts.clocks[i], &m->clocks[i])) cs->clocks_changed = 1;
    return n + cs->cards_changed + cs->scenes_changed + cs->clocks_changed + cs->resized;
}
