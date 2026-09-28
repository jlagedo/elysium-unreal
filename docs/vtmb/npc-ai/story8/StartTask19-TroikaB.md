# `CAI_BaseNPCTroika::StartTask` `0x102a1910` — the second part (arms `0x102a5046`..`0x102a77f7`)

Story 0019/8 pass I, lane L02. Port: `FElysiumNpc::StartTaskTroikaTail`
(`Source/ElysiumUE/Private/Substrate/ElysiumNpcStartTask19_2.cpp`), reached from the first part's
switch default. Helpers and seams: `ElysiumNpcStartTask19_2.inl`. Tests:
`Elysium.Substrate.NpcKernelStartTask19.TroikaTail.*`. Pass C folds this into
`schedule-kernel.md`.

The dispatch (`id - 5 > 0x144` → base; byte table `0x102a7ab8` → dword table `0x102a77f8`) is the
first part's. This part carries 108 arm entries of the 176: every arm whose start lies in
`[0x102a5046, 0x102a77f7]`, including the two shared tails and the base forward. Arms below are in
retail table order (ascending task id). "Complete" is `TaskComplete(false)` (`0x10273e80`, directly
or through the break tail `0x102a66d7`); "fail(line, r)" is `+0x1b44 = "AI_BaseNPCTroika.cpp"`,
`+0x1b48 = line`, slot 448 `TaskFail(r)`; "running" is a bare return.

## The tails

- `0x102a77ea` — ids `0x05`, `0xea`, `0xeb`: running, no write.
- `0x102a66d7` — ids `0x8b`, `0x125` (index `0x1d`, the arm Ghidra drops): complete.
- `0x102a77e2` — the base forward, 133 in-range ids plus every out-of-range id.
- `0x102a5904` — the movement-activity tail: `0x102ee250(nav, act)` (path `+0x2c`),
  `0x102a98e0(this, 2)` (`m_afMemory &= ~2`), complete.
- `0x102a7744` — the look tail: `0x10297940(pos)` then `0x102a18a0(task)` (`m_flWaitFinished =
  curtime + operand`, or `+ _DAT_10447ee0` for a non-positive operand).
- `0x102a63fc` — the facing tail: `0x10288670(motor, yaw)`, running.
- `0x102a76a9` — `m_flMoveWaitFinished (+0x5cf0) = curtime + operand`, running.

`0x10297940` (face a point with a turn animation): `0x102e0b40` (motor `+0x2c = -1`), `0x102e2020`
(ideal yaw toward the point), `0x10297a20` (the face-anim pick, writing `m_eFaceAnim` and
`m_flFaceYawDiff`), `m_bfAINPCFlags |= 0x8000000` (`PLAYING_FACE_ANIM`), then ideal yaw =
`GetAbsAngles().y + m_flFaceYawDiff`, flipped by 180 under `motor+0x28`, stored directly when
`motor+0x1c == 180` else through `0x102e0a80`.

## The arms

- `0x102a733f` `0x8c` MELEE_DODGE_ATTACK: `RestartIdealActivity(0x1155)`, running.
- `0x102a6de4` `0x92` MELEE_KNOCKBACK: slot 266, `RestartIdealActivity(m_knockbackType +0x6068)`.
- `0x102a6e09` `0x93` FLYING_KNOCKBACK_INTO: slot 266; `m_flGravity (+0x3ec) = m_fJumpGravity`;
  `SetAbsVelocity(m_KnockbackVelocity)` (`0x102a96b0`; no separate `m_vecVelocity` store); slot 208
  `SetGroundEntity(0)`; nav type 1 (`0x1027d9b0`); `m_bJumping = 1`; restart `m_knockbackType`.
- `0x102a6e70` `0x94` FLYING_KNOCKBACK_IDLE: `n = 0x10345480()`; `n < 0` → restart `0x8f`, else slot
  311 `ForcePreTranslatedSequenceAndActivity(0x8f, 0x8f, n)`.
