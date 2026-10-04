# Brief D3 — V3d: the owner vocabulary deleted; the debug views; the arbiter's tests

Read `README.md` here (§2 M15, §6, the "Shared names with D3" of `brief-D2-npc-arbiter.md`). Runs
beside D1 and D2. You never build.

## Files (only these)

- `Source/ElysiumUE/Private/Substrate/ElysiumNpcMind.h`, `ElysiumNpcMind.cpp`
- `Source/ElysiumUE/Public/ElysiumNpcMindTypes.h`
- `Source/ElysiumUE/Private/Debug/ElysiumCogWindow_Npc.cpp`, `ElysiumNpcDebugData.h`,
  `ElysiumNpcDebugData.cpp`, `ElysiumEntityDebugSubsystem.cpp`
- `Source/ElysiumUE/Private/Tests/ElysiumNpcMindTests.cpp`,
  `Source/ElysiumUE/Private/Tests/ElysiumAiScriptedScheduleTests.cpp`

## The job

1. **The owner vocabulary goes** (findings B rows 1–5, 14): `EElysiumBodyOwner`,
   `FElysiumBodyOwnerToken`, `LexToString(EElysiumBodyOwner)` (`ElysiumNpcMindTypes.h:18-40, 56-69`);
   in the mind: `Acquire`, `CanAcquire`, `Release`, `ForgetSuspended`, `IsAcquisitionAllowed`,
   `RefreshStateFromOwner` (the `m_NPCState` write retail never makes: a dialogue writes no state, a
   cine writes its state through `PossessEntity` / `CineCleanup` / `FixScriptNPCSchedule`), `Owner()`,
   `SuspendedOwner()`, `Generation()`, `CurrentToken()`, `IsResumableOwner`, `CurrentOwner`,
   `ParkedOwner`, `OwnerGeneration`, `RequestState` (its one caller is D2's) — `ElysiumNpcMind.cpp:48-52,
   151-338`, `ElysiumNpcMind.h:18-41, 60, 100-114`. `Restore` takes the state only; `Invalidate` keeps
   its signature and its dead-state write, no owner. What stays: admission (`ArmAdmission`, `Admit`,
   README Q5), the state words and their retail writes (`WriteNpcStateRetail` …), the trace,
   `bForceStateChange`. Rewrite the class comment: the mind holds `m_NPCState` / `m_IdealNPCState`
   and admission; there is no body owner.
2. **Debug views**: the owner lines in `ElysiumCogWindow_Npc.cpp` (`722`, `803`, `1201-1213`) and
   `ElysiumNpcDebugData.{h,cpp}` (`83`, and its `SuspendedOwner` / owner fields), and
   `ElysiumEntityDebugSubsystem.cpp:202` if it prints the owner. Where a view showed "who holds the
   body", show the retail words instead: `m_hCine` (resolved), `m_scriptState`, `m_hDialogPartner`,
   the running schedule.
3. **Tests** (README §6): delete `Elysium.Arm.NpcMind.BodyOwner` and `.Restore`
   (`ElysiumNpcMindTests.cpp:57, 127`); keep `.Admission` with its owner assertions cut. In
   `ElysiumAiScriptedScheduleTests.cpp` delete `MoveToGoal`, `AssignEnemy`, `Refusals`, `NamedSchedule`,
   `Precedence`, `CombatPreemption` (findings F: port / stub / dupl); keep `Tables` (it pins the
   authored→native tables of `0x101a98c0`). `script_aischedule_walk` replaces them.

## Not yours

`ElysiumNpc.*` and every caller of the deleted API outside your files (D2, D1); the arena records.

## Rules

README § "Rules for every agent of V3". The query budget: 10 s warns, 60 s stops; never read a file
over ~200 KB whole. Text through Grep / Read / Glob. No sleep, no polling loop. Do not build, do not
commit.

## Report (≤300 words)

What the mind keeps (one line per member), what the views now show, the tests deleted (names) and
kept, every remaining reference to a deleted symbol you found outside your files (exact file:line).
