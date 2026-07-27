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
- `Config/DefaultInput.ini` — engine-side input settings only: `ConsoleKeys=Tilde`, raw
  MouseX/MouseY (`Sensitivity=1`, FOV scaling and mouse smoothing **off**, because VtMB's `m_filter`
  is 0 and the router applies `sensitivity × m_yaw` itself). It carries **no** action or axis
  mappings — every key is installed by `UElysiumInputRouter` from `ElysiumBinds::Defaults()` (11.6).
  EnhancedInput is configured as the player-input class; its mapping contexts are 10.6's.
- Boot is decided once, at game-instance init, by `UElysiumGameFlowSubsystem::BootFromCommandLine`
  (below): the menu over a backdrop by default, **New Game** under `elysium.BootMenu 0`, or a bare
  **dev map** under `-ElysiumMap=<name>` (`play.bat <map>`) / `-ElysiumNewGame=0`, which also seeds a
  **mock character** (`BeginNewGame(Tremere, male)` — interim stand-in for chargen, 9.4) so the
  player sheet the dialogue gates read exists. `-ElysiumProfile` runs the headless profiling harness.

## Map load path

`UElysiumMapSubsystem` (UE5 hard travel: `Travel` stows the target + `OpenLevel`s the map's own
baked `.umap`; the fresh world's game mode hands off to `UElysiumGameFlowSubsystem::NotifyWorldReady`,
which calls `SpawnPendingMap`) → `AElysiumMapActor`
(`AdoptBakedLevel` buckets the level's actors by the tags in `ElysiumBakedTags.h`, then builds the
runtime half; owns it for one map epoch — the engine tears the world down on travel and GC frees
it, no manual flush, no force-GC). `/Game/Elysium` is only the boot world now.
`map-architecture.md` has the model; roadmap 10.8.

