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
  (vendored MIT debug-UI shell, `Plugins/External/Cog/`, stripped from Shipping via `ENABLE_COG`).
  Cog is the only vendored plugin; every skeletal asset is constructed from the `.eskm` container
  by `UElysiumSkeletalBuildLibrary`, so nothing third-party reads a model.
- **Third party:** vendored `dr_wav`/`dr_mp3`; CPython 2.7.18 SDK under `ThirdParty/CPython27/`
  (**fetched, not committed** — `pipeline/src/elysium_pipeline/devtools/fetch_cpython27.py`, gitignored, `ELYSIUM_WITH_CPYTHON`,
  Win64 only).

## C++ coding policy

The baseline for new and touched runtime code is Epic's
[C++ Coding Standard for Unreal Engine](https://dev.epicgames.com/documentation/unreal-engine/epic-cplusplus-coding-standard-for-unreal-engine).
Use Epic's [Object Pointers](https://dev.epicgames.com/documentation/unreal-engine/object-pointers-in-unreal-engine),
[Reflection System](https://dev.epicgames.com/documentation/unreal-engine/reflection-system-in-unreal-engine)
and [Include What You Use](https://dev.epicgames.com/documentation/unreal-engine/include-what-you-use-iwyu-for-unreal-engine-programming)
guides for the corresponding engine semantics. This is a code and review contract, not a license
header: never copy Epic's copyright notice into
project-owned source. Repository `.clang-format` owns C++ layout and `.editorconfig` owns the
remaining whitespace rules. Format touched code only; do not mix a formatting sweep with a
behavioral change.

- **Language and portability:** UE 5.8 code is C++20, constrained by Epic's cross-compiler rules.
  Prefer `nullptr`, `override`/`final`, `static_assert`, range-based loops, strongly typed enums and
  const-correct code. Keep types explicit; use `auto` only for lambdas, unwieldy iterators or
  template cases where spelling the type harms clarity. Use explicit lambda captures, especially
  for deferred work, and never capture a short-lived reference or an untracked `UObject` into it.
- **Unreal names and reflection:** use the `U`/`A`/`F`/`T`/`I`/`S`/`E` prefixes and `b` for
  booleans; boolean queries read as questions. Add `UCLASS`, `USTRUCT`, `UFUNCTION` and `UPROPERTY`
  only where the engine must see the type or member. The plain-C++ substrate remains
  reflection-free.
- **Object references:** a persistent, engine-tracked `UObject` field uses `UPROPERTY()` with
  `TObjectPtr<T>`. Short-lived locals and parameters normally use `T*`; expiring non-owning
  references use `TWeakObjectPtr<T>`; load-on-demand asset references use `TSoftObjectPtr<T>`.
  `TStrongObjectPtr<T>` is reserved for the uncommon strong reference owned outside a `UObject`.
- **Headers and modules:** every header includes what it needs; every `.cpp` includes its matching
  header first. Prefer forward declarations and fine-grained includes, never `Engine.h` or
  `UnrealEd.h`, and do not put `using` declarations in global scope. Dependencies belong in the
  narrowest correct `Build.cs` list.
- **File granularity:** one primary class — or one small, tightly coupled cluster (a class plus
  its private helpers) — per `.h`/`.cpp`, under the owning layer folder. A new class never lands
  inside an existing multi-class file. When a change substantially touches a class that lives in
  an oversized multi-class file, first move that class verbatim into its own file (includes by
  layer path, a thin registration site may remain behind), then make the behavioral edit — and
  keep the verbatim move and the behavioral change reviewable as separate diffs (separate
  commits when both land together). Pure moves change no behavior and no names.
- **APIs and diagnostics:** avoid boolean flag lists and long parameter lists; use an enum or a
  parameter struct. Use `TEXT()` for Unreal string literals, sized integers for serialized or
  replicated formats, named log categories, and the appropriate `check`/`verify`/`ensure` family.
  Address compiler warnings. Comments explain intent, units, constraints and non-obvious safety,
  not a paraphrase of the implementation.

## Gameplay integration contract

For every gameplay implementation or change in this runtime, first read and use
`docs/architecture/gameplay-systems-architecture.md` as the governing design guidance. This
includes map entities, entity I/O, the script bridge, interactive props, and every gameplay
domain. When working in existing gameplay code, assess the touched code against that architecture
and propose concrete changes for every divergence needed to bring it into conformance. Implement
those corrections when they are within the requested scope; otherwise report them explicitly
rather than expanding the task without authorization. The architecture composes the entity rules
**R1–R8** in `docs/architecture/engine-core.md`, the runtime rules **S1–S12** in
`docs/architecture/runtime-architecture.md`, and its own compatibility rules **K1–K13**. Exact
VtMB behavior remains in the owning `docs/vtmb/` document; this file carries only the coding
contract:

- **Keep one object language.** Gameplay identity and mutable map state live on plain-C++
  `FElysiumEntity` classes and the declared session/save structures. Unreal actors and components
  are optional bodies for rendering, collision, movement and overlap; they do not become a second
  gameplay model. The substrate reaches them only through nullable `FElysiumWorldServices`.
- **Unreal owns the engine; the substrate owns the game.** A Source subsystem is reproduced only
  when authored content or a game rule names its behavior — the Ownership test in
  `docs/project/remaster-direction.md`, with the closed register of deliberate reproductions in
  `docs/project/rebuild-strategy.md`. Geometry, visibility, reachability and physics are asked of
  the engine through the service seam, never approximated with substrate arithmetic. Reproducing
  Source's rules (formulas, call order, thresholds) is faithful; porting its mechanisms is a
  defect, and a port outside the register is a bug.
- **Preserve the addressability boundary.** Non-addressable GAME_LUMP dressing may be placed in the
  baked level. Anything a map or script can name, mutate, hide, use, save, receive an input on, or
  fire an output from remains a live `.ents` entity; baking its mesh must not bake away its entity
  identity. Runtime readers consume exporter-produced Unreal-native coordinates verbatim.
- **Add no fourth legacy API tier.** Tier 1 is the case-folded class-chain input/field surface;
  Tier 2 is the single retail-evidenced `GNativeBindings` table; Tier 3 is the console bridge. Map
  outputs, `logic_pythoncheck`, dialogue, `ScheduleTask` and level scripts converge on the installed
  script host and the same domain implementations. Do not add a per-surface adapter, catch-all
  dispatcher or duplicate native table.
- **Bind a name at its retail kind.** A datamap input stays a registered class-chain input, a
  Character method stays a Tier-2 row, and a script helper stays in `__main__`; the receiver may
  distinguish identical spellings. An unimplemented recovered input uses
  `ELYSIUM_PENDING_INPUT`; every other gap reports through the existing stub and wire-accounting
  funnels and returns the retail-shaped failure/default. Never silently succeed, invent a receiver,
  or repair authored defects without an explicit divergence in the owning VtMB document.
- **Use one event transport and preserve its order.** Real producers call `FireOutput` or enqueue
  owned work; only queue service delivers. Do not call a receiver synchronously from a producer,
  prebind a target name, or add `FTimerManager`, a latent action, a private timer/event list, or a
  second scheduler. The recovered synchronous Python reflected-input call still passes through
  `AcceptInput`; any outputs it fires rejoin the ordinary queue. Follow the exact row, deadline,
  equal-time, late-binding and service order in the gameplay architecture and `docs/vtmb/entity_io.md`.
  The documented determinism divergences are a closed set; any other ordering difference is a bug.
- **Keep the two Python systems separate.** `pipeline/` and Unreal editor Python are offline
  export/generation tools and never run to produce game content at runtime. Embedded CPython is the
  runtime host for the user's loose VtMB scripts. Level scripts load into the shared `__main__`
  before the entity spawn pass; Python entity attributes resolve through the same class chain as
  I/O. Scripts remain user-install data: do not commit a hand-fixed fork or grow a native binding
  without retail evidence.
- **Give every value and rule one owner.** Persistent state is a Save-flagged registered field, a
  session-record member, or a declared save block. Recovered catalogs load patch-first through the
  rulebook into typed tables; do not retype their constants into C++. Keep combat relationship,
  emotional disposition and RPG reaction separate; keep ratings separate from rolls; route typed
  damage through the shared descriptor/commit path; transfer NPC body control through its owner
  arbiter.
- **Land domains behind the existing seams.** A gameplay addition consists of class-chain
  fields/inputs/outputs, one plain-C++ domain service, declared command verbs where player-facing,
  and save state in an existing home. It does not add a dispatcher, clock, input owner, presentation
  backchannel or save path. UI reads published view state and sends intent through the command bus.
- **Prove the real producer path.** First add content-free Substrate coverage against
  `Private/Tests/ElysiumTestServices.h`; add Content coverage only when the real export corpus is
  required. Use `elysium.stubs`, `elysium.classes`, `elysium.wires` and the I/O history to distinguish
  never produced, missing target, missing input, refused receiver and invisible side effect. A quiet
  log is not acceptance, and debug injection does not prove an authored event producer.

For a gameplay change, review the diff by asking two questions: does it put a real implementation
behind these existing entity/API/event/save seams, or does it create another route around them —
and does it use Unreal's mechanism for everything the ownership register does not reserve? Only a
change that passes both belongs in the runtime.

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
`ElysiumEnvironment.{h,cpp}`, `ElysiumFog.h`. `UElysiumWieldTable`
(`Public/ElysiumWieldTable.h`) is the `/ElysiumBaked/Items/DA_WieldModels` row table the wield bake
writes and `(classname, sex)` resolves through; design:
`docs/architecture/wielded-weapon-integration.md`.

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
banking) and `ElysiumSheetRules::EvalPredependency`. `Substrate/ElysiumDice.{h,cpp}` is the d10
resolver beside it (`ElysiumDice::Roll` over the rulebook's `FElysiumDiceTables` and the Dice RNG
stream; `elysium.roll` drives it; `CalcFeat` stays a rating, never a roll). Quests sit beside it in the same shape:
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
`logic_pythoncheck`) over the shared `ElysiumTriggerBase.h` (`FElysiumTriggerBase`, the base every
trigger leaf derives from) and the shared keyfield-adder template `Substrate/ElysiumClassFields.h`,
`ElysiumLogicClasses.cpp` (math_counter, logic_timer, logic_case, env_fade,
func_brush, point_teleport), `ElysiumMover.{h,cpp}` (`FElysiumMoverBase`, `FElysiumDoorBase` and the
two `func_door` leaves) with `ElysiumButton.cpp`, `ElysiumElevator.cpp`, `ElysiumFuncRotating.cpp`
and the manifest loader `ElysiumMoverSounds.cpp` beside it, `ElysiumSignClasses.cpp`, `ElysiumAmbientGeneric.cpp`,
`ElysiumEventClasses.cpp`, `ElysiumScriptedSequence.cpp`, `ElysiumPropClasses.cpp` (the prop
registration site over `ElysiumProp.{h,cpp}`, `ElysiumPropLeaves.{h,cpp}` and
`ElysiumPhysProp.{h,cpp}`), `ElysiumItemClasses.{h,cpp}`
(`FElysiumItem`/`FElysiumKeyring` — one registered class per `vdata/items` definition, installed at
the rulebook's first `Items()` load; the loot container is `ElysiumItemContainer.{h,cpp}`, and
`FElysiumInventory` lives on the combat character, implemented in `ElysiumInventory.cpp`),
`ElysiumFeed.{h,cpp}` (the feed transaction and paired state machine on the combat character),
`ElysiumChoreoScene.cpp`
(`logic_choreographed_scene`, over the `.vcd` reader `ElysiumSceneData.{h,cpp}` and the event
timeline `ElysiumScenePlayer.{h,cpp}` — both world-free and shared by map-authored scenes and
per-line dialogue playback). `Substrate/ElysiumPendingInput.h` is the registration form for a recovered datamap
input with no system behind it yet; `elysium.stubs` reads the fired set back, and
`Elysium.Content.ScriptApiCoverage` asserts every corpus-called name resolves backed-or-pending.

The character chain's leaves are one class per file, with a registration site beside them.
`ElysiumNpcClasses.cpp` registers the `npc_*` family, `intersting_place` and the two `npc_maker`
classnames; the classes are `ElysiumInterestingPlace.{h,cpp}`, `ElysiumScriptedCharacter.{h,cpp}`,
`ElysiumNpc.{h,cpp}` (`FElysiumNpc` plus the scene-owned `FElysiumPlayerControllerNpc` duplicate)
and `ElysiumNpcMaker.{h,cpp}`, logging through `ElysiumNpcLog.h`. `ElysiumPlayerClasses.cpp`
declares the player chain's fields, inputs and outputs; the three implementations are
`ElysiumAnimatingImpl.cpp`, `ElysiumCombatCharacter.cpp` and `ElysiumPlayerEntity.cpp`, logging
through `ElysiumPlayerLog.h`.

**Gameplay domain services**, plain C++ under `Substrate/` — the design is
`docs/architecture/gameplay-systems-architecture.md` §5 and the behaviour is the `docs/vtmb/` doc
each file names in its header:
`ElysiumDamage.{h,cpp}` (`FElysiumDmg` + `ElysiumDamage::Apply`; the one typed health commit is
`FElysiumCombatCharacter::CommitDamage` in `ElysiumCombatCharacter.cpp`),
`ElysiumWeaponClasses.{h,cpp}` (`FElysiumWeapon` on the `CWeapon` chain node — mode dispatch, the
two-half attack transaction, reload),
`ElysiumGameSound.{h,cpp}` (`FElysiumGameSoundBus` behind `FElysiumEntityWorld::EmitGameSound`),
`ElysiumNpcSenses.{h,cpp}` (`FElysiumNpcPerception`, `FElysiumNpcMemory`),
`ElysiumNpcConditions.{h,cpp}` (`EElysiumNpcCond`, `FElysiumNpcConditions`, the ideal-state pass),
`ElysiumNpcEnemy.{h,cpp}` (the enemy-selection transaction),
`ElysiumSchedule.{h,cpp}` (`FElysiumSchedule`, the task vocabulary and its executor),
`ElysiumNpcCombatSchedules.{h,cpp}` (the recovered combat families and their selector),
`ElysiumNpcLoadout.{h,cpp}` (the weapon-capability join),
`ElysiumNpcWitness.{h,cpp}` (the two per-NPC player-law observation lanes),
`ElysiumAiScriptedSchedule.{h,cpp}` (`aiscripted_schedule`'s mode table and `forcestate` mapping),
`ElysiumDisciplines.{h,cpp}` (active states and the targeted cast transaction),
`ElysiumStealth.{h,cpp}` + `ElysiumStealthTrigger.{h,cpp}` (the player target surface and
`trigger_stealth_mod`),
`ElysiumLaw.{h,cpp}` + `ElysiumActivityTrigger.{h,cpp}` (the player's activity/Masquerade/pursuit
channels and `trigger_player_activity_level`),
`ElysiumReaction.{h,cpp}` (the RPG reaction score),
`ElysiumRelationships.{h,cpp}` and `ElysiumDisposition.{h,cpp}` (two of K4's three separate
social stores). The `FElysiumNpcMind` state machine and body-owner arbiter stay in
`ElysiumNpcMind.{h,cpp}`.

**The outbound seam** (`docs/architecture/runtime-architecture.md`): everything the substrate needs from the
engine arrives as `FElysiumWorldServices`. `IElysiumEmbodiment` (bodies + the player's own view/
teleport/damage/`+use`/camera, plus the geometry queries `QueryLineOfSight`/`QueryLightAtPoint`,
implemented by `AElysiumMapActor`; the per-body `IElysiumNpcMotor` beside it carries
`ProjectToNavigable`), `IElysiumAudio` (voice,
`AElysiumMapActor`), `IElysiumTravel` (`AElysiumMapActor`), `IElysiumPresenter` (fades/signs/
dialog moments, `UElysiumPresentationSubsystem`), `IElysiumWeather` (wetness and particle state,
`AElysiumMapActor`). Any member may be null; every call site handles it.
`Private/Tests/ElysiumTestServices.h` is the recording stub implementing all five.

**Subsystems by scope** (`docs/architecture/runtime-architecture.md`): GameInstance —
`UElysiumGameFlowSubsystem` (app state), `UElysiumGameStateSubsystem` (`G`, quest map, player
record, clock, snapshots, script host), `UElysiumMapSubsystem` (travel), `UElysiumSaveSubsystem`,
`UElysiumUISubsystem`, `UElysiumAudioSubsystem`, `UElysiumAnimSubsystem`,
`UElysiumRulebookSubsystem`. World — `UElysiumPresentationSubsystem`. LocalPlayer —
`UElysiumInputSubsystem` (the only `SetInputMode` caller). Engine — `UElysiumMcpSubsystem`.

**Player, commands, camera** (`docs/vtmb/controls.md`, `docs/architecture/input-architecture.md`,
`docs/vtmb/camera-view-modes.md`): `FElysiumCommands` (`Public/ElysiumCommands.h`, the verb registry),
`ElysiumCommandBus`, `FElysiumConsole`, `FElysiumUserCmd`/`Builder`/`Stream`, `ElysiumBinds`,
`UElysiumInputRouter`, `IElysiumPlayerBody`, `AElysiumPawn` + `UElysiumMovementComponent` (the
faithful body) over `ElysiumMoveSolve.h` (`docs/vtmb/source_movement.md`: `namespace ElysiumMove`'s
constants + the `CGameMovement` math as free functions, plus `FElysiumMoveTuning`'s `sv_*` cvar
surface — the same pure-rules/engine-half split as `ElysiumCameraSolve.h`),
`FElysiumCameraWeights`/`FElysiumCameraShotStack`/`UElysiumCameraComponent` (the mode weights, the
scripted-shot stack, the fade band and the VtMB cvar surface), `AElysiumPlayerCameraManager` +
`FElysiumCameraSample` (the one final view, the post-layer stack and the boom's state),
`UElysiumCameraModifier` /
`UElysiumCameraModifier_LegacyShot` (`ElysiumCameraModifiers.h`), `ElysiumCameraRig.h`
(`namespace ElysiumRig` — the boom's pure rules and its tuning struct, the same
pure-rules/engine-half split as `ElysiumCameraSolve.h`), `FElysiumViewState`. Actors:
`AElysiumGameMode`, `AElysiumPlayerController` (hosts `UElysiumCheatManager`, the router and
`PlayerCameraManagerClass`), `AElysiumPawn`, `AElysiumHUD` (Canvas, does not tick).

**Scripting, audio, shared readers**: `IElysiumScriptHost` (`FElysiumCPythonScriptHost` over
`FElysiumPythonVM` the map-load default; `FElysiumExprScriptHost`/`FElysiumNullScriptHost`
fallbacks), `ElysiumPythonEntity.{h,cpp}`, `ElysiumScriptNatives.{h,cpp}`, `FElysiumScriptFS`,
`ElysiumDlg.{h,cpp}`. Audio: `UElysiumAudioSubsystem` + `FElysiumSoundCache` +
`FElysiumSoundSchemeManager` (every voice passes `elysium.Mute`, default 1, a gain multiplier).
Shared readers: `ElysiumKeyValues.h`, `ElysiumRulebook.{h,cpp}` (the shared value types and the
stats/feats/rules/trait-effects/clans/histories/experience/leveling/strings sections) with the
per-section table libraries beside it — `ElysiumItemTable`, `ElysiumDiceTables`,
`ElysiumQuestTables`, `ElysiumChargenWizard`, `ElysiumReactionTables`, `ElysiumStealthTables`,
`ElysiumSoundVolumeTable`, `ElysiumDisciplineTargetTables`, all `{h,cpp}` under `Substrate/` over
the shared loaders `ElysiumVdataLoad.{h,cpp}` — and `FElysiumSignData`.

## Debug layer (non-Shipping)

`UElysiumCogSubsystem` (`#if ENABLE_COG`) registers the stock CogEngine windows plus the Elysium
ones (`_Status`, `_Maps`, `_Lights`, `_Entities`, `_Inspector`, `_EventQueue`, `_WorldViz`,
`_Audio`, `_SoundScheme`, `_Logic`, `_Scripting`, `_Npc`, `_Camera`, `_Environment`) over
`FElysiumCogWindow`.
`UElysiumEntityDebugSubsystem` hosts the `elysium.ent_*` verbs and world-viz layers.
`ElysiumPick.{h,cpp}` is click-selection; `FElysiumGizmoLayer` the retained gizmo ISM.
`UElysiumMcpSubsystem` is Layer 3, reached through `pipeline/src/elysium_pipeline/devtools/mcp_proxy.py`. Design:
`docs/architecture/debug-tooling.md`.

Headless self-driving harnesses, all armed from `UElysiumMapSubsystem::Initialize` on a
command-line flag and all exiting when done: `FElysiumProfileRun` (`-ElysiumProfile`),
`FElysiumShotRun` (`-ElysiumShots`), `FElysiumProbeRun` (`-ElysiumProbe`), `FElysiumMoveRun`
(`-ElysiumMove`, courses in `ElysiumMoveCourses.h` over the generated gym `ElysiumGymSpec.h` /
`ElysiumGymBuilder.h`, recorded through `FElysiumChannelRecorder` over the `ElysiumChannels.h`
registry, driven by `uv run elysium debug move`, compared by
`pipeline/src/elysium_pipeline/validation/channel_diff.py`), `FElysiumCastRun` (`-ElysiumCast`,
courses in `ElysiumCastCourses.h` over the arena `Debug/ElysiumArenaSpec.h` / `ElysiumArenaBuilder.h`,
driven by `uv run elysium debug cast`, same recorder and comparator). The two recording runs share
one schema and one writer, `Debug/ElysiumLocomotionTrace.h`.

Automation tests live in `Private/Tests/`: content-free `Elysium.Substrate.*` suites are split by
domain across the focused `Elysium*Tests.cpp` files and run under `-nullrhi`; `ElysiumContentTests.cpp`
parses real exports and self-skips when `$ELYSIUM_EXPORT_ROOT` is empty. Shared substrate recordings
and engine-service doubles live in `ElysiumTestServices.h`.

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
  both halves; `Elysium.Content.FacialMorphTargets` guards the contract.
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
  per-body bank copy to avoid this engine path; bank package count is independent of body-family
  count. See `docs/architecture/animation-architecture.md` § 2.4.
- **The Content Browser preview runs no anim graph, so it applies no axis interpolation.** A rig
  whose bones are procedurally driven previews with untwisted forearms: the stage evaluates over the
  blended pose rather than being baked into the clip, so the preview is showing what the asset says
  and not a bake defect. The same blind spot belongs to **any** graph-less evaluation, including a
  test that poses a `UPoseableMeshComponent` and skins it: every driven helper holds its bind while
  its control swings, so deformation measured that way tears at the deltoid and elbow and blames
  bones nothing drove. `Instrument.Elysium.DancerDecodeProbe3` measures both variants — the pose as
  skinned, and the same pose with `FElysiumCompositionRig` applied — because the difference between
  the two is the whole distance between a bake question and a measurement one.
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
  one rig family's `USkeleton`, and meshes are per-map-epoch.
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

## Build and test loop

The build command is `uv run elysium build`.

The test command is `uv run elysium test <tier>` — `Substrate` for anything under the substrate,
scripting, session, player or UI layers, `Content` when the change reads `$ELYSIUM_EXPORT_ROOT`.

These commands describe the available surfaces, not automatic permission to run them. Start with
the narrowest owning automation filter or pure-rules test. If validation needs a newly compiled
binary, state the build scope and expected cost and wait for explicit owner acceptance before
invoking `uv run elysium build`. Never start `--clean`, `--rebuild`, an entire `Substrate` or
`Content` tier, or another complete build/test pass as routine validation; each is a separately
planned and accepted operation.

**A live run is proposed, never assumed — ask the owner first, with a recommendation.**

**No feature flag and no A/B toggle without approval — ask the owner first.** Work lands as a
complete change, not behind a switch.

**Do not create cvars without explicit request or approval.** Cog is the debug surface: a new
control is a tab, not a console variable.

## Console commands

Every runtime verb is an `elysium.*` console command; the live set is whatever the module registers
(`FAutoConsoleCommand`/`FAutoConsoleVariableRef`). `elysium.commands [filter]` reads the coverage
back as a work list.
