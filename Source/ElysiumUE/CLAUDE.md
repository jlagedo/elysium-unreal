# Elysium — the C++ runtime (`Source/ElysiumUE/`)

The runtime half of the two clean halves (repo-root `CLAUDE.md`). A map's **look** is offline-baked
into `.uasset` content and a `.umap` under the `/ElysiumBaked` mount; the runtime spawns into that
level, adopts its actors, and builds everything the bake cannot hold — brush collision, ropes, the
entity substrate, entity-driven bodies, the sky cubemap — from the pipeline's on-disk intermediates,
with no coordinate conversion.

**This file is an orientation map: what exists and where.** How it works is the design docs' —
`docs/runtime-architecture.md` (the spine: frame, clock, seams, app state, presentation, input
scopes), `docs/engine-core.md` (entity object model), `docs/save-architecture.md` (persistence),
`docs/camera-view-modes.md`, `docs/ui-architecture.md`, `docs/python_bridge.md`,
`docs/debug-tooling.md`, `docs/map-architecture.md`. Per-task status is `docs/roadmap.md`.
Don't restate any of them here.

## Module

UE 5.8. Module `ElysiumUE` (Runtime, Default loading phase).

- **Plugins:** `ProceduralMeshComponent`; `PythonScriptPlugin` (offline scaffolding only); `Cog`
  (vendored MIT debug-UI shell, `Plugins/Cog/`, stripped from Shipping via `ENABLE_COG`);
  `glTFRuntime` (vendored MIT, the NPC skeletal path — `USkeletalMesh` + `UAnimSequence` from `.glb`
  at runtime, no editor import).
- **Third party:** vendored `dr_wav`/`dr_mp3`; CPython 2.7.18 SDK under `ThirdParty/CPython27/`
  (**fetched, not committed** — `tools/fetch_cpython27.py`, gitignored, `ELYSIUM_WITH_CPYTHON`,
  Win64 only).

## Source layout

`Private/` is subfoldered **by layer**, and the folder a file sits in states which layer it belongs
to. A private header is included by its layer path (`#include "Visual/ElysiumLightRig.h"`), so a
cross-layer dependency is visible at the top of the file. `Public/` stays flat — it is the module's
API surface, not a layering.

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

## Content root and config

- `FElysiumContentPaths::Root()` = `FPaths::ProjectDir()/"tools/out"` (in-repo, gitignored).
- `Config/DefaultEngine.ini` — boot map `/Game/Elysium`, `AElysiumGameMode`, `UElysiumGameInstance`,
  the fully-dynamic render path, the `ElysiumUse` trace channel (`ECC_GameTraceChannel1`).
- `Config/DefaultInput.ini` — engine-side settings only: `ConsoleKeys` = `Tilde` + `F7`, raw
  MouseX/MouseY, FOV scaling and mouse smoothing **off**. It carries **no** action or axis mappings;
  every key is installed by `UElysiumInputRouter` from `ElysiumBinds::Defaults()`.
- Boot is decided once, at GI init, by `UElysiumGameFlowSubsystem::BootFromCommandLine`: menu over a
  backdrop by default, New Game under `elysium.BootMenu 0`, a bare dev map under `-ElysiumMap=<name>`
  / `-ElysiumNewGame=0` (which also seeds a mock character so dialogue gates have a sheet).
  `-ElysiumProfile` / `-ElysiumShots` / `-ElysiumProbe` run the headless harnesses.

## Map and world

`UElysiumMapSubsystem` (UE5 hard travel) → `AElysiumMapActor`, which owns the map for one epoch. The
actor **orchestrates the load; it does not render it** — it owns the load order, the three frame
passes, the player's placement, the entity world / scheme manager / camera director, and three of the
four `FElysiumWorldServices`. Three components carry the rest, and the actor holds none of their
state:

| Component | Owns |
|---|---|
| `UElysiumMapVisuals` (`Visual/`) | the LOOK — `AdoptBakedLevel`, material-override MIDs, sky cube + backdrop, SkyLight level, both fog sets, the Lumen knobs, the light rig, cables, the texture cache, every `elysium.*` look cvar |
| `UElysiumMapCollision` (`Map/`) | the WALKABLE SURFACE — `.hulls` convex + `.dispcol` trimesh onto collision-only PMCs |
| `UElysiumEntityBodies` (`Visual/`) | the BODY FACTORY behind `IElysiumEmbodiment`'s mesh half — NPC skeletal and prop static bodies, prop skins, per-map mesh/anim caches |