| Type | Role |
|---|---|
| `FElysiumObjModel` | OBJ/MTL reader; `.emc` parse-cache |
| `FElysiumTextureCache` | DDS-preferred texture load, PNG fallback; a per-map decoded-texture dedup index owned by the map actor (a plain member), so its strong texture refs drop when the actor is torn down and GC reclaims them — threaded into the material factory |
| `FElysiumMaterialFactory` | one MID per OBJ surface. The material's blend flags pick the master — `M_World_Opaque` / `_Masked` (`illum 4`) / `_Translucent` (`blend 1`) / `M_Additive` (`additive 1`) — then bind its named params: `Albedo`, `Emissive`+`EmissiveScale`, `BumpMap`+`BumpAmount` (linear), the reflection channel (below), `BaseTex2`+`BlendAmount` (WVT). **On the baked path only ropes reach this** — world and prop surfaces render the bake's own `MaterialInstanceConstant`s, so `elysium.*` material knobs reach them through `AElysiumMapActor::ApplyMaterialOverrides`, not here |
| `ElysiumReflections.h` | the `$envmap` reflection channel (7.5), shared by the factory, the map actor's overrides and the tests. `env = saturate(EnvMask.r · EnvStrength)` drives `Roughness = lerp(RoughBase, RoughReflect, env)` and `Specular = lerp(SpecBase, SpecReflect, env)·(1 − f)`; the non-reflective base is **Lambert** (`RoughBase` 1.0 / `SpecBase` 0.0) because that is what VtMB's world is, and `$envmap` is the exception. `$envmaptint`'s grey half scales the specular level and its **chromatic** half drives `Metallic` off the mask — VtMB's own hand-authored metal mask, read not inferred (`FElysiumMaterialDef::IsChromatic`, translucent excluded: those are coloured glass). VtMB's own term is `(base + cube·mask·tint)·lightmap·2` — an albedo term the light multiplies — so the exported `tex/cube/` faces are never sampled; Lumen resolves the reflection. RE + whole-game survey: `docs/reflections.md` |
| `AElysiumMapActor::ApplyMaterialOverrides` | stands one MID per **unique** baked material in front of the level's instances so the look knobs reach real surfaces (a `MaterialInstanceConstant` has no runtime setter). **Lazy** — with every knob neutral no MID exists and the baked instances render as authored, because a runtime `SetMaterial` drops the primitive's built texture-streaming data and albedo *and* `EnvMask` fall back to a low mip (a near-white `EnvMask` mip mirrors the surface). Each knob reads the baked value and writes it whole, so the pass is idempotent and neutral restores exactly. `elysium.MaterialOverrides` gates it; `RoughBase`/`RoughReflect`/`SpecBase`/`SpecReflect` pin (negative = neutral), `EnvReflect` scales |
| `FElysiumDecals` | 7.2 decals: the C++ spec for the `.decals` projector sidecar, and what the content test validates the export against. The bake consumes it — one `ADecalActor` per `infodecal` in the `.umap`, MIC off `M_Decal`, oriented `MakeRotFromXZ(Normal, SDir)`: local +X = room normal (so −X projects into the wall), and since a deferred decal maps texture **U→local Z, V→local Y**, the surface horizontal `SDir` goes on local Z with `DecalSize = depth×HalfH×HalfW` |
| `FElysiumRopes` + `AElysiumMapActor::BuildRopes` | 8.7 ropes: parse the `.ropes` cable sidecar (chain-resolved segments) → one Verlet `UCableComponent` per line, `CableLength = RestCm` verbatim (the sidecar already carries VtMB's RE'd rest length, which is usually *below* the straight span — most ropes hang taut, not slack), `NumSegments = nodes − 1` off the sidecar's RE'd `m_nSegments` (VtMB takes it from `Type`, so a `Type 2` rope is **one** span — a straight line that cannot sag), start always pinned and end pinned unless the `Dangling` flag is set, width/tube/tiling from the sidecar, MID off `M_World_Opaque` (one per unique rope texture). `EndLocation` is the **world** endpoint B, because an unset `AttachEndTo` resolves to the owner's root component (`SceneRoot`, identity) — never to the cable itself. `elysium.Ropes` A/Bs the pass |
| `UElysiumLightRig` | one Unreal light per WORLDLIGHTS `.lights` source; soft exponent falloff, specular off (VtMB is pure Lambert), sun, skyambient, lightstyle animation, live retune via `ApplyLiveTuning()`, per-source hand override (`SetSourceIntensity`/`RevertSource`) that the calibration and style passes skip, a per-source disable (`SetSourceDisabled`/`ShouldSourceBeLit`) that changes no value and outranks the master toggle, a per-source reviewed mark (`SetSourceReviewed` — the survey's judged bit; disabling implies it), and the saved hand survey (`_lights/<map>.json`) auto-applied at adopt (`LoadSurvey`; `elysium.LightSurvey 0` = full faithful rig) |
| `FElysiumSkyDef` (`ElysiumEnvironment.h`) | the `<map>.sky` 3D-skybox placement — `world(v) = Scale·(v − OriginCm)`. Read at map load and used three ways: the `.ents` parser carries sky-scope entities through it, a miniature body takes its mesh scale from it, and the light rig scales a miniature source's reach by it |
| `ElysiumEnvironment.{h,cpp}` | `.env` sky/fog: the sky cubemap (SkyLight IBL + `M_Sky` backdrop). `BuildSkyCube` binds each decoded Source face to its Unreal slice **and rotates it into Unreal's D3D-derived cube layout** — `rt/lf/ft/bk/up/dn` onto `+X,−X,+Y,−Y,+Z,−Z` with rotations 90°CCW / 90°CW / 180° / none / 90°CCW / 90°CCW. One table carries both halves; `SkySliceFace`/`SkySliceSource` expose them to `Elysium.Substrate.SkyCube`, which re-solves the transform against VtMB's draw tables and UE's `GetCubemapVector` rather than against itself. The `.env` sidecar's `skyconv` states the export-side convention (`SkyConventionVersion`). `elysium.SkyProbe 1` swaps in the labelled RE-A2 face set for an orientation check; `elysium.SkyBrightness` is the backdrop's debug multiplier, **default 1 = parity** with VtMB's identity sky transfer, live-applied through `ApplySkyBrightness` |
| `ElysiumFog.h` | Source's distance fog as a **per-primitive** term: the six Custom Primitive Data slots (colour float4, start, `1/(end − start)`) the four surface masters read and `AElysiumMapActor::ApplySceneFog` writes onto every world / prop / miniature primitive from `<map>.env`. Per-primitive because the world and the 3D-skybox miniature carry two different authored fogs and **share screen depth** — measured, so `FogCutoffDistance`, a `LocalFogVolume` and a second fog actor are all ruled out, and a deferred fog pass has nothing else. `f = saturate((PixelDepth − start) · invRange)` is Source's own formula. An unwritten slot reads as zero, which is `invRange`, so the term is neutral by construction rather than by a branch (`Elysium.Substrate.FogPack`). The authored colour is decoded with a plain 2.2 like every VtMB colour. `elysium.Fog` A/Bs it live. The adopted `ExponentialHeightFog` is **not** the distance fog — it keeps the volumetric layer, and `FogCutoffDistance` keeps it off the backdrop |
| `FElysiumProfileRun` | the headless profiling harness (`-ElysiumProfile`) |
| `FElysiumShotRun` | the headless screenshot-regression harness (`-ElysiumShots`); shares the vantage table (`ElysiumVantages.h`) with the profiler and the capture path (`ElysiumScreenshot.{h,cpp}`) with the MCP screenshot tool |
| `ElysiumLightProbe` + `FElysiumProbeRun` | the light-attribution probe: a ray fan per `UElysiumLightRig` source traced on `ELYSIUM_PICK_CHANNEL` against the real built scene, recording what each light is nearest, whether that surface's bound MID emits (`EmissiveScale > 0`), how enclosed the light is, and its `share` of the illumination reaching the points it lights. `elysium.lightprobe [rays]` runs it live; `-ElysiumProbe` is the headless one-map-per-launch harness `probe.bat` walks every map with. Writes `tools/out/_lights/<map>.probe.json` |

Collision comes from `.hulls` (one convex `FKConvexElem` per solid world brush, PLAYERCLIP
included) + `.dispcol` (displacement trimesh) on collision-only PMCs; `elysium.BrushCollision`
A/Bs back to the render trimesh.

## The app state machine (11.3)

Design: `docs/runtime-architecture.md` §10. **`UElysiumGameFlowSubsystem`** (GI-scoped) owns
`EElysiumAppState` — Boot / FrontEnd / Loading / Playing / Paused / GameOver — and is its only
writer. The state, its names and its **transition table** are plain C++ free functions in
`Public/ElysiumAppState.h` (`CanEnter`, `IsInSession`, `HoldsWorld`), so the whole rule set is
asserted with no game instance, no world and no RHI — `Elysium.Substrate.AppState`. Two rows carry
weight: **Paused is reachable only from Playing** (the front end deliberately does not pause — the
live backdrop behind the menu is the feature) and **Boot is reachable from nowhere**.

| Entry point | Does |
|---|---|
| `BootFromCommandLine()` | at GI `Initialize`: *decides* the boot plan (dev map / new game / menu). No world exists yet, so the first `NotifyWorldReady` executes it, once |
| `NotifyWorldReady(Mode)` | the game mode's one `BeginPlay` call. A pending load → `SpawnPendingMap` then `FrontEnd` (backdrop: seat the `elysium.MenuVantage` camera) or `Playing`; no pending load → this is the boot world, run the plan |
| `NewGame(FElysiumNewGameRequest)` | clan/sex/history/spends + an `EntryPoint` (`story` \| `tutorial` \| `<map>[@<landmark>]`). The destination is checked against `ExportedMaps()` **before** `BeginNewGame` runs, because seeding is destructive. `elysium.SkipIntro` (default 1) rewrites `story` to the tutorial landmark until P12 lands the theatre act |
| `LoadGame` / `SaveGame` | the seam every caller (menu, `trigger_autosave`, quicksave, MCP) goes through. Logged stubs until **11.9** |
| `QuitToMenu()` | travel the backdrop map, then `UElysiumGameStateSubsystem::EndSession` (clear `G`, quests, the player record; rewind the clock). `G` and the quest map join the record as save blocks at 11.9 |
| `ReloadMap()` / `SetPaused` / `TogglePause` / `TriggerGameOver` | the rest of the surface; `OnAppStateChanged` is the delegate the UI listens on |

**The screen is a pure function of the state** (`ApplyMenuForState`, called from the one state
writer): FrontEnd/Paused/GameOver map to the matching `EElysiumMenuMode`, everything else hides. No
call site closes a menu — which is also what keeps the raw `elysium.map`/`elysium.reload` verbs
correct, since the `Loading` transition their `OpenLevel` raises takes a stale menu off the dying
world's viewport.

**Pause holds the world through `FElysiumTimeControl` (both halves, S1).** Leaving a run releases
that hold, but `ReleasePauseHold` is a deliberate no-op from a running state, so a hand
`elysium.pause` still survives a travel the way 11.1 says it does.

**The loading screen hooks `IGameMoviePlayer::OnPrepareLoadingScreen`, not
`FCoreUObjectDelegates::PreLoadMap`** — the movie player binds `PreLoadMap` itself at engine init,
ahead of any GI subsystem, and calls `PlayMovie()` from there, so setting attributes in our own
`PreLoadMap` handler is always one load too late. `OnPrepareLoadingScreen` is broadcast from *inside*
`PlayMovie` when none are prepared. The widget is **pure Slate with no UObjects** (`FCoreStyle` type
and brushes, `ElysiumUI::Palette` constants): it is drawn by `FSlateLoadingSynchronizationMechanism`
on another thread while the game thread is blocked in `LoadMap`, which is also where GC runs.
`IsMoviePlayerEnabled()` is false under `GIsEditor`/`GUsingNullRHI`, so PIE and the headless tiers
get `FNullGameMoviePlayer` and the hook no-ops. It covers the level-load flush only — the map
actor's build pass runs after `PostLoadMapWithWorld` (10.4).

**Esc is one verb with two key sources.** Both fire **`cancelselect`** (11.6), whose single
implementation is registered here, on the GI-scoped flow subsystem — which is what lets it resolve
when no controller is in the picture. The router binds the key (`bExecuteWhenPaused`; on the
controller, not the pawn — a backdrop world seats no pawn and the key must still be swallowed).
While a menu is up the input mode is UI-only and the controller sees nothing, so the other source is
`UElysiumMainMenu::NativeOnKeyDown` — reachable only because the menu's input scope names the widget
as its focus target and the widget is `SetIsFocusable(true)`. Escape is consumed in every menu mode,
and only Paused is a mode it leaves.

`GameOver` holds the world and raises the third menu mode. Its driver is the combat character's
death path (11.4): the player entity's health running out reaches
`UElysiumGameStateSubsystem::NotifyPlayerKilled` → `TriggerGameOver(Killed)`. The masquerade meter is
the second loss condition and is still 9.4's. `elysium.gameover` is the named command that reaches
the state by hand; the `MakePlayerUnkillable` latch is the damage system's gate, not the flow's.

Verbs: `elysium.appstate`, `.pausemenu [0|1]`, `.newgame [clan] [m|f] [entry]`, `.quittomenu`,
`.gameover [killed|masquerade]`, `.SkipIntro`, `.BootMenu`, `.MenuMap`, `.MenuVantage`,
`.LoadingScreen`, `.LoadingScreenMinTime`.

Player-facing actors: `AElysiumGameMode` (pawn/HUD/controller classes — including the
`elysium.SourceMovement` A/B between the two bodies — the no-pawn-on-a-backdrop rule, and one
`NotifyWorldReady`), `AElysiumPlayerController` (hosts `UElysiumCheatManager` — `Noclip`,
`ElysiumTeleport`, plus stock `UCheatManager` execs — and `UElysiumInputRouter`; it **binds no key**
of its own and instead implements the world verbs `+use` / `+attack` / `noclip` / `god` /
`snapshot`), `AElysiumPawn`
(the player's **body** — box hull, `UElysiumMovementComponent`, camera, noclip, and the handle of
the entity it embodies), `AElysiumHUD` (Canvas: the use-icon reticle,
`env_fade` screen fade, sign panels — player pose/mode/FPS live in the Cog Maps window; it also
ticks the native-Slate dialogue box off the world's open-conversation state, B4).

## Commands, intent and the body (11.6)

**S7 — one command registry; S5 — intent is data.** Design: `docs/runtime-architecture.md` §8.2–8.4;
the VtMB facts: `docs/controls.md`. VtMB has no action abstraction — **an action is a console command
string** — so the compiled verbs carry names and everything reaches them through one door.

| Type | Role |
|---|---|
| `FElysiumCommands` (`Public/ElysiumCommands.h`) | the registry, plain C++. **92 declared verbs** — the whole `controls.md` bindable inventory with its `+`/`-` pairs, `vphysicshand` deliberately absent. A declaration carries kind (`Once`/`ButtonPair`), group, the `FElysiumUserCmd` bit a pair latches, and help that names the owning task when nothing implements it. `Bind`/`Unbind` install implementations and **stack**, so a system owns a verb exactly while it is alive; binding an undeclared name is refused. **The latch is the verb's, not its implementation's** — `+forward` fills the user command with no handler in sight, and a world with no sink drops it |
| `ElysiumCommandBus` (`Private/ElysiumCommandBus.{h,cpp}`) | the one door: a bound key, a `ccmd` attribute-set, a `.dlg` action, `elysium.cmd <line>`, `elysium_console_exec` and `-ExecCmds` all arrive here. It reaches the `FElysiumConsole` on `FElysiumPythonVM` (which exists with or without CPython) and `EnsureSeeded`s it from `out/cfg`, so the patch's aliases resolve even when the interpreter never started |
| `FElysiumConsole::Execute` | the precedence, stated once and tested: **registered command → alias → cvar → Python**. Source's own `Cmd_ExecuteString` order, so no user alias can shadow `+forward`; the registry reporting *false* for an unknown word is the cue to try an alias |
| `FElysiumUserCmd` / `FElysiumUserCmdBuilder` / `FElysiumUserCmdStream` (`Public/ElysiumUserCmd.h`) | one frame of intent as a value: `Move`, `Up` (Source's `upmove`), `LookDelta` in degrees, `Buttons` (**uint64** — VtMB's ± inventory is 34 pairs), `DeltaSeconds`, `Seq`. The builder holds the latches and analog accumulators and composes the frame, including `+strafe` turning the turn keys into strafe and the `cl_yawspeed`/`cl_pitchspeed` keyboard-look rates. `ClearButtons` is what a scope change means for intent (`UElysiumInputSubsystem::ApplyToController` calls it, so a held key cannot bleed across a screen). The stream records, replays and round-trips through plain text |
| `ElysiumBinds` (`Public/ElysiumBinds.h`) | VtMB's default bind set — 75 rows of the Patch's `cfg/default.cfg` as `FKey` → console line, the commands held as **strings** because several defaults bind an alias (`vm_feed`, `skip`, `cam_restore`) and a key bound to either must behave identically. Also `ReservedKeys()`: the bare keys the dev layer owns, which is `` ` `` alone |
| `UElysiumInputRouter` (`Public/ElysiumInputRouter.h`) | on the player controller. Installs the binds (built by hand — `BindKey` carries no payload overload — with `bExecuteWhenPaused` on both edges, so a release cannot be swallowed by a pause and strand a latch), then `SampleFrame` runs from `PlayerTick` after `Super`, builds the command, writes the look delta **straight onto the control rotation** (not through `AddYawInput`, which applies the engine's legacy input scales) and hands the command to the body. Mouse look reads raw counts scaled only by `sensitivity × m_yaw` off the console store — VtMB's 0.066°/count, with no frame-rate term |
| `IElysiumPlayerBody` (`Public/ElysiumPlayerBody.h`) | what everything outside the body talks to: noclip, the embodied entity handle, the body half-height the teleport seam lifts a Source feet-origin by, the spawn-hold freeze, `ApplyUserCmd`. An interface because the two bodies cannot share a base |
| `AElysiumPawn` + `UElysiumMovementComponent` | the faithful body: `APawn` + a `UBoxComponent` (32×32×72 u) + a mover carrying Source's own `Friction`/`Accelerate`/`AirAccelerate`/`WalkMove`+`StepMove`/`CategorizePosition` over the `source_movement.md` constants. The hull is a **box** because `StepMove` depends on a flat bottom — a capsule reports ~0.65 against the 0.7 standable test and rejects every climb — and `ACharacter` will not take a box root. 4.7 owns the line-by-line port (real `surfaceFriction`, the gravity half-step split, ducking/**RE22**, ladders, water) |
| `AElysiumCapsulePawn` | the A/B baseline behind `elysium.SourceMovement 0`: the `ACharacter` capsule over `UCharacterMovementComponent`, consuming the same user command so the comparison is over the mover alone |

**The dev layer holds no bare key a player can bind.** `v` is `+movedown` and `t` is `toggleuiside`,
so the noclip and skybox toggles are the chords `Ctrl+V` and `Ctrl+T`, and a dev verb is an
`elysium.*` engine command (`elysium.togglesky`) that never enters the VtMB bus — the two planes do
not share a name (`decisions.md` 2026-07-27).

**Escape is one verb with two key sources.** The router binds it while the game has input; while a
screen holds input UI-only the controller sees nothing, so `UElysiumMainMenu::NativeOnKeyDown` fires
the same `cancelselect`. Its single implementation is on the GI-scoped flow subsystem, which is what
lets it resolve with no controller in the picture.

Verbs: `elysium.cmd <line>`, `elysium.commands [filter]`, `elysium.binds`, `elysium.togglesky`,
`elysium.SourceMovement`, and the command-stream set `elysium.cmd.record` / `.stop` / `.save <name>` /
`.replay [name]`.

## Input scopes (11.5)

**S6 — one input-mode arbiter.** Design: `docs/runtime-architecture.md` §8.1.
**`UElysiumInputSubsystem`** (LocalPlayer-scoped) owns a priority stack of `FElysiumInputScope` and is
the module's **only** `SetInputMode` caller. A screen, a conversation, a sign panel or the debug UI
pushes a scope while it is up and pops it when it goes away; the top decides mode, cursor, keyboard
focus and (at 10.6) mapping contexts. LocalPlayer-scoped because the stack has to survive travel and
the controller it writes to does not — `PlayerControllerChanged` re-applies onto the fresh one.

The stack itself is plain C++ (`Public/ElysiumInputScope.h`), like `ElysiumAppState.h`: no UObject and
no engine type, so the whole rule set is asserted with no local player, no controller and no viewport
(`Elysium.Substrate.InputScopes` walks every ordered pair of scopes in both close orders).
`FElysiumInputState` is what the top resolves to, as one comparable value, so a write that would
change nothing is skipped — re-applying UI-only re-steals keyboard focus.

**Push/pop is handle-based, not LIFO.** Screens close out of order (a conversation ends behind an open
pause menu), so `Pop` removes a scope from wherever it sits and the top re-resolves. Ids are never
reused, so a stale or doubled pop is a no-op. Priority decides; push order is the tie-break, so
same-priority screens behave like an ordinary modal stack.

One table (`ElysiumInput::Priority`) answers "what happens when X opens over Y":
`Game 0 < Sign 10 < Cinematic 20 < Chargen 30 < Dialogue 40 < Menu 50 < Debug 100`.

| Scope | Pushed by | Claim |
|---|---|---|
| `Menu` | `UElysiumUISubsystem::ShowMenu`/`HideMenu` | UI-only + cursor, focus on the menu widget (that is what lets Escape reach `NativeOnKeyDown`) |
| `Dialogue` | `AElysiumHUD` while a conversation is open | UI-only + cursor; the focus widget is re-pointed each turn, because the box rebuilds its whole tree |
| `Sign` | `AElysiumHUD`, reconciled off `GetOpenSign()` | **GameOnly, no cursor** — VtMB's popups are dismissed by a left-click, which is the `+attack` verb, so taking the mouse off the world would make them undismissable; the scope is there for the ordering |
| `Debug` | the subsystem's own per-frame reconcile off `FCogImguiContext::GetEnableInput()` | GameOnly + cursor, matching what Cog does to the controller anyway |
| `Cinematic` / `Chargen` | nothing yet — P12 and 9.4 | priorities reserved |

**Cog is arbitrated, not patched.** It is vendored and has no event to bind, so the subsystem observes
it. Debug sits at the top of the table on purpose: F1 over a screen is a deliberate ask, and the front
end has a menu up permanently. What keeps it from eating a screen's clicks is
`ElysiumInput::RevokesDebugCapture` — **a UI-only push revokes an inherited ImGui capture**
(`SetEnableInput(false)`, guarded on it already being enabled: that call dereferences the lazily
created ImGui context and crashes at boot otherwise). At push time only.

**CommonUI's fourth owner is declined explicitly**: `UElysiumMainMenu::GetDesiredInputConfig()` returns
unset, so `UCommonUIActionRouterBase` never writes mode or cursor.

**Pause is not a scope property** — `UElysiumGameFlowSubsystem` owns it, and the pause menu's scope is
pushed *because* the flow paused (`docs/decisions.md` 2026-07-27). Verb: `elysium.inputscopes` dumps
the stack bottom-to-top with the deciding scope marked.

## The entity substrate (Track B)

Plain C++, no UObject reflection — Unreal supplies bodies only. Design: `docs/engine-core.md`.

| Type | Role |
|---|---|
| `FElysiumVariant` | tagged Void/Bool/Int/Float/String/Vector/Handle |
| `FElysiumEntityHandle` | `{Index, Epoch}`, generation-checked through `Resolve` |
| `FElysiumEntityDef`/`FElysiumEntityDefs` | immutable parsed `.ents` records |
| `FElysiumEntity` | the live base entity: CBaseEntity keyfields + a runtime `Origin` (seeded from `Def->Origin`; `SetRuntimeOrigin`/`SetRuntimeAngles`/`SetRuntimeModel` back `Entity.SetOrigin`/`SetAngles`/`SetModel` and hand off to the `OnRuntimeTransformChanged`/`OnRuntimeModelChanged` body-follow hooks), `Kill`/`ScriptHide`/`ScriptUnhide`, one-switch dormancy gating the brush body, per-output `times` counters, `FireOutput` (Source `COutput<T>` value seam — fills any wire whose map-param is empty), `OnTouchStart`/`OnTouchEnd`, `GetDebugState` |
| `FElysiumClassDesc`/`FElysiumClassRegistry` | per-classname descriptor — factory, base-chain link, input + typed field tables; case-folded chain lookup, inert-record fallback for unregistered classnames |
| `FElysiumEntityWorld` | the substrate: one entity per def, name/class indices, spawn pass (also builds brush bodies), `SpawnRuntimeEntity` (B3 runtime creation for `npc_maker.Spawn`) = the scripted two-phase `CreateRuntimeEntityNoSpawn` + `CallEntitySpawn` (9.3 `CreateEntityNoSpawn`/`CallEntitySpawn`) fused, `RenameEntity` (`Entity.SetName` name-index re-key), the `AcceptInput` + event-queue **chokepoints**, output firing, `RouteBrushTouch`, `UpdateUseCursor`/`PlayerUse`, think-first tick, epoch teardown, `AddSink` seam, and the injected `FElysiumWorldServices` it reaches the engine through (below). Owned by `AElysiumMapActor` via `TPimplPtr` |
| `UElysiumBrushComponent` | the per-brush-entity body: collision-only `UPrimitiveComponent`, convex `UBodySetup` cooked from def hulls, handle-carrying, dormancy-gated, solidity by classname (trigger/solid/none); `elysium.BrushBodies` A/Bs it |
| `FElysiumEventQueue`/`FElysiumIOEvent` | the one time-sorted queue (R4 — no engine timers) |
| `IElysiumIOSink` | always-on `FElysiumRingBufferSink` (1,000-entry history) + `FElysiumLogSink` (`LogElysiumIO` + VLOG) |
| `FElysiumAnimating` / `FElysiumCombatCharacter` / `FElysiumPlayer` (`ElysiumPlayer.h`) | the character chain, VtMB's own (below) |
| `FElysiumGameClock`, `FElysiumTimeControl`, `UElysiumGameStateSubsystem` | the substrate clock + the one pause/time-scale facade over it (below); the GI-scoped `G` store, quest map, **player record** (`FElysiumPlayerRecord`), `BeginNewGame`/`EndSession`/`NotifyPlayerKilled` |

**Two rules that hold everywhere:** every input goes through `AcceptInput`/the event queue (so
it is loggable, pausable, single-steppable, serializable), and time comes from the substrate
clock, never `FTimerManager`.

## The player entity (11.4)

**The player is an entity; the pawn is its body (S3).** Design: `docs/runtime-architecture.md`
§5–6; the input inventory: `docs/script_api.md`. The chain in `Public/ElysiumPlayer.h` is VtMB's:

```
FElysiumEntity                     CBaseEntity           keyfields, dormancy, I/O, think
 └ FElysiumAnimating               CBaseAnimating        the body: BuildBody / PlayAnimClip /
    │                                                    ResetAnimToIdle / SetDispositionName,
    │                                                    transform + model follow, dormancy gate,
    │                                                    `skin` + `default_disposition`, SetAnimation
    └ FElysiumCombatCharacter      CBaseCombatCharacter  the SHEET (FElysiumSheet) + money / blood /
       │                                                 humanity / masquerade + TakeDamage/OnKilled;
       │                                                 the 25 datamap inputs
       ├ FElysiumNpc               CAI_BaseNPC           dialogue only — the body half is inherited
       └ FElysiumPlayer            CBasePlayer           the 10 recovered player inputs, the law
                                                         counters, the XP ledger, hydrate/dehydrate
```

`FElysiumEntityWorld::SpawnPlayer()` creates it after `Load` (the map actor calls it for every
non-backdrop map): classname `player`, targetname **`!player`** — the name the maps themselves
write, so `point_teleport target=!player` and `elysium.ent_fire !player …` are ordinary name
resolutions, not a magic keyword. `FindPlayer()`/`PlayerHandle()` are the world's accessors, and
**every reader handles null** — a menu backdrop and a headless logic world have no player, which is
the same null-service discipline 11.2 established.

**Live vs durable.** `FElysiumPlayerRecord` (on `UElysiumGameStateSubsystem`) is the session-lifetime
half; the entity is the live view. `SpawnPlayer` hydrates, `FElysiumEntityWorld::Teardown`
dehydrates — one point, covering travel, reload, quit-to-menu and a world rebuilt on a surviving
actor. `PlayerSheet()` resolves **live entity first, record otherwise**, so a write never lands on
the copy that is about to be overwritten. Health is a `Save`-flagged *entity* field (VtMB's own
placement); the record's copy exists only to carry it across a map boundary.

**The origin is sampled, the write is a move.** `FElysiumEntityWorld::Tick` reads the pawn into the
entity as its first statement (before thinks and the queue), so everything that frame reads one
place; a write (`point_teleport`, `pc.SetOrigin`) goes out through `OnRuntimeTransformChanged` →
`IElysiumEmbodiment::TeleportPlayer`, the same shape an NPC uses for its skeletal body.

**Damage and death.** `trigger_hurt` and a blocked door call `FElysiumCombatCharacter::TakeDamage`
(the body still gets the engine damage event for 4.9's reaction); at zero health the character fires
`OnDeath` and `FElysiumPlayer::OnKilled` calls `UElysiumGameStateSubsystem::NotifyPlayerKilled`,
which raises 11.3's `TriggerGameOver(Killed)` — the driver that state was waiting for.
`events_player`'s `MakePlayerUnkillable` writes the latch onto the player entity, where the damage
system is; an unkillable character floors at 1. A character with **no** health track (every NPC
until 9.4) records damage rather than dying. `ElysiumInterimPlayerMaxHealth` (100) is a **stated
interim** ceiling — VtMB derives the track from Stamina through `vdata/system`, which is 9.4's.

Eight combat-character inputs are backed by real fields (`MoneyAdd`/`MoneyRemove`/`HumanityAdd`/
`ChangeMasqueradeLevel`/`Bloodloss`/`Bloodgain`/`BloodHeal`/`WillTalk`), reproducing VtMB's
"a zero-valued input is a silent no-op"; the rest log the name and the task that owns them.

**`AElysiumPawn` is a body**: collision, movement, camera, noclip, and `GetPlayerEntity()`. The
verbs that are not movement are named commands the player controller implements (`+use`, `+attack`
for the sign dismissal, `noclip`, `god`, `snapshot`), reaching the world through one cached
map-actor pointer; the dev skybox toggle became `elysium.togglesky` on the map actor. The gait is
`+speed` in the frame's `FElysiumUserCmd`, not a key poll, so the pawn does not tick (11.6).

## The outbound seam (11.2)

Everything the substrate needs *from* the engine arrives as **`FElysiumWorldServices`**
(`ElysiumWorldServices.h`), taken by `FElysiumEntityWorld` at construction. Nothing under the world
casts its owner to a map actor, walks it to a GI subsystem, or touches
`GetWorld()->GetFirstPlayerController()`. Design: `docs/runtime-architecture.md` §7.

| Interface | Covers | Implemented by |
|---|---|---|
| `IElysiumEmbodiment` | NPC/prop/phys-prop bodies, clips, idles, skins, `BodyScaleFor` — **and the player's body**: view point, origin+yaw, teleport, damage, the `+use` trace | `AElysiumMapActor` |
| `IElysiumAudio` | `PlayVoice`/`StopVoice`/`SetVoiceVolume`/`IsVoicePlaying` (forwarded to the GI `UElysiumAudioSubsystem`) + `FadeInScheme`/`FadeOutScheme`/`ActiveSchemeRel` (this map's `FElysiumSoundSchemeManager`) | `AElysiumMapActor` |
| `IElysiumTravel` | `RequestLandmarkTravel`, `ChangeMap` (forwarded to `UElysiumMapSubsystem`) | `AElysiumMapActor` |
| `IElysiumPresenter` | `StartFade`, `OpenSign`/`CloseSign`, `OpenDialog`/`CloseDialog` | **nothing yet — 11.8** |

**Any member may be null**, and every call site handles it: that is the existing `elysium.NpcBodies 0`
/ `elysium.BrushBodies 0` A/B formalised, and it is what lets a whole map's logic run headlessly.
`Presenter` is null in play — the fade / open sign / open conversation are still world state that
`AElysiumHUD` polls, and the world *announces* to the presenter in addition to holding them, so 11.8
removes the polling path rather than migrating it.

`AActor* Owner` survives on the world, but only as the component outer (brush bodies, `phys_hinge`
constraints) and the VLOG context. It is not a fifth service.

The player-body calls live on `IElysiumEmbodiment` because the pawn *is* the player's body (S3).
Since 11.4 the substrate reaches them **through the player entity**, not directly: `point_teleport`
writes `SetRuntimeOrigin` and the entity places the body; `trigger_hurt` reduces the entity's health
and the body gets the engine damage event; `trigger_changelevel` reads the entity's own origin,
which the world sampled off the body at the top of the frame. Two behaviours live in the body
because they are its geometry: `TeleportPlayer`'s half-height lift (Source places feet, both Unreal
bodies are centred, so the number comes from `IElysiumPlayerBody::GetBodyHalfHeight`) and the `+use`
line trace on `ELYSIUM_USE_CHANNEL` — the substrate hands
over a segment and gets a handle back, keeping the usability arbitration on its own side.
`GetPlayerViewPoint` is the eye, and stays here until **11.7** owns the camera.

`Private/Tests/ElysiumTestServices.h` is the recording stub that implements all four (handing back
real transient components, so the leaf classes take their body-carrying path);
`Elysium.Substrate.WorldServices` drives a tutorial-shaped `logic_auto` chain through it with no RHI,
no actors and no `tools/out`, then re-runs the same defs with a null bundle and asserts the same
logical state.

## The frame and the clock (11.1)

Design: `docs/runtime-architecture.md` §3–4. The canonical order is declared in the engine's tick
graph (`dumpticks` reads it back), never inferred from registration order:

| # | Stage | Where |
|---|---|---|
| 1 | sample input | `APlayerController` (`TG_PrePhysics`) |
| 2 | advance the clock | `AElysiumMapActor::Tick`, first statement — **the only place `Now` moves** |
| 3–4 | run due thinks, then service the queue | `FElysiumEntityWorld::Tick` (think-first, RE2's retail order) |
| 5 | move the pawn | the movement component, prerequisite on the map actor's gameplay tick |
| 6 | physics + overlaps | engine (`TG_DuringPhysics`) → `RouteBrushTouch` |
| 7 | post-move gameplay | `AElysiumMapActor::PostMoveTick` (`TG_PostPhysics`) — the `+use` look cursor |

`AElysiumMapActor` carries **two** tick functions: `PrimaryActorTick` (`TG_PrePhysics`) for the
gameplay pass and `FElysiumPostMoveTickFunction` (`TG_PostPhysics`) for work that must see the
frame's final positions. The `+use` cursor traces, so it lives in the second — before the pawn's
move it would pick against last frame's geometry. `EnsureTickPrerequisites` binds the controller and
the movement component as they appear (a menu backdrop map never seats a pawn) and rebinds if the
pawn is replaced.

**`FElysiumTimeControl`** (`ElysiumTimeControl.{h,cpp}`, on `UElysiumGameStateSubsystem` beside the
clock) is the one pause/scale facade over the clock **and** engine time. `FElysiumGameClock` keeps
`Advance`/`SetPaused`/`SetScale`/`Reset` private and friends only this struct, so the single advance
site is a compile-time property rather than a convention. Both halves are always set: engine pause
freezes actor ticks, physics and animation; the clock hold freezes thinks, the queue, movers and
`ScheduleTask`. **Scale is applied exactly once** — engine dilation has already scaled the tick's
delta by the time `AdvanceFrame` sees it, so the clock multiplies by nothing; `SetScale` reads the
dilation back off `AWorldSettings` (which clamps it) before recording it. `ApplyToWorld`, called from
`BeginPlay`, re-stamps pause and dilation after a travel, since both are per-world state and the
clock is not. `StepFrames(N)` releases the world and `EndFrame` (the tail of the post-move pass)
counts the frames back down. Verbs: `elysium.timescale`, `elysium.pause`, `elysium.step`.

`bTickEvenWhenPaused` is **false** on both gameplay passes and **true** on the presentation side —
`AElysiumHUD` and `UElysiumEntityDebugSubsystem` — so a held world still draws a live HUD and keeps
its debug overlays, which is exactly when they are read.

Class implementations live in `ElysiumStarterClasses.cpp` (logic_auto/relay, trigger family,
`logic_pythoncheck`), `ElysiumLogicClasses.cpp` (math_counter, logic_timer, logic_case
+ the VtMB `logic_case_toggle` divergence, env_fade, func_brush, point_teleport),
`ElysiumMover.{h,cpp}` (`FElysiumMoverBase` = CBaseToggle, `FElysiumDoorBase` = the CBaseDoor
4-state machine, `FElysiumFuncDoor`, `FElysiumButton`), `ElysiumSignClasses.cpp` (`game_sign`),
`ElysiumAmbientGeneric.cpp`, `ElysiumEventClasses.cpp` (`events_player`/`events_world`),
`ElysiumNpcClasses.cpp` (B3 — the AI-free `FElysiumNpc` dialogue leaf for the living `npc_*`
classnames, over the 11.4 character chain, and `FElysiumNpcMaker` for
`npc_maker`/`npc_maker_fleshpile`), `ElysiumPlayerClasses.cpp` (11.4 — `CBaseAnimating`,
`CBaseCombatCharacter` and the `player` leaf),
`ElysiumScriptedSequence.cpp` (8.5 — `FElysiumScriptedSequence` for
`scripted_sequence`/`aiscripted_sequence`), and
`ElysiumPropClasses.cpp` (8.3 `FElysiumProp` for `prop_dynamic`/`prop_dynamic_ornament`; 8.4
`FElysiumPhysProp` for `prop_physics` and `FElysiumPhysHinge` for `phys_hinge`).

NPCs (B3, no AI) stand a real glTF skeletal body at their origin: `ElysiumNpcVisual.{h,cpp}` is the
shared glb→`USkeletalMesh` loader (the 8.2 path, reused by the `UElysiumNpcSubsystem` test harness)
plus `LoadAssetFromPath`/`RetargetClip`, and `IElysiumEmbodiment::BuildNpcVisual` caches the mesh per
stem and stands a `USkeletalMeshComponent` on the map actor (`elysium.NpcBodies` A/Bs the bodies;
I/O still resolves without them).

**Animation (8.5).** A VtMB NPC's own `.mdl` carries only its own clips — mostly dialogue — and
pulls idle/locomotion/combat from **shared animation banks** through the studiohdr include DAG, so
most NPCs have no idle of their own and would stand in the reference pose. `tools/npc_export.py`
resolves that DAG offline; the runtime looks a label up and is told which glb owns it.

| Type | Role |
|---|---|
| `ElysiumNpcClips.{h,cpp}` | plain-C++ readers for the two runtime sidecars: `FElysiumNpcIndex` (`out/npc/npc_index.json`, ~34 KB — every NPC and bank with its glb, counts and facial sidecar) and `FElysiumNpcClipSet` (`out/npc/clips/<stem>.json`, ~92 KB — one NPC's whole resolved vocabulary, ~1,360 clips). A slice interns its owner stems and activity literals, storing each clip as `[owner_i, activity_i, weight, flags, frames, fps]`. The 10.1 MB `npc_manifest.json` is **not** read at runtime — it is the offline probes' file, and a map places only 17–22 distinct models |
| `UElysiumNpcAnimSubsystem` | GI-scoped owner of everything skeleton-**in**dependent and expensive: the parsed bank `UglTFRuntimeAsset`s (2–35 MB each, session-lifetime because the same two stances banks serve essentially every map), the clip vocabularies, and the disposition table. `ResolveClip` finds a label's owning glb and retargets it onto a mesh; `PickIdleClip`/`IdleCandidates` run the default-idle policy and report which rule fired (`EElysiumIdleTier`) |
| `FElysiumDispositionTable` (`ElysiumDisposition.{h,cpp}`) | `vdata/system/dispositiontable.txt` — per disposition, the `Animation Name` that keys its stance clips plus the fidget/stance-change chances and thresholds. Shared: **8.5** reads `AnimName`, **9.9** (2,862 calls, `SetDisposition` alone 2,510) needs the same rows for the emotional-state model |
| `UElysiumNpcAnimInstance` (`ElysiumNpcAnimInstance.{h,cpp}`) | the animation host: a native anim instance (no Blueprint, no anim-graph asset) whose proxy runs two `FAnimNode_SequencePlayer_Standalone`s and lerps between them, so a clip change crossfades (0.25 s) instead of popping. It exists because VtMB's stance banks ship almost no authored transitions — one `Stance_<D>_Trans_<a>_<b>` across 21 dispositions × 2 gendered banks — so a stance change cannot route through an authored blend. The proxy must implement **`UpdateAnimationNode`**: a sequence player that is never `Update_AnyThread`'d holds its start frame forever, and the base `Update(float)` does not drive it. `elysium.NpcAnim 0` drops back to the single-node instance |

**The default-idle policy** is VtMB's own chain, and selects on **activity**, never on a label
substring: `default_disposition` (on 242 of 243 `npc_*` entities) → the table's `Animation Name` →
an `ACT_DISPOSITION` clip named `Stance_<Name>_Idle_*` → `ACT_IDLE` by `actweight` → a loose
idle-named clip → the reference pose. Weight is what discriminates inside a tier: `idle01` carries
30 against three fidgets at 1. A label read would pick `Stance_Dead_Idle_1` or `Bed_Left_Idle` out
of the 229 clips `regular_cop` resolves with "idle" in the name. Gender needs no branch — the
include DAG already bound each NPC to its male or female bank.

Ambient NPCs spread across the three standing idles VtMB authors per disposition, seeded from the
entity's own `Handle.Index` so the pick survives a reload. They do **not** cycle: every timing rule
in `dispositiontable.txt` is authored for conversation ("while waiting for the player to make a
dialog choice"), and nothing there governs an NPC standing alone.

**Root motion is not decoded**, and 66 of `move_and_ranged`'s 722 animdescs carry it (`walk` 23
records, `run` 9). A locomotion clip therefore plays in place with its feet sliding. Harmless while
NPCs do not move; the locomotion task has to decode `nummovements`/`movementindex` first.

**`FElysiumScriptedSequence`** (`ElysiumScriptedSequence.cpp`) is the cutscene beat —
`scripted_sequence` ×104 + `aiscripted_sequence` ×4. `BeginSequence` places the NPC on the marker,
plays `m_iszPlay` once, and schedules `OnEndSequence` off the clip's own length (`PlayAnimClip`
reports it through an out-param); the beat then holds `m_iszPostIdle` looping, or returns the NPC to
its disposition idle when none is authored. `m_iszIdle` is applied in `PostSpawn` (the NPC's own
`Spawn` must already have built its body), `m_iszNextScript` chains through the real event queue, and
a beat naming `!playercontroller` runs as a timing shell so the flow continues. Timing is `NextThink`
on the substrate clock — no `FTimerManager` (R4). **The outputs are the load-bearing half**: 88 wires
leave these entities across the exported maps, 48 of them `OnEndSequence`, and 67 of the 88 land on
inputs that already exist. One registered `BeginSequence` serves both the 68 I/O wires and the 68
receiver-qualified script calls, since a Python attribute and a Hammer input are the same namespace.
The class is VtMB's `CCineNPC` (HL1 `CCineMonster` lineage), which is what fixes the spawnflag
meanings — only `NOSCRIPTMOVEMENT` (128) is read, and no exported sequence starts on spawn
(`docs/entity_io.md`). **Movement is not reproduced**: the NPC is placed on the mark rather than
walked to it (`elysium.SeqTeleport 0` reverts to animation + outputs), and `OnScriptEvent01..08`
needs decoded animation events.

Resolved `UAnimSequence`s cache on the **map actor** (`NpcAnimCache`, keyed `<stem>|<clip>`), not on
the subsystem, because glTFRuntime binds each one to a specific `USkeletalMesh`'s `USkeleton` and
meshes are per-map-epoch. `IElysiumEmbodiment::PlayNpcClip`/`RefreshNpcIdle` (the map actor's, over its
private `ResolveNpcClip`) are the
seams the script surface reaches animation through, via two virtuals on `FElysiumEntity`
(`PlayAnimClip`, `SetDispositionName` — *declared* on the base for the same no-RTTI reason as
`GetAttachBody`, *implemented* once on `FElysiumAnimating`), plus `ResetAnimToIdle` for handing a
body back to its resting stance. Bound to
them: the **`SetAnimation`** input on `CBaseAnimating` (21 call sites), the
**`SetGesture`** Character method, and the animation half of **`SetDisposition`** — 2,510 calls, all
of them receiver-qualified (`npc.SetDisposition(...)`; zero bare), 2,467 a `.dlg` line's action, so
an NPC's stance follows the conversation. 9.9 still owns its emotional-state half, and the natives
table records it as `stance only` so the coverage report does not overclaim.
`elysium.npc.play <clip> [index]` plays any resolved clip on a spawned test NPC and
`elysium.npc.clips <stem> [filter]` lists a model's vocabulary with owner, activity and weight. `WillTalk` is the combat character's latch and `UseInteresting` the NPC leaf's; `StartPlayerDialogRemote` fires `OnDialogBegin` then
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
box (`SElysiumDialogueBox`, `ElysiumDialogueWidget.{h,cpp}`) the HUD adds to the viewport under a
UI-only input scope (11.5); `elysium.dlg`/`.choose`/`.advance` are its scriptable echo (`ElysiumDlgConsole.cpp`).
9.2 replaces the box on the 8.6 UI stack.

Dynamic props (8.3, no physics) stand a static-mesh body the same way: `FElysiumProp`
(`prop_dynamic`/`prop_dynamic_ornament`) calls `IElysiumEmbodiment::BuildPropVisual` — resolve the baked
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
`IElysiumEmbodiment::ApplyPropSkin`, which loads the map's baked `UElysiumPropSkinSet`
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
`IElysiumEmbodiment::BuildPhysPropVisual` — the same baked `SM_<stem>` every other prop stands, resolved
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

**Brush touch requires a pawn toucher (`UElysiumBrushComponent::RouteTouch`), and the toucher
resolves to its entity.** Only a pawn overlapping a trigger volume raises
`OnStartTouch`/`OnEndTouch`, and the activator it carries is the player entity's handle (11.4), so
`!activator` on the wires a trigger fires is real. A volume that merely intersects
another brush body — every body on a map is a component of the *same* map actor, so trigger∩trigger
and trigger∩solid overlaps fire begin/end at map-build time — is filtered out, matching VtMB (geometry
overlapping geometry is never a touch). Under OpenLevel hard travel each map builds in a fresh world
with a fresh pawn, so there is no stale previous-map pawn position to guard against (the earlier
`IsPlayerSeated` gate is retired, roadmap 10.8).

## Player-facing UI (8.6)

**CommonUI + CommonInput, with widget trees built in C++ Slate** — no Widget Blueprint assets and
no editor content loop (`docs/decisions.md` 2026-07-26). Design + the constraints that shaped it:
`docs/ui-architecture.md`; what VtMB's own UI is: `docs/vtmb-ui.md`.

| Type | Role |
|---|---|
| `UElysiumUISubsystem` | GI-scoped owner of the screens: create/show/hide, plus the `Menu` input scope it pushes while one is up (11.5 — the scope names the menu widget as its focus target, so the screen can see Escape). GI-scoped because the menu outlives any one world. *When* a screen is up is not its call — `UElysiumGameFlowSubsystem` drives it from the app state. `elysium.menu [pause\|gameover]` / `elysium.menu.close` |
| `UElysiumMainMenu` | the main / pause / game-over menu (`UCommonActivatableWidget`), one `EElysiumMenuMode` per item set; a mode change rebuilds the screen. Layout is `CVMainMenu::PerformLayout` verbatim in a 1024×768 virtual canvas — every item sized to the widest label + `20×4`, `pitch = height + 2`, centred — under one `SDPIScaler` at `ScreenH/768`. Every item calls the flow subsystem. **A `UCommonActivatableWidget` added straight to the viewport stays collapsed until `ActivateWidget()`** (`bAutoActivate` only fires inside a `UCommonActivatableWidgetContainer`) |
| `ElysiumUIStyle.{h,cpp}` | design tokens — the `VampireScheme.res` palette (gold chrome, blood accent, cyan active tab), the type ramp and spacing in virtual px, `ElysiumUI::ScaleFor` — plus `FElysiumUIFontLibrary`, which composes the committed `UFontFace` assets under `/Game/VtMB/UI/Fonts` into one runtime `UFont` per role (`FSlateFontInfo` resolves a composite font, not a bare face) |
| `ElysiumUIStrings.{h,cpp}` | the authored string table (`out/ui/strings.json`). Menu labels are **`VMainMenu_BTN_*`** tokens with retail English as the fallback — what `CVMainMenu` itself does |
| `ElysiumUITexture.{h,cpp}` | PNG → transient BGRA texture, shared by the PL3 use-icon atlas, the PL5c sign backgrounds and the PL8 title lockup |

**The menu backdrop.** `UElysiumMapSubsystem::TravelForMenu` loads `elysium.MenuMap` (default
`sm_hub_1`) as an ordinary map build **minus the player**: the substrate builds in full, because the
NPCs idling in frame are entities. `AElysiumGameMode` seats no pawn
(`GetDefaultPawnClassForController` → null) and `UElysiumGameFlowSubsystem::EnterMenuBackdrop` makes
an `ACameraActor` at `elysium.MenuVantage` the view target. The mode is latched at **Travel** time,
not at map-actor spawn, because `PostLogin` decides the pawn before `BeginPlay` runs. Leaving it is
an ordinary Travel. `elysium.BootMenu 0` boots straight into play; `elysium.MenuScrim` dials how far
the scene is knocked back behind the type.

Because the map's own logic runs behind the menu, **`AElysiumHUD` stands the player-facing HUD down
while a menu is up** (`IsMenuUp()`): no reticle, no sign panel, no dialogue box. `sm_hub_1`'s
`havenbum` opens a conversation unprompted, so the B4 box would otherwise draw over the menu; the
conversation still runs in the entity world, only its UI is withheld. The `env_fade` quad is
suppressed too — the menu-up early-return in `DrawHUD` precedes it.

`ElysiumScreenshot::Request` takes **`bShowUI`** (default false): the regression harness keeps the
UI-free capture so baselines hold, the MCP screenshot tool passes true so it shows what the player
sees. The Canvas HUD draws with the world and appears either way.

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
  raises "game entity has been deleted"), the 11 module
  globals, and the console objects **`ccmd`/`cvar`** (9.3b). **There is no `Player` type** — 11.4
  retired it; `FindPlayer()` returns an `Entity` over the player entity, so `pc.clan` is a field and
  `pc.MoneyAdd(50)` an input on the same chain walk. `Entity.__getattr__` resolves the type
  methods and instance `__dict__` first, then
  walks the class-chain tables — an **input** name manufactures a bound callable that fires
  through `EnqueueInput`, a **field** name marshals the live value — then the entity's
  `GetDynamicField` hook (the `vdata` half of the sheet, `pc.base_*`, until 9.4 names those fields),
  and only then the Character-method fallback; `__setattr__` is the mirror
  (datamap, then the dynamic bag, then `__dict__`; an input or a non-keyable field is read-only). The base
  method table is real, not stubbed: `SetOrigin`/`SetAngles`/`SetModel`/`SetName` mutate live state
  (and follow the NPC body / re-key the name index), and `CreateEntityNoSpawn`/`CallEntitySpawn` are
  the host's real two-phase spawn (they return/take an `Entity`, like `Find*`). `G` is proxied onto
  the game state with both the attribute *and* mapping protocols, because VtMB's `G[k]` is its `G.k`.
  `ccmd` executes a console command on attribute-*set* (`c.patchtype=""` → alias → Python fallthrough),
  reads back `""` on get; `cvar` get/set reads/writes a value string. `FElysiumPythonVM` owns the
  interpreter and the **`FElysiumConsole`** store (`ElysiumConsole.{h,cpp}`, plain C++): the alias/cvar
  tables parsed from `out/cfg` and the `Execute` path (alias-expand → cvar-set → Python fallthrough). The
  VM binds a mutable `Character` compatibility stub so the real `vamputil.py` imports (the patch
  monkeypatches `Character`; our 24 Character methods dispatch off the getattro, not a shared class —
  `decisions.md` 2026-07-24).
- **`FElysiumScriptFS`** (`ElysiumScriptFS.{h,cpp}`, plain C++) is the VM's own **filesystem
  namespace**. VtMB's scripts spell paths three ways and only one of them (`nt.getcwd() + moddir + …`)
  calls a redirectable function — the other two hand a relative path to the OS, which resolves it
  against the *process* cwd. That cwd belongs to the engine (`FPaths::EngineDir()` is the literal
  `"../../../Engine/"`, and UE sets the cwd to BaseDir at startup so it resolves), so the interception
  sits under `open` and the `nt` surface instead: the `FS_SHIM` bootstrap wraps them to rewrite every
  path through `vampire._fs_resolve` before the real call, which keeps genuine `file` objects and
  keeps the whole policy in C++. A path is normalized against the virtual root, has one leading
  `Vampire/` folded away, and then **reads** resolve overlay-first then through the mount table onto
  the `out/` mirror (`cfg/`, `vdata/`, `vdata/signs/`→`out/signs`, `python/`→`out/scripts`, `dlg/`,
  `sound/`), while **writes** always land in the `Saved/Elysium/ScriptFS` overlay — never in `out/`,
  which a re-export regenerates — with `a`/`r+` copying the mirror's copy up first. Escaping the
  sandbox is the one hard denial (`IOError` for `open`, re-raised as `OSError` for `nt.*`, because
  `fileutil` catches `nt.error` and the two are siblings). `sys.moddir` stays at the shipped
  `"Vampire"` so `fileutil`'s write guard passes as authored. `elysium.script.fs.log` (0/1/2) controls
  the trace; level 1 names every write and every read with no mirror behind it — which is how the
  `.lip` dialogue probes that this rebuild cannot answer stay a listed divergence rather than a silent
  one. Path policy is content-free and tests as `Elysium.Substrate.ScriptFS`.
- `ElysiumScriptNatives.{h,cpp}` is the engine `vampire` surface both hosts share: the binding
  table the Cog Scripting window renders, the stub defaults, the Character-method dispatch, and
  the native-call log. It lives outside either host so `elysium.script.cpython 0/1` swaps the
  interpreter without changing what a name does. `IsClan`/`IsPCMalk` and `IsMale` are real (they
  read the player sheet clan/gender), `CurrentMoney` reads the receiver's own `money` field (11.4),
  and **`OneOfSet(which, count)`** is VtMB's 1-based one-of-N dialogue selector,
  `(roll % count) == which - 1`, over **one roll per engine frame** — the granularity a conversation
  turn gathers its whole choice list at, which is what makes a set of N sibling `.dlg` rows show
  exactly one (`elysium.script.oneofset` pins the roll; `decisions.md` 2026-07-27). The rest of the
  character surface (inventory, blood, humanity/XP, `CalcFeat`) is a logged stub until 9.4.
  **`Whisper` and `FrenzyTrigger` are deliberately absent from the table**: each is a `vamputil.py`
  helper *and* a datamap input name, so the receiver decides which one runs — a bare call is the
  script's function, `pc.Whisper(...)` the player's input — and a row here would collapse the two.
- Both hosts bind the two names the dialogue gates and level scripts read: **`pc`** (the player
  entity — **re-bound per eval** in both hosts, because a generation-checked handle minted by one
  map is stale in the next; `None`/the Invalid-handle Character when a map has no player) and
  **`npc`** (the firing entity, per-eval from `Ctx.Self` — the conversation partner a `.dlg` action
  mutates).
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

- **Cog boots dormant and does not restore state.** `elysium.CogPersist 0` (the default) deletes
  Cog's ImGui layout ini (`Saved/ImGui/imgui.ini`) in `UElysiumCogSubsystem::Initialize`, *before*
  the dependency brings Cog up — its context reads that file on first initialise, so clearing it
  afterwards is a race. A startup ticker then closes any window and drops Cog's input capture for
  the first second. Both halves are needed: a restored window is cosmetic, but **restored input
  capture makes the game's own UI unclickable**, because ImGui consumes the click before Slate sees
  it. Set `elysium.CogPersist 1` to keep a hand-arranged layout across runs. A menu screen coming up
  also revokes Cog's capture (`UElysiumUISubsystem::ApplyInputMode`).
  **`FCogImguiContext::SetEnableInput` dereferences the ImGui context**, which Cog creates lazily on
  its first tick, so every call site guards on `GetEnableInput()` being true first — `bEnableInput`
  defaults false and only becomes true through a call that already required a live context, which
  makes it a safe proxy for "initialised". Calling it unguarded at boot crashes outright.
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
