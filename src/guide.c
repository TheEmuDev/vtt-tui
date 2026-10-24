#include "guide.h"

#include <stdlib.h>
#include <string.h>

#include "request.h"

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

/* Who reads a section, from the line under its heading, `<!-- read for: plan,
 * live -->`: the guide says it once, and the skill names no section. */
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

/* Does the section's read-for line name this topic, as a word? */
static int for_topic(const GuidePart *p, const char *topic)
{
    const char *who;
    size_t wl = guide_read_for(p, &who), tl = strlen(topic);
    for (size_t i = 0; i + tl <= wl; i++)
        if (!memcmp(who + i, topic, tl) && (i == 0 || who[i - 1] == ' ') &&
            (i + tl == wl || who[i + tl] == ','))
            return 1;
    return 0;
}

/* A section as an agent reads it: without its read-for line, and with the
 * `<!-- requests -->` line replaced by the table's rows for the topic. */
#define REQUESTS_MARK "<!-- requests -->"
static void put_section(FILE *out, const GuidePart *p, unsigned rows)
{
    for (const char *s = p->at, *end = p->at + p->len; s < end;) {
        const char *nl = memchr(s, '\n', (size_t)(end - s));
        const char *e = nl ? nl + 1 : end;
        size_t      l = (size_t)(e - s) - (nl != NULL);
        if (l > strlen(READ_FOR) && !memcmp(s, READ_FOR, strlen(READ_FOR))) { }
        else if (l == strlen(REQUESTS_MARK) && !memcmp(s, REQUESTS_MARK, l)) request_reference(out, rows);
        else fwrite(s, 1, (size_t)(e - s), out);
        s = e;
    }
}

/* The topics, each the sections that name it; the core is the opening and
 * its own sections, then this list. */
static const struct { const char *name; unsigned rows; const char *what; } TOPICS[] = {
    { "plan",   CTL_RQ_PLAN,    "a map from a description, or changes to a file" },
    { "live",   CTL_RQ_FORLIVE, "the GM's open session: jobs, proposals, the verdict" },
    { "events", 0,              "what `wait` answers, each event" },
    { "limits", 0,              "the sizes a request may not pass" },
    { "file",   0,              "the map's file, written by hand: a last resort" },
};
#define NTOPICS ((int)(sizeof TOPICS / sizeof *TOPICS))

int guide_compose(FILE *out, const char *topic)
{
    GuidePart open, sec[GUIDE_SECTIONS_MAX];
    int n = guide_parts(&open, sec, GUIDE_SECTIONS_MAX);
    int all = !strcmp(topic, "all");
    unsigned rows = all ? CTL_RQ_PLAN | CTL_RQ_FORLIVE : 0;
    int known = all || !strcmp(topic, "core");
    for (int t = 0; t < NTOPICS; t++)
        if (!strcmp(topic, TOPICS[t].name)) { rows = TOPICS[t].rows; known = 1; }
    if (!known) return -1;
    if (all || !strcmp(topic, "core")) fwrite(open.at, 1, open.len, out);
    for (int i = 0; i < n; i++)
        if (all || for_topic(&sec[i], topic)) put_section(out, &sec[i], rows);
    return 0;
}

static size_t topic_size(const char *topic)
{
    char  *buf = NULL;
    size_t len = 0;
    FILE  *f   = open_memstream(&buf, &len);
    if (!f) return 0;
    guide_compose(f, topic);
    fclose(f);
    free(buf);
    return len;
}

static void topic_list(FILE *out)
{
    fputs("**The topics** (`vtt --agent-guide TOPIC`, each in one call):\n\n", out);
    for (int t = 0; t < NTOPICS; t++)
        fprintf(out, "- `%s` (%zu bytes): %s\n", TOPICS[t].name, topic_size(TOPICS[t].name), TOPICS[t].what);
    fputs("- a request's name (`room`, `corridor`, `token`, ...): that request alone\n", out);
    fprintf(out, "- `all` (%zu bytes): everything, for a reader with no limit on what one command may print\n",
            topic_size("all"));
}

int guide_print(FILE *out, FILE *err, const char *arg)
{
    if (!arg || !strcmp(arg, "core") || !strcmp(arg, "toc")) {
        guide_compose(out, "core");
        topic_list(out);
        return 0;
    }
    if (guide_compose(out, arg) == 0) return 0;
    if (request_help_verb(out, arg) == 0) return 0;
    fprintf(err, "vtt: the guide has no topic or request called \"%s\"\n", arg);
    topic_list(err);
    return 2;
}