Exposed as `GetVisuals()` / `GetCollision()` / `GetBodies()`, with **no forwarders** for the look or
the collider — a façade would rebuild the god object the split removed. The actor does forward the
six `IElysiumEmbodiment` mesh calls, because the substrate's one engine seam is the actor.

Visual/readers worth knowing by name: `FElysiumObjModel` (OBJ/MTL + `.emc` cache),
`FElysiumTextureCache`, `FElysiumMaterialFactory`, `ElysiumReflections.h` (the `$envmap` channel),
`FElysiumDecals`, `FElysiumRopes`, `UElysiumLightRig`, `FElysiumSkyDef`, `ElysiumEnvironment.{h,cpp}`
(sky cube), `ElysiumFog.h` (Source distance fog as per-primitive Custom Primitive Data).

## The entity substrate (Track B)

Plain C++, no UObject reflection — Unreal supplies bodies only.

| Type | Role |
|---|---|
| `FElysiumVariant` | tagged Void/Bool/Int/Float/String/Vector/Handle |
| `FElysiumEntityHandle` | `{Index, Epoch}`, generation-checked through `Resolve` |
| `FElysiumEntityDef`/`FElysiumEntityDefs` | immutable parsed `.ents` records |
| `FElysiumEntity` | the live base entity: CBaseEntity keyfields, runtime origin/angles/model with body-follow hooks, `Kill`/`ScriptHide`/`ScriptUnhide`, dormancy, per-output `times`, `FireOutput`, touch routing |
| `FElysiumClassDesc`/`FElysiumClassRegistry` | per-classname factory, base-chain link, input + typed field tables; inert-record fallback for unregistered classnames |
| `FElysiumEntityWorld` | the substrate: the spawn pass, `SpawnRuntimeEntity`, `RenameEntity`, the `AcceptInput` + event-queue **chokepoints**, output firing, `RouteBrushTouch`, `UpdateUseCursor`/`PlayerUse`, tick, epoch teardown, `AddSink`, and the injected `FElysiumWorldServices`. Owned by `AElysiumMapActor` via `TPimplPtr` |
| `UElysiumBrushComponent` | the per-brush-entity body: collision-only, convex `UBodySetup` from def hulls, dormancy-gated, solidity by classname |
| `FElysiumEventQueue`/`FElysiumIOEvent` | the one time-sorted queue |
| `IElysiumIOSink` | always-on `FElysiumRingBufferSink` (1,000 entries) + `FElysiumLogSink` |

**Two rules that hold everywhere:** every input goes through `AcceptInput`/the event queue (so it is
loggable, pausable, single-steppable, serializable), and time comes from the substrate clock, **never
`FTimerManager`**.

The character chain in `Public/ElysiumPlayer.h` is VtMB's own:

```
FElysiumEntity                     CBaseEntity           keyfields, dormancy, I/O, think
 └ FElysiumAnimating               CBaseAnimating        the body: BuildBody / PlayAnimClip /
    │                                                    ResetAnimToIdle / SetDispositionName
    └ FElysiumCombatCharacter      CBaseCombatCharacter  the SHEET + money/blood/humanity/
       │                                                 masquerade + TakeDamage/OnKilled
       ├ FElysiumNpc               CAI_BaseNPC           dialogue only; body half inherited
       └ FElysiumPlayer            CBasePlayer           player inputs, law counters, XP ledger
```

The player entity is created by `SpawnPlayer()` with classname `player`, targetname **`!player`** —
the name the maps themselves write, so `point_teleport target=!player` is an ordinary name
resolution. `FindPlayer()`/`PlayerHandle()` are the accessors and **every reader handles null** (a
menu backdrop and a headless logic world have no player).

Entity class implementations: `ElysiumStarterClasses.cpp` (logic_auto/relay, triggers,
`logic_pythoncheck`), `ElysiumLogicClasses.cpp` (math_counter, logic_timer, logic_case, env_fade,
func_brush, point_teleport), `ElysiumMover.{h,cpp}` (`FElysiumMoverBase`, `FElysiumDoorBase`,
`FElysiumFuncDoor`, `FElysiumButton`), `ElysiumSignClasses.cpp`, `ElysiumAmbientGeneric.cpp`,
`ElysiumEventClasses.cpp`, `ElysiumNpcClasses.cpp`, `ElysiumPlayerClasses.cpp`,
`ElysiumScriptedSequence.cpp`, `ElysiumPropClasses.cpp`.

## The outbound seam

