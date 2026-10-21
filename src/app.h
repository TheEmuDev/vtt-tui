#ifndef VTT_APP_H
#define VTT_APP_H

#include "clock.h"
#include "ctl.h"
#include "editor.h"
#include "input.h"
#include "job.h"
#include "world.h"
#include "request.h"
#include "map.h"
#include "mapio.h"
#include "net.h"
#include "play.h"
#include "ruler.h"
#include "slog.h"
#include "stamp.h"
#include "render.h"
#include "settings.h"
#include "term.h"
#include "theme.h"
#include "turn.h"
#include "ui.h"
#include "undo.h"

typedef enum {
    SCREEN_MENU,
    SCREEN_BROWSER,
    SCREEN_EDITOR,     /* build mode */
    SCREEN_PLAY,
    SCREEN_HELP,       /* the ? reference, over whatever called it */
    SCREEN_OPTIONS,    /* the GM's settings, from the menu (app_options.c) */
} Screen;

/* Whose eyes a frame is drawn for. The GM's terminal is VIEW_GM; the
 * players' frame -- what the phones and the watcher receive, and what
 * :player preview shows the GM -- is VIEW_PLAYERS, which draws no modal, no
 * prompt, no profiler overlay and no hint that a note exists. */
typedef enum { VIEW_GM, VIEW_PLAYERS } View;

typedef enum { PCAM_FOLLOW, PCAM_PARTY, PCAM_HOLD } PlayersCamera;

typedef enum {
    MODAL_NONE,
    MODAL_PROMPT,
    MODAL_MESSAGE,     /* dismissed by any key */
    MODAL_CONFIRM_QUIT,
    MODAL_CONFIRM_DISCARD,
    MODAL_CONFIRM_DELETE,
    MODAL_CLEAR_STATUS,   /* which of a token's markers to take off */
    MODAL_CONFIRM_RECOVER,   /* an autosave newer than the map: take it? */
    MODAL_CONFIRM_HELD,      /* another vtt has this map open: open it here too? */
    MODAL_CONFIRM_ROWS,      /* the file had a row added or lost: save the rows as read? */
    MODAL_CONFIRM_KEY,       /* :agent key remove: take the stored API key away? */
    MODAL_CHOOSE_AGENT,      /* the first :ask with no agent chosen: how should agents answer? (app_agent.c) */
    MODAL_PICKER,            /* a list filtered as the GM types: App.picker */
    MODAL_CARD,              /* :card -- a creature's card whole, scrolled by card_top */
} ModalKind;

/* A handout's body: 2 KB of text, and its NUL. */
#define HANDOUT_BODY_MAX 2049

/* App.pending after i t: not a key of its own, so no key can collide. */
#define PENDING_IT 0x110000u

/* What the picker is choosing from. */
typedef enum { PICK_CHARACTER, PICK_STAMP, PICK_SCENE, PICK_HANDOUT, PICK_ENV } PickWhat;

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
    PROMPT_INITIATIVE,
    PROMPT_TOKEN_SEARCH,
    PROMPT_NOTE,
    PROMPT_COUNTERS,
    PROMPT_JOB_FEEDBACK,
    PROMPT_API_KEY,          /* the hidden prompt: the key goes to the keyring (app_agent_key_typed) */
    PROMPT_SETTING,          /* an Options row's text or number: App.options.prompt (app_options_prompt) */
} PromptWhat;

/* A ping: a ring round a block of squares, until a moment passes. */
typedef enum {
    DISK_NONE,          /* the file is as vtt left it */
    DISK_NOTICED,       /* it changed; not read yet */
    DISK_PROPOSED,      /* its change waits for :review, as job `disk_job` */
    DISK_STUCK,         /* it changed in a way no proposal can say (unreadable, resized) */
} DiskState;

#define AGENT_CMD_MAX SETTINGS_COMMAND_MAX   /* a command typed in */
#define AGENT_LINE_MAX 4400                    /* what is run: the Claude agent's line has vtt's path in it */
/* An agent vtt started (app_agent.c; docs/V2.md, D8): its child, cut from
 * the terminal and reaped by the tick. One a job: a second start of a job
 * whose agent still runs waits for it (`again`). Silent for
 * AGENT_DEADLINE_MS -- no request under its name -- it is told to stop, and
 * AGENT_GRACE_MS later made to. */
