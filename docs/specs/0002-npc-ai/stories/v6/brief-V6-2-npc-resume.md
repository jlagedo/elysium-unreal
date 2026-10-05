# Brief V6-2 — coherent NPC resume and lifecycle words

Read main AGENTS.md, V6 README and packets §§2–8. Start after V5b lands: ElysiumNpc.cpp and
ElysiumWeaponClasses.cpp overlap it (Schedule files also need its integrator hunks). Consume
landed V4d/V5b; no coder build/run. V6-1/V6-3 have already started on disjoint files/contracts.
Your exhaustive list (under Source/ElysiumUE) is exactly README V6-2:

```text
Private/Substrate/ElysiumNpc.h
Private/Substrate/ElysiumNpc.cpp
Private/Substrate/ElysiumNpcBase.h
Private/Substrate/ElysiumNpcBase.cpp
Private/Substrate/ElysiumSchedule.h
Private/Substrate/ElysiumSchedule.cpp
Private/Substrate/ElysiumNpcBaseLifecycle2.cpp
Private/Substrate/ElysiumNpcBaseLifecycle2.inl
Private/Substrate/ElysiumNpcLifecycle2.cpp
Private/Substrate/ElysiumNpcLifecycle.cpp
Private/Substrate/ElysiumNpcKernelBindings.cpp
Private/Substrate/ElysiumNpcNavigator.h
Private/Substrate/ElysiumNpcNavigator.cpp
Private/Substrate/ElysiumNpcScript.cpp
Private/Substrate/ElysiumNpcScript.inl
Private/Substrate/ElysiumNpcSenses.h
Private/Substrate/ElysiumNpcSenses.cpp
Private/Substrate/ElysiumNpcEnemyMemory.cpp
Private/Substrate/ElysiumInterestingPlace.h
Private/Substrate/ElysiumInterestingPlace.cpp
Private/Substrate/ElysiumNpcHints.cpp
Private/Substrate/ElysiumNpcStartTask.cpp
Private/Substrate/ElysiumWeaponClasses.cpp
Private/Substrate/ElysiumScriptedSequence.cpp
Private/Substrate/ElysiumNpcBaseSenses10.inl
Private/Substrate/ElysiumAnimatingOverlaySlotBodies.cpp
Public/ElysiumAnimatingOverlaySlotBodies.inl
Private/Substrate/ElysiumNpcBaseAnim.cpp
Private/Tests/ElysiumV6NpcRestoreTests.cpp
Private/Tests/ElysiumV6PlaceRestoreTests.cpp
```

## Numbered jobs

1. **Cursor and valid/invalid restore** —0x1027bc60/0x1027c160/0x1027bf50/0x1027be60;
   `ElysiumNpcBase.cpp::Serialize`, `ElysiumSchedule.{h,cpp}::FElysiumScheduleState`,
   `ElysiumNpcBaseLifecycle2.cpp::OnRestore`, `ElysiumNpc.cpp::OnPostRestore/RestartRestoredSchedule`.
   Save cursor+0x5c40, status+0x5c44, TIME schedule/task starts+0x5c48/+0x5c4c and failure+0x5c50;
   preserve wait/ideal/fail-program and task continuation operands through their existing owners.
   Validate schedule name/CRC/header/live enemy/target/cine after rebase, retain cursor on valid arm.
   FIX existing TaskIndex ceiling clamp to FailureReason>=0x2a→1. Do not cap a valid long program
   at42 tasks. Invalid prerequisites preserve retail give-up order and missing-cine IDLE arm.
   Remove RestartRestoredSchedule and its stale divergence only after jobs2/3 work; no StartTask,
   ClearSchedule or slot435 side effects on valid restore. Correct cursor comments here.
   Raw m_bConditionsGathered+0x5ca4 is BOOL SAVE: archive GatheredAt's sign meaning; restore
   true as nonnegative selected-base stamp, false as-1, never TIME. Do not force false at
   OnPostRestore; retain next RunAI's lawful clear. Snapshot and first consumer distinguish
   this from saving the unsaved condition mask.

