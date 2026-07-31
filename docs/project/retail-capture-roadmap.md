# Retail Animation Reverse-Engineering Roadmap

## Goal and ownership

This tracker drives one private reverse-engineering instrument for one exact
owner-controlled VtMB retail build. Its purpose is to recover how that build
loads character resources and turns them into the skeletal, scene, secondary-
motion, facial, lip-sync, deformation, and render state that Elysium must
reproduce.

The executable and module bytes are the contract. The capture format, reader,
recipes, and analyzers may change together whenever an experiment needs better
evidence. Unknown bytes are evidence, not schema defects.

`docs/project/roadmap.md` owns project priority and the roll-up rows `0.10`,
`RE32`, and `RE33`. This file owns the detailed task order. Confirmed VtMB facts
belong in the relevant `docs/vtmb/` document. Game-derived binaries, captures,
decompilation, indexes, and reports stay under `$ELYSIUM_WORK_ROOT/research`.

## Finish line

The program is successful when engine-neutral code can consume the owner's
original resources and reproduce the retail outputs needed by Elysium for
representative shipped cases. The useful comparison boundaries are:

1. requested resource bytes;
2. constructed runtime objects and state;
3. decoded local bone transforms;
4. composed model/world and skin matrices;
5. scene/root/entity movement;
6. secondary-motion state and writeback;
7. facial controllers, flex weights, and deformed vertices;
8. final render inputs.

Completion does not require naming every field, proving every unknown byte is a
no-op, supporting another executable, or recreating the original engine as a
general library. Unexplained bytes remain attached to their raw source spans and
are recorded as risks when they affect a reproduced path.

## Working rules

1. **Start from a visible output.** Use the final matrix, flex, vertex, or draw
   state as an oracle, then trace backward only to the first unexplained stage.
2. **Capture raw evidence before decoding it.** A record keeps registers, stack
   bytes, pointer values, bounded pointed-to spans, original addresses, and copy
   outcomes. Field names are analyzer hypotheses.
3. **Work on one controlled case at a time.** Add a hook or span to answer one
   question about one actor, resource, animation, scene, line, or visible
   secondary effect.
4. **Keep the live path bounded.** The game callback validates and copies bounded
   spans, accounts for failures and drops, and returns. It does not decode,
   index, compress, or retain a whole run in memory.
5. **Evolve the tool freely.** There is no schema registry, migration system, or
   public compatibility promise. A finalized run is nevertheless one
   self-contained, queryable evidence file whose raw payloads remain readable by
   the current research tools.
6. **Optimize only capture and analysis pain that is observed.** The 915 MB
   whole-theatre trace and frame-file workflow establish the current storage and
   query problem. Live callbacks still only enqueue bounded raw records; an
   offline finalizer owns indexing, lossless compression, and deduplication.
7. **Do not build ahead.** A later animation system does not justify capture
   infrastructure before its first concrete experiment.
8. **Delete dead machinery.** A hook, reader, control, dependency, or test stays
   only while it protects the exact build, preserves useful evidence, or answers
   a current research question.

## Evidence gate for a research conclusion

A task that claims retail behavior closes only when:

1. the executable/module hashes, tool commit, and capture recipe are recorded;
2. captured, written, dropped, truncated, unreadable, and incomplete counts are
   reported;
3. the relevant input and output spans are preserved as raw bytes;
4. an offline analyzer or evaluator can use the trace without reading the live
   process;
5. a repeat capture or independent byte comparison supports the conclusion;
6. the fact is written in its owning `docs/vtmb/` document;
7. the recovered rule has a game-independent regression or a local hash-gated
   retail comparison.

Synthetic tests establish probe safety and recorder mechanics only. They never
close a task that claims game behavior.

## Existing baseline

- [x] **CAP0.1 Existing evidence and analyzers.** `ELPOSE2` and `ELANIM2` BASE,
  FINL, DrawModel, matrix, whole-scene, authored-pose, and cinematic comparison
  paths remain usable as reference evidence.
- [x] **CAP0.2 Exact-build access.** The Win32 harness launches suspended or
  attaches, injects the probe, observes modules, records exact identity, and
  activates only matching profiles and signatures.
