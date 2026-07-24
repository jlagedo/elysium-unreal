# Decision log (append-only)

The project's one decision log, referenced from `roadmap.md` (the work tracker). Record a
decision when it is made, dated, newest first — including "pending" decisions with their
trigger. A behavioural divergence from retail lands here carrying both the faithful and the
chosen behaviour (`remaster-direction.md`'s governing rule). Entries are never rewritten —
append a correction as a new entry.

- **2026-07-24** — **8.3: dynamic props render per-entity; orientation converts at export; Skin/anim are
  deferred stubs.** Three owner calls landed with the `prop_dynamic` render path. **(1) Per-entity
  `UStaticMeshComponent`, not shared ISMs.** The roadmap text ("the ISM path grows per-instance
  addressability per `entity_visuals.md` R2") pointed at the Godot MultiMesh design, but every runtime
  writer on a prop is per-entity (hide = `SetVisibility`, move = `SetRelativeLocationAndRotation`,
  model-swap = destroy+rebuild), so an individual component per prop sharing a per-stem cached
  `UStaticMesh` is the lower-risk fit; geometry is trivial (~20k tris/map, the perf constraint is
  lights), so the extra components are negligible. GAME_LUMP static props keep their grouped-ISM
  `.props` path (no identity, no double-draw). **(2) `model_quat` converts at export, read verbatim.**
  `.ents` left `angles` as a raw Source string (only `origin` was pre-converted), and props need full
  3-axis orientation; rather than duplicate the coordinate math at runtime, `write_entities` now emits
  `model_quat = source_angles_to_unreal_quat(angles)` (the same function the `.props` path uses) for
  every `model_mesh` entity, and the runtime reads it 1:1 — honouring the load-bearing "convert at
  export, never at runtime" rule. Absent → identity (older exports still load). **(3) `Skin` /
  `SetAnimation` are logged stubs.** Faithful `prop_dynamic` selects an alternate skin family / plays a
  skeletal sequence; the prop decode is LOD0 static geometry, skin 0 only (no alternate skins or skeleton
  exported), so these can only record the request until a skin-family / skeletal-prop export exists (a
  tracked follow-up). `Break` (hide + `OnBreak`) and the base ScriptHide/Unhide + 9.3
  SetOrigin/SetAngles/SetModel body-follow are real; props are non-solid (collision is 8.4).
  **Incidental fix (out of 8.3 scope, recorded):** the re-export tripped a latent working-tree bug in
  `UE_bsp_to_scene.py` — the decal-material branch wrote a 4-tuple into `mat_info` where the `.mtl`
  writer and the world-material branch use a 5-tuple `(albedo, emis, alphatest, translucent, additive)`,
  so a map with a decal material crashed the `.mtl` write mid-file. Fixed by appending the missing
  `additive=False` (water/decal is never additive); the tutorial `.mtl` regenerated whole (2476 lines).
  Verified: `build.bat` + `test.bat Content`/`Substrate` green; `elysium.PropBodies` A/Bs the bodies.
- **2026-07-24** — **9.3: the unblocked entity-manipulation surface landed; the Character-method fill
  is delegated to its backing systems.** The four `Entity` writers are real (runtime `Origin` +
  body-follow hooks move/re-face/re-skin the NPC body; `RenameEntity` re-keys the name index), the
  scripted two-phase spawn is real (`SpawnRuntimeEntity` split into `CreateRuntimeEntityNoSpawn` +
  `CallEntitySpawn`, guarded by `bSpawnCalled`), and the field-table audit registered the one genuine
  gap (`npc.times_talked`, read-only, resolves to 0 until B4). **Owner call (scope):** the 22 unbacked
  Character methods stay fail-closed stubs rather than growing a throwaway backing — inventory
  (~280 corpus calls, the largest demand) is a **tracked follow-up**, feats/stats are 9.4,
  disposition/camera/barter are B-track. **Honesty caveat, recorded:** `CreateEntityNoSpawn` on a
  leafless class (`item_*`, `prop_*`) yields a logic-valid but **bodiless** entity (findable, I/O-wired,
  no mesh) until a per-entity prop/item render path lands; NPCs get visible bodies. Verified: `build.bat`
  green; `Elysium.Substrate.RuntimeSpawn` (two-phase create + rename + runtime origin), the
  `times_talked` field check, and **`Elysium.Substrate.CPythonWriters`** — which drives the writers +
  two-phase spawn through the **real embedded-CPython glue** (`SetName` re-key, `SetOrigin((x,y,z))`,
  `SetModel`/`GetModelName` round-trip, `CreateEntityNoSpawn`→`CallEntitySpawn`) — all pass; and a live
  `sp_tutorial_1` load confirmed CPython + `tutorial.py` import + world build healthy with no errors.
- **2026-07-24** — **7.4 $envmap is a Lumen roughness channel, not a baked-cube sample.** Owner call.
  VtMB's DX8 `$envmap` world path sampled a baked cubemap (`reflect(v,n)` → mask → tint → additive).
  The remaster's render path is fully dynamic (HWRT Lumen), and 7.5 is "Real reflections — Lumen", so
  `M_World_Opaque` does **not** sample the exported `tex/cube/` faces: instead the `$envmapmask` (or a
  uniform white mask when a reflective surface carries none) drives an `EnvStrength`-scaled drop of
  Roughness from the calibrated 0.5 toward 0.15, and Lumen produces the reflection. Base roughness is
  held at exactly the value the old unconnected pin defaulted to (0.5), so non-reflective surfaces keep
  their calibrated look; `envtint`/`envmapcontrast`/`envmapsaturation` are parsed offline but unused by
  the world graph (DX8 artifacts). The cube faces stay exported for the 2D sky. 7.5 tunes reflectivity.
  On `sp_tutorial_1`: 240 of 439 world materials are reflective. `elysium.EnvReflect 0` A/Bs the whole
  path off (matte).
- **2026-07-24** — **The dev boot path seeds `Linux_Wine=1` to suppress `popup_linux`.** The tutorial's
  `linux_check` (`logic_pythoncheck`, `python_script "G.Linux_Wine == 1"`) fires `OnFalse ->
  popup_linux.OpenWindow` — the Unofficial Patch's "an important Python script has not compiled
  correctly / you are in a Linux Wine environment, run Loader.exe" warning — whenever `G.Linux_Wine`
  is not 1 when `logic_auto.OnMapLoad -> linux_check.Test` fires (t=0.1). `Linux_Wine=1` is the patch's
  "Python works" sentinel (set by `vamputil.setBasic`/`setPlus`); `BeginNewGame` already seeds it, but
  the **bare `-ElysiumMap` dev path** (`play.bat`/`profile.bat`) skips `BeginNewGame`, so the check read
  `OnFalse` and the popup fired. Our embedded CPython always runs, so the warning is a false alarm on
  that path — `AElysiumGameMode::BeginPlay` now seeds `Linux_Wine=1` before the bare `Travel`, so
  `linux_check` reads `OnTrue`. (The value is a *suppressor*: setting it to 0 would *show* the popup, on
  New Game too.) Verified headless: no `popup_linux.OpenWindow` delivery on a bare `sp_tutorial_1` load.
- **2026-07-24** — **B3 minimal NPC presence: real glTF bodies for all `npc_*`, and runtime entity
  spawn.** Owner call (four forks). (1) NPCs stand their **real** `out/npc/<stem>.glb` skeletal body
  (not a placeholder) — PL4 already emitted the glbs and 8.2 the loader, so the faithful result is the
  cheap one; the model→stem map is the lowercased basename of the `.mdl` key, verified 1:1 for every
  tutorial NPC, so no `npc_manifest.json` lookup. (2) **All** `npc_*` with a model get a body at load
  (cached per stem, gated by `elysium.NpcBodies`), not just the beat's two — "stands the model at its
  origin" applies to every character. (3) `StartPlayerDialogRemote` fires `OnDialogBegin` only and leaves
  the session open; a manual **`EndDialog`** input (fireable via `ent_fire`) fires `OnDialogEnd`, keeping
  B3/B4 cleanly split until B4's `.dlg` runner replaces the manual close. (4) `npc_maker.Spawn` spawns
  exactly one child and **ignores `Flag_StartDisabled`** (retail fires `blueblood_maker.Spawn` without
  enabling it) — `SpawnFrequency`/`MaxLiveChildren`/`MaxNPCCount` are unmodelled (No AI). **Substrate
  consequence:** the entity world gained `SpawnRuntimeEntity` — a runtime-synthesized def stored past the
  map's immutable def array, appended to `EntityList` (Resolve indexes it directly, so identity needs only
  the append; `ResolveTargets` copies target pointers before firing, so a maker spawning mid-delivery is
  safe). The NPC skeletal body is a `USkeletalMeshComponent` on the map actor (not a separate actor),
  torn down with the world like the brush bodies. Facing uses the entity `angles` yaw (negated for the
  Source→Unreal Y reflection); exact facing is cosmetic and left to a later feel pass. **Feeds 8.5**
  (→ `[~]`); its `scripted_sequence` anim-at-marker and bank retargeting stay open.
- **2026-07-24** — **7.2 decals: deferred `UDecalComponent` chosen; the PMC-parity stage skipped.**
  Owner call. The task was framed as two stages (translucent PMC mesh for parity → `UDecalComponent`
  later, decide after both render); we built only the deferred path. A deferred decal writes the
  GBuffer *before* the lighting pass, so it is lit exactly like its host wall — **Lumen indirect
  bounce included** — which VtMB's bounce-dominated look needs and the PMC translucent path cannot
  give. Building the endgame path once avoids maintaining a throwaway. **Pipeline consequence:** the
  exporter no longer meshes decals — the `infodecal` projection now emits a `<map>.decals` projector
  sidecar (material + centre + room-normal + s/t axes + half-extents, Unreal cm) instead of
  `_decals.obj`; the runtime builds one `UDecalComponent` per line off a new `M_Decal` deferred
  master (regular `BLEND_Translucent` — the old `DecalBlendMode` is deprecated/no-op since UE 5.2).
  `M_Decal` is authored here even though it is listed under 7.4's master-material set, because the
  decal path needs it now; 7.4 still owns the rest (`M_World_*`, water, etc.). **Orientation took two
  capture passes to settle** (the API docs don't spell out the decal UV frame): a deferred decal maps
  texture **U→local Z, V→local Y** (not the intuitive U→Y), so `BuildDecals` uses
  `MakeFromXZ(Normal, SDir)` with the surface horizontal `SDir` on local Z and
  `DecalSize = depth×HalfH×HalfW`; and because V (local +Y) points up while VtMB authors V top-down,
  `M_Decal` samples at `(U, 1-V)` to un-flip vertically. The first attempt (`TDir` on Z) rendered
  decals rotated 90° + stretched; the second was upright but upside-down. `elysium.DecalFlipU` remains
  a horizontal-mirror knob. A/B via `elysium.Decals`; depth via `elysium.DecalDepth`. **Screenshot
  tooling:** `AElysiumHUD::DrawSignPanel` now gates on `elysium.DrawSigns` (the shot harness sets it 0
  so a map-load `game_sign` popup — the tutorial's `linux_check` Python-compile warning — does not
  cover every plate), and `shots.bat` renders **off-screen** (`-RenderOffScreen -ForceRes`), so no
  game window opens.
- **2026-07-24** — **PL4 done: shared-bank NPC format over the per-NPC monolith.** Include-model
  resolution cracked from data + VAMPTools: `NumIncludeModels`@404 / `IncludeModelIndex`@408 →
  `StudioModelGroup[]` (**stride 116**, not the naive 8 — `int Filler[27]` after the two index
  fields; `FilenameIndex`@0 relative to the group-entry base). The include tree is a recursive DAG
  (`frenzy`/`pc_idles` reappear), resolved cycle-deduped. Every bank bone name is present in the
  NPC skeleton (`move_and_ranged` 60 / `stances` 53 bones, 0 missing in a 69-bone gangmember), so
  clips retarget by **bone name** with no proportion rig. **Format decision (informed by measured
  cost):** baking the full include tree into each NPC glb measured **~94 MB / ~90k accessors per
  NPC** (81 MB anim payload + 13 MB JSON) → ~4.2 GB for 45 NPCs and ~1 GB resident on a busy map.
  Rejected. Adopted the shared-bank decomposition instead — which is also how modern engines and
  VtMB's own `virtualmodel` handle it: `out/npc/<npc>.glb` (mesh + skeleton + own clips),
  `out/npc/banks/<bank>.glb` (skeleton + a shared bank's clips, no mesh, decoded once),
  `npc_manifest.json` (`clip → owning-stem`). The runtime (8.5) loads a bank once and applies its
  clips to any NPC skeletal mesh by bone name via glTFRuntime — verified in the vendored plugin
  source (`LoadSkeletalAnimationFromTracksAndMorphTargets` binds tracks to the ref skeleton by
  `FindBoneIndex(BoneName)`, and `LoadSkeletalAnimation(mesh, …)` takes an external mesh). Full
  cast: **45 NPCs / 62 banks / ~410 MB** (243 MB shared banks + 149 MB meshes + 31 MB textures) in
  ~3.5 min, vs 4.2 GB. Bank stems keep the sub-path so male/female (and clan) banks that share a
  basename stay distinct. Pipeline: `mdl_skel.resolve_tree`/`local_sequences`,
  `mdl_gltf.export_npc`/`export_bank`, `npc_export.py` (`export_all.py --npc`). Runtime consumption
  is 8.5. *Feeds:* 8.5.
- **2026-07-24** — **Brush touch requires a pawn toucher, and waits until the pawn is seated.**
  Bug fix. `elysium.newgame` (or any travel) *from an already-loaded map* warped the player off
  the tutorial porch into the downtown alley and looked like the spawn "moving to the next spawn".
  Cause: two unrelated things collided. (1) Every brush body on a map is a component of the one
  `AElysiumMapActor`, so when the new map builds its bodies, Unreal fires begin/end overlap for
  every trigger∩trigger and trigger∩solid pair at once — and `UElysiumBrushComponent::RouteTouch`
  forwarded *all* of them as touches, since it never checked who was touching. (2) The pawn carries
  over from the previous map and is placed at the new map's spawn only on the first tick *after*
  the entity world exists, so for a moment it stands wherever the old map left it. Together, the
  phantom geometry-overlap touches ran real scripted beats — `trig_feed_fix.OnStartTouch ->
  fix_fade.Fade -> teleport_player.Teleport` — and teleported the player. Fix: `RouteTouch` now
  routes only when the overlapping actor `IsA<APawn>` (VtMB never treats geometry∩geometry as a
  touch — only movers touch), and only once `AElysiumMapActor::IsPlayerSeated()` is true (the pawn
  has been moved to this map's info_player_start/landmark, or this map requested no placement).
  Verified: `elysium.newgame` from a loaded `sp_tutorial_1` now produces zero `teleport_player`
  deliveries, zero `fix_fade`/`trig_feed_fix` fires, and exactly one legitimate touch — the player
  genuinely standing in the (inert, StartDisabled-on-arrival) `trig_theater_to_tutorial` changelevel
  volume at the porch, which fires nothing. Do not remove either guard. When NPC pawns (8.5) and
  physics props (8.4) can trip triggers, widen the `APawn` test rather than dropping it.
- **2026-07-24** — **Agentic QA is Layer 3 of the debug architecture, not a new track (P2.7–2.9).**
  `debug-tooling.md` already framed the `elysium.*` console verbs as "the thin scriptable layer for
  `-ExecCmds` automation and headless runs" — the third consumer beside the human (Cog) and the
  script. An AI agent is that third consumer made first-class: the same runtime state, reached
  through structured MCP tools instead of parsed console text. Owner call — chosen shape and why:
  (1) **Direct `AddTool()` registration, not the Toolset-Registry adapter** — the adapter is
  editor-only, but this project's whole loop is `-game`/cooked with no editor content loop, so tools
  register through `IModelContextProtocolModule::AddTool()`, which serves in every target. (2)
  **Tools live inside `ElysiumUE`, not a separate module** — `FElysiumEntityWorld` and the substrate
  carry no `ELYSIUMUE_API` exports (plain C++, Unreal supplies bodies only), so a sibling module
  couldn't link them; the MCP layer follows the vendored-Cog precedent (non-Shipping/editor private
  dep). (3) **On-by-default in dev, loopback + no-auth** — the server auto-starts wherever the plugin
  is present (editor target only, so never in Shipping/Test), because a QA surface you must remember
  to enable is one you forget; `-NoElysiumMcp` opts out. (4) **Two-tier tests** — a `-nullrhi` content-free suite that is the real
  regression net (substrate paths), plus a content-gated suite that self-skips on an empty
  `tools/out` so a fresh checkout stays green; the LLM is never the oracle — it *drives* Unreal's own
  automation runner. (5) **Screenshots share the profiler's vantages** (`ElysiumVantages.h`) so a
  look regression and a cost regression are the same frame; baselines are game-derived → gitignored.
  Industry survey that informed this (Epic's in-box `ModelContextProtocol` plugin, the community
  `unreal-mcp` servers, Gauntlet vs. functional-test guidance, the fire→screenshot→assert playtest
  loop): the editor-centric MCP toolsets buy little here, so only `AutomationTestToolset` +
  `LiveCodingToolset` are enabled beside ours; Gauntlet is deferred (single platform, no net
  sessions). Full design: `debug-tooling.md` Layer 3.
- **2026-07-24** — **Field-6 payloads evaluate in `__main__`, not the level module (B2 revises
  9.3a).** RE first. `G`'s type object (`PyDataManager`, `0x1058fa08`) carries a `tp_as_mapping`
  (`0x1058f9f8`) whose `mp_subscript` (`0x1019b4a0`) literally **tail-jumps into `tp_getattr`**
  after `PyString_AsString`, and whose `mp_ass_subscript` (`0x1019b720`) calls `tp_setattr` the
  same way — so `G[k]` **is** `G.k`, default-on-miss 0 and all. Reproduced, because
  `DialogPostProcess()` calls `saveState()` (`for k in G.keys(): G_tut[k] = G[k]`) on its first
  line and would otherwise never reach the beat branch. Two knowing divergences on that surface:
  a **non-string key on assignment raises `TypeError`** where retail tail-calls `PyDict_SetItem`
  with the manager object in the dict slot (a latent bug no shipped script reaches — every G key
  is a string), and our proxy keeps a **`has_key` method** that retail's 2-entry table does not
  have (retail resolves `G.has_key` to a flag read of 0; no script calls it, and the method
  predates this task on the expr host's G surface too).
  The namespace change is the second half. 9.3a evaluated payloads in the **level module's** dict;
  VtMB wraps every payload as `__main__.%s` (`0x1055e370`), which resolves the leading name as an
  attribute of `__main__` — so the level script's own `def`s must be *in* `__main__`, and so must
  the engine globals. Measured over the 10 exported maps' 363 field-6 payloads, the module-dict
  choice breaks 2 of them: `hw_609_1` fires a bare `FindPlayer().ClearActiveDisciplines()` and
  `hollywood.py`, unlike `tutorial.py`, never aliases `FindPlayer` at module level. `LoadLevelScript`
  now merges the imported module's public top-level names into `__main__` and everything evaluates
  there. A function keeps its defining module's globals, so `DialogPostProcess` still reads its own
  `G_tut`/`Find`/`statemap`; only the entry-point lookup moved. `__main__.Level = __name__` is the
  cross-check that retail imports-then-merges rather than exec'ing into `__main__` — the assignment
  only carries information if `__name__` is the script's own module name.
  Third, smaller call: the bootstrap's `IsClan`/`IsIdling` stubs (vamputil's, not engine API — they
  exist only so `tutorial.py`'s import-time guard resolves) now return **0** instead of a truthy
  stub object. A truthy predicate made every clan gate take its *first* branch, i.e. silently play
  as Brujah; 0 falls to the `else`, which is the honest "no clan matched". Real behaviour arrives
  with the real `vamputil` in 9.3b/B5.
- **2026-07-23** — **B1 landed, and `SF_FADE_STAYOUT` is the flag that brings the screen *back*.**
  RE first, over `vampire.dll` + `client.dll`. `CEnvFade::InputFade` (`0x10100F90`) maps
  spawnflags to the client's fade flags as `SF_FADE_IN`(0x1) → **0**, else `0x2` (`FFADE_OUT`)
  `| 0x20` when `SF_FADE_STAYOUT`(0x8); `SF_FADE_MODULATE`(0x2) → `|0x4`; `SF_FADE_ONLYONE`(0x4)
  sends to the activator alone. It then fires `OnBeginFade` at delay 0 and returns — no think, no
  second output. `CViewEffects::FadeCalculate` (`client.dll` `0x10197190`) is where the meaning of
  `0x20` lives: when a fade passes both `FadeEnd` and `FadeReset` it is normally **dropped**, but
  with `0x20` it instead flips to a fade-in (`flags &= ~0x22 | 0x1`), negates its speed and takes
  one more `duration` to uncover. The client's genuinely-permanent bit is `0x8` (which re-pushes
  `FadeReset` to `curtime + 0.1` every frame) and `env_fade` never sets it; `0x40` disconnects to
  the menu when the fade ends. **4.5 had this backwards** — it held `STAYOUT` covered forever and
  ramped the plain fade back — which would have left warp #2 on a black screen through Jack's
  dialogue. Corrected: cover over `duration`, hold `holdtime`, then uncover over `duration` only
  when `SF_FADE_STAYOUT`, else expire. That reproduces `teleport_fade`'s authored timing exactly
  (`duration 1`, `holdtime 2`, clear at t=4 — precisely when its `Jack.StartPlayerDialogRemote`
  wire fires). **`OnEndFade` and `ReverseFade` are not VtMB** (no such string in `vampire.dll`);
  the `ReverseFade` input is removed rather than kept as a debug affordance, so the Inspector's
  input list stays a faithful mirror of the datamap. `SF_FADE_IN`'s flag-0 path is reproduced
  as-is including its quirk — `FadeCalculate`'s `flags & 0x3` test fails, so the colour sits flat
  at full alpha for `holdtime + duration` and then snaps clear, with no ramp either way; no
  tutorial `env_fade` sets it. The fade stays **one slot** rather than VtMB's fade list: the list
  sums colours and maxes alphas, which is indistinguishable from one slot while every fade on a
  map is the same colour, and all seven on the tutorial are black.
- **2026-07-23** — **Inspection is click-to-select, not crosshair-follow (P2.6).** Owner call. The
  live crosshair inspector is removed: the Entity Inspector no longer traces the camera ray every
  frame. Instead, while the Cog menu owns the mouse, LMB over the world picks whatever is under the
  cursor and RMB clears — no pause (the world keeps running under the cursor; Time Scale stops it
  when wanted). The pick lives in `RenderTick`, which Cog runs for every window regardless of
  visibility, so it works with the inspector closed. Three things this settled:
  **(1) Physics cannot answer the pick, so two of the three sources are CPU ray-casts.** Under the
  default `elysium.BrushCollision 1` the world *render* mesh is built with collision off — the
  `.hulls` convex set is the collider, and it carries no material and no face, so a trace could
  never name the surface it hit (the old crosshair readout only worked at all under
  `elysium.BrushCollision 0`). Separately, a solid prop's cooked collision is a **single convex hull
  of the whole model**, and non-solid props have none. So `ElysiumPick::Trace` physics-traces only
  the entity bodies (`LineTraceMulti` on `ECC_Visibility`, which returns trigger overlaps too) and
  CPU-casts the prop instances and the world/sky sections against their real triangles. The map
  actor retains the geometry for it (`FPropPickSoup` per unique model, ~3 MB on the tutorial; a
  section → OBJ-group-key table), all `#if !UE_BUILD_SHIPPING`.
  **(2) A world pick highlights the BSP face, and that falls out of the exporter for free.**
  `UE_bsp_to_scene.py`'s `emit()` appends a fresh vertex per face corner (no dedup across faces) and
  `BuildMeshFromObj` remaps sections keyed on the *global* index, so the triangles of one face share
  local indices and adjacent faces share none. Flooding across shared edges therefore stops exactly
  at the face boundary — no position weld, no normal threshold, no bleeding around a corner. The
  coplanarity guard only bites on displacement grids, where one face is a whole curved patch. The
  flood costs an edge map over the section, so it runs on click; hover previews the single triangle.
  **(3) The highlight is imgui, not scene geometry.** Considered and rejected: custom-depth stencil
  outline (best silhouette, but cannot outline an invisible trigger volume and lights every instance
  of a prop model at once), a retained x-ray box (bounds-only, coarse on a world section), and a
  material tint/checkerboard on `M_VtMB_World` (cheapest code, worst granularity — the world OBJ is
  grouped per material, so it would highlight every face sharing that texture map-wide). Projected
  fill + outline + label is the only one exact for all three target kinds, needs no assets, and
  keeps drawing when the world is time-scaled to a stop, since Cog's render tick is not the game
  tick. Known bound: an entity's highlight is one box per def hull (the hulls are vertex sets with
  no faces) — exact for the axis-aligned box brushes nearly every trigger and door is made of.
  **(4) A World Viz gizmo marker outranks the geometry it is drawn over.** Owner call. The marker is
  a deliberate "select me" handle, and it is the *only* clickable representation the ~1,000 bodiless
  entities on a map have (on `sp_tutorial_1`, 394 of 1,868 records are `light`/`light_spot` alone —
  they are in the `.ents` lump as inert records, so they carry gizmos). Three sub-calls: **drawn is
  the whole test** — `Visible` depth-tests the cubes so one behind geometry does not pick, `All` is
  x-ray so any does, `Off` skips the source — which needs the nearest *rendered* hit tracked apart
  from the pick winner, since brush bodies render nothing and must not occlude a marker behind them;
  **the hit test is the exact 28 cm cube**, not a screen-space tolerance, so dense clusters stay
  separable at the cost of distant markers being small targets; and **clicking a cluster again
  cycles** to the next marker behind the current one, wrapping. With gizmos on, the bodiless-entity
  perpendicular fallback (2 m, inherited from the `ent_*` picker) is suppressed — it is far looser
  than the cube and would undo the precision — and stays armed only with gizmos off. The marker's
  anchor and size are consolidated into one definition (`ElysiumGizmoColor.h`) shared by the ISM
  layer, the label/beam overlays and the pick; they had been duplicated in two files, and the
  what-you-see-is-what-you-click rule only holds if they cannot drift.
  The `ent_fire` crosshair picker and the HUD's `+use` reticle are unaffected; they are separate
  systems.

- **2026-07-23** — **Direction: VtMB *remastered*, not a pixel-perfect recreation.** Owner call.
  Tone, ambience, feel and game logic are kept; craft is raised with tools 2004 did not have.
  Written up as `docs/remaster-direction.md` (the charter); `rebuild-strategy.md` principle 7
  and this doc's north star restated to match. Four decisions carry the weight:
  **(1) Three change layers, three rules.** *Presentation* (UI, type, HUD, textures, post) modernizes
  freely under the art-direction test, no approval gate. *Feel* (movement, camera, combat) is
  built faithful first, kept A/B-able, and polished one delta at a time by explicit call.
  *Logic and content* (entity semantics, I/O, scripts, dialogue, stats, saves) is reproduced.
  Layer assignment happens before the work, not after — the boundary is *game state*, not
  visibility.
  **(2) The governing rule: we only change what we understand, and only on an explicit call.**
  RE comes first — a behavioural divergence may only be *proposed* once the faithful behaviour
  is known and recorded, and it lands only with a dated owner decision in this log carrying both
  the faithful and the chosen behaviour. Default resolves to reproduce. This is why the RE
  backlog does not shrink under a remaster direction: you cannot judge what to keep until you
  know what is there.
  **(3) The UI drops its faithful path; the world keeps its.** The pixel-faithful VGUI port is
  **not built** — there is no classic UI mode. VtMB's screen structure (inventory, panel
  anatomy, reading order, palette, iconography, strings) is kept and **re-skinned**: vector/SDF
  type replacing the `.fnt` bitmap atlases, resolution-independent layout replacing the 640×480
  proportional canvas and the patch's `//ws-fix` pairs, restrained motion, gamepad-navigable
  components. The bitmap fonts are the loudest defect in the game on a modern display, and
  illegibility was hardware, not art direction. `m0_menu_build.md` and the `.res`/scheme/`.fnt`
  decoders become **reference and extraction machinery** (PL8), not a runtime layout stack. The
  world is untouched by this: geometry/placement/lighting stay anchored to VtMB's data and the
  lightmap calibration, with `elysium.EnhancedTextures` as the A/B toggle on top.
  **(4) Asset enhancement leaves the Options list and becomes scope** (remaster axis 2), with
  its tiers, its per-family curation and its A/B toggle unchanged, and its **P10 sequencing
  unchanged** — it is polish on a shipped look, not a blocker.
  Task deltas: P8 renamed *Characters & UI*; **8.6** recast from "VGUI menu port" to the UI
  foundation (design system + shell + menus + New Game); **8.8** recast from "sign window — VGUI
  fidelity" to sign/popup panels on that foundation (the `CSignUI` 1024×768 canvas model stays
  the *intent* reference, not the runtime coordinate system; the `Font_640`…`Font_1600`
  per-resolution overrides are dropped — vector type scales continuously); **8.9** (HUD on the
  foundation) and **8.10** (accessibility & options backing) added; **4.7** gains the
  faithful-first feel-layer note; **9.2** dialogue content verbatim, presentation modern;
  **PL8** added for the UI source inventory. Three risks registered: UI losing VtMB's voice,
  polish leaking into the logic layer, and having no classic mode to A/B against.

