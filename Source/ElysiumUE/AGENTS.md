# Elysium — the C++ runtime (`Source/ElysiumUE/`)

The runtime half of the two clean halves (repo-root `CLAUDE.md`). A map's **look** is offline-baked
into `.uasset` content and a `.umap` under the `/ElysiumBaked` mount; the runtime spawns into that
level, adopts its actors, and builds everything the bake cannot hold — brush collision, ropes, the
entity substrate, entity-driven bodies, the sky cubemap — from the pipeline's on-disk intermediates,
with no coordinate conversion.

**This file is orientation plus gotchas.** Design — lifetimes, the frame, the object graph,
ownership rationale — belongs to `docs/architecture/runtime-architecture.md` (the spine),
`engine-core.md` (entity object model), `save-architecture.md`, `ui-architecture.md`,
`input-architecture.md`, `map-architecture.md`, `debug-tooling.md`, and `docs/vtmb/` for
`camera-view-modes.md` and `python_bridge.md`. Per-task status: `docs/project/roadmap.md`.

Loaded alongside this file: `.claude/rules/cpp.md` (the C++ coding policy, path-scoped to
`Source/**`), `.claude/rules/tests.md` (test authoring, path-scoped to `Private/Tests/**`), the
`gameplay-change` skill (the entity/API/event/save contract every gameplay change
lands behind), and the `elysium-testing` skill (tiers, filters, and how much a change authorizes).

## Module

UE 5.8. Module `ElysiumUE` (Runtime, Default loading phase). Plugins, third-party dependencies and
their conditions are declared in `ElysiumUE.uproject` and `ElysiumUE.Build.cs`. Two facts those
files do not state:

- **`Cog` is the only vendored plugin** (`Plugins/External/Cog/`, MIT debug-UI shell, stripped from
  Shipping via `ENABLE_COG`). Every skeletal asset is constructed from the `.eskm` container by
  `UElysiumSkeletalBuildLibrary`, so nothing third-party reads a model.
- The **CPython 2.7.18 SDK is fetched, not committed** —
  `pipeline/src/elysium_pipeline/devtools/fetch_cpython27.py` writes `ThirdParty/CPython27/`, which
  is gitignored and gated by `ELYSIUM_WITH_CPYTHON`, Win64 only.

## Gameplay integration contract

Every gameplay implementation or change in this runtime lands behind the entity/API/event/save
seams described by the **`gameplay-change`** skill, over
`docs/architecture/gameplay-systems-architecture.md` as the governing design guidance.

## Source layout

`Private/` is subfoldered **by layer**; `Public/` stays flat, because it is the module's API
surface rather than a layering. The include convention that makes a cross-layer dependency visible
is in `.claude/rules/cpp.md`.

Types are named `FElysium*` (plain-C++ substrate and value types), `UElysium*`/`AElysium*` (the
Unreal half) and `IElysium*` (the service seam) — grep is the index.

## Config

`Config/DefaultEngine.ini` (boot map, game mode/instance, render path, trace channels) and
`Config/DefaultInput.ini` (engine-side settings only — no action/axis mappings; those are
installed by `UElysiumInputRouter`). Boot decision + flow: `docs/architecture/runtime-architecture.md`.
`FElysiumContentPaths::Root()` is the pipeline's `$ELYSIUM_EXPORT_ROOT` mount point.

## Debug layer (non-Shipping)

`UElysiumCogSubsystem` (`#if ENABLE_COG`) registers the stock CogEngine windows plus the Elysium
ones over `FElysiumCogWindow`; `UElysiumEntityDebugSubsystem` hosts the `elysium.ent_*` verbs and
world-viz layers; `UElysiumMcpSubsystem` is Layer 3, reached through
`pipeline/src/elysium_pipeline/devtools/mcp_proxy.py`. Design: `docs/architecture/debug-tooling.md`.

Headless self-driving harnesses are armed from `UElysiumMapSubsystem::Initialize` on a command-line
flag and exit when done — `-ElysiumProfile`, `-ElysiumShots`, `-ElysiumProbe`, `-ElysiumMove` and
`-ElysiumCast`, one per `uv run elysium debug <verb>`. The two recording runs (`move`, `cast`) share
one schema and one writer, `Debug/ElysiumLocomotionTrace.h`, and are compared by
`pipeline/src/elysium_pipeline/validation/channel_diff.py`.

Automation tests live in `Private/Tests/`. **`ElysiumScratchContentRoot.h` overrides the
`-ElysiumContentRoot` command-line pin as well as the environment variable** — the pin wins and
every automation launch passes one, so a test whose production reader resolves through
`FElysiumContentPaths` would otherwise read the real corpus. Every tier runs `-nullrhi`, so **no
test covers a rendered frame**; the pixel comparison is `validation/shots_diff.py` against a
baseline under `$ELYSIUM_EXPORT_ROOT/_shots/_baseline/`, which is a local instrument outside the
automation run.

## Engine gotchas

Hard-won, non-obvious, and easy to undo. The ones belonging to the animation bake, its readers
and the graph live beside their design, in `docs/architecture/animation-architecture.md` §2.5
and §4.1:

- **A Live Coding patch exists only in the editor process that compiled it.** `CompileLiveCoding`
  links a `UnrealEditor-ElysiumUE.patch_N.dll` into the running editor, and the on-disk module is
  untouched — so a commandlet, a headless test run or a fresh editor launched alongside it all load
  code that predates the patch. New `UPROPERTY`s are the sharp edge: a graph or asset authored live
  against them regenerates against a class that does not have them, and the properties resolve to
  nothing while the run still reports success. Anything proven live is proven again after
  `uv run elysium build`, which needs the editor closed because it holds the module open.
- **The vendored Cog shell has two local interaction patches.** `FCogImguiContext::SetEnableInput`
  fully restores high-precision locked mouselook when Cog closes, and `UCogSubsystem::RenderMenuItem`
  keeps navigation click-to-open instead of rendering whole live windows on hover. Reapply both when
  updating Cog. `SetEnableInput` dereferences the lazily created ImGui context, so every boot-time call
  guards on `GetEnableInput()` first.
- **An `APawn`'s owner is not permanent.** `AController::Possess` overwrites the pawn's owner with
  the controller, and `AElysiumNpcBody` possesses lazily on its first accepted travel order — so
  anything read back off `GetOwner()` answers only for a body that has never moved. The body holds
  its `AElysiumMapActor` directly (`SetOwningEntity`), and a body that cannot reach its character
  warns once naming the broken link.
- **The loading screen hooks `IGameMoviePlayer::OnPrepareLoadingScreen`, not `PreLoadMap`** — the
  movie player binds `PreLoadMap` itself at engine init, ahead of any GI subsystem. Its blocking
  screen auto-completes; `PostLoadMapWithWorld` installs the same visual in the player UI root's
  runtime-loading layer until the map actor publishes ready.
- **Activatable screens enter through `UElysiumPlayerUISubsystem` containers.** Adding one straight
  to a viewport bypasses CommonUI activation, Back routing and focus restoration; the composition
  policy test rejects direct insertion outside the one root owner.
- **`UElysiumCameraComponent::SolveFrameFor` must delegate to `UCameraComponent::GetCameraView`
  first** — skipping it silently breaks first-person rendering, and it is also the only thing that
  advances the non-ticking component's world rotation, so the boom would hang off a stale eye.
  Everything that produces a view goes through it.
- **The camera manager overrides `UpdateViewTargetInternal`, never `UpdateViewTarget`.** The outer
  function owns the `ACameraActor` branch that character generation's view target needs, the stock
  debug `CameraStyle` modes, the per-frame POV reset and the modifier pass; the inner one is exactly
  the `CalcCamera` dispatch being replaced. Overriding the outer silently breaks chargen.
- **`UCameraModifier::ModifyCamera` does not scale by `Alpha`** despite its header comment saying
  so, and `ApplyCameraModifiers` runs once per *view target* rather than once per frame — during a
  view-target blend that is twice with the same delta. `UElysiumCameraModifier` applies its own
  alpha, calls `Super` (without which `Alpha` never advances at all), and hands the repeat pass a
  zero delta rather than skipping it, because the second target's POV still needs the layer.
  Relatedly, `AddNewCameraModifier` returns null **silently** when `bExclusive` meets a duplicate
  priority.
- **The player hull is a box, not a capsule** — `StepMove` depends on a flat bottom, and `ACharacter`
  will not take a box root.
- **A `UCharacterMovementComponent` starts in `MOVE_None` and only a controller ever changes that.**
  `MovementMode` has no constructor initializer, so it is zero-initialised, and the walking mode is
  set by `ACharacter::Restart()` on possession. `AElysiumNpcBody` disables auto-possession and spawns
  its controller lazily on the first accepted `MoveTo`, so a body that only ever stands never gets
  one. `IsMovingOnGround()` reads the mode alone — not `IsActive()` — so such a body reports itself
  airborne for its whole life while standing on the floor, and anything reading the locomotion
  sample's grounded flag believes it. `ApplyEnabledState` sets the mode itself once its floor query
  succeeds. Note the asymmetry that makes this easy to misdiagnose: `Deactivate()` never touches
  `MovementMode`, so a body that has moved even once stays correctly grounded forever after, and only
  the never-moved background cast is affected.
- **An NPC movement tick already depends on the map actor while its character stands on map-owned
  collision.** CharacterMovement wires the primary tick of the movement base's owner. GameFrame
  therefore runs from `GameplayTickFunction`, which depends on the motor ticks; adding those
  prerequisites to `AElysiumMapActor::PrimaryActorTick` closes a cycle and floods `LogTick`.
- **`ApplyMaterialOverrides` is lazy** — a runtime `SetMaterial` drops the primitive's built
  texture-streaming data, so albedo and `EnvMask` fall back to a low mip.
- **`GetImportedModel()->LODModels` must grow in parallel with `AddLODInfo()`.** A skeletal mesh's
  LOD is two parallel arrays and both entries have to exist, but nothing reads the imported model
  while the mesh is being built — so a bake that adds only the LOD info runs clean and saves, and
  `PostLoad` then asserts in whichever process opens the package next.
