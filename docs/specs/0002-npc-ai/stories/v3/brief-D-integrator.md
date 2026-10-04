# Brief — V3d's integrator (V3's close)

Rewritten 2026-10-04 after V3c's integration. Runs after D1, D2 and D3 report, on the tree with V3c
committed. Read `AGENTS.md`; `spec.md` § Standing rules, § The bug protocol (and the owner's standing
rulings); `README.md` here (the "Corrections" block, §4 V3d, §6, §7, §8); `packets.md`; the three
briefs (`brief-D1-dialogue.md`, `brief-D2-npc-arbiter.md`, `brief-D3-mind-tests.md`) and their
reports; `stories/v1/triage.md` — N8, N13, N16, N18, N19, § "V3c integration" and its "Closing pass"
(how the last two integrators lost a build); `Arena/README.md`.

**Budget: at most two builds.** Build 1 is `uv run elysium build --arm`: it compiles the arm tier
too, so running `Elysium.Arm.` prefixes afterwards costs no second compile (V3c's integrator lost its
second build to that switch). Never run a plain `build` first.

1. **Apply the cross-lane lines** the three reports name, nothing more. Known ones:
   - D1 changes `FElysiumEntityWorld::OpenDialog`'s declaration (`Public/ElysiumEntityWorld.h:531-534`,
     D1's file per README §4) — check its callers compile (`ElysiumNpcDialogue.cpp`, the dialogue
     tests);
   - D2 deletes `BeginDialogueBodySession` / `EndDialogueBodySession` / `PrepareBodyForDialogue` /
     `GetDialogueBodyOwner` / `BeginDialog`; D1 removes their calls in its files;
   - D1 deletes `DialogFlags` / `DecodedDialogFlags`; D2's "In dialog" debug row stops reading them;
   - D3 deletes `RequestState` and the owner API; D2's executor calls `SetState(int32)`;
   - D3's `ElysiumEntityDebugSubsystem.cpp:219` reads D2's row names (`"Script state"`, `"Cine"`).
