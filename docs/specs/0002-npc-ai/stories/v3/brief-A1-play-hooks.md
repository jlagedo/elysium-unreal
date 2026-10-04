# Brief A1 — V3a: the body plays the kernel's sequence; the program claim leaves `ElysiumNpc.cpp`

Read `README.md` here (§1, §2 M1–M4, M17, §4 V3a). Runs after the seam commit and its review. You
never build.

## Files (only these)

- `Source/ElysiumUE/Private/Substrate/ElysiumNpcAnim.cpp`
- `Source/ElysiumUE/Private/Substrate/ElysiumNpc.cpp`
- `Source/ElysiumUE/Private/Substrate/ElysiumNpc.h`

## The job

1. **`PlaySequenceClip` plays on every body** (M1, `ElysiumNpcAnim.cpp:415-436`). Retail:
   `ResetSequenceInfo 0x10090950` plays `m_nSequence` on every body; `StudioFrameAdvance 0x1008f120`
   raises `m_bSequenceFinished`. Delete the owner read and its branch: the row's clip is played
   whenever the row exists. Keep the "length known from an earlier play" arm only for the case the
   clip player refuses (the body authors no such clip), and keep the trace's `rate=` honest (1 when
   played). Rewrite the comment block: it is no longer a "named modernization"; the sequence bridge
   (a named modernization, `ElysiumNpcAnim.cpp:313-317`) is the only thing standing between the row
   and the clip.
2. **The stance transition's owner term** (M2, `ElysiumNpc.cpp:1330-1332`). Retail `SetDisposition
   0x102c0f70` is gated on `m_bDisableAI +0x6080` only *(draft)*. Delete the `Mind.Owner()` term.
   Leave the `IsFeedBusy()` term and the by-name transition clip as they are, with a one-line
   comment naming them V4's (the rest of `0x102c0f70`'s body: `+0xff0 = 0xf1`, `+0x5ccc`,
   `ResetSequenceInfo`, `GetTransitionAnim 0x100ed150`).
3. **The `Schedule` claim leaves** (M3/M4). Delete `AcquireScheduleBody`, `ReleaseScheduleBody` and
   the `ScheduleOwner` token (`ElysiumNpc.cpp:1500-1508`, `ElysiumNpc.h:490-492, 595-600, 1162`) and
   every call in your files: `556`, `1068`, `1082`, `1603`, `1745`, `2298`, `2527`, `2744`. Retail:
   the running program (`m_pSchedule +0x5c38`) owns the navigator; a schedule change clears the goal
   in `OnScheduleChange 0x102a0940` at `0x102a0992` under `!PRESERVE_PATH` (already ported,
   `ElysiumNpcMaintain.cpp:151-163`). In `ReleaseProgramBody` (`:1474-1498`, still used by the
   `ScriptedSchedule` claim until V3d) delete the `Motor->Stop` / `ClearMoveIgnores` block and the
   `bMoveIssued` / `bWalkingAnimation` writes: a second navigator stop at the claim edge is port-only
   (findings B row 11). Fix every comment in these files that describes the `Schedule` owner.
4. **`bWalkingAnimation`** is written and never read (findings B row 29): delete every write in
   your files (`579`, `586`, `1494`, `2044`, `2115`, `2165`, `2709`); at `2165` keep the
   `StartWalkingAnimation()` call itself (its clip is the executor's pose until V3b deletes it).
   A2 deletes the member.
5. **`bMoveIssued`'s reader at `ElysiumNpc.cpp:1895`** (`GazeNavigationGoal`): if it stands for
   "the navigator has a goal" use `NavIsGoalActive()` (`IsGoalActive 0x102ee6a0`,
   `ElysiumNpcBaseMotor.cpp:101`); if it is motor bookkeeping, leave it and say why. Do not rename
   the member (README Q10).

## Not yours

The `Sequence`, `Ambient`, `Dialogue`, `ScriptedSchedule` claims and everything else in
`RouteScheduleMaintenance` stay (V3b–V3d). `ElysiumNpcBaseStartTask.cpp` / `ElysiumNpcScript.cpp`'s
claims are A2's. No test file. `UpdateIdealState`'s body (`:1020-1069`) loses only its claim line
(README Q8).

## Rules

README § "Rules for every agent of V3". The query budget: 10 s warns, 60 s stops; never read a file
over ~200 KB whole (`ElysiumNpc.cpp` is ~130 KB: read it by ranges). Text through Grep / Read / Glob.
No sleep, no polling loop. Do not build, do not commit.

## Report (≤300 words)

Per file: what changed, with the retail address at each change. Every line another lane's file
needs (A2 deletes the declarations you stop using? say which). Anything that would make `cover` or
the patrols still play at rate 0 that you saw and did not touch.
