# Brief V5b-1 — the ranged pre-pass's reload arms and the template's count (coder; no build)

Read `README.md` here (§1.2, §1.3 slot 280, §2 P1, P2, P4, P7, §3, §6), `CLAUDE.md`, `spec.md`
§ "Standing rules", `docs/vtmb/npc-ai/conditions-and-states.md` § "`FUN_102b8620` — the ranged
weapon pre-pass" (`uv run elysium research section 0x102b8620`). **Re-locate every site by Grep on
the function name**; cited lines are hints. Retail was read for you: `0x102b8620`, `0x102c54c0`,
`0x101d3f10`, `0x10253ab0`. If a body you meet contradicts this brief, stop and report it.

## Files (only these)

- `Source/ElysiumUE/Private/Substrate/ElysiumNpcCombat10_2.cpp` (`RangedWeaponPrePass`'s first
  three arms ~:132-182; `ActiveWeaponWantsReload` ~:116; `RangedDisciplineGate`'s comment ~:85)
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcCombat10.inl` (their declaration comments ~:205-263;
  `RangedReloadPrepCalls`)
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcConditions10.cpp` (`CharTemplateFakeReloadRange`
  ~:521; `ResetFakeReloadCount`'s comment ~:531)
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcConditions10.inl` (their declaration comments ~:129-141)
- `Source/ElysiumUE/Private/Tests/ElysiumNpcKernelCombat10Tests.cpp`,
  `Source/ElysiumUE/Private/Tests/ElysiumNpcKernelConditions10Tests.cpp`

## The job

1. **The template's count — `CharTemplateFakeReloadRange`** (`0x101d3f10`, the template loader's
   `General` block; `0x10207c40` = `GetCharTemplate` through the manager). Replace the seam:
   - `Min = (int32)GeneralFloat("NpcFakeReloadCountMin", 8.0f)` — retail's `__ftol` of a
     `GetFloat` whose default is the dword `0x41000000` (8.0);
   - `Max = (int32)GeneralFloat("NpcFakeReloadCountMax", (float)Min)` — the default is the Min
     **just parsed** (`(float)(int)lVar10`), so an NPC that authors only Min (all 20 shipped
     templates) rolls exactly Min.
   - The template is the NPC's resolved record: take it the way the footstep reader does
     (`ElysiumFootsteps.cpp`, `NpcSource`, `FElysiumClanTemplate::GeneralFloat` — Grep how its
     caller resolves the NPC's template; reuse that accessor, do not write a second resolver).
     **No template** → 8 / 8 (the loader writes both words into every record it loads; the
     footstep reader's own rule, same comment).
   - Truncation is toward zero (`__ftol`); `"6.0"` → 6. Return true when a template answered.
   - `ResetFakeReloadCount` (`0x102c54c0`) is already retail: `RandomInt(Min, Max)` on the
     `NpcSchedule` stream. Correct its comment only ("RandomInt(0, 0) is 0" no longer describes it).
2. **The fake-reload arm — `RangedWeaponPrePass`, `0x102b8626..0x102b86b5`.** In order, as today,
   with two changes:
   - the capability read is **`SelectActiveWeaponWord() & 0x6000`** (`FElysiumNpc`,
     `ElysiumNpcSelect.cpp`: the weapon's slot 360 `+0x5a0`, the real word; read, do not edit).
     `ActiveWeaponCapabilityWord()` is not asked here any more — lane 3 owns that seam;
   - where retail calls `thunk_FUN_102c54c0(this)` (`0x102b867e`), **call
     `ResetFakeReloadCount()`**; `RangedReloadPrepCalls` stays, tallied beside the real call (a
     test reads it), or goes with its test — say which.
   - Unchanged: the cvar gate `debug_allow_fake_reload` (`IsCommand()` false and the int non-zero),
     a live weapon, `FakeReloadCount < 1`; `m_pHintNode (+0x5ddc) == 0` → `0xc4` (line `0x5e8f`),
     else `0xc6` (`0x5e93`). Check the port's hint test against the listing: retail tests the
     pointer for null (`param_1[0x1777] == 0`); the port compares `BaseScheduleHost.HintNode == 0`
     while elsewhere "no hint" is `INDEX_NONE` — if `0` is not the port's "no hint", fix it at the
     line and say so.
3. **The real-reload arm — `0x102b86ba..0x102b87b0`.** `ActiveWeaponWantsReload()` becomes the
   weapon's slot 280: `Weapon->CanReloadMagazine(0)` (`0x10253ab0`; **lane 2 declares and writes
   it** in `ElysiumWeaponClasses.h` — README §3's exact name; you write the call and resolve the
   weapon as the body's other reads do). Keep retail's order: clip `> 0` declines (arm 2) **before**
   slot 280; then the second null check, the reserve (`ActiveWeaponReserveAmmo() < 1` → 0),
   `SEE_ENEMY 0x46` or `NEW_ENEMY 0x54` → `0xc2` (`0x5ecb`), else `0xc3` (`0x5ecf`). These lines
   exist; verify each against the listing and correct a difference at its line.
4. **`RangedDisciplineGate`'s comment** (the seam for `0x101e3f50(&DAT_10739a4c, entity)`): replace
   "an unnamed discipline record" with README §1.5 — a Presence level bit (discipline id 10) of
   `m_iDisciplineFlags2 (+0xeb4)`, set only by `AddDiscFlag 0x1033cfb0` from the discipline
   manager's status apply `0x101e3560`; owner spec 0006. It keeps answering false.
5. **Tests** (README §6): `Elysium.Arm.NpcKernelCombat10.FakeReloadArm`, `.RealReloadArm`;
   `Elysium.Arm.NpcKernelConditions10.ResetFakeReloadCount` rewritten (delete ~:693-696's "… 0").
   Delete or rewrite each `RangedWeaponPrePass` assertion (~:1040-1056) that held only because the
   gate read 0; list them. A fixture template is a `FElysiumClanTemplate` with the `General` key
   set, as the footstep seam tests build one (`Tests/ElysiumFootstepSeamTests.cpp`, read only).

## Not yours

`FElysiumWeapon` (lane 2: slots 280 / 322 / 323 and the per-set count-down — without it the count
never drops; the integrator checks both landed). `ActiveWeaponCapabilityWord`'s body (lane 3).
`ElysiumNpcSelect.cpp`, `ElysiumNpcLifecycle2.cpp` (the spawn's `ResetFakeReloadCount` call is
already there), `ElysiumEntitySlotBodies.cpp`, `ElysiumRulebook.*`, `ElysiumFootsteps.cpp`. The
draw / melee-switch / spacing arms of the pre-pass. The records (`Arena/` is the integrator's).

## Rules

Coders never build, launch the editor or run a suite. Retail first: the listing decides, and a
divergence you find is **recorded in your report, not adopted**. A shadowed local or member (C4458 /
C4459) is a compile error here: name nothing after a member or an outer local. Query budget: 10 s
warns, 60 s stops (never retried as-is, never widened); never read a file over ~200 KB whole; text
through Grep / Read / Glob, not shell. Only your files; a line needed elsewhere goes in the report,
exact. No commit. Report ≤300 words: what you ported (addresses), the hint test's verdict, the
tests added and deleted, cross-lane lines, anything that contradicted the brief.