- [x] **CAP0.3 Reusable hook mechanics.** The validated vtable and instruction-
  aware inline-hook backends, worker writer, failure accounting, supervision,
  and synthetic lifecycle coverage remain available.
- [x] **CAP0.4 Contract cleanup.** Stable record schemas, generated registries,
  schema migration tests, FlatBuffers, strict experiment manifests, numerical-
  policy infrastructure, and planned generic IPC/MCAP/MinHook work are absent.
- [x] **CAP0.5 Player-command experiment and source dictionary.** One unattended
  `howl` run proves the final-pose oracle, while the patch-first player inventory
  preserves exact owner/sequence/animation identities and unknown descriptor
  bytes. Forced `player_sequence` playback is not the corpus strategy.

These are baseline capabilities, not a platform to expand. The injected probe
stays loaded for the target process lifetime; hot unloading it while the game
continues is out of scope.

## Priority and chronological order

| Order | Priority | Phase | Outcome |
|---:|---|---|---|
| 1 | P0 — current | CAP1 — source-attributed theatre capture | One hook-active launch records every encountered skeletal actor, relevant resource load, fired animation contribution, and final transformation from process start through `sp_theatre`, then finalizes one queryable capture database |
| 2 | P0 | CAP2 — first observed skeletal path | One ordinary evaluation selected from the theatre database is reproduced from exact source bytes through decoded locals and final matrices |
| 3 | P0 | CAP3 — skeletal mismatches | Only the shipped timing, blend, remap, layer, root-motion, and procedural cases that break the simple evaluator are recovered |
| 4 | P1 | CAP4 — theatre scene | One authored scene is traced through binding, placement, layered animation, movement, events, and completion |
| 5 | P1 | CAP5 — face and lips | One spoken line is traced from VCD/DLG/audio/LIP/model resources to final flexed vertices |
| 6 | P2 | CAP6 — secondary motion | One visible accessory, hair, cloth-like, or jiggle behavior is classified and reproduced without assuming its mechanism first |
| 7 | P2 | CAP7 — reproduction handoff | The recovered paths run in engine-neutral code, have focused retail comparisons, and feed Unreal |

**The next and only current task is CAP1.1.** CAP1 advances through small
instrumented passes—actors/models, resource identity, fired evaluations, and
correlation—before the full theatre run. Later tasks may identify a missing span
or stage, but they do not authorize unrelated capture infrastructure early.

## CAP1 — Source-attributed `sp_theatre` skeletal capture

- [ ] **CAP1.1 One-run capture database and launch boundary.** Launch the exact
  retail build with the probe active before `sp_theatre` begins loading. Live
  hooks append bounded, self-length-delimited records through the existing
  worker; they never run SQL, decode, compress, or index. On stop, finalize the
  temporary streams transactionally into one SQLite file containing exact
  executable/module hashes, launch recipe, record counts, failures, and raw
  payloads. The database is the retained artifact; a partial temporary stream
  remains recoverable after a crash. No schema registry or migration system is
  added.
- [ ] **CAP1.2 Actors, models, and skeletons.** In the first small theatre pass,
  record every encountered skeletal client entity and its lifetime, handle or
  index when available, classname/target identity when cheaply reachable,
  runtime studio-header pointer, model path/checksum, skin/body selection, bone
  count, bone names/parents/flags, bind locals, inverse binds, and rendered draw
  identity. Store immutable model and skeleton data once and reference it from
  later events. This census establishes which actors and props actually need
  deeper hooks.
- [ ] **CAP1.3 Animation/model resource loads.** With the probe active before map
  load, record only resource requests that construct or feed the encountered
  skeletal models or trigger their scene animation: requested normalized path
  and resource class, request caller, success/failure, returned byte span or
  exact file hash, constructed runtime object/header address, include-model
  relationships, and unload/reuse events. This includes the encountered MDL and
  companion model data plus VCD/animation-set inputs, but not unrelated map,
  texture, audio, or UI traffic. Join runtime pointers to the patch-first
  installed bytes without crawling unrelated files or building a universal
  object database.
