# Runtime architecture — the game spine

**Status: adopted** (`decisions.md` 2026-07-26 cont. 4 — the seven §16 calls, with amendments).
Roadmap **P11** tracks the build; the playable path (`roadmap.md` → "The playable path")
sequences it as **PP0**.

The load-bearing structure *between* the systems this project has already designed: what owns what,
at which lifetime, in what order per frame, and through which seams the pieces talk. `engine-core.md`
defines the entity substrate, `map-architecture.md` the map lifecycle, `input-architecture.md` the
input path, `ui-architecture.md` the UI stack, `debug-tooling.md` the debug layers — each is sound in
isolation, and none of them owns the spine that turns "a map builds and a pawn walks" into "a session
boots, a game is played, and it saves". This doc owns that spine.

| Owns | Doc |
|---|---|
| the entity object language (R1–R8), class registry, queue | `engine-core.md` |
| map load/unload/travel | `map-architecture.md` |
| Enhanced Input planes, remapping, gamepad | `input-architecture.md` |
| the CommonUI/Slate stack, tokens, the virtual canvas | `ui-architecture.md` |
| debug layers 0–3, the MCP surface | `debug-tooling.md` |
| the first↔third-person camera solve | `camera-view-modes.md` |
| **persistence** | `save-architecture.md` |
| VtMB's own loop, state model, RPG data, opening flow | `game_runtime.md` |
| **lifetimes, the object graph, the frame, the player object, the session, the seams** | **this doc** |

Nothing here re-derives a VtMB fact; every one is cited to the doc that owns it.

---

## 1. The four lifetimes

Every piece of state has exactly one home, and the home is a lifetime, not a class (**S4**).

| Lifetime | Ends when | Owner | Holds |
|---|---|---|---|
| **Application** | the process exits | `UElysiumGameInstance` + its GI subsystems | settings, the key profile, the input scope stack, the CPython VM, the audio decode registry, the NPC animation banks, the UI screens, the map/flow subsystems |
| **Session** | New Game, Load Game, or Quit to menu | `FElysiumSessionRecord` on `UElysiumGameStateSubsystem` | `G` + `G.morgue`, the quest map, the player record (sheet, inventory, counters), the game clock, the owned RNG streams (`save-architecture.md` §8), per-map frozen snapshots, elapsed play time |
| **Map epoch** | `OpenLevel` tears the world down | `AElysiumMapActor` → `FElysiumEntityWorld` | entities, bodies, the event queue, the light rig, sound schemes, the texture cache |
| **Frame** | next tick | nobody — recomputed | the user command, the view state, the look cursor, camera weights |

Two rules fall out and are worth stating as prohibitions:

- **A map-epoch object never holds session state.** The player's blood pool does not live on
  `FElysiumPlayer` alone; the entity is *hydrated from* the session record at map build and
  *dehydrated back* at travel/save (§5). This is VtMB's own `.HL3` mechanism —
  "these entities travelled out with the player" (`savegame_format.md`).
- **An application-lifetime object never holds session state.** `UElysiumAudioSubsystem`,
  `UElysiumNpcAnimSubsystem` and the CPython VM survive New Game; nothing about a run may accumulate
  in them. Quit to menu clears the session record and nothing else.

The session record is also the **entire** save payload's mutable half (`save-architecture.md`), which
is what makes save/load mechanical rather than an archaeology exercise.

## 2. The object graph

