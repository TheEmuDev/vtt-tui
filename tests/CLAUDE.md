# tests/ -- notes for Claude

Loaded when a file under tests/ is read. The suite list by area is the `tests/` row of
docs/CODEMAP.md; `tests/run.c` is the table of suites and `main`, nothing else.

- `make test` builds with ASan+UBSan and runs everything. `make test-build` builds what the
  tests need, per object and a job a core (`build/run-tests` links the ASan objects with
  `build/asan/tests/*.o`; only what changed is compiled), then `build/run-tests NAME...`
  (suites side by side, each in a child of its own; `-j1` for one process in order, as a
  debugger wants; `VTT_CASE_TIMES=1` prints each case's time)
  runs only the suites named in run.c's table -- use it while iterating, the whole suite
  before a change is called done (docs/TESTSPEED.md has what each part takes). The
  line search's own counterexample tools are tools/shift/ (README.md).
- `VTT_UPDATE_GOLDEN=1` regenerates golden frames; `VTT_REQUIRE_NODE=1` fails rather than
  skips the suites that need node (`pagefeed`, `pageconn`, `netlag`'s page check); `VTT_FOGDIFF_OPS=36000 build/run-tests` is the long fog
  differential run.
- A forked test child ends with `child_exit(rc)`, never `_exit`: LeakSanitizer never runs
  at `_exit`, so a leak in the child's code would pass (docs/reviews/2026-10-20-4-memory.md,
  M5). Before it, the child frees what it inherited and holds only in a local of the
  forking function (a buffer the parent `malloc`ed there): at some optimization level the
  pointer is in no stack slot and reads as a leak. Check a new child at `-O2` once:
  `make test-build CFLAGS=-O2; build/run-tests SUITE` (build/asan is stamped with its flags:
  the next plain build rebuilds it back).
- A seeded fault must escape the compiler: an unused `malloc` is deleted (gcc keeps no call),
  so a seeded leak stores to a `volatile` or uses what it allocated.
- `make test` ends with `tools/oracle.sh check` (twice: over the release `./vtt`, then over
  `build/asan/vtt`, the sanitizer build, where a leak or an error fails it): ./vtt's own output (map tools, frames,
  `--apply` answers) against `tests/oracle/expected`. A difference is a bug or a meant
  change: for the second, `tools/oracle.sh record` and read `git diff` before committing it.
- A test that expects a channel request's edits to land sets `a.world.ctl_auto = 1` after
  `app_init`; otherwise they arrive as a proposal. Job numbers are not lowest-free
  (`World.job_last`): reset with `jobs_reset()`.
- Suite `protocol` (test_protocol.c) runs the channel on a bare `World` with a recording
  host and includes no app header (`check.h`, not `harness.h`); `make test` builds it a
  second time as `build/protocol` with no `src/app*.c` linked. A new host call gets a
  line in its `Rec` and a case; a protocol rule that needs no screen is tested there.
- The interactive loop is `src/loop.c`, and suite `loop` (test_loop.c) drives it: a pipe
  for the keyboard, `turn()` for one pass, time as an argument. What is still untested is
  main.c's few lines round it (`poll` itself, start-up, `--serve`): a live vtt for those.

## Writing tests

- `press(&a, "keys")` feeds bytes through the real parser; `\r` is enter, `\x1b`
  esc, `\025` ctrl-u (clears a prompt). `Key k = { KEY_ENTER, 0, 0 }` for raw keys.
- A new suite: a `void test_x(void)` in the file for its area (make it non-static and
  declare it in `harness.h`), then a row in `run.c`'s table. A helper another file needs
  moves its prototype to `harness.h` and loses `static`.
- Keep a sandbox's `XDG_RUNTIME_DIR` short: the channel's socket path must fit in 107 bytes,
  and past it the channel refuses to start ("the socket path is too long"), so a script runs
  with the channel off and may not notice (review 4, M11).
- `sandbox_enter` sets `XDG_DATA_HOME`, `XDG_RUNTIME_DIR` and `XDG_CONFIG_HOME` to the sandbox,
  `VISUAL` and `EDITOR` to `true` (a card key runs the editor and waits: the user's `nvim` held
  a run until killed, 2026-10-21), and `VTT_SECRET_TOOL` to no program: a test that runs `./vtt` or opens the channel outside
  one reaches the user's own files, settings, API key (their keyring) and running vtts. A test
  of the keyring sets `VTT_SECRET_TOOL` to a stand-in of its own; of curl, `VTT_CLAUDE_CURL`.
  To check: `XDG_RUNTIME_DIR=/some/canary build/run-tests` must leave the canary untouched.
- App tests: `Sandbox sb = sandbox_enter("name")`, `write_map_file(sb.dir, "fight.vtt")`
  (a **2×2 empty map** — place tokens with `ipAria\r` after setting `a.ed.cx/cy`),
  `app_open_map`, `Key f2 = {KEY_F2,0,0}` for play mode, `sandbox_leave(&sb)`.
- `play_focus` moves the selection, not the cursor; `enter` acts on the cursor.
- Rendered cells: `rnd_begin(&r); app_draw(&a);` then read `r.back[]`; `rnd_dump` for text.
- Checks are `CHECK` / `CHECK_EQ` under a `CASE("...")`; failures print `FAIL file:line`.
- `press` checks two things after every key, a failure each: the undo log is balanced, and
  (docs/V2.md, step 7, item 4) in play mode, while `app_view_differs` is 0, the players' frame
  drawn by its own renderer equals the GM's cell by cell -- the copy path would send the GM's
  frame otherwise. It saves and restores `a->ed.view` and `a->players` round its draws, so a
  test's camera is its own frames'. A failure prints the row both ways.
- Golden frames live under `tests/`; regenerate only when the change is intended.
  `golden_bytes(name, data, len)` compares any text (a map tool's report) the same way.

