#include "render.h"

#include <stdlib.h>
#include <string.h>

/* Sentinel that cannot equal any real color, so the first cell of a frame
 * always emits its SGR. */
#define COL_INVALID 0xFFFFFFFFu

static const Cell BLANK = { ' ', COL_DEFAULT, COL_DEFAULT, 0, { 0, 0, 0 } };

void rnd_init(Renderer *r)
{
    memset(r, 0, sizeof *r);
    r->clear_cell = BLANK;
    bb_init(&r->out, 64 * 1024);
    r->force_full = 1;
}

void rnd_free(Renderer *r)
{
    free(r->front);
    free(r->back);
    bb_free(&r->out);
    memset(r, 0, sizeof *r);
}

void rnd_resize(Renderer *r, int w, int h)
{
    if (w == r->w && h == r->h) return;
    if (w < 1) w = 1;
    if (h < 1) h = 1;

    size_t n = (size_t)w * (size_t)h;
    free(r->front);
    free(r->back);
    r->front  = xmalloc(n * sizeof(Cell));
    r->back   = xmalloc(n * sizeof(Cell));
    r->ncells = n;
    r->w = w;
    r->h = h;

    /* Poison front so every cell counts as changed on the next flush; the
     * terminal's contents after a resize are not knowable. */
    for (size_t i = 0; i < n; i++) {
        r->front[i]    = BLANK;
        r->front[i].ch = 0xFFFFFFFFu;
        r->back[i]     = BLANK;
    }
    r->force_full = 1;
    r->clip_x0 = r->clip_y0 = 0;
    r->clip_x1 = w;
    r->clip_y1 = h;
}

void rnd_set_clear(Renderer *r, uint32_t fg, uint32_t bg)
{
    r->clear_cell    = BLANK;
    r->clear_cell.fg = fg;
    r->clear_cell.bg = bg;
}

void rnd_begin(Renderer *r)
{
    for (size_t i = 0; i < r->ncells; i++) r->back[i] = r->clear_cell;
    r->cells_changed = 0;
    r->bytes_written = 0;

    /* Every frame starts unclipped, so a panel that forgets to restore its
     * clip cannot blank the next frame. */
    r->clip_x0 = r->clip_y0 = 0;
    r->clip_x1 = r->w;
    r->clip_y1 = r->h;
}

/* Intersects with the clip already in force, so nesting narrows and never
 * widens: a child panel cannot escape its parent's bounds. */
ClipRect rnd_clip_push(Renderer *r, int x, int y, int w, int h)
{
    ClipRect saved = { r->clip_x0, r->clip_y0, r->clip_x1, r->clip_y1 };

    r->clip_x0 = imax(saved.x0, x);
    r->clip_y0 = imax(saved.y0, y);
    r->clip_x1 = imin(saved.x1, x + w);
    r->clip_y1 = imin(saved.y1, y + h);
    if (r->clip_x1 < r->clip_x0) r->clip_x1 = r->clip_x0;
    if (r->clip_y1 < r->clip_y0) r->clip_y1 = r->clip_y0;
    return saved;
}

void rnd_clip_restore(Renderer *r, ClipRect saved)
{
    r->clip_x0 = saved.x0;
    r->clip_y0 = saved.y0;
    r->clip_x1 = saved.x1;
    r->clip_y1 = saved.y1;
}

static int cell_eq(const Cell *a, const Cell *b)
{
    return a->ch == b->ch && a->fg == b->fg && a->bg == b->bg && a->attr == b->attr;
}

static void emit_cup(ByteBuf *o, int x, int y)
{
    bb_puts(o, "\x1b[");
    bb_putu(o, (uint32_t)(y + 1));
    bb_putc(o, ';');
    bb_putu(o, (uint32_t)(x + 1));
    bb_putc(o, 'H');
}

static void emit_color(ByteBuf *o, uint32_t col, int is_fg)
{
    if (col & COL_DEFAULT) {
        bb_puts(o, is_fg ? "\x1b[39m" : "\x1b[49m");
        return;
    }
    bb_puts(o, is_fg ? "\x1b[38;2;" : "\x1b[48;2;");
    bb_putu(o, (col >> 16) & 0xFFu);
    bb_putc(o, ';');
    bb_putu(o, (col >> 8) & 0xFFu);
    bb_putc(o, ';');
    bb_putu(o, col & 0xFFu);
    bb_putc(o, 'm');
}

static void emit_attr(ByteBuf *o, uint8_t attr)
{
    if (attr & ATTR_BOLD)      bb_puts(o, "\x1b[1m");
    if (attr & ATTR_DIM)       bb_puts(o, "\x1b[2m");
    if (attr & ATTR_UNDERLINE) bb_puts(o, "\x1b[4m");
    if (attr & ATTR_REVERSE)   bb_puts(o, "\x1b[7m");
}

