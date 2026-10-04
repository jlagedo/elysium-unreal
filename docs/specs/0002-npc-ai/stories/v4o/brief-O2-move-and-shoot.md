# Brief O2 — V4o: the move-and-shoot overlay `0x102e8560` (coder; no build)

Read `README.md` here (§1 "The wire", §2 P5, §4 "Shared names", §6, §7),
`docs/vtmb/animation_events.md` → "The move-and-shoot overlay, arm by arm"
(`uv run elysium research section 0x10098cd0`), `docs/vtmb/npc-ai/senses.md` § "`StartTaskOverlay`
`0x10288710`", `docs/vtmb/npc-ai/shape.md` § "`OverrideMove`, `ShouldMoveAndShoot`…". After V4a's
commit **and V4b's** (amended after settling packet S3, 2026-10-04: `../v4/packets-S3.md` items
1, 2 and 4 are the reads behind items 0, 3 and 4 below; where this brief and the README's older
text disagree, this brief wins). Re-locate every site by Grep on the function name.

**Final for the code as landed (2026-10-04, after V5a, V4a, V4b and V11; `../v4/packets-S12.md`).**
Where an item below and this block disagree, this block wins.

- **Slot 575 is still the seam bug** (item 0 stands): `FElysiumNpc::ShouldMoveAndShoot`
  (`ElysiumNpcMotor.cpp` :397-424) tests `ActiveWeaponCapabilityWord() & GWeaponMoveShootMask`,
  and `FElysiumNpcBase::ActiveWeaponCapabilityWord` (`ElysiumNpcBaseMotor.cpp` :361) still
  answers 0. Replace that one read with `SelectActiveWeaponWord()` (`ElysiumNpcSelect.cpp` :230).
  The rest of the body (the enemy, `MOVE_FACE_ENEMY`, `CapabilityWord >> 6 & 1` for
  `0x10278c60`) is retail already.
- **The body's turn while it runs has landed (V4b B2)** — nothing for you to build:
  `FElysiumNpc::MotorMoveFacing(const FMotorMoveFacingGoal&)` (`ElysiumNpcMotor10.cpp` :483, slot
  18 `0x102e19e0`) blends the move direction with the facing queue through
  `FElysiumNpcBase::MotorFacingQueueBlend(float& OutInfluence, double& OutRangeCm)`
  (`ElysiumNpcBaseFacing.cpp` :131). Your step 9 feeds that queue with the existing slot 517
  `AddFacingTarget(FElysiumEntity*, const FVector&, float, float, float)` — `(enemy, lkp, 1.0,
  0.8, 0)` — and that is the whole wire: with it the running body turns toward the enemy and
  `0x61` clears. Do not call the motor yourself.
- **The activity swap reaches the body through the landed commit**: `NavMoveNormalPass`
  (`ElysiumNpcBaseMotor.cpp`) runs slot 310 `SetActivity(Navigator.GetMovementActivity())` every
  move step (`0x102efb80`). `UpdateMoveShootActivity` only writes the navigator's word
  (`0x102ee250`); the next step commits the aim twin. `MoveNormal`'s restore arm
  (`0x102efc11`, read whole in S12 d.1 item 5) undoes a commit only for a body that stood on a
  zero-speed sequence and did not move: it never undoes your swap on a running body, and it is
  not yours to port.
- **The burst pause is no longer a seam** (V5a): `FElysiumNpc::ActiveWeaponBurstPauseWords`
  (`ElysiumNpcConditions10.cpp` :464) reads `NPC_Attack_Rate_Min` / `_Max` through `0x102c5570`,
  and slot 419 `UpdateBurstShootPause` writes `BurstShootPauseMin` / `Max`, which
  `StartTaskOverlay` (`ElysiumNpcBaseSenses10.cpp` :706) already hands to
  `ArmMoveAndShootOverlay(PauseMin, PauseMax)`. The re-arm draws `RandomFloat(PauseMin,
  PauseMax)` on real numbers: bursts pause. The "Not yours" sentence below that says 0 / 0 is void.
- **`0x4f` is V5a-1's now** (`GatherAttackConditions 0x1026dd10` whole, with slot 562 twice and
  the friend-in-the-line timers, V5a-3). `CanAimAtEnemy`'s ungathered arm calls the landed
  `GatherEnemyConditions`; you add nothing to the gather.
- **Staging — who this is for** (S4 item d): on the two witness maps the overlay is reachable
  only on `sp_tutorial_1`, by **nine placed rows**: `thug_3` (`item_w_thirtyeight`); `Hunter1`,
  `sentry3`, `sabbat_redshirt_1`, `_2`, `_5`, `sabbat_redshirt_2_proxy` (`item_w_mac_10`);
  `mercenary_upstairs` (`item_w_ithaca_m_37`); `condotierre_upstairs` (`item_w_steyr_aug`). The
  hub has none with a ranged primary. `cover_move_shoot`'s `arena_gunman` stands for `thug_3`
  (same class `npc_VHumanCombatant`, same weapon, `stattemplate TutorialThug`). So the burst pair
  (item 2, `+0x3a4` / `+0x3a8`) must be right for **those four weapon records** — the .38, the
  MAC-10, the Ithaca, the Steyr: look each up in the weapon record the port parses and state the
  four pairs in your report; a record with no such key is the named seam answering 0 (then
  `RandomInt(0, 0)`: one shot per burst).
