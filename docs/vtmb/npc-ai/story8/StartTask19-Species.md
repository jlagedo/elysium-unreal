# StartTask19: the species `StartTask` overrides (slot 442)

Spec 0019, story 8, lane L04. This walks every species `StartTask` override the retail
`vampire.dll` ships, plus `CNPC_VCop::StartTask` (family Damaged19). It also covers the
verification of `CNPC_VFrenzyShadow::StartTask`.

- **Port:** `Source/ElysiumUE/Private/Substrate/ElysiumNpcStartTask19Species.cpp`. FrenzyShadow is
  in `ElysiumNpcFrenzyShadow.cpp`.
- **Tests:** `Tests/ElysiumNpcKernelStartTask19Tests_4.cpp`, suite
  `Elysium.Substrate.NpcKernelStartTask19.Species.*`.
- **Packet:** `$ELYSIUM_WORK_ROOT/research/npc-kernel-checklist/families-19-29/StartTask19-READING.md`.
  VCop is in `Damaged19-READING.md`.

Every body below has instruction addresses in the port's trailing comments. This page states the
behaviour; the addresses stay in the code.

## Shared facts

**Task number.**
- Retail's `Task_t::iTask` is the class-LOCAL task id, and every species switch compares local
  numbers.
- The port stores the GLOBAL id. Each body therefore translates the id back through slot 450
  `GetLocalTaskId` (`0x101a6640`) before it switches.
- A task a body builds on its own stack is translated forward before it is handed on. The head
  claw's `{0x4b, 1.0}` is the one case.

**Parent chains.** Every chain in these bodies is a DIRECT thunk, so the port makes an explicit
base call:

| Thunk | Target |
|---|---|
| `0x10010695` | `CAI_BaseNPCTroika::StartTask` `0x102a1910` |
| `0x10013336`, `0x1000a0c9` | `CNPC_VHuman::StartTask` `0x103847f0` |
| `0x10009b06` | `CNPC_VAnimal::StartTask` `0x1035f650` |
| `0x1000de7c` | `CNPC_VVampireBoss::StartTask` `0x103c5ac0` |

**Failure trace.**
- A traced failure writes `+0x1b44` (the source file) and `+0x1b48` (the line), then calls slot 448
  `TaskFail`. The port records `StartTask fail trace <file>:<line>` in the mind trace.
- A bare failure calls slot 448 alone.

**Motor facing idiom.**
- `0x102e0b40(m_pMotor)` resets the motor yaw-speed override (`motor+0x2c = -1`). The port has no
  carrier for it; this is a seam that writes nothing.
- `0x102e2020(m_pMotor, target)` sets the ideal yaw at a point. `0x102e20b0` does the same with a
  speed. Both land in `SetMotorHintYaw`.

**`RestartIdealActivity` (`0x10289ee0`).**
- Retail: when `m_Activity == act`, it sets `m_Activity = 0`, then calls `SetIdealActivity(act)`.
- The port's `RestartIdealActivityId` is still a no-op seam (family Hints).
- Arms that test `m_IdealActivity` afterwards therefore see the word as the caller left it.

**`SetGoal` (`0x102ecd20`).**
- Arms build an `AI_NavGoal_t` on the stack and submit it to the port's motor (`MoveTo`).
- Activity `0x13` (ACT_RUN) runs. Anything else walks.
- A tolerance of -1.0 means the schedule's `m_flGoalTolerance`, or the hull width when that is not
  set.

## CNPC_VTzimisceHeadClaw -- 0x103c1820

- **Tasks 0x122, 0x123, 0x124:** the task is REPLACED. A stack `Task_t {0x4b, 1.0}` goes to the
  Troika body, and the original id is never passed on.
- **Tasks 0x36, 0x37:** slot 618 plays the exertion: `EmitSound` on channel 4, volume 1.0,
  attenuation 0.8, pitch 100, one of three `TC_FatGuy/Exert_Heavy_N.wav` picked by
  `RandomInt(0, 2)`. The task then falls into the Troika body.
- **Everything else:** the Troika body.

## CNPC_VTzimisceRunner -- 0x103c35d0

- **Tasks 0x122–0x124:** `RestartIdealActivity(1)`, then
  `m_flWaitFinished = m_flWaitFinishedDelta + curtime`. The body returns WITHOUT calling the base.
- **Task 0x130:**
  - A live `m_hPotentialEnemy` (+0x6678): its slot 220 origin goes into `m_vSavePosition`, then
    `TaskComplete`, and the base STILL runs.
  - A dead handle goes straight to the base.
- **Tasks 0x36, 0x37:** the `TC_Runner` exertion, then the base.

## CNPC_VTaxiDriver -- 0x103b36d0

- **Tasks completed at once, without the base:** 0x2b, 0x2e, 0x2f, 0x31, 0xb2, 0xba–0xbd, 0xf8,
  0xf9, 0xfb–0xfd, 0x11b and 0x11d.
- **Task 0xb9:** runs the dialogue upkeep `0x102c1400`.
  - An answer of -1 (not in dialogue) completes the task.
  - Any other answer calls slot 310 `SetActivity(0x114e)` and leaves the task running.

## CNPC_VSabbatGunman -- 0x103a5650

