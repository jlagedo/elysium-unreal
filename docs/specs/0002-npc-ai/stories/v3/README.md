# V3 — the arbiter retired; scenes, dialogue and places as retail runs them: the design

Planner's design for `spec.md` § Step 2, V3 (size L). It is cut into a reading story and four
sub-stories (V3r, V3a–V3d), each testable at its close. Briefs are the `brief-*.md` files here.
Written 2026-10-04 from the tree at `257a892f`. Source paths are relative to
`Source/ElysiumUE/Private/Substrate/` unless they say otherwise.

| story | briefs |
|---|---|
| V3r | `brief-R-reader.md` (writes `packets.md`) |
| V3a | `brief-A0-seam.md`, `brief-A0-review.md`, `brief-A1-play-hooks.md`, `brief-A2-program-claims.md`, `brief-A-integrator.md` |
| V3b | `brief-B1-places-executor.md`, `brief-B2-maintain-return.md`, `brief-B-integrator.md` |
| V3c | `brief-C1-cine.md`, `brief-C2-npc-hold.md`, `brief-C3-sequence-bridge.md`, `brief-C-integrator.md` |
| V3d | `brief-D1-dialogue.md` (final only after `packets.md`), `brief-D2-npc-arbiter.md`, `brief-D3-mind-tests.md`, `brief-D-integrator.md` |

Line numbers are today's; each wave's coders re-locate them by Grep after the previous wave moved
them (a wave's deletions shift `ElysiumNpc.cpp` by hundreds of lines).

**Provenance marks.** *(read)* means the planner read the doc section or the port body.
*(draft)* means the address is from `consolidation/draft-2026-09-30.md` § C1 or
`findings-B-port-only.md` and was not re-verified. *(record)* means it comes from an arena record's
`about` text and was not re-verified.

## Corrections after V3r and the seam (2026-10-04) — these override §1 and §7 below

From `packets.md` (read from the listing) and the seam commit `f767f9f8`:

- **The dialogue inputs and `+use` do not force their schedule.** All four install through
  `0x102ae750(id, 0)` → `ForceScheduleChange` (cancels a live cine, clears `PRESERVE_PATH`, runs
  `OnScheduleChange`, so a visited place is released by that path). `StartPlayerDialog 0x1029ef80`
  and `…Unforced 0x1029f120` install `0x6d`; `…Remote 0x1029f060` installs `0x6e`. Unforced sets
  `+0x6495 = 0`. The `+0x5bac` store is a float (task `0xd9`'s walk-or-run threshold); Remote never
  writes it.
- **`PlayerUse 0x10167850` does not call `ClearSchedule`.** It checks `CanTalk` (false returns with
  no ordinary use), resets the think timers, installs `0x6a`, then calls slot 414.
- **The NPC's `+0xfe8` is written only by `StartTalking 0x102c0270`** (inside `CDialog::Acquire`),
  which also fires `m_OnDialogBegin +0x5f44`; only `0x102c0360` clears it. `FUN_10178280` writes
  the player's word only. (The seam's comment at `ElysiumPlayer.h:994` lists the old writers: D1
  corrects it.)
- **`OnDialogEnd` fires exactly once**, from `CDialog::Release`: `0x102c0360` fires only for a
  live partner and `0x102c1400` reaches it only once the partner is gone. Q7 is settled.
- **`0x102c1400`** is walked in `packets.md`: its usual answer is `m_Activity +0xfec`; slot 611 is
  asked only when `m_bSequenceFinished` is set, and it then commits that sequence itself
  (`0x10260a50`, `m_flCycle = 0`) and answers `0xf1`, or `1` when the lookup fails.
- **V3d**: the port's place-release lines ahead of the dialogue claims are replaced by the
  `ForceScheduleChange` → `OnScheduleChange` path, not dropped. `FElysiumNpcPayphone` has its own
  `DialogPartner` member (`ElysiumNpcPayphone.h:57`) hiding the base word: V3d deletes it.
- **Open before V3d**: `dialog_use_hold` is untriaged (after `+use` Hunter1 shows nothing and no
  conversation opens: the use focus refuses it, or the press never reaches `PlayerUse`); bug
  protocol step 1 first. `brief-D1-dialogue.md` is re-written from `packets.md` before V3d starts.
- The owner's rulings: K1 accepted, K2 accepted (the `m_nSequence` path), Q5 → V6, Q8 → V9.
- **After V3a (2026-10-04, the integrator's report in the V3a commit message)**: `cover` is green
  through the shot. The three patrols
  are not red 1 (review doubt 1 settled): the walk now plays at `rate=1` with every leg time
  unchanged; the slow body is N13 (`move_yaw`-weighted ground speed / facing), placed in V4. §4's
  "records turned green" for V3a is corrected to `cover` alone.
- **After V3b (2026-10-04, `report-B.md`)**: the executor is gone and every `use_interesting` NPC
  runs `0xff` → `0x100` → `0x103`. §4's V3b list is corrected: green are `input_useinteresting`,
  `map_hub_idle` and the two roll calls; `places_thug_pt1` and the sneak-past's first half wait on
  N15 (`ClaimAmbientSpot`'s `AcceptedClasses` / type terms, which `0x102dad60` lacks and no retail
  code reads), `places_pedestrian_visit` on H16 (the arena's anchors are typed `Stand`, removed at
  spawn), `hub_crosswalk_wait` on Q-V3b1 — one V3b follow-up wave, proposed to the owner. On removal
  a place's `OnInterestingPlaceLeft` no longer fires (`0x1028d707 PUSH 0`: retail's).

## 1. What retail does: the contract

**Who decides what the body plays.** No arbiter exists. `ResetSequenceInfo 0x10090950` plays
whatever `m_nSequence` holds, on every body *(read: `ElysiumNpcBaseAnim.cpp:53-108` follows it)*.
`StudioFrameAdvance` (slot 250, `0x1008f120`) advances `m_flCycle` and raises `m_bSequenceFinished`
*(read: ported at `ElysiumNpcBaseAnim.cpp:154-206`)*. Whoever writes `m_nSequence` decides the pose:
the running program's tasks through `SetActivity` and the activity ladder, or a cine's
`StartSequence` (slot 584, `0x101a82d0`), which does `LookupSequence` → `m_nSequence`,
`m_flCycle = 0` and `ResetSequenceInfo` *(read: `ElysiumScriptedSequence.cpp:495-518` cites it)*.
The disposition change `SetDisposition 0x102c0f70` is gated only on `m_bDisableAI +0x6080` and
writes `m_IdealActivity = 0xf1`, `m_nIdealSequence`, then `ResetSequenceInfo` *(draft)*.

**How a scene holds an NPC** (a `scripted_sequence` / `aiscripted_sequence`):
1. `BeginSequence 0x101a7390` → `PossessEntity` (`CCineNPC` slot 583 `0x101a7880`, `CCineAI`
   `0x101a9080`) writes `m_hCine +0x5d74`, `m_hTargetEnt`, `m_pGoalEnt`, the captures, and
   `m_scriptState +0x5d70` by `m_fMoveTo` (1 → 4, 2 → 5, 3 → 6, 0/4/5 → 1 WAIT), `DelayStart(1)`,
   and `m_IdealNPCState = 4` (line `0x2c8`) *(read: ported whole at
   `ElysiumScriptedSequence.cpp:319-425`)*. The cine never thinks again except `CineThink
   0x101a8070` while its target cannot be found.
2. The NPC's own `MaintainSchedule 0x102817c0` sees ideal ≠ state, sets state 4 (SCRIPT), and
   reselects: `SelectSchedule 0x1028a380` case 4 → `SCHED_AISCRIPT 0x2e` while `m_hCine` is live,
   else `"Script failed for %s"` + `CineCleanup 0x1027d170` + `IDLE_STAND` *(read: ported at
   `ElysiumNpcBaseSelect.cpp:260-270`; the state change at `ElysiumSchedule.cpp:369-376`)*.
   `TranslateSchedule 0x102cc080` splits `0x2e` by `m_fMoveTo` into `0x2f..0x33`, and the Troika
   row maps them to `SCHED_TROIKA_SCRIPTED_WALK 0xf2 / _RUN 0xf4 / _CUSTOM_MOVE 0xf6 / _WAIT 0xf8 /
   _FACE 0xf9` *(read: ported at `ElysiumNpcBaseTranslate.cpp:18-64`, `ElysiumNpcTranslate.cpp:78`)*.
3. The program's tasks run the scene: `TASK_WALK/RUN/SCRIPT_CUSTOM_MOVE_TO_TARGET 8/9/10` (navigator
   goal `GOALTYPE_TARGETENT` on the cine), `TASK_PLANT_ON_SCRIPT 0x65`, `TASK_FACE_SCRIPT 0x66`,
   `TASK_ENABLE_SCRIPT 100` (`DelayStart(0)`), `TASK_WAIT_FOR_SCRIPT 0x60` (pre-idle; run arm waits
   `IsTimeToStart 0x101a7540`, then `StartScript` → `OnBeginSequence`, `StartSequence(m_iszPlay)`),
   `TASK_PLAY_SCRIPT 0x62` (state 0; run waits `m_bSequenceFinished` → `SequenceDone 0x101a8460`),
   `TASK_PLAY_SCRIPT_POST_IDLE 99` (state 2; holds while unfinished and no `m_hNextCine`, else
   `Finish 0x101a8640`) *(read: every arm is ported, `ElysiumNpcBaseStartTask.cpp:365-430,
   1544-1657`, `ElysiumNpcBaseRunTask.cpp:463-475, 691-772`)*.
