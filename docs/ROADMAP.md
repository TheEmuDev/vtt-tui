# Roadmap

Features decided on, in the order they will be built. Each gets a plan signed off before
any code, and moves to the README when it ships. Ideas not yet decided live in IDEAS.md.

1. **Hidden creatures.** A flag on one creature -- an ambusher, something invisible, a
   mimic -- that keeps it off the players' screens even on lit ground, drawn dimmed on the
   GM's. Fog hides squares; this hides a creature. While any creature is hidden, the
   players' screens show no status messages, as over fog.
   - **Later, if play testing asks for it: messages marked at the source.** Blocking every
     message is safe but costly when something stays hidden all session (no rolls, rounds
     or spotlight news on the phones). The fix: messages that name no creature stay
     public; those that do go through one helper that keeps them to the GM when a creature
     they name is hidden; counts count only what the players can see; and a sweep test
     runs every play key and command with a creature hidden and checks its name never
     reaches the players' frame.
2. **Creature templates.** A creature saved once -- label, size, counters, note, named
   rolls -- and placed by name (`i e Ghoul`, the channel's `token add enemy Crypt Ghoul`).
   Stamps for creatures: fight prep without the typing.
3. **Scenes.** The creatures' places, markers and turn order saved under a name and put
   back: reset an encounter, stage "before" and "after the ambush", give an agent a point
   to diff against (the channel's unplanned checkpoints, CONTROL.md).
4. **Handouts on the phones.** A short text card -- an inscription, a letter -- pushed to
   the players' screens and taken down again. The page and server already carry messages;
   the work is the card and a `:handout` command.
5. **Links between map files.** A link end that opens another map and carries the party
   over (the town to the dungeon): links and floors across a campaign. The largest: it
   touches opening maps, the autosave and the players' connection.

**Housekeeping due alongside** (CLAUDE.md's watch list): `tests/run.c` is at 15.6k lines --
split it by area. (The `:` command chain became a table, 2026-09-27.)
