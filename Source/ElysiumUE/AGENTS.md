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

Hard-won, non-obvious, and easy to undo:

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
- **A morph target only drives when its curve is flagged on the skeleton.**
  `USkeletalMeshComponent::ActiveMorphTargets` is populated from the bone container's flags, and
  those come from `FCurveMetaData::Type.bMorphtarget` — so a curve registered without the flag
  evaluates to the right weight on a face that cannot receive it. `RegisterMorphTargetCurves` sets
  both halves; the character verifier guards the contract.
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
- **A proxy owning nodes outside the compiled graph must implement `UpdateAnimationNode`** —
  `FElysiumBipedAnimProxy`'s cinematic clip player is not in the graph, so the base call cannot
  reach it, and a sequence player never `Update_AnyThread`'d holds its start frame forever.
- **A layered blend's bone mask is not a pin.** `FAnimNode_LayeredBoneBlend::BlendMasks` is
  edit-time state, so a mask that changes per selection cannot be driven by a graph pin the way
  every other asset on `ABP_ElysiumBiped` is. It is written at runtime instead — the node is found
  by `FAnimSubsystem_Tag` under `ElysiumAnimGraph::UpperBodyLayerTag` and set through
  `SetBlendMask`, which is what Epic's own `ULayeredBoneBlendLibrary` does. Two consequences:
  a **null** mask is legal only because the graph is a *template* Animation Blueprint
  (`ValidateAnimNodeDuringCompilation` exempts one), which is what keeps a generated profile asset
  out of the tracked graph text; and the mask must be resolved by NAME against the **playing**
  skeleton, because the profile a bank's own skeleton hands back gates a shifted set of bones and
  logs nothing.
- **A `UBlendProfile`'s mode has to be set before its bone scales.** An entry equal to the mode's own
  default is not stored, and that default is 0 for `EBlendProfileMode::BlendMask` against 1 for every
  other mode. A profile still in its constructed `WeightFactor` mode therefore discards every 1.0
  written into it and saves empty — which reads at evaluation as owning the whole rig, the exact
  opposite of the mask that was asked for, with nothing logged.
- **`UBlendSpace::AddSample` reports failure only through its return value.** It validates the
  sample against the blend space's own skeleton and axis bounds and returns `INDEX_NONE` without
  logging, so a skeleton set *after* the first sample — or a value placed outside the axis range —
  yields an asset that saves clean and carries fewer samples than it was given. Set the skeleton
  before the first add and check every return. The related trap is `ExpandRangeForSample`, which
  runs inside `AddSample` and quietly widens the axis to fit whatever it is handed: an axis range
  that no longer matches what was written is the symptom of a misplaced sample, not a cosmetic
  difference.
- **A blend space with samples and no `ResampleData()` poses nothing.** That call builds the
  segments or triangulation the evaluator reads and is not implied by adding samples or by
  `PostEditChange`. Without it the asset lists its samples correctly everywhere that counts them and
  evaluates to an empty blend; `GetBlendSpaceData().IsEmpty()` is how a caller tells. Dimensionality
  is inferred there too, from the samples' bounding box rather than from the class, so a
  `UBlendSpace` whose samples all share one axis value takes the 1D path regardless.
- **`UAnimSequence::GetAnimationPose` silently falls back to the raw data model** whenever the
  compressed data for the current platform is not resident yet, and compression runs asynchronously
  after a bake. So the same call answers out of two different representations depending on how much
  work happened earlier in the same process, and a test that reads an additive can pass and fail on
  the same assets across runs. Call `WaitOnExistingCompression()` first when the assertion is about
  what a cooked build ships; `IsCompressedDataValid()` is how a caller tells which one it got.
- **`UAnimSequence::GetBoneTransform` never performs the additive conversion.** It is a plain track
  read, so a raw evaluation hands back the keys as written — and a baked `_delta`'s keys are the
  delta already composed onto its base, because the compressor subtracts that base back out. A test
  built on it reports a correct additive as broken by exactly one base pose, and would pass just as
  happily if the subtraction had never run. `GetAnimationPose` is the door the runtime uses;
  `EvaluateAdditiveFrame` in `ElysiumBakedCharacterTests.cpp` is the worked example.
