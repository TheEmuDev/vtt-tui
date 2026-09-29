#include "ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "prof.h"
#include "util.h"

void ui_keybar(Renderer *r, const Theme *th, const KeyMap *km)
{
    ui_keybar_ex(r, th, km, NULL, NULL);
}

/* The bar label a row shows, which is the table's unless the caller has one
 * that depends on state the table cannot know. */
static const char *bar_label(const KeyDoc *d, const char *keys, const char *label)
{
    if (keys && label && d->keys && strcmp(d->keys, keys) == 0) return label;
    return d->bar;
}

void ui_keybar_ex(Renderer *r, const Theme *th, const KeyMap *km,
                  const char *keys, const char *label)
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
        const KeyDoc *d  = &km->rows[last];
        const char   *ks = d->bar_keys ? d->bar_keys : d->keys;
        const char   *lb = bar_label(d, keys, label);
        int w = text_width(ks) + 1 + text_width(lb);

        if (w + 2 <= r->w) {
            int x = r->w - w - 1;
            x += draw_text(r, x, y, ks, -1, key);
            x += 1;
            draw_text(r, x, y, lb, -1, bg);
            budget = r->w - w - 3;      /* two spaces clear of the pinned one */
        }
    }

    int x = 1;
    for (int i = 0; i < km->n; i++) {
        const KeyDoc *d = &km->rows[i];
        if (!d->bar || i == last) continue;

        const char *ks = d->bar_keys ? d->bar_keys : d->keys;
        const char *lb = bar_label(d, keys, label);
        int need = text_width(ks) + 1 + text_width(lb) + 2;
        /* Rather than truncate a hint mid-word, stop cleanly. */
        if (x + need > budget) break;
        x += draw_text(r, x, y, ks, -1, key);
        x += 1;
        x += draw_text(r, x, y, lb, -1, bg);
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

/* A key wider than its column (a command with an example, ":link to crypt
 * Entrance") gets a line of its own and its description the next, as a man
 * page does, rather than being cut. */
static void page_key(Page *p, const KeyDoc *d, Style ks, Style ts)
{
    int wide = text_width(d->keys) > PAGE_TEXT_X - PAGE_KEY_X - 1;
    int y = p->y0 + p->line - p->top;
    p->line++;
    if (wide) {
        if (y >= p->y0 && y <= p->y1)
            draw_text_ellipsis(p->r, PAGE_KEY_X, y, d->keys, p->w - PAGE_KEY_X - 2, ks);
        y = p->y0 + p->line - p->top;
        p->line++;
    }
    if (y < p->y0 || y > p->y1) return;

    if (!wide) draw_text(p->r, PAGE_KEY_X, y, d->keys, PAGE_TEXT_X - PAGE_KEY_X - 1, ks);
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
        memmove(p->buf + start, p->buf + p->cursor, (size_t)p->len - (size_t)p->cursor + 1);
        p->len -= p->cursor - start;
        p->cursor = start;
        return 0;
    }
    case KEY_DELETE: {
        if (p->cursor >= p->len) return 0;
        int end = next_char_start(p->buf, p->len, p->cursor);
        memmove(p->buf + p->cursor, p->buf + end, (size_t)p->len - (size_t)end + 1);
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
            memmove(p->buf + e, p->buf + p->cursor, (size_t)p->len - (size_t)p->cursor + 1);
            p->len -= p->cursor - e;
            p->cursor = e;
        }
        return 0;
    }
    if (k.mods & MOD_ALT) return 0;

    char enc[4];
    int  n = utf8_encode(k.ch, enc);
    int cap = p->max > 0 && p->max < UI_PROMPT_MAX ? p->max : UI_PROMPT_MAX;
    if (n <= 0 || p->len + n >= cap) return 0;

    memmove(p->buf + p->cursor + n, p->buf + p->cursor, (size_t)p->len - (size_t)p->cursor + 1);
    memcpy(p->buf + p->cursor, enc, (size_t)n);
    p->len    += n;
    p->cursor += n;
    return 0;
}

