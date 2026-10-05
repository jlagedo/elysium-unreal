# Brief V7-1 — typed delivery and NPC input adapters

**Start after V5b and V6 commit.** V6 shares ElysiumNpc.cpp/.h, ElysiumEntity.cpp,
ElysiumEntityWorld.cpp and ElysiumClassRegistry.h/.cpp with this lane. Relocate functions
by name on V6's committed code. Fresh arenas/maps use1.0 before Load/initialization;
keep seed/reset order, shared draws, predicates and original behavioral bounds.

Read AGENTS.md, [README.md](README.md), [packets-V7.md](packets-V7.md) and Arena/README.md.
Work after V5b/V6 landing in the coordinator-named worktree; main is read-only.
Faint already exists; GhoulCroucher disturbed source already exists. Neither is a blank job.

## Files — exhaustive ownership

- `Source/ElysiumUE/Public/ElysiumClassRegistry.h`
- `Source/ElysiumUE/Private/Substrate/ElysiumClassRegistry.cpp`
- `Source/ElysiumUE/Private/Substrate/ElysiumEntityWorld.cpp`
- `Source/ElysiumUE/Private/Substrate/ElysiumEntityWorldDialogue.cpp`
- `Source/ElysiumUE/Private/Substrate/ElysiumEntity.cpp`
- `Source/ElysiumUE/Private/Substrate/ElysiumChoreoScene.cpp`
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcClasses.cpp`
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcLifecycle2.cpp`
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcBaseLifecycle2.inl`
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcCamera.cpp`
- `Source/ElysiumUE/Private/Substrate/ElysiumScriptedSequence.cpp`
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcNewscaster.cpp`
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcPlayerController.cpp`
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcPlaceholder.cpp`
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcSpawnSpecies.cpp`
- `Source/ElysiumUE/Private/Substrate/ElysiumNpc.cpp`
- `Source/ElysiumUE/Private/Substrate/ElysiumNpc.h`
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcGhoulCroucher.cpp`
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcGhoulCroucher.h`
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcRat.cpp`
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcRat.h`
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcPayphone.cpp`
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcPayphone.h`
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcSquad.cpp`
- `Source/ElysiumUE/Private/Substrate/ElysiumRulebook.cpp`
- `Source/ElysiumUE/Private/Substrate/ElysiumRulebook.h`
- `Source/ElysiumUE/Private/Audio/ElysiumLineService.cpp`
- `Source/ElysiumUE/Public/ElysiumLineService.h`
- `Source/ElysiumUE/Private/Tests/ElysiumInputTests.cpp`
- `Source/ElysiumUE/Private/Tests/ElysiumStealthKillTests.cpp`
- `Source/ElysiumUE/Private/Tests/ElysiumNpcInputsV7Tests.cpp`
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcGhoulCroucherSlotBodies.cpp` (new)
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcRatSlotBodies.cpp` (new)
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcPayphoneSlotBodies.cpp` (new)

README proves pairwise disjointness. Entity.cpp/Npc.cpp/Npc.h overlap V4d;
Npc.cpp also overlaps V5b's integrator. Preserve their landed changes.
No Animating/consumer/harness/generated file belongs to you.

## Numbered jobs

Every name below is a function site; new adapters live in Npc.cpp with declarations in Npc.h
and registration in NpcClasses.cpp::BuildNpcClass. Packet§3 supplies exact declared type,
raw-handler fallback and sequencing. Do not replace authoritative fields with new parallel flags.

1. **ClassRegistry.h::FElysiumClassDesc::Input**, ClassRegistry.cpp::FindInput and
   EntityWorld.cpp::DeliverInputTo, **0x100abc90 / 0x100d05d0 / 0x100d0a00**:
   add OPTIONAL declared input type beside a thunk; lookup type from the same winning
   descriptor as the thunk, including subclass shadowing. Convert on a private input-delivery
   copy before invoking. Implement packet§2 matrix exactly, report conversion failure distinctly
   from unknown input, no handler/tally-delivered on rejection. Keep activator/caller and input
   trace ordering. Do not change FElysiumVariant's general ToBool or generated metadata.
   Cover String atoi including empty/"true"/"-2", float truncation, no Bool→numeric/String,
   failed vector conversion, Void scalar defaults, Handle→String success retaining type.
   String handlers' own VariantToString fallback is separate (Bool true/false, %g, bracketed vector).
   Apply new typing only to the19 acceptance names plus DisableThink and ChangeSchedule;
   preserve existing untyped registrations elsewhere.

