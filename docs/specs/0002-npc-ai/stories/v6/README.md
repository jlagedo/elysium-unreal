# V6 — session, map clock and lifecycle

Planner handoff, 2026-10-05, against V4c `d0f79574`. Read `packets-V6.md` first.
This is a plan, not acceptance. V4d and V5b land before V6. Coordinator names coder worktrees
and the integration branch; do not work on the live main checkout.

Historical V4c acceptance: default169/0, arm1624/0, arena132 records:113 pass/1 fail/
16 expected-fail/2 unexpected-pass. The actual post-V4d/post-V5b baseline must replace those
numbers before integration; no planner run was performed.

## Result and dependency order

Fresh maps initialize curtime1.0; revisits restore their own frozen clock; load restores the
saved map clock. TIME fields encode deltas with explicit bases and sentinel policies, while
FLOAT stamps stay FLOAT. A restored NPC resumes cursor+0x5c40, task status, movement goal,
animation/event intervals, weapon bytes and callback identity. The correct failure clamp is
on+0x5c50. K1's possessing-cine save refusal goes after its resume witness passes. Hidden NPCs
admit their bodies and activate lawfully while preserving NULL think; unhide reinstalls the
callback due now. N14 gains the whole marker reservation/save/release chain. Damage, relationship
and dialogue are compared to the already landed chain before changing anything. In dialogue,
retail's ordinary gather skips a live partner; independent player-LOS upkeep continues and
GetNewSchedule can gather on reselection. The scouts' unconditional in-dialogue sensing claim
is refuted. The gathered latch itself is BOOL SAVE, preserved through restore before RunAI clears it.

Clock/context contract first; then coherent world decode and NPC restoration; then real
transaction witnesses. Coders may prepare disjoint changes concurrently, but integration applies
the core context/interface contract before dependents. The box's M estimate excludes the
missing transaction runner and navigator reconnection: this handoff is M–L, three coders plus
integrator, without pipeline/bake work. Do not silently split an acceptance dependency to V7.

## Lane manifests — exhaustive, repository-relative

### V6-1 — clock, world boundary and persistence core

```text
Source/ElysiumUE/Public/ElysiumGameClock.h
Source/ElysiumUE/Public/ElysiumTimeControl.h
Source/ElysiumUE/Public/ElysiumSessionSubsystem.h
Source/ElysiumUE/Public/ElysiumSaveTypes.h
Source/ElysiumUE/Public/ElysiumSaveArchive.h
Source/ElysiumUE/Public/ElysiumClassRegistry.h
Source/ElysiumUE/Public/ElysiumEntityWorld.h
Source/ElysiumUE/Public/ElysiumEntity.h
Source/ElysiumUE/Public/ElysiumMapSubsystem.h
Source/ElysiumUE/Private/Session/ElysiumSessionSubsystem.cpp
Source/ElysiumUE/Private/Session/ElysiumSessionSave.cpp
Source/ElysiumUE/Private/Session/ElysiumSaveArchive.cpp
Source/ElysiumUE/Private/Session/ElysiumTimeControl.cpp
Source/ElysiumUE/Private/Substrate/ElysiumClassRegistry.cpp
Source/ElysiumUE/Private/Substrate/ElysiumEntityWorld.cpp
Source/ElysiumUE/Private/Substrate/ElysiumEntityWorldPersistence.cpp
Source/ElysiumUE/Private/Substrate/ElysiumEntity.cpp
Source/ElysiumUE/Private/Map/ElysiumMapSubsystem.cpp
Source/ElysiumUE/Private/Map/ElysiumMapActorLifecycle.cpp
Source/ElysiumUE/Private/Tests/ElysiumV6ClockPersistenceTests.cpp
```

### V6-2 — NPC resume, reservations and retained words

