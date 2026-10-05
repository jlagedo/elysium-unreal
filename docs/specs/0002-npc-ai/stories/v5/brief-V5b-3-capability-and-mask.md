# Brief V5b-3 — capability word and corrected combat-interrupt cache

Final against V4c `d0f79574`. Read AGENTS.md, README and `packets-V5b-check.md`.
The corpus identity at `0x102ae920` is **CAI_BaseNPCTroika::PreSelectSchedule**, slot437.
The oracle heading currently says GetSchedule; the integrator owes that label correction.
Research section `0x10269d30` locates the tester contract.

## Files — only these six

- `Source/ElysiumUE/Private/Substrate/ElysiumNpcBaseMotor.cpp`
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcBaseMotor.inl`
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcMaintain.cpp`
- `Source/ElysiumUE/Private/Tests/ElysiumNpcKernelMotorTests.cpp`
- `Source/ElysiumUE/Private/Tests/ElysiumNpcKernelSpeciesTests.cpp`
- new `Source/ElysiumUE/Private/Tests/ElysiumNpcKernelInterruptMaskTests.cpp`

README's A∩B=A∩C=B∩C=∅ proves disjointness. Motor and its tests include V4o/V4c changes;
Species tests also changed under V4c. Do not restore old fixtures.
Already carried out: V4o move-and-shoot body and V4c's clock/animation-stream work; retain them.

## Numbered jobs

1. **ActiveWeaponCapabilityWord**, BaseMotor.cpp and declaration BaseMotor.inl:
   slot360 (+0x5a0), character body `0x1014f930` and weapon base `0x10149e80` answer0;
   subclass weapon answers its capability bits. No active weapon→0, else
   `ElysiumNpcCond::CapabilityBits(ElysiumNpcCond::WeaponCapability(*this))`.
   NpcSelect.cpp::SelectActiveWeaponWord already makes this read; include Conditions.h by
   layer path if needed. Remove the zero-word seam comments.

   Read these consumers but edit none: BaseMisc.cpp::CapabilitiesGet (slot513,
   m_afCapability OR weapon word), NpcAnim10.cpp, NpcHuman.cpp, NpcYukie.cpp,
   NpcFrenzyShadow.cpp, NpcRunTaskSpecies.cpp. Exact port functions/citations:
   BaseMisc.cpp::CapabilitiesGet (`0x1026db30`); NpcAnim10.cpp::PreTranslatePredicate and
   NpcHuman.cpp::HumanNpcEarlyTranslateActivity (`0x103854f0`);
   NpcYukie.cpp::Slot600 (`0x103dd900`); NpcFrenzyShadow.cpp::StartTaskSlot442
   (`0x10376035`); NpcRunTaskSpecies.cpp::FElysiumNpcManBat::RunTaskSlot444 (`0x1038d7cb`).
   Report each affected function's retail
   citation and the firearm0x6000 / melee0x18000 / unarmed0 answer; an obsolete comment is
   an exact owed line for the integrator. Lane1 switches its pre-pass to SelectActiveWeaponWord;
   lane2's count loop does not read this seam.

2. **CacheInterruptConditionsForMaintenance**, NpcMaintain.cpp:
   this is a behavior repair, replacing the former comment-only assignment.
   Retail `CAI_BaseNPC::CacheInterruptConditions 0x1026a0f0` ends at `0x1026a232 RET`.
   Direct PE read: `0x1026a233..23f` is 13 NOP; `0x1026a240..29f` is 96 INT3.
   `0x1026a267 / 0x1026a274` therefore justify neither operation currently cited there.

   In exact order:
   - stamp CacheInterruptTime=Now (`0x1026a16b`);
   - no installed schedule: reset both masks (`0x1026a173..0x1026a196`), return;
   - copy installed program's positive Interrupts, converted to local ordinals, into
     Cognition.CustomInterruptConditions (`0x1026a1a2..0x1026a1d2`);
   - copy its **InvertedInterrupts**, converted to local ordinals, into
     Cognition.InverseInterruptConditions (`0x1026a1d8..0x1026a207`).
     FElysiumScheduleProgram already stores InvertedInterrupts in ScheduleText.h;
     remove the claim “no authored inverse-mask column.” Do not reset this running-arm mask.
   - invoke BuildScheduleTestBits ONCE on the positive cache
     (`0x1026a211 CALL [EAX+0x714]`, slot453);
   - represent the next operation as the verified **empty slot411**
     (`0x1026a21b CALL [EDX+0x66c]`, `0x10280fd0 RET`).
     No new virtual hand body is needed; retain an address comment at this position.
   - add NpcFreeze to the **positive interrupt cache**
     (`0x1026a221 PUSH 0x75 / 0x1026a225 CALL 0x100123f5`,
     thunk→SetScheduleTestBits `0x10269eb0`, OR `0x10269f02`).

   Remove RemoveIgnoredConditions here (slot459 `0x1026d7f0` is a different function).
   Remove Cognition.Conditions.Clear(NpcFreeze); caching does not clear current conditions.
   Do not call EffectiveInterrupts AND BuildScheduleTestBits, which would dispatch twice.
   Use ElysiumScheduleFor for the installed program, with the existing id-space conversion.

   **Owed common-mask lines**, not yours: Schedule.cpp::EffectiveInterrupts must compute
   program-positive →virtual453 →empty411 →add freeze before returning; return empty with
   no program. Npc.cpp::BuildScheduleTestBits and Guard1.cpp::BuildScheduleTestBits must
   remove their folded unconditional freeze insertion once common handling lands.
   The integrator edits those files, their comments and the direct-slot assertion in
   NpcKernelScheduleTests.cpp. Schedule.h's “normal mask only” description also needs updating.

   Species relevance: Guard1 `0x1037cdf0` REPLACES Troika slot453, calling empty base
   `0x10280fb0`; its combat/default arm may add no bits. HEAD currently inserts freeze in
   Guard1's override too; do not claim it is absent today. Move the operation to its retail
   post-virtual position so all overrides and the cache/testers share the rule.
   Troika `0x102ad140`, HumanCombatant `0x10387520` (shared by Cop/Hunter/ProneDialog/
   GhoulCroucher/HumanCombatPatrol/SabbatGunman), Pedestrian `0x103a2980` and
   TzimisceHeadClaw `0x103c16f0` also require the unconditional post-virtual insertion.
   Cine/base NPCs use empty453 and still get the completed mask's freeze bit.

