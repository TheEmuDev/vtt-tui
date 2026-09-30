# Cards

A creature's card: its stat block and whatever else the GM writes, shown beside the map
while it is selected, never to the players. Character templates carry theirs, and the SRD's
adversaries can be imported as templates with cards. Roadmap items 6 and 9, signed off
2026-09-29. The README is the user's reference; this page is why it is the way it is.

## Decisions

| question | answer |
|---|---|
| where a card lives | on the map, in a table of cards by name, as named rolls are; a creature names its card (every Goblin names `goblin`), so editing one edits it for them all. A creature is a fixed-size record copied through the undo log, and a card is kilobytes |
| its form | plain text. The core knows no fields; the Daggerheart ruleset reads the lines it needs by their label (`Type:`, `Difficulty:`, `Thresholds: 8/15`) for `:dmg` and Battle Points later. `**bold**` is drawn bold, other markdown dropped |
| editing | `s k` opens the card in `$EDITOR`, as git opens a commit message: pre-filled with the card, or for a creature without one a skeleton (the stat block's labels under Daggerheart), and `#` help lines at the end that are stripped on save. vtt pauses meanwhile; the phones keep the last frame. Not in the undo history -- the editor has its own |
| showing | a box over the map's right edge for the selected creature, GM-only, cut with "..." when long; `:card` shows it whole, scrollable; `:card off` and `:card on` hide and show the box (a setting) |
| templates | `:character save` saves the card with the creature; placing copies it into the map when the map has no card of that name, as rolls are |
| the import | `vtt --import-adversaries FILE`: the SRD's adversary list as JSON (the GM downloads it; vtt ships no SRD data, whose license asks for attribution) becomes character templates -- an enemy, 1x1, HP and Stress full, a card laid out as the stat block, no rolls (the dice are physical and the card shows attack and damage). Existing templates are kept unless `--force` |
| JSON | read by hand, no dependencies; item 8's generator import reuses it |
| the file | version 13: `card "NAME"` then the text's lines each as `\| text`, then `endcard`; `tokencard "NAME"` on a creature. A map with no cards stays at its version |
| the channel | `characters` gives each template's card's first line; `token add ... from NAME` brings the card |

## Build order

1. Cards in the map: the table, `tokencard`, the file, the loader's diagnostics, tests (the
   fuzzer's round trip included).
2. The box: wrapping, bold, GM-only; `:card on|off`; `:card` whole.
3. `s k` with `$EDITOR`, pre-filled.
4. Templates carry cards.
5. The JSON reader; `--import-adversaries`.
6. README, the `?` page, CONTROL.md; a perf row; the review.