2. **Navigator continuation** —0x102ee1e0→0x102f1dc0→0x102f2330;
   `ElysiumNpcNavigator.{h,cpp}::FElysiumNpcNavigator`,
   `ElysiumNpcBaseLifecycle2.cpp::RefindPostRestorePath`,
   `ElysiumNpcScript.{cpp,inl}::NavFindPathCorners`,
   `ElysiumNpc.cpp::SerializePatrolBlock/RestorePatrolAndAmbient`.
   Carry semantic goal type/target/position, flags, movement and arrival activity, tolerance,
   retry/timeout words, path-corner handles and patrol objects. Rebase references before OnRestore.
   Reconnect the actual existing motor/navigator route without rerunning task start or hint claim;
   invalidate UE route caches and re-find through existing goal/corner substrate. Preserve all
   FindPath success/retry/failure arms and arrival hooks; no unconditional-success seam.
   Remove RestorePatrolAndAmbient's unconditional bMoveIssued=false for a restored active path.
   Route reconstruction uses existing Recast modernization, with retail semantic/order contract.
   Report any motor API missing to integrator's embodiment adapter; no new navigation algorithm.
   Apply packet §3's complete restore-reachable dispatch: type1 resolved target, type2 remembered
   enemy and goal-adjust hook, type3 speed/corner chain (max128; terminal bit8/goal publication
   only below128, cutoff still succeeds), stored4/5/6/9, owner-target7 and byte-setting8 where
   represented. Other types and missing type1/2 target, type3 corner or type7 owner refuse;
   type7's slot586 target is an unguarded native precondition, not an invented refusal arm.
   Initial success clears retry bit0x20 and uses already-arrived gate; initial failure with
   duration0 fails, otherwise arms retry+timeout. Strict timeout<now/retry<now and retry success
   state0x6e arrival suppression survive restore; no broad R2 selector/route rewrite.
   Proof: save_restore_mid_path and save_restore_invalid_schedule missing-target/path controls,
   plus Elysium.Arm.V6.NpcRestore dispatch/retry equality/success/failure/arrival arms.

3. **Animation and move/shoot** —0x1008f120/0x10091880/0x10098c80/0x1008df10,
  0x102e8aa0/0x102e8ac0; `ElysiumNpcBase.cpp::Serialize`,
   `ElysiumNpcBaseAnim.cpp::ResetSequenceInfo/StudioFrameAdvance`,
   `ElysiumAnimatingOverlaySlotBodies.cpp` and public inl layer state,
   `ElysiumNpcBaseSenses10.inl::FMoveAndShootOverlay`.
   Save actual native sequence, cycle, playback, anim/prev times, finish/loop/past-half and
   event window, derived/live speed words and all four layer records. Rebuild model/cache/clip
   binding with restored phase, without ResetSequenceInfo resetting cycle/cursors or drawing
   another weighted pick. Preserve base LastEventCheck TIME versus layer LastEventCheck FLOAT.
   Move-shoot +0x10..2c SAVE; next-shot TIME/mode4 sentinel; +0xc unread is not SAVE, outer+4
   rebinds. No invented persisted opaque word. Integrator implements a body-phase/overlay seek
   seam in WorldServices/MapActor/Bodies if absent; describe exact owed signature/fields. Visual
   seek dispatches no events. Next live dispatch uses the restored interval once.

4. **TIME annotations and memory/weapon words** —0x101a0a80/0x101a2a30,
  0x102df010/0x102df090,0x1025506f..77/0x1028918d;
   `ElysiumNpcKernelBindings.cpp::AddBaseEntitySaveFields/AddNpcBaseSaveFields/AddNpcSaveFields`,
   `ElysiumNpcSenses.cpp::FElysiumNpcBaseMemory::Serialize/FElysiumNpcMemory::Serialize`,
   `ElysiumNpcEnemyMemory.cpp::Serialize`, `ElysiumWeaponClasses.cpp::Serialize`.
   Use lane1's explicit context for raw datamap TIME rows and leaf counterparts, one writer each.
   Audit inherited/subtype SAVE TIME rows reached by this wave's fixtures too, not just wait.
   Enemy LastSeenTime is TIME; durations remain durations. Named port-only scan/debounce deadlines
   need an explicit clock-domain policy, never silently age across time spent in another map.
   Save V5b bInReload/bInterruptReload/bIsJammed exactly. Weapon next primary/secondary are raw
   FLOAT (do not convert), weapon idle TIME. Do not call FinishReload at decode/restore.
   Preserve V5b single-round NPC return retaining bInReload; player continuation stays later.
   Correct stale binding “no MoveAndShootOverlay member” comments and raw-row type names.

