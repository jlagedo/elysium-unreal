# Brief D3 — V3d: the owner vocabulary deleted; the debug views; the arbiter's tests

Rewritten 2026-10-04 from the tree with V3c integrated (V3c's commit lands before you; line numbers
are that tree's — **re-locate each by Grep on the symbol before editing**). Read `README.md` here (the
"Corrections" block, §2 M15, §6, §8 Q5/Q8) and `brief-D2-npc-arbiter.md` § "Shared names" and § "What
is left of the arbiter". Runs beside D1 and D2. You never build.

## Files (only these)

- `Source/ElysiumUE/Private/Substrate/ElysiumNpcMind.h`, `ElysiumNpcMind.cpp`
- `Source/ElysiumUE/Public/ElysiumNpcMindTypes.h`
- `Source/ElysiumUE/Private/Debug/ElysiumCogWindow_Npc.cpp`, `ElysiumNpcDebugData.h`,
  `ElysiumNpcDebugData.cpp`
- `Source/ElysiumUE/Private/Debug/ElysiumEntityDebugSubsystem.cpp` (`:219` only)
- `Source/ElysiumUE/Private/Debug/ElysiumNpcGameplayDebugger.cpp` (`109-110` only)
- `Source/ElysiumUE/Private/Visual/ElysiumNpcBody.cpp` (`552-553` only)
- `Source/ElysiumUE/Private/Tests/ElysiumNpcMindTests.cpp`,
  `Source/ElysiumUE/Private/Tests/ElysiumAiScriptedScheduleTests.cpp`

D1 owns the dialogue bodies and their tests; D2 owns `ElysiumNpc.{h,cpp}` and every other caller of
the API you delete. `Debug/ElysiumCastRun.cpp` no longer reads the owner (V3c): not yours, not touched.

## The job

