#ifndef VTT_TOKEN_H
#define VTT_TOKEN_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define TOKEN_LABEL_MAX 32
#define TOKEN_SIZE_MAX  3
/* A line of the GM's own text on a creature: what it wants, what it is
 * hiding, what it does when cornered. Never drawn on the map. */
#define TOKEN_NOTE_MAX  64

/* Markers a GM hangs on a creature: poisoned, marked, restrained, whatever
 * the table calls it. The tool attaches no meaning to them -- they are a
 * colour and a word, and what they do is between the GM and the players. */
#define TOKEN_STATUS_MAX   4
#define STATUS_LABEL_MAX   16
#define STATUS_COLOR_COUNT 8

typedef struct {
    uint8_t color;                    /* index into the theme's status palette */
    char    label[STATUS_LABEL_MAX];
} Status;

/* Named numbers on a creature: HP 4/6, Stress 2/6. Rules-agnostic -- a
 * short name, a value and a maximum -- and the GM's business, never the
 * table's: they are drawn in the GM's frame only. See counter.h. */
#define TOKEN_COUNTER_MAX 4
#define COUNTER_NAME_MAX  8
#define COUNTER_VALUE_MAX 999
typedef struct {
    char    name[COUNTER_NAME_MAX];
    int16_t value, max;             /* 0 <= value <= max, 1 <= max */
} Counter;

typedef enum {
    TOKEN_PLAYER = 0,   /* drawn as a circle */
    TOKEN_ENEMY  = 1,   /* drawn as a square, inset inside its tile */
} TokenKind;

/* A token occupies a size x size block of tiles anchored at its top-left.
 * Deliberately free of any rules concepts: a label, a footprint, and a side. */
typedef struct {
    int16_t x, y;
    uint8_t size;       /* 1, 2, or 3 */
    uint8_t kind;       /* TokenKind */
    char    label[TOKEN_LABEL_MAX];

    Status  status[TOKEN_STATUS_MAX];
    uint8_t nstatus;

    /* The turn order lives on the creatures rather than in a list beside
     * them: the order is these numbers sorted, so deleting, pasting, undoing
     * and saving a creature carry its place along with no index to patch.
     * Rules-agnostic on purpose -- init is just a number, highest first. */
    uint8_t turn;       /* TURN_IN | TURN_ACTING */
    int16_t init;       /* meaningful only with TURN_IN */
    char    note[TOKEN_NOTE_MAX];
    Counter counters[TOKEN_COUNTER_MAX];
    uint8_t ncounters;
} Token;

#define TURN_IN     0x01u   /* has a place in the order */
#define TURN_ACTING 0x02u   /* it is this creature's turn; at most one */

typedef struct {
    Token   *v;
    int      n;
    int      cap;
    unsigned shape;     /* bumped by every add and remove: sight's step path
                         * needs the list's indices to mean what they meant */
} TokenList;

void   tokens_free(TokenList *l);
int    tokens_add(TokenList *l, Token t);       /* returns the new index */
void   tokens_remove(TokenList *l, int idx);

/* Index of the topmost token whose footprint covers the tile, or -1.
 * Searched newest-first so the most recently placed token wins. */
int    tokens_at(const TokenList *l, int x, int y);

/* Walks the ring of tokens a size x size block covers, in list order. `after`
 * is the one currently chosen and the next one round is returned, so repeated
 * calls cycle; -1 starts at the beginning. Returns -1 when the block covers
 * nothing at all, and `after` itself when it covers only that one, which is
 * how a caller tells "the only candidate" from "one of several". */
int    tokens_covered_next(const TokenList *l, int x, int y, int size, int after);

/* The topmost token whose footprint overlaps the size x size block anchored
 * at (x,y), or -1 when the block is clear. `except` is an index to ignore, so
 * a token can be asked about a square it is already standing on. `kind` is
 * TOKEN_ANY_KIND to mean any token, or a TokenKind to look only for those --
 * a creature can walk past its own side but not through the other one. */
#define TOKEN_ANY_KIND (-1)

/* Does this token's footprint meet a w x h block at (x,y)? Two blocks miss
 * each other when either axis does. The one overlap test in the codebase:
 * the searches, the ring walk and the visual box all lean on it, so they
 * cannot disagree about what "covers" means. */
static inline int token_meets(const Token *t, int x, int y, int w, int h)
{
    if (x + w <= t->x || t->x + t->size <= x) return 0;
    if (y + h <= t->y || t->y + t->size <= y) return 0;
    return 1;
}

/* Field-wise, so padding and whatever sits past a label's NUL cannot make
 * two equal tokens look different (or be relied on to look the same). */
static inline int token_equal(const Token *a, const Token *b)
{
    if (a->x != b->x || a->y != b->y || a->size != b->size || a->kind != b->kind)
        return 0;
    if (strcmp(a->label, b->label) != 0 || a->nstatus != b->nstatus) return 0;
    if (a->turn != b->turn || ((a->turn & TURN_IN) && a->init != b->init)) return 0;
    if (strcmp(a->note, b->note) != 0) return 0;
    if (a->ncounters != b->ncounters) return 0;
    for (int i = 0; i < a->ncounters; i++)
        if (strcmp(a->counters[i].name, b->counters[i].name) != 0 ||
            a->counters[i].value != b->counters[i].value ||
            a->counters[i].max != b->counters[i].max)
            return 0;
    for (int i = 0; i < a->nstatus; i++)
        if (a->status[i].color != b->status[i].color ||
            strcmp(a->status[i].label, b->status[i].label) != 0)
            return 0;
    return 1;
}
int    tokens_overlapping(const TokenList *l, int x, int y, int size,
                          int except, int kind);

/* The same search skipping a set of indices rather than one. Creatures moving
 * as a group step through each other, so every member has to be transparent to
 * every other member's check, not only to its own. `skip` may be NULL. */
int    tokens_overlapping_set(const TokenList *l, int x, int y, int size,
                              const int *skip, int nskip, int kind);

const char *token_kind_name(uint8_t kind);

/* A label no other token carries, so pasting a copy of "Goblin" gives you
 * "Goblin 2" rather than two creatures you cannot tell apart in the readout.
 * A trailing number is continued rather than stacked. An unlabelled token
 * stays unlabelled. */
void tokens_unique_label(const TokenList *l, const char *base,
                         char *out, size_t outsz);

const char *status_color_name(uint8_t colour);
int         status_color_from_name(const char *name);   /* -1 if unknown */

/* Returns 0 when the token already carries as many as it can hold. */
int  token_add_status(Token *t, uint8_t colour, const char *label);
void token_clear_status(Token *t);

/* Drops one marker, keeping the rest in order. Out-of-range is a no-op. */
void token_remove_status(Token *t, int idx);

/* The character drawn on the map for a marker: the first letter of its label,
 * so the map says what it is, falling back to a dot for an unlabelled one. */
uint32_t status_glyph(const Status *st);

#endif /* VTT_TOKEN_H */
