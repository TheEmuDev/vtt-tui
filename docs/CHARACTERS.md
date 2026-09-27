# Character templates

A creature saved once -- its label, size, counters, note and the named rolls it uses --
and placed again by name, on any map. Stamps for creatures: fight prep without the typing.
Signed off 2026-09-27. The README is the user's reference; this page is why it is the way
it is.

## Decisions

| question | answer |
|---|---|
| name | character templates, `:character` (players' companions are templates too, so not "foe") |
| placing | `i t e` places one as an enemy, `i t p` as a player: the side is the key's, so one Wolf template is a foe or a companion. `i p` and `i e` are unchanged and never look a template up. |
| choosing | a picker: the list filters on every letter, `tab` fills in the highlighted name and again moves to the next match. The same picker serves `:stamp`. |
| rolls | saved only when named: `:character save ghoul claw bite` |
| hidden | never saved; a placed character arrives visible, `s h` hides it |
| the channel | `token add enemy SQ from NAME [hidden]` places one; a `characters` read lists them |
| not here | placing several at once, deleting a template from inside vtt (they are files, as stamps are), a key for the stamp picker in build mode |

## Shape

- **The file.** A template is a map file, `NAME.vtt` in `$XDG_DATA_HOME/vtt/characters`
  (else `~/.local/share/vtt/characters`), beside the stamps: a map the creature's size
  square with the one creature at its corner and the saved rolls. The loader, the writer
  and the map tools work on it unchanged; the format version does not move. A file that
  does not hold exactly one creature is refused by name.
- **The name** follows the stamp rules (letters, digits, `-`, `_`), so it is always a file
  and never a path. The creature keeps its own label, so `ghoul` places "Crypt Ghoul",
  "Crypt Ghoul 2" and on, numbered as pastes are. `:character save` with no name takes
  the label without its copy number, anything but a letter or digit made `-`.
- **Saved fresh.** Counters are saved full (a wounded Ghoul saves a whole one); markers,
  the turn order, where it stands and hidden are one fight's and are not saved.
- **Placing** is one undo step at the cursor, at the template's size, checked as `i e`
  checks (on the map, on nobody). A roll the map lacks is added; one it has by that name
  is kept, and the status line says so when the two differ; a full roll table is said
  too. The rolls go through the undo log (`OP_ROLL`), so `u` takes back the whole
  placement and the channel's all-or-nothing rollback takes back its rolls.
- **The picker** is a modal: a line to type in and up to ten rows under it, each a name
  and what it is (`ghoul  Crypt Ghoul  enemy 2x2  HP 12/12`; `pillar  3x3`). Matches are
  anywhere in the name or the label, ignoring case: the exact name first, then names
  starting with the text, then the rest, alphabetical within each. `up`/`down` (and
  `ctrl-p`/`ctrl-n`) move the highlight, `tab`/`shift-tab` fill in the next or previous
  match without narrowing the list, `enter` takes the highlighted row, `esc` cancels.
  It is the GM's: a modal, so the players' frame never has it.
- **Keys.** `i` then `t` waits for `p` or `e` and says so. `:character` and
  `:character NAME` open the picker (NAME already typed) and place on the template's
  saved side; `:character save [NAME] [ROLL...]` saves the creature under the cursor.
  `:stamp` with no name opens the stamp picker where it used to list; `enter` takes the
  stamp up as `:stamp NAME` does. Characters are play mode's; stamps stay build mode's.
- **The channel.** `token add player|enemy SQ from NAME [hidden]` in place of the size and
  label: the template's size and label (numbered to stay unique, as labels there must).
  `characters` lists name, label, side and size.

## Build order

1. `character.c`: the directory, save, load (checked, fresh), list, place; `OP_ROLL`.
2. The picker in `ui.c`, `MODAL_PICKER` in the app; `:stamp` onto it.
3. `i t p`/`i t e`, `:character`, the save.
4. The channel's `from` and `characters`; README, the `?` page, AGENTS.md, CONTROL.md.
5. Tests, `tools/perf.sh` rows, PERFORMANCE.md; then the review.

## Instrumentation

`PROF_ZONE("picker")` round filtering, `PROF_ZONE("picker.open")` round reading the
directory and the files, `PROF_ZONE("character.place")`. perf.sh rows: opening the picker
over 500 templates and typing into it, and placing one.
