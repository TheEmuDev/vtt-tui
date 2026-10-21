---
name: vtt
description: Build or change maps for vtt, the terminal virtual tabletop -- encounter maps from a description, a batch of edits to a .vtt file, or proposals live in a GM's open session. Use when asked to make, edit, check or answer a job on a vtt map.
---

# vtt: maps for a GM

1. **Read the full guide first, every session:** `vtt --agent-guide` prints it, the one
   that matches the vtt installed. What follows is only the opening move.
2. **Is the GM there?** `vtt --ctl status`. Exit 2 with `no vtt is taking requests` on
   stderr: work on the file with a plan. Exit 2 with a list of `--ctl-pid` lines: several
   vtts are listening; add `--ctl-pid N` for the GM's to every call. Otherwise it names the
   map, whether edits land at once or wait for the GM's review, and ends `seq N`.
3. **A plan (a new map, or edits to a file):** write requests one a line, then
   `vtt map.vtt --apply plan.txt --new 40x30` (`--new` only when the file is not there).
   All or nothing; a map the GM has open gets the plan as a proposal instead. Then
   `vtt map.vtt --check`: exit 0 is clean.
4. **Live, a job the GM asked for:** `vtt --ctl jobs`, `vtt --ctl 'job N take'`, read with
   `vtt --ctl 'dump REGION'`, send the edits headed `job N propose "what it is"`, and read
   them back with `vtt --ctl 'job N check'`. Then wait for the verdict:
   `vtt --ctl 'wait SEQ for 300'`, SEQ the `seq` line `status` gave, again from each `seq`
   it answers until your job's event comes. If vtt started you (`:agent command`), your
   standard input is the job and the exact calls to make (use those); exit once you have
   proposed, without waiting.
5. A refusal is exit 1 with `vtt: error CLASS: line N: WHY` on stderr (no `line N` when the
   request as a whole is refused; a plan's ends `- nothing saved`) and nothing on stdout. `help VERB` shows how a request is
   written: `vtt --ctl 'help VERB'` while a vtt is taking requests, else a plan holding just
   that line (`--apply`, with `--new WxH` if there is no file yet; nothing is written).
