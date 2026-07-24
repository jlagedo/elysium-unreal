# Elysium — the C++ runtime (`Source/ElysiumUE/`)

The runtime half of the two clean halves (see the repo-root `CLAUDE.md`). It reads the
offline pipeline's intermediates from disk at map-load time and builds every engine object in
code — geometry, materials, textures, collision, lights, entities. No `.uasset` baking, no
editor content loop, no coordinate conversion (sidecars are already Unreal cm/Z-up/LH).

This file is the runtime fact sheet: what exists and where. **Per-task status and as-built
narrative live in `docs/roadmap.md`.** Design intent lives in `docs/engine-core.md` (entity
object model), `docs/debug-tooling.md` (debug layers), `docs/map-architecture.md` (map
lifecycle), `docs/python_bridge.md` (scripting).

## Module

UE 5.8. Module `ElysiumUE` (Runtime, Default loading phase).

- **Plugins:** `ProceduralMeshComponent` (runtime), `PythonScriptPlugin` (offline scaffolding
  only), `Cog` (vendored MIT debug-UI shell under `Plugins/Cog/`; main plugin only —
  CogImgui/Cog/CogEngine/CogCommon/CogDebug/CogDebugEditor + bundled ImGui/ImPlot/NetImgui;
  stripped from Shipping via `ENABLE_COG`), `glTFRuntime` (vendored MIT runtime glTF loader
  under `Plugins/glTFRuntime/`; the NPC skeletal path — `USkeletalMesh` + `UAnimSequence` from
  `.glb` at runtime, no editor import).
- **Module deps:** ProceduralMeshComponent, ImageWrapper, ImageCore, RenderCore, RHI,
  MeshDescription, StaticMeshDescription, PhysicsCore, glTFRuntime, EnhancedInput, Slate,
  SlateCore, CogCommon (all configs) + Cog/CogDebug/CogEngine/CogImgui (non-Shipping only).
- **Layout:** `Public/*.h` (types other code includes) + `Private/*.cpp,*.h` (everything
  else, including per-window Cog headers). Targets: `Source/*.Target.cs`; build rules:
  `ElysiumUE.Build.cs`.
- **Third party:** vendored single-header `dr_wav`/`dr_mp3`; CPython 2.7.18 SDK under
  `ThirdParty/CPython27/` (**fetched, not committed** — `tools/fetch_cpython27.py`,
  gitignored, `ELYSIUM_WITH_CPYTHON` Win64-only).

## Content root and config

- `FElysiumContentPaths::Root()` = `FPaths::ProjectDir()/"tools/out"` (in-repo, gitignored).
  Packaged builds later read a `content/` folder next to the exe.
- `Config/DefaultEngine.ini` — boot map `/Game/Elysium`, `AElysiumGameMode` default,
  `UElysiumGameInstance`, the fully-dynamic render path, and the `ElysiumUse` trace channel
  (`ECC_GameTraceChannel1`).
