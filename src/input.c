#include "input.h"

#include <stdio.h>
#include <string.h>

#include "util.h"

void input_init(InputParser *p) { p->len = 0; }

void input_feed(InputParser *p, const char *b, size_t n)
{
    size_t room = sizeof p->buf - p->len;
    if (n > room) n = room;      /* a burst larger than the buffer is not real input */
    memcpy(p->buf + p->len, b, n);
    p->len += n;
}

static void consume(InputParser *p, size_t n)
{
    if (n >= p->len) { p->len = 0; return; }
    memmove(p->buf, p->buf + n, p->len - n);
    p->len -= n;
}

static Key mk(uint16_t kind, uint8_t mods, uint32_t ch)
{
    Key k;
    k.kind = kind;
    k.mods = mods;
    k.ch   = ch;
    return k;
}

/* xterm encodes modifiers as a 1-based bitmask in the trailing CSI param. */
static uint8_t decode_mods(int param)
{
    if (param <= 1) return 0;
    unsigned m = (unsigned)(param - 1);
    uint8_t out = 0;
    if (m & 1u) out |= MOD_SHIFT;
    if (m & 2u) out |= MOD_ALT;
    if (m & 4u) out |= MOD_CTRL;
    return out;
}

/* Maps the `~`-terminated CSI numbers to keys. */
static uint16_t tilde_key(int n)
{
    switch (n) {
    case 1: case 7:  return KEY_HOME;
    case 2:          return KEY_INSERT;
    case 3:          return KEY_DELETE;
    case 4: case 8:  return KEY_END;
    case 5:          return KEY_PGUP;
    case 6:          return KEY_PGDN;
    case 11:         return KEY_F1;
    case 12:         return KEY_F2;
    case 13:         return KEY_F3;
    case 14:         return KEY_F4;
    case 15:         return KEY_F5;
    case 17:         return KEY_F6;
    case 18:         return KEY_F7;
    case 19:         return KEY_F8;
    case 20:         return KEY_F9;
    case 21:         return KEY_F10;
    case 23:         return KEY_F11;
    case 24:         return KEY_F12;
    default:         return KEY_NONE;
    }
}

static uint16_t letter_key(char c)
{
    switch (c) {
    case 'A': return KEY_UP;
    case 'B': return KEY_DOWN;
    case 'C': return KEY_RIGHT;
    case 'D': return KEY_LEFT;
    case 'H': return KEY_HOME;
    case 'F': return KEY_END;
    case 'P': return KEY_F1;
    case 'Q': return KEY_F2;
    case 'R': return KEY_F3;
    case 'S': return KEY_F4;
    default:  return KEY_NONE;
    }
}

/* Result of trying to read one key from the front of the buffer. */
typedef enum { R_OK, R_EMPTY, R_INCOMPLETE } ParseResult;

/* Decodes a non-escape byte sequence: a control code or a UTF-8 scalar. */
static ParseResult parse_plain(const char *s, size_t len, Key *out, size_t *used)
{
    unsigned char c = (unsigned char)s[0];

    switch (c) {
    case 0x00: *out = mk(KEY_CHAR, MOD_CTRL, ' '); *used = 1; return R_OK;
    case 0x08: case 0x7F: *out = mk(KEY_BACKSPACE, 0, 0); *used = 1; return R_OK;
    case 0x09: *out = mk(KEY_TAB, 0, 0);   *used = 1; return R_OK;
    case 0x0A: case 0x0D: *out = mk(KEY_ENTER, 0, 0); *used = 1; return R_OK;
    default: break;
    }

    /* Ctrl-A..Ctrl-Z arrive as 0x01..0x1A, minus the three handled above. */
    if (c >= 0x01u && c <= 0x1Au) {
        *out  = mk(KEY_CHAR, MOD_CTRL, (uint32_t)('a' + c - 1));
        *used = 1;
        return R_OK;
    }
    if (c >= 0x1Cu && c <= 0x1Fu) {
        static const char sym[] = { '\\', ']', '^', '_' };
        *out  = mk(KEY_CHAR, MOD_CTRL, (uint32_t)sym[c - 0x1Cu]);
        *used = 1;
        return R_OK;
    }

    if (c < 0x80u) {
        *out  = mk(KEY_CHAR, 0, c);
        *used = 1;
        return R_OK;
    }

    /* Multi-byte UTF-8: hold off until the whole scalar has arrived. */
    int need = (c & 0xE0u) == 0xC0u ? 2 : (c & 0xF0u) == 0xE0u ? 3 : (c & 0xF8u) == 0xF0u ? 4 : 1;
    if (need > 1 && len < (size_t)need) return R_INCOMPLETE;

    uint32_t cp;
    int n = utf8_decode(s, len, &cp);
    *out  = mk(KEY_CHAR, 0, cp);
    *used = (size_t)n;
    return R_OK;
}

