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
- [x] **CAP0.3 The three evaluation hooks.** `CStudioRender::DrawModel` yields bone-to-world
  plus skin palette; `resolve_virtual_model_pose` yields decoded locals and the selected-bone
  mask; `C_BaseAnimating::BuildTransformations` yields composed locals and the root/entity
  transform. CAP2.1's three bracket hooks are armed beside them.
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

Uncompressed and unfiltered, one captured cutscene costs **3.5 GB** before the CAP2.1
brackets: ~872,000 records over a ~308 s span, split ~396,000 draw records and ~477,000
skeletal contribution records, across 141 distinct runtime studio headers and 416 client
entities. Mean throughput is **8.1 MB/s** and the peak second is **21.4 MB**, so a filter
budget sized on the mean is 2.7× short.

The brackets cost record count rather than volume. They raise one cutscene to **1,693,202**
records and a **3.7 GB** database, but only to **8.3 MB/s** mean and a **22.1 MB** peak
second, because a bracket record carries no bone payload — 48% of the records for roughly
1.4% of the bytes. The writer absorbs it, though the queue high-water rises from **82** to
**178**, so a third of the drain headroom is now spent and the queue is what would go first.
Storage is not the binding constraint at this scale; CAP3.1 stays the sanctioned response if
a later, longer corpus makes volume bite.

## Priority and chronological order

| Order | Priority | Phase | Outcome |
|---:|---|---|---|
| 1 | P0 — done | CAP1 — first run and calibration | The instrument that exists produces one finalized `sp_theatre` database, and its measured rates, counts, and joins replace every estimate |
| 2 | P0 — in progress | CAP2 — complete the capture | Contributions group, actors and skeletons are identified, and every fired contribution names its source owner, indices, and consumed byte spans |
| 3 | P0 | CAP3 — decode and index | One deduplicated, compressed, joinable database answers per-actor and per-time questions without re-running the game |
| 4 | P0 | CAP4 — inspect against export and decoder | Byte ranges the runtime reads that we do not, and the first mismatching stage and bone per pose group |
| 5 | P1 | CAP5 — close what the difference proves | Recovered rules, each with a regression and a fact in the owning topic |
| 6 | P1 | CAP6 — face and lips | The same loop over expression, flex, phoneme, and deformed-vertex state |
| 7 | P2 | CAP7 — handoff and trim | Engine-neutral evaluator feeds Unreal; unused probes and readers are deleted |

**The next and only current task is CAP2.5.**

## CAP1 — First theatre run and calibration

The instrument holds three complete cutscene captures. Every filter, budget, and schema
decision below is measured from them rather than estimated.

`sp_theatre` is a cutscene from end to end: one arrival trigger starts it and it finishes by
loading `sp_tutorial_1`, with no choice to make in between. A console `map` load spawns the
player short of that trigger, which has no targetname, so the operator walks the gap once and
every stage after it is authored.

That walk lands at a different moment every run, so the run is bracketed by its own events
rather than by elapsed time: it stamps the trigger instant from the same performance counter
the hook writes on every record, and it stops on the map transition. Analysis aligns captures
on a zero derived from the stream itself and uses the console stamp only to confirm it, so a
slow walk costs a longer idle prefix and nothing else.

- [x] **CAP1.1 First theatre acquisition.** `uv run elysium research capture_theatre` builds the
  native tools, installs the inert cfg, launches the exact retail build with the probe armed
  before `sp_theatre` loads, captures the cutscene from the arrival trigger to the map
  transition, and finalizes one SQLite database. Three runs each carry ~872,000 records over a
  ~308 s span with **zero drops and no incomplete tail**, all four binary profiles matched,
  every recipe marker present, no level-script traceback, and a stop on the observed
  `sp_tutorial_1` transition rather than the duration backstop. Their record counts span
  **0.16%**, so the cutscene is reproducible enough to diff against the decoder.