#define AGENT_DEADLINE_MS (10 * 60 * 1000)
#define AGENT_GRACE_MS    5000
/* The name vtt gives the agent it starts for job N, and so the name such a
 * job's taker has: the one place it is written. */
#define AGENT_NAME_FMT    "job-%d"
typedef struct {
    long     pid;
    int      job;           /* its job's number; 0 once that job is gone */
    uint64_t heard_ms;      /* first seen by a tick, or last heard from on the channel; 0 not yet seen */
    uint8_t  again;         /* feedback came while it ran: start the job's agent again when it stops */
    uint8_t  stopping;      /* past its deadline: SIGTERM sent at heard_ms */
} AgentRun;

#define PING_SHOW_MS 2000
#define PING_GM      0u                     /* phones are 1 and up */
#define PING_MAX     (NET_MAX_CLIENTS + 1)
typedef struct {
    uint32_t who;
    int      x0, y0, x1, y1;
    uint64_t until_ms;
} Ping;

/* A summon in progress (app_character.c). The range overlay is the
 * highlight; this is what is in hand and what to put back afterwards. */
typedef struct {
    Map         *tpl;           /* the template, read once; NULL when not summoning */
    char         name[MAP_NAME_MAX];
    int          left;          /* still to place; not counted with `any` */
    int          placed;
    int          any;           /* place until esc */
    int          hidden;        /* put them down hidden */
    unsigned     stamp;         /* the undo log's after the summon's last change:
                                 * u takes one back only while it still matches */
    uint8_t      saved_size;    /* the cursor's footprint before */
    RangeOverlay saved_range;   /* the overlay before, shape and all */
} Summon;

/* The ? page (app_draw.c): which screen to go back to, which key map to lead
 * with, and how far down it is scrolled. */
typedef struct {
    Screen   from;
    KeyMapId id;
    int      top;
    int      lines;    /* what the last draw measured, for clamping */
} HelpView;

/* The Options screen (app_options.c): its row, the row a prompt is for, and
 * what was read when it opened (the key's source and last four, the spend). */
typedef struct {
    ListState list;
    int      prompt;
    int      key;
    char     key_last4[8];
    double   spent, spent_month;
    int      runs, runs_month;
} OptionsView;

/* The handout (app_handout.c; docs/HANDOUTS.md): the last one shown, kept so
 * :handout on puts it back, and whether it is up on the players' screens.
 * The server holds its own copy for phones that join later. */
typedef struct {
    char title[64];
    char body[HANDOUT_BODY_MAX];
    int  up;
} HandoutState;

/* The review (app_job.c): `slot` is the job ED_REVIEW shows (a slot of
 * World.jobs, -1 for none), and the review's own box (v) is held here, not
 * in the editor's visual mode: on, and its anchor. */
typedef struct {
    int slot;
    int box, ax, ay;
} ReviewState;

