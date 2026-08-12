# Elysium-Unreal

*Vampire: The Masquerade – Bloodlines* (VtMB, 2004, early Source engine) rebuilt as a
playable game — **remastered** — on **Unreal Engine 5.8 + C++**. Every game-derived runtime input
is produced by this repo's own offline decode/export pipeline from the user's own install: the
world's *look* and every NPC character are baked into a gitignored `.uasset` plugin mount and
adopted/loaded at load, while collision, entities, scripting and audio are built in code at
map-load time from engine-neutral intermediates. Original project-owned remaster assets may be authored in Unreal
under the repository's explicit authored-content namespace.

## Read first

- **`docs/project/roadmap.md`** — the master work tracker: the playable-path ladder
  (PP0–PP6, the master sequence), phases P0–P13, project roll-up status, pipeline + RE backlogs,
  and risk register.
- **`docs/project/retail-capture-roadmap.md`** — a scoped subtracker: detailed status for
  the retail capture harness and original-runtime animation/facial investigation.
- **`docs/project/animation-roadmap.md`** — a scoped subtracker: detailed status for the skeletal
  animation asset programme — the character asset bake, the shared skeleton, layer masks, blend
  spaces, and the action catalog.
- **`docs/project/three-cs-roadmap.md`** — a scoped subtracker: detailed status for the player-feel
  vertical — Character, Camera and Controls — the mover's published body state, the resolver seam,
  the player animation graph, the camera service and rig, input response, and the gym that
  measures them.

  Those three are the only scoped subtrackers. The master roadmap owns their roll-up and priority.
  Status lives in the four trackers and nowhere else, including this file. There is no as-built
  archive and no decision log; git history is the as-built record, and a decision's outcome is a
  present-tense fact in the doc that owns the system.
- **`docs/project/rebuild-strategy.md`** — the strategy reference: north star, principles, the two
  tracks, sidecar contracts, per-system design targets.
- **`docs/project/remaster-direction.md`** — the direction charter: what may be modernized, what must
  be reproduced, who decides.

Directory-scoped facts live in sub-files that load with the code they describe:

| File | Covers |
|---|---|
| `Source/ElysiumUE/CLAUDE.md` | the C++ runtime — module/plugin list, folder → layer map, build/test loop, gotchas |
| `pipeline/CLAUDE.md` | the offline Python pipeline — boundaries, paths, formats and exporters |
| `research/CLAUDE.md` | reproducible research cases and tooling |
| `docs/CLAUDE.md` | how the documentation set is organised and maintained |
| `Content/CLAUDE.md` | licensed source fonts, project-authored assets, and generated local package policy |

## Load-bearing rules

### Remaster direction

Keep VtMB's tone, ambience, feel and logic; raise the craft. **Not** a pixel-perfect
recreation. Three change layers, three rules:

- **Presentation** (UI, type, HUD, textures, post) — modernize freely, adjudicated by *does it
  serve VtMB's grimy gothic-punk direction (or fix a technical deficit), or invent/override an
  artist decision?* The **UI has no classic mode**: VtMB's screen structure is kept and
  re-skinned with vector type on a resolution-independent Slate/UMG stack — no VGUI port, no
  640×480 canvas, no `.fnt` bitmap atlas at runtime.
- **Feel** (movement, camera, combat) — build the RE'd original first, keep it A/B-able, polish
  one delta at a time by explicit owner call.
- **Logic & content** (entity semantics, I/O, scripts, dialogue, stats, saves) — reproduce.

**The governing rule: only change what we understand, and only on an explicit owner call.** RE
comes first; a behavioural divergence needs the faithful behaviour known and recorded **in the doc
that owns the system**, stated beside the divergence and marked as one. Default resolves to
reproduce. The **world** keeps its
faithful baseline (lightmap calibration, plus the planned `elysium.EnhancedTextures` A/B
toggle); only the UI drops its.
Full charter: `docs/project/remaster-direction.md`.

### Bring-your-own-game