4. The scene ends through retail's exits only: `Finish` → `CineCleanup 0x1027d170` (state 0,
   restores, ideal IDLE) → slot 586 `FixScriptNPCSchedule 0x101a8840` (ideal IDLE unless dead,
   `ClearSchedule 0x10280d30`); or `CancelScript 0x101a8c30` / `ScriptEntityCancel 0x101a7170`
   (state 3, `CineCleanup`); or `ExitScriptedSequence 0x1027d0a0` from `SelectIdealState`
   *(read: all ported in `ElysiumScriptedSequence.cpp` and `ElysiumNpcBaseScript.cpp`)*.
   `ShouldThinkFrequently 0x102c2430` reads `m_scriptState ∈ {4,5,6}` *(read:
   `ElysiumNpcThinkCadence.cpp:70-89`)*.

**How a dialogue holds an NPC.** No body claim and no state change: a dialogue writes no
`m_NPCState` (`findings-B` row 5, *draft*). The partner is `m_hDialogPartner +0xfe8`
(`CBaseCombatCharacter`, writer `SetDialogPartner 0x10107050`); `IsInDialog 0x102c1170` is
`m_bIsTalking +0x64c0` ∥ a non-empty `m_szDialogQue +0x64ec` ∥ a live `+0xfe8` ∥ the `+0x6554` handle
*(read: `conditions-and-states.md:358-367`)*.
- The three inputs `StartPlayerDialog 0x1029ef80`, `…Remote 0x1029f060`, `…Unforced 0x1029f120`:
  guards, `FinishTalking`, think reset, `m_bForceDialogStart +0x6495`, and a **forced schedule**:
  `SCHED_TROIKA_START_PLAYER_DIALOG 0x6d` (and per `game_runtime.md:1308-1310` `0x6e` for Remote;
  `lifecycle.md:232-234` says all three install `0x6d` — **the two docs disagree; packet R2
  settles it**). The program walks up and runs `TASK_START_PLAYER_DIALOG 0xda` (`0x102a4e7e`:
  closest player, slot 295 `CanTalk`, `player->vtable[0x678](this)` = `FUN_10178280`, the real
  StartDialog: `CDialog::Acquire`, `SetDialogPartner`, input lock, holster, camera), then
  `TASK_RUN_DIALOG 0xb9` *(read: `schedule-kernel.md:2784`, `game_runtime.md:1493-1507`; the
  program text from `script_dialog_hold.json`, record)*.
- `+use`: `CBasePlayer::PlayerUse 0x10167850` tests `WillTalk`, **clears the NPC schedule and
  pushes `0x6a SCHED_TROIKA_RUN_DIALOG`** (`TASK_RUN_DIALOG 0`, interrupt `COND_PROVOKED`), then
  calls `FUN_10178280` *(read: `game_runtime.md:1493-1496`, `conditions-and-states.md:136`)*.
  `GetSchedule 0x102ae920` step 12 also answers `0x6a` in state 1 for a live `m_hDialogPartner`
  *(read: `conditions-and-states.md:101-103`)*.
