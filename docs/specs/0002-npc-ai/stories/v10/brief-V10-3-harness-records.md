# Brief V10-3 — diagnostic, boundary fixtures and tutorial hearing records

Read AGENTS.md, Arena/README, this wave's README and packets-V10. This lane owns
the support needed to make every wave behavior an arena record. Coordinator names
worktree; you never run the records. Implement support without editing substrate.

## Only these 26 files

- `Source/ElysiumUE/Private/Debug/ElysiumArenaScenario.h`
- `Source/ElysiumUE/Private/Debug/ElysiumArenaScenario.cpp`
- `Source/ElysiumUE/Private/Debug/ElysiumArenaScenarioRunner.h`
- `Source/ElysiumUE/Private/Debug/ElysiumArenaScenarioRunner.cpp`
- New `Source/ElysiumUE/Private/Debug/ElysiumArenaSoundFixtures.h`
- New `Source/ElysiumUE/Private/Debug/ElysiumArenaSoundFixtures.cpp`
- `Arena/README.md`
- `pipeline/tests/test_arena_suite.py`
- `Arena/scenarios/perception/hear_world_investigate.json`
- `Arena/scenarios/perception/interest_mode_never.json`
- `Arena/scenarios/world/map_tutorial_sneak_past.json`
- New `Arena/scenarios/perception/hear_world_diagnostic.json`
- New `Arena/scenarios/perception/sound_lifetime_grace.json`
- New `Arena/scenarios/perception/sound_lifetime_long.json`
- New `Arena/scenarios/perception/sound_lifetime_prune.json`
- New `Arena/scenarios/perception/sound_freshness_equal.json`
- New `Arena/scenarios/perception/sound_freshness_new.json`
- New `Arena/scenarios/perception/sound_listen_empty_mask.json`
- New `Arena/scenarios/perception/sound_listen_order.json`
- New `Arena/scenarios/perception/sound_player_reserved.json`
- New `Arena/scenarios/perception/sound_player_modes.json`
- New `Arena/scenarios/perception/sound_player_decay.json`
- New `Arena/scenarios/perception/sound_player_gates.json`
- New `Arena/scenarios/perception/footstep_events_no_ai.json`
- New `Arena/scenarios/world/map_tutorial_hearing_walk_radius.json`
- New `Arena/scenarios/world/map_tutorial_hearing_sneak_radius.json`

C∩A=C∩B=∅, no V4d/V5b coder-manifest overlap. The two new helper files are debug
code only; do not add a module/build dependency or use editor services.

## Numbered jobs

1. **Observation support first**, `ElysiumArenaScenario.cpp::TraceKinds` and
   reader kinds; `ElysiumArenaScenarioRunner.cpp::Start/RecordEvent/Detach`,
   retail **0x101bac90 /
   0x101ba890 / 0x1030f940 / 0x10310710**: whitelist README's seven new sound/
   gate trace kinds. Install lane2's bus observer on the real stage with a scoped
   lifetime, restoring any prior observer on every exit/error/destructor. At
   insert identify diagnostic listener and read lane1's actual LastListenTime
   accessor before the next pass. Resolve owner for trace naming but retain id
   even when owner is absent/removed. Never require an existing event serial to
   observe empty Listen. Correlate candidate loss to cleanup/eviction; timestamps
   include absolute World::NowSeconds and scenario-relative time. Callback/sink
   cannot force Listen, enqueue an action, change conditions, reseed or advance
   clocks. Build1 observation has no fixtures or corrections on diagnostic path.

   Add optional top-level `sound_watch:["<listener name>", ...]`, a strict
   array of nonempty names in the scenario reader/data. Donor/diagnostic names
   arena_listener; map records name thug_1 (it may not exist until maker Spawn).
   At every relevant insert/refresh, resolve each present watched NPC and emit
   a `sound_word` snapshot with its actual lastListen/full_investigate/alert
   and sound identity. This supplies ordered pre-hearing snapshots without a
   dynamic `at` probe or hardcoded listener name. Watch only observes; absent
   child before spawn emits no invented words. Expect snapshot presence once
   child exists. Include this schema/support in the observation hunk.

