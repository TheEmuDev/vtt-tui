# Choosing keys

The rules every key binding in vtt follows. Read them before adding or changing a binding.
The bar and the `?` page are both generated from the tables in `src/keys.c`.

1. **Vim's key, if vim has one.** `d` `y` `p` `c` `i` `u` `/` `n` `v` `:` mean what they mean
   in vim. Nothing else may use them.
2. **One meaning in every mode.** A letter that works in build mode and in play mode does the
   same job in both: `b` is the cursor's size, `m` measures, `o` is a door. If a key cannot
   mean the same thing in both modes, choose another key.
3. **Digits are counts, and a count names a value outright.** `3l` moves three squares. On a
   key that cycles, the count selects: `2b` is 2×2, `2r` the second range band, `20r` twenty
   squares, `2R` the cone. A bare press cycles; a count past the end selects the last value.
4. **Lowercase is the tool, the capital is its variant.** `m` measures and `M` changes the
   metric; `r` is the range highlight and `R` its shape; `v` and `V` are the two selections;
   `b`/`B`, `t`/`T` and `a`/`A` run the same cycle in opposite directions. A capital never
   starts something unrelated.
5. **A family gets a prefix, not a row of keys.** `i p` `i e`, `s a` `s c` `s d`, `g r` `g h`.
   The prefix alone lists its options on the status line; `esc` abandons it; it consumes the
   next key whatever that key is.
6. **The cursor is the pointer.** Anything that needs a place or a direction reads the
   cursor: the ruler's far end, the brush's footprint, the range template's aim. There are no
   separate aiming keys.
7. **`esc` backs out one layer at a time, and `enter` commits.** Settings survive `esc`;
   modes and overlays that were switched on do not.
8. **Six hints on the bar, everything on `?`.** Both read the same table in `src/keys.c`,
   no key string appears twice in one table, and anything rarely used is a `:` command
   rather than a key.
9. **`off` switches off; `remove` destroys.** In `:` commands and the control channel, `off`
   only turns something off that `on` brings back as it was (`:fog off`, `:serve off`,
   `hidden off`). Anything that throws something away says so and never says `off`:
   `remove` after the name it removes (`:clock Dragon remove`, `:roll attack remove`, `:link 3 remove`), the
   channel's `token del` for a creature, and `:turns end` to end a fight.
   An old spelling says what replaced it rather than doing something else.

## The review, read against the rules

`:review` is a mode of build mode for looking at a change someone else proposed
(docs/CONFLICTS.md); its table in `src/keys.c` is `REVIEW`. Its keys were chosen by the
rules above, and one of them bends a rule on purpose:

| key | does | rule |
|---|---|---|
| `enter` | accepts the change, or the part in the box | 7: `enter` commits |
| `esc` | drops the box, then leaves the review; the change stays waiting | 7: one layer at a time |
| `d` | scraps it; `:review N` brings it back | 1: vim's delete. Nothing is destroyed, so it is not a `remove` (9) |
| `v` | boxes a part to accept alone | 1, 2: the same selection as anywhere |
| `n` `N` | the next and the previous change waiting | 1, 4 |
| `u` | undoes the GM's own last step, as anywhere | 1, 2 |
| `c` | sends it back to the agent with a line of what to do instead | 1, and it **strains 2** |

`c` is vim's "change", and here it asks for one, in the GM's words. In play mode `c` changes a
creature's label. Both change the thing under the hand, but not the same thing, which rule 2
asks for. It was accepted (the user, 2026-10-07) over `:review say TEXT` with no key, because
sending a change back is the commonest answer after accepting it. A second key that strains
rule 2 the same way should not lean on this one: it needs its own case.

The bar holds accept, scrap, feedback, box, back and `? keys` (rule 8); `n`/`N`, `u` and
`:jobs` are on the `?` page.
