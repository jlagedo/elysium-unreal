# Runtime architecture — the game spine

Roadmap P11 tracks this design's implementation; the playable path sequences it as PP0.

The load-bearing structure *between* the systems this project has already designed: what owns what,
at which lifetime, in what order per frame, and through which seams the pieces talk. `docs/architecture/engine-core.md`
defines the entity substrate, `docs/architecture/map-architecture.md` the map lifecycle, `docs/architecture/input-architecture.md` the
input path, `docs/architecture/ui-architecture.md` the UI stack, `docs/architecture/debug-tooling.md` the debug layers — each is sound in
isolation, and none of them owns the spine that turns "a map builds and a pawn walks" into "a session
boots, a game is played, and it saves". This doc owns that spine.

| Owns | Doc |
|---|---|
| the entity object language (R1–R8), class registry, queue | `docs/architecture/engine-core.md` |
| map load/unload/travel | `docs/architecture/map-architecture.md` |
| Enhanced Input planes, remapping, gamepad | `docs/architecture/input-architecture.md` |
| the CommonUI/Slate stack, tokens, the virtual canvas | `docs/architecture/ui-architecture.md` |
| debug layers 0–3, the MCP surface | `docs/architecture/debug-tooling.md` |
| the first↔third-person camera solve | `docs/vtmb/camera-view-modes.md` |
| **persistence** | `docs/architecture/save-architecture.md` |
| VtMB's own loop, state model, RPG data, opening flow | `docs/vtmb/game_runtime.md` |
| VtMB's player properties, lifecycle and world relationships | `docs/vtmb/player-entity.md` |
| **lifetimes, the object graph, the frame, the player object, the session, the seams** | **this doc** |

Nothing here re-derives a VtMB fact; every one is cited to the doc that owns it.

---

## 1. The four lifetimes

Every piece of state has exactly one home, and the home is a lifetime, not a class (**S4**).

| Lifetime | Ends when | Owner | Holds |
|---|---|---|---|
| **Application** | the process exits | `UElysiumGameInstance` + its GI subsystems | settings, the key profile, the input scope stack, the CPython VM, the audio decode registry, the NPC animation banks, the UI screens, the map/flow subsystems |
| **Session** | New Game, Load Game, or Quit to menu | `FElysiumSessionRecord` on `UElysiumGameStateSubsystem` | `G` + `G.morgue`, the quest map, the player record (sheet, inventory, counters), the game clock, the owned RNG streams (`docs/architecture/save-architecture.md` §8), per-map frozen snapshots, elapsed play time |
| **Map epoch** | `OpenLevel` tears the world down | `AElysiumMapActor` → `FElysiumEntityWorld` | entities, bodies, the event queue, interaction focus/prompt/session, the light rig, sound schemes, the texture cache |
| **Frame** | next tick | nobody — recomputed | the user command, interaction candidates, the view state, camera weights |

Two rules fall out, stated as prohibitions:

- **A map-epoch object never holds session state.** The player's blood pool does not live on
  `FElysiumPlayer` alone; the entity is *hydrated from* the session record at map build and
  *dehydrated back* at travel/save (§5). This is VtMB's own `.HL3` mechanism —
  "these entities travelled out with the player" (`docs/vtmb/savegame_format.md`).
- **An application-lifetime object never holds session state.** `UElysiumAudioSubsystem`,
  `UElysiumAnimSubsystem` and the CPython VM survive New Game; nothing about a run may accumulate
  in them. Quit to menu clears the session record and nothing else.

The session record is also the **entire** save payload's mutable half (`docs/architecture/save-architecture.md`) —
save/load is mechanical, not an archaeology exercise.

## 2. The object graph

```
UElysiumGameInstance                                   ── application
 ├─ UElysiumGameFlowSubsystem      app state machine, New Game / Load / Quit, loading screen
 ├─ UElysiumMapSubsystem           Travel / OpenLevel / landmark placement          (exists)
 ├─ UElysiumGameStateSubsystem     FElysiumSessionRecord + script host + debug logs (exists)
 ├─ UElysiumSaveSubsystem          slots, autosave ring, serialize/deserialize      (new, 9.5)
 ├─ UElysiumUISubsystem            screens                                          (exists)
 ├─ UElysiumAudioSubsystem         voice pool + decode cache                        (exists)
 ├─ UElysiumAnimSubsystem          banks + clip vocabularies + disposition table    (exists)
 └─ UElysiumCogSubsystem / UElysiumMcpSubsystem                        (dev-only)   (exists)

ULocalPlayer
 └─ UElysiumInputSubsystem         the input scope stack; owns mode + cursor + focus  (exists)

UWorld  (one per map epoch)                            ── map epoch
 ├─ AElysiumGameMode               class provider + one BeginPlay handshake         (exists, thinned)
 ├─ AElysiumPlayerController       UElysiumInputRouter, FElysiumUserCmd, cheat manager
 │   └─ AElysiumPawn               body only: box collision, UElysiumMovementComponent,
 │                                 UElysiumCameraComponent. No game state.
 ├─ UElysiumPresentationSubsystem  publishes FElysiumViewState; the UI's only source   (new)
 └─ AElysiumMapActor               the map epoch's owner                            (exists)
      ├─ FElysiumEntityWorld       entities, queue, chokepoints                     (exists)
      │    └─ FElysiumPlayer       the player ENTITY — hydrated from the session record  (new)
      ├─ UElysiumLightRig / ropes / decals / sky / fog / sound schemes              (exists)
      └─ implements IElysiumWorldServices                                           (new seam)
```

**Direction of dependency is one-way and enforced by interfaces (§7).** The substrate
(`FElysiumEntityWorld` and everything under it) is plain C++ that knows about `IElysiumWorldServices`,
not about `AElysiumMapActor`, `UWorld`, or any widget. UI knows about `FElysiumViewState`, not about
the substrate. Actors know about both.

## 3. The frame

VtMB's frame is `Input → Server(GameFrame) → Client → Sound → Render` (`docs/vtmb/game_runtime.md` §1), and
it is **move-first**: player movement is not in `GameFrame` at all. The engine runs the whole
`SV_ReadPackets` → `ProcessUsercmds` → `CPlayerMove::RunCommand` chain while draining the client's
`clc_move` message, strictly before `SV_Frame` calls `GameFrame` — so the pawn has already moved
before a single think or queued event runs (**RE21**). Inside `GameFrame` it is then
**think-first**: `Physics_RunThinkFunctions` then `CEventQueue::ServiceEvents` (**RE2**). That is
the order reproduced below; Unreal's tick groups and prerequisites are how it is *pinned* rather
than left to registration order (**S2**).