2. `ElysiumArenaScenario.h/.cpp::action enum/strict action reader`,
   `ElysiumArenaScenarioRunner.cpp::FireDueActions`, proposed helper
   `ElysiumArenaSoundFixtures.h/.cpp::RunCase`, retail **0x1030f7b0 /
   0x101ba890 / 0x1016b480**: add `do:"sound_fixture", case:"<finite name>"`
   for Green Room only. Reject unknown case, wrong fields, map host, absent
   required substrate API and fixture errors as structured `error`, never pass.
   Case names correspond one-for-one to README's 12 exact sound/footstep control
   records (all names beginning sound_, plus footstep_events_no_ai: **12 cases**).
   Each uses an isolated FElysiumEntityWorld with explicit substrate stamps,
   real GameSoundBus methods, real TickHearing/PerformSensing, real player
   UpdatePlayerSound/PostThinkAnimation or real HandleAnimEvent as appropriate.
   Recording services local to new helper provide locomotion/LOS/audio; do not
   edit shared TestServices.h or WorldServices.h. No Unreal import or asset bake.
   Copy returned observed words/events to the stage trace through the runner;
   include fixture internal stamp as text (host scenario time remains runner
   time). Never return an expected bool instead of observed transaction words.
   Multiple fresh subfixtures are allowed for before/equal/after boundaries.
   Keep fixed-case inputs reviewable, named and documented, no arbitrary C++/
   console evaluator. Teardown all callbacks/fixture state after each action.

3. `ElysiumArenaScenario.h/.cpp::probe enum/reader`,
   `ElysiumArenaScenarioRunner.cpp::ReadProbe` (relocate actual probe switch),
   **0x102b8980 / 0x1016b480 / 0x1030f7b0**: add read-only probes for
   `full_investigate`, `alert_level`, player raw sound volume/type/time/expiry/
   stable identity, and actual ear-to-player-sound distance. Read existing
   FullInvestigate/AlertLevel and lane2's const sound accessor, never shadow
   guessed state. Return numeric volume in Source units, distance in cm with
   documented conversion; named missing/wrong-entity probes fail. Numeric sound
   words/trace lines suffice for exact fixture checks; don't design a second
   opaque fixture verdict language. Lane2 owes a typed const accessor if private
   fields prevent observation; no substrate file ownership is gained here.

4. `Arena/scenarios/perception/hear_world_diagnostic.json`, actual
   **ambient_generic::PlaySound -> EmitAiSoundEvent**, **0x101ad470 /
   0x101bac90 / 0x1030f940**: copy donor staging verbatim, t2 PlaySound/no
   teleport/duration8. Expectations assert insert plus subsequent actual Listen
   or an observed gate/disposition, not a successful hear. An “either gate or
   listen” diagnostic can use regex on a common `sense_gate` disposition line
   emitted when a candidate is considered or the full-sense path is refused;
   agree this exact typed event with integrator rather than creating an
   unsupported expect-OR field. Keep actual sound_listen and candidate lines
   alongside it. Diagnostic must finish and retain all eight seconds on a miss;
   put a never window through duration so early expectations don't end collection.
   Never run a fixture, force a first listen, lengthen D or change cadence there.
   This is the wave's first record the integrator runs after compilation/default.

5. All **12 fixture JSON records** in manifest, proposed helper RunCase,
   **0x101ba6f0 / 0x101ba890 / 0x101baf80 / 0x1030f940 / 0x1030f7b0 /
   0x1016b480 / 0x10274e30 / 0x10178a10**: transcribe the exact staging/expect/
   never contracts in README. Each JSON has actual assertions over sound_word/
   admitted/rejected/output chronology; no known_red and no expect_fail.
   `sound_player_modes` covers all six category/priority arms plus silent and
   3-D/strict threshold controls; `sound_player_gates` separates early flag
   return from late silence/restamp and dead PostThink. Fixed dt1/60 decay uses
   integer235/230, not fractional235.833. `sound_listen_order` observes stable
   reserved position and query vs delay order, including unchanged freshness,
   wrong mask/self/range equality/outside/occlusion/slot467 controls. Emit
   per-case/per-step words so ordered JSON checks cannot pass from another
   control's event. Never mask a real symptom with a precomputed case result.

6. Existing `hear_world_investigate.json` / `interest_mode_never.json`,
   **TickHearing -> OnListened -> sound sweep -> SelectSoundAlertSchedule**,
   **0x1030f940 / 0x1026a5e0 / 0x102b3270 / 0x102b9060**: keep full original
   behavior expectations and negatives. Add OnHearWorld assertion to mode0;
   reorder output/cond+ expectations only if their actual trace order demands
   it, documenting emission order rather than widening deadlines. Supply removal
   of N4 known_red as a separately gated record correction for integrator after
   diagnosis and three boots. Do not turn real ambient scripts into fixtures.