5. **Marker transaction and restore scan** —0x102da0d0/0x102da860/0x102da7c0,
  0x102da600/0x102d9240/0x102d9320/0x102db5e0/0x10299a80;
   `ElysiumInterestingPlace.{h,cpp}::FMarker/Claim/Release/Serialize/OnPostRestore`,
   `ElysiumNpc.cpp::ClaimAmbientSpot/RestorePatrolAndAmbient/FinishAmbientUse`,
   `ElysiumNpcLifecycle2.cpp::FindInterestingPlaceHoldingMe/ValidateRestoredInterestingPlace`,
   `ElysiumNpcHints.cpp::InterestingPlaceMarkerOccupant`.
   Add complete occupant + min/max bounds marker rows, used-row count and allocated capacity;
   the real PickSpotFor writes AddMarker before ClaimMarker. Current ClaimAmbientSpot uses
   place origin, and StartTask overwrites InterestingPlacePosition with that origin; a marker-only
   patch would preserve the missing input. In InterestingPlace implement0x102d9fa0 sampler,
  0x102da9e0 overlap (retain its mins.x-for-Y/Z comparison bug),0x102d9ed0 four-slot failed-box
   ring and0x102da0d0 whole: padded hull±1 Source unit, local min/max bounds from existing raw
   authored keys, XY draws+place origin, Z unchanged for argument1, occupied resample counter>8
   increments failed-attempts and returns false; at most two area-clear attempts, store failed box
   with now+2, then even exhausted clearance warns/AddMarker/returns true. Use live IsAreaClear
   hull seam, no collision-success stub. Add bindings for min_bounds/max_bounds and sampled
   InterestingPlacePosition. Keep selector rating/group/order; no broader place-selection rewrite.
   `ElysiumNpcStartTask.cpp::StartTask` FIND_INTERESTING_PLACE consumes sampled result/refusal,
   must not overwrite it with Place->Origin. ClaimMarker only
   increments in-use on an existing row, release swap-removes entire row and conditional outputs.
   Save handles/both POSITION bounds, restore/rebase all rows before NPC scan; preserve linked-list
   ordering so LAST match wins. Implement both consistency refusals and early return on invalid
   release. Eliminate post-load Claimants hack as restore authority; no duplicate arrival/left.
   Entry gate is signed capacity+0x584 minus used rows+0x588 minus failed attempts+0x58c>=1,
   followed by enabled+0x57c; remove IsAvailable's capacity floor-to1. Shared occupied counter
   spans clearance attempts; ninth replacement draw refuses and increments +0x58c exactly once.
   Clearance failures write the failed-box ring but not that counter; second attempt warns even
   when clear, then AddMarker succeeds (packet §4). Keep constructor/list semantics. Proof:
   place_marker_reservation (capacity0/disabled/full/overlap/clearance/clear),
   save_restore_interesting_place_visit, save_restore_place_activity and restore_place_invalid_marker.
   If this required writer
   closes N17's zero-capacity symptom, report exact consequence/record to integrator for R2;
   do not silently broaden other defaults. Any absence of a needed POSITION
   transition offset in core is an exact owed line to lane1, not a different local encoding.

6. **Callback/hidden/death state** —0x100a8710/0x100a8990/0x102c1ec0/0x102998c0,
  0x1032c0e0/0x102696f0/0x103a38c0/0x10269960;
   `ElysiumNpcBase.{h,cpp}::ThinkFunctionName/Serialize`, `ElysiumNpc.cpp::Think/OnDormancyChanged/
   RestoreDeathBodyState`, `ElysiumNpcLifecycle.cpp::TroikaScriptUnhideTail`,
   `ElysiumNpcBaseLifecycle2.cpp::NPCInit/StartNPC/ThinkSet`.
   Expose callback identity and script-saved identity to lane1; preserve NULL as a meaningful
   callback. Born hidden initialization/body admission may not replace parked think with NPCThink.
   Unhide reinstalls correct callback due now; ResetThinkTimers once, weapon unhide, real cine latch
   handback of six physical/effect/flags words then clear. Do not use bHidden as cine latch or
   zero an existing EffectsWord. StartNPC's floor sweep writes through a real origin/motor sync
   (current direct Origin assignment can leave body elsewhere); gravity/fall task uses real motor.
   No hidden scheduling/perception; no special unhide teleport. Restore corpse callback/deadline/
   fade alpha and death latch; preserve V4d capability/once-only visual handoff/release, entity death
   spot. Never replay OnDeath, corpse creation, picks, sound or a new +10 clock at load.

7. **Cine save release** —0x1027bf50/0x1008df10, director possession0x101a7880;
   `ElysiumScriptedSequence.cpp::Serialize/SaveBlockReason`,
   `ElysiumNpc.cpp::OnPostRestore`, `ElysiumNpcLifecycle2.cpp::TroikaOnRestore`.
   Rebind the saved director's possessed NPC/activator and NPC cine before prerequisite checking;
   maintain script state, current task, sequence phase and output countdown. Remove only K1's
   possessing-director refusal when valid resume is complete; preserve unrelated gate reasons.
   The integrator's save_cine_possession_resume must prove no second Begin/End. Never cite the
   interesting-place helper0x10299a80 as cine evidence.

