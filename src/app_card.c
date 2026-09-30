/* Cards in play (docs/CARDS.md): s k writes a creature's card in the GM's
 * own editor, as git opens a commit message -- the card, or a new one's
 * skeleton, with help lines that are left out again. The table and the
 * file are card.c's and mapio.c's; the box is app_draw.c's. */

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "app_priv.h"
#include "card.h"
#include "character.h"

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
        /* Through the shell, so an editor named with its flags ("code -w")
         * works as it does for git. */
        execl("/bin/sh", "sh", "-c", "exec ${VISUAL:-${EDITOR:-vi}} \"$1\"", "sh", path, (char *)NULL);
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

/* The file read back without its # lines, trimmed. NULL when unreadable. */
static char *read_back(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    size_t cap = 2 * CARD_TEXT_MAX, n = 0;
    char  *out = xmalloc(cap), line[1024];
    int    at_start = 1;
    while (fgets(line, sizeof line, f)) {
        size_t len = strlen(line);
        int    whole = len && line[len - 1] == '\n';
        if (at_start && line[0] == '#') {
            /* A help line, the rest of it too however long. */
            while (!whole && fgets(line, sizeof line, f)) { len = strlen(line); whole = len && line[len - 1] == '\n'; }
            continue;
        }
        if (n + len + 1 < cap) { memcpy(out + n, line, len); n += len; }
        at_start = whole;
    }
    fclose(f);
    out[n] = '\0';
    return out;
}

void app_card_edit(App *a)
{
    Map  *m  = a->map;
    Play *pl = &a->play;
    int i = pl->sel >= 0 && pl->sel < m->tokens.n ? pl->sel : app_token_under_cursor(a);
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

    char path[256];
    const char *tmp = getenv("TMPDIR");
    snprintf(path, sizeof path, "%s/vtt-card-XXXXXX", tmp && tmp[0] ? tmp : "/tmp");
    int fd = mkstemp(path);
    FILE *f = fd >= 0 ? fdopen(fd, "w") : NULL;
    if (!f) {
        if (fd >= 0) { close(fd); unlink(path); }
        app_set_status(a, "cannot write a file for the editor");
        return;
    }
    /* What the editor opens on: the card, or a new one's start. */
    char *start = xmalloc(CARD_TEXT_MAX);
    if (old) str_lcpy(start, old, CARD_TEXT_MAX);
    else {
        const Ruleset *rs = ruleset_by_name(m->ruleset);
        if (rs && rs->card_skeleton) snprintf(start, CARD_TEXT_MAX, rs->card_skeleton, root[0] ? root : name);
        else                         snprintf(start, CARD_TEXT_MAX, "%s\n", root[0] ? root : name);
    }
    fputs(start, f);
    fprintf(f, "\n# The card \"%s\", shown beside the map while a creature using it is selected,\n"
               "# to the GM only. Every creature using it shares it.\n"
               "# Lines starting with # are left out. **word** shows bold.\n"
               "# Save and quit to keep it; quit without saving, or empty it, to change nothing.\n", name);
    fclose(f);

    int rc = run_editor(a, path);
    char *text = rc == 0 ? read_back(path) : NULL;
    unlink(path);
    if (rc != 0) {
        app_set_status(a, rc == 127 ? "no editor to run - set $EDITOR" : "the editor did not finish - the card is as it was");
        free(text);
        free(start);
        return;
    }

    /* Nothing to keep, or nothing new: as it was. Compared the way it will
     * be stored. */
    char *now   = card_clean(text);
    char *begun = card_clean(start);
    free(start);
    char msg[160];
    if (!now[0]) {
        app_set_status(a, "an empty card changes nothing - the card is as it was");
    } else if (!strcmp(begun, now)) {
        /* Untouched: a new card's start is not a card yet. */
        app_set_status(a, old ? "card unchanged" : "no card written - the start was left as it was");
    } else if (card_set(m, name, now) < 0) {
        app_set_status(a, "a map holds 64 cards - this one is not kept");
    } else {
        /* A new card goes to the creature, and to the others of its name
         * that have none: Goblin 2 and Goblin 3 want Goblin's. */
        int more = 0;
        if (!t->card[0]) {
            str_lcpy(t->card, name, sizeof t->card);
            for (int j = 0; j < m->tokens.n; j++) {
                Token *o = &m->tokens.v[j];
                char r2[TOKEN_LABEL_MAX];
                if (j == i || o->card[0]) continue;
                token_label_root(o->label, r2, sizeof r2);
                if (root[0] && !strcmp(r2, root)) { str_lcpy(o->card, name, sizeof o->card); more++; }
            }
        }
        map_touch(m);
        if (more) snprintf(msg, sizeof msg, "card %s written - and given to %d more %.30s", name, more, root);
        else      snprintf(msg, sizeof msg, "card %s %s", name, old ? "saved" : "written");
        app_set_status_gm(a, msg);
    }
    free(now);
    free(begun);
    free(text);
}