- **2026-07-23** — **9.3a: the level scripts are wired; CPython is the default host.** The 5.5 PoC
  proved the embed offline but nothing connected it to a map — `LoadLevelScript` was reachable only
  from `elysium.py.load` and the Cog button, and the map-load host was still ElysiumExpr. Three
  decisions closed that:
  **(1) The import happens off the parsed `.ents`, before the spawn pass** — not off the live entity
  world after it. VtMB runs the level script's top-level code first and spawns entities into that
  namespace; doing it after `FElysiumEntityWorld::Load` would leave a spawn-time field-6 payload
  evaluating against a module that does not exist yet. Hence `FElysiumEntityDefs::LevelScriptModule()`
  rather than a worldspawn walk over the built world.
  **(2) Importing is a host capability, and the module name is remembered.**
  `IElysiumScriptHost::LoadLevelScript` defaults to "this host cannot import", which the expr and null
  hosts inherit truthfully; `SetScriptHost` re-imports the remembered module into whatever host is
  installed next. Without that, `elysium.script.cpython 1` mid-map would install a CPython host with a
  bare `__main__` and every level constant would read NameError — an A/B that lies.
  **(3) A CPython VM that fails to start falls back to the expr host.** Its evals would all be Void,
  which is exactly what error-to-false looks like (RE3) — so a missing `python27.dll` in a packaged
  build would degrade the entire scripting surface **silently and plausibly**. `MakePreferredScriptHost`
  checks `IsUsable()` and warns.
  Routing `EvalScript` through the installed host came with it: the console verbs and the Cog eval box
  hard-wired ElysiumExpr, so with CPython live they would report NameError for names the game itself
  resolves — a debug surface contradicting the runtime. `IElysiumScriptHost::Eval` gains an optional
  `OutError` (a Void return cannot distinguish "evaluated to None" from "raised"), and the eval/exec
  split collapses to one path: the CPython host already tries `Py_eval_input` then `Py_file_input`, and
  `ElysiumExpr::Exec` returns its last statement's value.
  Verified in the built game, not offline: `sp_tutorial_1` imports `tutorial` at map load into host
  `cpython`; `elysium.eval cCelerity` = **8**; `G.Tutorial_Discflags |= cCelerity` flips `G` to **8**;
  `elysium.script.cpython 0` returns NameError; travel to `sm_pawnshop_1` re-imports per map.
  That last one surfaced the first real gap: `santamonica` raises `ImportError: cannot import name
  RandomLine` — the bootstrap's `vamputil` stub is thinner than the retail module. Logged, non-fatal,
  map load continues. It is 9.3 work, and it is evidence for doing `Entity.__getattr__` and a real
  `vamputil` before hand-porting the 24 Character methods.