- [ ] **CAP1.4 Fired animation triggers and source evaluations.** Record every
  sequence/activity change and every lower-level skeletal sequence evaluation
  actually fired for every model during the theatre run. Each contribution keeps
  the target entity/model, trigger caller, source owner studio header and model
  identity, owner-local sequence and animation indices, phase/cycle, playback
  time, pose parameters, active blend cells/weights, selected-bone mask, local
  positions/quaternions, raw source descriptor identity, and copy failures.
  Capture repeated calls as events; deduplication may share payload storage but
  never erases timing or call multiplicity.
- [ ] **CAP1.5 Evaluation-to-skeleton-to-draw correlation.** Assign one pose-build
  generation identity that groups all base, transition, autoplay, layer, gesture,
  controller, and included-model contributions for an entity. Carry it through
  BASE, FINL, root/entity/world composition, `boneToWorld`, skin palette, and
  DrawModel. Retain the source-to-target skeleton mapping actually used—mapping
  object identity, source and target bone indices/names, absent bones, and raw
  remap flags/matrices—without interpreting every field yet. Record engine
  frame/tick, QPC, thread, pose-buffer identity, entity origin/angles,
  visibility/draw outcome, and pointer lifetimes so correlation does not depend
  on nearest timestamps or model names alone.
- [ ] **CAP1.6 Full theatre acquisition and integrity audit.** Run from clean
  process launch through `sp_theatre` load, authored playback, and completion.
  The finalized database reports every relevant resource, actor/model/skeleton,
  fired trigger, source evaluation, pose-build group, and final draw; written,
  dropped, truncated, unreadable, unjoined, and incomplete counts; queue and disk
  high-water marks; process/map/scene boundary markers; capture span; and natural
  or forced cleanup. Repeat only if the first run is contaminated or an identity
  join is missing.
- [ ] **CAP1.7 Source/export join and question report.** Join each captured source
  contribution by exact owner path/checksum plus owner-local sequence/animation
  identity to the patch-first descriptor bytes, CAP0.5 inventory where
  applicable, current decoder, and exported animation. Report observed coverage,
  unresolved runtime identities, BASE-to-FINL and FINL-to-draw differences, and
  the first mismatching stage/bone. The report selects the smallest CAP2, CAP3,
  CAP4, CAP5, or CAP6 follow-up; it does not claim that unobserved animations or
  continuous blend space were covered.

CAP1 captures every skeletal evaluation **actually fired** during the controlled
theatre run, including repeated and layered contributions. It does not force or
enumerate every possible game animation. Facial flexes remain CAP5, and solver
state behind visible secondary motion remains CAP6; CAP1 still retains their
encountered model resources and final skeletal outputs when present.

For any selected theatre time/entity, the database must answer: which actor,
target model, and target skeleton existed; which resources constructed them;
what request caused an animation change; which source owner/sequence/animation
contributions were evaluated; how source bones mapped to the target skeleton;
what BASE, FINL, entity/world, bone-to-world, and skin transforms resulted; which
stage changed the pose; and whether the exact source identity and output join to
the current decoder/export. An explicit unknown is an acceptable answer; a lost
join or silently missing event is not.

## CAP2 — First observed resource-to-final-matrix path

- [ ] **CAP2.1 Select one ordinary observed evaluation.** Choose one complete,
  source-attributed CAP1 pose-build group whose source bytes and export are
  available and whose final draw is uncontaminated. Pin its resource, entity,
  source owner, sequence/animation, time, selected bones, and final matrices.
- [ ] **CAP2.2 Targeted skeletal stage trace.** Starting from that group, add only
  the smallest editable raw hook recipe needed to capture evaluator inputs,
  compressed bytes consumed, decoded local position/quaternion output, hierarchy
  composition, and draw input. Preserve registers, bounded stack and pointed-to
  spans, original addresses, copied lengths, neighboring unknown bytes, and
  failures; no generic hook schema is required.
- [ ] **CAP2.3 First ordinary-path equivalence.** Compare the retail locals and
  final matrices with the current decoder and retained reference output. Trace
  backward from the first bad bone/frame, fix only the demonstrated rule, add a
  minimized regression, and record the confirmed behavior.

The final draw is an oracle in CAP2, not a late phase after every animation
system has been decoded.

## CAP3 — Skeletal expansion driven by mismatches

