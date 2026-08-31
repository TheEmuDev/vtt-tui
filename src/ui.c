#include "ui.h"

#include <stdio.h>
#include <string.h>

void ui_keybar(Renderer *r, const Theme *th, const KeyMap *km)
{
    Style bg  = style(th->bar_fg, th->bar_bg, 0);
    Style key = style(th->bar_key, th->bar_bg, ATTR_BOLD);

    int y = r->h - 1;
    draw_fill(r, rect(0, y, r->w, 1), ' ', bg);

    /* The last hint is pinned to the right instead of queueing with the rest.
     * Every table ends with ?, and the one hint that has to survive a narrow
     * terminal is the one that leads to all the others. */
    int last = -1;
    for (int i = 0; i < km->n; i++) if (km->rows[i].bar) last = i;

    int budget = r->w;
    if (last >= 0) {
        const KeyDoc *d = &km->rows[last];
        const char   *ks = d->bar_keys ? d->bar_keys : d->keys;
        int w = text_width(ks) + 1 + text_width(d->bar);

        if (w + 2 <= r->w) {
            int x = r->w - w - 1;
            x += draw_text(r, x, y, ks, -1, key);
            x += 1;
            draw_text(r, x, y, d->bar, -1, bg);
            budget = r->w - w - 3;      /* two spaces clear of the pinned one */
        }
    }

    int x = 1;
    for (int i = 0; i < km->n; i++) {
        const KeyDoc *d = &km->rows[i];
        if (!d->bar || i == last) continue;

        const char *ks = d->bar_keys ? d->bar_keys : d->keys;
        int need = text_width(ks) + 1 + text_width(d->bar) + 2;
        /* Rather than truncate a hint mid-word, stop cleanly. */
        if (x + need > budget) break;
        x += draw_text(r, x, y, ks, -1, key);
        x += 1;
        x += draw_text(r, x, y, d->bar, -1, bg);
        x += 2;
    }
}

/* ------------------------------------------------------------- key page */

/* One pass emits the whole reference and draws only the slice on screen, so
 * the total line count comes back from the same walk that renders. Nothing is
 * allocated and nothing has to agree with a second pass. */
typedef struct {
    Renderer *r;
    int line;          /* lines emitted so far, on screen or not */
    int top, y0, y1;   /* scroll offset and the rows available */
    int w;
} Page;

/* Three indents for three levels -- which mode, which family, which key --
 * so the eye can find a family without reading the keys. */
#define PAGE_MAP_X  2
#define PAGE_GRP_X  4
#define PAGE_KEY_X  6
#define PAGE_TEXT_X 20

/* Returns the screen row the line landed on, or -1 when it scrolled past. */
static int page_row(Page *p, int x, const char *text, Style s)
{
    int y = p->y0 + p->line - p->top;
    p->line++;
    if (y < p->y0 || y > p->y1) return -1;

    if (text) draw_text_ellipsis(p->r, x, y, text, p->w - x - 2, s);
    return y;
}

static void page_key(Page *p, const KeyDoc *d, Style ks, Style ts)
{
    int y = p->y0 + p->line - p->top;
    p->line++;
    if (y < p->y0 || y > p->y1) return;

    draw_text(p->r, PAGE_KEY_X, y, d->keys, PAGE_TEXT_X - PAGE_KEY_X - 1, ks);
    draw_text_ellipsis(p->r, PAGE_TEXT_X, y, d->what, p->w - PAGE_TEXT_X - 2, ts);
}

static int keypage_pass(Renderer *r, const Theme *th, const KeyMap *const *maps,
                        int nmaps, int top, const BoxGlyphs *frame);

int ui_keypage(Renderer *r, const Theme *th, const KeyMap *const *maps, int nmaps,
               int *top, const BoxGlyphs *frame)
{
    if (*top < 0) *top = 0;

    int visible = imax(1, r->h - 3);
    int total   = keypage_pass(r, th, maps, nmaps, *top, frame);
    int last    = imax(0, total - visible);

    /* Scrolled past the end: correct the offset and lay it out again. That
     * costs one extra pass on the frame where it happens, which is the frame
     * where the user pressed G or held j at the bottom. */
    if (*top > last) {
        *top = last;
        total = keypage_pass(r, th, maps, nmaps, *top, frame);
    }
    return total;
}

