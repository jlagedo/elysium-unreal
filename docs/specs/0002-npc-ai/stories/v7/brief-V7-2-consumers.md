# Brief V7-2 — shared scalars and retail behavior consumers

**Start after V5b and V6 commit.** V6 shares ElysiumSchedule.cpp/.h,
ElysiumNpcSenses.cpp and ElysiumNpcBaseAnim.cpp with this lane. Relocate functions
by name on V6's committed code. Fresh arenas/maps use1.0 before Load/initialization;
keep seed/reset order, shared draws, predicates and original behavioral bounds.

Read AGENTS.md, [README.md](README.md), [packets-V7.md](packets-V7.md), especially§4/5,
and Arena/README.md. Main reads only; worktree named by coordinator after predecessors land.

## Files — exhaustive ownership

- `Source/ElysiumUE/Public/ElysiumAnimating.h`
- `Source/ElysiumUE/Private/Substrate/ElysiumAnimatingImpl.cpp`
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcEntityChain.inl`
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcBaseAnim.cpp`
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcBaseAnim.inl`
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcConditions.cpp`
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcConditions10.cpp`
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcConditions.h`
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcSenses.cpp`
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcBaseSounds.cpp`
- `Source/ElysiumUE/Private/Substrate/ElysiumFeed.cpp`
- `Source/ElysiumUE/Private/Substrate/ElysiumFeedSchedules.cpp`
- `Source/ElysiumUE/Private/Substrate/ElysiumStealth.cpp`
- `Source/ElysiumUE/Private/Substrate/ElysiumSchedule.cpp`
- `Source/ElysiumUE/Private/Substrate/ElysiumSchedule.h`
- `Source/ElysiumUE/Private/Tests/ElysiumNpcInvestigateTests.cpp`
- `Source/ElysiumUE/Private/Tests/ElysiumNpcSoundSweepTests.cpp`
- `Source/ElysiumUE/Private/Tests/ElysiumNpcKernelSoundsTests.cpp`
- `Source/ElysiumUE/Private/Tests/ElysiumFeedingTests.cpp`
- `Source/ElysiumUE/Private/Tests/ElysiumFeedTranceTests.cpp`
- `Source/ElysiumUE/Private/Tests/ElysiumScheduleTests.cpp`
- `Source/ElysiumUE/Private/Tests/ElysiumNpcConsumersV7Tests.cpp`

README proves disjointness. Schedule.cpp/.h overlap V5b's **integrator**, not its coders;
BaseAnim.inl has live predecessor changes. Preserve final reload/cache/RNG/sequence-zero behavior.
Npc.cpp/.h belong to lane1; declaration/caller lines there are integrator-owed.

## Numbered jobs

1. **Animating.h / AnimatingImpl.cpp::SetGroundSpeedScalar, GetGroundSpeedScalar,
   SetPlaybackAndSpeedScalar** (new shared methods), **0x1008d0f0 / 0x1008d190 / 0x1008d230**:
   put GroundSpeedScalar=1 and the ONE existing SequencePlaybackRate=1 on FElysiumAnimating;
   remove the old playback member from NpcEntityChain.inl (address+0x6f4).
   Ordered negative→write ground1 THEN playback1; nonnegative/zero/NaN→both supplied value.
   Setter must not reset cycle/sequence/animation time, pick a sequence or draw RNG.
   Use existing World/embodiment SetBodySequencePlaybackRate for an NPC's visual rate without
   restart; absent embodiment is a no-presentation case, not missing scalar storage.
   Preserve base/player APIs; this is the same animating substrate, not an NPC-only helper.

2. **NpcBaseAnim.cpp::WriteSequenceSpeedWords / StudioFrameAdvance / ResetSequenceInfo**,
   **0x10091595 / 0x100915f6 / 0x1008f2fa / 0x1008f306 / 0x10090a23**:
   multiply resolved pose-fan/sequence ground speed by shared GroundSpeedScalar exactly ONCE.
   Playback independently scales cycle/interval movement/events through existing readers.
   ResetSequenceInfo resets playback1, leaves GroundSpeedScalar live. Existing animation reset
   and event state from V4c/V4d stay in order. Add shared accessor declarations in Animating.h;
   update BaseAnim.inl comments without undoing predecessor methods.
   No extra scalar in Motor, MaxWalkSpeed or presentation ground speed.
   If a non-NPC consumer needs an unowned line, report exact reader/address; no guessed rewrite.