- **Tasks 0x11b, 0x11c, 0x122–0x124:**
  `m_flWaitFinishedDelta *= 1.0 / sabbat_gunman_speed_scalar`, with the ConVar read as
  `IsCommand ? 0.0 : m_fValue`. The IsCommand arm divides by zero; that is the retail defect.
  Then the VHuman body.

## CNPC_VCop -- 0x10371b70

- **Task 0x107:** `TaskComplete` (tail jump).
- **Task 0x14a:** `m_hClosestPlayer` (null when stale) is handed to slot 598 by tail jump. No
  completion.
- **Everything else:** VHuman.

## CNPC_VDog -- 0x10374940

- **Task 0x36:**
  1. `AutoMovement`.
  2. The motor facing at the enemy's last-known position, at speed -2.0.
  3. `RestartIdealActivity(0x4b)`.
  4. `TaskComplete` only if slot 251 `IsActivityFinished` answers true.
- **Task 0xbc:** `SetActivity(3)`, then the Animal body.
- **Everything else:** Animal.

## CNPC_VGhoulCroucher -- 0x1037b8b0

- **Task 0x14a:** `RestartIdealActivity(table A)` (`0x1037b870`). If `m_IdealActivity` did not
  take, `TaskFail(0x15)`.
- **Task 0x14b:** the same over table B (`0x1037b890`). A refusal also sets `m_bUnawareExited`
  (+0x6667).
- Neither task completes.
- **Everything else:** VHuman.

## CNPC_VHuman -- 0x103847f0

- **Tasks 0x89, 0x8a:** `m_flLastAttackTime = curtime`, then `RestartIdealActivity(0x4b)`.
- **Task 0x8b** (a downed enemy):
  - With an enemy that has a combat character, face its last-known position.
  - Play the enemy sequence's paired activity (descriptor `+0x2d8`) when it is non-negative, else
    0x51.
- **Tasks 0x8d, 0x8e, 0x8f, 0x90, 0x91:** activities 0x1154, 0x52, 0x1156, 0x1152 and 0x1153.
- **Task 0x9f:**
  - No weapon: line 0x138, `TaskFail(3)`.
  - Otherwise the tolerance is `weapon+0x8c0 * data + 2 * NAI_Hull::Width(m_eHull)`, then
    `TaskComplete`.
- **Everything else:** Troika.

## CNPC_VAnimal -- 0x1035f650

- **Tasks 0x89, 0x8a, 0x8b, 0x8e, 0x9f:** as VHuman, with activities 0x4b / 0x4b / 0x50 / 0x52.
  The 0x9f failure is at line 0x165.
- **Task 0xa5:**
  - No `m_pInterestingPlace`: line 0x19e, `TaskFail(0x22)`.
  - Otherwise a goal of type 9 at `m_vecInterestingPlace`, with activity words -1 and tolerance
    -1.0. Accepted: complete. Refused: line 0x199, `TaskFail(0xc)`.
- **Everything else:** Troika.

## CNPC_VAndreiBlood -- 0x1035d1b0

- **Task 0x14b** (unhide at the hint):
  - No hint: line 0x130, `TaskFail(4)`.
  - Otherwise, in order: `SetAbsOrigin(hint)`, slot 181 `Teleport`, slot 62, `SetHullSizeNormal(1)`,
    `m_bTriggerUnhide = 1`, `m_fEffects &= ~0x20`, `RemoveSolidFlags(4)`, `ForceTransmit`, `Relink`,
    `m_fEffects |= 0x10`, complete.
- **Task 0x150:** nothing.
- **Tasks 0x151, 0x152:** the teleport-out / teleport-in wav on channel 2 (0.8), the matching blood
  emitter, then activity 0x113c / 0x113b. No completion.
- **Task 0x153:** `m_pHintNode = SelectTeleportNode()`, complete.
- **Task 0x154:** the summon wav, activity 0x113a, the summon emitter. Then the nearest
  `npc_maker_fleshpile` within 1024 of the origin makes a runner. No completion.
- **Task 0x155:** `m_OnDeath` fires with the closest player as activator, then `UTIL_Remove(this)`.
- **Task 0x156:** `+0x66d0 = curtime`.
- **Everything else:** VampireBoss.

## CNPC_VAsianVampire -- 0x103611a0

- **Task 0x150:** consumed.
- **Tasks 0x151, 0x152:** `m_pHintNode` from `SelectLedgeNode` / `SelectJumpbaseNode`.
  - A null result stamps line 0x110 / 0x118 and calls `TaskFail(1)`.
  - Both paths then reach the ONE shared `TaskComplete(0)`. After a failure `COND_TASK_FAILED`
    refuses it.
- **Everything else:** VampireBoss.

## CNPC_VBach -- 0x103645a0

The dispatch is three nested compares: above 0x14c, equal to 0x14c, and the low range.

- **Tasks 0x34, 0x35:**
  - Holding a non-rifle: the VHuman body runs, then `+0x66a4` is cleared.
  - Holding the rifle (`item_w_rem_m_700_bach`, compared with `__strcmpi`) or nothing:
    - `+0x66a4` clear: complete. Bach does not turn while aimed.
    - `+0x66a4` set: the VHuman body, then clear.