Everything the substrate needs *from* the engine arrives as **`FElysiumWorldServices`**, taken by
`FElysiumEntityWorld` at construction. Nothing under the world casts its owner to a map actor or
touches `GetWorld()->GetFirstPlayerController()`.

| Interface | Covers | Implemented by |
|---|---|---|
| `IElysiumEmbodiment` | NPC/prop/phys-prop bodies, clips, idles, skins — **and the player's body**: view point, origin+yaw, teleport, damage, the `+use` trace, camera shots | `AElysiumMapActor` |
| `IElysiumAudio` | `PlayVoice`/`StopVoice`/`SetVoiceVolume`/`IsVoicePlaying` + the scheme fades | `AElysiumMapActor` |
| `IElysiumTravel` | `RequestLandmarkTravel`, `ChangeMap` | `AElysiumMapActor` |
| `IElysiumPresenter` | `StartFade`, `OpenSign`/`CloseSign`, `OpenDialog`/`CloseDialog` — the discrete *moments*, not the state | `UElysiumPresentationSubsystem` |

**Any member may be null**, and every call site handles it — that is what lets a whole map's logic
run headlessly. `Private/Tests/ElysiumTestServices.h` is the recording stub implementing all four.

## The frame

Declared in the engine's tick graph (`dumpticks` reads it back), never inferred from registration
order. Retail is **move-first**: the pawn moves out of the `clc_move` drain, before `GameFrame` runs
a single think or queued event.

| # | Stage | Where |
|---|---|---|
| 1 | sample input | `APlayerController` (`TG_PrePhysics`) |
| 2 | advance the clock | `AElysiumMapActor::PreMoveTick`, first statement — **the only place `Now` moves** |
| 3 | the player's own think + the spawn hold | `FElysiumEntityWorld::RunPlayerThink` |
| 4 | **move the pawn** | the movement component, prerequisite on the pre-move tick |
| 5–6 | run due thinks, then service the queue | `FElysiumEntityWorld::Tick` (think-first) |
| 7 | physics + overlaps | engine (`TG_DuringPhysics`) → `RouteBrushTouch` |
| 8 | post-move gameplay | `AElysiumMapActor::PostMoveTick` (`TG_PostPhysics`) — the `+use` cursor |
| 9 | camera | `AElysiumPawn::CalcCamera` — the weight stack is solved here |
| 10 | publish the view | `UElysiumPresentationSubsystem::Publish` (`TG_PostUpdateWork`) |

`AElysiumMapActor` carries **three** tick functions. The two `TG_PrePhysics` passes are separated by
**prerequisites, not groups**: the gameplay tick waits on the pre-move tick unconditionally (a map
that seats no pawn still needs thinks on an advanced clock) and additionally on the movement
component. `EnsureTickPrerequisites` rebinds if the pawn is replaced.

`FElysiumTimeControl` (on `UElysiumGameStateSubsystem`) is the one pause/scale facade over the clock
**and** engine time; `FElysiumGameClock` keeps its writers private and friends only that struct, so
the single advance site is a compile-time property. **Scale is applied exactly once.**
`bTickEvenWhenPaused` is false on both gameplay passes and true on the presentation side.

## Subsystems by scope

| Scope | Subsystem | Owns |
|---|---|---|
| GameInstance | `UElysiumGameFlowSubsystem` | `EElysiumAppState` (Boot/FrontEnd/Loading/Playing/Paused/GameOver), its transition table (`Public/ElysiumAppState.h`), New Game / Load / Save / QuitToMenu / pause / game-over. **The screen is a pure function of the state.** |
| GameInstance | `UElysiumGameStateSubsystem` | `G`, the quest map, `FElysiumPlayerRecord`, the clock + `FElysiumTimeControl`, map snapshots, the installed script host |
| GameInstance | `UElysiumMapSubsystem` | travel, `PendingMapLoad`, the menu backdrop |
| GameInstance | `UElysiumSaveSubsystem` | slots, `CanSave`, `BuildPayload`/`ApplyPayload` |
| GameInstance | `UElysiumUISubsystem` | the screens + the `Menu` input scope |
| GameInstance | `UElysiumAudioSubsystem` | the decode registry, the voice pool, the global mute |
| GameInstance | `UElysiumNpcAnimSubsystem` | parsed bank assets, clip vocabularies, the disposition table |
| GameInstance | `UElysiumRulebookSubsystem` | the 12 `vdata/system/` table families, lazy per table |
| World | `UElysiumPresentationSubsystem` | the only writer of `FElysiumViewState` |
| LocalPlayer | `UElysiumInputSubsystem` | the priority stack of `FElysiumInputScope` — the module's **only** `SetInputMode` caller |
| Engine | `UElysiumMcpSubsystem` | ~20 `elysium_*` MCP tools (editor-gated) |