- **2026-07-23** — **There is a fifth scripting surface: the console (9.3b + PL5d).**
  `python_bridge.md` lists four Python surfaces (level scripts, `.dlg`, entity field-6,
  `logic_pythoncheck`); the console is a fifth path and it is **bidirectional**. Scripts execute
  console commands by attribute-assigning on `__main__.ccmd` (`c.patchtype = ""`), and a command
  the console cannot resolve falls through to Python. The Unofficial Patch uses that round trip as
  its whole Basic/Plus switch — the install variant lives in `cfg/user.cfg`
  (`alias patchtype "setPlus()"`), not in any script or map, so `setPlus`/`setBasic` appear
  uncalled to any search of `.py`/`.ents`/`.dlg`/`.bsp`. Found while asking why `trig_popup_move`
  never fires: `logic_auto.OnMapLoad -> unhidePlus()` is wired on 107 of 108 maps and is the
  ignition for the whole chain, so with `ccmd` unbound every Plus-mode entity tweak silently
  no-ops and `G.Patch_Plus` stays 0. Tracked as **9.3b**, with the cfg copy as **PL5d**.
- **2026-07-23** — **The sign panel draws on a 1024×768 canvas, uniformly scaled by height (4.10).**
  The layout is **client.dll's `CSignUI`**, not `vampire.dll` — the game DLL owns only the entity and
  `LoadSignData`, which is why the earlier `sign.txt` dump had the file resolution but no paint code.
  `FUN_10061520` parses; `FUN_10061830` (background) and `FUN_10060560` (text block) map the authored
  rect through the doubles at `0x10227ec8` = 1/1024 and `0x10227eb8` = 1/768 and call SetPos/SetSize
  (`FUN_101a9950`). Three findings the data alone could not give:
  (1) **The scale is uniform, driven by height** — `FUN_100cd100`, which the width math divides by
  1024, is a 4:3-proportional width (`ScreenH·4/3`), not the backbuffer width, so `Wide·A/1024`
  collapses to `Wide·ScreenH/768`. The canvas keeps its aspect and letterboxes horizontally rather
  than stretching. Measured against the retail game on 16:9: panel width is **1.513×** the screen
  width, where a stretch model predicts 2.0× and this one predicts 1.50×.
  (2) **Text blocks are children of the panel**, so `XPos`/`YPos` are offsets inside the
  `BackgroundImage` rect, not screen coordinates — `FUN_10061830` is a `CSignUI` method setting the
  panel's own bounds. The shipped data corroborates: the patch moved `tutorial_popup_moving1`'s body
  text 324 → 836 in the same edit that widened the background 1024 → 2048, and a centred 2048-wide
  panel starts at virtual −512, so −512+836 lands on retail's original pixel.
  (3) When `XPos + YPos == 0` the background takes a **centring branch**, which is why
  `interface/Pop_Ups/general` at `2048×1024` deliberately overscans and bleeds off every edge.
  Three keys the entity-side survey had missed also turned up: **`HideHUD`**, and **`ClientCommand`**
  inside a **`Rules`** block (with `CloseOnLeftClick`, whose retail default is the panel's constructed
  value = **true**, and `MinShowTime`). Fonts pick by *exact* screen-width match on
  640/800/1024/1280/1600, else the plain `Font`, else `"Default"`.