- [x] **CAP1.2 Calibration measurement.** `uv run elysium research calibrate_theatre_capture
  <session>…` reads a finalized database read-only and reports identity and integrity, rates
  and volume, the model/header/actor census, the derived run zero, and a cross-run comparison.
  The measured baseline, per complete run: **8.1 MB/s** mean and a **21.4 MB** peak second;
  **141** distinct runtime studio headers over 1–96 bones, of which 29 carry more than 20;
  **367** draw and **49** skeletal client entities; **60** distinct studio sequences; a
  per-actor draw rate spanning **10 rec/s** at the median to **75 rec/s** at the top, so the
  busiest actor is drawn about 2.5× per simulation step; and 1.40× SQLite inflation over the
  raw streams. Record size is a closed form of the bone count, and summing it reproduces each
  stream's file size exactly, so byte accounting is self-checking. The global sequence counter
  is dense with no gaps or duplicates in every run, which proves independently of the hook's
  own counters that nothing was lost between emission and flush. The run zero is derived from
  the stream — the first frame after the map-load batch in which two or more unseen
  `character/pc` models are drawn, which is `chooseSire()`/`castUnderstudy()` — and the console
  stamp is a cross-check the report accepts or rejects rather than a source of truth. A sound
  stamp trails the derived zero by **0.15–0.22 s**, because the console line is written when
  the trigger's script output fires and the cast is drawn a few frames later; a stamp matched
  against a stale console log is tens of seconds out and is rejected. On the derived zero the
  three runs place the courtroom batch within **0.12 s** of each other at 64.6 s.

  The hook accounts for its own writer: a queue depth high-water, a skipped counter for
  callbacks reaching no readable header or pose buffer, a filtered counter for the configured
  checksum filter, and a written-byte total. The finalizer archives `boundary.json` and
  `console.log`, so the run zero and the console evidence travel inside the database.
  Measured: queue high-water **82** records, **zero** skipped, **zero** filtered, and a
  written-byte total agreeing with the finalized stream sizes to the byte.

  Wall-clock span is an operating point of the machine, not a property of the cutscene.
  `host_framerate` pins the simulation step rather than the wall clock, so a slower render
  stretches the same authored sequence over more wall time without changing which step fires
  or what it draws. The pinned rate is **29.3** steps per second; an unfocused game window
  halves it to **16.3**, which stretches the cutscene from ~308 s to ~553 s. Only runs that
  reached the transition are compared on span, and every per-second rate in this tracker is
  quoted at the pinned rate.
- [x] **CAP1.3 Entity-pointer join verification.** `uv run elysium research
  verify_entity_pointer_join <session>…` reads a finalized database read-only and reports
  pointer-space geometry, a cross-field sweep of every pointer the draw record already
  carries, the per-model difference distribution, the verdict, and a cross-run comparison.
  The two streams are **one pointer space**: the draw stream's render-info entity field is
  the skeletal instance pointer **plus 4**. That difference is the only one holding across
  all **39** shared models, and it resolves **49 of 49** skeletal instance pointers to a
  drawn entity in every run. The three runs share **zero** instance addresses yet produce
  the same **−4**, so it is a fixed offset inside the object rather than a heap coincidence
  — the draw side stores an interface subobject 4 bytes into the `C_BaseAnimating`. Address
  alignment corroborates it without any pairing at all: every draw-side value is 4 above an
  8-aligned address (328 at `0x…4`, 39 at `0x…c`) while every skeletal value is 8-aligned.

  The delta is derived from model identity alone. Timing appears only as a labelled
  diagnostic — all 53 matched pairs overlap in lifetime — and no pairing in the report comes
  from a timestamp. None of the five raw `DrawModel` arguments and neither the render-info
  nor `studio_hdr` pointer carries the instance pointer, so the +4 relation is the only
  route between the streams.

  Two bounds travel with the join. **49 of 367** drawn entities have a skeletal counterpart;
  the remaining 318 are drawn without any skeletal evaluation. And **10** addresses served
  more than one model checksum inside a single run, so the join holds only inside an
  address's lifetime. One model group is lopsided —
  `character/npc/common/doppleganger/doppleganger_male.mdl` animates two actors and draws
  one under that model, while the second resolves to a draw under a different model. That
  is model attribution, not a pointer-space failure. The engine fact and the method that
  establishes a relation like it are owned by
  `docs/vtmb/vtmb-animation-reverse-engineering.md`.

