# 0002 npc-ai — the character AI, consolidated 2026-10-03: tools first, then the landed work proven live, then the road to close

This spec replaces 0002's earlier text (moved verbatim to `record-2026-09-30.md`, which keeps every
old story's detail and is cited below as `record § <n>`). It absorbs 0018's open rows 8–20, 0019's
hand-offs and 0003's stories 1–2; 0018 and 0019 are closed with a pointer here. The serial tracker
is § "The sequence" below; the old tracker is `tracker-record-2026-09-30.md`. The audit behind this
text is in `consolidation/` (findings A–F; the 2026-09-30 draft is `consolidation/draft-2026-09-30.md`).

## The goals, in order (the owner, 2026-10-03)

1. **Make every later step cheap.** Optimize the tools and the tests first. No development starts
   until step 1's gate is met.
2. **Prove what is landed works — live, in the Green Room.** The synthetic tests proved they do
   not find the defects that matter: 1,722 green unit tests while the first live arena run showed
   an NPC that never fired. Every landed behaviour family gets an arena scenario; every red is
   traced to its retail chain and fixed in this step; the step closes with `sp_tutorial_1` and
   `sm_hub_1` played live.
3. **Plan and run the optimized road to close the character AI**, so the other specs (0003–0017)
   can resume.

## Why: the failure modes this spec exists to end

- **Waiting instead of building.** Measured over 2026-09-19 → 09-30 (`findings-E-time.md`): a
  tool call was blocking for 40% of active wall time, 90.7 agent-hours of waiting. Builds 20.5 h
  (behind one lease shared by five checkouts: 1,659 refusals); sleep-polling loops 18.9 h; tests
  18.0 h (193 runs of the whole `Elysium.Substrate` at ~128 s, 72 full `pytest` runs at ~245 s);
  shell one-liners ~20 h, almost all of it the ~1 s per-call shell floor; `kernel_*` tools 9.1 h
  (14.6 machine-hours), 5.4 h of it one uncached chain, `kernel_shape.build`. The corpus MCP was
  0.2 h — its cost is reply size, not time.
- **Tests that pin the port, not the game.** Thousands of unit tests, run constantly; none ran a
  program that walks and then animates through `Think`, so the body arbiter that froze every
  animation-ending task passed them all. It was found by hand in the arena
  (`findings-A-tests.md`, `findings-F-tests.md`).
- **Port-only mechanisms gating retail bodies**, carried as "modernizations" although they change
  state or event order (`findings-B-port-only.md`, `findings-D-landed.md`).

## Standing rules, in priority order

1. **Follow retail.** A behaviour is ported from the listing, every arm, in retail's order, with
   retail's constants and what it writes. A defect claim needs the retail chain that reaches it.
2. **Diverge only where retail cannot be followed** (Source's node graph, its motor, its studio
   sequence table), keeping the retail contract: same inputs, outcomes and event order. A
   divergence is named at its line and in its story. Nothing that changes state or event order is
   a "modernization".
3. **The query budget: 10 s warns, 60 s stops** (the owner's global rule, `~/.claude/CLAUDE.md`).
   Here a query is any `kernel_*` / `research` tool, the `vtmb-corpus` MCP, an `elysium` MCP read
   (`entity_get`, `console_exec`, `log_tail`, dumps), and any read or script over `docs/**/*.md`,
   `*.tsv` or the schedule texts. A query past 10 s is logged to
   `$ELYSIUM_WORK_ROOT/logs/slow-queries.tsv`; at 60 s it is stopped, never retried as-is or
   widened, and the work waits while the path is optimized. A planned process that needs a query
   is timed first and optimized before it is scheduled. Every brief, Claude or external worker,
   carries this rule.
4. **The test budget.** The default run is census + arena scenarios + a small smoke set. During a
   story an agent runs only the scenario and the family filter it touches; the opt-in tiers and
   the full suite run once, at the story's close, by the integrator. No full suite "to check
   nothing broke" mid-story. A test pins a retail behaviour (an address, a schedule text or a
   scenario), never a port mechanism.
5. **No polling, no shell for text.** Wait on a blocking command or a background-task
   notification, never an `until …; sleep` loop. Search and read text with the built-in
   Grep / Read / Glob tools (0.02–0.12 s a call), not shell `rg` / `grep` / `cat` / `sed` (~1 s a
   call); a recurring question gets a `research` verb, not a fresh one-off script.
6. **Live is the acceptance.** A story ticks on a green arena scenario, not on unit tests.
7. **No shortcuts.** The kernel is a state machine tuned by 691 shipped programs; a gate, guard or
   stand-in that makes a symptom disappear is a defect until the retail chain behind it is read.
8. **One checkout, serialized; coders fan out, one integrator builds.** No worktrees and no
   parallel checkouts. A wave is ≤3 coder agents on disjoint files named in their briefs: a coder
   writes the code and its tests, and may run only the single Python test file of the module it
   changed; it never builds, launches the editor or runs a suite. Then one integrator agent builds
   once, runs the wave's scenarios and family tests once, fixes only integration breaks (compile,
   link, a wrong call across two lanes' files) and reports. Waves run one after another; nothing
   builds in parallel. Briefs and findings are files; reports are ≤300 words; ≤5 agents and ≤2
   builds per story.

## State of the tree, 2026-10-03

| Layer | Landed | Proven by | Standing |
|---|---|---|---|
| Kernel data: verdict overlay, datamap bindings, 691 schedule texts, tunables, class tree, deletions, mechanism seams, reach cut (0019/1–7) | all | generator `--check`s, census tests, `kernel --check` 7/7 | closed; RE-BACKLOG 40–44 open reads; 0019/3's corpus switch and 0019/4's 83 constants never observed live |
| The loop `NPCThink → RunAI → GatherConditions → MaintainSchedule → 437/438 → StartTask/RunTask` (0019/8) | 380 `rule` rows | 17 family suites, three IDLE map smokes | live; **its body contract is broken (§ defects 1–3); combat and death never observed on a retail map** |
| Senses, memory, conditions (old 0002/1–9, 10a–c, 15, 20) | all | unit suites | live; **the tutorial witness (sneak-past, stealth kill, trance) never played** |
| World objects: baked actors, collision, NavMesh, places, navigator, geometry, traversals, hints (0018/1–8) | all | content tests, `verify nav`, live smokes | live; the crosswalk wait and locked-door refusal never observed (no live NPC reaches them under the ambient executor) |
| Save / restore | the generated walk, `OnPostRestore` | `SaveRoundTrip` only | never observed live; restart, not resume |
| Port-only executors: ambient places, follower owner, the body arbiter | — | executor-shaped tests | the ambient executor is still the live path for every `use_interesting` NPC |
| Green Room arena (`uv run elysium gr --arena --headless`, `gr_scenario cover`, `npc_trace`, `gr_hints --validate`; `docs/harness/green-room-arena.md`) | 2026-09-30 | run 6: claim → release → claim observed | the instrument step 1 turns into a suite |

Of 46 landed stories that change runtime behaviour, 29 have no live check or an unobservable one
and 6 were checked in part; 22 of 42 "named modernizations" change state or event order
(`findings-D-landed.md`).

## The known reds (found 2026-09-30, each with its retail chain)

These are what step 2 expects its first scenario runs to show. The full text is
`consolidation/draft-2026-09-30.md` § "The defects landed work carries".

1. **The body arbiter refuses the kernel.** `PlaySequenceClip` and the stance transition play
   only under owner `None` / `Dialogue`; the kernel's path tasks claim `Schedule`, so every
   schedule that walks then animates commits a sequence at rate 0 and `IsActivityFinished` never
   fires (`ElysiumNpcAnim.cpp:402-431`). Retail has no arbiter: `ResetSequenceInfo 0x10090950`
   plays `m_nSequence`; a scene holds an NPC through `m_scriptState +0x5d70` / `SCHED_SCRIPTED_*`,
   a dialogue through `m_hDialogPartner` / `TASK_RUN_DIALOG`; `ClearSchedule 0x10280d30` ends a
   program.
