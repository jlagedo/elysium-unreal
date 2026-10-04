# Brief A2 — V4a: the clock's speed words and the bridge row's baked data (coder; no build)

Read `README.md` here (§1 "The clock"; §2 M4, M8; §4 V4a), `packets.md` § R1 item 2 and § R2 items
1 and 8, `docs/vtmb/animation_and_movers.md` :1656-1760. After the seam commit. Re-locate by Grep.

## Files (only these)

- `Source/ElysiumUE/Private/Substrate/ElysiumNpcBaseAnim.{cpp,inl}` (`ResetSequenceInfo`,
  `StudioFrameAdvance`; the loop-bit guess ~:65-75)
- `Source/ElysiumUE/Private/Substrate/ElysiumAnimatingSlots.cpp` (slot 242 `GetIdealYawSpeed`
  ~:185-190 and slot 248 `GetIdealSpeed` only)
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcAnim.cpp` (the row accessors' bodies; the row's
  data — **not** `SequenceForActivity`'s pick, which is V4c's)
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcPositions2.cpp`
- `Source/ElysiumUE/Private/Tests/ElysiumNpcKernelAnimTests.cpp`,
  `Source/ElysiumUE/Private/Tests/ElysiumNpcKernelMotorTests.cpp` (the `:~246` stub assertion)

## The job

1. **The row's baked data.** Each sequence row the bridge builds carries, from the clip it resolves
   (`UElysiumClipData`, the body's `FElysiumNpcClip` / `FElysiumBodySequence`): the event timeline
   (`Events`), `STUDIO_LOOPING` (`Flags & 1`) replacing today's loop guess, `SequenceDuration`
   (`cycleSeconds`), the turn yaw (`YawDegrees` of the movement, per R1 item 2 — if R1 says another
   field, that one), and the ground speed: the scalar `GroundSpeedCmPerSecond` for a plain clip, the
   fan's pose-weighted speed for a gridded one (`ElysiumBlendGrids`, the same table the visual reads),
   evaluated at the kernel's pose parameters (`move_yaw` from the kernel's own record,
   `ElysiumCombatCharacterSlotBodies.cpp:~134-146`). Fill the seam's accessors. Units: the kernel
   speaks Source units where its neighbours do — state the conversion at the line.
2. **The words.** `StudioFrameAdvance 0x1008f120`: after the cycle update, every advance writes
   `YawSpeed = SequenceTurnYaw / duration` (0 for a zero duration, `0x10091310`) and `GroundSpeed =
   SequenceGroundSpeedAt(m_nSequence, pose)` (`0x1008f2e5`, `0x1008f306`). `ResetSequenceInfo
   0x10090950`: the same two writes, playback rate 1.0 as today, and `LastEventCheck = 0`. Leave
   the finish / past-half writes `StudioFrameAdvance` already makes; R2 item 1 says how they meet the
   dispatcher's (A1) — if R2 shows one of them must not stand, say so in your report, do not guess.
   If R2 item 8 shows `AutoMovement`'s second advance is not retail, report it with the line (it is
   `ElysiumNpcBaseMotor.cpp`, not your file).
3. **The reads.** Slot 242 `GetIdealYawSpeed 0x100916a0` returns `YawSpeed`; slot 248 `GetIdealSpeed
   0x10091740` returns `GroundSpeed`, no playback term. `GroundSpeedCm()` keeps reading the word.
4. **Tests**: `Elysium.Arm.NpcKernelAnim.SpeedWords` (README §6) over a fixture row with a known
   turn yaw, duration and a two-cell fan; delete the stub assertion at
   `ElysiumNpcKernelMotorTests.cpp:~246` and replace it with the turning arm over a real `YawSpeed`
   (`0x10297ce0`: `|GetIdealYawSpeed| × cvar`, floor 1.0).

## Not yours

The dispatcher (A1), the weighted pick (V4c), the body's commanded speed (V4b: today's body keeps
reading its own fan until B1), the cone (A3).

## Rules

README § "Rules for every agent of V4": no build, no editor, no suite; only your files; cross-lane
lines in the report. The query budget (10 s warns, 60 s stops). Text through Grep / Read / Glob. Do
not commit. Report ≤300 words: what you ported, units and conversions, tests, cross-lane lines.
