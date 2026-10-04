# Brief D2 — V3d: `NPCThink` runs retail's phases and nothing else; the order, the save, the dialogue claim

Read `README.md` here (§1, §2 M5, M12, M14, M16, §4 V3d, §8 Q5) and `docs/vtmb/npc-ai/authored-control.md`
§ `aiscripted_schedule` (lines ~251-320). Runs beside D1 and D3. You never build.

## Files (only these)

- `Source/ElysiumUE/Private/Substrate/ElysiumNpc.cpp`, `ElysiumNpc.h`
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcMaintain.cpp` (`128-141`)
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcBaseMaintain.cpp` (`50-60`)
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcAnim.cpp` (`HasLiveDialogPartner`, `81-88`)
- `Source/ElysiumUE/Public/ElysiumEntity.h` (the dialogue-body virtuals, `509-516`)
- `Source/ElysiumUE/Public/ElysiumSaveTypes.h` (one schema step)

## The job

1. **The routing goes** (M5): `ThinkInDialog` (`ElysiumNpc.cpp:917-937`) and `RouteScheduleMaintenance`;
   `FElysiumNpcBase::MaintainSchedule` (`ElysiumNpcBaseMaintain.cpp:50-60`) calls
   `MaintainScheduleRetail` for every body (`0x102817c0` as `RunAI 0x1026f302` reaches it).
   `ThinkStanceOrIdle` (`1071-1100`) reduces to the interpreter call; if nothing else calls it, fold it
   away. The STORY8-TWIN markers (`ElysiumNpc.cpp:893-904`, `ElysiumNpc.h:386`) go.
2. **The dialogue claim goes** (M12): `BeginDialogueBodySession`, `EndDialogueBodySession`,
   `PrepareBodyForDialogue`, `GetDialogueBodyOwner`, `DialogueBodyOwner` (`ElysiumNpc.cpp:2242-2339`;
   `ElysiumNpc.h:627-641, 1165`) and the base virtuals (`Public/ElysiumEntity.h:509-516`); their calls
   in your files (`2513-2523` in `ReleaseAllBodyOwnership`). The `ScriptOwner` cancel that
   `BeginDialogueBodySession` did (`2268-2292`) is retail's only where a retail body does it: a
   dialogue start does not cancel a cine unless R1/R2 show it; `ForceScheduleChange 0x102ae490`
   already cancels an interruptable cine on the forced install (`ElysiumNpcMaintain.cpp:96-112`).
3. **`HasLiveDialogPartner`** (`ElysiumNpcAnim.cpp:81-88`) resolves `GetDialogPartner()` to a live
   entity (`+0xfe8`), not the session bit. Its readers (RunAI's gather skip `0x1026f1f0`,
   `ElysiumNpcBaseRunAi.cpp:25`, the scene queue) then see retail's word.
4. **The pushed order** (M14; findings B row 12): `BeginScriptedSchedule` (`1522-1650`) is the
   executor `0x101a98c0`: goal resolve first (none → the log, return before the force state), then
   `SetState(native)` by the authored→native table (0/1→1/2→3/3→2) — not `Mind.RequestState`; then the
   mode's call. Delete the `ScriptedSchedule` claim (`1510-1518`, `1607-1614`), the `ClearSchedule` and
   `FinishAmbientUse` before the push (`1596-1603`; retail does not clear, `schedule-kernel.md:663`),
   `EndScriptedSchedule` (`1652-1670`) and its callers, the program-end coupling in `ThinkStanceOrIdle`
   (`1088-1095`), the `UpdateIdealState` gate (`1057-1060`), and `OnScheduleChange`'s order pre-step
   (`ElysiumNpcMaintain.cpp:128-136`). `ScriptedScheduleOrder` keeps only what a retail word needs
   (`m_eForcedState +0x65cc` is `0x102ae840`'s, not this executor's — leave that body alone). The
   deferred replay before admission (`811-823`, `1533-1546`) stays as it is, without a claim (Q5).
5. **The mind's owner, as this file sees it** (M15/M16): `AcquireProgramBody`, `ReleaseProgramBody`,
   the remaining tokens (`ElysiumNpc.h:1115-1117, 1161-1165`); `ReleaseAllBodyOwnership`
   (`2511-2542`) keeps only what retail's death / dormancy path does (rename it if its name lies);
   `SaveBlockReason` (`2921-2931`) deleted (the base virtual stays for other entities);
   `SerializeMindBlock` (`2846-2862`) saves the state only; `RestoreMindState` (`2727-2756`) restores
   the state only; `RestoredMindOwner` (`ElysiumNpc.h:1107`) goes; one save-schema step. The
   `GetDebugState` owner fields (`2982`) go. `UpdateIdealState` (`1020-1069`) stays for its tests
   (README Q8) minus the lines this list names.

## Shared names with D3

`FElysiumNpcMind::Restore(EElysiumNpcState SavedState)` (one argument); `Invalidate(const TCHAR*
Reason, bool bDead)` unchanged in signature, no owner inside; `RequestState` deleted (its one caller is
yours, item 4); `Owner()`, `Generation()`, `CurrentToken()`, `CanAcquire`, `Acquire`, `Release`
deleted.

## Not yours

The dialogue bodies (D1); the mind, the enum, the debug views and the arbiter's tests (D3).

## Rules

README § "Rules for every agent of V3". The query budget: 10 s warns, 60 s stops; never read a file
over ~200 KB whole (read `ElysiumNpc.cpp` by ranges). Text through Grep / Read / Glob. No sleep, no
polling loop. Do not build, do not commit.

## Report (≤300 words)

Per change, the retail address; the executor's order as it now reads; what `ReleaseAllBodyOwnership`
became; every symbol another lane must change for the build (exact file:line).