void rnd_flush(Renderer *r, Term *t)
{
    ByteBuf *o = &r->out;
    bb_reset(o);

    /* Synchronized output: the terminal buffers the whole frame and presents
     * it at once, so a partially drawn frame is never visible. Terminals that
     * do not implement it ignore both sequences. */
    bb_puts(o, "\x1b[?2026h");

    int      cx = -1, cy = -1;               /* where the terminal's cursor is */
    uint32_t cur_fg = COL_INVALID, cur_bg = COL_INVALID;
    uint8_t  cur_attr = 0;
    int      attr_valid = 0;

    for (int y = 0; y < r->h; y++) {
        /* Whole clean rows are skipped in one comparison. memcmp may read
         * the padding bytes, which is sound because every Cell descends from
         * BLANK by struct copy and is only ever mutated field by field, so
         * the padding is zero everywhere -- a test pins that invariant. A
         * typical frame changes two or three rows of a 50-row window, and
         * this is what makes the other 47 cost one SIMD sweep each. */
        size_t row = (size_t)y * (size_t)r->w;
        if (!r->force_full &&
            memcmp(&r->back[row], &r->front[row],
                   (size_t)r->w * sizeof(Cell)) == 0)
            continue;

        for (int x = 0; x < r->w; x++) {
            size_t      i = row + (size_t)x;
            const Cell *b = &r->back[i];

            /* Second half of a wide glyph: the preceding cell already moved
             * the cursor across it. */
            if (b->ch == 0) continue;

            if (!r->force_full && cell_eq(b, &r->front[i])) continue;
            r->cells_changed++;

            if (cx != x || cy != y) {
                emit_cup(o, x, y);
                cx = x;
                cy = y;
            }

            if (!attr_valid || b->attr != cur_attr) {
                /* No selective "unset" exists for these, so reset and rebuild;
                 * that also invalidates the colors the reset cleared. */
                bb_puts(o, "\x1b[0m");
                emit_attr(o, b->attr);
                cur_attr   = b->attr;
                attr_valid = 1;
                cur_fg = cur_bg = COL_INVALID;
            }
            if (b->fg != cur_fg) { emit_color(o, b->fg, 1); cur_fg = b->fg; }
            if (b->bg != cur_bg) { emit_color(o, b->bg, 0); cur_bg = b->bg; }

            char enc[4];
            int  n = utf8_encode(b->ch, enc);
            if (n <= 0) { enc[0] = ' '; n = 1; }
            bb_put(o, enc, (size_t)n);

            cx += imax(1, utf8_width(b->ch));
            /* Past the last column the terminal may or may not have wrapped,
             * so stop trusting our idea of the cursor. */
            if (cx >= r->w) { cx = -1; cy = -1; }
        }
    }

    bb_puts(o, "\x1b[?2026l");

    /* Only pay for a write when something actually changed. The two sync
     * markers are the empty-frame floor. */
    int delivered = 1;
    if (r->cells_changed > 0 || r->force_full) {
        /* A NULL term is the headless path: the diff still runs and the byte
         * count is still meaningful, there is just nowhere to send it. */
        size_t wrote = o->len;
        if (t) wrote = term_write(t, o->data, o->len);

        /* Report what actually went out, not what we hoped to send. */
        r->bytes_written = (uint32_t)wrote;
        delivered = (wrote == o->len);
    }

    /* `front` means "what the terminal has actually received". Advancing it
     * after a frame that did not fully arrive turns the diff into a lie: the
     * missing cells are never re-emitted, so they stay wrong on screen until
     * something else happens to overwrite them. Repaint from scratch instead,
     * which costs one frame and self-heals. */
    if (delivered) {
        /* The delivered frame becomes the front by swapping pointers rather
         * than copying 16 bytes a cell: whatever stale contents the old
         * front leaves in the new back are gone the moment rnd_begin clears
         * it. On a short write nothing swaps, so front keeps meaning what
         * the terminal actually received. */
        Cell *swap = r->front;
        r->front = r->back;
        r->back  = swap;
        r->force_full = 0;
    } else {
        r->force_full = 1;
    }
}

void rnd_dump(const Renderer *r, ByteBuf *out)
{
    for (int y = 0; y < r->h; y++) {
        for (int x = 0; x < r->w; x++) {
            const Cell *c = &r->back[(size_t)y * (size_t)r->w + (size_t)x];
            if (c->ch == 0) continue;
            char enc[4];
            int  n = utf8_encode(c->ch, enc);
            if (n <= 0) { enc[0] = ' '; n = 1; }
            bb_put(out, enc, (size_t)n);
        }
        bb_putc(out, '\n');
    }
}