- `TASK_RUN_DIALOG 0xb9` (start `0x102a496b`, run `0x102ab303`): both arms call `0x102c1400`; while
  `IsInDialog` it answers the disposition activity (slot 611) which the arm sets (slot 310) and
  holds the motor's yaw; once all four terms clear it runs `0x102c0360` and answers −1 → the task
  completes (run arm also clears `COND_HEAR_PLAYER 0x6f`). No fail, no facing, no route stop.
  **`COND_PROVOKED` ends the program, not the conversation** *(read:
  `conditions-and-states.md:358-367`)*.
- The end: `CDialog::Release 0x100e5240` flushes the pending script, clears the live state, then
  calls the NPC's `0x102c0360`, which fires `m_OnDialogEnd +0x5f5c` and clears the partner
  *(read: `game_runtime.md:1363-1370`; `0x102c0360` walked in `shape.md:2917` and ported as
  `FElysiumNpc::OnDialogRelease`, `ElysiumNpcTroikaHelpers2.cpp:347`, with no production caller
  today)*. `0x102c1400` itself is **unrecovered** (`lifecycle.md:2095`): packet R1.
- RunAI skips the gather while `m_hDialogPartner` lives and `!bReduced` (`0x1026f1f0`) *(read:
  `schedule-kernel.md:4501`)* — today's port reads a session bit for it (`ElysiumNpcAnim.cpp:81-88`).

**How a place or a patrol is held and released.** A place and a patrol are programs, nothing
else. `SelectSchedule` (Troika `0x102af660`) case 1: a patrol path's node schedule first
(`0x102af6b6..0x102af743`), then `m_bUseInteresting +0x63d9` → `0xff
SCHED_TROIKA_WALK_TO_INTERESTING_PLACE_SETUP` when the navigator's goal type is not 8 and
`m_pInterestingPlace +0x62ec` is null, else `0x102` crosswalk / `0x106` / `0x105` / `0x100`
*(read: ported at `ElysiumNpcSelect.cpp:577-612`)*. `0xff` is `FIND_INTERESTING_PLACE
(PickRandomInterestingPlace 0x102db590); SET_PRESERVE_PATH 1; SET_SCHEDULE 0x100` (record). The
visit's claim, the into/idle/outof phases and the wait are retail words (`+0x62e8`, `+0x62ec`,
`+0x6304`, `+0x63d4`, `m_flWaitFinished +0x5db4`) driven by `0x102a9f40` and the loop `0x102aa210`
*(read: ported at `ElysiumNpcHints.cpp:619-818`)*. The visit is released by `0x102b53d0`
(`"Leaving interesting place (…)"`) from exactly these callers: `OnScheduleChange 0x102a0940` at
`0x102a09a0`, **only under `!PRESERVE_PATH`** *(read: ported at `ElysiumNpcMaintain.cpp:151-167`)*,
`TaskFail 0x1029adb0` step 1, the RunTask wait-finished arm, `Event_Killed`, `UpdateOnRemove
0x1028d6e0` *(read: `research where 0x102b53d0`)*. `InputUseInteresting 0x102c2a70` writes the byte
`+0x63d9` and nothing else (record; packet R2 verifies).

**What ends a program.** Only the kernel: task completion, a failure routed to the fail schedule,
an interrupt, `ClearSchedule 0x10280d30` (from a body; reselect next think), or a forced
`SetSchedule`. Each change runs slot 435 `OnScheduleChange 0x102a0940`, which clears the navigator
goal under `!PRESERVE_PATH` *(read: `ElysiumNpcMaintain.cpp:143-199`)*. Retail saves anywhere: the
datamap carries `m_NPCState`, `m_hCine`, `m_scriptState`, and `OnRestore`'s
`DiscardScheduleState 0x1027be60` strips a dead cine *(draft)*.

**A pushed order (`aiscripted_schedule`).** Executor `CCineAISchedule::vfunc586 0x101a98c0`:
resolve `goalent` (none → log and return before the force state), `SetState` by the
authored→native table (0/1→1/2→3/3→2), then mode 1/2 `ScheduledMoveToGoalEntity 0x102800c0`, 3
`SetEnemy` + `NEW_ENEMY 0x54`, 4/5 `ScheduledFollowPath 0x102801e0`; base `2 IDLE_WALK`,
translated by slot 440 to `0x46 SCHED_TROIKA_IDLE_PATROL`. It never resets, has no end and no
release *(read: `authored-control.md:251-320`)*. The `0x102ae840` push (`m_eForcedState +0x65cc`,
`m_bForceStateChange +0x1b28`) is a separate body other callers use *(read: `schedule-kernel.md:1033`)*.

## 2. What the port does instead

### The arbiter, counted 2026-10-04

