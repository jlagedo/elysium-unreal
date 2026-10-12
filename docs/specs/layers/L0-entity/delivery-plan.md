# L0 delivery plan — subsystem closure

Replanned 2026-10-11 at the owner's request. This is the active L0 scheduling and ownership
plan. It supersedes the execution order, readiness labels and estimates in the original
`runs.md`, story table and briefs. Those files retain retail evidence and traceability.
The replan itself did not resume the Goal or certify implementation. The owner subsequently
authorized main to start this flow through the revised [Goal](goal.md).

## Authority and scope

1. Follow `AGENTS.md`, `docs/vision.md` and the owner's current instructions. Unreal supplies
   engine services; the substrate reproduces VtMB's observable rules and authored contracts.
2. **Retail-save importing is a non-goal.** No external-save decoder, dry-run import,
   retail-to-native roster matching or import commandlet is an L0 closing requirement.
   Native Elysium save/load, travel and restoration of gameplay state remain required.
   Save files are disposable: no old-version migration or compatibility promise.
3. Preserve the core-first layer order. Higher-layer implementations do not become prerequisites
   because a parked beat spec appended them to an L0 brief. Keep the actual L0 producer and
   register the higher-layer consumer; never substitute an empty same-layer producer.
4. The old plan also contains non-core entity families and zero-row content work. They remain
   explicitly accounted for in D09 and the coverage map, rather than silently disappearing.
   Resolve their core dependency or explicit scope disposition before L0 closure. A low map
   count alone does not permit deleting an obligation from the existing gate.
5. Reuse confirmed walks and accepted code. Retain historical run receipts. Reopen a behavior
   only on a failed integration contract, new evidence or an identified missing requirement.

The index is [delivery-map.tsv](delivery-map.tsv): every original brief has one primary owner,
milestone, disposition and proof obligation. A `higher-layer` row preserves a handoff, not an
implemented hook or completed higher-layer task. `excluded` means the owner's explicit
save-import exclusion. No audit implementation verdict is changed by this replan.

[service-contract-map.tsv](service-contract-map.tsv) also assigns all 77 entries of the existing
engine-replacement checklist. It distinguishes owner-level observable review from engine-only
mechanisms and single-player boundary review. These are planning classifications, not new
implementation verdicts; existing no-observer conclusions still need their review rationale.
Reuse the existing rationale when it is sound. A mechanism-only entry needs no new clone,
per-layout golden or repeated decompilation; its owning service carries any observable contract.

## What counts as a deliverable

A deliverable is a usable L0 service with its state, callers, lifecycle and tests connected.
It is not a new C++ module and does not require a folder refactor. One or more small runs build
each milestone. The milestone closes only when its complete observable contract passes.

For each requirement, record:

- **Observer:** shipped keyfield, input/output, script, authored asset value, native saved state,
  or measurable gameplay/feel rule. Name the retail address and consumer where known.
- **Ownership:** substrate rule versus Unreal service, including data conversion between them.
- **Contract:** inputs, outputs, state writes, failures, lifetime and event/clock phase.
- **Proof:** real producer, affected consumer, positive and negative cases, registered records
  and report paths. A probe that proves an engine setter was called is not consumer proof.
- **Coverage:** source story/brief/audit rows; any higher hook; remaining unsupported arms.

Source/IVP memory layout, allocator, topology, broadphase, solver, mixer, decoder and streaming
internals are not deliverables. Preserve observable identity/capacity/selection semantics where
gameplay uses them; do not confuse these with a requirement to reproduce their memory layout.
Numeric tolerances must be justified per output. They cannot hide changed branch decisions,
contact admission, callback order or gameplay timing. Naming an adapter Chaos proves nothing.

## Deliverables and milestones

Dependencies below are on **milestone closing contracts**, not whole subsystems. A bounded run
needs the actual producers it reads; it need not wait for unrelated acceptance work elsewhere
in a dependency milestone. No milestone closes until all its required inputs and witnesses pass.
These are planning gate IDs, not names of tests that already exist. Existing records come from
`delivery-map.tsv`; a fresh
packet must supply any missing integration records before it can claim completion.

