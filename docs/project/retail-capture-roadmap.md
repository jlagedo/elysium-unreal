# Retail Animation Capture and Verification Roadmap

## Goal and ownership

This tracker drives one private instrument against one exact owner-controlled VtMB
retail build, and one executable loop over it:

> **build the capture → run `sp_theatre` → capture → decode and index → inspect against
> the export and the current decoder → name what is missing → extend the capture.**

The program answers one question: which parts of the retail character transformation
chain — requested resource bytes, skeletons, fired animation contributions, composed
matrices, skin palettes, flexes — does the offline decoder fail to reproduce, and why.
File archaeology alone has not answered it. The running process is the only oracle that
shows which source bytes are actually read and which transforms actually result.

The executable and module bytes are the contract. The capture format, reader, recipes,
and analyzers change together whenever an experiment needs better evidence. Unknown bytes
are evidence, not schema defects.

**The Unofficial Patch is part of the exact build.** The unpatched install does not launch,
so the capture target is the retail engine modules running under the patch's executable,
startup profile, and content — never something to strip for a "purer" capture. The bound on
what that costs is narrow and pinned in the binary profiles: the patch supplies the process
launcher, while `client.dll`, `engine.dll`, and `StudioRender.dll` are the original retail
modules loaded from the base install, and every hooked animation target lives in those
three. Source joins resolve patch-first for the same reason.

`docs/project/roadmap.md` owns project priority and the roll-up rows `0.10`, `RE32`, and
`RE33`. This file owns the detailed task order. Confirmed VtMB facts belong in the
relevant `docs/vtmb/` document; hook target addresses, prototypes, confidence, and
relationships belong in `research/cases/animation-pose/specs/`; evidence model and method
belong in `docs/vtmb/vtmb-animation-reverse-engineering.md`. Game-derived binaries,
captures, decompilation, indexes, and reports stay under `$ELYSIUM_WORK_ROOT/research`.

## The loop

| Step | Owned by | Produces |
|---|---|---|
| Run the exact build with hooks armed before map load | CAP1 | one finalized capture database plus its calibration measurement |
| Complete what the capture records | CAP2 | grouping, actor/model/skeleton census, source attribution, consumed byte spans |
| Decode and index the streams | CAP3 | one queryable, deduplicated, joinable database |
| Inspect retail against export and decoder | CAP4 | byte-coverage and per-bone transform differences |
| Close what the difference proves | CAP5 | recovered rules, regressions, facts in the owning `docs/vtmb/` topic |

The loop repeats. Each pass extends the capture only where the previous pass named a
missing byte range, an unjoinable identity, or a mismatching stage. Face and lips
(CAP6) run the same loop over flex and phoneme state once the skeletal pass closes.

## Finish line

The program succeeds when engine-neutral code consumes the owner's original resources and
reproduces the retail outputs Elysium needs for the theatre corpus. The comparison
boundaries are:

1. requested resource bytes and the spans the runtime dereferences;
2. constructed runtime objects, skeletons, and actor identity;
3. decoded local bone transforms per fired contribution;
4. composed model/world, bone-to-world, and skin matrices;
5. scene/root/entity movement;
6. facial controllers, flex weights, and deformed vertices;
7. final render inputs.

Completion does not require naming every field, proving every unknown byte is a no-op,
supporting another executable, or recreating the original engine as a general library.
Unexplained bytes stay attached to their raw source spans and are recorded as risks when
they affect a reproduced path.

## Working rules

1. **Capture what the runtime reads, not only what it emits.** A pose that matches proves
   nothing about a field the decoder silently skipped. Consumed byte spans are first-class
   evidence.
2. **Immutable source bytes are a dictionary, not a stream.** Model images, skeletons, and
   animation descriptors are stored once by content hash; per-call records keep identity
   and offsets only.
3. **Start from a visible output.** Use the final matrix, flex, vertex, or draw state as an
   oracle, then trace backward only to the first unexplained stage.
4. **Capture raw evidence before decoding it.** A record keeps registers, stack bytes,
   pointer values, bounded pointed-to spans, original addresses, and copy outcomes. Field
   names are analyzer hypotheses.
5. **Keep the live path bounded.** The game callback validates and copies bounded spans,
   accounts for failures and drops, and returns. It does not decode, index, compress, or
   retain a run in memory.
6. **Measure before optimizing.** The first run replaces every storage, filter, and rate
   estimate in this document with a number.
7. **Evolve the tool freely.** There is no schema registry, migration system, or public
   compatibility promise. A finalized run is nevertheless one self-contained, queryable
   evidence file whose raw payloads stay readable by the current research tools.