## CAP2 — Complete the capture

Each task adds the smallest hook or span that closes one named gap in CAP1's report.
Targets whose prototype confidence is still partial get a Ghidra pass pinning arguments and
calling convention into the case specification before a hook is written.

- [x] **CAP2.1 Pose-build generation identity.** Bracket the confirmed `SetupBones` entry
  and exit with a per-thread generation counter and stamp it on every nested record.
  *Acceptance:* every composed-pose record and its contributing evaluations for one entity
  share one generation, and no record in a full run is unassigned.

  Three frames are bracketed on entry and exit — `C_BaseAnimating::SetupBones` in the
  client, and both engine frames that submit a studio draw, `CModelRender::DrawModel` and
  `CModelRender::DrawModelShadow` — each opening a generation on a per-thread stack that
  every nested record carries. `uv run elysium research verify_pose_build_generation
  <session>…` reads a finalized database read-only and reports assignment coverage, bracket
  integrity, the per-entity statement, the attribution cross-check, and the bracket cost.

  One complete cutscene carries **1,693,202** records across **820,827** generations with
  **zero** drops, skipped and filtered records, **zero** unbracketed records and **zero**
  bracket overflow, exact byte closure, a dense sequence counter, and a stop on the observed
  transition. Every record is assigned; across **419,700** pose builds no generation spans
  several entities or carries an evaluation naming another; all five integrity checks are
  zero. The enclosing generation and the independently carried instance identity agree on
  **249,538 of 249,538** draws whose frame built a pose, with no disagreement, which is what
  makes the grouping evidence rather than convention.

  Two engine frames were needed because the same actor is drawn on both, and the shadow
  frame reaches the studio draw without passing through the ordinary one. That the two are
  the *complete* set is what lets an unenclosed draw be read as a missing hook; the engine
  fact and its evidence are owned by `docs/vtmb/animation_and_movers.md`, the grouping
  method and its measurements by `docs/vtmb/vtmb-animation-reverse-engineering.md`.

  Two measured facts bound what the generation can be asked. **146,247 of 395,785** draws
  are submitted by a frame that built no pose, and **181,751 of 419,700** `SetupBones` calls
  produce no evaluation — both consistent with a bone cache answering a second call for an
  actor already posed this frame. Such a draw is reported as having no pose build rather
  than being attributed to an earlier one. Reproduction on a second cutscene is CAP2.7.
