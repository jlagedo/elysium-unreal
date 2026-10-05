# Brief V5b-1 — ranged pre-pass reload arms and template count

Final against V4c `d0f79574`, after V4o `64895278`. Read AGENTS.md, this README and
`packets-V5b-check.md`; use research where/section before oracle searches.
Retail entries: `0x102b8620`, `0x102c54c0`, `0x101d3f10`, `0x10253ab0`.
Locate sites by function name. V4c's shared stream/equal-bound behavior is already carried out;
reuse it. V4o/V4c's event shot, NPC guard and fresh-stage clock need no work here.

## Files — only these six

- `Source/ElysiumUE/Private/Substrate/ElysiumNpcCombat10_2.cpp`
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcCombat10.inl`
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcConditions10.cpp`
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcConditions10.inl`
- `Source/ElysiumUE/Private/Tests/ElysiumNpcKernelCombat10Tests.cpp`
- `Source/ElysiumUE/Private/Tests/ElysiumNpcKernelConditions10Tests.cpp`

README's A∩B=A∩C=B∩C=∅ proves these do not overlap another lane. Conditions10.cpp and its
tests include V4c changes; read their current bodies rather than importing the older version.

## Numbered jobs

1. **CharTemplateFakeReloadRange**, Conditions10.cpp, loader `0x101d3f10`:
   use **`FootstepTemplate.Get()`**, the already resolved FElysiumClanTemplate stored by
   FElysiumNpc::ApplyResolvedTemplate (Npc.cpp) and passed to the footstep NpcSource. No edit to Npc.cpp/h,
   no second template resolver.
   Min=(int32)GeneralFloat("NpcFakeReloadCountMin",8.0f), truncating toward zero
   (`0x101d4394 / 0x101d43a3 / 0x101d43b1`).
   Max=(int32)GeneralFloat("NpcFakeReloadCountMax",(float)Min)
   (`0x101d43ad..0x101d43c8`): default is the just-truncated Min.
   Template present → true; absent → 8/8, false, explicitly the loader-default stand-in.
   Update Conditions10.inl's false/zero seam description.
   **ResetFakeReloadCount**, same file, `0x102c54c0`: preserve the unconditional write,
   V4c's `Max <= Min ? Min : NpcSchedule.RandRange(Min,Max)` (`vstdlib.dll 0x10002e60`).
   Correct its missing-template comment to 8/8; do not reintroduce a draw for equal bounds.

2. **RangedWeaponPrePass**, Combat10_2.cpp, `0x102b8626..0x102b86b5`:
   preserve cvar enabled → live weapon → ranged word &0x6000 → FakeReloadCount<1.
   Read **SelectActiveWeaponWord()**, the existing real slot-360 read in NpcSelect.cpp, rather
   than Motor's seam. At `0x102b867e` call ResetFakeReloadCount before choosing the schedule.
   Keep RangedReloadPrepCalls as a diagnostic tally beside the real call; update its declaration
   comment in Combat10.inl. At `0x102b8692` test **BaseScheduleHost.HintNode == INDEX_NONE**:
   retail's null pointer maps to this existing sentinel, not numeric zero.
   No hint →0xc4 (retail diagnostic 0x5e8f), hint →0xc6 (0x5e93).
   Update the block/declaration comments which still describe Motor's zero read or no reroll.

3. **ActiveWeaponWantsReload / RangedWeaponPrePass**, Combat10_2.cpp,
   `0x102b86ba..0x102b87b0`, slot 280 `0x10253ab0`:
   resolve FElysiumWeapon via the current active-entity/item downcast and call
   **CanReloadMagazine(0)**, declared by lane 2 in ElysiumWeaponClasses.h.
   Include that header by layer path if needed.
   Keep clip>0 declining BEFORE this gate, then second active-weapon null check, reserve>=1,
   SEE_ENEMY (0x46) OR NEW_ENEMY (0x54) →0xc2 (0x5ecb), neither →0xc3 (0x5ecf).
   Preserve draw/melee/spacing tail unchanged. Update Combat10.inl's reload-seam comments.

4. **RangedDisciplineGate**, Combat10_2.cpp and Combat10.inl, `0x101e3f50 / 0x1033d940):
   name Presence id 10 in m_iDisciplineFlags2 +0xeb4. Its bit is applied by AddDiscFlag
   `0x1033cfb0` from discipline status apply `0x101e3560` / `0x101dfc20`; spec 0006 owns
   the cast/status source. Keep the hook answering false until that source lands.

5. **Arm tests**, the two owned test files:
   - `Elysium.Arm.NpcKernelCombat10.FakeReloadArm` (`0x102b8626 / 0x102b867e / 0x102b8692`):
     ranged count<1 rerolls; INDEX_NONE selects 0xc4; valid hint index **0** selects 0xc6;
     count>=1, disabled cvar, no weapon and melee skip fake reload. Keep reserve/clip staging
     separate so skipped fake arms are not confused with real reload.
   - `.RealReloadArm` (`0x102b86e5 / 0x10253ab0`): clip0 + may reload + reserve>=1 chooses
     0xc2 under either SEE_ENEMY or NEW_ENEMY and 0xc3 under neither; reserve0 declines,
     clip>0 declines first. Arrange positive FakeReloadCount to isolate real reload.
   - rewrite `Elysium.Arm.NpcKernelConditions10.ResetFakeReloadCount`
     (`0x101d4394..0x101d43c8 / 0x102c54c0 / vstdlib.dll 0x10002e60`):
     Min6/no Max→6, no keys→8, absent template→8, fractional values truncate toward zero,
     Min4/Max9 bounded; equal/inverted bounds consume no shared RNG draw.
     Use FElysiumClanTemplate General keys as in the read-only FootstepSeam tests.
   Rewrite assertions which pinned the zero capability/range seams, preserving V4c's
   shared-stream fixture checks. List changed/deleted assertions by test name in your report.

## Dependencies and owed lines

Lane 2 owns the weapon declaration and per-set decrement; reroll, template and count-down land
together. Lane 3 owns Motor and maintenance. You do not touch NpcSelect.cpp, NpcLifecycle2.cpp
(the spawn ResetFakeReloadCount call already exists), EntitySlotBodies.cpp, Rulebook, Footsteps,
generated Slots, or Arena. If their comments need an exact line, report it for the integrator.
There is no new cross-lane edit for the template accessor or hint sentinel.

## Rules

Write only this lane's six files, in the worktree the coordinator names, **by absolute path**.
Read files and run read-only tools from **E:\dev\elysium-unreal**.
Never build, run the editor/game/tests/arena, or commit. Retail first, with the retail address
at every changed runtime line. Report a new divergence; do not adopt it.
Never hand-edit generated `*Slots.cpp`: a hand body goes in the matching `*SlotBodies.cpp`;
the integrator owns the kernel_verdicts.tsv row and regeneration.
Shadowed locals/members/globals are compile errors (C4458 / C4459).
End with a report **under 350 words**: addresses ported, tests changed, and the exact lines owed
by each file outside the lane, or explicitly none.