- [ ] **CAP3.1 Sampling and time.** Exercise the shipped boundary cases that
  produce a mismatch: frame selection, interpolation, looping/clamping,
  playback rate, pause, seek, transition, and first-frame/reset state.
- [ ] **CAP3.2 Selection, blends, and layers.** Recover sequence/activity choice,
  blend inputs, pose parameters, base pose, overlays, gestures, transitions,
  masks, weights, and missing-channel defaults one failing case at a time.
- [ ] **CAP3.3 Included-model remapping.** For a selected model that uses shared
  animation data, recover include resolution, sequence/bone remaps, donor bind
  use, absent-bone behavior, and the runtime cache identity needed to reproduce
  it. Cache lifetime and invalidation are investigated only if they change an
  observed result or pointer relationship.
- [ ] **CAP3.4 Controllers and procedural order.** Locate bone controllers,
  procedural rules, IK-like work, and other post-decode adjustments only when
  they cause the first remaining mismatch. Record raw state immediately before
  and after the contributing stage.
- [ ] **CAP3.5 Root and entity motion.** Separate animated root/pelvis movement,
  entity movement, and the outer world transform so Elysium neither loses nor
  double-applies motion.
- [ ] **CAP3.6 Representative skeletal closure.** The engine-neutral evaluator
  matches selected ordinary clips, one layered/gesture case, one included-model
  case, and every additional skeletal case required by the current Elysium
  consumer. Unknown fields remain preserved and explicitly unresolved; they do
  not block closure unless they change covered output.

Save/load, ragdoll, map teardown, and exhaustive animation-cache behavior are
not default skeletal tasks. Add a focused case only when a shipped behavior
needed by Elysium exposes a mismatch there.

## CAP4 — Authored scene placement and movement

- [ ] **CAP4.1 Interpret one captured scene binding.** Starting from CAP1's raw
  theatre resource, trigger, entity, and pose-build records, follow one VCD's
  participant lookup, animation set, actor/understudy substitution, target
  entity, and initial state. Add another trigger hook only if the retained events
  cannot identify the binding.
- [ ] **CAP4.2 Placement and per-frame ownership.** Recover the selected scene's
  stage anchor, local-to-world placement, yaw/origin rules, animation layers,
  root/entity movement, collision changes, event timing, and the owner of each
  transform component through final matrices.
- [ ] **CAP4.3 Completion path.** Capture the interruption, completion, and
  restoration behavior exercised by the selected theatre path and reproduce its
  actor trajectory, event order, and final state. Other lifecycle edges are
  added only when a concrete scene needs them.

## CAP5 — Facial animation and lip sync

- [ ] **CAP5.1 One spoken line's resource path.** Follow one controlled theatre
  line from VCD/DLG/audio/LIP and model facial bytes through load, runtime object
  construction, timing identity, and the pointers used by facial evaluation.
- [ ] **CAP5.2 Controller contribution trace.** Capture raw state before and
  after expression, phoneme, amplitude-mouth, eyelid, blink, and gaze stages
  that actually contribute to the selected line. Recover lookup, ramps,
  clamping, defaults, and mixing order from isolated changes.
- [ ] **CAP5.3 Flex and vertex path.** Trace the encountered vertex-animation
  encoding and flex rules through controller/flexdesc weights to representative
  final deformed vertices and render inputs.
- [ ] **CAP5.4 Controlled-line equivalence.** Reproduce controller values, lip
  timing, flex weights, and selected final vertices for the controlled line,
  then expand only to another line/model that exposes a new mismatch.

## CAP6 — Secondary motion and physics

- [ ] **CAP6.1 Mechanism classification.** Select one visible shipped effect and
  use pre/post stage captures to determine whether it is an animated bone,
  procedural rule, jiggle/spring update, cloth-like solver, collision response,
  or another mechanism. Do not begin with an exhaustive list of assumed
  systems.
- [ ] **CAP6.2 Resource and persistent state.** For the classified mechanism,
  connect raw parameters to their source bytes and capture prior-frame state,
  delta time, input transforms/velocities, relevant forces or collisions, reset
  flags, and output/writeback.
- [ ] **CAP6.3 Update and discontinuity.** Isolate the observed update order and
  reproduce steady motion plus one relevant discontinuity such as pause,
  teleport, scene restart, or visibility change. Add solver details only when
  the trace demonstrates them.