2. **Grep the whole `Source/`** for the retired vocabulary: `EElysiumBodyOwner`,
   `FElysiumBodyOwnerToken`, `BodyOwner`, `DialogueBodyOwner`, `DialogueBodySession`,
   `PrepareBodyForDialogue`, `AcquireProgramBody`, `ReleaseProgramBody`, `ScriptedScheduleOwner`,
   `ScriptedScheduleBody`, `EndScriptedSchedule`, `RefreshStateFromOwner`, `RestoredMindOwner`,
   `IsResumableOwner`, `SuspendedOwner`, `RequestState(`, `RouteScheduleMaintenance`, `ThinkInDialog`,
   `STORY8-TWIN`, and V3a–V3c's (`AcquireSequenceBody`, `ClaimScriptBody`, `ThinkAmbient`,
   `TakeExternalExecutorReturn`, `EBeatPhase`, `NpcScriptState`, `BeginScriptMove`). Zero hits outside
   comments that say what retail does instead (fix any comment that only says "retired").
   `ReleaseAllBodyOwnership` may survive renamed (D2's report says).
3. **Ledger before the build**: regenerate the ledger and run `uv run elysium kernel --check` (clean);
   the generated bindings regenerate C++ if the shape map binds `+0xfe8`, so this goes ahead of build 1.
4. **Build 1**: `uv run elysium build --arm`. Fix only integration breaks (compile, link, a wrong call
   across two lanes' files).
5. **Families, once, one boot**: `uv run elysium test Elysium.Arm.NpcKernelDialogue.
   Elysium.Arm.Dialog Elysium.Arm.Dlg Elysium.Arm.NpcUseStartsDialog Elysium.Substrate.Dialogue
   Elysium.Arm.NpcKernelStartTask19. Elysium.Arm.NpcKernelRunTask19. Elysium.Arm.NpcKernelTroikaHelpers
   Elysium.Arm.NpcKernelRunAi19. Elysium.Arm.NpcKernelMaintain19. Elysium.Arm.AiScriptedSchedule.
   Elysium.Arm.NpcMind. Elysium.Arm.Session. Elysium.Arm.NpcThinkCadence
   Elysium.Substrate.NpcThinkCadence Elysium.Arm.UseTargetingEmbodiment
   Elysium.Arm.FeedTargetingOcclusion`. Expect movement where D2's executor now runs `SetState
   0x1026e340` (slot 463, the idle enemy strip) instead of the mind's request: 43 test calls of
   `BeginScriptedSchedule(FElysiumScriptedScheduleOrder(), true, State)` in `ElysiumNpcKernelAnim10Tests`,
   `…Sounds…`, `…Species…`, `…SpeciesMisc10…`, `…Squad…`, `…Motor10…`, `…Conditions2…`,
   `ElysiumNpcComfortSweepTests`, `ElysiumNpcThinkCadenceTests` (they run in the arm tier, item 8).
   Each failure is classified (test staging / record error / game) with its retail address, never
   loosened.
6. **Arena by name**, once: `uv run elysium arena script_dialog_hold input_startplayerdialogremote
   dialog_use_hold script_aischedule_walk script_walk_to_mark control_sequence cover map_tutorial_idle
   places_thug_pt1 input_useinteresting map_hub_idle rollcall_vhuman rollcall_vhumancombatpatrol
   patrol_sentry2_pingpong places_pedestrian_visit hub_crosswalk_wait`.
7. **Build 2 only if needed** (`uv run elysium build --arm` again), for integration breaks and test
   staging found in items 5–6; then re-run only what moved.
8. **V3's close, once each**: the arm tier (`uv run elysium test arm`), the default tier
   (`uv run elysium test`), the whole arena (`uv run elysium arena`, every record).

## Acceptance (README §4 V3d; triage "The fix order" row V3)

- **`script_dialog_hold`**: the program installed through `0x102ae750(0x6d, 0)` (not forced), the
  walk-up, `task_start_player_dialog`, `OnDialogBegin` from `StartTalking 0x102c0270` inside the open,
  `task_run_dialog` holding, `dialog_choose end` → one `OnDialogEnd` (`0x102c0360`) → the task completes
  → a new selection. **`input_startplayerdialogremote`**: the same through `0x102ae750(0x6e, 0)`
  (`START_PLAYER_DIALOG`, `RUN_DIALOG`, no walk-up). **`dialog_use_hold`**: needs D1's use-focus fix
  (N18: the use query refuses the NPC's own capsule); `+use` → `CanTalk` → `0x102ae750(0x6a, 0)`
  (no `ClearSchedule`) → the conversation opens → `task_run_dialog`. **`script_aischedule_walk`** green.
  Each: `known_red` removed, expectation `pass`. N8 and N18 closed in `stories/v1/triage.md`.
- **Every earlier V3 record green, or red on its stated cause only** (the V3c suite's verdicts are the
  baseline: 105 records, 71 pass, 32 expected-fail, 1 fail `rollcall_vzombie` H11, 1 unexpected-pass
  `hear_world_investigate` N4): green `cover`, `control_sequence`, `map_tutorial_idle`,
  `places_thug_pt1`, `input_useinteresting`, `map_hub_idle`, `rollcall_vhuman`,
  `rollcall_vhumancombatpatrol`; red on **N13 → V4** (`patrol_sentry2_pingpong`, `patrol_monk_loop`,
  `input_clearpatrolpath`, `places_pedestrian_visit`), **N19 → V4r's judge** (`script_walk_to_mark`),
  **N16 → V13** (`hub_crosswalk_wait`), N11 → V7 (`verbs_stealth_kill_scripted`), V12
  (`map_tutorial_sneak_past`'s hearing half). A record that fails anywhere else is a finding: bug
  protocol step 1, filed in the triage.
- Item 2's grep returns zero. The owner's rulings stand: the admission barrier (`FElysiumNpcMind::Admit`)
  is V6's and stays; `UpdateIdealState` and its tests are V9's (only its claim lines went); K1 (a cine
  refuses a save while it possesses an NPC, `FElysiumScriptedSequence::SaveBlockReason`) stays.

## At the cap

If the second build is spent and anything is still red for a reason other than its stated cause:
**stop**. No third build, no commit. Record in `stories/v1/triage.md` § "V3d integration" (as V3c's
integrator did) and report: each failing test or record by exact name, file:line, its class (test
staging / record error / game / harness) and its retail cause; the fixes written but not built.

## The commit (only when acceptance holds)

- The ledger step done (item 3; the override census and `unported.tsv` with it). Divergence 1 in
  `stories/v1/divergences.md` closed by V3d; K1 stands (accepted by the owner).
- Tick **V3d** and **V3** in `docs/specs/0002-npc-ai/spec.md` (the measured result: records, tier
  totals) and `docs/specs/TRACKER.md`, in the same commit.
- **One commit on `spec-0002/step-2`, never pushed**: `fix(npc): V3d -- the dialogue hold as
  TASK_RUN_DIALOG and m_hDialogPartner; the body arbiter retired whole`. The report goes **in the
  commit message** (a hook refuses report files): a verdict table — each record of item 6 before → after;
  the tier totals (default, arm, suite); the tests deleted across V3; the arbiter's site count now
  (zero); what is left red and where it is placed.

Rules: wait on a build, a test run or an arena run by its completion notification, never a sleep or a
polling loop. The query budget: 10 s warns (log to `$ELYSIUM_WORK_ROOT/logs/slow-queries.tsv`), 60 s
stops, never retried as-is or widened; never read a file over ~200 KB whole. Text through Grep / Read /
Glob. Report ≤300 words: build wall times, each record's verdict before and after, the family and tier
totals, the item-2 result, what is left red and where it is placed.
