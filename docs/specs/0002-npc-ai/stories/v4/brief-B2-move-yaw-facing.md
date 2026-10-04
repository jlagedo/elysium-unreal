# Brief B2 — V4b: the kernel's move facing (`0x102e2180`, `0x102e19e0`) and the turn (coder; no build)

**Final in items 1–3 (amended after V4r, 2026-10-04 — R1 and J9); item 4, the slow turn, is final
once reader R1b has named its cause** (`packets-R1b.md` § "For B2"). Do not start before
`packets-R1b.md` exists. Read `README.md` here (§1 "Turning and the walk" with its amendment; §2
M4–M6; § "Shared names"), `packets-R1.md` items 1 and 3, `packets-R1b.md`,
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
  embodiment files, `Tests/ElysiumTestServices.h`); if it is B1's, write the exact line in your
  report for the integrator

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
4. **The slow turn — from `packets-R1b.md` § "For B2".** Known and fixed for you: retail's bound
   is 0.5 s (`MaxYawSpeed 0x10297ce0` answers 90 untagged in combat with `m_Activity` 1 or 5,
   else 45; `RunTask` 0x2e `0x102889b5` sets `m_flLastYawTime = −1` each call, so `UpdateYaw
   0x102e1e20` integrates 0.1 s: 90° per call); the turn ladder `0x10297640` is closed for a
   Troika human and **`0x2000` is never tagged** — do not port a "turn rung" for humans, and do
   not rely on V4a's `YawSpeed` (it is 0 on shipped data). Restore the one divergence R1b names,
   at the address it cites. **If `packets-R1b.md` says "not determined", you do not fix the turn
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