| # | Stage | Where | Notes |
|---|---|---|---|
| 1 | sample input | `APlayerController::PlayerTick` (`TG_PrePhysics`) | key bindings → command bus → latches; then `UElysiumInputRouter::SampleFrame` builds the frame's `FElysiumUserCmd` (§8) |
| 2 | advance the clock | `AElysiumMapActor::PreMoveTick`, **first** tick function | **the only place `Now` moves** (§4); ahead of the move, because the move runs on this frame's `now` |
| 3 | the player's own think | `FElysiumEntityWorld::RunPlayerThink(Now)` | retail runs it inside `RunCommand`, not in the think pass; admitted only after map activation |
| 4 | **move the pawn** | `UElysiumMovementComponent::TickComponent` (`TG_PrePhysics`, prereq on the pre-move tick) | consumes the frame's `FElysiumUserCmd`, on **the command's** delta |
| 4a | map-floor barrier | `AElysiumMapActor::PrimaryActorTick` (`TG_PrePhysics`) | carries no gameplay; Unreal makes characters standing on map-owned collision depend on this tick |
| 4b | **move NPC agents** | active native `UCharacterMovementComponent` ticks (`TG_PrePhysics`) | only requested movers tick; they follow Recast/Detour requests issued by the prior entity think, and the separate gameplay tick has a prerequisite on each live motor |
| 5 | run due thinks | `AElysiumMapActor::GameplayTick` → `FElysiumEntityWorld::RunThinks(Now)` | `GameFrame` begins here; movers issue their swept kinematic moves |
| 6 | service the queue | `FElysiumEntityWorld::ServiceEvents(Now)` | delayed I/O, field-6 Python, `ScheduleTask`; the audio/scheme pass follows |
| 7 | physics + overlaps | engine (`TG_DuringPhysics`) | Chaos overlap callbacks → `RouteBrushTouch` |
| 8 | post-move gameplay | `AElysiumMapActor::PostMoveTick`, **fourth** tick function (`TG_PostPhysics`) | settle camera/body `+use` focus, consume queued command edges, update `Follow` camera shots |
| 9 | camera | `APawn::CalcCamera` via `APlayerCameraManager` | weight stack solved here (§9) |
| 10 | publish | `UElysiumPresentationSubsystem` (`TG_PostUpdateWork`) | builds `FElysiumViewState`, fires discrete events |

Three mechanisms hold the order, all stock UE 5.8:

- `AElysiumMapActor` registers **four tick functions** — a `TG_PrePhysics` pre-move tick (2–3), its
  native primary tick as the map-floor barrier (4a), a separate `TG_PrePhysics` gameplay tick
  (5–6), and a `TG_PostPhysics` post-move tick (8). Four functions on
  one actor is the engine's own answer to "some of my work must straddle the move and physics";
  splitting into separate actors would reintroduce the ordering question it solves.
- Prerequisites, not groups, order the two `TG_PrePhysics` passes around the move: the pre-move tick
  takes one on the player controller, the movement component takes one on the pre-move tick, and the
  gameplay tick takes one on the movement component. **The gameplay tick also takes one on the
  pre-move tick unconditionally**, wired at registration — the menu backdrop and a headless logic
  world seat no pawn, so the movement edge never forms there and the clock would otherwise advance
  in registration order relative to the floor barrier. Character movement automatically depends
  on the primary tick of the actor owning its floor, so GameFrame cannot live on that primary tick:
  every native NPC motor instead adds a prerequisite to the separate gameplay tick. That tick
  samples this frame's feet/yaw before advancing a route or issuing the next request without
  closing a movement-base cycle.
- Before map activation, the pre-move, floor-barrier, and gameplay tick functions have
  `bTickEvenWhenPaused = true`
  so an inherited dev hold cannot deadlock readiness; their gameplay branches are phase-gated.
  Activation restores all three to false. The post-move gameplay tick is always false, while the
  presentation tick is **true**, so a paused world still draws a live HUD and a Cog window still
  updates (`docs/architecture/debug-tooling.md`).

**What the move-first order gives up, stated.** A door's think issues its swept move in step 5,
after the pawn moved in step 4 — so the pawn is moved against last frame's mover positions, and the
overlap is resolved from the *mover's* side when it sweeps. Retail resolves it by **pushing**:
movers are `MOVETYPE_PUSH` and displace what they touch. `FElysiumMoverBase` does not push — it
sweeps, and `FElysiumDoorBase::OnMoveBlocked` deals `dmg` and reverses. That absorbs the tunnelling
case (the pawn does not pass through), but the resolution is a reverse where retail's is a shove.
Reproducing the push is the mover family's remainder, tracked on **roadmap 4.8**, not the frame's.

**One retail behaviour is a contract rather than code.** `frametime`/`curtime` are rebound to the
user command's own timing for the move's duration. `UElysiumMovementComponent::TickComponent` takes
its delta from `PendingCmd.DeltaSeconds` in preference to the tick's, which is the identity while
the router builds exactly one command per frame — and stops being the identity the moment a frame
carries more or fewer (a replayed command stream, a hitch clamp, a fixed step under 4.7).

Full chain and addresses: `docs/vtmb/game_runtime.md` §1.

## 4. Time, pause and time scale

`FElysiumGameClock` is the single game-visible clock (**R4**, **S1**), with **one advance site** and
**one facade over engine time** (11.1).

```cpp
struct FElysiumTimeControl                 // on UElysiumGameStateSubsystem, beside the clock
{
    double AdvanceFrame(double Delta);     // the ONE advance site (§3 step 2); Delta is already dilated
    void   EndFrame();                     // tail of a released frame (§3 step 8): spends one dev step

    void  SetPaused(bool bPaused);         // clock hold + UGameplayStatics::SetGamePaused
    void  SetScale(double Scale);          // clock scale + SetGlobalTimeDilation (Celerity, host_timescale)
    void  StepFrames(int32 N);             // dev: release N frames while paused
    bool  IsPaused() const;
    void  ResetClock(double StartSeconds); // fresh session / load restoring a saved curtime
    void  ApplyToWorld();                  // re-stamp pause + dilation after travel
};
```

The clock keeps `Advance`/`SetPaused`/`SetScale`/`Reset` private and friends this struct alone, so
"one clock, advanced in one place" is a compile-time property. `SetScale` reads the dilation back
off `AWorldSettings` (which clamps it) before recording it, so the two halves cannot disagree.
Engine pause and dilation are per-world state and the clock is not, which is what `ApplyToWorld`
is for — the map actor calls it from `BeginPlay`. Verbs: `elysium.timescale`, `elysium.pause`,
`elysium.step`.

