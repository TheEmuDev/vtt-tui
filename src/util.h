#ifndef VTT_UTIL_H
#define VTT_UTIL_H

#include <stddef.h>
#include <stdint.h>

/* ---------------------------------------------------------------- errors */

void die(const char *fmt, ...);

/* Allocation wrappers that abort rather than return NULL. The app has no
 * meaningful recovery path for OOM, and checking every call site costs more
 * in noise than it buys in robustness. */
void *xmalloc(size_t n);
void *xcalloc(size_t n, size_t sz);
void *xrealloc(void *p, size_t n);

/* -------------------------------------------------------------- byte buf */

/* Growable byte buffer. One of these accumulates an entire frame's escape
 * sequences so the frame goes out in a single write(). */
typedef struct {
    char  *data;
    size_t len;
    size_t cap;
} ByteBuf;

void bb_init(ByteBuf *b, size_t cap);
void bb_free(ByteBuf *b);
void bb_reset(ByteBuf *b);
void bb_reserve(ByteBuf *b, size_t extra);
void bb_put(ByteBuf *b, const void *p, size_t n);
void bb_puts(ByteBuf *b, const char *s);
void bb_putc(ByteBuf *b, char c);
/* Unsigned decimal, no printf. Escape sequences are almost entirely small
 * integers, and this is measurably cheaper than snprintf in the flush loop. */
void bb_putu(ByteBuf *b, uint32_t v);

/* ------------------------------------------------------------------ utf8 */

/* Encodes cp into out (max 4 bytes); returns the byte count, or 0 if cp is
 * not a legal scalar value. */
int utf8_encode(uint32_t cp, char out[4]);

/* Decodes one scalar from s[0..len). Returns bytes consumed and stores the
 * codepoint in *cp. On a malformed sequence, consumes 1 byte and yields
 * U+FFFD so a bad byte can never stall the caller's loop. */
int utf8_decode(const char *s, size_t len, uint32_t *cp);

/* The span in which no codepoint is zero- or double-width, letting
 * utf8_width() skip its table lookups for the glyphs the grid draws most. */
#define UTIL_WIDTH_FASTPATH_LO 0x2100u
#define UTIL_WIDTH_FASTPATH_HI 0x2E80u

/* Display width of a codepoint in terminal cells: 0, 1, or 2. */
int utf8_width(uint32_t cp);

/* Table-only version, for asserting the fast path agrees with it. */
int utf8_width_slow(uint32_t cp);

/* ------------------------------------------------------------------ math */

static inline int imin(int a, int b) { return a < b ? a : b; }
static inline int imax(int a, int b) { return a > b ? a : b; }
static inline int iclamp(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

/* ---------------------------------------------------------------- string */

/* strlcpy semantics: always NUL-terminates, returns strlen(src). */
size_t str_lcpy(char *dst, const char *src, size_t dstsz);

/* Case-insensitive substring search, returning a pointer into `hay` or NULL.
 * Folds ASCII only: it exists to match what someone typed against a token
 * label, and getting "Goblin" from "gob" is the whole job. An empty needle
 * matches at the start, as strstr has it. */
const char *str_casestr(const char *hay, const char *needle);

#endif /* VTT_UTIL_H */
