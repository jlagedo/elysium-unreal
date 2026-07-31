# Retail Animation Reverse-Engineering Roadmap

## Purpose and ownership

This is the detailed tracker for the private instrument used to understand how
the fixed, supported retail VtMB build loads character resources and transforms
them through animation, scene, secondary-motion, facial, lip-sync, deformation,
and rendering stages.

The goal is to recover enough of that behavior to reproduce it in Elysium. This
is not a public capture SDK, a production telemetry system, a general game
decompiler, or a compatibility layer for multiple game builds.

`docs/project/roadmap.md` owns project priority and the roll-up rows `0.10`,
`RE32`, and `RE33`. This file owns the detailed capture and experiment tasks.
Confirmed VtMB facts belong in the relevant `docs/vtmb/` document. Specifications
may be tracked under `research/cases/`. Game-derived binaries, captures,
decompilation, indexes, and reports stay under `$ELYSIUM_WORK_ROOT/research`.

## Fixed scope

The supported target is one exact owner-controlled retail installation. The
executable and module bytes are the contract:

- probe activation requires the expected module name, PE identity, file size,
  SHA-256, target RVA, and instruction bytes;
- an unknown or modified binary is left untouched;
- a new target hash is research work, not a schema migration;
- the reader and writer may change together whenever a better experiment needs
  different data;
- old captures may be discarded when they have yielded their facts and
  regressions.

The instrument follows the engine's character path end to end:

1. resource path resolution and file loading;
2. MDL, included-model, sequence, animation, scene, expression, lip, and physics
   resource construction and caching;
3. skeletal channel decode, sampling, selection, blending, layering, remapping,
   procedural work, and hierarchy construction;
4. scene placement, root/entity movement, lifecycle, and physical state;
5. cloth, hair, accessory, jiggle, and animation-to-ragdoll behavior;
6. expression, eyelid, phoneme, and amplitude-mouth evaluation;
7. final bone matrices, flex weights, vertex deformation, and render submission.

It does not need to support other games, later Source branches, unknown retail
patches, public consumers, remote collection, long-term capture compatibility,
or instruction-by-instruction emulation.

## Operating rules

1. **Raw evidence precedes semantics.** A probe records the bytes, registers,
   stack words, pointer values, and bounded pointed-to spans available at a
   debugger-validated boundary. Names and decoded fields are analyzer
   hypotheses.
2. **Unknown data is retained.** Do not replace a structure with the understood
   99 percent. Preserve the uninterpreted bytes around known values, including
   flags and padding that may explain rare behavior.
3. **Capture recipes are editable.** A recipe identifies exact module hashes,
   hook RVA, entry or exit, register snapshot, stack window, memory spans,
   filters, trigger, and sampling. It is experiment configuration, not a stable
   data contract.
4. **Capture is targeted.** Raw does not mean whole-process dumps. Spans are
   bounded and selected at known resource, evaluator, simulation, or render
   boundaries so the trace remains attributable and safe.
5. **Callbacks stay small.** Verify pointers, copy into preallocated bounded
   storage, account for truncation or drops, and return. No decoding, semantic
   validation, compression, index building, or blocking file I/O in the game
   thread.
6. **The trace is append-only and recoverable.** Each record carries enough
   length and timing information for a scanner to skip, recover, and report a
   partial tail. There is no per-record schema registry or compatibility
   promise.
7. **Indexes are disposable.** Build file-offset and experiment-specific indexes
   offline. SQLite is acceptable through Python's standard library when it
   helps analysis; it is never part of the in-process capture path.
8. **Static bytes are deduplicated.** Large immutable resources may be stored
   once by content hash and referenced by capture records.
9. **Optimize measured pain only.** Preallocation, batching, chunk size,
   deduplication, and compression are justified by observed callback cost,
   memory pressure, writer backlog, or storage volume.
10. **Less code wins.** A dependency, abstraction, control, or test that does not
    make the retail evidence safer, faster, easier to query, or more decisive is
    removed.

## Evidence gate

A reverse-engineering task closes only when:

1. the exact executable and module hashes and hook recipe are recorded;
2. the trace reports captured, written, dropped, truncated, and incomplete
   counts;
