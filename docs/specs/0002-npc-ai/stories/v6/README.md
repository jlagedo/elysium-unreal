# V6 — session, map clock and lifecycle

Planner handoff, 2026-10-05, with the final third-sitting rulings applied. Read `packets-V6.md`
first. This is a plan, not acceptance. The integrator takes the latest commit of the work branch
when it starts; today that is V4d `a5b58f37`: arena114 pass / 1 fail (`rollcall_vzombie`) /
15 expected-fail / 2 unexpected-pass; default169/0; arm1625/0. These are reported commit results,
not a planner measurement. V5b lands before V6 integration; refresh the baseline after it lands.
Coordinator names coder worktrees and the integration branch; do not work on the live main checkout.

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
transaction witnesses. Start V6-1 and V6-3 first, before V5b is committed: neither owns a V5b
file. V6-2 starts only after V5b lands, because ElysiumNpc.cpp and ElysiumWeaponClasses.cpp
overlap it; Schedule files also require its integrator hunks. Integration applies the core
context/interface contract before dependents. The box's M estimate excludes the missing runner
and navigator reconnection: this handoff is M–L, three coders plus integrator. All rulings fit
these manifests and serial integrator steps; no second round or lanes 4–6 is needed. A proved
missing required payload gets only the serialized prerequisite in integrator job6, not a broad
pipeline lane. Do not silently split an acceptance dependency to V7.

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

### Early-start interfaces (V6-1 and V6-3 do not depend on V6-2's implementation)

V6-1 treats each entity's Serialize/OnPostRestore and registered field metadata as opaque existing
interfaces. It supplies `SaveBase`, `RestoreBase`, policy-aware leaf TIME, shared stable-identity
fixup and the capture/decode/apply/post-restore/ready fences; it neither calls a new NPC helper
nor edits NPC/weapon bindings. Later V6-2 annotations and overrides plug into those interfaces.
V6-1 can code fresh-world/clock/context ordering against existing entity and session contracts.

V6-3 codes schema, GI-owned runner survival, envelope, clock and fencing against the agreed V6-1
operation-id/result and common codec/applier interfaces. Typed probes consume existing read-only
entity/kernel accessors through a field-dispatch boundary; missing NPC restore witnesses/fixture
adapters are exact owed signatures for V6-2 or the integrator, and return unavailable until wired.
No direct access to a proposed V6-2 member or assumed successful NPC restore is required to code
the harness. V6-2 later supplies saved-state/probe consumers without changing the transport schema.
Full resume acceptance waits for all lanes and V5b; early coding does not claim those records green.

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
- M2: packet §8 now records the load/construction/factory chain and CWorld constructor/destructor.
  Confirm that exact reconstruction chain before coding the policy; then measure empty fresh-world
  lists at apply, the first real melee admission, cap2/idempotency and later admission/release.
  No serialized membership or guessed re-admission; an unread edge must be read before parity.
- M3: transaction gates, freeze/apply/ready times and runner rebinding on both hosts. Check early
  clock1.0 separately from ready/startup latency and RNG draw position.
- V4d/post-V5b asset availability and the actual sequence/save instant are integration measurements;
  the listing cannot prove what the installed build contains. Record revision and recipe evidence.

## Overlap with incoming work

V6-1 and V6-3 intersect neither V5b coder nor V5b integrator manifests. V6-2 shares
`ElysiumNpc.cpp`, `ElysiumWeaponClasses.cpp` and the Schedule declarations/implementation with
V5b; it starts after V5b lands. `ElysiumWeaponClasses.h` is read, not assigned here.
Integrator repair/fixtures may share V5b's `ElysiumNpcBaseRunTask.cpp`,
`ElysiumNpcConditionsBodies.inl`, existing weapon/
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

## Judge's rulings applied

Final authority: `../v1/judge-third-sitting.md`, V6 rows, Clock standing question and V6 M2.
No V6 row is a Refuse or an extra Pull forward; V6.4's bounded R2 prerequisites run now.

