# Elysium — the C++ runtime (`Source/ElysiumUE/`)

The runtime half of the two clean halves (repo-root `CLAUDE.md`). A map's **look** is offline-baked
into `.uasset` content and a `.umap` under the `/ElysiumBaked` mount; the runtime spawns into that
level, adopts its actors, and builds everything the bake cannot hold — brush collision, ropes, the
entity substrate, entity-driven bodies, the sky cubemap — from the pipeline's on-disk intermediates,
with no coordinate conversion.

**This file maps what exists and where, not how it behaves.** Design — lifetimes, the frame, the
object graph, ownership rationale — belongs to `docs/architecture/runtime-architecture.md` (the spine),
`docs/architecture/engine-core.md` (entity object model), `docs/architecture/save-architecture.md` (persistence),
`docs/vtmb/camera-view-modes.md`, `docs/architecture/ui-architecture.md`, `docs/architecture/input-architecture.md`,
`docs/vtmb/python_bridge.md`, `docs/architecture/debug-tooling.md`, `docs/architecture/map-architecture.md`. Per-task status:
`docs/project/roadmap.md`. Gotchas below are the one exception.

## Module

UE 5.8. Module `ElysiumUE` (Runtime, Default loading phase).

- **Plugins:** `ProceduralMeshComponent`; `PythonScriptPlugin` (offline scaffolding only); `Cog`
  (vendored MIT debug-UI shell, `Plugins/External/Cog/`, stripped from Shipping via `ENABLE_COG`);
  `glTFRuntime` (vendored MIT, the NPC skeletal path — `USkeletalMesh` + `UAnimSequence` from `.glb`
  at runtime, no editor import).
- **Third party:** vendored `dr_wav`/`dr_mp3`; CPython 2.7.18 SDK under `ThirdParty/CPython27/`
  (**fetched, not committed** — `pipeline/src/elysium_pipeline/devtools/fetch_cpython27.py`, gitignored, `ELYSIUM_WITH_CPYTHON`,
  Win64 only).

## Source layout

`Private/` is subfoldered **by layer**. A private header is included by its layer path
(`#include "Visual/ElysiumLightRig.h"`), so a cross-layer dependency is visible at the top of the
file. `Public/` stays flat — the module's API surface, not a layering.

| Folder | Layer |
|---|---|
| `Map/` | the map actor, the map subsystem, brush bodies, the walkable surface |
| `Substrate/` | Track B — the plain-C++ entity object model, every entity class, I/O, movers, expressions, the rulebook |
| `Scripting/` | the CPython VM, the script hosts and natives, the `.dlg` conversation machine |
| `Visual/` | everything that produces or tunes what is **rendered** — materials, textures, the light rig, sky/fog, decals, prop skins, NPC meshes and animation, ropes, entity bodies |
| `Audio/` | the decoders, the voice mixer, the SoundScheme system |
| `Player/` | the pawn, movement, the camera, input, the command bus |
| `UI/` | the HUD, the menu, the widgets, the presentation publisher |
| `Debug/` | the Cog windows, the dev console, MCP, the probes and headless harnesses |
| `Session/` | the game/flow state subsystems, the clock, persistence, the RNG streams |
| (root) | `ElysiumUE.cpp` plus the two module-wide readers, `ElysiumContentPaths.h` and `ElysiumKeyValues.h`, which every layer includes bare |

## Config

`Config/DefaultEngine.ini` (boot map, game mode/instance, render path, trace channels) and
`Config/DefaultInput.ini` (engine-side settings only — no action/axis mappings; those are
installed by `UElysiumInputRouter`). Boot decision + flow: `docs/architecture/runtime-architecture.md`.
`FElysiumContentPaths::Root()` is the pipeline's `$ELYSIUM_EXPORT_ROOT` mount point.

## Key type index

Grep entry points, one line each — semantics live in the design doc named per group.

**Map** (`docs/architecture/map-architecture.md`, `docs/architecture/engine-core.md`): `AElysiumMapActor` (owns one map's
epoch) with three components — `UElysiumMapVisuals` (`Visual/`, the look), `UElysiumMapCollision`
(`Map/`, the walkable surface), `UElysiumEntityBodies` (`Visual/`, NPC/prop body factory) — exposed
as `GetVisuals()`/`GetCollision()`/`GetBodies()`, no forwarders. Its runtime phase is
`Building → WaitingForPrerequisites → Activating → Active|Failed`; the map subsystem forwards the
current actor's one-shot ready/failed delegates. Visual readers:
`FElysiumObjModel`, `FElysiumTextureCache`, `FElysiumMaterialFactory`, `ElysiumReflections.h`,
`FElysiumDecals`, `FElysiumRopes`, `UElysiumLightRig`, `FElysiumSkyDef`,
`ElysiumEnvironment.{h,cpp}`, `ElysiumFog.h`.