- **Tasks 0xb0, 0xb1** (the sniper wait):
  - No weapon: complete.
  - A non-rifle: `+0x66a4 = 1`, the VHuman body, then `m_flWaitFinished -= 0.35`.
  - The rifle, with the camper flag `+0x66a0` set:
    - Occluded (`+0x6674` non-zero): wait = curtime + 1e9, and the warning time equals it.
    - Not occluded: wait = curtime + 0.15, clear the flag and `+0x6678`, warning = wait + 1e9.
  - The rifle, with the camper flag clear:
    1. Without skip-to-warning: wait = curtime + 0.2 + (enemy feat 0xc, 1 with no enemy) * 0.08.
    2. With it: wait = curtime + `+0x6698`, then clear both skip words.
    3. Both paths then add max(enemy feat 10, feat 9) * 0.1.
    4. warning = wait, then wait += 1.5.
  - With no hint, `RestartIdealActivity(5)`.
- **Tasks 0xba–0xbd:** complete.
- **Task 0x14a:** the rifle via `Inventory_Find` and slot 388, `+0x66a4 = 0`,
  `+0x668c = curtime + 0.5`, complete.
- **Task 0x14b:** the katana, `+0x668c = curtime + 0.5`, complete.
- **Task 0x14c:** the holy-light equip `0x103656a0`, its wav, `+0x66a1 = 0`, complete.
- **Task 0x14d:**
  - With a weapon: type-3 stat 0xf base 1, then weapon `+0x518` and `+0x4f0(0)`.
  - Always completes.
- **Task 0x14e:** the teleport-ring hint for `m_iBachTeleportState` 0..3 (types 0x4268..0x426b),
  searched with flags 2. Any other state: line 0x350, `TaskFail(4)`.
- **Task 0x14f** (the teleport):
  - No hint: line 0x355, fail 4.
  - Otherwise, in order:
    1. Capability 1 off, `+0x66a7 = 0`.
    2. `SetAbsOrigin`, `Teleport`, slot 62.
    3. The state advances, wrapping above 3.
    4. Stat 0xd = 5.
    5. `+0x6690 += 6`, the shield up, `+0x6680 = curtime + 3`.
    6. The shield wav, complete.
- **Task 0x150:**

  | State | Effect |
  |---|---|
  | 1 | capability 1, hint 0x426c, movement spot |
  | 0 | capability 1, hint 0x426d |
  | 3 | capability 1, movement spot, then as state 2 |
  | 2 | skip-to-warning at `0x1062d210[state]` = {1.3, 1.0, 1.2, 1.2}, line 0x387, fail 4 |

- **Task 0x151:** without a movement spot, capability 1 off. Then the rifle, `+0x66a4 = 0`, skip
  at the state's time, complete.

## CNPC_VChangBros -- 0x1036b750

Also the body of the Blade and Claw classes.

- **Task 0x13b:** in sector 3, `+0x66cc = curtime`. Then the base.
- **Task 0x150:** motor reset, `AddSolidFlags(4)`, activity 0x1148, then FALLS THROUGH into 0x151.
- **Task 0x151:** motor reset, the teleport-in emitter, `RecordHealthPercent`,
  `+0x66d0 = curtime`.
- **Task 0x152:** teleport node, complete.
- **Task 0x153:** ground nav, `m_bJumping = 0`, `flags2 &= 0x7ffffffd`, `CommitSetupJump`, the
  teleport-out emitter, activity 0x1149.
- **Task 0x154:** ledge node. A null result stamps line 0x1aa and fails 1. Both paths complete; a
  failure refuses the completion.
- **Task 0x155:** `SetupSuperJump(hint pointer)` (the pointer is tested as a float), complete.
- **Task 0x156:**
  1. Face the closest player.
  2. Activity 0x114c.
  3. `+0x66f0 = curtime + 1.5`.
  4. The charge emitters.
- **Task 0x157:** `+0x66d8 = 0`, activity 0x114a, `m_flLastAttackTime`.
- **Task 0x158:** `CheckJumpPathToHintNode`: complete, or line 0x1ce and fail 1.
- **Task 0x159:** the united node, complete.
- **Task 0x15a:** nothing.
- **Task 0x15b:** `AddSolidFlags(4)`, activity 0x114b, face the stored centre,
  `+0x66d4 = curtime + 4`.
- **Task 0x15c:** activity 0x114c.
- **Task 0x15d:**
  1. `+0x66ec = curtime`, activity 0x114d, `RemoveSolidFlags(4)`.
  2. Only when `m_ChangType == 0`: the blast emitter 50 above the arena centre.
  3. Then, unless the closest player is in sector 4 or the template is negative:
     `CausePlayerAOEDamage(point, 1000, template+0xcc)`.
- **Everything else:** VampireBoss.

## CNPC_VGargoyle -- 0x103790d0

- **Task 0x12f:** activity 0x5e.
- **Task 0x130:** a live pillar's origin into `m_vSavePosition`, complete. With no pillar, nothing
  happens and the task keeps running.
- **Tasks 0xe9, 0xea:**
  1. `m_iDoingGibDeath = 1`.
  2. With a physics object: the impulse `velocity + forward * 400 + (0, 0, 100)` and
     `m_OnGibDeath`.
  3. The VHuman body.
- **Task 0x31:** with a pillar, face it and call `SetTurnActivity`. Complete only if
  `FacingIdeal`.
- **Tasks 0x36, 0x37:** the gargoyle exertion, then VHuman.

