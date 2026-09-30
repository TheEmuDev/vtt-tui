#ifndef VTT_UI_H
#define VTT_UI_H

#include "draw.h"
#include "input.h"
#include "keys.h"
#include "theme.h"

/* ------------------------------------------------------------ key hints */

/* The bar along the bottom of every screen, drawn from the rows of `km` that
 * carry a bar label. Hints are dropped from the right when they do not fit
 * rather than wrapping or truncating mid-word, so table order is priority
 * order. */
void ui_keybar(Renderer *r, const Theme *th, const KeyMap *km);

/* The same, with one label replaced: the row whose `keys` match takes `label`
 * instead of the table's. For a hint whose word depends on the mode's state --
 * what enter would lay, say -- so the bar can say which without the table
 * having to hold every version of the sentence. */
void ui_keybar_ex(Renderer *r, const Theme *th, const KeyMap *km,
                  const char *keys, const char *label);

/* The whole key reference, ? -- a full-screen page listing every map in turn,
 * starting at line `*top`.
 *
 * `top` is both the offset asked for and, on return, the one actually used.
 * Only this function knows how the page lays out, so letting the caller clamp
 * the scrolling would put the page length in a second place that has to agree
 * -- and be wrong until the first frame had been drawn. Returns the total
 * number of lines. */
int  ui_keypage(Renderer *r, const Theme *th, const KeyMap *const *maps, int nmaps,
                int *top, const BoxGlyphs *frame);

/* Title line across the top; `right` is right-aligned and may be NULL. */
void ui_titlebar(Renderer *r, const Theme *th, const char *left, const char *right);

/* ---------------------------------------------------------------- lists */

typedef struct {
    int sel;
    int top;      /* first visible row, for scrolling */
} ListState;

/* Moves the selection by delta, clamped, and scrolls to keep it visible. */
void ui_list_move(ListState *st, int n, int delta, int visible_rows);

/* Draws `n` rows via a callback that fills a buffer for row i. */
typedef void (*UiRowFn)(void *ctx, int i, char *buf, size_t bufsz);
void ui_list_draw(Renderer *r, const Theme *th, Rect area, const ListState *st,
                  int n, UiRowFn row, void *ctx);

/* --------------------------------------------------------------- prompt */

#define UI_PROMPT_MAX 256

typedef struct {
    char title[64];
    char hint[96];
    char buf[UI_PROMPT_MAX];
    int  len;        /* bytes used */
    int  cursor;     /* byte offset */
    int  active;
    int  max;        /* bytes the answer may hold, NUL included; 0 means the buffer */
} TextPrompt;

void ui_prompt_open(TextPrompt *p, const char *title, const char *hint,
                    const char *initial);

/* Returns 1 when the user accepted (Enter), -1 when canceled (Esc),
 * 0 while still editing. */
int  ui_prompt_key(TextPrompt *p, Key k);

/* `frame` picks the border glyphs, so --ascii reaches the chrome and not
 * just the map. */
void ui_prompt_draw(Renderer *r, const Theme *th, const TextPrompt *p,
                    const BoxGlyphs *frame);

/* The same editing state rendered inline on one row, vim's `:` line, rather
 * than as a centered modal. */
void ui_cmdline_draw(Renderer *r, const Theme *th, const TextPrompt *p, int row,
                     char lead);

/* --------------------------------------------------------------- picker */

/* A prompt with a list under it, filtered on every letter: the character
 * templates and the stamps. Matches are anywhere in an item's name or its
 * detail, ignoring case -- the exact name first, then names starting with the
 * text, then names holding it, then details holding it, in the list's order
 * within each. Tab fills in the highlighted name and, pressed again, the next
 * match, without narrowing the list; typing narrows it again. */
#define UI_PICK_NAME   64
#define UI_PICK_DETAIL 72
#define UI_PICK_ROWS   10

typedef struct {
    char name[UI_PICK_NAME];
    char detail[UI_PICK_DETAIL];
} UiPickItem;

typedef struct {
    TextPrompt  p;
    UiPickItem *items;     /* owned */
    int         n;
    int        *match;     /* indices into items, ranked */
    int         nmatch;
    int         sel;       /* into match */
    int         cycling;   /* tab filled the text: the list is kept as it was */
} UiPicker;

/* Takes ownership of items (malloc'd, freed by ui_picker_free). */
void ui_picker_open(UiPicker *pk, const char *title, UiPickItem *items, int n,
                    const char *initial);
void ui_picker_free(UiPicker *pk);
/* 1 accepted (see ui_picker_chosen), -1 canceled, 0 still choosing. */
int  ui_picker_key(UiPicker *pk, Key k);
/* The highlighted item's index in items, or -1 when nothing matches. */
int  ui_picker_chosen(const UiPicker *pk);
void ui_picker_draw(Renderer *r, const Theme *th, const UiPicker *pk,
                    const BoxGlyphs *frame);

/* -------------------------------------------------------------- handout */

/* The handout card (docs/HANDOUTS.md): its title on the border, its body
 * wrapped to the box, line breaks kept, centered; cut short with an ellipsis
 * when the screen is not tall enough. The terminal mirror and the GM's
 * :player preview draw it; the phones draw their own. */
/* A creature's card in a box, its title on the top edge: the text wrapped
 * to the box, "**bold**" drawn bold and "_emphasis_" marks dropped, from
 * wrapped line `top` on, and a "..." on the bottom edge when there is more.
 * `lines` is ui_card_lines' answer when the caller has it, else -1. Returns
 * how many wrapped lines the text makes. */
int  ui_card_draw(Renderer *r, const Theme *th, Rect box, const char *title,
                  const char *text, int top, int lines, const BoxGlyphs *frame);
/* How many wrapped lines the text makes in a box box_w wide. */
int  ui_card_lines(const char *text, int box_w);

void ui_handout_draw(Renderer *r, const Theme *th, const char *title, const char *body,
                     const BoxGlyphs *frame);

/* ---------------------------------------------------------------- modal */

void ui_modal(Renderer *r, const Theme *th, const char *title, const char *body,
              const char *footer, const BoxGlyphs *frame);

/* Centers a message box with a yes/no footer. The caller interprets keys. */
void ui_confirm(Renderer *r, const Theme *th, const char *title, const char *body,
                const BoxGlyphs *frame);

/* --------------------------------------------------------------- chooser */

#define UI_CHOICE_MAX 8

typedef struct {
    char     text[64];
    uint32_t color;     /* the row's foreground, so a colored thing looks it */
} UiChoice;

/* A modal listing a handful of numbered rows. Short by construction -- it
 * never scrolls, so the whole choice is on screen and one keypress answers
 * it. The caller interprets the keys and writes the footer that names them. */
void ui_choice(Renderer *r, const Theme *th, const char *title,
               const UiChoice *items, int n, const char *footer,
               const BoxGlyphs *frame);

#endif /* VTT_UI_H */