- [x] **CAP2.2 Model and skeleton census.** Once per distinct runtime studio header record
  model path, checksum, bone count, bone names/parents/flags, bind locals, inverse binds,
  and the header span itself; reference it from later events rather than repeating it.
  Record first observation, reuse, and unload.

  The census adds no hook: both existing paths already hold
  the `studiohdr` pointer, so it reads more from a pointer in hand under the four binary
  profiles that were already validated. A third stream `model.elmdl` (`ELMDL1`) carries two
  kinds — one observation per sighting of a header at an address, carrying the full 128-byte
  name, checksum, bone count and index, `Length`, and the include-model array; and one model
  image per checksum. Identity per sighting, bytes once: a model loaded at two addresses
  costs two observations and one image. The image is the whole model, not a header prefix,
  because every `*Index` in the header is an offset from the header base — which is what
  makes CAP2.5's pointer-to-offset conversion a subtraction rather than a second capture pass.
  Bones are decoded offline by the pipeline's own decoder; the probe validates and copies.

  Census rows land in their own `model_headers` and `model_images` tables rather than in
  `records`, because a dictionary entry is not an event and CAP2.1's coverage section counts
  every row it finds. `uv run elysium research verify_model_skeleton_census <session>…` reads
  a finalized database read-only and reports coverage, the header dictionary, reuse,
  residency, decoded skeletons, the truncated-name measurement, the repeated-dictionary cost,
  the source join, and the cost against CAP2.1's baseline.

  One complete cutscene carries **1,696,055** records and **423** census rows — **141** first
  observations, **141** images and **141** resident-at-stop re-reads — with **zero** drops,
  faults, overflow, capped images, unobserved identities and late observations. All **141**
  used `(studio_hdr, checksum)` identities were observed at or before their first use, one
  image per checksum and an image for every checksum. The **141** images decode to
  **2,286** bones whose composed bind and stored `poseToBone` return the identity within
  **1.03e-4**, over 27 split-inheritance bones. The census costs **38.0 MB**, **1.46%** of a
  3.7 GB database, at an unchanged **8.35 MB/s** mean and a **22.0 MB** peak second; the queue
  high-water is **141**, below CAP2.1's 178. CAP2.1 re-establishes on the same database —
  every record assigned, **250,057 of 250,057** posed draws agreeing — and byte closure and
  sequence density still hold across all three streams.

  **The source join is the result.** Every one of the 141 `Length`@140 values equals its
  installed file size and 42 images are byte-identical, so the runtime `studiohdr` is the
  `.mdl` image at offset 0 and a captured pointer minus the header base is a file offset —
  which is what CAP2.5 needed. The other 99 differ in 26,958 bytes confined to
  `StudioBone.Flags`, `StudioMesh.VertexData`, `StudioSeqDesc`+0xc, the include-model
  records, `MDLHeader.Flags`/`NumLocalNodes`, and one unindexed gap written on exactly the 27
  models carrying include models and no other. Those are the ranges CAP4.2 must treat as
  loader-written rather than as source bytes. The bone-flag rewrite is additive only — over
  all 2,286 captured bones the loader sets bits and clears none, and `0x2` is untouched, so
  the split-inheritance rule and the exported `split_bones` inventory read a value the loader
  leaves alone. Facts:
  `docs/vtmb/mdl_v2531.md`. Reproduction on a second cutscene is CAP2.7; identifying the
  gap's records is CAP2.4's remap work.

  Two measurements bound what this run proves. `sp_theatre` loads no model whose name reaches
  the draw record's 64-byte field — the longest is 63 — so that field is lossless for this
  corpus even though 50 of the 4,445 installed models would overflow it. And no studio header
  address served a second model, so the replacement path is implemented and exercised only by
  tests; the 11 client-entity addresses that did serve several models are CAP1.3's population,
  not this one.

  Unload is scoped to what is observable. No case specification declares a model-cache
  free and no hooked target sees one, so the run records first observation, reuse at a
  reused address, and a resident-at-stop sweep, and counts headers that no longer read as
  their own header. A run that stops on the map transition sweeps its headers mid-teardown
  and finds most of them gone, which is the free happening and being seen only as absence.

  **There is no single model-cache free to hook, and residency is the right shape.** A studio
  model's bytes are a cache slot at `model+0xb0`; `CModelLoader::UnloadModel` only drops a
  reference; and the free is a store inlined into `Cache_Alloc`'s eviction loop and
  `Cache_Flush`'s walk, which are exactly the two paths a map change takes, so the standalone
  `Cache_Free` is bypassed. Catching an unload would mean bracketing all three and re-reading
  the recorded headers afterwards, and the event would still be "these headers no longer read"
  rather than a per-model free. Facts: `docs/vtmb/animation_and_movers.md`. That work is
  parked until a difference report names it; actor lifetime never depended on it, since that
  pair is `C_BaseEntity`'s constructor and destructor and CAP2.3 closes on them.

  Three defects were closed on the way. `client.get_studio_hdr` was the one hook-contract
  target with no `source_function_label`, so the generator never checked it against a
  specification; it is now declared in `animation_pose.json`. The pose-build verifier gated
  on every stream's version rather than the two it reads, so a database carrying any further
  stream would have reported itself unjudgeable. And the sequence-density check read only
  `records`, so census rows drawing from the same global counter read as holes and turned
  CAP1.2's loss proof into a false alarm; it now counts every table the counter reaches.
  Each has a regression.
