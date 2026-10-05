# Brief V6-1 — map clock, world boundary and save core

Read main AGENTS.md, V6 README and packets §§1–3/5/8/9. Start first with V6-3, before V5b
is committed. This lane shares no file with V5b; V6-2 alone waits for its NPC/weapon hunks.
The exhaustive twenty-file lane list is README § V6-1; it is reproduced here as grouped paths
(braces expand to separate files, all under Source/ElysiumUE):

```text
Public/ElysiumGameClock.h
Public/ElysiumTimeControl.h
Public/ElysiumSessionSubsystem.h
Public/ElysiumSaveTypes.h
Public/ElysiumSaveArchive.h
Public/ElysiumClassRegistry.h
Public/ElysiumEntityWorld.h
Public/ElysiumEntity.h
Public/ElysiumMapSubsystem.h
Private/Session/ElysiumSessionSubsystem.cpp
Private/Session/ElysiumSessionSave.cpp
Private/Session/ElysiumSaveArchive.cpp
Private/Session/ElysiumTimeControl.cpp
Private/Substrate/ElysiumClassRegistry.cpp
Private/Substrate/ElysiumEntityWorld.cpp
Private/Substrate/ElysiumEntityWorldPersistence.cpp
Private/Substrate/ElysiumEntity.cpp
Private/Map/ElysiumMapSubsystem.cpp
Private/Map/ElysiumMapActorLifecycle.cpp
Private/Tests/ElysiumV6ClockPersistenceTests.cpp
```

## Numbered jobs

Early-start assumption: entity Serialize/OnPostRestore and registered accessor metadata are
existing opaque interfaces. Define the base/type/policy/identity/fence contract in your own files;
later V6-2 plugs in TIME annotations, saved leaf state and override behavior. No new NPC member,
WeaponClasses edit or V6-2 helper is required to code this lane. V6-3 consumes operation ids,
capture/write/apply/ready results and common transport; report exact public signatures early.

1. **Map time selection** — engine0x200f5bb4..c4,0x200975f0, server0x1011a7a0;
   `ElysiumGameClock.h::FElysiumGameClock::Reset`, `ElysiumTimeControl.{h,cpp}::ResetClock`,
   `ElysiumSessionSubsystem.cpp::BeginNewGame`, `ElysiumMapActorLifecycle.cpp::LoadMap/RebuildStageWorld`.
   Keep one advancing clock facade but select destination-local1.0 or snapshot FrozenAt BEFORE
   Load/Spawn/any init; load selects saved current-map time. Freeze departing map before selection.
   GI ownership does not imply a monotonically increasing clock. Do not reset already resumed
   maps at ActivateRuntime. Preserve scale/pause sequencing and session playtime's separate role.

2. **Fresh load versus travel/load** — engine0x2008f120/0x200f55f0 and0x20096010;
   `ElysiumMapSubsystem.{h,cpp}::Travel/Reload`, `ElysiumEntityWorld.cpp::Teardown`,
   `ElysiumSessionSave.cpp::ApplyPayload/Load`. Add explicit FreshLoad(Map) operation/alias
   (`elysium.map_load <map>`), documenting spec's map_load vocabulary; normal elysium.map Travel
   still restores visits. Fresh load detaches/discards outgoing entity world first and clears
   target snapshot so EndPlay cannot freeze it back into the new epoch. Old epoch/queues/handles
   must disappear; authored entities rebuild once. Reload should use this fresh operation.
   Explicit load's detachment must likewise prevent outgoing teardown replacing loaded data.
   Check the existing ConsumeFreshMapState door; consolidate its ordering rather than add two
   competing clears. No BeginNewGame merely to travel, no source corpus reads at runtime.