- `0x102a6ef7` `0x95`/`0x98`: `0x102c41b0(this, "impact_dust_emitter", 0, 0, 0)`.
- `0x102a6eb4` `0x96`: `m_fKnockbackWallHitFallTime (+0x6014) = curtime + 0.01`.
- `0x102a6ed5` `0x97`: `0x102c4e80` (`SetMoveType(4, 0)`, `m_flGravity = m_fJumpGravity`), then
  `SetAbsVelocity(m_KnockbackVelocity)`.
- `0x102a6f16` `0x99`: `GetBonePosition01("Bip01 Spine2")`; `+0x6018 = curtime`; `+0x601c = pos`.
- `0x102a6fc5` `0x9b`: `m_flLastMeleeStepbackTime (+0x606c) = curtime`; complete.
- `0x102a6f70` / `0x102a6f93` / `0x102a6fac` `0x9c..0x9e`: (`FearSound` first on `0x9c`)
  `SetIdealActivity(0x1082 / 0x1083 / 0x1084)`.
- `0x102a634a` `0xb2` FACE_INTEREST: no `m_pInterestingPlace (+0x62ec)` → fail(0x3848, 0x22);
  `0x102ae310` (weapon holster policy); `m_bMatchOrientation (+0x570)` clear → complete; set →
  stop-turn, yaw = `AngleMod(place GetAbsAngles().y)`, facing tail.
- `0x102a63bd` `0xb3`: hint `0x1029f730(&patrol)`, place `0x1029f780(&patrol)`; no place →
  complete; match-orientation clear → `+0x6300 = +0x659c = 0`, complete; set → stop-turn, yaw =
  `0x102d12e0(hint)`, facing tail.
- `0x102a644e` `0xb4`: `+0x6308 = -1` first; no place → fail(0x3874, 0x22); else
  `0x102a9f40(place, 0, 1)`.
- `0x102a64a6` `0xb5`: no patrol place → complete; else `0x102a9f40(place, hint, 0)` — the third
  argument (the marker byte) is 0 (`0x102a64c2 PUSH 0`); `0xb4` passes 1.
- `0x102a64de` / `0x102a64e4` `0xb6`/`0xb7`: `TranslateActivity(1 / 9)` into `0x102ee250`, running.
- `0x102a650b` `0xb8`: `+0x6308 == -1` → clear `INTERESTING_INTO` (`0x20000000`), complete; else
  restart it.
- `0x102a5046` `0xe4` ADD_GESTURE: `SelectWeightedSequence((int)op) < 0` → break tail; else
  `0x10099250(act, 2.45, 0)`, complete.
- `0x102a5087` `0xe5` DO_DAMAGE: `n = (int)op`, negative → `m_iLastStoredDmg (+0xfdc)`; `CVDmg_t`
  `SetSrc(this)`, `Set(1, 0x80, n)`, `+0xc = 1`; `0x101c26d0(this, this, 1.0, 0, 0, dmg, -1)`; slot
  142 on itself; complete.
- `0x102a5125` / `0x102a516c` `0xe6`/`0xe7`: `act == 0x1097` → `m_iCowerAnimOffset (+0x6414) =
  RandomInt(0,2)*3`; Set / Restart `act + offset`; running.
- `0x102a778e` `0xe8` SET_DYING: `m_lifeState = 1`; complete.
- `0x102a51b3` `0xec`: `act = RandomInt(0,1)*3 + 0x106a`, `SelectWeightedSequence` discarded,
  `SetIdealActivity(act)`.
- `0x102a51e8` `0xed`/`0xee`: `m_Activity + 1 == 0` → `m_Activity = 0`; else
  `SetIdealActivity(m_Activity + 1)`.
- `0x102a521d` / `0x102a5283` `0xef`/`0xf0`: on `m_iLastDisciplineHitBy (+0xfd4)`: 6 → `0x108e` /
  `0x1091`, 12 → `0x108f` / `0x1092`, 5 and anything else → `0x108d` / `0x1090`.