typedef struct {
    /* The map, its log, the jobs, the events and the request being run: what
     * a request handler is given (world.h). */
    World    world;
    Term        *term;
    Renderer    *rnd;
    const Theme *th;

    Screen screen;
    int    running;
    int    dirty;      /* a redraw is owed */
    int    ascii;
    View   view;       /* what app_draw_view is drawing right now */
    int    preview;    /* :player preview -- the GM's terminal shows VIEW_PLAYERS */

    char status[160];

    /* Stretches of the status message drawn in a color of their own -- the
     * two duality dice. Byte offsets into status; cleared with every new
     * message, so a span can never outlive the text it was measured on. */
    struct { int at, len; uint32_t fg; } status_span[2];
    int nstatus_span;
    /* The status message is the GM's alone -- a counter's value, say -- and
     * the players' frame leaves it out. Cleared with every new message. */
    int status_gm;
    /* Something was hidden when the key being handled began. */
    int key_saw_hidden;

    ListState menu;

    MapEntry *entries;
    int       nentries;
    ListState browser;

    Editor  ed;
    Play    play;

    /* Measuring is available in both build and play, so it lives beside the
     * editor rather than inside either mode's state. */
    Ruler   ruler;

    /* The players' floor (docs/FLOORS.md), by name like Editor.floor: the
     * floor their screens show, a pin the GM set, the floor each side last
     * moved on (the tie-breaker's third step), the creature that had the
     * turn at the last key (a change is a turn starting), and the camera
     * their frame is drawn through when their floor is not the GM's. */
    char     pfloor[AREA_NAME_MAX];
    char     ppin[AREA_NAME_MAX];
    char     side_floor[2][AREA_NAME_MAX];    /* [0] players, [1] enemies */
    int      last_acting;
    GridView pview;
    char     pview_floor[AREA_NAME_MAX];      /* the floor pview was centered for */
    int      psplit;                          /* the players' frame is being drawn through pview */
    /* The players' camera (docs/CAMERA.md): the GM's (follow), framing the
     * party, or held where the GM left it. Party's framing is worked out
     * again only when what it was worked out from changes. */
    PlayersCamera pcam;
    int      pcam_pinned;                     /* hold pinned their floor: undone when it ends */
    struct { const Map *map; unsigned gen; int floor, zoom; Rect view; } pcam_for;

    /* The session log, off until :log. */
    SessionLog slog;

    /* The remote view's server, off until :serve. main polls it. */
    Net net;

    /* The control channel, off until :agent on (docs/CONTROL.md). main
     * polls it; app_tick runs what it has read. */
    Ctl ctl;

    /* The recovery autosave: a copy of the map written beside its file once
     * the changes have been quiet for a moment, removed by a save or a
     * deliberate discard, and offered back the next time the map is opened
     * if it is still there -- which it only is after a crash or a lost
     * terminal. Off for headless runs, which would litter. */
    int      autosave_on;
    unsigned autosave_gen;   /* the map generation the autosave holds */
    int      autosave_said;  /* a failure is on the GM's line: not again until one works or :w */

    /* Pings: rings on the table for PING_SHOW_MS, one per source -- the
     * GM (PING_GM) or a phone, by its connection id -- a newer one from the
     * same source replacing it. Not the map's: nothing here is saved. */
    Ping     pings[PING_MAX];
    int      npings;
    /* The last ping from each source, kept after its ring comes down, for
     * the control channel's `marked`: "the spot I pinged a minute ago".
     * until_ms here is when it was made. Cleared with the map. */
    Ping     pinged[PING_MAX];
    int      npinged;
    /* Round what the control channel's last edit changed, for the same two
     * seconds; the GM's screen alone (never the players' frame), and only
     * while until_ms is set. */
    Ping     agent_ring;
    ReviewState review;
    /* Another writer of the map's file (app_disk.c): whether the file has
     * changed since vtt last read or wrote it (Map.disk), the change's
     * proposal (`disk_job`), and the file's bytes and identity as last seen,
     * which become the map's base once the change is reviewed. */
    int      save_force;        /* :w! -- over a change on disk nobody has reviewed */
    /* The open map's file as last read had a row added or lost by hand
     * (W031, E011: maptools_file_finish): its line, 0 for none. A save over
     * it writes the rows as read and the evidence is gone, so it asks once
     * (file_rows_ok); a trip, which saves, refuses. */
    int      file_rows, file_rows_ok;
    int      save_then_close;   /* :wq, waiting on that question */
    int      save_pending_force;
    char     save_pending[MAP_PATH_MAX];
    int      ask_holders;       /* interactive: opening a map asks the other vtts first */
    int      open_anyway;       /* ... and the GM said yes */
    int      disk_state;        /* DiskState */
    int      disk_reread;       /* the file moved again: read it when the keys go quiet */
    int      disk_job;
    int      disk_full;         /* DISK_STUCK only for want of a job slot: read again once one is free */
    MapDisk  disk_seen;
    char    *disk_new;
    size_t   disk_new_len;
    char     disk_why[160];     /* DISK_STUCK: what cannot be made a proposal */
    uint64_t disk_checked_ms, disk_key_ms;
    unsigned ctl_refused;       /* the last refusal logged, hashed: a caller repeating itself is one line */
    uint64_t ctl_turn_ns;       /* CTL_TURN_NS; a test sets 0 for one request a turn */
    /* :agent command (app_agent.c): what vtt starts for each :ask and each
     * job sent back, the job on its stdin. Empty: none. */
    AgentRun agents[JOB_MAX];
    int      nagents;
    int      agent_deadline_ms; /* AGENT_DEADLINE_MS; a test sets less */
    int      agent_grace_ms;    /* AGENT_GRACE_MS; likewise */
    char     agent_cmd[AGENT_LINE_MAX];  /* what an :ask starts now, from the settings (app_agent_apply) */
    int      agent_cmd_off;     /* :agent command off: kept, not started */
    int      agent_from_env;    /* agent_cmd is VTT_AGENT_COMMAND's, for this session */

    /* The GM's settings (settings.c; docs/APIAGENT.md): what the Options
     * screen and :agent change. Saved to settings_file on each change; ""
     * (a test, a headless run) keeps them in memory only. */
    Settings settings;
    char     settings_file[1024];
    int      key_then;          /* what the key prompt does once a key is stored: KEY_THEN_* (app_agent.c) */
    int      choice_job;        /* the job the agent choice box (or what it opened) is for; 0: none */
    OptionsView options;

    TextPrompt prompt;
    PromptWhat prompt_what;

    HandoutState handout;

    /* The picker (MODAL_PICKER): the character templates or the stamps, and
     * for a character the side it goes down on, -1 for the side it was
     * saved on. */
    UiPicker   picker;
    PickWhat   pick_what;
    int        pick_kind;
    /* The names the character picker is to hold, when it is not to hold
     * them all (:env adversaries); NULL for all. Set round app_pick_open. */
    char     (*pick_only)[MAP_NAME_MAX];
    int        npick_only;
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

    /* The stamp in hand: what y copied or :stamp picked, ready for p. Kept
     * across maps, so a piece of one map can go down on another. `turns`
     * and `mirrored` are only for the status line: the stamp itself is
     * turned each time. */
    Map     *stamp;
    StampShow stamp_view;                /* the preview's buffers, kept across frames (stamp.h) */
    char     stamp_name[MAP_NAME_MAX];   /* "" for a copy not yet saved */
    int      stamp_turns, stamp_mirrored;

    HelpView help;

    /* Cards (docs/CARDS.md): the box beside the map is on unless :card off
     * says otherwise; :card's view is scrolled to card_top, and names whose
     * card it shows (card_token), the last draw measuring card_lines. */
    int      card_box_off;
    int      card_top;
    int      card_token;
    /* :condition's text in the same view, in place of a creature's card;
     * NULL when the view is a card's. */
    const char *card_text;
    char        card_title[48];
    char       *card_own;      /* :env's copy of a card: the map's may change under the view */
    char        cond_modal[640];
    /* The box's text for a creature with a marker the ruleset knows: its
     * card and what each such marker does, built when what it was built from
     * changes (app_card_box_text). */
    char      *box_text;
    const Map *box_map;
    int        box_token;
    unsigned   box_gen, box_cards_gen;
    int      card_lines;
    /* The map and generation the phones' names were last offered from
     * (app_whisper.c): rebuilt only when one changes. */
    const Map *offer_map;
    unsigned   offer_gen;

    /* :summon (docs/SUMMON.md): a template in hand and the range it may be
     * put down in. `tpl` is NULL when nothing is being summoned. */
    Summon   summon;

    /* A prefix key waiting for the one that completes it -- i for placing, s
     * for markers, PENDING_IT for i t. 0 when nothing is pending. */
    uint32_t pending;

    ModalKind modal;
    char      modal_title[64];
    char      modal_body[192];
} App;

