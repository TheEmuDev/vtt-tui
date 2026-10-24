/* The agents' guide, docs/AGENTS.md, as built into the binary (src/agentguide.c,
 * written by tools/embed.sh), and its parts: `vtt --agent-guide [toc|SECTION]`.
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

/* `vtt --agent-guide ARG`: "toc" prints the opening and the sections with their
 * sizes; anything else prints the one section whose heading holds it (any
 * case). 0 printed; 2 no section or more than one, said on err. */
int guide_print(FILE *out, FILE *err, const char *arg);

#endif