2. **Npc.cpp::InputDisableThink / SetDisableAi**, **0x1029f2a0 / 0x1029f300**:
   leave raw handler Bool-only; dispatcher now converts String1/0. Repeated false does not
   reset timers, true→false resets614 before false write. Existing V4c Bool records stay Bool.

3. **Npc.cpp::InputChangeSchedule** (new; split InputNamedSchedule),
   NpcClasses::BuildNpcClass, **0x102c33f0 / 0x102c47e0 / 0x102ae7f0**:
   first-character '-' skips ONLY forced-word write; null fallback skips similarly;
   otherwise resolve raw then SCHED_ name and store registered id equivalent, including -1
   failure and zero sentinel. Always set raw flags2 mask0x82000000.
   Do not StartScheduleId or reset timers.
   Existing ElysiumNpcSelect.cpp::FElysiumNpc::PreSelectSchedule consumes the word
   (0x102ae920); keep NPC StartSchedule K1 alias immediate without claiming a retail caller.

4. **InputAllowAlertLookaround**, **0x102c2b90**: write existing bAllowAlertLookaround.
5. **InputAllowKickHintUse**, **0x102c2c10**: write existing ScheduleHost.bAllowKickHintUse.
6. **InputAllowOpenDoors**, **0x102c3540**: call CapabilitiesAdd/Remove(0xd00).
7. **BuildNpcClass Faint thunk**, **0x1029f250 / 0x102ae780**:
   retain single landed reset614→cause0x26c2→SetSchedule(0xfa,false) implementation;
   annotate Void input metadata. Match the existing source/cause observer, no invented retail pointer.
8. **InputFleeAndDie**, **0x1029f210 / 0x102ae750**:
   reset614→cause0x26b5→SetSchedule(0x6f,false), preserving dead/ideal-dead/life refusal.
9. **InputMakeInvincible**, **0x102c2a30**: write existing bInvincible.
10. **InputSetBloodShieldDiscipline**, Npc.cpp/.h and NpcClasses::BuildNpcClass,
    **0x102c32d0 -> 0x101e1590 / 0x101e3380 / 0x101e1870 / 0x101e3af0**:
    bind typed Bool to the integrator's bounded direct self-status/self-target service,
    proposed ElysiumDisciplines::SetBloodshieldDirect(FElysiumCombatCharacter&, bool).
    True resolves the named record and executes status BEFORE target; false removes the
    high-bit target effect with interrupted=false. Reuse actual status/modifier/hit/teardown
    state. No generic Use, learned/cost/world-area/ordinary-Use gates, or EndBloodshield shortcut.
    Integrator supplies the service after lane integration; no unavailable-hook fallback can close.
    **Proof: input_bloodshield_v7**, on/repeated-on/off/clean-off, buffer/modifier/status and
    observed sound/replacement/removal ordering; packet §8 includes human vs BloodGuardian arms.
    Service implementation is integrator-owned; return the exact API binding/declaration.

11. **InputSetBossMonster**, **0x102c3500**: write existing bIsBossMonster.
12. **InputSetDefaultDialogCamera**, **0x102c2910**:
    native trim/buffer260/nonempty write only; preserve previous value on whitespace/empty.
    Match retail byte bound through the port string representation; identify non-ASCII conversion
    limitation if encountered, do not silently choose Unicode character count as recovered bytes.
13. **InputSetDontFacePlayerInDialog**, **0x102c34b0**, PE row**0x105d0d18**:
    register **String**, retain raw-handler Bool test/OR8 else AND~8. String"1" clears;
    Bool true delivery fails conversion. Do not “correct” the shipped datamap.
    Existing NpcDialogue::StartTalking0x102c0270 already reads bit8; do not rewrite it.
14. **InputSetFallToGround**, **0x102c3470**:
    true AND0x7fffbfff else OR0x80004000 through raw flag-word setter.
    Existing Think19NormalSet2 reads bit0x4000; no fall movement is owed because0x102bfdf0 is empty.
15. **InputSetFollowerBoss**, **0x102c3350 / 0x102c4430**:
    delegate SetFollowerBossName (Werewolf2.cpp), not only SetFollowerBoss handle storage.
    Name writes even when resolution fails; keep reset/0x3008 order and existing connected-squad
    fatal→log/refusal divergence. No new squad/follower-locomotion implementation.
16. **InputSetFollowerType / NpcSquad.cpp::SetFollowerType**,
    **0x102c33a0 / 0x102c4640 / 0x102c4680 / 0x101e8c90**:
    route input to existing setter. Complete its runtime row source with FElysiumRules::Raw/Flt,
    recording nested child order in Rulebook.h/Rulebook.cpp::FElysiumRules::Load.
    Case-insensitive chosen row, empty/missing→FIRST authored child; no hardcoded Default selector.
    Read all three distances before ordered +10 clamps; store caller's type text afterward.
    Absent rule source remains a named no-row seam, distinct from a missing name with fallback.
    No rules.txt, pipeline, bake or full follower story work.
