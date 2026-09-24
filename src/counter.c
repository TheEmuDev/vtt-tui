#include "counter.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "util.h"

int counter_find(const Token *t, const char *name)
{
    for (int i = 0; i < t->ncounters; i++)
        if (!strcasecmp(t->counters[i].name, name)) return i;
    return -1;
}

static int name_ok(const char *name)
{
    size_t n = strlen(name);
    if (n == 0 || n >= COUNTER_NAME_MAX || !isalpha((unsigned char)name[0])) return 0;
    for (size_t i = 0; i < n; i++)
        if (!isalnum((unsigned char)name[i])) return 0;
    return 1;
}

int counter_set(Token *t, const char *name, int value, int max)
{
    if (!name_ok(name)) return -2;
    int i = counter_find(t, name);
    if (i < 0) {
        if (t->ncounters >= TOKEN_COUNTER_MAX) return -1;
        i = t->ncounters++;
        memset(&t->counters[i], 0, sizeof t->counters[i]);
        str_lcpy(t->counters[i].name, name, sizeof t->counters[i].name);
    }
    max = iclamp(max, 1, COUNTER_VALUE_MAX);
    t->counters[i].max   = (int16_t)max;
    t->counters[i].value = (int16_t)iclamp(value, 0, max);
    return i;
}

void counter_remove(Token *t, int idx)
{
    if (idx < 0 || idx >= t->ncounters) return;
    memmove(&t->counters[idx], &t->counters[idx + 1],
            (size_t)(t->ncounters - 1 - idx) * sizeof(Counter));
    t->ncounters--;
    memset(&t->counters[t->ncounters], 0, sizeof(Counter));
}

void counter_format(const Token *t, char *buf, size_t bufsz)
{
    if (bufsz) buf[0] = '\0';
    size_t off = 0;
    for (int i = 0; i < t->ncounters && off + 1 < bufsz; i++) {
        int n = snprintf(buf + off, bufsz - off, "%s%s %d/%d", i ? "  " : "",
                         t->counters[i].name, t->counters[i].value, t->counters[i].max);
        if (n < 0) break;
        off += (size_t)n;
    }
}

void counter_default(const char *names, char *buf, size_t bufsz)
{
    size_t n = 0;
    if (names) {
        while (*names == ' ') names++;
        while (names[n] && names[n] != ' ') n++;
    }
    if (n == 0 || n >= bufsz) { str_lcpy(buf, "HP", bufsz); return; }
    memcpy(buf, names, n);
    buf[n] = '\0';
}

/* The ruleset's spelling of `name`, when it has one. */
static void canonical(const char *names, const char *name, char *out, size_t outsz)
{
    str_lcpy(out, name, outsz);
    if (!names) return;
    size_t len = strlen(name);
    for (const char *p = names; *p; ) {
        while (*p == ' ') p++;
        size_t n = 0;
        while (p[n] && p[n] != ' ') n++;
        if (n == len && n < outsz && !strncasecmp(p, name, n)) {
            memcpy(out, p, n);
            out[n] = '\0';
            return;
        }
        p += n;
    }
}

static int parse_int(const char *s, const char **end, long *out)
{
    char *e;
    long v = strtol(s, &e, 10);
    if (e == s) return 0;
    *out = v;
    *end = e;
    return 1;
}

int counter_apply(Token *t, const char *text, const char *names,
                  char *current, size_t cursz, char *msg, size_t msgsz)
{
    char buf[256];
    str_lcpy(buf, text, sizeof buf);
    msg[0] = '\0';
    int changed = 0;

    for (char *clause = strtok(buf, ",;"); clause; clause = strtok(NULL, ",;")) {
        while (*clause == ' ') clause++;
        size_t cl = strlen(clause);
        while (cl && clause[cl - 1] == ' ') clause[--cl] = '\0';
        if (!*clause) continue;

        int removing = clause[0] == '-' && isalpha((unsigned char)clause[1]);
        const char *p = clause + removing;

        char raw[32];
        size_t n = 0;
        while (isalnum((unsigned char)p[n]) && n + 1 < sizeof raw) { raw[n] = p[n]; n++; }
        raw[n] = '\0';
        if (!n || !isalpha((unsigned char)raw[0])) {
            snprintf(msg, msgsz, "\"%.20s\" - a counter starts with its name: hp -2", clause);
            return -1;
        }
        if (n >= COUNTER_NAME_MAX) {
            snprintf(msg, msgsz, "a counter's name is at most %d letters", COUNTER_NAME_MAX - 1);
            return -1;
        }
        char name[COUNTER_NAME_MAX];
        canonical(names, raw, name, sizeof name);
        int  at = counter_find(t, name);
        if (at >= 0) str_lcpy(name, t->counters[at].name, sizeof name);

        const char *r = p + n;
        while (*r == ' ') r++;

        if (removing) {
            if (*r) { snprintf(msg, msgsz, "-%s takes it off; nothing follows it", name); return -1; }
            if (at < 0) { snprintf(msg, msgsz, "there is no %s to take off", name); return -1; }
            counter_remove(t, at);
            changed = 1;
            continue;
        }

        str_lcpy(current, name, cursz);
        if (!*r) continue;                              /* just naming it */

        long a, b;
        const char *e;
        if (*r == '+' || *r == '-') {
            if (!parse_int(r, &e, &a) || *e) { snprintf(msg, msgsz, "%s %.12s - a step is +2 or -2", name, r); return -1; }
            /* Bounded before the add: nothing past the widest counter means
             * anything, and an unbounded long would overflow the int sum. */
            if (a >  COUNTER_VALUE_MAX) a =  COUNTER_VALUE_MAX;
            if (a < -COUNTER_VALUE_MAX) a = -COUNTER_VALUE_MAX;
            if (at < 0) { snprintf(msg, msgsz, "no %s yet - %s 6 sets one", name, name); return -1; }
            counter_set(t, name, t->counters[at].value + (int)a, t->counters[at].max);
            changed = 1;
            continue;
        }
        if (!parse_int(r, &e, &a) || a < 0 || a > COUNTER_VALUE_MAX) {
            snprintf(msg, msgsz, "%s %.12s - a value is 0 to %d", name, r, COUNTER_VALUE_MAX);
            return -1;
        }
        int max;
        if (*e == '/') {
            const char *e2;
            if (!parse_int(e + 1, &e2, &b) || *e2 || b < 1 || b > COUNTER_VALUE_MAX) {
                snprintf(msg, msgsz, "%s %.12s - the maximum is 1 to %d", name, r, COUNTER_VALUE_MAX);
                return -1;
            }
            max = (int)b;
        } else if (!*e) {
            if (at < 0 && a < 1) { snprintf(msg, msgsz, "a new %s needs its maximum: %s 0/6", name, name); return -1; }
            max = at >= 0 ? t->counters[at].max : (int)a;
        } else {
            snprintf(msg, msgsz, "%s %.12s - try %s 6, %s 4/6 or %s -2", name, r, name, name, name);
            return -1;
        }
        int got = counter_set(t, name, (int)a, max);
        if (got == -1) { snprintf(msg, msgsz, "a creature holds at most %d counters", TOKEN_COUNTER_MAX); return -1; }
        changed = 1;
    }

    if (changed) counter_format(t, msg, msgsz);
    if (!msg[0]) snprintf(msg, msgsz, changed ? "no counters" : "%s is the counter < and > step", current);
    return 0;
}
