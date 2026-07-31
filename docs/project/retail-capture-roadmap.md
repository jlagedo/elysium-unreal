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
5. **Evolve the tool freely.** There is no schema registry, migration system,
   public compatibility promise, or requirement to read captures after their
   facts and regression fixtures are secured.
6. **Optimize only capture and analysis pain that is observed.** Queue changes,
   deduplication, indexing, batching, and compression require a measured callback,
   memory, writer, disk, or query problem.
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

These are baseline capabilities, not a platform to expand. The injected probe
stays loaded for the target process lifetime; hot unloading it while the game
continues is out of scope.

## Priority and chronological order

| Order | Priority | Phase | Outcome |
|---:|---|---|---|
| 1 | P0 — next | CAP1 — automated player-animation corpus | One clean unattended player capture scales to the raw resolved player-animation inventory and is compared across the current export/decoder stack, with explicit evidence for every failure or skip |
| 2 | P0 | CAP2 — first skeletal path | One simple actor is traced from requested files through runtime objects and local transforms to final draw matrices |
| 3 | P0 | CAP3 — skeletal mismatches | Only the shipped timing, blend, remap, layer, root-motion, and procedural cases that break the simple evaluator are recovered |
| 4 | P1 | CAP4 — theatre scene | One authored scene is traced through binding, placement, layered animation, movement, events, and completion |
| 5 | P1 | CAP5 — face and lips | One spoken line is traced from VCD/DLG/audio/LIP/model resources to final flexed vertices |
| 6 | P2 | CAP6 — secondary motion | One visible accessory, hair, cloth-like, or jiggle behavior is classified and reproduced without assuming its mechanism first |
| 7 | P2 | CAP7 — reproduction handoff | The recovered paths run in engine-neutral code, have focused retail comparisons, and feed Unreal |

**The next and only current task is CAP1.1.** The existing final-pose capture is
used before adding generic raw hooks. Later tasks may identify a missing span or
stage, but they do not authorize building capture infrastructure early.

## CAP1 — Automated player-animation corpus

- [ ] **CAP1.1 Reproducible unattended seed capture.** Turn the validated launch
  shape (`-game Unofficial_Patch -dev -console -sw +exec`) into one repeatable
  `howl` capture on the current player model. Load the fixed save, let the world
  settle at normal time, enter third person, pulse and release movement to reset
  the player idle timer, allow locomotion to settle, arm the existing final-pose
  recorder, and only then run `player_sequence howl`. Acceptance requires the
  exact executable/module/model hashes and launch recipe, bounded capture
  counters, no stuck retail process, and monotonic live-to-authored `howl`
  alignment without an idle or locomotion interruption.
- [ ] **CAP1.2 Raw resolved player-animation inventory.** Enumerate every raw
  sequence descriptor and animation descriptor reachable through the include-
  model graphs of the installed player body models. Do not use the current
  `local_sequences` convenience view as the coverage authority: it deduplicates
  labels and exposes only blend cell `[0][0]`. Preserve target model, owner model,
  raw sequence and animation indices, name, the complete 16×16 animation grid,
  frames, FPS, flags, blend/pose metadata, neighboring unknown bytes, and the
  list of compatible player models. Deduplicate only identical owner/sequence/
  animation-data identities across clans and armor variants; never collapse
  duplicate names that resolve to different bytes.
- [ ] **CAP1.3 Automated player-sequence capture.** Drive every inventory row
  that is unambiguously addressable through `player_sequence` using the CAP1.1
  reset/settle/play/capture recipe, initially with one fresh retail launch per
  sequence for isolation. Capture final bone/world and skin matrices plus the
  existing draw correlation, enforce per-run timeout and cleanup, and retain a
  terminal result for every raw inventory row. Duplicate-name, pose-parameter,
  blend-cell, layer-only, and otherwise unaddressable rows remain explicit and
  identify the smallest additional stimulus or selection probe they require.
  Reuse one process only if measured startup cost makes the isolated loop
  impractical.