/* A dialog's frame: the box cleared, its border in `border`, and the title
 * padded off the border on the top edge. Every dialog here is one. */
static void dialog_frame(Renderer *r, const Theme *th, Rect box, const BoxGlyphs *frame,
                         uint32_t border, const char *title)
{
    Style fs = style(border, th->bg, 0);
    draw_fill(r, box, ' ', style(th->fg, th->bg, 0));
    draw_box(r, box, frame, fs);
    if (!title || !title[0]) return;
    draw_text(r, box.x + 2, box.y, " ", 1, fs);
    draw_text(r, box.x + 3, box.y, title, box.w - 7, style(th->fg, th->bg, ATTR_BOLD));
    draw_text(r, box.x + 3 + imin(text_width(title), box.w - 7), box.y, " ", 1, fs);
}

/* A one-line entry field, scrolled so the cursor stays in view in a long
 * entry, the cursor cell in the accent. */
static void entry_field(Renderer *r, const Theme *th, Rect field, const TextPrompt *p)
{
    Style fst = style(th->fg, th->sel_bg, 0);
    draw_fill(r, field, ' ', fst);
    char before[UI_PROMPT_MAX];
    memcpy(before, p->buf, (size_t)p->cursor);
    before[p->cursor] = '\0';
    int cw = text_width(before), shift = imax(0, cw - (field.w - 2));
    draw_text(r, field.x, field.y, p->buf + imin(shift, p->len), field.w - 1, fst);
    Cell *c = rnd_at(r, field.x + cw - shift, field.y);
    if (c) { c->bg = th->accent; c->fg = th->bg; }
}