8. **Do not build ahead.** A later animation system does not justify capture infrastructure
   before the difference report names it.
9. **Delete dead machinery.** A hook, reader, control, dependency, or test stays only while
   it protects the exact build, preserves useful evidence, or answers a current question.

## Evidence gate for a research conclusion

A task that claims retail behavior closes only when:

1. the executable/module hashes, tool commit, and capture recipe are recorded;
2. captured, written, dropped, truncated, unreadable, unjoined, and incomplete counts are
   reported;
3. the relevant input and output spans are preserved as raw bytes;
4. an offline analyzer can use the database without reading the live process;
5. a repeat capture or independent byte comparison supports the conclusion;
6. the fact is written in its owning `docs/vtmb/` document;
7. the recovered rule has a game-independent regression or a local hash-gated retail
   comparison.

Synthetic tests establish probe safety and recorder mechanics only. They never close a task
that claims game behavior.

## Existing baseline

- [x] **CAP0.1 Exact-build access.** The Win32 launcher creates the target suspended under
  the Unofficial Patch startup profile, injects the probe host, observes module loads,
  hash-gates each module against its binary profile, byte-validates every hook target, and
  activates only matching profiles.
- [x] **CAP0.2 Hook mechanics.** Validated vtable and instruction-aware inline backends,
  worker-thread writer, failure accounting, supervision, and synthetic lifecycle coverage.
- [x] **CAP0.3 Three armed hooks.** `CStudioRender::DrawModel` yields bone-to-world plus
  skin palette; `resolve_virtual_model_pose` yields decoded locals and the selected-bone
  mask; `C_BaseAnimating::BuildTransformations` yields composed locals and the root/entity
  transform.
- [x] **CAP0.4 One-run database boundary.** The theatre recipe installs one inert cfg that
  waits before issuing `map sp_theatre`, so hooks arm before map resources load; on stop the
  temporary streams finalize transactionally into one SQLite file carrying module hashes,
  launch recipe, counts, failures, artifacts, and raw payloads. The recipe is implemented
  and exercised against the theatre; CAP1.1 records what it produced.
- [x] **CAP0.5 Player-command experiment and source dictionary.** One unattended `howl` run
  proves the final-pose oracle; the patch-first player inventory preserves exact
  owner/sequence/animation identities and unknown descriptor bytes. Forced `player_sequence`
  playback is not the corpus strategy.
- [x] **CAP0.6 Contract cleanup.** Stable record schemas, generated registries, schema
  migration tests, FlatBuffers, strict experiment manifests, and planned generic IPC/MCAP
  work are absent.

These are capabilities, not a platform to expand. The injected probe stays loaded for the
target process lifetime; hot unloading it while the game continues is out of scope.

## What one theatre run has to hold

`sp_theatre` is the corpus because it exercises the whole chain at once: 42 skeletal NPC
entities across 33 distinct character models, 42 dynamic props, 12 choreographed scenes,
7 scripted sequences, and a tracked camera. One named participant resolves over 1,600
clips through 31 owner model files, so include-graph attribution is exercised on the first
run rather than deferred.

Uncompressed and unfiltered, one captured cutscene costs **3.5 GB**: ~872,000 records over a
~308 s span, split ~395,000 draw records and ~477,000 skeletal contribution records, across
141 distinct runtime studio headers. Storage is therefore not the binding constraint at this
scale. CAP1.2 turns the rest of the run report into the per-stream and per-actor rates that
CAP2's filters and CAP3's storage design need; CAP3.1 stays the sanctioned response if a
later, longer corpus makes volume bite.

## Priority and chronological order

| Order | Priority | Phase | Outcome |
|---:|---|---|---|
| 1 | P0 — current | CAP1 — first run and calibration | The instrument that exists produces one finalized `sp_theatre` database, and its measured rates, counts, and joins replace every estimate |
| 2 | P0 | CAP2 — complete the capture | Contributions group, actors and skeletons are identified, and every fired contribution names its source owner, indices, and consumed byte spans |
| 3 | P0 | CAP3 — decode and index | One deduplicated, compressed, joinable database answers per-actor and per-time questions without re-running the game |
| 4 | P0 | CAP4 — inspect against export and decoder | Byte ranges the runtime reads that we do not, and the first mismatching stage and bone per pose group |
| 5 | P1 | CAP5 — close what the difference proves | Recovered rules, each with a regression and a fact in the owning topic |
| 6 | P1 | CAP6 — face and lips | The same loop over expression, flex, phoneme, and deformed-vertex state |
| 7 | P2 | CAP7 — handoff and trim | Engine-neutral evaluator feeds Unreal; unused probes and readers are deleted |

