#ifndef VTT_INPUT_H
#define VTT_INPUT_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
    KEY_NONE = 0,
    KEY_CHAR,          /* Key.ch holds the Unicode scalar */
    KEY_ESC,
    KEY_ENTER,
    KEY_TAB,
    KEY_BACKSPACE,
    KEY_DELETE,
    KEY_INSERT,
    KEY_UP,
    KEY_DOWN,
    KEY_LEFT,
    KEY_RIGHT,
    KEY_HOME,
    KEY_END,
    KEY_PGUP,
    KEY_PGDN,
    KEY_F1,  KEY_F2,  KEY_F3,  KEY_F4,
    KEY_F5,  KEY_F6,  KEY_F7,  KEY_F8,
    KEY_F9,  KEY_F10, KEY_F11, KEY_F12,
} KeyKind;

#define MOD_SHIFT 1u
#define MOD_ALT   2u
#define MOD_CTRL  4u

typedef struct {
    uint16_t kind;     /* KeyKind */
    uint8_t  mods;     /* MOD_* bitmask */
    uint32_t ch;       /* codepoint when kind == KEY_CHAR */
} Key;

/* Milliseconds to wait before deciding a lone ESC is the Escape key rather
 * than the start of a sequence the terminal has not finished sending. */
#define INPUT_ESC_TIMEOUT_MS 25

typedef struct {
    char   buf[512];
    size_t len;
} InputParser;

void input_init(InputParser *p);

/* Appends raw bytes read from the tty. Bytes beyond the buffer's capacity are
 * discarded, so callers should not offer more than input_room() reports. */
void input_feed(InputParser *p, const char *b, size_t n);

/* How many bytes input_feed() can still accept. */
static inline size_t input_room(const InputParser *p)
{
    return sizeof p->buf - p->len;
}

/* Pops one key. Returns 1 if *out was filled, 0 if the buffer is empty or
 * holds only an incomplete escape sequence. */
int  input_next(InputParser *p, Key *out);

/* True when bytes remain that could still become a longer sequence. The
 * event loop uses this to switch poll() from blocking to a short timeout. */
int  input_pending(const InputParser *p);

/* Called when that timeout expires: resolves a stalled sequence, which in
 * practice means reporting a bare Escape. Returns 1 if *out was filled. */
int  input_timeout(InputParser *p, Key *out);

/* Human-readable name, for the keybinding bar and debugging. */
const char *key_name(Key k, char *buf, size_t bufsz);

#endif /* VTT_INPUT_H */
