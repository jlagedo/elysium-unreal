# Brief R1b — V4b's short read: the slow turn's cause, and the stop's constants (reader; no code, no build, no run)

**New after V4r (2026-10-04), by the judge's ruling J9.** One agent, one short read, before lanes
B1 and B2 start. You write no code and run nothing: the measurement is already made (the seam
agent A0, `stories/v4/packets-R1b-measurement.md`).

Read `README.md` here (§1 "Turning and the walk" with its amendment; §2 M4, M6; §7 K1),
`packets-R1.md` (items 1 and 3 and their "Unrecovered" lines), the ruling J9
(`stories/v1/triage.md` § "Judge's rulings, V4"; Grep, read only it),
`packets-R1b-measurement.md`, `docs/vtmb/npc-ai/shape.md` § "CAI_Motor's unnamed bodies …" (the
2026-10-04 addendum) and its yaw ladders (~:1460-1500).

## What is known (do not re-derive)

Retail turns a Troika human 135° in two `RunTask` calls, ≤ 0.2 s: the turn ladder `0x10297640`
is closed (`debug_turning` 0, `m_bAllowTurningAnims +0x65f9` 0), `0x2000` is never tagged,
`MaxYawSpeed 0x10297ce0` answers 90 in combat with `m_Activity` 1 or 5 (else 45), and `RunTask`
0x2e (`0x102889b5`) sets `m_flLastYawTime = −1` each call so `UpdateYaw 0x102e1e20` integrates
0.1 s: 90° per call. The port took **13.4 s** — 135° at 10°/s, the 1.0 floor of the *tagged*
arm (`|TurnYaw / duration| × debug_turn_scalar 0.15`, floor 1.0). R1 found the port's
`MaxYawSpeed` arm-for-arm and nothing tagging `0x2000` on a human, so README §2 M4's first cause
is refuted and the real one is unread.

## The questions

1. **Name the arm that reaches the 1.0 floor in the port** — from A0's trace and its samples of
   `max_yaw_speed`, `m_afMemory`, `m_Activity`, the yaw and the ideal yaw during the turn. Decide
   between, at least: (a) the port tags `0x2000` where retail does not (which writer; is the
   port's `m_bAllowTurningAnims` or its `debug_turning` read different from retail's); (b)
   `MaxYawSpeed` answers 90 or 45 and the loss is elsewhere — the per-call integration (the port
   does not reset `m_flLastYawTime` to −1 in `RunTask` 0x2e, or integrates the frame's dt instead
   of 0.1 s), the think rate, `AI_ClampYaw`'s port, or the body's own turn rate overriding the
   kernel's yaw (K1: the motor on the body's tick); (c) `m_Activity` during the task is neither 1
   nor 5 and another ladder arm answers; (d) other, as the measurement shows. Read the port's
   body for the arm you name (Grep the function; cite the file, the function and the line) and
   the retail address it diverges from. **One divergence, named**, or "not determined" with what
   measurement would decide it — never a guess.
   Also read, in the listing, the two bodies R1 left: `StartTask` 0x2e's "turn tail" and, if the
   cause points at it, `AI_ClampYaw` (engine; say so if it is not in the corpus).
2. **The three constants** R1 left unread, by address, value and type: `_DAT_104493c0` (the
   velocity script's acceleration term: acceleration = ideal + it), `_DAT_10449198` (the corner
   term: `clamp(dot(in, out) + it, 0, 1)`), `_DAT_10457f60` (the turn script's backward rate
   limit). State the units.
3. **The retail arrival tolerance**: what distance to the goal ends a ground move in retail (the
   navigator's or the motor's own test on the path's last waypoint — cite the body), against the
   port's follower acceptance radius `max(FollowerArrivalFloorCm 1.0, 0.0625 units)`
   (`Visual/ElysiumNpcBody.h`, `ResolveFollowerRequest`; `MakeNavigatorMoveRequest`,
   `ElysiumNpcBaseStartTask.cpp`). Say whether the 1 cm floor is retail's, in units and cm.

## Method

Look an address up before searching: `uv run elysium research where <addr>` / `research section
<addr>`, the `vtmb-corpus` tools for the listing and the constants' data. Text through Grep / Read
/ Glob. The query budget: 10 s warns (log to `$ELYSIUM_WORK_ROOT/logs/slow-queries.tsv`), 60 s
stops — never retried as-is or widened; never read a file over ~200 KB whole. Mark every claim
**[listing]**, **[measured]** (A0's), **[port]** (read in `Source/`) or **[inferred]**.

## Output

- `stories/v4/packets-R1b.md`: the three answers, each with its address; then two short sections,
  **"For B1"** (the constants, the tolerance, anything that changes the velocity script's port)
  and **"For B2"** (the slow turn's arm, the port file and function that hold it, the retail
  behaviour to restore), and **"Unrecovered"**.
- The recovered facts into the matching `docs/vtmb/` document: the constants beside R1's addendum
  in `npc-ai/shape.md`; the arrival tolerance where the navigator's arrival is walked (look it up,
  do not create a document).
- Nothing else is edited. Do not commit.

Report ≤200 words: the arm named (or "not determined" and why), the three values, the tolerance,
the file B2 must touch if it is outside B2's list, slow queries if any.
