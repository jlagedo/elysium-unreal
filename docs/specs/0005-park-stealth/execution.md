# 0005 execution plan

Coordinator: Astra. Baseline: `222c8555` on `main`, clean working tree at intake on
2026-09-08. The scope and acceptance contract remain [spec.md](spec.md).

## Agent responsibilities

- Astra owns decomposition, file ownership, conflict resolution, integration, and acceptance;
  it does not implement runtime or test code.
- Scout: Terra low, read-only locations, callers, test and command discovery.
- Explorer: Terra high, read-only code and retail-contract mapping.
- Worker: Terra high, implementation within an explicitly assigned file boundary.
- Tester: Terra high, verification under the exclusive execution lease.
- Reviewer: a separate Astra high agent, independent of the Terra implementation author.

## Serialization rules

Only the coordinator can grant the single build/test execution lease. It includes builds,
automation, doctor, editor launches, Play acceptance, and content bakes. A worker may author
tests but cannot execute them without that lease. Read-only searches and retail recovery can
overlap implementation. Source edits are frozen before verification begins; a build or test
never runs against a moving source tree.

Use only the public commands `uv run elysium build` and `uv run elysium test <filter>`.
The CLI guards the project with `assert_project_idle` and an OS-exclusive `WorkspaceLease`
under the configured export root; UBT also uses `-WaitMutex`. Those locks supplement the
coordinator's lease, rather than authorizing concurrent commands. Never bypass an occupied
lock or terminate another task's editor/build.

One task is exactly one checkbox from the spec. Assign a fresh-context Terra high worker to
the complete checkbox, including runtime wiring, necessary shared-module repairs, persistence,
tests, and documentation. Internal functions and producers are implementation steps within
that assignment. Do not split them into separately coordinated tasks or bundle checkboxes.
Keep one implementation owner for the tightly coupled NPC/schedule lane. Set a broad explicit
boundary upfront; routine repairs do not require file-by-file handoffs. Parallel implementation
must have genuinely independent scope and disjoint ownership; unused agent slots stay unused.

Scouts and explorers answer specific unanswered questions; reuse their established findings.
Each checkbox gets a fresh-context worker, a fresh-context designated tester when verification
begins, and a fresh-context independent Astra high reviewer after implementation and verification.
Assignments inherit no conversation history and carry the task, documents, evidence, dependency
state, file boundary, and acceptance criteria explicitly. Reuse an agent only for continuation
or corrections to its original task and role; never repurpose it for another checkbox or role.

Recover consequential retail contracts before implementation, then compile a coherent patch
promptly. Request one consolidated independent review; batch findings back to that same worker,
retest affected behavior, and ask the same task-specific reviewer to re-review those areas.
Early focused review is reserved for consequential contract ambiguities. The coordinator does
not implement corrections.

Verification starts with focused affected behavior. Broader regression checks follow when the
changed dependencies justify them; do not repeat a full suite for isolated fixture corrections
whose runtime dependencies already passed. Report the states separately: in progress;
implementation-ready and frozen; compiled; tested; independently reviewed; accepted.
Required branches and available live producers must be wired before claiming
implementation-ready. Resolve current verification failures before the next dependent slice.

## Dependency and acceptance queue

| Order | Spec checkbox | Integration boundary | Acceptance evidence |
|---|---|---|---|
| 1 | 5 | Enemy record store, sight writes, memory-only choice, initial schedule interest | Unseen tutorial player is not a candidate; sight admission; hearing separation; retention/elusion; save/rebase |
| 2 | 6 | Sight cadence, cone, attention, concealment, hearing and ambient producers | Recovered boundaries and delayed hearing; authored `sound_event=0` stays audio-only |
| 3 | 13 | Failure transaction and navigation state seam | Exact masks/reasons, failure routing, stuck-on-top |
| 4 | 14 | Activity task completion | Sequence match or one-second deadline; trance regression; accepted |
| 5 | 15 | Four think clocks and reduced schedule maintenance | Due equality, interval branches, reduced bound, condition retention, resets |
| 6 | 10 | Condition sweeps and investigate programs | Task/interrupt order, sound route, once-per-life alert ladder |
| 7 | 11 | Interesting-place programs | Selector arms, group masks, tutorial pool, wait |
| 8 | 12 | Reaction keys and consumers | Authored ladders, hints, cover/kick, entrenched state |
| 9 | 8 | Discipline flag payload | Apply/expiry and teardown |
| 10 | 16 | Followers and possession/frenzy | Bands, composed relations, program/effect order |
| 11 | 17 | Shared squad memory | Ownership, disconnection/reconnection, lifecycle, retail defects |
| 12 | 1 | Light service and target surface | Falloff/styles/shadows, sampling, eligibility, sentinel |
| 13 | 2 | Light gauge producer | Row publication and view mapping |
| 14 | 3 | Stealth-kill transaction | Admission, paired action, input commitment, death output |
| 15 | 4 | Committed observer snapshot | Post-commit publication and read-only presentation |
| 16 | 21 | Flee/cower/disoriented/lost programs | Programs and ideal-activity feed acceptance |
| 17 | 19 | AIN jump-link bake and motor nav type | Baked links, actual traversal, stuck-on-top path |
| 18 | Visual debugger | Read-only non-Shipping observability | Build configurations and debugger display |
| 19 | Retail-defect note | TaskFail oracle record | Recovered leak documented |