void app_init(App *a, Term *t, Renderer *r);
void app_free(App *a);
/* The floor the GM's screen shows, as an area index; -1 for the whole map. */
int  app_floor_shown(const App *a);
/* The players' floor, worked out afresh (a pin, the turn, the spotlight,
 * else the party); -1 for the whole map. */
int  app_players_floor(App *a);
/* The players are on a floor the GM is not showing: their frame is drawn
 * through its own camera. */
int  app_players_split(const App *a);
/* Their frame is drawn through their own camera, pview: on a split floor,
 * or with :player camera party or hold. Pings and the frame ask this. */
int  app_players_own_camera(const App *a);
void app_key(App *a, Key k);
/* Works fog's sight out again if anything on the map changed since last
 * time -- a creature moved, a door opened, a patch was painted, an undo.
 * app_key calls it after every key, so it is once a keystroke at most. */
void app_fog_sync(App *a);
/* Draws into a->rnd: the GM's view, or the players' under :player preview. */
void app_draw(App *a);
void app_draw_view(App *a, View view);

/* One whole frame: the GM's view to the terminal (NULL for headless), then,
 * with clients attached and play mode live, the players' frame to them --
 * drawn when the two views could differ, copied when they cannot. Every
 * frame the app shows goes through here, so main, the bench and the tests
 * cannot disagree about the sequence. */
