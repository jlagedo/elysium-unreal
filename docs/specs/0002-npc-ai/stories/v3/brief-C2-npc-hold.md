# Brief C2 — V3c: the NPC runs its own scripted program; the `Sequence` claim and the scripted move deleted

Read `README.md` here (§1, §2 M5, M9, M11, §4 V3c). Runs beside C1 and C3. You never build.

## Files (only these)

- `Source/ElysiumUE/Private/Substrate/ElysiumNpc.cpp`, `ElysiumNpc.h`
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcBase.h`, `ElysiumNpcBase.cpp`
- `Source/ElysiumUE/Private/Substrate/ElysiumScriptedCharacter.h`, `ElysiumScriptedCharacter.cpp`
- `Source/ElysiumUE/Public/ElysiumEntity.h`
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcThinkCadence.cpp`
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcBaseMaintain.cpp`

## The job

1. **The routing's scene arms go** (findings B row 25): `TickScriptWatchdog` (`ElysiumNpc.cpp:848-883`)
   and `ThinkScriptOwned` (`939-959`). `RouteScheduleMaintenance` keeps only `ThinkInDialog` (V3d's)
   then `ThinkStanceOrIdle`. Retail: `PossessEntity`'s ideal state 4 makes `MaintainSchedule
   0x102817c0` change state and reselect (`ElysiumSchedule.cpp:368-376`); case 4 → `0x2e` → `0xf2…`
   (`ElysiumNpcBaseSelect.cpp:260-270`, `ElysiumNpcBaseTranslate.cpp:18-64`). The watchdog's "Cine
   died!" and "Script failed" are already retail's exits in the task arms and the select.
2. **The `Sequence` claim goes** (findings B rows 6–7): `AcquireSequenceBody`, `ReleaseSequenceBody`,
   `ClaimScriptMove`, `ReleaseScriptMove`, `ClaimScriptBody`, `ReleaseScriptBody`
   (`ElysiumNpc.cpp:544-644`), `bScriptBodyRequested`, `bScriptBodyHeld`, `SequenceOwner`
   (`ElysiumNpc.h:364-378, 1164, 1169-1170`), and their calls elsewhere in your files: `2268-2292`
   (only the `ReleaseScriptBody` / `EndScriptMove` lines inside the dialogue session; the session is
   V3d's), `2524`, `2533`, `2540-2541`, `2707`, `2753-2755`. The pre-claim `FinishAmbientUse` calls
   at `573` / `606` go with their functions.
3. **The scripted move goes** (M11): in `Public/ElysiumEntity.h` the `BeginScriptMove`,
   `AdvanceScriptMove`, `EndScriptMove`, `ClaimScriptBody`, `ReleaseScriptBody` virtuals
   (`472-499`) and `EElysiumScriptGait` / `EElysiumScriptMove` (`44-60`) if nothing else names them
   (C1 deletes the cine's use); in `ElysiumScriptedCharacter.{h,cpp}` the move (`h:17-22, 31, 80-82`;
   `cpp:33-345` — keep the motor's construction and anything the NPC motor uses). Retail's travel is
   tasks 8/9/10's navigator goal (`ElysiumNpcBaseStartTask.cpp:365-430`), already ported.
4. **`m_scriptState`'s readers** read the NPC word: `IsScriptDriven` (`ElysiumNpcBase.h:462-465`) —
   `ShouldThinkFrequently 0x102c2430` reads `m_scriptState ∈ {4,5,6}` (`ElysiumNpcThinkCadence.cpp:86`);
   `TaskMovementComplete`'s test at `0x10273f01..0x10273f14` (`ElysiumNpcBaseMaintain.cpp:34`) — read
   the listing (`vtmb_asm 0x10273ec0`) and test the word retail tests there; if the two readers test
   different things, split the helper into two named after their addresses.
5. Rewrite the STORY8-TWIN comment in `RouteScheduleMaintenance` (one survivor left, the dialogue)
   and the `ElysiumNpcBaseMaintain.cpp:52-57` comment.

## Not yours

The cine (C1); `ElysiumNpcBaseAnim.*`, `ElysiumNpcAnim.cpp`, the task arms, every test, the debug
reader `ElysiumCastRun.cpp` (C3). The dialogue session and the `ScriptedSchedule` claim beyond the
lines named (V3d). `ElysiumChoreoScene.cpp` still stamps `ScriptOwner` on its cast (0010's): leave it.

## Rules

README § "Rules for every agent of V3". The query budget: 10 s warns, 60 s stops; never read a file
over ~200 KB whole (read `ElysiumNpc.cpp` by ranges). Text through Grep / Read / Glob. No sleep, no
polling loop. Do not build, do not commit.

## Report (≤300 words)

Per change, the retail address; what `0x10273f01` tests; every symbol you deleted that a file outside
your list still names (exact file:line, for the integrator).
