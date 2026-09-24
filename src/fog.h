#ifndef VTT_FOG_H
#define VTT_FOG_H

#include <stddef.h>
#include <stdint.h>

#include "map.h"
#include "undo.h"

/* Fog of war. The design and every decision behind it are in docs/FOG.md;
 * this is the model.
 *
 * Fog is made of patches: named areas painted onto chosen tiles, each with
 * its own settings. A tile belongs to one patch or to none, and a tile in
 * none is always visible. What a patch still hides is drawn as nothing at
 * all in the players' frame, dimmed in the GM's, and tinted in the patch's
 * own colour in build mode.
 *
 * Every drawing path answers its question with one load of the tile's fog
 * byte and a mask; the patch table is read only to know whether a patch is
 * live, which is fifteen bytes that stay in cache.
 *
 * Deleted patches are tombstoned rather than freed: the undo log may still
 * hold ops that put their number back on a tile, and a new patch in the
 * same slot would inherit that ground. A tombstoned number hides nothing
 * and is never written to a file. */

typedef enum {
    FOGV_BUILD,      /* painted ground tinted in its patch's colour */
    FOGV_GM,         /* what fog hides from the table, dimmed */
    FOGV_PLAYERS,    /* what fog hides, not drawn at all */
} FogView;

static inline uint8_t fog_at(const Map *m, int x, int y)
{
    return map_in_bounds(m, x, y) ? m->fog[(size_t)y * (size_t)m->w + (size_t)x] : 0;
}

static inline int fog_patch_live(const Map *m, int id)
{
    return id > 0 && id <= FOG_PATCH_MAX && m->fog_patches[id - 1].name[0] &&
           !m->fog_patches[id - 1].dead && !m->fog_patches[id - 1].disabled;
}

/* Is fog hiding anything on this map at all? The one check a frame makes
 * before paying for any of the rest: false is the fog-free path. */
int fog_any(const Map *m);

/* Ground the players cannot see: fog is on, the tile's patch is live, and
 * the party has neither seen it nor lights it now. Off the map counts as
 * hidden, so a wall on the map's edge is judged by its inner side alone. */
static inline int fog_ground_hidden(const Map *m, int x, int y)
{
    if (!map_in_bounds(m, x, y)) return 1;
    uint8_t f = fog_at(m, x, y);
    return m->fog_on && fog_patch_live(m, f & FOG_ID) && !(f & (FOG_SEEN | FOG_LIT | FOG_HELD));
}

/* A creature standing here is drawn only where the party can see *now*:
 * remembered ground shows its floor, never who is on it. */
static inline int fog_creature_hidden(const Map *m, int x, int y)
{
    uint8_t f = fog_at(m, x, y);
    return m->fog_on && fog_patch_live(m, f & FOG_ID) && !(f & (FOG_LIT | FOG_HELD));
}

/* A creature is hidden when every square it covers is. */
int fog_token_hidden(const Map *m, const Token *t);

/* Does this patch show its rim? Its own setting, or the map's when it
 * follows the map. */
static inline int fog_patch_soft(const Map *m, int id)
{
    int s = m->fog_patches[id - 1].soft_edge;
    return s < 0 ? m->fog_soft_edge : s;
}

/* The soft edge: a square the party cannot see now, beside one it can, in a
 * patch that shows its rim. The players' frame draws its walls dimmed and
 * any creature on it as a silhouette; its ground stays as memory left it. */
static inline int fog_rim_shown(const Map *m, int x, int y)
{
    uint8_t f = fog_at(m, x, y);
    return (f & FOG_RIM) && m->fog_on && fog_patch_live(m, f & FOG_ID) &&
           fog_patch_soft(m, f & FOG_ID);
}

/* A hidden creature with a square on a shown rim: drawn as a silhouette,
 * its shape and nothing else. */
int fog_token_silhouette(const Map *m, const Token *t);

/* Patches, by name or any prefix of one, case aside; an exact name beats a
 * longer one. Returns a patch number 1..15, 0 for none, -1 when several
 * match. */
int fog_find(const Map *m, const char *prefix);

/* A new patch with the defaults -- reveal 2, memory on, the map's soft
 * edge -- or the existing one of that name. Returns its number, 0 when all
 * fifteen are taken, -1 for a bad name. */
int fog_create(Map *m, const char *name);

/* Deletes a patch for good: its ground is scrubbed directly, outside the
 * undo log, and its number is tombstoned. */
void fog_delete(Map *m, int id);

/* Paints tile (x,y) into patch `id` (0 scrubs it), through the undo log.
 * Repainting starts the tile afresh: nothing seen, nothing lit. */
void fog_paint(Map *m, Undo *u, int x, int y, int id);

/* The GM's hand: lights a painted tile (held, and seen) or puts it back in
 * the dark, through the undo log. Unpainted tiles are left alone. */
void fog_light(Map *m, Undo *u, int x, int y, int on);

/* The same over a whole patch, by its extent. Returns how many tiles
 * changed. */
int  fog_light_patch(Map *m, Undo *u, int id, int on);

/* Memory switched off forgets what the patch remembered: SEEN goes from
 * every square of it the GM is not holding lit, so "the dark closes
 * behind them" is true of ground seen before the switch too. A setting,
 * not an undo op, like the rest of a patch's settings. */
void fog_forget(Map *m, int id);

/* Tiles painted into a patch, and how many of those the party has seen or
 * has had lit. */
int  fog_count(const Map *m, int id, int *seen);

/* Sight: which painted ground the party can see *now*. Rebuilds LIT (and
 * SEEN, for a patch that remembers) from every player creature, each patch
 * lighting to its own `reveal` in the map's own metric, along lines the
 * boundaries do not block -- the same test the ruler and the range use.
 * Then RIM on the unlit fog next to it. Clears only where it lit last time
 * and tests each patch's extent before touching a tile, so the cost is the
 * party's reach, never the size of a patch. HELD is never touched: the GM's
 * light stays down. Not an undo op and not saved: it is where the creatures
 * stand, and is rebuilt whenever that could have changed. */
void fog_recompute(Map *m);

/* The patch colour index for the build-mode tint, 0..14. */
static inline int fog_tint(int id) { return (id - 1) % FOG_PATCH_MAX; }

#endif /* VTT_FOG_H */