3. **CacheTail arm**, new InterruptMaskTests.cpp:
   `Elysium.Arm.NpcKernelInterruptMask.CacheTail` must pin **all three tail call addresses
   0x1026a211, 0x1026a21b, 0x1026a225** in the test's evidence/assertion labels, plus the
   push `0x1026a221`. Exercise real maintenance caching and EffectiveInterrupts:
   HumanCombatant and Guard1 with a running schedule, current NpcFreeze set, and an ignored
   condition set. Assert positive mask contains freeze AFTER the species overlay, current
   freeze/ignored conditions survive, authored inverse survives separately, and no program
   resets both caches. Use a fixture program with a distinct inverse bit to catch reset/copy
   errors; inverse-only bit must not satisfy HasInterruptCondition.
   Pin direct Guard1 slot453 separately: no folded freeze, no Troika law overlay on its
   combat arm; completed common mask has freeze. The old implementation should fail the
   current-condition and inverse assertions. The integrator owns any existing direct-slot
   test edits outside your files. Do not duplicate already valid coverage.

4. **Ranged masks / testers**, new InterruptMaskTests.cpp:
   - `.RangedPrograms`: Troika human in combat with enemy, 0xef's six authored bits plus
     slot453 overlay and post-virtual0x75, none of 0x61/0x3c/0x08/0x5f; RANGE_ATTACK1 also
     has0x5f; 0xf0 text contains ENEMY_DEAD alone. Cite the three deployed .sch texts under
     Content/ElysiumCorpus/ai/schedules/cai_basenpctroika/.
     Load those texts via the existing schedule fixture; if inaccessible, report the exact
     fixture/load boundary and owed loader line rather than inventing a different program.
   - `.Break` (`IsScheduleValid 0x10281243..0x10281340`): 0x61+0x3c do not break running
     0xef; LIGHT_DAMAGE breaks and names the cause. Include freeze break with the condition
     retained across CacheInterruptConditionsForMaintenance.
   - `.Testers` (`0x10269d30`): no schedule false; condition-only false; positive mask
     AND condition true; inverse-only false. Keep inverse absence behavior in IsScheduleValid
     separate from this positive-only tester.

5. **Capability arm tests**, MotorTests.cpp / SpeciesTests.cpp:
   `Elysium.Arm.NpcKernelMotor.WeaponCapabilityWord`: firearm0x6000, bat0x18000,
   unarmed0; slot513 carries the weapon word. Rewrite assertions that pinned Motor's seam0,
   including the species melee gate. List exact test names. Other-file tests/comments which
   need changes go into the report for the integrator, not into your diff.

## Dependencies and owed lines

Schedule.cpp/h, Npc.cpp, Guard1.cpp, existing NpcKernelScheduleTests.cpp, oracle and verdict
table are integrator-owned. No generated Slots edit, new slot411 body, or new runtime signature
is needed. Preserve the pre-pass/weapon work owned by lanes1/2 and V4o/V4c's ShouldMoveAndShoot.

## Rules

Write only this lane's six files, in the worktree the coordinator names, **by absolute path**.
Read files and run read-only tools from **E:\dev\elysium-unreal**.
Never build, run the editor/game/tests/arena, or commit. Retail first, with the retail address
at every changed runtime line. Report a new divergence; do not adopt it.
Never hand-edit generated `*Slots.cpp`: a hand body goes in the matching `*SlotBodies.cpp`;
the integrator owns the kernel_verdicts.tsv row and regeneration.
Shadowed locals/members/globals are compile errors (C4458 / C4459).
End with a report **under 350 words**: addresses ported, tests changed, and the exact lines owed
by each file outside the lane, including the common-mask relocation.