3. inputs and outputs at the relevant stage are available as raw bytes;
4. an offline analyzer can replay the trace without reading the live process;
5. the conclusion survives a repeat capture or independent byte comparison;
6. the confirmed fact is written in its owning `docs/vtmb/` document;
7. the recovered rule has a game-independent regression or local hash-gated
   oracle comparison.

Synthetic tests prove process safety and recorder mechanics. They do not prove
retail behavior. Every task that claims an engine rule requires a retail gate.

## Capture shape

```mermaid
flowchart LR
    A["Exact-build launcher or attach"] --> B["Hash-pinned module profile"]
    B --> C["Editable raw capture recipes"]
    C --> D["Entry/exit hooks and bounded memory copies"]
    D --> E["Preallocated bounded queue"]
    E --> F["Single append-only trace writer"]
    F --> G["Raw trace plus static blob store"]
    G --> H["Python recovery scanner"]
    H --> I["Rebuildable file-offset indexes"]
    I --> J["Experiment analyzers"]
    J --> K["Standalone evaluator and VtMB facts"]
```

Keep the writer in the injected host until retail measurements show that an
external collector is necessary. Do not introduce FlatBuffers, MCAP,
Boost.Interprocess, a schema registry, or a database in the callback path. The
current direct layouts may remain only as short-lived readers for existing
`ELPOSE2` and `ELANIM2` evidence; the generic raw trace supersedes them
incrementally.

## Current baseline and audit

| Capability | Evidence | Decision |
|---|---|---|
| Read-only final palette polling | Retail captures and analyzers exist | Retain as a lightweight cross-check |
| Attach-time x86 injection | Existing direct-save path | Retain |
| Suspended launch, bootstrap, supervision, attach fallback | Synthetic native tests | Retain; perform retail gates as experiments need them |
| Module observation and exact binary profiles | Synthetic/native implementation | Retain exact identity and fail-closed activation |
| Shared instruction-aware hook backend | Synthetic/native tests and current retail hook use | Retain unless a real target defeats it |
| DrawModel, BASE, and FINL capture | Existing `ELPOSE2`/`ELANIM2` evidence | Retain as the first raw-trace vertical slice |
| Whole-scene and authored-pose analyzers | Existing Python tools | Retain and adapt to raw records |
| Stable record schemas and generated schema registry | No RE value for one fixed build | Removed |
| Strict experiment/session manifest validator | More control than the research needs | Removed; keep simple hashes, recipe, counts, and paths |
| Numerical-policy contract | Premature before the evaluator stages are known | Removed; tolerances belong to individual oracle tests |
| FlatBuffers and pinned native source-package machinery | Compatibility platform without a consumer | Removed |
| Planned MinHook migration | Current backend already handles known targets | Cancelled; revisit only after a demonstrated hook failure |
| Planned IPC/MCAP/Boost/codec platform | No measured need | Cancelled; add the smallest measured fix if direct writing fails |
| Durable SQLite capture schema | Couples capture to current theories | Cancelled; indexes are offline and rebuildable |

Completed work is credited only for the part still needed by this strategy.
Launcher, injection, safety, identity, hook mechanics, and existing evidence stay
closed. Contract-platform work does not create a maintenance obligation.

## Program ladder

| Phase | Outcome | Depends on |
|---|---|---|
| CAP0 — Retained evidence and cleanup | Useful captures and safety code remain; compatibility machinery is absent | — |
| CAP1 — Exact-build access | The supported game launches or attaches, activates only exact profiles, and tears down safely | CAP0 |
| CAP2 — Raw recorder | Editable recipes capture bounded unknown bytes at useful rate into recoverable traces and rebuildable indexes | CAP1 |
| CAP3 — Resource path | Runtime resources are followed from requested path and disk bytes through constructed objects and caches | CAP2 |
| CAP4 — Skeletal evaluation | Compressed channels become final model/world bone matrices with timing, selection, blend, layer, and hierarchy behavior explained | CAP3 |
| CAP5 — Scene and movement | Placement, transform ownership, root/entity movement, events, interruption, and restoration are explained | CAP3, CAP4 |
| CAP6 — Secondary motion and physics | Procedural, jiggle, cloth, hair, accessory, collision, history, and ragdoll handoff are explained | CAP4, CAP5 |
| CAP7 — Face and lips | Scene/audio time becomes expression, eyelid, phoneme, amplitude-mouth, flex, and vertex state | CAP3, CAP5 |
| CAP8 — Final deformation and render | Every character contribution is correlated with final matrices, flexed vertices, and draw submission | CAP4–CAP7 |
| CAP9 — Reproduction closure | A standalone evaluator reproduces the controlled retail coverage and feeds Unreal | CAP3–CAP8 |