**Entity substrate / Track B** (`docs/architecture/engine-core.md`), plain C++, no UObject reflection:
`FElysiumVariant`, `FElysiumEntityHandle`, `FElysiumEntityDef`/`FElysiumEntityDefs`,
`FElysiumEntity`, `FElysiumClassDesc`/`FElysiumClassRegistry`, `FElysiumEntityWorld` (owned by
`AElysiumMapActor`), `UElysiumBrushComponent`, `FElysiumEventQueue`/`FElysiumIOEvent`,
`IElysiumIOSink`. Every input goes through `FElysiumEntityWorld::AcceptInput`/the event queue, and
time comes from the substrate clock, never `FTimerManager` — this holds everywhere in the layer.
`Load()` constructs a dormant world; a direct caller must `Activate(Now)` before driving gameplay.

The character chain in `Public/ElysiumPlayer.h` is VtMB's own: `FElysiumEntity` (CBaseEntity) →
`FElysiumAnimating` (CBaseAnimating) → `FElysiumCombatCharacter` (CBaseCombatCharacter) →
`FElysiumNpc` (CAI_BaseNPC) / `FElysiumPlayer` (CBasePlayer). `SpawnPlayer()` creates the player
entity, classname `player`, targetname `!player` (the name the maps themselves write).
`FindPlayer()`/`PlayerHandle()` are the accessors; every reader handles null.

The character sheet on that chain is `FElysiumSheet` (`Public/ElysiumPlayer.h`) over the compiled
slot tables in `Public/ElysiumSheetSlots.h` + `Substrate/ElysiumSheet.cpp`; the values it seeds from
are the rulebook's, reached as `World->GetGameState()->Stats()`. The arithmetic over those slots is
`Substrate/ElysiumSheetMath.{h,cpp}` — `FElysiumSheetEffects` (a character's resolved
`m_tEffectList`), `ElysiumFeats::FeatValue`/`Calc` (what `CalcFeat` answers), `ElysiumXp` (the award
banking) and `ElysiumSheetRules::EvalPredependency`. Quests sit beside it in the same shape:
`Substrate/ElysiumQuestLog.{h,cpp}` is the pure decision (`ElysiumQuestLog::Apply` — resolve,
gate, reconcile the `FElysiumAssignedQuest` rows on the player record), and
`UElysiumGameStateSubsystem::SetQuestState` is the funnel that performs what it reports.
`Substrate/ElysiumQuestView.{h,cpp}` is the read side of the same rows — `ResolveRow` joins one row
to the catalogue, `Build` splits a hub's worth into the three columns the quest log draws — and both
the screen and the `elysium.quest` verb go through it. `Substrate/ElysiumChargen.{h,cpp}` is the
same shape again for character generation: `FElysiumChargenState` + `FElysiumChargenRules` (the
rulebook tables gathered into one view), `BuildPools`/`ApplyBaseline`/`CanBuy`/`Buy`/`Sell`/
`IsRowVisible`/`SuggestClan`.
**Gotcha:** the sheet's own
health slots register as `vhealth`/`vmax_health`, not `health`/`max_health` — those two are
`CBaseEntity` keyfields on the same chain, and the registry resolves derived-shadows-base, so a
sheet slot taking the bare name would silently repoint `trigger_hurt`. `Elysium.Substrate.Sheet`
guards it.

Entity class implementations: `ElysiumStarterClasses.cpp` (logic_auto/relay, triggers,
`logic_pythoncheck`), `ElysiumLogicClasses.cpp` (math_counter, logic_timer, logic_case, env_fade,
func_brush, point_teleport), `ElysiumMover.{h,cpp}` (`FElysiumMoverBase`, `FElysiumDoorBase`,
`FElysiumFuncDoor`, `FElysiumButton`), `ElysiumSignClasses.cpp`, `ElysiumAmbientGeneric.cpp`,
`ElysiumEventClasses.cpp`, `ElysiumNpcClasses.cpp`, `ElysiumPlayerClasses.cpp`,
`ElysiumScriptedSequence.cpp`, `ElysiumPropClasses.cpp`, `ElysiumChoreoScene.cpp`
(`logic_choreographed_scene`, over the `.vcd` reader `ElysiumSceneData.{h,cpp}` and the event
timeline `ElysiumScenePlayer.{h,cpp}` — both free of the world so 12.2's per-line dialogue path can
reuse them).