```text
Source/ElysiumUE/Private/Substrate/ElysiumNpc.h
Source/ElysiumUE/Private/Substrate/ElysiumNpc.cpp
Source/ElysiumUE/Private/Substrate/ElysiumNpcBase.h
Source/ElysiumUE/Private/Substrate/ElysiumNpcBase.cpp
Source/ElysiumUE/Private/Substrate/ElysiumSchedule.h
Source/ElysiumUE/Private/Substrate/ElysiumSchedule.cpp
Source/ElysiumUE/Private/Substrate/ElysiumNpcBaseLifecycle2.cpp
Source/ElysiumUE/Private/Substrate/ElysiumNpcBaseLifecycle2.inl
Source/ElysiumUE/Private/Substrate/ElysiumNpcLifecycle2.cpp
Source/ElysiumUE/Private/Substrate/ElysiumNpcLifecycle.cpp
Source/ElysiumUE/Private/Substrate/ElysiumNpcKernelBindings.cpp
Source/ElysiumUE/Private/Substrate/ElysiumNpcNavigator.h
Source/ElysiumUE/Private/Substrate/ElysiumNpcNavigator.cpp
Source/ElysiumUE/Private/Substrate/ElysiumNpcScript.cpp
Source/ElysiumUE/Private/Substrate/ElysiumNpcScript.inl
Source/ElysiumUE/Private/Substrate/ElysiumNpcSenses.h
Source/ElysiumUE/Private/Substrate/ElysiumNpcSenses.cpp
Source/ElysiumUE/Private/Substrate/ElysiumNpcEnemyMemory.cpp
Source/ElysiumUE/Private/Substrate/ElysiumInterestingPlace.h
Source/ElysiumUE/Private/Substrate/ElysiumInterestingPlace.cpp
Source/ElysiumUE/Private/Substrate/ElysiumNpcHints.cpp
Source/ElysiumUE/Private/Substrate/ElysiumNpcStartTask.cpp
Source/ElysiumUE/Private/Substrate/ElysiumWeaponClasses.cpp
Source/ElysiumUE/Private/Substrate/ElysiumScriptedSequence.cpp
Source/ElysiumUE/Private/Substrate/ElysiumNpcBaseSenses10.inl
Source/ElysiumUE/Private/Substrate/ElysiumAnimatingOverlaySlotBodies.cpp
Source/ElysiumUE/Public/ElysiumAnimatingOverlaySlotBodies.inl
Source/ElysiumUE/Private/Substrate/ElysiumNpcBaseAnim.cpp
Source/ElysiumUE/Private/Tests/ElysiumV6NpcRestoreTests.cpp
Source/ElysiumUE/Private/Tests/ElysiumV6PlaceRestoreTests.cpp
```

### V6-3 — completion-aware arena and comparisons

```text
Source/ElysiumUE/Private/Debug/ElysiumArenaScenario.h
Source/ElysiumUE/Private/Debug/ElysiumArenaScenario.cpp
Source/ElysiumUE/Private/Debug/ElysiumArenaScenarioRunner.h
Source/ElysiumUE/Private/Debug/ElysiumArenaScenarioRunner.cpp
Source/ElysiumUE/Private/Debug/ElysiumArenaRun.h
Source/ElysiumUE/Private/Debug/ElysiumArenaRun.cpp
Source/ElysiumUE/Private/Debug/ElysiumArenaStage.h
Source/ElysiumUE/Private/Debug/ElysiumArenaStage.cpp
Source/ElysiumUE/Private/Tests/ElysiumV6ArenaPersistenceTests.cpp
Arena/README.md
```

Proof: A∩B=A∩C=B∩C=∅; |A|20, |B|30, |C|10; union60. Tests named V6 are new.
Paths, not functions, define ownership; a shared include/call does not authorize another file.
The coder briefs use these exact manifests. Records, oracle/spec/triage/tracker, verdicts,
existing tests, presentation adapters and measurement taps outside these lists are integrator-owned.

## Contracts the lanes exchange

1. Lane1 gives SaveArchive an explicit save/restore time base and a type/policy-aware TIME
   operation; registered accessors get independent persistence type metadata (not a new Python
   number type). Lane2 annotates raw-datamap TIME registrations/leaf words. FLOAT exception table
   in packets §2 must survive. No field is written twice by accessor walk and leaf archive.