- **`UBodySetup::CalculateMass` reads the owning primitive's `FBodyInstance`**, which a runtime-built
  component never seeds from the asset — physics props re-apply mass to the component.
- **`USkeleton::AddCurveMetaData` defaults `bTransact = true`**, which under `WITH_EDITOR` calls
  `GEditor->BeginTransaction`. `run play` is `UnrealEditor.exe -game`, where `GEditor` is null, so a
  runtime curve-metadata write crashes on a null dereference in `-game` while working fine in the
  editor. Pass `bTransact = false` from any runtime path.
- **`+use` and the debug pick use dedicated channels** (`ELYSIUM_USE_CHANNEL` /
  `ELYSIUM_PICK_CHANNEL`), because the walkable surface is a material-less `.hulls` collider that
  would otherwise be reported instead of the wall.
- **A sheet recompute needs BOTH the rules and the effect layer** — `RecomputeCurrent(Stats)` still
  compiles (the effect argument defaults to null) and silently drops every clan bane and gift. On a
  character, go through `FElysiumCombatCharacter::RecomputeSheet()`, which passes both and re-derives
  the `health` keyfields after.
- **A restored NPC's sheet is not its health.** An NPC sheet is re-seeded from the stat template at
  spawn and is not save state, so after a load its damage slot reads zero while the field walk has
  already restored the real `health` keyfield — and `RecomputeSheet()` (which `RebuildEffects()`
  ends in) re-derives the pair off that sheet, handing a wounded NPC its whole track back with
  nothing logged. `FElysiumNpc::Serialize` (`Substrate/ElysiumNpc.cpp`) holds the restored
  `Health`/`MaxHealth` across its own `RebuildEffects()` and puts them back; any new restore path
  that recomputes a sheet needs the same guard.
- **`SetQuestState` pays out; a save load must not go through it** — it resolves the completion
  state and fires `AwardMoney`/`AwardXP`/`Event`, so restoring a payload key-by-key through it would
  replay the whole run's awards. `RestoreQuests` is the silent bulk door, and it is the only one.
- **Save omission diffs against a post-Load baseline, not zero** — a fresh-constructed reference
  omits the wrong things and a restored map re-runs every `logic_auto` ignition.
- **Resolved `UAnimSequence`s cache on the map actor, not the subsystem** — a sequence is bound to
  one body's own `USkeleton`, and meshes are per-map-epoch.
- **A character has exactly one build: the `/ElysiumBaked` mount.** `ElysiumNpcVisual::LoadMesh`
  fails by name for a stem the character export has not covered rather than substituting anything,
  so a partial export is a missing body rather than a differently-posed one. `uv run elysium export
  characters` with no arguments bakes the whole cast.
- **`FElysiumEntityWorld::NowSeconds()` falls back to the last ticked time when there is no game
  state** — only `RunThinks`/`RunPlayerThink` see the tick argument, so without that fallback a
  headless think measuring elapsed time reads zero forever no matter what `Tick(t)` is passed.
- **Cog boots dormant** (`elysium.CogPersist 0` deletes its layout ini before the dependency brings
  Cog up) — restored input capture makes the game's own UI unclickable otherwise.
- **`IElysiumNpcMotor::SetEnabled(false)` also hides the body, so it cannot immobilise a cutscene
  actor.** A choreographed scene's `position_start` cast has to stop moving while staying on
  camera, which is what `SetFrozen` is for; `SetIgnoreCharacterCollision` is the separate
  character-vs-character switch a `scripted_sequence` beat borrows. Collapsing any of the three into
  the others makes a scene's cast vanish. They are resolved together in `ApplyCollisionState`, so a
  new caller must go through it rather than touching the capsule directly.
- **No game-thread bone query tells you where a leader-pose follower is drawn.**
  `GetSocketTransform` on a follower answers through the leader bone map, and
  `GetCurrentRefToLocalMatrices` rebuilds fresh matrices from current game-thread state — both can
  report a followed weapon riding the hand while the mesh draws frozen at its reference pose off a
  proxy nothing has updated. The drawn frame is the skinning matrices in
  `GetMeshObject()->GetReferenceToLocalMatrices()` (the last dynamic-data packet the render thread
  received), guarded by `HaveValidDynamicData()` — on the install frame the packet does not exist
  yet and the accessor dereferences it unchecked. The worked example is the green room's wield
  tracking check (`ElysiumRenderedWield` in `Debug/ElysiumGreenRoomWield.cpp`).

## Build and test

`uv run elysium build`, then `uv run elysium test <tier>`. The tiers, what each one needs, the
filter rules, and how much export/bake a change authorizes are the **`elysium-testing`** skill.

Three standing approvals, because each is easy to add without noticing:

- **A live run is proposed, never assumed** — ask the owner first, with a recommendation.
- **No feature flag and no A/B toggle without approval.** Work lands as a complete change, not
  behind a switch.
- **Do not create cvars without explicit request or approval.** Cog is the debug surface: a new
  control is a tab, not a console variable.