| ID | Deliverable | Milestones and closing contract |
|---|---|---|
| D00 | Evidence and harness | H1: retain the completed harness/tooling baseline; add only instrumentation needed to observe a named remaining contract. |
| D01 | Entity state, lifetime and world clock | E1: construction, keyfields, identity, registries and base slots. E2: think contexts, deadlines, callback modes, spawn/register/remove and deletion fences. E3: complete world startup, frame dispatch and shutdown using the completed L0 services. |
| D02 | Physics bodies, queries and transforms | P1: real Chaos body inputs, identity, replacement and destruction. P2: collision/filter/query and material-result contracts. P3: shadow targets, solved poses, game-clock publication, bounds and parent transforms through the actual consuming loop. |
| D03 | Entity movement and touch | M1: motion/water/conveyor primitives, touch admission and enter/leave lifetime. M2: noclip, parented, custom, STEP, fly and toss rules. M3: linear/rotating push transactions, rollback, blocked feedback and complete simulation dispatch. |
| D04 | Sound emission | A1: authored tables, selection rules and Unreal resource lifetime. A2: typed emission, channel/stop/update behavior, origins, ambient/scheme activation and map retirement. |
| D05 | Gameplay sound stimuli | G1: identity, reserved/indexed slots, admission, retention and expiry. G2: actual L0 insertion/readback and saved pending state, with higher-layer producer/listener ownership explicit. |
| D06 | Entity I/O and world rules | Q1: variant conversion, names, global/rule/configuration data and base damage/ammo definitions. Q2: queue/timers/logic_auto, deterministic dispatch and restored pending deliveries. |
| D07 | Movers, doors and triggers | T1: trigger initialization, filtering, enter/exit/output and rearming. T2: mover/door activation, arrival, obstruction, knobs/switches, sound and output sequencing, plus saved phases. |
| D08 | Props, impacts and breakables | I1: prop/brush construction and authored shape/admission. I2: contact-to-damage/material/audio/stimulus rules and attribution. I3: break lifecycle, debris and fade, with correct removal and saved state. |
| D09 | Constraints, forces, ropes and physics content | C1: real endpoint/body resolution and constraint/rope lifetime. C2: limits, forces, break/disable/output/restore rules. C3: retained generic physics-asset bake obligation; separate bone/death/animation gameplay into its owning layers. |
| D10 | Entity-controlled effects | V1: retain and integrate completed light/sprite/particle/decal/blood/brush rules through current parent, I/O and Unreal rendering services; capture their L0 saved state. |
| D11 | Native persistence and travel | S1: native archive, block registry, identity/fixup and restore phases. S2: every L0 owner's state captured/restored with correct clocks and no event replay. S3: safe slot storage/admission, native cross-process load and map travel, followed by the complete L0 integration gate. |

### E1–E3: entity ownership and simulation

Keep base metadata and virtual defaults in L0 even when an NPC later overrides them. E1 does
not implement NPC makers, NPC input bindings, stealth sensing, dialogue ownership or combat
reaction logic. Private activity-name registration remains the generic L0 data contract; the
per-class combat/NPC producer is a separate handoff.

E2 supplies real scheduling to physics and I/O. Prove named versus primary callbacks, deadline
and stamp rules, current-context lifetime, callbacks that remove entities, and deferred physical
retirement. E3 integrates the movetype dispatcher only after its required M2/M3 arms exist.
There is no requirement to finish all E3 consumers before implementing the E2 clock service.

### P1–P3: the next foundation milestone

Use the [existing producer evidence and ownership correction](walks/L0-physics-producer.md),
the current code and accepted receipts. Do not restart the IVP construction walk.

- **P1:** spawn a real supported body through production data, inspect semantic parameters,
  enumerate/read velocity, replace/clear/remove it, and prove no stale callback or alias use.
  Separate model, static bbox and shadow-body caller arms. Existing selected-recipe save
  support is retained; an unsupported required body remains a named open case.