void app_frame(App *a, Term *t, uint64_t now_ms);

/* Could the players' frame differ from the GM's right now? Conservative:
 * true unless nothing GM-only is on screen. This is a privacy boundary. */
/* The creature whose card the box beside the map shows -- the selected
 * one, else the one under the cursor, in play mode with the box on -- or -1. */
int  app_card_shown(const App *a);
/* A Horde's changed attack while half or more of its HP is marked, for the
 * card box's title: 1 with it in buf (app_damage.c). */
int  app_horde_note(const App *a, int idx, char *buf, size_t sz);
int  app_view_differs(const App *a);

/* Why an agent's edit would be refused right now, or NULL when it would
 * be taken. */
const char *app_ctl_busy(const App *a);

void app_set_status(App *a, const char *msg);
void app_note(App *a, const char *msg);     /* status line + session log */
void app_note_gm(App *a, const char *msg);  /* the same, kept off the players' frame */
void app_set_status_gm(App *a, const char *msg);  /* a hint or error the table must not see */
void app_status_span(App *a, int at, int len, uint32_t fg);   /* color part of it */

/* Whether the remote view should be streaming this frame: play mode is what
 * the players may see. GM-only things are not in the players' frame at all,
 * so nothing else has to freeze it. */
static inline int app_remote_live(const App *a)
{
    return a->screen == SCREEN_PLAY;
}

/* How to run this binary again, for :mirror's second window; main sets it
 * from /proc/self/exe or argv[0]. */
extern const char *app_self_path;

/* Opens a map by path, replacing whatever is loaded. Returns 0 on success
 * and leaves a message modal up on failure -- or, on success, the offer of
 * a newer autosave, which the next key answers. */
int  app_open_map(App *a, const char *path);

/* The autosave's clock. app_tick is called once round the event loop with
 * the time; app_autosave_due says how many ms until it will want to write,
 * -1 for never, so the loop can sleep exactly that long. */
#define AUTOSAVE_QUIET_MS 1500
void app_tick(App *a, uint64_t now_ms);
int  app_autosave_due(const App *a, uint64_t now_ms);

/* Events for agents (app_event.c). After a key and in every tick: keeps the
 * map's checkpoint running while an agent listens, makes the map-changed
 * event once the map has been quiet, and answers the held waits that have
 * something to hear, or have waited their time. app_events_due is the
 * milliseconds until a flush has something to do, -1 for nothing owed. */
void app_events_flush(App *a, uint64_t now_ms);
int  app_events_due(const App *a, uint64_t now_ms);
/* The agents vtt started (app_agent.c): app_tick reaps the ones that
 * stopped and stops the ones past their deadline; this is when the next
 * deadline falls, -1 with none running. */
int  app_agents_due(const App *a, uint64_t now_ms);
/* The settings (and VTT_AGENT_COMMAND) made what :ask starts: agent_cmd,
 * agent_cmd_off. main calls it once they are read. */
void app_agent_apply(App *a);

/* Another writer of the file (app_disk.c): app_disk_due is the milliseconds
 * until a changed file is read (once the keys are quiet), -1 for none. */
int  app_disk_due(const App *a, uint64_t now_ms);

/* Pings. app_ping rings the block x0..x1, y0..y1 for `who` and says so on
 * the status line; app_ping_cell is a phone's tap, a screen cell of the
 * frame it was shown, turned into the square under it -- or nothing, off
 * the map or out of play. app_tick drains the server's taps and takes
 * rings down; app_ping_due is ms until the next goes, -1 with none. */
void app_ping(App *a, uint32_t who, int x0, int y0, int x1, int y1);
int  app_ping_cell(App *a, uint32_t who, int sx, int sy);
int  app_ping_due(const App *a, uint64_t now_ms);
int  app_autosave(App *a);      /* writes it now; 0 on success */
/* The autosave holds generation `gen` of the map, and a failure of the next
 * one is news: after a save, and wherever a new map comes in. */
void app_autosave_track(App *a, unsigned gen);

#endif /* VTT_APP_H */
