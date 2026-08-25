# Elysium-Unreal

*Vampire: The Masquerade – Bloodlines* (VtMB, 2004, early Source engine) rebuilt as a playable
game — **remastered** — on **Unreal Engine 5.8 + C++**. Every game-derived runtime input is
produced by this repo's own offline decode/export pipeline from the user's own install.

## Read first

- **`docs/project/rebuild-strategy.md`** — north star, principles, the two tracks, sidecar
  contracts, per-system design targets.
- **`docs/project/remaster-direction.md`** — what may be modernized, what must be reproduced, who
  decides.
- **`docs/project/roadmap.md`** — project priority and all task status, including the LIFE and CCC
  programme sections. Status lives nowhere else.

Directory-scoped facts live in sub-files that load with the code they describe:
`Source/ElysiumUE/CLAUDE.md` (the C++ runtime), `pipeline/CLAUDE.md` (the offline Python
pipeline), `research/CLAUDE.md` (research cases and tooling), `docs/CLAUDE.md` (documentation
ownership), `Content/CLAUDE.md` (tracked and generated packages).

Procedures live in skills, not here: `build-slots` (one engine install per concurrent checkout),
`elysium-testing` (the tiers, and how much export/bake a change authorizes), `gameplay-change`
(the entity/API/event/save contract), `authored-assets`, `vscode-intellisense`.
`.claude/rules/cpp.md` carries the C++ coding policy and loads with `Source/**`.

## Load-bearing rules

### Remaster direction

Keep VtMB's tone, ambience, feel and logic; raise the craft. **Not** a pixel-perfect recreation.
**Default resolves to reproduce.** A behavioural divergence needs the faithful behaviour known and
recorded in the doc that owns the system, stated beside the divergence and marked as one, on an
explicit owner call. **No A/B mechanism, feature flag, or state-enabling cvar exists without owner
approval by name**; work lands as a complete change, and the faithful behaviour stays recoverable
through git history. The one standing exception is the UI, which has no classic mode: VtMB's
screen structure re-skinned with vector type on a resolution-independent Slate/UMG stack — no VGUI
port, no 640×480 canvas, no `.fnt` bitmap atlas at runtime. Full charter:
`docs/project/remaster-direction.md`.

### Unreal owns the engine; the substrate owns the game

Troika's source is an RE oracle, never an implementation to port. A system is reproduced in this
repo only when **authored content or a game rule names it** — a keyfield, an entity input or
output, a script call, a rulebook row, a save field, a timing the player can observe. Everything
the world merely needs in order to work — traces, visibility, pathfinding, physics solving,
skinning, audio mixing — is Unreal's, reached through a query on the service seam. Reproducing
Source's *rules* (formulas, call order, thresholds) is faithful work; porting Source's
*mechanisms* is a defect. The adjudication is the Ownership test in
`docs/project/remaster-direction.md`; the closed register of deliberate reproductions is in
`docs/project/rebuild-strategy.md`. A port outside the register is a bug, not a tolerance.

### Runtime failures are never silent

Every unexpected runtime failure must be observable. A path may recover or return failure, but it
must emit at least a warning to the appropriate console or named log category, with enough context
to identify the failed operation and affected object or input; use an error, assertion or fatal
failure when the severity warrants it. Never swallow an exception, ignore a failed return value, or
turn a missing prerequisite or unsupported case into a quiet no-op or plausible default. Log once
where the failure is owned or handled, then propagate a structured failure when callers need it,
rather than producing duplicate warning spam. An ordinary negative query result or an explicitly
optional absence is not a failure and does not require a warning.

### Bring-your-own-game

**Nothing game-sourced is committed.** The decoders read *the user's own VtMB install*; their
output (`$ELYSIUM_EXPORT_ROOT/`) is gitignored and regenerable, and so is everything derived from
it — including the baked `.uasset` mount `Plugins/ElysiumBaked/Content/` (only the `.uplugin` is
committed). This is the legal posture, not a convenience: prior community rebuilds died to a C&D,
not to technical failure. What may be tracked, and where: `Content/CLAUDE.md`.

### The two clean halves

- **Offline — `pipeline/`** (Python): decodes VtMB's proprietary formats (BSP v17, MDL v2531,
  TTH/TTZ, VPK, VMT, `.fnt`, `.res`) into engine-neutral intermediates under
  `$ELYSIUM_EXPORT_ROOT/<map>/`, then bakes each map's *look* and every NPC body onto the
  `/ElysiumBaked` mount. Both bake stages are editor commandlets, so an editor build is a
  prerequisite for export, never for running.
- **Runtime — `Source/ElysiumUE/`** (C++): opens the baked level, **adopts** its actors, loads the
  baked meshes and clips off the same mount, and builds everything else in code from the
  intermediates on disk — collision, ropes, the sky cubemap, the entity substrate, audio,
  scripting. Light values are re-derived from `.lights` at load rather than adopted, so live
  calibration always wins. Python is **never** run at runtime to produce content; the seam is
  file-based. (The embedded CPython 2.7 VM runs VtMB's *own* level scripts; that is game logic, not
  pipeline.) Cost and design: `docs/architecture/uasset-bake-spike.md`.

### Authored live: tracked in the authored namespace, or captured as text