The draft counted **38 production sites in 7 files** (9 claims, 15 releases, 9 gates, 5 script
wrappers). Counted today by Grep over `Source/ElysiumUE/Private/{Substrate,Debug}` (every call that
acquires, releases or reads the owner, wrappers' call sites included, declarations excluded):
**15 claims, 22 releases, 13 gates, 3 debug reads = 53 sites in 10 files**, plus the vocabulary in
`Public/ElysiumNpcMindTypes.h`, `ElysiumNpcMind.{h,cpp}`, `Public/ElysiumEntity.h:491-516`,
`ElysiumDialogueSession.h:322` and `ElysiumNpc.h` (`364-378`, `490-492`, `595-640`, `747`,
`1107`, `1115-1117`, `1161-1170`). The difference from the draft is method, not growth: the draft
did not count the world-side dialogue session, the wrapper call sites or `ReleaseAllBodyOwnership`.

| kind | sites (file:line) |
|---|---|
| claims (15) | `ElysiumNpc.cpp` 557, 574, 608, 954, 1165, 1471, 1607, 2299, 2338; `ElysiumNpcBaseStartTask.cpp` 2229, 2538; `ElysiumNpcScript.cpp` 634; `ElysiumScriptedSequence.cpp` 1271; `ElysiumEntityWorldDialogue.cpp` 266, 283 |
| releases (22) | `ElysiumNpc.cpp` 556, 567, 593, 643, 873, 1068, 1082, 1495, 1603, 1669, 1745, 2118, 2288, 2298, 2313, 2521, 2527, 2530 (`Invalidate`); `ElysiumNpcMaintain.cpp` 135; `ElysiumScriptedSequence.cpp` 858; `ElysiumEntityWorldDialogue.cpp` 1378; `ElysiumNpcDialogue.cpp` 150 |
| gates (13) | `ElysiumNpcAnim.cpp` 421-422 (`PlaySequenceClip`); `ElysiumNpc.cpp` 950, 1330-1332 (stance), 1462, 1481, 2730-2741 (restore), 2849-2852 (save), 2923 (`SaveBlockReason`); `ElysiumNpcBaseStartTask.cpp` 2227, 2537; `ElysiumNpcScript.cpp` 633; `ElysiumNpcDialogue.cpp` 148; `ElysiumEntityWorldDialogue.cpp` 264-268 |
| debug (3) | `Debug/ElysiumCastRun.cpp:672`; `Debug/ElysiumCogWindow_Npc.cpp:1213`; `ElysiumNpc.cpp:2982` |

### The port-only mechanisms V3 removes

| # | mechanism | port | retail word |
|---|---|---|---|
| M1 | `PlaySequenceClip` plays only under owner None/Dialogue; a never-played row commits at rate 0 | `ElysiumNpcAnim.cpp:415-445` | `ResetSequenceInfo 0x10090950` on every body |
| M2 | stance-transition owner gate | `ElysiumNpc.cpp:1330-1332` | `SetDisposition 0x102c0f70`, gated on `m_bDisableAI` only |
| M3 | `Schedule` claim in `SetGoal` / `SetRandomGoal` / `AdvancePath`; `bScriptedOrderHolds` bypass | `ElysiumNpcBaseStartTask.cpp:2181-2229, 2537-2538`; `ElysiumNpcScript.cpp:627-652`; `ElysiumNpc.cpp:1459-1508` | none: the program owns the navigator |
| M4 | `ReleaseProgramBody`'s `Motor->Stop` | `ElysiumNpc.cpp:1474-1498` | `OnScheduleChange` `0x102a0992` (already ported) |
| M5 | `RouteScheduleMaintenance` and its five pre-steps | `ElysiumNpc.cpp:848-1007`; caller `ElysiumNpcBaseMaintain.cpp:53-57` | `MaintainSchedule 0x102817c0` alone |
| M6 | the ambient executor (`ThinkAmbient`, `BeginAmbientUse`, `BeginAmbientLeave`, `PlayAmbientActivity`, `FailedSpotIndices`, `AmbientLeaveAt`, `AmbientActivityCycle`, the `Ambient` claim, the motor stop in `FinishAmbientUse`) | `ElysiumNpc.cpp:1121-1175, 1235-1288, 2040-2240, 2091-2130`; `ElysiumNpc.h:1174-1181` | programs `0xff/0x100/…`, loop `0x102aa210`, release `0x102b53d0` |
| M7 | the place release under any program (red 6) | `ElysiumNpc.cpp:985-988`; also `320` (`UseInteresting 0`), `573`, `603-607`, `1596-1599`, `2337` | `0x102b53d0` only from §1's callers |
| M8 | the external-executor return | `ElysiumNpcMaintain.cpp:348-356, 464-469`; `ElysiumNpcMaintain.inl:38, 45`; `ElysiumSchedule.cpp:297-302, 385-401, 421-423`; `ElysiumSchedule.h:260`; `ElysiumNpc.cpp:1829` | `MaintainSchedule` reselects in the same pass (`0x10281be5`, `0x10281c46`) |
| M9 | the scripted-beat stand-in (`TickBeat`, `EBeatPhase`, `BeatClipEndsAt`, `PlayBeatClip`, travel, `PlaceOnMark`, `bRestartBeat`) and the body claim it takes | `ElysiumScriptedSequence.h:24-31, 210-229, 262-276`; `.cpp:427, 1263-1512`; `ElysiumAiScriptedSequence.cpp` (its `BeginBeat`) | `SCHED_AISCRIPT` and the ported task arms |
| M10 | `m_scriptState` held on the cine (`NpcScriptState`) | `ElysiumScriptedSequence.h:180-186`, `.cpp:482-488, 761-768`; shape map binds `+0x5d70` to `FElysiumScriptedCharacter::ScriptPhase` (`ElysiumNpcKernelShapeMap.cpp:181`) | an NPC word `+0x5d70` |
| M11 | the scripted-move seam (`BeginScriptMove`, `AdvanceScriptMove`, `EndScriptMove`, `ScriptPhase`, `ClaimScriptMove`) | `Public/ElysiumEntity.h:44-60, 472-499`; `ElysiumScriptedCharacter.{h,cpp}` (`h:17-22, 31, 80-82`; `cpp:33-345`); `ElysiumNpc.cpp:571-594` | the navigator goal of tasks 8/9/10 |
| M12 | the dialogue body session (`Begin/EndDialogueBodySession`, `DialogueBodyOwner`, `ThinkInDialog`, the synchronous open at the input) | `ElysiumNpc.cpp:917-937, 2242-2339`; `ElysiumNpcDialogue.cpp:15-110, 148-151`; `ElysiumEntityWorldDialogue.cpp:249-294, 1332-1378`; `ElysiumDialogueSession.h:322` | `0x6d/0x6e/0x6a`, `TASK_START_PLAYER_DIALOG`, `TASK_RUN_DIALOG`, `0x102c0360` |
| M13 | the partner as a session bit | `ElysiumNpcAnim.cpp:81-88`; `ElysiumEntityWorldDialogue.cpp:529` | `m_hDialogPartner +0xfe8` |
| M14 | the `ScriptedSchedule` claim and the order's lifetime coupling (`EndScriptedSchedule` at program end, the `UpdateIdealState` gate, `Mind.RequestState` for `forcestate`) | `ElysiumNpc.cpp:1057-1060, 1088-1095, 1510-1670`; `ElysiumNpcMaintain.cpp:132-136` | `0x101a98c0`: `SetState`, the mover, no end |
| M15 | `RefreshStateFromOwner` writes `m_NPCState`; the owner enum, tokens, parked slot, `Follower` | `ElysiumNpcMind.{h,cpp}` (`cpp:167-338`); `Public/ElysiumNpcMindTypes.h:18-69` | `SetState 0x1026e340` from retail's writers only |
| M16 | the NPC's save refusal and the owner byte in the save | `ElysiumNpc.cpp:2846-2862, 2921-2931, 2727-2756` | none (datamap words) |
| M17 | `bMoveIssued` standing for `IsGoalActive` in readers; `bWalkingAnimation` written, never read | readers `ElysiumNpcStartTask.cpp:1827`, `ElysiumNpcBaseStartTask.cpp:2363`, `ElysiumNpc.cpp:1895`; `bWalkingAnimation` `ElysiumNpcBase.h:681` + 9 writes | navigator `IsGoalActive 0x102ee6a0` (`NavIsGoalActive()`, `ElysiumNpcBaseMotor.cpp:101`) |

**Already retail in the port — reuse, do not rewrite:** `PossessEntity`, `StartSequence`,
`SequenceDone`, `Finish`, `CineCleanup`, `CancelScript`, `FixScriptNPCSchedule`; `SelectSchedule`
case 4; `TranslateSchedule`; every scripted task arm; `OnScheduleChange`'s `PRESERVE_PATH` release;
the interest-place selector, programs, wait and loop; `TASK_START_PLAYER_DIALOG`'s start arm
(`ElysiumNpcStartTask.cpp:1916-1925`); `TASK_RUN_DIALOG`'s arms (`ElysiumNpcStartTask.cpp:1684-1697`,
`ElysiumNpcRunTask.cpp:1184`); `0x102c0360` (`OnDialogRelease`); `StudioFrameAdvance`;
`ResetSequenceInfo`. V3 is mostly deletion and re-wiring, which is why it is cheaper than its size
suggests.

