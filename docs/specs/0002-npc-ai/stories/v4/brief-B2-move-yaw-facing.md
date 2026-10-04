# Brief B2 — V4b: the kernel's `move_yaw` (`0x102e19e0`) and the turn (coder; no build) — PROVISIONAL until R1

Read `README.md` here (§1 "Turning and the walk"; §2 M4–M6), `packets.md` § R1 items 1 and 3,
`docs/vtmb/npc-ai/shape.md` :419-500 and :1460-1500 and § "CAI_Motor's unnamed bodies" (slot 18).
After V4a's commit. **Final only once R1 is in**; the marked parts are filled from it. Re-locate by
Grep.

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

## The job

1. **Slot 18 `0x102e19e0`'s steering half on the kernel**, as the shape.md walk and R1 item 1 state
   it: past the owner's `+0x838` test, when `m_nSequence` carries `move_yaw` (the row's fan says so),
   the heading is slot 15's answer (**with an empty queue: R1's finding — TO BE FILLED**), the ideal
   yaw is reissued at the quantised heading (`& 0xffff × 0.0054931640625`, `_DAT_1044ffdc`), and
   `-(AngleDiff(heading, GetAbsAngles().y))` is written to `m_flDesiredMoveYaw +0x63ec` (the Troika)
   — the value V4a's clock reads for the fan through the `move_yaw` pose parameter. Without
   `move_yaw` on the sequence, the reissue at the quantised yaw only. The verdict "mechanism"
   (`kernel_verdicts.tsv:1466`) is narrowed: the kernel computes the words, the body moves (K1).
2. **The move's ideal yaw** (`ElysiumNpcBaseMotor.cpp:~1560-1563`, a named divergence copying the
   body's yaw): replaced by slot 18's reissue if R1 shows that is where retail writes it; otherwise
   leave it and say why.
3. **The turn**: with V4a's `YawSpeed`, the Troika `MaxYawSpeed`'s turning arm already reads a real
   value. Check, against R1 item 3, that `task_face_enemy` takes the turn ladder's rung and tag the
   way retail does on the `face_enemy_turn` staging; port the missing piece R1 names, if any (the
   ladder `0x10297640` is ported — Grep `SetTurnActivity`).
4. **Tests**: `Elysium.Arm.NpcKernelMotor.MoveYaw` (slot 18: heading from a one-entry queue → the
   negated angle difference in `+0x63ec`; the empty-queue case as R1 reads it; no `move_yaw` on the
   sequence → reissue only).

## Not yours

The body (B1), the clock and its speed words (V4a), the facing queue's adders (already retail).

## Rules

README § "Rules for every agent of V4": no build, no editor, no suite; only your files; cross-lane
lines in the report. The query budget (10 s warns, 60 s stops). Text through Grep / Read / Glob. Do
not commit. Report ≤300 words.