```
UElysiumGameInstance                                   ── application
 ├─ UElysiumGameFlowSubsystem      app state machine, New Game / Load / Quit, loading screen
 ├─ UElysiumMapSubsystem           Travel / OpenLevel / landmark placement          (exists)
 ├─ UElysiumGameStateSubsystem     FElysiumSessionRecord + script host + debug logs (exists)
 ├─ UElysiumSaveSubsystem          slots, autosave ring, serialize/deserialize      (new, 9.5)
 ├─ UElysiumUISubsystem            screens                                          (exists)
 ├─ UElysiumAudioSubsystem         voice pool + decode cache                        (exists)
 ├─ UElysiumNpcAnimSubsystem       banks + clip vocabularies + disposition table    (exists)
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

VtMB's frame is `Input → Server(GameFrame) → Client → Sound → Render` (`game_runtime.md` §1), and
inside `GameFrame` it is **think-first**: `Physics_RunThinkFunctions` then
`CEventQueue::ServiceEvents` — **RE2**'s finding (`roadmap-archive.md`, mirrored in
`engine-core.md`). That is the order to reproduce; Unreal's tick groups are how it is *pinned*
rather than left to actor registration order (**S2**).

| # | Stage | Where | Notes |
|---|---|---|---|
| 1 | sample input | `APlayerController::PlayerTick` (`TG_PrePhysics`) | key bindings → command bus → latches; then `UElysiumInputRouter::SampleFrame` builds the frame's `FElysiumUserCmd` (§8) |
| 2 | advance the clock | `AElysiumMapActor` gameplay tick, first statement | **the only place `Now` moves** (§4) |
| 3 | run due thinks | `FElysiumEntityWorld::RunThinks(Now)` | movers issue their swept kinematic moves here |
| 4 | service the queue | `FElysiumEntityWorld::ServiceEvents(Now)` | delayed I/O, field-6 Python, `ScheduleTask` |
| 5 | move the pawn | `UElysiumMovementComponent::TickComponent` (`TG_PrePhysics`, prereq on the controller) | consumes the frame's `FElysiumUserCmd` |
| 6 | physics + overlaps | engine (`TG_DuringPhysics`) | Chaos overlap callbacks → `RouteBrushTouch` |
| 7 | post-move gameplay | `AElysiumMapActor` **second tick function** (`TG_PostPhysics`) | `+use` look-cursor trace, attachment follow-up |
| 8 | camera | `APawn::CalcCamera` via `APlayerCameraManager` | weight stack solved here (§9) |
| 9 | publish | `UElysiumPresentationSubsystem` (`TG_PostUpdateWork`) | builds `FElysiumViewState`, fires discrete events |
| 10 | audio | `UElysiumAudioSubsystem` | voice pool update, scheme scheduler |

Three mechanisms hold the order, all stock UE 5.8:

- `AElysiumMapActor` registers **two tick functions** — a `TG_PrePhysics` gameplay tick (2–4) and a
  `TG_PostPhysics` post-move tick (7). Two functions on one actor is the engine's own answer to
  "some of my work must straddle physics"; splitting into two actors would reintroduce the ordering
  question it solves.
- `AddTickPrerequisiteActor(PlayerController)` on the map actor, and on the movement component a
  prerequisite on the map actor's gameplay tick. Order is then declared, not observed.
- `bTickEvenWhenPaused = false` on both gameplay ticks; **true** on the presentation tick, so a paused
  world still draws a live HUD and a Cog window still updates (`debug-tooling.md`).

**Why step 5 sits after 3–4 rather than before.** A door's think computes its swept move for this
frame; the pawn must be moved against the door's *new* position or it tunnels on fast movers. The
argument stands on its own; whether VtMB itself runs movement after the think pass is *inferred,
unverified* — **RE21** (the usercmd stage of `GameFrame`) settles it.

## 4. Time, pause and time scale

`FElysiumGameClock` is the single game-visible clock (**R4**, **S1**), with **one advance site** and
**one facade over engine time** (11.1).

```cpp
struct FElysiumTimeControl                 // on UElysiumGameStateSubsystem, beside the clock
{
    double AdvanceFrame(double Delta);     // the ONE advance site (§3 step 2); Delta is already dilated
    void   EndFrame();                     // tail of a released frame (§3 step 7): spends one dev step

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

- **Pause is one call.** Today the menu does not pause anything; the substrate keeps running behind
  it (which the menu backdrop *wants* — implied by `decisions.md` 2026-07-26 cont. 3, decided as
  call E in cont. 4 — and the pause menu does not). The difference is a property of the app state (§10), not of the menu widget: `FrontEnd` runs
  the world unpaused as a backdrop, `Paused` holds it. Engine pause freezes actor ticks, physics and
  animation; the clock hold freezes thinks, the queue, movers and `ScheduleTask`. Both are needed —
  either alone leaves half the world moving.
- **Time scale is one call.** VtMB's third-person blend is explicitly scaled by the player's time
  scale (`camera-view-modes.md` §3), and disciplines schedule on the same queue as everything else, so
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
  inventory handles (`savegame_format.md` → "What the player entity holds").
- The **script surface** treats it as one: `FindPlayer()` is documented *"Find the first player
  entity"*, the player class carries **11 datamap inputs** (`GiveItem`, `AwardExperience`, `Whisper`,
  `SetCriminalLevel`, …) and `CBaseCombatCharacter` carries **25 more** (`MoneyAdd`, `HumanityAdd`,
  `Bloodloss`, `FrenzyTrigger`, …) that scripts reach through the ordinary `__getattr__` datamap walk
  (`script_api.md`).
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
themselves write (48 of the 49 `point_teleport.target` keys), so it resolves through the ordinary
name index rather than a magic-target branch.

`FElysiumPlayerRecord` (session lifetime) is the durable half:

```cpp
struct FElysiumPlayerRecord
{
    FElysiumSheet     Sheet;        // attributes / abilities / disciplines, base + current
    int32             Money = 0;
    int32             Humanity = 7, BloodPool = 10, Masquerade = 0;
    // health is NOT here — m_iHealth is a Save-flagged entity field on FElysiumCombatCharacter,
    // saved by the chain walk (save-architecture.md §4), which is VtMB's own placement
    int32             Health = 0, MaxHealth = 0;  // NOT a second home: m_iHealth is a Save-flagged
                                   // entity field on the chain (save-architecture.md §4, VtMB's own
                                   // placement). This copy exists only to carry the value across a
                                   // map boundary, because our entity dies with its map.
    TArray<FElysiumXpEntry>    ExperienceLog;  // EXPERIENCE_ENTRY — itemised, not a total
    TArray<FString>            Effects;        // m_tEffectList
    TArray<FString>            EmailFlags;     // the Player block, save-architecture.md §3
    FElysiumLawState  Law;         // criminal / supernatural / investigate counters
    bool              bUnkillable; // events_player's MakePlayerUnkillable, which must cross a warp
    // 9.8 adds the inventory and the equipped handles (items are entities; these are their frozen
    // form). `G` and the quest map are still the game-state subsystem's own stores until 11.9
    // gathers the save blocks.
};
```

`Hydrate(const FElysiumPlayerRecord&)` at map build, `Dehydrate()` when the world is torn down (and,
at 11.9, into the save). The entity is the *live* view; the record is the truth that crosses a map
boundary, and `UElysiumGameStateSubsystem::PlayerSheet()` resolves live-entity-first so a write can
never land on the copy that is about to be overwritten.

### What the pawn is

`AElysiumPawn` is the body and nothing else: collision, movement, camera, noclip, and the handle of
the entity it embodies. No `+use` routing, no sign dismissal, no `IsInputKeyDown` polling, no
`GI → MapSubsystem → MapActor → EntityWorld` walk — the non-movement verbs are named commands
(§8.2) and the gait is the `+speed` bit of the frame's user command.
`FElysiumPlayer::SetRuntimeOrigin` moves the body, exactly as `FElysiumNpc` moves a skeletal one, so
`point_teleport`, landmark placement and a scripted `pc.SetOrigin(...)` are one path. The reverse
direction is a sample: the world reads the pawn into the entity once a frame, before thinks and the
queue. `UElysiumMovementComponent` arrived with 11.6; `UElysiumCameraComponent` joins at 11.7.

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

`engine-core.md` R2 says one name table per class serves I/O dispatch, Python attribute access,
keyvalue application, save-field enumeration and the inspector. That only pays off if the chain
mirrors VtMB's, because VtMB's *data* is authored against that chain: a `.dlg` action calls
`npc.SetDisposition(...)` and a Hammer wire fires `MoneyAdd` on the same class.

Adding `FElysiumAnimating` and `FElysiumCombatCharacter` as real registry nodes:

- puts `PlayAnimClip` / `SetDispositionName` (currently virtuals on the base "for the same no-RTTI
  reason") where they belong, and lets `prop_dynamic` and NPCs share them without the base carrying
  character concerns;
- gives 9.9's disposition model, 9.10's economy, 9.4's counters and 9.8's inventory **one** place to
  land, shared by every character in the game rather than special-cased on the player;
- makes save-field enumeration a chain walk with no player branch (`save-architecture.md`).

Registration stays what it is: static per-class descriptors, case-folded chain lookup, unregistered
classnames as inert records.

## 7. Services — how the substrate reaches the engine

`FElysiumEntityWorld` reaches the engine through one injected service bundle and nothing else (11.2):

```cpp
struct FElysiumWorldServices
{
    IElysiumEmbodiment* Embodiment = nullptr;  // bodies, meshes, clips, skins, and the player's body
    IElysiumAudio*      Audio      = nullptr;  // PlayVoice/StopVoice/scheme control
    IElysiumTravel*     Travel     = nullptr;  // RequestLandmarkTravel, ChangeMap
    IElysiumPresenter*  Presenter  = nullptr;  // OpenDialog/OpenSign/StartFade -> the view state
};
```

`AElysiumMapActor` implements the first three and hands the bundle to `FElysiumEntityWorld` at
construction; **`IElysiumPresenter` has no production implementation until 11.8**, so it is null in
play and the world announces to it *in addition to* holding the fade/sign/dialogue state `AElysiumHUD`
still polls. The actor is still the world's component outer and VLOG context — that is not a fifth
service, and nothing under the world casts it to a map actor or walks it to a subsystem.

The player's body lives on `IElysiumEmbodiment` because the pawn *is* the player's body (**S3**):
view point, origin, teleport, damage, and the `+use` trace. Since 11.4 the substrate reaches them
*through the player entity* rather than directly — `point_teleport` writes `SetRuntimeOrigin` and
the entity places the body, `trigger_hurt` reduces the entity's health — so what is left on the
interface is the body's own geometry and the eye, which is 11.7's.

Any member may be null, and every call site has to handle "no body" (`elysium.NpcBodies 0`,
`elysium.BrushBodies 0`), so null-service is the existing A/B path formalised.

What this buys, concretely: a **Substrate-tier test can run a whole map's logic headlessly** against a
recording stub — the elevator chain, a dialogue tree, a save round-trip — with no RHI, no actors, and
no `tools/out`. That is the missing middle tier between "variant arithmetic" and "launch the game",
and `Elysium.Substrate.WorldServices` is its first occupant.

## 8. The control surface

`input-architecture.md` owns the Enhanced Input design — four planes, one `UInputAction` per bindable
command, `UPlayerMappableKeySettings` ids, contexts as client modes, the `config.cfg` projection. It
is complete and unbuilt (10.6). Three pieces of the *spine* around it are not designed anywhere, and
each is a live rough edge today.

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
    bool     bShowCursor;
    TArray<FName> Contexts;          // mapping contexts applied while this scope is top (10.6)
    TSharedPtr<SWidget> FocusWidget; // where keyboard focus goes under UIOnly
};