17. **InputSetInvestigateMode**, **0x102c2ab0**: typedInt0..6 write; invalid logs, preserves.
18. **InputSetInvestigateModeCombat**, **0x102c2b20**: same, other word; no range clamp.
19. **InputSetMovementMultiplier**, **0x102c3580 / 0x1008d230**:
    existing MotionTrail branch exact ±1, >1 and low floor. Call lane2's
    SetPlaybackAndSpeedScalar. NaN follows listing trail3/store; do not add a finite-only gate.
    No playback duplicate, speed multiplier in motor or animation restart.
20. **InputSetSpeechVolume**, **0x102c2680 / 0x102c278d..27a2**:
    clamp/store existing Dialogue.SpeechVolume; talking only updates same live voice gain.
    LineService::SpeechVolumeFor needs the resolved owner, not static default1.
    Add owner-indexed active speech handles in LineService.h/.cpp and an update method
    which calls existing IElysiumAudio::SetVoiceVolume (gain0 legal).
    Wire Entity.cpp::PlayDialogFile, ChoreoScene.cpp's PlayDirect speaker calls, and
    EntityWorldDialogue.cpp's dialogue-turn submission to carry actual speaker/volume;
    honor SoundOverride emitter without transferring speaker's volume ownership.
    Do not CancelSession/PlayDirect again to change gain, advance lip/scene/media time,
    reset TalkingUntil or fire dialog outputs. Decode-pending updates ledger gain.
    This maps engine channel5 flags4 volume update to the existing Unreal voice API;
    report uncertain routes for the integrator's required handle/cursor measurement.
21. **InputStayEntrenched**, **0x102c2bd0**: write existing bStayEntrenched; lane2 owns predicate.
22. **InputWalkToNode**, **0x1029e840 / 0x102c47e0 / 0x102d2900 / 0x1029f460**:
    DEAD no-op; byte buffer256; comma/space strtok first2 tokens, third ignored.
    Resolve schedule first, then node; missing tokens/node leave path unchanged.
    Call existing BuildPatrolPath with one node, Replace/repeat0/type0, preserving retained
    schedule quirks and failure-1. Do not delegate whitespace-only FollowPatrolPath parsing.
23. **Npc::IsValidStealthKillTarget**, **0x102c2300 / 0x1028c680**:
    virtual const hand query; hidden, DialogName(+128), invincible, liveConVar state arm,
    hear,see,virtual IsAlive in order. Read slot464 state through existing port enum/retail
    mapping rather than assuming Mind::Alert's numeric value. Shipping default1 comes from
    generated live tunable; zero/command branch restricts1/0xd.
    GhoulCroucher.h declares the override; GhoulCroucherSlotBodies.cpp::IsValidStealthKillTarget
    **0x1037bbc0** returns true on
    !IsDisturbed0x1037bb20 before all base guards, else qualified base call.
    Rat.h/Payphone.h declare overrides; RatSlotBodies.cpp **0x103ad680** always true;
    PayphoneSlotBodies.cpp **0x101aa8d0** false.
    Keep existing Spawn/GatherConditions/OnDisturbed producer, not a second latch or class switch.
    Keep existing hand-query identities; new species hand bodies are in the owned SlotBodies
    files. Report generator glue/verdict lines to integrator rather than editing Slots.
24. **Tests**, InputTests/StealthKillTests/new NpcInputsV7Tests,
    **0x100abc90 / packet§3 handler addresses / 0x102c2300**:
    one TABLE test enumerates every33 retail inventory name (existing pending names identified)
    and separately the19 V7 types/real thunk resolution across inheritance.
    Registration existence is distinct from effect success. Test converter matrix,
    Faint/Flee dead refusal/order, ChangeSchedule deferred/dash-preserve/invalid sentinel,
    investigate ranges, camera preserve/limits, DontFace shipped mismatch, follower type
    first-row/clamps, volume same-handle update and all stealth species/ConVar/base guards.
    Name new unit family Elysium.Arm.NpcInputsV7.; no stale false “developer-only” comments.
    Restore live ConVar after each arm. Do not test by changing global Python coercion.

