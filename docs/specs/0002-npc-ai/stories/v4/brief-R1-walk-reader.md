# Brief R1 — V4r, packet R1: the walk (one reader, no code, no build; one lab session)

Read `README.md` here (§1 "Turning and the walk", §2 M5–M6, §5). You recover the retail bodies the
V4b coders will port, settle N13's cause with **one** diagnostic lab session, and compute the bound
of the new `face_enemy_turn` record. You write no C++, no Python, no record. Runs in parallel with
R2 (disjoint files).

## Your files (only these)

- `docs/vtmb/npc-ai/shape.md` § "CAI_Motor's unnamed bodies …" (~:2959-3014): slot 18 and slot 15
  with an empty queue.
- `docs/vtmb/animation_and_movers.md` § "Scripted travel speed is the resolved clip's own ground
  speed" (~:1656) / § "One speed pipeline…" (~:1706): how the motor consumes `+0x654`; the turn yaw.
- `docs/specs/0002-npc-ai/stories/v1/triage.md` row N13: the measured cause (append, keep the row).
- Your report: `docs/specs/0002-npc-ai/stories/v4/packets.md`, section "R1" (create the file if R2
  has not; R2 writes section "R2" — append, never overwrite the other's section).

## Packet R1

1. **Slot 18 `0x102e19e0`** (the decompile is in README §1): walk it with addresses. Then slot 15
   `0x102e2180` — **what heading it answers when the queue is empty** (the move direction? the yaw?
   nothing, leaving the stack value?). Who calls slot 18 (`CAI_HumanoidMotor` vfunc19 `0x10264680`,
   `research where 0x10264680`), and `MoveGroundExecute`'s second write of `+0x654`
   (`0x10264841` / `0x10264846`): from what. How `MoveGroundStep 0x102e1760`
   (`schedule-kernel.md` § `CAI_Motor::MoveGroundStep`) turns `GetIdealSpeed` into a step. The
   readers of `m_flDesiredMoveYaw +0x63ec` (where the Troika applies it to the pose parameter).
2. **`FUN_10428690`** (called by `GetSequenceTurnYaw` `0x1008f8f0` with the live pose parameters):
   which field of the sequence's movement it returns. Is it the baked `YawDegrees`
   (`Source/ElysiumUE/Public/.../ElysiumClipMovement.h:56`, `pipeline/.../formats/mdl_skel.py:385`)?
3. **`face_enemy_turn`'s bound.** Stage: a hostile `npc_VHumanCombatant` (`regular_cop`,
   `TutorialThug`, as `sense_enemy_facing_me`), in combat, the player ~135° behind its facing.
   Recover: the activity while `TASK_FACE_ENEMY` runs in combat; the Troika turn ladder `0x10297640`
   (`shape.md` § "The turn-activity ladder") — which rung it takes at 135° and whether it tags
   `m_afMemory |= 0x2000`; then `MaxYawSpeed 0x10297ce0`'s answer (turning arm: `|GetIdealYawSpeed| ×
   cvar(0x10924c94)`, floor 1.0 — say what is known of the cvar; off it, the activity arm) and the
   turn clip's yaw speed on `regular_cop` (`TurnYaw / duration` from the baked clip data, found
   through `uv run elysium research` or the body's clip JSON under `$ELYSIUM_WORK_ROOT`, never a file
   over ~200 KB read whole); `UpdateYaw 0x102e1e20`'s rate law (`int(speed) × 10` per second in the
   port, `ElysiumNpcBaseRunTask.cpp:183-187`) and `FacingIdeal`'s 0.006. Answer: the latest second at
   which retail's `task_face_enemy` completes, with the arithmetic. If a term is unrecovered, give
   the bound under each reading and say which you recommend.
4. **The diagnostic session** (the only run you make; the existing build, no build):
   `uv run elysium gr --arena --headless` (`docs/harness/green-room-arena.md` § "lab"), then
   `elysium.gr_scenario patrol_sentry2_pingpong` over the `elysium` MCP (`elysium_console_exec`).
   Mid-way along the long straight leg, call `elysium_entity_get` on `sentry2` at least five times:
   `speed2d`, `facing_yaw`, `locomotion.move_yaw_vel`, `move_yaw_wish`, `animation`,
   `next_animation`, `axis_fraction` (`Source/ElysiumUE/Private/.../ElysiumMcpTools.cpp:236-246,
   679-698`). Read the walk fan's cells for sentry2's model (`ElysiumBlendGrids` table; the
   `walk_0` / `walk_90` cells) from the baked data. Verdict:
   - `move_yaw_vel ≈ ±90` and `speed2d ≈ 60` → the lead holds; then find **why** the body is yawed
     off its path (a facing-queue entry? `bOrientRotationToMovement` off with no focus? the rotation
     rate?) from the code paths in README §2 M5–M6.
   - `move_yaw_vel ≈ 0` and `speed2d ≈ 60` → the lead is refuted; compare `walk_0`'s cell with
     136.7 cm/s (units: Source units × 2.54 = cm) — a wrong cell or scale is baked data (README §8
     Q2, for the judge).
   - anything else: report the numbers and the next measurement H19 (README §3) would make.
   If the lab cannot stage a record or the MCP read fails, stop, report it, and do not improvise a
   build or a code change: the seam's H19 then makes the measurement in V4a.
   Close the editor when done (the lab session is one boot).

## How to read

- Look every address up first: `uv run elysium research where <addr>`, `research section <addr>`;
  then the `vtmb-corpus` MCP (`vtmb_func`, `vtmb_asm`, `vtmb_code`, `vtmb_callers`, `vtmb_fields`).
  Prefer `vtmb_asm` slices to whole decompiles.
- The query budget: 10 s warns (log the query to `$ELYSIUM_WORK_ROOT/logs/slow-queries.tsv`), 60 s
  stops; a stopped query is not retried as-is or widened. Never read a file over ~200 KB whole.
- Text through the built-in Grep / Read / Glob tools. Wait on a background command (the lab boot)
  by its completion notification or a blocking MCP call, never a sleep or polling loop.
- Every claim carries its address. What you could not settle is written **Unrecovered:** with what
  you tried.

## Report (≤300 words, plus `packets.md` § R1)

`packets.md` § R1: the four items as numbered findings with addresses and the doc section you wrote
each into; item 4's raw samples as a small table. The report: N13's verdict (cause, or what remains),
the `face_enemy_turn` bound, what stays unrecovered, every query over 10 s.