**The outbound seam** (`docs/architecture/runtime-architecture.md`): everything the substrate needs from the
engine arrives as `FElysiumWorldServices`. `IElysiumEmbodiment` (bodies + the player's own view/
teleport/damage/`+use`/camera, implemented by `AElysiumMapActor`), `IElysiumAudio` (voice,
`AElysiumMapActor`), `IElysiumTravel` (`AElysiumMapActor`), `IElysiumPresenter` (fades/signs/
dialog moments, `UElysiumPresentationSubsystem`), `IElysiumWeather` (wetness and particle state,
`AElysiumMapActor`). Any member may be null; every call site handles it.
`Private/Tests/ElysiumTestServices.h` is the recording stub implementing all five.

**Subsystems by scope** (`docs/architecture/runtime-architecture.md`): GameInstance —
`UElysiumGameFlowSubsystem` (app state), `UElysiumGameStateSubsystem` (`G`, quest map, player
record, clock, snapshots, script host), `UElysiumMapSubsystem` (travel), `UElysiumSaveSubsystem`,
`UElysiumUISubsystem`, `UElysiumAudioSubsystem`, `UElysiumNpcAnimSubsystem`,
`UElysiumRulebookSubsystem`. World — `UElysiumPresentationSubsystem`. LocalPlayer —
`UElysiumInputSubsystem` (the only `SetInputMode` caller). Engine — `UElysiumMcpSubsystem`.

**Player, commands, camera** (`docs/vtmb/controls.md`, `docs/architecture/input-architecture.md`,
`docs/vtmb/camera-view-modes.md`): `FElysiumCommands` (`Public/ElysiumCommands.h`, the verb registry),
`ElysiumCommandBus`, `FElysiumConsole`, `FElysiumUserCmd`/`Builder`/`Stream`, `ElysiumBinds`,
`UElysiumInputRouter`, `IElysiumPlayerBody`, `AElysiumPawn` + `UElysiumMovementComponent` (the
faithful body) over `ElysiumMoveSolve.h` (`docs/vtmb/source_movement.md`: `namespace ElysiumMove`'s
constants + the `CGameMovement` math as free functions, plus `FElysiumMoveTuning`'s `sv_*` cvar
surface — the same pure-rules/engine-half split as `ElysiumCameraSolve.h`),
`AElysiumCapsulePawn` (the `elysium.SourceMovement 0` A/B baseline),
`FElysiumCameraWeights`/`FElysiumCameraShotStack`/`UElysiumCameraComponent`,
`FElysiumViewState`. Actors: `AElysiumGameMode`, `AElysiumPlayerController` (hosts
`UElysiumCheatManager` and the router), `AElysiumPawn`, `AElysiumHUD` (Canvas, does not tick).

**Scripting, audio, shared readers**: `IElysiumScriptHost` (`FElysiumCPythonScriptHost` over
`FElysiumPythonVM` the map-load default; `FElysiumExprScriptHost`/`FElysiumNullScriptHost`
fallbacks), `ElysiumPythonEntity.{h,cpp}`, `ElysiumScriptNatives.{h,cpp}`, `FElysiumScriptFS`,
`ElysiumDlg.{h,cpp}`. Audio: `UElysiumAudioSubsystem` + `FElysiumSoundCache` +
`FElysiumSoundSchemeManager` (every voice passes `elysium.Mute`, default 1, a gain multiplier).
Shared readers: `ElysiumKeyValues.h`, `ElysiumRulebook.{h,cpp}`, `FElysiumSignData`.

## Debug layer (non-Shipping)