FElysiumInputScopeHandle UElysiumInputSubsystem::Push(FElysiumInputScope Scope);
bool                     UElysiumInputSubsystem::Pop(FElysiumInputScopeHandle&);
```

The **top scope decides everything** — mode, cursor, focus, which mapping contexts are applied.
Push/pop is **handle-based, not last-in-first-out**, because screens genuinely close out of order: a
conversation ends behind an open pause menu, and the menu has to find exactly the mode it pushed
over. Ids are never reused, so a stale or doubled pop is a no-op rather than a mismatched pop.
Priority decides, and push order is the tie-break, so two screens at the same priority behave like an
ordinary modal stack.

One table answers "what happens when X opens over Y":
`Game 0 < Sign 10 < Cinematic 20 < Chargen 30 < Dialogue 40 < Menu 50 < Debug 100`. Two rows carry
weight. **The sign scope claims game input, not UI-only** — VtMB's popups are dismissed by a
left-click, which is the `+attack` verb, so taking the mouse off the world would make them
undismissable; the scope is there for the ordering. **Debug is the top of the table**, because F1
over a screen is a developer asking for the debug UI and the front end has a menu up permanently.
What keeps it from eating that screen's clicks is the other half of the rule: **a UI-only push
revokes an inherited ImGui capture** (`ElysiumInput::RevokesDebugCapture`), at push time only, so the
deliberate F1 afterwards still works. Cog itself is a vendored plugin with no event to bind, so the
arbiter observes it — a per-frame reconcile pushes and pops the `Debug` scope off
`FCogImguiContext::GetEnableInput()`.

**Pause is not a scope property.** It has exactly one owner (`UElysiumGameFlowSubsystem`, §10), and
the pause menu's scope is pushed *because* the flow paused — a scope that also drove pause would be
a second writer, and a re-entrant one (`decisions.md` 2026-07-27).

The stack and its arbitration are plain C++, so the whole rule set is asserted with no local player,
no controller and no viewport — `Elysium.Substrate.InputScopes` walks every ordered pair of scopes in
both close orders. `FModifyContextOptions::bIgnoreAllPressedKeysUntilRelease` (default true) settles
held keys across a push, which is `controls.md`'s open "what does conversation do to held input"
question on our side; it lands with the contexts at 10.6.

CommonUI brings its own input writer — `UCommonUIActionRouterBase` applies an `FUIInputConfig` per
activated widget, a *fourth* mode owner living inside the engine. **The scope stack is the sole
authority** (call F, `decisions.md` 2026-07-26 cont. 4): Elysium's activatable widgets return no
desired input config (`GetDesiredInputConfig()` → unset) so the action router never writes mode or
cursor, and `UElysiumUISubsystem` pushes/pops scopes instead.

### 8.2 One command registry (S7)

VtMB has no action abstraction — **an action is a console command string**, and the patch's whole
vocabulary is *aliases* (`f` → `vm_feed` → `checkFeed()`), so a key bound to a compiled verb and a key
bound to a user alias must be indistinguishable (`controls.md`, `input-architecture.md`). That already
argues for routing bindings through `FElysiumConsole`. The missing half is the other end: the console
knows aliases, cvars and Python, but has **no registry of gameplay verbs** — `+use` is hard-bound to a
key on the pawn, and `togglecamera`, `holster`, `slotN`, `+feed`, `vdiscipline_*` have nowhere to
land.

**Declaration and implementation are separate.** The inventory is static data — one table of every
bindable verb with its kind, its group, and the user-command bit a `+`/`-` pair latches — while the
*implementation* is installed by whatever owns the verb and released when that goes away:

```cpp
// Declared once, at startup, from the controls.md inventory.
Registry.Declare({ TEXT("use"), EElysiumCmdKind::ButtonPair, EElysiumCmdGroup::Combat,
                   uint64(EElysiumButton::Use), TEXT("world interaction") });