## 3. The seam (method step 1) — one commit, opening V3a

Lands first, builds green on its own, changes no behaviour, leaves every acceptance record red.
Written and built by one seam agent (`brief-A0-seam.md`), reviewed once by Fable
(`brief-A0-review.md`).

| stub | retail | admitting default | stands for |
|---|---|---|---|
| `int32 FElysiumNpcBase::ScriptState` + `GetScriptState()` / `SetScriptState(int32)` | `m_scriptState +0x5d70` (writers `0x101a7880`, `0x102827f0`, `0x1027d170`, `0x101a7170`) | 0; nothing writes it yet | the NPC's word the cine holds today (`NpcScriptState`) |
| shape-map row `0x5d70` moved to `FElysiumNpcBase::ScriptState` | — | — | `ElysiumNpcKernelShapeMap.cpp:181` |
| `FElysiumEntityHandle FElysiumCombatCharacter::DialogPartner` + `GetDialogPartner()`, declared `SetDialogPartner(const FElysiumEntityHandle&)` with an empty body | `m_hDialogPartner +0xfe8`, `SetDialogPartner 0x10107050` | invalid; the setter does nothing (comment: "0x10107050, filled in V3d after packet R1") | the session bit `HasLiveDialogPartner` reads today |
| `int32 FElysiumNpcBase::LookupSequence(const FString&)` | `CBaseAnimating::LookupSequence` | already a seam answering −1 (`ElysiumNpcBaseAnim.cpp:38-44`): unchanged | filled in V3c |
| `int32 FElysiumNpc::RunDialogActivity()` | `0x102c1400` | already a seam (`ElysiumNpcRunTask.cpp:307-321`): unchanged | filled in V3d after packet R1 |
| the harness action `dialog_choose` (H12) | the player's response pick / end (`game_runtime.md` § Turn: pick −1 releases) | — | `script_dialog_hold`'s release half |
| records, red: new `world/script_aischedule_walk.json`, `world/dialog_use_hold.json`; `script_dialog_hold.json` gains its release half (H12) and its `about` corrected (`0x6a` is the schedule, the task is `0xb9`); `known_red` of every V3 record names its sub-story | as §1 | — | — |

Files the seam commit touches: `Private/Substrate/ElysiumNpcBase.h`, `Public/ElysiumPlayer.h`
(the combat character), `Private/Substrate/ElysiumCombatCharacter.cpp`,
`Private/Substrate/ElysiumNpcKernelShapeMap.cpp`, `Private/Debug/ElysiumArenaScenario.{h,cpp}`,
`Private/Debug/ElysiumArenaScenarioRunner.{h,cpp}`, `Arena/README.md`, `Arena/scenarios/world/*.json`
(the four named), `Arena/scenarios/cover.json` and the patrol / place records' `known_red` text only.

## 4. The cut

V3 cannot fit one story under rule 8 (≤5 agents, ≤2 builds): it needs about eleven coder lanes.
Cut by dependency — the kernel's own body first, then each consumer the routing serves, then the
arbiter's last vocabulary:

| story | size | agents | builds planned (allowed) | records turned green |
|---|---|---|---|---|
| **V3r** reading | S | 1 reader | 0 | — (packets R1, R2 into `docs/vtmb/`) |
| **V3a** seam + the kernel's body plays | M | seam agent, Fable review, A1, A2, integrator = 5 | 2 (2) | `patrol_sentry2_pingpong`, `patrol_monk_loop`, `input_clearpatrolpath`; `cover` through `outof_done` (see the V4 line) |
| **V3b** places and patrols as programs | M | B1, B2, integrator = 3 | 1 (2) | `places_pedestrian_visit`, `places_thug_pt1`, `input_useinteresting`, `map_hub_idle`, `hub_crosswalk_wait`, `rollcall_vhuman`, `rollcall_vhumancombatpatrol`, `map_tutorial_sneak_past` first half |
| **V3c** the scene hold (0003/1–2's kernel half) | M | C1, C2, C3, integrator = 4 | 1 (2) | `script_walk_to_mark` (and N11's record staged red for V7) |
| **V3d** the dialogue hold; the arbiter deleted whole | M–L | D1, D2, D3, integrator = 4 | 1–2 (2) | `script_dialog_hold`, `input_startplayerdialogremote`, `dialog_use_hold`, `script_aischedule_walk`; every V3a–c record still green |

Order V3r → V3a → V3b → V3c → V3d. 17 agents; 5–6 builds planned, 8 allowed. V3b and V3c could
swap; places first because red 6 also silences the tutorial's `thug_1` and the hub.