- **P2:** run real queries and contacts through group, pass-entity/owner, bilateral veto and
  damage-selector rules. Prove filter changes take effect at the required phase. Map start/all
  solid, fraction, normal, contents and surface meaning to actual consumer decisions. Authored
  material parsing is retained; selector-to-cooked-geometry association and representative
  material selection need consumer proof. A per-point IVP coefficient/layout is not assumed.
- **P3:** join submitted target, solved pose, shadow permissions/caps/thresholds, blocked
  feedback, think/simulate/sample/submit order, dirty bounds and local/absolute/parent frames.
  Use the real minimal L0 caller loop identified by the walk. The old r022↔r044/45 cycle is
  treated as one integrated contract across small runs, not as mutually completed prerequisites.
  If a specific STEP/pusher arm is required here, assign that arm explicitly to this milestone
  and retain its later full M2/M3 gate. Do not stub it, or require all movement to close first.

P2 also owns the minimum physical touch event producer required by filter mutation. The
2026-10-12 bounded recovery distinguishes native44 contact snapshots and226 trigger discovery
from100315c0/100316f0 physical StartTouch/EndTouch, their immediate/queued phase and event-driven
link lifetime. Resolve the backend recheck-to-game listener bridge before claiming setter
callback equivalence; backend pair insertion/deletion is not itself proof of a game event.
Join the complete reached1002f1e0 native predicate, not only Simple/SP query rules. This is a
specific M1 producer assigned to P2, not a prerequisite on full M1 or a change to Chaos ownership.
Full motion/trigger touch integration remains M1 after P3. See next-packet.md and
physics-interaction.md §Current-body recheck callback frontier; no runtime closure claimed.

P3 closes on an entity being parented, moved, solved, queried, teleported/unlinked and removed
without stale bounds, pose feedback loops or misplaced callbacks. It must exercise the consumer
that invalidated the earlier r022a attempt, not just the five formerly passing cache records.
Game timing and native solver stepping are different responsibilities: preserve observable
sample/publication timing without cloning the IVP scheduler. Compare at the same gameplay phase.

The current contact/frame/material discrepancies remain open until their effect on these
contracts is resolved. Do not invent an approved gameplay tolerance or repeat arbitrary native
trajectory matching without identifying its observer. Impact sound and damage are I2 consumers;
only a demonstrated initialization dependency can put their resource preparation before P3.

### A1–A2 and G1–G2: two distinct sound services

Audio emission owns authored manifest precedence, sound names, defaults, intervals, wave/gender
selection, channels/flags and emission lifecycle. Unreal owns asset loading, decompression,
streaming, voices and mixing. The bounded native-audio packet committed during this review as
`8f8aea1b`; reuse its A1/A2 evidence and `walks/L0-sound-producer.md` checkpoint. It does not close
generic emission or the impact consumer, and is not a reason to replay the completed packet.

Prove table resolution and native playback separately, then together. Review whether one missing
asset or unavailable audio device may block unrelated sound resolution or world activation:
that failure/timing rule needs a retail observer. A global readiness barrier is not justified
merely because Unreal exposes precache APIs. Native-audio proof must actually enable audio;
the ordinary `-nosound` regression run cannot certify readiness or playback.

The full audio corpus bake belongs to A1 content evidence, not G1 sound-record lookup. Reuse
unchanged bake receipts; do not impose a stale hard-coded asset count or rebake on every run.
Dialogue timing, lip interpretation and player/NPC-specific emission rules stay in L2–L5.

The gameplay stimulus service owns the sound-record behavior consumed by hearing: stable
identity, bounds, reserved slots, capacity consequences, expiry and insertion order. Unreal
audio playback is not that service. Preserve these rules using native storage; Source offsets
`+0x464` and stride `0x2c` are oracle notation, not required addresses in an arena assertion.
Test full/expired/absent-pool cases through actual service initialization and insertion. The old
128-event bus is not accepted as equivalent merely because the lookup row is small. L3 footsteps
and L4 listening/program/alert logic are explicit consumers; they do not block the L0 API gate.

