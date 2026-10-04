# Brief B2 — V4b: the kernel's move facing (`0x102e2180`, `0x102e19e0`) and the turn (coder; no build)

**Final in items 1–3 (amended after V4r and settling packet S1, 2026-10-04 — R1, J9, S1 item 5);
item 4's retail side is now read whole (below); what is still unknown is only why the PORT's turn
took 13.4 s, which reader R1b names** (`packets-R1b.md` § "For B2"). Items 1–3 and 5 do not wait
on R1b; item 4's fix does. Where this brief and `packets-R1.md` item 3 or the README disagree on
`RunTask` 0x2e, this brief wins (`packets-S1.md` item 5). Read `README.md` here (§1 "Turning and
the walk" with its amendment; §2 M4–M6; § "Shared names"), `packets-R1.md` items 1 and 3,
`packets-S1.md` item 5, `packets-R1b.md` when it exists,
`docs/vtmb/npc-ai/shape.md` :419-500, :1460-1500 and § "CAI_Motor's unnamed bodies …" (the
2026-10-04 addendum: slot 15, slot 18, `MoveGroundExecute`). After V4a's commit. Re-locate by Grep.

## Files (only these)

- `Source/ElysiumUE/Private/Substrate/ElysiumNpcMotor10.{cpp,inl}` (slot 18's home; today a comment
  at `.inl:~21`)
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcBaseFacing.cpp` (slot 15's blend ~:131-168,
  `MotorHandFacingTarget` ~:170-188)
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcBaseMotor.cpp` (the move's ideal-yaw copy
  ~:1560-1563 only)
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcThink.cpp` (the `move_yaw` pose write ~:343-344 only)
- `Source/ElysiumUE/Private/Tests/ElysiumNpcKernelFacingTests.cpp`,
  `Source/ElysiumUE/Private/Tests/ElysiumNpcKernelMotorTests.cpp`
- for item 4 only: the one file `packets-R1b.md` names as holding the slow turn's cause, if it is
  none of the above **and not one of B1's** (`Visual/ElysiumNpcBody.{h,cpp}`,
  `Visual/ElysiumNpcMoveScript.{h,cpp}`, `Public/ElysiumWorldServices.h`, the map actor's
  embodiment files, `Tests/ElysiumTestServices.h`) **nor one of V11-1's when V11 shares the wave**
  (`../v11/README.md` §4 — among them `ElysiumNpcBaseStartTask.cpp` and `ElysiumNpcStartTask_2.cpp`,
  where a `TASK_FACE_ENEMY` arm may live: V11-1 owns `StartTaskChooseBestMeleeWeapon` and
  `TaskTailCoordinatorCircleSide` there, you own no function in them); if it is B1's or V11-1's,
  write the exact line in your report for the integrator

## The job

1. **The motor's slot 15 `0x102e2180` returns the influence as well as the vector** (R1 item 1):
   the total interest `1 − Π(1 − wᵢ)` over the facing queue (`102e22de..102e22f5`, returned at
   `102e2318`). **An empty queue answers the zero vector and influence 0.0.** Make the port's
   blend return both. (This is the MOTOR's slot 15; the entity's slot 15, `SetAttackExtents
   0x1009af40`, is another table and V4c's.)
2. **Slot 18 `0x102e19e0` = `CAI_Motor::MoveFacing`, on the kernel** (R1 item 1), in order:
   - owner slot 526 (`+0x838`, `OverrideMoveFacing(move, m_flMoveInterval)`) true → return. *What
     slot 526 answers on the Troika line is unrecovered; the coder reads it first* (`uv run
     elysium research where "slot 526"`); if it stays unread, keep the port's existing answer
     behind a seam named for it.
   - `flMoveYaw = UTIL_VecToYaw(move.dir)` (`move+0x0c`).
   - the sequence has no `move_yaw` pose parameter (`0x102e2820`; the row's fan says so):
     `SetIdealYawAndUpdate(AngleMod(flMoveYaw), −1)` (`0x102e1c10`); the sequence-move-yaw read
     `0x102e2790` is discarded (`102e1a31 FSTP ST0`). Done.
   - with `move_yaw`: `dir = facingDir · w + move.facing · (1 − w)` (`102e1a83..102e1ae6`;
     `facingDir`, `w` from slot 15; `move.facing` at `move+0x18`), normalised;
     `SetIdealYawAndUpdate(AngleMod(VecToYaw(dir)), −1)`; then **`m_flDesiredMoveYaw +0x63ec =
     −UTIL_AngleDiff(flMoveYaw, GetAngles().y)`** (`102e1b4c`, `FCHS 102e1b5d`) when the Troika
     self-cast (`owner+0x98`) resolves, else `SetPoseParameter("move_yaw")` (`0x102e27d0`).
     **So with an empty queue the heading is `move.facing`, whole.** `AngleMod` is the 16-bit
     quantisation (`& 0xffff × 0.0054931640625`, `_DAT_1044ffdc`).
   - **`move.facing` is `MoveGroundExecute`'s** (`0x10264680`): `UTIL_YawToVector(yaw)` where
     `yaw` is the turn script's answer (the direction along the path, eased through corners) or,
     with no script, the current yaw (`GetLocalAngles().y`, owner `+0x36c`), `AngleMod`-quantised.
     The path is the body's (K1): ask it through `GetNpcMoveFacingYaw(float&)` (README § "Shared
     names"; B1 adds it and owns that header — you only call it); on false take the current yaw.
   - `m_flDesiredMoveYaw` is what V4a's clock reads for the fan through the `move_yaw` pose
     parameter; its one reader is `0x102bf310` (the port calls it from `Think19NormalSet2`) — the
     `move_yaw` write at `ElysiumNpcThink.cpp:~343-344` is reconciled with it, one writer, cited.
     Its zeroing writers (`TaskFail 0x1029adb0`, `OnScheduleChange 0x102a0940`, `RunTask
     0x102aacf0`, `0x102bf770`) are checked by Grep; a missing one is reported with its line, not
     added outside your files.
   - The verdict "mechanism" (`kernel_verdicts.tsv:1466`) is narrowed: the kernel computes the
     words, the body moves (K1) — report the row's new text for the integrator's ledger step.
   - Not ported here, named at the line: `MoveGroundExecute`'s second `+0x654` write
     (`0x10264841/46`, right after slot 18) — write it where the port runs slot 18 if `GroundSpeed`
     is reachable there, else report the line for the integrator.
3. **The move's ideal yaw** (`ElysiumNpcBaseMotor.cpp:~1560-1563`, a named divergence copying the
   body's yaw into the kernel's ideal yaw during a move): replaced by slot 18's
   `SetIdealYawAndUpdate` — that is where retail writes it (R1 item 1). If removing the copy
   leaves a caller that still needs the body's yaw, say which and why, and leave that one read.
4. **The slow turn.** Retail, read whole (`packets-S1.md` item 5) — check the port's
   `TASK_FACE_ENEMY` against it arm by arm before reading R1b's cause:
   - **`StartTask`, Troika arm `0x102a4417`**: point = `m_hShootTargetOverride (+0x5ba8)`'s
     `GetAbsOrigin` when it resolves, else the enemy's last known position
     (`GetEnemies()->0x102dfed0(enemy)`). Slot 364 `FInAimCone(point)` true → complete. False →
     **the turn tail `0x102a44d1`**: `0x102e0b40(motor)` (`m_flLastYawTime motor+0x2c = −1.0`);
     `0x102e2020(motor, &point, 0)` — the ideal yaw to the point (`± 180` when `motor+0x28`,
     clamped to `motor+0x18 ± motor+0x1c` unless `motor+0x1c == 180.0`), **no update**; slot 572
     `SetTurnActivity`; RUNNING.
   - **`RunTask`, Troika arm `0x102aae61`** (index 4 of table `0x102ac760`) — **not the base
     `0x102889b5`**: `m_afMemory (+0x5d8c) & 0x2000` clear → slot 572 `SetTurnActivity`, **every
     call**; the same point; `0x102e20b0(motor, &point, −1.0)` = `SetIdealYawAndUpdate(yaw, −1)`:
     the ideal yaw as above, then because the speed is `−1.0`, `0x102e1cf0` **re-reads
     `MaxYawSpeed` into `motor+0x38` every call**, then `UpdateYaw(−1)`; `FacingIdeal 0x10278c80`
     (`|DeltaIdealYaw| <= 0.006`, double `0x10499568`) → `TaskComplete`, else running. **It does
     not call `0x102e0b40`: the yaw clock is reset only by `StartTask`'s turn tail.**
   - **`UpdateYaw 0x102e1e20(speed)`**: `−1` → `(int) motor+0x38`. `current` = the owner's local
     yaw, `ideal = motor+0x34`, each quantised (`× 182.0444`, `& 0xffff`, `×
     0.0054931640625`). `m_flLastYawTime < 0` → `= curtime − 0.1`. `new = AI_ClampYaw(speed ×
     10.0, current, ideal, curtime − m_flLastYawTime)`; `m_flLastYawTime = curtime`; `new !=
     current` → `SetLocalAngles`. So the first `RunTask` integrates 0.1 s and later calls **the
     real time between thinks**.
   - **`AI_ClampYaw 0x102e1d10(rate, current, target, dt)`** (in `vampire.dll`): equal → target;
     `step = rate × dt`; `move = target − current`; `target > current`: `move >= 180` → `− 360`;
     else `move <= −180` → `+ 360`; `move > 0`: `min(move, step)`; else `max(move, −step)`;
     return `current + move` quantised.
   - `MaxYawSpeed 0x10297ce0` answers 90 untagged in combat with `m_Activity` 1 or 5, else 45: at
     90 that is 900°/s, 135° in two calls. **The 0.5 s bound stands.** The turn ladder
     `0x10297640` is closed for a Troika human and **`0x2000` is never tagged** — do not port a
     "turn rung" for humans, and do not rely on V4a's `YawSpeed` (it is 0 on shipped data).
   An arm above that the port lacks or does differently is a divergence: restore it at its line
   with the address when the line is in your files, else write the exact line in your report for
   the integrator. **Why the port's `task_face_enemy` ran 13.4 s is unrecovered; reader R1b settles
   it before item 4 starts** (`brief-R1b-slow-turn-reader.md`, from A0's measurement): restore the
   one divergence R1b names, at the address it cites. **If `packets-R1b.md` says "not determined", you do not fix the turn
   on a guess**: leave item 4 undone and say so; `face_enemy_turn` then stays red with its
   `known_red` naming the unread cause.
5. **Tests**: `Elysium.Arm.NpcKernelMotor.MoveYaw` (`0x102e19e0`): a one-entry queue with
   influence `w` → the blended heading and the negated angle difference in `+0x63ec`; the empty
   queue → `move.facing` whole; no `move_yaw` on the sequence → the reissue at `flMoveYaw` only;
   slot 526 true → nothing written. `Elysium.Arm.NpcKernelFacing.Influence` (`0x102e2180`: `1 −
   Π(1 − wᵢ)`; empty → zero vector, 0.0). For item 4, an arm test at the address R1b names.

## Not yours

The body, the velocity script and the embodiment accessors (B1), the clock and its speed words
(V4a), the facing queue's adders (already retail), the fan.

## Rules

README § "Rules for every agent of V4": no build, no editor, no suite; only your files; cross-lane
lines in the report. The query budget (10 s warns, 60 s stops; never a file over ~200 KB whole).
Text through Grep / Read / Glob. Do not commit. Report ≤300 words.
