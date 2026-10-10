# tests/ -- notes for Claude

Loaded when a file under tests/ is read. The suite list by area is the `tests/` row of
docs/CODEMAP.md; `tests/run.c` is the table of suites and `main`, nothing else.

- `make test` builds with ASan+UBSan and runs everything. `build/run-tests NAME...` runs
  only the suites named in run.c's table -- use it while iterating, the whole suite before
  a change is called done.
- `VTT_UPDATE_GOLDEN=1` regenerates golden frames; `VTT_REQUIRE_NODE=1` fails rather than
  skips the page decoder test; `VTT_FOGDIFF_OPS=36000 build/run-tests` is the long fog
  differential run.
- A test that expects a channel request's edits to land sets `a.ctl_auto = 1` after
  `app_init`; otherwise they arrive as a proposal. Job numbers are not lowest-free
  (`App.job_last`): reset with `jobs_reset()`.
- Nothing covers main.c's interactive loop: drive a live vtt for that (CLAUDE.md, *Shell*).

## Writing tests

- `press(&a, "keys")` feeds bytes through the real parser; `\r` is enter, `\x1b`
  esc, `\025` ctrl-u (clears a prompt). `Key k = { KEY_ENTER, 0, 0 }` for raw keys.
- A new suite: a `void test_x(void)` in the file for its area (make it non-static and
  declare it in `harness.h`), then a row in `run.c`'s table. A helper another file needs
  moves its prototype to `harness.h` and loses `static`.
- `sandbox_enter` sets `XDG_DATA_HOME` and `XDG_RUNTIME_DIR` to the sandbox: a test that runs
  `./vtt` or opens the channel outside one reaches the user's own files and running vtts.
  To check: `XDG_RUNTIME_DIR=/some/canary build/run-tests` must leave the canary untouched.
- App tests: `Sandbox sb = sandbox_enter("name")`, `write_map_file(sb.dir, "fight.vtt")`
  (a **2×2 empty map** — place tokens with `ipAria\r` after setting `a.ed.cx/cy`),
  `app_open_map`, `Key f2 = {KEY_F2,0,0}` for play mode, `sandbox_leave(&sb)`.
- `play_focus` moves the selection, not the cursor; `enter` acts on the cursor.
- Rendered cells: `rnd_begin(&r); app_draw(&a);` then read `r.back[]`; `rnd_dump` for text.
- Checks are `CHECK` / `CHECK_EQ` under a `CASE("...")`; failures print `FAIL file:line`.
- Golden frames live under `tests/`; regenerate only when the change is intended.
  `golden_bytes(name, data, len)` compares any text (a map tool's report) the same way.

