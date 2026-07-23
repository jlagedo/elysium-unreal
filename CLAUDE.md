# Elysium-Unreal

*Vampire: The Masquerade – Bloodlines* (VtMB, 2004, early Source engine) rebuilt as a
playable game on **Unreal Engine 5.8 + C++**. The runtime loads engine-neutral
intermediates produced by this repo's own offline decode/export pipeline and builds all
engine objects in code at map-load time — no `.uasset` baking, no editor content loop.

**`docs/roadmap.md` is the single source of truth work tracker** (all phases, tasks,
status, pipeline/RE backlogs, risks, decision log). **`docs/rebuild-strategy.md` is the
strategy reference** (north star, principles, the two tracks, sidecar contracts, system
designs). Read those two first. This file is the quick-orientation fact sheet.

## Bring-your-own-game (load-bearing)

**Nothing game-sourced is committed.** The decoders read *the user's own VtMB install*;
their output (`tools/out/`) is gitignored and regenerable. This is the legal posture, not
a convenience — prior community rebuilds died to a C&D, not to technical failure. The only
assets in `Content/` are hand-authored and game-agnostic.

## The two clean halves

- **Offline — `tools/`** (Python): decodes VtMB's proprietary formats (BSP v17, MDL v2531,
  TTH/TTZ, VPK, VMT, `.fnt`, `.res`) into intermediates under `tools/out/<map>/`
  (OBJ+MTL+PNG/DDS, glTF `.glb`, plain-text/JSON sidecars). `UE_bsp_to_scene.py` is the map
  exporter; `export_all.py` batches. Formats + decoders are documented in `tools/CLAUDE.md`.
  Runs against the user's install; needs Python + `tools/requirements.txt`.
- **Runtime — `Source/ElysiumUE/`** (C++): loads those intermediates from disk at map-load
  and builds `UProceduralMeshComponent` geometry, materials (MIDs off master materials),
  textures, and collision in code. Python is **never** run at runtime — the seam is
  file-based.

## Exporter status — the `UE_` convention (load-bearing)

An exporter prefixed **`UE_`** (e.g. `UE_bsp_to_scene.py`) is verified to emit
**Unreal-native** output: centimetres, Z-up, left-handed, triangle winding pre-reversed —
so the C++ runtime reads every file 1:1 with **no coordinate conversion**. There is no
Godot legacy left in a `UE_` exporter. Any exporter **without** the `UE_` prefix
(`mdl.py`, `mdl_gltf.py`, `bsp_to_obj.py`, …) still emits the old Godot Y-up/metres space
(`source_to_godot`) and is **flagged for review** — do not consume its output as Unreal
space until it is converted and renamed. When you convert one, rename it `UE_*` and update
its callers + docs in the same pass.

## Current state — M0 verified, M1 in progress

Boot into an empty persistent level → **New Game** (`UElysiumMapSubsystem::NewGame`): seed a
fresh story context and enter the story entry, `sp_tutorial_1` at its `tutorial` `info_landmark`.
`-ElysiumMap=<name>` (`play.bat <map>`) instead loads that map bare and unseeded — the dev path —
and `-ElysiumNewGame=0` boots the story map bare. Either way `UElysiumMapSubsystem::Travel`
(synchronous) loads the
map as world + 3D-skybox PMC actors, MIDs off the single master material `M_VtMB_World`
(albedo + alpha-masked `$selfillum` emissive via `map_Ke` → `Emissive`/`EmissiveScale`,
tunable by `elysium.EmissiveScale`), DDS-preferred textures (PNG fallback), brush collision
(`.hulls` convex + `.dispcol` trimesh, render-trimesh fallback), `.emc` parse-cache,
`.spawn`/`.sky` placement, a Character-movement FPS pawn with noclip, a trimmed Canvas HUD
(always-on FPS/position overlay only — map/light counts live in the Cog windows), and
`elysium.newgame` / `elysium.map` / `elysium.maps` / `elysium.reload` / `elysium.debug` /
`elysium.lights` / `elysium.campos` console commands. The New Game context lives on
`UElysiumGameStateSubsystem`: `FElysiumPlayerSheet` (clan in the level-script 2..8 encoding,
gender, an open `Stats` map) plus `BeginNewGame`, which seeds `Story_State=-4`, `Tut_Jack=0`,
`Tut_Patch=0`, `Linux_Wine=1` — the flags the tutorial's own scripts read once chargen and the
theatre intro are skipped. Player-centric dev cheats live on `UElysiumCheatManager` (a
`UCheatManager` subclass hosted by `AElysiumPlayerController`): `Noclip` + `ElysiumTeleport`
plus the stock `UCheatManager` execs, autocompleted.

