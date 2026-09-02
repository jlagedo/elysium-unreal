# Documentation (`docs/`)

Documentation records design intent, VtMB reverse-engineering facts, and project status.

## Ownership

- `project/roadmap.md` owns master sequencing, playable-path priority, and all task status —
  one line per task, linked to its specification while open.
- `project/plans/` holds open-task specifications keyed by roadmap ID, one file per area
  (world, gameplay, audio, characters-ui, input, spine, theatre, animation, three-cs,
  pipeline). A plan file carries **no status marks**; landing a task deletes its entry.
- `project/rebuild-strategy.md` owns strategy, milestone vocabulary, and sidecar contracts.
- `project/reconstruction-direction.md` owns modernization boundaries.
- `architecture/` owns Unreal system designs and integration seams.
- `vtmb/` owns engine-neutral VtMB formats and behavior.
- `recovered/` owns explicitly uncertain reconstructions and their confidence.
- `operations/` owns repository, build, research, and Git procedures.

## House rules

- Record a deliberate divergence beside the faithful behavior in the owning topic and
  identify the explicit owner call.
- Say what evidence would verify an uncertain reconstruction.
- Never place status outside `project/roadmap.md`.

Research specifications may cite hashes, provenance, eliminated leads, open questions,
and the consuming document. Raw decompilation, Ghidra projects, captures, reports, and
third-party reference source remain under `ELYSIUM_WORK_ROOT`.
