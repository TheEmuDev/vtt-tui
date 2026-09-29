/* Handouts (docs/HANDOUTS.md): a short text card on the players' screens --
 * an inscription, a letter -- put up and taken down by the GM. Prepared ones
 * are plain text files in the handouts directory; a one-liner can be said on
 * the spot. The phones draw the card themselves from the server's 'H' record;
 * the terminal mirror and :player preview draw it with ui_handout_draw. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_priv.h"
#include "store.h"

/* $XDG_DATA_HOME/vtt/handouts, else ~/.local/share/vtt/handouts. */
static void handout_dir(char *buf, size_t sz)
{
    store_dir("handouts", buf, sz);
}

/* Reads NAME.txt into body: UTF-8, at most HANDOUT_BODY_MAX-1 bytes, line
 * endings made \n, tabs made spaces, trailing blank lines dropped. 0, or -1
 * with why in err. */
static int read_handout(const char *name, char *body, size_t bodysz, char *err, size_t errsz)
{
    char path[MAP_PATH_MAX + 80];
    if (!store_name_ok(name)) { snprintf(err, errsz, "no handout called %.40s", name); return -1; }
    store_path("handouts", name, ".txt", path, sizeof path);    /* a checked name always fits */
    FILE *f = fopen(path, "rb");
    if (!f) { snprintf(err, errsz, "no handout called %.40s - it would be %.80s", name, path); return -1; }
    size_t n = fread(body, 1, bodysz, f);
    fclose(f);
    if (n >= bodysz) { snprintf(err, errsz, "%.40s is over %d bytes - a handout is a card, not a book", name, (int)bodysz - 1); return -1; }
    if (!utf8_valid(body, n)) { snprintf(err, errsz, "%.40s is not UTF-8 text", name); return -1; }
    size_t k = 0;
    for (size_t i = 0; i < n; i++) {
        if (body[i] == '\r') continue;
        body[k++] = body[i] == '\t' ? ' ' : body[i];
    }
    while (k && (body[k - 1] == '\n' || body[k - 1] == ' ')) k--;
    body[k] = '\0';
    if (!k) { snprintf(err, errsz, "%.40s is empty", name); return -1; }
    return 0;
}

/* Sends the kept handout up, or takes it down. */
static void publish(App *a, int up)
{
    a->handout_up = up;
    a->dirty = 1;
    if (!up) { net_set_handout(&a->net, "", 0, a->now_ms); return; }
    char   rec[WIRE_HANDOUT_MAX];
    size_t tl = strlen(a->handout_title), bl = strlen(a->handout_body);
    if (tl + 1 + bl > sizeof rec) bl = sizeof rec - tl - 1;
    memcpy(rec, a->handout_title, tl);
    rec[tl] = '\n';
    memcpy(rec + tl + 1, a->handout_body, bl);
    net_set_handout(&a->net, rec, tl + 1 + bl, a->now_ms);
}

void app_handout_show(App *a, const char *name)
{
    char err[200], msg[200];
    char *body = xmalloc(HANDOUT_BODY_MAX);
    if (read_handout(name, body, HANDOUT_BODY_MAX, err, sizeof err) < 0) {
        free(body);
        app_set_status_gm(a, err);
        return;
    }
    str_lcpy(a->handout_title, name, sizeof a->handout_title);
    str_lcpy(a->handout_body, body, sizeof a->handout_body);
    free(body);
    publish(a, 1);
    snprintf(msg, sizeof msg, "handout up: %.40s - :handout off takes it down", name);
    app_note_gm(a, msg);
}

/* The picker's rows: each handout's name and its first line. */
UiPickItem *app_handout_items(int *count)
{
    char dir[MAP_PATH_MAX];
    handout_dir(dir, sizeof dir);
    int n;
    char (*names)[MAP_NAME_MAX] = store_list_all(dir, ".txt", &n);
    *count = 0;
    if (n <= 0) return NULL;
    UiPickItem *items = xmalloc(sizeof *items * (size_t)(n > 0 ? n : 1));
    char *body = xmalloc(HANDOUT_BODY_MAX), err[200];
    for (int i = 0; i < n; i++) {
        str_lcpy(items[i].name, names[i], sizeof items[i].name);
        if (read_handout(names[i], body, HANDOUT_BODY_MAX, err, sizeof err) < 0) {
            str_lcpy(items[i].detail, "cannot be read", sizeof items[i].detail);
            continue;
        }
        char *nl = strchr(body, '\n');
        if (nl) *nl = '\0';
        str_lcpy(items[i].detail, body, sizeof items[i].detail);
    }
    free(body);
    free(names);
    *count = n;
    return items;
}

/* :handout              the picker
 * :handout NAME         put NAME.txt up
 * :handout say TEXT     put a line up, said on the spot
 * :handout off, on      take it down; put the last one back up */
void app_handout_command(App *a, const char *rest)
{
    char msg[200];
    if (!*rest) { app_pick_open(a, PICK_HANDOUT, -1, ""); return; }
    if (!strcmp(rest, "off")) {
        if (!a->handout_up) { app_set_status_gm(a, "no handout is up"); return; }
        publish(a, 0);
        app_note_gm(a, "handout down - :handout on puts it back up");
        return;
    }
    if (!strcmp(rest, "on")) {
        if (!a->handout_body[0]) { app_set_status_gm(a, "no handout yet - :handout NAME, or :handout say TEXT"); return; }
        publish(a, 1);
        snprintf(msg, sizeof msg, "handout up again: %.40s", a->handout_title[0] ? a->handout_title : a->handout_body);
        app_note_gm(a, msg);
        return;
    }
    if (!strncmp(rest, "say", 3) && (rest[3] == ' ' || !rest[3])) {
        const char *text = rest + 3;
        while (*text == ' ') text++;
        if (!*text) { app_set_status_gm(a, ":handout say TEXT puts a line up on the players' screens"); return; }
        a->handout_title[0] = '\0';
        str_lcpy(a->handout_body, text, sizeof a->handout_body);
        publish(a, 1);
        snprintf(msg, sizeof msg, "handout up: \"%.60s\" - :handout off takes it down", text);
        app_note_gm(a, msg);
        return;
    }
    app_handout_show(a, rest);
}