**The next and only current task is CAP1.2.**

## CAP1 — First theatre run and calibration

The instrument has run against the theatre and holds two complete cutscene captures. Every
filter, budget, and schema decision below is measured from them rather than estimated.

`sp_theatre` is a cutscene from end to end: one arrival trigger starts it and it finishes by
loading `sp_tutorial_1`, with no choice to make in between. A console `map` load spawns the
player short of that trigger, which has no targetname, so the operator walks the gap once and
every stage after it is authored.

That walk lands at a different moment every run, so the run is bracketed by its own events
rather than by elapsed time: it stamps the trigger instant from the same performance counter
the hook writes on every record, and it stops on the map transition. Analysis aligns two
captures on the stamp, so a slow walk costs a longer idle prefix and nothing else.

- [x] **CAP1.1 First theatre acquisition.** `uv run elysium research capture_theatre` builds the
  native tools, installs the inert cfg, launches the exact retail build with the probe armed
  before `sp_theatre` loads, captures the cutscene from the arrival trigger to the map
  transition, and finalizes one SQLite database. Two runs each carry ~872,000 records over a
  ~308 s span with **zero drops and no incomplete tail**, all four binary profiles matched,
  every recipe marker present, no level-script traceback, and a stop on the observed
  `sp_tutorial_1` transition rather than the duration backstop. The two record counts differ by
  0.07%, so the cutscene is reproducible enough to diff against the decoder.
- [ ] **CAP1.2 Calibration measurement.** From the captured runs report record and byte
  rates per stream, queue and disk high-water marks, dropped/truncated/unreadable counts,
  distinct runtime studio headers with their model identity and bone counts, distinct
  client entities, per-actor record rates, and capture span against wall clock. Derive the
  trigger instant from the stream — the `SetModel` batch the arrival trigger fires lands
  within 0.15 s of the console stamp and is frame-exact — and report the two runs against
  each other on that zero. This measurement is the input to CAP2's filters and CAP3's
  storage design.
- [ ] **CAP1.3 Entity-pointer join verification.** Determine whether the draw stream's
  render-info entity field and the skeletal streams' instance pointer occupy one pointer
  space, and report the distribution of their difference per model. A single constant delta
  makes the existing streams joinable as they are; anything else makes CAP2.1 the only
  join, and says so in the report rather than silently correlating by timestamp.

## CAP2 — Complete the capture

Each task adds the smallest hook or span that closes one named gap in CAP1's report.
Targets whose prototype confidence is still partial get a Ghidra pass pinning arguments and
calling convention into the case specification before a hook is written.

- [ ] **CAP2.1 Pose-build generation identity.** Bracket the confirmed `SetupBones` entry
  and exit with a per-thread generation counter and stamp it on every nested record.
  *Acceptance:* every composed-pose record and its contributing evaluations for one entity
  share one generation, and no record in a full run is unassigned.
- [ ] **CAP2.2 Model and skeleton census.** Once per distinct runtime studio header record
  model path, checksum, bone count, bone names/parents/flags, bind locals, inverse binds,
  and the header span itself; reference it from later events rather than repeating it.
  Record first observation, reuse, and unload.
- [ ] **CAP2.3 Actor identity and lifetime.** Record each skeletal client entity's
  construction and destruction, classname/target identity where cheaply reachable,
  model/skin/body selection, origin and angles, and draw outcome, under a scoped generation
  so pointer reuse cannot join unrelated actors.
- [ ] **CAP2.4 Source attribution per contribution.** For every fired contribution record
  the resolved owner studio header and model identity, owner-local sequence and animation
  indices, active blend cells and weights, cycle and playback time, pose parameters,
  selected-bone mask, trigger caller, and copy failures. Repeated calls stay separate
  events; shared payload storage never erases timing or call multiplicity.
- [ ] **CAP2.5 Consumed byte spans.** Convert each contribution's descriptor and animation
  block pointers into offsets within the owning model image and retain the exact spans the
  runtime dereferences. This is what makes CAP4.2 possible; without it a matching pose
  cannot distinguish a correct decoder from a lucky one.
- [ ] **CAP2.6 Trigger and scene events.** Record every sequence/activity change and
  scene-driven animation request with caller, target entity, and time, so a pose group is
  attributable to what asked for it.
