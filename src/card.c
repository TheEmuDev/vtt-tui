#include "card.h"

#include <stdlib.h>
#include <string.h>
#include <strings.h>

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
    /* One pass: line ends made \n; bytes that are not UTF-8 made U+FFFD, so
     * what is kept always writes and reads back; control characters dropped,
     * tabs made spaces; spaces at a line's end dropped (an editor may trim
     * them, and a card should not change for it); whole characters only up
     * to the limit; no blank lines at the start or the end. */
    char *t = xmalloc(CARD_TEXT_MAX);
    size_t n = 0, line = 0;
    const char *p = text ? text : "", *end = p + strlen(p);
    while (p < end && (*p == '\n' || *p == '\r')) p++;
    while (p < end) {
        if (*p == '\n' || *p == '\r') {
            if (*p == '\r' && p + 1 < end && p[1] == '\n') p++;
            p++;
            while (n > line && t[n - 1] == ' ') n--;
            if (n + 2 > CARD_TEXT_MAX) break;
            t[n++] = '\n';
            line = n;
            continue;
        }
        uint32_t cp;
        int k = utf8_decode(p, (size_t)(end - p), &cp);
        char enc[4];
        int  m = 0;
        if (cp == '\t') enc[m++] = ' ';
        else if (cp < 0x20 || cp == 0x7F) m = 0;                      /* dropped */
        else if (cp == 0xFFFD && !(k == 3 && !memcmp(p, "\xef\xbf\xbd", 3))) m = utf8_encode(0xFFFD, enc);
        else { memcpy(enc, p, (size_t)k); m = k; }
        if (n + (size_t)m + 1 > CARD_TEXT_MAX) break;
        memcpy(t + n, enc, (size_t)m);
        n += (size_t)m;
        p += k;
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

void card_first_line(const char *text, char *buf, size_t sz)
{
    size_t n = 0;
    const char *p = text ? text : "";
    for (; *p && *p != '\n' && n + 1 < sz; p++) {
        if (p[0] == '*' && p[1] == '*') { p++; continue; }
        buf[n++] = *p;
    }
    buf[n] = *p;                             /* the next byte, for the cut to see */
    n = utf8_cut(buf, n);
    buf[n] = '\0';
}


/* Does a label start here: the start of a line or after a space, and not
 * the end of a longer word ("Thresholds:" is not in "MyThresholds:")? */
static int label_at(const char *text, const char *p, const char *label, size_t n)
{
    if (p > text && p[-1] != ' ' && p[-1] != '\n') return 0;
    return !strncasecmp(p, label, n) && p[n] == ':';
}

int card_value(const char *text, const char *label, char *buf, size_t sz)
{
    size_t n = strlen(label);
    buf[0] = '\0';
    if (!text || !n || !sz) return 0;
    for (const char *p = text; *p; p++) {
        if (!label_at(text, p, label, n)) continue;
        p += n + 1;
        while (*p == ' ') p++;
        /* Up to the line's end or a run of two spaces, which is how a card
         * puts several on a line: "Thresholds: 8/15   HP: 3". */
        size_t k = 0;
        while (p[k] && p[k] != '\n' && !(p[k] == ' ' && p[k + 1] == ' ')) k++;
        while (k && p[k - 1] == ' ') k--;
        if (k >= sz) k = utf8_cut(p, sz - 1);
        memcpy(buf, p, k);
        buf[k] = '\0';
        return 1;
    }
    return 0;
}

int card_feature(const char *text, const char *name, char *buf, size_t sz)
{
    size_t n = strlen(name);
    buf[0] = '\0';
    if (!text || !n || !sz) return 0;
    for (const char *line = text; *line; ) {
        const char *p = line;
        while (*p == '*' || *p == '_' || *p == ' ') p++;     /* "**Minion (3)**" as typed */
        if (!strncasecmp(p, name, n) && p[n] == ' ' && p[n + 1] == '(') {
            p += n + 2;
            const char *close = p;
            while (*close && *close != ')' && *close != '\n') close++;
            if (*close == ')') {
                size_t k = (size_t)(close - p);
                if (k >= sz) k = utf8_cut(p, sz - 1);
                memcpy(buf, p, k);
                buf[k] = '\0';
                return 1;
            }
        }
        const char *nl = strchr(line, '\n');
        if (!nl) break;
        line = nl + 1;
    }
    return 0;
}
