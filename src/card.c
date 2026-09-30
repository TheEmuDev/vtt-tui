#include "card.h"

#include <stdlib.h>
#include <string.h>

#include "store.h"
#include "util.h"

int card_name_ok(const char *name)
{
    return store_name_ok(name) && strlen(name) < CARD_NAME_MAX;
}

int card_find(const Map *m, const char *name)
{
    for (int i = 0; i < m->ncards; i++)
        if (!strcmp(m->cards[i].name, name)) return i;
    return -1;
}

char *card_clean(const char *text)
{
    /* \r\n and lone \r become \n; cut on a character's edge; no blank
     * lines at the start or the end. */
    char *t = xmalloc(CARD_TEXT_MAX);
    size_t n = 0;
    const char *p = text ? text : "";
    while (*p == '\n' || *p == '\r') p++;
    for (; *p && n + 1 < CARD_TEXT_MAX; p++) {
        if (*p == '\r') { if (p[1] != '\n') t[n++] = '\n'; continue; }
        t[n++] = *p;
    }
    /* Cut inside a character: the next byte is one of its continuations, so
     * the ones already copied go, and its first byte with them. */
    if (((unsigned char)*p & 0xC0) == 0x80) {
        while (n > 0 && ((unsigned char)t[n - 1] & 0xC0) == 0x80) n--;
        if (n > 0) n--;
    }
    while (n > 0 && (t[n - 1] == '\n' || t[n - 1] == ' ')) n--;
    t[n] = '\0';
    return t;
}

int card_set(Map *m, const char *name, const char *text)
{
    if (!card_name_ok(name)) return -1;
    int i = card_find(m, name);
    if (i < 0) {
        if (m->ncards >= MAP_CARDS_MAX) return -1;
        i = m->ncards++;
        memset(&m->cards[i], 0, sizeof m->cards[i]);
        str_lcpy(m->cards[i].name, name, sizeof m->cards[i].name);
    }
    free(m->cards[i].text);
    m->cards[i].text = card_clean(text);
    return i;
}

const char *card_of(const Map *m, const Token *t)
{
    if (!t->card[0]) return NULL;
    int i = card_find(m, t->card);
    return i >= 0 ? m->cards[i].text : NULL;
}