Priority table for input scopes (`ElysiumInput::Priority`):
`Game 0 < Sign 10 < Cinematic 20 < Chargen 30 < Dialogue 40 < Menu 50 < Debug 100`. Push/pop is
**handle-based, not LIFO** — screens close out of order.

## Player, commands, camera

**An action is a console command string** — VtMB has no action abstraction, so everything reaches one
registry through one door.

| Type | Role |
|---|---|
| `FElysiumCommands` (`Public/ElysiumCommands.h`) | the registry, plain C++. **92 declared verbs** with `+`/`-` pairs. Implementations `Bind`/`Unbind` and **stack**. **The latch is the verb's, not its implementation's** |
| `ElysiumCommandBus` | the one door: a bound key, a `ccmd` set, a `.dlg` action, `elysium.cmd`, MCP, `-ExecCmds` |
| `FElysiumConsole::Execute` | the precedence, stated once and tested: **command → alias → cvar → Python** |
| `FElysiumUserCmd` / `Builder` / `Stream` | one frame of intent as a value (`Buttons` is **uint64**); records, replays, round-trips through text |
| `ElysiumBinds` | VtMB's 75-row default bind set + `ReservedKeys()` (`` ` `` and `F7`) |
| `UElysiumInputRouter` | installs the binds, `SampleFrame` from `PlayerTick`, writes look **straight onto the control rotation** |
| `IElysiumPlayerBody` | what everything outside the body talks to; an interface because the two bodies share no base |
| `AElysiumPawn` + `UElysiumMovementComponent` | the faithful body: `APawn` + a `UBoxComponent` (32×32×72 u) + Source's movement functions |
| `AElysiumCapsulePawn` | the A/B baseline behind `elysium.SourceMovement 0` |
| `FElysiumCameraWeights` / `FElysiumCameraShotStack` / `UElysiumCameraComponent` | one camera and a weight, not two; `AElysiumPawn::CalcCamera` is the single apply point |
| `FElysiumViewState` (`Public/ElysiumViewState.h`) | everything on screen, assembled once per frame; three total functions over it |

Actors: `AElysiumGameMode`, `AElysiumPlayerController` (hosts `UElysiumCheatManager` and the router;
**binds no key of its own**), `AElysiumPawn`, `AElysiumHUD` (Canvas; **does not tick** — it
reconciles from `OnViewPublished`).

## Scripting, audio, shared readers

`IElysiumScriptHost` is the one seam. `FElysiumCPythonScriptHost` over `FElysiumPythonVM` is the
map-load default (embedded CPython 2.7.18 running VtMB's own level scripts 1:1);
`FElysiumExprScriptHost` and `FElysiumNullScriptHost` are the fallbacks.
`ElysiumPythonEntity.{h,cpp}` is the `vampire` module's object surface (the `Entity` type — **there
is no `Player` type**), `ElysiumScriptNatives.{h,cpp}` the engine surface both hosts share,
`FElysiumScriptFS` the VM's own filesystem namespace, `ElysiumDlg.{h,cpp}` the engine-neutral `.dlg`
core.

Audio: `UElysiumAudioSubsystem` + `FElysiumSoundCache` (WAV/MP3 → `USoundWaveProcedural`) +
`FElysiumSoundSchemeManager`. Every voice passes the global mute (`elysium.Mute`, **default 1**),
which is a gain multiplier, not a stop.

Shared readers: `ElysiumKeyValues.h` (whole-file character-stream tokenizer; **brace depth is the
only structural signal**, and a repeated *leaf* key is data — `Values` keeps the last, the ordered
`Pairs` array keeps them all), `ElysiumRulebook.{h,cpp}`, `FElysiumSignData`.

## Engine gotchas

Hard-won, non-obvious, and easy to undo:

- **`FCogImguiContext::SetEnableInput` dereferences the ImGui context**, which Cog creates lazily on
  its first tick. Every call site guards on `GetEnableInput()` first; unguarded at boot it crashes.
- **The loading screen hooks `IGameMoviePlayer::OnPrepareLoadingScreen`, not `PreLoadMap`** — the
  movie player binds `PreLoadMap` itself at engine init, ahead of any GI subsystem.
