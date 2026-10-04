# V4r — packet R1: the walk (reader R1, 2026-10-04)

Read-only; one lab session on the existing build (closed). Marks: **[listing]** read in the corpus
this session; **[measured]** the lab session; **[data]** a pipeline sidecar; **[inferred]** not
verified. No query ran over 10 s.

## 1. Slot 18 `0x102e19e0`, slot 15 `0x102e2180`, `MoveGroundExecute 0x10264680`

Written into `docs/vtmb/npc-ai/shape.md` § "CAI_Motor's unnamed bodies …" (the 2026-10-04 addendum).

- **Slot 15 `0x102e2180` returns a float as well as the vector** [listing]: the total interest
  `1 − Π(1 − wᵢ)` (`102e22de..102e22f5`, returned at `102e2318 FLD [ESP+0x14]`). **An empty queue
  answers the zero vector and influence 0.0** (the out vector is zeroed at entry; the loop does not
  run).
- **Slot 18 `0x102e19e0`** [listing] is the SDK's `CAI_Motor::MoveFacing`. Owner slot 526
  (`+0x838`, `OverrideMoveFacing(move, m_flMoveInterval)`) true → return. `flMoveYaw =
  UTIL_VecToYaw(move+0x0c)` (the move's `dir`). Sequence without `move_yaw` (`0x102e2820`):
  `SetIdealYawAndUpdate(AngleMod(flMoveYaw), −1)` (`0x102e1c10`; the sequence-move-yaw read
  `0x102e2790` is discarded, `102e1a31 FSTP ST0`). With `move_yaw`: `dir = facingDir·w +
  move.facing(move+0x18)·(1 − w)` (`102e1a83..102e1ae6`), normalised, `SetIdealYawAndUpdate(
  AngleMod(VecToYaw(dir)), −1)`; then `−UTIL_AngleDiff(flMoveYaw, GetAngles().y)` (`102e1b4c`,
  `FCHS 102e1b5d`) to the owner's `m_flDesiredMoveYaw +0x63ec` when `owner+0x98` (the Troika
  self-cast) resolves, else `SetPoseParameter("move_yaw")` (`0x102e27d0`).
  **So with an empty queue the heading is `move.facing`, whole.**