2. **`GatherAttackConditions` never clears** (`ElysiumNpcConditions.cpp:1018`): the top clear
   `0x1026de02` and the tail `0x1026dfd0..0x1026e107` are missing; `TOO_CLOSE_TO_ATTACK` and
   `CAN_RANGE_ATTACK1` stack; `0xef` steps back forever.
3. **The animation chain is half stub, half out of order.** Slot 258 `DispatchAnimEvents
   0x10091880` is a counting stub; events fire from the world tick's poll
   (`ElysiumEntityWorld.cpp:1889`), not from `PostRun 0x1026c8c4`; `SetAttackExtentsForSequence
   0x10090c80` is a stub; a dead NPC stays standing; `task_face_enemy` ran 13.4 s; slot 363
   `FInViewCone(entity)` answers false, so `BEHIND_ENEMY 0x57` is always set.
4. **`0xef` is a sink**: its interrupt mask was not honoured for 99 s.
5. **Hidden and flipped NPCs never fight**: `StartHidden 1` + `ScriptUnhide` sits in
   `FALL_TO_GROUND`; a `SetRelationship`-flipped NPC never enters `Sighted()`; an open dialogue
   blinds perception; `map_load` of the same map keeps the previous session's entities.
6. **The executor release**: `ThinkSchedulePolicy` (`ElysiumNpc.cpp:985-988`) releases a place under
   any program; retail only in `OnScheduleChange 0x102a0940` without `PRESERVE_PATH`.
7. **Reach beyond retail's graph**: the human mesh walks every jump-only pair retail refuses.
8. **Restart, not resume** after a restore (slot 435 releases twelve saved rows).
9. **One clip per activity** where `SelectWeightedSequence` draws by weight.
10. **19 of retail's 33 NPC inputs unregistered** (`SetInvestigateModeCombat` 69×, `StayEntrenched`
    67×, `SetInvestigateMode` 66×, `FleeAndDie` 27× over the corpus).
11. **`UpdateTargetPos 0x10271b10`** a seam verdicted to a closed story.

## Step 1 — the instrument: tools and tests

**Gate (no development before it):** on a re-run of the time census, zero queries over 60 s and
every `kernel_*` command and corpus probe under 10 s; the default C++ run ≤25 s wall and the
default `pytest` ≤20 s wall, both with zero failures; a three-prefix C++ run in one boot under
25 s wall; a one-`.cpp` edit rebuilt in ≤60 s; no polling loop and no lease refusal in the
waves' journals; `uv run elysium arena` runs every scenario in one headless boot and writes its
report.

- [x] **T1. The `kernel_*` tools under budget.** *Measured (`findings-E-time.md` §3–4, profiled at
  HEAD):* 1,960 runs in 12 days, 14.6 machine-hours. **One chain is 5.4 h of it:**
  `kernel_shape.build`, rebuilt from scratch by `kernel_shape`, `gen_kernel_shape`,
  `gen_kernel_bindings`, `kernel_story8_shape`, `kernel` and the gate's residue (852 runs, ~17–18 s
  each); inside it `sdk_layout.preprocess` is 52% (the `_strip` char loop, 36.8M `startswith`) and
  the uncached `sdk_members` 36% (`kernel_shape.py:243`; findings-C measured the cache at 4.10 s →
  0.57 s, never applied). `kernel_gate --family` takes 146 s median, 92% of it a full `kernel_shape
  --unported` build for the residue. `kernel_ledger --check` 8.2 s: SQL load 46% (16,010 queries),
  the citation scan of 1,639 files 29%, render 17%. *Job:* every `kernel_*` command answers in
  under 10 s cold and under 1 s when nothing changed: the regex `_strip` and the cached
  `sdk_members` first; the built shape, the corpus stage and the SDK index pickled under
  `$ELYSIUM_WORK_ROOT` on an input hash, so every generator and the gate share one build; a
  per-file citation cache; `--check` short-circuits on an unchanged stamp hash; `--reach` joins the
  gate; the `SyntaxWarning` gone; the research CLI warns at 10 s and stops at 60 s, appending to
  the slow-query log. The reading-packet tools (`kernel_skeleton`, `kernel_packet`) are timed and
  brought under budget before any step-2 fix uses them. *Acceptance:* every `--check`
  byte-identical before and after; the timings re-measured and recorded. *Size:* M.
  *Model:* Opus/high. *Landed (wave 1, 2026-10-03):* `kernel_cache.py` (the corpus stage, SDK
  index and shape pickled on an input hash; a per-file citation cache; the `--check` stamp), the
  regex `_strip`, cached `sdk_members`, the gate's residue from the cached build, `--reach` in
  `kernel --check`. Integrator's re-measure (s, cold / warm / unchanged): `kernel_ledger` 5.5 /
  2.5 / 0.29, `kernel_lists` 4.6 / 1.0 / 0.22, `kernel_shape` 7.4 / 1.6 / 0.25,
  `gen_kernel_shape` 8.1 / 2.5 / 0.23, `gen_kernel_bindings` 7.8 / 2.0 / 0.23,
  `kernel_story8_shape` 8.0 / 2.6 / 0.24, `kernel_gate --family` 8.3 / 3.3 / 3.3 (before 146),
  **`kernel --check` 10.5 cold (0.5 s over; profiled: the ledger build 5.2 and the shape build
  4.5, each once — no duplication)** / 4.9 / 0.44. Every generated file byte-identical to `HEAD`;
  the two `reach/` cuts regenerated (they were stale against the committed verdicts).