/* Parses ESC [ ... final */
static ParseResult parse_csi(const char *s, size_t len, Key *out, size_t *used)
{
    size_t i = 2;
    int    params[8] = { 0 };
    int    nparams = 0;
    int    have_digit = 0;
    int    private_marker = 0;

    if (len <= i) return R_INCOMPLETE;

    if (s[i] == '?' || s[i] == '<' || s[i] == '>' || s[i] == '=') {
        private_marker = 1;
        i++;
    }

    for (; i < len; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c >= '0' && c <= '9') {
            if (nparams < 8) params[nparams] = params[nparams] * 10 + (c - '0');
            have_digit = 1;
            continue;
        }
        if (c == ';' || c == ':') {
            if (nparams < 8) nparams++;
            have_digit = 0;
            continue;
        }
        if (c >= 0x40u && c <= 0x7Eu) break;      /* final byte */
        if (c >= 0x20u && c <= 0x2Fu) continue;   /* intermediate */
        /* Anything else means the stream is not a CSI after all. */
        *out  = mk(KEY_ESC, 0, 0);
        *used = 1;
        return R_OK;
    }
    if (i >= len) return R_INCOMPLETE;
    if (have_digit && nparams < 8) nparams++;

    char   final = s[i];
    size_t total = i + 1;
    *used = total;

    if (private_marker) {           /* a report we did not ask for; drop it */
        *out = mk(KEY_NONE, 0, 0);
        return R_OK;
    }

    /* CSI u (kitty/fixterms): CSI <codepoint> ; <mods> u */
    if (final == 'u' && nparams >= 1) {
        uint8_t mods = nparams >= 2 ? decode_mods(params[1]) : 0;
        *out = mk(KEY_CHAR, mods, (uint32_t)params[0]);
        return R_OK;
    }
    if (final == 'Z') {             /* back-tab */
        *out = mk(KEY_TAB, MOD_SHIFT, 0);
        return R_OK;
    }
    if (final == '~') {
        uint16_t k = tilde_key(nparams >= 1 ? params[0] : 0);
        uint8_t  m = nparams >= 2 ? decode_mods(params[1]) : 0;
        *out = mk(k, m, 0);
        return R_OK;
    }

    uint16_t k = letter_key(final);
    if (k != KEY_NONE) {
        /* Modified arrows come through as CSI 1 ; <mods> <letter>. */
        uint8_t m = nparams >= 2 ? decode_mods(params[1]) : 0;
        *out = mk(k, m, 0);
        return R_OK;
    }

    *out = mk(KEY_NONE, 0, 0);      /* recognised shape, unmapped key */
    return R_OK;
}

/* Parses ESC O <final> */
static ParseResult parse_ss3(const char *s, size_t len, Key *out, size_t *used)
{
    if (len < 3) return R_INCOMPLETE;
    uint16_t k = letter_key(s[2]);
    *used = 3;
    *out  = mk(k, 0, 0);
    return R_OK;
}

static ParseResult parse_one(const char *s, size_t len, Key *out, size_t *used)
{
    if (len == 0) return R_EMPTY;

    if ((unsigned char)s[0] != 0x1Bu) return parse_plain(s, len, out, used);

    if (len == 1) return R_INCOMPLETE;         /* ESC, or the start of a sequence */

    if (s[1] == '[') return parse_csi(s, len, out, used);
    if (s[1] == 'O') return parse_ss3(s, len, out, used);

    if ((unsigned char)s[1] == 0x1Bu) {
        /* ESC ESC: report the first as a real Escape and re-examine the rest. */
        *out  = mk(KEY_ESC, 0, 0);
        *used = 1;
        return R_OK;
    }

    /* ESC <key> is Alt+<key>. */
    Key         inner;
    size_t      inner_used;
    ParseResult r = parse_plain(s + 1, len - 1, &inner, &inner_used);
    if (r != R_OK) return r;
    inner.mods |= MOD_ALT;
    *out  = inner;
    *used = inner_used + 1;
    return R_OK;
}

int input_next(InputParser *p, Key *out)
{
    for (;;) {
        size_t      used = 0;
        ParseResult r = parse_one(p->buf, p->len, out, &used);

        if (r == R_EMPTY) return 0;
        if (r == R_INCOMPLETE) {
            /* A sequence that fills the whole buffer is malformed, not slow.
             * Drop a byte so the parser can never wedge. */
            if (p->len == sizeof p->buf) { consume(p, 1); continue; }
            return 0;
        }

        consume(p, used);
        if (out->kind != KEY_NONE) return 1;
        /* Recognised but unmapped: keep going rather than returning a no-op. */
    }
}

int input_pending(const InputParser *p) { return p->len > 0; }

int input_timeout(InputParser *p, Key *out)
{
    if (p->len == 0) return 0;

    if ((unsigned char)p->buf[0] == 0x1Bu) {
        /* Nothing followed the ESC in time, so it was the Escape key. */
        consume(p, 1);
        *out = mk(KEY_ESC, 0, 0);
        return 1;
    }
    /* A truncated UTF-8 scalar that never completed; discard the fragment. */
    consume(p, 1);
    return 0;
}

const char *key_name(Key k, char *buf, size_t bufsz)
{
    static const char *names[] = {
        "None", "Char", "Esc", "Enter", "Tab", "Backspace", "Delete", "Insert",
        "Up", "Down", "Left", "Right", "Home", "End", "PgUp", "PgDn",
        "F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10", "F11", "F12",
    };
    char prefix[16];
    prefix[0] = '\0';
    if (k.mods & MOD_CTRL)  str_lcpy(prefix, "C-", sizeof prefix);
    if (k.mods & MOD_ALT)   strncat(prefix, "M-", sizeof prefix - strlen(prefix) - 1);
    if (k.mods & MOD_SHIFT) strncat(prefix, "S-", sizeof prefix - strlen(prefix) - 1);

    if (k.kind == KEY_CHAR) {
        char enc[5];
        int  n = utf8_encode(k.ch, enc);
        enc[n > 0 ? n : 0] = '\0';
        snprintf(buf, bufsz, "%s%s", prefix, enc);
    } else if (k.kind < sizeof names / sizeof *names) {
        snprintf(buf, bufsz, "%s%s", prefix, names[k.kind]);
    } else {
        snprintf(buf, bufsz, "%s?", prefix);
    }
    return buf;
}