**Nothing game-sourced is committed.** The decoders read *the user's own VtMB install*; their
output (`$ELYSIUM_EXPORT_ROOT/`) is gitignored and regenerable, and so is everything derived from it —
including the baked `.uasset` mount `Plugins/ElysiumBaked/Content/` (only the `.uplugin` is
committed). This is the legal posture, not a convenience — prior community rebuilds died to a
C&D, not to technical failure. Tracked `Content/` inputs are licensed loose fonts and original
project-owned Unreal packages below `Content/ElysiumAuthored/`; the latter use Git LFS and must not
contain bytes, transforms, timing, or other content derived from the user's game. Generated
`/Game/Elysium`, `/Game/VtMB/**`, and `/ElysiumBaked/**` packages remain local and ignored.

### The two clean halves

- **Offline — `pipeline/`** (Python): decodes VtMB's proprietary formats (BSP v17, MDL v2531,
  TTH/TTZ, VPK, VMT, `.fnt`, `.res`) into intermediates under `$ELYSIUM_EXPORT_ROOT/<map>/`
  (OBJ+MTL+PNG/DDS, glTF `.glb`, plain-text/JSON sidecars). `UE_bsp_to_scene.py` is the map
  exporter; `uv run elysium export` coordinates map and whole-game profiles. Its Python package,
  dependencies, and command entrypoint are declared in `pyproject.toml` and locked by `uv.lock`.
  The same export command invokes `pipeline/unreal/bake_map.py` to turn each exported map's
  *look* into real assets and a `.umap` on the `/ElysiumBaked` mount, and `pipeline/unreal/bake_characters.py`
  to bake every NPC body and its clips onto the same mount. Both stages are editor commandlets, so
  an editor build is a prerequisite for export, never for running.
- **Runtime — `Source/ElysiumUE/`** (C++): opens the baked level and **adopts** its actors
  (bucketed by the tags the bake stamped), loads each NPC's baked `USkeletalMesh` and clips off the
  same mount, then builds everything else in code from the intermediates on disk — collision,
  ropes, the sky cubemap, the entity substrate, audio, scripting. Light values are re-derived from
  `.lights` at load rather than adopted, so live calibration always wins. Python is **never** run at
  runtime to produce content — the seam is file-based. (The embedded CPython 2.7 VM runs VtMB's
  *own* level scripts; it is game logic, not pipeline.) The architecture and what it costs:
  `docs/architecture/uasset-bake-spike.md`.

### Authored live, captured as text, rebuilt by a generator

The editor is the authoring tool for anything Unreal authors better than code — an animation
graph, a material, a widget, a Niagara system. What it produces is **never** the tracked
artifact. Every such asset is captured into a **reviewable text source** under `pipeline/unreal/`,
and a generator rebuilds the package from that text.

Live editing is the loop; the text is the record. A hand-edited binary package committed as-is
breaks `reconstruct`, is unreviewable in a diff, and puts a generated package inside the tracked
set — which is the boundary "Bring-your-own-game" exists to hold.

The worked example is the player animation graph: `elysium.animbp.build` constructs it through
the engine's own node-placement path, `UElysiumAnimGraphLibrary::ExportGraphToText` captures it as
`pipeline/unreal/graphs/ABP_ElysiumBiped.t3d`, and `pipeline/unreal/make_player_anim_bp.py` rebuilds
the asset from that text. The round trip is the authoring loop: open the generated asset, edit it in
the editor, copy the graph, paste it back over the text.

An editor MCP toolset drives all of it in-process (`docs/architecture/debug-tooling.md`). Whatever
was proven live is proven again through `uv run elysium build` and `uv run elysium test`, which
remain the only gate.

### The `UE_` exporter convention