// Implemented by whoever can answer it, for as long as it can.
Binding = Registry.Bind(TEXT("use"), [this](const FElysiumCommandCall&) { World->PlayerUse(); });
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
  regression becomes a diff, which is what `remaster-direction.md`'s "keep it A/B-able" needs to
  actually mean something;
- **the `+speed` gait** and every other VtMB button behave as buttons, bindable like the original's.

### 8.4 The player body must be a box

`source_movement.md` records this as a hard requirement, and it decides a class hierarchy:
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

The capsule pawn survives as `AElysiumCapsulePawn` behind `elysium.SourceMovement 0`, the A/B
baseline while 4.7 lands. Because the two cannot share a base — one is an `APawn`, the other an
`ACharacter` — what everything outside them talks to is **`IElysiumPlayerBody`**: noclip, the
embodied entity handle, the body half-height the teleport seam lifts a Source feet-origin by, the
spawn-hold freeze, and `ApplyUserCmd`. Nothing outside the two bodies names a concrete pawn class,
which is also what keeps the A/B honest: it compares the movers, not two input paths.

## 9. The camera

`camera-view-modes.md` has the full solve. Two spine-level facts:

- **One camera, one weight stack, one apply point.** `UElysiumCameraComponent` holds the four VtMB
  weights (third-person toggle, scripted, feed/death, secondary) and `AElysiumPawn::CalcCamera` is the
  single place the view is modified — delegating to `UCameraComponent::GetCameraView` first so
  post-process and first-person-rendering fields are filled, then applying offset and rotation.
- **The scripted channel is a service, not a special case.** `SetCamera(shotfile)` (115 script calls,
  keyed to `vdata/camerashots/`), `camera_keyframe`/`camera_track`, the conversation camera and the
  feed camera all push onto the same weight stack through one seam
  (`IElysiumPresenter::PushCameraShot`). That keeps `RestoreCameraToPlayerControl` a pop, and keeps
  cutscene cameras out of the pawn.

## 10. Session, boot and the app state machine

`AElysiumGameMode::BeginPlay` is currently a decision tree over the command line, three cvars and the
map subsystem's pending state. It works and it is the wrong owner: a game mode is per-world, and boot
is an application concern that must survive travel.

```cpp
enum class EElysiumAppState : uint8
{
    Boot,        // process start; nothing loaded
    FrontEnd,    // menu over a live backdrop map, no pawn        (8.6, exists)
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
| `QuitToMenu()` | clear the session, travel the backdrop map, `FrontEnd` |
| `SetPaused(bool)` | `FElysiumTimeControl` + the pause input scope + the pause menu |
| `OnAppStateChanged` | the delegate the UI, the HUD and the input scope stack listen on |

`AElysiumGameMode` shrinks to: pawn/HUD/controller classes, `GetDefaultPawnClassForController` (null
in `FrontEnd`), and a single `Flow->NotifyWorldReady(this)` in `BeginPlay`.

### The New Game seam

Retail's New Game is a four-map chain — `sp_genesisdevice_1` (chargen) → `sp_theatre` (embrace +
trial) → `sp_tutorial_1` → `sm_pawnshop_1` (Santa Monica, the real start) — driven entirely by
entities and landmarks (`game_runtime.md` §4, `level_transitions.md`). Today `NewGame()` seeds a
mock Tremere and jumps to the tutorial.

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
  `ccmd.createplayer` (`game_runtime.md` §4; it rides the field-6 Python path via the `ccmd`
  object, `python_bridge.md`) — which becomes a **registered command** (§8.2) that raises the
  chargen screen and writes the result onto the player entity. That is VtMB's own wiring,
  unchanged; only the screen behind the verb is new.
- `EntryPoint = tutorial` is the dev shortcut that exists today, kept as `elysium.SkipIntro`.
- The theatre act needs `logic_choreographed_scene` + scene playback, owned by roadmap **P12** —
  the playable path's PP2, which it blocks **in full** (eyes and lipsync included; owner call,
  `decisions.md` 2026-07-26 cont. 5). The chain is authored now; `elysium.SkipIntro` stays the
  dev shortcut until P12 lands, so the flow never has to be re-plumbed.

### Death and game over

`MakePlayerUnkillable`/`MakePlayerKillable` are already latched by `events_player` (4.9 — the
death signal on that bus is the still-undriven `OnPlayerKilled` output), and
`Masquerade == 5` is a loss condition (`game_runtime.md` §3). `GameOver` is a real app state with one
screen (Load / Quit), reached from the combat character's death path — not a special case bolted onto
the HUD later.

### Loading

`Loading` exists so the OpenLevel hitch has somewhere to hide: `FCoreUObjectDelegates::PreLoadMap` →
`GetMoviePlayer()->SetupLoadingScreen`, torn down on `PostLoadMapWithWorld`. That is the engine's own
mechanism and it works with hard travel, which is what makes 10.4's time-sliced build an optimisation
rather than a prerequisite.

## 11. The presentation seam

Today `AElysiumHUD` polls the entity world every frame for the reticle icon, the screen fade, the open
sign and the open dialogue, and gates all of it on `IsMenuUp()`. Each new screen adds another poll and
another gate, and none of it is testable without a world.

One publisher, one struct, one set of events (**S8**):

```cpp
struct FElysiumViewState                       // rebuilt each frame, TG_PostUpdateWork
{
    EElysiumAppState App;
    int32        ReticleIcon = 0;              // 0 = none; already resolves locked -> locked_icon
    FLinearColor Fade;                         // rgb + alpha
    const FElysiumSignData*        Sign = nullptr;
    const FElysiumDlgConversation* Dialogue = nullptr;
    FElysiumVitals Vitals;                     // blood, health, frenzy, masquerade (8.9); stealth joins with 13.1
    FText          Subtitle;
};

