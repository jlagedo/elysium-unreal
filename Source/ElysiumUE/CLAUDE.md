# Elysium — the C++ runtime (`Source/ElysiumUE/`)

The runtime half of the two clean halves (see the repo-root `CLAUDE.md`). A map's **look** is
offline-baked into real `.uasset` content and a real `.umap` under the `/ElysiumBaked` mount
(`tools/bake_map.py`, `docs/uasset-bake-spike.md`), which the map subsystem opens directly; the
runtime is spawned into that level, adopts its actors, and builds everything the bake cannot hold
— brush collision, ropes, the entity substrate, entity-driven bodies, the sky cubemap. It reads
the offline pipeline's intermediates from disk for all of that, with no coordinate conversion
(sidecars are already Unreal cm/Z-up/LH).

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
  the story entry at its `info_landmark`; `-ElysiumMap=<name>` (`play.bat <map>`) loads the named
  map and seeds a **mock character** (`BeginNewGame(Tremere, male)` — interim stand-in for chargen,
  9.4) so the player sheet the dialogue gates read exists. `-ElysiumProfile` runs the headless
  profiling harness.

## Map load path

`UElysiumMapSubsystem` (UE5 hard travel: `Travel` stows the target + `OpenLevel`s the map's own
baked `.umap`; the fresh world's game mode calls `SpawnPendingMap`) → `AElysiumMapActor`
(`AdoptBakedLevel` buckets the level's actors by the tags in `ElysiumBakedTags.h`, then builds the
runtime half; owns it for one map epoch — the engine tears the world down on travel and GC frees
it, no manual flush, no force-GC). `/Game/Elysium` is only the boot world now.
`map-architecture.md` has the model; roadmap 10.8.

| Type | Role |
|---|---|
| `FElysiumObjModel` | OBJ/MTL reader; `.emc` parse-cache |
| `FElysiumTextureCache` | DDS-preferred texture load, PNG fallback; a per-map decoded-texture dedup index owned by the map actor (a plain member), so its strong texture refs drop when the actor is torn down and GC reclaims them — threaded into the material factory |
| `FElysiumMaterialFactory` | one MID per OBJ surface (world + prop ISMs). The material's blend flags pick the master — `M_World_Opaque` / `_Masked` (`illum 4`) / `_Translucent` (`blend 1`) / `M_Additive` (`additive 1`) — then bind its named params: `Albedo`, `Emissive`+`EmissiveScale`, `BumpMap`+`BumpAmount` (linear), `EnvMask`+`EnvStrength` ($envmap → Lumen roughness, uniform white mask when unmasked), `BaseTex2`+`BlendAmount` (WVT). `elysium.BumpScale` / `EnvReflect` / `EmissiveScale` tune the three feature scalars |
| `FElysiumDecals` | 7.2 decals: the C++ spec for the `.decals` projector sidecar, and what the content test validates the export against. The bake consumes it — one `ADecalActor` per `infodecal` in the `.umap`, MIC off `M_Decal`, oriented `MakeRotFromXZ(Normal, SDir)`: local +X = room normal (so −X projects into the wall), and since a deferred decal maps texture **U→local Z, V→local Y**, the surface horizontal `SDir` goes on local Z with `DecalSize = depth×HalfH×HalfW` |
| `FElysiumRopes` + `AElysiumMapActor::BuildRopes` | 8.7 ropes: parse the `.ropes` cable sidecar (chain-resolved segments) → one Verlet `UCableComponent` per line, `CableLength = RestCm` verbatim (the sidecar already carries VtMB's RE'd rest length, which is usually *below* the straight span — most ropes hang taut, not slack), `NumSegments = nodes − 1` off the sidecar's RE'd `m_nSegments` (VtMB takes it from `Type`, so a `Type 2` rope is **one** span — a straight line that cannot sag), start always pinned and end pinned unless the `Dangling` flag is set, width/tube/tiling from the sidecar, MID off `M_World_Opaque` (one per unique rope texture). `EndLocation` is the **world** endpoint B, because an unset `AttachEndTo` resolves to the owner's root component (`SceneRoot`, identity) — never to the cable itself. `elysium.Ropes` A/Bs the pass |
| `UElysiumLightRig` | one Unreal light per WORLDLIGHTS `.lights` source; soft exponent falloff, specular off (VtMB is pure Lambert), sun, skyambient, lightstyle animation, live retune via `ApplyLiveTuning()`, per-source hand override (`SetSourceIntensity`/`RevertSource`) that the calibration and style passes skip, a per-source disable (`SetSourceDisabled`/`ShouldSourceBeLit`) that changes no value and outranks the master toggle, a per-source reviewed mark (`SetSourceReviewed` — the survey's judged bit; disabling implies it), and the saved hand survey (`_lights/<map>.json`) auto-applied at adopt (`LoadSurvey`; `elysium.LightSurvey 0` = full faithful rig) |
| `ElysiumEnvironment.{h,cpp}` | `.env` sky/fog: the sky cubemap (SkyLight IBL + `M_Sky` backdrop) |
| `FElysiumProfileRun` | the headless profiling harness (`-ElysiumProfile`) |
| `FElysiumShotRun` | the headless screenshot-regression harness (`-ElysiumShots`); shares the vantage table (`ElysiumVantages.h`) with the profiler and the capture path (`ElysiumScreenshot.{h,cpp}`) with the MCP screenshot tool |
| `ElysiumLightProbe` + `FElysiumProbeRun` | the light-attribution probe: a ray fan per `UElysiumLightRig` source traced on `ELYSIUM_PICK_CHANNEL` against the real built scene, recording what each light is nearest, whether that surface's bound MID emits (`EmissiveScale > 0`), how enclosed the light is, and its `share` of the illumination reaching the points it lights. `elysium.lightprobe [rays]` runs it live; `-ElysiumProbe` is the headless one-map-per-launch harness `probe.bat` walks every map with. Writes `tools/out/_lights/<map>.probe.json` |

Collision comes from `.hulls` (one convex `FKConvexElem` per solid world brush, PLAYERCLIP
included) + `.dispcol` (displacement trimesh) on collision-only PMCs; `elysium.BrushCollision`
A/Bs back to the render trimesh.

Player-facing actors: `AElysiumGameMode`, `AElysiumPlayerController` (+ `UElysiumCheatManager`
— `Noclip`, `ElysiumTeleport`, plus stock `UCheatManager` execs), `AElysiumPawn`
(Character-movement FPS pawn with noclip), `AElysiumHUD` (Canvas: the use-icon reticle,
`env_fade` screen fade, sign panels — player pose/mode/FPS live in the Cog Maps window; it also
ticks the native-Slate dialogue box off the world's open-conversation state, B4).

## The entity substrate (Track B)

Plain C++, no UObject reflection — Unreal supplies bodies only. Design: `docs/engine-core.md`.

| Type | Role |
|---|---|
| `FElysiumVariant` | tagged Void/Bool/Int/Float/String/Vector/Handle |
| `FElysiumEntityHandle` | `{Index, Epoch}`, generation-checked through `Resolve` |
| `FElysiumEntityDef`/`FElysiumEntityDefs` | immutable parsed `.ents` records |
| `FElysiumEntity` | the live base entity: CBaseEntity keyfields + a runtime `Origin` (seeded from `Def->Origin`; `SetRuntimeOrigin`/`SetRuntimeAngles`/`SetRuntimeModel` back `Entity.SetOrigin`/`SetAngles`/`SetModel` and hand off to the `OnRuntimeTransformChanged`/`OnRuntimeModelChanged` body-follow hooks), `Kill`/`ScriptHide`/`ScriptUnhide`, one-switch dormancy gating the brush body, per-output `times` counters, `FireOutput` (Source `COutput<T>` value seam — fills any wire whose map-param is empty), `OnTouchStart`/`OnTouchEnd`, `GetDebugState` |
| `FElysiumClassDesc`/`FElysiumClassRegistry` | per-classname descriptor — factory, base-chain link, input + typed field tables; case-folded chain lookup, inert-record fallback for unregistered classnames |
| `FElysiumEntityWorld` | the substrate: one entity per def, name/class indices, spawn pass (also builds brush bodies), `SpawnRuntimeEntity` (B3 runtime creation for `npc_maker.Spawn`) = the scripted two-phase `CreateRuntimeEntityNoSpawn` + `CallEntitySpawn` (9.3 `CreateEntityNoSpawn`/`CallEntitySpawn`) fused, `RenameEntity` (`Entity.SetName` name-index re-key), the `AcceptInput` + event-queue **chokepoints**, output firing, `RouteBrushTouch`, `UpdateUseCursor`/`PlayerUse`, think-first tick, epoch teardown, `AddSink` seam. Owned by `AElysiumMapActor` via `TPimplPtr` |
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
classnames and `FElysiumNpcMaker` for `npc_maker`/`npc_maker_fleshpile`), and
`ElysiumPropClasses.cpp` (8.3 `FElysiumProp` for `prop_dynamic`/`prop_dynamic_ornament`; 8.4
`FElysiumPhysProp` for `prop_physics` and `FElysiumPhysHinge` for `phys_hinge`).

NPCs (B3, no AI) stand a real glTF skeletal body at their origin: `ElysiumNpcVisual.{h,cpp}` is the
shared glb→`USkeletalMesh` loader (the 8.2 path, reused by the `UElysiumNpcSubsystem` test harness),
and `AElysiumMapActor::BuildNpcVisual` caches the mesh + idle clip per stem and stands a
`USkeletalMeshComponent` on the map actor (`elysium.NpcBodies` A/Bs the bodies; I/O still resolves
without them). `FElysiumNpc` latches `WillTalk`/`UseInteresting`; `StartPlayerDialogRemote` fires `OnDialogBegin` then
opens the NPC's `.dlg` conversation (B4 — its `dialogname` keyfield names the file). `npc_maker.Spawn`
creates its `NPCTargetname` child through
**`FElysiumEntityWorld::SpawnRuntimeEntity`** — a runtime-synthesized def stored past the map's
immutable def array, appended to `EntityList` (identity needs only the append). Runtime NPC bodies are
tracked for teardown like brush bodies.

**Dialogue (9.1 / B4).** `ElysiumDlg.{h,cpp}` is the engine-neutral core: the 13-field `.dlg` parser
(`FElysiumDlgFile`), the `dlgexpr` front-normalizer (`ElysiumDlgExpr` — skill-checks → `CalcFeat(...) >=`,
condition `&`/`|` → `and`/`or`, action `&` → `;`, else verbatim; the host then error-to-falses anything
malformed), and the host-agnostic branch machine (`FElysiumDlgConversation`, injected condition/action
callbacks). Text columns: col-1/2 are the gendered spoken text, **col-12 is the Malkavian-PC variant** of the
same line (`TextMalkavian`, not a short label — 97% of the 9,576 rows that carry it differ from col-1). Raw
`Text(bMale)`/`RawFor(bMale,bMalk)` are verbatim; `DisplayText(bMale,bMalk)` picks the Malkavian variant when
the player is Malkavian and strips the `[...]` VO stage directions (`ElysiumDlgText::StripStageDirections`) the
way VtMB does on screen — the box (subtitle + choices) uses it, keyed off the sheet's clan/gender. An NPC's
`StartPlayerDialogRemote` loads its `dialogname` `.dlg`, builds a conversation whose
callbacks route through `FElysiumEntityWorld::EvalCondition` (the installed CPython/expr host — field-5
writes hit the same `G` the level script reads), and hands it to the world's open-dialogue seam
(`OpenDialog`/`GetOpenDialog`/`PlayerDialogChoose`/`PlayerDialogAdvance`/`CloseDialog`, one at a time like
the sign slot). Closing routes `EndDialog` to the owner via `!self`, firing `OnDialogEnd` (→
`DialogPostProcess`). NPC col-4 = action, PC col-4 = gate. The interim UI is a native-Slate visual-novel
box (`SElysiumDialogueBox`, `ElysiumDialogueWidget.{h,cpp}`) the HUD adds to the viewport under
`FInputModeUIOnly`; `elysium.dlg`/`.choose`/`.advance` are its scriptable echo (`ElysiumDlgConsole.cpp`).
9.2 replaces the box on the 8.6 UI stack.

Dynamic props (8.3, no physics) stand a static-mesh body the same way: `FElysiumProp`
(`prop_dynamic`/`prop_dynamic_ornament`) calls `AElysiumMapActor::BuildPropVisual` — resolve the baked
`SM_<stem>` through `ResolvePropMesh` (the 8.1 `model_mesh` annotation names the stem; cached per stem,
one load however many entities place it) and stand a movable **non-solid** `UStaticMeshComponent` (a
per-entity component, not a shared ISM — the GAME_LUMP `.props` path keeps its own baked actors).
Placement rotation is the exporter's pre-converted `model_quat` (read verbatim). The leaf
gates the body on dormancy/`start_hidden`, follows `SetOrigin`/`SetAngles`/`SetModel`, and takes `Break`
(hide + `OnBreak`); `SetAnimation` logs a stub (the decode is LOD0 static geometry, no skeleton).
The same leaf also stands bodies for the `+use` static-mesh family (`prop_button`/`prop_switch`/
`prop_sign`/`prop_hacking`/`prop_doorknob(_electronic)`/`item_container(_animated/_lock)`) under a
class desc that registers **only** the `skin` field — a body and its skin, no invented I/O; their
interaction surface is 4.10/8.8.

**Skin families.** `Skin`/`SetSkin` and the `skin` keyfield/script write all repaint the body through
`AElysiumMapActor::ApplyPropSkin`, which loads the map's baked `UElysiumPropSkinSet`
(`ElysiumPropSkins.h` — `Stem -> family -> [slot name, UMaterialInterface]`, authored by
`bake_map.py` from the exporter's `props/<stem>.skins`) and `SetMaterial`s each repainted slot,
resolving the slot by name through `UStaticMesh::GetMaterialIndex`. Overrides are cleared first, so
skin 0 — or a family the model does not carry, which Source draws as the authored set — restores the
authored materials. The bound materials are the map's own baked instances, so a swapped skin renders
at exactly the quality the base one does, emission included. Skin changes **snap**: VtMB's `skin` is
one datamap record flagged both KEY and INPUT with a null `inputFunc`, so the keyvalue, the wire and
`.skin =` are the same direct write (`docs/entity_io.md`). `elysium.PropSkins` A/Bs the pass.
`World->RegisterPropBody` tracks it for teardown like NPC bodies; `elysium.PropBodies` A/Bs the bodies
(I/O still resolves without them).

Physics props (8.4) stand a **simulating** Chaos body: `FElysiumPhysProp` (`prop_physics`) calls
`AElysiumMapActor::BuildPhysPropVisual` — the same baked `SM_<stem>` every other prop stands, resolved
through the shared `ResolvePropMesh`. For a physics model the bake gave that asset **VtMB's own convex
collision** (one shape per `.phy` ledge, from `props/<stem>.phys`) under `CTF_UseSimpleAndComplex`, so a
Chaos body simulates against the simple shapes while the debug pick still gets a per-poly face index —
one asset serves both a simulating body and a static placement of the same model. The component takes
the `PhysicsActor` profile; the leaf drives `SetSimulatePhysics` and mass. **Mass** is the model's
authored `.phy` value unless the entity's `override_mass` > 0 (Source's precedence; `override_mass` is
−1 on every `prop_physics` in the exported maps). The leaf re-applies it to the *component*:
`UBodySetup::CalculateMass` reads the owning primitive's own `FBodyInstance` whenever there is one, and
a runtime-built component never seeds that from the asset. A model with **no** collision model stands
visible but non-solid and non-simulating — `CPhysicsProp::CreateVPhysics` drops such a prop to
`SOLID_NONE`/`MOVETYPE_NONE` rather than removing it (`docs/phy_vphysics.md`); the runtime reads that
off the asset's empty `AggGeom`. The RE'd I/O
surface (decisions.md 2026-07-24) is `Wake` (real), `Break` (hide + `OnBreak`), and `Skin`/`SetSkin`/
`FadeToSkin`/`SetSkinFadeTime` (skin-0 stubs) — VtMB has **no** `EnableMotion`/`DisableMotion`/`Sleep`.
`FElysiumPhysHinge` (`phys_hinge`) is a bodiless constraint: in a **second `PostSpawn()` pass** (Source's
`Activate()`, run after every entity has `Spawn()`'d so both attach bodies exist) it builds a
`UPhysicsConstraintComponent` at the pivot with its twist axis on the exporter's pre-converted
`Def->HingeAxis` (swings/linear locked → one rotational DOF), wiring `attach1`↔`attach2` (empty → world);
`forcelimit`/`torquelimit` = 0 → unbreakable; inputs `TurnOn`/`TurnOff`/`Break`, output `OnBreak`.
`World->RegisterConstraintBody` tears the constraint down; `elysium.PhysicsProps` A/Bs simulation
(0 = static/non-solid body, visual parity). `FElysiumEntity::GetAttachBody` is the seam a constraint reaches
a target's physics body through (base returns the brush body; `FElysiumPhysProp` returns its simulating mesh).

`+use` picks on the dedicated `ELYSIUM_USE_CHANNEL` (`ECC_GameTraceChannel1` = "ElysiumUse",
default-Block so world + solid bodies occlude the ray, isolated from `ECC_Visibility`). The
72-entry icon-name table and the channel constant live in `ElysiumUseIcons.h`.

**Brush touch requires a pawn toucher (`UElysiumBrushComponent::RouteTouch`).** Only a pawn
overlapping a trigger volume raises `OnStartTouch`/`OnEndTouch`; a volume that merely intersects
another brush body — every body on a map is a component of the *same* map actor, so trigger∩trigger
and trigger∩solid overlaps fire begin/end at map-build time — is filtered out, matching VtMB (geometry
overlapping geometry is never a touch). Under OpenLevel hard travel each map builds in a fresh world
with a fresh pawn, so there is no stale previous-map pawn position to guard against (the earlier
`IsPlayerSeated` gate is retired, roadmap 10.8).

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
  raises "game entity has been deleted"), the sheet-backed **`Player`**, the 11 module
  globals, and the console objects **`ccmd`/`cvar`** (9.3b). `Entity.__getattr__` resolves the type
  methods and instance `__dict__` first, then
  walks the class-chain tables — an **input** name manufactures a bound callable that fires
  through `EnqueueInput`, a **field** name marshals the live value; `__setattr__` is the mirror
  (datamap first, `__dict__` on a miss; an input or a non-keyable field is read-only). The base
  method table is real, not stubbed: `SetOrigin`/`SetAngles`/`SetModel`/`SetName` mutate live state
  (and follow the NPC body / re-key the name index), and `CreateEntityNoSpawn`/`CallEntitySpawn` are
  the host's real two-phase spawn (they return/take an `Entity`, like `Find*`). `G` is proxied onto
  the game state with both the attribute *and* mapping protocols, because VtMB's `G[k]` is its `G.k`.
  `ccmd` executes a console command on attribute-*set* (`c.patchtype=""` → alias → Python fallthrough),
  reads back `""` on get; `cvar` get/set reads/writes a value string. `FElysiumPythonVM` owns the
  interpreter and the **`FElysiumConsole`** store (`ElysiumConsole.{h,cpp}`, plain C++): the alias/cvar
  tables parsed from `out/cfg` and the `Execute` path (alias-expand → cvar-set → Python fallthrough). The
  VM points its `nt.getcwd`/`sys.moddir` at `out/` so VtMB's `getcwd()+moddir` file paths (e.g.
  `FixKeyBindings` reading `cfg/config.cfg`) resolve into the content mirror, and binds a mutable
  `Character` compatibility stub so the real `vamputil.py` imports (the patch monkeypatches `Character`;
  our 24 Character methods dispatch off the getattro, not a shared class — `decisions.md` 2026-07-24).
- `ElysiumScriptNatives.{h,cpp}` is the engine `vampire` surface both hosts share: the binding
  table the Cog Scripting window renders, the stub defaults, the Character-method dispatch, and
  the native-call log. It lives outside either host so `elysium.script.cpython 0/1` swaps the
  interpreter without changing what a name does. `IsClan`/`IsPCMalk` and `IsMale` are real (they
  read the player sheet clan/gender); the rest of the character surface (inventory, money, blood,
  humanity/XP, `CalcFeat`) is a logged stub until 9.4.
- Both hosts bind the two names the dialogue gates and level scripts read off the sheet: **`pc`**
  (the sheet-backed `Player` — CPython binds it once at VM start, the expr host resolves it as the
  Invalid-handle Character) and **`npc`** (the firing entity, per-eval from `Ctx.Self` — the
  conversation partner a `.dlg` action mutates).
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
  (live calibration sliders + a per-light inspector: world markers, click-select, attribute edits,
  a per-source enable switch + reviewed mark, authored-batch verdicts, and a transform gizmo on one
  source; Save writes the map's edits + `reviewed[]` coverage to `tools/out/_lights/<map>.json` —
  the standing hand-authored light state the rig auto-applies at map load — and Load is the same
  pass mid-session), `_Entities` (filter/histogram/dormancy browser), `_Inspector`,
  `_EventQueue` (pending queue + history + pause/step), `_WorldViz`, `_Audio`, `_SoundScheme`,
  `_Logic`, `_Scripting`, `_Npc`.
- `ElysiumCogStyle.{h,cpp}` is the debug UI's skin and its one palette: blood/bone/ink colours,
  the ImGui style built from them, semantic aliases (`ColOk`/`ColWarn`/`ColError`/`ColName`/
  `ColDim`/`ColInert`/`ColSelected`) that every window uses instead of literal `ImVec4`s, and
  `LabelValue` — the overlap-safe label/value row the key/value sections share. The style is
  global ImGui state, so it also covers the stock Cog windows and the F1 menu bar;
  `FElysiumCogWindow::GameTick` re-installs it whenever Cog rebuilds the style (a DPI change).
  `elysium.CogTheme 0` restores stock ImGui dark.
- Selection is **by click** (`ElysiumPick.{h,cpp}`, `#if !UE_BUILD_SHIPPING`): while the
  Inspector window is open **and** the Cog menu owns the mouse, LMB over the world (not over an
  imgui window) picks, RMB clears. The game is not paused. `ElysiumPick::Trace` returns the
  world-space fill triangles + outline segments the overlay draws, from four sources.
  **A World Viz gizmo marker wins outright**
  whenever it is drawn — it is the only clickable representation a bodiless entity (light,
  `ambient_generic`, logic) has. "Drawn" follows the mode: `Visible` depth-tests the markers,
  so one behind geometry does not pick (tested against the nearest *rendered* hit, since brush
  bodies render nothing and must not occlude); `All` is x-ray, so any marker picks; `Off`
  skips the source. The test is the exact 28 cm cube, and passing the current selection as
  `CycleAfter` steps to the next marker behind it, so clicking a cluster walks through it.
  With gizmos on, the bodiless-entity perpendicular fallback is suppressed (2 m tolerance
  would undo the marker's precision). Failing a gizmo, the nearest of two geometry sources
  wins — brush entity bodies (a `LineTraceMulti` on `ECC_Visibility`, so invisible trigger
  volumes are pickable) and the baked level's render geometry (one `LineTraceSingle` on
  `ELYSIUM_PICK_CHANNEL` with `bTraceComplex` + `bReturnFaceIndex`, resolved to a material slot
  through `GetMaterialFromCollisionFaceIndex`). The dedicated channel is what separates the two:
  the walkable surface is the `.hulls` brush collider, which carries no material and no face, so
  a pick on a shared channel would report the invisible clip volume instead of the wall clicked.
  A surface/prop highlight is an oriented patch on the impact normal plus the component's
  bounds — a baked static mesh keeps no CPU-side section geometry, so the exact BSP face is not
  recoverable. An entity's highlight is one box per def hull — the hulls are vertex sets with no
  faces, so it is exact for the axis-aligned box brushes nearly every trigger and door is made
  of, and a tight bound otherwise.
- The **Inspector** runs the pick from `RenderTick`, which Cog calls for every window whether or
  not it is visible, so the two halves gate separately: **arming** needs the window open, since
  the click-pick is a tool of this window and not a global mode (opening World Viz or Lights
  must leave LMB alone), while the **committed selection and its highlight** survive closing the
  window and the menu both — a pick is dropped only by RMB, the next pick, or the map going
  away. The highlight is drawn with imgui (projected translucent fill + outline + label, near-plane
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
  the server listens on port 8000. Compiles to an empty shell on a non-Editor target. The plugin, plus
  `AutomationTestToolset` + `LiveCodingToolset`, are `TargetAllowList: [Editor]` in the `.uproject`.
  Because that HTTP server dies with the game process, Claude Code connects **through a reconnecting
  stdio proxy** — `tools/mcp_proxy.py`, registered in the repo-root `.mcp.json` as server `elysium`
  (`type: "stdio"`). Claude Code keeps the proxy alive for the whole session while it bridges to the
  in-game HTTP endpoint, connecting lazily so the link survives the game's rebuild/relaunch cycles: the
  tool list stays visible (served from a cached `tools/out/_mcp/tools_cache.json` when the game is down),
  a `tools/call` while down returns a clean "game not running" result, and a relaunch reconnects on the
  next call and refreshes the list via a `tools/list_changed` notification — no manual `/mcp` reconnect.
  Point it at a non-default port with `ELYSIUM_MCP_URL` in the `.mcp.json` `env` block.
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

Every runtime verb is an `elysium.*` console command; the live set is whatever the module
registers (`FAutoConsoleCommand`/`FAutoConsoleVariableRef`). `elysium.mcp.*` gates the Layer 3
agent server.