- **The layer you push** (S12 a.1): `TranslateActivity(0x1a)` resolves to the weapon's
  `*_attack_layer` — a snap, non-looping clip with 3031 at cycle 0.0 (the .38's is 0.4667 s,
  shorter than its 0.8 s rate, so slot 270 `HasLayer` is false again by the next shot).
  `flamet_attack_layer` authors no 3031: a flamethrower's overlay pushes and fires nothing —
  retail's, no arm of yours.

## Files (only these)

- `Source/ElysiumUE/Private/Substrate/ElysiumNpcBaseMaintain.cpp` (`FElysiumNpcBase::RunTaskOverlay` only)
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcBaseSenses10.cpp`, `ElysiumNpcBaseSenses10.inl`
  (`FMoveAndShootOverlay`, `ArmMoveAndShootOverlay`, `DisableMoveAndShootOverlay`, the new
  declarations)
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcBaseMoveAndShoot.cpp` (new: the three bodies)
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcMotor.cpp` (`FElysiumNpc::ShouldMoveAndShoot`
  only — item 0)

**Not** `ElysiumNpcKernelTunables.h`: it is generated from
`research/tooling/ghidra/driver/kernel_tunables.tsv` ("Do not hand-edit"). The row item 4 needs is
a **cross-lane line**: write it exactly in your report and code against the name
`EConVar::DebugAllowMfTurn`; the integrator adds the row and regenerates before the build.
**Not** `ElysiumNpcBaseMotor.cpp` (the `ActiveWeaponCapabilityWord` seam's home; V4b lane B2's
file, and V4c C1's).

Wave check (O1 / O2 / O3, re-checked after S3): `ElysiumNpcMotor.cpp` is in neither O1's list nor
O3's; no file of yours is.
- `Source/ElysiumUE/Private/Tests/ElysiumNpcKernelSenses10Tests.cpp` (the slot-445 block only)
- `Source/ElysiumUE/Private/Tests/ElysiumNpcKernelMoveAndShootTests.cpp` (new)

## The job

Read the four bodies in the listing first (`vtmb_code 102e8270`, `102e83e0`, `102e84a0`,
`102e8560`; `vtmb_asm` where a constant is folded). Then:

0. **Slot 575 — the fix without which nothing below ever runs** (S3 item 4.1). Retail slot 575
   `0x102bf4a0` tests the active weapon's slot 360 capability word `& 0x6000`. The port's
   `FElysiumNpc::ShouldMoveAndShoot` (`ElysiumNpcMotor.cpp`) tests
   `ActiveWeaponCapabilityWord() & 0x6000`, and that function (`ElysiumNpcBaseMotor.cpp`) is **a
   seam returning 0**: `StartTaskOverlay` always takes the disable (`+0x18 = FLT_MAX`) and
   `0x102e8560` returns at step 1 — the overlay can never arm. **Read the word through
   `FElysiumNpc::SelectActiveWeaponWord()`** (`ElysiumNpcSelect.cpp`, the real word; the read V11
   prescribes for `Slot600`). **Do not change the seam**: answering there moves Combat10's reload
   pre-pass, and its file is not yours. Cite `0x102bf4a0` at the line and say in your report
   that the seam still answers 0 for its other callers.
1. **The words**: `FMoveAndShootOverlay` gains `bMovingAndShooting` (`+0x10`), `MoveShots`
   (`+0x14`), `MinBurst` / `MaxBurst` (`+0x1c` / `+0x20`), `InitialDelay` (`+0x2c`, 0 from
   `0x1027c300`); `UpdateCalls`, `Arms`, `Disables` go with their assertions.
2. **`ArmMoveAndShootOverlay` whole** (`0x102e8270`): state 4, no weapon, or
   `SelectHeaviestSequence(TranslateActivity(0x11))` / `(0x15)` below 0 → the disable; the burst
   pair from the weapon data (`+0x3a4` / `+0x3a8` — Grep the weapon record for the two fields; a
   record with no such field is a seam named for the offset, answering 0, reported);
   `MoveShots = RandomInt(MinBurst, MaxBurst)` on the `NpcSchedule` stream;
   `NextShotTime = curtime + InitialDelay`.
3. **`RunMoveAndShootOverlay()`** = `0x102e8560`, the nine steps of the doc section in their order,
   with `CanAimAtEnemy` = `0x102e83e0` and `UpdateMoveShootActivity(bool)` = `0x102e84a0` as their
   own functions. **`CanAimAtEnemy`'s first test** (S3 item 2): `m_bConditionsGathered +0x5ca4`
   clear (cleared by `RunAI 0x1026f110`, set by `GatherConditions 0x1026ec30`) → slot 481 =
   **`CAI_BaseNPC::GatherEnemyConditions 0x10270b20`** — the overlay gathers the enemy
   conditions itself (the LOS debounce, `SEE_ENEMY`, `ENEMY_TOO_FAR`, and through slots 564 /
   561 the attack conditions) when the think has not gathered yet, **before** it reads `0x4f`.
   The slot answers nothing; its effect is the condition set. The port binds that byte to
   `Cognition.GatheredAt`: read it as `ElysiumNpcMaintain.cpp` does (Grep `GatheredAt` there).
   `0x4f` itself is read with plain `HasCondition` (`0x10269b30` is a forwarder), not the
   interrupt-masked read `0x10269d30`; the port already raises it
   (`ElysiumNpcCond::GatherAttackConditions`) — nothing of V5 is needed. Every slot it calls exists in the port — find each by address
   (`uv run elysium research where "slot 478" "slot 560" "slot 481" "slot 517" "slot 381"
   0x102ee3f0 0x102ee250 0x102dfed0`): `BestEnemy`, `ClearAttackConditions`, `SetEnemy`,
   `SetState(2)`, `NavIsGoalActive`, the navigator's movement activity getter and setter,
   `TranslateActivity`, `WeaponSetActivity`, `AddFacingTarget`. The layer calls are O1's
   `AddGesture(int32, bool)` and `HasLayer(int32)`. Slots 557 / 558 are `return 1` / empty on every
   class: call the existing slots. The fire rate is the weapon's slot 332 (`0x10254410`: the weapon
   data's `+0x260` through the owner's scale `0x1033d940`); the constants are 0.3 (`0x1047b868`)
   and 0.1 (`0x104493d0`).
4. **The `0x47` / `0x48` gesture arm** (step 7) — **settled (S3 item 1); port the gate, no
   seam.** The cvar is **`debug_allow_mf_turn`**, default `"0"`, flags 0, object `0x10923cf0`
   (`0x10923cf4` is its parent pointer; help: "If this is on, NPCs will turn to look behind them
   periodically when they run for cover."). The gate, in order (`0x102e8626..0x102e86a1`):
   `IsCommand()` false; the int value (`+0x2c`) non-zero; the Troika self-cast `+0x98` non-null;
   `|GetPoseParameter("move_yaw")| > 90.0` (double `0x1044e668`, strict); `RandomInt(0,
   ftol((190.0 − |yaw|) × 0.25)) < 5` (doubles `0x1049d910` = 190.0, `0x10449260` = 0.25) on the
   `NpcSchedule` stream. The side pick compares the dot with float `0.0` (`0x104454c4`): `<= 0`
   → `0x48`, else `0x47`. Read the cvar through a new tunables row **`DebugAllowMfTurn`** —
   `debug_allow_mf_turn` "0", object `0x10923cf0`, the shape of `DebugAllowMoveFacing`'s row —
   which you write in your report (see Files). **With the shipped default the gate is closed: no
   gesture is pushed and no `RandomInt` draw is taken** — the draw sits behind the cvar test,
   exactly there, and the arm test says so. S3 already recorded the values in
   `docs/vtmb/animation_events.md`; you edit no doc.
5. **`RunTaskOverlay`** `0x10289c90`: slot 529 true → `RunMoveAndShootOverlay()`.
6. **Tests** `Elysium.Arm.NpcKernelMoveAndShoot.Arm`, `.CanAim` (ungathered → slot 481 runs
   before `0x4f` is read; gathered → it does not), `.MoveActivity`, `.Run` (README §6; the
   gesture arm with `debug_allow_mf_turn` 0: no gesture, **no draw** — the stream's next value
   is unchanged), `.Slot575` (`0x102bf4a0`: a `0x6000` active weapon passes through
   `SelectActiveWeaponWord()`; no weapon or a melee word does not). In `ElysiumNpcKernelSenses10Tests.cpp` delete only the assertions that pin the counters or
   the arm taken without the sequence test; list them.

## Not yours

The layer table, its advance and dispatch (O1); the weapon's handler (O3); `StartTaskOverlay`
and slot 529 (retail already — do not rewrite; **slot 575 is not: item 0**); the
`ActiveWeaponCapabilityWord` seam (`ElysiumNpcBaseMotor.cpp`); the attack conditions (already
raising `0x4f`; V5a-1 rewrote that body, landed); the burst-pause words
(`ActiveWeaponBurstPauseWords`, `ElysiumNpcConditions10.cpp` — landed by V5a, real values; read,
not edited); the body's turn toward the facing target while running (V4b B2, landed:
`MotorMoveFacing`, `0x102e1a83`); the navigator's activity → sequence commit (landed:
`NavMoveNormalPass`'s slot 310) and `MoveNormal`'s restore arm (`0x102efc11`, V13).

## Rules

README § "Rules for every agent of V4o". Retail first; cite the address at every line. Never
build, launch or run. Only your files; a line another file needs goes in your report, exact, with
its place. A divergence is recorded, not adopted. Query budget: 10 s warns, 60 s stops. Never read
a file over ~200 KB whole. Grep / Read / Glob. Do not commit. Report ≤300 words: what you ported
(addresses), tests added and deleted, cross-lane lines, what stayed unrecovered.