- [x] **CAP2.3 Actor identity and lifetime.** Record each skeletal client entity's
  construction and destruction, classname/target identity where cheaply reachable,
  model/skin/body selection, origin and angles, and draw outcome, under a scoped generation
  so pointer reuse cannot join unrelated actors.

  A fourth stream `actor.elact` (`ELACT2`) lands in its own `actor_observations` table, on
  the same split the model census uses: identity per sighting, keyed by the entity address.
  Three reasons come free from the skeletal evaluators, which already hold the entity, the
  header and the checksum — first sighting, identity change, resident at stop — and two come
  from `C_BaseEntity`'s constructor and destructor. `uv run elysium research
  verify_actor_identity_lifetime <session>…` reads a finalized database read-only and reports
  coverage, the actor dictionary, intervals, the witnessed lifetime, reuse, placement, draw
  outcome and cost.

  One complete cutscene carries **1,692,922** records with **zero** drops, skipped, filtered,
  unbracketed and bracket overflow, zero actor faults or overflow, and zero incomplete tail
  across all four streams. **All 54 actor identities across 49 addresses were observed at or
  before first use**, none late; **five addresses served more than one model and five observed
  identity changes account for all five**; and **no evaluation record falls outside the
  interval in which its address meant the model it names, nor outside a witnessed lifetime of
  that address**. The 49 is CAP1.2's and CAP1.3's skeletal entity count reached independently.

  **The lifetime is what closes CAP1.3's residual.** 615 constructions and 49 destructions
  show **three addresses rebuilt into a new actor under an unchanged checksum** — invisible to
  an identity interval, because only the destructor separates the two actors. The renderable
  sits a constant **+4** above the entity across all 49, measured from two separately recorded
  addresses rather than derived from one.

  Two fields were already arguments the probe discarded. The composed-pose stage's third
  argument is the root/entity transform: **237,903 of 237,903** composed poses now carry it,
  and 256 sampled decode as a rotation and a translation with worst determinant error
  **5.57e-08**. The draw hook's render info is retained as a 32-byte span rather than named
  fields — **38,768 of 38,768** draws agree with the two fields decoded out of it. Facts:
  `docs/vtmb/animation_and_movers.md`; method and measurements:
  `docs/vtmb/vtmb-animation-reverse-engineering.md`.

  The census cost **0.089 MB**, 0.0035% of a 3.5 GB database, at an unchanged **8.42 MB/s**
  mean against CAP2.2's 8.35. The construction hook's volume was the one real risk and one run
  answered it, so no filter was added. CAP1.2, CAP1.3, CAP2.1 and CAP2.2 all re-establish on
  the same database. The queue high-water rose to **368** against CAP2.2's 141 with nothing
  dropped and no cap reached; two probe runs measured 67 before the lifetime hooks and 31
  after, so the cause is not the new records and is not yet established — CAP2.7 is where it
  reproduces or does not.

  Three bounds travel with the task. **There is no targetname on the client**: it is
  server-side, so client identity is the address, the renderable subobject and the model.
  **Construction covers every client entity**, because the hookable pair is the shared base;
  narrowing to actors is an offline join and the 562 addresses constructed but never posed are
  counted apart. And **the render info's two 16-bit fields are captured, not named** — every
  draw in an idle capture carries their `0xffff` default, so what they select is open.

  Two defects were closed on the way. `verify_entity_pointer_join` grouped bracket records,
  which carry neither checksum nor bone count, into a null-checksum group whose bone count no
  row could supply, so it failed on any bracketed capture and counted the `CModelRender`
  singleton as an entity; both sites now read only records that name a model. And a
  construction target chosen from the vtable carrying the pose slots turned out to belong to
  one concrete class rather than to every skeletal entity; it is retained as an eliminated
  lead in `animation_pose.json` with the evidence that killed it. Each has a regression.
- [x] **CAP2.4 Source attribution per contribution.** For every fired contribution record
  the resolved owner studio header and model identity, owner-local sequence and animation
  indices, active blend cells and weights, cycle and playback time, pose parameters,
  selected-bone mask, trigger caller, and copy failures. Repeated calls stay separate
  events; shared payload storage never erases timing or call multiplicity.

  A fifth stream `contribution.elcon` (`ELCON1`) carries two kinds — one per fired sequence
  and one per decoded blend cell — as events in `records` rather than as a dictionary,
  because a contribution sits on the same generation spine as the evaluation it nests
  inside. Three new client hooks feed it. `evaluate_sequence_pose` gives sequence identity,
  cycle, pose parameters and the selected-bone mask; `decode_selected_bones` gives the
  animation descriptor of each cell that fired; `resolve_blend_axis_weight` emits no record
  at all and stashes its axis result for the sequence frame to fold in, so witnessed blend
  weights cost no record volume. A contribution scope opens before the sequence frame runs,
  because the cells it decodes reach the queue while it is still on the stack. `uv run
  elysium research verify_source_attribution <session>…` reads a finalized database
  read-only and reports coverage, owner census closure, index and pointer range, blend
  closure, caller attribution, multiplicity, faults and cost.

  **The owner is witnessed, not inferred.** All four frames below
  `resolve_virtual_model_pose` share one seven-dword `__cdecl` contract that takes the
  owning `studiohdr` as argument zero, so a hook reads it rather than deriving it from the
  entity — which for a character is usually a shared bank that is nobody's entity model.

  Two complete cutscenes carry **2,171,485** and a comparable record count with **zero**
  drops, skipped, filtered, unbracketed and bracket overflow, **zero** contribution faults,
  overflow and unscoped records, and no incomplete tail on any of the five streams. They
  record **238,529** and **238,793** sequence contributions plus **241,969** and **242,227**
  decoded cells, every one scoped and inside a pose build. Across both, **961,518 of
  961,518** contributions satisfy `pointer - studiohdr == LocalSeqIndex + index * 764` or
  `LocalAnimIndex + index * 72` with every index inside the owner's declared count — the
  check that makes this evidence rather than convention, since it verifies displacement,
  stride and owner attribution together. **34** and **35** owner identities were observed at
  or before first use with an image each, **17** and **18** of them owners no actor animates
  under. **3,440** and **3,434** sequence contributions are multi-blend, every one a 9×1
  grid firing two adjacent cells, with **zero** unwitnessed weights. Caller addresses resolve
  to four sites, all of them the instruction after a call identified statically beforehand.

  The cost is **89 MB**, 3.4% of a 4.2 GB database, at **8.60 MB/s** mean against CAP2.3's
  8.42; the queue peaked at **239** and **357** against the 368 CAP2.3 already reached, so
  the contribution stream never became the binding constraint. CAP1.2, CAP1.3, CAP2.1,
  CAP2.2 and CAP2.3 all re-establish on both databases, and CAP2.2's census grows from
  **141** used identities to **158** and **159** observed — the difference being exactly the
  bank models only this path can see. Facts: `docs/vtmb/animation_and_movers.md` A.3 and
  A.4b, `docs/vtmb/mdl_v2531.md`; method and measurements:
  `docs/vtmb/vtmb-animation-reverse-engineering.md`.

  **CAP2.2's open gap is closed on the way.** The 24,234 loader-written bytes before
  `LocalAnimIndex` are not unreachable and are not a gap: they are the include-model bone
  remap arrays, addressed by `StudioModelGroup`+0x10 relative to the group entry. Over the
  27 include-carrying models the offset is byte-identical on disk in **41 of 41** groups, so
  it is authored rather than written, every array base lands inside the region, and the
  arrays span **173,320 of 173,320 bytes — 100.0%** of it.

  Two bounds travel with the run. **An idle prefix exercises no blend at all** — a 90-second
  probe over the theatre before the cutscene fires 8,959 contributions and zero multi-blend
  sequences, so a short run must not be read as covering that path. And **the caller is an
  address, not a name**: which builder asked for a contribution is a lookup against the case
  specification by nearest preceding seed, so an address inside an unlisted function is
  reported unresolved rather than attributed to the seed below it.

  Two defects were closed on the way. A hook signature could not be a fixed byte sequence:
  `evaluate_sequence_pose` begins `MOV AL,[0x104902c9]`, whose absolute operand the loader
  rewrites, and `client.dll` loaded at a different base on every run — so the profile
  contract now declares the relocated span and the backend compares it after adding the load
  delta, keeping the check exact instead of masking the operand away. And CAP2.3's reuse
  check required an identity change for every extra identity at a reused address, which
  faulted a census that recorded the alternative exactly: an address destroyed and rebuilt as
  a different actor. A destruction is a witnessed separation too. Each has a regression.
- [ ] **CAP2.5 Consumed byte spans.** Convert each contribution's descriptor and animation
  block pointers into offsets within the owning model image and retain the exact spans the
  runtime dereferences. This is what makes CAP4.2 possible; without it a matching pose
  cannot distinguish a correct decoder from a lucky one.
- [ ] **CAP2.6 Trigger and scene events.** Record every sequence/activity change and
  scene-driven animation request with caller, target entity, and time, so a pose group is
  attributable to what asked for it.
- [ ] **CAP2.7 Second acquisition.** Repeat CAP1.1 with the completed capture and report the
  same integrity counts plus unjoined-record counts. This database, not CAP1's, is the one
  CAP4 inspects. The scene must include off-center ranged aiming on both axes: every 3×3
  blend grid in the corpus is a weapon aim layer that no cutscene reaches, so a second
  cutscene leaves the four-cell blend path unexercised through CAP4. The attribution
  report's grid distribution and per-sequence cell counts are what show whether it fired.

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
| The operator reaches the arrival trigger at a different moment every run | Bracket the cutscene on its own events: derive the zero from the casting batch in the stream, stop on the map transition, and treat the console stamp as a cross-check the calibration may reject |
| The same cutscene occupies different wall-clock spans on different runs | `host_framerate` pins the simulation step, so a slower render stretches wall time without changing what fires. An unfocused game window halves the rate, and the engine exposes no cvar or launch switch to stop it, so the operator keeps the window focused and the backstop covers the throttled rate with margin; only runs that reached the transition are compared on span |
| Draw and skeletal records occupy different pointer spaces | Measured: they are one space separated by a fixed +4, so the streams join on actor identity as they are. Pointer reuse still bounds that join to an address's lifetime; CAP2.1's generation scopes every record in a run, so the join no longer rests on an address alone |
| Capture volume overwhelms memory or disk | The writer queue reports its depth high-water and the hook reports its written bytes; filter first, measure, then apply one triggered optimization |
| A decoder matches common cases but loses rare flags | CAP2.5 and CAP4.2 make unread bytes visible instead of inferring correctness from a matching pose |
| Instrumentation changes game timing | Measure callback and frame time; narrow or sample before adding machinery |
| A stale pointer crashes the game | Validate pages, cap reads, catch faults, report failures, and fail closed |
| Pointer reuse joins unrelated actors or sources | Record construction and unload events and use scoped generation identities rather than raw addresses |
| One final pose hides multiple contributors | Record every evaluator call and group contributions under the enclosing pose-build generation |
| Render visibility omits an actor | Distinguish fired evaluation, completed pose build, and final draw coverage instead of treating no draw as no animation |
| Raw evidence becomes difficult to query | Keep records self-bounded and add a disposable index only for a demonstrated slow query |
| Infrastructure expands faster than evidence | The chronological order is binding and only one task is current at a time |
