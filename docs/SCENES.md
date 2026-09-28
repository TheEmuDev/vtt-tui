# Scenes

The creatures and the fight, saved under a name on the map and put back: reset an
encounter, stage "before" and "after the ambush", give an agent a point to compare against.
Signed off 2026-09-27. The README is the user's reference; this page is why it is the way
it is.

## Decisions

| question | answer |
|---|---|
| what a scene holds | every creature as it stands -- place, size, side, label, markers, counters, note, hidden, its place in the turn order -- and, for a whole-map scene, the round and the spotlight |
| what it does not | terrain, walls, door state, square notes, fog (the party's explored ground too), clocks, rolls: the map and the session, not the staging. A door kicked open stays open; the GM closes it |
| a box | `:scene save NAME` with a `v` box up saves the creatures in the box, and the scene keeps the box: putting it back replaces only the creatures in it |
| putting one back | at once, one undo step; no question, since `u` takes it back |
| removing one | `:scene NAME remove` (docs/KEYS.md rule 9: `off` never destroys) |
| where | build and play mode; saving and putting back are refused while a creature is carried |
| privacy | every message about a scene is the GM's (`app_note_gm`): "after the ambush" is a spoiler |
| undo | putting a scene back is a step; saving and removing one are not, as saving a named roll is not |
| the channel | `scene save NAME [REGION]`, `scene NAME`, `scene NAME remove`, and the reads `scenes` and `scene diff NAME`. Saving and removing change nothing the undo log can take back, so each must be alone in its request, as `undo` is |

## Shape

- **On the map.** `Map.scenes[16]`, each a name, an optional box, the round and spotlight,
  and its own `TokenList`. Names are compared ignoring case, 1-31 characters, no quote,
  and do not start with the words `save` or `diff` (the command's own). Saved in the map file from version 11: `scene "Name"` (with `x0 y0 x1 y1` for a box),
  then the ordinary creature lines and `round`/`spotlight gm`, then `endscene`. The loader
  reads a scene's creature lines with the scene's list swapped in for the map's, so every
  creature parser serves both. A scene with no `endscene`, or a second `scene` line before
  it, is dropped with `W025`.
- **Putting one back** (`scene_restore`): checked first -- a scene creature that would
  land on a creature the scene does not replace (one outside a boxed scene's box) refuses
  the whole restore, naming both -- then one batch: the creatures it replaces go (a whole
  scene: all; a boxed one: every creature meeting the box), the scene's arrive. A whole
  scene sets the round and spotlight; a boxed one leaves them, and its creatures' turn
  places are kept but not the turn itself when a creature outside holds it. The
  selection, the group and the range overlay are cleared: the indices they held are gone.
- **The picker** (from character templates) serves `:scene` with no name: each row the
  name, the creatures, the round, and the box.
- **`scene diff NAME`** matches creatures by label (the channel's labels are unique);
  unlabeled ones match by side and square. Lines: `moved`, `gone`, `new`, `changed` (size,
  side, markers, counters, note, hidden, turn place), then the round and spotlight for a
  whole scene; `no changes` when there are none.
- **Resizing the map** drops scene creatures that no longer fit, and a scene's box is
  clipped; a scene whose box is wholly cut away goes, since it holds nothing and would
  clear the map if put back as a whole-map scene.

## Build order

1. `Scene` on the map, file version 11, `W025`; `scene.c`: names, save, remove, restore, diff.
2. `:scene`, `:scenes`, the picker; the channel.
3. README, the `?` page, AGENTS.md, CONTROL.md; tests; `tools/perf.sh`; then the review.

## Instrumentation

`PROF_ZONE("scene.restore")` and `PROF_ZONE("scene.save")`; a perf.sh row saving and
putting back 500 creatures.