8. **Relationship/perception audit and witnesses** —0x10273790/0x102b38b0/0x10310710/
  0x1030ff10/0x1026f110/0x102814d0/0x10291610, `ElysiumNpc.cpp::InputSetRelationship`,
   `ElysiumNpcSenses.cpp::PerformSensing/TickSight/Serialize/OnPostRestore`.
   Compare current landed behavior: neutral player can already be Sighted; hated relationship
   produces enemy conditions on next legitimate Look. Preserve QuerySeeEntity and actual can-sense
   gate. Preserve RunAI's live-partner ordinary-gather skip and GetNewSchedule's gather on
   reselection; no manufactured continuous dialogue sensing. SetPlayerLos runs independently
   (2s/512-unit bypass/8s grace). Expose sighted/can-sense/player-LOS/cache and dialog/partner/
   task/gather probes via existing accessors. A mismatch in BaseRunAi/Think/Maintain is an
   integrator-owed line; those files are not yours.
   OnTakeDamage_Alive is already corrected in BaseDamage2; report a real divergence there to
   integrator, do not obtain the file. Damage witnesses distinguish absent attacker from qualified
   attacker and current-enemy unseen memory update. No invented “TakeDamage always records”.

9. **Arm evidence** — same addresses above;
   `Tests/ElysiumV6NpcRestoreTests.cpp` / `ElysiumV6PlaceRestoreTests.cpp` under
   Elysium.Arm.V6.NpcRestore and PlaceRestore. Prove nonzero cursor/status retained and failure>=42
   clamped separately, invalid CRC/cine/target/path fallback, live route resume, base/layer event
   cursor and sentinel/type distinctions, hidden callback and corpse/fade/null state, retained
   single-round flags, marker save/rebase/swap-remove/LAST scan/both refusals. Use real common
   serializer/applier, not mock member copies. RNG state and output counters distinguish replay.

10. **V6.3 landed words and filed producer accessors** — slot141/0x10265ed0,
    animating restore0x1008df10, combat restore0x10323b60, player gate0x1025506f..77;
    `ElysiumNpcBase.{h,cpp}::Serialize`, `ElysiumNpcBaseLifecycle2.cpp::OnRestore`,
    `ElysiumAnimatingOverlaySlotBodies.cpp`/public inl layer state,
    `ElysiumWeaponClasses.cpp::Serialize`. Persist/restore ALL already landed flinch, layer and
    weapon words; rebind existing owner/source references after shared fixup, without introducing
    a producer or replaying its event. Declare named unavailable producer accessors where needed
    at the lane-owned restore boundary: ReactionSlot141Source → 0005/4 reaction follow-up;
    PresenceListSource → 0006/2 Presence-source follow-up; PlayerSingleRoundReloadSource →
    named 0008 firearms-controller reload follow-up (no existing numbered 0008 story owns it).
    They answer unavailable/nothing for missing producers and identify the retail field/list,
    never fake a successful reaction/Presence admission/player continuation. An accessor required
    in an outside-lane combat declaration is an exact owed integrator line. Keep
    save_restore_flinch_live, save_restore_presence_live and save_restore_player_single_round
    absent under those owners. Proof now: save_restore_single_round_reload invokes the real
    V5b WeaponFinishReload/FinishReload NPC early-exit fixture; save_restore_move_shoot and
    save_cine_possession_resume prove ordinary native animation/event resume. No authored-map
    stand-in producer; no missing-source excuse to erase existing saved words.

## Owed lines and acceptance

Lane1 owns context/schema/phase ordering and base hide/now semantics. Lane3 owns fences/probes/
transactions/records. Integrator owns presentation seek/reconnection in ElysiumWorldServices,
MapActorEmbodiment/Bodies, existing arm fixtures, damage corrections if demonstrated, kernel
verdicts and oracle recovery. Every external owed line includes file, function, address and API.
No generated *Slots.cpp edits; a new virtual body belongs in a hand *SlotBodies.cpp, integrator
adds its verdict row/regenerates. Your list includes hand animation bodies, not generated slots.

Records: mid_path, cine possession, move_shoot, invalid_schedule, place visit/activity/invalid,
six corpse arms, hidden round-trip/fall, retained reload, relationship and dialogue (full names
in README/integrator brief). No reset/restart success standing in for these witnesses.

## Coder rules

Write only this lane's listed files, in the worktree the coordinator names, using absolute paths.
Read and run read-only tools from `E:/dev/elysium-unreal`; write nothing in main. Never build,
bake, run game/tests/arena, commit or push. No git clean/reset/stash/checkout/worktree operations
or deletion. Address at every changed implementation line; datamap type/offset beside persistence
changes. A new divergence is reported, not adopted. Shadowed locals are compile errors
(C4458/C4459); check declarations/includes/duplicate definitions. Report under350 words with
changed files/functions/addresses, read-only validation, interface agreements, unrecovered inputs
and exact lines owed by files outside the lane. No file named report*.md.