void ui_prompt_draw(Renderer *r, const Theme *th, const TextPrompt *p,
                    const BoxGlyphs *frame)
{
    int w = imin(imax(48, text_width(p->title) + 8), r->w - 4);
    int h = p->hint[0] ? 6 : 5;
    Rect box = rect_center(rect(0, 0, r->w, r->h), w, h);

    Style dim = style(th->dim, th->bg, 0);

    dialog_frame(r, th, box, frame, th->accent, p->title);
    entry_field(r, th, rect(box.x + 2, box.y + 2, box.w - 4, 1), p);

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

/* --------------------------------------------------------------- picker */

/* Is `needle` in `hay`, ignoring ASCII case? */
static int has_ci(const char *hay, const char *needle)
{
    size_t nl = strlen(needle);
    if (!nl) return 1;
    for (; *hay; hay++)
        if (!strncasecmp(hay, needle, nl)) return 1;
    return 0;
}

static void picker_filter(UiPicker *pk)
{
    PROF_ZONE("picker");
    const char *q = pk->p.buf;
    size_t ql = strlen(q);
    pk->nmatch = 0;
    for (int rank = 0; rank < 4; rank++)
        for (int i = 0; i < pk->n; i++) {
            const UiPickItem *it = &pk->items[i];
            int r;
            if (!strcasecmp(it->name, q))           r = 0;
            else if (!strncasecmp(it->name, q, ql)) r = 1;
            else if (has_ci(it->name, q))           r = 2;
            else if (has_ci(it->detail, q))         r = 3;
            else continue;
            if (ql == 0) r = 1;                     /* nothing typed: the list as it is */
            if (r == rank) pk->match[pk->nmatch++] = i;
        }
    pk->sel = 0;
}

void ui_picker_open(UiPicker *pk, const char *title, UiPickItem *items, int n,
                    const char *initial)
{
    ui_picker_free(pk);
    ui_prompt_open(&pk->p, title, "", initial);
    pk->p.max = UI_PICK_NAME;
    pk->items = items;
    pk->n     = n;
    pk->match = xmalloc(sizeof(int) * (size_t)(n > 0 ? n : 1));
    picker_filter(pk);
}

void ui_picker_free(UiPicker *pk)
{
    free(pk->items);
    free(pk->match);
    memset(pk, 0, sizeof *pk);
}

int ui_picker_chosen(const UiPicker *pk)
{
    return pk->nmatch ? pk->match[pk->sel] : -1;
}

static void picker_move(UiPicker *pk, int d)
{
    if (!pk->nmatch) return;
    pk->sel = ((pk->sel + d) % pk->nmatch + pk->nmatch) % pk->nmatch;
}

int ui_picker_key(UiPicker *pk, Key k)
{
    if (k.kind == KEY_ENTER) return pk->nmatch ? 1 : 0;
    if (k.kind == KEY_ESC)   return -1;
    if (k.kind == KEY_UP   || (k.kind == KEY_CHAR && (k.mods & MOD_CTRL) && k.ch == 'p')) { picker_move(pk, -1); return 0; }
    if (k.kind == KEY_DOWN || (k.kind == KEY_CHAR && (k.mods & MOD_CTRL) && k.ch == 'n')) { picker_move(pk,  1); return 0; }
    if (k.kind == KEY_TAB) {
        if (!pk->nmatch) return 0;
        /* The first tab fills in what is highlighted; the next ones step on. */
        if (pk->cycling) picker_move(pk, (k.mods & MOD_SHIFT) ? -1 : 1);
        pk->cycling = 1;
        const char *name = pk->items[pk->match[pk->sel]].name;
        str_lcpy(pk->p.buf, name, sizeof pk->p.buf);
        pk->p.len = pk->p.cursor = (int)strlen(pk->p.buf);
        return 0;
    }
    char was[UI_PROMPT_MAX];
    memcpy(was, pk->p.buf, sizeof was);
    ui_prompt_key(&pk->p, k);
    pk->p.active = 1;
    if (strcmp(was, pk->p.buf) != 0) {
        pk->cycling = 0;
        picker_filter(pk);
    }
    return 0;
}

void ui_picker_draw(Renderer *r, const Theme *th, const UiPicker *pk,
                    const BoxGlyphs *frame)
{
    /* As tall as the whole list, so typing never resizes the box. */
    PROF_ZONE("picker.draw");
    int rows = imax(1, imin(imin(UI_PICK_ROWS, pk->n), r->h - 8));
    int w = imin(imax(56, text_width(pk->p.title) + 8), r->w - 4);
    int h = rows + 6;
    Rect box = rect_center(rect(0, 0, r->w, r->h), w, h);

    Style text  = style(th->fg, th->bg, 0);
    Style dim   = style(th->dim, th->bg, 0);
    Style hi    = style(th->fg, th->sel_bg, ATTR_BOLD);

    dialog_frame(r, th, box, frame, th->accent, pk->p.title);
    entry_field(r, th, rect(box.x + 2, box.y + 2, box.w - 4, 1), &pk->p);

    /* The highlight on screen: the list scrolls a page at a time. */
    int ly = box.y + 4, top = pk->sel / rows * rows;
    if (!pk->nmatch) draw_text(r, box.x + 2, ly, "nothing matches", box.w - 4, dim);
    int namew = 0;
    for (int i = top; i < pk->nmatch && i < top + rows; i++)
        namew = imax(namew, text_width(pk->items[pk->match[i]].name));
    namew = imin(namew, (box.w - 8) / 2);
    for (int i = top; i < pk->nmatch && i < top + rows; i++) {
        const UiPickItem *it = &pk->items[pk->match[i]];
        int y = ly + i - top, on = i == pk->sel;
        Style s = on ? hi : text;
        if (on) draw_fill(r, rect(box.x + 1, y, box.w - 2, 1), ' ', hi);
        draw_text(r, box.x + 2, y, on ? ">" : " ", 1, s);
        draw_text_ellipsis(r, box.x + 4, y, it->name, namew, s);
        draw_text_ellipsis(r, box.x + 6 + namew, y, it->detail, box.w - 8 - namew, on ? hi : dim);
    }
    char foot[64];
    snprintf(foot, sizeof foot, " %d of %d   tab complete   enter take   esc cancel ", pk->nmatch, pk->n);
    draw_text(r, box.x + 2, box.y + h - 1, foot, box.w - 4, dim);
}

/* -------------------------------------------------------------- handout */

/* Calls line() for each line of `text` wrapped at `width` cells: at a space
 * when there is one, else mid-word. Returns how many lines. */
static int wrap(const char *text, int width, void (*line)(void *ctx, int i, const char *s, size_t n),
                void *ctx)
{
    int         lines = 0;
    const char *p = text;
    for (;;) {
        const char *eol = strchr(p, '\n');
        const char *end = eol ? eol : p + strlen(p);
        /* One paragraph, cut into lines. */
        do {
            const char *q = p, *brk = NULL;
            int w = 0;
            while (q < end) {
                uint32_t cp;
                int n = utf8_decode(q, (size_t)(end - q), &cp);
                int cw = utf8_width(cp);
                if (w + cw > width) break;
                if (cp == ' ') brk = q;
                w += cw;
                q += n;
            }
            const char *cut = q;
            if (q < end && brk && brk > p) cut = brk;     /* break at the last space */
            if (q < end && cut == p) cut = q > p ? q : p + 1;
            if (line) line(ctx, lines, p, (size_t)(cut - p));
            lines++;
            p = cut;
            while (p < end && *p == ' ') p++;            /* the space it broke at */
        } while (p < end);
        if (!eol) break;
        p = eol + 1;
    }
    return lines;
}

typedef struct {
    Renderer *r;
    int       x, y, rows, width;
    Style     s;
} HandoutDraw;

static void handout_line(void *ctx, int i, const char *s, size_t n)
{
    HandoutDraw *h = ctx;
    if (i >= h->rows) return;
    char buf[4096];                    /* a line is cells wide, not bytes: combining marks */
    if (n >= sizeof buf) n = sizeof buf - 1;
    memcpy(buf, s, n);
    buf[n] = '\0';
    draw_text(h->r, h->x, h->y + i, buf, h->width, h->s);
}

void ui_handout_draw(Renderer *r, const Theme *th, const char *title, const char *body,
                     const BoxGlyphs *frame)
{
    PROF_ZONE("handout.draw");
    int w  = imin(64, r->w - 4);
    int iw = w - 6;
    if (iw < 8 || r->h < 5) return;
    int lines = wrap(body, iw, NULL, NULL);
    int rows  = imin(lines, r->h - 6);
    if (rows < 1) rows = 1;
    Rect box = rect_center(rect(0, 0, r->w, r->h), w, rows + 4);

    Style text  = style(th->fg, th->bg, 0);
    dialog_frame(r, th, box, frame, th->accent, title);
    HandoutDraw h = { r, box.x + 3, box.y + 2, rows, iw, text };
    wrap(body, iw, handout_line, &h);
    if (lines > rows)
        draw_text(r, box.x + box.w - 5, box.y + box.h - 1, " … ", 3, style(th->dim, th->bg, 0));
}

/* ---------------------------------------------------------------- modal */

void ui_modal(Renderer *r, const Theme *th, const char *title, const char *body,
              const char *footer, const BoxGlyphs *frame)
{
    int w = imax(text_width(body) + 6, text_width(title) + 8);
    w = imin(w, r->w - 4);
    if (footer) w = imax(w, imin(text_width(footer) + 6, r->w - 4));

    Rect box = rect_center(rect(0, 0, r->w, r->h), w, 7);

    Style text  = style(th->fg, th->bg, 0);
    Style dim   = style(th->dim, th->bg, 0);

    dialog_frame(r, th, box, frame, th->warn, title);

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

    Style dim   = style(th->dim, th->bg, 0);

    dialog_frame(r, th, box, frame, th->warn, title);

    _Static_assert(UI_CHOICE_MAX <= 9, "the row numbers have to stay one key each");

    for (int i = 0; i < n; i++) {
        const char num[2] = { (char)('1' + i), '\0' };
        draw_text(r, box.x + 3, box.y + 2 + i, num, 2, dim);
        draw_text_ellipsis(r, box.x + 6, box.y + 2 + i, items[i].text, box.w - 9,
                           style(items[i].color, th->bg, 0));
    }

    if (footer) draw_text(r, box.x + 3, box.y + n + 3, footer, box.w - 6, dim);
}
