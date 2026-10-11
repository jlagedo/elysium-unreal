# L0 Goal — subsystem delivery

Owner-authorized replacement operating instructions, 2026-10-11. Continue the existing L0
Goal in the main thread. The owner authorizes resuming implementation under this flow; this
supersedes the earlier pause pending the planning review. Keep the existing Goal and its
history. Do not create a duplicate or falsely mark it complete to replace its wording.

## Objective and authority

Complete L0 entity against [spec.md](spec.md)'s full closing gate. Use
[delivery-plan.md](delivery-plan.md), [milestones.tsv](milestones.tsv),
[delivery-map.tsv](delivery-map.tsv), [service-contract-map.tsv](service-contract-map.tsv) and
[handoffs.md](handoffs.md) as the active scheduling and ownership plan. Follow `AGENTS.md` and
`docs/vision.md`. Original briefs, runs and receipts remain evidence and traceability; their
old order, ready labels, byte-based estimates and package counts do not control dispatch.

Deliver D01–D11 as working subsystem contracts, retaining D00's harness foundation. Keep
small runs as implementation checkpoints. A subsystem closes when its actual producers,
consumers, state, failure behavior, lifecycle, persistence and integration evidence are joined.
Do not turn this organization into a folder refactor or rewrite accepted code.

## Coordinator and workers

Use GPT-6.1 Sol (`gpt-6.1-sol`) Medium as the main coordinator. It directly owns planning,
review, gates, exact-path commits and checkpoints. Use one fresh GPT-6.1 Sol High coding
subagent per run, with no inherited chat history (`fork_turns="none"`). Supply a compact
packet and exact evidence links. One active run and one coding worker at a time; no nested
coordinator, parallel workers or worker-created subagents. Workers do not commit or push.
Never push from any role. Preserve corpus, content and worktree protections.

Continue through accepted runs toward the full gate without asking for routine approvals.
Bring the owner only a genuine proposed change to observable gameplay or scope, with the
faithful behavior and consequences stated. A missing walk, API choice, faithful threshold,
producer repair or hook/test registration is routine work, not an owner decision.

## Ownership and scope

Unreal supplies engine services; our substrate reproduces VtMB's game-visible rules and data
contracts. Chaos owns geometry, broadphase, contact generation, physics solving and rigid-body
mechanics. Unreal owns audio loading, decoding, streaming, voices and mixing. The substrate
owns observed filtering, movement formulas and thresholds, shadow rules, authored surface and
material semantics, sound selection/emission, gameplay sound stimuli, lifetime and event timing.

Name the observer for every claimed requirement: shipped content, keyfield, input/output,
script, semantic saved game state or measurable gameplay rule. Retail decompilation is the
behavioral oracle, not an implementation blueprint. Do not reconstruct Source/IVP topology,
packed layouts, allocators, solver internals or byte-exact outputs without such an observer.
Check consumers before removing existing machinery. Preserve observable identity, capacity,
failure, timing and ordering even when Unreal uses different mechanisms. Neither the name
Chaos nor a narrow passing test proves equivalence; no gameplay approximation is preapproved.

Retail-save importing is explicitly excluded, including external-save dry runs, retail roster
conversion and import-only gates. Native save/load/travel and faithful L0 state restoration
remain required. Saves are disposable: no migration or compatibility project. Implement L0
core first. Retain higher-layer obligations as explicit hooks/handoffs; do not implement their
gameplay to close L0. Account for the four retained scope-review briefs before final closure;
resolve core-producer evidence routinely and ask only if an actual scope decision remains.

## Start and select work

First read [checkpoint.md](checkpoint.md), inspect current HEAD/status and reconcile the sound
packet committed as `8f8aea1b`. Preserve its acceptance and source evidence; do not replay it.
Review the side conversation's uncommitted planning changes, correct concrete inconsistencies,
run appropriate documentation checks and commit only those reviewed paths as a separate plan
commit before coding. Do not mix unrelated work into it or run engine gates for documentation.