25. **V7.4 initialization and BOOL SAVE**, NpcLifecycle2.cpp::TroikaNPCInit and
    Npc.cpp::Activate, NpcBaseLifecycle2.inl::bIsBccTargetable,
    NpcClasses.cpp::BuildNpcBaseClass/BuildNpcClass, **0x1029a0b0 / 0x1029a4a2**:
    use the existing latch; nonempty StatTemplate sets true, empty does NOT force false.
    Preserve construction/default policy verified by integrator §0 job1 and dispatch order.
    Bind native m_bIsBCCTargetable +0x1480 as **BOOL SAVE**, count1, no external/input name
    (raw CBaseCombatCharacter datamap0x1061664c, flags2). Inspect V6's inherited registration
    first; ensure one effective SAVE binding, never a shadow flag or hand edit of generated
    bindings. Use the owned hand registration site for an absent binding, or report an exact
    generator-owned correction for serial integration.
    Preserve/repair only this word's initialization writers at ScriptedSequence.cpp::Spawn
    **0x101a6f10** (false), NpcCamera.cpp::NPCInit **0x103692c0** (false after platform
    refusal), NpcNewscaster.cpp::NPCInit **0x103a0420** (false),
    NpcPlayerController.cpp::NPCInit **0x103a4580** (false),
    NpcPayphone.cpp::NPCInit **0x101aab90** (true after Troika),
    NpcPlaceholder.cpp::NPCInit **0x103a4350** (true after Troika), and
    NpcSpawnSpecies.cpp::FElysiumNpcWerewolf::Spawn **0x103caa30** / VampireBoss::TransformationStart
    **0x103c60a0, write0x103c6218** (true). Preserve already landed bodies; no species rewrite.
    Add accessor declarations needed by lane2 to Npc.h, with retail address.
    **Proof:** comfort_idle_weight ordinary initialized donor and empty-template/cine/camera
    refusals; npc_inputs_v7_dispatch latch provenance; NpcInputsV7 arm includes true/false
    actual codec/applier roundtrip (integrator executes). Packet §8 records writer/SAVE evidence.

26. **V7.3 filed mechanism seams**, Npc.cpp setters, NpcSquad.cpp::SetFollowerType,
    Rulebook.cpp::Load, **0x102c2c10 / 0x102c3540 / 0x102c4430 / 0x102c4680**:
    leave typed setters and named real SelectCoverOrKickSchedule/OnObstructingDoor and
    GetFollowerBoss/type reader seams. Label KickHintMechanism, DoorInputMechanism,
    CoverMechanism, FollowerLocomotion and IncapacitationRecovery at these boundaries;
    absent execution/locomotion answers unavailable, never invented success.
    **Proof:** input_allowkickhintuse, input_allowopendoors, input_follower_type and
    map_hub_follower_boss_v7 prove conversion/state/landed readers only. README §6 files full
    kick/door/cover to R3 and squad/follower locomotion to R4; live mechanism records remain
    absent. Faint/Flee still prove real schedules, with additional recovery programs under R5.
    Follower row loading remains required V7 work.

## Dependencies / exact owed lines

Lane2 supplies shared scalar methods (Animating.h), removes NPC playback declaration itself,
and fixes ShouldInvestigate. Lane3 adds the strict arena cases and observation support.
Return the exact Npc.h removals needed by25a (RequestClearSchedule, TakeClearScheduleRequest,
private latch) and Npc.cpp::TakeClearScheduleRequest deletion to the integrator:
lane1 owns these files, but apply at integration after lane2 drops polling.
Report the direct Bloodshield binding signature to the integrator; its service lives in
ElysiumDisciplines.h/.cpp with affected Public/ElysiumPlayer.h declarations/state only.
Report any newly needed voice owner/harness observer declaration outside your manifest.
Integrator records recovery in authored-control/social/stealth oracle sections.

## Coder rules

Write only this lane's listed files, **by absolute path**, in the implementation worktree
the coordinator names. Read files and run read-only tools from **E:\\dev\\elysium-unreal**.
Never build, run the editor/game/tests/arena, bake or commit. Do not edit another checkout.
Put the retail address at **each changed line**, including declarations, adapters, tests and
harness observations. Report a new divergence; do not adopt it.
Never hand-edit generated `*Slots.cpp`: new generated-slot hand bodies go in matching
`*SlotBodies.cpp`; the integrator adds the exact `kernel_verdicts.tsv` row and regenerates.
Shadowed locals, members or globals are compile errors (C4458 / C4459).
Return a report **under 350 words**: files/functions/addresses, changed test names, record
coverage, and exact lines owed by each file outside the lane (or explicitly none).
Do not save it as a file named `report*.md`. Never clean/reset/stash/checkout/delete/remove
worktrees; never write the retail install or the main checkout.