- **2026-07-23** — **Sign windows tracked, split substrate/UI (4.10 + 8.8 + PL5c).** `game_sign` /
  `prop_sign` had no task: the only mention anywhere was `entity_visuals.md` R5, which called them
  "textured quads/decals… small, cosmetic; last" — wrong, and the reason they were never scheduled.
  The decompile settles what they are (`CGameSign::LoadSignData` `FUN_10212da0` /
  `CPropSign::LoadSignData` `FUN_10212200`): a **full-screen VGUI window** whose `definition_file`
  is a `SignData` KeyValues panel, opened by `OpenWindow` or `+use`, with a first-true `Sign
  { dependency filename }` redirect evaluated as Python (`Py_eval_input`, error-to-false) — i.e. a
  UI + scripting subsystem, not geometry. They are also **load-bearing for the tutorial**: 71 of the
  73 `game_sign` in the game sit on `sp_tutorial_1` as the `popup_*` help windows, and `tutorial.py`'s
  beat machine opens/rewrites them directly, so "tutorial completable as retail" (P9) can't be met
  without them. **Split:** the entity classes + a Canvas panel land in P4 as **4.10** (so the
  substrate is complete and the tutorial's 51 `OpenWindow` wires stop dropping, before P5's scripts
  start firing them), and the pixel-faithful panel waits for the VGUI stack the menu port brings, as
  **8.8**. The definitions + background materials export as **PL5c**. R5 in `entity_visuals.md` is
  corrected to point here.
