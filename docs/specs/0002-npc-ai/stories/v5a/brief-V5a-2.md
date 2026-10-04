# Brief V5a-2 — the wait before the shot (N2) and the cover tail's weapon read (N1) (coder; no build)

Read `README.md` here (§1 second and third blocks, §2 P4–P6, §6, §7), `CLAUDE.md`, `spec.md`
§ Standing rules, `../v4/README.md` § "Rules for every agent of V4" (they apply to you),
`stories/v1/triage.md` rows N1 and N2 (Grep `| N1 |`, `| N2 |`; do not read the file whole), the
records `Arena/scenarios/combat/ranged_open_fire.json` and `cover_armed.json`.
**Re-locate every site by Grep on the function name**; cited lines are hints.
**Amended after settling packet S2, 2026-10-04** (`../v4/packets-S2.md` item 3: both parsers and
the readers were read; items 1–3 below carry the result, and where this brief and README §1 / §7
disagree, this brief wins).

## Files (only these)

- `Source/ElysiumUE/Private/Substrate/ElysiumNpcStartTask.cpp` (`StartTask19WeaponNextAttackTime` ~:577-581 only; the arm at ~:1664-1686 is already retail)
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcStartTask.inl` (its declaration comment ~:208)
- `Source/ElysiumUE/Private/Substrate/ElysiumItemTable.h`, `ElysiumItemTable.cpp` (three keys beside `Attack_Rate`, ~:192, and `Attack_Rate`'s own default)
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcSchedule.cpp` (`SelectCoverOrKickSchedule`'s ranged-threat block ~:481-495 only)
- `Source/ElysiumUE/Private/Tests/ElysiumNpcKernelStartTaskTests.cpp`, `Tests/ElysiumNpcKernelScheduleTests.cpp`

## The job

1. **What retail has — read, both parsers; nothing to read first and no stop.** The server's
   `WeaponModeDataLoader 0x10259230` and the client's `0x101a3eb0` are identical, per mode
   record:

   | key | offset | default |
   |---|---|---|
   | `Attack_Rate` | `+0x260` | **`1.0`** (`0x102593cd`) |
   | `NPC_Attack_Rate_Min` | `+0x264` | **`2 × Attack_Rate`** as just parsed (`0x102593e5`) |
   | `NPC_Attack_Rate_Max` | `+0x268` | **`3.0 × Attack_Rate`** (`[0x10449258]`) |
   | `NPC_Attack_Rate_Base_Range` | `+0x26c` | **`120.0`** |

   Neighbours, for orientation: `Ammo_Type` index `+0x10c`, `Ammo_Cost` `+0x110`, `Ammo_Fired`
   `+0x114` (default `max(Ammo_Cost, 1)`), `Range` `+0x270`. README §1's mapping stands; its
   "inferred" and its "unrecovered: the server-side parser" (§7) are settled. The staged corpus
   carries the keys (11 item files under `Content/ElysiumCorpus/vdata/items/`, 60 lines): no
   pipeline change.
   **The readers**: `0x102c5730(weapon)` — `v = RandomFloat(+0x264, +0x268)`, then
   **`0x102c5570(rec, v)`**; one caller, `CAI_BaseNPCTroika::StartTask 0x102a1910` (this wait).
   `0x102c5780` / `0x102c57c0` pass `+0x264` / `+0x268` themselves to `0x102c5570` (the burst
   pause, V5 — not yours). **`0x102c5570`** (listing): `BaseRange <= 0` → `(v − Attack_Rate) ×
   1.0`; else the distance to `m_hShootTargetOverride +0x5ba8`, else to the enemy's last known
   position (`GetEnemies()->0x102dfed0`), else **no target → `(v − Attack_Rate) × sqrt(1.0 /
   BaseRange)`**; `dist <= 0` → `(v − Attack_Rate) × dist`; else `(v − Attack_Rate) × sqrt(dist /
   BaseRange)`.
2. **The item keys.** `NPC_Attack_Rate_Min`, `NPC_Attack_Rate_Max`, `NPC_Attack_Rate_Base_Range`
   parsed into the mode record beside `AttackRate`, with the defaults of item 1's table, each
   cited at its line; the two rate defaults are computed from `Attack_Rate` **as just parsed**
   (so a file that states `Attack_Rate 0.4` and no `NPC_…` keys gets 0.8 / 1.2). Units: seconds,
   seconds, Source units.
   **`Attack_Rate`'s own default is 1.0, not 0.0 — an old port bug, fixed here.**
   `ElysiumItemTable.cpp` ~:192 parses `Attack_Rate` with default 0.0; retail's is 1.0
   (`0x102593cd`). Change it and cite the address. One caution S2 raised, which you check and do
   not fix outside your files: `ElysiumWeaponClasses.cpp` reads `AttackRate == 0` as "absent".
   Grep every reader of the mode record's `AttackRate` for a zero test and write each exact line
   in your report (file, function, what it does with 0), so the integrator sees what the
   corrected default moves; a zero-test left behind is then dead for a parsed record and is the
   integrator's line, not yours.
3. **`StartTask19WeaponNextAttackTime(bSecondary)`** as `0x102a33a2` + `0x102a33b0`:
   - `0x10252450(weapon, i)`: `i == 0` → the active weapon's `NextPrimaryAttackTime`
     (`+0x730`), `i == 1` → `NextSecondaryAttackTime` (`+0x734`);
   - plus `0x102c5730`: `v = RandomFloat(Min, Max)` on the `NpcSchedule` stream (the draw is
     taken on every call, as retail's), then `0x102c5570` — call
     `FElysiumNpc::ScaleWeaponBurstPause(v, AttackRate, BaseRange, distance, bHasTarget)`
     (`ElysiumNpcConditions10.cpp`: `(v − base) × scale`) with
     the distance of `FElysiumNpc::ShootTargetDelta` (`ElysiumNpcPositions.cpp`:
     `m_hShootTargetOverride +0x5ba8`, else the enemy's last known position, else no target).
     Check the two helpers' signatures by Grep; you call them, you do not edit them.
     **Check `ScaleWeaponBurstPause` against item 1's `0x102c5570`, arm by arm**: `BaseRange <=
     0` → scale 1.0; **no target → `sqrt(1.0 / BaseRange)`**; **`dist <= 0` → the multiplier is
     `dist` itself** (not `sqrt`); else `sqrt(dist / BaseRange)`. A difference is not yours to
     edit (`ElysiumNpcConditions10.cpp` is no lane's in this wave): write the exact line and the
     corrected one in your report for the integrator.
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
   address; and the parser's defaults (`0x10259230`): no keys → `Attack_Rate` 1.0, Min 2.0, Max
   3.0, Base_Range 120.0; `Attack_Rate 0.4` alone → Min 0.8, Max 1.2; stated keys win — in the
   item-table test that already covers `Attack_Rate` if one exists in your files, else inside
   `ElysiumNpcKernelStartTaskTests.cpp`, and say which. Rewrite `ElysiumNpcKernelStartTaskTests.cpp` ~:652-657 (it pins the seam). Delete any
   schedule-test assertion that expects `0xa4` only because the stub fired; list them.
6. **Check, do not edit**: `RunTask`'s arm for `0xb0` / `0xb1` (`ElysiumNpcRunTask.cpp`
   `RunTaskSlot444`, ~:531 and the wait's completion test) completes on `m_flWaitFinished <=
   curtime`. If it does not, the exact line goes in your report.

## Not yours

`GatherAttackConditions` and `ElysiumNpcConditions.{h,cpp}` (lane V5a-1), the weapon classes
(`ElysiumWeaponClasses.*`: you read the stamps, you do not change who writes them),
`ElysiumNpcConditions10.cpp` (`UpdateBurstShootPause`'s seam `ActiveWeaponBurstPauseWords` reads
the same two words through `0x102c5780` / `0x102c57c0` and answers 0 / 0 today; wiring it to your
three fields would give V4o's overlay its burst pause — one function, in nobody's lane: name it in
your report with the line, do not write it), `ElysiumNpcRunTask.cpp`, the rest of
`ElysiumNpcSchedule.cpp`, slot 363 (A3).

Wave check ([V5a-1, V5a-2, A3], re-checked after S2): S2 added no file to this lane
(`ElysiumItemTable.{h,cpp}` was already yours and is in neither V5a-1's list —
`ElysiumNpcConditions.{h,cpp}`, `ElysiumNpcBaseClosure.cpp`, its two tests — nor A3's —
`ElysiumCombatCharacterSlots.cpp`, `ElysiumCombatCharacterSlotBodies.cpp`,
`ElysiumNpcSenses.{h,cpp}`, `ElysiumPlayerEntity.cpp`, its test). `ElysiumWeaponClasses.cpp` and
`ElysiumNpcConditions10.cpp` are in no lane of the wave: lines for them go to the integrator.

## Rules

Coders never build, never launch the editor, never run the arena or a suite. Retail first: follow
the listing, cite the address at every line. A divergence is recorded in your report, not adopted.
Query budget: 10 s warns (log to `$ELYSIUM_WORK_ROOT/logs/slow-queries.tsv`), 60 s stops — never
retried as-is or widened. Never read a file over ~200 KB whole. Text through Grep / Read / Glob.
`research where <addr>` before searching `docs/`. No record under `Arena/` is yours. Do not
commit. Report ≤300 words: what you ported (addresses), the four defaults as written, every
reader of `AttackRate` that tests 0, `ScaleWeaponBurstPause`'s check against `0x102c5570`, tests
added and deleted, cross-lane lines, what stays unrecovered.