- `Config/DefaultInput.ini` — legacy axis/action mappings (WASD + arrows, mouse-look, Space
  jump, Q/E/Ctrl vertical, V noclip, T skybox, E use; F1 is Cog's). EnhancedInput is
  configured as the player-input class but unused.
- Boot paths: **New Game** (`UElysiumMapSubsystem::NewGame`) seeds a story context and enters
  the story entry at its `info_landmark`; `-ElysiumMap=<name>` (`play.bat <map>`) loads a map
  bare (the dev path — it seeds only `Linux_Wine=1`, so the tutorial's `linux_check`
  logic_pythoncheck reads OnTrue and the Wine-warning `popup_linux` stays down); `-ElysiumNewGame=0`
  boots the story map bare. `-ElysiumProfile` runs the headless profiling harness.

## Map load path

`UElysiumMapSubsystem` (travel, synchronous) → `AElysiumMapActor` (owns everything for one
map epoch; tears down on travel).

| Type | Role |
|---|---|
| `FElysiumObjModel` | OBJ/MTL reader; `.emc` parse-cache |
| `FElysiumTextureCache` | DDS-preferred texture load, PNG fallback |
| `FElysiumMaterialFactory` | MIDs off the master materials |
| `FElysiumStaticMeshBuilder` | runtime `UStaticMesh` per unique prop model (`BuildFromMeshDescriptions`), drawn as one ISM per (model, solidity) |
| `FElysiumDecals` + `FElysiumMaterialFactory::BuildDecal` | 7.2 decals: parse the `.decals` projector sidecar → one deferred `UDecalComponent` per `infodecal`, MID off `M_Decal`. Orient `MakeFromXZ(Normal, SDir)` — local +X = room normal (so −X projects into the wall), and since a deferred decal maps texture **U→local Z, V→local Y**, the surface horizontal `SDir` goes on local Z with `DecalSize = depth×HalfH×HalfW`. `elysium.Decals` A/Bs the pass, `elysium.DecalDepth` the depth, `elysium.DecalFlipU` mirrors U |
| `UElysiumLightRig` | one Unreal light per WORLDLIGHTS `.lights` source; soft exponent falloff, specular off (VtMB is pure Lambert), sun, skyambient, lightstyle animation, live retune via `ApplyLiveTuning()` |
| `ElysiumEnvironment.{h,cpp}` | `.env` sky/fog + `.cube` LUT onto `M_Sky` + post-process |
| `FElysiumProfileRun` | the headless profiling harness (`-ElysiumProfile`) |
| `FElysiumShotRun` | the headless screenshot-regression harness (`-ElysiumShots`); shares the vantage table (`ElysiumVantages.h`) with the profiler and the capture path (`ElysiumScreenshot.{h,cpp}`) with the MCP screenshot tool |

Collision comes from `.hulls` (one convex `FKConvexElem` per solid world brush, PLAYERCLIP
included) + `.dispcol` (displacement trimesh) on collision-only PMCs; `elysium.BrushCollision`
A/Bs back to the render trimesh.

Player-facing actors: `AElysiumGameMode`, `AElysiumPlayerController` (+ `UElysiumCheatManager`
— `Noclip`, `ElysiumTeleport`, plus stock `UCheatManager` execs), `AElysiumPawn`
(Character-movement FPS pawn with noclip), `AElysiumHUD` (Canvas: the use-icon reticle,
`env_fade` screen fade, sign panels — player pose/mode/FPS live in the Cog Maps window).

## The entity substrate (Track B)

Plain C++, no UObject reflection — Unreal supplies bodies only. Design: `docs/engine-core.md`.

| Type | Role |
|---|---|
| `FElysiumVariant` | tagged Void/Bool/Int/Float/String/Vector/Handle |
| `FElysiumEntityHandle` | `{Index, Epoch}`, generation-checked through `Resolve` |
| `FElysiumEntityDef`/`FElysiumEntityDefs` | immutable parsed `.ents` records |
| `FElysiumEntity` | the live base entity: CBaseEntity keyfields, `Kill`/`ScriptHide`/`ScriptUnhide`, one-switch dormancy gating the brush body, per-output `times` counters, `FireOutput` (Source `COutput<T>` value seam — fills any wire whose map-param is empty), `OnTouchStart`/`OnTouchEnd`, `GetDebugState` |
| `FElysiumClassDesc`/`FElysiumClassRegistry` | per-classname descriptor — factory, base-chain link, input + typed field tables; case-folded chain lookup, inert-record fallback for unregistered classnames |
| `FElysiumEntityWorld` | the substrate: one entity per def, name/class indices, spawn pass (also builds brush bodies), `SpawnRuntimeEntity` (B3 runtime creation for `npc_maker.Spawn`), the `AcceptInput` + event-queue **chokepoints**, output firing, `RouteBrushTouch`, `UpdateUseCursor`/`PlayerUse`, think-first tick, epoch teardown, `AddSink` seam. Owned by `AElysiumMapActor` via `TPimplPtr` |
| `UElysiumBrushComponent` | the per-brush-entity body: collision-only `UPrimitiveComponent`, convex `UBodySetup` cooked from def hulls, handle-carrying, dormancy-gated, solidity by classname (trigger/solid/none); `elysium.BrushBodies` A/Bs it |
| `FElysiumEventQueue`/`FElysiumIOEvent` | the one time-sorted queue (R4 — no engine timers) |
| `IElysiumIOSink` | always-on `FElysiumRingBufferSink` (1,000-entry history) + `FElysiumLogSink` (`LogElysiumIO` + VLOG) |
| `FElysiumGameClock`, `UElysiumGameStateSubsystem` | the substrate clock; the GI-scoped `G` store, quest map, player sheet (`FElysiumPlayerSheet`), `BeginNewGame` |

**Two rules that hold everywhere:** every input goes through `AcceptInput`/the event queue (so
it is loggable, pausable, single-steppable, serializable), and time comes from the substrate
clock, never `FTimerManager`.

Class implementations live in `ElysiumStarterClasses.cpp` (logic_auto/relay, trigger family,
`logic_pythoncheck`), `ElysiumLogicClasses.cpp` (math_counter, logic_timer, logic_case
+ the VtMB `logic_case_toggle` divergence, env_fade, func_brush, point_teleport),
`ElysiumMover.{h,cpp}` (`FElysiumMoverBase` = CBaseToggle, `FElysiumDoorBase` = the CBaseDoor
4-state machine, `FElysiumFuncDoor`, `FElysiumButton`), `ElysiumSignClasses.cpp` (`game_sign`),
`ElysiumAmbientGeneric.cpp`, `ElysiumEventClasses.cpp` (`events_player`/`events_world`),
`ElysiumNpcClasses.cpp` (B3 — the AI-free `FElysiumNpc` character leaf for the living `npc_*`
classnames and `FElysiumNpcMaker` for `npc_maker`/`npc_maker_fleshpile`).

NPCs (B3, no AI) stand a real glTF skeletal body at their origin: `ElysiumNpcVisual.{h,cpp}` is the
shared glb→`USkeletalMesh` loader (the 8.2 path, reused by the `UElysiumNpcSubsystem` test harness),
and `AElysiumMapActor::BuildNpcVisual` caches the mesh + idle clip per stem and stands a
`USkeletalMeshComponent` on the map actor (`elysium.NpcBodies` A/Bs the bodies; I/O still resolves
without them). `FElysiumNpc` latches `WillTalk`/`UseInteresting` and turns `StartPlayerDialogRemote`
into `OnDialogBegin` + a manual `EndDialog`→`OnDialogEnd` seam (B4 replaces it with the `.dlg`
runner). `npc_maker.Spawn` creates its `NPCTargetname` child through
**`FElysiumEntityWorld::SpawnRuntimeEntity`** — a runtime-synthesized def stored past the map's
immutable def array, appended to `EntityList` (identity needs only the append). Runtime NPC bodies are
tracked for teardown like brush bodies.

`+use` picks on the dedicated `ELYSIUM_USE_CHANNEL` (`ECC_GameTraceChannel1` = "ElysiumUse",
default-Block so world + solid bodies occlude the ray, isolated from `ECC_Visibility`). The
72-entry icon-name table and the channel constant live in `ElysiumUseIcons.h`.

**Brush touch requires a pawn toucher (`UElysiumBrushComponent::RouteTouch`).** Only a pawn
overlapping a trigger volume raises `OnStartTouch`/`OnEndTouch`; a volume that merely intersects
another brush body — every body on a map is a component of the *same* map actor, so trigger∩trigger
and trigger∩solid overlaps fire begin/end at map-build time — is filtered out, matching VtMB (geometry
overlapping geometry is never a touch). A second gate suppresses touches until the map has seated the
pawn (`AElysiumMapActor::IsPlayerSeated`): on `elysium.newgame`/travel from an already-loaded map the
pawn briefly stands where the *previous* map left it, and any volume it lands inside then is an
artifact of the old position, not an entered trigger.

## Scripting hosts

`IElysiumScriptHost` is the one seam (installed on `UElysiumGameStateSubsystem`; covers field-6
payloads, `logic_pythoncheck`, `EvalScript`, `ScheduleTask`, level-script import):

- `FElysiumCPythonScriptHost` over `FElysiumPythonVM` — the **map-load default**: embedded
  CPython 2.7.18 running VtMB's own level scripts 1:1. `MakePreferredScriptHost` falls back when
  the SDK is absent or the VM fails to start (a dead VM's all-Void evals are indistinguishable
  from error-to-false). The map's `worldspawn.levelscript` imports **before the spawn pass**,
  matching VtMB's order, and `LoadLevelScript` then merges the module's public top-level names
  into `__main__`. Everything — field-6 payloads, `ScheduleTask` sources, callbacks, the console
  verbs — evaluates in `__main__`, which is where VtMB evaluates them (its dispatch wraps the
  payload as `__main__.%s`, so the leading name is an attribute of the bus).
- `ElysiumPythonEntity.{h,cpp}` is the `vampire` module's object surface: the **`Entity`** type
  (a generation-checked handle, not a pointer — a reference kept across a `Kill` or a travel
  raises "game entity has been deleted"), the sheet-backed **`Player`**, and the 11 module
  globals. `Entity.__getattr__` resolves the type methods and instance `__dict__` first, then
  walks the class-chain tables — an **input** name manufactures a bound callable that fires
  through `EnqueueInput`, a **field** name marshals the live value; `__setattr__` is the mirror
  (datamap first, `__dict__` on a miss; an input or a non-keyable field is read-only). `G` is
  proxied onto the game state with both the attribute *and* mapping protocols, because VtMB's
  `G[k]` is its `G.k`. `FElysiumPythonVM` itself owns only the interpreter.
- `ElysiumScriptNatives.{h,cpp}` is the engine `vampire` surface both hosts share: the binding
  table the Cog Scripting window renders, the stub defaults, the Character-method dispatch, and
  the native-call log. It lives outside either host so `elysium.script.cpython 0/1` swaps the
  interpreter without changing what a name does.
- `FElysiumExprScriptHost` over `ElysiumExpr` — the self-contained lexer + recursive-descent
  parser + tree-walk for VtMB's restricted expression subset; every error collapses to Void
  (error-to-false).
- `FElysiumNullScriptHost` — logs + Void. `elysium.script.live 0` installs it to darken the
  whole scripting surface for A/B; `elysium.script.cpython` swaps CPython/expr.

## Audio

`UElysiumAudioSubsystem` (GI-scope) owns the decode registry and the voice pool
(`PlayVoice`/`StopVoice`/`SetVoiceVolume`/`SetVoicePitch` over a `UAudioComponent` per voice —
3D attenuation, 2D beds, entity attach, fades, looping via underflow re-queue).
`FElysiumSoundCache` decodes to interleaved int16 PCM at runtime — WAV (MS-ADPCM/IMA/PCM) via
`dr_wav`, MP3 via `dr_mp3`, dispatched by extension — feeding a `USoundWaveProcedural` per play
(Unreal has no runtime path for loose WAV/MP3). `FElysiumSoundSchemeManager` +
`FElysiumSoundScheme` (owned by the map actor) run the ambient bed, the music state machine,
and the polar RandomSound scheduler. RoomDSP reverb submixes are not built.

Every voice passes through the subsystem's **global mute** (`elysium.Mute`, **default 1 = muted**;
`IsMuted`/`SetMuted`/`MasterGain`, mirrored by the Cog Audio window's Mute checkbox). It is a gain
multiplier, not a stop: a muted voice keeps playing at zero gain, so beds and music stems stay in
sync and unmuting rejoins the mix mid-stream. `FElysiumAudioVoice::Volume` holds the *requested*
volume, so a flip re-applies in one pass (voices already fading out toward a reap are skipped).

## Shared readers

- `ElysiumKeyValues.h` — the Source KeyValues reader (whole-file character-stream tokenizer, so
  a quoted value may span lines). Used by sound schemes and sign definitions.
- `FElysiumSignData` — the `SignData` panel plus client.dll's `CSignUI` coordinate model (a
  1024×768 virtual canvas scaled uniformly by `ScreenH/768`, blocks positioned relative to the
  panel rect, centring branch when `XPos + YPos == 0`). That canvas model is the **intent
  reference** for the UI re-skin, not a runtime coordinate system to keep.

