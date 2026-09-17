#ifndef VTT_DICE_H
#define VTT_DICE_H

#include <stddef.h>
#include <stdint.h>

/* Dice, with no rules in them: an expression like 2d6+1d4-1 rolled and
 * reported die by die. The one rules-aware roll, Daggerheart's duality dice,
 * is a shape (two d12s read against each other) rather than a table, so it
 * lives here too and a ruleset merely names it as its action roll. */

#define DICE_MAX_TERMS 8       /* dice groups and constants in one expression */
#define DICE_MAX_DICE  100     /* per group */
#define DICE_MAX_SIDES 1000
#define DICE_MAX_ROLLS 64      /* individual results kept for the readout */

typedef struct {
    int total;
    int nrolls;                /* how many dice were thrown, even past the cap */
    int rolls[DICE_MAX_ROLLS];
} DiceResult;

/* Seeds the generator. dice_seed_random() takes the seed from the OS, and
 * is what main does unless --seed asked for a repeatable session. */
void dice_seed(uint64_t seed);
void dice_seed_random(void);

/* One die, 1..sides inclusive, unbiased. sides < 1 rolls 1. */
int dice_one(int sides);

/* Rolls an expression. Returns 0 and fills out, or -1 with a short reason in
 * err ("2d6+" -> "expected a number"). Whitespace is allowed anywhere. */
int dice_roll_expr(const char *expr, DiceResult *out, char *err, size_t errsz);

/* "2d6+3 = 9  [4 2]": the expression as typed, the total, the dice. */
void dice_format(const char *expr, const DiceResult *r, char *buf, size_t bufsz);

/* ------------------------------------------------------------- duality */

typedef struct {
    int hope, fear;            /* the two d12s */
    int mod;
    int total;                 /* hope + fear + mod */
} DualityRoll;

void dice_duality(int mod, DualityRoll *out);

/* "with Hope", "with Fear" or "critical success" (the dice match). Pure, so
 * the rule is tested without a seed. */
const char *dice_duality_verdict(int hope, int fear);

/* Where the two dice landed in the formatted text, as byte offsets, so a
 * caller that can draw in colour knows which digits are which. */
typedef struct { int hope_at, hope_len, fear_at, fear_len; } DualitySpans;

/* "Duality +2 = 17 with Hope  [hope 9, fear 6]". spans may be NULL. */
void dice_duality_format(const DualityRoll *d, char *buf, size_t bufsz, DualitySpans *spans);

#endif /* VTT_DICE_H */
