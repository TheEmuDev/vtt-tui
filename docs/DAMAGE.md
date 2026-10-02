# Damage

`:dmg 11` marks a creature's HP by its card's thresholds and says why. Roadmap item 10.
Signed off 2026-10-02 with the recommendations (list Minions only, massive damage saved with
the map, the group effect included). The README is the user's reference; this page is why it
is the way it is.

The rules it follows are the SRD's (2025-09-09), "Hit Points & Damage Thresholds",
"Resistance, Immunity, and Direct Damage", "Multi-target Attack Rolls", "Multiple Damage
Sources", "Rounding Up" and the adversary passives Minion (X) and Horde (X).

## Decisions

| question | answer |
|---|---|
| the command | `:dmg N` on the selected creature, else the one under the cursor (`app_target_token`). `N` may be a sum, `:dmg 6+4`, since damage from several sources is totaled before the thresholds. The dice stay physical: vtt never rolls it |
| the rule | under Daggerheart, from the card's `Thresholds: Major/Severe` line: below Major 1 HP, at Major 2, at Severe 3, at twice Severe 4 when the table plays massive damage; 0 or less marks none. Without a ruleset that has thresholds, `:dmg N` takes N off HP, which is what most other games mean |
| HP | the creature's HP counter, as `s v`, `<` and `>` keep it: the value is what is left, so marking takes it down. At 0 the creature is defeated: vtt says so and does nothing else to it; the GM removes it or leaves the body |
| what it says | GM-only, in the status line and the log: `Goblin: 11 is Major (8/15) - 2 HP marked, 3/5 left`. Nothing about a creature reaches the players' screens; only the usage hint, which names nothing, is plain status, as `:card`'s is |
| resistance | `:dmg 11 half`: halved before the thresholds, rounded up (the SRD rounds up unless told otherwise). Immunity needs no word: the GM does not type `:dmg` |
| massive damage | `:dmg massive on` and `:dmg massive off`, saved with the map (a `rule massive` line), since a table rule that quietly reverted next session would mark the wrong HP |
| Minion (X) | a card line starting `Minion (X)` (the import writes the SRD's that way). Any damage defeats it; `X` or more also defeats `floor(N/X)` more Minions within the attack's range. The attack's range is the attacker's and the GM judges it, so vtt lists the candidates -- creatures on the target's side whose cards say Minion, still up, nearest the target first, with their band from it -- and marks none of them |
| Horde (X) | a card line starting `Horde (X)`. When a hit takes it to half or more of its HP marked, the line says its standard attack now deals X; the card box's title says it too while it lasts |
| several at once | while the `g e` group effect is up, `:dmg N` hits every creature with a square inside it, each against its own thresholds (one damage roll applied to each target, per the SRD), in one message and one undo step. Creatures with no HP or no thresholds are named as skipped |
| missing pieces | no HP: `no HP on Goblin - s v sets it: HP 5`. Under Daggerheart, no thresholds: `no thresholds on Goblin's card - s k adds them: Thresholds: 8/15`. A minion's `None` thresholds are fine: Minion decides |
| undo | one step per `:dmg`, through `undo_edit_token`, as `<` and `>` are |
| players | the same rule if a player's creature has a card with thresholds; Armor Slots are not counted, since a player's sheet is on paper and the player marks them before saying the damage |
| the channel | not in this change: the channel has no counter requests yet. Recorded for later |
| no key | a command only. A count key (`11 D`) can come later if the table wants it; rule 3 leaves room |

## Where it goes

- `card.c`: `card_line(text, label, buf, sz)` -- the value after `Label:` on the first line
  that has it, up to the next run of spaces (`Thresholds: 8/15   HP: 3` gives `8/15`), case
  aside; `card_feature(text, "Minion", buf, sz)` -- the `X` of a line starting `Minion (X)`.
  The core reads no meaning into either.
- `ruler.c`: `Ruleset.thresholds`, set for Daggerheart; the rule itself, `damage_marks(dmg,
  major, severe, massive)`, a pure function with the SRD's table.
- `app_dmg.c` (new): `cmd_dmg` -- parse, pick the targets (the burst, else the target),
  apply, the Minion list (reusing the burst's band lookup from the target), the Horde check,
  the message. `app_cmd.c` gains the table row.
- `mapio.c`: `rule massive`; `Map.massive`.
- `app_draw.c`: the Horde note in the card box title.
- Tests in `tests/test_damage.c`: the threshold table at its edges (Major-1, Major, Severe-1,
  Severe, twice Severe with and without massive, 0), `half` rounding, sums, a minion and its
  list (sides, order, already-defeated left out), a horde crossing half, a burst with mixed
  creatures, undo, the missing-piece messages, `rule massive` round trip and its version.
  Each checked to fail with its behavior broken.
- `PROF_ZONE("dmg.apply")`; a perf.sh row, "play, damage" (`:dmg` on a burst of eight, then
  `u`); PERFORMANCE.md regenerated after.
- README (a Damage section, the command table, `rule massive` in the file format), the `?`
  page (`keys.c`, "Cards" group or a new "Damage" one), CLAUDE.md, ROADMAP.md 10 built.

## Build order

1. `card_line`, `card_feature`, `damage_marks`, their tests.
2. `:dmg N` on one creature, `half`, sums, the messages, undo.
3. Minion and Horde.
4. The burst.
5. `:dmg massive`, the file.
6. Docs, the `?` page, the perf row, the review (health-check questions included).

## As built

- **No new file version.** The plan had `rule massive` raise the file to 14. A vtt without
  `:dmg` ignores the line (W015) and loses nothing it uses, so a version that made it refuse
  the whole map would cost more than it saves. W029 is a `rule` line naming another rule.
- **Where:** the game's words (`DamageRule`: the `Thresholds` label, the `Minion` and `Horde`
  features) and the SRD's table (`damage_marks`, `damage_thresholds`, `damage_minion`,
  `damage_horde_attack`) are in `ruler.c` with the rest of the ruleset; `Ruleset.damage` is
  NULL for a game that takes damage off HP as is. `card_value` and `card_feature` in `card.c`
  read a card without knowing a game. The command is `app_damage.c`; `app_horde_note` feeds
  the card box's title.