Then close D02 bodies, queries and transforms through P1, E2/P2 and P3. Reuse confirmed
body/material/shadow walks and existing implementations. [next-packet.md](next-packet.md) is
the first candidate: inspect the current base velocity getter, confirm its precise oracle and
production inputs, and implement its bounded contract if still missing. It is a candidate,
not a completed walk or permission to skip a required producer.

Keep one active closing milestone. Follow actual producer dependencies in the graph; do not
wait for unrelated acceptance work elsewhere in a prerequisite milestone. Handle tightly
coupled producer/caller behavior within the integrated milestone, especially P3. Do not round
robin between subsystems, put all audio ahead of physics, or require native slot storage before
a body query. Queue independent discoveries; add only required producer work to the active run.

## Each run

1. Select a specific observable contract that advances the active closing milestone. Record
   the mapped briefs/audit rows, real callers and producers, accepted oracle sections, exact
   unknowns, permitted files, acceptance records and stop boundary. Include applicable units,
   coordinate frames, clock phase, state writes, formulas, boundaries, failures, callback order
   and save/removal behavior. Reuse confirmed recovery; recover only material missing facts.
2. Verify installed Unreal public API declarations and existing project usage before designing
   the adapter: signatures, supported access, threading and lifetime. Reuse existing unit/frame/
   timing conversions; keep logical pose, submitted target and solved pose distinct.
3. Implement with real same/lower-layer producers. Name any newly discovered dependency, repair
   the plan and prerequisite, then continue. An unfinished same-layer service cannot answer
   nothing. Only documented higher-layer hooks or unreachable code may have empty seams.
4. Prove test fixtures reach the intended production branch using actual geometry, frame
   advancement and callbacks before expanding assertions. Check inputs, outputs and event
   sequences, including negative and lifecycle cases. Distinguish bad stimuli from runtime
   defects with evidence; never weaken an expectation merely to get green tests.
5. Build and run focused affected tests during development. Bound each diagnostic by one
   implementation question, its observer and an exit condition. Stop when answered, use the
   result, and distinguish diagnostic evidence from implemented behavior. Avoid general
   cross-engine experiments, repeat corpus bakes or duplicate forensic receipts without need.
6. Freeze the coherent result. The coordinator independently reviews the diff and retail
   contract, runs build, default tests and affected arena records, and checks baseline
   regressions. Run arm tests at story closure and other required relevant checks. Changes after
   the freeze invalidate affected gates. Reuse evidence only when its source, artifacts and
   verification assumptions remain valid. Commit reviewed paths locally and checkpoint before
   the next run. A failed review/gate returns to bounded repair; it is not an accepted run.

## Evidence, progress and completion

Maintain [checkpoint.md](checkpoint.md) as the compact restart state: accepted HEAD, current
milestone, exact contract closed and authoritative receipts, remaining closing requirements,
first unmet condition, next packet and genuine owner decisions. Link detailed walks/history;
do not copy entire logs into fresh worker context. Preserve original row/brief traceability and
update audit verdicts only on supported closure. Do not regenerate the retail oracle ledger.

Report progress as **contract closed → milestone requirements remaining → next closing
witness**. At subsystem closure reconcile its mapped requirements, saved/rebuilt/reset state,
lifecycle and higher hooks. Show implementation, prerequisites and diagnostics distinctly.
The historical 23/88 bundles are not a current percentage, ETA or definition of progress.

Continue until the complete spec gate passes: all active L0 and integration records in one
full arena run; required repeated boot orders; default and arm tiers with zero failures;
baseline preservation; every owned audit row done or an owner-accepted named divergence;
observable engine-replacement contracts; registered higher hooks; and complete disposition of
all mapped requirements. Do not mark the Goal complete for a plan, a packet or a subsystem.
Respect explicit user stops and actual platform limits, with an honest resumable checkpoint.
