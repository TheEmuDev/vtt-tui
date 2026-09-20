# Ideas set aside

Things worth building that were consciously not built, with the reason, so
the reason can be re-examined rather than the idea re-invented. Add to the
top; move an entry to the README when it ships.

## A Fear pool for Daggerheart

*Set aside 2026-09-20.* The GM's Fear is a number that changes every few
minutes of a Daggerheart session, and the tool already knows when a duality
roll lands with Fear. The natural shape: a counter in the side panel under
the daggerheart ruleset, bumped by one when a `:roll` under that ruleset
comes up Fear, adjustable by hand (`:fear 4`, `:fear +1`), saved with the
map, and stepped back by undo like the round counter is. Cap it at twelve,
the rule's ceiling, and light the row when it is full.

Why not now: the table this tool serves runs Fear on a physical tracker in
the middle of the table, which everyone can see and which the GM can move
without looking at a screen. In-person play is what the tool augments, and
the tracker already does this job better than a panel row would. It becomes
worth doing when a session is run with players who cannot see the table --
the remote view's later phases -- or when the roll-driven bump saves enough
reaching for the tracker to matter.

Where it would live: `Map.fear` beside `Map.round`; `OP_FEAR` in the undo
log; a `fear` line in the file, written only when non-zero; a `fear` flag
on the `Ruleset` table so no other game grows one; the panel row under the
spotlight block; a `:fear` command rather than a key, since the roll does
the frequent part.
