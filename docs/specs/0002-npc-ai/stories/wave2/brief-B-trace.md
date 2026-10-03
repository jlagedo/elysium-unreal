# Brief B — T5: the trace-event seam, its taps, the `arena` launcher; T2's C++ half

Read `README.md` and `seam.md` here first. `seam.md` is fixed: add its declarations verbatim (lane A
compiles against them unseen).

## What exists

- Retail's one-NPC trace: `Substrate/ElysiumNpcBaseTrace.cpp` (`NpcTraceMessage`,
  `TraceConditionDelta`, `DebugScheduleInstalled`, `DebugScheduleBreak`), gated on the entity's
  overlay bits (`OverlayTaskTextBit`, `OverlayEntTraceBit`) and appended to the world's 512-entry
  ring only for the `ai_debug_npc` (`Public/ElysiumEntityWorld.h:783-825`). The verbs
  (`elysium.npc_trace`, `npc_trace_tail`, `npc_brief`) are in
  `Debug/ElysiumEntityDebugSubsystem.cpp`.
- The game's MCP tools: `Debug/ElysiumMcpTools.cpp` (sizes measured in
  `consolidation/findings-C-tooling.md` § 4).
- The `cast` harness's launcher in `pipeline/src/elysium_pipeline/unreal.py` (kind `cast`) and the
  `test` command's lease, summary and exit codes in `cli.py` (wave 1).

## Job

1. **The seam** (`seam.md`): `FElysiumAiTraceEvent`, `FElysiumAiTraceSink`, `SetAiTraceSink`,
   `HasAiTraceSink`, `EmitAiTrace` on `FElysiumEntityWorld`; cleared on world teardown.
2. **The taps**, one per kind of `seam.md`, each at the single place the event happens and each
   behind `HasAiTraceSink()` (the existing overlay-bit prints keep working as they do: where a
   print site already formats the text, gate it on `bits || HasAiTraceSink()` and emit from there).
   A tap is additive: it reads, formats and emits; it never reorders, guards or writes kernel
   state. Find each site by its retail address through `research where` / the port's citations
   (`SetSchedule 0x10280e50`, `IsScheduleValid 0x10280ff0`, `TaskFail`, the gather's delta,
   `ResetSequenceInfo 0x10090950`'s port site `PlaySequenceClip`, `m_bSequenceFinished`'s writer,
   the anim-event dispatch as the port has it today (`AdvanceAnimEvents`), the navigator's goal
   issue / arrival / failure, `OnTakeDamage_Alive 0x10265ed0`, slot 144, `CreateCorpse
   0x1032c0e0`, the hint claim `0x102d1350` and release `0x102d1420`, the entity I/O dispatch for
   `output` / `input`). A kind whose producer is a stub today gets no tap; list it.
   `sequence` matters most: emit it where the kernel's commit reaches the body **whether or not
   the body plays it**, with the rate actually applied, so the arbiter's refusal (known red 1)
   reads as `rate=0` in the trace.
3. **The names**: conditions `0x29` (`HINT_INVALID`) and `0x57` (`BEHIND_ENEMY`) print named
   (`ElysiumNpcCondName`'s table); confirm both names against the retail condition table before
   adding them. `npc_brief`'s player line states one unit.
4. **`elysium.npc_trace_tail`** stays as it is. Nothing else in the verbs changes.
5. **T2's C++ half — the `elysium` MCP reads** (`Debug/ElysiumMcpTools.cpp`): `elysium_entity_get`
   gains `fields` (a list of field names), `brief` (the `npc_brief` lines instead of every field)
   and `limit` (default 1 when the target is a classname, default unchanged for a targetname), and
   drops the duplicated `described`; `elysium_console_exec` gains `max_lines` (default 200) and
   cuts lines at 300 characters; `elysium_entity_list` and `elysium_log_tail` gain `limit` (default
   25) and `grep`; `elysium_wire_report` `limit` default 25. Every cut says how to get the rest. Do
   not edit the engine's MCP plugin (the doubled `structuredContent` stays).
6. **The launcher** — a new `pipeline/src/elysium_pipeline/arena_suite.py` and one `arena` command
   appended to `cli.py`: `uv run elysium arena [names…] [--hz 60] [--json]`, the launch contract of
   `seam.md`. It takes the checkout lease as `test` does, groups records by `stage` (arena first,
   then one boot per map; a map record without `"shares_map": true` gets its own boot), builds the
   command line as the `cast` kind does, merges the reports under
   `$ELYSIUM_WORK_ROOT/reports/arena/<timestamp>/index.json`, prints one line per scenario with the
   first unmet expectation of each failure, and exits 7 on a verdict failure. A name that matches
   no record is an error before anything boots. Tests in `pipeline/tests/test_arena_suite.py`
   (record discovery, grouping, the command line, the merge and the verdict from canned
   `index.json` files; no editor).

## Files you own

`Source/ElysiumUE/Public/ElysiumEntityWorld.h`, `Private/Substrate/ElysiumEntityWorld.cpp` (the
teardown clear), `Private/Substrate/ElysiumNpcBaseTrace.cpp`, the trace declarations in
`Substrate/ElysiumNpcBase.h`, the one-line taps in the Substrate / Map files that own each site
(list every file and line in your report), the two names in the condition-name table,
`Private/Debug/ElysiumEntityDebugSubsystem.cpp`, `Private/Debug/ElysiumMcp*`,
`pipeline/src/elysium_pipeline/arena_suite.py`, the `arena` command in `cli.py`,
`pipeline/tests/test_arena_suite.py`. Not `Debug/ElysiumArena*`, `ElysiumGreenRoom*`,
`ElysiumMapSubsystem.*`, `Arena/**` (lane A); not `Tests/**`, the `test` command or `unreal.py`
(lane C).