**V3a — the kernel's body plays.** Seam, then: `PlaySequenceClip` unconditional (M1), the stance
gate's owner term (M2), the `Schedule` claim and its bypass (M3), `ReleaseProgramBody`'s stop (M4);
`bWalkingAnimation` deleted, the two `IsGoalActive` readers of `bMoveIssued` moved to
`NavIsGoalActive()` (M17). The `Sequence`, `Ambient`, `Dialogue`, `ScriptedSchedule` claims stay
until their sub-story; they cannot be refused by a `Schedule` claim any more because none exists.
Safe while the executors live: a beat NPC is in SCRIPT (RunAnimation's idle re-pick skips state 4)
and its clip is on the montage slot. Transitional, until V3b: an ambient-executor NPC runs no
program, but RunAnimation's idle re-pick (`ElysiumNpcBaseAnim.cpp:136-149`) may now replace the
executor's clip with the idle sequence — the place records are red in V3a anyway.
- A1: `ElysiumNpcAnim.cpp`, `ElysiumNpc.{h,cpp}`.
- A2: `ElysiumNpcBaseStartTask.cpp`, `ElysiumNpcScript.cpp`, `ElysiumNpcStartTask.cpp`,
  `ElysiumNpcBase.{h,cpp}`, `ElysiumNpcMaintain.cpp` (lines 175-176 only), `Tests/ElysiumNpcCombatTests.cpp`,
  `Tests/ElysiumNpcKernelSelectTests.cpp`.

**V3b — places and patrols.** The ambient executor deleted (M6), the place released only by
`0x102b53d0`'s callers (M7: `ThinkSchedulePolicy`, `UseInteresting`'s release), the external
return (M8), `ThinkAutonomous`; `RouteScheduleMaintenance` keeps only the watchdog, `ThinkInDialog`,
`ThinkScriptOwned`, then `ThinkStanceOrIdle`. `ClaimAmbientSpot` becomes `PickRandomInterestingPlace`
+ `PickSpotFor` without the arbiter or the blacklist. The `573/603/1596/2337` pre-claim releases stay
for V3c/V3d, which delete the claims they precede.
- B1: `ElysiumNpc.{h,cpp}`, `Public/ElysiumSaveTypes.h` (schema bump).
- B2: `ElysiumNpcMaintain.{cpp,inl}`, `ElysiumSchedule.{h,cpp}`, `ElysiumNpcRunTask.cpp`,
  `ElysiumNpcSaveRestore10.cpp`, `ElysiumNpcStartTask.cpp` (`0x102a1f23` arm comment only),
  `Tests/ElysiumNpcKernelMaintainTests.cpp`.

**V3c — the scene hold.** `m_scriptState` on the NPC (M10); the beat stand-in deleted (M9); the cine
writes the NPC's words and claims nothing; `StartSequence` through `LookupSequence` → `m_nSequence`
→ `ResetSequenceInfo` on the sequence bridge, so `TASK_PLAY_SCRIPT` waits the kernel's own
`m_bSequenceFinished`; `ThinkScriptOwned` / `TickScriptWatchdog` and the `Sequence` claim deleted;
the scripted-move seam deleted (M11). An NPC whose model has no walk sequence completes
`WALK_TO_TARGET` at once and is planted by `TASK_PLANT_ON_SCRIPT` — retail's own answer for the
bodiless case 0003/4 worried about.
- C1: `ElysiumScriptedSequence.{h,cpp}`, `ElysiumAiScriptedSequence.{h,cpp}`,
  `ElysiumAiScriptedSchedule.{h,cpp}` (only where it reaches the beat).
- C2: `ElysiumNpc.{h,cpp}`, `ElysiumNpcBase.{h,cpp}`, `ElysiumScriptedCharacter.{h,cpp}`,
  `Public/ElysiumEntity.h`, `ElysiumNpcThinkCadence.cpp`, `ElysiumNpcBaseMaintain.cpp`.
- C3: `ElysiumNpcBaseAnim.{cpp,inl}` (`LookupSequenceByName`), `ElysiumNpcAnim.cpp` (the bridge row
  by name), `ElysiumNpcBaseStartTask.cpp` (the `NpcScriptState` writes → the NPC word),
  `Debug/ElysiumCastRun.cpp`, `Tests/ElysiumSequenceTests.cpp`,
  `Tests/ElysiumNpcKernelDirectorTests.cpp`, `Tests/ElysiumNpcKernelScriptTests.cpp`.

**V3d — the dialogue hold, then the arbiter whole.** The inputs force their programs; `+use`
clears and pushes `0x6a`; the conversation opens from `TASK_START_PLAYER_DIALOG`; `m_hDialogPartner`
is a real handle; `TASK_RUN_DIALOG` holds through `0x102c1400` (packet R1) and releases through
`0x102c0360`; `CDialog::Release` calls `0x102c0360` once (M12, M13). Then `ThinkInDialog` and
`RouteScheduleMaintenance` deleted (Troika calls `MaintainScheduleRetail`), the `ScriptedSchedule`
claim and the order coupling (M14: the executor does `SetState`), the mind's owner half, the enum,
the tokens, `Follower`, `RefreshStateFromOwner` (M15), the save owner byte and the NPC's
`SaveBlockReason` with a schema bump (M16), the debug reads.
- D1: `ElysiumNpcDialogue.{h,cpp}`, `ElysiumEntityWorldDialogue.cpp`, `ElysiumDialogueSession.h`,
  `ElysiumNpcRunTask.cpp`, `ElysiumNpcStartTask.cpp`, `ElysiumNpcTroikaHelpers2.cpp`,
  `ElysiumCombatCharacter.cpp`, the dialogue tests.
- D2: `ElysiumNpc.{h,cpp}`, `ElysiumNpcMaintain.cpp`, `ElysiumNpcBaseMaintain.cpp`,
  `ElysiumNpcAnim.cpp` (`HasLiveDialogPartner`), `Public/ElysiumEntity.h`, `Public/ElysiumSaveTypes.h`.
- D3: `ElysiumNpcMind.{h,cpp}`, `Public/ElysiumNpcMindTypes.h`, `Debug/ElysiumCogWindow_Npc.cpp`,
  `Debug/ElysiumNpcDebugData.{h,cpp}`, `Debug/ElysiumEntityDebugSubsystem.cpp`, the arbiter's tests
  (§6).

**What cannot go fully green until V4.** `cover`'s `fires` / `fired` are a ranged attack whose shot
is an anim event (slot 258 `DispatchAnimEvents 0x10091880`, V4) and whose wait is N2 (V5). V3a's
acceptance for `cover` is the trace through `outof_done`: `sequence <cover clip> rate=1`,
`seqfinished`, `taskdone task_play_cover_outof`. If `atk`/`fires`/`fired` then fail, the integrator
retargets the record's `known_red` to red 3 / N2 with the trace line, not a V3 red. The same rule
for the patrols: green expected (the walk plays at its rate); a residual failure is re-triaged under
the bug protocol (review doubt 1). `map_tutorial_sneak_past`'s hearing half stays red on V12/R1.
`script_walk_to_mark`'s `played` needs only `m_bSequenceFinished` (read directly by `0x62`'s run
arm, not slot 251), so V3c needs nothing from V4.

## 5. Reading packets (method step 4) — V3r, before any coder

