# Documentation (`docs/`)

Documentation records design intent, VtMB reverse-engineering facts, and project status.
Source code remains the as-built record.

## Ownership

- `project/roadmap.md` owns master sequencing, playable-path priority, and roll-up status.
- `project/retail-capture-roadmap.md`, `project/animation-roadmap.md` and
  `project/three-cs-roadmap.md` are the three scoped subtrackers. The first owns detailed status
  for the retail capture harness and the capture-index-inspect loop that verifies the
  original-runtime animation and facial decode against the export. The second owns detailed status
  for the skeletal animation asset programme — the character asset bake, the shared skeleton, layer
  masks, blend spaces, and the action catalog. The third owns detailed status for the player-feel
  vertical — the mover's published body state, the resolver seam, the player animation graph, the
  camera service and rig, input response, and the gym that measures them.
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
- Never place status outside `project/roadmap.md` and its three declared scoped subtrackers,
  `project/retail-capture-roadmap.md`, `project/animation-roadmap.md` and
  `project/three-cs-roadmap.md`.

Directory `CLAUDE.md` files orient readers to code and workflow. They do not own VtMB
facts or roadmap status.

Research specifications may cite hashes, provenance, eliminated leads, open questions,
and the consuming document. Raw decompilation, Ghidra projects, captures, reports, and
third-party reference source remain under `ELYSIUM_WORK_ROOT`.