An exporter prefixed **`UE_`** (e.g. `UE_bsp_to_scene.py`) is verified to emit
**Unreal-native** output: centimetres, Z-up, left-handed, triangle winding pre-reversed —
so the C++ runtime reads every file 1:1 with **no coordinate conversion**. Every tracked
coordinate-bearing OBJ/sidecar exporter is Unreal-native; do not add a coordinate exporter
without the prefix. Format parsers and orchestration modules are not coordinate exporters.
`mdl_gltf.py` is the one standing exemption: it writes self-describing standard glTF 2.0 as an
inspection product, and nothing the game loads comes off it. Details: `pipeline/CLAUDE.md`.

### Coordinates are read verbatim

The Source→Unreal math lives once in `pipeline/src/elysium_pipeline/formats/bsp.py` (`source_to_unreal` for positions,
`source_dir_to_unreal` for directions; the Y negation is a reflection, so the exporter
reverses winding at OBJ-write time). Never inline it, and never convert at runtime. Full
rules: `docs/project/rebuild-strategy.md` → "Coordinate conventions".

### Poses are baked native — a VtMB rule never reaches the frame path

The same rule as coordinates, applied to the **skeletal pose frame**, which is a coordinate
convention like any other. A baked animation asset is a **complete, self-describing,
Unreal-native local pose**: every track is parent-relative, an additive names its own base,
and stock Unreal nodes compose it. The runtime applies no VtMB rule.

A VtMB clip is not a pose. It is a set of channels whose meaning is completed by state stored
outside the clip — the bone's `Flags & 0x2`, the animation record's per-bone `weight` mask, the
sequence's additive flags, and at runtime whatever pose it is accumulated onto. **Where a clip's
meaning depends on state the clip does not contain, the bake resolves that state and writes the
answer.** It never forwards the question to the frame path.

The decidable test: **if a clip has to know a fact about VtMB in order to be evaluated, the bake
failed.** "It cannot be baked" is a claim about a *named* value the file does not carry, and it
has to name it; a value the file states elsewhere — an additive's base clip, a bone's ancestor
chain — is a bake input, not a runtime dependency. Where a frame genuinely is not recoverable,
**change the representation** until the rule is unnecessary, the way blend grids became
`UBlendSpace` assets, rather than adding a stage that carries it forward.

A 2004 storage quirk is a **defect fixed at bake**, not semantics to reproduce
(`docs/project/remaster-direction.md` → Behaviour test). VtMB's own inverse binds are
conventional FK, so the authored pose is ordinary and recoverable; only the file's storage frame
ever disagreed.

**One standing exemption**, and it is different in kind: **axis interpolation** reads a *live*
control-bone orientation, so it is a rig rule like an IK node rather than a frame conversion.
The design is `docs/architecture/animation-architecture.md`.

### Docs describe design, RE, and status — not the current build

Documentation exists for what the code cannot say for itself: design intent, VtMB
reverse-engineering facts, and status in the master roadmap plus its three declared
subtrackers. The source is the as-built record — read
it rather than paraphrasing it. The five directory orientation `CLAUDE.md` files (`Source/ElysiumUE/
CLAUDE.md`, `pipeline/CLAUDE.md`, `research/CLAUDE.md`, `docs/CLAUDE.md`, `Content/CLAUDE.md`) point at *where* something
lives — module/plugin list, folder → layer map, build/test commands — never *how* the current
implementation behaves. The one exception is a hard-won gotcha: a non-obvious trap (lazy-init
order, a silent side effect, an easy-to-undo fix) that a source read would not reliably surface on
its own. A VtMB **format or behaviour fact** (byte layouts, discovered engine rules) belongs in
the `docs/` topic file that owns it (`docs/CLAUDE.md` → "Ownership") — **never**
in a `CLAUDE.md`, including `pipeline/CLAUDE.md`, no matter how much it reads like "how something
works," because it describes VtMB, not our own implementation.

### CLAUDE.md carries no history