`UElysiumCogSubsystem` (`#if ENABLE_COG`) registers the stock CogEngine windows plus the Elysium
ones (`_Status`, `_Maps`, `_Lights`, `_Entities`, `_Inspector`, `_EventQueue`, `_WorldViz`,
`_Audio`, `_SoundScheme`, `_Logic`, `_Scripting`, `_Npc`, `_Environment`) over
`FElysiumCogWindow`.
`UElysiumEntityDebugSubsystem` hosts the `elysium.ent_*` verbs and world-viz layers.
`ElysiumPick.{h,cpp}` is click-selection; `FElysiumGizmoLayer` the retained gizmo ISM.
`UElysiumMcpSubsystem` is Layer 3, reached through `pipeline/src/elysium_pipeline/devtools/mcp_proxy.py`. Design:
`docs/architecture/debug-tooling.md`.

Headless self-driving harnesses, all armed from `UElysiumMapSubsystem::Initialize` on a
command-line flag and all exiting when done: `FElysiumProfileRun` (`-ElysiumProfile`),
`FElysiumShotRun` (`-ElysiumShots`), `FElysiumProbeRun` (`-ElysiumProbe`), `FElysiumMoveRun`
(`-ElysiumMove`, courses in `ElysiumMoveCourses.h`, driven by `uv run elysium debug move`, compared by
`pipeline/src/elysium_pipeline/validation/move_diff.py`).

Automation tests live in `Private/Tests/`: `ElysiumSubstrateTests.cpp` (content-free, `-nullrhi`)
and `ElysiumContentTests.cpp` (parses real exports, self-skips when `$ELYSIUM_EXPORT_ROOT` is empty).

## Engine gotchas

Hard-won, non-obvious, and easy to undo:

- **The vendored Cog shell has two local interaction patches.** `FCogImguiContext::SetEnableInput`
  fully restores high-precision locked mouselook when Cog closes, and `UCogSubsystem::RenderMenuItem`
  keeps navigation click-to-open instead of rendering whole live windows on hover. Reapply both when
  updating Cog. `SetEnableInput` dereferences the lazily created ImGui context, so every boot-time call
  guards on `GetEnableInput()` first.
- **The loading screen hooks `IGameMoviePlayer::OnPrepareLoadingScreen`, not `PreLoadMap`** — the
  movie player binds `PreLoadMap` itself at engine init, ahead of any GI subsystem. Its blocking
  screen auto-completes; `PostLoadMapWithWorld` installs the same visual in the player UI root's
  runtime-loading layer until the map actor publishes ready.
- **Activatable screens enter through `UElysiumPlayerUISubsystem` containers.** Adding one straight
  to a viewport bypasses CommonUI activation, Back routing and focus restoration; the composition
  policy test rejects direct insertion outside the one root owner.
- **`UElysiumCameraComponent::CalcCameraFor` must delegate to `UCameraComponent::GetCameraView`
  first** — overriding `CalcCamera` without it silently breaks first-person rendering.
- **The player hull is a box, not a capsule** — `StepMove` depends on a flat bottom, and `ACharacter`
  will not take a box root.
- **An NPC movement tick already depends on the map actor while its character stands on map-owned
  collision.** CharacterMovement wires the primary tick of the movement base's owner. GameFrame
  therefore runs from `GameplayTickFunction`, which depends on the motor ticks; adding those
  prerequisites to `AElysiumMapActor::PrimaryActorTick` closes a cycle and floods `LogTick`.
- **`ApplyMaterialOverrides` is lazy** — a runtime `SetMaterial` drops the primitive's built
  texture-streaming data, so albedo and `EnvMask` fall back to a low mip.
- **glTFRuntime's morph-target vertex base is a local patch, and losing it fails silently.**
  `FMorphTargetDelta::SourceIdx` addresses the LOD's vertex buffer;
  `FinalizeSkeletalMeshWithLODs` upstream advances its per-primitive base by `Indices.Num()`
  instead of `Positions.Num()`, so on a multi-primitive mesh every primitive after the first
  writes its deltas at out-of-range vertices and the GPU discards them. Nothing reports an
  error — weights animate, curves arrive, delta magnitudes read correct, and the mesh never
  moves. The fix lives in `dev/dependencies/patches/gltfruntime-skeletal-multiroot.patch` and is
  pinned by `post_patch_tree` in `dev/dependencies.lock.json`; `Plugins/External/` is gitignored,
  so editing the vendored tree directly is lost on the next `deps sync`. Change the patch, not the
  checkout, and re-pin the tree hash. `Elysium.Content.FacialMorphTargets` guards the contract.