2. Lane1 exposes a common capture/decode/apply/post-restore fence, operation id/result and
   selected clock base. Payload schema/floor advances once; saves are disposable, no migration.
   Lane2 supplies callback identities, leaf state and post-restore rebind; lane3 waits for the
   capture/storage/apply/readiness results. Agree names in reports before integrating calls.
3. Lane1 makes all entities/words available before any OnPostRestore; handles rebase before
   virtual OnRestore, including cine, target, maker, place and weapon owner. Reverse restored-list
   post-restore order follows0x1011a620. Later retail callback/deadline writes win.
4. Lane2 supplies a read-only snapshot witness for cursor/status/times, movement goal, base/four
   layer words, move-shoot, weapon flags/stamps, marker rows, hidden and callback. Lane3 compares
   at capture and immediately after common restoration before the next simulation think.
   Re-derived cache/transient counters and Troika's deliberate shoot-at reroll are not equality fields.
5. Green Room checkpoint transport uses normal save codec/storage/common entity applier and
   saved stage defs/network/seat, with nonshipping harness provenance. Normal map save/load uses
   RequestSave/Load and its baked-map gate. No direct copying of NPC members as a “load”.

## Acceptance records

Every row below has staging and expect/never citations in `brief-V6-integrator.md`. No new known_red.
Use actual witness maps for authored reach; Green Room for controlled arms/unreachable weapon state.

| family | records |
|---|---|
| clock/world/command | session_map_load_fresh_world; session_map_clock_revisit; session_load_saved_clock; session_time_rebase |
| cursor/scene/animation | save_restore_mid_path; save_cine_possession_resume; save_restore_move_shoot; save_restore_invalid_schedule |
| reservations | save_restore_interesting_place_visit; save_restore_place_activity; restore_place_invalid_marker; place_marker_reservation |
| corpse/hidden | save_restore_corpse_unseen; save_restore_corpse_seen; save_restore_corpse_burn; save_restore_corpse_static; save_restore_corpse_fade; save_restore_corpse_pedestrian; save_restore_hidden_unhide |
| lifecycle/perception/damage | map_tutorial_unhide_thug3; lifecycle_unhide_fall_to_ground; lifecycle_startnpc_ground_drop; dialogue_perception; damage_last_record; damage_record_branches |
| handoffs/measurements | save_restore_single_round_reload; maker_refusal_measurement; maker_respawn (corrected only after M1); save_restore_melee_coordinator |
| existing guards | lifecycle_unhide_fights; lifecycle_relationship_flip; input_setrelationship; rollcall_vanimal; rollcall_vdog; rollcall_vscurrying; places_pedestrian_visit; script_walk_to_mark; script_dialog_hold; cover_move_shoot; ranged_open_fire; corpse_removed_unseen |
| harness | persistence_refused_save; persistence_missing_load; persistence_world_rebind (self-tests) |

New/changed records are this wave's. At-most/after/within already exist: use them to ban duplicate
claims/begin/death/events after load. Scenario elapsed time remains monotonic across clock rewind;
world time is a separate probe. Same-time snapshot comparisons are taken at the restore fence,
not a frame later. Measure bounds against the map1.0 stream, without arbitrary seed changes.

## Measurements the listing cannot provide

- M1: first refused post-death maker attempt, live/global/gate/candidate flags/life/bounds.
  Corpse blockage => record correction, no alive filter; clear it through normal Kill then spawn.
- M2: same-map retail coordinator constructor/destructor/list lifetime. Read thunks/callers and
  CWorld Precache; no exact load hook established. Capture port empty-world policy and capacity
  guards; if no retail debugger is available report this precise inference, do not serialize lists.
- M3: transaction gates, freeze/apply/ready times and runner rebinding on both hosts. Check early
  clock1.0 separately from ready/startup latency and RNG draw position.
