# Brief R2 — hint census and live-check recipe for 0018/8

Role: data reader. You write only `docs/specs/0018-world-ai-infrastructure/story8/census.md`.
Nothing under `Source/`. Report back ≤300 words, no dumps.

Inputs:
- Deployed corpus: `Content/ElysiumCorpus/` (maps' entity data may be under `_import` or the
  exported entity lists; if the entity rows are not in the repo, use the work tree named by
  `ELYSIUM_WORK_ROOT` in `.elysium.local.env`, e.g. `exports_v2/<map>/…`). Find where the per-map
  entity rows (classname, targetname, HintType, Group, group_id, StartHintDisabled, origin) live;
  `pipeline/src/elysium_pipeline/importers/map_ai_infra.py` and `map_places.py` show how the
  pipeline reads them — reuse their loaders from a short `uv run python -c` snippet rather than
  re-parsing.
- The reach cut: `docs/vtmb/npc-kernel/reach/sp_tutorial_1.tsv` and `sm_hub_1.tsv` (+ `.md`).
- The hint call sites: `docs/vtmb/npc-ai/shape.md` lines 860-875 ("Call sites"): tasks `0x40` /
  `0x41` (base find / find-and-claim), Troika `StartTask` cower arm `(0x2774, 2, …)`,
  `0x10365780` (type, flags, 5000), `0x102b6b50` shoot-at (mask 8), `0x102b7110` tactical (mask 8).
- The port's arms: `Source/ElysiumUE/Private/Substrate/ElysiumNpcBaseStartTask.cpp:1210-1260`
  (TASK_FIND_HINTNODE / LOCK), `ElysiumNpcStartTask.cpp:1190-1215` (the cower arm),
  `ElysiumNpcHints.cpp:143` (`FindHintNode`), `ElysiumNpcTroikaHelpers.cpp:790-820`,
  `ElysiumNpcTroikaHelpers2.cpp:150-230`.

## Deliver in census.md

1. **Hint census** per witness map (`sp_tutorial_1`, `sm_hub_1`): count of hint-carrying rows
   (`info_node*` with HintType ≠ 0, `info_hint`, and any other class the pipeline treats as family
   "hint"); a table `HintType → count`, with the retail class-mask each type folds to (1 for
   100/101/0x27d8=10200; 4 for 0x283c=10300; 8 for 0x283d=10301; 0x10 for 0x28a0=10400; else 0);
   `group_id` distribution; how many `StartHintDisabled`; how many carry `Group` (patrol) or
   `target_name`. List the first five hints in retail LIST order (reverse BSP order of unparented
   rows) with name, type, origin.
2. **Which reached programs call a search** on each map: from the reach TSV, the schedule texts
   whose task list contains `TASK_FIND_HINTNODE` (0x40), `TASK_FIND_LOCK_HINTNODE` (0x41),
   `TASK_LOCK_HINTNODE`, the cower task, `FIND_COVER`-family tasks that route through
   `0x102b7110`, and the shoot-at search (which schedule / condition triggers `0x102b6b50`); for
   each, the NPC classes on that map that can select it and the condition that selects it.
3. **Live-check recipe**, concrete: on `sp_tutorial_1`, the cheapest way to make a reached NPC run
   a hint search that should SUCCEED given the census (name the NPC targetname, the hint it should
   pick by retail's rules — nearest admitted from the cursor walk, type/mask, radius — and the
   trigger: e.g. the player shooting it, or an `elysium_entity_fire` input, or a
   `elysium_script_eval` Python call). What to observe: the NPC's `m_pHintNode` and the hint's
   `m_hHintOwner` / `m_flNextUseTime` (via `elysium_entity_get`), and the release after. Also a
   recipe for `DisableHint` visibility (which hint, which Python call).
   If the tutorial offers nothing reachable, say so and give the hub's.
4. **Numbers for the landing paragraph**: totals over the 108 maps if cheap (a one-liner over the
   pipeline loader): hints per map min / max, types seen, disabled-at-start count.

≤150 lines. Tables over prose.