### Q1–Q2, T1–T2 and I1–I3: observable entity chains

Q2 closes delayed delivery, tie order, caller/activator identity, conversion/failure behavior,
timer draw/deadline order, and callback-driven deletion through the real E2 scheduler. Preserve
the L0 transport contract of queued Python results; Python execution is L5. Higher-layer ammo
use and combat calculations do not belong in the L0 definition table.

T1/T2 join queries, movement, I/O and sound: admission/rearm, use/lock state, mover arrival,
blocked transitions, damage callbacks, knob/switch state and outputs. Test at least an ordinary
successful operation, a rejected/blocked case, deletion while active and native restore without
duplicate outputs. Rotating sweeps and pusher rollback remain M3 contracts used here.

I2 converts actual engine contacts into retail eligibility, attribution, damage packets and
authored sound/stimulus requests. It does not implement L2 health/death or L3 grabbing/throwing.
I3 proves the break decision and resulting L0 body/debris lifetime. A probe-injected callback
does not replace a real contact-to-consumer integration test.

### C1–C3, V1 and S1–S3: completion without cross-layer expansion

Chaos owns constraint and rope mechanics. L0 owns authored endpoints, parameters, limits,
activation, break/disable notifications and restore semantics. C3 retains the existing generic
asset-build obligation over source physics data. Hit-bone selection, death-to-ragdoll transition
and animation/NPC ownership are handed to their layers, with the exact L0 service still tested.
The low-reach `phys_*`/`phys_animlink` family stays visible as a retained scope-review item;
it cannot silently become a prerequisite for every core body or silently disappear from the gate.

V1 reuses the accepted effects work. Rerun its integration with corrected transforms and I/O;
implement only demonstrated gaps. Rendering, skinning and visual particle mechanics stay Unreal.

S1 is the archive/block foundation; S2 is accumulated with each subsystem, and S3 owns slot
storage/admission and final integration. Do not make a file-storage/validation project a
prerequisite for a body velocity query or an in-memory body snapshot contract.
Every state owner supplies its own semantic save projection and restore behavior before its
deliverable closes. The final save milestone does not rediscover all those fields. Prove
references/tombstones, relative times and sentinels, RNG draw position, pending events, sleeping
bodies, disabled/broken constraints and callbacks during retirement where those contracts apply.
Classify state as saved, reconstructed or deliberately reset according to the retail contract;
do not serialize every cache, contact/manifold, allocator or other engine-internal object.
Use native saves made by this build. Invalid/truncated/unsupported native saves must fail safely;
do not create content migration, cross-version compatibility or external-save conversion work.
Higher layers register their own blocks later. L0 does not wait for their complete saved game.

## Dependency graph and execution order

The machine-readable graph is [milestones.tsv](milestones.tsv). A milestone with a retained
completed implementation still needs its named integration evidence; the graph is not a rewrite
queue. Runtime callbacks into later consumers do not create a prerequisite on their whole
subsystem: preserve the lower service and the declared hook/callback contract.

1. **Reconcile the checkpoint and accept the plan.** Main committed the sound packet as `8f8aea1b` during
   this review. Retain its files, final gates and checkpoint; this planning pass ran no engine
   jobs and did not independently repeat that acceptance. The owner has authorized resume
   under `goal.md`: main reviews and commits these planning changes before the next code run.
2. **Account for existing foundations.** Reuse H1, E1 and S1 accepted work. Resolve only named
   missing producer/proof requirements; do not replay completed r001–r021/r029/r030 by default.
3. **Close D02 next:** P1 → E2/P2 → P3, one run at a time. Retain the completed bounded A1 work;
   do not turn all sound or impact feedback into a prerequisite for D02.
   [next-packet.md](next-packet.md) provides a concrete base-velocity candidate, observed still
   stubbed during this review, with a bounded oracle/producer check before dispatch.
