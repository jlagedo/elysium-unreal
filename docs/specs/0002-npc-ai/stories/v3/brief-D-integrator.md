# Brief — V3d's integrator (V3's close)

Runs after D1, D2 and D3 report. Read `README.md` here, the three briefs, `packets.md`,
`stories/v1/triage.md` (N8, the fix order row V3), `Arena/README.md`.

1. **Apply the cross-lane lines**, nothing more. Then Grep the whole `Source/` for the deleted
   vocabulary: `EElysiumBodyOwner`, `FElysiumBodyOwnerToken`, `BodyOwner`, `AcquireSequenceBody`,
   `ClaimScriptBody`, `BeginDialogueBodySession`, `RouteScheduleMaintenance`, `ThinkAmbient`,
   `TakeExternalExecutorReturn`, `EBeatPhase`, `NpcScriptState`, `BeginScriptMove`. Zero hits outside
   comments that say they were retired (and fix those comments to say what retail does instead).
2. **Build** (`uv run elysium build`). Fix only integration breaks. V3d allows two builds; a third
   means stop and report.
3. **Run, by name**: `uv run elysium arena script_dialog_hold input_startplayerdialogremote
   dialog_use_hold script_aischedule_walk script_walk_to_mark cover patrol_sentry2_pingpong
   places_pedestrian_visit hub_crosswalk_wait map_hub_idle control_sequence`. Family filters once:
   `uv run elysium test Elysium.Arm.NpcKernelDialogue. Elysium.Arm.Dialogue Elysium.Arm.DialogueCamera.
   Elysium.Substrate.Dialogue Elysium.Arm.NpcKernelRunTask19. Elysium.Arm.NpcKernelTroikaHelpers.
   Elysium.Arm.NpcKernelRunAi19. Elysium.Arm.AiScriptedSchedule. Elysium.Arm.NpcMind. Elysium.Arm.Session.`
4. **Acceptance** (README §4, V3d; triage § "The fix order" row V3 entire):
   - `script_dialog_hold`: the forced program, the walk-up, `task_start_player_dialog`, `OnDialogBegin`
     from the task, `task_run_dialog` holding, `dialog_choose end` → one `OnDialogEnd` → the task
     completes → a new selection. `input_startplayerdialogremote` and `dialog_use_hold` green.
     `script_aischedule_walk` green. N8 closed.
   - Every V3a–V3c record still green (or carrying the red placed at its sub-story's close).
   - Zero hits from item 1.
5. **V3's close**: the arm tier once, the whole arena once, the default tier once; the ledger step
   (`kernel --check`, the override census, `unported.tsv`; the generated bindings for `+0xfe8` if the
   shape map binds it). Divergence 1 in `stories/v1/divergences.md` closed; K1 stands only if the owner
   accepted it. `stories/v3/report-d.md`: verdicts that moved, the test count deleted across V3 (from
   the four reports), the sites count now (zero).
6. **Commit once**: `fix(npc): V3d -- the dialogue hold as TASK_RUN_DIALOG and m_hDialogPartner; the
   body arbiter retired whole`. Tick **V3** in `docs/specs/0002-npc-ai/spec.md` (with the measured
   result: records, totals) and in `docs/specs/TRACKER.md` in the same commit. Do not push.

Rules: wait on a build or a run by its completion notification, never a sleep or a polling loop.
The query budget (10 s warns, 60 s stops; never read a file over ~200 KB whole). Text through Grep /
Read / Glob. Report ≤300 words: build wall times, each record's verdict before and after, the family
and tier totals, the item-1 result, what is left red and where it is placed.