static int keypage_pass(Renderer *r, const Theme *th, const KeyMap *const *maps,
                        int nmaps, int top, const BoxGlyphs *frame)
{
    Style text  = style(th->fg, th->bg, 0);
    Style dim   = style(th->dim, th->bg, 0);
    Style key   = style(th->bar_key, th->bg, ATTR_BOLD);
    Style group = style(th->accent, th->bg, ATTR_BOLD);
    Style title = style(th->fg, th->bg, ATTR_BOLD);

    draw_fill(r, rect(0, 0, r->w, r->h), ' ', text);

    /* A rule under the header and above the footer, so the scrolling middle
     * reads as a page rather than as text loose on the screen. */
    ui_titlebar(r, th, "VTT KEYS", nmaps ? maps[0]->name : NULL);
    for (int x = 0; x < r->w; x++)
        draw_cell(r, x, r->h - 2, frame->h, dim);

    Page p = { r, 0, top, 1, r->h - 3, r->w };

    for (int i = 0; i < nmaps; i++) {
        const KeyMap *km = maps[i];

        if (i) page_row(&p, 0, NULL, text);
        int y = page_row(&p, PAGE_MAP_X, km->name, title);
        /* A rule the width of the name, drawn only when the name is on
         * screen, so a title scrolling off does not leave its underline. */
        if (y >= 0) {
            int nw = text_width(km->name);
            for (int x = 0; x < nw; x++)
                draw_cell(r, PAGE_MAP_X + x, y + 1, frame->h, dim);
        }
        page_row(&p, 0, NULL, text);

        for (int j = 0; j < km->n; j++) {
            const KeyDoc *d = &km->rows[j];
            if (!d->keys) {                       /* a group heading */
                if (j) page_row(&p, 0, NULL, text);
                page_row(&p, PAGE_GRP_X, d->what, group);
            } else {
                page_key(&p, d, key, text);
            }
        }
    }

    draw_text(r, 2, r->h - 1, "j k  scroll      ctrl-d ctrl-u  page      "
                              "g G  ends      q esc ?  close", -1, dim);
    return p.line;
}

void ui_titlebar(Renderer *r, const Theme *th, const char *left, const char *right)
{
    Style bg = style(th->bar_fg, th->bar_bg, 0);
    Style hi = style(th->accent, th->bar_bg, ATTR_BOLD);

    draw_fill(r, rect(0, 0, r->w, 1), ' ', bg);
    draw_text_ellipsis(r, 1, 0, left, r->w - 2, hi);

    if (right) {
        int w = text_width(right);
        if (w + 2 < r->w) draw_text(r, r->w - w - 1, 0, right, w, bg);
    }
}

/* ---------------------------------------------------------------- lists */

void ui_list_move(ListState *st, int n, int delta, int visible_rows)
{
    if (n <= 0) { st->sel = 0; st->top = 0; return; }

    st->sel = iclamp(st->sel + delta, 0, n - 1);

    if (visible_rows < 1) visible_rows = 1;
    if (st->sel < st->top)                    st->top = st->sel;
    if (st->sel >= st->top + visible_rows)    st->top = st->sel - visible_rows + 1;
    st->top = iclamp(st->top, 0, imax(0, n - visible_rows));
}

void ui_list_draw(Renderer *r, const Theme *th, Rect area, const ListState *st,
                  int n, UiRowFn row, void *ctx)
{
    Style normal = style(th->fg, th->bg, 0);
    Style sel    = style(th->accent, th->sel_bg, ATTR_BOLD);
    Style dim     = style(th->dim, th->bg, 0);

    for (int i = 0; i < area.h; i++) {
        int idx = st->top + i;
        if (idx >= n) break;

        char buf[256];
        buf[0] = '\0';
        row(ctx, idx, buf, sizeof buf);

        int   y  = area.y + i;
        int   on = (idx == st->sel);
        Style s  = on ? sel : normal;

        if (on) draw_fill(r, rect(area.x, y, area.w, 1), ' ', sel);
        draw_text(r, area.x + 1, y, on ? ">" : " ", 2, on ? sel : dim);
        draw_text_ellipsis(r, area.x + 3, y, buf, area.w - 4, s);
    }

    /* Tell the user there is more above or below rather than silently hiding
     * it; a file list that scrolls invisibly is a list you cannot trust. */
    if (st->top > 0)
        draw_text(r, area.x + area.w - 2, area.y, "^", 1, dim);
    if (st->top + area.h < n)
        draw_text(r, area.x + area.w - 2, area.y + area.h - 1, "v", 1, dim);
}

/* --------------------------------------------------------------- prompt */

void ui_prompt_open(TextPrompt *p, const char *title, const char *hint,
                    const char *initial)
{
    memset(p, 0, sizeof *p);
    str_lcpy(p->title, title, sizeof p->title);
    if (hint) str_lcpy(p->hint, hint, sizeof p->hint);
    if (initial) {
        str_lcpy(p->buf, initial, sizeof p->buf);
        p->len = (int)strlen(p->buf);
    }
    p->cursor = p->len;
    p->active = 1;
}