The initial R5/R14 work and acceptance evidence are preserved. Subsequent checkbox assignments
use the fresh task-specific roles and single active coupled-lane owner above. Later families cannot be marked
accepted merely because their schedule tables compile.

The whole-spec gate follows the checkbox work: focused verification, justified broader
regressions, independent review, and the integrated tutorial Play/rendered witness. It does
not turn multiple checkboxes into one implementation assignment.

## Evidence and status

- Intake: full spec and named oracle sections reviewed across coordinator and explorers;
  current code checked instead of assuming the spec's historical port-status descriptions.
- The V2 tutorial entity, lighting, and navigation GLBs exist. The entity seam confirms
  `thug_maker`'s `OnFoundPlayer -> logic_failed_thug.Trigger` at 0.5 seconds, and `pt1` as the
  enabled group-2 place with a 30–60 second wait. `pt2` and `pt3` start disabled.
- Retail memory anchors verified through the corpus: `UpdateEnemyMemory` `0x102709c0`,
  `RefreshMemories` `0x102df320`; `BestEnemy` `0x102743c0` is the selection contract.
- First worker assignments: requirement 5 and the disjoint requirement 14 task body/tests.
- Requirement 5's first freeze was returned for required source-review corrections. Its regression set includes
  `NpcEnemy.MemoryAdmission`, record save/rebase coverage, updated damage-path expectations,
  and save schema 30. Requirement 14 has passed independent source review and released the
  shared NPC files back to the memory worker for damage-input integration. Those corrections
  are now frozen, and the independent Astra reviewer approves the R5/R14 source cohort for
  serial build/test. This is not whole-spec acceptance.
- Requirement 5 also replaces the old damage-derived relationship workaround. Raw recovery
  identified `0x102beda0 -> 0x1028e8b0 -> 0x1028e940` as a five-second extension of
  `m_flStealthVisionOverrideTime`, not a relationship write. Requirement 6 must wire that
  sight-range override; memory records and transient damage notices remain separate.
- Independent raw review confirmed `ChooseEnemy` `0x10279dd0` explicitly treats a null
  schedule as interested. Preserve that arm; do not manufacture a spawn schedule.
- Required R5 review corrections: complete known/current/anonymous damage-memory branches;
  reject old save layouts under the disposable-save policy; use truncated squared Source-unit
  distance; refresh only last position during the authored `FreeKnowledgeDuration` window
  (default and current V2 value 0.25 seconds). Retain record anchor, velocity, and observation time.
- Requirement 14's full timing was recovered during review: `StartTask` `0x102a1c0f`
  sets the ideal activity and a one-second deadline; `RunTask` `0x102aad1f` completes on
  current/ideal sequence equality or deadline expiry. Neither arm fails on a missing clip.
- First build completed with an incomplete `FElysiumWeapon` type error in
  `ElysiumNpcEnemy.cpp`; no tests ran. Report:
  `E:/elysium-work/logs/20260908T154626.902191Z-build.json`.
  The worker added the owning include; the next build passed:
  `E:/elysium-work/logs/20260908T154951.651754Z-build.json`.
- First substrate run executed all 571 tests: 566 passed (87 with warnings), five failed;
  zero not-run or in-process cases. Report:
  `E:/elysium-work/reports/tests/20260908T155007.476201Z-elysium-substrate/index.json`.
  New memory admission, damage branches, free-knowledge boundary, save/rebase, real swing,
  and activity-resolution cases passed. The initial failures were `FeedTrance.Patrol`,
  `NpcCombat.Retaliation`, `NpcCombat.RetaliationExpiry`, `NpcEnemy.ScheduleGate`,
  `NpcEnemy.ShouldChoose`.
- The five regression corrections are frozen and independently reviewed. They change only
  test setup/expectations, retaining actual sensing, memory, timing, and patrol assertions.
  The incremental build passed (`E:/elysium-work/logs/20260908T160013.147116Z-build.json`).
  Focused reruns passed: `NpcEnemy` 15/15, `NpcCombat` 17/17, `FeedTrance` 4/4, zero failures
  or unexecuted cases. Reports:
  `E:/elysium-work/reports/tests/20260908T160037.454732Z-elysium-substrate-npcenemy/index.json`,
  `E:/elysium-work/reports/tests/20260908T160108.341215Z-elysium-substrate-npccombat/index.json`,
  `E:/elysium-work/reports/tests/20260908T160132.047849Z-elysium-substrate-feedtrance/index.json`.
- Astra accepts R5 and R14 at the compiled/substrate level. The five initial failures are
  resolved; the earlier broad report remains evidence for its 566 passing cases. No redundant
  full-suite rerun was performed. The public CLI supports one literal filter per invocation,
  so the focused cohorts ran sequentially under one tester lease.