3. **Type-aware delta encoding** —0x101a0a80/0x101a2a30 and engine0x20097d00;
   `ElysiumSaveArchive.h::FElysiumSaveArchive`, `ElysiumClassRegistry.h::FElysiumFieldAccessor`,
   `ElysiumClassRegistry.cpp::SaveFields`, `ElysiumEntityWorldPersistence.cpp::CaptureState/
   Freeze/ApplySnapshot/ApplyEntityRecord`, `ElysiumSaveArchive.cpp::operator<<` for entity/map/queue.
   Carry explicit section SaveBase and RestoreBase. Introduce independent persistence-type/policy
   metadata on accessors; lane2 annotates NPC bindings. TIME writes relative, restores onto selected
   base; FLOAT remains raw even if its name says NextAttack/LastEventCheck. Baseline omission
   comparisons must compare in the same representation and not erase a required timestamp because
   defaults were captured at another epoch. Queue delays must survive travel/load and fire once.
   Public Entity state contains NextThink, saved think callback and callback identity as needed;
   do not serialize pointers. Preserve exact zero/-1/FLT_MAX exceptional representations using
  0x100a9f70/0x100aa140 and0x101cf250/0x101cf2f0, not a blanket “all zero means never” policy.
   Expose a policy-aware leaf TIME operation to lane2; one writer per field. Bump schema and
   MinSupported once in SaveTypes; no old-save migration/compatibility work.

4. **Restore phase ordering** —0x1011a710/0x1011a620,0x1027bf50/0x102998c0;
   `ElysiumEntityWorldPersistence.cpp::ApplySnapshot/ApplyEntityRecord`,
   `ElysiumMapActorLifecycle.cpp::LoadMap/ActivateRuntime`.
   Instantiate all authored/runtime entities; decode every accessor/leaf word; rebase every saved
   reference; run post-restore in reverse restored-list order; reconnect presentation; activate only
   genuinely new rows; publish ready. No NPC think/I/O between these phases. Split per-record hook
   dispatch out of ApplyEntityRecord. Do not reset NextThink after a legitimate OnRestore write
   (hidden NULL/FLT_MAX, subtype or shoot-at rearm). Missing target handles become invalid before
   prerequisite validation. Preserve V4c team re-registration and coordinator teardown; no guessed
   saved attack-list reconstruction. Report exact public phase hooks to lanes2/3.

5. **Base hidden transaction** —0x100a8710/0x100a8990;
   `ElysiumEntity.cpp::Construct/ScriptHide/ScriptUnhide`, `ElysiumEntity.h::saved physical/think state`,
   `ElysiumEntityWorld.cpp::Activate/ActivateListedEntity`.
   Save callback identity, not only its due time; hide NULLs it and parks. Unhide restores callback
   but due-time=NOW; restore is not unhide. Save/restore solid/move/collide/solid-flags/effects words
   through real readers/writers where represented; missing primitive input gets named no-input seam,
   not a guessed flag. A StartHidden NPC is admitted/activated (not EFL_DORMANT) and its final
   callback remains parked after NPCInit; coordinate the NPC half with lane2. Preserve V4d Kill's
   visual release. Never teleport to floor in ScriptUnhide: StartNPC/motor owns ground handling.

6. **Direct load and completion contract** — engine0x20096010/0x200975f0;
   `ElysiumSessionSave.cpp::RegisterSaveCommands/RequestSave/Load/BuildPayload/ApplyPayload`,
   `ElysiumSessionSubsystem.h::save operation/result API`.
   Register elysium.load <slot> through the existing load backend, report failures with reason.
   Existing elysium.cmd load remains valid. Reuse live PublishSaveResult/OnSaveResult/LastSaveResult
   (Capturing/Writing/Written/Failed and operation id already exist); do not add a second save-result
   state machine. Expose the missing exact capture and load decoded/applied/ready fences and
   their result contract for lane3; no success notification on enqueue alone. Saved
   snapshot comparison must sample EXACTLY at capture and applied-before-think fences. Publish
   common capture/apply services suitable for lane3's nonshipping Green Room provenance route;
   production map Load keeps baked-map and session gates. No bypassed CanSave disguised as a test.
   Lane2 removes K1 only when its supporting restore is ready; leave unrelated modal gates alone.