- **The burst's catch** is `range_caught`, the same test its status line counts with
  (`geom_catches`, taken out of `range_status`), so `:dmg` hits exactly the creatures the line
  names, those out of sight (`*`) included. Distances in the Minion list are
  `token_gap_units`, footprint to footprint.
- **Messages:** one creature's is the long form; a burst's is one line,
  `9 damage to 3 caught: Goblin 2 HP (3/5), Raiders 2 HP (2/4), attack now 1d4+1; skipped
  Blank (no HP)`. The status line holds 160 bytes; the session log keeps all of it.
- **A creature already at 0** is skipped ("already defeated"), not marked again, and makes no
  undo step. A hit that marks fewer than its tier because fewer were left says the number
  actually marked.

## As reviewed

The review of f9a02d0 found, and these fixed:
- `Thresholds: 1/2000000000` overflowed twice Severe (UBSan): a threshold over 99999
  (`DAMAGE_THRESHOLD_MAX`) is no threshold, and twice Severe is compared as `dmg - severe >=
  severe`;
- the SRD gives two oozes `4/None` -- a Major and no Severe -- and the import writes it as
  given: it was refused; now Severe is none, 3 HP is never marked, and the message says
  `(4/None)`;
- the SRD's own stat-block line, `**Difficulty:** 14 | **Thresholds:** 8/15 | **HP:** 2`, was
  not read: `card_value` takes a label after a bold mark or a bar and ends a value at a bar;
- `:dmg massive on` reached the phones: it is `app_note_gm` now;
- a `Horde (3/HP)` type line at a line's start read as the attack: an X with a `/` is not one;
- a burst said "no HP" for both "marked none" and "has no counter": `marks none` and `(no HP
  counter)`;
- `:dmg 11half` was taken as `11 half`; the word needs its space;
- the counter's name is the rule's (`DamageRule.hp`), not a literal in three places
  (`hp_of`), and the cut-and-copy in `card_value`/`card_feature` is one `put_cut`.

Left as they are: in a burst the Minion list measures from the first Minion hit (by its place
in the creature list), since the attacker's range is the GM's call either way; and each Minion
the burst catches counts its own `N/X` extras, as the SRD has a multi-target attack's damage
applied to each target.
