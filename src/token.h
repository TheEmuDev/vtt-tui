#ifndef VTT_TOKEN_H
#define VTT_TOKEN_H

#include <stddef.h>
#include <stdint.h>

#define TOKEN_LABEL_MAX 32
#define TOKEN_SIZE_MAX  3

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
} Token;

typedef struct {
    Token *v;
    int    n;
    int    cap;
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
int    tokens_overlapping(const TokenList *l, int x, int y, int size,
                          int except, int kind);

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