/* Steps one whole UTF-8 scalar, so cursor motion never lands mid-sequence. */
static int prev_char_start(const char *s, int pos)
{
    if (pos <= 0) return 0;
    pos--;
    while (pos > 0 && ((unsigned char)s[pos] & 0xC0u) == 0x80u) pos--;
    return pos;
}

static int next_char_start(const char *s, int len, int pos)
{
    if (pos >= len) return len;
    pos++;
    while (pos < len && ((unsigned char)s[pos] & 0xC0u) == 0x80u) pos++;
    return pos;
}

int ui_prompt_key(TextPrompt *p, Key k)
{
    switch (k.kind) {
    case KEY_ENTER:
        p->active = 0;
        return 1;
    case KEY_ESC:
        p->active = 0;
        return -1;
    case KEY_LEFT:
        p->cursor = prev_char_start(p->buf, p->cursor);
        return 0;
    case KEY_RIGHT:
        p->cursor = next_char_start(p->buf, p->len, p->cursor);
        return 0;
    case KEY_HOME:
        p->cursor = 0;
        return 0;
    case KEY_END:
        p->cursor = p->len;
        return 0;
    case KEY_BACKSPACE: {
        if (p->cursor == 0) return 0;
        int start = prev_char_start(p->buf, p->cursor);
        memmove(p->buf + start, p->buf + p->cursor, (size_t)(p->len - p->cursor + 1));
        p->len -= p->cursor - start;
        p->cursor = start;
        return 0;
    }
    case KEY_DELETE: {
        if (p->cursor >= p->len) return 0;
        int end = next_char_start(p->buf, p->len, p->cursor);
        memmove(p->buf + p->cursor, p->buf + end, (size_t)(p->len - end + 1));
        p->len -= end - p->cursor;
        return 0;
    }
    default: break;
    }

    if (k.kind != KEY_CHAR) return 0;

    if (k.mods & MOD_CTRL) {
        if (k.ch == 'u') { p->len = p->cursor = 0; p->buf[0] = '\0'; }
        else if (k.ch == 'a') p->cursor = 0;
        else if (k.ch == 'e') p->cursor = p->len;
        else if (k.ch == 'w') {
            /* Delete the previous word, the shell/readline habit. */
            int e = p->cursor;
            while (e > 0 && p->buf[e - 1] == ' ') e--;
            while (e > 0 && p->buf[e - 1] != ' ') e--;
            memmove(p->buf + e, p->buf + p->cursor, (size_t)(p->len - p->cursor + 1));
            p->len -= p->cursor - e;
            p->cursor = e;
        }
        return 0;
    }
    if (k.mods & MOD_ALT) return 0;

    char enc[4];
    int  n = utf8_encode(k.ch, enc);
    if (n <= 0 || p->len + n >= UI_PROMPT_MAX) return 0;

    memmove(p->buf + p->cursor + n, p->buf + p->cursor, (size_t)(p->len - p->cursor + 1));
    memcpy(p->buf + p->cursor, enc, (size_t)n);
    p->len    += n;
    p->cursor += n;
    return 0;
}

void ui_prompt_draw(Renderer *r, const Theme *th, const TextPrompt *p,
                    const BoxGlyphs *frame)
{
    int w = imin(imax(48, text_width(p->title) + 8), r->w - 4);
    int h = p->hint[0] ? 6 : 5;
    Rect box = rect_center(rect(0, 0, r->w, r->h), w, h);

    Style fs    = style(th->accent, th->bg, 0);
    Style label = style(th->fg, th->bg, ATTR_BOLD);
    Style text  = style(th->fg, th->bg, 0);
    Style dim   = style(th->dim, th->bg, 0);

    draw_fill(r, box, ' ', text);
    draw_box(r, box, frame, fs);
    draw_text(r, box.x + 2, box.y, " ", 1, fs);
    draw_text(r, box.x + 3, box.y, p->title, box.w - 6, label);
    draw_text(r, box.x + 3 + text_width(p->title), box.y, " ", 1, fs);

    int fy = box.y + 2;
    Rect field = rect(box.x + 2, fy, box.w - 4, 1);
    draw_fill(r, field, ' ', style(th->fg, th->sel_bg, 0));

    /* Scroll the field so the cursor stays visible in a long entry. */
    char before[UI_PROMPT_MAX];
    memcpy(before, p->buf, (size_t)p->cursor);
    before[p->cursor] = '\0';
    int cw = text_width(before);
    int shift = imax(0, cw - (field.w - 2));

    Style field_style = style(th->fg, th->sel_bg, 0);
    draw_text(r, field.x, fy, p->buf + imin(shift, p->len), field.w - 1, field_style);

    Cell *c = rnd_at(r, field.x + (cw - shift), fy);
    if (c) { c->bg = th->accent; c->fg = th->bg; }

    if (p->hint[0]) draw_text(r, box.x + 2, box.y + 4, p->hint, box.w - 4, dim);
    draw_text(r, box.x + 2, box.y + h - 1, " enter accept   esc cancel ", box.w - 4, dim);
}