- **2026-07-22** — **5.5 decided: embed CPython 2.x (option c), plus a working PoC.** The survey
  settles it. The 36 loose level scripts (out/scripts, **16,473 lines** excl. the bundled 2.1 stdlib)
  are full Python 2.1, not an expression dialect: **1,119 `def`, 45 old-style `class`, 345 `for` /
  26 `while`, 30 `try`/23 `except`, 162 `import`, 627 `print` statements, 3 `exec`, list-comps,
  `lambda`, `%`-formatting**, importing `random`/`time`/`types`/`struct`/`string` (+ VtMB's own
  `lib/` pickle/string/random). That is decisively past `ElysiumExpr` (an expression evaluator — no
  statements/classes/control-flow/exceptions/imports); option (a) would mean reimplementing all of
  CPython 2 + a stdlib, and (b) transpiling 16.5k lines of dynamic Py2 with `exec`/pickle/old-style
  classes is huge and fragile. **The 2.1→2.7 delta is ~0** (checked every script: **zero**
  string-exceptions — the one thing removed in 2.6 — and **zero** `from __future__`; classic `/`
  division and old-style-`class` are the default on both), so a maintained 2.7 fork runs the retail
  scripts 1:1. VtMB's own VM is stock CPython 2.1 (`vampire_python21.dll`, 653 exports; `.pyc` magic
  60202), and it saves *through* `pickle` — so a real embed aligns with the R8 save model rather than
  fighting it (G is proxied so C++ and Python share one store).
  **Fork chosen: `qnox/python-2.7`** (CPython **2.7.18**, actively maintained — release `v20260109`,
  Jan 2026). Its `x86_64-pc-windows-msvc` install_only build is **MSC v.1944 (VS2022) / 64-bit** — the
  same toolchain family as UE 5.8 — with headers + `python27.lib` + `python27.dll` + stdlib; verified
  running (classic `7/2==3`, stdlib imports). Tauthon (2.7.18 + Py3 backports, semi-maintained) is the
  fallback; actual 2.1 isn't worth the modern-MSVC pain given the ~0 delta.
  **PoC pulled forward from 9.3 (per the scope call):** vendored the SDK at
  `Source/ElysiumUE/ThirdParty/CPython27/` (dll+lib+headers+PythonHome/Lib); `Build.cs` wires it Win64-
  only (delay-load `python27.dll`, `ELYSIUM_WITH_CPYTHON`, stdlib as a RuntimeDependency); the include
  shim (`ThirdParty/ElysiumPython.h`) undefs `_DEBUG` across `<Python.h>` (else `Py_DEBUG` ABI + the
  `python27_d.lib` auto-link break the build — the standard PythonScriptPlugin trick). New
  `FElysiumPythonVM` (process-global; `GetDllHandle` the vendored dll → `Py_SetPythonHome` → `Py_NoSite`
  → `Py_Initialize`) registers a `vampire` C-module whose only *real* binding is **`G` proxied onto
  `UElysiumGameStateSubsystem`** (attribute get/set = flag read/write, default-0 / assign-None-deletes,
  `keys`/`has_key`/`ClearAll`); a Python **bootstrap** stands up forgiving stubs for the natives 9.3 will
  make C (`FindPlayer`/`FindEntityByName`/… + a stub `vamputil`), and stdout/stderr route to the UE log.
  `FElysiumCPythonScriptHost` slots into the existing `IElysiumScriptHost` seam (`elysium.script.cpython
  [0|1]`), alongside `elysium.py.smoke`/`exec`/`load`/`fire` verbs and a **CPython panel in the
  `Elysium.Scripting` Cog window** (status/version, host toggle, load-level-script, list + fire the On*
  callbacks, a python exec box; the G table + eval log below reflect its evals). **Validated offline
  with the vendored interpreter against the real `tutorial.py`:** it imports cleanly (its module-level
  code + `from vamputil import *` run, `levelscript` loads, `cCelerity=8` resolves), its On* callbacks
  execute, and field-6-shaped statements resolve level-script constants —
  **`G.Tutorial_Discflags |= cCelerity` flips G to 8**, exactly the acceptance ElysiumExpr can never
  meet (NameError). In-engine, all CPython C++ TUs **compile**; the full editor link + in-editor run is
  blocked only by the parallel **P8 glTFRuntime** WIP (plugin not yet installed). **Remainder = 9.3:**
  replace the Python native-stubs with the real C `vampire` bindings (54 methods), auto-load the map's
  `worldspawn.levelscript` at map load, and pin field-6 ↔ level-script name resolution.