- [ ] **CAP2.7 Second acquisition.** Repeat CAP1.1 with the completed capture and report the
  same integrity counts plus unjoined-record counts. This database, not CAP1's, is the one
  CAP4 inspects.

## CAP3 — Decode and index

- [ ] **CAP3.1 Content-addressed payload store.** The offline finalizer deduplicates and
  losslessly compresses payloads by content hash while retaining every event row. Immutable
  model, skeleton, and descriptor bytes are stored once and referenced.
- [ ] **CAP3.2 Join schema.** Resources, models, skeleton bones, actors, pose-build groups,
  contributions, and draws hang off one time-ordered event spine keyed by generation rather
  than nearest timestamp. *Acceptance:* for any theatre time and entity the database answers
  which actor, model, and skeleton existed; which resources constructed them; what request
  changed the animation; which owner/sequence/animation contributions were evaluated; how
  source bones mapped to the target skeleton; and what local, composed, bone-to-world, and
  skin transforms resulted. An explicit unknown is an acceptable answer; a lost join is not.
- [ ] **CAP3.3 Integrity audit.** The finalized database reports written, dropped,
  truncated, unreadable, unjoined, and incomplete counts; queue and disk high-water marks;
  process, map, and scene boundary markers; capture span; and natural or forced cleanup.

## CAP4 — Inspect against the export and the current decoder

This phase is the point of the program. It runs entirely offline against the database.

- [ ] **CAP4.1 Source join.** Join each captured contribution to the patch-first installed
  bytes by exact model path and checksum plus owner-local sequence and animation indices,
  and to the current exported animation and the CAP0.5 inventory. Report observed coverage
  and every unresolved runtime identity explicitly.
- [ ] **CAP4.2 Byte-coverage difference.** Mark the byte ranges of each owning model image
  that the retail runtime dereferenced, and the ranges the current decoder reads. Ranges
  read by retail and unread by us are the missing-data list. Ranges read by neither stay
  recorded as unknown rather than assumed inert.
- [ ] **CAP4.3 Transform difference.** For every joined pose-build group evaluate the
  current decoder at the captured identity and time, normalize entity and root placement,
  and compare decoded locals, composed matrices, bone-to-world, and skin palette per frame
  and bone. Report the first mismatching stage and bone, descendant propagation, and the
  worst bone. Numerical bands and comparison rules are owned by
  `docs/vtmb/vtmb-animation-reverse-engineering.md`.
- [ ] **CAP4.4 Missing-work report.** One ranked list of what the run proves is missing:
  unread byte ranges by model, unresolved identities, mismatch clusters by stage, and stages
  the theatre never exercised. This report is the only thing that authorizes CAP5 work, and
  it never claims that unobserved animations or continuous blend space were covered.

## CAP5 — Close what the difference proves

- [ ] **CAP5.1 One mismatch at a time.** Take the highest-ranked cluster, trace backward
  from the first mismatching bone or frame, make the smallest change that explains the
  evidence, add a game-independent regression, and write the confirmed behavior into the
  owning `docs/vtmb/` topic.
- [ ] **CAP5.2 Deeper stage capture on demand.** When a mismatch cannot be explained from
  the retained spans, add the smallest editable raw hook recipe at the decoder, blend,
  remap, or procedural site it names. Preserve registers, bounded stack and pointed-to
  spans, original addresses, copied lengths, neighbouring unknown bytes, and failures.
- [ ] **CAP5.3 Theatre-corpus closure.** The engine-neutral evaluator matches the joined
  theatre corpus within the recorded bands, including layered, transition, and
  included-model cases the run exercised. Unknown fields stay preserved and explicitly
  unresolved; they do not block closure unless they change covered output.

### Candidate causes for a mismatch

Reference for classifying a CAP4.3 cluster. It is a vocabulary, not a work plan; a row
becomes work only when the difference report points at it.

| Class | Typical signature |
|---|---|
| Sampling and time | frame selection, interpolation, loop/clamp boundary, playback rate, seek, first-frame reset |
| Selection, blends, layers | sequence/activity choice, blend inputs, pose parameters, overlays, gestures, transitions, masks, missing-channel defaults |
| Included-model remapping | include resolution, sequence/bone remaps, donor bind use, absent bones, runtime cache identity |
| Controllers and procedural order | bone controllers, procedural rules, IK-like work, post-decode adjustment order |
| Root and entity motion | animated root/pelvis versus entity movement versus the outer world transform |
| Hierarchy composition | split inheritance, parent multiplication order, inverse-bind convention |

## CAP6 — Face and lips

