# Documentation (`docs/`)

Documentation records design intent, VtMB reverse-engineering facts, and project status.
Source code remains the as-built record.

## Ownership

- `project/roadmap.md` owns master sequencing, playable-path priority, and all task status —
  one line per task, linked to its specification while open.
- `project/plans/` holds open-task specifications keyed by roadmap ID, one file per area
  (world, gameplay, audio, characters-ui, input, spine, theatre, animation, three-cs,
  pipeline). A plan file carries **no status marks**; landing a task deletes its entry.
- `project/rebuild-strategy.md` owns strategy, milestone vocabulary, and sidecar contracts.
- `project/remaster-direction.md` owns modernization boundaries.
- `architecture/` owns Unreal system designs and integration seams.
- `vtmb/` owns engine-neutral VtMB formats and behavior.
- `recovered/` owns explicitly uncertain reconstructions and their confidence.
- `operations/` owns repository, build, research, and Git procedures.
- `index.yaml` maps each document to its kind, owning system, and canonical subject. It
  contains no task status.

## House rules

- Write present-tense facts. Git history carries change narratives.
- Keep one fact in one owning document; other documents link to it.
- Record a deliberate divergence beside the faithful behavior in the owning topic and
  identify the explicit owner call.
- Mark uncertain reconstructions and say what evidence would verify them.
- Correct contradicted facts in place.
- Never place status outside `project/roadmap.md`.

Directory `CLAUDE.md` files orient readers to code and workflow. They do not own VtMB
facts or roadmap status.

Research specifications may cite hashes, provenance, eliminated leads, open questions,
and the consuming document. Raw decompilation, Ghidra projects, captures, reports, and
third-party reference source remain under `ELYSIUM_WORK_ROOT`.