- **2026-07-22** — 4.5 landed (tutorial logic/point/brush + trigger classes). **Grounded in the
  decompile, not guessed:** `tools/ghidra/run.ps1 -Script DumpGrep` against the analyzed `vampire.dll`
  recovered every class factory + datamap by classname/field-string anchor. **The one real VtMB
  divergence is `logic_case_toggle`** (`FUN_101344f0` / core `FUN_101346e0`): its `InValue` is a
  *delta* that advances a current-case pointer that many **configured** cases (skipping empty slots,
  wrapping 0..15) and fires the landed case — not stock `logic_case`'s value-string match (whose class
  is 4 bytes smaller, lacking the current-index int). This is why the tutorial wires
  `math_counter.OutValue → logic_case_toggle.InValue`: the counter value is the advance amount. Both
  are implemented (shared Case base); the value flows through a new Source-`COutput<T>` seam
  (`FireOutput(name, activator, value)` fills an empty map-param, else the authored param wins).
  `math_counter`/`logic_timer` confirmed **stock** (present, unmodified). `func_brush` is nearly a
  base entity (`FUN_1013dd30` — trivial ctor); its Solidity/Enable/Disable fold into the body's single
  `SetDormant` switch so they don't fight dormancy. `env_fade` renders on one screen-fade state held on
  the entity world and polled by `AElysiumHUD`. The player is reached from the plain-C++ substrate via
  a new `FElysiumEntityWorld::GetPlayerPawn()` seam (point_teleport, trigger_hurt). Debug: `GetDebugState`
  on every class + a dedicated `Elysium.Logic` Cog window. **RE4 (Ghidra datamap export)** is now the
  proven, low-cost method for any future class pass — DumpGrep on the persisted project, no re-import.
  `trigger_stealth_mod`/`trigger_inventory_check`/`trigger_environmental_audio` left as inert records
  (their stealth/inventory/RoomDSP systems don't exist yet).
- **2026-07-22** — 5.3 landed (native bindings). **Grounded in the exported field-6, not the table
  count:** across all 10 exported maps only `FindPlayer()` (7×) + Character methods off it
  (`RemoveItem`/`SewerMap`/`GiveItem`/`ClearActiveDisciplines`) are native; the heavy callees
  (`spawnCopCar`, `resetHos`, the `cXxx` discipline constants, the level's `On*` callbacks) are all
  **level-script** names → 5.5, correctly still NameError, so the tutorial's `G.Discflags |= cCelerity`
  keeps no-opping. **Zero field-6 references `self`/`activator`** — they are I/O *targets*, not Python
  names — so those bind from the eval context but sit **below** the module globals as a harmless
  fallback. **Stub policy = log-only (roadmap default):** only the two systems that already exist wire
  for real (`SetQuest`/`GetQuestState` → the quest map); the other 9 globals + 22 methods log a stub +
  a sensible default (predicate globals read false). `ScheduleTask` (deferred source) and `ChangeMap`
  (travel) stay stubs to respect their phase owners (5.4 / P4). **Forgiving dispatch:** any attribute
  off a `Character` object binds (unlisted names too), so a call outside the known-24 runs to the
  generic stub instead of raising — matching retail's `__getattr__` fall-through and the slice
  acceptance (`FindPlayer().ClearActiveDisciplines()` runs). NPC handles accept Character methods too.
  One static `GNativeBindings` table drives both membership and the debug view. **Precedence caveat for
  5.5:** `OneOfSet` is both a native global and a `vamputil.py` helper exec'd into `__main__` (the
  latter wins in retail); when level-script names resolve, the level-script definition must shadow the
  native one. Debug: the `Elysium.Scripting` window gains a Native-bindings table (kind/status/live
  call-count) + a Recent-native-calls log, fed by `UElysiumGameStateSubsystem`'s native-call ring.
- **2026-07-22** — 5.2 landed (expression evaluator). **One evaluator, no second dispatch:**
  `ElysiumExpr` (lexer + recursive-descent AST parser + tree-walk) is exception-free — every error
  collapses to Void (error-to-false, RE3). **Scope beyond the literal task line** (calls / attr /
  literals / compare / and-or): added assignment statements + the full `+ - * / % | & ^ << >> **`
  operator set + `None`, because the tutorial's real field-6 is 60% `G.<flag> = <expr>` (46 of 77,
  the only operator being `|` for the `Tutorial_Discflags` accumulation) and the P5 slice needs
  flags to flip. Python-2 semantics (floor int div/mod, `and`/`or` return an operand, chained
  comparisons). **`ent.Input()` dispatches through the existing chokepoints** (`EnqueueInput
  "!self"` targeting the one handle) rather than a new synchronous path — visible in the queue
  window, single-steppable, serializable. **Live field-6 is opt-in** (`elysium.script.live`,
  default off; the null host stays the map-load default) so 5.2 changes no map-load behaviour; 5.4
  makes it the default and adds logic_pythoncheck + ScheduleTask. Bare names + native globals
  (FindPlayer, `cCelerity`, DialogPostProcess) are NameErrors until 5.3/5.5, so `G.x = G.x |
  cCelerity` correctly no-ops for now while plain `G.x = 1` flips visibly. Debug surface: live `G`
  table + eval/exec box + recent-eval log in the `Elysium.Scripting` Cog window, echoed by
  `elysium.eval` / `elysium.exec`.
