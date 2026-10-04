# Brief A2 — V4a: the clock's speed words, the bridge row's baked data, sequence 0 (coder; no build)

**Final (amended after V4r, 2026-10-04 — J1, J9).** Read `README.md` here (§1 "The clock" with its
amendment; §2 M4, M8, M16; §4 V4a; § "Shared names"), `packets-R1.md` item 2, `packets-R2.md`
items 1 and 8, the ruling J1 (`stories/v1/triage.md` § "Judge's rulings, V4"; Grep, read only it),
`docs/vtmb/animation_and_movers.md` :1656-1760. After the seam commit. Re-locate by Grep.

## Files (only these)

- `Source/ElysiumUE/Private/Substrate/ElysiumNpcBaseAnim.{cpp,inl}` (`ResetSequenceInfo`,
  `StudioFrameAdvance`; the loop-bit guess ~:65-75)
- `Source/ElysiumUE/Private/Substrate/ElysiumAnimatingSlots.cpp` (slot 242 `GetIdealYawSpeed`
  ~:185-190 and slot 248 `GetIdealSpeed` only)
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcAnim.cpp` (the row accessors' bodies; the row's
  data; row 0 — **not** `SequenceForActivity`'s pick, which is V4c's)
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcPositions2.cpp`
- `Source/ElysiumUE/Public/ElysiumWorldServices.h` (`IElysiumEmbodiment`: the one accessor of
  item 4), `Source/ElysiumUE/Public/ElysiumMapActor.h` and
  `Source/ElysiumUE/Private/Map/ElysiumMapActorEmbodiment.cpp` (its body),
  `Source/ElysiumUE/Private/Tests/ElysiumTestServices.h` (the recording double)
- `Source/ElysiumUE/Private/Visual/ElysiumAnimationResolve.cpp` (the stale comment ~:294 only)
- `Source/ElysiumUE/Private/Tests/ElysiumNpcKernelAnimTests.cpp`,
  `Source/ElysiumUE/Private/Tests/ElysiumNpcKernelMotorTests.cpp` (the `:~246` stub assertion)

## The job

