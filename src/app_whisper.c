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

/* The names a phone is offered: every player creature's label, once each,
 * in the map's order -- hidden or in the dark too, since the players are
 * all at the table whatever their creatures can see. Rebuilt only when the
 * map has changed or another map is up; the server sends it only when the
 * text differs. */
static void sync_offer(App *a)
{
    const Map *m = a->map;
    if (m == a->offer_map && m && m->gen == a->offer_gen) return;
    a->offer_map = m;
    a->offer_gen = m ? m->gen : 0;
    PROF_ZONE("names.sync");
    char   text[WIRE_TEXT_MAX];
    size_t len = 0;
    for (int i = 0; m && i < m->tokens.n; i++) {
        const Token *t = &m->tokens.v[i];
        if (t->kind != TOKEN_PLAYER || !t->label[0]) continue;
        char name[NET_NAME_MAX];
        if (!net_name_clean(t->label, strlen(t->label), 0, name)) continue;
        int dup = 0;
        for (int j = 0; j < i && !dup; j++) {
            const Token *o = &m->tokens.v[j];
            dup = o->kind == TOKEN_PLAYER && !strcasecmp(o->label, t->label);
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
    /* Said, not logged: a phone on a flaky network comes back often. */
    char name[NET_NAME_MAX], msg[96];
    while (net_take_arrival(&a->net, name, sizeof name)) {
        snprintf(msg, sizeof msg, "%s's phone is here", name);
        app_set_status_gm(a, msg);
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
    char name[NET_NAME_MAX], msg[WIRE_TEXT_MAX + 128];
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
    /* The whole text, for the session log; the status line shows what fits. */
    if (got) snprintf(msg, sizeof msg, "whispered to %s (%d phone%s): %s", name, got, got == 1 ? "" : "s", text);
    else     snprintf(msg, sizeof msg, "%s's phone is not here - it gets this when it comes back: %s", name, text);
    app_note_gm(a, msg);
}

/* :players -- who is watching, and who has a whisper waiting. */
void app_players_command(App *a)
{
    Net *n = &a->net;
    char who[160], msg[240];
    net_who(n, who, sizeof who);
    int off = snprintf(msg, sizeof msg, "%s", !net_active(n) ? "no one can watch - :serve lets the phones join"
                                             : who[0] ? who : "no one is watching");
    for (int k = 0, first = 1; k < NET_KEPT_MAX && n->kept[k].len && off < (int)sizeof msg; k++, first = 0)
        off += snprintf(msg + off, sizeof msg - (size_t)off, "%s%s", first ? " - a whisper waits for " : ", ",
                        n->kept[k].name);
    app_set_status_gm(a, msg);
}