- **2026-07-22** — 1.6 landed (starter classes). **`logic_auto` owns map-load ignition** via a
  one-shot think (first world tick), replacing the generic `FElysiumEntityWorld::FireMapLoadOutputs`
  bootstrap (removed) — matches retail (logic_auto fires on the first server think, after every
  target has spawned) and exercises the think path. **`CBaseTrigger` is a registry-only chain node**
  (no entity carries that classname) so `trigger_multiple`/`trigger_once` share Enable/Disable/Toggle
  + StartDisabled/wait through one base; this is the pattern the P4.5 trigger family extends.
  **Trigger activation filter reduces to the `ALLOW_CLIENTS` (0x1) bit** in P1.6 because the only
  toucher is the player and its activator is unresolved (the pawn is not an entity until P4);
  empirically the tutorial's triggers carry 0x1 (43/44 `trigger_multiple`, all `trigger_once`), and
  the one `0x8` physics-only trigger *should* ignore the player — so the reduction is faithful, not a
  shortcut. **Remove-on-fire / fast-retrigger spawnflags left unmodelled** for `logic_relay`/
  `logic_auto`: those bits are unconfirmed for VtMB (RE1 only covered button + trigger flags, and
  buttons diverge from stock), and firing `Kill()` on a wrong bit is a worse failure than a spurious
  re-fire, which per-output `times` already bounds. Revisit if a tutorial relay over-fires.
- **2026-07-22** — 1.1 landed. Currency types are plain C++ structs (R1, no reflection):
  `FElysiumVariant` carries the seven runtime categories with total, never-throwing
  coercions (a Void variant is the falsy / error-to-false case). `FElysiumEntityHandle` is
  the value only — `IsSet()` is structural; true falsy-when-dead/stale is decided by
  `FElysiumEntityWorld::Resolve` (1.4), not here. **The `G` store and quest map key
  case-sensitively** (custom `KeyFuncs` + `FCrc::StrCrc32`) because they mirror Python dicts;
  UE's default `FString`/`FName` maps are case-insensitive, which would silently merge
  distinct flags. `G` is variant-valued, default-0-on-miss, and assigning Void deletes the
  key (decompiled `tp_getattr`/`tp_setattr`, `python_bridge.md`). Clock + `G` + quests live on
  the GI subsystem so they survive travel.
- **2026-07-22** — 0.5 Cog spike closed: vendored the **main Cog plugin only** (upstream `cb1b435`
  on `main`, MIT) into `Plugins/Cog/`, minimal deps, `UElysiumCogSubsystem` registering stock
  CogEngine windows. Builds + runs on UE 5.8 with **no source patches** (the flagged ImPlot
  `INFINITY` MSVC error did not reproduce on VS 14.50). Build products gitignored; provenance
  (base commit) recorded here so future updates re-base off `main@cb1b435`.
- **2026-07** — Cog (MIT, vendored, dev-only) adopted as the debug UI shell after tooling
  research; Slate console superseded. (`debug-tooling.md`)
- **2026-07** — Entity object model adopted: plain-C++ entities with optional Unreal bodies;
  one name table per class (I/O + Python + keyvalues + saves + inspector); generation-checked
  handles on stable `.ents` indices; one clock + one queue, no `FTimerManager`; two
  instrumented chokepoints; dormancy as one switch. (`engine-core.md`)
- **2026-07** — Queue-serviced-before-thinks tick order chosen (retail order unknown, RE2).
- **2026-07-22** — RE2 resolved: **retail is think-first** — `Physics_RunThinkFunctions` then
  `CEventQueue::ServiceEvents` in the `vampire.dll` server frame (addresses in
  `roadmap-archive.md` → "Ghidra extraction", RE2 note).
  The provisional queue-first choice diverges from retail. Decision for task 1.4: match retail
  (think-first) unless save-determinism argues for queue-first; `engine-core.md` Tick note now
  documents think-first.