- **Who calls it and what `move.facing` is** [listing]: `CAI_HumanoidMotor` vfunc 19 `0x10264680`
  (the SDK's `CAI_BlendedMotor::MoveGroundExecute`; one direct caller, thunk `0x1000dc24`). It
  rebuilds the move script (`0x10262590` → velocity script `0x102630b0`, turn script `0x102627e0`),
  takes `flNewSpeed = |m_vecVelocity (motor+0x3c)|` or, with > 1 script entries, the velocity script
  (`owner+0x6024`, count `+0x6030`, stride `0x38`) interpolated at `m_flMoveInterval (motor+0x30)`;
  takes the yaw = `GetLocalAngles().y` (owner `+0x36c`) or the turn script (`owner+0x6038`, count
  `+0x6044`) interpolated the same way and `AngleMod`-quantised; **copies the move (0x1f dwords),
  overwrites the copy's `facing` (+0x18) with `UTIL_YawToVector(yaw)` (`0x101d2f40`), and calls slot
  18 on the copy** (`1026482f CALL [EDX+0x48]`). The turn script (`0x102627e0`) is the direction
  from each waypoint to the next, rate-limited backwards (`_DAT_10457f60`): a walking NPC with no
  facing target faces along its path, eased through corners.
- **The second `+0x654` write** [listing]: `10264841 CALL GetSequenceGroundSpeed(m_nSequence)` /
  `10264846 FSTP [ESI+0x654]`, immediately after slot 18 — the same pose-weighted read as
  `StudioFrameAdvance`, refreshed after the move's facing/`move_yaw` step.
- **How `GetIdealSpeed` becomes distance** [listing]: `0x102630b0` reads owner slot 248 (`+0x3e0`,
  `GetIdealSpeed 0x10091740`) as the ideal velocity (**50.0 when it answers 0**), acceleration =
  ideal + `_DAT_104493c0`; each waypoint's speed is `ideal × clamp(dot(in, out) + _DAT_10449198,
  0, 1)` and **the last waypoint's is 0**; forward/backward passes limit by constant acceleration.
  `MoveGroundExecute` then steps `(|m_vecVelocity| + flNewSpeed) × interval × 0.5`
  (`_DAT_10449270`), clamps to `move.maxDist (+0x28)` as `MoveGroundStep 0x102e1760` does, writes
  `m_vecVelocity = move.dir × flNewSpeed`, and moves through `0x102e0bd0`. So retail accelerates
  to, cruises at, and **decelerates at a constant rate from** `+0x654`; the stop is finite.
- **Readers of `m_flDesiredMoveYaw +0x63ec`** [listing]: one — `0x102bf310`,
  `SetPoseParameter("move_yaw", +0x63ec, 0)` through slot 345 (`+0x564`); it has no static caller
  (dispatched; the port calls it from `Think19NormalSet2`). Writers: slot 18; `0x102a9940`; zeroed
  by `TaskFail 0x1029adb0`, `OnScheduleChange 0x102a0940`, `RunTask 0x102aacf0`, `0x102bf770`,
  `CNPC_VFrenzyShadow::StartTask 0x10375f50`, `CNPC_VTzimisceRunner::RunTask 0x103c3870`.
- **Unrecovered:** `_DAT_104493c0`, `_DAT_10449198`, `_DAT_10457f60` (values not read);
  `0x102e0bd0`; what slot 526 answers on the Troika line.

## 2. `GetSequenceTurnYaw 0x1008f8f0` → `FUN_10428690`

Written into `docs/vtmb/animation_and_movers.md` § "One speed pipeline…" (addendum).

`0x10428690` is `GetSequenceLinearMotion`'s twin with the angle kept: `Studio_SeqMovement
0x100c5d10(hdr, seq, 0.0, 1.0, poseParams, &pos, &angles)`; `GetSequenceTurnYaw` returns
**`angles[1]`** (`1008fa36 FLD [ESP+0x14]`). `0x100c5b00` (`Studio_AnimMovement`) writes only
`angles.y = yaw(cycle 1) − yaw(cycle 0)`; `0x100c57e0` (`Studio_AnimPosition`) reads the yaw from
the movement record's `+0x10` (`angle`). **So it is the pose-weighted sum, over the up-to-four blend
corners, of the last movement record's `angle` — the baked `YawDegrees`** (`ElysiumClipMovement.h`,
`mdl_skel.py` `Movement.angle`). That header states every shipped record's angle is 0.0, so
`GetSequenceTurnYaw` and `GetSequenceYawSpeed 0x10091310` answer **0 on shipped data** [doc, not
re-counted].

## 3. `face_enemy_turn`'s bound

- **The turn ladder is closed for a Troika human** [listing + data]: `0x10297640` runs only under
  `debug_turning` (`0x109247ec`, default `"0"`, `convars.md:44`) `|| m_bAllowTurningAnims +0x65f9`.
  `+0x65f9` is datamap `FIELD_BOOLEAN`, flags 2 (save only, **not a keyfield**); the Troika ctor
  `0x1028d230` writes 0 and the only other writer is the ctor `0x103b6c60` (Tzimisce address range;
  class not confirmed). So `SetTurnActivity` falls to `SetIdealActivity(ACT_IDLE)` and **`0x2000`
  is never tagged**; the lab showed `m_bAllowTurningAnims 0`, `m_afMemory 0x40000` on sentry2.
- **`MaxYawSpeed 0x10297ce0`**, untagged, combat (state-flags bit 7), `m_Activity` 1 or 5:
  `debug_turning` 0 → `debug_turning_speed` (`0x10923e84`) = **90**. Any other activity: 45.
- **Rate**: `RunTask` 0x2e (`0x102889b5`): `0x102e0b40` sets `m_flLastYawTime = −1` every call, so
  `UpdateYaw 0x102e1e20` always integrates over 0.1 s: `int(90) × 10 × 0.1` = **90° per call**; then
  `FacingIdeal` (`|Δ| <= 0.006`).
- **Arithmetic**: 135° → call 1 leaves 45°, call 2 reaches the ideal → complete on the 2nd
  `RunTask`, ≤ 0.2 s after the task starts at a 0.1 s think (0.3 s under the 45 reading).
  **Recommended bound: `taskdone` within 0.5 s of `task_face_enemy`.**
- The turning arm (were it reachable): `|TurnYaw / duration| × debug_turn_scalar 0.15`, floor 1.0;
  TurnYaw is 0 on shipped data (item 2), so 10°/s — 13.5 s for 135°. The male `move_and_ranged`
  vocabulary names none of the six turn activities (string search of the sidecar) [data].
- **Unrecovered:** why the port's `task_face_enemy` ran 13.4 s. The port's `MaxYawSpeed` is already
  arm-for-arm, and nothing tags `0x2000` on a human, so README §2 M4's cause (the 1.0 floor) is
  doubtful; 13.4 s ≈ 135° at 10°/s says some path does reach the floor. Not measured (the record
  does not exist yet). Also unread: `StartTask` 0x2e's "turn tail" body; `AI_ClampYaw` (engine).

## 4. The diagnostic session — N13

`uv run elysium gr --arena --headless`, existing build. `elysium.gr_scenario
patrol_sentry2_pingpong` **was refused** ("runs on sp_tutorial_1's own entities, not in the lab"),
so the same NPC was measured on `input_clearpatrolpath` (sentry2 from the map, flat arena floor, N13
red), run three times. Session closed with `quit` (exit 0).

Mid-leg, leg 1 (0,850)→(−500,600), seven `elysium_entity_get sentry2` samples over two runs, all
identical but the position:

| field | value |
|---|---|
| `speed2d` | 101.278 cm/s (constant) |
| `local_velocity` | (101.278, 0.000003, 0) |
| `facing_yaw` | −153.435 (the leg's own direction) |
| `move_yaw_vel` / `move_yaw_wish` | 0 / 0 |
| `m_flDesiredMoveYaw` | 0.0 |
| `animation` → `next_animation` | `walk_270` → `walk_315` |
| `axis_fraction` | 0.5567 (0.5486 in run 1), constant for the whole leg |
| positions | 428.1 cm in 4.258 s between two samples = 100.5 cm/s |

Approach to the node (run 3; distance to the goal against `m_flLastAIThink`, zero ≈ 129.7):

| t (s) | 4.36 | 5.07 | 5.79 | 6.50 | 7.20 | 7.91 | 8.53 | 9.34 | 9.76 |
|---|---|---|---|---|---|---|---|---|---|
| distance (cm) | 165.6 | 96.3 | 30.4 | 10.5 | 6.7 | 4.3 | 2.7 | 1.7 | 1.4 |

Trace (`Saved/Elysium/_arena/lab/input_clearpatrolpath.trace.tsv`): `0.709 goal`, footsteps
2050/2051 until 6.281, **`10.428 arrived`**.

The walk fan (`$ELYSIUM_WORK_ROOT/import/characters/character/shared/{female,male}/
move_and_ranged.clips.json`, cm/s) [data]:

| cell | 180 | 225 | 270 | 315 | **0** | 45 | 90 | 135 |
|---|---|---|---|---|---|---|---|---|
| female | — | 116.93 | 99.67 | 90.39 | **101.278** | 90.39 | 62.29 | 116.93 |
| male | — | 113.92 | 97.10 | 88.06 | **136.683** | 88.06 | 60.69 | 113.92 |

**Verdict.**
1. **The lead is refuted** [measured]: the body faces its path, `move_yaw_vel` is 0.
2. **The cruise speed is not wrong** [measured + data]: sentry2's model
   (`vampire_hunter_chick`) is on the **female** bank, whose `walk_0` is 101.278 cm/s — exactly the
   measured `speed2d`. 136.7 cm/s is the **male** `walk_0`; the "0.44×" compared a female body with
   the male cell. No baked cell or scale is at fault: **nothing for the judge (§8 Q2 is moot)**.
3. **N13's cause is the arrival** [measured; mechanism inferred from code]: the body covers the leg
   in ~5.6 s, then creeps the last ~30 cm exponentially (distance × ~0.53 per 0.7 s) for **~4.1 s**
   until it is inside the follower's radius, and only then does the kernel see `arrived`. The body
   is a `UCrowdFollowingComponent` agent (`Visual/ElysiumNpcBody.cpp` `ApplyCrowdState`,
   `MoveTo`); nothing calls `SetCrowdSlowdownAtGoal(false)`, so Detour scales the speed by the
   distance to the goal, and the acceptance radius is `max(FollowerArrivalFloorCm 1.0,
   0.0625 units)` = 1 cm (`ResolveFollowerRequest`; `MakeNavigatorMoveRequest`,
   `ElysiumNpcBaseStartTask.cpp`). 5.6 + 4.1 s over 559 cm is the triage's "0.55 m/s". Not
   verified by toggling the flag (no build).
4. **A readout fault, not the game's** [inferred from code]: `animation` / `next_animation` /
   `axis_fraction` are written only by a resolve; the driver's continuous path
   (`FElysiumAnimationDriver::Tick`, the `!bChanged` branch) refreshes `MoveYaw`, the axis values
   and the ground speed but not those three, so they report the cell of the frame the walk started
   (mid-turn, ≈ −65°). H19 should print `Selection.MoveYaw` and say so.

**For B1** (`Visual/ElysiumNpcBody.cpp`): remove the asymptotic arrival — the crowd follower's
slowdown at goal off (in `ApplyCrowdState`, beside `SetCrowdSeparation`), the stop being the
kernel's (retail: constant deceleration to the last waypoint, item 1); keep the commanded speed the
kernel's `GetIdealSpeed`. No fan change. **For B2** (`ElysiumNpcMotor10.{cpp,inl}`,
`ElysiumNpcBaseFacing.cpp`, `ElysiumNpcBaseMotor.cpp`, `ElysiumNpcThink.cpp`): slot 15 returns the
influence; slot 18 blends the queue with `move.facing` by it; `move.facing` is
`MoveGroundExecute`'s yaw (path direction, else the current yaw); `m_flDesiredMoveYaw =
−AngleDiff(moveYaw, yaw)`.

**Record errors for the integrator** (bug protocol step 1): the four N13 `known_red` texts and H18's
acceptance ("within 10 % of 136.7 cm/s") name the male cell; sentry2's retail speed is 101.28 cm/s —
the acceptance must be the body's own `walk_0`.

**Unrecovered:** the female `walk_0` clip's `cycleSeconds` is 1.06446 (not a multiple of 1/30, unlike
every other cell) — not checked against the MDL (a 32-frame reading would move the speed 0.2 %).
The other three N13 records were not run. `patrol_monk_loop`'s body sex not checked.