| item | ruling | where it landed / proof |
|---|---|---|
| V6.1 body/clip/physics provenance | do now | Integrator jobs1/6: admission ledger before M3, bounded serialized missing body/clip/rig prerequisite; V6-3 job9 controlled same-arm donor only. Authored map_tutorial_unhide_thug3 stays; unavailable required witness blocks acceptance. |
| V6.2 presentation persistence | file | Integrator jobs1/7 and close: existing visual reconstruction without death replay; EmbodimentPoseCapture/Apply seam without logic feedback. Death impulse/prop_ragdoll/remaining rigs → 0014/4, /6 and full-corpus physics rollout; burning → 0014 handoff follow-up; Chaos pose/velocity → 0017/35. Four visual-only records stay absent; all six logical corpse records close now. |
| V6.3 flinch/Presence/player reload producers | file | V6-2 jobs3/4/10 persist existing words and named producer accessors; integrator job7/close. Slot141 → 0005/4 reaction follow-up; Presence → 0006/2 Presence-source follow-up; player single-round → named 0008 firearms-controller reload follow-up (no numbered owner). Three live-producer records stay absent; NPC real-function fixture and ordinary animation/event resume remain. |
| V6.4 place/navigation/N17 | do now | Packet §§3/4, V6-2 jobs2/5/9, V6-3 jobs5/6/9, integrator marker recipes/job8: full writer/save/release, raw capacity/failed attempts, sampled FIND and restore goal/refind arms. place_marker_reservation proves capacity0; visit/activity/invalid/mid_path records prove continuation. Only a demonstrated N17 consequence goes to R2. |
| V6.5 complete transition import | file | V6-1 jobs3/8 explicit bases/shared identity-fixup-apply/rebase today's entire carrier; integrator jobs7/8/close list excluded classes/global entities. Complete transition set → 0017/3 and /9; transition_full_carried_roster/global_merge stay absent. Clock/revisit/saved/rebase/current-carrier proofs run actual codec/applier. |
| Clock standing ruling | do now | V6-1 job1, V6-3 jobs3/10 and integrator job9: fresh1.0 before init/Load, revisit frozen, load saved; all zero-tuned records re-measured alone/paired at original bounds. Supersedes V7/V10 preserve-zero; no RNG compensation. |
| V6 M2 coordinator lifetime | do now | Packet §8 reconstructed retail chain; V6-1 job9, V6-3 job11 and integrator job10/recipe/close. Fresh coordinators; save_restore_melee_coordinator proves apply-fence empty, first real admission, cap2/idempotency and lawful release. No optional-debugger inference fallback. |

## Integrator evidence, 2026-10-05 — not green

Ten additional builds were used (five passed/five failed). Build10 failed on the class-descriptor declaration in the restore-place trace; its include is corrected but unbuilt at the cap. Nothing was staged, committed or pushed; V6 remains unticked. AGENTS.md and ElysiumRng.cpp were excluded.

The last successful binary (build9) has default169/0, arm1650/1, and full arena130 pass /9 fail /12 expected-fail /7 unexpected-pass /9 error. Kernel regenerates/checks7/7 against the final source. The one arm miss is the initial-state parser fixture omitting its required probe, corrected only in the unbuilt source. Three baseline passes are now red: range_bands, ranged_sustained_fire and idle_lookaround. Five old red5 labels are stale after actual hidden-lifecycle passes; the two inherited hearing/interest unexpected passes remain.

[proof-V6.json](proof-V6.json) records each remaining cause, first_unmet and trace path. The full run is E:/elysium-work/reports/arena/20261005T152300.880828Z/index.json; its traces and the baseline are preserved under E:/elysium-work/codex/V6-int/evidence. The exhaustive alone/paired epoch audit and full recipe/admission coverage are incomplete; no acceptance claim is made from the aggregate run.

N9 measured the first refused post-death attempt: live0/global0, gate occupied, child#18 flags0x12000/alive0/life1 with intersecting logical bounds. Retail0x1034b692..737 /0x101cc9e0 has no alive test. Corrected maker_respawn observes refusal, normal Kill clears the corpse, and the real retry produces child2. No maker branch was changed.

The real flamethrower reload record passes, as does its loaded silent control. It records actual post-equip clip1, disclosed initial0/reserve250/fake8, condition0x40, c2 and authored reload continuation, the male native clip, slots322/323, min bulk250 with NPC reserve250 retained, both stamps, clears/completion and final flags0. Native final observation is measured at30.000001564621925 scenario seconds; no seed or behavioral bound was widened. Its control_sequence pairing passed on build9; the ranged_fake_reload pairing is separately recorded in proof-V6.json when completed.

Remaining named unavailable sources are event-free body seek (0017/35 per the integrator instruction's filing), conversation producer0018/18, slot141 reaction0005/4, Presence0006/2, player single-round0008 firearms-controller follow-up, Chaos pose/velocity0017/35, death impulse/prop_ragdoll0014/4,/6, burning look0014 handoff follow-up, and complete transition-set/global import0017/3,/9. These do not waive the unowned failures. The nine filed records from the packet remain absent and uncredited.
