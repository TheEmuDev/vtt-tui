/* The agents' guide, docs/AGENTS.md, as built into the binary (src/agentguide.c,
 * written by tools/embed.sh), and its topics: `vtt --agent-guide [TOPIC|VERB]`.
 * A section is a `## ` heading outside a code fence, to the next one. */
#ifndef VTT_GUIDE_H
#define VTT_GUIDE_H

#include <stddef.h>
#include <stdio.h>

extern const char   AGENT_GUIDE[];
extern const size_t AGENT_GUIDE_LEN;

/* The most one part may hold: Claude Code's Bash tool passed 30,000 bytes of a
 * command's output whole and turned 32,000 into a 2 KB preview (probed
 * 2026-10-23, docs/AGENTLOOP.md, *The guide's size*). Suite agentguide holds
 * every section and the toc under it. */
#define GUIDE_PART_MAX 30000

typedef struct { const char *at; size_t len; } GuidePart;

/* The opening (the title to the first section) and the sections, in order, as
 * spans of AGENT_GUIDE; returns how many sections, at most max. */
int guide_parts(GuidePart *opening, GuidePart *sections, int max);

/* Who a section is for, from the `<!-- read for: plan, live -->` line under its
 * heading: its length, *who set to it; 0 when the section has none. */
size_t guide_read_for(const GuidePart *section, const char **who);

/* A topic of the guide -- core, plan, live, events, limits, file, all -- the
 * sections whose read-for line names it, the `<!-- requests -->` line
 * replaced by REQUESTS[]'s rows for it (request_reference). 0, or -1 when
 * there is no such topic. The Claude API agent's prompt is `live`. */
int guide_compose(FILE *out, const char *topic);

/* `vtt --agent-guide [ARG]`: no ARG the core and the topics with their sizes;
 * a topic; else a request, as `help VERB` says it. 0 printed; 2 nothing by
 * that name, said on err with the list. */
int guide_print(FILE *out, FILE *err, const char *arg);

#endif