- [x] **T2. The corpus MCP and the text tree under budget.** *Measured (`findings-E-time.md` §1,
  §5):* the corpus MCP is fast (7,639 calls, 0.2 h, one call over 10 s) but its replies are large
  (`vtmb_slot` median 27 KB); the text tree is where queries cost: 1,851 agent-written Python
  scripts over `docs` / TSV took 3.9 h, 53 of them over 30 s (2.9 h, gate-style checks hitting the
  600 s limit), and 4,566 shell searches over `docs` 1.8 h. *Job:* `corpus_mcp.py` enforces the
  60 s stop server-side (an SQLite progress handler) and journals every call (tool, arguments,
  seconds, reply size), flagging over 10 s; regex `vtmb_grep` (2–5 s) answered from the FTS index
  where it can; default caps:
  `vtmb_asm max_lines=400` with `from=`/`to=`, `vtmb_closure sections=`/`brief`, `vtmb_code` 20 KB
  with `from_line=`, `vtmb_vtable slots=`/`overridden_only`, `vtmb_fields range=`/`name=`,
  `vtmb_slot limit=50`, each truncation naming the parameter that gets more; the `elysium` MCP
  reads: `entity_get fields=`/`brief`/`limit=1` on a classname and `described` dropped,
  `console_exec max_lines=200`, `entity_list` / `log_tail limit=25` + `grep=`. **The address
  index:** one generated lookup from a retail address (or name) to its `docs/vtmb` section(s), its
  ledger rows and the port's `file:line`s, answered in under a second by a `research` verb and an
  MCP tool, so nobody greps 1.4 MB `functions.md` or reads a checklist whole; `CLAUDE.md`'s pointer
  at the `npc-ai` index file, which does not exist, replaced by it. **Standing queries as verbs:**
  the questions the 1,851 one-off scripts kept asking (read their shapes from the transcripts)
  become `research` verbs over the same index. *Acceptance:* a fixed probe set (every
  `vtmb_*` tool on `CAI_BaseNPC`, `StartTask`, `0x1028a380`) under 10 s each; no default reply over
  20 KB. *Size:* M. *Model:* Sonnet/high. *Python half landed (wave 1, 2026-10-03):* the 60 s
  deadline, the journal and the caps in `corpus_mcp.py`, `vtmb_where` and `research
  where|section|verdict|cited|rows` (`research/tooling/lookup/`); the probe set's 43 calls at
  ≤0.11 s and ≤19.7 KB (`vtmb_code 0x103692c0` 38 s → 0.002 s), `where` ≤0.09 s, the index's
  cold rebuild 1.3 s; the search index rebuilt (`corpus reindex --search-only`, 8.6 s). *C++ half
  landed (wave 2, 2026-10-03):* `elysium_entity_get` `fields` / `brief` / `limit` (1 on a classname)
  with `described` dropped, `elysium_console_exec` `max_lines=200` and 300-character lines,
  `elysium_entity_list` / `elysium_log_tail` `limit=25` + `grep`, `elysium_wire_report` `limit=25`,
  every cut naming the parameter. Measured live on the wire (the plugin's doubled
  `structuredContent` included): `brief` 3.1 KB, `fields` 1.4 KB, `log_tail` 9.4 KB, `entity_list`
  11.1 KB, `console_exec gr_hints` 1.8 KB, `wire_report` 0.8 KB. **Miss: `elysium_entity_get` on one
  NPC with no `fields`/`brief` is 82 KB** (its default stays the full detail for a targetname, as the
  wave-2 brief kept it), so "no default reply over 20 KB" is not met; not ticked. *Closed in wave 3
  (2026-10-03, T6's rider):* an NPC answers its `npc_brief` readout unless `full=true` (every field)
  or `fields` is set, and the reply's `more` says how to get the rest; any other entity answers as
  before. Measured live on the wire (`gr --arena --headless`, `gr_scenario cover`,
  `arena_gunman`): 3.4 KB by default (was 82 KB), `brief` 3.1 KB, `fields` 1.3 KB, `full=true`
  82.2 KB. Ticked.
- [x] **T3. The test scale-down (tiered).** *Measured (`findings-F-tests.md`):* C++ 1,778 tests,
  default run 122 s wall (19 s boot, 87.7 s of bodies, of which `MapActorTeardown` alone is
  50.6 s); `pytest` 4,600 tests, 384 s serial, of which 150 corpus-reading tests are 326 s; 1,241
  C++ tests pass "with warnings" and eight fixture messages are 70% of 28,102 warnings; two
  Python tests fail on `main`. *Job:* (1) delete the port-only (21), seam (16 whole tests and 130
  seam lines inside arm tests), stub-count (13) and duplicate (13) tests — the ones that pin the
  arbiter, the executors and the owner enum go in V3 with their mechanisms, never ahead;
  (2) **C++ tiers:** the default run is the 32 census tests, the scenario tests and a hand-picked
  smoke set (~170 cases); arm and unit tests move to an opt-in `Elysium.Arm.<family>` tier run at
  a story's close, content tests to `Elysium.Content`; `MapActorTeardown`'s 50 s found and cut
  (an `EndPlay` guard does not need a minute), `SkeletalStageOwners` to `Elysium.Slow`; the
  eight fixture warnings silenced at their source so "with warnings" means something again;
  (3) **`pytest`:** a `corpus` marker off by default (384 s → 58 s serial with no test deleted),
  `pytest-xdist`, the 12 import-time `ELYSIUM_WORK_ROOT` writes fixed first (an order hazard under
  one process), one session-scoped kernel fixture, the two failing tests fixed (the dangling
  `test_oracle_citations` citations; `ai-schedules`' extension missing from `ROOT_EXTENSIONS`);
  the 4,249 pipeline-unit tests stay in the default — moving them saves ~6 s on 8 workers.
  (Test compile cost is T6's.) *Budgets:* default C++ run ≤25 s wall (19 s of it is boot); default
  `pytest` ≤20 s wall; zero failures in both. *Size:* M. *Model:* Sonnet/medium. *`pytest` half
  landed (wave 1, 2026-10-03):* the `corpus` marker (off by default; `-m corpus` 154 passed in
  71 s), `pytest-xdist -n auto`, the import-time root writes moved to `conftest.py`, both failing
  tests fixed at their cause. Default run 4,526 passed, 0 failed, **19.7–25.9 s wall over eleven
  runs (not reliably under 20 s: 76 s of test bodies, the rest xdist's 16-worker start and
  per-report overhead)**. *C++ half landed (wave 2, 2026-10-03):* 1,778 → 1,774 tests, tiers by name:
  default (`Elysium.Session.` + `Elysium.Substrate.`) 176, `Elysium.Arm` 1,551, `Elysium.Content` 53,
  `Elysium.Slow` 2; 4 whole tests and 134 seam lines deleted, the 21 port-only tests kept (as Arm);
  six fixture warning families declared expected from `FElysiumRecordingServices`. Integrator's run:
  default 176 passed, 0 failed, 39 "with warnings", 2.1 s of bodies, **35.5 s wall** (first boot after
  a build; a second run 45.7 s with a 5 s lease wait — engine init alone 26.8 s); Arm 1,551 passed,
  0 failed (189 with warnings), 13.9 s bodies, 57.2 s wall; Content 53/0 (7 with warnings), 28.6 s
  wall; Slow 2/0, 29.5 s wall, `MapActorTeardown` 50.6 s → 10.1 s (`bStageWithoutCatalogue`);
  `pytest` 4,558 passed, 0 failed, 20.8 s. **Misses: the default C++ run's ≤25 s (the boot is the
  cost, not the 2 s of tests: T6 / gate 1) and `pytest`'s ≤20 s by 0.8 s**; not ticked. *Wave 3
  (T6's boot cut, 2026-10-03):* the default C++ run 176 of 176, 0 failed, **15.6–19.6 s wall** (16.6 s
  on the first boot after a build); `--all` 1,782 of 1,782 in one boot. Ticked on the default tier;
  `pytest` (4,568 passed, 0 failed) measured 19.7 s and 26.0 s, still not reliably under 20 s — a
  gate-1 item.
- [x] **T4. The runner, the lease and the waits.** *Measured:* an `elysium test` run is 19 s of
  editor boot around 0.1–0.7 s of tests; the shared export-root lease refused 1,659 builds and 836
  test runs in 12 days, and the retry wrappers cost 9.2 h; agents' sleep-polling loops on build and
  test logs cost 18.9 h (52 hit the 600 s tool limit) because no command blocks until done; the
  ~1 s floor of a shell call (`bash -lc true` 0.35 s against `bash -c true` 0.03 s: the login
  profile) over 37,408 calls. *Job:* the lease keyed per checkout, so an idle sibling checkout
  never blocks this one; a build exclusive against its checkout's loaded binaries; waiting by
  default (bounded, naming the holder) with `--no-wait`; `build` and `test` block until done and
  end with a one-screen summary, so nothing needs polling; `elysium test A B C` in one boot with
  the report slug capped (~40 chars + a hash); the summary per prefix with failures by name; the
  shell's login-profile cost measured and the slow step named for the owner (machine
  configuration, outside the repo). *Acceptance:* zero lease refusals and zero polling loops in a
  day's journal. *Size:* S. *Model:* Sonnet/medium. *Landed (wave 1, 2026-10-03):* the checkout
  lease (`Saved/Elysium/leases/`), a 30-minute wait naming the holder, `--no-wait` /
  `ELYSIUM_NO_WAIT=1` exit 8, `build`/`test` verdict lines, `test A B C` in one boot with a
  per-prefix summary and a capped slug, the research watchdog (10 s warn, 60 s stop,
  `slow-queries.tsv`, `RESEARCH_NOT_A_QUERY` on the tools that are runs). Live: three prefixes,
  71 tests, one boot, 24.2 s warm (46.5 s on a cold first boot); a concurrent `--no-wait` refused
  with 8, a concurrent plain run waited and ran. The day's journal is read at gate 1's census.
- [x] **T5. The Green Room as the live test suite.** *Today:* `gr_scenario cover` is one console
  verb over a hand-authored C++ row; the observer is a human reading `npc_trace_tail` over MCP.
  *Job:*
  - **Scenarios are data**, one JSON record per scenario under `Arena/scenarios/` (tracked text;
    `Content/ElysiumAuthored/` holds packages only), so adding or tuning one needs no build: the
    stage, the cast (classname + keyvalues, or `from_map` to stand retail rows verbatim from the
    map's baked `DA_<map>_Entities`), further entity rows, the player's seat, a timed script of
    actions (teleport or walk the player, fire an input, spawn, kill, any console verb), and the
    expectations. Schema: `Arena/README.md`.
  - **Expectations** are ordered matches over a stream of trace events, each with a deadline,
    plus `never` events and end-state probes. The events are a new seam on the entity world (a
    sink; one tap per kind, nothing emitted without a sink): schedule, task, task done, task fail,
    break, condition set / clear, NPC state, sequence handed to the body with its applied rate,
    sequence finished, anim event, move goal / arrival / failure, damage, death, corpse, hint
    claim / release, entity output / input (`stories/wave2/seam.md`). The two unnamed conditions
    (`0x29`, `0x57`) and `npc_brief`'s units are fixed with it.
  - **Two hosts, one runner.** The lab needs a real RHI, so the suite is a new headless run
    (`-ElysiumArena`: `-nullrhi`, fixed timestep), modelled on the cast harness: it stands the
    arena in the stage world and runs each record on a freshly rebuilt entity world. With
    `"stage": "map:<map>"` the same runner drives a retail map's own entities (doors, crosswalks
    and paths that the arena does not have). `elysium.gr_scenario <name>` runs the same record in
    the lab, rendered, for observation over MCP.
  - **One command:** `uv run elysium arena [names…]` boots once per stage, writes
    `$ELYSIUM_WORK_ROOT/reports/arena/<timestamp>/index.json` (per scenario: result, the first
    unmet expectation, the trace file, wall and game time) and exits non-zero on a failure. A
    record's `known_red` turns its failure into `expected-fail` and its pass into
    `unexpected-pass`, which is how step 2 tracks its reds.
  - **The harness's own tests:** a scenario that must fail, a bound that must trip, and a control
    (a path-free program whose finite sequence finishes headless, proving the host animates).
  *Acceptance:* `cover` (the 2026-09-30 run) as a record, red exactly at known red 1; the arena
  host's suite under 30 s wall. *Size:* L, two coders (`stories/wave2/brief-A-scenarios.md`,
  `brief-B-trace.md`). *Model:* Opus/high. *Landed (wave 2, 2026-10-03):* `uv run elysium arena`,
  one boot, 4 records: `must_fail` and `bound_trips` pass (they fail, `expect_fail`),
  `control_sequence` passes (`idle01` rate 1, `seqfinished` at 2.0 s: the host animates), `cover`
  `expected-fail` at expect[3] `seqfinished` (deadline 27.3 s): the walk to `cover_corner_nw` arrives
  (7.2 s), `task_snap_to_hint` completes (7.3 s), `SCHED_TROIKA_TAKE_COVER_HINT_VS_MELEE (0xa4)` runs
  `task_play_cover_outof`, the body is handed `smith_lean_left_into rate=0` and nothing finishes —
  known red 1. The lab (`gr_scenario cover`, live over MCP) reproduces it event for event (106 events,
  the same expectation, the same deadline). Wall: **28.6 s** warm, **37.1 s** on the first boot after
  a build (miss); the four records take 2.4 s, the stage world's boot build 14.7 s warm / 22.5 s cold
  (item catalogue, the player's chargen body, the native model contexts). The map host is not
  exercised: no map record exists yet.
- [ ] **T6. The incremental build.** *Measured:* builds are the largest single wait, 20.5
  agent-hours in 12 days: 667 real builds, median 46 s, p90 280 s, max 1,456 s. Known compile
  costs: `ElysiumTestServices.h` (2,504 lines, included by 102 of 120 test files, pulling
  skeletal-mesh, light and anim headers into kernel tests) and the generated
  `ElysiumNpcKernelOverrideCensus.cpp` (5,210 lines, 1,051 template checks). *Job:* measure what
  one typical edit recompiles (a kernel `.cpp`, a kernel header such as `ElysiumNpcBase.h`, a test
  file, a generated table) and why (include fan-out, unity grouping, PCH); then cut it: adaptive
  unity for edited files, the hot headers split so a body edit recompiles its own translation unit,
  `ElysiumTestServices.h` split by need, the census file out of the hot path. *Acceptance:* a
  one-`.cpp` edit rebuilds in ≤60 s and the p90 of a re-measured edit mix is ≤90 s (today 280 s).
  It is build work, so it runs as its own wave with one agent that holds the build.
  *Size:* M. *Model:* Opus/high.
  *Landed (wave 3, 2026-10-03; `stories/wave3/brief-T6.md`, one agent holding the build).* **The
  boot**, split from the engine's own log timestamps. A test boot's largest cost was the Zen DDC
  server: Unreal starts one per launch, owned by that launch, and its startup reloads a thousand
  persisted sessions — 0.8 s, or 13.5 s (the 35.5–45.7 s runs). Then the automation controller's
  5 s wait for remote workers before the first test, and the uncontrolled-changelist tracker (a
  Perforce feature: a 12 MB state file read on the critical path, 1.1–2.6 s, then a walk of 86,000
  packages). The arena's cost is its stage-world build, 14.8 s warm: catalogues 4.6 s, native bodies
  10.2 s (the player's chargen body and every wield model's cast body, each with every clip
  `PrepareMany` makes resident), residency 0.03 s; then an orderly shutdown, 1.4 s. Cut: the pipeline
  keeps the Zen server running between its launches (`UE-ZenLimitProcessLifetime=false`; `true` in
  `.elysium.local.env` restores Unreal's default), `Automation Now` ahead of `RunTest`, the tracker
  off for every unattended launch, and the arena host exits forced once its index and traces are on
  disk. The stage world's residency is the game's own rule (the map path makes the same wield
  catalogue resident) and stays; its phases are now in the `prepared native model contexts` line.

  | boot, s wall | before | after |
  |---|---|---|
  | default tier (176 tests), warm | 22.1–22.5 (35.5–45.7 when Zen started slow) | 15.6–16.0 |
  | default tier, first after a build | 35.5 | 16.6–19.6 |
  | arena (4 records), warm | 28.6–29.1 | 25.6–26.1 |
  | arena, first after a build | 37.1 | 26.1–27.1 |
  | three prefixes in one boot | 24.2 (46.5 cold) | 12.0 |

  **The build.** One edit simulated by touching the file (content unchanged), then `uv run elysium
  build`: s wall / translation units compiled / compile CPU s. The link is 0.4 s (`.lib`) + 2.0 s
  (`.dll`) in every row, so no linker setting is worth changing.

  | edit | before | after |
  |---|---|---|
  | nothing (null build) | 1.9 / 0 | 1.9 / 0 |
  | `Substrate/ElysiumNpcConditions.cpp` | 9.4 / 1 / 7 | 9.3 / 1 / 7 |
  | `Substrate/ElysiumNpcBase.h` | 193.6 / 44 / 1,090 | 156.8 / 43 / 898 |
  | `Substrate/ElysiumNpc.h` | 187.8 / 43 / 1,054 | 154.2 / 42 / 866 |
  | `Public/ElysiumEntityWorld.h` | 209.8 / 48 / 1,196 | 182.6 / 47 / 1,039 |
  | an arm test (`ElysiumNpcKernelSpawnTests.cpp`) | 13.6 / 1 / 21 | 11.9 / 1 / 15 |
  | `Tests/ElysiumTestServices.h` | 93.4 / 19 / 544 | 72.1 / 18 / 352 |
  | the census, alone | 11.6 / 1 / 14 | 8.1 / 1 / 4 |
  | median / p90 of the eight | 53.5 / 198.5 | 42.0 / 164.5 |
  | a real `.cpp` edit: first build / again / reverted | 10.9 / 8.1 / 10.8 | 9.6 / 9.5 / 9.5 |
  | a wave's commit (~300 files leave `git status`) | 226 / 50 | 1.9 / 0 (218 files hidden from it, content unchanged) |
  | switching the arm tier on / off | — | 104 / 21 / 626 on; 81 / 21 / 410 off |
  | after a `research kernel` that changed nothing | 202 / 50 | 1.6 / 0 |

  Where the time went, and what was done:
  - **Adaptive unity** took every file `git status` lists out of its blob and compiled it alone, and
    put it back when it left the list: a commit rebuilt 50 blobs (226 s), and a header edited
    mid-story compiled every working-set file as its own translation unit (wave 2's 9 m 24 s). Off
    (`bUseAdaptiveUnityBuild = false`, both targets): a `.cpp` edit recompiles its blob — the
    kernel's 5 s, the heaviest 35 s — and a commit recompiles nothing.
  - **The arm tier compiled on demand.** Every `Elysium.Arm.*` case sits behind
    `ELYSIUM_WITH_ARM_TESTS` from the generated, Git-ignored `Tests/ElysiumArmTier.h` (only test
    files include it, so switching it recompiles test files only): 127 arm-only files guard the whole
    file and depend on no project header with the tier off, 72 mixed files guard their 598 arm cases.
    A plain `build` compiles it out, `build --arm` in; `test arm`, `test --all` and any selection
    reaching an arm case switch it on and build first, and say so. `test_test_tiers.py` fails an arm
    case outside the guard and a default case inside it.
  - **The hot headers.** `ElysiumNpcBase.h` reaches 348 `.cpp` (114 tests), `ElysiumNpc.h` 336 (318
    include it directly), `ElysiumEntityWorld.h` 462 (456 directly): every source blob holds an
    includer. The 168 default-tier cases sit in 79 files across 18 of the 21 test blobs, so every
    test blob still compiles on a kernel-header edit, now without its arm cases: the test blobs'
    compile CPU fell 35–40% (520 → 314 s for `ElysiumNpcBase.h`); the source blobs' 570–630 s is
    untouched. `ElysiumTestServices.h` split by need: dropped — under unity a header is parsed once
    per blob, and every test blob holds a file that needs the whole services.
  - **The bound is the commit charge, not the cores.** UnrealBuildTool allows 10–11 compiles at once;
    3–6 ran: each `cl.exe` commits the UnrealEd shared PCH (2.37 GB) privately — 2.6–2.7 GB each
    against a 0.5–0.9 GB working set — and the machine's commit limit (32 GB of RAM and a fixed
    21 GB pagefile, ~34 GB committed by other programs at rest) is reached with 13–14 GB of RAM
    still free. A module PCH of the Engine set would be ~2.0 GB (the game target's measures 1.97 GB):
    15% less per compile, not worth a module-wide PCH change; dropped.
  - **The ledger regeneration.** `research kernel` rewrote 21 generated sources with the same bytes
    (`ElysiumInfraKeyfields.h`, the slot `.inl`s included inside the kernel's class bodies, the
    census), so the build after every story close's regeneration was a full one. The two generator
    writers (`gen_kernel_shape._emit`, `gen_kernel_bindings._emit`) skip unchanged text now.
  - **The census** compiles in 3.5–7 s within its blob: not in the way; dropped.
  - Riders: `elysium_entity_get`'s NPC default (T2, above); `ElysiumFixtureNoise.h` says how the two
    stub-counting tests declare their own expectation ahead of the fixture's.

  **Miss: the edit mix's p90 is 164.5 s (target 90 s)** — the three kernel/world header rows, 42–47
  blobs and 870–1,040 s of compile CPU run 3–6 at a time. The levers left: the machine's commit limit
  (proposed to the owner), a narrower kernel header (a refactor across hundreds of includers), and
  the default-tier cases gathered into fewer files so test blobs leave a kernel header's reach. Not
  ticked.

Waves: **wave 1 (Python)** — three coders on disjoint files: T1; T2's Python half; T4 with T3's
`pytest` half (markers, xdist, the import hazard, the two failing tests). Then the integrator:
default `pytest` in budget, every `--check` byte-identical, the timings re-measured. **Wave 2
(C++)** — three coders: T5's scenario record, runner and two hosts; T5's trace events and
launcher with T2's C++ half (the `elysium` MCP read caps); T3's C++ half. Then the integrator: one
build, the default run, the arm tier once, the arena suite. **Wave 3** — T6
alone, one agent holding the build. Gate 1 is the integrator's report after wave 3 against the
numbers above.

### Gate 1, measured 2026-10-03 (waves 1–3: `327beebf`, `ae3eba85`, `809404ba`) — accepted by the owner, 2026-10-03

**The ruling:** the gate is passed with its four misses accepted as measured (`kernel --check` cold
10.5 s, default `pytest` 19.7–26 s, the edit mix's p90 108 s, seven short sleep-waits). T6 stays
unticked: the kernel headers' include fan-out is an on-demand story. Step 2 is open.

| Clause | Target | Measured | Before |
|---|---|---|---|
| each `kernel_*` check | ≤10 s | ≤8.3 s cold, ≤0.44 s unchanged | 7–19 s |
| combined `kernel --check` | ≤10 s | **10.2–10.5 s cold** (4.9 s after an edit, 0.4 s unchanged) | 21 s |
| corpus MCP probes | ≤10 s, ≤20 KB | 0.11 s, 19.7 KB; 276 calls in the waves, max 1.7 s | replies to 148 KB |
| default C++ tier | ≤25 s wall | 15.6–16.0 s warm, 16.6–19.6 s first after a build; 0 failed | 122 s |
| three prefixes, one boot | ≤25 s | 12.0 s | not possible |
| default `pytest` | ≤20 s wall | **19.7–26.0 s**; 0 failed | 384 s, 2 failing |
| one `.cpp` edit rebuilt | ≤60 s | 9.3 s | — |
| edit-mix p90 | ≤90 s | **108.0 s** after the pagefile fix (kernel header edits 94–121 s; 164.5 s before it) | 198.5 s |
| arena suite | runs, ≤30 s | 25.6–27.1 s | — |
| queries over 60 s | zero | zero (two calls over 60 s were an approval wait and an edit script) | 185+ |
| polling loops | zero | **7 short sleep-waits, 99 s in all** (two agents waiting on a process or a log) | 696 loops, 18.9 h |
| lease refusals | zero | zero unintended (one deliberate `--no-wait` probe) | 2,495 |

Census of the three waves (3.89 h, 2,875 tool calls, 148 run journals): text was read through the
built-in tools 1,351 times against 88 shell calls; a tool was blocking for 33% of active wall time
(40% with approval waits), now builds and test runs rather than refusals and polling.

The misses, each explained: the combined `kernel --check` runs one ledger build and one shape
build with no duplication, and only from an empty cache; `pytest`'s remainder is xdist's worker
start on Windows (8, 12 and 16 workers measured alike); a kernel-header edit ran 3–6 compiles at
once instead of 10–11 because each commits ~2.6 GB against the editor's precompiled header and the
machine's commit limit was 53 GB (RAM 32 GB + a 21 GB pagefile).

**The machine fix (the owner, 2026-10-03):** a fixed 64 GB pagefile on `D:` (commit limit 99.9 GB).
BitLocker sets `PagefileOnOsVolume = 1` (`HKLM\SYSTEM\CurrentControlSet\Control\Session Manager\Memory
Management`), which makes Windows open only the system drive's pagefile at boot; with it at 0 the
`D:` file is used. Re-measured with 14–15 parallel compiles (touch one file; s wall):

| edit | before wave 3 | after wave 3 | after the pagefile |
|---|---|---|---|
| `ElysiumNpcConditions.cpp` | 9.4 | 9.3 | 9.2 |
| `ElysiumNpcBase.h` | 193.6 | 156.8 | 102.4 |
| `ElysiumNpc.h` | 187.8 | 154.2 | 93.8 |
| `ElysiumEntityWorld.h` | 209.8 | 182.6 | 121.2 |
| `ElysiumTestServices.h` | 93.4 | 72.1 | 37.9 |
| p90 of the eight-edit mix | 198.5 | 164.5 | 108.0 |

What is left of a kernel-header edit is its include fan-out (code, not the machine): T6 stays
unticked at 108 s against 90 s, and the fan-out cut is an on-demand story. The sleep-waits: later briefs state that a
background command is waited on by its completion notification, never by a sleep.

## Step 2 — the landed work, proven live

**Gate:** every scenario in the inventory green on one `uv run elysium arena` run; `sp_tutorial_1`
and `sm_hub_1` played live with 0 ensure / assert and every difference from retail filed; the arm
tests the green scenarios cover deleted.

- [x] **V1. The inventory.** Every landed runtime behaviour (the 46 stories of
  `findings-D-landed.tsv`, the old 0002 rows "done, record only": 25b, 25c, 26, 10e, 10j, 10k, 13b,
  21b, 27) mapped to the scenario that proves it, and the 22 state-changing "modernizations"
  re-classed at their lines as named divergences, each with the scenario or story that owns it.
  The scenario set (each one record; several per family where the arms differ):
  - **Idle and cadence:** an NPC idles, thinks on its four stamps, waits randomly, wanders by the
    capped point pick (task `0x1f`).
  - **Senses and memory:** walks into the cone → `SEE_ENEMY` and the alert ladder; behind the block
    → `ENEMY_OCCLUDED` → `LOST_ENEMY`; an `ambient_generic` sound → the sound sweep and
    investigation; the dim-light see-unknown; obliviousness; the comfort sweep.
  - **Movement:** chase around the block; a door opened and a locked door refused with its timer;
    a crosswalk red/green wait and a queue (needs a crosswalk pair in an arena variant).
  - **Cover:** claim → release → re-claim (run 6 as a record).
  - **Combat:** cover-and-fire; melee to the player; the player stepping to 96 cm and to 10 m (step
    back, re-face, fire).
  - **Damage and death:** flinch, death, the corpse landing.
  - **The player's verbs on an NPC:** the stealth kill (victim selection, commit, synchronized
    death); the trance (`FeedInterrupt → SCHED_TROIKA_MESMERIZED`).
  - **Scripts:** a `scripted_sequence` walk-to-mark-and-play; a dialogue hold; `SetRelationship`
    flipping an NPC hostile; `StartHidden` + `ScriptUnhide`.
  - **Patrol and places:** a patrol loop on path corners; a pedestrian visiting an interesting
    place.
  - **Lifecycle:** a maker spawning children; save mid-`RUN_PATH` and load.
  - **Retail rows verbatim:** `thug_1` and `pt1..pt3` from `sp_tutorial_1`, a hub pedestrian and a
    patrol cop from `sm_hub_1`, stood in the arena by `from_map`.
  *Size:* S (writing records needs no build). *Model:* Sonnet/high, with one Opus review against
  the listings.
- [ ] **V2. The first full run and the triage.** All scenarios run once; each red gets its retail
  chain read (a reading packet under the query budget, never a guess) and lands on a fix story
  below, or a new one. *Size:* S. *Model:* Opus/high.
  *V1 and V2 landed (2026-10-03; `stories/v1/`):* 96 records (perception 12, combat 15 and
  `cover`, world 67, one save/restore record parked), `inventory.md`, `divergences.md` (22 rows),
  `review.md`, `triage.md`. Latest verdict per record: 54 pass, 35 expected-fail, 4 fail, 1
  unexpected-pass, 2 error. Known reds shown: 1, 2, 3, 5 (the hidden half; **the `SetRelationship`
  flip works once the NPC sees the player**), 6 (**wider: a `use_interesting 1` NPC never runs a
  schedule; the ambient executor replaces selection**); 4, 7–11 have no record yet. Ten new reds
  N1–N10, each placed (`triage.md` § "New reds"); two planning bugs, corrected below as V11 and
  V12; fifteen harness gaps H1–H15, the first five as wave H.
- [x] **H. The harness wave** (the bug protocol's tool class; `stories/hwave/`). H1 a model-less
  row no longer kills the stage, and a failed stage does not error the boot's later records; H2
  arena solids block NPC sight; H3 `never` with a start and a count; H4 the player's state reset
  per record; H5 player probes, the stealth-kill trace event, crouch and light-pin actions.
  H6–H15 on demand, by the story that needs each. *Acceptance:* `triage.md` § "The fix order",
  row H. *Size:* S–M, three coders and the integrator.
  *Landed 2026-10-04, acceptance open:* H1–H5 in; the camera hang fixed at its root (a geometryless
  model admits, `RequestCharacterModel`); `debug_stealth_light` ported. Suite 103 records: 61 pass,
  35 expected-fail, 5 fail, 1 unexpected-pass, 1 error (designed, parked); after the by-name re-run
  62 / 36 / 3 / 1 / 0 of 102. Cameras and `sense_cone_enter` green, `verbs_stealth_kill` classified
  (red 3), N11 filed; default tier 176 / 0 failed. Unticked: `memory_occluded_kept` (Q-H2) and
  `cover_reclaim` (Q-H1, pass → fail with opaque walls) are open questions (`triage.md` § "Wave H").
  *Closed 2026-10-04:* both were record errors, verified against the listing — the port matches
  retail in each. Q-H1: the cover search's hint line-of-sight test `0x102968f0` refuses the hints
  the opaque block hides, and firing when no hint qualifies is retail's answer; re-staged, the
  re-claim is green. Q-H2: retail itself clears `ENEMY_OCCLUDED` for `OccludedDelayNormal` 0.50 s
  after the outputs (`0x1028e700`); re-stated, green. Ticked. One lead left for a reader
  (Q-H3, `triage.md`): the guard walks to the player's actual hiding spot, not the last-seen one.
- [ ] **V3. Fix: the arbiter retired; scenes, dialogue and places as retail runs them.** (Was C1;
  absorbs 0003/1–2's kernel half, old 0002/11's executor retirement, 16a's `Follower` owner, the
  four `STORY8-TWIN` survivors.) Retail: `m_scriptState +0x5d70`, `SelectSchedule 0x1028a380` case 4
  → `SCHED_SCRIPTED_*`, the scripted tasks (`0x60`, `0x62`, `99`, `0x65`, `0x66`, `100`),
  `CineCleanup 0x1027d170`, `ExitScriptedSequence 0x1027d0a0`; the dialogue hold as `0x6a
  RUN_DIALOG` and `m_hDialogPartner`; a place or patrol as a program released only by
  `OnScheduleChange 0x102a0940` → `0x102b53d0` without `PRESERVE_PATH`; the body always plays
  `m_nSequence`. Job, sites and the kept divergence: `consolidation/draft-2026-09-30.md` § C1
  (38 arbiter sites in 7 files, `RouteScheduleMaintenance`'s five pre-steps, the ambient executor,
  `bMoveIssued`, the save owner word). *Scenarios:* cover-and-fire, scripted-walk, place-visit,
  crosswalk. *Size:* L. *Model:* Opus/high, one Fable review of the seam commit.
  *Planned 2026-10-04 (`stories/v3/README.md`, accepted by the owner):* the arbiter is 53 sites in
  10 files today. Cut into five stories, each closing on its own records:
  - [x] **V3r** — two reading packets: the dialogue upkeep `0x102c1400`, the three
    `StartPlayerDialog` inputs and `PlayerUse`'s NPC arm. S. *Landed 2026-10-04* (`a156e746`,
    `stories/v3/packets.md`; three docs corrected).
  - [x] **V3a** — the seam (`m_scriptState +0x5d70` on the NPC, `m_hDialogPartner +0xfe8`, the
    `dialog_choose` harness action), then the body plays the kernel's sequence on every NPC and
    the program claims go. *Records:* the three patrols; `cover` through its cover-out clip. M.
    *Landed 2026-10-04* (seam `f767f9f8`; the integrator's report is the V3a commit's message):
    `PlaySequenceClip` plays on
    every body, the stance gate's owner term, the `Schedule` claim, `ReleaseProgramBody`'s stop and
    `bWalkingAnimation` gone. Default tier 176 / 0 failed; arm tier 1550 / 0 failed; suite 105
    records: 66 pass, 37 expected-fail, 1 fail (`rollcall_vzombie`, H11), 1 unexpected-pass
    (`hear_world_investigate`, N4) — the one verdict that moved is `cover`, expected-fail → pass
    (through the shot). The patrols play their walk at `rate=1` but stay red: not red 1 (doubt 1
    settled) — N13, a ground speed ~0.44× retail's, placed in V4. New red N12
    (`TASK_WALK_RUN_PATH`'s `nav+0x14`, R2). Ledger step not run: `kernel --check` is stale since
    the seam (the `+0x5d70` binding regenerates C++ and needs a build; V3b's integrator).
  - [ ] **V3b** — places and patrols as programs; the ambient executor deleted; the place released
    only by `0x102b53d0`'s retail callers. *Records:* the places, `map_hub_idle`,
    `hub_crosswalk_wait`, `rollcall_vhuman`, `rollcall_vhumancombatpatrol`. M.
    *Landed 2026-10-04, acceptance open* (`stories/v3/report-B.md`): the executor, the blacklist,
    the `Ambient` claim, `ThinkSchedulePolicy` / `ThinkAutonomous` and the external-executor return
    deleted; `0x102b53d0` ported from the listing (fires `OnInterestingPlaceLeft` once, only with
    argument 1); every `use_interesting` NPC selects `0xff` and runs `0xff` → `0x100` → `0x103`.
    Default 176 / 0, arm 1551 / 0; suite 70 pass / 33 expected-fail / 1 fail (H11) / 1
    unexpected-pass (N4). Green: `input_useinteresting`, `map_hub_idle`, both roll calls (record
    errors corrected). **Plan correction, stated to the owner:** N15 (the port's `AcceptedClasses`
    term, which retail never reads) blocks `places_thug_pt1` and the sneak-past's first half, so it is
    pulled from R2 into a V3b follow-up wave with H16 (the arena's anchors are typed `Stand`, a type
    the table lacks) and Q-V3b1 (`hub_crosswalk_wait`: no curb wait in 300 s). N10 was red 6, now red
    5 (V6); N14 (a restored visitor loses its place) filed to V6.
  - [ ] **V3c** — the scene hold: the cine writes the NPC's words, `SCHED_AISCRIPT` runs the
    scene, `StartSequence` writes `m_nSequence`. *Record:* `script_walk_to_mark`. M.
  - [ ] **V3d** — the dialogue hold as a program (`SCHED_TROIKA_RUN_DIALOG 0x6a`, task
    `TASK_RUN_DIALOG 0xb9`), then the arbiter, the owner enum and the save owner byte deleted
    whole. *Records:* the dialogue records, `script_aischedule_walk`. M–L.

  Rulings: a cine keeps refusing a save while it possesses an NPC, a named divergence V6 removes
  with the restart (K1); scene clips play through `m_nSequence`, the draft's montage-slot
  divergence dropped (K2); the admission barrier is V6's and `UpdateIdealState` with its tests
  V9's. `cover`'s shot stays red on V4 (anim events) and V5 (N2). (The text above says
  "`0x6a RUN_DIALOG`": `0x6a` is the schedule; the task is `0xb9`.)
- [ ] **V4. Fix: the animation chain under the kernel.** (Was C2.) `PostRun 0x1026c8c4`:
  `StudioFrameAdvance` (slot 250), `DispatchAnimEvents` (slot 258, `0x10091880`) over the clip's
  baked event table (the world-tick poll retired for NPCs), `HandleAnimEvent 0x10274e30`,
  `SetAttackExtentsForSequence 0x10090c80`, `IsActivityFinished` (slot 251); death through slot
  144 → `CreateCorpse 0x1032c0e0`; facing (`GetIdealYawSpeed`, `FacingIdeal 0x10278c80`); slot 363
  on the combat character; the weighted sequence pick from the `.mdl` `activityweight` the bake
  carries. Detail: draft § C2. *Scenarios:* melee-and-die, damage-and-death. *Size:* M.
  *Model:* Opus/high.
- [ ] **V5. Fix: the attack conditions and the combat interrupts.** (Was C3.) The two clears and
  the timers of `GatherAttackConditions`; why `0xef`'s mask does not break (packet first),
  `HasInterruptCondition 0x10269d30`. *Scenario:* cover-and-fire with the player at 96 cm and
  10 m. *Size:* S. *Model:* Fable/medium.
- [ ] **V6. Fix: session, clock and lifecycle.** (Was C4.) Per-level `curtime`; `map_load` tearing
  the entity world down first; `elysium.load <slot>`; resume at the task cursor `+0x5c50` with the
  animating words (the divergence closed); `ScriptUnhide`'s ground snap; `SetRelationship` into
  `Sighted()`; perception during a dialogue; `OnTakeDamage_Alive 0x10265ed0`'s last-damage record.
  Detail: draft § C4. *Scenarios:* hidden-then-unhidden, relationship-flip, save-mid-schedule.
  *Size:* M. *Model:* Opus/high.
- [ ] **V7. The inputs.** (Was D1.) All 19 unregistered NPC inputs bound to their landed bodies,
  plus the one-line items (old 25a, 10i, 12a, 10d's `+0x60dc` mirror, 16b's flat-table reads, 21c).
  *Test:* one table test that every retail input name resolves; the scenarios that fire them.
  *Size:* S. *Model:* Sonnet/medium.
- [ ] **V8. The maps, live.** `sp_tutorial_1` from a new game over MCP: `thug_1` idles at `pt1`,
  hears, walks, sees inside the light scalar, commits, fires `OnFoundPlayer`; the stealth kill; the
  trance. `sm_hub_1` at idle for 20 minutes: pedestrians visit and cross, cops patrol, makers
  cycle, a save round-trips. 0 ensure / assert. Every difference from retail filed as a red for
  V2's loop or as a step-3 story. *Size:* S. *Model:* Opus/high.
- [ ] **V9. The second cut.** The arm tests the green scenarios cover are deleted (T3's tier B);
  `ScheduleIntegration` and `NpcWitness` retired in favour of the scenarios. *Size:* S.
  *Model:* Sonnet/medium.

- [ ] **V10. A sound's life in `Listen`.** (New, from V2: N4.) `CanHearSound 0x1030f7b0` tests
  only "inserted after `m_LastListenTime`" and the radius; `CSoundEnt`'s think `0x101ba890` prunes
  a sound at its expiry plus a grace. The port drops it in `Listen` at its expiry
  (`ElysiumNpcSenses.cpp:713`), so hearing is a race. *Scenario:* `hear_world_investigate` green in
  three boots. *Size:* S.
- [ ] **V11. The attack coordinator's list.** (Pulled forward from R4: a planning bug, N3 — no
  melee scenario can go green without it.) The object behind `m_pAttackCoordinator +0x65e8`, its
  cap and the four bodies `0x1025db50` / `0x1025db70` / `0x1025dca0` / `0x1025de90`, all four stubs
  answering false today (`ElysiumNpcTroikaHelpers.cpp:134-160`), so slot 599 `0x102b5650` never
  admits melee; behind it `DIST:COMBATMOVE` resolving to the sentinel and
  `task_choose_best_melee_weapon` failing. Squads and followers stay in R4. *Scenarios:*
  `chase_melee`, `melee_swing`. *Size:* S–M. *Model:* Opus/high.
- [ ] **V12. The footstep sound producer.** (Pulled forward from R1: a planning bug — V8's
  tutorial needs heard footsteps.) The player's footstep `CSound` producer into the list V10
  fixes; the list's other producers and words stay in R1. *Scenario:*
  `map_tutorial_sneak_past`'s hearing half. *Size:* S. Reading packet first.

Order: V1 → V2 → H → V3 → V4 → V5 → V11 ‖ V6 → V7 → V10 → V12 → V2 again (full run) → V8 → V9.
The new reds ride their stories: N1, N2 in V5; N5, N6 in V7; N7, N8 in V3; N9, N10 in V6; N13 in
V4 (from V3a: the patrols' acceptance moves there); N12 in R2; N14 in V6; N15 in V3b's follow-up
wave (from V3b; N10 closed into red 5).

## Step 3 — the road to close the character AI

Re-planned at step 2's close from what V2 and V8 found; the draft below is the remaining work as
it stands. Each story runs the method (§ below) and ticks on its scenario.

- [ ] **R1. Investigation: the sound list, the alert ladder, the programs.** 0018/13's `CSound`
  list with expiry and producers; old 10d/10e/10f/10k (arms landed, the list missing), 10h's `0xaf`
  builder; `SpeakVSound`'s table (Q3). *Scenario:* footsteps-behind-the-block. *Size:* M.
- [ ] **R2. Places and patrols.** 0018/10–11, old 10g and 27, the hub's `copcar` / `func_brush`
  contents, RE-BACKLOG 45–47, the arrival tolerance `0x102f2ea0`. *Scenarios:* place-visit,
  patrol-loop. *Size:* S–M.
- [ ] **R3. Cover, kick and the goal selectors.** 0018/8's tick, 0018/9, old 12b, the weapon `+0x8c0`
  range, `CAI_Hint::ObjectCaps 0x102d2ee0`, `FindLosPos 0x102edaa0`, RE-BACKLOG 39 and 49.
  *Scenarios:* cover-and-fire with a shoot node and a flank; back-away. *Size:* L.
- [ ] **R4. Social: squads, followers, the coordinator, relationships, the logic entities.**
  0018/14, 15, 17, 18; old 16a (`DIST:ACCUM`), 16b, 16c, 17. *Scenario:* squad-of-two. *Size:* M.
- [ ] **R5. Flee, cower, the player on the head.** Old 21a, 21c, 28. *Scenario:* witness-and-flee.
  *Size:* S.
- [ ] **R6. Makers and templates.** 0018/16; hidden makers thinking; `MemberSync 0x10337ca0`.
  *Scenario:* maker-cycle. *Size:* S.
- [ ] **R7. The hub's species rows.** `CNPC_VCop`, `CNPC_VHuman`, `CNPC_VHunter`,
  `CNPC_VTaxiDriver`, `CCineAI`, `CCineAISchedule`: 65 unported `rule` rows. *Size:* S–M.
- [ ] **R8. The witnesses, closing.** The tutorial played end to end and the hub at idle, with every
  step-3 behaviour observed; 0018/20's cook guards. Closes 0002.

Order: R1 → R2 → R3 → R4 → R5 ‖ R6 → R7 → R8. Then 0003 onward resume (`docs/specs/README.md`).

On demand, not in the sequence: 0018/12 the flying mover; 0018/19 the debugger view; 0018/21-8 /
21-9 / 21-10; 0003/3–4 (Q4); 0004's open rows; the species outside the six maps' reach
(`Weapon_Switch 0x1032dde0`, slot-166 dispatch, the Werewolf fake-hull check, Andrei's spawner,
the controller's solidity); 0018/6's leftovers; `hw_warrens_4`'s `iris_clip` door; the ~280
`unported.tsv` rows outside both reach lists; `BeginNavigationJump`'s launch direction.

## The method per story

1. **Seam first.** The first commit declares the interface or the named stubs (retail address,
   admitting default, the retail field they stand for) and the scenario record, red. It builds
   green on its own.
2. **Coders fan out.** ≤3 coder agents per wave, each on disjoint files named in its brief, in the
   one checkout; briefs and findings are files under `docs/specs/0002-npc-ai/stories/`; reports
   ≤300 words. A coder does not build or run suites (rule 8).
3. **One integrator per wave.** It builds once, runs the story's scenario and the family filter
   once, fixes only integration breaks and reports. The family arm tier and the full suite run once,
   at the story's close, by the integrator.
4. **Read before write.** A body whose retail walk is not in `docs/vtmb/` gets a reading packet
   first (looked up through the address index, under the query budget), never a guess.
5. **The scenario is the acceptance.** A story closes when its scenario is green in `uv run elysium
   arena` and its arm tests are green.
6. **Ledger last.** `kernel --check`, the override census and `unported.tsv` regenerated once, at
   close.

## The bug protocol (the owner, 2026-10-03)

A bug found is fixed: none is recorded and left. Each one goes through these steps, in order.

1. **Classify.** *Tool or harness bug* (the runner, a tap, a tool answers wrong): fixed in the
   harness, the record untouched. *Record error* (retail or the schema misread): the record is
   fixed, with its retail source. *Game bug*: the port diverges from retail; the steps below.
2. **Reproduce as a record.** A game bug is an arena record first, stating what retail does, red,
   with `known_red`.
3. **Recover the retail chain** (address, schedule text, map), under the query budget; file it in
   the triage with the port's `file:line`.
4. **Place it.**
   - *It sits in a closed story or task* (landed work that is wrong): the fix is assessed — what
     retail does, what the port does, the files, the size — and proposed to the owner, then run
     as a fix story of the current step.
   - *It can only be fixed by a story of a later phase*: that is a bug in the spec's planning, not
     a red to park. Every phase must be testable on its own: the plan is corrected (the needed
     story or its seam pulled forward, or the phase's gate re-cut to what it can prove) and the
     correction is stated to the owner before the work goes on.
5. **Fix it the project's way.** The code follows retail's behaviour (the listing). The data —
   maps, map scripts, entity rows — is the installed corpus as it stands, Unofficial Patch
   included: the patch mostly corrects map behaviour, so a record or a fix reads the installed
   map and nobody fetches or loads an unpatched retail map to compare. Where retail cannot be
   followed, the fix is a divergence: named at its line and in its story, and it stands only once
   the owner accepts it.
6. **Done** when the record is green and its `known_red` removed.

## The sequence

[T1 + T2 + T4] → [T3 + T5 + T2's C++] → T6 → **gate 1** → V1 → V2 → H → V3 → V4 → V5 → [V11 + V6] → V7 → V10 → V12 → V2 → V8 → V9 →
**gate 2** → R1 → R2 → R3 → R4 → [R5 + R6] → R7 → R8 → 0002 closes.

Serial: one wave at a time in the one checkout. Brackets are stories whose coders share one wave
because their files are disjoint; each wave ends with its integrator. What is next = the first
unticked box in that order.

## Decisions

Settled by the owner, 2026-10-03:
- This text replaces 0002's spec; 0018 and 0019 close with a pointer; 0003/1–2 fold into V3.
- The three goals and their order; no development before gate 1.
- The query budget (10 s / 60 s), global and here.
- Tests: the tiered run (delete the port-only, seam, stub and duplicate tests; arm, content and
  slow tests opt-in; default = census + scenarios + smoke); the arm tests a scenario covers deleted
  in V9.
- Step 2 fixes its reds; it does not only record them.
- Live means the arena per behaviour family plus both maps at step 2's close (was Q6).
- Work is serialized in one checkout, no worktrees; each wave fans out coders and ends with one
  integrator that builds and tests (rule 8). Q2's editor cap is moot: one wave builds at a time.
- The integrator commits once per wave, after its build and tests are green.
- The bug protocol (§ above): a bug found is fixed; a bug in closed work gets an assessed, proposed
  fix; a bug that needs a later phase's story is a planning bug and the plan is corrected so each
  phase is testable; fixes follow retail's code over the installed (Unofficial Patch) maps, never
  an unpatched retail map, and anything else is a divergence the owner accepts.

Carried from the 2026-09-30 draft, unchanged: the arbiter goes whole (V3); port-only tests are
deleted, not converted; resume, not restart (V6); the five port-only think steps go with the
arbiter; the weighted sequence pick is ported (V4); model tiers as stated per story.

Open, decided at step 3's planning:
- Q3. The VSound table: load retail's `SOUND:` table as corpus data so `PLAY_SOUND` speaks, or
  leave vocalisation silent until an audio story owns it?
- Q4. 0003/3–4 (the cine entity's own lifecycle, the bodiless targets): fold here or leave 0003
  open beside this spec?
- Q5. Reach beyond retail's graph: keep the mesh's extra reach as a named divergence per map, or
  cut the mesh over each jump-only span at bake time?

## Findings

`consolidation/findings-A-tests.md` (the NPC test corpus), `-B-port-only.md` (port-only mechanisms
and the interface layer), `-C-tooling.md` (tooling and time), `-D-landed.md` (the landed stories),
`-E-time.md` (where the time went, 14 days of sessions), `-F-tests.md` (the whole C++ and Python test
corpus and the scale-down).
