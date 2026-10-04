# Brief V5a-2 — the wait before the shot (N2) and the cover tail's weapon read (N1) (coder; no build)

Read `README.md` here (§1 second and third blocks, §2 P4–P6, §6, §7), `CLAUDE.md`, `spec.md`
§ Standing rules, `../v4/README.md` § "Rules for every agent of V4" (they apply to you),
`stories/v1/triage.md` rows N1 and N2 (Grep `| N1 |`, `| N2 |`; do not read the file whole), the
records `Arena/scenarios/combat/ranged_open_fire.json` and `cover_armed.json`.
**Re-locate every site by Grep on the function name**; cited lines are hints.

## Files (only these)

- `Source/ElysiumUE/Private/Substrate/ElysiumNpcStartTask.cpp` (`StartTask19WeaponNextAttackTime` ~:577-581 only; the arm at ~:1664-1686 is already retail)
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcStartTask.inl` (its declaration comment ~:208)
- `Source/ElysiumUE/Private/Substrate/ElysiumItemTable.h`, `ElysiumItemTable.cpp` (three keys beside `Attack_Rate`, ~:192)
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcSchedule.cpp` (`SelectCoverOrKickSchedule`'s ranged-threat block ~:481-495 only)
- `Source/ElysiumUE/Private/Tests/ElysiumNpcKernelStartTaskTests.cpp`, `Tests/ElysiumNpcKernelScheduleTests.cpp`

## The job

1. **Read first** (≤3 queries): `vtmb_asm 102c5730` (20 lines), `research section 0x102c5570`
   (`shape.md` :2110), and `vtmb_code 101a3eb0` in `client.dll` around `NPC_Attack_Rate` (page
   with `from_line`; find the three keys' defaults and which field each lands in). If the client
   parser contradicts README §1's mapping (`+0x260 Attack_Rate`, `+0x264 Min`, `+0x268 Max`,
   `+0x26c Base_Range`), **stop and report**.
2. **The item keys.** `NPC_Attack_Rate_Min`, `NPC_Attack_Rate_Max`, `NPC_Attack_Rate_Base_Range`
   parsed into the mode record beside `AttackRate`, with retail's defaults as you read them
   (cite the address at each). Units: seconds, seconds, Source units.
3. **`StartTask19WeaponNextAttackTime(bSecondary)`** as `0x102a33a2` + `0x102a33b0`:
   - `0x10252450(weapon, i)`: `i == 0` → the active weapon's `NextPrimaryAttackTime`
     (`+0x730`), `i == 1` → `NextSecondaryAttackTime` (`+0x734`);
   - plus `0x102c5730`: `v = RandomFloat(Min, Max)` on the `NpcSchedule` stream (the draw is
     taken on every call, as retail's), then `0x102c5570` — call
     `FElysiumNpc::ScaleWeaponBurstPause(v, AttackRate, BaseRange, distance, bHasTarget)`
     (`ElysiumNpcConditions10.cpp`, already the listing's arithmetic: `(v − base) × scale`) with
     the distance of `FElysiumNpc::ShootTargetDelta` (`ElysiumNpcPositions.cpp`:
     `m_hShootTargetOverride +0x5ba8`, else the enemy's last known position, else no target).
     Check the two helpers' signatures by Grep; you call them, you do not edit them.
   - Which mode's data: `0x102517e0(weapon)` resolves the weapon's current mode record (matched
     on weapon `+0x848`); use the weapon's active mode as the port resolves it and say how.
   The function is `const` today; the draw makes it not: change the declaration in the `.inl`.
4. **The cover tail (N1)**, `0x102b78a2..0x102b78ee`: replace the stub with the read — the
   resolved enemy's `AsCombatCharacter()` (retail's `+0x9c`), its active weapon, the capability
   word `& 0x6000`: `ElysiumNpcCond::WeaponCapability(const FElysiumCombatCharacter&) ==
   ECapability::Ranged` → `bNoRangedThreat = false`. No weapon, a melee weapon, or an enemy that is
   no combat character: `true`. Delete the `ElysiumStub::Fired` line. Note at the line that
   `WeaponThrown` taking the ranged branch is the reader's existing "chosen, not recovered".
5. **Tests**: `Elysium.Arm.NpcKernelStartTask19.WaitAttackTime` and
   `Elysium.Arm.NpcKernelSchedule.CoverTailRangedThreat` (README §6), each assertion naming its
   address. Rewrite `ElysiumNpcKernelStartTaskTests.cpp` ~:652-657 (it pins the seam). Delete any
   schedule-test assertion that expects `0xa4` only because the stub fired; list them.
6. **Check, do not edit**: `RunTask`'s arm for `0xb0` / `0xb1` (`ElysiumNpcRunTask.cpp`
   `RunTaskSlot444`, ~:531 and the wait's completion test) completes on `m_flWaitFinished <=
   curtime`. If it does not, the exact line goes in your report.

## Not yours

`GatherAttackConditions` and `ElysiumNpcConditions.{h,cpp}` (lane V5a-1), the weapon classes
(`ElysiumWeaponClasses.*`: you read the stamps, you do not change who writes them),
`ElysiumNpcConditions10.cpp` (`UpdateBurstShootPause`'s seam reads the same two words: V5, name it
in your report), `ElysiumNpcRunTask.cpp`, the rest of `ElysiumNpcSchedule.cpp`, slot 363 (A3).

## Rules

Coders never build, never launch the editor, never run the arena or a suite. Retail first: follow
the listing, cite the address at every line. A divergence is recorded in your report, not adopted.
Query budget: 10 s warns (log to `$ELYSIUM_WORK_ROOT/logs/slow-queries.tsv`), 60 s stops — never
retried as-is or widened. Never read a file over ~200 KB whole. Text through Grep / Read / Glob.
`research where <addr>` before searching `docs/`. No record under `Arena/` is yours. Do not
commit. Report ≤300 words: what you ported (addresses), the defaults you read, tests added and
deleted, cross-lane lines, what stays unrecovered.
