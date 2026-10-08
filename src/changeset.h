#ifndef VTT_CHANGESET_H
#define VTT_CHANGESET_H

/* A change set: what one map would have to change to become another, element
 * by element, each with its value before and after (docs/CONFLICTS.md). It is
 * what a proposal is -- an agent's plan run on a copy of the map, or the file
 * on disk changed by someone else -- and everything the GM does with one goes
 * through it: the highlight, the summary, the conflicts, the preview drawn but
 * never applied, and the accept, whole or by box, as one undo step.
 *
 * Applied as values: what the GM previewed is what lands. An element whose
 * live value is no longer its `before` is a conflict -- something changed it
 * since -- and accepting overwrites it, except that an accept never makes two
 * creatures with one label. */

#include <stddef.h>
#include "map.h"
#include "undo.h"

typedef enum {
    CS_TILE,
    CS_VEDGE,
    CS_HEDGE,
    CS_FOG,            /* the painting only: the patch number (FOG_ID) */
} CsCellKind;

typedef struct {
    int16_t x, y;
    uint8_t kind;      /* CsCellKind */
    uint8_t before, after;
    uint8_t conflict;
} CsCell;

/* Creatures are matched by label; one with none by side, square and size, so
 * an unlabeled creature that moved reads as one gone and one come. */
typedef struct {
    uint8_t had, has, conflict;
    Token   before, after;
} CsToken;

typedef struct {
    uint8_t had, has, conflict;
    Area    before, after;
} CsArea;

/* By number. A new link (had 0) takes the lowest free number at accept, so it
 * never lands on a link made meanwhile. */
typedef struct {
    uint8_t had, has, conflict;
    Link    before, after;
} CsLink;

typedef struct {
    int16_t x, y;
    uint8_t conflict;
    char    before[NOTE_MAX], after[NOTE_MAX];   /* "" for none */
} CsNote;

typedef struct {
    uint8_t   conflict;
    NamedRoll before, after;                     /* empty name for none */
} CsRoll;

/* Cards are not in the undo log (card.h): an accepted card stays when the
 * accept is undone, a card named by nobody. */
typedef struct {
    char    name[CARD_NAME_MAX];
    uint8_t conflict;
    char   *before, *after;                      /* NULL for none; owned */
} CsCard;

#define CS_BLOCK 16

typedef struct {
    int w, h;                       /* the map the set was made against */

    CsCell *cells;                  /* sorted by 16x16 block */
    int     ncells, cap_cells;
    int    *block_start;            /* cells of block b: [block_start[b], block_start[b+1]) */
    int     bw, bh;                 /* blocks across and down */

    CsToken *toks;  int ntoks;
    CsArea  *areas; int nareas;
    CsLink  *links; int nlinks;
    CsNote  *notes; int nnotes;
    CsRoll  *rolls; int nrolls;
    CsCard  *cards; int ncards;
    uint8_t  round_changed, spot_changed, round_conflict, spot_conflict;
    int      round_before, round_after, spot_before, spot_after;

    /* What a checkpoint (checkpoint.h) can only say changed, not how: the
     * cards, the scenes, the clocks, and the map's size. Never applied. */
    uint8_t  cards_changed, scenes_changed, clocks_changed, resized;
    int      old_w, old_h;

    /* The fog patches' names in the map the set came from: painting into a
     * patch the live map has since deleted, or reused, is a conflict. */
    char     fog_names[FOG_PATCH_MAX][FOG_NAME_MAX];

    int      conflicts;             /* from the last cs_check */
    int      checked;               /* cs_check has run since the set was made */
    unsigned checked_gen;           /* the live Map.gen it ran for, */
    unsigned checked_cards;         /* and Map.cards_gen: card_set touches nothing */

    /* The preview (cs_show): built once per live Map.gen, swapped in and out
     * around each draw. */
    struct {
        unsigned  gen;
        int       valid, shown;
        TokenList tokens;           /* the live list with the set applied */
        Link      links[MAP_LINKS_MAX]; int nlinks;
        Note      notes[MAP_NOTES_MAX]; int nnotes;
        int       x0, y0, x1, y1;   /* the cells swapped in this draw */
        uint8_t  *saved;            /* their live values meanwhile, by cell index */
    } pv;
} ChangeSet;

void cs_init(ChangeSet *cs);
void cs_free(ChangeSet *cs);

/* What `before` must change to become `after` (two maps of one size). With a
 * log -- the scratch log `after` was edited through -- only the squares it
 * wrote are compared; without, every square is. The small parts are compared
 * whole. Returns the number of elements. */
int  cs_diff(ChangeSet *cs, const Map *before, const Map *after, const Undo *hint);

/* A set built from something other than two whole maps (a checkpoint's
 * saved blocks): begin for a map of w x h, push each changed cell, then
 * finish with the small parts of the maps before and after -- creatures,
 * areas, links, notes, rolls, round, spotlight, and the cards only when
 * `cards` is set. Returns the elements, as cs_diff does. */
void cs_begin(ChangeSet *cs, int w, int h);
void cs_push_cell(ChangeSet *cs, int kind, int x, int y, uint8_t before, uint8_t after);
int  cs_finish(ChangeSet *cs, const Map *before, const Map *after, int cards);

static inline int cs_empty(const ChangeSet *cs)
{
    return !cs->ncells && !cs->ntoks && !cs->nareas && !cs->nlinks && !cs->nnotes &&
           !cs->nrolls && !cs->ncards && !cs->round_changed && !cs->spot_changed &&
           !cs->cards_changed && !cs->scenes_changed && !cs->clocks_changed && !cs->resized;
}

/* Marks every element whose live value is no longer its `before`; returns
 * how many. Does nothing when `live` has not changed since the last check. */
int  cs_check(ChangeSet *cs, const Map *live);

/* A box to accept part of a set by: the elements wholly inside it. */
typedef struct { int x0, y0, x1, y1; } CsBox;

/* Applies the set (or the part `box` holds, NULL for all) to `live`, through
 * `u`, as one nesting batch: one undo step. A creature whose label the live
 * map already holds is left out, never doubled; `out` (may be NULL) says
 * what was left out. Returns the elements applied. */
int  cs_apply(const ChangeSet *cs, Map *live, Undo *u, const CsBox *box, char *out, size_t outsz);

/* One line: "2 creatures added (Ghoul, Ghoul 2), walls in B2:F6, area Crypt".
 * With a box, only what it holds. */
void cs_summary(const ChangeSet *cs, const CsBox *box, char *out, size_t outsz);

/* The squares the set touches, as a bounding box; 0 when it touches none. */
int  cs_bounds(const ChangeSet *cs, int *x0, int *y0, int *x1, int *y1);

/* The preview: the set's `after` values swapped into `live` for one draw --
 * the cells inside the window, the creatures, links and notes -- and back
 * out. Nothing is touched: no Map.gen, no undo, no fog; the two calls must
 * bracket a draw with nothing between them that reads or writes the map for
 * anything but drawing (the rule stamp_show lives by). */
void cs_show(ChangeSet *cs, Map *live, int x0, int y0, int x1, int y1);
void cs_unshow(ChangeSet *cs, Map *live);
/* While shown, the set holds the live map's own creature list: cs_free or
 * cs_diff on it then would free the map's list. Unshow first. */

#endif /* VTT_CHANGESET_H */