Runs the same loop over facial state once CAP5.3 closes. Skeletal work is not blocked on it,
and it is not started before the skeletal difference report exists.

- [ ] **CAP6.1 One line's resource and object path.** Follow one controlled theatre line
  from its expression, VCD, audio, `.lip`, and model facial bytes through load, runtime
  object construction, timing identity, and the pointers facial evaluation uses.
- [ ] **CAP6.2 Controller and flex stage capture.** Capture raw state before and after the
  expression, phoneme, amplitude-mouth, eyelid, blink, and gaze stages that actually
  contribute to that line, plus the flex weights and representative deformed vertices they
  produce.
- [ ] **CAP6.3 Controlled-line equivalence.** Reproduce controller values, lip timing, flex
  weights, and selected final vertices for that line, then expand only to another line or
  model that exposes a new mismatch.

## CAP7 — Handoff and trim

- [ ] **CAP7.1 Engine-neutral evaluator and Unreal handoff.** The evaluator consumes
  original resources plus explicit runtime state and matches the verified retail outputs
  without Unreal retargeting or presentation transforms in the equivalence test. Unreal
  consumes its output before basis conversion; numerical source equivalence and retargeted
  visual acceptance stay separate tests.
- [ ] **CAP7.2 Final trim.** Delete probes, readers, fixtures, controls, and dependencies
  that no retained evidence path or active investigation uses.

## Triggered capture and storage improvements

Responses to measurement, not scheduled prerequisites.

| Observed problem | Smallest allowed response |
|---|---|
| Callback time or heap allocation is measurable | Reuse fixed-size slots or a preallocated pool for the active recipe |
| Queue reaches its byte cap | Narrow spans or filter first, then tune the cap or batching from measurements |
| Immutable resource bytes dominate the trace | Store that blob once by hash and reference its byte range |
| Exact pose payloads dominate the database | Losslessly compress and content-deduplicate in the offline finalizer while retaining every event row |
| A finalized query is materially slow | Add only the index or derived summary table that query requires |
| Disk write rate is the bottleneck | Batch or compress on the writer thread after measuring the codec cost |
| A hook is too noisy | Add the one model, entity, time, or call filter that experiment requires |
| The shared hook backend fails on a validated target | Repair or replace only the failing backend behavior |

No triggered improvement becomes a general subsystem unless more than one real experiment
needs it.

## Explicit non-goals

- hot unloading the probe while the target keeps running;
- multiple game builds, schema versions, migrations, or public consumers;
- a general hook SDK, remote collector, IPC framework, or database-backed live capture
  service;
- a pre-planned ladder for secondary motion, scene lifecycle, save/load, ragdoll, or
  teardown — each is a CAP5 case only when the difference report names it;
- a complete process dump or recursive pointer-graph crawler;
- forcing or enumerating every possible game animation; the run captures what actually
  fires;
- duplicate captures for clan or armor models that resolve to the same owner, sequence,
  animation data, and compatible skeleton;
- explaining or discarding every unknown bit before useful behavior can be reproduced;
- native VtMB authoring, model compilation, or writing animation back into the original
  game.

## Risks

| Risk | Response |
|---|---|
| The operator reaches the arrival trigger at a different moment every run | Bracket the cutscene on its own events: stamp the trigger instant from the shared performance counter, stop on the map transition, and align analysis on the stamp rather than on elapsed time |
| Draw and skeletal records occupy different pointer spaces | CAP1.3 measures it before any analysis depends on the join; CAP2.1 removes the dependency |
| Capture volume overwhelms memory or disk | Stream with a hard queue-byte cap, filter first, measure, then apply one triggered optimization |
| A decoder matches common cases but loses rare flags | CAP2.5 and CAP4.2 make unread bytes visible instead of inferring correctness from a matching pose |
| Instrumentation changes game timing | Measure callback and frame time; narrow or sample before adding machinery |
| A stale pointer crashes the game | Validate pages, cap reads, catch faults, report failures, and fail closed |
| Pointer reuse joins unrelated actors or sources | Record construction and unload events and use scoped generation identities rather than raw addresses |
| One final pose hides multiple contributors | Record every evaluator call and group contributions under the enclosing pose-build generation |
| Render visibility omits an actor | Distinguish fired evaluation, completed pose build, and final draw coverage instead of treating no draw as no animation |
| Raw evidence becomes difficult to query | Keep records self-bounded and add a disposable index only for a demonstrated slow query |
| Infrastructure expands faster than evidence | The chronological order is binding and only one task is current at a time |