## Current front

**Immediate next slice: CAP2.1.**

Use the already known DrawModel, BASE, and FINL boundaries to prove one generic
raw record from callback to Python analyzer. Capture the original typed values
only as labeled spans alongside registers, stack, pointers, and unknown
neighboring bytes. Once that vertical slice is reliable, make resource loading
the first new engine investigation.

Work in this order:

1. CAP2.1–CAP2.5 — generic raw record, editable spans, bounded queue, chunked
   writer, and recovery scanner;
2. CAP2.6–CAP2.9 — filters, static-blob deduplication, offline indexes, and retail
   throughput measurement;
3. CAP3 — resource loading, object identity, includes, caches, and pointer-to-file
   provenance;
4. CAP4 — skeletal evaluation from compressed source bytes to final matrices;
5. CAP5 and CAP6 — scene/movement and secondary physics;
6. CAP7 — facial and lip processing;
7. CAP8 and CAP9 — final render correlation and standalone reproduction.

Do not build later infrastructure in anticipation of these phases. Add one
capture field or storage optimization when a concrete experiment requires it.

## CAP0 — Retained evidence and cleanup

- [x] **CAP0.1 Existing retail evidence remains readable.** `ELPOSE2` and
  `ELANIM2` captures, summaries, extractors, and pose-comparison tools retain
  their direct layouts long enough to serve as reference evidence.
- [x] **CAP0.2 Capture metadata is intentionally small.** Sessions record method,
  PID, exact module paths and hashes, binary profile IDs, target model where
  relevant, output paths, and captured/written/dropped/incomplete state. There
  is no validating manifest schema.
- [x] **CAP0.3 Compatibility machinery is out of scope.** The tool has no record
  registry, generated schema bindings, schema migration tests, FlatBuffers
  dependency, numerical-policy document, or strict experiment contract.
- [x] **CAP0.4 Existing analyzer value is preserved.** Whole-scene summaries,
  BASE/FINL comparisons, cinematic composition analysis, and palette
  comparisons remain available.
- [ ] **CAP0.5 Archive facts from superseded captures.** Before deleting a useful
  local trace, ensure its conclusions and regression evidence are stored in the
  owning documentation and tests.

## CAP1 — Exact-build access

- [x] **CAP1.1 Native x86 harness.** Win32 debug and release presets build the
  synthetic retail executable, named modules, launcher, injector, probe host,
  and tests with strict warnings.
- [x] **CAP1.2 Suspended launch.** The launcher constructs the retail command and
  environment, creates the process suspended, injects the host, waits for
  readiness, and resumes.
- [x] **CAP1.3 Attach fallback.** A deliberately non-owning attach path exists for
  experiments that cannot start from the launcher.
- [x] **CAP1.4 Supervision and partial finalization.** Owned children, timeout,
  crash, collector exit, Ctrl-C, and partial-finalization paths have synthetic
  coverage.
- [x] **CAP1.5 Module observation.** Loader work is separated from callback
  observation, and the host can enumerate and process already loaded modules.
- [x] **CAP1.6 Exact binary profiles.** Profile generation and native/Python
  lookup cover exact hashes, PE identity, RVAs, calling convention, signature
  bytes, and hook backend selection.
- [x] **CAP1.7 Fail-closed activation.** Unknown hashes, PE mismatches, bad
  signatures, and unsupported targets do not install hooks.
- [x] **CAP1.8 Shared hook backend.** Vtable replacement and instruction-aware
  inline detours are centralized and tested.
- [ ] **CAP1.9 Active-call teardown.** Uninstall waits for active callbacks and
  proves that no callback can touch destroyed recorder state.
- [ ] **CAP1.10 Retail lifecycle smoke.** On the supported owner build, launch,
  capture, stop, unload, and exit without a crash or stuck process, with exact
  hashes and zero dropped records recorded.

## CAP2 — Raw recorder

- [ ] **CAP2.1 Generic raw-record vertical slice.** At DrawModel, BASE, and FINL,
  record hook ID, entry/exit, QPC, thread ID, sequence, x86 registers, bounded
  stack bytes, arbitrary memory spans with original addresses and requested
  versus copied lengths, plus the current known pose buffers.