void ui_cmdline_draw(Renderer *r, const Theme *th, const TextPrompt *p, int row,
                     char lead)
{
    Style s = style(th->fg, th->bar_bg, 0);
    Style c = style(th->accent, th->bar_bg, ATTR_BOLD);

    draw_fill(r, rect(0, row, r->w, 1), ' ', s);

    char lead_s[2] = { lead, '\0' };
    draw_text(r, 1, row, lead_s, 1, c);

    /* Keep the caret in view on a long command. */
    char before[UI_PROMPT_MAX];
    memcpy(before, p->buf, (size_t)p->cursor);
    before[p->cursor] = '\0';
    int cw    = text_width(before);
    int avail = r->w - 4;
    int shift = imax(0, cw - avail);

    draw_text(r, 2, row, p->buf + imin(shift, p->len), avail, s);

    Cell *cell = rnd_at(r, 2 + (cw - shift), row);
    if (cell) { cell->bg = th->accent; cell->fg = th->bar_bg; }
}

/* ---------------------------------------------------------------- modal */

void ui_modal(Renderer *r, const Theme *th, const char *title, const char *body,
              const char *footer, const BoxGlyphs *frame)
{
    int w = imax(text_width(body) + 6, text_width(title) + 8);
    w = imin(w, r->w - 4);
    if (footer) w = imax(w, imin(text_width(footer) + 6, r->w - 4));

    Rect box = rect_center(rect(0, 0, r->w, r->h), w, 7);

    Style fs    = style(th->warn, th->bg, 0);
    Style label = style(th->fg, th->bg, ATTR_BOLD);
    Style text  = style(th->fg, th->bg, 0);
    Style dim   = style(th->dim, th->bg, 0);

    draw_fill(r, box, ' ', text);
    draw_box(r, box, frame, fs);

    /* Pad the title off the border, the way the prompt does. */
    draw_text(r, box.x + 2, box.y, " ", 1, fs);
    draw_text(r, box.x + 3, box.y, title, box.w - 7, label);
    draw_text(r, box.x + 3 + imin(text_width(title), box.w - 7), box.y, " ", 1, fs);

    draw_text_ellipsis(r, box.x + 3, box.y + 2, body, box.w - 6, text);
    if (footer) draw_text(r, box.x + 3, box.y + 4, footer, box.w - 6, dim);
}

void ui_confirm(Renderer *r, const Theme *th, const char *title, const char *body,
                const BoxGlyphs *frame)
{
    ui_modal(r, th, title, body, "y  yes      n  no      esc  cancel", frame);
}

void ui_choice(Renderer *r, const Theme *th, const char *title,
               const UiChoice *items, int n, const char *footer,
               const BoxGlyphs *frame)
{
    if (n > UI_CHOICE_MAX) n = UI_CHOICE_MAX;

    int w = text_width(title) + 8;
    for (int i = 0; i < n; i++) w = imax(w, text_width(items[i].text) + 10);
    if (footer) w = imax(w, text_width(footer) + 6);
    w = imin(w, r->w - 4);

    Rect box = rect_center(rect(0, 0, r->w, r->h), w, n + 5);

    Style fs    = style(th->warn, th->bg, 0);
    Style label = style(th->fg, th->bg, ATTR_BOLD);
    Style text  = style(th->fg, th->bg, 0);
    Style dim   = style(th->dim, th->bg, 0);

    draw_fill(r, box, ' ', text);
    draw_box(r, box, frame, fs);

    draw_text(r, box.x + 2, box.y, " ", 1, fs);
    draw_text(r, box.x + 3, box.y, title, box.w - 7, label);
    draw_text(r, box.x + 3 + imin(text_width(title), box.w - 7), box.y, " ", 1, fs);

    _Static_assert(UI_CHOICE_MAX <= 9, "the row numbers have to stay one key each");

    for (int i = 0; i < n; i++) {
        const char num[2] = { (char)('1' + i), '\0' };
        draw_text(r, box.x + 3, box.y + 2 + i, num, 2, dim);
        draw_text_ellipsis(r, box.x + 6, box.y + 2 + i, items[i].text, box.w - 9,
                           style(items[i].color, th->bg, 0));
    }

    if (footer) draw_text(r, box.x + 3, box.y + n + 3, footer, box.w - 6, dim);
}