7. **Meaningful arm evidence** —0x200f5bc4,0x101a0a80/0x101a2a30,0x1011a620,
   `Tests/ElysiumV6ClockPersistenceTests.cpp` under Elysium.Arm.V6.ClockPersistence.
   Exercise first-map1, frozen return, saved load and different section bases; sentinel policies;
   raw FLOAT weapon/layer exception; delayed I/O once; all-entity decode before callbacks;
   later hidden/deadline writes winning; fresh-load old runtime/one-shot absence. Test actual
   capture/apply paths, not a duplicate arithmetic helper. Coders only write tests, integrator runs.

8. **V6.5 carrier boundary and filed importer** — engine0x20097d00, vampire0x101a0a80/
   0x101a2a30; `ElysiumSaveArchive.{h,cpp}::FElysiumSaveArchive/operator<<`,
   `ElysiumEntityWorldPersistence.cpp::CaptureState/Freeze/ApplySnapshot/ApplyEntityRecord`,
   `ElysiumSessionSave.cpp::BuildPayload/ApplyPayload`. Expose explicit source SaveBase and
   destination RestoreBase, shared stable-identity/fixup and apply hooks for today's carrier.
   Route EVERY currently carried entity through them, auditing TIME/FLOAT/POSITION/handle types;
   no local copying or map-snapshot-as-full-import claim. Report actual carried classes and
   excluded transition/global classes to integrator. Proof: session_time_rebase executes real
   codec/applier at different bases, with a current-carrier entity as well as wait/queue/memory
   controls; session_map_load_fresh_world, session_map_clock_revisit and session_load_saved_clock
   prove clock selection. Full transition-set/global importer is filed to 0017/3 and /9;
   transition_full_carried_roster and transition_global_merge remain absent under those owners.

9. **M2 reconstructed-world coordinator lifetime** — engine0x20096010→0x2008f2e0,
   vampire0x1011a7a0/0x101a2e40→0x10136580→0x1023ae40→0x1023b840→0x1028d770→0x1025d880;
   destructor0x1023ba30→0x1028d800→0x1025d940 (packet §8).
   `ElysiumEntityWorld.cpp::FElysiumEntityWorld/Teardown/AttackCoordinator`,
   `ElysiumEntityWorldPersistence.cpp::ApplySnapshot`, `ElysiumSessionSave.cpp::ApplyPayload`.
   Preserve fresh world-scoped coordinators on load, no serialized membership or guessed
   re-admission. Integrator confirms the full reconstruction edge before policy coding; report
   any newly unread edge for listing recovery before parity. V6-3 observes empty lists at apply
   and real subsequent admission/release, not synthetic restore admission. Proof:
   save_restore_melee_coordinator, first real melee admit, cap2 and idempotency.

## Owed integration lines

Lane3 owns ArenaStage::Stage reset-before-Load1.0 and transaction runner. Lane2 owns TIME annotations,
NPC saved callback/leaf/rebase, navigator and K1. Integrator owns ElysiumWorldServices.h and
MapActor/Bodies adapter declarations, SaveStorage callback glue only if existing APIs cannot expose
results through this lane, oracle/verdict rows, existing save/session tests whose old absolute-time
assumptions fail. Report exact path/function/address/signature, not “someone should fix restore”.

Arena records for your chain: session_map_load_fresh_world, session_map_clock_revisit,
session_load_saved_clock, session_time_rebase, plus every restored-NPC/corpse/hidden record in
the integrator brief. Contract failures are this wave's, not expected reds.

## Coder rules

Write only this lane's listed files, in the worktree the coordinator names, using absolute paths.
Read files and run read-only research tools from `E:/dev/elysium-unreal`; write nothing there.
Never build, bake, run game/tests/arena, commit or push. No git clean/reset/stash/checkout/worktree
operations or deletion. Never hand-edit generated *Slots.cpp: hand bodies go in *SlotBodies.cpp;
an outside-lane body is owed to integrator, who adds kernel_verdicts.tsv and regenerates.
Put the retail address at every changed implementation line; state persistence datamap row/type
beside its save/restore address. Report a new divergence, do not adopt it. Check shadowed locals
(C4458/C4459 are errors), includes, declarations and duplicate definitions. Report under350 words:
changed files/functions/addresses, read-only checks, exact lines owed by files outside your lane,
API agreements and unrecovered inputs. No file named report*.md.