Every `CLAUDE.md` in this repo — this file and its four sub-files (`Source/ElysiumUE/CLAUDE.md`,
`pipeline/CLAUDE.md`, `research/CLAUDE.md`, `docs/CLAUDE.md`, `Content/CLAUDE.md`) — states present-tense facts only, same
as every other doc (`docs/CLAUDE.md` → "House rules"). **Never** write a roadmap task-ID
parenthetical (`(11.9)`, `roadmap 8.6`, `(PL13)`), a date, or a change/migration narrative
("was X, now Y") into any of them — task tracking lives only in the master roadmap and its three
declared subtrackers. Cite a doc by name, with no date or task number attached.

## What runs today

**Project priority and roll-up status: `docs/project/roadmap.md`; detailed retail-capture
status: `docs/project/retail-capture-roadmap.md`; detailed skeletal-animation status:
`docs/project/animation-roadmap.md`; detailed Character/Camera/Controls status:
`docs/project/three-cs-roadmap.md`.** Runtime types and where they live:
`Source/ElysiumUE/CLAUDE.md`.

## Target hardware

- **Minimum floor:** NVIDIA **RTX 4060-class** desktop GPU with **16 GB**, **1440p native** — the
  shipped config targets this floor. **Recommended:** RTX 4070/5070-class.
- **DX12/SM6 is mandatory.** The render path is fully dynamic — HWRT Lumen, MegaLights, VSM
  (`Config/DefaultEngine.ini`). Every one of those silently disables under DX11/SM5 (no error,
  just a CPU-bound slideshow). The window title must read `PCD3D_SM6`. There is no non-RT
  fallback, so a DXR-capable GPU is required.
- **Lumen GI is load-bearing, not optional.** VtMB's look is indirect-bounce-dominated, so the
  bounce carries it; geometry is trivial (~20k tris/map) and MegaLights keeps hundreds of
  dynamic lights ~constant-cost.

Tuning, the SM6 setup, the MegaLights-engagement checklist, the floor budget, and the
calibration findings: **`docs/architecture/rendering-perf.md`**.

## Build & run (Windows)

Copy `dev/paths.example.env` to `.elysium.local.env`, configure the UE, VtMB and work roots,
then use `uv run elysium` as the only public command surface.

- `reconstruct [--clean] [--rebuild]` restores the complete project from its declared inputs.
- `deps sync|check` restores or verifies pinned external plugins and fetched SDKs.
- `doctor` checks repository policy, local paths, dependency ownership and generated prerequisites.
- `lane create|dispatch|status|mark` owns detached, generated-state-isolated QA worktrees.
- `build [--rebuild|--clean|--analyze]` drives UnrealBuildTool.
- `export grid|all` runs a complete profile; `export map|model|bundle` handles focused work.
- Export generates the required `/Game/Elysium`, `/Game/VtMB/**`, and
  `/ElysiumBaked/<map>/**` packages unless an explicit intermediate-only mode is selected.
- `test [filter]` runs the `Substrate`, `Content`, or fully qualified automation tier.
- `run editor|play` and `debug profile|probe|shots|move|greenroom|modelroom` expose the Unreal
  development and acceptance harnesses.
- `research <case>`, `ide vscode`, and `mcp` expose research, IDE and control tooling.

The running game receives `-ElysiumContentRoot` and reads the export corpus from disk. It never
runs the offline Python pipeline. Generated reports and captures remain below `ELYSIUM_WORK_ROOT`.
## Git workflow

Solo-dev project on GitHub (`jlagedo/elysium-unreal`, private). Work lands as commits
directly on `main` — do not create feature branches, pull requests, or merge requests
unless explicitly asked.

## Documentation index

`docs/index.yaml` is the ownership and kind index. The documentation set is divided into:

- `docs/project/` — the master and scoped roadmaps, strategy, and direction charter;
- `docs/architecture/` — Unreal system designs;
- `docs/vtmb/` — engine-neutral VtMB formats and behavior;
- `docs/recovered/` — explicitly uncertain reconstructions;
- `docs/operations/` — repository, build, research and Git procedures.

`research/CLAUDE.md` documents tracked research specifications and instruments. Ghidra projects,
decompilation, dumps, captures, third-party source and generated evidence live outside the checkout
under `ELYSIUM_WORK_ROOT`.