- [ ] **CAP2.2 Editable capture recipes.** Define a compact local recipe containing
  module profile, hook target, timing, register set, stack window, pointer
  expressions, span bounds, trigger, filter, and sampling. Unknown fields are
  allowed and expected.
- [ ] **CAP2.3 Safe span copying.** Validate readable pages, cap every span and
  record, catch access faults, report truncated/failed copies, and never chase
  recursive pointer graphs in a callback.
- [ ] **CAP2.4 Preallocated bounded queue.** Replace per-record heap allocation
  with fixed or pooled slots sized from the active recipe. Account separately
  for full-queue drops, oversize records, unreadable spans, and writer failures.
- [ ] **CAP2.5 Append-only chunked writer.** One writer emits length-delimited
  records and periodic chunk boundaries so a scanner can recover every complete
  record after interruption.
- [ ] **CAP2.6 Filters and triggers.** Filter by model checksum, entity/pointer
  identity, thread, call count, time window, scene, or a manually armed marker;
  support deterministic sampling for high-frequency hooks.
- [ ] **CAP2.7 Static blob store.** Store immutable resource bytes once by SHA-256
  and let records refer to hash, source path, offset, and length.
- [ ] **CAP2.8 Recovery scanner.** Python reports chunks, records, unknown hook
  IDs, copied spans, partial tails, corruption, and all loss counters without a
  semantic decoder.
- [ ] **CAP2.9 Rebuildable index.** Generate a file-offset index for hook, time,
  thread, model, entity, pointer, and blob hash. Add experiment-specific SQLite
  tables only when they speed a real query.
- [ ] **CAP2.10 Retail throughput budget.** Measure callback duration, queue
  occupancy, memory, write rate, trace growth, and game perturbation on a noisy
  scene. Tune slots, chunking, batching, deduplication, or compression only from
  those results.
- [ ] **CAP2.11 Legacy equivalence.** From one controlled run, the raw analyzer
  reproduces the useful `ELPOSE2`/`ELANIM2` BASE, FINL, DrawModel, matrix, model,
  entity, and count outputs.

## CAP3 — Resource loading and runtime identity

- [ ] **CAP3.1 File request boundary.** Capture requested path, search roots,
  caller, result, file size, and raw returned bytes for character-related
  resources.
- [ ] **CAP3.2 MDL construction.** Correlate MDL/TTH/TTZ/VTX/VVD requests with the
  runtime studio header, checksum, pointer graph, and immutable source blob.
- [ ] **CAP3.3 Pointer-to-file provenance.** For every runtime structure used by a
  later hook, identify source file, byte offset, copied/transformed region, and
  unknown neighboring bytes.
- [ ] **CAP3.4 Included models and virtual models.** Capture include resolution,
  animation banks, bone/sequence remaps, ownership, failure paths, and cache
  reuse.
- [ ] **CAP3.5 Sequence and animation caches.** Trace descriptor lookup, lazy
  decode, cache keys, cache lifetime, and invalidation from resource bytes to
  evaluator inputs.
- [ ] **CAP3.6 Scene and dialogue resources.** Follow VCD, DLG, scene animation,
  gesture, expression, and timing resources into runtime objects.
- [ ] **CAP3.7 Face and lip resources.** Follow expression tables, flex rules,
  vertex animation data, and `.lip` data into runtime objects and caches.
- [ ] **CAP3.8 Secondary-motion resources.** Locate procedural, jiggle, cloth,
  hair, accessory, collision, and ragdoll parameters and preserve their raw
  bytes.
- [ ] **CAP3.9 Resource identity oracle.** Given a runtime pointer observed in an
  evaluator or render hook, the offline index resolves the exact owner resource
  and source byte range.

## CAP4 — Skeletal evaluation

- [ ] **CAP4.1 State and time.** Capture sequence, cycle, playback rate, frame
  time, pause, seek, loop, transition, and accumulated state at evaluator entry
  and exit.
- [ ] **CAP4.2 Raw channel decode.** For the first mismatch, record compressed
  input bytes, offsets, flags, scale/bias, selected frames, interpolation
  fraction, and output position/quaternion.