| packet | what must be recovered | size | feeds |
|---|---|---|---|
| **R1** | `0x102c1400` (506 bytes, `TASK_RUN_DIALOG`'s helper; unrecovered per `lifecycle.md:2095`): the four `IsInDialog` terms, the `+0x6554` release, `FinishTalking`, `0x102c0520`, the slot-611 disposition answer (0xf1 or 1), `ShowPlayerChoices`, when it calls `0x102c0360`. And: who writes and clears the NPC's `+0xfe8` (`SetDialogPartner 0x10107050`'s callers: `FUN_10178280`, `CDialog::Release 0x100e5240`, `0x102c0360`), and whether `CDialog::Release` and `0x102c0360` can both fire `OnDialogEnd` for one conversation | ~0.8 KB of listing | D1 |
| **R2** | the three inputs `0x1029ef80`, `0x1029f060`, `0x1029f120`: guards, the `+0x5bac` store, the forced install path, and **which schedule each forces** (`0x6d` vs `0x6e`, the docs disagree); the `0x6d` / `0x6e` program texts; `PlayerUse 0x10167850`'s NPC arm (order of `ClearSchedule`, `SetSchedule(0x6a)`, slot 414); `InputUseInteresting 0x102c2a70` (a byte write and nothing else?); and, if cheap, whether retail refuses a save with a conversation open | ~0.6 KB | D1, B1 |

Everything else V3 ports is walked in `docs/vtmb/` or already ported (§2's last paragraph). The
reader records R1/R2 in `npc-ai/conditions-and-states.md` § `TASK_RUN_DIALOG` and
`game_runtime.md` § Runtime / branching, correcting whichever doc is wrong.

## 6. Tests

Deleted, not converted (decision "port-only tests are deleted"); each lane's brief names its own:

| test | file | tag (findings F) | lane |
|---|---|---|---|
| `Arm.NpcMind.BodyOwner`, `Arm.NpcMind.Restore` | `ElysiumNpcMindTests.cpp` | port | D3 |
| `Arm.AiScriptedSchedule.MoveToGoal`, `.AssignEnemy`, `.Refusals`, `.NamedSchedule`, `.Precedence`, `.CombatPreemption` (6 of 7; `.Tables` stays) | `ElysiumAiScriptedScheduleTests.cpp` | port / stub / dupl | D3 |
| `Arm.ScriptedSequenceBodyClaim` | `ElysiumSequenceTests.cpp:1136` | port | C3 |
| the beat-driven `Substrate.ScriptedSequence*`, `PlayerControllerSequenceLocomotion` and `NpcKernelDirector.*` tests that drive `TickBeat` / `EBeatPhase` / `BeginScriptMove` (C3 audits each; expected ≈8: `ScriptedSequence`, `ScriptedSequenceLocomotion`, `PlayerControllerSequenceLocomotion`, `ScriptedSequenceSelfChain`, `NpcKernelDirector.StartGate`, `.SaveRestore`, `.RevisitMidBeat`, `.AiEndToEnd`, `.LookWalk`) | `ElysiumSequenceTests.cpp`, `ElysiumNpcKernelDirectorTests.cpp` | port | C3 |
| `Arm.DialogueCamera.BodyOwnerLifecycle` | `ElysiumDialogueCameraTests.cpp:358` | port | D1 |
| `NpcCombat.Chase`, `NpcCombat.Death` | `ElysiumNpcCombatTests.cpp` | port | A2 |
| owner assertions only (lines): `NpcKernelSelectTests.cpp:512, 520`; `ElysiumDialogueTests.cpp:1747-1756`; `ElysiumNpcThinkCadenceTests.cpp:400-403`; the `TakeExternalExecutorReturn` override `ElysiumNpcKernelMaintainTests.cpp:164` | — | A2, D1, B2 |

Replaced by the arena records of §4 and two arm tests the draft named, each pinning a retail
address: `OnScheduleChange 0x102a0940` releases the place only under `!PRESERVE_PATH` (B2), and
`SelectSchedule 0x1028a380` case 4 by `m_fMoveTo` through `TranslateSchedule` (C3, if
`NpcKernelTranslate` does not already pin it — check first). `Arm.NpcMind.Admission` stays while the
admission barrier does (Q5). Kept: the retail cine tests (`NpcKernelScript19.*`, `NpcKernelDirector`'s
`Spawn`, `RadiusGate`, `Queue`, `AiPossess`, `AiFinishSchedule`, `Interrupt`, `Removal`,
`KilledMidPlay`, `ConstantSlots`, `ProceduralRadius`) once their beat assertions are cut.

## 7. Kept divergences (rule 2), for the owner

- **K1 (recommended: accept).** `FElysiumScriptedSequence::SaveBlockReason` keeps refusing a save
  while the cine possesses an NPC (`m_hCine == this`), re-keyed from the beat phase to the
  possession, until V6 lands resume. Why: retail saves mid-scene and resumes at the task cursor
  `+0x5c50`; the port restarts the restored program (divergence 3, V6), and a restarted `0xf2` re-runs
  `TASK_ENABLE_SCRIPT` and `TASK_WAIT_FOR_SCRIPT`, firing `OnBeginSequence` twice — an event-order
  change worse than a refused save. V6 deletes it with the restart.
- **K2 (proposed: drop the draft's).** The draft kept "a scene's clip played by the scene's montage
  slot while `m_scriptState ∈ {1,2}`". This design does not need it: `StartSequence` writes
  `m_nSequence` through `LookupSequence` into the sequence bridge, already a named modernization (the
  studio table swapped for the clip resolver). If the bake cannot answer a named clip's
  `STUDIO_LOOPING` bit, the bridge row's loop bit falls back to today's rule (pre/post-idle loop,
  `m_iszPlay` once), named at its line — the only residue.
- **Not V3's, unchanged:** the choreographed scene's montage-slot clips and its stamp of
  `ScriptOwner` on its cast (`ElysiumScriptedSequence.cpp:339`, `choreographed_scenes.md` § Where the
  rebuild diverges, 0010's); the sequence bridge itself; the motor on the body's tick.

## 8. Risks and open questions, each with a recommendation

- **Q1. The cut.** Five stories instead of one. *Recommend accept*: each is testable; the alternative
  breaks rule 8.
- **Q2. K2 above.** *Recommend* the `m_nSequence` path; it is retail's and removes a divergence.
- **Q3. K1 above.** *Recommend accept*, V6 retires it.
- **Q4. The world's own refusal while a conversation is open** (`FElysiumEntityWorld::
  ScriptedSessionSaveBlockReason`, `ElysiumEntityWorldDialogue.cpp:1152-1157`) is not the arbiter and
  V3 leaves it. *Recommend* R2 answers it if cheap; otherwise it stays, named, outside V3.
- **Q5. The admission barrier** (`FElysiumNpcMind::Admit`, `ElysiumNpc.cpp:782-794`, and the
  deferred `aiscripted_schedule` replay `:811-823, 1533-1546`) is port-only and is not in V3's
  text. *Recommend* V6 (lifecycle: `NPCInit`'s `NPCInitThink 0x10273aa0`) owns it; V3 only stops the
  dialogue and the order from asking it for a body.
- **Q6. A choreographed-scene actor will run its kernel** once `ThinkScriptOwned` goes (V3c): Troika
  case 1 answers the idle disposition for `m_bInChoreoScene` (`ElysiumNpcSelect.cpp:566-569`), the body
  is frozen by `SetBodyFrozen`, and the scene's clip is on the montage slot over the base. That is
  retail's shape, but no V3 record stages a scene. *Recommend* the V3c integrator runs the scene's
  family filter (`Elysium.Substrate.Dialogue.BodyScene` and the choreo tests) and `map_tutorial_idle`.
- **Q7. Double `OnDialogEnd`.** Today `FElysiumNpcDialogue::End` fires it from the queued `EndDialog`;
  retail fires it from `0x102c0360` (called by `CDialog::Release` and by `0x102c1400`). *Recommend* D1
  routes every close through `0x102c0360` once (R1 decides the guard); whether `EndDialog` is a retail
  input at all is V7's question (the record's note calls it port-only).
- **Q8. `UpdateIdealState`** (`ElysiumNpc.cpp:1020-1069`) has no production caller; 12 tests drive
  it. V3 only removes its claim lines. *Recommend* V9 retires it with those tests.
- **Q9. Spec wording.** `spec.md` V3 says "the dialogue hold as `0x6a RUN_DIALOG`": `0x6a` is the
  schedule `SCHED_TROIKA_RUN_DIALOG`, the task is `TASK_RUN_DIALOG 0xb9`. `script_dialog_hold.json`'s
  `about` carries the same slip (a record error; the seam corrects it).
- **Q10. `bMoveIssued`.** Not a pure rename: it is also the motor's own "a leg was issued"
  bookkeeping. *Recommend* only its readers that stand for `IsGoalActive` move to the navigator; the
  member stays, its comment saying it is motor bookkeeping with no retail word.
- **Risk: filling `LookupSequenceByName` turns on six retail callers** that have been getting −1
  (the scene-event queue `ElysiumNpcAnim.cpp:151, 193`, `ElysiumNpcBaseAnim.cpp:258, 282`, the custom
  move `:391`, the arrival sequence `ElysiumNpcBaseStartTask.cpp:425`). That is retail, but a scene
  event committing `m_nSequence` under a choreographed scene's montage clip is new behaviour for 0010.
  *Recommend* C3 audits them and the V3c integrator runs the scene families; a moved scene test is
  filed, not patched.
- **Risk: `ElysiumNpc.cpp`** (3,085 lines) is touched by every sub-story; one lane per wave owns it.
- **Risk: perception during a dialogue.** Making `m_hDialogPartner` real turns on RunAI's gather skip
  (`0x1026f1f0`) for the live conversation, which is retail; V6's "an open dialogue blinds perception"
  item must be re-read against it, not re-fixed.

## 9. Harness gaps

- **H12** (a dialogue answer / close action): needed for `script_dialog_hold`'s release half and
  `dialog_use_hold`; lands in the seam.
- H6 (`from_map` by more than targetname): not needed — every V3 record's rows are named or
  record-placed.
- H13 (ensure counts): not needed by V3 (V8's).

## 10. Boundaries — named, not designed here

V4: `DispatchAnimEvents`, `HandleAnimEvent`'s arms, `SetAttackExtentsForSequence`, the weighted pick,
death, slot 363, and the rest of `SetDisposition 0x102c0f70`'s body (V3 removes only its owner
term). V5: the attack conditions, N1, N2. V6: resume (`+0x5c50`), the admission barrier (Q5),
`ScriptUnhide`, the per-level clock, the dialogue-perception item. V7: `EndDialog`'s status, N5, N6,
N11 (V3c stages its record red). R2: arrival tolerance `0x102f2ea0`, door/stale-link divergences.
0010: scenes and their cast. 0003/3–4 (Q4 of the spec): the cine's own lifecycle beyond what V3c
deletes.

## Rules for every agent of V3

- Read `CLAUDE.md`, `spec.md` § Standing rules, § The method per story, § The bug protocol, this
  README, and your brief. `Arena/README.md` if you touch records or the runner.
- **Query budget**: 10 s warns (log to `$ELYSIUM_WORK_ROOT/logs/slow-queries.tsv`), 60 s stops
  (never retried as-is or widened). Never read a file over ~200 KB whole. Look an address up with
  `uv run elysium research where <addr>` / `research section` before searching `docs/`.
- Text through the built-in **Grep / Read / Glob** tools, never shell `grep`/`cat`/`sed`.
- **Wait on a background command by its completion notification**, never a sleep or polling loop.
- **Coders never build**, never launch the editor, never run the arena or a suite; a coder may run
  only the single Python test file of a module it changed. Touch only your brief's files; a line
  another file needs goes in your report, exact, with its place.
- No record under `Arena/scenarios/` is edited by a coder; the integrator does it.
- Follow the listing; cite the address at every line you port. A retail input with no source yet is
  a seam answering "nothing", named for the retail field. Divergences only as named in §7.
- No new test of a port mechanism. Do not commit. Report ≤300 words.

## Shared names (fixed here so lanes agree)

- `FElysiumNpcBase::GetScriptState()` / `SetScriptState(int32)` — `m_scriptState +0x5d70`.
- `FElysiumCombatCharacter::GetDialogPartner()` / `SetDialogPartner(const FElysiumEntityHandle&)` —
  `+0xfe8`, `0x10107050`.
- `FElysiumNpc::FinishAmbientUse(bool bFireLeft)` — V3b drops `bStopMovement`; it is `0x102b53d0`'s
  port and nothing else (no motor stop, no anim release).
- `FElysiumNpc::OnDialogRelease()` — `0x102c0360`, the one door every dialogue close goes through
  (V3d).
- Trace kinds are unchanged; records match `schedule`, `task`, `taskdone`, `sequence`, `seqfinished`,
  `state`, `output`, `input`, `move`.