- Execution lease is released. Next implementation slice: requirement 6, one worker owning
  the complete sensing/hearing dependency slice and its necessary runtime wiring, shared
  changes, tests, and documentation. Other spec checkboxes remain open.
- R6 implementation owner: `worker_r6` (Terra high). The previous owner reached its context
  limit, was interrupted, and transferred the existing findings and partial patch once. Its explicit
  boundary is `Source/ElysiumUE/**`, relevant `docs/vtmb/**` and `docs/contracts/**`, and
  this spec's `spec.md`; necessary pipeline support is authorized within `pipeline/**`
  after reading its instructions. Scope remains R6 and its required dependencies.
  `execution.md` remains coordinator-owned. No other implementation owner is active.
  A coherent frozen patch goes promptly to a fresh R6 tester for focused build/test, then
  to a fresh R6 Astra high reviewer for one consolidated independent review. The worker
  has no execution lease and will not be repurposed for another checkbox.
- R6 status: worker reports implementation-ready and frozen; not yet compiled, tested,
  independently reviewed, or accepted. Fresh task-specific `tester_r6` now holds the sole
  execution lease. It builds promptly, starts with `NpcSenses` and `GameSound`, and broadens
  to substrate only when the shared changed dependencies justify it. Source is frozen until
  the active process exits and tester releases the lease. No prior tester is being reused.
- R6 verification preparation: start with the worker's new/changed `NpcSenses` and
  `GameSound` cohorts after compilation. Relevant existing consumer coverage includes
  `Footsteps.PlayerHearing`, `Discipline.AlertSound`, `Stealth.Senses`, and door tests.
  A broader substrate gate is justified if the completed patch changes their shared sound,
  sensing, or save dependencies; choose it after focused results rather than running every
  overlapping cohort and then repeating all of them automatically.
- The repository currently has no Play automation tier or beat driver from spec 0000.
  That prerequisite remains open; existing substrate fixtures do not replace the specified
  tutorial Play witness.
- Later light-query work must account for current bake-lane drift: adopted `FLightSource`
  entries explicitly have zero raw magnitude/radius/cone fields because the bake consumed
  them. Deriving gameplay illumination from calibrated component intensity would still
  violate the recovered query. Establish the authored worldlight-data join before replacing
  `QueryLightAtPoint` arithmetic.
- The new `NpcCombat.Swing` regression drives schedule, two-frame weapon contact, typed
  damage, and anonymous memory with different weapon-pickup and live-owner positions.
  Retail held-weapon origin is established by `Weapon_Equip` `0x1032d380` / `0x10252ea0`
  and FOLLOW simulation `0x10039470`; both melee and ranged packets carry the weapon handle.

## Continuation on 2026-09-08

The new task resumed the existing dirty tree without discarding the previous R5/R14 work.
No prior worker or tester process was running. Work in this continuation is performed locally;
permission for separate review agents has been requested because this session requires explicit
user authorization for delegation. Historical role assignments above are not active agents.

- R6's initial patch did not compile (private `Mind` access and a shadowed `RadiusCm`). It
  also still promoted hearing immediately, omitted concealment/deaf-zone consumers, multiplied
  the wrong cone term, and omitted priority-category memory writes. Raw retail recovery and
  corrections are recorded in the oracle's "R6 integration corrections" subsection.
- Added the shared `StealthKillRules` loader, actual sight-channel cadences and retained lists,
  shared FVisible fallback, observer BrainWipe/Obfuscate consumers, delayed sound conditions
  and outputs, saved pending deadlines, exact type selection, sensitivity/reduction order,
  sound interests, and the audio-duration query for ambient sound events. Save floor is 32.
- Broad substrate run: 573 executed, 567 passed, six failed on old sensing/ownership fixture
  assumptions. Report:
  `E:/elysium-work/reports/tests/20260908T175020.465517Z-elysium-substrate/index.json`.
- Rerun after the runtime and fixture corrections: 62/63 NPC tests passed; the remaining
  ownership assertion was corrected to check the forced running program and absence of patrol
  movement, without requiring a body token for an unresolved animation. Final `Npc.Classes`
  passed 1/1 and `Stealth` passed 7/7:
  `E:/elysium-work/reports/tests/20260908T175735.768782Z-elysium-substrate-npc/index.json`,
  `E:/elysium-work/reports/tests/20260908T175908.949887Z-elysium-substrate-npc-classes/index.json`,
  `E:/elysium-work/reports/tests/20260908T175928.102921Z-elysium-substrate-stealth/index.json`.
- Latest successful build:
  `E:/elysium-work/logs/20260908T175856.236822Z-build.json`.
- R6 is compiled and substrate-verified; independent review and the integrated Play witness
  remain open. The unnamed cone-offset ConVar and spec 0007's cloak/detection-record producers
  remain explicit seams, as documented. No checkbox was marked accepted from compilation alone.

Static recovery, source review, compilation, headless automation, and played/rendered
acceptance are separate evidence. `-nullrhi` automation cannot close the tutorial's rendered,
real-input witness.