1. **The owner vocabulary goes** (M15; findings B rows 1–5, 14).
   - `Public/ElysiumNpcMindTypes.h`: `EElysiumBodyOwner` and `FElysiumBodyOwnerToken` (`18-40`),
     `LexToString(EElysiumBodyOwner)` (`56-69`). `EElysiumNpcState` and its `LexToString` (`5-16`,
     `42-54`) stay — `ElysiumEntity.h:7`, `ElysiumAnimationIntent.h:10`, `ElysiumScriptedScheduleOrder.h:6`,
     `ElysiumNpcConditions.h:5` and eleven more files include the header for them.
   - `ElysiumNpcMind.h`: `RequestState` (`16`; its one caller is D2's), `Acquire` / `CanAcquire` /
     `Release` / `ForgetSuspended` (`18-24`), `Restore`'s owner argument (`27-28`), `Owner()` /
     `SuspendedOwner()` / `Generation()` / `CurrentToken()` (`34-41`), `IsResumableOwner` (`60`),
     `IsAcquisitionAllowed` (`100`), `RefreshStateFromOwner` (`107`), `CurrentOwner` / `ParkedOwner` /
     `OwnerGeneration` (`112-114`). `ElysiumNpcMind.cpp`: `IsResumableOwner` (`48-52`), `RequestState`
     (`151-165`), `IsAcquisitionAllowed` (`167-211`), `RefreshStateFromOwner` (`213-239` — the
     `m_NPCState` write retail never makes: a dialogue writes no state, R1; a cine writes its state
     through `PossessEntity 0x101a7880` / `CineCleanup 0x1027d170`), `Acquire` (`241-271`), `Release`
     (`273-294`), `ForgetSuspended` (`296-304`).
   - `Invalidate` (`306-321`) keeps its signature and its state write (dead or idle) and record, no
     owner. `Restore` (`323-338`) becomes `Restore(EElysiumNpcState SavedState)`: admission, the state,
     the record. Its `Scripted → Idle` clamp (`329-330`) stays, named: it pairs with K1 and the
     restart, both V6's.
   - **What stays** (do not touch their behaviour): admission (`ArmAdmission`, `Admit` — README Q5,
     V6's), `IsSupportedState`, the state words and their retail writes (`WriteNpcStateRetail`,
     `WriteIdealStateRetail`, `RequestDesiredState`, the raw overlays), the trace (`Record`,
     `RecordExternal`, `LastTransition`, `Trace`), `bForceStateChange`, `LastStateChangeTime`.
   - Rewrite the class comment (`ElysiumNpcMind.h:6-8`): the mind holds `m_NPCState` /
     `m_IdealNPCState` and admission; there is no body owner. Fix the comments that cite
     `RequestState` (`49-57`, `62-68`).
2. **Debug views** — where a view showed "who holds the body", show retail's words: `m_hCine`
   (`ScriptOwner`, resolved), `m_scriptState` (`GetScriptState()`), `m_hDialogPartner`
   (`GetDialogPartner()`, resolved), and the running schedule.
   - `ElysiumCogWindow_Npc.cpp`: the tab text (`178`, "the body-owner arbiter"), the roster's
     "Owner" column (`715`, `759`), the mind facts (`821-823`), the head overlay (`1207-1216`).
   - `ElysiumNpcDebugData.h:53-55` (`BodyOwner`, `SuspendedOwner`, `OwnerGeneration`) → the three
     retail words; `ElysiumNpcDebugData.cpp:87-89` and the archive line `246` with them.
   - Their two readers: `ElysiumNpcGameplayDebugger.cpp:109-110`, `Visual/ElysiumNpcBody.cpp:552-553`.
   - `ElysiumEntityDebugSubsystem.cpp:219` reads the NPC's "Body owner" debug row, which D2 deletes:
     read `"Script state"` (and `"Cine"` if the brief line has room) — D2's row names.
3. **Tests** (README §6; re-checked against today's tree — all eight below still exist).
   - `ElysiumNpcMindTests.cpp`: delete `Elysium.Arm.NpcMind.BodyOwner` (`56-125`) and
     `Elysium.Arm.NpcMind.Restore` (`126-`). **Trim** `Elysium.Arm.NpcMind.Admission` (`12-54`) to the
     admission barrier: arm, admit once, idempotent, idle (`21-26`); cut the token / `Acquire` /
     `Owner()` / `RequestState` lines (`18-20`, `22-23`'s claim, `27-52`). It stays while the barrier
     does (Q5).
   - `ElysiumAiScriptedScheduleTests.cpp`: delete `MoveToGoal` (`322`), `AssignEnemy` (`438`),
     `Refusals` (`515`), `NamedSchedule` (`661`), `Precedence` (`735`), `CombatPreemption` (`798`) —
     findings F: port / stub / dupl; their owner assertions are `347, 380, 491, 538, 599, 619, 758`.
     Keep `Elysium.Arm.AiScriptedSchedule.Tables` (`257`, it pins `0x101a98c0`'s authored→native
     tables) and whatever fixture it still uses; drop helpers and includes nothing reads.
     `script_aischedule_walk` replaces the six.
   - No new test of a port mechanism.

## Shared names with D2

`FElysiumNpcMind::Restore(EElysiumNpcState SavedState)` (one argument); `Invalidate(const TCHAR*
Reason, bool bDead)` unchanged; `IsSupportedState` stays (D2's `RestoreMindState` calls it);
`RequestState`, `Owner()`, `SuspendedOwner()`, `Generation()`, `CurrentToken()`, `CanAcquire`,
`Acquire`, `Release`, `ForgetSuspended`, `IsResumableOwner` deleted. D2 maps the executor's typed
state with an existing helper; you add no typed→retail map. D2's `GetDebugState` rows are `"Cine"`,
`"Dialog partner"`, `"Script state"`; "Body owner" is gone.

## Rules

README § "Rules for every agent of V3". Retail first: cite the address where a comment names one. The
query budget: 10 s warns (log to `$ELYSIUM_WORK_ROOT/logs/slow-queries.tsv`), 60 s stops, never
retried as-is or widened; never read a file over ~200 KB whole. Text through Grep / Read / Glob, never
shell `grep`/`cat`/`sed`. Wait on a background command by its completion notification, never a sleep
or a polling loop. Touch only these files; a line another file needs goes in the report, exact. Do not
build, do not run the arena or a suite, do not edit a record, do not commit.

## Report (≤300 words)

What the mind keeps (one line per member), what each view now shows, the tests deleted (names) and
the one trimmed, every remaining reference to a deleted symbol you found outside your files (exact
file:line).