3. **Conditions.cpp::ElysiumNpcCond::ShouldInvestigate**, Conditions.h overload,
   **0x102b3270 / 0x102b8cd0**:
   port the WHOLE ordered body in packet§5:
   flags0x4000080→StayEntrenched→null→resolved boss→mutable GetEnemy(slot168)
   →friendly source/type suppression→third-party combat proximity→mode switch0..6.
   Add nullable candidate entry and keep reference callers forwarding (no invalid pointer fixture).
   Use real candidate CONST GetEnemy(slot167), own mutable168, virtual404 and existing origins.
   Friendly record type=InvestigateSound.TypeMask(+0x60e0): nonzero rejects outside combat,
   or at exact type1/0x10; other nonzero combat falls through modes.
   Zero type + combat + candidate enemy nonnull/!=me + my HATE toward it:
   either candidate or that enemy within XY²<=65536 Source² AND |Z|<=80 admits.
   Convert boundaries once to cm; EQUALITY admits (assembly0x102b337a..33b0 and33f0..3427).
   Current enemy only outranks the later arms, not flags/stay/null/boss.
   Unknown mode logs false; don't clamp or infer relationships from classname.
   All substrate inputs exist; don't stub the proximity or friendly-sound branch.

4. **Senses.cpp::FElysiumNpcSenses::CommitBestSound**, **0x102b4090 / 0x102b3d90**:
   retain the seven existing records and condition priority (packet§5).
   Replace return-on-match with selection then one unconditional tail:
   BestSound.Source→Npc.BaseMemory.BestSoundSource and ENTIRE BestSound→InvestigateSound.
   No matching condition retains BestSound but still copies source/mirror. Do not clear it.
   Copy all port record fields, including raw TypeMask, origin, source, times, radii, serial
   and sensitivity metadata; no partial mirror. Call no sound producer or expiry sweep.
   The selector already calls this body; note the redundant world mirror outside your lane
   for integrator removal only after verifying its call order. V10 is not a prerequisite.

5. **Conditions.cpp::ShouldInvestigate / GatherSight / GatherSounds**, **0x10299da0 /
   0x1026a2c0 / 0x102b8cd0**, and
   **FeedSchedules.cpp::ElysiumFeedSchedules::BeginPostFeedTrance**, **0x1033a9e0**:
   route relationship TYPE queries through Npc.IRelationTypeOf(real candidate), the real slot404
   virtual forward. Correct enum/int comparison at the adapter boundary; do not call table and
   compose a second algorithm. Keep table tail in Conditions10::BaseIRelationType and existing
   ResolvePriority readers direct; they are not type-query defects.
   Post-feed relation is VICTIM toward feeder, !=D_HT → existing reset614/source/install0xfb;
   never use feeder's relation or null victim approximation.
   Test own bossLIKE, candidate boss/insane hate, reverse-candidate relation and const167/
   mutable168 asymmetry using existing implementations, not just default table candidates.
   Stealth.cpp::PublishObservers may consult IRelationTypeOf for its existing visual HUD
   projection: provider0x10299da0 is verified, retail HUD reader is NOT.
   Label this INFERRED consistency repair of the already-named presentation modernization;
   report any game-state consequence instead of adopting it. Player+647c blind read is a
   named unavailable source handled by job11; don't fabricate it.

6. **NpcBaseSounds.cpp::ShouldPlayIdleSound**, **0x1027a420 / 0x1027a4a9 / 0x1027a4b0**:
   runtime already tests local0x12f and inclusive weight20, bypasses float. Preserve it.
   Remove stale sentinel-only schedule-space commentary and update corresponding arm fixture.
   Keep the five refusals, normal float-before-roll and shared RNG.
   Read actual bIsBccTargetable +0x1480 at the third refusal, after state/gag and before
   dialog/busy. Remove incorrect +0x7ec/no-member seam wording; initialization is lane1
   job25, not a synthetic default. Preserve local0x12f/weight20 and shared RNG.
   **Proof:** comfort_idle_weight ordinary initialized nonempty-template donor without
   fixture-writing the latch, plus empty-template/cine/camera and other refusal controls.
   Do not use a statistical “many successes” check instead of the actual weight/call evidence.

7. **Feed.cpp::FElysiumCombatCharacter::IsFeedAutoAcceptState / AttemptFeed**,
   **0x10168910**:
   replace IsAutoAcceptDispositionName and MESMERIZED-schedule shortcut with the actual victim
   IDEAL activity word+0xff0 exactly104e/1068/1069/1098. Current activity may disagree.
   Add `int32 FElysiumCombatCharacter::FeedIdealActivityNumber() const` here:
   NPC→existing IdealActivityNumber; non-NPC no source→INDEX_NONE, comment retail+0xff0.
   Public/ElysiumPlayer.h declaration is integrator-owed; do not write it.
   Keep admission, partners, resistance/opposed roll/stealth fallback, V4c transaction and
   post-feed outputs unchanged. Ordinary other activities remain on that existing ladder.
   No missing incapacitation producer, player-animation story or new schedule belongs here.