- [ ] **CAP4.3 Sampling boundaries.** Recover frame count, endpoint, looping,
  clamping, negative rate, wrap, and zero-duration behavior.
- [ ] **CAP4.4 Sequence selection and blends.** Recover activity/sequence choice,
  blend grids, pose parameters, controller inputs, and missing-channel defaults.
- [ ] **CAP4.5 Layers and transitions.** Recover ordering and weights for base
  pose, overlays, gestures, crossfades, autoplay, scene layers, and interruption.
- [ ] **CAP4.6 Virtual-model remap.** Recover included-model bone and sequence
  mapping, donor bind use, masks, and absent-bone behavior.
- [ ] **CAP4.7 Controllers and procedural order.** Place bone controllers,
  procedural rules, IK-like work, and other post-decode adjustments in exact
  order.
- [ ] **CAP4.8 Hierarchy construction.** Recover parent traversal, split
  position/rotation inheritance, root handling, entity transform application,
  and final model/world matrices.
- [ ] **CAP4.9 Save/load and reset.** Recover serialized state, time restoration,
  teleport/reset behavior, and first-frame initialization.
- [ ] **CAP4.10 Skeletal evaluator oracle.** The standalone evaluator matches
  retail locals and final matrices across representative model, animation,
  layer, transition, and boundary cases.

## CAP5 — Scene placement and movement

- [ ] **CAP5.1 Scene actor binding.** Capture participant lookup, animation set,
  actor/understudy substitution, target entity, and initial state.
- [ ] **CAP5.2 Entry placement.** Recover stage anchors, local-to-world placement,
  yaw/origin rules, snapping, collision changes, and initial animation state.
- [ ] **CAP5.3 Per-frame transform ownership.** Identify whether scene, animation
  root motion, entity movement, navigation, or physics owns each transform
  component.
- [ ] **CAP5.4 Movement and events.** Recover movement extraction, event timing,
  footstep/gesture dispatch, and double-application prevention.
- [ ] **CAP5.5 Lifecycle boundaries.** Recover pause, seek, loop, cancellation,
  actor loss, completion, and map teardown behavior.
- [ ] **CAP5.6 Restoration.** Recover final placement, velocity, collision,
  movement mode, AI, animation, and physical-state restoration.
- [ ] **CAP5.7 Scene oracle.** Reproduce controlled entry, playback,
  interruption, completion, and cancellation trajectories.

## CAP6 — Secondary motion and physics

- [ ] **CAP6.1 Stage inventory.** Identify every shipped procedural, jiggle,
  cloth, hair, accessory, spring, constraint, collision, and ragdoll stage and
  its ordering relative to skeletal hierarchy and skinning.
- [ ] **CAP6.2 Inputs and persistent state.** Capture raw parameters, prior-frame
  state, delta time, bone transforms, velocities, gravity/wind, collision
  shapes, and reset flags.
- [ ] **CAP6.3 Solver updates.** Capture before/after state around each update and
  isolate integration, constraints, damping, limits, collision, and writeback.
- [ ] **CAP6.4 Initialization and discontinuities.** Recover spawn, visibility
  change, teleport, pause, seek, scene restart, save/load, and large-delta reset
  behavior.
- [ ] **CAP6.5 Animation/physics handoff.** Recover ragdoll entry, pose seeding,
  ownership transfer, blending, wake/sleep, and recovery.
- [ ] **CAP6.6 Secondary-motion oracle.** Reproduce representative cloth, hair,
  accessory, procedural, and handoff traces from captured initial state and
  inputs.

## CAP7 — Facial animation and lip sync

- [ ] **CAP7.1 Controller sources.** Capture model controllers, scene expression,
  dialogue state, eyelids, look state, phonemes, and audio amplitude before
  mixing.
- [ ] **CAP7.2 Expression evaluation.** Recover expression lookup, controller
  mapping, flex rules, ramps, clamping, defaults, and composition order.
- [ ] **CAP7.3 Vertex-animation decode.** Recover both shipped encodings from raw
  source bytes through flexdesc contribution.
- [ ] **CAP7.4 Eyelids and eyes.** Recover upper/lower lid controllers, blink,
  gaze coupling, and missing-data behavior.
- [ ] **CAP7.5 Lip timeline.** Recover `.lip` phoneme timing, interpolation,
  coarticulation, silence, seek, pause, rate, and line transitions.