1. **The row's baked data.** Each sequence row the bridge builds carries, from the clip it resolves
   (`UElysiumClipData`, the body's `FElysiumNpcClip` / `FElysiumBodySequence`): the event timeline
   (`Events`), `STUDIO_LOOPING` (`Flags & 1`) replacing today's loop guess, `SequenceDuration`
   (`cycleSeconds`), the turn yaw — **the baked `YawDegrees` of the movement** (R1 item 2:
   `GetSequenceTurnYaw 0x1008f8f0` returns `angles[1]`, the pose-weighted sum over the blend
   corners of the last movement record's `angle`) — and the ground speed: the scalar
   `GroundSpeedCmPerSecond` for a plain clip, the fan's pose-weighted speed for a gridded one
   (`ElysiumBlendGrids`, the same table the visual reads), evaluated at the kernel's pose
   parameters (`move_yaw` from the kernel's own record,
   `ElysiumCombatCharacterSlotBodies.cpp:~134-146`, read only). Fill the seam's accessors. Units:
   the kernel speaks Source units where its neighbours do — state the conversion at the line.
2. **The words.** `StudioFrameAdvance 0x1008f120`: after the cycle update, every advance writes
   `YawSpeed = SequenceTurnYaw / duration` (0 for a zero duration, `0x10091310`) and `GroundSpeed =
   SequenceGroundSpeedAt(m_nSequence, pose)` (`0x1008f2e5`, `0x1008f306`). `ResetSequenceInfo
   0x10090950`: the same two writes, playback rate 1.0 as today, and `LastEventCheck = 0`.
   **The finish and past-half writes `StudioFrameAdvance` already makes stay** (R2 item 1): it only
   SETS the finish flag (cycle left in `[0,1)`), writes past-half from the real cycle, and makes
   its own rising-edge `OnSequenceFinished`; `ResetSequenceInfo` clears. Check the port's body
   against those four statements and report a difference, do not guess. **`AutoMovement`'s
   advance is retail** (R2 item 8: `0x10280a50` calls slot 250 first, unconditionally; a second
   advance in one tick is inert because `dt = 0` fails `dt > *0x1044f020`) — nothing to change;
   if the port's second advance in one tick is not inert, report it with the line. *`0x1044f020` is the
   f64 **0.001** (`packets-S5.md` item 9). Unrecovered: the early-out's return; if your port of
   the guard needs it, the coder reads it first.*
3. **The reads.** Slot 242 `GetIdealYawSpeed 0x100916a0` returns `YawSpeed`; slot 248 `GetIdealSpeed
   0x10091740` returns `GroundSpeed`, no playback term. `GroundSpeedCm()` keeps reading the word.
   **Note (J9):** every shipped movement record's angle is 0.0, so `YawSpeed` is real and still 0
   on shipped data. This lane does **not** fix the 13.4 s `task_face_enemy` (README §2 M4's cause
   is refuted) — write no code that assumes it does.
4. **N19 — a missed lookup plays the model's sequence 0** (J1). Retail: `StartSequence 0x101a82d0`
   on a `LookupSequence` miss writes `m_nSequence := 0` (`0x101a833d`); `ResetSequenceInfo` plays
   the model's own sequence 0 at rate 1.0 (`0x10090a23`) and `StudioFrameAdvance` raises the finish
   at its length. The port's bridge row 0 plays nothing.
   - The embodiment accessor `IElysiumEmbodiment::GetBodyClipByRawIndex(USkeletalMeshComponent*
     Body, int32 RawIndex)` (README § "Shared names"): the body's `FElysiumNpcClip` whose
     `RawIndex` is the argument (`Public/Visual/ElysiumNpcClips.h:~68`, loaded by
     `Private/ElysiumBodyData.cpp:~14`), or none. The interface's default and the recording double
     answer none; the body in `ElysiumMapActorEmbodiment.cpp`.
   - The bridge's row 0 (`SequenceRowFor` / `PlaySequenceClip`) is resolved from
     `GetBodyClipByRawIndex(body, 0)`: the clip's own `STUDIO_LOOPING`, rate 1.0, the trace naming
     `seq 0`. Where a body answers none, row 0 stays today's and logs **once per model**.
   - No pipeline change, no re-bake: none of the 1,430 staged body tables lacks a `rawIndex 0`
     row (`rawIndex = sequenceBase + the descriptor's index in its own .mdl`,
     `importers/body_data.py:83-89`). Not checked by the judge: that every baked `DA_` is current
     with its staged table.
   - Correct the stale comment in `Visual/ElysiumAnimationResolve.cpp` ("the character export
     writes no raw index"). Its twin in `Public/ElysiumAnimationIntent.h` (~:882) is not your
     file: report the line.
5. **Tests**: `Elysium.Arm.NpcKernelAnim.SpeedWords` (README §6) over a fixture row with a known
   turn yaw, duration and a two-cell fan; `Elysium.Arm.NpcKernelAnim.SequenceZero` (`0x101a833d`,
   `0x10090a23`: a body whose double answers a row-0 clip plays it at rate 1.0 with the clip's
   loop bit and finishes at its length; a body answering none keeps today's row 0). Delete the
   stub assertion at `ElysiumNpcKernelMotorTests.cpp:~246` ("`GetIdealYawSpeed()` answers 0, so it
   lands on 1.0") and replace it with the turning arm over a real `YawSpeed` (`0x10297ce0`:
   `|GetIdealYawSpeed| × cvar`, floor 1.0, **under `m_afMemory & 0x2000` only**).

## Not yours

The dispatcher (A1), the player and camera (A4), the weighted pick (V4c), the body's commanded
speed (V4b: today's body keeps reading its own fan until B1), the cone (A3). `script_walk_to_mark`
is re-measured by the A integrator.

## Rules

README § "Rules for every agent of V4": no build, no editor, no suite; only your files; cross-lane
lines in the report. The query budget (10 s warns, 60 s stops; never a file over ~200 KB whole).
Text through Grep / Read / Glob. Do not commit. Report ≤300 words: what you ported, units and
conversions, tests, cross-lane lines.