- **A `UCommonActivatableWidget` added straight to the viewport stays collapsed until
  `ActivateWidget()`** (`bAutoActivate` fires only inside a container).
- **`UElysiumCameraComponent::CalcCameraFor` must delegate to `UCameraComponent::GetCameraView`
  first** — overriding `CalcCamera` without it silently breaks first-person rendering.
- **The player hull is a box, not a capsule** — `StepMove` depends on a flat bottom, and `ACharacter`
  will not take a box root.
- **`ApplyMaterialOverrides` is lazy** — a runtime `SetMaterial` drops the primitive's built
  texture-streaming data, so albedo and `EnvMask` fall back to a low mip.
- **`UBodySetup::CalculateMass` reads the owning primitive's `FBodyInstance`**, which a runtime-built
  component never seeds from the asset — physics props re-apply mass to the component.
- **`UElysiumNpcAnimInstance`'s proxy must implement `UpdateAnimationNode`** — a sequence player never
  `Update_AnyThread`'d holds its start frame forever.
- **`+use` and the debug pick use dedicated channels** (`ELYSIUM_USE_CHANNEL` /
  `ELYSIUM_PICK_CHANNEL`), because the walkable surface is a material-less `.hulls` collider that
  would otherwise be reported instead of the wall.
- **Save omission diffs against a post-Load baseline, not zero** — a fresh-constructed reference
  omits the wrong things and a restored map re-runs every `logic_auto` ignition.
- **Resolved `UAnimSequence`s cache on the map actor, not the subsystem** — glTFRuntime binds each to
  a specific `USkeleton`, and meshes are per-map-epoch.

## Debug layer (non-Shipping)

`UElysiumCogSubsystem` (`#if ENABLE_COG`) registers the stock CogEngine windows plus the Elysium ones
(`_Status`, `_Maps`, `_Lights`, `_Entities`, `_Inspector`, `_EventQueue`, `_WorldViz`, `_Audio`,
`_SoundScheme`, `_Logic`, `_Scripting`, `_Npc`) over the `FElysiumCogWindow` base.
`UElysiumEntityDebugSubsystem` hosts the Source-style `elysium.ent_*` verbs and the world-viz layers.
`ElysiumPick.{h,cpp}` is click-selection; `FElysiumGizmoLayer` is the retained gizmo ISM.
`UElysiumMcpSubsystem` is Layer 3, reached through the reconnecting stdio proxy `tools/mcp_proxy.py`.
**Cog boots dormant** (`elysium.CogPersist 0` deletes its layout ini before the dependency brings Cog
up) — restored input capture makes the game's own UI unclickable.

**Debug injection always uses the real chokepoint** (`FElysiumEntityWorld::EnqueueInput`).

Automation tests live in `Private/Tests/` (inside the module — the plain-C++ substrate carries no
`ELYSIUMUE_API` exports): `ElysiumSubstrateTests.cpp` (content-free, runs under `-nullrhi`) and
`ElysiumContentTests.cpp` (parses real exports, self-skips when `tools/out` is empty).

## Build and test loop

After a C++ change: `build.bat`, then `test.bat <tier>` — `Substrate` for anything under the
substrate, scripting, session, player or UI layers, `Content` when the change reads `tools/out`. Both
are cheap: the build is adaptive non-unity (~10 s for a handful of files), and the Substrate tier
runs in about the same under `-nullrhi`.

**The result surface is the report, not stdout.** Every run writes JSON + HTML under
`tools/out/_tests/` and `test.bat` echoes the path and propagates the exit code.

**One run per change, not one per claim.** A green tier stays green until code moves. A roadmap
task's acceptance list is a set of things that must be **true**, not a set of runs to perform.

**A live run is proposed, never assumed — ask the owner first, with a recommendation.** The ask names
what the live run would answer *that the tiers cannot*. **Worth it** when the claim only exists in a
built world: the tick graph and its prerequisites, map build and actor adoption, the spawn hold,
pawn ↔ mover collision, the camera solve, anything tracing real geometry, or a script/dialogue path
needing the level script running. **Not worth it** for plain-C++ work the Substrate tier covers, for
doc-only changes, or for anything a green tier already answered. When in doubt the recommendation is
*no* — the headless one-shot harnesses are the pattern for turning a repeated live check into a
per-change one.

## Console commands

Every runtime verb is an `elysium.*` console command; the live set is whatever the module registers
(`FAutoConsoleCommand`/`FAutoConsoleVariableRef`). `elysium.commands [filter]` reads the coverage
back as a work list.