- [ ] **CAP7.6 Amplitude mouth.** Recover waveform windowing, envelope, gain,
  clamps, controller selection, and composition with phonemes and expressions.
- [ ] **CAP7.7 Facial deformation oracle.** Match retail controller values,
  flexdesc weights, and representative final vertices through complete
  controlled lines.

## CAP8 — Final deformation and render

- [ ] **CAP8.1 Final bone path.** Correlate skeletal, scene, and secondary-motion
  outputs with the exact model/world and skin matrices consumed for a draw.
- [ ] **CAP8.2 Final flex path.** Correlate expression/lip outputs with flex
  weights and CPU/GPU vertex-deformation inputs.
- [ ] **CAP8.3 Draw identity.** Tie render submission to entity, model, body/skin,
  LOD, mesh, material, bone palette, flex state, and source resources.
- [ ] **CAP8.4 CPU/GPU boundary.** Determine the final CPU buffers and any
  transformations applied while uploading constants, streams, or shader inputs.
- [ ] **CAP8.5 Representative vertices.** Match selected final deformed vertices
  and diagnose the first stage where any mismatch appears.
- [ ] **CAP8.6 End-to-end trace.** For one controlled actor action, navigate the
  offline index from file request through runtime objects and every contributing
  stage to draw submission.

## CAP9 — Reproduction closure

- [ ] **CAP9.1 Standalone evaluator.** Engine-neutral code consumes original
  resources and recovered state to reproduce controlled retail outputs.
- [ ] **CAP9.2 Coverage suite.** Cover representative skeleton families,
  included-model cases, sparse channels, transitions, scenes, secondary
  systems, expressions, eyelids, lip lines, and discontinuities.
- [ ] **CAP9.3 Unknown-byte audit.** For each unexplained field or span, show it
  is invariant/no-op in shipped coverage or retain it as an explicit unresolved
  risk. Never silently discard it.
- [ ] **CAP9.4 Unreal handoff.** Unreal consumes the evaluator output before basis
  conversion/retargeting; presentation acceptance is separate from source
  equivalence.
- [ ] **CAP9.5 Tool retirement.** Keep only recipes, analyzers, fixtures, and
  capture mechanics still needed to reproduce or investigate failures. Delete
  one-off probes and obsolete readers after their evidence is secured.

## Priority experiments

1. **EXP-R01 — Raw BASE/FINL/DrawModel vertical slice.** Prove the generic record,
   safe span copy, writer, scanner, and analyzer against the retained outputs.
2. **EXP-R02 — MDL path to studio header.** Follow one actor's model request,
   source bytes, runtime object construction, included-model resolution, and
   pointer use at BASE/FINL/DrawModel.
3. **EXP-R03 — First skeletal mismatch.** Compare retail final matrices with the
   offline evaluator, then trace backward only to the first bad bone and stage.
4. **EXP-R04 — Theatre scene transform.** Follow one participant from VCD binding
   through entry placement, animation layers, root/entity movement, completion,
   and final draw.
5. **EXP-R05 — Secondary-motion discontinuity.** Capture a character accessory
   across steady motion, pause, teleport, restart, and save/load.
6. **EXP-R06 — Spoken theatre line.** Follow VCD/DLG/audio/lip/expression inputs
   through controller and flex evaluation to final deformed vertices.

Each experiment should add only the recipes and analyzer logic it actually
needs.

## Risks

| Risk | Response |
|---|---|
| A decoded structure is almost right but loses rare flags | Preserve the complete bounded source span and compare unknown bytes across divergent cases |
| Capture volume overwhelms memory or disk | Filter first, deduplicate static blobs, measure queue occupancy, then batch or compress |
| Instrumentation changes game timing | Measure callback duration and game frame time; sample or narrow spans before adding architecture |
| A stale pointer crashes the game | Validate pages, cap reads, catch faults, account failures, and fail closed |
| Data cannot be tied to an asset or actor | Capture exact hashes, addresses, call site, thread/time, and build CAP3 provenance before widening hooks |
| Final matrices hide the source of a mismatch | Trace backward only from the first mismatching bone, vertex, or frame |
| Raw bytes become an unqueryable dump | Require a recipe, stage boundary, length-delimited records, and rebuildable offsets |
| Infrastructure expands faster than evidence | Current-front order is binding; no dependency or subsystem without a measured experiment need |
