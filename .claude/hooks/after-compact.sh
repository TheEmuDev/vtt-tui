#!/bin/sh
# SessionStart hook, matcher "compact": what a summary loses. Its output goes into the
# session's context, so every part is capped. CLAUDE.md, *Read on demand*, says what to
# read next.
cd "${CLAUDE_PROJECT_DIR:-.}" 2>/dev/null || exit 0
git rev-parse --is-inside-work-tree >/dev/null 2>&1 || exit 0
mem="$HOME/.claude/projects/$(pwd | sed 's/[^A-Za-z0-9]/-/g')/memory/MEMORY.md"
st=$(git status --short)

echo "After compaction -- the state the summary may have lost:"
echo
echo "Last commits:"
git log --oneline -5
echo
echo "Uncommitted (git status --short):"
if [ -z "$st" ]; then
    echo "(clean)"
else
    echo "$st" | head -30
    n=$(echo "$st" | wc -l)
    [ "$n" -gt 30 ] && echo "... and $((n - 30)) more"
fi
if [ -r "$mem" ]; then
    echo
    echo "In flight (memory):"
    awk '/^## /{on = /^## In flight/; next} on && NF' "$mem" | head -10
fi
echo
echo "Before going on: read the plan's Progress section named above, and the row of"
echo "docs/CODEMAP.md for each file about to be touched."
exit 0