- `0x102a52e9` `0xf1`: `m_knockbackType = (int)op`; complete.
- `0x102a530d` `0xf2`: slot 387 `Weapon_Drop_All`; complete. `0x102a532d` `0xf3`: slot 304; complete.
- `0x102a534d` `0xf4` CLEAR_HATRED: `0x102b52a0(this, 1, 1)`; complete.
- `0x102a536e` `0xf5`: `DisconnectFromSquad` (`0x1026d050`), `+0x14bc |= 0x80800000` (raw, bit 31
  included); complete.
- `0x102a5397` `0xf6`: `0x102ca2a0(registry, 2, this, pos, 2, op-bits, 0, this, 0)`;
  `InsertSound(8, pos, table[0x1b], 10.0, flag[0x1b], this)`; complete.
- `0x102a5426` `0xf7` FACE_NEXT_NODE: no route → `DevWarning(2, "No route to face!\n")`,
  fail(0x3599, 0xc); no waypoint → fail(0x35b9, 0xc); else the waypoint (or its successor),
  stop-turn, `0x102e2020`, `FacingIdeal()` → complete, else running.
- `0x102a5515` `0xf8`/`0xf9`: slot 474 null → fail(0x35cb, 0x12) (unreachable on the Troika line:
  slot 474 answers `&m_BestSound`); else `0x101b99d0` (owner origin for types `0x10`/`0x400` with a
  live owner, else the sound's origin), look tail.
- `0x102a555c` `0xfa`: `0x1010e530(&v, -100, 100)` (CRT `rand()` per axis) — around the WORLD
  origin — look tail.
- `0x102a5599` `0xfb`: `m_hClosestPlayer (+0x628c)` dead → fail(0x35e2, 0x17); else look.
- `0x102a55e3` `0xfc`: slot 586 live → look; else `m_hLastSeeUnknown (+0x608c)` live → look at
  `+0x6090`; else fail(0x35f7, 0x21).
- `0x102a5662` `0xfd`: `+0x65c0` dead → fail(0x3607, 0x21); else look.
- `0x102a56a2` `0xfe` / `0x102a5754` `0xff`: `m_eFaceAnim` 1..8 → `0x1100..0x1107`, else ACT_IDLE;
  `SetIdealActivity`; yaw = `GetAbsAngles().y`; `PLAYING_FACE_ANIM`; stop-turn; ideal yaw = yaw −
  `m_flFaceYawDiff` (`0xfe`) or yaw + {0, 0, 45, 90, 135, −45, −90, −135, 180}[face] (`0xff`);
  `0x102a18a0`; running.
- `0x102a585d` `0x100`: raw mask `>= 0` → `+0x14b8 |= mask`, else `+0x14bc |= mask`; complete.
- `0x102a58ae` `0x101`: the mirror (`&= ~mask`); complete.
- `0x102a5886` `0x102`: `AddMiscFlag(1 << (raw & 31))`; complete.
- `0x102a58d7` `0x103`: `TranslateActivity(0x1093)`, no sequence → `TranslateActivity(0x13)`; tail.
- `0x102a5932` `0x104`: `0x10295460(0x1115)` (untranslated) `== -1` → 9; tail.
- `0x102a594d` `0x105` / `0x102a5956` `0x126`: `TranslateActivity(0x1115 / 0x1121)`, no sequence →
  `TranslateActivity(9)`; tail.
- `0x102a59eb` `0x106`: `motor+0x28 = (op != 0)`; complete.
- `0x102a5a45` `0x107` ATTEMPT_DIVE: `d = navGoal − GetAbsOrigin()`, 2-D `len2`: `< 4096` →
  complete; `> 12100` → complete (`TEST AH,0x41; JP` at `0x102a5ac4` is taken when neither C0 nor
  C3 is set); in the band `dot2D(norm(d.xy), m_vecRight)`: `> 0.98` →
  `ANIM_MOVEMENT` + `SetIdealActivity(0x110b)`; `< −0.98` → `0x110a`; else complete.
- `0x102a5b32` `0x108`: from = origin + (0, 0, StepHeight*0.5); right/left = from ± `m_vecRight*60`;
  `0x110b` with a sequence and a clear move probe (kind 0, mask `0x202400b`, 100.0) →
  `ANIM_MOVEMENT` + `0x110b`; else `0x110a` likewise; else fail(0x3736, 0xe).
- `0x102a5cd4` `0x109`: forward `m_vecForward*96`, activity `0xf1d`; else fail(0x3759, 0xe).
- `0x102a5dda` `0x10a` / `0x102a5e1c` `0x10c`: the cover-anim restart (`0x102a1560` / `0x102a15c0`)
  true → running; false → complete. `0x102a5dff` `0x10b`: `0x102a1590`, complete.
- `0x102a5e41` `0x10d` PLAY_COVER_AIM: `0x102a15f0`; point = `m_hShootTargetOverride` origin if
  live; else if `m_hHintCoverObject (+0x6448) == GetEnemy()` (null == null included) → the enemy's
  LKP (`0x102dfed0`); else complete and fall through with the zero point; `FInAimCone(point)` →
  complete; else stop-turn + `0x102e2020(point)`, running.
- `0x102a5f16` `0x10e`: no `m_pHintNode` → fail(0x37aa, 4); else `0x102b6120(&pos, 0)`, slot 62
  `SetOrigin(pos)`, complete.
- `0x102a5f86` `0x10f`: no hint → fail(0x37bd, 4); `RestartIdealActivity(0xc84)`; hint output
  `0x102d0910`; `ClearHintNode(60.0)`; running.
- `0x102a5fcc` `0x110`: no hint → fail(0x37f4, 4); restart `0xc84`; a hint target name → find it,
  store in `m_hKickPhysicsProp (+0x643c)`, `DevMsg` (no arguments pushed) if not found; a live prop
  within 128 units → `0x102b6890(prop)`, else `DevMsg`; clear the handle; hint output; clear hint 60.
- `0x102a6128` `0x111`: dead prop → fail(0x3812, 0x25); no enemy → fail(0x380d, 6); goal
  `0x102a9c80(10, -1, -1.0, 0, …)` whose type word is then OVERWRITTEN to 4 (`0x102a61b4`), dest =
  prop − norm(enemy − prop) * 64 (`0x102a61f1` → `0x1001395d` = `0x10146190`, FSUB: the far side
  of the prop), target 0; `SetGoal(goal, 0)`; running.
- `0x102a6289` `0x112`: dead prop → fail(0x381f, 0x25); else complete.
- `0x102a62db` `0x113`: dead prop → fail(0x3832, 0x25); restart `0xc84`; kick; clear; running.
- `0x102a654b` `0x114`: slot 168 null or slot 530 unreachable → fail(0x38c3, 0xc); goal
  `0x102a9d20(lkp, -1, -1.0, 0)`, t = nav arrival (`0x102f2fc0`); slot 563(t, &dest, &goal.tol, &t);
  `SetGoal(goal, 2)` false → `DevWarning("GetPathToLastEnemyLKP failed!!\n")`,
  `RememberUnreachable`, fail(0x38d8, 0xc); true → `0x102f2fe0(nav, t)`, complete.
- `0x102a6694` `0x115`: t = slot 168 or this; `FindLateralCover(t eye, t)` (`0x102784a0`) → move
  wait + complete; else `0x102edc80(origin, eye, 0.0, CoverRadius, &out, t)` false →
  fail(0x3907, 8); goal `0x102a9dc0(6, out, 0x13, -2.0)`, `SetGoal(goal, 0)`; with a hint: slot 569
  → `0x102ee410`, `0x102d11f0` → `0x102ee530`; move wait; running.
- `0x102a6801..0x102a6861` `0x116..0x118`: `0x102a9770(0x40000)` answers 1 when `BOTCHED_ATTACK` is
  CLEAR, and that arm completes. Set: `0x116` restarts `0x55`; `0x117` `0x102a1620`; `0x118`
  `0x102a1560` true → running, else `0x102a1620`.
- `0x102a68a8` `0x119`: `+0x6334 = 1`, `+0x6330 = op`; complete. `0x102a68bd` `0x11a`: `+0x6330 +=
  RandomFloat(0, op)`; complete.
- `0x102a68df` `0x11b`/`0x11c`: act `0x10`/`0x14`; no sequence → turn act `9`/`0x13`,
  `0x102a1650(act, 180, delta)` false → fail(0x396f, 0x15); true → `+0x63ec = 180`,
  `m_flWaitFinished = curtime + delta`, restart. With a sequence: hull trace from origin +
  half step height to `− m_vecForward*50*delta` (`0x102a69ff`, the same subtract helper; mask
  `0x202400b`, collision mins/maxs); fraction
  `!= 1.0` → fail(0x3997, 0xe); else wait + restart.
- `0x102a6fd8` `0x11d`: stop-turn; yaw = `AngleMod(m_qaLastFacing.y)`; facing tail.
- `0x102a7000` `0x11e`: `0x101f5950(&DAT_1073dc28, this, (int)op, 2, 1.0, 1.25)`; complete.
- `0x102a7025` `0x11f`: from `m_vSavePosition`, to = from + `m_vecViewOffset`;
  `0x102edaa0(from, to, 0, 4096, 1.0, 1, &out)` false → fail(0x3ac1, 0xb); goal
  `0x102a9d20(out, 0x13, -2.0)`, `SetGoal(0)`; running.
- `0x102a70e6` `0x121`: op != 0 → `+0x14bc |= 0x80000001`, `+0x65d0 = GetAttackExtents()`,
  `SetAttackExtents(60, 60, 80)`; op == 0 → `SetAttackExtents(+0x65d0)`, `+0x65d0 = (-1,-1,-1)`,
  `+0x14bc &= ~0x80000001`; complete.
- `0x102a6ab7` `0x122`: no `0x1121` sequence → fail(0x39a7, 0x15) AND tries = 1000 (the arm goes
  on); side = `0x1025df40(coordinator, this, enemy)`, 0 → `RandomInt(0,1) ? -1 : 1`; while tries <
  2: yaw = `RandomFloat(side*80, side*120)`, `0x102a1650(0x1121, yaw, delta)` true → `+0x63ec = yaw`,
  wait, restart `0x1121`; false → side = −side. tries == 1000 → running; else fail(0x39d5, 0xe).
- `0x102a6bf7` `0x123`/`0x124`: radius = slot 418(op); t = slot 168: dist computed, or
  fail(0x39ec, 6) + tries = 1000; `debug_circle_dist_override > 0` replaces radius; radius == −1
  with a weapon → `w[0x8c0]*0.75 + w[0x8b8]*0.25`; act `0x1121`, else 9, else fail(0x3a06, 0x15)
  (returns); up to two slot-603 yaws through `0x102a1650`; tries == 1000 → running; else
  fail(0x3a30, 0xe).
- `0x102a597f` `0x127`: `+0x6320 = debug_melee_advance_combatmove_dist + RandomFloat(0,
  slot418(op))`; then the tolerance tail: `0x102ee1c0` (path `+0x28`) and `0x102f2fe0` (path
  `+0x20`) with the same value (`0x102a59d1`, `0x102a42df..0x102a42e8`); complete.
- `0x102a7185` `0x128`: `m_flWaitFinished = delta + curtime`; `RandomInt(0,1)` 0 → ACT 1, 1 → 3;
  `RestartIdealActivity(TranslateActivity(act))`.
- `0x102a71e4..0x102a721c` `0x129..0x12d`: `m_flSpecialDistanceAccum (+0x5bac)` set / += / += rand /
  −= / −= rand of slot 418(op); complete.
- `0x102a72a7` `0x12e`: stop-turn, `0x102e2020(m_vSavePosition)`, slot 572; running.
- `0x102a72e3` `0x131`: op != 0 → `+0x14bc |= 0x80001000`, `0x1026d130` (`SetEnemy(NULL)`,
  `DisconnectFromSquad`, `++m_iIsOblivious`), fire `+0x5fd4`; op == 0 → `0x1026d160`
  (`--m_iIsOblivious` floored, `ReconnectToSquad`), `&= ~0x80001000`, fire `+0x5fec`; complete.
- `0x102a7358` `0x137`: `0x10295460(+0x65e4, 1) == 0` → `TaskFail(0x15)` with no line write; else
  restart `+0x65e4`. (A -1 "no sequence" answer is NOT refused — retail defect.)
- `0x102a73a0` `0x138`: squad and enemy → `SquadNewEnemy`; complete.
- `0x102a73da` / `0x102a7468` / `0x102a748c` `0x139`/`0x13b`/`0x13c`: stop-turn, restart
  `0x2b`/`0x30`/`0x32`.
- `0x102a73fe` `0x13a`: `m_flGravity = m_fJumpGravity`; `0x102c4c50` solves the arc; motor `+0x18`
  applies it; nav type 1; `m_bJumping = 1`; running.
- `0x102a74b0` `0x13d`: `m_bInvincible (+0x63d8) = (op != 0)`; running (no complete).
- `0x102a74ed..0x102a7586` `0x13e..0x144`: the activity copy-prop calls; all complete except
  `0x142`, which keeps running when `0x1018eb50` answers 0.
- `0x102a7598` `0x145`: `BurnModel(GetSkeletonModelName(), false)`; complete.
- `0x102a75b2` `0x146`: `+0x5db8 = +0x62a8`, `+0x5dc4 = +0x62b4`; complete.
- `0x102a75db` `0x147`: `m_hLastDamageEnt (+0x5b7c)` dead → fail(0x3be0, 0x21); `0x102edc80(origin,
  eye, 32.0, CoverRadius, &out, this)` false → fail(0x3bdb, 8); goal `0x102a9d20(out, 0x13, -2.0)`,
  `SetGoal(0)` (answer unread), move wait; running.
- `0x102a7722` `0x148`: dead → fail(0x3bee, 0x21); else look at it.
- `0x102a779d` `0x149`: `(int)op`, no sequence → `0x1d`, none → 1; `SetIdealActivity`; ACT_IDLE →
  complete, else running.

## Reads and writes

Reads: the task's two words; `+0xfd4`, `+0xfdc`, `+0xfec`, `+0x14b8`, `+0x5b7c`, `+0x5ba8`,
`+0x5d8c`, `+0x5dc4`, `+0x5dd0`, `+0x5ddc`, `+0x6004`, `+0x6068`, `+0x608c`, `+0x6090`, `+0x60b0`,
`+0x628c`, `+0x6290`, `+0x629c`, `+0x62a8`, `+0x62b4`, `+0x62ec`, `+0x6300`, `+0x6308`, `+0x6330`,
`+0x63e4`, `+0x63e8`, `+0x643c`, `+0x6448`, `+0x649c..+0x64b8`, `+0x658c`, `+0x65c0`, `+0x65d0`,
`+0x65e4`, `+0x65e8`, motor `+0x1c`/`+0x28`, navigator path `+0x20`/`+0x24`.
Writes: `+0x200`, `+0x3d4`, `+0x3ec`, `+0xfec`, `+0x14b8`, `+0x14bc`, `+0x1b44/+0x1b48` (debug),
`+0x5bac`, `+0x5bb4`, `+0x5cf0`, `+0x5db4`, `+0x5db8`, `+0x5dc4`, `+0x6014`, `+0x6018`, `+0x601c`,
`+0x6068`, `+0x606c`, `+0x6300`, `+0x6308`, `+0x6320`, `+0x6330`, `+0x6334`, `+0x6414`, `+0x63d8`,
`+0x63ec`, `+0x643c`, `+0x6498`, `+0x659c`, `+0x65d0`, motor `+0x28`/`+0x2c`/`+0x34`, navigator
path `+0x20`/`+0x28`/`+0x2c`.

**Unrecovered:** the category name of sound-table row `0x1b` (`DAT_1072bc20`, `0x102a9ea0` /
`0x102a9ec0`); the `+0xb0` word `0x1029f780` caches from a place; `0x102b6890`'s kick impulse;
`0x102c4c50`'s arc solve; the `activity_copy_prop` bodies `0x1018e790..0x1018ecf0`; the default
string of `cvar_debug_circle_dist_override` (`0x105399a0`); `0x10345480`'s studio-header read; the
eluded-record fallback inside `0x102dfed0`; `0x102ae310` (weapon holster policy) in full.