- **Pause is one call.** The menu does not pause anything; the substrate keeps running behind it,
  which the menu backdrop wants and the pause menu does not (owner call). The difference is a
  property of the app state (§10), not of the menu widget: `FrontEnd` runs the world unpaused as a
  backdrop, `Paused` holds it. Engine pause freezes actor ticks, physics and
  animation; the clock hold freezes thinks, the queue, movers and `ScheduleTask`. Both are needed —
  either alone leaves half the world moving.
- **Time scale is one call.** VtMB's third-person blend is explicitly scaled by the player's time
  scale (`docs/vtmb/camera-view-modes.md` §3), and disciplines schedule on the same queue as everything else, so
  bullet time must be a clock property, never a per-system multiplier. **Scale is applied exactly
  once:** engine dilation already scales the tick's `DeltaSeconds`, so the clock consumes dilated
  dt and applies no factor of its own — the facade sets both, the clock multiplies by neither.
- **`FTimerManager` stays banned** (R4). Anything that needs a delay uses the queue, which
  serializes; anything that needs a frame uses a tick function.

## 5. The player object

**The player is an entity; the pawn is its body (S3)** — built by 11.4.

VtMB's own architecture says so three times over:

- The **save** puts the player in the entity table with `FENTTABLE_PLAYER`, and its 277 datamap
  fields *are* the character sheet — attributes, disciplines, quests, XP ledger, masquerade counters,
  inventory handles (`docs/vtmb/savegame_format.md` → "What the player entity holds").
- The **script surface** treats it as one: `FindPlayer()` is documented *"Find the first player
  entity"*, the player class carries **10 datamap inputs** (`GiveItem`, `AwardExperience`, `Whisper`,
  `SetCriminalLevel`, …) and `CBaseCombatCharacter` carries **25 more** (`MoneyAdd`, `HumanityAdd`,
  `Bloodloss`, `FrenzyTrigger`, …) that scripts reach through the ordinary `__getattr__` datamap walk
  (`docs/vtmb/script_api.md`).
- The **map data** wires to it: `pc_0` inputs, `ALLOW_CLIENTS` trigger filters, `trigger_hurt`,
  `point_teleport`, `+use` activators.

### The shape

```
FElysiumEntity                    CBaseEntity      — keyfields, dormancy, I/O, think
 └ FElysiumAnimating              CBaseAnimating   — body follow, PlayAnimClip, skin, disposition
    └ FElysiumCombatCharacter     CBaseCombatCharacter — the SHEET: 25 inputs, money,
      │                                                  blood, humanity, masquerade,
      │                                                  damage/death, inventory ownership
      ├ FElysiumNpc               CAI_BaseNPC      — SetRelationship, the 16 NPC outputs
      └ FElysiumPlayer            CBasePlayer/CHL2_Player — the player inputs, the body link
```

The player leaf's classname is `player` and its targetname is **`!player`** — the name the maps
themselves write (48 of the 49 `point_teleport.target` keys). Retail resolves `!player` and
`!pvsplayer` through the special leading-`!` single-result path; the literal targetname also keeps
the entity legible to ordinary inspection and save state. The player-wide lookup facts belong to
`docs/vtmb/player-entity.md`.

`FElysiumPlayerRecord` (session lifetime) is the durable half:

It carries the player name and appearance identity, sheet, money, health projection, XP ledger and
accumulators, passive effects, terminal email flags, law counters, journal, selected quest-log area,
History, unkillable latch and an in-progress feed transaction. Humanity, blood, Masquerade, clan and
sex remain slots on the sheet rather than parallel members. Inventory travel preserves complete
moveable item entities and their equipped relationships; it is not a list of class names folded into
the record. `docs/vtmb/player-entity.md` owns the recovered retail object boundary and the remaining
transition questions.

`Hydrate(const FElysiumPlayerRecord&)` at map build, `Dehydrate()` when the world is torn down and
again into the save's `Player` block (11.9). The entity is the *live* view; the record is the truth
that crosses a map boundary, and `UElysiumGameStateSubsystem::PlayerSheet()` resolves live-entity-first so a write can
never land on the copy that is about to be overwritten.

### What the pawn is

`AElysiumPawn` is the body and nothing else: collision, movement, camera, noclip, and the handle of
the entity it embodies. No `+use` routing, no sign dismissal, no `IsInputKeyDown` polling, no
`GI → MapSubsystem → MapActor → EntityWorld` walk — the non-movement verbs are named commands
(§8.2) and the gait is the `+speed` bit of the frame's user command.
`FElysiumPlayer::SetRuntimeOrigin` moves the body, exactly as `FElysiumNpc` moves a skeletal one, so
`point_teleport`, landmark placement and a scripted `pc.SetOrigin(...)` are one path. The reverse
direction is a sample: the world reads the pawn into the entity once a frame, before thinks and the
queue. `UElysiumMovementComponent` arrived with 11.6 and `UElysiumCameraComponent` with 11.7.

### What it collapsed

| Before | Now |
|---|---|
| `vampire.Player`, a second Python type over a struct on the game state | `FindPlayer()` returns an ordinary `Entity`; `pc.clan` / `pc.humanity` are datamap fields; `pc.MoneyAdd(50)` is a datamap input — the R2 walk, no new dispatch |
| 25 `CBaseCombatCharacter` + the player inputs unreachable | registered inputs on the chain, shared by the I/O bus and the script bus (**one** `MoneyAdd`) |
| a 3-field sheet struct + a loose `TMap` on the game instance | `FElysiumSheet` on the combat character, its `vdata` half reached by `GetDynamicField` until 9.4 names those fields |
| triggers filter on "is the toucher a pawn"; activator is always `Invalid` | the toucher resolves to its owning entity; `!activator` is real |
| the NPC leaf carried its own body, clips, idle policy and dormancy gate | all of it on `FElysiumAnimating`, shared with the player |
| damage/death/frenzy have no receiver | `FElysiumCombatCharacter` is the receiver, shared by player and NPC — which is where VtMB put it — and the player's death is what drives `GameOver` |

## 6. The entity class chain, and why the middle nodes matter

`docs/architecture/engine-core.md` R2 says one name table per class serves I/O dispatch, Python attribute access,
keyvalue application, save-field enumeration and the inspector. That only pays off if the chain
mirrors VtMB's, because VtMB's *data* is authored against that chain: a `.dlg` action calls
`npc.SetDisposition(...)` and a Hammer wire fires `MoneyAdd` on the same class.

Adding `FElysiumAnimating` and `FElysiumCombatCharacter` as real registry nodes:

- puts `PlayAnimClip` / `SetDispositionName` (currently virtuals on the base "for the same no-RTTI
  reason") where they belong, and lets `prop_dynamic` and NPCs share them without the base carrying
  character concerns;
- gives 9.9's disposition model, 9.10's economy, 9.4's counters and 9.8's inventory **one** place to
  land, shared by every character in the game rather than special-cased on the player;
- makes save-field enumeration a chain walk with no player branch (`docs/architecture/save-architecture.md`).

Registration stays what it is: static per-class descriptors, case-folded chain lookup, unregistered
classnames as inert records.

## 7. Services — how the substrate reaches the engine

`FElysiumEntityWorld` reaches the engine through one injected service bundle and nothing else (11.2):

```cpp
struct FElysiumWorldServices
{
    IElysiumEmbodiment*    Embodiment = nullptr;  // bodies, meshes, clips, skins, and the player's body
    IElysiumAudio*         Audio      = nullptr;  // PlayVoice/StopVoice/scheme control
    IElysiumTravel*        Travel     = nullptr;  // RequestLandmarkTravel, ChangeMap
    IElysiumPresenter*     Presenter  = nullptr;  // OpenDialog/OpenSign/StartFade -> the view state
    IElysiumWeather*       Weather    = nullptr;  // wetness transitions and particle emitter state
    IElysiumCameraService* Camera     = nullptr;  // the camera seam
};
```

`AElysiumMapActor` implements embodiment, audio, travel and weather and hands the bundle to
`FElysiumEntityWorld` at construction; the presenter is the world-scoped
`UElysiumPresentationSubsystem` (11.8), which the actor looks up and threads in. The actor is still
the world's component outer and VLOG context — that is not another service, and nothing under the
world casts it to a map actor or walks it to a subsystem.

**What `IElysiumPresenter` carries is a moment, not the state.** The fade, the open sign and the
open conversation stay on `FElysiumEntityWorld` — each is world state with the map epoch's lifetime,
and 11.9 serialises it — and the publisher *samples* the clock-derived half (the fade's current
alpha, the panel's `fade_in` ramp, the interaction view, the meters) once per frame. What has no clock
behind it is announced: a fade starting, a panel opening, a conversation opening or closing. A diff
over the published state cannot tell a conversation that closed and reopened inside one frame from
one that never moved, which is why the announcement is kept rather than inferred (§11).

The player's body lives on `IElysiumEmbodiment` because the pawn *is* the player's body (**S3**):
view point, body-use origin, origin, teleport, damage, and the registered-anchor `+use` query. Since 11.4 the substrate reaches them
*through the player entity* rather than directly — `point_teleport` writes `SetRuntimeOrigin` and
the entity places the body, `trigger_hurt` reduces the entity's health — so what is left on the
interface is the body's own geometry, the eye, and the camera — 11.7 added the scripted-shot channel
here rather than to `IElysiumPresenter`, because the camera is part of the body.

An NPC's visible skeleton and native motor use that same outbound boundary. The substrate asks
`IElysiumEmbodiment` to build/play the glTF body, select a manifest clip by ACT activity, and create
an engine-neutral `IElysiumNpcMotor`. The implementation is an `ACharacter` with Detour crowd path
following; the interface exposes only move/stop/teleport/enable/freeze/ignore-collision/sample.
Recast, controllers and movement components therefore never enter the plain-C++ entity layer, while
route/place ownership, I/O and serialization never enter Unreal AI state. Visibility and capsule
collision remain enabled for a standing character.

**A cutscene borrows body state, and the two ways it does so are deliberately separate.** A
choreographed scene's `position_start` immobilises its cast — the engine's `MOVETYPE_NONE` +
`SOLID_NONE` + `FSOLID_NOT_SOLID` — while the cast stays on camera, and a `scripted_sequence`
carrying spawnflag 4096 turns character-vs-character collision off for the beat's duration so
several NPCs can share one mark. Neither is `SetEnabled(false)`, which also takes the body off
screen. They reach the motor as `SetFrozen` and `SetIgnoreCharacterCollision`, and the substrate
calls them through `FElysiumEntity::SetBodyFrozen` / `SetIgnoreCharacterCollision` so a scene or a
beat never needs to know whether its actor has a body at all. Solidity is therefore three
independent decisions — enabled, frozen, character-ignoring — resolved in one place on the body and
re-applied whenever any of them moves. When an enabled body crosses the map-ready barrier, one uncached
`FindFloor` plus `AdjustFloorHeight` pass settles its approximate authored feet origin against the
live capsule collision, then its movement component remains inactive and its controller absent until
the first request. Stop, arrival, failure and path-following loss all return the movement component
to that sleeping state; abandoning an ambient place cancels its outstanding request before releasing
the authored claim.

### Execution and query are separate kinds (S11)

Every call across the bundle is one of two kinds, and naming which one it is decides what may
cross. An **execution** call carries a decision the substrate has already made and asks the engine
to perform it — `PlayNpcClip`, `MoveTo`, `StartFade`. A **query** call asks the engine something the
substrate cannot know: geometry, visibility, reachability, a rendered fact. The distinction matters
because a query's answer enters a decision as its premise — an execution's return only reports what
the engine did with a decision already made, a clip's authored length or a request's refusal — so
the query seam is where a rule can quietly stop being VtMB's.

A query carries three obligations, all discharged at its declaration:

- **It returns geometry or a candidate, never a verdict.** `QueryFeedTarget` hands back whatever the
  hull trace found, and every eligibility question — paired state, automatic acceptance,
  `ResistsFeeding`, the opposed check — stays in the substrate. `QueryPlayerUse` returns focus
  geometry and leaves class eligibility and session policy to `FElysiumEntityWorld`.
- **Its headless answer is part of the contract, not a placeholder** — stated in its default
  implementation, or in the null-service branch its call sites share when the method is pure
  virtual. A `-nullrhi` run and an editor
  commandlet never render, so `IsNpcBodyVisible` defaults to `true`: a query answering "not visible"
  there would stall every idle schedule in exactly the runs meant to prove it. A query whose absent
  implementation changes a decision has to say which way it fails and why.
- **A divergence from the oracle VtMB used is named where the query is declared.** Source's PVS is
  leaf-to-leaf and view-independent; a render-time query is frustum-dependent, so an NPC standing
  behind the player holds its pose and resumes a frame after it returns to view. That belongs on
  `IsNpcBodyVisible`, not discovered at a call site.

**The decidable test is who arbitrates.** A value someone typed — a keyfield, a table row, a
threshold, a weight, a selection order — is a decision: the rule that reads it runs in the
substrate, and it is never handed to an engine subsystem as a parameter to arbitrate (K13 states
the same test for the gameplay layer). Authored *data* may still cross the seam as data —
`ResolveDisposition` hands a typed row down by value because the table's loader needs the export
root — and an execution's receipt may feed a rule, the way a clip's authored length paces the
idle schedule. A fact only the live world can answer is a
query, and Unreal answers it. **A query approximated with substrate arithmetic because no seam
carries it is a missing query, not a substrate rule**: the arithmetic and the world it stands in for
will disagree, and the schedule built on it fails for a reason retail never had.

Any member may be null, and every call site has to handle "no body" (`elysium.NpcBodies 0`,
`elysium.BrushBodies 0`), so null-service is the existing A/B path formalised.

What this buys, concretely: a **Substrate-tier test can run a whole map's logic headlessly** against a
recording stub — the elevator chain, a dialogue tree, a save round-trip — with no RHI, no actors, and
no `$ELYSIUM_EXPORT_ROOT`. That is the missing middle tier between "variant arithmetic" and "launch the game",
and `Elysium.Substrate.WorldServices` is its first occupant.

## 8. The control surface

`docs/architecture/input-architecture.md` owns the Enhanced Input design — four planes, one
`UInputAction` per bindable command, `UPlayerMappableKeySettings` ids, gameplay contexts, UI
navigation ownership and the `config.cfg` projection. Roadmap 10.6 owns its staged status. The
runtime spine below owns arbitration shared by gameplay, screens and debug input.

### 8.1 One input-mode arbiter (S6)

`UElysiumInputSubsystem` (LocalPlayer-scoped) owns a priority stack of input scopes and is the only
thing in the module that calls `SetInputMode`. A screen, a conversation, a cutscene or the debug UI
pushes a scope while it is up and pops it when it goes away; the top of the stack decides what the
engine is told. Roadmap 11.5.

```cpp
struct FElysiumInputScope            // plain C++ — ElysiumInputScope.h
{
    FName    Name;                   // "Menu", "Dialogue", "Sign", "Cinematic", "Chargen", "Debug"
    int32    Priority;               // ElysiumInput::Priority::*
    EElysiumInputMode Mode;          // GameOnly | GameAndUI | UIOnly
    EElysiumCursorPolicy CursorPolicy; // Never | Auto | Always
    TArray<FName> Contexts;          // mapping contexts applied while this scope is top (10.6)
    TSharedPtr<SWidget> FocusWidget; // where keyboard focus goes under UIOnly
};

FElysiumInputScopeHandle UElysiumInputSubsystem::Push(FElysiumInputScope Scope);
bool                     UElysiumInputSubsystem::Pop(FElysiumInputScopeHandle&);
```

The **top scope decides everything** — mode, cursor policy, focus, which mapping contexts are applied.
Push/pop is **handle-based, not last-in-first-out**, because screens genuinely close out of order: a
conversation ends behind an open pause menu, and the menu has to find exactly the mode it pushed
over. Ids are never reused, so a stale or doubled pop is a no-op rather than a mismatched pop.
Priority decides, and push order is the tie-break, so two screens at the same priority behave like an
ordinary modal stack.

One table answers "what happens when X opens over Y":
`Game 0 < Sign 10 < Cinematic 20 < Chargen 30 < Dialogue 40 < Terminal 42 < Character 45 < Menu 50
< Debug 100`. Sign, chargen, dialogue, terminal, character and menu are UI-only and remove gameplay mapping contexts;
CommonUI owns their focus, navigation, Accept and Back. The sign presents one Continue action which
requests dismissal through presentation, while the entity world revalidates dwell and click-close
policy. The legacy `+attack` command reaches the same world request without owning physical sign
input. The terminal's semantic actions route through the same authoritative command bus as typed
input (`docs/architecture/computer-terminal-architecture.md`). **Debug is the top of the table**,
because F1
over a screen is a developer asking for the debug UI and the front end has a menu up permanently.
What keeps it from eating that screen's clicks is the other half of the rule: **a UI-only push
revokes an inherited ImGui capture** (`ElysiumInput::RevokesDebugCapture`), at push time only, so the
deliberate F1 afterwards still works. Cog itself is a vendored plugin with no event to bind, so the
arbiter observes it — a per-frame reconcile pushes and pops the `Debug` scope off
`FCogImguiContext::GetEnableInput()`.

**Pause is not a scope property.** It has exactly one owner (`UElysiumGameFlowSubsystem`, §10), and
the pause menu's scope is pushed *because* the flow paused — a scope that also drove pause would be
a second writer, and a re-entrant one.

The stack and its arbitration are plain C++, so the whole rule set is asserted with no local player,
no controller and no viewport — `Elysium.Substrate.InputScopes` walks every ordered pair of scopes in
both close orders. `FModifyContextOptions::bIgnoreAllPressedKeysUntilRelease` plus the input router's
held-button clear settles keys held across a push, which is `docs/vtmb/controls.md`'s open "what does
conversation do to held input" question on our side.

CommonUI brings its own input writer — `UCommonUIActionRouterBase` applies an `FUIInputConfig` per
activated widget, a *fourth* mode owner living inside the engine. **The scope stack is the sole
authority** (owner call): Elysium's activatable widgets return no
desired input config (`GetDesiredInputConfig()` → unset) so the action router never writes mode or
cursor, and `UElysiumActivatableScreen` pushes/pops the centralized screen scopes instead. An
`Auto` cursor policy follows CommonInput's live device without changing the active screen.

### 8.2 One command registry (S7)

VtMB has no action abstraction — **an action is a console command string**, and the patch's whole
vocabulary is *aliases* (`f` → `vm_feed` → `checkFeed()`), so a key bound to a compiled verb and a key
bound to a user alias must be indistinguishable (`docs/vtmb/controls.md`, `docs/architecture/input-architecture.md`). That already
argues for routing bindings through `FElysiumConsole`. The other end is the gameplay-verb registry:
the console knows aliases, cvars and Python, while `FElysiumCommands` declares button-pair/one-shot
verbs and their user-command bits. `+use` therefore has no pawn key callback and no direct entity
handler; every source latches the same command bit.

**Declaration and implementation are separate.** The inventory is static data — one table of every
bindable verb with its kind, its group, and the user-command bit a `+`/`-` pair latches — while the
*implementation* is installed by whatever owns the verb and released when that goes away:

```cpp
// Declared once, at startup, from the controls.md inventory.
Registry.Declare({ TEXT("use"), EElysiumCmdKind::ButtonPair, EElysiumCmdGroup::Combat,
                   uint64(EElysiumButton::Use), TEXT("world interaction") });

// Every input source writes the same latch. The controller compares consecutive commands and
// queues the edge into the current entity world; no command callback chooses or uses an entity.
if (Cmd.JustPressed(EElysiumButton::Use, PreviousCmd))
    World->QueuePlayerUseEdge(EElysiumUseEdge::Pressed);
```

That split is what makes the registry usable before the systems exist: a declared verb with no
implementation logs the task that owns it, and `elysium.commands` is the coverage report. It also
puts the **button latch on the verb rather than on its implementation** — `+forward` fills the user
command with no handler in sight, and a headless world with no sink drops it.

Precedence in `FElysiumConsole::Execute`, stated once and tested: **registered command → alias
expansion → cvar set → Python fallthrough** — Source's own `Cmd_ExecuteString` order, so no user
alias can shadow a compiled verb. One registry means a level script, a `.dlg` action, a key, a
gamepad button, `-ExecCmds`, and an MCP tool all fire the same verb by name — which is exactly how
the original behaves, and it is also the whole automation story (§12).

### 8.3 Intent is data (S5)

A faithful `CGameMovement` port (4.7) needs Source's `CUserCmd`: a per-frame record of intent, not
live key state. Nothing reads a key directly — the gait was an `IsInputKeyDown(EKeys::LeftShift)`
poll on the pawn's tick, which is unbindable, untestable and unrecordable; it is now the `+speed`
bit of the command.

```cpp
struct FElysiumUserCmd
{
    FVector2D Move;          // -1..1, already deadzoned/modified per device
    float     Up;            // Source's upmove: +moveup / +movedown, and the noclip fly
    FVector2D LookDelta;     // degrees this frame
    uint64    Buttons;       // Attack | Jump | Duck | Speed | Use | ...
    float     DeltaSeconds;
    uint32    Seq;
};
```

`Buttons` is 64 bits because VtMB's ± inventory is 34 pairs, past Source's own set — its camera and
look-mode pairs are client-side state there rather than user-command bits, and here they are neither
special-cased nor dropped.

Filled by `UElysiumInputRouter` on the controller, consumed by `UElysiumMovementComponent`, the camera
and the command bus. Three things become free:

- **headless play** — inject a command stream with no input device (§12);
- **replay** — record `{FElysiumUserCmd, clock, RNG seed}` and re-run it; a movement or feel
  regression becomes a diff, which is what `docs/project/remaster-direction.md`'s "keep it A/B-able" needs to
  actually mean something;
- **the `+speed` gait** and every other VtMB button behave as buttons, bindable like the original's.

### 8.4 The player body must be a box

`docs/vtmb/source_movement.md` records this as a hard requirement, and it decides a class hierarchy:
Source's player hull is an AABB, and `StepMove` depends on it — a capsule's rounded bottom catches a
step's top edge and reports a normal of ~0.65 against the `0.7` standable test, so **every** step
climb is rejected.

`ACharacter` creates a `UCapsuleComponent` as its root and does not allow substitution. So the
faithful path is: **`AElysiumPawn : APawn`** with a `UBoxComponent` (32 × 32 × 72 Source units =
81.3 × 81.3 × 182.9 cm) and `UElysiumMovementComponent : UPawnMovementComponent` porting
`Friction`/`Accelerate`/`AirAccelerate`/`WalkMove`+`StepMove`/`CategorizePosition` line by line.
What is given up — `ACharacter`'s crouch, jump and root-motion plumbing, and `UCharacterMovementComponent`'s
own `StepUp` — is precisely the code being replaced. NPCs are unaffected: they need navmesh agents,
not Source step semantics, and can keep capsules. Open RE: the **ducked** hull's dimensions are
unrecorded (**RE22**) — `IN_DUCK` is in the user command with nothing sizing it.

`AElysiumPawn` is the only player body. What everything outside it talks to is
**`IElysiumPlayerBody`**: noclip, the embodied entity handle, the body half-height the teleport seam
lifts a Source feet-origin by, the spawn-hold freeze, `ApplyUserCmd`, and the camera's resolved draw
policy. The interface remains an interface rather than collapsing into the pawn because the bodies a
view can retarget to are not all pawns; nothing outside it names a concrete pawn class.

## 9. The camera

`docs/architecture/camera-architecture.md` owns the remaster integration. The spine supplies its
lifetime and dependency boundaries:

- `AElysiumPlayerCameraManager` is the one final-view authority per local player;
  `UElysiumCameraService` arbitrates handle-based base-view requests and post-layer policy.
- The pawn owns only the player camera rig and candidate first-/third-person poses. The camera service
  is reached through `IElysiumCameraService` beside the other injected world services; the entity
  substrate and embedded Python exchange values and entity handles, never camera actors or UObjects.
- Movement owns character facing and navigation, `UElysiumInputSubsystem` owns control scopes, and
  `IElysiumPresenter` carries the resolved HUD/reticle/letterbox state. A camera request describes
  policy but cannot mutate any of those owners directly.
- The recovered `UElysiumCameraComponent` evaluator and scripted-shot stack remain the compatibility
  adapter for original player behaviour, `SetCamera`, VCD camera actions, and
  `camera_keyframe`/`camera_track`. `docs/vtmb/camera-view-modes.md` owns their source semantics.
- Map-scoped request handles carry the map epoch and die before travel. Player mode and accessibility
  preferences live above the world. Dialogue and scripted sessions refuse saving while active, so
  dialogue cursors, body-owner tokens, camera handles, and transient scripted animation state are
  neither serialized nor republished after load.

## 10. Session, boot and the app state machine

`AElysiumGameMode::BeginPlay` is currently a decision tree over the command line, three cvars and the
map subsystem's pending state. It works and it is the wrong owner: a game mode is per-world, and boot
is an application concern that must survive travel.

```cpp
enum class EElysiumAppState : uint8
{
    Boot,        // process start; nothing loaded
    FrontEnd,    // static menu in the empty boot world, no run   (8.6, exists)
    Loading,     // travel in flight; loading screen up
    Playing,     // a session is running with a pawn
    Paused,      // Playing + time held + pause menu
    GameOver,    // death / Masquerade 5 -> load or quit
};
```

`UElysiumGameFlowSubsystem` owns the state, the transitions, and these entry points:

| Call | Does |
|---|---|
| `BootFromCommandLine()` | the whole current `BeginPlay` tree, once, at GI init |
| `NewGame(FElysiumNewGameRequest)` | clear the session record → chargen (or seed it) → travel the story entry |
| `LoadGame(SlotName)` | restore the session record → travel the saved map with a restore payload |
| `SaveGame(SlotName, EElysiumSaveKind)` | manual / quick / auto (`trigger_autosave` fires this) |
| `QuitToMenu()` | clear the session, travel the empty boot world, `FrontEnd` |
| `SetPaused(bool)` | `FElysiumTimeControl` + the pause input scope + the pause menu |
| `OnAppStateChanged` | the delegate the UI, the HUD and the input scope stack listen on |

`AElysiumGameMode` shrinks to: pawn/HUD/controller classes, `GetDefaultPawnClassForController` (null
in `FrontEnd`), and a single `Flow->NotifyWorldReady(this)` in `BeginPlay`.

### The New Game seam

Retail's New Game is a four-map chain — `sp_genesisdevice_1` (chargen) → `sp_theatre` (embrace +
trial) → `sp_tutorial_1` → `sm_pawnshop_1` (Santa Monica, the real start) — driven entirely by
entities and landmarks (`docs/vtmb/game_runtime.md` §4, `docs/vtmb/level_transitions.md`). An empty
player-facing request enters that chain at genesis and lets chargen replace its provisional player.

The design keeps the chain as **data, not code**:

```cpp
struct FElysiumNewGameRequest
{
    int32   Clan = 0;             // 2..8, the level-script encoding; 0 = ask (chargen)
    bool    bMale = true;
    int32   HistoryId = -1;       // m_iVHistoryID
    TMap<FName,int32> Spends;     // chargen's attribute/ability/discipline allocation
    FName   EntryPoint;           // "story" (the full chain) | "tutorial" | "<map>@<landmark>"
};
```

- `EntryPoint = story` travels `sp_genesisdevice_1`, whose `newplayer` trigger fires
  `ccmd.createplayer` (`docs/vtmb/game_runtime.md` §4; it rides the field-6 Python path via the `ccmd`
  object, `docs/vtmb/python_bridge.md`) — which becomes a **registered command** (§8.2) that raises the
  chargen screen and writes the result onto the player entity. That is VtMB's own wiring,
  unchanged; only the screen behind the verb is new.
- `EntryPoint = tutorial` is the dev shortcut that exists today, kept as `elysium.SkipIntro`.
- Developer entries construct one canonical request through `MakeMockCharacterRequest`: female
  Malkavian, the `Completely Batshit` History, and the untouched rulebook
  `Malkavian_CharGen` baseline. This reproduces the retail tutorial reference sheet and remains below
  Jack's authored modified-character thresholds. `elysium.newgame`, the theatre replay, direct
  dev-map boot and the MCP New Game tool differ only in destination or an explicit identity
  override; no entry writes a second ad hoc sheet.
- The theatre act needs `logic_choreographed_scene` + scene playback, owned by roadmap **P12** —
  the playable path's PP2, which it blocks **in full** (eyes and lipsync included; owner call). The chain is authored now; `elysium.SkipIntro` stays the
  dev shortcut until P12 lands, so the flow never has to be re-plumbed.

### Death and game over

`MakePlayerUnkillable`/`MakePlayerKillable` are already latched by `events_player` (4.9 — the
death signal on that bus is the still-undriven `OnPlayerKilled` output), and
`Masquerade == 5` is a loss condition (`docs/vtmb/game_runtime.md` §3). `GameOver` is a real app state with one
screen (Load / Quit), reached from the combat character's death path — not a special case bolted onto
the HUD later.

### Loading

`Loading` covers both halves of map admission. `IGameMoviePlayer::OnPrepareLoadingScreen` prepares a
pure-Slate MoviePlayer screen for the blocking `OpenLevel`/`LoadMap` portion; it auto-completes
normally. `PostLoadMapWithWorld` then installs the same visual in the unified player UI root's
runtime-loading layer while the map actor's
runtime activation barrier polls construction, final player placement/tick wiring, and required
asynchronous collision cooks. `NotifyWorldReady` only spawns the runtime actor and leaves the app in
`Loading`; the current actor's one-shot `MapReady` callback removes the screen and transitions to
`Playing`/`FrontEnd`. `MapFailed` remains gated and replaces the spinner with the structured missing
prerequisite. `docs/architecture/map-architecture.md` owns the full lifecycle; roadmap 10.4 remains the separate
time-slicing optimisation inside this correctness boundary.

## 11. The presentation seam *(built — 11.8)*

One publisher, one struct, one set of events (**S8**):

```cpp
struct FElysiumViewState                       // rebuilt each frame, TG_PostUpdateWork
{
    EElysiumAppState App;
    FElysiumInteractionView Interaction;       // focus/actionability/icon/lock/fade/semantic Use
    FLinearColor Fade;                         // rgb + alpha
    const FElysiumSignData*        Sign = nullptr;
    const FElysiumDlgConversation* Dialogue = nullptr;
    FElysiumVitals Vitals;                     // blood, health, frenzy, masquerade (8.9); stealth joins with 13.1
    FText          Subtitle;
};

DECLARE_MULTICAST_DELEGATE_OneParam(FOnElysiumDialogueOpened, const FElysiumDlgConversation&);
// ... Closed / SignOpened / FadeStarted / VitalsChanged / AppStateChanged
```

`UElysiumPresentationSubsystem` (world-scoped) builds it in **step 9** of the frame — a declared
`TG_PostUpdateWork` tick function with `bTickEvenWhenPaused` true, because a held world still has to
publish the *suppression*. `AElysiumHUD`, the CommonUI screens and the dialogue box read it and
nothing else; the HUD does not tick at all, reconciling its retained surfaces from `OnViewPublished`
(an actor tick is `TG_PrePhysics` and would always be a frame behind the publish). Pointer fields
(`Sign`, `Dialogue`) are **valid until the next publish and never stored** — a widget that needs the
data longer keeps a copy; the map epoch they point into ends at travel. Player *input* still goes the
other way through the command bus and the publisher's two routing calls, never by a widget calling
into the substrate.

**The gating is one rule**, `App == Playing && !bMenuOpen`, over *publishing* rather than drawing.
Both writers are asked, because `elysium.menu` raises a screen without moving the app state: a
conversation already on screen when the pause menu opens is republished as closed, the box comes
down, and closing the screen rebuilds it from the next publish. The conversation itself is
untouched in the entity world; only its UI is withheld.

Within an admitted player surface, cinematic camera ownership, HideHUD signs, dialogues, CommonUI
modal screens, and explicit interaction sessions suppress `Interaction`. The world still owns focus
and any captured session; the presentation projection simply exposes no actionable prompt. Prompt
transitions are world-clock values (0.10 s in, 0.15 s out), and a fading-out retained icon is marked
non-actionable. The HUD preserves the use-icon ring and resolves the semantic `Use` binding from
CommonInput, with `E`/`RB` text when a platform glyph is unavailable.

That rule owns the complete player-facing surface. Inside an admitted surface, `bCinematic` marks
the narrower heads-up layer suppressed while `FElysiumEntityWorld::HasTrackCamera()` or
`HasScriptedCamera()` owns the view. Reticle and meters come down, but fades, dialogue and future
cutscene subtitles keep publishing. Camera ownership, rather than choreographed-scene activity,
also means an ambient NPC scene cannot hide the player's HUD.

The struct and the three rules over it (`ShowsPlayerSurface`, `ResolveReticle`,
`ReconcileDialogue`) are plain C++ in `Public/ElysiumViewState.h`, like `ElysiumAppState.h` and
`ElysiumInputScope.h`, so the whole set is asserted with no world, no HUD and no viewport —
`Elysium.Substrate.ViewState`. `Subtitle` is deliberately **not** declared until 12.3 produces one.

## 12. The automation seam

`docs/architecture/debug-tooling.md`'s Layers 1–3 are built (Cog, `ent_*`, MCP). What is missing is the layer that
makes **a playthrough** an assertion instead of a demo (**S10**).

**A fourth test tier: `Play`.** Beside Substrate (`-nullrhi`, content-free) and Content (parses
`$ELYSIUM_EXPORT_ROOT`, self-skips), a tier that drives a real headless world:

```
uv run elysium test Play
```

A **beat script** is a list of steps over the surfaces that already exist:

```json
[ {"do": "elysium.newgame tremere m"},
  {"wait": "map == sp_tutorial_1", "timeout": 30},
  {"do": "input +forward 1.2"},
  {"wait": "G.Tut_Patch == 1"},
  {"assert": "entity(popup_1).open == true"},
  {"do": "dialog.choose 1"},
  {"assert": "G.Tut_Jack == 1"},
  {"shot": "porch"} ]
```

Every verb in it already has a home: `do` is the command registry (§8.2), `input` is
`UEnhancedInputLocalPlayerSubsystem::InjectInputForAction` over `FElysiumUserCmd`, `wait`/`assert` are
predicates over `G`, quests, entity fields and player pose — the exact surface the MCP tools expose,
`shot` is `ElysiumScreenshot::Request` against the 2.9 baseline. Nothing new is invented; the tier is
a driver over seams that exist.

What it gates:

- **P9's slice acceptance** ("`sp_tutorial_1` is completable as in retail") stops being a manual
  play-through and becomes a CI run.
- **Save/load** gets its real test: run a beat script to step N, save, load, re-run to the end,
  and assert the same beats fire (`docs/architecture/save-architecture.md` §10's Play-tier test; the byte-level
  digest compare is its Substrate-tier sibling — freeze, rebuild, apply, match).
- **Feel regressions** get a replay diff (§8.3).
- The **agent** driving the game through MCP and the **headless harness** run the same script, so a
  bug an agent finds is directly a regression test.

Add to the MCP surface (`docs/architecture/debug-tooling.md` Layer 3): `elysium_input_inject`, `elysium_beat_run`,
`elysium_save`/`elysium_load`, `elysium_time` (pause/scale/step). Each is a wrapper over a registered
command, so the console keeps parity by construction.

## 13. The rules

Numbered like `docs/architecture/engine-core.md`'s R1–R8, and orthogonal to them.

- **S1 — One clock.** `FElysiumGameClock` is advanced in exactly one place, once per frame. Pause and
  time scale are properties of the clock, mirrored onto the engine by one facade. No system keeps its
  own time base; `FTimerManager` is never used for game-visible time.
- **S2 — One frame order.** The order in §3 is declared with tick groups and tick prerequisites, never
  inferred from registration order. A new per-frame system names its slot in that table.
- **S3 — The player is an entity; the pawn is its body.** All player game state lives on the entity
  (live) and the session record (durable). The pawn holds collision, movement and camera, and nothing
  a save would need.
- **S4 — One home per value.** Every piece of state belongs to exactly one of the four lifetimes in
  §1. Application-scope objects never accumulate session state; map-scope objects never own it.
- **S5 — Intent is data.** Input produces an `FElysiumUserCmd`; movement, camera and commands consume
  it. Nothing polls a key, and any intent stream can be injected, recorded and replayed.
- **S6 — One input-mode arbiter.** Mode, cursor and mapping contexts come from the top of the input
  scope stack. No subsystem, widget or plugin sets `FInputMode*` directly.
- **S7 — One command registry.** Every player-facing verb has a name. Keys, aliases, level scripts,
  `.dlg` actions, the console, `-ExecCmds` and agents all reach it through that name, with one stated
  precedence order.
- **S8 — UI reads a published view state.** No widget touches the substrate; no substrate code touches
  a widget. Player input travels back through the command bus, not through a widget callback.
- **S9 — Persistence is a walk, not a list.** Anything that must survive is a save-flagged field on a
  registered class, a member of the session record, or a declared block. Adding state to a system
  without choosing one of the three is the bug.
- **S10 — Every capability has a headless driver.** `docs/architecture/debug-tooling.md`'s F1-first rule says a feature
  needs a Cog control; this extends it — a feature also needs a named command, so a script, a test and
  an agent can reach it. A capability reachable only by hand is incomplete.
- **S11 — A service answers; the substrate decides.** Every call across `FElysiumWorldServices` is an
  execution (perform a decision already made) or a query (answer what only the live world knows). A
  query returns geometry or a candidate and never a verdict, states its headless answer, and names
  any divergence from the oracle VtMB used. An authored value is arbitrated in the substrate, never
  by an engine subsystem; a query approximated with substrate arithmetic is a missing seam, not a
  rule (§7).
- **S12 — Unreal owns the engine; the substrate owns the game.** A system is reproduced in this
  runtime only when authored content or a game rule names it — the Ownership test in
  `docs/project/remaster-direction.md`. Everything the world merely needs in order to work —
  traces, visibility, pathfinding, physics solving, skinning, mixing — is Unreal's, reached
  through a query (S11). Reproducing Source's rules (formulas, call order, thresholds) is
  faithful; porting Source's mechanisms is a defect. The deliberate reproductions are the closed
  register in `docs/project/rebuild-strategy.md`; a port outside it is a bug, not a tolerance.

## 14. Tracking

Task status and unresolved decision work live only in `docs/project/roadmap.md` P11 onward. Once resolved,
the durable architecture decision is stated in this document.

## 15. Not covered here

Combat/perception AI beyond the native route motor, barter, stealth, disciplines, firearms, and choreography remain separate
systems tracked in `docs/project/roadmap.md`. Each must join the class chain, frame, command registry, and save
walk defined here without creating another dispatcher, clock, or input owner.
