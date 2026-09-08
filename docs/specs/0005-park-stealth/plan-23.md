# 23 — The NPC debugger

Planning baseline: `aa71776d`, 2026-09-08. The working tree was already dirty; this document is
the only planned artifact for requirement 23. No runtime code, build, test, or live acceptance was
performed.

## Outcome

Development and test builds expose one selectable Gameplay Debugger category named `ElysiumNPC`.
Selecting an `AElysiumNpcBody` shows a read-only projection of the authoritative `FElysiumNpc`:
admission/mind state, body ownership, transition trace, current schedule and task, gathered
conditions, enemy-memory records, resolved sense tuning, committed-enemy LOS, and the currently
held interesting place. The world view draws the exact VtMB vision radius and cone, the current
hearing radius when a sound record exists, the committed-enemy LOS line, and the held place.

The same body actor implements `IVisualLoggerDebugSnapshotInterface`. Its snapshot is a status view
of the same substrate state. Gameplay events add `UE_VLOG` geometry at sense admission, enemy
choice, and schedule installation. The debugger only observes state; it never runs a trace,
selects an enemy, starts a schedule, changes a condition, claims a body, or mutates the entity
world.

The feature is absent from Shipping. It is a visual-only modernization, so it is not a new AI
producer and does not change retail event order or persistence.

## Evidence that fixes the design

The current substrate already owns the requested facts:

- `FElysiumNpc::GetMind()` exposes admission, current/ideal state, owner, suspended owner,
  generation, last transition, and the bounded transition trace.
- `Cognition`, `Schedule`, `ScheduleHost`, `Senses`, and `EnemyMemory.Records()` expose the
  gathered conditions, program/task state, perception/memory, and CAI-memory admission list.
- `FElysiumNpc::GetDebugState()` and `ElysiumCogWindow_Npc.cpp` already prove the intended
  read-only field vocabulary. The new category must use the same fields, not create another AI
  state model.
- UE 5.8's `FGameplayDebuggerCategory` contract makes `CollectData` authority-side and `DrawData`
  local. `SetDataPackReplication<T>` carries typed state; `AddShape`/`AddTextLine` are replicated
  convenience data.
- UE's `FVisualLoggerDebugSnapshotInterface` is invoked for the owning UObject, while
  `FVisualLogStatusCategory` and the `UE_VLOG_*` shape macros are the native snapshot/event seams.
- `FGameplayDebuggerShape::MakeCone` is fixed at roughly 15 degrees in UE 5.8. It cannot represent
  the recovered VtMB observer cone, so the exact cone parameters must be replicated and drawn with
  `DrawDebugCone` in `DrawData`.

## Implementation sequence

### 1. Enable the feature only in non-Shipping targets

- In `Source/ElysiumUE.Target.cs` and `Source/ElysiumUEEditor.Target.cs`, set
  `bUseGameplayDebugger = Target.Configuration != UnrealTargetConfiguration.Shipping;`.
- In `Source/ElysiumUE/ElysiumUE.Build.cs`, call `SetupGameplayDebuggerSupport(Target)` and keep
  `GameplayDebugger` private. Do not manually add `GameplayDebuggerEditor`.
- Guard category code with `#if !UE_BUILD_SHIPPING && WITH_GAMEPLAY_DEBUGGER`; guard event logging
  with `#if !UE_BUILD_SHIPPING && ENABLE_VISUAL_LOG`.
- Verify the Shipping target neither registers the category nor retains Gameplay Debugger menu
  code.

### 2. Add a typed, read-only NPC projection

Add `Private/Debug/ElysiumNpcDebugData.{h,cpp}`. It is plain C++ and contains no UObject pointers.
Build it from `FElysiumNpc` plus its `FElysiumEntityWorld`, resolving handles only for labels and
draw positions.

The projection carries:

| Group | Data |
|---|---|
| Identity | handle/index, targetname, classname, model, epoch, body-present |
| Mind | admission, current/ideal state, owner, suspended owner, generation, last transition, 16 trace rows |
| Schedule | name/retail id, task index/count/name/operand, fail route, tolerance, started flag |
| Conditions | gathered names, gather time, authored interrupt names, gathered∩interrupt names |
| Senses | eye/origin, forward, vision radius, exact half-angle, hearing scalar, current sound radius/source/position |
| Enemy memory | committed enemy, LOS/occlusion/failure count, last enemy, bounded CAI-memory records |
| Place | held place handle/name/origin/type/rating/group, ambient phase, or “none” |

Use stable handles and names, fixed deterministic caps for records/tasks/trace rows, and explicit
stale/no-body reasons. Add only small const accessors required by this read path, notably
`GetCurrentAmbientSpotForDebug()` and an ambient-phase value; do not expose mutable internals.

The projection must not call `IsVisible`, `ShouldInvestigate`, `BestEnemy`, schedule selection,
motor commands, or any behavior function. It must not write to the NPC, event queue, map, or body.

### 3. Implement `FGameplayDebuggerCategory` `ElysiumNPC`

Add `Private/Debug/ElysiumNpcGameplayDebugger.{h,cpp}` with one `FRepData` pack registered as
`ResetOnActorChange`.

`CollectData` resolves only an `AElysiumNpcBody` → `GetOwningEntity()` → current map's entity world
→ `AsNpc()`. It fills the pack from the typed projection and returns diagnostic empty data for a
stale body, epoch, entity, or NPC. It never mutates gameplay.

`DrawData` consumes only the pack:

- `DrawDebugCircle` for the horizontal vision radius;
- `DrawDebugCone` with the replicated forward/radius/half-angle, preserving the recovered 157°
  total observer cone;
- the current listener-effective sound radius, when a selected/last sound provides one, plus the
  hearing scalar and an explicit “no current sound” state otherwise;
- a red solid or amber memory/occlusion enemy line;
- a point/box and label for the held interesting place;
- compact text for identity, state/owner, schedule/task, condition hits, memory count, and trace.

The local drawing pass must not query gameplay again. Missing enemy/place/sound data draws nothing
for that channel instead of inventing a fallback.

### 4. Register the category from the game module

In `Private/ElysiumUE.cpp`, under `#if WITH_GAMEPLAY_DEBUGGER`, include the category and:

- `StartupModule`: `RegisterCategory("ElysiumNPC", ...,
  EGameplayDebuggerCategoryState::EnabledInGameAndSimulate, INDEX_NONE)`;
- call `NotifyCategoriesChanged()`;
- `ShutdownModule`: guard availability, unregister the category, and notify again.

Do not change AIModule's default categories or make this view depend on Cog/ImGui. The Cog NPC
window remains the deeper inspector and Cast harness.

### 5. Add body Visual Logger snapshots and event instrumentation

Extend `AElysiumNpcBody` to inherit `IVisualLoggerDebugSnapshotInterface`. In
`GrabDebugSnapshot` (under `ENABLE_VISUAL_LOG`), resolve `OwningMap`/`OwningEntity`, set the body
location, and add one `FVisualLogStatusCategory("Elysium NPC")` using the same projection. A stale
owner produces a clear status row and returns.

Add a small `ElysiumNpcDebugLogging` helper that derives the body UObject from the NPC's skeletal
component attach parent and no-ops when there is no body:

- after the authoritative `EnemyMemory.Update` sight admission, log observer/candidate geometry,
  radius, cone, and admission state with `UE_VLOG_CIRCLE`, `UE_VLOG_CONE`, and `UE_VLOG_SEGMENT`;
- after `ElysiumNpcEnemy::SetEnemy` changes the committed handle, log old/new handles and an
  arrow to the selected enemy with `UE_VLOG_ARROW`;
- after `ElysiumSchedule::Start` installs a program, call a typed optional debug callback on
  `IElysiumScheduleRunner` (default no-op; implemented by `FElysiumNpc`) and log the installed
  schedule at the NPC body.

