#include "dice.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "util.h"

/* ------------------------------------------------------------ generator */

/* xoshiro256** -- small, fast, and far better distributed than rand(). The
 * state is file scope: there is one table and one set of dice. */
static uint64_t S[4];

static uint64_t rotl(uint64_t x, int k) { return (x << k) | (x >> (64 - k)); }

static uint64_t next64(void)
{
    uint64_t r = rotl(S[1] * 5, 7) * 9;
    uint64_t t = S[1] << 17;
    S[2] ^= S[0]; S[3] ^= S[1]; S[1] ^= S[2]; S[0] ^= S[3];
    S[2] ^= t;
    S[3]  = rotl(S[3], 45);
    return r;
}

void dice_seed(uint64_t seed)
{
    /* splitmix64 spreads one word over the four, and never leaves them all
     * zero, which xoshiro cannot recover from. */
    for (int i = 0; i < 4; i++) {
        uint64_t z = (seed += 0x9e3779b97f4a7c15ULL);
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
        z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
        S[i] = z ^ (z >> 31);
    }
}

void dice_seed_random(void)
{
    uint64_t seed = 0;
    FILE *f = fopen("/dev/urandom", "rb");
    if (f) {
        if (fread(&seed, sizeof seed, 1, f) != 1) seed = 0;
        fclose(f);
    }
    if (!seed) seed = (uint64_t)time(NULL) ^ ((uint64_t)getpid() << 32);
    dice_seed(seed);
}

int dice_one(int sides)
{
    if (sides < 2) return 1;
    /* Rejection sampling over the smallest power-of-two mask: unbiased, and
     * the expected number of draws is under two. */
    uint64_t n    = (uint64_t)sides;
    uint64_t mask = ~0ULL >> __builtin_clzll(n | 1);
    uint64_t v;
    do v = next64() & mask; while (v >= n);
    return (int)v + 1;
}

/* --------------------------------------------------------------- parser */

static const char *skip_ws(const char *p) { while (*p == ' ' || *p == '\t') p++; return p; }

/* Reads a run of digits into *out, capped so no expression can overflow an
 * int. Returns the position after them, or p itself when there were none. */
static const char *read_int(const char *p, int *out)
{
    const char *start = p;
    long v = 0;
    while (isdigit((unsigned char)*p)) {
        v = v * 10 + (*p - '0');
        if (v > 1000000) v = 1000000;
        p++;
    }
    *out = (int)v;
    return p == start ? start : p;
}

int dice_roll_expr(const char *expr, DiceResult *out, char *err, size_t errsz)
{
    memset(out, 0, sizeof *out);
    err[0] = '\0';

    const char *p = skip_ws(expr);
    if (!*p) { snprintf(err, errsz, "roll what? e.g. 2d6+3"); return -1; }

    int sign  = 1;
    int terms = 0;
    for (;;) {
        p = skip_ws(p);
        if (*p == '+' || *p == '-') {
            sign = *p == '-' ? -1 : 1;
            p = skip_ws(p + 1);
        } else if (terms) {
            if (!*p) break;
            snprintf(err, errsz, "expected + or - before \"%.8s\"", p);
            return -1;
        }
        if (++terms > DICE_MAX_TERMS) { snprintf(err, errsz, "too many terms"); return -1; }

        int         n;
        const char *q = read_int(p, &n);
        int         had_n = q != p;

        if (*q == 'd' || *q == 'D') {
            if (!had_n) n = 1;
            int sides;
            const char *r = read_int(q + 1, &sides);
            if (r == q + 1) { snprintf(err, errsz, "d needs a number of sides"); return -1; }
            if (n < 1 || n > DICE_MAX_DICE) {
                snprintf(err, errsz, "1 to %d dice a group", DICE_MAX_DICE); return -1;
            }
            if (sides < 2 || sides > DICE_MAX_SIDES) {
                snprintf(err, errsz, "2 to %d sides", DICE_MAX_SIDES); return -1;
            }
            for (int i = 0; i < n; i++) {
                int v = dice_one(sides);
                if (out->nrolls >= 0 && out->nrolls < DICE_MAX_ROLLS) out->rolls[out->nrolls] = v;
                out->nrolls++;
                out->total += sign * v;
            }
            p = r;
        } else if (had_n) {
            out->total += sign * n;
            p = q;
        } else {
            snprintf(err, errsz, *p ? "expected a number at \"%.8s\"" : "expected a number", p);
            return -1;
        }
    }
    return 0;
}

void dice_format(const char *expr, const DiceResult *r, char *buf, size_t bufsz)
{
    /* The expression is echoed without its spaces so "2d6 + 3" and "2d6+3"
     * read the same in the log. */
    char clean[64];
    size_t k = 0;
    for (const char *p = expr; *p && k + 1 < sizeof clean; p++)
        if (*p != ' ' && *p != '\t') clean[k++] = *p;
    clean[k] = '\0';

    int off = snprintf(buf, bufsz, "%s = %d", clean, r->total);
    if (off < 0 || (size_t)off >= bufsz || r->nrolls == 0) return;

    off += snprintf(buf + off, bufsz - (size_t)off, "  [");
    int shown = r->nrolls < DICE_MAX_ROLLS ? r->nrolls : DICE_MAX_ROLLS;
    for (int i = 0; i < shown && (size_t)off + 8 < bufsz; i++)
        off += snprintf(buf + off, bufsz - (size_t)off, "%s%d", i ? " " : "", r->rolls[i]);
    if ((size_t)off + 4 < bufsz)
        snprintf(buf + off, bufsz - (size_t)off, "%s]", r->nrolls > shown ? " ..." : "");
}

/* ------------------------------------------------------------- duality */

void dice_duality(int mod, DualityRoll *out)
{
    out->hope  = dice_one(12);
    out->fear  = dice_one(12);
    out->mod   = mod;
    out->total = out->hope + out->fear + mod;
}

const char *dice_duality_verdict(int hope, int fear)
{
    if (hope == fear) return "critical success";
    return hope > fear ? "with Hope" : "with Fear";
}

void dice_duality_format(const DualityRoll *d, char *buf, size_t bufsz)
{
    char mod[16] = "";
    if (d->mod) snprintf(mod, sizeof mod, " %+d", d->mod);
    snprintf(buf, bufsz, "Duality%s = %d %s  [hope %d, fear %d]",
             mod, d->total, dice_duality_verdict(d->hope, d->fear), d->hope, d->fear);
}