- V4d/post-V5b asset availability and the actual sequence/save instant are integration measurements;
  the listing cannot prove what the installed build contains. Record revision and recipe evidence.

## Overlap with incoming work

V5b coder overlap: `ElysiumWeaponClasses.cpp`; its declarations `ElysiumWeaponClasses.h` are
read, not assigned here. Integrator repair/fixtures may share V5b's `ElysiumNpcBaseRunTask.cpp`,
`ElysiumNpc.cpp`, `ElysiumSchedule.h/.cpp`, `ElysiumNpcConditionsBodies.inl`, existing weapon/
run-task/lifecycle tests and Arena combat records. Consume V5b's bytes/deadline writers/cache;
do not reimplement fake reload or interrupt masks.

V4d D3 overlap: `ElysiumNpc.h/.cpp`, `ElysiumNpcBase.h/.cpp`, `ElysiumEntity.cpp`;
integrator adapters share `Public/ElysiumWorldServices.h`, `Public/ElysiumMapActor.h`,
`Private/Map/ElysiumMapActorEmbodiment.cpp`, `Private/Visual/ElysiumEntityBodies.h/.cpp`,
`Private/Tests/ElysiumTestServices.h` and `ElysiumNpcCombatTests.cpp`. Oracle overlap:
`docs/vtmb/npc-ai/lifecycle.md`, `docs/vtmb/combat-and-damage.md`. V4d's visual rig/bake remains
untouched. V4c also owns already landed clock/seed, death, team/memory and animation logic
in EntityWorld/Persistence/Npc/WeaponClasses/ArenaStage: preserve it while replacing divergences.
Integrator reconciles exact incoming hunks, never overwrites worker edits with this planner snapshot.

## For the judge

No pipeline or re-bake lane is authorized by this plan.

1. **Actual missing body/clip/physics provenance**, if M3 or thug3 staging exposes it: record exact
   class, model, native sequence, asset path/recipe and runtime admission result. Runtime hook
   absent => V6 repairs its own seam. Baked payload missing => content owner supplies it/re-bakes,
   or judge selects an already baked same-arm donor; never invent an event or enlarge a deadline.
   Existing V4d regular_cop rig attachment is a prerequisite, not a V6 re-bake task.
2. **0014 presentation**: Chaos pose/velocity persistence, death impulse/hitbox producer,
   prop_ragdoll, burning look and full-corpus rollout. Evidence: retail corpse entity remains at
   death spot, client ragdoll0x10090180; V4d named Chaos modernization. Alternatives: fresh visual
   reconstruction smoke now (chosen), or a later explicit visual-save feature; no logic feedback.
3. **0005/0006/player weapon**: flinch slot141 producer, Presence list/source and player
   single-round continuation. Evidence:0x10265ed0/0x10323b60/0x1025506f..77. Persist landed
   words now; those later specs supply producers, never mock them for an authored map witness.
4. **R2 broader place/navigation families and N17 bookkeeping**: raw capacity arithmetic in
  0x102da0d0, type3/corner/retry path arms0x102f2330. The real PickSpotFor writer is required by
   N14 and recovered/ported here, including sampled bounds and all refusal/success arms; broader
   selection/nav algorithms stay R2. This prerequisite can eliminate N17's zero-capacity symptom;
   report that consequence with the new capacity0 witness, never silently tick unrelated R2.
   If a needed existing seam is actually missing,
   judge chooses a same-arm witness or re-scopes that prerequisite; do not replace it with success.
5. **Cross-map entity import beyond today's transition carrier**: engine0x20097d00 proves TIME
   rebasing. The V6 core and controlled rebase record must support different bases, including
   any already carried entity. A full retail transition-set/global-entity importer belongs to
   session/world work (0017), not a new content pipeline lane. Alternatives: preserve current
   importer boundary with named unrepresented entity set, or schedule its complete later port.

M2 is an unrecovered sequencing measurement, not permission for a new modernization. If it cannot
be obtained, report exactly the inferred empty-list policy and limit the acceptance claim.