4. **Build the consumer services in dependency order:** M1/M2, A2, G1 and Q1/Q2, then M3.
   Select the ready milestone that closes the nearest active deliverable; do not round-robin
   between subsystems merely because their packets are small.
5. **Close entity chains:** T1/T2, I1/I2/I3, G2, C1/C2/C3 and V1 as their actual inputs become
   ready. Integrate E3. Account for every retained scope-review item before final closure.
6. **Close S2/S3 and L0:** native restore/travel and the unchanged behavioral acceptance gates
   in `spec.md`, with explicit scope exclusions/handoffs rather than lost requirements.

## Serial execution contract

Sol Medium coordinates; a fresh Sol High coding worker handles one bounded run. One worker and
one run at a time. This document describes future main-thread execution; the planning side
conversation does not create or communicate with subagents.

Before dispatch, the coordinator creates a short packet containing:

1. Deliverable/milestone and the exact closing requirement advanced; mapped source brief IDs.
2. Observer and accepted retail evidence sections; the exact unknown question if recovery is needed.
3. Required producer status, permitted files and the Unreal service seam.
4. Expected observable results, negative cases and actual test/arena record names.
5. The explicit stop boundary and the remaining requirements after this run.

Old briefs are reference inputs, not direct dispatch instructions. Their repeated “read each
body” wording does not require re-walking a confirmed chain. Retrieve the relevant section and
verify it applies to the current code/corpus; reopen only an actual gap or contradiction.

Workers run build and focused affected tests/records. The coordinator independently reviews,
builds, runs the default tier and the run's/affected stories' arena records, checks regression
against the previous gate, then commits only reviewed paths and checkpoints. Never push. Apply
the arm tier at story closure per `AGENTS.md`; preserve the full L0 closing gates. Do not repeat
unrelated content bakes, whole-arena, doctor runs or forensic receipt copies without a changed
input, failure or relevant requirement. One authoritative report reference per gate is enough.

A run may close a complete adapter contract without closing its original coarse run. Report
that precise result; do not manufacture a story completion or treat an internal diagnostic as
a subsystem. A diagnostic packet names its hypothesis, observable, exit condition and the next
implementation decision. If its result adds no decision, do not dispatch another similar probe.

On a new same-layer dependency: name the producer and consumer, classify engine mechanism versus
observable rule, update the milestone graph and packet boundary, then continue routine work.
On an engine-internal difference without an observer: retain research evidence, remove it from
the implementation prerequisite list. On an unavoidable observable difference or genuine scope
change: bring the owner the faithful behavior, alternatives and consequence. Do not escalate
routine API choice, hook registration, faithful threshold implementation or missing test repair.

## Progress and context budget

The complete mapping accounts for 264 briefs: 244 L0 assignments, 12 higher-layer remainders,
four retained scope-review items and four excluded import-only briefs. All 165 original stories
remain traceable. These counts are scope accounting, not a current implementation-progress score.

The original 165 stories / 264 briefs / 88 runs and 23 accepted runs are historical identifiers
and evidence, not a percentage, current remaining-run count or duration estimate. No new ETA is
derived from retail byte size. The active map distinguishes exclusions and higher-layer work.

Maintain [checkpoint.md](checkpoint.md) as a compact entry point: accepted HEAD, dirty/in-flight
ownership, active milestone, completed contracts with receipts, exact first unmet condition,
next packet and owner decisions. Link old walks/reports; do not paste the entire chronological
`progress.md` or `runs.md` into each worker's context. Keep at most one active implementation
milestone, allowing only explicitly named producer work needed to close it.

At each accepted run report: **contract closed → milestone requirements still open → next
closing witness**. At subsystem closure reconcile all mapped source rows and saved-state/hook
obligations. A new deliverable dashboard must not reset or erase earlier implementation credit.

The [planning review](plan-review.md) records coverage checks and the limits of this replan.