## CNPC_VHengeyokai -- 0x103805d0

- **Task 0x135:** drop the carried body (`0x103828a0`), complete.
- **Task 0x136:** the thaw (`0x10383130`), complete.
- **Task 0x14a:** the shark form (`0x103831c0`), complete.
- **Task 0x14b:** nothing.
- **Task 0x14c:** the hengeyokai model, `m_fEffects |= 0x10`, render words 0, hull 0x12,
  `SetHullSizeNormal`, complete.
- **Task 0x14d:** `SUB_Remove` think at curtime + 0.01, complete.
- **Task 0x14e:** `m_bfAINPCFlags2 |= 0x80000800`, complete. The packet row claimed `+0x19c`; the
  word is `+0x14bc`.
- **Task 0x134:** activity 0x127.
- **Tasks 0x36, 0x37:** the exertion, then VHuman.
- **Task 0xc8:** face the pickup target. Complete only once `FacingIdeal`.
- **Task 0xc9:**
  - `COND 0x1b` and `0x103822a0` both answer: complete.
  - Otherwise: face, `SetTurnActivity`, `+0x6674 = curtime + 1`.
- **Task 0xca:** the same over slot 168, with no condition gate and no timer.
- **Task 0x130:**
  1. `+0x6680 = 2`.
  2. A live target: its origin into `m_vSavePosition`, the tolerance pushed, complete.
  3. A dead target: line 0x3b3, fail 1.
- **Tasks 0x132, 0x133:** the carry facing `0x10382bb0`, activity 0x126.

## CNPC_VManBat -- 0x1038c390

**Mode word.** The mode is a secure word:
- `+0x6670` holds `0x103908c0(0x1042fb50(mode))`.
- The port also keeps the decoded mode (`ManBatMoveGoalNodeMode`).

**Fly-node searches.** Each is `0x102d1af0(this, 20000, type, 5000.0, 0, 0)`, and the result goes
into `m_pFlyNode` (+0x6688).

**Tasks:**
- **0x14a:** ideal 0x28, plus `0x1038c250(node)` when there is a fly node.
- **0x14b:** the first flap (ideal 0x22, flap timer 2.3), then the steer `0x1038b370` and the flap
  selector `0x1038e720`.
- **0x14c:** in order:
  1. Release the carried object.
  2. Ground switch 2.
  3. Mode 5.
  4. Ideal 0x2f.
  5. A ballistic velocity to the fly node.
- **0x14d:** yaw `RandomInt(-180, 180)`, quantised to the 16-bit angle; speed 500; climb
  `RandomFloat(0.1, 0.5) * 500`. Then `SetAbsVelocity` and the flap selector.
- **0x14e:** mode 1, search type 2. Found: complete. Not found: fail 4.
- **0x14f:**
  - With no node: mode 0, search, else node id 1 and search again. Still none: fail 4.
  - Otherwise: node id + 1, complete.
- **0x150:** ideal 0x30, velocity zero.
- **0x151:** mode 2, node id `RandomInt(1, 3)`, search. Complete, or fail 4.
- **0x152:** mode 3, search. Steer and flap, or fail 4.
- **0x153:** the throw row `RandomInt(1, 4)` of four overlapping tables, via `0x1038f2c0`.
  Complete, or fail 1.
- **0x154:** a node id `RandomInt(1, 3)` different from the current one. With a node, set the
  origin to it, clear the node, complete. Otherwise fail 4.
- **0x155:** mode 4, search, then ideal 0x116f, or fail 4.
- **0x156:** face the closest player (pitch 0, roll kept), ideal 0xb0.
- **0x157:** mode 6, clear the node.
- **0x158:** ideal 0x1170, the screech cone.
- **0x159 / 0x162:** mode 7, fly-by target = closest player.
- **0x15a / 0x161:** ideal 0x4b.
- **0x15b:** ideal 0x1054.
- **0x15c:** mode 8, node id 1, search, then steer or fail 4, then FALL THROUGH into 0x15d.
- **0x15d:** origin z + 10, the first flap, complete.
- **0x15e / 0x15f:** `0x1038fc80` / `0x1038fd40`, complete.
- **0x160:** the nearest entity named "Cop" becomes the fly-by target. Live: mode 7. Otherwise
  fail 1.
- **0x163:** mode 9, coast timer curtime + 1.
- **0x164:** clear the fly-by sound flag, complete.
- **Everything else:** VHuman.

## CNPC_VMingXiao -- 0x10392d80

- **Tasks 0x89, 0x8a, 0x8b, 0x8e, 0x9f:** as VHuman. The 0x9f failure is at line 0x296.
- **Task 0x14a:** the transform `0x1039a750`, complete.
- **Task 0x14b:** nothing.
- **Task 0x14c:** `MingXiao.mdl`, 0x10, hull 0xf, `+0x6678 = 1`, complete.
- **Task 0x14d:** `SUB_Remove`, complete.
- **Task 0x14e:** `BeginDefeatSequenceOnce` (Misc19), complete.
- **Tasks 0x14f–0x152:** the throw attack for tentacles 0..3, with activities 0x112a..0x112d.
- **Tasks 0x153, 0x154:** the motor clamp around the current yaw (range 20), activity 0x1131 /
  0x1130.
- **Task 0x155:**
  1. Switch to `m_hRangedWeapon`.
  2. Activity 0x19.
  3. `+0x66c0 = 0x10397f70() + curtime`.