- [ ] **CAP1.4 Corpus-wide export and decoder differential.** Match every clean
  retail capture to the exact patch-first source bytes, raw owner/sequence/
  animation identity, and exported animation produced by the current stack.
  Evaluate the engine-neutral decoder at the captured times, remove entity/root
  placement, and report per-frame/per-bone local position, local rotation,
  composed matrix, and skin-palette error. Cluster failures by owner bank,
  sequence flags, blend grid, pose parameters, bone flags, missing channels,
  timing, and boundary behavior. A proposed rule counts as inferred only when it
  predicts held-out captures or a focused repeat; correlation alone remains a
  hypothesis.
- [ ] **CAP1.5 Corpus audit and mismatch queue.** Write a disposable JSONL or CSV
  index with attempted, captured, deduplicated, ambiguous, interrupted,
  unselectable, timed-out, and failed counts; capture paths and hashes; authored
  alignment and decoder-comparison coverage; and the first mismatching frame,
  stage, and bone. Retry contamination rather than accepting it. A sequence that
  `player_sequence` cannot address by name keeps its exact failure evidence and
  becomes a focused selection-state case for CAP3 instead of disappearing from
  coverage.

For CAP1, **all player animations** means every unique skeletal sequence in the
resolved player-model union has a terminal status: validated capture, proven
identity deduplication, or an exact evidenced blocker with a focused follow-up.
Facial flexes, scene-only actor control, ragdoll, and secondary solvers remain in
their owning later phases. Every validated capture is also matched to source and
export identities and compared against the current decoder. CAP1 closes on that
complete inventory, corpus, and differential report, not on a claim that every
animation path or every point in a continuous blend space is already understood.

## CAP2 — First resource-to-final-matrix path

- [ ] **CAP2.1 One actor's resource load.** For one ordinary humanoid and one
  simple body clip, capture the requested model/animation paths, returned raw
  bytes or file hashes, load callers, and the first constructed runtime object.
  Follow MDL/TTH/TTZ/VTX/VVD and included files only when the selected actor
  actually requests them.
- [ ] **CAP2.2 Runtime identity chain.** Connect the loaded byte ranges and
  constructed object addresses to the model/entity pointers observed at BASE,
  FINL, and DrawModel. Preserve neighboring unknown bytes; do not build a
  universal object database.
- [ ] **CAP2.3 Targeted skeletal stage trace.** For matched model, sequence, and
  time, start from the CAP1 final matrices and add only the smallest editable raw
  hook recipe needed to capture evaluator inputs, compressed bytes consumed,
  decoded local position/quaternion output, hierarchy composition, and draw
  input. Each record preserves registers, bounded stack and pointed-to spans,
  original addresses, copied lengths, and failures; no durable generic schema is
  required.
- [ ] **CAP2.4 First ordinary-clip equivalence.** Compare the retail locals and
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

- [ ] **CAP4.1 One scene's resource and actor binding.** Follow one theatre VCD,
  its referenced animation/dialogue resources, participant lookup, animation
  set, actor/understudy substitution, target entity, and initial state into the
  runtime objects that drive playback.
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
| Finding records is materially slow | Build a disposable file-offset index; add a local SQLite table only for the slow query |
| Disk write rate is the bottleneck | Batch or compress on the writer thread after measuring the codec cost |
| A hook is too noisy | Add the one model/entity/time/call filter required by that experiment |
| The shared hook backend fails on a validated target | Repair or replace only the failing backend behavior |
| One-process-per-sequence startup dominates the corpus run | Reuse the settled world only after measuring the cost and proving reset isolation between sequences |

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
| Runtime data cannot be tied to an asset or actor | Capture addresses, caller, time/thread, exact hashes, and establish CAP2 identity before widening hooks |
| Final output hides the cause of a mismatch | Trace backward only from the first mismatching bone, vertex, or frame |
| Raw evidence becomes difficult to query | Keep records self-bounded and add a disposable index only for a demonstrated slow query |
| Player idle or locomotion contaminates a forced sequence | Pulse movement before capture, release it, allow a fixed settle window, and reject non-monotonic authored alignment |
| Batch coverage silently omits duplicate names or unselectable sequences | Inventory by owner/index/data identity and require one terminal result for every row |
| Infrastructure expands faster than evidence | The chronological order is binding and CAP1.1 is the only current task |