DECLARE_MULTICAST_DELEGATE_OneParam(FOnElysiumDialogueOpened, const FElysiumDlgConversation&);
// ... Closed / SignOpened / FadeStarted / VitalsChanged / AppStateChanged
```

`UElysiumPresentationSubsystem` (world-scoped) builds it; `AElysiumHUD`, the CommonUI screens and the
dialogue box read it and nothing else. Pointer fields (`Sign`, `Dialogue`) are **valid until the
next publish and never stored** — a widget that needs the data longer keeps a copy; the map epoch
they point into ends at travel. The menu-up gating becomes one rule in the publisher ("in
`FrontEnd`, publish no player-facing surface") instead of a check in every draw path. Player *input*
still goes the other way through the command bus, never by a widget calling into the substrate.

## 12. The automation seam

`debug-tooling.md`'s Layers 1–3 are built (Cog, `ent_*`, MCP). What is missing is the layer that
makes **a playthrough** an assertion instead of a demo (**S10**).

**A fourth test tier: `Play`.** Beside Substrate (`-nullrhi`, content-free) and Content (parses
`tools/out`, self-skips), a tier that drives a real headless world:

```
test.bat Play            # or: play.bat --script tools/beats/tutorial_open.json
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

What it gates, and why it is worth it:

- **P9's slice acceptance** ("`sp_tutorial_1` is completable as in retail") stops being a manual
  play-through and becomes a CI run.
- **Save/load** gets its real test: run a beat script to step N, save, load, re-run to the end,
  and assert the same beats fire (`save-architecture.md` §10's Play-tier test; the byte-level
  digest compare is its Substrate-tier sibling — freeze, rebuild, apply, match).
- **Feel regressions** get a replay diff (§8.3).
- The **agent** driving the game through MCP and the **headless harness** run the same script, so a
  bug an agent finds is directly a regression test.

Add to the MCP surface (`debug-tooling.md` Layer 3): `elysium_input_inject`, `elysium_beat_run`,
`elysium_save`/`elysium_load`, `elysium_time` (pause/scale/step). Each is a wrapper over a registered
command, so the console keeps parity by construction.

## 13. The rules

Numbered like `engine-core.md`'s R1–R8, and orthogonal to them.

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
- **S10 — Every capability has a headless driver.** `debug-tooling.md`'s F1-first rule says a feature
  needs a Cog control; this extends it — a feature also needs a named command, so a script, a test and
  an agent can reach it. A capability reachable only by hand is incomplete.

## 14. What is missing today

The honest register, for "launch it and actually play a new game". Every row is either an existing
roadmap task or a new P11 one.

