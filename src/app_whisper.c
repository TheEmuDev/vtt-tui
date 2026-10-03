/* Whispers (docs/WHISPER.md): a handout to one phone. Each phone says who it
 * is as it connects -- a player creature's label, or any name -- and
 * :whisper sends to the phones of a name, or keeps it for one asleep. The
 * server holds the names and the kept whispers (net.c); this is the GM's
 * side: the commands, the names offered, and saying who arrived. */

#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "app_priv.h"
#include "prof.h"

/* The names a phone is offered: every player creature's label the players
 * can see, once each, in the map's order. Kept in step after every tick;
 * the server sends it only when it changes. */
static void sync_offer(App *a)
{
    PROF_ZONE("names.sync");
    char   text[WIRE_TEXT_MAX];
    size_t len = 0;
    const Map *m = a->map;
    for (int i = 0; m && i < m->tokens.n; i++) {
        const Token *t = &m->tokens.v[i];
        if (t->kind != TOKEN_PLAYER || !t->label[0] || t->hidden) continue;
        char name[NET_NAME_MAX];
        if (!net_name_clean(t->label, strlen(t->label), 0, name)) continue;
        int dup = 0;
        for (int j = 0; j < i && !dup; j++) {
            const Token *o = &m->tokens.v[j];
            dup = o->kind == TOKEN_PLAYER && !o->hidden && !strcasecmp(o->label, t->label);
        }
        size_t nl = strlen(name);
        if (dup || len + nl + 1 > sizeof text) continue;
        if (len) text[len++] = '\n';
        memcpy(text + len, name, nl);
        len += nl;
    }
    net_set_offer(&a->net, text, len, a->now_ms);
}

void app_whisper_tick(App *a)
{
    if (!net_active(&a->net)) return;
    sync_offer(a);
    char name[NET_NAME_MAX], msg[96];
    while (net_take_arrival(&a->net, name, sizeof name)) {
        snprintf(msg, sizeof msg, "%s's phone is here", name);
        app_note_gm(a, msg);
    }
}

/* The name a :whisper line starts with: the longest a phone has used (here
 * now or before), case aside, followed by a space. Its spelling into out;
 * the text after it in *text. */
static int whisper_name(const Net *n, const char *line, char *out, const char **text)
{
    size_t best = 0;
    const char *found = NULL;
    for (int pass = 0; pass < 2; pass++) {
        int count = pass ? n->ncl : n->nseen;
        for (int k = 0; k < count; k++) {
            const char *name = pass ? n->cl[k].name : n->seen[k];
            size_t l = strlen(name);
            if (!l || l <= best || strncasecmp(line, name, l) || (line[l] && line[l] != ' ')) continue;
            best  = l;
            found = name;
        }
    }
    if (!found) return 0;
    str_lcpy(out, found, NET_NAME_MAX);
    *text = line + best;
    while (**text == ' ') (*text)++;
    return 1;
}

/* :whisper NAME TEXT -- to the phones of NAME, or kept for the first. */
void app_whisper_command(App *a, const char *rest)
{
    Net *n = &a->net;
    char name[NET_NAME_MAX], msg[200];
    const char *text;
    if (!*rest) { app_set_status_gm(a, ":whisper NAME TEXT - to one player's phone; :players lists who is here"); return; }
    if (!whisper_name(n, rest, name, &text)) {
        char first[NET_NAME_MAX];
        size_t l = strcspn(rest, " ");
        if (l >= sizeof first) l = sizeof first - 1;
        memcpy(first, rest, l);
        first[l] = '\0';
        snprintf(msg, sizeof msg, "no phone is %s - :players lists who is here", first);
        app_set_status_gm(a, msg);
        return;
    }
    if (!*text) {
        snprintf(msg, sizeof msg, ":whisper %s TEXT - what to tell them", name);
        app_set_status_gm(a, msg);
        return;
    }
    size_t len = strlen(text);
    if (len > WIRE_TEXT_MAX) len = WIRE_TEXT_MAX;
    int got = net_whisper(n, name, text, len, 1, a->now_ms);
    if (got) snprintf(msg, sizeof msg, "whispered to %s (%d phone%s): %.100s", name, got, got == 1 ? "" : "s", text);
    else     snprintf(msg, sizeof msg, "%s's phone is not here - it gets this when it comes back: %.80s", name, text);
    app_note_gm(a, msg);
}

/* :players -- who is watching, and who has a whisper waiting. */
void app_players_command(App *a)
{
    Net *n = &a->net;
    if (!net_active(n)) { app_set_status_gm(a, "no one can watch - :serve lets the phones join"); return; }
    char who[160], msg[240];
    net_who(n, who, sizeof who);
    int off = snprintf(msg, sizeof msg, "%s", who[0] ? who : "no one is watching");
    for (int k = 0, first = 1; k < NET_KEPT_MAX && n->kept[k].len && off < (int)sizeof msg; k++, first = 0)
        off += snprintf(msg + off, sizeof msg - (size_t)off, "%s%s", first ? " - a whisper waits for " : ", ",
                        n->kept[k].name);
    app_set_status_gm(a, msg);
}