7. `world/map_tutorial_sneak_past.json`, **maker::MakeNPC ->
   Player::PostThink/UpdatePlayerSound -> SelectSoundAlertSchedule /
   AdvanceAlertLevelGrade**, **0x1034b7b0 / 0x1016b480 / 0x102b9060 /
   0x102b8980**: replace first INVESTIGATE regex with exact
   `SCHED_TROIKA_ALERT_TURN_TO_SOUND (0x4c)`. Update about/notes and remove stale
   producer claim/Q-V3bf1 known_red, retaining existing place and later sight/
   enemy leg. Assert actual OnHearPlayer/HEAR_PLAYER and real type4 restamp before
   the sound-triggered break; the record's `after` labels still refer to the
   correct first alert program. Probe child full_investigate0 and pre-hearing
   alert0; use a named ordered snapshot if a dynamic `at` probe isn't supported,
   not a guessed absolute time. Never SEE_PLAYER/OnFoundPlayer until deliberate
   sight leg, then expect those existing real outputs. Ordered matches must
   follow actual event chronology, including same-think output-before-cond tap.
   Do not alter baked keys or set full_investigate1 to fit the old record.

8. New `world/map_tutorial_hearing_walk_radius.json` /
   `map_tutorial_hearing_sneak_radius.json`, actual
   **runner::FireDueActions -> input replay -> UpdatePlayerSound -> TickHearing**,
   **0x1016b480 / 0x1030f7b0**: stage README's same behind-pt1 path with real
   maker/place and standing vs crouched input. No shares_map, spawn rows, fake
   sound injection, sample override or sight suppression on map hosts. Standing
   expects volume240 and hearing; crouched volume180 and never heard at >180.
   Include walk-end/distance/crouch probes, type/time refresh and >=1.5-second
   negative reaction tail after the end of walking. Before crouched movement
   wait for prior decaying volume0; instrument/probe that, don't assume the old
   heartbeat's tail. Integrator measures collision/range and adjusts only a
   disproved seat, retaining the (180,240] witness band and clear path. A different
   destination to fabricate hearing is not a correction of this control.

9. `Arena/README.md` schema docs and
   `pipeline/tests/test_arena_suite.py::test_discovery_reads_every_record_in_relative_path_order /
   test_grouping_boots_the_arena_first_then_one_boot_per_map_record_unless_shared`, **0x1030f940 /
   0x101ba890 / 0x1016b480**: document exact action case inventory, permitted
   host, sound_watch, trace words/probes and error behavior. C++ parser checks
   for unsupported fields/cases/map fixtures/unknown probes belong under
   WITH_DEV_AUTOMATION_TESTS in the owned new fixture helper, using the real
   reader/runner; register `Elysium.Arm.ArenaSoundFixtures.Schema`. Python discovery tests cover inventory/host grouping and
   preserved top-level fields; they cannot prove the C++ action schema. Adapt
   only actual census assumptions to discovered records. Do not run pytest.
   Integrator runs the changed Python tests after compilation. No changes to pipeline implementation,
   baking or launcher are required for named records.

## Owed lines, chronology and close

Lanes1/2 emit/provide actual observations; integrator owns NPCThink/full-gather
gate taps and World cleanup/producer placement. Report their exact signature/
callsite requirements. All map timing is measured after the landed prerequisites;
do not claim the planner ran it. Stage-zero/seed and state-ban behavior remain V4c.
Records this wave writes or breaks belong to this wave. Do not add known_red for
a later owner without concrete retail evidence and an actual owner decision.

## Coder rules

Write only the 26 listed files in the worktree the coordinator names, **by
absolute path**. Read files and run read-only tools from
`E:\dev\elysium-unreal`. Never build, run editor/game/tests/arena, bake or commit;
never push. No git clean/reset/stash/checkout/worktree/delete. Address at every
changed runtime line and every numbered job; report a new divergence, do not adopt
it. Never hand-edit generated `*Slots.cpp`; hand bodies belong in matching
`*SlotBodies.cpp`, integrator owns `kernel_verdicts.tsv` and regeneration.
Shadowed locals/members/globals are compile errors **C4458 / C4459**. Report
**under 350 words**: files/functions/addresses, support/records ready, observation
vs correction hunks, and exact lines owed by files outside the lane (or none).
No file named `report*.md`.