8. **Schedule.cpp::Tick / ClearSchedule**, Schedule.h::IElysiumScheduleRunner,
   **0x10280d30**:
   remove dead TakeClearScheduleRequest interface/polls, including its clear-on-clear read.
   Existing direct NpcBase::ClearSchedule/ElysiumSchedule::ClearSchedule remains THE exit.
   Six-word zeroing order, preserve-path clear, slot435 argumentzero remain retail.
   Direct StartTask clear falls through/reselects SAME loop; RunTask clear exits and reselects
   next think. Preserve V5b positive/inverse interrupt masks and common freeze tail.
   Npc.cpp TakeClearScheduleRequest + Npc.h Request/private latch deletions are exact owed
   lines for integrator after your polling removal; don't edit lane1 files.

9. **Arm tests**, named owned files, **the addresses in jobs1–8**:
   new family `Elysium.Arm.NpcConsumersV7.` covers all scalar branches, no second multiplier,
   sequence reset preserving ground scalar, ShouldInvestigate full matrix and boundaries,
   full seven-record mirror + no-condition retained tail, composed consumer relations,
   ideal-vs-current/activity-vs-disposition mismatches, four numeric autoaccept IDs and nearby
   nonautomatic COWER2/3/INTO controls.
   NpcInvestigateTests covers guard priority, both modes, friendly sound values, null,
   candidate/enemy geometry independently; every actual consumer remains wired.
   NpcSoundSweepTests verifies complete mirrors, not Source alone.
   NpcKernelSoundsTests replaces stale Local=-1 assertion with real comfort local0x12f and
   actual draw0..20 plus float skip/control. ScheduleTests removes latch-mirroring fixtures,
   replaces with direct-clear same-pass/next-pass evidence. Feeding/FeedTrance fixtures stage
   ideal IDs through real activity setters and retain paired transaction order.
   Leave tests of predecessor capability/reload/cache files to their owners; any cross-file
   repair is exact owed lines, not a manifest expansion.

10. **V7.3 filed consumers and seams**, Senses.cpp::CommitBestSound,
    Conditions.cpp::ShouldInvestigate, **0x102b4090 / 0x102b3270**:
    retain complete cached records and real-source/virtual-enemy/origin accessors for EVERY
    recovered arm now. Sound lifetime is 0002/V10; other producers/VSound are R1.
    Name SoundLifetime and OtherSoundProducers/VSoundSource at the existing stimulus
    receipt/lifetime boundary, without producing fake sounds. These are handoff labels,
    not new shipping inputs or implemented producer claims.
    **Proof:** sound_commit_v7_mirror and investigate_v7_priority exercise every landed
    consumer and conversion/state snapshot; unproduced-sound live witnesses remain absent.
    Full kick/door/cover, squad/follower locomotion and extra incapacitation programs stay
    R3/R4/R5 as README §6; no fixture claims those mechanisms executed.

11. **V7.4 source-accessor boundary**, Conditions10.cpp::TroikaIRelationType and new
    CandidatePlayerRelationHandle647c, **0x10299da0 / 0x10299eaa**:
    replace the anonymous player blind-read fallback with a named accessor returning invalid/
    nothing for the genuinely absent player +0x647c source; candidate NPC still resolves actual
    FollowerBoss. Npc.h declaration is lane1-owned/owed; no invented handle or layout.
    Job7's Feed.cpp::FeedIdealActivityNumber returns INDEX_NONE only for absent non-NPC
    +0xff0; the actual NPC ideal word is required. Packet §8 records the source chains before
    either behavior is extended.
    **Proof:** relationship_composition_v7 and feed_auto_accept_ideal fixtures prove real
    consumer arms only. relationship_player_647c_live is absent under 0002/R4;
    feed_auto_accept_non_npc_live is absent under 0005/3. HUD stays presentation-only.

## Dependencies / owed lines

Lane1 supplies adapters, source type metadata and virtual stealth query. Its multiplier calls
your shared setter. Lane3 supplies finite debug fixture/call probes for the records in README.
Owe Public/ElysiumPlayer.h::FeedIdealActivityNumber declaration (definition in Feed.cpp);
Npc.cpp/.h latch deletion; NpcSelect.cpp::TroikaSelectSchedule redundant HearWorld-only mirror
review; NpcSenses.h stale “unwritten” InvestigateSound comment.
Npc.h::CandidatePlayerRelationHandle647c declaration is lane1-owned/owed.
Any scalar caller outside your files is explicitly owed; targetability is the real landed latch.
Integrator alone writes matching oracle/verdict recovery; no generated Slots edit.

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