- **2026-07-22** — 1.4 tick-order resolved and shipped: **think-first**. No save/load exists yet
  (M6/P9), so no determinism argument for queue-first; `FElysiumEntityWorld::Tick` runs
  `RunThinks(now)` then `ServiceEvents(now)`, matching retail. Save-determinism can revisit at P9
  if needed (the queue and think times both serialize regardless of service order).
- **2026-07** — Debug-substrate-before-M3 sequencing chosen ("foundation now"): P1 → P2 → P4.
- **Standing (from strategy)** — fully dynamic lighting committed (HWRT Lumen + MegaLights +
  VSM, DX12/SM6 mandatory; no baked GI — lump-8 bake parked as low-end contingency);
  no game content in `.uasset`s ever (extend `.emc`-style caches instead); variable timestep
  matching retail (no fixed tick); Unreal-native substitutions where they beat porting
  (Chaos physics props, NavMesh+BT AI, Single Layer Water, deferred decals, Cable ropes,
  native audio submixes); Nanite not applicable; bring-your-own-game legal posture.
- **2026-07-22** — Web-research de-risk pass over the open questions:
  **MegaLights is Production-Ready in UE 5.8** and the per-light control is the
  "MegaLights Shadow Method" property (3.1 is a straightforward property set); MegaLights
  does not light water and Single Layer Water gets forced-mirror Lumen reflections
  (accepted for VtMB water — noted on 7.3). **Audio libraries pinned:** `dr_wav` for
  MS-ADPCM/IMA-ADPCM (6.1), `dr_mp3`/`minimp3` for MP3 (6.2) — all public domain,
  single-header, MP3 patents expired. **CPython 2.x embed fallback confirmed viable**
  (maintained 2.7.18 forks build with VS2019+) — 5.5 decision itself stays pending on the
  script survey. **Cog** (UE 5.5+, active) and **glTFRuntime** (UE 5.7, active) both look
  healthy but lack explicit 5.8 confirmation — the 0.5 and 8.2 spikes stand. **Lumen Lite**
  (new 5.8 medium-quality GI) recorded as an Options entry only — the HWRT-Lumen commitment
  is unchanged; revisit trigger: 10.3 floor validation fails or a sub-DXR audience matters.
- **2026-07-22** — Ghidra extraction pass over the RE-drivable open items (`vampire.dll`; dumps in
  `tools/ghidra/out/re*.txt`). **RE3 confirmed** two load-bearing assumptions with recovered logic:
  `Entity.__setattr__` (`FUN_10195a10`) writes through the same datamap walk as `__getattr__`, falls
  through to the instance `__dict__` on unknown names, holds `_entity_ptr_` read-only, gates writes
  on record flag bit `0x8`, and marshals per VtMB's shifted `fieldtype_t`; **error-to-false** is real —
  `logic_pythoncheck` (`FUN_10135290`) evals with `Py_eval_input`, `PyErr_Print`s on a raise, and
  returns false, and the exec/field-6 paths (`FUN_100ce8a0`/`FUN_100ce990`) swallow errors likewise.
  **RE1 partially** recovered (`CBaseButton` layout/vftable `0x1045293c`, use-icon offsets), spawnflag
  bit→behavior map still pending the Use/Touch handlers. **RE4** (datamap JSON export) and **RE3 `G`
  default-0** scoped with exact seeds — see `roadmap-archive.md` → "Ghidra extraction —
  findings + plan".
- **2026-07-22 (cont.)** — Ghidra pass continued. **RE4 method confirmed**: VtMB datamaps are
  runtime-built, so the export decompiles per-class **datamap builders** (`CBaseEntity` =
  `FUN_100a22f0`), not a static `.data` walk; the CBaseEntity base keyfield/input/output contract is
  extracted and recorded in `python_bridge.md` (RE4 → `[~]`). **RE1** (button spawnflag bits) and
  **`G` default-0** both hit the same wall — their target functions are `m_pfn*`/vtable/immediate-
  reached and carry no references in Ghidra's DB, so xref discovery stalls; unblock via an
  `EnableAIF` re-import or direct PE method-table/vtable parsing (recorded as the "shared blocker").
- **2026-07-22 (cont. 2)** — `EnableAIF` re-import of `vampire.dll` ran and unblocked both stalled
  items. **`G` default-0 CONFIRMED** → RE3 fully closed (`[x]`): G is `PyDataManager`; `tp_getattr`
  `0x1019b3d0` returns `PyInt_FromLong(0)` on a flag miss. **RE1 `CBaseButton::Spawn` map recovered**
  (`m_spawnflags`@`+0x204`; `FUN_100c8d60`) — bits `0x1`/`0x40`/`0x100`/`0x400`/`0x800`/`0x1000`
  decoded into `entity_io.md`; bits `0x20`/`0x2000` + `trigger_multiple` filter still to do (RE1 stays
  `[~]`). Findings live in `python_bridge.md` (G) + `entity_io.md` (button); the AIF-analyzed DB is now
  the project baseline.
- **2026-07-22 (cont. 3)** — **0.4 sidecar space audit done.** Traced all five downstream-consumed
  sidecars through `UE_bsp_to_scene.py`: `.ents`, `.sprites`, `.spawn`, `.water`, `_decals.obj` are
  **already emitted in Unreal cm** (every one routes through `source_to_unreal`/`INCH_TO_CM`, decals
  with winding reversed) — the exporter had been fully migrated and the audit found **no code
  straggler**. The `rebuild-strategy.md` contract table was stale (`.sprites` "Godot metres", `.spawn`
  "Source coords"); corrected, and space made explicit on `.ents`/`.water` too. **PL7 is empty.**
  Runtime `.spawn` reader confirmed verbatim.
- **2026-07-22** — Asset-enhancement direction accepted (design; not scheduled). Stance:
  **faithful baseline + opt-in, code-driven remaster layer** that preserves VtMB's grimy
  gothic-punk art direction, always as an `elysium.EnhancedTextures` A/B toggle. Adjudication
  test: serve the art direction / fix a technical deficit that fights the dynamic relight → in;
  invent or override an artist decision → out. Sequence is dictated by the dynamic relight —
  **delight albedo first** (recover true base color from 2004 painted-in shading), then
  super-resolve, then derive normal/roughness/AO from the *delit* albedo, metallic by hand-mask
  only. Tier 0 (delight + upscale) is a real deficit fix; Tier 1 (PBR synthesis) is
  style-anchored, curated per material family, budget-gated by the 3060/12 GB floor. Scaffolding
  already exists (`upscale_bench.py`/`sky_upscale.py`/`retex_dds.py`); runtime hooks are
  `M_VtMB_World`'s planned normal/envmask slots. Written up in `asset-enhancement.md`; strategy
  principle 6a; Options + risk-register entries added. Web-research pass confirmed the technique
  set (AI PBR-from-diffuse, de-lighting tools, ESRGAN game-remaster practice) and their caveats
  (guesswork needing curation; delighters tuned for photoscans, not hand-painted art).
- **Pending** — 5.5 level-script execution strategy (interpreter vs transpile vs CPython);
  8.2 glTFRuntime confirmation for the skeletal path; 10.6 EnhancedInput migrate-or-remove.