The editor is the authoring tool for anything Unreal authors better than code — an animation graph,
a material, a widget, a Niagara system. An **original, game-independent** asset is tracked directly
under `Content/ElysiumAuthored/**` (Git LFS), edited live and saved in place, with no generator;
the `authored-assets` skill owns the register and the procedure. An asset that must live on a
**generated** mount is never the tracked artifact: it is captured into a reviewable text source
under `pipeline/unreal/` and a generator rebuilds the package from that text, because a hand-edited
binary package on a generated mount breaks `reconstruct` and puts a generated package inside the
tracked set. The worked example is `pipeline/unreal/graphs/ABP_ElysiumBiped.t3d`.

Whatever was proven live is proven again through `uv run elysium build` and `uv run elysium test`,
which remain the only gate.

### Coordinates are read verbatim

The Source→Unreal math lives once in `pipeline/src/elysium_pipeline/formats/bsp.py`
(`source_to_unreal` for positions, `source_dir_to_unreal` for directions; the Y negation is a
reflection, so the exporter reverses winding at OBJ-write time). Never inline it, and never convert
at runtime. Full rules: `docs/project/rebuild-strategy.md` → "Coordinate conventions".

### The `UE_` exporter convention

An exporter prefixed **`UE_`** is verified to emit **Unreal-native** output: centimetres, Z-up,
left-handed, triangle winding pre-reversed — so the C++ runtime reads every file 1:1 with **no
coordinate conversion**. Every tracked coordinate-bearing OBJ/sidecar exporter is Unreal-native; do
not add a coordinate exporter without the prefix. Format parsers and orchestration modules are not
coordinate exporters. Exemptions and per-product detail: `pipeline/CLAUDE.md`.

### Poses are baked native — a VtMB rule never reaches the frame path

The same rule as coordinates, applied to the **skeletal pose frame**, which is a coordinate
convention like any other. A baked animation asset is a **complete, self-describing,
Unreal-native local pose**: every track is parent-relative, an additive names its own base, and
stock Unreal nodes compose it. The runtime applies no VtMB rule.

A VtMB clip is not a pose — its meaning is completed by state stored outside the clip (the bone's
`Flags & 0x2`, the animation record's per-bone `weight` mask, the sequence's additive flags, and
whatever pose it is accumulated onto). **Where a clip's meaning depends on state the clip does not
contain, the bake resolves that state and writes the answer.** The decidable test: **if a clip has
to know a fact about VtMB in order to be evaluated, the bake failed.** "It cannot be baked" is a
claim about a *named* value the file does not carry; a value the file states elsewhere is a bake
input, not a runtime dependency. Where a frame genuinely is not recoverable, **change the
representation** until the rule is unnecessary — the way blend grids became `UBlendSpace` assets —
rather than adding a stage that carries it forward. A 2004 storage quirk is a defect fixed at bake,
not semantics to reproduce.

**One standing exemption**, different in kind: **axis interpolation** reads a *live* control-bone
orientation, so it is a rig rule like an IK node rather than a frame conversion. Design:
`docs/architecture/animation-architecture.md`.

### What belongs in a CLAUDE.md

A directory `CLAUDE.md` carries **where things live** and **hard-won gotchas** — a non-obvious trap
that fails silently and that a source read would not reliably surface. It does not restate design a
`docs/architecture/` file owns, it does not index what a grep answers, and it never carries a VtMB
**format or behaviour fact** (byte layouts, discovered engine rules); those belong to the `docs/`
topic file that owns them (`docs/CLAUDE.md` → "Ownership"). The source is the as-built record —
read it rather than paraphrasing it.

Each `AGENTS.md` in this repo is a byte copy of the `CLAUDE.md` beside it, refreshed by
`.claude/hooks/agents-md-sync.sh` on every write; editing one directly is refused. A directory
joins the mirror by having an `AGENTS.md` created once beside its `CLAUDE.md`.

Every `CLAUDE.md` states present-tense facts only. **Never** write a roadmap task-ID parenthetical
(`(11.9)`, `roadmap 8.6`, `(PL13)`), a date, or a change/migration narrative ("was X, now Y") into
one — task tracking lives only in the master roadmap and its three declared subtrackers. Cite a doc
by name, with no date or task number attached.

## Build & run (Windows)

Copy `dev/paths.example.env` to `.elysium.local.env`, configure the UE, VtMB and work roots, then
use `uv run elysium` as the only public command surface. `uv run elysium --help` is the live list;
`uv run elysium doctor` checks repository policy, local paths, dependency ownership and generated
prerequisites.

The running game receives `-ElysiumContentRoot` and reads the export corpus from disk. It never
runs the offline Python pipeline. Generated reports and captures remain below `ELYSIUM_WORK_ROOT`.

## Documentation index

`docs/index.yaml` is the ownership and kind index, over `project/` (roadmaps, strategy, direction
charter), `architecture/` (Unreal system designs), `vtmb/` (engine-neutral VtMB formats and
behaviour), `recovered/` (explicitly uncertain reconstructions) and `operations/` (repository,
build, research and Git procedures). Ghidra projects, decompilation, dumps, captures, third-party
source and generated evidence live outside the checkout under `ELYSIUM_WORK_ROOT`.
