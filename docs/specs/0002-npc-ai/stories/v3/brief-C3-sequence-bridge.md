# Brief C3 — V3c: `LookupSequence` answers from the bridge; the task arms write the NPC word; the beat's tests

Read `README.md` here (§1, §4 V3c, §6, §7 K2, §8 Q6). Runs beside C1 and C2. You never build.

## Files (only these)

- `Source/ElysiumUE/Private/Substrate/ElysiumNpcBaseAnim.cpp`, `ElysiumNpcBaseAnim.inl`
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcAnim.cpp`
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcBaseStartTask.cpp` (the script arms, `1544-1657`)
- `Source/ElysiumUE/Private/Debug/ElysiumCastRun.cpp`
- `Source/ElysiumUE/Private/Tests/ElysiumSequenceTests.cpp`,
  `Source/ElysiumUE/Private/Tests/ElysiumNpcKernelDirectorTests.cpp`,
  `Source/ElysiumUE/Private/Tests/ElysiumNpcKernelScriptTests.cpp`

## The job

1. **`LookupSequenceByName`** (`ElysiumNpcBaseAnim.cpp:38-44`, a seam answering −1;
   `CBaseAnimating::LookupSequence`) answers the sequence-bridge row of a clip the body's model
   authors under that name (`IElysiumEmbodiment::HasNpcClip` / `NpcClipOwner`, then
   `FElysiumNpc::SequenceRowFor`, `ElysiumNpcAnim.cpp:319-338`), −1 when it authors none (retail's
   own "unknown scripted sequence"). It lives on `FElysiumNpcBase` and is `const`; make the bridge
   reachable (through `AsNpc()` as `ResetSequenceInfo` already does, a virtual answered by
   `FElysiumNpc`, or the rows `mutable`) — your choice, stated. `ElysiumNpc.h` is C2's file: a
   declaration you need there is a cross-lane line in your report, exact, for the integrator.
   The row's loop bit is the clip's own `STUDIO_LOOPING` if the bake exposes it; if it does not,
   fall back to today's rule (the pre-idle / post-idle loop, `m_iszPlay` once) and name that at the
   line as K2's residue. Report which.
2. **Audit the other callers this turns on** (README Q6): `ElysiumNpcAnim.cpp:151, 193`,
   `ElysiumNpcBaseAnim.cpp:258, 282, 391`, `ElysiumNpcBaseStartTask.cpp:425`. Each is a retail
   caller that has been getting −1. For each, say what now happens (a scene event's sequence
   committing `m_nSequence` under a choreographed scene's montage clip, a custom-move sequence, an
   arrival sequence). Change none of them; the integrator runs their families.
3. **The task arms write the NPC word**: `TASK_PLAY_SCRIPT 0x62` → `SetScriptState(0)`
   (`0x10286af3`), `TASK_PLAY_SCRIPT_POST_IDLE 99` → `SetScriptState(2)` (`0x10286b13`), and every
   `FElysiumScriptedSequence::ScriptStateOf(*this)` → `GetScriptState()` (`ElysiumNpcBaseStartTask.cpp`
   `1567`, `1652`). The crash guards stay as they are.
4. **Debug**: `ElysiumCastRun.cpp:672` asks the owner enum whether a sequence holds the NPC; answer it
   from `m_hCine` resolving to a director (`ResolveCine()`).
5. **Tests** (README §6): delete `Elysium.Arm.ScriptedSequenceBodyClaim`
   (`ElysiumSequenceTests.cpp:1136`). Audit `Substrate.ScriptedSequence`, `ScriptedSequenceLocomotion`,
   `PlayerControllerSequenceLocomotion`, `ScriptedSequenceFlags`, `ScriptedSequenceSelfChain`,
   `MontageSlotRun` and every `NpcKernelDirector.*` / `NpcKernelScript19.*` test: a test that drives
   the beat (`TickBeat`, `EBeatPhase`, `BeatClipEndsAt`, `BeginScriptMove`, `ClaimScriptBody`,
   `NpcScriptState`) is deleted, or — where it also pins a retail cine body (`PossessEntity`'s queue,
   `FindEntity`'s radius, `Finish`'s chain) — keeps those assertions with the NPC word
   (`GetScriptState()`) and loses the beat's. No test is rewritten to drive the NPC's program by hand:
   `script_walk_to_mark` is that test. Add, only if `NpcKernelTranslate` does not already pin it, one
   arm test: `SelectSchedule 0x1028a380` case 4 → `0x2e` → `TranslateSchedule 0x102cc080` by
   `m_fMoveTo` → `0xf2/0xf4/0xf6/0xf8/0xf9`.

## Not yours

The cine (C1); `ElysiumNpc.*`, `ElysiumNpcBase.*`, `ElysiumScriptedCharacter.*`, `ElysiumEntity.h`
(C2). `ElysiumNpcBaseRunTask.cpp` needs no change (its arms read the cine and `m_bSequenceFinished`);
if you find otherwise, report the line.

## Rules

README § "Rules for every agent of V3". The query budget: 10 s warns, 60 s stops; never read a file
over ~200 KB whole. Text through Grep / Read / Glob. No sleep, no polling loop. Do not build, do not
commit.

## Report (≤300 words)

The `LookupSequence` design and its loop-bit answer; the audit of item 2, one line per caller; the
tests deleted, trimmed and added (names and counts).