Each hook runs after the authoritative write. Rejected candidates must not be logged as admissions.

### 6. Keep Cog and Gameplay Debugger on the same authority

Compare the new projection against `ElysiumCogWindow_Npc.cpp`'s Mind, Senses, Conditions,
Schedule, and world-overlay code. Extract pure formatters only after the category works; do not
make the Gameplay Debugger depend on Cog modules. Neither surface may write NPC state outside the
existing Cast tab, and the new category must not call Cast writers.

## Verification plan

### Automated/static gates

1. Build Development and Editor targets with Gameplay Debugger enabled.
2. Build Shipping and verify macro-elision/no registration/no debugger dependency in the game
   module.
3. Add focused tests for projection and pack serialization: populated state round-trip, stale body
   and epoch, deterministic caps, exact cone/radius data, and a before/after assertion that mind
   trace, schedule, conditions, memory, owner tokens, and entity queue are unchanged.
4. Run the focused NPC/substrate filters after source freeze. Headless tests prove the projection and
   serialization contract, not rendered geometry.

### Live witness

On `sp_tutorial_1` with real body actors:

- activate Gameplay Debugger with the apostrophe key, select `thug_1`, and enable `ElysiumNPC`;
- before detection, verify radius/cone, no enemy line, held `pt1`, and the initial trace;
- make a real footsteps stimulus and verify the delayed condition → ladder/task changes;
- enter the authored light/cone/LOS admission and verify memory admission precedes enemy choice,
  then the solid LOS line and schedule-install trace;
- occlude the committed enemy and verify the amber memory line and failure counter;
- record Visual Logger data and scrub the body actor row for sense admission, enemy choice, and
  schedule install;
- travel/reload and destroy/reset an NPC while the debugger is open; no stale pointer or old-epoch
  data may remain.

Check the overlay at 1920x1080, 2560x1440, and 3840x2160. Do not mark #23 complete from `-nullrhi`
or a headless pack test.

## Completion boundary

Requirement 23 is complete when the non-Shipping category selects a live NPC body, shows the
authoritative read-only state and exact world diagnostics, the body snapshot and three event hooks
are recorded, Shipping excludes the feature, focused tests pass, and a played tutorial witness
confirms sense → memory → enemy → schedule without debugger-side mutation.

It does not include new AI behavior, schedule fixes, new perception queries, an NPC control panel,
an all-NPC always-on overlay, a replacement for Cog, or a save-format change.

## References

Repository: `spec.md` requirement 23; `docs/vtmb/npc-ai-reverse-engineering.md` (AI loop,
perception/memory, schedules/tasks, observability); `docs/vtmb/stealth.md` (visual observer and
“HUD observability is not authority”); `ElysiumCogWindow_Npc.cpp`; `ElysiumNpc*.{h,cpp}`;
`ElysiumSchedule.{h,cpp}`.

UE 5.8 source: `D:\Epic\UE_5.8\Engine\Source\Runtime\GameplayDebugger\Public\GameplayDebuggerCategory.h`,
`GameplayDebugger.h`, `AIModule\Private\AIModule.cpp`,
`AIModule\Private\GameplayDebugger\GameplayDebuggerCategory_AI.cpp`,
`Engine\Public\VisualLogger\VisualLoggerDebugSnapshotInterface.h`, `VisualLogger.h`, and
`VisualLoggerTypes.h`.

Official references: [FGameplayDebuggerCategory](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/GameplayDebugger/FGameplayDebuggerCategory),
[Gameplay Debugger category setup](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/GameplayDebugger/EGameplayDebuggerCategoryState),
[Using the Gameplay Debugger](https://dev.epicgames.com/documentation/unreal-engine/using-the-gameplay-debugger-in-unreal-engine?lang=en-US),
[AI Debugging](https://dev.epicgames.com/documentation/en-us/unreal-engine/ai-debugging-in-unreal-engine), and
[Visual Logger](https://dev.epicgames.com/documentation/en-us/unreal-engine/visual-logger-in-unreal-engine).