| # | Gap | Consequence today | Owner |
|---|---|---|---|
| 1 | ~~player is not an entity~~ | closed by **11.4**: the chain, the record, the pawn demoted to a body. Sheet mutation, damage/death and `!activator` are real; inventory is 9.8's and the save 11.9's | **11.4** |
| 2 | legacy `DefaultInput.ini` bindings | nothing is rebindable, no gamepad, Esc does not pause, dev keys collide with player keys | 10.6 |
| 3 | movement is stock CMC on a capsule | not VtMB's feel; step semantics differ; no baseline to A/B against | 4.7 (+ **11.6** box pawn) |
| 4 | no chargen | New Game mocks Tremere male; clan-gated content untestable | 9.4 |
| 5 | no save/load | a session cannot be resumed; `trigger_autosave` is inert | 9.5 on **11.9** |
| 6 | no vitals HUD (the Canvas HUD covers reticle/signs only) | blood/health/frenzy/masquerade invisible; the sheet has no readout | 8.9 on **11.8** |
| 7 | ~~pause has no input path~~ | closed by **11.3** — Esc on the player controller drives `UElysiumGameFlowSubsystem::TogglePause`, which holds the world and raises the pause menu, and the menu's scope comes from **11.5**'s stack (pause stays the flow's, never a scope property) | **11.3** |
| 8 | no camera modes | `togglecamera` unbound and unimplemented; scripted cameras have no channel | **11.7** |
| 9 | ~~three input-mode owners~~ | closed by **11.5**: one arbiter owns mode, cursor and focus; CommonUI's router is declined explicitly, and a UI-only push revokes an inherited ImGui capture. Mapping contexts are declared on the scope and applied at 10.6 | **11.5** |
| 10 | ~~no loading screen~~ | closed by **11.3** for the level-load flush; the map actor's build pass after it is 10.4's | **11.3** |
| 11 | ~~no death / game-over path~~ | closed: **11.3** made `GameOver` a state, **11.4** gave it its driver — the player entity's health running out reaches `NotifyPlayerKilled` → `TriggerGameOver(Killed)`. The masquerade meter is the second loss condition, still 9.4's | **11.3** + **11.4** + 9.4 |
| 12 | dialogue line audio unwired | `PlayDialogFile` (41 calls) silent though decode is done | 9.2 |
| 13 | no `logic_choreographed_scene` | the theatre act cannot run, so the story chain is short-circuited | **12.1** (P12 = PP2) |
| 14 | no items/containers/barter | 853 script calls fail closed | 9.8 |
| 15 | ~~substrate reaches the engine by back-pointer~~ | closed by **11.2** — the seam is `FElysiumWorldServices`, and `Elysium.Substrate.WorldServices` drives a map's logic headlessly | **11.2** |
| 16 | UI polls the substrate | no view contract; every screen re-invents its gating | **11.8** |
| 17 | no playthrough harness | "the tutorial is completable" is a manual claim | **11.10** |

## 15. The refactor ladder

Ordered so each step compiles, ships, and is observable on its own — and so nothing later has to
re-do an earlier step. Roadmap IDs in **P11**. Step zero — **11.0 Adopt the spine**, the seven §16
calls as one dated `decisions.md` entry — was recorded 2026-07-26 (cont. 4) and gates the rest.

1. **11.1 Frame + clock ownership** *(landed)* — the §3 tick table pinned with tick groups and
   prerequisites; the map actor split into gameplay (`TG_PrePhysics`) and post-move
   (`TG_PostPhysics`) tick functions; `FElysiumTimeControl` as the one pause/scale facade.
   *Observable:* `Elysium.Substrate.FrameOrder` + `Elysium.Substrate.TimeControl`, and a
   `elysium.timescale 0.25` that slows movers, the queue and animation together (the camera blend
   joins them at 11.7, which is where a blend first exists).
2. **11.2 World services** *(landed)* — `FElysiumWorldServices` injected into `FElysiumEntityWorld`;
   the map actor implements three of the four interfaces (`IElysiumPresenter` waits for 11.8); a
   recording stub in the module's test folder. *Observable:* `Elysium.Substrate.WorldServices` runs a
   tutorial-shaped `logic_auto` chain end to end with no RHI, no actors and no `tools/out`, and the
   same defs with a null bundle reach the same state.
3. **11.3 App state machine** *(landed)* — `UElysiumGameFlowSubsystem`, `EElysiumAppState` with its
   transition table as plain C++, boot out of the game mode, the movie-player loading screen, pause,
   quit-to-menu, `GameOver`; the screen is a pure function of the state, so no call site closes a
   menu. *Observable:* `Elysium.Substrate.AppState`, and in the built game Esc pauses, the menu holds
   the world, quit-to-menu returns to the backdrop with the session cleared, and travel shows a
   loading screen.
4. **11.4 The player entity** *(landed)* — `FElysiumAnimating` + `FElysiumCombatCharacter` chain
   nodes, `FElysiumPlayer` under the targetname `!player`, `FElysiumPlayerRecord` with
   hydrate-at-map-build / dehydrate-at-teardown, the pawn demoted to a body, `FindPlayer()`/`pc`
   returning an `Entity`, `vampire.Player` retired, `FElysiumNpc` re-based onto the same nodes.
   *Observable:* `Elysium.Substrate.PlayerEntity`, and in the built game `pc.MoneyAdd(50)` and
   `ent_fire !player MoneyAdd 50` land on the same field, `point_teleport` moves the player through
   `SetRuntimeOrigin`, and a `trigger_hurt` that empties the player's health ends the run.
5. **11.5 Input scope stack** *(landed)* — `UElysiumInputSubsystem` over a plain-C++ priority stack;
   the menu, the dialogue box, sign panels and Cog push scopes, and nothing else calls
   `SetInputMode`. *Observable:* `Elysium.Substrate.InputScopes` walks every ordered pair of screens
   in both close orders; in the game `elysium.inputscopes` dumps the live stack, and a menu coming up
   takes the mouse back from an inherited ImGui capture.
6. **11.6 Command registry + user command** *(landed)* — `FElysiumCommands` (92 declared verbs),
   the stated console precedence, `FElysiumUserCmd` filled by `UElysiumInputRouter` off VtMB's own
   default bind table; the box pawn + `UElysiumMovementComponent`, with the capsule body behind
   `elysium.SourceMovement 0`. *Observable:* `elysium.cmd <verb>` fires every bindable verb from the
   console, a script, a `.dlg` action or MCP; `elysium.commands` reports the coverage; a recorded
   command stream replays identically. Feeds 10.6 and 4.7.
7. **11.7 Camera component** — the weight stack, `CalcCamera` as the apply point, `togglecamera` and
   the cvar surface, the scripted-shot channel. *Observable:* `camera-view-modes.md`'s weight-driver
   automation test passes; `SetCamera` has somewhere to land.
8. **11.8 Presentation seam** — `UElysiumPresentationSubsystem` + `FElysiumViewState`; the HUD and the
   dialogue box re-based onto it. *Observable:* no widget references `FElysiumEntityWorld`; the
   front-end gating is one rule.
9. **11.9 Save/load** — `save-architecture.md` in full. *Observable:* save mid-tutorial, quit to menu,
   load, and the beat machine continues.
10. **11.10 Play test tier** — the beat-script driver, replay, the save round-trip test, the MCP input
    and time tools. *Observable:* `test.bat Play` walks the tutorial opening unassisted and fails
    loudly when a beat regresses.

Steps 1–4 have landed. Step 4 was the hinge: 9.4, 9.8, 9.9, 9.10 and 9.5 all sit on it, and every
one of them built first would have had to be re-based.

## 16. Owner calls — decided

**All seven were adopted 2026-07-26** (`decisions.md` cont. 4 = roadmap 11.0), with the
amendments noted per row. The table keeps the rationale.

| # | Call | Recommendation |
|---|---|---|
| A | **Is the player an entity?** | **Yes.** It is VtMB's own architecture, the save format requires it, and ~36 of the script surface's unbacked names are datamap inputs on a class chain we would otherwise have to fake (35 enumerated — `script_api.md` lists 10 of the player's stated 11 inputs). Cost measured at 11 call sites plus retiring one Python type, and it only grows. |
| B | **Does the player pawn become a box on `APawn`?** | **Yes**, together with 4.7. `ACharacter` cannot take a box root, and the box is a *recovered requirement*, not a preference (`source_movement.md`). Keep the capsule pawn behind `elysium.SourceMovement 0` until the port is at parity. |
| C | **Does the sheet move off `UElysiumGameStateSubsystem`?** | **Yes**, to `FElysiumCombatCharacter` (live) + `FElysiumPlayerRecord` (durable). It is the same move as (A) and 9.4 should target the new home directly. |
| D | **Does New Game reproduce the four-map chain?** | **Author it now; the theatre is P12.** `EntryPoint = story` walks the real chain through `sp_genesisdevice_1`; the theatre act lands as **P12** (the playable path's PP2, which it blocks in full — cont. 5). Until then `elysium.SkipIntro` skips it: a recorded, reversible divergence, and the flow never has to be re-plumbed. |
| E | **Does pause use engine pause as well as the clock?** | **Both.** Either alone leaves half the world moving. `FrontEnd` deliberately does not pause (the backdrop is the feature); `Paused` does. |
| F | **Does UI get a view-state seam, or keep polling?** | **Seam.** Three screens land on it in the next phase (HUD, dialogue, chargen), and the front-end gating is already scattered. |
| G | **Does the game mode keep the boot decision?** | **No** — it moves to `UElysiumGameFlowSubsystem`. A per-world object cannot own an application-lifetime decision that must survive travel. |

## 17. Not covered here

Full combat AI, navigation, and the vendor/barter loop remain `roadmap.md` 10.7; stealth,
disciplines and firearms basics are **P13**, choreography is **P12**. Each is
a system with its own doc when it is reached; this doc's contract is that each one
lands on the chain in §6, the frame in §3, the command registry in §8.2 and the save walk in §9's
`save-architecture.md` — with no new dispatch mechanism, no new clock, and no new input owner.
