# Story 6 · working files (retired at close)

Ground rules every lane reads first. The plan is the owner's approved plan of 2026-09-29;
the story text is `../spec.md` § "6. The deletions and the mechanism seams".

## Standing rule

Follow retail behaviour. Take on no work already planned in another story of this spec or in
another spec. Work assigned to this story is executed in full; refuse a piece only for an
architectural choice or a retail divergence, and write the refusal down with its reason.

## How a lane works

- **One checkout, no worktree, no git commands that change state** (no commit, stash,
  checkout, reset). You edit files in place on `main`.
- **You own only the files named in your brief.** Touch nothing else. If a change you need
  lives in a file you do not own, write it in your report under "Needs another owner" with the
  exact edit, and stop there.
- **You never build or run Unreal tests.** The orchestrator builds and tests once per wave.
  You may run `grep`/`rg`, `ast-grep`, `sd`, `git diff`, `git log -p` on files, and read-only
  python (`uv run python -c ...`) that writes nothing under `docs/`, `Source/` or `research/`.
  Lane T may run `uv run pytest` on the pipeline tests it owns.
- **The ledger is the orchestrator's.** Never edit `research/tooling/ghidra/driver/kernel_verdicts.tsv`,
  `SLOT_PORT_MAP` in `gen_kernel_shape.py`, `docs/vtmb/npc-kernel/signatures.tsv`, or any generated
  file (`docs/vtmb/npc-kernel/*.md`, `Source/ElysiumUE/Private/Substrate/ElysiumNpcKernelShape*`,
  `*Slots.inl`, `*Slots.cpp`, `ElysiumNpcKernelTunables.*`, `ElysiumNpcKernelBindings.*`).
  Instead deliver `targets-<lane>.tsv`: one line per ledger row you closed,
  `address<TAB>new_target<TAB>note`, where `new_target` is `-` for a dead body you removed,
  or the service name for a mechanism body you replaced (see "Target spellings").
- **Keep your context small.** Read excerpts, not whole files. Do not paste code or logs into
  your report.
- **Report** to `report-<lane>.md` in this directory, at most 300 words: what you removed or
  replaced (counts, by file), what you found that contradicts the brief, rows you refused to
  delete and why (a live observer, a save field, a retail divergence), and "Needs another
  owner" edits. Then answer the orchestrator in one paragraph pointing at the report.

## What `dead` and `mechanism` mean here

From `docs/vision.md` § "The three adjudication tests", restated for the kernel:

- **dead**: a retail body nothing can observe — no keyfield, schedule text, script name,
  entity output, save field, player-visible timing or witness would notice its absence.
  Story 1 verdicted it (`docs/vtmb/npc-kernel/delete-list.md`, the "Why" column). Story 6
  removes the port body and its tests. Nothing is rewritten; a dead body is not "simplified".
  A debug print (`EmitDevMsg`, `EmitDebugMsg`, `DevMsg`, `Msg`, an overlay draw) has no output
  device in this port and is dead wherever it sits, including inside a `rule` body: delete the
  print line, leave the rule.
- **mechanism**: a retail body the world merely needs and Unreal supplies
  (`docs/vtmb/npc-kernel/seam-list.md`, the "Unreal service" column names the service). Story
  6 replaces the body with a one-line forward into the existing seam, or with nothing when
  the service has no port-side function at all (physics tick, replication, destructors).
  Retail thresholds it carried go to story 4's tunables overlay
  (`research/tooling/ghidra/driver/kernel_tunables.tsv` — report the cell, do not edit the
  overlay) — never left inline.
- **A dead row with a live observer is not deleted.** If a `rule` body calls it for a value
  (not for a print), if it is a SAVE field, a script-visible name, or an entity output, report
  the row for re-verdict with the caller's address and stop.

## Target spellings (`targets-<lane>.tsv`)

- Dead body removed: `-`.
- Mechanism body now a one-line forward that survives as a chain method: keep `hand:<Class>::<Method>`.
- Mechanism body gone, service answers it: a bare service name. Use these words:
  `CMC` (character movement component), `UNavigationSystem`, `UPathFollowingComponent`,
  `TraceRetail` (the collision service), `Chaos` (physics, nothing at run time),
  `Replication` (network state, nothing), `CRT:operator delete`, `FElysiumSaveArchive`
  (the generated SAVE walk), `Bake` (asset loading resolved at bake / load time),
  `0010`, `0015`, `0018/3`, `0018/16`, `0002/28` (owned by that spec / story; forwarded only to a
  seam it already built, nothing built here).

## Retail sources

`docs/vtmb/npc-kernel/` (`functions.md`, `fields.md`, `slots.md`, `entries.md`), the walked
prose in `docs/vtmb/npc-ai/` (`index.md` maps an address to its section), and the
`vtmb-corpus` MCP tools (`vtmb_func <address>`, `vtmb_callers`, `vtmb_readers`) for what
neither holds. Cite addresses in the report when a decision rests on retail.