- **Task 0x156:** the throwable-object mode from the data word (0..4, anything else 0), complete.
- **Task 0x157:** a goal of type 4 at `+0x6720`, running. The answer is the return value.
- **Task 0x158:** activity 0x112f for tentacle 4, else 0x112e.
- **Task 0x159:** modes 3/4 run the throw clean-up; always completes.
- **Task 0x15a:** clamp, then 0x1131 / 0x1130 by tentacle.
- **Task 0x15b:** `hit_yaw = RandomFloat(-90, 90)`, activity 0x73.
- **Task 0x15c:** `hit_yaw = -VecToYaw(origin - bone) + RandomFloat(-15, 15)`, activity 0x74. The
  FCHS negation is retail's.
- **Tasks 0x15d, 0x15e, 0x15f:** the damage / death / proxy-death emitters, complete.

## CNPC_VMingXiaoTentacle -- 0x1039c4c0

- **Tasks 0x8b, 0x8e:** as MingXiao.
- **Task 0x9f:**
  - No weapon: line 0x181, fail 3.
  - Otherwise the tolerance is `self hull mins.x + enemy hull mins.x + range * data`.
    `0x102d6100` answers `&row.mins`.
- **Task 0x14a:** when `(int)data` differs from `m_ePhase`:
  1. The teardown `0x1039f310`.
  2. Phase 1 is invincible, phase 2 is vulnerable, phase 3 and anything else (stored as 0) are
     invincible. Each clears `+0x6674`.
  3. Always completes.
- **Task 0x14b:** the form swap on `(int)data`:

  | Data | Model | Model index | Hull | Attack extents | Emitter |
  |---|---|---|---|---|---|
  | 2 | grub | `+0x6668` | 0x11 | 0 | none |
  | 3 | transformation | `+0x666c` | 0xf | 64/64/32 | baby-transform |
  | other | grub | `+0x6664` | 0x11 | 0 | tentacle-transform |

  Then `SetHullSizeNormal`, complete.
- **Task 0x14c:** `SetForceFrequentThink(1)`.
- **Task 0x14d:** complete.
- **Task 0x14e:** `0x1039ef10`, complete.
- **Task 0x14f** (the evade):
  1. No enemy: line 0x200, fail 6.
  2. The navigator node search around the enemy (512, 30000). None: line 0x20b, fail 7.
  3. Jitter bands on the dominant axis: 0..60 away from the enemy, and ±80 across it.
  4. Up to 5 candidates. Z is lifted by half the step height; Y is drawn before X; each is tested
     with `IsAreaClear` against `0x202400b`. After the fifth refusal the original node is kept.
  5. A run goal there. Refused: line 0x26b, fail 0xc.
  6. Accepted: `+0x667c = RandomFloat(1, 2) + curtime`, complete.
- **Task 0x150:** nothing.
- **Task 0x151** (hide behind the companion):
  1. No companion or enemy: line 0x2a0, fail 1.
  2. Draw `RandomFloat(-20, 20)` three times: z, then y, then x.
  3. The point is `companion + normalize(companion - enemy) * 200 + jitter`.
  4. Not clear: line 0x29a, fail 0x1a.
  5. A run goal. Refused: line 0x294, fail 0xc.
  6. Accepted: `+0x667c = curtime + 600`, `+0x6680 = RandomFloat(5, 15) + curtime`, complete.
- **Task 0x152** (scatter):
  1. An enemy within 512 of this body: search at `(enemy * 3 + scatter centre) / 4`.
  2. Otherwise, or when that search fails: search at the scatter centre. Failure: line 0x2c6,
     fail 7.
  3. A goal. Refused: line 0x2d4, fail 0xc.
  4. Accepted: `+0x667c = curtime + 600`, complete.
- **Task 0x153:** notify the owner, complete.
- **Task 0x154:** activity 0x49.
- **Task 0x155:** the baby death emitter, complete.
- **Task 0x156:** `0x1039ea60` (Misc19), complete.

## CNPC_VSabbatLeader -- 0x103a78c0

**Prologue** (every task): a spawned nova particle (`+0x66e4`) is killed unless the task is 0x161
or 0x162.

**Tasks:**
- **0x36, 0x37:** slot 621 `AttackSound`, then the base.
- **0x6e, 0x92, 0x93, 0xe5:** `CommitSetupJump`. A body still on the jump nav type is stopped
  (motor slot 8), put back on ground nav, and `m_bJumping` is cleared. Then the base.
- **0x14e:** `+0x66b8 = 1`, `m_bIsBossMonster = 1`, `m_flLastAttackTime`, then the base.
- **0x150–0x153, 0x159:** `SelectHintNode` with (0x3e80, 2), (0x3e80, 4), (0x3e81, 2), (0x3e82, 2)
  and (0x3e83, 2).
- **0x154:** the dive-in point. None: line 0x25a, fail 1. Found: complete.
- **0x155:** the teleport archway, complete.
- **0x156:** the dive-out point, complete.
- **0x157:** the ambient run loop, complete.
- **0x158:** `StopSound(2, ambient_run)`, complete.
- **0x15a:** jump origin/target to the hint, height 200, track and dive cleared, complete.
- **0x15b:** needs a live closest player outside the no-jump zone, else line 0x28d, fail 1. Then
  `SetJumpOriginAndTarget(player, 35, 25)`, the leap wav, track the player, complete.
