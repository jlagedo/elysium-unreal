# Brief V5b-3 — slot 360's word behind the Motor seam; the interrupt mask pinned (coder; no build)

Read `README.md` here (§1.1, §1.4, §2 P6 and its last row, §6, §7), `AGENTS.md`, `spec.md`
§ "Standing rules" and § "The known reds" item 4, `docs/vtmb/npc-ai/conditions-and-states.md`
§ "`GetSchedule` `0x102ae920` runs ahead of `SelectSchedule`" (`uv run elysium research section
0x10269d30`). **Re-locate every site by Grep on the function name**; cited lines are hints.
Retail was read for you: `0x10269d30`, `0x1026a0f0`, `0x102ad140`, `0x10387520`, `0x1014f930`,
`0x10149e80`, and the three ranged `.sch` texts. If a body you meet contradicts this brief, stop
and report it.

## Files (only these)

- `Source/ElysiumUE/Private/Substrate/ElysiumNpcBaseMotor.cpp` (`ActiveWeaponCapabilityWord` ~:361 only)
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcBaseMotor.inl` (its declaration comment ~:334-337)
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcMaintain.cpp`
  (`CacheInterruptConditionsForMaintenance`'s last two lines ~:330-331: a comment only)
- `Source/ElysiumUE/Private/Tests/ElysiumNpcKernelMotorTests.cpp`,
  `Source/ElysiumUE/Private/Tests/ElysiumNpcKernelSpeciesTests.cpp`
- new `Source/ElysiumUE/Private/Tests/ElysiumNpcKernelInterruptMaskTests.cpp`

## The job

1. **`ActiveWeaponCapabilityWord` answers the weapon's word** (slot 360, `+0x5a0`: the character
   line's `0x1014f930` and the weapon base `0x10149e80` answer 0; a weapon subclass answers its
   word). The body becomes: no active weapon → 0; else
   `ElysiumNpcCond::CapabilityBits(ElysiumNpcCond::WeaponCapability(*this))` — the read
   `FElysiumNpc::SelectActiveWeaponWord` (`ElysiumNpcSelect.cpp:230`, not your file) already makes;
   `WeaponCapability(const FElysiumCombatCharacter&)` is declared in `ElysiumNpcConditions.h`
   (read, include by its layer path if the file does not have it). Remove "SEAM" from the body
   and the declaration; keep the retail addresses.
   **Its readers, which change with it — read each, edit none** (they are not your files; a
   reader whose surrounding comment now lies goes in your report, exact, for the integrator):
   `FElysiumNpcBase::CapabilitiesGet` (`ElysiumNpcBaseMisc.cpp` ~:43-55, slot 513: `m_afCapability |
   weapon word`, retail's), the aim-activity gates (`ElysiumNpcAnim10.cpp` ~:814,
   `ElysiumNpcHuman.cpp` ~:307), `ElysiumNpcYukie.cpp` ~:80-90, `ElysiumNpcFrenzyShadow.cpp` ~:451,
   `ElysiumNpcRunTaskSpecies.cpp` ~:1097. For each, state in the report which retail address it
   stands for and what it now answers for a .38 and for a bat.
