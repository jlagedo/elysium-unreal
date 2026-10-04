# Brief R1b — V4b's short read: why the port's `task_face_enemy` ran 13.4 s (reader; no code, no build, no run)

**New after V4r (2026-10-04), by the judge's ruling J9; narrowed after settling packet S1.** One
agent, one short read, before lane B2's item 4. **Lane B1 no longer waits on you**: the stop's
three constants (50.0, 0.2, 150.0), the five passes of `0x102630b0` and the arrival tolerance
(0.0625 units, 2-D, `0x102ef510`) are read (`packets-S1.md` items 5–6) and are in
`brief-B1-body-speed.md`; retail's `TASK_FACE_ENEMY` (`StartTask`'s turn tail `0x102a44d1`,
`RunTask`'s Troika arm `0x102aae61`, `UpdateYaw 0x102e1e20`, `AI_ClampYaw 0x102e1d10`) is read
whole too. **One question is left: the port's side of the slow turn.** You write no code and run
nothing: the measurement is already made (the seam agent A0,
`stories/v4/packets-R1b-measurement.md`).

Read `README.md` here (§1 "Turning and the walk" with its amendment; §2 M4, M6; §7 K1),
`packets-R1.md` (items 1 and 3 and their "Unrecovered" lines), `packets-S1.md` item 5 (retail's
turn, the authority where R1 and it disagree), the ruling J9
(`stories/v1/triage.md` § "Judge's rulings, V4"; Grep, read only it),
`packets-R1b-measurement.md`, `docs/vtmb/npc-ai/shape.md` § "CAI_Motor's unnamed bodies …" (the
2026-10-04 addendum) and its yaw ladders (~:1460-1500).

## What is known (do not re-derive)

Retail turns a Troika human 135° in two `RunTask` calls, ≤ 0.2 s: the turn ladder `0x10297640`
is closed (`debug_turning` 0, `m_bAllowTurningAnims +0x65f9` 0), `0x2000` is never tagged,
`MaxYawSpeed 0x10297ce0` answers 90 in combat with `m_Activity` 1 or 5 (else 45). **`RunTask`
0x2e on a Troika NPC is `0x102aae61`, not the base `0x102889b5`** (`packets-S1.md` item 5): it
calls `SetTurnActivity` (slot 572) every call unless `m_afMemory & 0x2000`, re-reads `MaxYawSpeed`
into `motor+0x38` every call (`0x102e20b0(motor, &point, −1.0)` → `0x102e1cf0`), and **does not
reset the yaw clock** — only `StartTask`'s turn tail `0x102a44d1` does (`0x102e0b40`:
`m_flLastYawTime = −1`). So `UpdateYaw 0x102e1e20` integrates 0.1 s on the first call and the real
time between thinks after it, through `AI_ClampYaw 0x102e1d10(speed × 10.0, current, ideal, dt)`:
900°/s at 90. The point turned to is `m_hShootTargetOverride +0x5ba8` when it resolves, else the
enemy's last known position. The port took **13.4 s** — 135° at 10°/s, the 1.0 floor of the *tagged*
arm (`|TurnYaw / duration| × debug_turn_scalar 0.15`, floor 1.0). R1 found the port's
`MaxYawSpeed` arm-for-arm and nothing tagging `0x2000` on a human, so README §2 M4's first cause
is refuted and the real one is unread.

## The question

1. **Name the arm that reaches the 1.0 floor in the port** — from A0's trace and its samples of
   `max_yaw_speed`, `m_afMemory`, `m_Activity`, the yaw and the ideal yaw during the turn. Decide
   between, at least: (a) the port tags `0x2000` where retail does not (which writer; is the
   port's `m_bAllowTurningAnims` or its `debug_turning` read different from retail's); (b)
   `MaxYawSpeed` answers 90 or 45 and the loss is elsewhere — the per-call integration (the
   port's `StartTask` turn tail does not reset `m_flLastYawTime` to −1, the port's `RunTask`
   0x2e does not re-read `MaxYawSpeed` or does not call `UpdateYaw` every call, or `UpdateYaw`'s
   port drops the `× 10.0` or the elapsed time), the think rate, `AI_ClampYaw`'s port against
   `0x102e1d10`, or the body's own turn rate overriding the kernel's yaw (K1: the motor on the
   body's tick); (c) `m_Activity` during the task is neither 1 nor 5 and another ladder arm
   answers; (d) the port runs the base arm `0x102889b5`'s shape where the Troika arm
   `0x102aae61` applies; (e) other, as the measurement shows. Read the port's
   body for the arm you name (Grep the function; cite the file, the function and the line) and
   the retail address it diverges from. **One divergence, named**, or "not determined" with what
   measurement would decide it — never a guess. Retail's bodies are not yours to re-read: they
   are in `packets-S1.md` item 5; re-open a listing only where the port's arm needs a detail
   that packet does not carry.

**Settled, not yours** (`packets-S1.md`): the three constants (`0x104493c0` float64 50.0,
`0x10449198` float64 0.2, `0x10457f60` float32 150.0), the arrival tolerance (0.0625 units,
0.119 cm, 2-D; the port's 1.0 cm floor is not retail's — the judge rules on it), `AI_ClampYaw`
(`0x102e1d10`, in `vampire.dll`), `StartTask` 0x2e's turn tail.

## Method

Look an address up before searching: `uv run elysium research where <addr>` / `research section
<addr>`, the `vtmb-corpus` tools for the listing and the constants' data. Text through Grep / Read
/ Glob. The query budget: 10 s warns (log to `$ELYSIUM_WORK_ROOT/logs/slow-queries.tsv`), 60 s
stops — never retried as-is or widened; never read a file over ~200 KB whole. Mark every claim
**[listing]**, **[measured]** (A0's), **[port]** (read in `Source/`) or **[inferred]**.

## Output

- `stories/v4/packets-R1b.md`: the answer with its addresses, in one section **"For B2"** (the
  slow turn's arm, the port file and function that hold it, the retail behaviour to restore),
  and **"Unrecovered"**. No "For B1" section: B1 has its inputs.
- A retail fact recovered on the way goes into the matching `docs/vtmb/` document (look it up, do
  not create a document); S1 already wrote the turn into `npc-ai/schedule-kernel.md`.
- Nothing else is edited. Do not commit.

Report ≤200 words: the arm named (or "not determined" and why), the file B2 must touch if it is
outside B2's list (and whether it is one of B1's or V11-1's), slow queries if any.