- **A bone a sequence carries no track for evaluates to identity rather than to the reference pose —
  on an additive.** The reset differs by kind: `ResetToAdditiveIdentity` for an additive against
  `ResetToRefPose` for an ordinary sequence, so the single signature "the error equals that bone's
  full bind transform" means a dropped track on one and the exact opposite on the other. Read the
  additive stamp before hunting a rotation bug. On an additive that magnitude is ambiguous between
  three causes — a dropped track, the additive round-trip above, and compressed data that is not
  resident — so check `IsCompressedDataValid()` before reading anything into it.
- **Compatible-skeleton remapping always reads both skeleton reference rotations.** Any valid pair
  can make `DecompressPose` apply that rotation delta even without an explicit retarget node, so
  every skeleton that shares a bank uses identity common-bone reference rotations. The mesh keeps
  its exact authored bind, rotation keys pass verbatim, and `OrientAndScale` reads the sequence's
  named donor pose only to map translations onto the playing mesh. A bone no ordinary sequence
  tracks resolves to that playing **mesh's** reference pose, not the `USkeleton`'s. Never restore a
  per-body bank copy to avoid this engine path; bank package count is independent of body count.
  See `docs/architecture/animation-architecture.md` § 2.4.
- **The Content Browser preview runs no anim graph, so it applies no axis interpolation.** A rig
  whose bones are procedurally driven previews with untwisted forearms: the stage evaluates over the
  blended pose rather than being baked into the clip, so the preview is showing what the asset says
  and not a bake defect. The same blind spot belongs to **any** graph-less evaluation, including a
  test that poses a `UPoseableMeshComponent` and skins it: every driven helper holds its bind while
  its control swings, so deformation measured that way tears at the deltoid and elbow and blames
  bones nothing drove. The seam was closed as a measurement rather than a bake defect (`e347ebb`),
  which measured both variants — the pose as skinned, and the same pose with
  `FElysiumCompositionRig` applied.
- **`FAnimNode_BlendStack`'s defaults are a minefield, and four of them fail silently.**
  `BlendspaceUpdateMode` defaults to `InitialOnly`, which samples a hosted blend space's xy once at
  `BlendTo` and never again — a gait fan freezes at the steering value it was entered with.
  `BlendParametersDeltaThreshold` defaults to `0`, and a plain *sequence* player answers
  `GetBlendParameters()` with the zero vector, so any non-zero requested parameter pushes a new
  player every frame; a threshold no steering value can reach is what turns the comparison off.
  `bResetOnBecomingRelevant` defaults to `true`, and it pairs with `FAnimNode_BlendListBase`'s
  `ZERO_ANIMWEIGHT_THRESH` child skip: a full-weight blend-list sibling makes the stack
  non-relevant, so the default `Reset()`s it and restarts the clip at frame 0 the moment the
  sibling releases. And `bLoop` is read only inside `BlendTo` — `ConditionalBlendTo` returns early
  when the requested asset matches the playing one, so a loop flip on the *same* asset holds the
  pin and changes nothing; `ForceBlendNextUpdate()` is the door, and it must not be called on an
  empty stack, where the flag survives the blend and forces a second one.
- **`EAlphaBlendOption::HermiteCubic` is the engine's own default, so it never appears in exported
  T3D.** A graph text round-trip cannot prove the curve, and `UAnimGraphNode_BlendStack::Serialize`
  carries a downgrade-to-`Linear` path on an old custom version — only an assertion against the
  compiled node proves what is actually running.
- **`GetSlotMontageGlobalWeight` is filled during graph *evaluation*, not `TickAnimation`.** Read
  in a tick-time path it answers the previous frame's weight or zero.
- **`GetRelevantAnimTimeFraction` returns `0.0` both at the start of a clip and when there is no
  relevant player at all.** The two are indistinguishable from that call alone;
  `GetRelevantAnimLength` is what disambiguates them.
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
