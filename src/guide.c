#include "guide.h"

#include <ctype.h>
#include <string.h>

#define GUIDE_SECTIONS_MAX 32

/* A line is a fence when, past its indent, it starts with three backticks:
 * the guide's code blocks sit indented inside list items too, and a `## `
 * line in one is not a heading. */
static int is_fence(const char *s, const char *end)
{
    while (s < end && *s == ' ') s++;
    return end - s >= 3 && !memcmp(s, "```", 3);
}

int guide_parts(GuidePart *opening, GuidePart *sections, int max)
{
    const char *g = AGENT_GUIDE, *end = AGENT_GUIDE + AGENT_GUIDE_LEN;
    int n = 0, fenced = 0;
    opening->at = g;
    opening->len = AGENT_GUIDE_LEN;
    for (const char *s = g; s < end;) {
        const char *nl = memchr(s, '\n', (size_t)(end - s));
        const char *e = nl ? nl + 1 : end;
        if (is_fence(s, e)) fenced = !fenced;
        else if (!fenced && e - s > 3 && !memcmp(s, "## ", 3)) {
            if (n == 0) opening->len = (size_t)(s - g);
            else if (n - 1 < max) sections[n - 1].len = (size_t)(s - sections[n - 1].at);
            if (n < max) { sections[n].at = s; sections[n].len = (size_t)(end - s); }
            n++;
        }
        s = e;
    }
    return n < max ? n : max;
}

/* A section's heading, without "## " and the newline. */
static size_t heading(const GuidePart *p, const char **name)
{
    *name = p->at + 3;
    const char *nl = memchr(*name, '\n', p->len - 3);
    return nl ? (size_t)(nl - *name) : p->len - 3;
}

static int holds(const char *hay, size_t hn, const char *needle)
{
    size_t nn = strlen(needle);
    for (size_t i = 0; nn && i + nn <= hn; i++) {
        size_t k = 0;
        while (k < nn && tolower((unsigned char)hay[i + k]) == tolower((unsigned char)needle[k])) k++;
        if (k == nn) return 1;
    }
    return 0;
}

/* Who reads a section, from the line under its heading, `<!-- read for: plan,
 * live -->`: the guide says it once, and the skill sends an agent to `toc`
 * rather than naming sections itself. */
#define READ_FOR "<!-- read for: "
size_t guide_read_for(const GuidePart *p, const char **who)
{
    const char *name;
    size_t      hl = heading(p, &name);
    const char *s = name + hl + 1, *end = p->at + p->len;
    size_t      rl = strlen(READ_FOR);
    if (s >= end || (size_t)(end - s) < rl || memcmp(s, READ_FOR, rl)) return 0;
    *who = s + rl;
    const char *c = memchr(*who, '\n', (size_t)(end - *who));
    if (!c || c - *who < 4 || memcmp(c - 4, " -->", 4)) return 0;
    return (size_t)(c - 4 - *who);
}

static void list(FILE *out, const GuidePart *sec, int n)
{
    for (int i = 0; i < n; i++) {
        const char *name, *who = "";
        int hl = (int)heading(&sec[i], &name);
        int wl = (int)guide_read_for(&sec[i], &who);
        fprintf(out, "  %-30.*s %-12.*s %6zu bytes   vtt --agent-guide \"%.*s\"\n", hl, name, wl, who, sec[i].len, hl, name);
    }
}

int guide_print(FILE *out, FILE *err, const char *arg)
{
    GuidePart open, sec[GUIDE_SECTIONS_MAX];
    int n = guide_parts(&open, sec, GUIDE_SECTIONS_MAX);
    if (!strcmp(arg, "toc")) {
        fwrite(open.at, 1, open.len, out);
        fprintf(out, "## Sections\n\nRead the ones marked for your work: `plan` for a map from a description or a file,\n"
                     "`live` for the GM's open session. Each under %d bytes; the whole guide, %zu, is\n"
                     "`vtt --agent-guide` alone.\n\n", GUIDE_PART_MAX, AGENT_GUIDE_LEN);
        list(out, sec, n);
        return 0;
    }
    /* A heading named whole wins; else the one heading that holds the words. */
    int hit = -1, hits = 0;
    for (int i = 0; i < n && hits != -1; i++) {
        const char *name;
        size_t hl = heading(&sec[i], &name);
        if (hl == strlen(arg) && holds(name, hl, arg)) { hit = i; hits = -1; }
        else if (holds(name, hl, arg)) { hit = i; hits++; }
    }
    if (hits == -1) hits = 1;
    if (hits == 1) { fwrite(sec[hit].at, 1, sec[hit].len, out); return 0; }
    fprintf(err, "vtt: %s section of the guide is named by \"%s\"; the sections:\n", hits ? "more than one" : "no", arg);
    list(err, sec, n);
    return 2;
}
