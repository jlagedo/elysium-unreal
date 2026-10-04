# Brief — V3c's integrator

Runs after C1, C2 and C3 report. Read `README.md` here, the three briefs,
`stories/v1/triage.md` § "New reds" (N7, N11), `Arena/README.md`.

1. **Apply the cross-lane lines**, nothing more (`SetScriptState` at the cine's sites, the deleted
   `ScriptStateOf` / `ClaimScriptBody` / `EndScriptMove` callers C1 and C2 reported).
2. **Build once** (`uv run elysium build`). Fix only integration breaks. A second build is allowed; a
   third means stop and report.
3. **Run, by name**: `uv run elysium arena script_walk_to_mark control_sequence cover
   patrol_monk_loop places_pedestrian_visit map_tutorial_idle`. Family filters once: `uv run elysium
   test Elysium.Arm.NpcKernelDirector. Elysium.Substrate.NpcKernelDirector. Elysium.Arm.NpcKernelScript19.
   Elysium.Substrate.ScriptedSequence Elysium.Arm.NpcKernelTranslate Elysium.Arm.NpcKernelAnim
   Elysium.Substrate.Dialogue.BodyScene Elysium.Substrate.MontageSlotRun` plus the choreographed-scene
   family (find its prefix in `ElysiumChoreo*Tests.cpp`) — C3's audit says which retail callers
   `LookupSequence` turned on; a scene test that moved is a finding (README Q6), not a fix.
4. **Acceptance**: `script_walk_to_mark` green, `known_red` removed — `-> Script`,
   `SCHED_TROIKA_SCRIPTED_WALK`, `task_walk_to_target`, `arrived`, `task_wait_for_script`,
   `OnBeginSequence` on the mark (never before 2.5 s), `pre_fight_bow` played and finished,
   `OnEndSequence`, `Script -> …`, a new selection. N7 closed in `stories/v1/triage.md`.
   **Stage N11's record** (triage N11): `verbs_stealth_kill_scripted` — the mark held by a
   `scripted_sequence` in `NPC_STATE_SCRIPT`, crouched player, knife; retail's shipping arm admits
   it (`IsValidStealthKillTarget 0x102c2300` term 4 with `debug_allow_non_idle_auto_sk` "1",
   `stealth.md:405`); `known_red: "N11: … (V7)"`. It must run `expected-fail`.
5. **Story close**: the arm tier once, the whole arena once, the default tier once; the ledger step
   (`kernel --check`, the override census, `unported.tsv`; the generated bindings now carry
   `m_scriptState +0x5d70` on the NPC). Divergence 18 (`stories/v1/divergences.md`) marked closed by
   V3c; K1 noted there as a new row pending the owner. Verdicts that moved go in
   `stories/v3/report-c.md`.
6. **Commit once**: `fix(npc): V3c -- the scene hold as the NPC's scripted schedules; the beat
   stand-in retired (0003/1-2 kernel half)`. Do not push.

Rules: wait on a build or a run by its completion notification, never a sleep or a polling loop.
The query budget (10 s warns, 60 s stops; never read a file over ~200 KB whole). Text through Grep /
Read / Glob. Report ≤300 words: the build's wall time, each record's verdict before and after, the
family and tier totals, the scene-family result, what is left red and where it is placed.