- **0x15c** (dive in):
  1. Gravity 0.1, dive on, activity 0x113f.
  2. `EF_NODRAW`, not solid.
  3. Velocity = delta / 1.1, with Z replaced by `gravity * sv_gravity * 1.1 / 2`.
  4. Jump nav, `m_bJumping`.
- **0x15d** (dive out):
  1. Dive off, `Unhide`, 0x10.
  2. Face the player at speed 50.
  3. The splash wav, the blood pool at the hint.
  4. Activity 0x1140.
- **0x15e:** teleport onto the hint, the warning wav, `+0x66dc = curtime + 1`.
- **0x15f:** `flags2 |= 0x80000800`, complete.
- **0x160:** roar, activity 0x1141.
- **0x161:** nova on, body emitters, particle spawned, activity 0x1142.
- **0x162:** activity 0x1143.
- **0x163:** the blast emitter 80 above the origin, the AOE over 350 when the template resolves,
  activity 0x1144, `m_flLastAttackTime`.
- **Everything else:** VampireBoss.

## CNPC_VScurrying -- 0x103ac740

Also the body of `CNPC_VRat`.

- **Task 0xbc:** `SetActivity(3)`.
- **Task 0x14b:** complete.
- **Task 0x14a** (flee):
  1. A scarer that does not resolve AND a spent scare stamp: line 0xe9, fail 6.
  2. Flee from the scarer at 2 * detection distance, or from the stored scare point at the fright
     distance.
  3. The destination search `0x103acba0`. None: line 0xfa, fail 7.
  4. A run goal. Refused: line 0x107, fail 0xc. Accepted: complete.
- **Everything else:** Animal.

## CNPC_VSheriffMan -- 0x103aec70

- **Task 0x13b:** the land-blast emitter and the AOE over 300 at the origin, then the base.
- **Task 0x150** (teleport out):
  1. `m_bTeleporting`.
  2. The weapon hidden and made non-solid.
  3. The teleport emitter.
  4. `Hide`, `EF_NODRAW`, not solid.
- **Tasks 0x151, 0x155, 0x156, 0x157:** the teleport, centre, ledge(0) and ledge(1) nodes. None
  stamps lines 0x192, 0x1ac, 0x1b4 and 0x1bc and fails 1. Both paths reach the completion.