M1 landed so far: the `.env` sky/fog + `.cube` LUT (`M_Sky`), and the **real-time
`UElysiumLightRig`** — one Unreal light per WORLDLIGHTS source from `.lights` (point/spot
soft exponent falloff with specular killed (VtMB is pure Lambert), directional sun,
skyambient tint, lightstyle animation), with a
fully dynamic renderer (HWRT Lumen + MegaLights + VSM; `Config/DefaultEngine.ini`). Static
props also load: `.props` + `props/*.obj` build one runtime `UStaticMesh` per unique model
(`FElysiumStaticMeshBuilder`, `BuildFromMeshDescriptions`), drawn as one
`UInstancedStaticMeshComponent` per (model, solidity) bucket with MIDs off `M_VtMB_World` and
convex collision on solid props (`elysium.props` toggles; 809 instances / 161 models for the
tutorial). **Brush collision** also lands: `AElysiumMapActor` loads `.hulls` (one convex
`FKConvexElem` per solid world brush, invisible PLAYERCLIP volumes included) and `.dispcol`
(displacement terrain trimesh) onto collision-only PMCs, replacing the render-mesh trimesh as
the walkable surface (`elysium.BrushCollision` toggles back to trimesh for A/B). The **Track-B
entity substrate** also runs at map load: `.ents` parse → one `FElysiumEntity` per def through the
class registry → spawn pass → per-brush-entity `UElysiumBrushComponent` collision/overlap bodies
(185 on the tutorial) → spawn pass, all through the two chokepoints with the I/O ring buffer + log
sinks. The **starter leaf classes** also run (`ElysiumStarterClasses.cpp`): `logic_auto` fires
`OnMapLoad` on its first-think ignition; `logic_relay` re-fires `OnTrigger` (Enable/Disable/Toggle-
gated); `trigger_multiple`/`trigger_once` (over a shared `CBaseTrigger` chain node) turn a brush
body's begin/end overlap into `OnStartTouch`/`OnEndTouch`/`OnTrigger`, filtered to the ALLOW_CLIENTS
spawnflag (the player toucher), with `trigger_once` self-`Kill`ing after first touch. Every
runtime spawn path also carries an editor-only (`#if WITH_EDITOR`, compiled out of Shipping) World
Outliner label via `ElysiumEditorObjectName` (`ElysiumEditorLabels.h`): the map actor is
`Map:<name>` in an `Elysium` folder, brush bodies are `Body_<idx>_<name>_<class>` (plus the exact
`#<idx> <name>(<class>)` debug string as a `ComponentTag`), lights are `Light_<idx>_<kind>`, prop
ISMs are `Props_<model>_<solidity>`; the same canonical debug string (`FElysiumEntity::DebugString`)
threads every I/O log line. The **P4.1 mover base** also runs (`ElysiumMover.h/.cpp`):
`FElysiumMoverBase` is the CBaseToggle primitive — `LinearMove`/`AngularMove` drive the entity's
brush body at constant velocity (no easing) toward a target transform on the substrate clock/think
(R4, no engine timers), moving swept so a solid kinematic body pushes the pawn + reports blockers,
and firing `MoveDone()` on arrival (angular rotation pivots about the def origin = the hinge).
`FElysiumDoorBase` (registered as the `CBaseDoor` chain node) layers the CBaseDoor 4-state machine
(`m_toggle_state` AT_TOP/AT_BOTTOM/GOING_UP/GOING_DOWN) — `Open`/`Close`/`Toggle`/`Lock`/`Unlock`/`Use`
inputs, `OnOpen`/`OnClose`/`OnFullyOpen`/`OnFullyClosed`/`OnLockedUse`/`OnBlockedClosing` outputs,
`wait` autoclose (`-1` = stay open), the locked path, and blocked-while-closing (deal `dmg` via
`ApplyDamage`, reverse, `OnBlockedClosing`) — with `speed`/`distance`/`wait`/`lip`/`dmg`/`linked_door`
as chain fields. **P4.3** completes the door family with both leaves: `func_door_rotating` swings
`distance°` about yaw/Z around the hinge, and the sliding `func_door` (`FElysiumFuncDoor`) translates
along `angles` by its own depth minus `lip` (open pose `pos + movedir·(|size·movedir| − lip)`, matching
decompiled `CBaseDoor::Spawn`; `SourceAnglesToUnrealDir` for the movedir, `speed` in/s via `LinearMove`).
The **full spawnflag table (B.5)** is decoded/honoured — `START_OPEN`/`REVERSE`/`LOCKED` +
`NO_AUTO_RETURN` (0x20, stay-open) + **`PUSE` (0x100)**, the dominant bit (105 doors) that arms the +use
look-cursor (`IsUsable`) and runs the **doorknob path** `DoorUse` (toggle this leaf, locked →
`OnLockedUse`, and mirror onto the **`linked_door`** partner for the double-door swing); the rest
(`PASSABLE`/`ONEWAY`/`NONPCS`/`SILENT`/`USE_CLOSES`/`PTOUCH`) are labelled but deferred. Movers surface
runtime state through `FElysiumEntity::GetDebugState` in the Cog Inspector's **Live state** section
(door toggle-state/move/poses/link/spawnflag decode; button press-state/latch/flags). Doors are driven
through the I/O inputs (`elysium.ent_fire <door> Open`/`Unlock`/`Use`) and the +use look-cursor. The
**P4.2 `func_button`** (`FElysiumButton`) also runs: a concrete `CBaseButton` over the same mover
primitive — press → `TriggerAndWait` (fire `OnPressed`) → autoclose/spring-back or latch (`wait -1`)/
toggle, spawnflags reconciled against the decompiled `CBaseButton::Spawn` (`0x100`=touch, `0x400`=use
— VtMB keeps the stock layout, correcting an `entity_io.md` swap; `0x1` DONTMOVE = the logical no-slide
path every exported button uses, `0x20` TOGGLE, `0x800` LOCKED), with `Press`/`Lock`/`Unlock` inputs.
The **+use look-cursor** lands with it (`FElysiumEntityWorld::UpdateUseCursor`/`PlayerUse`, from the
map-actor tick + the pawn `E` key): a per-frame camera-ray pick over the usable, non-inert brush
bodies in reach fires `OnIn`/`OnOut` on aim enter/leave and presses the aimed button — so
`StartHidden`→`ScriptUnhide` arm/disarm gates reticle + collision together. **P4.4** completes it:
the pick runs on a **dedicated use-only trace channel** (`ELYSIUM_USE_CHANNEL` =
`ECC_GameTraceChannel1` = "ElysiumUse" in `Config/DefaultEngine.ini`, default-Block so world + solid
bodies occlude the ray, isolated from `ECC_Visibility`), and the **use-icon HUD** draws VtMB's context
cursor over the crosshair while a usable is aimed: `use_icon`/`locked_icon` parse onto the base entity
(`FElysiumEntity::UseIcon`/`LockedIcon`), `GetUseIcon()` resolves locked → `locked_icon` (via
`IsUseLocked()`, overridden by door/button `bLocked`), and `AElysiumHUD` draws the ring frame + that
icon cell from the PL3 atlas (`out/hud/use_icons.png` + `.json`, lazily loaded, `FCanvasTileItem`), else
the plain aim cross. The 72-entry icon-name table + the channel constant live in `ElysiumUseIcons.h`;
the Cog **Entity Inspector** grows a `+use` section (usable/locked, `use_icon`/`locked_icon` named, the
resolved reticle icon = what the HUD draws, look-cursor on-this). The **P4.5 tutorial-logic classes**
also run (`ElysiumLogicClasses.cpp` + three trigger leaves in `ElysiumStarterClasses.cpp`), each
grounded in the decompiled `vampire.dll` (`tools/ghidra/run.ps1 -Script DumpGrep`): **`math_counter`**
(stock — Add/Subtract/Multiply/Divide/SetValue/SetValueNoFire/SetHitMax/SetHitMin/GetValue, clamps to
`[min,max]` only when a bound is set, edge-fires `OnHitMax`/`OnHitMin`, `OutValue` carries the value),
**`logic_timer`** (stock — `OnTimer` every `RefireTime` on the substrate clock, `UseRandomTime` band,
Enable/Disable/Toggle/FireTimer), **`logic_case`** (stock value-match `InValue`→`OnCaseNN`/`OnDefault` +
PickRandom) **and `logic_case_toggle`** (the VtMB divergence `FUN_101344f0`/`FUN_101346e0`: `InValue` is
a *delta* that advances a current-case pointer that many **configured** cases — skipping empty slots,
wrapping 0..15 — then fires that case; `InitialCase` seeds it; the class is 4 bytes larger for the
current-index int), **`env_fade`** (`Fade`/`ReverseFade` full-screen colour fade — `SF_FADE_IN` reveal,
`SF_FADE_STAYOUT` hold-covered — held as one screen-fade state on `FElysiumEntityWorld` and drawn by
`AElysiumHUD`), **`func_brush`** (Enable/Disable/Toggle + `Solidity` never/always/toggle, folded with
dormancy through the body's one `SetDormant`), **`point_teleport`** (`Teleport` moves `!player` to
origin+`angles` yaw via the world's `GetPlayerPawn()` seam, capsule-lifted), and the trigger family over
`CBaseTrigger`: **`trigger_hurt`** (`damage` every 0.5 s while stood in it, `ApplyDamage`),
**`trigger_look`** (fires `OnTrigger` once the player looks at `target` within `FieldOfView` for
`LookTime` cumulative seconds), and **`trigger_autosave`** (checkpoint volume — logs + fires once; the
save is P10). A Source `COutput<T>` value seam lands with it —
`FElysiumEntity::FireOutput(name, activator, value)` fills any wire whose map-param is empty (so
`math_counter.OutValue` passes its value/delta to `logic_case_toggle.InValue`), else the authored param
wins. Every P4.5 class implements `GetDebugState` (Cog Inspector Live state), and a new **Cog
`Elysium.Logic` window** (`FElysiumCogWindow_Logic`) boards them all — per-class live state, an Inspect
button, a quick-fire of each primary input, and the current env_fade screen-fade the HUD is drawing.
`trigger_stealth_mod`/`trigger_inventory_check`/`trigger_environmental_audio` stay inert records (their
stealth/inventory/RoomDSP systems are unbuilt). The **P4.10 `game_sign` sign/popup window** also runs —
the tutorial's teaching layer, which its progression is wired through (`popup_3.OnUseEnd` opens
`popup_4`, `popup_6.OnUseEnd` unhides the chop-shop door). Offline `UE_extract_signs.py` (**PL5c**)
mirrors all 278 `vdata/Signs/*.txt` definitions verbatim into `out/signs/` (flat + lowercased) and
decodes the 57 referenced `BackgroundImage` materials to `out/signs/tex/` behind a `backgrounds.json`
manifest. At runtime `FElysiumSignData` parses the `SignData` KeyValues panel over the shared
`ElysiumKeyValues.h` reader and resolves the first-true `Sign { dependency; filename }` redirect
through `EvalCondition` (the same error-to-false host path `logic_pythoncheck` uses); `game_sign`
(`ElysiumSignClasses.cpp`) is a bodiless leaf carrying `definition_file`/`fade_in`/`fade_out`/`pause`
with `OpenWindow`/`CloseWindow`/`ChangeFile` inputs and `OnUseBegin`/`OnUseEnd` outputs; the open
panel is one screen state on `FElysiumEntityWorld` (the `env_fade` pattern, so it dies with the map)
that `AElysiumHUD` draws — background tile + word-wrapped text blocks — dismissed by left-click via
`PlayerDismissSign` (honouring `CloseOnLeftClick` + `MinShowTime`, `HideHUD` suppressing the reticle).
The layout is **client.dll's `CSignUI`**, RE-verified: a **1024×768 virtual canvas scaled uniformly by
`ScreenH/768`** (the width factor's `FUN_100cd100` is a 4:3-proportional width, so the canvas
letterboxes horizontally instead of stretching), text blocks positioned **relative to the panel rect**
rather than the screen, and a centring branch when `XPos + YPos == 0` — which is why
`interface/Pop_Ups/general` at 2048×1024 deliberately overscans off every edge. `prop_sign`, the
`NewspaperData` multi-column layout, `ClientCommand` execution, `fade_out`, and pixel-faithful VGUI
type stay on 4.10/8.8. The **P6 audio decode foundation** also runs: `FElysiumSoundCache` decodes VtMB's sounds to interleaved int16 PCM at
runtime — WAV (MS-ADPCM/IMA/PCM) via vendored `dr_wav` (6.1) and loose dialogue/music/radio MP3 via
vendored `dr_mp3` (6.2), dispatched by file extension (`LoadSoundDecoded`) — feeding a
`USoundWaveProcedural` per play (Unreal has no runtime path for loose WAV/MP3). `UElysiumAudioSubsystem`
(GI-scope) owns the decode registry + `PreviewSound2D` + a **voice pool** (`PlayVoice`/`StopVoice`/
`SetVoiceVolume`/`SetVoicePitch` over a `UAudioComponent` per voice, 3D sphere attenuation / 2D beds /
`SourceEntityName` attach / built-in fades / **looping** via `MakeWave(bLoop)` re-queuing on
`OnSoundWaveProceduralUnderflow`, reaped in `TickAudio` from the map actor) + the `elysium.playsound`/
`elysium.sound_info` verbs, surfaced in the **Cog `Elysium.Audio` window** (path input + Play, this
map's `ambient_generic` refs, live per-decode metadata + live-voices tables). **P6.3 entity-driven
audio** also runs: **`ambient_generic`** (`ElysiumAmbientGeneric.cpp`) is a leaf class playing its
`message` WAV/MP3 at the def origin — `health` = volume 0–10, `radius`→cm attenuation, `pitch`,
`SourceEntityName` parents to a mover body, Source spawnflags (0x1 everywhere / 0x10 start-silent /
0x20 not-looped), wiring `PlaySound`/`StopSound`/`ToggleSound`/`Volume`/`FadeIn`/`FadeOut`; the
**SoundScheme** system (`ElysiumSoundScheme.h/.cpp`) is a runtime KeyValues scheme parser (fields +
retail defaults from the decompiled `CSoundScheme` @0x1022a930), the **`ambient_soundscheme`** anchor
entity (`start_enabled` + `FadeIn`/`FadeOut`/`Disable` crossfade), and `FElysiumSoundSchemeManager`
(owned by `AElysiumMapActor`, ticked with the listener pos): a looping ambient bed, the **music state
machine** (explore/combat/alert stems started phase-locked + volume-crossfaded on `EElysiumMusicState`,
cvar/Cog-driven until combat scoring lands in P9), and the **polar RandomSound scheduler** (per-sound
Frequency cadence, `RandomSoundCount` cap, Dist/Height/Angle placement around the anchor) — surfaced in
the **Cog `Elysium.Sound Schemes` window** (active scheme + stems + randoms + music-state buttons + per-
anchor FadeIn/FadeOut). RoomDSP reverb submixes are deferred (P-later). **P6.4 mover sounds** also run:
doors/buttons carry a `soundgroup` token that VtMB resolves *by directory convention* (RE-verified — no
data file) to `sound/usable/<category>/<token>/<subkey>.wav` — `openable` doors use open/close/swing/locked,
`switches` buttons use on/off (`CBaseDoor::Spawn`@`0x100ef060`, `CBaseButton::Spawn`@`0x100c8810`);
`ElysiumMover.cpp`'s `FElysiumMoverBase` loads the offline manifest once (`ElysiumMoverSoundManifest`) and
the door/button state machines play through the 6.3 voice pool at the body — door open/close one-shots on
motion start with the looping `swing` moving sound stopped on arrival, `locked` on the locked `+use` path
(honouring the SILENT spawnflag 0x1000); button `on`/`off` on press/spring-back, with the explicit
`locked_sound`/`unlocked_sound` WAVs overriding on a locked/unlocked press. Offline `UE_extract_sounds.py`
verbatim-copies each map's `.ents`-referenced WAV/MP3 into `out/sound/` (no transcode; `--radio` adds
the loose radio loops as MP3 test material), mirrors each map's `ambient_soundscheme` `.txt` schemes + the
music/ambient assets they reference (**PL5a**; `--no-schemes` to skip), and mirrors each referenced mover
soundgroup's `usable/…` WAVs + writes the `out/sound/usable/soundgroups.json` manifest the runtime resolves
tokens through (**6.4**). Mover sounds surface in the Cog **Entity Inspector** Live-state (soundgroup/
resolved subkeys/last-played) and the **Audio window**'s new *Mover soundgroups* browser (per-category tree,
Play any subkey 2D) + live-voices table. The **P5 5.2 expression evaluator** also runs: `ElysiumExpr` (a
self-contained lexer + recursive-descent AST parser + exception-free tree-walk) evaluates the
restricted Python-2.1 expression subset VtMB scripts speak — int/float/str/`None` literals,
`+ - * / % | & ^ << >> **` (Python-2 floor int div/mod, string `+`/`%`), chained comparisons,
short-circuit `and`/`or`/`not`, and assignment — collapsing every error to Void (error-to-false,
RE3). Binding is one namespace, no new dispatch: `G.<flag>` reads/writes the game-state bag
(default-0; `None` deletes) with `has_key`/`keys`/`ClearAll`, and a bare targetname resolves to an
entity whose `.Input()` fires through the real chokepoints and whose `.field` reads/writes the P1
class-chain field table. `FElysiumExprScriptHost` implements the B6 field-6 seam via
`ElysiumExpr::Exec` but is **opt-in** (`elysium.script.live`; the null host stays the map-load
default until 5.4). `UElysiumGameStateSubsystem` gains `EvalScript` + a recent-eval ring; the
`Elysium.Scripting` Cog window grows a live `G` table, an eval/exec box, and an eval log, echoed by
`elysium.eval`/`elysium.exec`. The **P5 5.3 native bindings** also run: the engine `vampire`-module
surface in `ElysiumExpr` — the **11 module globals** resolve as bare names (ahead of targetnames) and
the **24 Character methods** dispatch off a character object. `FindPlayer()` returns the PC (a
`Character` value; no player entity yet), `FindEntityByName` resolves a targetname to a handle, and
`SetQuest`/`GetQuestState` route to the real quest map; `ScheduleTask` defers a source on the event
queue (5.4, below); every other global/method **logs a stub and returns a plausible default**
(predicate globals read false; `ChangeMap` stays a stub for its phase owner P4). An **unlisted** method
still binds off the character (so
`FindPlayer().ClearActiveDisciplines()` dispatches to the generic stub, not a raise), and NPC entity
handles accept Character methods too; `self`/`activator` resolve to the eval's I/O provenance when
bound (below the globals, so nothing real is shadowed); `BumpStat`/`CalcFeat` normalise their stat
name case-insensitively (`CanonicalStatName`). A single static `GNativeBindings` table drives both
membership and the debug view. `UElysiumGameStateSubsystem` gains a native-call ring
(`RecordNativeCall` + per-name counters); the `Elysium.Scripting` window grows a **Native bindings**
table (name/kind/backing-status/live call-count) and a **Recent native calls** log. Level-script
functions/constants (`cCelerity`, the level's `On*` callbacks) resolve only under the CPython host
(9.3a below); under `elysium.script.cpython 0` they are NameError, so `G.x |= cCelerity` no-ops.
The **P5 5.4 live host** also runs: real evaluation is the **map-load default** (not opt-in), so
field-6 payloads run live at map load (the tutorial's
`G.x = ...` outputs flip visibly); `elysium.script.live 0` swaps in the null host for A/B and turns the
**whole** scripting surface (field-6 + pythoncheck + ScheduleTask) dark together. **`logic_pythoncheck`**
is a leaf class (`ElysiumStarterClasses.cpp`): its `python_script` keyfield expression is evaluated by
the **`Test`** input through `FElysiumEntityWorld::EvalCondition` (via the installed host) and fires
**`OnTrue`**/`OnFalse` on truthiness (Void/error → `OnFalse`, retail's error-to-false), activator
propagated. **`ScheduleTask(delay, "<source>")`** is real (`FElysiumEntityWorld::EnqueuePython`): a
python-only deferred event (no I/O target, just the source) rides the one event queue at `now+delay`,
delivered through `DeliverEvent`'s Python half — single-steppable + serializable (R8); sources resolve
against 5.3's binding surface (retail's `__main__.`-prefixed forms NameError until 5.5, but
`ScheduleTask(1.0, "G.Foo = 42")` works today). `AddSubclassField` gains FString support. Debug: the
Event Queue window shows the deferred source verbatim in the last column (a `(python)` Target row); the
`Elysium.Scripting` window's **Live script eval** checkbox toggles the surface; the Native bindings row
for `ScheduleTask` is non-stub with a call count; a pythoncheck's `python_script` + last `Test` outcome
show in the Cog Inspector's Live-state (`GetDebugState`). **P5 5.5 is decided + has a working PoC**: the
36 loose level scripts (16,473 lines — 1,119 defs, 45 classes, try/except, imports, `exec`) are full
Python 2.1, past `ElysiumExpr`, and the 2.1→2.7 delta is ~0 (no string-exceptions, no `__future__`), so
the runtime **embeds a maintained CPython 2.7.18** (`qnox/python-2.7`) to run VtMB's own scripts 1:1 —
implementation completes in 9.3. The embed (`FElysiumPythonVM`, Win64 `ELYSIUM_WITH_CPYTHON`): loads the
vendored `python27.dll` (linked via `python27.lib`, staged next to the module binary — data-symbol
imports rule out delay-load), `Py_SetPythonHome` at the vendored stdlib, `Py_Initialize` (isolated from
UE's own embedded Python 3), and a `vampire` C-module whose one **real** binding is **`G` proxied onto
`UElysiumGameStateSubsystem`** (attribute get/set = flag read/write, default-0 / assign-None-deletes,
`keys`/`has_key`/`ClearAll`); a Python bootstrap stands up forgiving stubs for the natives 9.3 makes C
(`FindPlayer`/… + a stub `vamputil`) and routes Python stdout/stderr to the UE log. `FElysiumCPythonScriptHost`
slots into the same `IElysiumScriptHost` seam, with `elysium.py.smoke`/
`exec`/`load`/`fire`/`poc` verbs and a **CPython panel in the `Elysium.Scripting` Cog window** (status,
host toggle, load-level-script, fire the On* callbacks, python exec box). The SDK is fetched, not
committed (`tools/fetch_cpython27.py`; gitignored under
`Source/ElysiumUE/ThirdParty/CPython27/`). **P9 9.3a wires the scripts in:** the CPython host is the
**map-load default** wherever the module carries the SDK *and* the interpreter starts —
`UElysiumGameStateSubsystem::MakePreferredScriptHost` falls back to the expr host when it doesn't,
because a dead VM's all-Void evals are indistinguishable from error-to-false. `AElysiumMapActor` reads
`worldspawn.levelscript` off the parsed defs (`FElysiumEntityDefs::LevelScriptModule()`) and imports it
through `UElysiumGameStateSubsystem::LoadLevelScript` **before the spawn pass**, matching VtMB's order:
the module's top-level code (constants, `from vamputil import *`, the `On*` defs) is in place before any
entity evaluates a field-6 payload against it. Importing is a host capability
(`IElysiumScriptHost::LoadLevelScript`; the expr/null hosts report that they cannot), and the module
name is remembered so `SetScriptHost` re-imports into any newly installed host — an `elysium.script.
cpython` swap mid-map never leaves the new host with a bare `__main__`. `EvalScript` (the
`elysium.eval`/`exec` verbs and the Cog eval box) runs through the **installed host**, so a hand-run
eval resolves exactly what a field-6 payload resolves; `IElysiumScriptHost::Eval` carries an optional
`OutError` for it, since a Void return cannot separate "evaluated to None" from "raised". In the built
game on `sp_tutorial_1`: `tutorial` imports at map load into host `cpython`, `elysium.eval cCelerity` =
8, and `G.Tutorial_Discflags |= cCelerity` flips `G` to 8 — the acceptance `ElysiumExpr` can't meet.
Maps whose scripts need more of `vamputil` than the bootstrap stub provides (e.g. `santamonica` →
`RandomLine`) log an ImportError and load fine without their script; the real C `vampire` bindings and
`Entity.__getattr__` are 9.3. The **P8 8.2 glTFRuntime spike** also lands: the MIT
`rdeioris/glTFRuntime` plugin is vendored (`Plugins/glTFRuntime`) + wired into the module, loading a VtMB
NPC exported to `out/npc/<stem>.glb` (`mdl_gltf.py`: mesh + StudioBone skeleton + one animation, standard
glTF 2.0) through `UElysiumNpcSubsystem` (GI-scoped) — `glTFLoadAssetFromFilename` → `LoadSkeletalMesh(0,0)`
→ `LoadSkeletalAnimation` → an `AActor` + `USkeletalMeshComponent` spawned at the player's feet playing the
clip single-node, driven by `elysium.npc.load`/`clear`/`list` and the Cog **`Elysium.NPC`** window. glTF is
self-describing, so glTFRuntime's default config (`SceneScale 100`, `TransformBaseType::Default`,
`bAllowExternalFiles`) does the glTF→Unreal basis/scale change — **no `UE_` pre-conversion** (that rule is
for dumb containers, not a self-describing one the loader reorients). `export_all.py --npc` regenerates the
test glb (`gangmember_male_2`, 69 bones). The **P4.6 landmark transition** also runs: `trigger_changelevel`
(a `CBaseTrigger` leaf with `map`/`landmark` keyfields, `SF_CHANGELEVEL_NOTOUCH 0x2` decode, `OnChangeLevel`)
captures the player's offset from the **source** `info_landmark` and requests a deferred cross-map travel;
`UElysiumMapSubsystem::RequestLandmarkTravel` runs it on a next-tick timer (Travel destroys the entity world
mid-touch, so it can't run inline), and `AElysiumMapActor::ResolveLandmarkSpawn` seats the player at
`dest_landmark.Origin + offset` (translation-only, view yaw preserved), fires the dest landmark's
`OnEnterMapHere`, and falls back to `info_player_start` if the landmark is missing. `info_landmark` is a
first-class leaf; the scripted `ChangeMap(delay, landmark, trigger)` binding (`ElysiumExpr.cpp`) enqueues the
named trigger's `ChangeLevel` input; `elysium.map <map> [landmark]` gains a direct/console landmark entry
(offset zero, lifted onto the landmark, facing its angles). A **Transitions** section in the `Elysium.Maps`
Cog window boards it (per-changelevel "Change now", landmarks + origins, pending-travel banner, "Entered via").
Still planned: elevators + keyframe movers, Source movement, the rest of the master-material set, the real
`vampire` C bindings + level-script auto-load (9.3), menu, dialogue — see `docs/rebuild-strategy.md`.
`sp_tutorial_1` is the canonical vertical slice; `sm_pawnshop_1` (the tutorial's `newgame` landmark target)
and several other maps are also exported, so cross-map landmark travel is exercisable end to end.

## Repository facts

- **Engine:** UE 5.8. Module `ElysiumUE` (Runtime, Default loading phase). Plugins:
  `ProceduralMeshComponent` (runtime), `PythonScriptPlugin` (offline scaffolding only),
  `Cog` (vendored MIT debug-UI shell under `Plugins/Cog/`; main plugin only — CogImgui/Cog/
  CogEngine/CogCommon/CogDebug/CogDebugEditor + bundled ImGui/ImPlot/NetImgui; stripped from
  Shipping via `ENABLE_COG`), `glTFRuntime` (vendored MIT runtime glTF loader under
  `Plugins/glTFRuntime/`; the P8 NPC skeletal path — USkeletalMesh + UAnimSequence from `.glb` at
  runtime, no editor import). Module deps: ProceduralMeshComponent, ImageWrapper, ImageCore,
  RenderCore, RHI, MeshDescription, StaticMeshDescription, PhysicsCore, glTFRuntime, EnhancedInput,
  Slate, SlateCore, CogCommon (all configs) + Cog/CogDebug/CogEngine/CogImgui (non-Shipping only).
- **Source layout:** `Source/ElysiumUE/Public/*.h` + `Private/*.cpp,*.h`,
  `Source/*.Target.cs`, `Source/ElysiumUE/ElysiumUE.Build.cs`. Key types:
  `UElysiumMapSubsystem`, `AElysiumMapActor`, `AElysiumGameMode`, `AElysiumHUD`,
  `AElysiumPawn`, `AElysiumPlayerController`, `UElysiumCheatManager`, `UElysiumGameInstance`,
  `FElysiumObjModel`, `FElysiumTextureCache`,
  `FElysiumMaterialFactory`, `FElysiumStaticMeshBuilder`, `FElysiumContentPaths`,
  `UElysiumLightRig`, `UElysiumAudioSubsystem` (decode registry + voice pool) + `FElysiumSoundCache` (the
  P6 runtime WAV/MP3 decode-to-PCM path, vendored `dr_wav`/`dr_mp3`), `FElysiumSoundSchemeManager` +
  `FElysiumSoundScheme` (the P6.3 scheme parser/playback — ambient bed, music state machine, polar random
  scheduler; owned by the map actor), `ElysiumKeyValues.h` (the shared Source KeyValues reader — a
  whole-file character-stream tokenizer, so a quoted value may span lines; used by the sound schemes and
  the sign definitions), `FElysiumSignData` (the P4.10 `SignData` panel + the `CSignUI` 1024×768
  uniform-scale coordinate model, `ElysiumSign::RectToScreen`),
  `UElysiumNpcSubsystem` (the P8 8.2 GI-scoped glTFRuntime skeletal-path
  harness — `.glb` NPC → runtime USkeletalMesh + UAnimSequence spawned near the player, `elysium.npc.*`),
  `FElysiumProfileRun` (the headless profiling harness, `-ElysiumProfile`),
  `UElysiumCogSubsystem` (world subsystem that registers the 15 stock CogEngine debug windows plus the
  custom Elysium windows under an `Elysium` F1-menu group; `#if ENABLE_COG`), `FElysiumCogWindow` (the
  base for those custom windows — hands them `GetMapActor`/`GetEntityWorld`/`GetGameState` since Track-B
  entities are plain C++, invisible to Cog's UObject inspector, plus a shared `static` browser→inspector
  entity selection, plus `GetMapSubsystem` for the lifecycle windows), `FElysiumCogWindow_Status` (a live
  read-only map + entity-substrate summary), `FElysiumCogWindow_Maps` (the P2.5 map-lifecycle window: the
  exported-map list with per-row Travel buttons + a Reload button, and the current map's counts + per-phase
  load-timing table), `FElysiumCogWindow_Lights` (the P2.5 light-rig window: rig-visibility toggle, source-
  type breakdown, a clipper-paged per-source list, and live calibration sliders that re-tune the running rig
  in place via `UElysiumLightRig::ApplyLiveTuning()` — no reload), `FElysiumCogWindow_Entities` (a filter/
  histogram/dormancy browser over every record; sets the shared selection), `FElysiumCogWindow_Inspector` (the selected entity's identity, chain-walked live fields, raw
  keyvalues, 7-field outputs, and a fire-button-per-input test harness — and doubles as a **live crosshair
  inspector**: left open it keeps updating while you play (Cog renders visible windows with the menu closed),
  draws an imgui reticle, and traces the camera ray each frame to report whatever it hits — surface
  (actor/component/mesh/material + textures) *and* the entity, sticky-selected — with Text/Box/Messages
  overlay + breakpoint toggles that drive `UElysiumEntityDebugSubsystem`), `FElysiumCogWindow_EventQueue`
  (pending queue + I/O history ring buffer + pause/step), and `FElysiumCogWindow_WorldViz` (the P2.4
  world-visualization control panel: entity-gizmo off/visible/all + labels + distance sliders + color
  legend, show-triggers by class/state, I/O-beam toggle + fade window — flipping the same
  `UElysiumEntityDebugSubsystem::Viz()` state the tick renders), `FElysiumCogWindow_Audio` (the P6.1/6.2
  audio decoder harness: play a WAV/MP3 by path or from this map's `ambient_generic` refs, with a live
  per-decode metadata table + a live-voices table over the audio subsystem's voice pool), and
  `FElysiumCogWindow_SoundScheme` (the P6.3 scheme runtime: the active scheme's ambient bed/music stems/random
  one-shots, the Explore/Combat/Alert music-state buttons, and this map's `ambient_soundscheme` anchors with
  FadeIn/FadeOut), and `FElysiumCogWindow_Logic` (the P4.5 logic board: math_counter/logic_timer/logic_case
  /env_fade/func_brush/point_teleport + the trigger family, each with live `GetDebugState`, an Inspect
  button, and a quick-fire of its primary input, plus the current env_fade screen-fade the HUD draws), and
  `FElysiumCogWindow_Npc` (the P8 8.2 glTFRuntime spike board: pick a `.glb` under `out/npc`, choose a clip,
  Load through `UElysiumNpcSubsystem`, and read back bone-count/anims/applied-clip/load-ms/spawn-location +
  per-clip re-play);
  the inspector's fire buttons and the `ent_fire`
  verb both inject through `FElysiumEntityWorld::EnqueueInput` (a hand-made input queued via the real chokepoint).
  `UElysiumEntityDebugSubsystem` (a `UTickableWorldSubsystem`, `#if !UE_BUILD_SHIPPING`) hosts the Source-style
  `elysium.ent_*` verbs — `ent_fire` (targetname/classname/crosshair-picker, discovery-lists inputs when none
  given), `ent_dump`/`ent_info` (off the class tables), `ent_pause`/`ent_step`, `ent_break`, and the
  `ent_text`/`ent_bbox`/`ent_messages` per-entity `DrawDebug` overlay bitmask (`ENABLE_DRAW_DEBUG`) — with a
  multi-trace crosshair picker (nearest brush body, else the bodiless logic ent nearest the aim ray);
  `ent_break` + the `ent_messages` capture ride a `FElysiumDebugTapSink` it installs into each world epoch
  through `FElysiumEntityWorld::AddSink`. The same subsystem also hosts the **P2.4 world-visualization
  layers** as a `FVizSettings` block its always-running tick renders (so a layer left on stays on while you
  play): color-keyed entity gizmos, wireframe trigger-hull AABBs (by class or enabled/dormant state), and
  fading caller→target I/O beam arrows captured at the `TapDelivered` chokepoint — driven from the World Viz
  Cog window (primary) and the `elysium.ent_gizmos`/`showtriggers`/`ent_beams` verbs (echo). The gizmos are a
  **retained** layer (`FElysiumGizmoLayer`, `#if !UE_BUILD_SHIPPING`): one `UInstancedStaticMeshComponent` of
  unit cubes built once per epoch (one instance per entity), colour packed into per-instance custom data read
  by `M_Gizmo`/`M_Gizmo_XRay` (off/visible=depth-tested/all=x-ray via material swap) — so idle frames cost
  only the instanced draw, and a dormancy/liveness flip re-uploads just that one instance through
  `FElysiumEntityWorld::SetVisualChangedHook` (fired from `FElysiumEntity::OnDormancyChanged`/`Kill`), never a
  per-frame rebuild. Only the gizmo labels stay immediate-mode (distance-culled; no instanced text). The primary
  interactive surface is the live crosshair inspector
  (the Cog Entity Inspector above); the verbs are the scriptable echo. The Track-B entity substrate (plain C++, no reflection): `FElysiumVariant`
  (tagged Void/Bool/Int/Float/String/Vector/Handle), `FElysiumEntityHandle` (`{Index, Epoch}`),
  `FElysiumGameClock`, `UElysiumGameStateSubsystem` (GI subsystem: the `G` store, quest map, clock),
  `FElysiumEntityDef`/`FElysiumEntityDefs` (immutable parsed `.ents` records), `FElysiumEntity`
  (the live base entity: CBaseEntity keyfields + `Kill`/`ScriptHide`/`ScriptUnhide` + one-switch
  dormancy that gates the brush body + per-output `times` counters + a `World` back-pointer and
  `FireOutput` seam + `OnTouchStart`/`OnTouchEnd` overlap hooks), `FElysiumClassDesc`/`FElysiumClassRegistry` (the
  per-classname descriptor — factory, base-chain link, input + typed field tables — with
  case-folded chain lookup and an inert-record fallback for unregistered classnames),
  `FElysiumEntityWorld` (the substrate: one entity per def, name/class indices, spawn pass that
  also builds brush bodies, generation-checked `Resolve`, the `AcceptInput` + event-queue
  chokepoints, output firing, `RouteBrushTouch` overlap routing, think-first tick, epoch teardown,
  an `AddSink` seam for extra debug taps; owned by `AElysiumMapActor` via `TPimplPtr`),
  `UElysiumBrushComponent` (the per-brush-entity
  body: collision-only `UPrimitiveComponent` with a convex `UBodySetup` cooked from the def hulls,
  handle-carrying, dormancy-gated, solidity by classname — trigger/solid/none — routing begin/end
  overlaps back to the world; `elysium.BrushBodies` A/Bs it),
  `FElysiumEventQueue`/`FElysiumIOEvent` (the one time-sorted queue, R4), `IElysiumIOSink` with
  the always-on `FElysiumRingBufferSink` (1,000-entry I/O history) + `FElysiumLogSink`
  (`LogElysiumIO` + VLOG), `IElysiumScriptHost` (the field-6 Python seam on
  `UElysiumGameStateSubsystem`) with `FElysiumNullScriptHost` (logs + Void), `FElysiumExprScriptHost`
  (the 5.2 evaluator; the fallback when CPython is absent or fails to start), and
  `FElysiumCPythonScriptHost` (routes field-6/pythoncheck/`EvalScript` through the embedded VM and
  imports the map's level script; the map-load default, `elysium.script.cpython` A/Bs
  it), `ElysiumExpr` (the standalone expression evaluator
  — lexer + recursive-descent AST parser + tree-walk, `Eval`/`Exec`), and `FElysiumPythonVM` (the P5.5
  embedded CPython 2.7.18 VM — `Py_Initialize` + the `vampire` C-module with `G` proxied onto the game
  state; vendored under `Source/ElysiumUE/ThirdParty/CPython27/`, `ELYSIUM_WITH_CPYTHON` Win64-only,
  fetched via `tools/fetch_cpython27.py`).
- **Committed content (only these):** `Content/Elysium.umap` (empty boot persistent level),
  `Content/VtMB/Materials/M_VtMB_World.uasset` (world master material), `M_Sky.uasset` (2D-skybox
  cube master material), and `M_Gizmo.uasset` + `M_Gizmo_XRay.uasset` (the P2.4 entity-gizmo ISM
  masters — unlit/two-sided/translucent, colour+opacity from per-instance custom data; XRay disables
  the depth test for the x-ray mode). No converted game content, no vendored Python. A UMaterial graph
  and a `.umap` can only be compiled by the editor, so these are authored offline by generators under
  `tools/` and rebuilt as one batch by `content.bat` → `tools/build_content.py` (which the export runs
  — see Build & run).
- **Content root:** `FElysiumContentPaths::Root()` = `FPaths::ProjectDir()/"tools/out"`
  (in-repo, gitignored). Packaged builds later read a `content/` folder next to the exe.
- **Config:** `Config/DefaultEngine.ini` (boot map `/Game/Elysium`, `AElysiumGameMode`
  default, `UElysiumGameInstance`, the `ElysiumUse` +use trace channel = `ECC_GameTraceChannel1`);
  `Config/DefaultInput.ini` (legacy axis/action mappings — EnhancedInput is configured but unused).

## Target hardware

- **Minimum floor:** NVIDIA **RTX 3060-class** desktop GPU (12 GB), **1080p**. The shipped
  config targets this floor (we develop against min-spec).
- **Recommended:** **RTX 4070 / 5070-class**.

The render path is **fully dynamic** — HWRT Lumen (GI + reflections), MegaLights, Virtual
Shadow Maps (`Config/DefaultEngine.ini`). Two load-bearing facts:

- **DX12/SM6 is required.** Every one of those features silently disables under DX11/SM5 (no
  error, just a CPU-bound slideshow on the many lights). The window title must read
  `PCD3D_SM6`. HWRT also needs the GPU skin cache. There is no non-RT fallback in the shipped
  config, so a DXR-capable GPU is mandatory.
- **Lumen GI is not optional.** The dominant cost is light shadowing + Lumen GI, not geometry
  (VtMB is ~20k tris/map); the calibration (`tools/probe_light_calibration.py`) proves VtMB's
  look is indirect-bounce-dominated, so the bounce carries it. MegaLights keeps the
  many-light cost ~constant (hundreds of dynamic lights per map via `UElysiumLightRig`).

All tuning, the SM6 setup, the MegaLights-engagement checklist, the floor-budget reality, and
the calibration findings live in **`docs/rendering-perf.md`** (kept out of this fact sheet).

## Build & run (Windows)

Requires a UE 5.8 install; the `.bat` files pin `UE_ROOT=D:\Epic\UE_5.8` — edit if yours
differs. Also requires the `tools/` pipeline to have exported at least `sp_tutorial_1` from
your VtMB install.

- `build.bat` — compile `ElysiumUEEditor` (Win64 Development) via UnrealBuildTool
  (`rebuild` / `clean` / `analyze` subcommands; extra args pass through).
- `content.bat` — rebuild all committed `Content/` assets in one headless editor session
  (`tools/build_content.py` runs every offline asset generator: world + sky master materials,
  boot map). `python tools/export_all.py` invokes this at the end of a run (skip with
  `--no-content`), so a generator can't be forgotten and go stale.
- `editor.bat` — open the project in the Unreal editor (PIE via Play).
- `play.bat [map]` — launch standalone (`-game`, 1600×900); optional map name under
  `tools/out` (default `sp_tutorial_1`). WASD + mouse to fly.
- `profile.bat [map] [cam]` — **headless render profiling** (roadmap 0.1/0.2). Drives the
  `-ElysiumProfile` harness (`ElysiumProfiler.cpp`) at 2560×1440/SM6: fixed vantages near
  spawn, warmup + 300-frame CSV capture (per-pass GPU ms via `-csvGpuStats`), summary, exit —
  no interaction. `tools/profile_report.py` builds the table; results in `tools/out/_profile/`,
  baseline in `docs/roadmap.md` appendix. Add a vantage in-game with `elysium.campos`.

## Coordinate conventions

`UE_bsp_to_scene.py` emits **Unreal space directly** — centimetres, Z-up, left-handed,
winding pre-reversed — so the runtime reads geometry and every sidecar verbatim into
`FVector`, with no swap, scale, or winding flip. The Source→Unreal math lives once in
`tools/bsp.py` (`source_to_unreal` for positions, `source_dir_to_unreal` for directions;
the Y negation is a reflection, so the exporter reverses winding at OBJ-write time). Full
rules: `docs/rebuild-strategy.md` → "Coordinate conventions". Legacy non-`UE_` exporters
still emit Godot Y-up/metres (`source_to_godot`) — see the `UE_` convention above.

## Documentation map

`docs/` — the reverse-engineering reference this project builds on. **Engine-neutral VtMB
facts** (valid regardless of target engine):

- `roadmap.md` — **the work tracker** (single source of truth: phases P0–P10, task status,
  pipeline + RE backlogs, risk register, decision log). All other docs' plan sections
  point here.
- `rebuild-strategy.md` — strategy reference (tracks, milestone vocabulary, sidecar
  contracts, per-system design targets).
- `game_runtime.md` — main loop, three-layer split, RPG data model, the opening flow.
- `entity_io.md` — the Source I/O bus (7-field outputs, ScriptHide/Unhide, `use_icon`).
- `python_bridge.md` — the CPython 2.1 embedding, datamap reflection, the four call paths, `G`.
- `animation_and_movers.md` — skeletal `.mdl` v2531 (Part A) + brush movers (Part B).
- `mdl_v2531.md` — the static-geometry `.mdl` struct map.
- `audio_pipeline.md` — codecs, mixer, DSP, the SoundScheme system.
- `source_movement.md` — `CGameMovement` constants + formulas.
- `level_transitions.md` — the three spawn mechanisms + the opening map chain.
- `map-architecture.md` — the Unreal map load/unload/travel design.
- `rendering-perf.md` — the fully-dynamic render path, the shipped perf cvars, and the
  MegaLights-engagement checklist.
- `asset-enhancement.md` — the offline, code-driven remaster track (delight → upscale → PBR
  synthesis), the adjudication test, pipeline hooks, and VRAM budget. Design/not-yet-scheduled.
- `debug-tooling.md` — the three-layer debug/dev-tooling architecture (engine built-ins,
  the vendored Cog ImGui shell, Source-style `ent_*` verbs on the B2 chokepoints).
- `engine-core.md` — the entity object model (the "object language": plain-C++ entities
  with Unreal bodies, class registry, handles, one clock/queue, two chokepoints) and the
  two-phase build plan (core substrate, then the debug layer).
- `recovered/dice-system.md` — the World-of-Darkness d10 resolver (unverified; needs a
  golden test against the running game).

**Godot-prototype reference docs** (carry a banner; describe the read-only Godot prototype's
implementation, not Elysium-Unreal — the VtMB facts inside are still valid, the C#/Godot
detail is porting reference): `lighting.md`, `entity_visuals.md`, `color_gamma.md`,
`m0_menu_build.md`, and the "Mapping to Godot" sections of `audio_pipeline.md` /
`source_movement.md` / `animation_and_movers.md`.

`tools/CLAUDE.md` — the VtMB input formats and their standalone decoders.
`tools/ghidra/README.md` — the headless-Ghidra RE workspace (local-only; the whole
`tools/ghidra*/` + `tools/re/` trees are gitignored RE references, not in the repo).

## The Godot project (`E:\dev\elysium`)

A **read-only reference**: the first-attempt prototype. Consulted for proven designs and
exact data formats (Track A is a class-for-class port of its runtime) and for un-ported
system source at `E:\dev\elysium\game\src`. No further work lands there. When a doc here
mentions a bare `CLAUDE.md`, `docs/archive/…`, or `game/src/…` path, it means that repo.
