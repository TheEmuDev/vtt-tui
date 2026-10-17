# tools/shift: testing the line search against damaged and undamaged files

The line search (`vtt MAP --check`, docs/V2.md *Step 5, second piece*) must never name a
wrong line, nor say anything of an undamaged file. These scripts, from its reviews, look
for counterexamples. Each runs `./vtt` in a sandbox of its own (XDG dirs under OUT or a
temporary directory); run them with `python3 -I`, from the repository's root.

| script | what it does |
|---|---|
| `gen_shapes.py ./vtt OUT [ROUNDS] [SEED]` | random maps of nine shapes (doors, gaps, walls in void, thick, 512-wide, runs, caves, big tokens, mixed), one or two edits (`TWO=1`), separators (`SEP`); WRONG for a span no fix in it makes right |
| `gen_scenarios.py ./vtt OUT [ROUNDS] [SEED]` | named multi-edit scenarios (`SCEN`: hidden losses, two blanks, junk lines, tiny maps, pastes, repeated headers...); checks placed lines by section and every count and no-place claim |
| `scan_endings.py OUT MAP...` | undamaged files: every way three sections may end (blanks, spaces, comments, tabs, CR, records), both row forms, CRLF, orders, fog; any finding is a false positive |
| `judge.py FILE...` | judges flagged files by section: a two-edit flag whose fix restores the map is RIGHT |
| `shipped_edits.py ./vtt OUT` | every single edit of the shipped maps that changes the map, classed by what `--check` says |

What a run of each found is in docs/V2.md's *Progress* (the line search's nine reviews).
After a change to the line search: `make && python3 -I tools/shift/gen_scenarios.py ./vtt
/tmp/g 200 1`, then `scan_endings.py` over the fixtures, then `judge.py` on anything flagged.
