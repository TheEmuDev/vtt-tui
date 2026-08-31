#ifndef VTT_APP_H
#define VTT_APP_H

#include "editor.h"
#include "input.h"
#include "map.h"
#include "mapio.h"
#include "play.h"
#include "ruler.h"
#include "render.h"
#include "term.h"
#include "theme.h"
#include "ui.h"
#include "undo.h"

typedef enum {
    SCREEN_MENU,
    SCREEN_BROWSER,
    SCREEN_EDITOR,     /* build mode */
    SCREEN_PLAY,
    SCREEN_HELP,       /* the ? reference, over whatever called it */
} Screen;

typedef enum {
    MODAL_NONE,
    MODAL_PROMPT,
    MODAL_MESSAGE,     /* dismissed by any key */
    MODAL_CONFIRM_QUIT,
    MODAL_CONFIRM_DISCARD,
    MODAL_CONFIRM_DELETE,
    MODAL_CLEAR_STATUS,   /* which of a token's markers to take off */
} ModalKind;

typedef enum {
    PROMPT_NONE,
    PROMPT_NEW_NAME,
    PROMPT_NEW_SIZE,
    PROMPT_SAVE_AS,
    PROMPT_TOKEN_LABEL,
    PROMPT_RELABEL,
    PROMPT_RENAME_MAP,
    PROMPT_DUPLICATE_MAP,
    PROMPT_STATUS_LABEL,
    PROMPT_TOKEN_SEARCH,
} PromptWhat;

typedef struct {
    Term        *term;
    Renderer    *rnd;
    const Theme *th;

    Screen screen;
    int    running;
    int    dirty;      /* a redraw is owed */
    int    ascii;

    char status[160];

    ListState menu;

    MapEntry *entries;
    int       nentries;
    ListState browser;

    Map    *map;
    Editor  ed;
    Play    play;

    /* Measuring is available in both build and play, so it lives beside the
     * editor rather than inside either mode's state. */
    Ruler   ruler;

    /* One log for the whole session: token moves in play mode undo through
     * the same history as wall edits in build mode. */
    Undo    undo;

    TextPrompt prompt;
    PromptWhat prompt_what;
    char       pending_name[MAP_NAME_MAX];

    /* The file a pending delete or rename acts on, held in full so the
     * question and the action cannot disagree about which one is meant even
     * if the list changes underneath. */
    char       pending_file[MAP_PATH_MAX];

    /* Where and what a pending token placement will become once the label
     * prompt is answered. */
    uint8_t pending_kind;
    uint8_t pending_size;
    int     pending_tx, pending_ty;
    int     pending_token;   /* token a pending status marker hangs on */

    /* The ? page: which screen to go back to, which key map to lead with, and
     * how far down it is scrolled. */
    Screen   help_from;
    KeyMapId help_id;
    int      help_top;
    int      help_lines;   /* what the last draw measured, for clamping */

    /* A prefix key waiting for the one that completes it -- i for placing, s
     * for markers. 0 when nothing is pending. */
    uint32_t pending;

    ModalKind modal;
    char      modal_title[64];
    char      modal_body[192];
} App;

void app_init(App *a, Term *t, Renderer *r);
void app_free(App *a);
void app_key(App *a, Key k);
void app_draw(App *a);
void app_set_status(App *a, const char *msg);

/* Opens a map by path, replacing whatever is loaded. Returns 0 on success
 * and leaves a message modal up on failure. */
int  app_open_map(App *a, const char *path);

#endif /* VTT_APP_H */