## Debug layer (non-Shipping)

- `UElysiumCogSubsystem` (`#if ENABLE_COG`) registers the stock CogEngine windows plus the
  custom ones under an `Elysium` F1-menu group. Stock `CogEngineWindow_ImGui` (the Dear ImGui /
  ImPlot demo, metrics, debug-log and style-editor toggles) is not registered. `FElysiumCogWindow`
  is their base — it hands them
  `GetMapActor`/`GetEntityWorld`/`GetGameState`/`GetMapSubsystem` (Track-B entities are
  plain C++ and invisible to Cog's UObject inspector) plus a shared static browser→inspector
  selection. Windows: `_Status`, `_Maps` (travel + load timings + transitions + player pose/mode/FPS),
  `_Lights`
  (live calibration sliders), `_Entities` (filter/histogram/dormancy browser), `_Inspector`,
  `_EventQueue` (pending queue + history + pause/step), `_WorldViz`, `_Audio`, `_SoundScheme`,
  `_Logic`, `_Scripting`, `_Npc`.
- `ElysiumCogStyle.{h,cpp}` is the debug UI's skin and its one palette: blood/bone/ink colours,
  the ImGui style built from them, semantic aliases (`ColOk`/`ColWarn`/`ColError`/`ColName`/
  `ColDim`/`ColInert`/`ColSelected`) that every window uses instead of literal `ImVec4`s, and
  `LabelValue` — the overlap-safe label/value row the key/value sections share. The style is
  global ImGui state, so it also covers the stock Cog windows and the F1 menu bar;
  `FElysiumCogWindow::GameTick` re-installs it whenever Cog rebuilds the style (a DPI change).
  `elysium.CogTheme 0` restores stock ImGui dark.
- Selection is **by click** (`ElysiumPick.{h,cpp}`, `#if !UE_BUILD_SHIPPING`): while the Cog
  menu owns the mouse, LMB over the world (not over an imgui window) picks, RMB clears. The
  game is not paused. `ElysiumPick::Trace` returns the world-space fill triangles + outline
  segments the overlay draws, from four sources. **A World Viz gizmo marker wins outright**
  whenever it is drawn — it is the only clickable representation a bodiless entity (light,
  `ambient_generic`, logic) has. "Drawn" follows the mode: `Visible` depth-tests the markers,
  so one behind geometry does not pick (tested against the nearest *rendered* hit, since brush
  bodies render nothing and must not occlude); `All` is x-ray, so any marker picks; `Off`
  skips the source. The test is the exact 28 cm cube, and passing the current selection as
  `CycleAfter` steps to the next marker behind it, so clicking a cluster walks through it.
  With gizmos on, the bodiless-entity perpendicular fallback is suppressed (2 m tolerance
  would undo the marker's precision). Failing a gizmo, the nearest of three geometry sources
  wins — brush entity bodies (a `LineTraceMulti` on `ECC_Visibility`, so invisible trigger
  volumes are pickable), prop instances, and world/sky surfaces. Props and surfaces are **CPU ray-casts
  against the real triangles**, because physics cannot answer them: with the default
  `elysium.BrushCollision 1` the world render mesh is built with collision off (the `.hulls`
  collider carries no material and no face), and a solid prop's cooked collision is one convex
  hull of the whole model. The map actor keeps the CPU geometry for this — `FPropPickSoup` per
  unique model (~3 MB on the tutorial) and a section-index → OBJ-group-key table. A world pick
  highlights the whole BSP face: the exporter emits a fresh vertex per face corner and
  `BuildMeshFromObj` remaps sections on the global index, so triangles of one face share local
  indices and adjacent faces share none, and an edge flood stops at the face boundary by
  itself (a coplanarity test bounds displacement patches). An entity's highlight is one box
  per def hull — the hulls are vertex sets with no faces, so it is exact for the axis-aligned
  box brushes nearly every trigger and door is made of, and a tight bound otherwise.
- The **Inspector** runs the pick from `RenderTick`, which Cog calls for every window whether
  or not it is visible — so picking and the highlight work with the window closed. The
  highlight is drawn with imgui (projected translucent fill + outline + label, near-plane
  clipped), not scene geometry: no assets, it reaches meshless trigger volumes, and it keeps
  drawing when the world is time-scaled to a stop. The window shows the picked surface
  (component / material + textures / section / instance / triangle) above the entity detail,
  with the entity half sticky so picking a wall does not drop a half-set-up test harness. It
  is the primary interactive surface; the verbs are the scriptable echo.
- The Inspector's layout adapts to the record, because the map is mostly inert ones: every
  record inherits the whole CBaseEntity field chain whether or not its class uses it, so an
  `info_node` (3 keyvalues on disk) resolves ~20 fields all at their default. `PreBegin` caps
  the window to the viewport and the detail lives in a scrolling child, so content never runs
  off screen. Fields/Keyvalues/Outputs carry counts in their headers (`###` ID suffixes so the
  count does not reset the open state) and re-seat their open state on a selection *change*
  only — a registered class leads with Fields, an inert record with Keyvalues, and Outputs
  open only when the record wires any. A "Hide unset" filter drops fields whose value is falsy
  by `FElysiumVariant::ToBool` (0 / empty / zero vector / unbound handle / Void), which is
  exactly the "never touched" test. The fire-input widgets appear only when the class registers
  inputs.
- `UElysiumEntityDebugSubsystem` (`UTickableWorldSubsystem`, `#if !UE_BUILD_SHIPPING`) hosts the
  Source-style `elysium.ent_*` verbs — `ent_fire` (targetname/classname/crosshair picker;
  discovery-lists inputs when none given), `ent_dump`/`ent_info`, `ent_pause`/`ent_step`,
  `ent_break`, the `ent_text`/`ent_bbox`/`ent_messages` overlay bitmask — and the world-viz
  layers as a `FVizSettings` block its always-running tick renders (entity gizmos, trigger-hull
  AABBs, fading caller→target I/O beams captured at `TapDelivered`). `ent_break` and the
  message capture ride a `FElysiumDebugTapSink` installed into each world epoch via `AddSink`.
- Gizmos are a **retained** layer (`FElysiumGizmoLayer`): one ISM of unit cubes built once per
  epoch, colour packed into per-instance custom data read by `M_Gizmo`/`M_Gizmo_XRay`. The
  marker's anchor and size are one definition in `ElysiumGizmoColor.h` (`ElysiumGizmoAnchor`,
  `ElysiumGizmoScale`/`ElysiumGizmoHalfExtent`), shared by the layer, the label/beam overlays,
  and the click-pick — "what you see is what you click" only holds if they cannot drift. A
  dormancy/liveness flip re-uploads that one instance through `SetVisualChangedHook`, never a
  per-frame rebuild. Only the distance-culled labels stay immediate-mode.
- Debug injection always uses the real chokepoint (`FElysiumEntityWorld::EnqueueInput`) — the
  Inspector's fire buttons and `ent_fire` are the same path a map's own I/O takes.
- **Layer 3 — the agent-facing MCP surface** (`debug-tooling.md` Layer 3): `UElysiumMcpSubsystem`
  (`UEngineSubsystem`, editor-gated by `ELYSIUM_WITH_MCP`) registers ~20 `elysium_*` MCP tools
  (`ElysiumMcpTools.cpp`) through the engine's `ModelContextProtocol` plugin via
  `IModelContextProtocolModule::AddTool()` (direct registration → works in `-game`/PIE/cooked, not
  only the editor-only Toolset adapter). The tools are structured wrappers over the same runtime
  state the Cog windows read, resolving the live world at call time; `entity_fire` goes through
  `EnqueueInput` like everything else. `console_exec`/`log_tail` read `FElysiumLogTap` (an always-on
  2,000-line `FOutputDevice` ring). **On by default in dev builds** (auto-starts wherever the plugin
  is present — editor target only, so never in Shipping/Test); `-NoElysiumMcp` opts out,
  `-ElysiumMcp=<port>` pins a port, `elysium.mcp.start`/`stop` toggle it live. Loopback + no-auth;
  an agent connects via the repo-root `.mcp.json` (server `elysium`, port 8000). Compiles to an empty
  shell on a non-Editor target. The plugin, plus `AutomationTestToolset` + `LiveCodingToolset`, are
  `TargetAllowList: [Editor]` in the `.uproject`.
- **Automation tests** live in `Private/Tests/` (inside the module — the plain-C++ substrate carries
  no `ELYSIUMUE_API` exports for a separate test module): `ElysiumSubstrateTests.cpp` (content-free,
  app-context so it runs under `-nullrhi` — variant/expr/KeyValues/queue/registry + an end-to-end
  `logic_relay→math_counter` I/O chain on a bare `FElysiumEntityWorld`) and `ElysiumContentTests.cpp`
  (parses real exported `.ents`, self-skips when `tools/out` is empty). Run headless via `test.bat`.
- Editor-only World Outliner labels (`ElysiumEditorLabels.h`, `#if WITH_EDITOR`): map actor
  `Map:<name>` in an `Elysium` folder, brush bodies `Body_<idx>_<name>_<class>` (plus the exact
  `#<idx> <name>(<class>)` debug string as a `ComponentTag`), lights `Light_<idx>_<kind>`, prop
  ISMs `Props_<model>_<solidity>`. The same canonical string
  (`FElysiumEntity::DebugString`) threads every I/O log line.

## Console commands

Lifecycle `elysium.newgame` / `map` / `maps` / `reload`; inspection `elysium.campos` /
`lights` / `props` / `ents` / `classes` / `world` / `world.io` / `world.fireinput` / `g`;
A/B toggles `elysium.BrushCollision` / `BrushBodies` / `NpcBodies` / `Decals` (+ `DecalDepth`) / `EmissiveScale` /
`LightScale` / `LightFit` / `CogTheme`; entity debug `elysium.ent_*` / `showtriggers`; scripting `elysium.eval` / `exec` /
`script.live` / `script.cpython` / `py.*` (`py.smoke` / `exec` / `load` / `fire`, plus the two
single-token acceptance harnesses `py.poc` and `py.firstbeat`); audio `elysium.Mute` / `playsound` / `sound_info` /
`MusicState` / `MusicCrossfade` / `SchemeRandom*`; NPC `elysium.npc.load` / `clear` / `list`; MCP
`elysium.mcp.start` / `stop` / `status` / `tools` (Layer 3 — the agent server, opt-in).
