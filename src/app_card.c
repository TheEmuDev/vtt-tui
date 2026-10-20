/* Cards in play (docs/CARDS.md): s k writes a creature's card in the GM's
 * own editor, as git opens a commit message -- the card, or a new one's
 * skeleton, with help lines that are left out again. The table and the
 * file are card.c's and mapio.c's; the box is app_draw.c's. */

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/wait.h>
#include <unistd.h>

#include "app_priv.h"
#include "world_ops.h"
#include "card.h"
#include "character.h"
#include "floor.h"
#include "import.h"
#include "store.h"
#include "prof.h"

/* Runs $VISUAL, else $EDITOR, else vi, on the file, the terminal handed over
 * for the while. Returns the editor's exit status, or -1. */
static int run_editor(App *a, const char *path)
{
    if (a->term) term_suspend(a->term);
    /* As system() does: ctrl-c and ctrl-backslash in the editor are the editor's,
     * not a reason for vtt to die while it waits. */
    struct sigaction ign, old_int, old_quit;
    memset(&ign, 0, sizeof ign);
    ign.sa_handler = SIG_IGN;
    sigemptyset(&ign.sa_mask);
    sigaction(SIGINT, &ign, &old_int);
    sigaction(SIGQUIT, &ign, &old_quit);
    pid_t pid = fork();
    if (pid == 0) {
        signal(SIGINT, SIG_DFL);
        signal(SIGQUIT, SIG_DFL);
        /* Through the shell and eval'd, as git runs one, so an editor named
         * with flags or quotes ("code -w", "\"/opt/My Editor/ed\"") works. */
        execl("/bin/sh", "sh", "-c", "eval \"${VISUAL:-${EDITOR:-vi}}\" '\"$1\"'", "sh", path, (char *)NULL);
        _exit(127);
    }
    int status = -1;
    if (pid > 0) while (waitpid(pid, &status, 0) < 0 && errno == EINTR) { }
    sigaction(SIGINT, &old_int, NULL);
    sigaction(SIGQUIT, &old_quit, NULL);
    if (a->term) {
        term_resume(a->term);
        rnd_resize(a->rnd, a->term->w, a->term->h);
        rnd_invalidate(a->rnd);
    }
    a->dirty = 1;
    return pid > 0 && WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

/* What the help block starts with; everything from it on is left out, and
 * nothing above it -- a card may have its own lines starting with #. */
#define SCISSORS "# ------------------------ >8 ------------------------"

/* The file read back up to the scissors line. NULL when unreadable; *cut
 * set when it held more than a card can. */
static char *read_back(const char *path, int *cut)
{
    size_t len = 0;
    int    big = 0;
    char  *text = file_read(path, (size_t)1 << 20, &len, &big);
    *cut = big;
    if (!text) return NULL;
    for (char *line = text; line && *line; ) {
        if (!strncmp(line, SCISSORS, strlen(SCISSORS))) { *line = '\0'; break; }
        line = strchr(line, '\n');
        if (line) line++;
    }
    if (strlen(text) >= CARD_TEXT_MAX) *cut = 1;
    return text;
}

/* `start` in the GM's editor under the help lines, for the card `name`
 * (`had` when it exists already). Returns what to keep, cleaned as it will be
 * stored, for the caller to free; or NULL, the status line saying why
 * nothing changes. *cut is set when the text was more than a card holds. */
static char *edit_text(App *a, const char *name, const char *start, int had, const char *shown, int *cut)
{
    char path[256];
    const char *tmp = getenv("TMPDIR");
    snprintf(path, sizeof path, "%s/vtt-card-XXXXXX", tmp && tmp[0] ? tmp : "/tmp");
    int fd = mkstemp(path);
    FILE *f = fd >= 0 ? fdopen(fd, "w") : NULL;
    if (!f) {
        if (fd >= 0) { close(fd); unlink(path); }
        app_set_status(a, "cannot write a file for the editor");
        return NULL;
    }
    fputs(start, f);
    fprintf(f, "\n%s\n"
               "# Everything from the line above on is left out.\n"
               "# The card \"%s\" %s\n"
               "# Save and quit to keep it; quit without saving, or empty it, to change nothing.\n",
            SCISSORS, name, shown);
    fclose(f);

    int rc = run_editor(a, path);
    *cut = 0;
    char *text = rc == 0 ? read_back(path, cut) : NULL;
    unlink(path);
    if (rc != 0) {
        app_set_status(a, rc == 127 ? "no editor to run - set $EDITOR" : "the editor did not finish - the card is as it was");
        free(text);
        return NULL;
    }

    /* Nothing to keep, or nothing new: as it was. Compared the way it will
     * be stored. */
    char *now   = card_clean(text);
    char *begun = card_clean(start);
    free(text);
    const char *why = !now[0] ? "an empty card changes nothing - the card is as it was"
                    : strcmp(begun, now) ? NULL
                    /* Untouched: a new card's start is not a card yet. */
                    : had ? "card unchanged" : "no card written - the start was left as it was";
    free(begun);
    if (why) { app_set_status(a, why); free(now); return NULL; }
    return now;
}

void app_card_edit(App *a)
{
    Map *m = a->world.map;
    int  i = app_target_token(a);
    if (i < 0) { app_set_status(a, "no creature here - s k writes the selected one's card"); return; }

    /* Its card, or a new one named after its label: "Crypt Ghoul 2" is
     * crypt-ghoul, the name a saved character of it would have. */
    Token *t = &m->tokens.v[i];
    char name[CARD_NAME_MAX];
    if (t->card[0]) str_lcpy(name, t->card, sizeof name);
    else {
        character_name_from_label(t->label, name, sizeof name);
        if (!card_name_ok(name)) str_lcpy(name, t->kind == TOKEN_PLAYER ? "player" : "creature", sizeof name);
    }
    const char *old = card_find(m, name) >= 0 ? m->cards[card_find(m, name)].text : NULL;

    char root[TOKEN_LABEL_MAX];
    token_label_root(t->label, root, sizeof root);

    /* What the editor opens on: the card, or a new one's start. */
    char *start = xmalloc(CARD_TEXT_MAX);
    if (old) str_lcpy(start, old, CARD_TEXT_MAX);
    else {
        const Ruleset *rs = ruleset_by_name(m->ruleset);
        if (rs && rs->card_skeleton) snprintf(start, CARD_TEXT_MAX, rs->card_skeleton, root[0] ? root : name);
        else                         snprintf(start, CARD_TEXT_MAX, "%s\n", root[0] ? root : name);
    }
    int   cut = 0, had = old != NULL;
    char *now = edit_text(a, name, start, had,
                          "shows beside the map while a creature using it is selected,\n"
                          "# to the GM only; every creature using it shares it. **word** shows bold.", &cut);
    free(start);
    if (!now) return;

    char msg[160];
    if (card_set(m, name, now) < 0) {
        app_set_status(a, "a map holds 64 cards - this one is not kept");
    } else {
        /* A new card goes to the creature, and to the others of its name
         * that have none: Goblin 2 and Goblin 3 want Goblin's. */
        int more = 0;
        t = &m->tokens.v[i];
        if (!t->card[0]) {
            str_lcpy(t->card, name, sizeof t->card);
            for (int j = 0; j < m->tokens.n; j++) {
                Token *o = &m->tokens.v[j];
                char r2[TOKEN_LABEL_MAX];
                if (j == i || o->card[0] || o->kind != t->kind) continue;   /* a player Goblin is not one */
                token_label_root(o->label, r2, sizeof r2);
                if (root[0] && !strcmp(r2, root)) { str_lcpy(o->card, name, sizeof o->card); more++; }
            }
        }
        map_touch(m);
        const char *tail = cut ? " - cut to 4 KB, the rest is gone" : "";
        if (more) snprintf(msg, sizeof msg, "card %s written - and given to %d more %.30s%s", name, more, root, tail);
        else      snprintf(msg, sizeof msg, "card %s %s%s", name, had ? "saved" : "written", tail);
        app_set_status_gm(a, msg);
    }
    free(now);
}

/* ----------------------------------------------------------- environments */

/* Where an environment is kept: an area, or the map. */
typedef struct {
    int   area;         /* index, or -1 for the map */
    char *env;          /* the name's own bytes: Area.env or Map.env */
} EnvAt;

/* The environment the cursor's square reads: the smallest area holding it
 * that has one, else its floor's, else the map's. `any` takes the smallest
 * area whether it has one or not: where a new one goes. */
static EnvAt env_here(App *a, int any)
{
    Map *m  = a->world.map;
    int  ai = map_area_at(m, a->ed.cx, a->ed.cy);
    if (ai >= 0 && (any || m->areas[ai].env[0])) return (EnvAt){ ai, m->areas[ai].env };
    if (!any) {
        int fl = floor_at(m, a->ed.cx, a->ed.cy);
        if (fl >= 0 && m->areas[fl].env[0]) return (EnvAt){ fl, m->areas[fl].env };
    }
    return (EnvAt){ -1, m->env };
}

static void env_where(const Map *m, EnvAt at, char *buf, size_t sz)
{
    if (at.area >= 0) snprintf(buf, sz, "%.31s", m->areas[at.area].name);
    else              snprintf(buf, sz, "the map");
}

/* Names the card as the place's environment, or takes the name off with "". */
static void env_set(App *a, EnvAt at, const char *card)
{
    Map *m = a->world.map;
    char msg[160], where[40];
    env_where(m, at, where, sizeof where);
    WorldErr we;
    if (card[0] && world_card_name(card, &we) < 0) {
        app_set_status(a, we.text);
        return;
    }
    if (!strcmp(at.env, card)) {
        snprintf(msg, sizeof msg, card[0] ? "%s already has %s" : "%s has no environment", where, card);
        app_set_status_gm(a, msg);
        return;
    }
    if (at.area >= 0) {
        int rc = world_area_env(&a->world, at.area, card, &we);
        if (rc < 0) { app_set_status_gm(a, we.text); return; }
    } else {
        /* The map's own goes round the undo log, as the cards do. */
        str_lcpy(m->env, card, sizeof m->env);
        m->cards_gen++;
        map_touch(m);
    }
    /* A card the map lacks is copied from the GM's kept environments, when
     * there is one of that name (--import-environments). */
    int copied = 0, full = 0;
    if (card[0] && card_find(m, card) < 0) {
        char *kept = environment_load(card);
        if (kept) { copied = card_set(m, card, kept) >= 0; full = !copied; }
        free(kept);
    }
    if (!card[0])                     snprintf(msg, sizeof msg, "%s has no environment now", where);
    else if (copied)                  snprintf(msg, sizeof msg, "%s: environment %s, copied from your environments - :env reads it", where, card);
    else if (full)                    snprintf(msg, sizeof msg, "%s: environment %s - not copied in: a map holds 64 cards", where, card);
    else if (card_find(m, card) >= 0) snprintf(msg, sizeof msg, "%s: environment %s - :env reads it", where, card);
    else                              snprintf(msg, sizeof msg, "%s: environment %s - no card of that name yet, :env edit writes it", where, card);
    app_note_gm(a, msg);
}

static void env_edit(App *a, EnvAt at)
{
    Map *m = a->world.map;
    char msg[160], where[40], name[CARD_NAME_MAX];
    env_where(m, at, where, sizeof where);
    if (!at.env[0]) {
        snprintf(msg, sizeof msg, "%s has no environment - :env NAME names its card first", where);
        app_set_status_gm(a, msg);
        return;
    }
    str_lcpy(name, at.env, sizeof name);
    int   ci  = card_find(m, name);
    char *start = xmalloc(CARD_TEXT_MAX);
    if (ci >= 0) str_lcpy(start, m->cards[ci].text, CARD_TEXT_MAX);
    else {
        const Ruleset *rs = ruleset_by_name(m->ruleset);
        if (rs && rs->env_skeleton) snprintf(start, CARD_TEXT_MAX, rs->env_skeleton, name);
        else                        snprintf(start, CARD_TEXT_MAX, "%s\n", name);
    }
    int   cut = 0;
    char *now = edit_text(a, name, start, ci >= 0,
                          "is this place's environment: :env shows it, to the GM only.\n"
                          "# **word** shows bold.", &cut);
    free(start);
    if (!now) return;
    if (card_set(m, name, now) < 0) app_set_status(a, "a map holds 64 cards - this one is not kept");
    else {
        map_touch(m);
        snprintf(msg, sizeof msg, "card %s %s%s", name, ci >= 0 ? "saved" : "written",
                 cut ? " - cut to 4 KB, the rest is gone" : "");
        app_set_status_gm(a, msg);
    }
    free(now);
}

/* The names on a card's "Potential Adversaries" line, each as a saved
 * character would be named. The line groups them, with lists in
 * parentheses ("Beasts (Bear, Dire Wolf), Bandits"): a group's own word
 * before a parenthesis is not a creature, what is inside is. Returns how
 * many, at most `max`. */
static int env_names(const char *line, char (*out)[MAP_NAME_MAX], int max)
{
    int n = 0;
    for (const char *p = line; *p && n < max; ) {
        const char *end = p + strcspn(p, ",");
        const char *from = p, *open = memchr(p, '(', (size_t)(end - p));
        if (open) from = open + 1;
        char label[TOKEN_LABEL_MAX];
        size_t len = (size_t)(end - from);
        if (len >= sizeof label) len = sizeof label - 1;
        memcpy(label, from, len);
        label[len] = '\0';
        char *close = strchr(label, ')');
        if (close) *close = '\0';
        character_name_from_label(label, out[n], sizeof out[n]);
        int seen = !out[n][0];
        for (int k = 0; k < n && !seen; k++) seen = !strcasecmp(out[k], out[n]);
        if (!seen) n++;
        p = *end ? end + 1 : end;
    }
    return n;
}

/* :env adversaries -- the character picker holding only the creatures the
 * environment's card names, those that are saved characters. */
static void env_adversaries(App *a, EnvAt at)
{
    PROF_ZONE("env.pick");
    Map *m = a->world.map;
    char msg[320], where[40], line[512];
    env_where(m, at, where, sizeof where);
    if (a->screen != SCREEN_PLAY) { app_set_status(a, "placing is play mode's - F2 first"); return; }
    int ci = at.env[0] ? card_find(m, at.env) : -1;
    if (ci < 0) {
        snprintf(msg, sizeof msg, at.env[0] ? "%s: environment %s has no card - :env edit writes it"
                                            : "%s has no environment - :env NAME names its card%s", where, at.env);
        app_set_status_gm(a, msg);
        return;
    }
    char names[32][MAP_NAME_MAX];
    int  n = card_line(m->cards[ci].text, "Potential Adversaries", line, sizeof line)
           ? env_names(line, names, 32) : 0;
    if (!n) {
        snprintf(msg, sizeof msg, "card %s has no \"Potential Adversaries:\" line to read", at.env);
        app_set_status_gm(a, msg);
        return;
    }
    /* Those that are saved characters stay; the rest are named. */
    /* The count leads: the list may be longer than the line is wide, and
     * the log keeps it whole. */
    int have = 0, off = 0, missing = 0;
    for (int i = 0; i < n; i++) missing += !character_exists(names[i]);
    off = snprintf(msg, sizeof msg, "%d of %d are saved characters", n - missing, n);
    for (int i = 0, first = 1; i < n; i++) {
        if (character_exists(names[i])) { if (have != i) memcpy(names[have], names[i], sizeof names[0]); have++; }
        else if (off < (int)sizeof msg - 1) {
            off += snprintf(msg + off, sizeof msg - (size_t)off, "%s%s", first ? "; not saved: " : ", ", names[i]);
            first = 0;
        }
    }
    if (!have) {
        char none[400];
        snprintf(none, sizeof none, "none of %.24s's potential adversaries is a saved character - :character save, or --import-adversaries", at.env);
        app_set_status_gm(a, none);
        return;
    }
    a->pick_only  = names;
    a->npick_only = have;
    app_pick_open(a, PICK_CHARACTER, -1, "");
    a->pick_only  = NULL;
    a->npick_only = 0;
    if (missing) app_note_gm(a, msg);
}

static void env_row(const char *name, char *buf, size_t sz)
{
    char *text = environment_load(name);
    if (text) card_first_line(text, buf, sz);
    else      str_lcpy(buf, "cannot be read", sz);
    free(text);
}

UiPickItem *app_env_items(int *count)
{
    char dir[MAP_PATH_MAX];
    store_dir("environments", dir, sizeof dir);
    int n, keep = 0;
    char (*names)[MAP_NAME_MAX] = store_list_all(dir, ".txt", &n);
    /* A file's name may be longer than a card's: one that cannot be a card is not offered. */
    for (int i = 0; i < n; i++)
        if (card_name_ok(names[i])) { if (keep != i) memcpy(names[keep], names[i], sizeof names[0]); keep++; }
    return app_pick_named(names, keep, env_row, count);
}

void app_env_name(App *a, const char *name)
{
    if (a->world.map) env_set(a, env_here(a, 1), name);
}

/* :env                    the environment here, read whole
 * :env NAME               the smallest area under the cursor (else the map) names the card NAME
 * :env map NAME           the map does
 * :env area "AREA" NAME   that area does (a floor is named this way)
 * :env edit               its card in the editor, the ruleset's block to start a new one
 * :env adversaries        the picker, holding the creatures its card names
 * :env pick               the picker, holding the environments kept in the folder
 * :env remove             the name comes off; the card stays in the map */
void app_env_command(App *a, const char *rest)
{
    static const char USAGE[] = ":env, :env NAME, :env map NAME, :env area \"AREA\" NAME, :env edit, :env pick, :env adversaries, :env remove";
    Map *m = a->world.map;
    char msg[200], where[40];
    if (!m) { app_set_status(a, "environments are a map's - open one first"); return; }
    while (*rest == ' ') rest++;

    if (!*rest) {
        EnvAt at = env_here(a, 0);
        env_where(m, at, where, sizeof where);
        if (!at.env[0]) { app_set_status(a, "no environment here - :env NAME names a card for this area, or the map"); return; }
        int ci = card_find(m, at.env);
        if (ci < 0) {
            snprintf(msg, sizeof msg, "%s: environment %s has no card - :env edit writes it", where, at.env);
            app_set_status_gm(a, msg);
            return;
        }
        snprintf(a->card_title, sizeof a->card_title, "%.20s: %.24s", where, at.env);
        free(a->card_own);
        a->card_own  = xstrdup(m->cards[ci].text);
        a->card_text = a->card_own;
        a->card_top  = 0;
        a->modal     = MODAL_CARD;
        return;
    }
    if (!strcmp(rest, "edit"))   { env_edit(a, env_here(a, 0)); return; }
    if (!strcmp(rest, "adversaries")) { env_adversaries(a, env_here(a, 0)); return; }
    if (!strcmp(rest, "pick"))   { app_pick_open(a, PICK_ENV, -1, ""); return; }
    if (!strcmp(rest, "remove")) { env_set(a, env_here(a, 0), ""); return; }

    if (!strncmp(rest, "map ", 4)) {
        const char *name = rest + 4;
        while (*name == ' ') name++;
        if (!*name || strchr(name, ' ')) { app_set_status(a, USAGE); return; }
        env_set(a, (EnvAt){ -1, m->env }, name);
        return;
    }
    if (!strncmp(rest, "area ", 5)) {
        const char *q = rest + 5;
        while (*q == ' ') q++;
        const char *end = *q == '"' ? strchr(q + 1, '"') : NULL;
        char aname[AREA_NAME_MAX];
        if (!end || (size_t)(end - q - 1) >= sizeof aname) { app_set_status(a, USAGE); return; }
        memcpy(aname, q + 1, (size_t)(end - q - 1));
        aname[end - q - 1] = '\0';
        const char *name = end + 1;
        while (*name == ' ') name++;
        if (!*name || strchr(name, ' ')) { app_set_status(a, USAGE); return; }
        int ai = map_area_find(m, aname);
        if (ai < 0) {
            snprintf(msg, sizeof msg, "no area called %.31s - :areas lists them", aname);
            app_set_status_gm(a, msg);            /* as :area says it */
            return;
        }
        env_set(a, (EnvAt){ ai, m->areas[ai].env }, strcmp(name, "remove") ? name : "");
        return;
    }
    if (strchr(rest, ' ')) { app_set_status(a, USAGE); return; }
    env_set(a, env_here(a, 1), rest);
}

/* ------------------------------------------------------------- conditions */

const char *app_card_box_text(App *a, int i)
{
    const Map     *m    = a->world.map;
    const Token   *t    = &m->tokens.v[i];
    const char    *card = card_of(m, t);
    const Ruleset *rs   = ruleset_by_name(m->ruleset);
    if (!ruleset_has_condition(rs, t)) return card;

    /* Built once and kept while the map, the creature and the cards are as
     * they were: the box is drawn every frame the cursor rests there. */
    if (a->box_text && a->box_map == m && a->box_token == i && a->box_gen == m->gen &&
        a->box_cards_gen == m->cards_gen)
        return a->box_text;

    ByteBuf b;
    bb_init(&b, 512);
    if (card) { bb_puts(&b, card); bb_putc(&b, '\n'); }
    for (int k = 0, first = 1; k < t->nstatus; k++) {
        const Condition *c = ruleset_condition(rs, t->status[k].label);
        if (!c) continue;
        if (!first || card) bb_putc(&b, '\n');       /* between lines, never after the last */
        first = 0;
        bb_puts(&b, "**");
        bb_puts(&b, c->name);
        bb_puts(&b, ":** ");
        bb_puts(&b, c->text);
    }
    bb_putc(&b, '\0');
    free(a->box_text);
    a->box_text      = b.data;
    a->box_map       = m;
    a->box_token     = i;
    a->box_gen       = m->gen;
    a->box_cards_gen = m->cards_gen;
    return a->box_text;
}

void app_card_box_forget(App *a)
{
    /* A view of the copy goes with it. */
    if (a->card_text && a->card_text == a->card_own) {
        a->card_text = NULL;
        if (a->modal == MODAL_CARD) a->modal = MODAL_NONE;
    }
    free(a->card_own);
    a->card_own = NULL;
    free(a->box_text);
    a->box_text = NULL;
    a->box_map  = NULL;
}

void app_condition_command(App *a, const char *rest)
{
    char msg[256];
    if (!a->world.map) { app_set_status(a, "conditions are a map's ruleset's - open a map first"); return; }
    const Ruleset *rs = ruleset_by_name(a->world.map->ruleset);
    if (!rs || !rs->nconditions) {
        app_set_status(a, "this ruleset names no conditions - a marker is a word of your own: s a");
        return;
    }
    while (*rest == ' ') rest++;
    if (!*rest) {
        int off = snprintf(msg, sizeof msg, "conditions: ");
        for (int i = 0; i < rs->nconditions && off < (int)sizeof msg - 1; i++)
            off += snprintf(msg + off, sizeof msg - (size_t)off, "%s%s", i ? ", " : "", rs->conditions[i].name);
        if (off < (int)sizeof msg - 1)
            snprintf(msg + off, sizeof msg - (size_t)off, " - :condition NAME reads one, s a puts one on");
        app_set_status(a, msg);
        return;
    }
    const Condition *c = ruleset_condition(rs, rest);
    if (!c) {
        snprintf(msg, sizeof msg, "no condition called %.24s - :condition lists them", rest);
        app_set_status(a, msg);
        return;
    }
    snprintf(a->cond_modal, sizeof a->cond_modal, "%s%s%s", c->text, c->note ? "\n\n" : "", c->note ? c->note : "");
    str_lcpy(a->card_title, c->name, sizeof a->card_title);
    a->card_text = a->cond_modal;
    a->card_top  = 0;
    a->modal     = MODAL_CARD;
}

const char *app_condition_said(CondChange cc)
{
    switch (cc) {
    case COND_ADDED:   return " - now ";
    case COND_REMOVED: return " - no longer ";
    case COND_NO_ROOM: return " - four markers already, no room for ";
    default:           return "";
    }
}