2. **The cache's tail — a comment, no behaviour change** (`CacheInterruptConditions 0x1026a0f0`,
   323 bytes, read whole: it ends at `0x1026a232 RET`). After the two mask copies the listing has
   three statements: slot 453 (`0x1026a211`, `CALL [vtable + 0x714]`), **slot 411** (`0x1026a21b`,
   `CALL [vtable + 0x66c]`; `0x10280fd0` on the whole NPC line, **an empty body**, read), and
   `SetScheduleTestBits(0x75)` (`0x1026a225`, a mask **set** — the port does it in
   `BuildScheduleTestBits`). The port's two lines after the mask —
   `RemoveIgnoredConditions(); // 0x1026a267, slot 459` and
   `Cognition.Conditions.Clear(NpcFreeze); // 0x1026a274` — cite addresses **past the function's
   end**, in bytes (`0x1026a233..0x1026a29f`) that no function of the corpus index covers
   (`vtmb_where 0x1026a267`: no name; `functions.md` has no row between `0x1026a0f0` and
   `0x1026a2a0`). Their retail body is therefore **not read**, and neither caller of the cache
   that was read (`0x102814d0`) runs them after it. You: write that at the two lines (the three
   listed statements with their addresses; "the two lines below cite `0x1026a267` / `0x1026a274`,
   outside `0x1026a0f0`; the bytes are not in the corpus index — for the judge, V5b README §7")
   and change nothing else. Do not delete or move either call.
3. **The mask, pinned** — new `ElysiumNpcKernelInterruptMaskTests.cpp`:
   - `Elysium.Arm.NpcKernelInterruptMask.RangedPrograms`: on a Troika human with an enemy, in
     combat, running `SCHED_TROIKA_STEP_BACK_RANGE_ATTACK1 (0xef)`: the cached mask
     (`ElysiumSchedule::EffectiveInterrupts`) holds `NEW_ENEMY`, `ENEMY_DEAD`, `LIGHT_DAMAGE`,
     `HEAVY_DAMAGE`, `ENEMY_OCCLUDED`, `NO_PRIMARY_AMMO` and slot 453's overlay, and **none of**
     `NOT_FACING_ATTACK 0x61`, `WEAPON_THROUGH_WALL 0x3c`, `TOO_CLOSE_FOR_RANGED 0x08`,
     `TOO_CLOSE_TO_ATTACK 0x5f`; under `SCHED_TROIKA_RANGE_ATTACK1` it holds `0x5f` too; under
     `SCHED_TROIKA_FORCED_RANGE_ATTACK1 (0xf0)` the text's part is `ENEMY_DEAD` alone. Cite the
     three `.sch` files (`Content/ElysiumCorpus/ai/schedules/cai_basenpctroika/`). The test needs
     the deployed schedule texts: if the arm tier's fixtures do not load them, say so and pin the
     program records the fixture does load.
   - `.Break`: with `0x61` and `0x3c` set and `0xef` running, a maintain pass breaks nothing
     (`IsScheduleValid`'s test, `0x10281243..0x10281340`); with `LIGHT_DAMAGE` set it breaks and
     the trace line names it.
   - `.Testers` (`0x10269d30`): no schedule → false with the bit set; the bit in the condition
     set and not in the mask → false; both → true; a bit only in the inverted mask → false.
   If an existing test already pins one of these, cite it in the report and do not duplicate it.
4. **Tests of the word**: `Elysium.Arm.NpcKernelMotor.WeaponCapabilityWord` (a firearm `0x6000`,
   a bat the melee word, unarmed 0; slot 513 carries it). Delete or rewrite the assertions that
   pin the seam's 0 (`ElysiumNpcKernelSpeciesTests.cpp` ~:528 and any in
   `ElysiumNpcKernelMotorTests.cpp`); list them. A test in a file not yours that now fails for
   the same reason goes in the report with its name.

## Not yours

The pre-pass (lane 1 reads `SelectActiveWeaponWord()` directly and does not wait for you), the
weapon (lane 2), `ElysiumSchedule.{h,cpp}`, `ElysiumNpc.cpp` (`BuildScheduleTestBits`: already
retail), `ElysiumNpcSelect.cpp`, the six readers' files, `ShouldMoveAndShoot` (V4o: it reads
`SelectActiveWeaponWord`), the records.

## Rules

Coders never build, launch the editor or run a suite. Retail first: the listing decides, and a
divergence you find is **recorded in your report, not adopted**. A shadowed local or member (C4458 /
C4459) is a compile error here. Query budget: 10 s warns, 60 s stops (never retried as-is, never
widened); never read a file over ~200 KB whole; text through Grep / Read / Glob, not shell. Only
your files; a line needed elsewhere goes in the report, exact. No commit. Report ≤300 words: the
word's body, the six readers with what each now answers, the cache's comment, the mask tests
(and what the fixture could load), tests deleted, cross-lane lines.