- **`UBodySetup::CalculateMass` reads the owning primitive's `FBodyInstance`**, which a runtime-built
  component never seeds from the asset — physics props re-apply mass to the component.
- **`USkeleton::AddCurveMetaData` defaults `bTransact = true`**, which under `WITH_EDITOR` calls
  `GEditor->BeginTransaction`. `run play` is `UnrealEditor.exe -game`, where `GEditor` is null, so a
  runtime curve-metadata write crashes on a null dereference in `-game` while working fine in the
  editor. Pass `bTransact = false` from any runtime path.
- **`UElysiumNpcAnimInstance`'s proxy must implement `UpdateAnimationNode`** — a sequence player never
  `Update_AnyThread`'d holds its start frame forever.
- **A `UBlendProfile`'s mode has to be set before its bone scales.** An entry equal to the mode's own
  default is not stored, and that default is 0 for `EBlendProfileMode::BlendMask` against 1 for every
  other mode. A profile still in its constructed `WeightFactor` mode therefore discards every 1.0
  written into it and saves empty — which reads at evaluation as owning the whole rig, the exact
  opposite of the mask that was asked for, with nothing logged.
- **`UAnimSequence::GetAnimationPose` silently falls back to the raw data model** whenever the
  compressed data for the current platform is not resident yet, and compression runs asynchronously
  after a bake. So the same call answers out of two different representations depending on how much
  work happened earlier in the same process, and a test that reads an additive can pass and fail on
  the same assets across runs. Call `WaitOnExistingCompression()` first when the assertion is about
  what a cooked build ships; `IsCompressedDataValid()` is how a caller tells which one it got.
- **`+use` and the debug pick use dedicated channels** (`ELYSIUM_USE_CHANNEL` /
  `ELYSIUM_PICK_CHANNEL`), because the walkable surface is a material-less `.hulls` collider that
  would otherwise be reported instead of the wall.
- **A sheet recompute needs BOTH the rules and the effect layer** — `RecomputeCurrent(Stats)` still
  compiles (the effect argument defaults to null) and silently drops every clan bane and gift. On a
  character, go through `FElysiumCombatCharacter::RecomputeSheet()`, which passes both and re-derives
  the `health` keyfields after.
- **`SetQuestState` pays out; a save load must not go through it** — it resolves the completion
  state and fires `AwardMoney`/`AwardXP`/`Event`, so restoring a payload key-by-key through it would
  replay the whole run's awards. `RestoreQuests` is the silent bulk door, and it is the only one.
- **Save omission diffs against a post-Load baseline, not zero** — a fresh-constructed reference
  omits the wrong things and a restored map re-runs every `logic_auto` ignition.
- **Resolved `UAnimSequence`s cache on the map actor, not the subsystem** — glTFRuntime binds each to
  a specific `USkeleton`, and meshes are per-map-epoch.
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

## Build and test loop

After a C++ change: `uv run elysium build`, then `uv run elysium test <tier>` — `Substrate` for anything under the
substrate, scripting, session, player or UI layers, `Content` when the change reads `$ELYSIUM_EXPORT_ROOT`. Both
are cheap: the build is adaptive non-unity (~10 s for a handful of files), and the Substrate tier
runs in about the same under `-nullrhi`.

**The result surface is the report, not stdout.** Every run writes JSON + HTML under
`$ELYSIUM_EXPORT_ROOT/_tests/` and `uv run elysium test` reports the path and propagates the exit code.

**One run per change, not one per claim.** A green tier stays green until code moves. A roadmap
task's acceptance list is a set of things that must be **true**, not a set of runs to perform.

**A live run is proposed, never assumed — ask the owner first, with a recommendation.** The ask names
what the live run would answer *that the tiers cannot*. **Worth it** when the claim only exists in a
built world: the tick graph and its prerequisites, map build/adoption and activation barrier,
pawn ↔ mover collision, the camera solve, anything tracing real geometry, or a script/dialogue path
needing the level script running. **Not worth it** for plain-C++ work the Substrate tier covers, for
doc-only changes, or for anything a green tier already answered. When in doubt the recommendation is
*no* — the headless one-shot harnesses are the pattern for turning a repeated live check into a
per-change one.

## Console commands

Every runtime verb is an `elysium.*` console command; the live set is whatever the module registers
(`FAutoConsoleCommand`/`FAutoConsoleVariableRef`). `elysium.commands [filter]` reads the coverage
back as a work list.