- [ ] **CAP6.4 Representative secondary-motion equivalence.** Match the selected
  effect from captured initial state and inputs. Add another mechanism only
  when it is visibly shipped and needed by an Elysium character. Ragdoll
  handoff is not part of this phase unless it becomes such a selected case.

## CAP7 — Reproduction and handoff

- [ ] **CAP7.1 End-to-end evidence paths.** For each recovered track, retain one
  focused recipe, raw trace, analyzer, and regression that navigates from source
  resource bytes to the final matrix or vertex evidence used for comparison.
- [ ] **CAP7.2 Engine-neutral evaluator.** The evaluator consumes original
  resources plus explicit runtime state and matches the selected retail
  skeletal, scene, facial, and secondary-motion outputs without Unreal
  retargeting or presentation transforms in the equivalence test.
- [ ] **CAP7.3 Unreal handoff.** Unreal consumes the verified evaluator output
  before basis conversion/retargeting. Numerical source equivalence and
  retargeted visual acceptance remain separate tests.
- [ ] **CAP7.4 Final tool trim.** Delete probes, readers, fixtures, controls, and
  dependencies that no retained evidence path or active investigation uses.

## Triggered capture and storage improvements

These are responses to evidence, not scheduled prerequisites:

| Observed problem | Smallest allowed response |
|---|---|
| Callback time or heap allocation is measurable | Reuse fixed-size slots or a preallocated pool for the active recipe |
| Queue reaches its byte cap | Narrow spans/filter first, then tune the cap or batching from measurements |
| Immutable resource bytes dominate the trace | Store that blob once by hash and reference its byte range |
| A finalized SQLite query is materially slow | Add only the index or derived summary table required by that measured query |
| Disk write rate is the bottleneck | Batch or compress on the writer thread after measuring the codec cost |
| A hook is too noisy | Add the one model/entity/time/call filter required by that experiment |
| The shared hook backend fails on a validated target | Repair or replace only the failing backend behavior |
| Exact pose payloads dominate the database | Losslessly compress and content-deduplicate payloads in the offline finalizer while retaining every event row |

No triggered improvement becomes a general subsystem unless more than one real
experiment needs it.

## Explicit non-goals

- hot unloading the probe while the target keeps running;
- multiple game builds, schema versions, migrations, or public consumers;
- a general hook SDK, remote collector, IPC framework, or database-backed live
  capture service;
- chunk/container design beyond the length-delimited recovery the current trace
  needs;
- a complete process dump or recursive pointer-graph crawler;
- exhaustive cache, solver, save/load, ragdoll, and teardown coverage before a
  selected shipped case requires it;
- duplicate captures for clan or armor models that resolve to the same owner,
  sequence, animation data, and compatible skeleton;
- explaining or discarding every unknown bit before useful behavior can be
  reproduced.

## Risks

| Risk | Response |
|---|---|
| A decoder matches common cases but loses rare flags | Keep bounded original source bytes and unknown neighbors in the evidence path |
| Capture volume overwhelms memory or disk | Stream with a hard queue-byte cap, filter first, measure, then apply one triggered optimization |
| Instrumentation changes game timing | Measure callback and frame time; narrow or sample before adding machinery |
| A stale pointer crashes the game | Validate pages, cap reads, catch faults, report failures, and fail closed |
| Runtime data cannot be tied to an asset or actor | CAP1 captures the resource/header/entity lifetime chain before widening the evaluation hook |
| Final output hides the cause of a mismatch | Trace backward only from the first mismatching bone, vertex, or frame |
| Raw evidence becomes difficult to query | Keep records self-bounded and add a disposable index only for a demonstrated slow query |
| Pointer reuse joins unrelated actors or sources | Record construction/unload events and use scoped generation identities rather than raw addresses alone |
| One final pose hides multiple animation contributors | Record every lower evaluator call and group contributions under the enclosing pose-build generation |
| Render visibility omits an actor | Distinguish fired evaluation, completed pose build, and final draw coverage instead of treating no draw as no animation |
| Infrastructure expands faster than evidence | The chronological order is binding and CAP1.1 is the only current task |