- **Task 0x152:** only with no hint held: the selection, or line 0x19c and fail 1. No completion.
- **Task 0x153** (teleport in, retail's order):
  1. Onto the hint.
  2. The weapon shown and made solid.
  3. `Unhide`, visible, solid.
  4. The best melee weapon, `KillTeleportBats`, `MatchOriginAnglesToAnimation("bip01", 1, 1)`.
  5. `m_bTeleporting = 0`, 0x10, `RecordHealthPercent`.
  6. The emitter, `m_flLastAttackTime`.
- **Task 0x154:** `OnFinishTransformation`.
- **Task 0x158:** a start-solid stand trace stamps line 0x1c8 and fails 1. The body STILL sets up
  the jump, stamps the attack time and completes; the failure refuses the completion.

## CNPC_VTzimisce -- 0x103ba7c0

- **Task 0x3:** complete.
- **Task 0xf:** `m_ePathMode = 1`, then the base.
- **Tasks 0x89, 0x8a, 0x8b, 0x8e:** as Animal.
- **Task 0xa7:** face the enemy's last-known position, `SetTurnActivity`.
- **Tasks 0xbf, 0xc0:** the carry facing `0x103bf440`, activity 0xf2 / 0xf4.
- **Task 0xc1:** 0xf6 for a heavy body, else 0xf8.
- **Task 0xc2:** the gib clean-up, complete.
- **Task 0xc3:**
  1. `m_ePathMode = 2`.
  2. A live target: `m_vSavePosition = +0x6674`, then the lead helper `0x102c3b50`, the tolerance
     pushed twice, complete.
  3. Otherwise: line 0x722, fail 1.
- **Tasks 0xc8, 0xc9, 0xca:** as Hengeyokai, with timer `+0x66a8` and predicate `0x103be8e0`.
- **Task 0xcb:** `AutoMovement`, face at speed -2, stamp, activity 0x102. Complete when finished.
- **Task 0xcc:**
  1. No enemy: line 0x78c, fail 6.
  2. The pounce check `0x103bf660`. Refused: line 0x7ab, fail 0x1a.
  3. Face, stamp, activity 0x103.
  4. When finished, complete only if the translated activity (slots 375 → 381 → 376) equals
     `m_Activity`.
- **Task 0xcd:** as 0xcc, without the pounce check (no enemy: line 0x7b5), activity 0x104.
- **Task 0xce:** activity 0x105, the same completion.
- **Task 0xd1:** `+0x6324 = (ResolveTaskDistance(data) + 150)^2`, complete.
- **Task 0xd2:** `SetIdealActivity((int)data)`.

## CNPC_VVampireBoss -- 0x103c5ac0

**Prologue:** `+0x669c = curtime` on EVERY task.

**Tasks:**
- **0x2f:** face the hint, `SetTurnActivity`.
- **0x14a:**
  - No hint: line 0xee, fail 4.
  - Otherwise: `SetHullSizeSmall(1)`, `Hide`, `EF_NODRAW`, complete.
- **0x14b:**
  - No hint: line 0xfd, fail 4.
  - Otherwise, in order: onto the hint, `SetHullSizeNormal`, `Unhide`, visible, solid,
    `MatchOriginAnglesToAnimation("bip01")`, 0x10, complete.
- **0x14c:** slot 618 (virtual), complete.
- **0x14d:** nothing.
- **0x14e:**
  - No monster model: line 0x131, fail 4.
  - Otherwise: the model, 0x10, render words 0, hull 0 in both words, `SetHullSizeNormal`,
    complete.
  - The packet named `m_fEffects` as `+0x168`; it is `+0x19c`.
- **0x14f:** `SUB_Remove`, complete.
- **Everything else:** VHuman.

## CNPC_VWerewolf -- 0x103ccda0

The hint machine. The dispatch splits at 0x154, 0x14d and 0x14a, then uses two jump tables:
0x14e..0x153 and 0x155..0x161.

**Tasks 2, 0x4e, 0x100, 0x14a–0x14d:**
- **Task 2:** the Werewolf's own only while `m_pSchedule` is the schedule
  `0x102cc1f0(0x158)` resolves to. It then sets `m_flWaitFinished = curtime +
  werewolf_teleport_in_time` ("2.0", object `0x1093d6f0`, read `IsCommand ? 0 : m_fValue`) and
  does NOT complete. Otherwise the base runs.
- **Task 0x4e:** `m_flGoalTolerance = ResolveTaskDistance(data) + (+0x66d0) + (+0x66cc) + 5.0`,
  pushed twice, complete.
- **Task 0x100:** the Troika body FIRST, then `m_bfAINPCFlags &= ~0x10000`.
- **Task 0x14a:** with no enemy, or an empty `+0x6720`:
  1. `SetClosestPlayer`.
  2. For a live player: `+0x6710` = (template == `Player_Malkavian`),
     `AddEntityRelationship(player, D_HT, 10)`, `SetEnemy`, `SetTarget`, slot 600.
  3. Still no enemy: `DevWarning "%s could not find an enemy... oh well!"`.
  4. Always completes.
- **Task 0x14b:**
  1. The activity: zone bit 0 SET gives a random roar. Otherwise the enemy inside the view cone
     gives 0x100, and outside it the random roar.
  2. The random roar is `RandomInt(0, 1)`: non-zero gives 0x124, zero gives 0x125.
  3. `RestartIdealActivity`, then face the enemy. No completion.
  4. The packet's verdict line had the bit-0 arm backwards.
- **Task 0x14c** (path out of sight): the `TASK_WAIT_FOR_MOVEMENT` start shape.
  1. `COND 0x77`: complete.
  2. A set goal is stopped.
  3. Goal type 0: `m_bShouldMove = 0`, `TaskFail("Did not path out of player's sight")` (the code
     is the string's address `0x10661dac`), then clear the goal.
  4. Goal not active: `m_bShouldMove = 0`, `SetIdealActivity(GetStoppedActivity)`.
  5. Not at the goal (`0x102f2ea0`): `m_bShouldMove = 1`, `ValidateNavGoal`.
  6. At the goal: `m_bShouldMove = 0` and the string failure.
- **Task 0x14d:** nothing.

**Tasks 0x14e–0x153** (teleport and move hints):
- **0x14e:** `TeleportIn`, complete.
- **0x14f:** `dt = max(curtime - +0x66ec, 0)`. With `dt >= 1.0` (or unordered), `TeleportOut`,
  complete. Otherwise fail 0x1a.
- **0x150:**
  - No teleport hint: fail 4.
  - Type 0x3aa9: complete.
  - Otherwise `SetHintActivity`: running, or fail 0x15.
- **0x151:**
  1. No teleport hint: complete.
  2. Blacklist it for 5 s, remember it (`+0x66b4`), release it, clear it.
  3. Resolve `lastused.m_strTargetName` with `FindEntityByName` + `RTDynamicCast<CAI_Hint>` and set
     that hint.
  4. None: complete. Otherwise `SnapToAnimationPoint`, then `SetHintActivity`: running, or
     complete.
- **0x152:** zone 0, blacklist for 5 s unless listed (`0x10366400` also evicts an expired row),
  clear, `SetHullSizeSmall(1)`, `+0x66ec = curtime`, complete.
- **0x153:** needs a move hint, else fail 4. Then the hint debug string (discarded), then the
  hint's groundpoint into `m_vSavePosition`, complete.

**Tasks 0x154–0x161** (move, break and leap hints):
- **0x154:** needs a move hint, else fail 4. The hint's yaw (slot 219, `+4`), flipped by 180 when
  motor `+0x28` is set, stored into motor `+0x34`, then `0x102e1e20(-1)`. No completion.
- **0x155:**
  1. `SetHintActivity(move)`, else fail 0x15.
  2. For type 0x3aa0 only: add a 15 s blacklist row when `0x10366490` answers exactly 0. An
     absent hint (-1) is NOT added. This is the retail bug, reproduced.
  3. Always keeps running.
- **0x156:**
  1. No move hint: complete.
  2. Release it, remember it (`+0x66c0`), add a 3 s row when its index is 0, clear it.
  3. Chain by name: `SetMoveHint(next, m_bRandomHint)`.
  4. None: complete. Otherwise `SetHintActivity`, or `TaskFail(0x15)`, which then FALLS INTO
     `TaskComplete` (the retail defect).
- **0x157:** blacklist for 3 s unless listed, zone 0, clear, `SetHullSizeSmall(1)`, complete.
- **0x158:** `FindBreakHint`, else fail 4. The break hint's groundpoint into `m_vSavePosition`,
  complete.
- **0x159:** needs a break hint and `SetHintActivity`, else fail 0x15.
- **0x15a** (the leap):
  1. Needs a move hint, else fail 4.
  2. Jump origin = the origin; jump target = the hint's endpoint; gravity 2.
  3. Height = `atof(m_iszUserData)` plus a rise term:
     - `dz > 0`: `dz + 100`.
     - Otherwise (including `dz == 0` and unordered): `(|dy| + |dx|) * 0.25`.
  4. Complete.
- **0x15b** (the landing):
  1. `CheckStuck(0)`.
  2. Zone 0, gravity 1.
  3. A 15 s blacklist row UNCONDITIONALLY.
  4. Clear the move hint, `+0x66a8 = 0`, complete.
- **0x15c–0x161:** `RestartIdealActivity` of 0x11c, 0x11d, 0x11f, 0x11e, 0x120 and 0x121, then
  fail 0x15 unless `m_IdealActivity` took. 0x15c first runs `PositionAtHint`. 0x15f, when the
  activity took, also runs `AddSolidFlags(4)` and fires `m_OnBeginCrushAnimation` with the enemy.

**Everything else:** Troika.

## CNPC_VZombie -- 0x103dfd80

Walked in the body's own compare order.

- **Task 0x151:** `m_bShouldMove = 1`, navigator movement activity 0x1014, and
  `m_flWaitFinished = RandomFloat(min, max) + curtime`. The bounds come from the lunge-distance
  fields `+0x250` / `+0x254`; the max getter runs first. A wait drawn from DISTANCE fields is what
  retail does.
- **Task 0x152:** a closest player with a player record: its slot 425 `BeFedOnByZombie(this)`.
  Always completes.
- **Task 0x153:** variant 1/2/3 plays 0x1098 / 0x109b / 0x109e; any other variant keeps
  `m_Activity`. Fail 0x15 unless it took.
- **Task 0x150:** the nearest node. None: fail 0x18. Found: a type-4 goal there, whose answer is
  ignored.
- **Tasks 0x14f, 0x14e:** `SetIdealActivity` 0x4a / 0x1081.
- **Task 0x36:** as Dog.
- **Task 0x14c:** clear the crawl-out flag, `Unhide`, activity 0x1053.
- **Everything else:** Animal.

## CNPC_VFrenzyShadow -- 0x10375f50 (verified)

The landed body was checked against the packet and the listing. Three corrections were made:

1. **Local ids.** The switch compared raw global ids; it now switches on `GetLocalTaskId`. The
   PlayerController tests that set raw 0x14a were corrected.
2. **Attack arguments.** `(0xf18, 1, 1)` is pushed BEFORE the task-0x37 compare (pass R), so every
   attack task makes the same `+0x5d0` call. The port no longer passes a task-0x37 flag.
3. **Tolerance.** `0x102d61b0(0)` is `NAI_Hull::Width` of hull 0 (26.0), multiplied by the DOUBLE
   0.2. The interim store to `+0x6320` comes before slot 418. The landed body used 0.

The other arms matched the listing:
- the prologue's owner hunger copy;
- the 0xae/0xaf hunt;
- the 0x123/0x124 turn with its no-enemy sentinel 1000 and the single clearance try;
- the grapple.

The packet's verdict line named `+0x6320` for `m_vecHuntPatrolTarget`; the listing writes
`+0x645c`.

## Unrecovered

- `RestartIdealActivity` `0x10289ee0` is still a no-op seam. Every species `Restart…` arm decides
  the id; the play is not reproduced.
- The weapon's maximum range (`+0x8c0`) has no port carrier; the 0x9f tolerances drop that term.
- Motor words `+0x18`, `+0x1c`, `+0x28`, `+0x2c` and `+0x38` (clamp centre, clamp range, facing
  flip, yaw-speed override, yaw speed) have no port carrier.
- The navigator:
  - `0x102f2ea0` (at-goal) is a seam that answers false.
  - `0x102edae0` (the node search) answers false.
  - `0x102ee620` / `0x102ee6a0` both read the mover's single goal latch.
- `CNPC_VWerewolf::FindBreakHint` `0x103d0ec0` is a seam that answers none (lane L12's row).
- The player's character template (`GetCharTemplate` against `Player_Malkavian`) is a seam that
  answers not-Malkavian.
- The template AOE's third argument (`template+0xcc`) is dropped. The port's
  `CausePlayerAOEDamage` takes two arguments.
- These calls are counted seams: MingXiao `0x1039a750` and `0x10397f70`; the Tentacle helpers
  `0x1039f310`, `0x1039ef10` and `0x1039ea60`; the ManBat helpers; the Hengeyokai helpers; the Bach
  holy light and weapon `+0x518` / `+0x4f0`.
- The ManBat throw's bone and float cells: `ThrowModel` takes only (model, parent).
