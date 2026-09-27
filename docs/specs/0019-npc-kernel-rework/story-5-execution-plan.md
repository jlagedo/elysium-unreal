# 0019 story 5 — reviewed execution plan: the class tree

Status: **reviewed plan; steps 0–4 accepted (step 4 with its review follow-up, packet 4r); step 5 complete in the working tree (packets 5a–5h), its commit pending the owner** (2026-09-26).
Execution checkpoint and evidence: [story-5/progress.md](story-5/progress.md).
Scope: [spec.md, story 5](spec.md). Tracker: [06b](../TRACKER.md).
This document defines execution order, intermediate states and acceptance. The spec defines the
finished product. The reviewed execution rules below replace the earlier plan's estimates and
claims of behaviour-neutral proof.

## Start here

- Follow steps 0–11 in §5. Finish the gate for one landed step before integrating the next.
- Step 0 settles identities and rehearses difficult cases; it does not build a general C++ rewriter.
- Step 4 lands bodies, words and bindings together in **one commit**, as the owner requested.
- A **session is a bounded work packet**, not a whole step or commit. Use §6 to resume or hand off.
- One integration owner writes shared C++ infrastructure and runs builds. Independent evidence
  review and disjoint tooling work may proceed alongside it.
- Deferred classes keep only explicitly listed compatibility code. Step 6 is not the final deletion
  gate; the last folds consume their dependencies before step 11 removes the remaining machinery.
- Story 8 implementation waits for step 11. Retail retrieval may continue independently.
- No step is complete because tests still pass: acceptance combines retail evidence, structural
  checks and focused observable assertions.

### Settled scope

Preserve these owner decisions:

1. `FElysiumNpcBase` represents `CAI_BaseNPC`; `FElysiumNpc` represents
   `CAI_BaseNPCTroika`.
2. One header and cpp per live retail class, with the agreed port names in Appendix A. Dead
   classes retain census records but get no port class.
3. Delete the established dead species subset before moving live species implementations.
4. Correct the factory map with the tree and ultimately register every live retail classname.
5. Move bodies and words to their retail owners; use C++ overrides for variation and exact
   qualified calls for retail direct calls.
6. Land species bodies, words and bindings together; migrate fixtures to real classname factories.
   Internally constructed retail classes, such as `CAI_TestHull`, need typed construction tests.
7. Move generated slot bodies to their owners; finish with a census-only shape generator.
8. Fold the controller with retail AI, followed by makers, directors and the test hull.
9. Preserve retail state, ordering and observables. Any modernization is named and evidenced.

## 1. Execution contract

Use branch `0019-5-class-tree`. Do not create it or start runtime work as part of reviewing this
document. At implementation start, record the actual checkout, commit, dirty files and local roots.
Preserve existing unrelated edits; neither a fresh session nor a new worktree authorizes resetting
them.

A landed step is a coherent, validated change. Step 4 has one final commit. Other steps normally
have one commit, but a preflight may identify independently valid prerequisites; name those before
editing rather than forcing unrelated changes into an oversized commit. Every C++ commit must
have a passing runtime gate. A documentation-only checkpoint needs document consistency checks,
not an unchanged editor rebuild.

A session may end before the step can land. Save the work and the §6 handoff, mark the step
incomplete, and resume from that exact tree. Do not commit an incomplete step just to shorten a
conversation. Build counts are observations, not quotas.

Classify changes before running the gate:

| Classification | Required evidence |
|---|---|
| Structural migration | Same covered observables, plus verified ownership, dispatch and bindings |
| Retail correction | Retail address and reaching content/caller chain, old/new behaviour, focused assertions |
| Newly active registration | Previous registry disposition, factory/type, authored or dynamic creation path, exercised behaviour |
| Diagnostic change | Stable semantic identity and an explicit mapping of old/new diagnostics |
| Unrecovered input | Named retail field, existing producer or explicit empty seam, retained backlog entry |

A green baseline describes the current port; it is not the retail oracle. Do not retain a proven
divergence merely to make a structural step's delta empty. Record the correction within the
affected work packet and update its expected delta. New, unrelated behaviour ports remain in their
own story.

## 2. Order, dependencies and reviewed hazards

The main order remains deletion, species migration, base separation, then the folds. Species move
straight to their final class files; base bodies wait for their actual owner. This reduces repeated
large edits without pretending every transition can be behaviour-neutral.

The following corrections determine the intermediate gates:

- **Registration scope:** the current ordinary-NPC list has 15 classnames; step 2's 44 species
  classes require 45 classnames, including aliases. That is **30 additional active names**,
  distinct from the seven corrected census resolutions. The controller and infrastructure
  registrations are tracked separately.
- **Direct call target:** Zombie `0x103e1080` calls `CAI_BaseNPC::ShouldPlayFloatSound`
  `0x1027a530`, bypassing Troika `0x10294070`. The current port's
  `FUN_103e1080` calls `ShouldPlayFloatSound()`, which runs the Troika gates. A mechanical
  `FElysiumNpc::Slot(...)` replacement would preserve that divergence.
- **Ownership is not a target-string lookup:** the verdict overlay names
  `FElysiumNpc::ShouldPlayFloatSound` for both of those base-layer retail bodies, while the port
  also has `BaseShouldPlayFloatSound`. Resolve body identity, signatures and actual definitions
  before assigning owners.
- **No factory does not mean dead:** direct calls and internal construction survive even when no
  classname dispatches to the body. See `population.md`'s death criteria and `CAI_TestHull`.
- **Offsets and slot numbers are not globally unique:** sibling datamaps reuse offsets; branches
  introduce unrelated virtuals at 583 and 617 onward. See `shape.md` § "The tables".
- **Folding changes participation:** makers already duplicate NPC fields, and inheriting
  `AsNpc()` affects world timer, sense and other consumers. Suppressing their decision pass is
  only one acceptance condition.
- **Ten classes are deferred:** controller/FrenzyShadow/WolfMorph, three makers, three directors
  and TestHull. Any existing methods, tests or lookups they need remain on a shrinking,
  row-specific compatibility list until their fold.
- **Compilation does not establish retail reachability:** never delete a stub because removal
  happens to compile, or delete a test because a fixture codemod cannot express it.

Evidence starting points:

- [Factory and liveness recovery](../../vtmb/npc-ai/population.md), final two sections.
- [Direct calls and branch-specific slots](../../vtmb/npc-ai/shape.md).
- [Think and initialization contracts](../../vtmb/npc-ai/lifecycle.md).
- [Kernel ledger conventions](../../vtmb/npc-kernel/README.md).
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcClasses.cpp` — actual registered leaves.
- `ElysiumNpcKernelSpecies2.cpp` / `ElysiumNpcKernelSounds.cpp` — Zombie and the two sound gates.
- `ElysiumNpcMaker.h` — duplicated solidity and police-threshold fields.
- `research/tooling/gen_kernel_bindings.py::parse_shape_map` — currently offset-only.

## 3. Validation and build economy

### 3.1 Baseline and provenance

The saved 2026-09-24 reports at commit `cb115e31` are reference evidence:

| Suite | Completed | Report under `$ELYSIUM_WORK_ROOT/reports/tests/` |
|---|---:|---|
| `Elysium.Substrate` | 1,268; zero failures | `20260924T001027.201870Z-elysium-substrate/index.json` |
| `Elysium.Content` | 14; zero failures | `20260924T001500.460030Z-elysium-content/index.json` |
| `Elysium.PlayerWorld` | 1; zero failures | `20260924T001559.443050Z-elysium-playerworld/index.json` |

The Substrate report includes 791 successes with warnings. Its 107 distinct stub surfaces describe
that test run, not coverage of real play. Record report provenance and confirm the source state
still matches before using it as the implementation baseline. If relevant inputs changed, establish
a new baseline and retain both.

Each gate record names the source commit plus pending-diff identity, relevant corpus/module and
manifest hashes, commands, exit status and report paths. Compare step N to the last accepted step,
and retain a cumulative comparison to the starting baseline. Do not reuse a report from a different
tree, harness, asset state or RNG setup without accounting for that difference.

### 3.2 Cheap checks first

Before a C++ build:

1. Review the work packet's diff and proposed source moves.
2. Run affected Python tests, codemod precondition checks and the phase-manifest checks.
3. Regenerate affected ledger, shape and binding outputs through their existing commands.
4. Verify all applicable generated outputs and ledgers:

```text
uv run elysium research kernel_shape --check
uv run elysium research kernel_ledger --check
uv run elysium research kernel_lists --check
uv run elysium research gen_kernel_shape --check
uv run elysium research gen_kernel_bindings --check
uv run pytest pipeline/tests/test_kernel_ledger.py pipeline/tests/test_kernel_shape.py pipeline/tests/test_gen_kernel_shape.py
```

Add the affected factory, migration and binding tests as they are introduced in step 0 and later
steps. The commands above already exist; the proposed migration checker and `test_delta` do not
exist at the time this plan is written.

During migration, generators must understand the current ownership manifest and refrain from
re-emitting moved declarations or definitions. Keep their `--check` gates active; do not obtain
a green step by hand-editing generated code or disabling the check.

### 3.3 Runtime gate

For each landed C++ change, after the cheap checks:

```text
uv run elysium build
uv run elysium test Elysium.Substrate
uv run elysium test Elysium.Content
uv run elysium test Elysium.PlayerWorld
```

Run focused suite prefixes while developing a packet; run the complete gate when the landed tree
is ready. Do not repeat an identical successful full gate without a new source change, failure or
specific unresolved concern. Add the step's map/play checks where stated. Record maps or events
that could not be exercised, with the actual blocker; absence of a playable map is not coverage.

### 3.4 Regression comparison and observable assertions

Build `test_delta` as regression bookkeeping, **not proof of behaviour equivalence**. It compares
all three suite reports and their relevant diagnostics. An expectation entry has an exact test or
semantic surface, change kind, evidence/reason and owning packet. Support additions, removals,
renames and diagnostic remaps; reject unused expectations and unmatched differences.

No expectation turns a failing test into a passing gate. An intended correction changes assertions
and must still finish green. Test removals require a reviewed coverage disposition.

For deterministic fixtures, compare the relevant results and ordered events: direct callee,
virtual override, state writes, outputs, timers, RNG use and restored values. Use existing recording
services and traces where possible. Avoid building a general replay framework for this story.

Track stub observations per test by stable retail identity, including module/address and declaring
slot family where available, plus receiver class. Counts and order matter for targeted deterministic
cases. Do not blindly compare wall-clock timing or whole nondeterministic logs. A renamed owner
prefix is a diagnostic remap; a new receiver or different call count may be a behaviour change.

### 3.5 Measurement

Header changes historically rebuilt 70–117 files in 2.5–19 minutes. That is a range from three
builds, not a forecast. Discard the earlier fixed 28-build / 4–6-hour commitment.

Measure the rehearsal's compile fan-out, build duration, test duration and manual exceptions.
Batch compatible edits around dependency boundaries. Prefer one warm integration checkout over
rebuilding the same editor in several worktrees. A source-only rehearsal may use an isolated
checkout; a build there requires explicit local-root, ignored-file, asset and output setup.
Do not copy an entire scratch or generated-content tree merely to get a fresh session.

## 4. Durable migration manifest

Step 0 creates a small, versioned manifest and checker. Reuse the existing ledger/generator models
where possible. Do not create six independent ownership databases or a new authoritative retail
ledger. The manifest records execution decisions and joins back to the existing oracle.

Store authored mappings and decisions beside this plan under `story-5/`; keep generated detailed
reports, decompilation, logs and rehearsal patches under
`$ELYSIUM_WORK_ROOT/research/npc-kernel/story-5/`. Commit no retail bodies.

| Record | Required identity and decisions |
|---|---|
| Class/factory | Retail class, final port class/base, aliases, factory/constructor/vtable evidence, current registry disposition, introduction/fold step |
| Field | Declaring retail class + offset/member, type/width/flags, final port owner/path, aggregate or seam disposition, inherited/shadowed binding policy |
| Virtual/body | Module + address, introducing class + slot/signature, class-specific override, actual port definition, final owner/name, verdict |
| Call edge | Caller and callee addresses, direct versus virtual, required qualified owner or virtual receiver, evidence |
| Compatibility | Exact symbol/site or fixture case, remaining consumer, removal step, reason and replacement |
| Validation | Affected fixture/witness, unchanged observables or intended delta, required checks, unresolved inputs |

Class-specific override identity includes the receiver class; an address shared by folded bodies
does not collapse distinct slot contracts. A helper's callers may suggest ownership, but data
access, receiver type, direct edges and visibility must agree. Mixed component structs such as
`FElysiumNpcScheduleHost` need an explicit base/Troika storage decision, not a caller-majority vote.

The checker must establish:

- No ambiguous factory mapping, duplicate field identity, ownerless move or lost signature.
- One declared migration disposition for every affected live body, field, fixture and lookup.
- Each generated/moved definition has exactly one active home at the current phase.
- Deferred compatibility entries name actual consumers and cannot grow silently.
- A live unported rule stays in the residue even if its stub, spelling or location changes.
- Structural completeness and implemented behaviour are separate counts.
- Per-phase class/factory tests check introduced classes and explicitly list deferred ones.
- Final state has all 56 live classes, no deferred entries, and the dead census classes remain listed.

A migration script uses exact source preconditions, handles overloads/macros deliberately, prints
a unified diff, and fails on unrecognized shapes. Apply each dry run to the predecessor's actual
output, not independently to `main`. Reapplying a completed operation must be a documented no-op
or a clear precondition failure. Ambiguity is a review item, never an automatic dead verdict.

## 5. Implementation steps

### Recommended GPT execution settings

Recommendations recorded 2026-09-24 for execution in Codex. The workflow, work packets and gates
remain harness-agnostic and can also be executed by another coding agent. These assignments are
engineering judgments based on this story's risks, not benchmark results on this repository.

In the table, **Astra** means `gpt-6-astra`, **Sol** means `gpt-6-sol`, and **Extra High** means
`xhigh`. A single-model alternative is Astra High, raising effort for the packets marked Extra
High. Select from the models and effort levels available in the executing client.

| Step | Recommended GPT / effort | Landed result | Main risk | Gate emphasis |
|---:|---|---|---|---|
| 0 | **Astra / High** | Manifest, reconciled evidence, minimum tooling and rehearsals | Wrong identity or unsupported codemod assumption | Python, source replay, representative build |
| 1 | **Sol / High** | Established dead species subset removed | Deleting a shared live implementation or its only coverage | Deletion evidence, complete runtime gate |
| 2 | **Sol / High** | Species shells, corrected factories, phased fixture migration | 30 newly active names and lifecycle effects | Factory/registry matrix, witnesses |
| 3 | **Astra / Extra High** | Introduced species use overrides and exact direct calls | Preserving an old wrong target or changing nested dispatch | Deterministic dispatch assertions |
| 4 | **Astra / High** | Species bodies, words and bindings at their owners, one commit | Offset collisions, saves and newly effective keys | Binding flags, inheritance, round trips |
| 5 | **Astra / Extra High** | Base and Troika separated | Mixed component state and helper receiver types | Storage/accessor ownership, base behaviour |
| 6 | **Astra / High** | Available slot owners migrated; census enforces current tree | Erasing residue or deleting deferred dependencies | Generator/manifest checks, entity-chain coverage |
| 7 | **Sol / High** | Controller, FrenzyShadow and WolfMorph folded | AI admission, cadence and inherited lifecycle | Controller creation and played event |
| 8 | **Astra / Extra High** | Three makers folded | Shadow state and new world participation | Cadence, activation, I/O and restore |
| 9 | **Astra / High** | Three script directors folded | Base-only semantics and possession ordering | Scripted beats and restore |
| 10 | **Sol / Medium** | Internal test hull folded | Confusing internal construction with dead code | Typed construction and six bodies |
| 11 | **Astra / High** | Compatibility removed; final gate and story handoff | Stale mappings or false completion | Empty migration residue, explicit behaviour backlog |

Apply these refinements per work packet:

- **Step 0:** raise Astra to Extra High for ambiguous retail recovery. Sol High can implement
  tooling after its schema and semantic mappings are settled.
- **Step 7:** use Astra High for the lifecycle-integration review after Sol's implementation.
- **Step 9:** raise Astra to Extra High for unresolved possession, interruption or restore ordering.
- **Step 11:** retain Astra High for the completeness audit; Sol Medium can update documentation
  after the findings and final state are settled.
- **Supporting work:** GPT-6 Luna (`gpt-6-luna`) High can extract inventory rows, summarize reports
  and perform tightly specified transformations. Keep retail verdicts, ownership decisions and
  final semantic acceptance with the recommended Sol/Astra executor or reviewer.

Use Max only for a specific unresolved reasoning problem after gathering its evidence. Ultra uses
subagents and is an option for independent audits; it does not change the single-writer rule for
the integration checkout. Higher effort costs more time and tokens. Missing retail evidence still
requires retrieval, regardless of effort. These model roles and effort tradeoffs follow the
[official model guidance](https://learn.chatgpt.com/docs/models#choosing-sol-terra-and-luna) and
[effort guidance](https://learn.chatgpt.com/docs/models#pick-a-reasoning-effort); the per-step
assignments above are this plan's recommendations.

Switch models or harnesses at saved work-packet boundaries using the §6 handoff. A large step can
span several sessions, including the atomic step 4 commit.

### Step 0 — Establish the manifest and rehearse

**Scope.** Resolve the hard identities before bulk C++ changes. Inspect existing ledger, skeleton,
packet and generator code before adding tools. Build a shared manifest reader, a phase checker,
`test_delta`, and only the first required codemod operations. Subsequent operations can land with
their consuming step.

1. Replace the proximity-based factory inference with a reviewed allocation/constructor/final
   primary-vtable map. Use `scratch/0019-5/factory_map.py` as research input, not an unqualified
   production parser: it scans raw bytes, excludes makers and does not cover the directors.
   Verify instruction/function boundaries, constructor thunks and receiver identity; reject
   unresolved or multiple answers. Pin the module and input hashes.
2. Cover the existing 68 NPC-related classname observations, the three directors, three makers
   and nine census differences. Separate observed factories from liveness. Activate the new
   survey/census behaviour in step 2; preflight tooling can be committed without changing it.
3. Resolve port targets against actual source definitions and retail direct edges. Classify
   dispatch sites, type tests, data queries and diagnostics. Name the deferred consumers.
4. Extend the planned field identity to declaring class + offset before species generation.
   Inventory component fields spanning both base layers, duplicate maker state, entity-method
   signature differences and all source dependencies of moved bodies.
5. Classify fixtures as direct-body, virtual-dispatch, lifecycle/integration or internal-construction
   tests. Identify cases that reclass the same object or drive multiple slots.
6. Rehearse Cop, Rat and Zombie as separate small packets through their necessary edits. Also
   check a base-only/director path and the maker duplicate-state boundary. The rehearsal must
   exercise the proposed schema and code transformation, not just compile empty class shells.
   Preserve patches, results and exceptions outside the repo. Do not merge partial rehearsal
   implementations into the integration branch.

**Acceptance.** Reviewed mappings cover the next step; ambiguous rows fail closed. Pinned-input
tests distinguish aliases, inline constructors, shared bodies, sibling offsets and branch slots.
Dry runs compose against staged predecessor source. The representative build and focused tests
support a revised estimate. Preflight reports explicitly separate confirmed facts from hypotheses.

**Session units.** Factory evidence; field/body identity; call/fixture classification; regression
comparison; representative rehearsal. Agree on the manifest schema before parallel tool writers
consume it. There is no requirement to finish every future codemod in this step.

### Step 1 — Delete the established dead species subset

Use the current delete list and reviewed liveness criteria. The prior inventory found 228
hand-written bodies in 43 files and 195 override rows; regenerate the exact list rather than
treating these counts as acceptance.

- Remove bodies, declarations, dispatch arms and storage only when no live implementation,
  inherited contract or direct caller still needs them. A shared port target needs all of its
  retail identities checked.
- Remove dead-only assertions; preserve or re-home coverage in mixed tests. A citation alone does
  not make an entire test dead. Unconvertible fixtures are not deletion candidates.
- Stop emitting the deleted executable overrides while retaining their dead ledger/census
  evidence. Set a deleted target to `-` only when the corresponding implementation is gone.
- Regenerate lists, ledger and affected shape outputs.

**Acceptance.** Full gate; every removed test/assertion has a coverage disposition. Remaining live
observables are unchanged. Stub removals attributable solely to retired tests have exact
expectations. A contradictory live caller reopens the verdict with evidence; it is not suppressed
to satisfy a deletion target.

**Session unit.** One family or a reviewed group of dead-only bodies and their tests.

### Step 2 — Introduce species shells and correct the factories

Land the 44 species classes assigned to step 2 in Appendix A. `FElysiumNpc` remains the existing
combined base until step 5; Appendix A describes its **final** parent.

- Activate the reviewed factory survey and regenerate the census. Each introduced production
  class answers its own class row. Remove production reliance on the most-derived claimant
  rule; retain only enumerated compatibility consumers until their migration.
- Register abstract retail descriptors separately from constructible classname aliases.
  `Create` already checks a null factory and falls back to `FElysiumEntity`; change that
  abstract-descriptor case to explicit refusal, audit callers' refusal handling and preserve
  intentional unknown-class record behaviour.
- Build descriptor chains for introduced classes without attaching typed NPC accessors to an
  old deferred entity that is not yet an NPC. Represent the combined base projection explicitly
  until step 5. Final chain equality is a closing gate, not a false claim about this phase.
- Expand the ordinary-NPC registry from 15 to 45 names. Record all 30 new active names, their old
  absent/stub disposition and creation paths. Cover all seven corrected classname resolutions.
- Migrate fixtures for introduced classes to real factories. Reclassing loops need independent
  typed instances/fixtures and explicit clock/RNG setup. Choose fixtures for all behaviour the
  case exercises, not merely one slot.
- Retain any still-required test-only class instrumentation solely for enumerated deferred
  cases. It remains a recorded transitional fiction, never evidence that the C++ tree matches
  retail. Keep it out of production use; remove each case with its fold and the API at closure.

**Acceptance.** Factory tests cover all introduced classnames and aliases, default class identity,
abstract refusal and dead-name disposition. Registry tests check the phase's projected hierarchy.
Deferred factories keep their existing behaviour and are named as exceptions. Cop uses Cop's
schedule space. Re-pin old wrong-answer tests with evidence.

Smoke every newly active class through available placed, maker or script creation paths, including
classes absent from the tutorial. Generate the map list from the population rather than four
selected names. Check crashes/assertions and relevant world, entity, script and AI diagnostics;
do not restrict observation to one log category. Record unported behaviour as such.

**Expected changes.** Corrected identity and newly active instances. The tutorial's placed
population does not require those new names; its covered behaviour should remain unchanged.

### Step 3 — Replace introduced-species dispatch

Work by coherent slot family and complete its dependencies, not by a fixed six-build quota.
Forwarding overrides may call existing species methods until step 4 moves their bodies.

- Convert address arms, member-pointer tables and dispatch-shaped class tests to overrides.
  Port genuine retail type tests with the supported typed mechanism; do not assume RTTI support.
- For every guarded direct call, use the **recovered callee owner**, not an automatic immediate
  base or `FElysiumNpc::` substitution. Preserve nested virtual calls made inside that body.
- Where the callee's final base class does not yet exist, call its distinct existing helper
  explicitly and record the step-5 rename/qualification. Do not route through the wrong layer
  temporarily and describe it as equivalent.
- Resolve the Zombie case against the whole `0x103e1080` chain. Test bypass of the Troika idle
  gates and the base frequency/time/random decision. Preserve explicit missing-input seams
  and their backlog; recover inputs necessary to exercise the accepted behaviour.
- Convert vocalisation for introduced classes and retire their table entries. Deferred controller
  variants retain only their listed entries until step 7.
- Rename slot-numbered APIs only after matching signature and introducing class. Update overlay
  targets and generator ownership in the same packet.

**Acceptance.** Full gate plus focused Bach 606, Tzimisce 593, Zombie 510 and slot-482 cases.
Assert the exact direct destination, order and side effects, not just the final return value.
Retail corrections have named deltas and matching oracle updates.

The static gate allows only listed deferred dispatch sites and the lookup declarations/definitions
still required to support them. It distinguishes code from comments and tests. No blanket
"zero occurrences in Source" claim is made yet.

### Step 4 — Move species bodies, words and bindings together

**One final commit.** Several saved work sessions may prepare it. Restrict this step to introduced
species; deferred-class bodies and words stay in explicitly listed transitional homes until their
folds. This avoids pretending their final owners already exist.

- Move definitions and declarations to final class files; collapse forwarding overrides.
  Include live `rule`, `present` and `mechanism` implementations, with their existing seams.
- Move existing species state and declare missing required datamap state using class-qualified
  identities. Preserve types, defaults, inherited fields, output semantics and known gaps.
- Generate per-class KEY/INPUT/OUTPUT/SAVE bindings with retail flags and lookup precedence.
  An inherited row must work on descendants without being copied into sibling descriptors.
- Extend the shape map, binding parser and save handling together. Do not add species rows to an
  offset-only map or silently overwrite a sibling's field.
- Move private helpers with their sole owner; place truly shared helpers in narrow private
  support files. Headers include their base and required complete member types; forward-declare
  other classes where sufficient. "Only include the base header" is not a correctness rule.
- Update fixture casts only after checking their factory/type contract. Exercise full snapshot
  capture/apply/restore, not only leaf serialization.

**Acceptance.** Every migrated word/body has one final home. Binding tests cover inherited rows,
sibling rejection, INPUT/KEY flag differences and shadowing. Save round trips cover introduced
classes with non-default distinguishable values, handles and affected aggregates.

The tutorial's three rats receive their eight authored keys. Check the complete scurrying
detection/fright paths, including `0x103acb1a`, against retail. Any witness changes are justified
by the newly effective inputs and recorded in the oracle and delta. Full gate on the final atomic
tree; intermediate packet results do not make the step complete.

### Step 5 — Separate CAI_BaseNPC from Troika

Create `FElysiumNpcBase : FElysiumScriptedCharacter` and make `FElysiumNpc` derive from it.

- Move the 135 base-layer and 253 Troika-layer word dispositions to the appropriate storage
  owner. These include component-backed and implicit/seam dispositions, not 388 interchangeable
  direct data members.
- Separate mixed components or expose correctly typed accessors where required. Preserve
  receiver contracts for senses, memory, schedule hosting, navigation and serialization.
  Do not solve an upward dependency with an unchecked cast to Troika.
- Split base/Troika method bodies and mixed family declarations. Audit helpers using their
  actual state/call dependencies. Update bindings and generator declarations with the split.
- Resolve step 3's temporary direct-helper references to exact final qualified callees.
- Define and test base-NPC versus Troika type access explicitly. Review `AsNpc()` consumers
  before the subsequent folds change membership.
- Preserve safe transitional homes for deferred bodies and document their remaining dependencies.

**Acceptance.** Full gate, no unexplained observable delta. Ownership checks cover component paths
as well as direct members. A base-only typed probe exercises base behaviour without executing
Troika-only code; do not require a fictitious retail classname to construct it.
Descriptor/accessor types agree with the C++ chain.

### Step 6 — Move available slot bodies and enforce the current tree

Move generated bodies whose final entity-chain or NPC owners now exist. Inventory by semantic
identity; reconcile the old 268-body headline against its 269-entry ownership subtotal first.

- Preserve one-constant bodies and stub diagnostics on their actual owners. For linker-folded
  bodies, resolve the slot introduction independently from the shared body address.
- Audit the eight proposed entity-method integrations by signature, units and semantics:
  `AcceptInput`, `GetAbsOrigin`, `GetAngles`, `GetModelIndex`, `GetOrigin`, `SetMoveType`,
  `SetOrigin`, `Weapon_Switch`. These are integration candidates, not eight proven same-name
  C++ methods. In particular, reference-return lowering and existing runtime writers need
  deliberate adapters or signature changes.
- Remove only stubs with a reviewed dead/seam/implemented disposition. A successful compile after
  deletion is not a reachability argument. Preserve live unported contracts and residue.
- Delete obsolete dispatch machinery as its last listed consumer disappears. If deferred classes
  still require a surface, retain precisely those rows and its support code.
- Extend the census tests for field owners, implemented override contracts and current factories.
  All ten deferred classes remain explicitly listed at this boundary.
- Add `kernel_shape --unported` for the full live-rule inventory; `--residue` retains its
  existing layout/signature meaning. Establish both count and stable row set.

**Acceptance.** Full gate including affected non-NPC/entity-chain users and moved default-body
tests. Diagnostic owner renames and newly observed receivers have explicit dispositions.
The shape generator emits the census plus only any named deferred compatibility output still
needed; census-only output and deletion of the old files are enforced at step 11.

### Step 7 — Fold the controller line

Replace the old controller with `FElysiumNpcPlayerController : FElysiumNpcVampire`; introduce
FrenzyShadow and WolfMorph under it and register all three names.

- Port `NPCThink` `0x103a4700`: direct Troika think, then virtual slot 614 reset. The
  decompilation is damaged; the recovered listing establishes CALL followed by the tail JMP.
- Port `PreSelectSchedule` `0x103a46b0`: idle returns `0x6b`, otherwise direct Troika call.
  Audit each descendant's own overrides; inherited controller behaviour is not assumed for all.
- Move their deferred bodies, fields, bindings and fixtures as one coherent fold. Include silent
  vocalisation and view-cone overrides, initialization and restore.
- Compare the old non-solid motor and collision behaviour with `Spawn` `0x103a4510` and
  `NPCInit` `0x103a4580`; preserve retail sequencing and state, naming any modernization.

**Acceptance.** Full gate; `CreateControllerNPC` constructs the correct type, schedules and clock
behaviour, including idle and non-idle cases. Exercise an available shipped controller event
(cemetery, downtown, e32004 or vamputil path) through creation, beat completion and removal.
Remove these three classes' compatibility entries and deferred fixture instrumentation.

### Step 8 — Fold the makers

Stand `FElysiumNpcMaker : FElysiumNpc`, with Fleshpile and Zombie subclasses.

- Reconcile inherited storage first: solidity, police thresholds and every duplicate of a base
  word must have one authoritative home. Verify that bindings and maker code access that home.
- Move variant behaviour to overrides, including Fleshpile `MakeNPC` `0x1034c2d0` and
  `DeathNotice` `0x1034c8e0`. Preserve the three spawn contracts
  `0x1034afe0`, `0x1034c020`, `0x1034cc60`.
- Recover/audit lifecycle participation before inheriting callbacks: Spawn, Activate, installed
  think, AI-enable/wake timer resets, dormancy, kill/removal, body/model admission and restore.
  The retail think-function installation is distinct from simply overriding `NPCThink`.
- Audit new `AsNpc()` and combat-character membership in world and sense consumers against
  retail flags/casts. Do not force all previous nonparticipation to remain if retail says otherwise.
- Attach the inherited datamap only when the C++ type and field ownership support it.

**Acceptance.** Full gate plus deterministic enabled/disabled, frequency, depletion, child
inheritance, death notice and save/restore cases. Inherited inputs must write the same state the
maker reads. Prove expected decision-pass participation and that global wake/AI-toggle paths do
not accidentally replace maker cadence. Tutorial and hub infrastructure witnesses must remain
green; any changed assertion needs retail evidence, not a blanket empty-delta requirement.
Exercise tutorial staged encounters. Remove the three maker compatibility entries.

### Step 9 — Fold the script directors

Stand `FElysiumScriptedSequence` for `CCineNPC` under `FElysiumNpcBase`.
Stand `FElysiumAiScriptedSequence` for `CCineAI` and `FElysiumAiScriptedSchedule` for
`CCineAISchedule` beneath it. These are siblings below CCineNPC, not Troika subclasses.

- Split the two sequence classnames into their actual classes. Move their own words, bodies,
  bindings and deferred fixtures; check the branch-specific 583–586 signatures.
- Audit construction, Spawn/Activate, target acquisition, possession/release, installed think,
  interruption, chaining, removal and restore before accepting inherited base behaviour.
- Attach the base-NPC datamap without Troika-only accessors or scheduling assumptions.

**Acceptance.** Full gate plus existing ScriptedSequence, AiScriptedSchedule and ScheduleWitness
coverage. Exercise all three factories, the CCineAI-specific bodies, interruption and restore of
an in-progress beat. Check tutorial and hub scripted witnesses and a played tutorial sequence.
Remove the three director compatibility entries; document any corrected existing divergence.

### Step 10 — Fold the internal test hull

Stand `FElysiumNpcTestHull : FElysiumNpcBase` and its six own mechanism bodies. It has no retail
classname factory. Check step height 40, jump speed 40 and the 1024/1024/1024 jump-legal limits.

Migrate the remaining Motor class checks and fixtures. Instantiate the concrete C++ type for
internal-construction tests; do not fabricate a classname or mark it dead because no factory
exists. If port navigation does not construct it, state that explicitly and retain its contract.

**Acceptance.** Full gate and typed tests. The class deferral list is empty. All remaining
compatibility entries must now be removable support code, with no live or test consumer.

### Step 11 — Remove compatibility and close

- Remove remaining lookup dispatch, species tables, guards, latches and
  `SetRetailClassForTests`. Keep census query helpers only where a documented non-dispatch
  consumer needs them. Genuine type tests must use the final supported type mechanism.
- Make `gen_kernel_shape` census-only and delete the obsolete generated slot files after their
  final dependencies are gone. Update generator tests and final source checks together.
- Verify 56 live classes, the exact final factory and descriptor chains, class-qualified fields,
  implemented overrides and an empty compatibility list. Preserve all dead census records.
- Reconcile the entire live unported-rule set. Explain every removed or added row by implementation,
  corrected evidence or explicit scope change; never hide a missing body by deleting its surface.
- Run the final structural/generator and full runtime gate for any cleanup C++ changes. If closure
  changes documentation only, reuse the accepted final runtime reports with their exact source
  provenance. Finish the available played checks and record any remaining coverage limitations.
- Update `shape.md`, `population.md`, affected oracle topics and coverage. Tick story 5 and
  tracker 06b only after the acceptance above, then hand story 8 the final class map, residue row
  set and report paths. Do not describe unported species behaviour as completed by this story.

## 6. Context, sessions and handoffs

This workflow is independent of the AI harness. Session memory, compaction summaries and chat
history are conveniences; repository state, evidence and recorded results are authoritative.

### 6.1 Persistent state

Create these authored files when implementation begins, not placeholder completion records now:

| File under `story-5/` | Purpose |
|---|---|
| `progress.md` | Current phase/packet, tree identity, completed gates, next action and unresolved decisions |
| `manifest.*` or a small set of typed tables | The §4 mappings; choose the minimum format supported by the existing Python tools |
| `packets/<id>.md` | One bounded work instruction and its final disposition |
| `expectations/<step>.*` | Reviewed regression expectations and coverage dispositions |

Keep `progress.md` short: current state and an index to completed packets, not copied logs.
Keep generated reports and full command output in the work root and link them by path and hash.
Record facts once and reference them; do not duplicate retail recovery prose into every packet.

### 6.2 Work-packet contract

A packet covers one coherent slot family, small related class group, or cross-cutting seam.
It includes implementation and its focused validation. Split when it needs several unrelated
retail contracts, too many simultaneous source families, or a handoff that cannot explain what is
safe to change. Do not split a caller from its required callee or a binding from its storage merely
to hit a line count.

Use this compact template:

```text
Packet: <step and short name>
Start: <branch/checkout, commit, pending-diff identity, manifest revision>
Executor: <coding harness, model and effort; use the applicable recommendation from §5>
Outcome: <one concrete migration result>
Owns: <exact writable files/symbols; shared files reserved to the integration owner>
Inputs: <specific manifest rows, ledger addresses, oracle sections, source/test ranges>
Preserve: <state, ordering, direct/virtual calls and fixture assumptions>
Permitted changes: <named retail corrections or registration effects>
Deferred: <exact dependencies not completed here and their removal step>
Checks: <cheap checks, focused test prefixes, required final gate>
Done: <observable and structural acceptance>
Handoff: <changed files, results, open question, next command/action>
```

Reference source ranges as retrieval hints, not immutable line numbers. Addresses, symbols,
manifest keys and commit identity survive file moves more reliably.

### 6.3 Start and finish a session

At session start:

1. Read root instructions, `progress.md`, the active packet and this plan's active step/gate.
   Read nested instructions when entering their directories.
2. Check branch, commit and working-tree diff against the handoff. Account for edits made since
   it was written before continuing; do not assume another harness left a clean tree.
3. Load only the relevant manifest rows, ledger/index entries and oracle sections. Retrieve
   decompilation only for unresolved questions and use disassembly when it is damaged.
4. Confirm the previous result paths and run the next necessary check; do not rerun expensive
   successful gates just to reconstruct confidence after a context reset.

At session end or before changing harness:

- Save all files and record the actual tree/diff identity.
- State what is complete, what is edited but unverified, and whether the tree currently builds.
- Record commands and outcomes, including failures and their unresolved cause.
- Name the next concrete action and the evidence it requires.
- Mark an unfinished step as in progress. Do not tick a story, remove a residue row or weaken an
  expectation because a session is ending.

Keep a handoff to roughly a screen of actionable state with links. If needed, save a patch and
source hashes in the work root for recovery. Do not stash, reset, clean or overwrite the checkout
as a routine handoff operation. Step 4 can remain saved and uncommitted across sessions while its
single integration owner continues the atomic change.

### 6.4 Parallel work and resource ownership

Implement and integrate one step at a time, with one active coding agent editing the integration
checkout. That integration owner controls shared headers, registry/bindings, generators'
integration points, overlay updates, the current manifest phase and runtime builds. Ownership can
transfer between sessions, models or harnesses through an explicit saved handoff: the previous
owner stops editing before the next owner resumes. This does not require one uninterrupted session.

Useful independent packets include retail call-edge review, factory evidence, fixture mapping,
and Python work in disjoint files after schema agreement. Read-only reviews can run during
compilation. Workers return evidence, unresolved cases, exact patches and test results; they do
not all regenerate the shared ledger or edit the common NPC header.

Use isolated branches/checkouts for independent edits and review against their starting commit
before integration. A worktree isolates tracked source, not the shared work root, editor process,
generated assets or report paths. Serialize operations that contend on those resources and use
the repository's existing leases. Do not run another source mutation in the integration checkout
while the tree being built/tested is supposed to remain fixed.

### 6.5 Efficient retrieval

- Keep only the active packet, its invariants and immediate dependencies in working context.
- Search by symbol/address first; read bounded source sections and individual test cases.
- Query large generated ledgers for selected rows instead of repeatedly loading whole files.
- Pass concise evidence references between workers, not copied conversations or entire reports.
- Refresh stale references after moves through the manifest; do not repeat the retail recovery.
- Begin a new session after a validated packet or coherent saved boundary. A long atomic commit
  does not require a single long session.

## 7. Completion and remaining behaviour

The migration closes only when every live class, factory, field and carried override has the final
home required by the spec; temporary compatibility is gone; generation checks are reproducible;
and the accepted runtime/witness evidence refers to the final tree.

Keep three separate completion measures:

1. **Structure:** classes, inheritance, bindings and implemented-body ownership.
2. **Behaviour backlog:** live unported rules and named missing producers, with stable identities.
3. **Validation coverage:** tests and played witnesses actually exercised, plus unavailable paths.

A smaller stub log or fewer tests does not establish progress on measure 2. A factory smoke is not
a claim that a newly active boss behaves correctly. Changes to evidence or scope can correct the
inventory, but must be recorded separately from implemented rows.

Story 8 receives the final tree and residue after step 11. Do not open a competing implementation
lane on the shared headers during this migration; independent retail research can continue.

## Appendix A — final live class inventory (56)

The table preserves the reviewed class and factory inventory from 2026-09-24. Step 0 verifies it
against pinned retail inputs. The 21 dead classes remain listed in `population.md` and the
census, without port classes.

**Port base means the final base**, not the temporary step-2 C++ shape. Step 2 introduces 44 new
species types plus the existing Troika type; aliases yield 45 ordinary-NPC classnames. The ten
deferred classes land in steps 7–10.

Own bodies counts ledger slots filled by the class, not necessarily unique function addresses.
Own words counts its top-level layout dispositions. The two base rows use the 135/253 declaring-
layer counts from the flattened Troika layout; the previous zeroes were an inventory error.
Implementation and binding counts must be derived from the manifest, not inferred from this table.

| Retail class | Port class | Port base | Classnames (retail factories) | Own bodies | Own words | Step |
|---|---|---|---|---:|---:|---:|
| `CAI_BaseNPC` | `FElysiumNpcBase` | `FElysiumScriptedCharacter` | none (abstract, or built by code) | 285 | 135 | 5 |
| `CAI_BaseNPCTroika` | `FElysiumNpc` | `FElysiumNpcBase` | none (abstract, or built by code) | 170 | 253 | 2 |
| `CAI_TestHull` | `FElysiumNpcTestHull` | `FElysiumNpcBase` | none (abstract, or built by code) | 6 | 0 | 10 |
| `CCineNPC` | `FElysiumScriptedSequence` | `FElysiumNpcBase` | `scripted_sequence` | 12 | 25 | 9 |
| `CNPCMaker` | `FElysiumNpcMaker` | `FElysiumNpc` | `npc_maker` | 17 | 19 | 8 |
| `CNPC_VAnimal` | `FElysiumNpcAnimal` | `FElysiumNpc` | `npc_VAnimal` | 20 | 5 | 2 |
| `CNPC_VBaseBoss` | `FElysiumNpcBaseBoss` | `FElysiumNpc` | none (abstract, or built by code) | 5 | 1 | 2 |
| `CNPC_VCamera` | `FElysiumNpcCamera` | `FElysiumNpc` | `npc_VCamera` | 54 | 0 | 2 |
| `CNPC_VHuman` | `FElysiumNpcHuman` | `FElysiumNpc` | `npc_VHuman` | 19 | 0 | 2 |
| `CNPC_VMingXiaoTentacle` | `FElysiumNpcMingXiaoTentacle` | `FElysiumNpc` | `npc_VMingXiaoTentacle` | 38 | 16 | 2 |
| `CNPC_VNewscaster` | `FElysiumNpcNewscaster` | `FElysiumNpc` | `npc_VNewscaster` | 27 | 6 | 2 |
| `CNPC_VPlaceholder` | `FElysiumNpcPlaceholder` | `FElysiumNpc` | `npc_VPlaceholder` | 24 | 0 | 2 |
| `CPayphone` | `FElysiumNpcPayphone` | `FElysiumNpc` | `npc_payphone` | 23 | 0 | 2 |
| `CCineAI` | `FElysiumAiScriptedSequence` | `FElysiumScriptedSequence` | `aiscripted_sequence` | 5 | 0 | 9 |
| `CCineAISchedule` | `FElysiumAiScriptedSchedule` | `FElysiumScriptedSequence` | `aiscripted_schedule` | 14 | 3 | 9 |
| `CNPCMaker_Fleshpile` | `FElysiumNpcMakerFleshpile` | `FElysiumNpcMaker` | `npc_maker_fleshpile` | 12 | 0 | 8 |
| `CNPCMaker_Zombie` | `FElysiumNpcMakerZombie` | `FElysiumNpcMaker` | `npc_maker_zombie` | 10 | 3 | 8 |
| `CNPC_VDog` | `FElysiumNpcDog` | `FElysiumNpcAnimal` | `npc_VDog` | 19 | 0 | 2 |
| `CNPC_VScurrying` | `FElysiumNpcScurrying` | `FElysiumNpcAnimal` | `npc_VScurrying` | 14 | 9 | 2 |
| `CNPC_VZombie` | `FElysiumNpcZombie` | `FElysiumNpcAnimal` | `npc_VZombie` | 30 | 10 | 2 |
| `CNPC_VMingXiao` | `FElysiumNpcMingXiao` | `FElysiumNpcBaseBoss` | `npc_VMingXiao` | 50 | 29 | 2 |
| `CNPC_VTzimisce` | `FElysiumNpcTzimisce` | `FElysiumNpcBaseBoss` | `npc_VTzimisce` | 61 | 17 | 2 |
| `CNPC_VTzimisceHeadClaw` | `FElysiumNpcTzimisceHeadClaw` | `FElysiumNpcBaseBoss` | `npc_VTzimisceHeadClaw` | 26 | 5 | 2 |
| `CNPC_VTzimisceRunner` | `FElysiumNpcTzimisceRunner` | `FElysiumNpcBaseBoss` | `npc_VTzimisceRunner` | 31 | 5 | 2 |
| `CNPC_VWerewolf` | `FElysiumNpcWerewolf` | `FElysiumNpcBaseBoss` | `npc_VWerewolf` | 49 | 36 | 2 |
| `CNPC_VCameraSecurity` | `FElysiumNpcCameraSecurity` | `FElysiumNpcCamera` | `npc_VCameraSecurity` | 5 | 2 | 2 |
| `CNPC_VGuard1` | `FElysiumNpcGuard1` | `FElysiumNpcHuman` | `npc_VGuard1` | 15 | 2 | 2 |
| `CNPC_VHumanCombatant` | `FElysiumNpcHumanCombatant` | `FElysiumNpcHuman` | `npc_VHumanCombatant` | 12 | 0 | 2 |
| `CNPC_VPedestrian` | `FElysiumNpcPedestrian` | `FElysiumNpcHuman` | `npc_VDialogPedestrian`, `npc_VPedestrian` | 20 | 4 | 2 |
| `CNPC_VTaxiDriver` | `FElysiumNpcTaxiDriver` | `FElysiumNpcHuman` | `npc_VTaxiDriver` | 16 | 1 | 2 |
| `CNPC_VVampire` | `FElysiumNpcVampire` | `FElysiumNpcHuman` | `npc_VVampire` | 9 | 0 | 2 |
| `CNPC_ProneDialog` | `FElysiumNpcProneDialog` | `FElysiumNpcHumanCombatant` | `npc_VMercurio`, `npc_VProneDialog` | 2 | 0 | 2 |
| `CNPC_VCop` | `FElysiumNpcCop` | `FElysiumNpcHumanCombatant` | `npc_VCop` | 23 | 6 | 2 |
| `CNPC_VGhoulCroucher` | `FElysiumNpcGhoulCroucher` | `FElysiumNpcHumanCombatant` | `npc_VGhoulCroucher` | 24 | 9 | 2 |
| `CNPC_VHumanCombatPatrol` | `FElysiumNpcHumanCombatPatrol` | `FElysiumNpcHumanCombatant` | `npc_VHumanCombatPatrol` | 8 | 0 | 2 |
| `CNPC_VHunter` | `FElysiumNpcHunter` | `FElysiumNpcHumanCombatant` | `npc_VHunter` | 16 | 1 | 2 |
| `CNPC_VSabbatGunman` | `FElysiumNpcSabbatGunman` | `FElysiumNpcHumanCombatant` | `npc_VSabbatGunman` | 8 | 0 | 2 |
| `CNPC_VYukie` | `FElysiumNpcYukie` | `FElysiumNpcHumanCombatant` | `npc_VYukie` | 15 | 1 | 2 |
| `CNPC_VRat` | `FElysiumNpcRat` | `FElysiumNpcScurrying` | `npc_VRat` | 18 | 0 | 2 |
| `CNPC_VBach` | `FElysiumNpcBach` | `FElysiumNpcVampire` | `npc_VBach` | 26 | 23 | 2 |
| `CNPC_VBrujah` | `FElysiumNpcBrujah` | `FElysiumNpcVampire` | `npc_VBrujah` | 6 | 0 | 2 |
| `CNPC_VGargoyle` | `FElysiumNpcGargoyle` | `FElysiumNpcVampire` | `npc_VGargoyle` | 35 | 5 | 2 |
| `CNPC_VHengeyokai` | `FElysiumNpcHengeyokai` | `FElysiumNpcVampire` | `npc_VHengeyokai` | 35 | 14 | 2 |
| `CNPC_VLasombra` | `FElysiumNpcLasombra` | `FElysiumNpcVampire` | `npc_VLasombra` | 8 | 1 | 2 |
| `CNPC_VManBat` | `FElysiumNpcManBat` | `FElysiumNpcVampire` | `npc_VManBat` | 22 | 21 | 2 |
| `CNPC_VPlayerController` | `FElysiumNpcPlayerController` | `FElysiumNpcVampire` | `npc_VPlayerController` | 11 | 0 | 7 |
| `CNPC_VVampireBoss` | `FElysiumNpcVampireBoss` | `FElysiumNpcVampire` | `npc_VVampireBoss` | 14 | 9 | 2 |
| `CNPC_VFrenzyShadow` | `FElysiumNpcFrenzyShadow` | `FElysiumNpcPlayerController` | `npc_VFrenzyShadow` | 31 | 2 | 7 |
| `CNPC_VWolfMorph` | `FElysiumNpcWolfMorph` | `FElysiumNpcPlayerController` | `npc_VWolfMorph` | 14 | 2 | 7 |
| `CNPC_VAndreiBlood` | `FElysiumNpcAndreiBlood` | `FElysiumNpcVampireBoss` | `npc_VAndreiBlood` | 25 | 10 | 2 |
| `CNPC_VAsianVampire` | `FElysiumNpcAsianVampire` | `FElysiumNpcVampireBoss` | `npc_VAsianVampire` | 22 | 6 | 2 |
| `CNPC_VChangBros` | `FElysiumNpcChangBros` | `FElysiumNpcVampireBoss` | `npc_VChangBros` | 24 | 12 | 2 |
| `CNPC_VSabbatLeader` | `FElysiumNpcSabbatLeader` | `FElysiumNpcVampireBoss` | `npc_VSabbatLeader` | 36 | 15 | 2 |
| `CNPC_VSheriffMan` | `FElysiumNpcSheriffMan` | `FElysiumNpcVampireBoss` | `npc_VSheriffMan` | 25 | 12 | 2 |
| `CNPC_VChangBrosBlade` | `FElysiumNpcChangBrosBlade` | `FElysiumNpcChangBros` | `npc_VChangBrosBlade` | 7 | 0 | 2 |
| `CNPC_VChangBrosClaw` | `FElysiumNpcChangBrosClaw` | `FElysiumNpcChangBros` | `npc_VChangBrosClaw` | 7 | 0 | 2 |


## Appendix B — reference measurements and remaining estimates

These are starting observations, not hard acceptance counts or session-size targets.

| Observation | Source / treatment |
|---|---|
| 1,268 Substrate, 14 Content, 1 PlayerWorld completions | Saved reports in §3.1; confirm relevant source/content provenance |
| Header rebuild: 70 / 96 / 117 compiles; 156 / 366 / 1,123 seconds | 2026-09-23 build logs; measure the new rehearsal rather than extrapolating a fixed build budget |
| 171 direct NPC-header includers, 77 tests | Original source inventory; refresh before choosing batches |
| 228 dead species hand bodies, 195 override rows | Original delete-list/owner join; confirm shared live targets before deleting |
| 125 address arms, 42 member-pointer rows, 68 lookups, 50 class checks, 54 guard sites, 21 numbered methods | Original non-test scan; classify actual sites and deferred consumers |
| About 590 live species rule bodies: 496 slot bodies and 94 helpers | Original verdict/source join; deduplicate definitions and include present/mechanism bodies separately |
| 309 live species layout words; 230 datamap rows; 42 KEY/INPUT rows | Original census/replay join; partition by introduction/fold step and count aliases/flags deliberately |
| About 174 species words already on the flat NPC | Original declaration scan; component ownership and missing rows require reconciliation |
| Base layers: 135 + 253 word dispositions; about 172 + 157 bodies and 263 unnamed helpers | Original census/overlay analysis; caller-based helper assignment is only a proposal |
| Generated slot body count: headline 268, old owner subtotal 269 | Unreconciled; step 0 inventory and step 6 reconciliation must explain the difference |
| Tutorial: 14 makers, 51 scripted sequences; hub: 48 makers, 30 scripted sequences, 4 AI sequences, 1 AI schedule | Exported entity sidecars; use to select witnesses |
| Earlier map-load run: 37 of 108 green | Historical coverage limit, not a permanent allowlist; record current playable paths |
| Story 8 pass R: 295 rules, 227 species-owned, 213 species slot rows | Existing tracker cross-check; refresh target references after migration |

Estimate effort after the rehearsal from actual manual exceptions, header fan-out and validated
packets. Optimize repeated work and build contention; do not impose a model-specific token budget,
session count or fixed number of builds.
