# Retail Animation Capture and Verification Roadmap

## Goal and ownership

This tracker drives one private instrument against one exact owner-controlled VtMB
retail build, and one executable loop over it:

> **build the capture → run `sp_theatre` → capture → decode and index → inspect against
> the export and the current decoder → name what is missing → close it in the export and the
> runtime, and extend the capture only where closing it needs more evidence.**

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

The request side adds a fifth module with one caveat that travels with it. `vampire.dll`
is the server game DLL, and the install carries two: the file the process maps is a
five-section plaintext image, beside a seven-section `vampire.dll.12` whose extra `stxt*`
sections are the wrapper the retail installer wrote and in which none of the recovered
addresses exist. The capture target is therefore an owner-install image rather than the
shipped bytes. Nothing about the method changes — the profile hash-gates whatever loads,
so an install carrying only the wrapped image fails to activate rather than hooking noise
— but the phrase "original retail modules" covers the other three and not this one.

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
| Complete what the capture records | CAP2 | grouping, actor/model/skeleton census, source attribution, consumed byte spans, the request that caused each pose group |
| Decode and index the streams | CAP3 | one queryable, deduplicated, joinable database that reports its own counts |
| Inspect retail against export and decoder | CAP4 | byte-coverage and per-bone transform differences |
| Close what the difference proves | CAP5 | recovered rules, regressions, facts in the owning `docs/vtmb/` topic |
| Carry a closed rule into the export and the runtime | CAP7 | `sp_theatre` animating in Unreal under the recovered rules |
| Verify facial state where the built face diverges | CAP6 | flex, phoneme and deformed-vertex evidence for a named divergence |

The loop repeats. Each pass extends the capture only where the previous pass named a
missing byte range, an unjoinable identity, or a mismatching stage.

**Capture is the oracle, not the gate.** Two consequences decide what runs when. A rule that
has passed the evidence gate is carried into the export and the runtime immediately, beside
whatever capture work is still open — CAP7 runs alongside CAP4 and CAP5 rather than behind
them. And a system whose format is already closed is **built from that specification and
captured only where the build diverges**: facial state is decoded, exported and specified end
to end (`docs/vtmb/facial_animation.md`), so CAP6 is a response to a divergence rather than a
prerequisite for one.

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

**The delivered outcome is `sp_theatre` playing with every animation live**: skeletal pose
under the recovered composition rules, scene-driven placement, facial flex, eyelids and lip
sync, and whatever moves the secondary-motion bones. A boundary above is *reproduced* when
the offline comparison closes; the program is *finished* when the built act shows it.

## The critical path to a fully animated `sp_theatre`

What stands between the evidence on disk and the act playing with every animation live,
listed in the order work can start rather than in phase order.

| # | What | Owned by | State |
|---:|---|---|---|
| 1 | The two closed composition rules reach the runtime — a rule table out of the exporter, two skeletal controls in retail's own order | CAP7.1, CAP7.2 | **done**; visual isolation still owed |
| 2 | The shipped clip decoder is differenced against retail for the first time | CAP4.3 | **done — the decoder is clean**; what surrounds it is not |
| 3 | The difference instrument models what the export actually writes on multi-biped banks | CAP5.7 | **done**; the shipped path carries no correspondence error at all |
| 4 | The face is built from its closed specification — flex rig, eyelids, lip sync | `docs/project/roadmap.md` 12.3–12.5 | 12.3 done pending a clean visual; 12.4 and 12.5 open |
| 5 | Blend grids and the animation weight are carried out of the model | CAP5.3, then `docs/project/animation-roadmap.md` | CAP5.3 **done** — byte-coverage missing list empty; carrying them to the runtime is the animation tracker's |
| 6 | The include-model remap route | CAP5.8 | **done**; 6,927 → 2,452, and it needed no capture |
| 6b | Frame interpolation — is the host loader's LINEAR what retail does | CAP5.10 | open |
| 7 | Secondary motion — cloth and hair | CAP5.5 | **the mechanism is located**; the clamp is proved, the solve it clamps is not |
| 8 | The persistent partial update is adjudicated | CAP5.4 | open; the replay that measures it exists |
| 9 | **The cast has living eyes** — a plain reproduction; the whole eye system is specified | `docs/project/roadmap.md` 12.4 | open as a build; nothing here gates it |
| 10 | A visual-acceptance harness that actually isolates one body | `pipeline/` green-room path | open; **now blocking rows 1 and 4** |

Nothing on the list is blocked by anything else on it except row 5 feeding the animation
tracker's blend-space bake, and rows 1 and 4 both waiting on row 10 to *demonstrate* what their
numbers already establish. Row 9's
build consumes row 4's flex runtime, but no capture work stands between them.

**Row 3 is a measurement defect, and the distinction cost a round of work.** CAP4.3's
`bone_name` candidate — plain name matching, 130,651 records over the band — was read as the
shipped export path, which made cutscene actors look catastrophically mis-posed. They are
not. The biped family is not a property of the clip at all: a cinematic bank's sequence
animates *every* actor in the scene, so no per-sequence field could carry it, and the witnessed
masks confirm it — `Courtroom_bip3.mdl` sequence 0 is witnessed under four families at once.
The family is a property of the **actor binding**, carried by the choreo scene's per-actor
`bonerename "BipNN" "Bip01"` (`docs/vtmb/choreographed_scenes.md`), and both halves already
ship: the export splits each cinematic model into one bank per `BipNN` root with the prefix
folded back, and the runtime resolves it through the scene actor's own rename pair. Joined
against the capture, that rule agrees with **180,812** witnessed contributions and disagrees
with **0**, and modelling it in the instrument reproduces the `complete` candidate band for
band. The export carries no correspondence error; the instrument did.

**Row 9 is not an owner call — it is a build against a closed specification.** RE34 recovered
VtMB's eye system whole: `StudioEyeball` records on all 301 character models, the renderer pass
that aims the iris and writes the eyelid flexdescs back, the `Eyes` shader's `$vampire` variant,
and the server-side gaze, saccade and blink behaviour whose constants are content in
`vdata/System/DispositionTable.txt`. The eyelid bridge is part of the same record and needs no
adjudication. Two pieces are inert or defective in retail — head turn drives bone controllers no
model declares, and `LookAtEntityCenter` aims at the eye — and those are the only calls left.
The specification is `docs/vtmb/facial_animation.md`. Row 9 stays on the critical path because
the theatre is shot in close-up, but **capture is not what unblocks it**; the build is.

**Both of the path's known unknowns have resolved, and neither resolved badly.** The shipped
decoder did *not* diverge, which retires the largest risk the program carried and makes every
downstream visual discrepancy attributable. And secondary motion did *not* turn out to be
runtime state outside the installed file — it is authored data in the model header, so it is
reproducible rather than an owner call. What replaced them is narrower and better bounded:
four undecoded floats beside a proved angular limit, and the biped-family defect at row 3.

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
8. **Do not build ahead, and do not capture behind.** A later animation system does not
   justify capture infrastructure before the difference report names it. Equally, a rule that
   has passed the evidence gate is carried into the export and the runtime without waiting
   for the rest of its phase, and a system whose format is already closed is built from that
   specification rather than re-derived through a capture pass.
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
Storage is not the binding constraint at this scale. CAP3.1 nevertheless removes a quarter of
a finalized database offline, because the duplication is real rather than anticipated: a
cutscene draws the same actor several times a frame and decodes the same channel bitmaps over
and over, so **42%** of one run's payload bytes repeat bytes it already holds.

## Priority and chronological order

| Order | Priority | Phase | Outcome |
|---:|---|---|---|
| 1 | P0 — done | CAP1 — first run and calibration | The instrument that exists produces one finalized `sp_theatre` database, and its measured rates, counts, and joins replace every estimate |
| 2 | P2 — deferred | CAP2 — complete the capture | Contributions group, actors and skeletons are identified, every fired contribution names its source owner, indices, and consumed byte spans, and each pose group names the request that caused it. CAP2.1–CAP2.7 landed; only the two off-path tasks below remain |
| 3 | P0 — done | CAP3 — decode and index | One deduplicated, joinable database answers per-actor and per-time questions without re-running the game, and reports its own counts |
| 4 | P0 — CAP4.1–4.3 done | CAP4 — inspect against export and decoder | Byte ranges the runtime reads that we do not, and the first mismatching stage and bone per pose group. The shipped decoder reproduces the corpus; CAP4.4's consolidation is what remains |
| 5 | P0 — current | CAP5 — close what the difference proves | Recovered rules and carried byte ranges, each with a regression and a fact in the owning topic. The decode stage's residual is down to **2,452**, and CAP5.3's blend grids are the ranking item — the largest remaining group is a walk grid whose cells the export never wrote |
| 6 | P0 — current, beside CAP5 | CAP7 — the two composition rules in Unreal | CAP7.1 and CAP7.2 are done. The rest of the skeletal stack — and `sp_theatre`'s animation acceptance — is `docs/project/animation-roadmap.md`'s |
| 7 | P1 — build-first | CAP6 — face and lips | The built face is captured against retail only where it diverges |
| 8 | P2 | CAP8 — trim | Unused probes, readers and fixtures are deleted |

Order is the sequence work may *start* in, and rows 4, 5 and 6 overlap by design: CAP7
consumes rules that already closed, so holding it behind the phases still measuring other
rules buys nothing (Working rule 8).

**CAP2.8 and CAP2.9 are deferred and block nothing.** Both are `sp_tutorial_1` populations —
CAP2.8's unbracketed frame is melee, on `baseball.mdl` and `tireiron.mdl`, which no cutscene
reaches — so neither is on the `sp_theatre` path.

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
  than being attributed to an earlier one. Both reproduce on a second scene that is not a
  cutscene: CAP2.7 measures **108,340 of 166,233** and **44,710 of 76,832** on
  `sp_tutorial_1`, so the cache is a property of the engine rather than of the theatre.
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
  `docs/vtmb/mdl_v2531.md`. The loader-written spans reproduce on a second scene — CAP2.7
  reports **45 of 64** captured images differing from the installed bytes for the same
  checksum, in the same bone and include-model ranges — so they are what the loader fixes up
  in place rather than a property of one map's cast. Identifying the gap's records is
  CAP2.4's remap work.

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
  after, so the cause is not the new records. It does not reproduce: CAP2.7 measures a
  high-water of **160** on a second scene carrying every stream, so 368 is a property of that
  run rather than of the hook set, and the queue is not the binding constraint it looked like.

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
- [x] **CAP2.5 Consumed byte spans.** Convert each contribution's descriptor and animation
  block pointers into offsets within the owning model image and retain the exact spans the
  runtime dereferences. This is what makes CAP4.2 possible; without it a matching pose
  cannot distinguish a correct decoder from a lucky one.

  The task splits deliberately. The probe **witnesses** pointers and decodes nothing; an
  offline walker **derives** the spans from the image using the confirmed rules, importing
  nothing from the exporter's own decoder; and the two are checked against each other. Had
  one walker produced both sides, CAP4.2's byte-coverage difference would compare a decoder
  to itself.

  Two client hooks feed it, `decode_bone_quaternion` and `decode_bone_position`, each
  emitting no record: they fold one bone into a per-thread accumulator that the cell frame
  reads back, so a witnessed per-bone decode costs two bitmaps on the cell rather than a
  record. The cell frame resets the accumulator *before* the original runs, because unlike
  the blend stash it both opens and reads the thing. `uv run elysium research
  resolve_consumed_spans <session>…` walks a finalized database and writes its span
  dictionary back into it; `verify_consumed_spans <session>…` reads it read-only and reports
  witness, roots, frames, gating, spans, dictionary, loader-written overlap and cost.

  **The pointers are solved for, not assumed.** The accumulator latches from the first bone
  a decoder ran for and indexes its bitmaps off that pointer, so which bone that was is
  recorded nowhere. The check solves it independently from each pointer —
  `(record base − studiohdr − animdesc − animindex) / 32` and
  `(bone base − studiohdr − BoneIndex) / 160` — and requires both to divide exactly, land
  inside the owner's bone count, and name the same bone. That verifies the animindex
  indirection, both strides and both base fields together, from two values recorded
  separately and neither produced by the walker.

  Two complete cutscenes carry **2,173,701** and **2,173,009**-record databases with
  **zero** drops, skipped, filtered, stride and nested faults and no incomplete tail on any
  of the five streams. **231,747 of 231,747** and **231,649 of 231,649** witnessed record
  and bone pointers land where the walker predicts; the same counts of witnessed frames
  equal `floor((numframes − 1) × cycle)`; and the same counts of decoded-bone sets equal
  the selected-bone mask of their enclosing sequence. **185** and **188** span sets cover
  **3,449,152** and **3,474,414** bytes of **34** and **35** owner images, every interval
  inside the image it names, with **480,678** and **480,488** contributions resolved and
  none faulted. The **176** shapes the two runs share produce byte-identical spans, so the
  walk is a function of the shape and nothing else; their consumed unions differ by which
  frames each run sampled, which is coverage rather than disagreement.

  The cost is **8.65** and **8.68 MB/s** mean against CAP2.4's 8.60, with the queue at
  **180** and **268** against the 368 CAP2.3 reached — the **12.2M** channel-decoder
  invocations per cutscene cost invocations rather than volume, so the `gActiveHooks` pair
  they carry stays. CAP1.2, CAP1.3, CAP2.1, CAP2.2, CAP2.3 and CAP2.4 all re-establish on
  both databases. Facts: `docs/vtmb/animation_and_movers.md` A.4 and A.4b,
  `docs/vtmb/mdl_v2531.md`; method and measurements:
  `docs/vtmb/vtmb-animation-reverse-engineering.md`.

  **Two engine facts close on the way.** The selected-bone mask reaches the cell unchanged
  through the include-model dispatcher — 463,396 cells across both runs, no exceptions —
  which `dispatch_model_pose`'s partial confidence left open. CAP5.8 later downgrades that
  from structural to measured: the dispatcher **rebuilds** the mask in the include model's
  index space, so the equality is a property of the rigs this corpus fires rather than of the
  dispatch (`docs/vtmb/animation_and_movers.md`). And a cell whose mask selects
  no bone dereferences exactly sixteen bytes and decodes nothing: **10,311** in each run.
  A third run since records **10,353**, so the behaviour is authored and the count is a
  sample — two runs agreeing exactly is what a narrow corpus looks like.

  Three bounds travel with the task. **The walk inside a track is transcribed, not
  witnessed** — the run-skipping and key-selection rules come from the confirmed decompilation
  and are checked here only for staying inside the image and terminating; the pointers above
  them are what the capture refutes. **The sequence descriptor is partly claimed**, so its
  byte totals are a floor and the rest of the 764 bytes stay unknown rather than inert. And
  **no cutscene fires a 3×3 grid**: every multi-blend sequence in both runs is a 9×1, so the
  four-cell path needs a scene with off-center ranged aiming rather than a third theatre run.
  CAP2.7 supplies one and the four-cell walk resolves there.

  Four defects were closed on the way. The span dictionary keyed sets by frame, which on a
  cutscene means 143,610 sets against the 122 that are actually distinct and turned a 4 GB
  capture into a 13 GB one; sets are now keyed without the frame and the per-model union is
  a bitmap. The resolver wrote bare strings into `capture_metadata`, where every value is
  JSON, which broke all four earlier verifiers. CAP2.4's fault check counted any bit in the
  fault word, so this task's new bits read as attribution failures. And the roots check
  assumed the first decoded bone was bone zero. Each has a regression.
- [x] **CAP2.6 Trigger and scene events.** Record every sequence/activity change and
  scene-driven animation request with caller, target entity, and time, so a pose group is
  attributable to what asked for it.

  The request side is in `vampire.dll`, so the capture stands the server game DLL up as a
  fifth hash-gated module. Six inline detours cover the scene lifecycle, the choreographed
  event dispatch, the actor resolver and the animation-set application, and one client
  detour covers the sequence-change point; all seven are inline because the module is
  incrementally linked and a vtable slot holds a thunk. A sixth stream `scene.elscn`
  (`ELSCN1`) carries them in its own `scene_events` and `sequence_changes` tables rather
  than in `records`, because a scene request runs outside every pose-build bracket and is
  neither a dictionary row nor an event on the generation spine. `actor.elact` bumps to
  `ELACT3` to carry each entity's own handle. `uv run elysium research
  verify_scene_requests <session>…` reads a finalized database read-only and reports
  coverage, binding, the join, attribution, the dispatched-event histogram, callers,
  truncation, faults and cost.

  **The join is the result.** A server request and a client pose group name the same entity
  only if the handle each side records independently agrees: the server stores it at
  `CBaseEntity+0x448` and the client at `C_BaseEntity+0xe4`, both packing a 13-bit index
  and a serial. One complete cutscene resolves **470 of 470** actor bindings to an indexed
  entity, reproduces the fixed renderable offset on **195 of 195** sequence changes — a
  population CAP1.3 never measured — and closes **124 of 124** scene actors onto the
  animation set their scene applied, four values from four observation points with none
  derived from another. Facts: `docs/vtmb/choreographed_scenes.md`,
  `docs/vtmb/animation_and_movers.md`; method and measurements:
  `docs/vtmb/vtmb-animation-reverse-engineering.md`.

  The run carries **zero** drops, skipped, filtered, unbracketed, bracket overflow,
  contribution unscoped, scene unscoped, scene overflow and scene truncation, with no
  incomplete tail on any of the six streams and exact byte closure over the scene stream.
  It costs **0.4 MB** of a 4.08 GB database at a queue high-water of **176**, against the
  368 CAP2.3 reached and CAP2.5's 180–268.

  **The request side is reproducible.** Two acquisitions agree on every authored count —
  **20** playback transitions, **113** dispatched events, **40** animation-set
  applications and **173** scopes — and on the join, closing **124 of 124** scene actors
  onto their animation set in both. Compared as authored shape rather than as reached set,
  all **10** scene files present in both runs dispatched an identical ordered sequence of
  event type, param, scene-relative time and actor, with none divergent and none reached
  by one run only. The per-actor resolutions split **280,857 + 496** to exactly the
  **281,353** the other run recorded whole.

  Two measured facts bound what the run proves. **The actor resolver runs every frame, not
  once per scene** — `position_start` makes a scene re-pin and re-resolve its whole cast
  each frame, which is 281,353 resolutions against 40 animation-set applications, so only
  the resolutions a request scope encloses are recorded and the rest are counted. Two runs
  reproduced the uncovered count at **280,857** exactly. And **an activity change is
  observable only as a sequence change**: the selection data is recovered, but no selector
  is located in either module, so the run counts sequence transitions and dispatched
  sequence labels and never an activity.
- [x] **CAP2.7 Second acquisition.** Repeat CAP1.1 with the completed capture and report the
  same integrity counts plus unjoined-record counts. The scene must include off-center ranged
  aiming on both axes: every 3×3 blend grid in the corpus is a weapon aim layer that no
  cutscene reaches, so a second cutscene leaves the four-cell blend path unexercised through
  CAP4. The attribution report's grid distribution and per-sequence cell counts are what show
  whether it fired.

  The scene is `sp_tutorial_1`, entered from an owner save placed inside it and stopped by
  the authored transition to `sm_pawnshop_1`. A `Recipe` record carries everything a scene
  binds — entry command, console markers, stop signals, run-zero rule and durations — so the
  launch, hook, finalization and analysis paths stay scene-independent and `--map` selects
  between them. A `load` recipe restores transition state as part of loading its save, so the
  watcher takes its save-state baseline when its own map marker appears rather than before
  launch; a set snapshotted earlier reads those files as an ending and stops the capture at
  load. Beats are bound to keys and stamped in `boundary.json` apart from the arm stamp,
  because an operator keystroke is a bookmark and not the scene's own start.

  **The four-cell path fires.** One complete run carries **2,766** 3×3 sequence contributions
  off centre on both axes, on pose parameters 2 and 3, beside **2,033** 9×1 and **64** 5×1;
  of the corpus distribution only 2×1 stays unreached. **43,935 of 43,935** contribution
  scopes decode exactly the cells their grid and witnessed axis cells declare, split
  `{1: 39,072, 2: 2,097, 4: 2,766}`, with none disagreeing — two quantities recorded at
  different observation points, neither derived from the other. The four decode sites inside
  `evaluate_sequence_pose` each fire exactly 2,766 times and the two two-cell sites exactly
  2,097, so the one/two/four-cell branch is witnessed at runtime rather than only decompiled.
  Spans resolve over **22** owner images and **49,712 of 49,712** decoded-bone sets equal the
  selected-bone mask of their enclosing sequence.

  The run stops on the authored transition with **zero** drops, skipped, filtered and
  incomplete tails across all six streams, a queue high-water of **160** against CAP2.5's
  368, exact byte closure and a dense sequence counter. **817 of 817** sequence changes
  reproduce CAP1.3's fixed renderable offset on a second map, which is the class of claim a
  cross-scene comparison is entitled to make; cast, model set, scene files and record count
  are not, and the comparators state non-applicability rather than reporting a difference as
  a disagreement.

  **Four populations stay unaccounted, and one cause explains the first two.** 3,926 complete
  pose evaluations — 3,926 `BASE` plus 3,926 `SEQP` plus 3,926 `ANIM`, **11,778 of 586,673**
  records — fire on the render thread with no bracket open, which is the same population the
  attribution report counts as **7,852 of 43,935** unbracketed contributions. Beside them,
  595 records fall outside the interval in which their address meant the model they name, and
  1 of 18 skeletal entities resolves to no drawn entity. CAP2.8 owns the cause.

  `uv run elysium research verify_capture_integrity <session>` reports the roll-up: each
  verifier's own unjoined count against its own denominator, a bucket aggregate that never
  sums across populations because they measure different ones, and a stated reason beside
  every count whose non-zero value is a recorded property of the runtime rather than a
  defect. CAP3.3 stores that roll-up in the database; the tool computes it and still carries
  no writer.

  **CAP4 inspects the theatre corpus first — an explicit owner call.** The blend-coverage
  requirement is met by this database whichever corpus CAP4 reads, so the tutorial run is
  evidence on disk rather than a prerequisite, and completing it is CAP2.9.
- [ ] **CAP2.8 The fourth pose-build frame.** CAP2.1 holds that `C_BaseAnimating::SetupBones`
  and the two engine frames that submit a studio draw are the complete set reaching a pose
  build, and that completeness is what lets an unenclosed record be read as a missing hook.
  Melee combat refutes it: 3,926 pose evaluations owned by
  `models/character/shared/male/baseball.mdl` and `shared/female/tireiron.mdl` run on the
  render thread with `generation` and `generation_entity` both zero. Their callers are the
  ordinary `dispatch_model_pose` and `evaluate_sequence_pose` sites, so the unhooked frame is
  the one above those. Find it by walking the callers of `resolve_virtual_model_pose` that
  reach none of the three, then bracket it beside them. *Acceptance:* the same scene records
  zero unassigned, and the 595 out-of-interval records and the unresolved skeletal entity are
  either closed with it or counted apart with a reason.
- [ ] **CAP2.9 Complete the `sp_tutorial_1` corpus.** The captures that exist cover one
  authored ending. What stays open is whether a longer combat run reaches the 2×1 grids, what
  the melee frame changes once CAP2.8 brackets it, and whether the run's own request
  population closes the way the theatre's does. Deferred behind the theatre track; it blocks
  nothing.

## CAP3 — Decode and index

The three tasks landed as one pass over one corpus, because they interlock: the join schema
wants the payload store's identity, and an audit can only report an *unjoined* count against
the schema that defines what a join is.

The corpus is a working copy, not an acquisition. `golden_theater` is the chronologically
last theatre run copied out of the capture tree and verified byte-identical against the
digest the finalizer recorded, so the passes below rewrite it while the acquisition stays
untouched and available as an independent oracle. Order is pinned finalize → resolve →
compact → index, and each pass records the digest of the file it consumed.

- [x] **CAP3.1 Content-addressed payload store.** `uv run elysium research
  compact_capture_payloads <session>` rewrites a finalized database so each distinct payload
  is stored once by content hash and every event row keeps its place. `records` becomes a
  view over `record_events` and `payloads`, column for column and in the original order, so a
  reader that selects `raw_payload` still receives the exact bytes the probe wrote.

  **Nothing is compressed, and the measurement is why.** Deduplication removes **24%** of the
  file; the best codec over the deduplicated set removes a further **5.4%**, because this is
  float32 matrix data — zlib reaches only 0.844, and byte-transposition helps the draw stream
  (0.830 → 0.784) while actively hurting the three others. That 5.4% would cost an inflate on
  every read of CAP4.3's core loop, permanently. `raw_header` is not a target either: it is
  **2,181,209 of 2,181,209** distinct, because every header embeds the dense global sequence
  counter. Model images were already stored once per checksum by the probe at capture time.

  One complete cutscene holds **2,181,209** records whose **2,467,495,928** payload bytes are
  **566,164** distinct payloads totalling **1,431,297,652** — a ratio of **0.580061** — and
  the database falls from **4,373,876,736** to **3,224,928,256** bytes. The draw stream
  carries the duplication: **0.412** of its bytes are distinct against 0.869 for the composed
  and decoded locals, and the channel bitmaps reduce to **47** distinct payloads across
  242,561 cells.

  **The store is proved against the bytes it replaced, not against its own hash.** Before the
  file is replaced, every stored payload is re-hashed to the key it is filed under, every
  event's digest taken from the *original* bytes is matched to the payload it now points at,
  and one true byte comparison per distinct payload runs against the record that first
  produced it — so the proof does not rest on sha256 being collision free. The view is then
  required to return the same row count and the same **2,467,495,928** byte total the flat
  table did, with no dangling reference, a clean `foreign_key_check`, and a clean
  `integrity_check`. Payload identities are assigned in first-use order over record id, so two
  passes over one input produce the same file — the digest of a compacted database is recorded
  provenance and must not move under it.

  Three consequences travel with the shape. The base table's DDL is derived from the
  catalogue's own stored SQL rather than from `PRAGMA table_info`, which reports neither
  table-level `UNIQUE` nor foreign keys and would have silently dropped
  `UNIQUE(stream_name, ordinal)` — and with it the autoindex two verifiers search on. The view
  reaches the store through a correlated scalar subquery rather than a join, because SQLite
  offers the omit-noop-join optimization only on the non-aggregate path and every verifier
  here aggregates: as a `LEFT JOIN`, `count(*)` alone measured three orders of magnitude
  slower. And a foreign key cannot reference a view, so `record_span_sets` takes its parent
  from the catalogue; SQLite accepts either `CREATE` and refuses only at the first insert,
  which in the resolver's rewrite path lands after the previous dictionary is already dropped.

  **Every reader re-establishes, and most get faster.** Run over the corpus before and after,
  eight of the nine reports are identical field for field; the two differences are the
  calibration's own file size and the inflation ratio it derives from it, which is what the
  pass changes. Aggregating over `records` used to drag the whole capture off disk, so moving
  the payloads out of the event rows pays for itself: the actor verifier runs at **0.37×** its
  former time, the pointer join at **0.53×**, the census at **0.54×**, the calibration at
  **0.58×**, and the sweep as a whole falls from **424 s to 325 s**. Only the span verifier is
  slower, at **1.03×** — it is the one reader that genuinely materializes payloads, and
  first-use identity ordering keeps even that walk reading mostly forward. Three catalogue
  queries had to be corrected for the view to be transparent: `sqlite_master WHERE type =
  'table'` does not match one, which would have dropped every event row from the sequence
  counter's union and turned CAP1.2's loss proof into a false alarm, and would have flipped
  the scene verifier's contribution support silently false.
- [x] **CAP3.2 Join schema.** `uv run elysium research index_capture_database <session>`
  materializes the joins six verifiers each rebuilt as their own TEMP tables, keyed on the
  generation every record already carries. Ten tables: `generation_bracket`, `pose_group`,
  `actor_interval`, `actor_life`, `actor_renderable`, `entity_slot`, `model_identity`,
  `skeleton_bone`, `bone_remap_group`, and `scene_binding`.

  **Nothing is joined by time.** Every key is an identity — a generation, an address and a
  checksum, an entity index, a scope — and a timestamp appears only as the containment
  predicate on a pairing an identity already established, or as a `min`/`max` tie-break inside
  one identity's own group. The established SQL is lifted from the verifier that owns each
  grain rather than paraphrased.

  One complete cutscene builds **824,187** brackets — exactly the PBLD, DBLD and SHDW record
  counts summed, and exactly the run's zero-length-payload row count reached independently —
  over **420,978** pose groups, **54** actor identity intervals across **49** addresses,
  **50** witnessed lifetimes, **49** renderable bridges, **158** model identities, **160**
  decoded skeletons carrying **5,517** bones with none failing, **41** include-model remap
  groups every one of which locates its array inside its image, and **132** scene bindings.
  The 54-across-49 and the 158 are CAP2.3's and CAP2.4's counts reached by a different route.

  **A pose group stores both addresses it is known by.** The bracket owner is the renderable
  subobject and the evaluations name the `C_BaseAnimating`; deriving one from the other would
  erase the relation four separate verifiers exist to establish, so both are stored as
  recorded and the offset between them is measured rather than assumed.

  Three bounds travel with the schema. **`model_identity` resolves a real disagreement**: the
  model census excludes the capture-stop residency sweep from its observation grain and source
  attribution did not, so the same identity could be "observed before first use" in one report
  and late in the other; the spine excludes it, which is the stricter of the two and the one
  whose exclusion is reasoned. **The client side carries no entity serial** — `actor_observations`
  records an index and no serial while the scene tables record both — so a slot is bounded by
  its address's own intervals and the serial is recorded where it exists and stated unknown
  where it does not. And **the bone remap arrays are located, not interpreted.** Their
  addressing is confirmed — an authored offset at `StudioModelGroup`+0x10 relative to the group
  entry, one 56-byte record per bone of the including model — but the nested branch that
  consumes them is an open seam, so the arrays are stored raw as evidence and no field is
  decoded. The join from owner model to include group to array is complete; the meaning of the
  56 bytes is an explicit unknown.

  *Acceptance, answered:* for one instant and one entity the spine names the actor, its model
  and its 60-bone skeleton; the image those bones decoded from; the `Courtroom_bip4_scene.vcd`
  request and the animation set it applied; the pose build running at that instant with both
  addresses it is known by, differing by the expected four; the five contributions it
  evaluated with their owner banks, indices and cycle; the include group mapping
  `npc_allsequences.mdl` onto that skeleton at **60 records of 56 bytes**, one per bone of the
  including model exactly as `docs/vtmb/mdl_v2531.md` states; and the decoded locals, the
  composed pose with its 48-byte root frame, and the 5,760-byte draw that resulted. Nothing in
  that chain is unresolved. The whole corpus builds with **no unjoined population**: five
  counts are non-zero and each carries the reason it is — chiefly the **182,557** pose builds
  that produce no evaluation, which is the bone cache CAP2.1 already measured.
- [x] **CAP3.3 Integrity audit.** The same pass stores the roll-up into `integrity_shard`,
  `integrity_bucket` and `integrity_summary`, so the database reports its own written,
  dropped, truncated, unjoined and incomplete counts, its queue and disk high-water marks, its
  process/map/scene boundary markers, its capture span, and whether cleanup was natural or
  forced, without the verifiers being re-run to learn them.

  `verify_capture_integrity` computes it and still has no writer: re-deriving fifty predicates
  in the pass would fork them from the verifiers that own them, and the first correction to any
  one would silently stop applying. The roll-up is computed **before** the write connection
  opens, because each verifier takes its own read-only connection and running them underneath
  an open write transaction would have them read around it.

  That ordering is also why the spine's own unjoined counts are not among the shards — the
  roll-up can only describe a database with no spine in it — so they are measured with the same
  query the verifier judges them by and stored beside it. `uv run elysium research
  verify_capture_index <session>` reads the result read-only and reports spine coverage,
  payload-store integrity, and stored-versus-declared shard agreement, separating counts that
  are defects from counts whose non-zero value is a recorded property of the runtime.

## CAP4 — Inspect against the export and the current decoder

This phase is the point of the program. It runs entirely offline against the database.

- [x] **CAP4.1 Source join.** Join each captured contribution to the patch-first installed
  bytes by exact model path and checksum plus owner-local sequence and animation indices,
  and to the current exported animation and the CAP0.5 inventory. Report observed coverage
  and every unresolved runtime identity explicitly.

  `uv run elysium research verify_source_join <session>…` reads a finalized database
  read-only and writes one `source-join.json` beside it. **Nothing is written into the
  capture.** The other CAP4 products derive from the capture alone, but this join depends on
  the machine's install and export state, so storing it would put a claim about a moment
  outside the evidence file inside it. The tool reads `model_headers` rather than the spine,
  so a capture carrying the census and contribution streams answers whether or not it has
  been indexed.

  **Coverage is stated twice, because the two denominators answer different questions.** One
  cutscene fires **34** owner models, **45** distinct `(owner, sequence index)` and **53**
  distinct `(owner, animation index)` identities, across **481,683** contribution records —
  six Courtroom cinematic banks carry over 170,000 of them while the two `character/pc`
  owners carry 13. All **98** identities and all **481,683** records resolve to patch-first
  installed bytes, over owners served from both the VPKs and the patch's loose tree.

  **The join is falsified against a second source.** Every captured pointer's image
  displacement equals `LocalSeqIndex + index × 764` or `LocalAnimIndex + index × 72` read
  from the **installed file** — the same predicate CAP2.4 satisfies against the image the
  probe copied out of the process, now answered by a copy of the header the probe never
  touched. Byte for byte, **58** of the 98 descriptors are identical between the two copies
  and **40** differ only in `StudioSeqDesc`+0xc, the activity dword the loader rewrites;
  **none** differs anywhere else. Where the CAP0.5 inventory covers an identity, its
  `descriptor_sha256` and `source_span` — produced by a tool that never saw the capture —
  agree with the installed bytes on every one. The report carries each joined descriptor
  verbatim, so a span is readable without the install it was joined against.

  **The corpus bound is a number.** The run fires **45 of the 2,046** sequences its own
  owners declare, which is what a coverage claim over this corpus may say and no more.

  **What the current export cannot name is the result.** **41 of 45** fired sequences reach
  an exported clip, and the manifest's activity, weight and flags agree with the installed
  descriptor on every one. Four owners are absent from the export entirely —
  `scenery/structural/la/LAmanhole.mdl`, `scenery/structural/doorknoba/drknobantiquel.mdl`,
  `scenery/furniture/computer/monitor_useable.mdl` and
  `hands/male/shared/v_shared_male_hands.mdl` — because the export seeds from entity lists
  and clandoc bodies, which no scenery mover or viewmodel reaches. And **10** fired blend
  cells over **6,878** records, all on the male and female `move_and_ranged` 9×1 walk grids,
  have no exported counterpart, because `local_sequences` bakes cell `[0][0]` alone. The
  inventory covers **8** sequence and **16** animation identities and leaves 74 outside its
  player-body seed.

  Three bounds travel with the report. **The export is measured, not changed**: the tool
  replays `local_sequences`'s own rules over the installed image with indices preserved, so a
  missing clip is attributed to the rule that dropped it — empty label, first-wins lowercased
  dedup, base cell outside `NumLocalAnims` — rather than reported as an unexplained absence.
  Changing the exporter is CAP5.3, which this pass authorizes by naming what is absent and
  why. **Only the install arm can
  fail**: an identity that reaches no installed bytes, or whose pointer misses the array
  position the installed header declares, is a defect because every later comparison would
  read the wrong bytes, while an identity the export or the inventory cannot name is a
  measured shortfall carrying the reason it holds. An unreadable install fails too, since
  resolving those bytes is the task. And **a population no declaration covers is not
  coverage**: a count that is neither a declared defect nor an accounted one fails the
  verdict rather than passing unremarked.
- [x] **CAP4.2 Byte-coverage difference.** Mark the byte ranges of each owning model image
  that the retail runtime dereferenced, and the ranges the current decoder reads. Ranges
  read by retail and unread by us are the missing-data list. Ranges read by neither stay
  recorded as unknown rather than assumed inert.

  `uv run elysium research verify_byte_coverage <session>…` reads a finalized,
  span-resolved database read-only and writes one `byte-coverage.json` beside it.
  **Nothing is written into the capture**, for CAP4.1's reason: our arm is a claim about
  this checkout's decoder at this commit, and the report names it — module hashes,
  tool commit and the decoder's own digest — so a difference is readable against the
  build it is a difference from.

  **The subtrahend is measured, not described.** CAP2.5's walker is a transcription of
  the decompilation, so a second walk written from the same understanding would agree
  with it by construction. `decoder_coverage` instead runs
  `elysium_pipeline.formats.mdl_skel` unmodified and observes it, swapping the module's
  own `struct` global and wrapping the image for the handful of raw-indexing sites. A
  read route it does not model raises rather than returning bytes unrecorded, so a
  decoder change cannot silently shrink our side and read as retail requiring bytes we
  already handle.

  **The result is five fields.** One cutscene consumes **3,430,830** bytes across **34**
  owner images, of which **9,936** are read by retail and by nothing offline:
  `StudioAnimRecord.weight`@0 (9,016 bytes over 34 owners), `StudioSeqDesc.groupsize`@572
  (360), `paramindex`@580 (360), `numblends`@52 (180) and the fired
  `anim[16][16]`@56 cells beyond `[0][0]` (20 bytes over 2 owners). The four
  descriptor figures reproduce CAP4.1's counts by a different route — 45 fired sequence
  identities at 4, 8 and 8 bytes each, and ten 2-byte cells on the two `move_and_ranged`
  owners CAP4.1 named as clips without a counterpart. **The weight is 1.0 on all 2,254
  decoded `(owner, animation, bone)` triples**, so what is missing on this corpus is the
  zero test rather than a value: a zero-weight record would decode as an ordinary one
  instead of the zero output retail writes.

  **The re-walk is what makes the difference trustworthy.** The stored per-model union
  came from one pass over the records; re-walking it from the dictionary that pass wrote
  reproduces it byte for byte — **3,430,830 = 3,430,830** over **143,612** span-set
  frames — so neither pass drifted. **7,993,786** bytes are ours alone, each counted with
  the reason it is: a whole-track walk against a to-frame one (7,358,840), every bone
  against the masked ones (384,176), every declared sequence against the fired ones, and
  the name strings no captured span claims. **35,858,024** bytes are read by neither and
  stay unknown. Zero of the missing bytes are loader-written, and re-running the same
  decode against the installed image rather than the captured one records the identical
  byte set on all **34** owners, so an offline read set is a property of the source rather
  than of the copy it was handed.

  **One containment is checked rather than declared.** An offline walk reads every key of
  every run it enters and retail reads two, and retail's look-ahead sits in the run after
  the one it stopped in, which the offline walk also enters whenever frames remain — so
  the only track byte retail can reach that we do not is the look-ahead running past a
  track's last run. A retail-only track span wider than one two-byte key would be a run
  our walk skipped and is a declared defect; there are none. **Two bytes are that
  look-ahead**, on `scenery/structural/la/LAmanhole.mdl`, and are reported apart from the
  missing list rather than as the `weight` field they landed in. Facts:
  `docs/vtmb/mdl_v2531.md`, `docs/vtmb/animation_and_movers.md` A.4; method and
  measurements: `docs/vtmb/vtmb-animation-reverse-engineering.md` 9.15d.

  Three bounds travel with the report. **The frame is the animation path**: retail's spans
  come from the animation evaluation hooks, so geometry, material, skin and flex reads are
  outside the comparison on both sides rather than measured and found absent. **A byte is
  only in retail's set if this run fired the identity that reads it**, so a range read by
  neither means unread by this run. And **the pass measures the decoder without repairing
  it** — changing the exporter is CAP5.3, which this pass authorizes by naming the five
  ranges.
- [x] **CAP4.3 Transform difference.** For every joined pose-build group evaluate the
  current decoder at the captured identity and time, normalize entity and root placement,
  and compare decoded locals, composed matrices, bone-to-world, and skin palette per frame
  and bone. Report the first mismatching stage and bone, descendant propagation, and the
  worst bone. Numerical bands and comparison rules are owned by
  `docs/vtmb/vtmb-animation-reverse-engineering.md`.

  **The two composition stages are done; the two decode stages are what remains.**
  Bone-to-world and skin palette need no `BASE`-to-contribution binding and no clip decode,
  so they run over the largest population the capture holds and land first — an explicit
  owner call. `uv run elysium research verify_transform_difference <session>…` reads a
  finalized, indexed database read-only and writes one `transform-difference.json` beside
  it. **Nothing is written into the capture**, for CAP4.1's reason, and the report names
  the decoder, the evaluator and the exporter by content hash. The transcription layer and
  every metric live in `decoder_pose.py`, a library beside the tool on the shape
  `decoder_coverage.py` established. Each stage is fed **retail's own input for that
  stage**, so no earlier error cascades into a later stage's numbers.

  **The skin palette closes.** `boneToWorld × StudioBone.poseToBone` reproduces the palette
  retail wrote on **397,796 of 397,796** draws over 141 models — worst translation
  **3.46e-4** source units and worst rotation **2.96e-06°**, with nothing outside the
  excellent band on either metric. The static-bind arm adds why the exporter gets away with
  discarding the stored bind: `poseToBone` is the conventional hierarchy-FK inverse on
  **all 5,517** captured bones, **0** of them over the band.

  **Split inheritance is confirmed against a running engine.** Over **110,082** paired
  records, **0 of 84,202** `Flags & 0x2` bone observations leave the excellent band on
  translation, model-space translation or rotation. The ordinary hierarchy leaves it on
  **8,142** of the same observations, and on rotation only — translation is identical under
  both rules, which is A.4a's pseudocode shape measured rather than assumed. The two
  coincide on the rest because a flagged bone's parent chain often carries no rotation
  relative to the entity transform, so the disagreement is the rule firing rather than the
  rule being marginal.

  **The procedural rule is corroborated from the other direction, and it closes.** The
  hierarchy alone puts **748,666 of 968,910** procedural bone observations outside the
  rotation band against **37,712 of 5,702,073** ordinary ones — all **32** of its clusters
  procedural, and `StudioBone.ProcType` alone separates the two populations. Adding the
  `ProcType == 1` correction `docs/vtmb/procedural_bones.md` owns, as a third candidate
  evaluated inside the composition, takes that to **0 of 968,910** on translation,
  model-space translation and rotation alike. Rotation over the whole stage goes from
  **46,972** excellent records to **107,763**, and **107,654 of 110,082** records now
  reproduce completely. That document reached the rule by decompilation and replay; this
  pass reached the same bones by differencing transforms with no knowledge of it, and then
  reproduces the capture once the rule is applied — two methods, one result.

  **What remains is a rotation-only stage the model does not declare.** **2,428** records
  stay over the band with the complete rule applied, on `left`/`right breast`
  (`Therese.mdl`), `Bone`-chain bones under `Bip01 Head` and `Bip01 Spine1` (`VV.mdl`,
  `Damsel.mdl`), and `Sheriff Sword` (`Cin_Sheriff_Sword.mdl`). None carries `ProcType`,
  `Flags & 0x1` or `Flags & 0x2`.

  Three measurements over `Therese.mdl`'s two breast bones, on the **728** draws that
  select them, say what the divergence is:

  - **no clip animates them** — the captured composed local is the bind pose exactly, to
    `0.0000` in position and `0.002°` in rotation, on every record;
  - **translation composes exactly** — `parent bone-to-world × local` reproduces retail's
    translation to a median `1e-5` and a maximum `1e-4`;
  - **only the rotation diverges, and intermittently** — `right breast` by a median
    **4.64°** and `left breast` by a median `0.0002°`, both peaking at **20°**. Two mirror
    bones behaving differently is state rather than a formula.

  So something replaces the orientation of bones no clip touches and no model field
  declares, leaving their position alone. That is the shape of the axis-interpolation rule
  without the rule, and `docs/vtmb/procedural_bones.md` records that VtMB's `ProcType` enum
  stops at `AXISINTERP` and never reaches the later `JIGGLE`, so a second *declared*
  mechanism is unlikely to exist in the format. What fits is a runtime stage writing
  bone-to-world after the pose build: `FINL` captures the composed locals and the draw
  captures `object+0x5C`, so a write between the two is invisible in the locals and visible
  only in the drawn matrix — which is exactly the discrepancy measured.

  Two hypotheses are already eliminated. **These are not attachments**: none appears in its
  model's `StudioAttachment` array, and `Sheriff Sword` is the sole bone of a separate prop
  model drawn as its own entity, which is entity-level parenting and a different question.
  And **`StudioBone.PhysicsBone` does not distinguish them**: it takes 15 distinct values
  across `Therese.mdl`'s 79 bones and the breast bones share `1` with their parent
  `Bip01 Spine1`, so it is a ragdoll-part index every bone carries and says nothing about
  the live pose.

  *Evidence that would settle it, cheapest first:* correlate the divergence against the
  entity's own motion offline, from the root transforms the capture already stores — a
  secondary-motion solve should go quiet when the actor is still, and the intermittency
  already points that way. If that holds, bracket the window between
  `C_BaseAnimating::BuildTransformations` and the studio draw and record who writes those
  slots, which is a CAP5.2 recipe rather than new capture infrastructure.

  **2,209** records separately keep a definite *translation* mismatch, worst **12.16**
  source units. Which bones carry it this pass does not isolate: `worst_bones` is ranked
  across every candidate, so the deliberately-wrong hierarchy and inverse-bind arms crowd
  the complete rule out of the list. Ranking per candidate is the fix and it is not made
  yet.

  The pass costs **~95 s** over a 3.2 GB database for three bone-to-world candidates and
  two palette ones, because payload identity does the work: 397,796 draws are **99,493**
  distinct comparisons and 110,082 paired records are **95,151**. The capture's digest is
  unchanged by a run.

  **Three instrument defects were closed on the way, each found by the corpus and not by a
  test.** Two identical *zero* matrices — a slot the renderer never wrote — read as a
  perfect translation match and an `acos(-0.5)` = **120°** rotation disagreement, so
  singular slots now leave both metrics under their own name. A rotation metric built on
  `arccos` turned a row **3e-4** short, which is the ordinary float32 noise a captured
  `matrix3x4_t` carries, into **2.4°** of rotation that is not there; the metric now
  normalizes and orthonormality is reported as itself. And per-bone band counts were
  reported under names that claimed records, which multiplied one wrong pose by a 96-bone
  actor. Each has a regression.

  Four bounds travel with the pass. **Neither stage measures shipped code** — Unreal
  composes glTF conventionally through glTFRuntime and `mdl_gltf` regenerates inverse binds
  by ordinary FK, so both stages carry a second candidate whose band counts are the cost of
  the current export rather than a defect of the transcribed rule. **There is no single
  denominator**: every draw reaches the palette stage, but only a draw whose consumed pose
  build produced a composed pose for the same entity and model reaches bone-to-world, and
  the three excluded populations — **243,692**, **39,607** and **139,651** — are counted
  with their reasons. **Maxima and band counts are exact; quantiles are not**, being the
  upper edge of a log-spaced bucket. And **the corpus is one cutscene**, so a bone this run
  never posed is unmeasured rather than correct.

  **The shipped decoder reproduces the corpus, and that is the headline.** Every number
  above grades a rule transcribed for this pass; `mdl_skel` — the decoder the export actually
  runs — had never been differenced against retail at all. Run unmodified at the witnessed
  owner, animation, frame and blend cells it reproduces retail's `BASE` locals over
  **229,101** bound evaluations, leaving **2,452 (1.07%)** over the band once each population
  is given the rule it needs, and **every residual is outside `mdl_skel`**. Worst clean
  figures are ~`1e-5` source units and ~`0.02°`. The largest risk the program carried is
  retired: a visual discrepancy downstream is now attributable rather than ambiguous.

  **What surrounds the decoder is where the work is.** Four candidates, each measured against
  the same population:

  | Candidate | Over the band | What its residual is |
  |---|---:|---|
  | complete — family correspondence + frame interpolation + cell blend | 6,927 | the shared-bank position transform |
  | `include_remap` — plus the include group's position transform | 72,523 | applied where it does not belong |
  | `frame_key` — complete, no frame interpolation | 50,538 | only **15,336 of 242,561** cells fire at a whole frame |
  | `cinematic_split` — **the shipped path** | 6,927 | identical to `complete`, band for band |
  | `bone_name` — plain name matching, a counterfactual | 130,651 | what binding by name *would* cost |

  **The biped family separates the population, but `bone_name` is not the shipped path.** A
  cinematic bank is one skeleton holding several complete actors — `Courtroom_bip3.mdl` carries
  288 bones over four `BipNN` chains — so matching an actor's bones to it by plain name costs
  **491 source units and 119°**, which is what the `bone_name` candidate's 130,651 measures.
  The export does not take that route on these banks: it splits each cinematic model into one
  bank per `BipNN` root and the runtime binds it through the scene actor's `bonerename` pair.
  Reading that candidate as the export's cost is a mistake this entry made, and CAP5.7 now owns
  correcting the instrument rather than the export.

  **The include-model remap is real, and it is a position transform.**
  `StudioModelGroup`+0x10's 56-byte record is a u16 source bone@0, a transform byte@3 and a
  3×4 matrix@8, and A.4b's rule — clear byte copies the position, set byte transforms it,
  rotation copied verbatim either way — holds for the nested include path too. Applying it
  takes `doppleganger←misc` from 1,830 of 1,830 over the band to **0** and `Isaac←stances`
  from 260 to **0**. But **each candidate closes the other's population and breaks it**:
  split per owner, the cinematic bank reads 1,061 over under `complete` against 70,985 under
  `include_remap`, while the shared bank reads 5,801 against 1,326. Which route an evaluation
  took is not something the capture witnesses — the group's virtual sequence range is a
  runtime field no image carries — so both are reported side by side with a per-owner split
  rather than chosen. CAP5.8 owns it.

  **The composed-locals stage separates cleanly.** `FINL` against retail's own `BASE` is
  **227,836** excellent, 124 and 445 of 228,405 — so the transition/layer/controller stage
  nothing offline models touches 0.25% of pose builds, and a decode error is now
  distinguishable from a layer error. 685 builds evaluated more than one sequence.

  **The 2,209 translation mismatches are attributed**, per-candidate-and-metric ranking having
  fixed the crowding. They are `Bone` chains at depth 8–11 on `Therese` (0.38), `Damsel`
  (1.94), `VV` (0.38), `Sheriff` (8.24), `malk_girl_armor_3` (12.16, the worst) and
  `Gangrel_Male_Armor_2` (2.02) — the same bones as the rotation residual, so the
  secondary-motion population diverges in translation as well. `docs/vtmb/secondary_motion.md`
  explains both from one authored angle: the depth growth is the chord `2r·sin(θ/2)`
  lengthening, not an accumulating solve. **The claim that translation composes exactly to
  `1e-4` was measured on `Therese`'s two breast bones and does not hold for the `Bone`
  chains.**

  **The chain, our decode carried to the palette with nothing of retail's fed between**, over
  100,789 records: decoded locals 3,909 → composed locals 433 → bone-to-world 1,428 → skin
  palette 4,450, with **90,569 reproducing completely**. The palette figure is amplification
  rather than a new fault — a within-band rotation error at the world stage becomes a larger
  palette translation through the inverse bind's lever arm.

  Binding is verified four ways and all four are zero: 0 unpaired `BASE`, 0 cycle
  disagreements, 0 renderable-offset disagreements over 239,122 pairs and 242,561 cells, 0
  disputed remap bones. `witnessed_frame_disagrees_with_the_cycle_rule` is **0** —
  `floor((numframes − 1) × cycle)` reproduces every witnessed frame. Excluded and counted:
  10,021 cells that decoded no bone, 1,353 records sampled at the last frame, 2,103,400 bone
  observations with no owner source, 7,638,442 bones outside the selected mask, 123,924 pose
  builds with no draw, 4,319 non-rigid root transforms, and 16 evaluations that are neither
  the first nor the last of their build.
- [ ] **CAP4.4 Missing-work report.** One ranked list of what the run proves is missing:
  unread byte ranges by model, unresolved identities, mismatch clusters by stage, and stages
  the theatre never exercised. It never claims that unobserved animations or continuous blend
  space were covered.

  **It consolidates; it no longer gates.** CAP4.1, CAP4.2 and CAP4.3's composition half each
  named their own missing work explicitly and with counts — four absent owners and ten
  unexported blend cells, five unread field ranges, one 2,428-record secondary-motion
  population with a measured signature — so CAP5.3 and CAP5.5 are authorized by the pass that
  named them rather than by this report. Requiring a further report before work those passes
  already specify would add a rung without adding knowledge. What CAP4.4 still owes is the
  ranking *across* passes and the decode-stage result CAP4.3 has not produced, so that the
  order CAP5 works in is measured rather than chosen.

## CAP5 — Close what the difference proves

**Two task shapes, because the difference reports produced two kinds of finding.** A
*mismatch cluster* is a stage that runs and disagrees; CAP5.1 traces it backward from the
first wrong bone. A *missing-data range* is a field retail reads that nothing on our side
carries, where nothing disagrees because nothing is there; CAP5.3 carries the bytes. Neither
is a special case of the other, and treating the second as the first is why the blend grids
have had no rung to sit on since CAP4.1 named them.

- [ ] **CAP5.1 One mismatch at a time.** Take the highest-ranked cluster, trace backward
  from the first mismatching bone or frame, make the smallest change that explains the
  evidence, add a game-independent regression, and write the confirmed behavior into the
  owning `docs/vtmb/` topic.
- [ ] **CAP5.2 Deeper stage capture on demand.** When a mismatch cannot be explained from
  the retained spans, add the smallest editable raw hook recipe at the decoder, blend,
  remap, procedural, or post-composition site it names. Preserve registers, bounded stack
  and pointed-to spans, original addresses, copied lengths, neighbouring unknown bytes, and
  failures.

  CAP4.3 names the first such site: the window between
  `C_BaseAnimating::BuildTransformations` and the studio draw, where something reorients
  bones no clip animates and no model field declares. It is CAP5.5's third step and runs only
  if the two offline steps above it fail, because a hook costs a capture cycle and they do
  not.
- [x] **CAP5.3 The missing-data ranges, closed at the exporter.** CAP4.2 names five field
  ranges retail reads that nothing offline does, and CAP4.1 names the export shortfall they
  cause. Nothing mismatches here, so CAP5.1's shape does not apply: the work is to carry the
  bytes.

  **Blend grids are all of it but one field.** `local_sequences` bakes cell `[0][0]` alone,
  so `numblends`@52, `groupsize`@572, `paramindex`@580 and the fired `anim[16][16]`@56 cells
  beyond the base go unread and the grid has no representation in the export at all — which
  is why CAP4.1 found ten fired cells over 6,878 records with no exported counterpart, on the
  male and female `move_and_ranged` walk grids. The corpus fires **9×1** grids through the
  theatre and **3×3** grids on the `sp_tutorial_1` weapon-aim layers, on pose parameters 2
  and 3 (CAP2.7). What the exporter must emit is the grid extents, the pose-parameter
  binding, and every cell — data beside the clips rather than a new clip format.

  **`StudioAnimRecord.weight`@0 is a zero test, not a value.** It is 1.0 on all 2,254 decoded
  `(owner, animation, bone)` triples this corpus reached, so what is missing is the branch: a
  zero-weight record decodes as an ordinary one instead of the zero output retail writes.
  Cheap to add, and unvalidatable on this corpus — so it lands with a game-independent
  regression and a recorded statement that no captured record exercises it.

  **What the field means is now settled offline, and the capture corpus was simply the wrong
  population.** Read install-wide it is strictly binary — `{0.0, 1.0}` over 736,208
  `(animation, bone)` records across 4,508 models, with 18,346 zeros — and its zero set is a
  per-bone mask naming the bones a partial-body layer does *not* own. The theatre corpus reads
  1.0 throughout because a cutscene fires no layer sequence, not because the field is inert.
  The fact belongs to `docs/vtmb/animation_and_movers.md`; carrying the mask to the runtime is
  `docs/project/animation-roadmap.md`'s.

  **The missing list is empty.** 9,938 → **0 of 3,430,830** bytes retail dereferences that the
  decoder does not read. The 2 bytes that remain retail-only are the quaternion look-ahead past
  a track end, which CAP4.2 already reports apart from the missing list by design. CAP4.1's ten
  cells reach a baked clip against a **regenerated** export, not an asserted one — 16 models
  author 279 grids, the banks grow 679 → 723 MB, the male bank 602 → 826 clips and the female
  564 → 780, and `verify_source_join` agrees independently: `blend_cell_not_exported` falls from
  10 identities over 6,878 records to **0**, with 80 → 90 of 98 identities reaching a clip. The
  8 that remain are CAP4.1's scenery and viewmodel owners, which no entity list or clandoc body
  seeds.

  **The blend-cell address in `StudioSeqDesc` was transposed, in the resolver and in the doc.**
  Axis 0 takes the fixed 16-`short` row stride: `anim[i0][i1]`. Retail's own records settle it —
  the correct address reproduces the fired set on **8,302 of 8,302** multi-blend contributions
  across the theatre and tutorial captures, while the transposed one reproduces 1,858 and misses
  6,444. A 9×1 grid cannot distinguish them, which is why it survived; a 3×3 names six of nine
  cells wrongly. `docs/vtmb/animation_and_movers.md` A.3 is corrected and the span dictionary
  re-derived over 481,683 contributions with 0 faults and an unchanged byte total.

  **The weight-zero branch ships unvalidated against retail, by construction.** `read_anim`
  reads `weight`@0 and on zero writes an exact zero position and quaternion and skips the
  record. All 2,254 decoded triples in the corpus carry 1.0, so the branch is transcribed from
  the decompiled decoder and exercised only by a synthetic fixture; that statement lives in the
  code and its test rather than only here.

  **CAP5.3 did not move the transform residual, and could not have.** The shipped-path figure is
  **2,452, unchanged**, because `verify_transform_difference` decodes at the witnessed animation
  index and never routes through `local_sequences` — the exporter's base-cell baking was never
  in that path. This is the second, independent refutation of the blend-cell hypothesis CAP5.9
  carried.
- [ ] **CAP5.4 Adjudicate the persistent partial update.** `docs/vtmb/procedural_bones.md`
  records the behaviour and leaves the rebuild call open. Retail's bone-to-world array is
  persistent and each build refreshes only the bones a mask selects, so a drawn skeleton is
  an accumulation across builds — and two theatre models never refresh a complete skeleton at
  any point in the run. An evaluator that composes every bone each frame therefore cannot
  reproduce a draw whose bones were composed against a root transform that has since moved,
  which is a divergence whether or not it is chosen deliberately.

  **Measure it before choosing it.** The accumulation replay reproduces **54,742 of 54,742**
  draws; the same pass with every bone refreshed on every build measures what a
  compose-everything evaluator gets wrong, on which bones and by how much. That number is
  what the owner call needs and nothing else produces it. The replay is promoted into
  `research/tooling/capture/` as part of this task — it is currently the one instrument
  behind a published finding that is not a tracked tool.

  **The alternative to diverging is recomputation, not capture.** The mask bits are
  loader-written and read as unset on disk, but Source computes them from hitboxes,
  attachments and per-LOD skinned vertices, and all three are in the installed image — so
  `0x10` is derivable from the LOD0 vertex references and `0x4` tracks the procedural
  declaration the decoder already parses. Whether recomputing them offline costs less than
  accepting the divergence is the second half of the same call, and it is answerable from the
  measurement.

  *Acceptance:* one measured divergence population with its worst bone and worst error, a
  recorded owner call, and — if the call is to diverge — that divergence written beside the
  faithful behaviour in `docs/vtmb/procedural_bones.md`, with
  `docs/architecture/animation-architecture.md` carrying the number rather than the
  assertion.
- [~] **CAP5.5 Secondary motion — the bones no rule covers.** **The mechanism is located and
  it is authored data**: a count/index pair at `MDLHeader` +396/+400 addressing 28-byte
  per-bone records whose last float is an angular limit in degrees. 107 of 4,445 models carry
  one, 600 records, zero faults. The facts are `docs/vtmb/secondary_motion.md`; what remains
  here is the four undecoded floats beside the limit.

  **The limit is proved, three ways.** 48 of the 50 records across the capture's 7 carrying
  models equal the measured ceiling exactly, and the 2 that differ are both *below* the
  authored value on series too short to saturate. 585 of 600 records are exact multiples of
  5°, with plateau spread of `1e-6` to `1e-3` degrees — a solve does not land on a round
  number to six significant figures across hundreds of consecutive draws. And
  `Smiling_Jack.mdl`'s three 100° records never engage across 1,570 paired draws, which is
  what separates a ceiling from a fixed per-bone offset.

  **Steps 1 and 2 are firm negatives and should not be repeated.** Motion correlation is
  refuted — Spearman medians of `+0.227`/`+0.030`/`+0.225`/`+0.183` spanning both signs, with
  `Damsel` rooted on 673 of 683 samples while pinned at exactly 15.000°. So is a first-order
  lag (`α` spreading 0.002–0.847 where one lag gives one `α`) and a visible spring (lag-1
  autocorrelation `+0.728`, sign-change rate 0.08, no settling). VPhysics is eliminated
  structurally: `Therese`, `VV` and `Damsel` ship byte-identical `.phy` files, and no hair,
  breast or ponytail bone appears in their solids at all.

  **The residual is fully accounted.** 90 bones over 7 models: 47 named by the array, 43
  descendants of a named bone. Descendants inherit it because retail composes a child off the
  pre-correction parent and overwrites the parent's rotation in place, which predicts their
  translation error as the chord of the clamped angle — `2 × 2.181 × sin 5° = 0.380` against a
  measured 0.38, `0.82 + 2 × 2.152 × sin 15° = 1.934` against 1.94, nothing fitted. Two
  independent analyses reached those numbers from opposite ends. `Sheriff Sword` was never
  part of this population and is corrected out of it.

  *Remaining:* `+8` `{9, 30, 60}`, `+12` `0…10`, `+16` `{0.05 … 0.97}` and `+20` `0…7` are
  almost certainly the solve the limit clamps, and nothing establishes that. The corpus
  saturates most frames, so sub-ceiling behaviour is barely sampled. Two routes, and the first
  costs nothing: work the samples that fall *short* of the limit, and look for a model whose
  limit is loose enough to leave the solve visible unclamped — `Smiling_Jack`'s 100° is the
  candidate the corpus already contains. Failing that, the CAP5.2 recipe on the window between
  `C_BaseAnimating::BuildTransformations` and the studio draw.

  *Acceptance:* the four floats named in `docs/vtmb/secondary_motion.md` with a
  game-independent regression, or a recorded statement of what the rebuild does with a proved
  clamp over an undecoded solve. **Reproducing the clamp alone is worth measuring first** —
  the corpus sits at the ceiling on most frames, so a limit-only implementation may be visually
  indistinguishable for the theatre without the solve ever being decoded.
- [x] **CAP5.7 Make the shipped-path candidate model the shipped path.** The difference
  instrument's `bone_name` candidate is documented as what the exporter bakes and the loader
  resolves, but it models the single-bank exporter only. On a multi-biped cinematic owner the
  export takes a different route entirely — one bank per `BipNN` root, bound through the scene
  actor's `bonerename` — so the candidate's 130,651 records over the band measure a path
  nothing takes, and reading it as the export's cost led this tracker to name a defect that
  does not exist.

  **The family is settled and needs no recovery work.** It is not derivable from the `.mdl`
  and no field in it could carry it: a cinematic sequence animates every actor in the scene, so
  the animated-bone family count per sequence is 2–5 and never 1 across all 85 multi-family
  models, and 12 of 45 witnessed `(owner, sequence)` keys carry more than one family. Grouped
  by `(owner, entity)` instead, 84 keys mix no families at all. The carrier is the `.vcd`'s
  per-actor `bonerename` (`docs/vtmb/choreographed_scenes.md`), which joins to the capture at
  **180,812 agreeing and 0 disagreeing** contributions.

  Three pieces of work, in order:

  1. Teach the shipped-path candidate the cinematic split, so its figure measures what ships
     and is comparable against the `complete` candidate's 6,927.
  2. Close the one real divergence the split carries: it drops every owner bone whose head is
     not `BipNN`, where the family rule keeps matching those by plain name. Small, and
     measurable before it is changed.
  3. A regression over the existing split, on a synthetic multi-biped fixture.

  **The shipped path carries no correspondence error at all.** The `cinematic_split` candidate
  reproduces `complete` **band for band** — `{excellent 222,174, investigate 992, definite
  5,935}` on both — with `cinematic_root_unresolved` and `owner_family_ambiguous` at zero over
  all 30 `(entity, bank)` pairs. `bone_name` reproduces its old 130,651 exactly, which is the
  self-check that the pass added a candidate rather than moving the baseline underneath the
  earlier numbers.

  **The dropped non-BipNN bones are measured and deliberately left alone.** 191 of them do carry
  animated channels, so they are not inert in the bank — but only 18 of their 115 names exist on
  any of the install's 485 character skeletons, all 18 come from `creation1_scripted.mdl`,
  `creation1_scripted_both.mdl` and `kiki_carried.mdl`, and no `logic_choreographed_scene` in any
  exported map names those three, so `export_cinematic` never runs on them. Over all 30 fired
  pairs the split reaches exactly the bones the family rule does. The behaviour is pinned by a
  regression rather than changed.

  One bound travels with the candidate: it takes the root from the **owner's witnessed mask**
  rather than from the scene, because the database holds no pose-build to scene-actor join. The
  two agree on 180,812 of 180,812 contributions the scene bindings reach, and the equivalence is
  stated in the candidate's own declaration — but it makes this one candidate capture-bound in a
  way the others are not.
- [x] **CAP5.8 The include-remap route.** Two offline models each closed the other's population
  and broke it — applying the include group's position transform took `doppleganger←misc` from
  1,830 to 0 and the cinematic banks from 1,061 to 70,963; not applying it did the reverse.

  **The answer is neither, globally: the route decides, and each candidate was right on exactly
  the population the route assigns it.**

  | Over the band | `complete` | `include_remap` | `include_route` |
  |---|---:|---:|---:|
  | shared bank, reached through the include tree | 5,801 | 1,326 | 1,326 |
  | cinematic bank, posed on directly | 1,039 | 70,963 | 1,039 |
  | the model's own sequences, local path | 87 | 234 | 87 |
  | **total** | 6,927 | 72,523 | **2,452** |

  **6,927 → 2,452, 64.6% of the shipped path's residual closed**, with both prior candidates
  reproducing exactly so the baseline did not move underneath. 2,452 is also the figure CAP4.3
  reached by hand-picking the rule per population; one rule now mechanizes it.

  **It needed no capture cycle, and the reason is method rather than luck.** "Which route ran is
  unwitnessed" was true of the *decision* and irrelevant: at a bone whose remap matrix moves the
  position beyond the band, the two routes land in different places, so retail's own captured
  `BASE` position discriminates them. Our decode supplies only the source position, so neither
  side is produced by the code that produces the other — CAP2.5's split with retail as the
  witness. **788,799 `(record, bone)` observations, every one decided**: 74,797 transformed,
  714,002 copied, 0 undecided, 0 matching neither, 0 disagreeing with the include graph.

  The control is what makes it per-contribution rather than per-model: `Sheriff ← stances` reads
  transformed over 6,572 observations while `Sheriff ← Embrace_bips2` reads copied over 44,172 —
  same entity, same remap array, opposite verdicts.

  `dispatch_model_pose` goes partial → confirmed, and the record layout, the include-group-only
  reachability, the rebuilt mask and the latent hang in the chain-rebase branch are
  `docs/vtmb/animation_and_movers.md` A.4b.

  Three bounds travel with it. **The include graph is read from the installed `.mdl` headers**,
  the one input this pass takes from outside the database, stated in the tool and counted when
  absent — the intermediate aggregators `npc_allsequences`, `npcsequences` and `allsequences` are
  never an owner, so no hook holds their header and the census carries no image for them. Chains
  run 3–5 deep and **level-1-only application reproduces retail on 74,797 of 74,797**, so the
  intermediate arrays are measurably immaterial rather than proved inert. And evidence-gate
  point 5 is met by independent byte comparison rather than by a repeat; the cheapest closure is
  indexing the `sp_tutorial_1` acquisition — copied out under a `golden_` name and digest-verified
  first, per CAP3's working-copy rule, never mutating the acquisition.
- [ ] **CAP5.10 Frame interpolation.** Only **15,336 of 242,561** cells fire at a whole frame, so
  what happens between keys is most of the corpus. The export defers interpolation to the host
  loader, and the `frame_key` candidate — complete, with interpolation removed — sits at 50,538
  over the band, which measures how much the deferral is carrying rather than how well it does
  it. What is open is whether the host's LINEAR interpolation is what retail does.
- [ ] **CAP5.9 The `Prince_Escort_Male` cluster.** The 1,061 records the include transform does
  *not* explain, concentrated hard: **693 of them — 65% — sit on one two-actor cinematic bank**,
  split 351 on `brujah_Male_Armor_0` and 342 on `Lacroix`, with every other cinematic owner a
  tail of 14 records or fewer. A two-actor bank holding two thirds of a residual is a shape
  rather than noise, and it is not CAP5.8's: applying the include transform takes this population
  the wrong way.

  Undiagnosed and named rather than attributed. **CAP5.8's route evidence does not explain it**
  — the cinematic total is 1,039 under both `complete` and `include_route`, so this population
  survives the correction that closed two thirds of everything else.

  It is no longer the largest remaining group. **`move_and_ranged` at 1,318 across five entities
  is**, and it is **not** the blend-grid gap — that hypothesis is refuted twice over. All **3,107**
  `move_and_ranged` records sit on non-`[0][0]` cells, the 1,789 excellent ones as well as the
  1,318 residual, so the cell does not discriminate; and CAP5.3 closing the export shortfall
  moved the figure by zero, because the difference tool decodes at the witnessed animation index
  and never routes through `local_sequences`.

  **The shape that does discriminate is the entity, not the bank.** The residual fraction runs
  26% for Isaac against 47% for Skelter and 41% for Nines — all three on the *same* male grid,
  all on the 3→4 cell pair — while Therese and VV sit at 46–48% on the female grid. Same bank,
  same cells, different rates per actor. Whatever this is, it is carried by the actor rather than
  by the clip, which is where CAP5.9 should start.
- [ ] **CAP5.6 Theatre-corpus closure.** The engine-neutral evaluator matches the joined
  theatre corpus within the recorded bands, including layered, transition, and
  included-model cases the run exercised. Unknown fields stay preserved and explicitly
  unresolved; they do not block closure unless they change covered output.

  **Closure is stated against the adjudicated baseline, not against retail unconditionally.**
  If CAP5.4's call is to compose every bone each frame, the population that accepted
  divergence explains is counted and bounded rather than matched, and closure means every
  *other* record inside the bands. Stated the old way the task is unachievable by
  construction, because the evaluator it describes does not reproduce the accumulation.

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
| Post-composition writes | a bone-to-world slot the composed locals do not explain: secondary motion, a follow constraint, or any stage running between the pose build and the draw. Signature: the bone's local equals its bind, its translation composes exactly, and only its orientation diverges |

## CAP6 — Face and lips

**The face is built from its closed specification and captured only where the build
diverges.** `docs/vtmb/facial_animation.md` closes the entire chain — the studiohdr facial
block, 44 flex controllers, 60 RPN flex rules, 65 flexdescs, the per-flex target ramp,
`mstudiomouth_t`, the `.lip` grammar and the phoneme→controller tables — and the export
already ships every input: morph targets in each rigged NPC's `.glb`, the rig in
`$ELYSIUM_EXPORT_ROOT/npc/facial/<stem>.json`, 249 phoneme tables under
`$ELYSIUM_EXPORT_ROOT/expressions/`, and 7,136 `.lip` files. Nothing in this phase is a
prerequisite for animating a face. `docs/project/roadmap.md` owns that build as 12.3, 12.4
and 12.5, and it does not wait here.

What this phase owns is the skeletal track's oracle pointed at what the built face cannot
explain: flex weights that the rules do not account for, a phoneme the three-file join cannot
key, a controller with no source, or a mouth that mistimes against the line audio. It fires
on a named divergence and stays closed otherwise.

**Eyes have a faithful baseline and it is already recovered statically.** Every character model
carries `StudioEyeball` records, the renderer aims the iris from the networked gaze point and
writes the eyelid flexdescs back into the flex weights, and the gaze, saccade and blink
behaviour is tuned by `vdata/System/DispositionTable.txt` (RE34 →
`docs/vtmb/facial_animation.md`). So gaze *is* in scope for this phase, on the same footing as
everything else here: capture verifies the built behaviour where it diverges and stays closed
otherwise. Two retail behaviours are inert or defective rather than absent — head turn reaches
no skeleton, and `LookAtEntityCenter` aims at the eye — and a capture that observes either is
confirming a recorded fact, not discovering one.

- [ ] **CAP6.1 One line's resource and object path.** Follow one controlled theatre line
  from its expression, VCD, audio, `.lip`, and model facial bytes through load, runtime
  object construction, timing identity, and the pointers facial evaluation uses.
- [ ] **CAP6.2 Controller and flex stage capture.** Capture raw state before and after the
  expression, phoneme, amplitude-mouth, eyelid and blink stages that actually contribute to
  that line, plus the flex weights and representative deformed vertices they produce. The
  eyelid stage is the renderer's eye pass, so the capture has to straddle `SetFlexWeights` to
  see both the rules' output and the lid values written over it.
- [ ] **CAP6.3 Controlled-line equivalence.** Reproduce controller values, lip timing, flex
  weights, and selected final vertices for that line, then expand only to another line or
  model that exposes a new mismatch.

## CAP7 — Deliver the animation in Unreal

The recovered rules reach the built game here. The design — where the stages sit in Unreal's
pipeline, what the export has to carry, and why the basis forces a single exporter — is
`docs/architecture/animation-architecture.md`.

**This phase's scope is now the two composition stages alone.** Delivering the rest of the
skeletal animation stack in Unreal is owned by `docs/project/animation-roadmap.md`, whose
governing decision is that VtMB's animation data is baked into native Unreal assets and run by
Unreal's animation system rather than reproduced by a bespoke evaluator. CAP7.1 and CAP7.2 are
unaffected by that decision and stay here: they carry the two rules Unreal has no equivalent for,
and both are complete. CAP7.3, CAP7.4 and CAP7.5 are superseded — see each entry.

**It runs beside CAP4 and CAP5, not after them.** Everything below consumes a rule that has
already passed the evidence gate, so none of it waits on the decode stages, the
secondary-motion cause, or the facial build.

- [x] **CAP7.1 Carry the rule table out of the model.** Per model: the driven bone, its
  control bone, the axis, and `pos[6]`/`quat[6]` — the 176-byte `mstudioaxisinterpbone_t`
  emitted as data the runtime reads, beside the `split_bones` inventory the character index
  already carries.

  **The basis is the hazard and it decides the arrangement.** The rule's six entries and its
  *axis index* are expressed in VtMB's basis, and a change of basis conjugates bone locals —
  so the axis a rule names is not the same axis after conversion, and may be negated. The
  table is therefore converted by the same exporter and the same conversion functions that
  write the model's mesh and clips, which makes the two consistent by construction rather
  than by agreement; a separately authored native exporter reintroduces exactly the
  reconciliation this avoids.

  **The axis ships as a direction, not an index.** Under the glTF conjugation Source Y maps
  to −Z and Source Z to Y, so carrying the index through is wrong on **2,356 of the install's
  3,123 rules**. Folding the change into the entry ordering does not work either: terms 1 and
  2 are interchangeable under the inner slerp but term 3 is distinguished as the
  `a1 + a2 == 0` fallback and the outer slerp target, and after conversion the distinguished
  term reads glTF Y. So the sidecar states the extraction as data — the converted axis per
  rule plus the three converted Source axes per file — and the runtime takes each term's
  signed weight as a dot product, leaving entry order and the rule body retail's verbatim.

  **Round-trip is byte-exact, not within a tolerance.** 3,123 of 3,123 rules re-encode to the
  original float32, which required conjugating the quaternions through the quaternion rather
  than through the rotation matrix — the matrix route loses which of the two representatives
  names the rotation and costs about an ulp, neither of which matters to a baked mesh and both
  of which matter to a table read back and re-evaluated.
- [x] **CAP7.2 The two composition stages as skeletal controls.** Both derive
  `FAnimNode_SkeletalControlBase` and run in a post-process Anim Blueprint — retail's slot
  exactly, after the graph blends locals and before skinning. Split inheritance first
  (`Flags & 0x2`: rotation from the component root, translation from the parent,
  `docs/vtmb/animation_and_movers.md` A.4a), then axis interpolation (`ProcType == 1`,
  `docs/vtmb/procedural_bones.md`), at the tail of the anim proxy's evaluation — this runtime
  carries no Anim Blueprint asset, and that is the same slot.

  The correction cannot be baked into clips instead. It is non-linear — sign-selected among
  six entries, two slerps, a `1/(a1+a2+a3)` normalisation — so evaluating per clip and
  blending the results is not the same as blending first and evaluating once. Measured
  mid-crossfade between two of a real body's own clips, bake-then-blend departs from
  blend-then-evaluate by up to **9.6°**. Blend grids, transitions and layered sequences all
  occur in the corpus.

  Bone indices resolve once in `InitializeBoneReferences`; `LODThreshold` drops the correction
  where limb twist is not resolvable. **`split_bones` runtime application is enabled**, so
  12.1's note that it remains disabled no longer holds. Verified three ways: known locals into
  a real compact pose against a second transcription written from the doc pseudocode with its
  own slerp; **44 of 44** rules across three real models landing on the driven bone's own bind
  translation in centimetres; and the blend measurement above. The A/B is not subtle — with the
  stages off, a pedestrian's whole upper body folds about 90° forward.

  Two things the design did not anticipate. **The sidecar takes the glb's import transform**,
  read once from the same loader configuration the mesh is parsed with; without it every driven
  bone's translation is off by a factor of 100, silently, and the bind-position check is what
  catches it. And **the two stages commute on the shipped corpus** — no rule names a split bone
  as driven or as control — so retail's order is kept because it is faithful, not because this
  evidence could catch getting it wrong.

  *Remaining:* a visual acceptance on an isolated body. The A/B above is a street pedestrian,
  and the green-room harness yields no shots because it is passed empty `-GreenRoomAnimSet=`
  and `-GreenRoomBoneRoot=` and takes the cinematic path.
- **CAP7.3 Blend spaces from the exported grids — superseded.** The task and its acceptance are
  unchanged in substance; what changed is that a grid becomes a **baked `UBlendSpace` asset**
  rather than a runtime resolver, which puts it in the character bake alongside the meshes and
  clips. Owned by `docs/project/animation-roadmap.md`. CAP5.3, which carried the grids out of the
  model, is unaffected and remains this tracker's.
- **CAP7.4 Numerical equivalence, separately from the visual — superseded.** It required the
  shipped pose to match retail's numerically. Under the animation programme's governing decision
  the interpolation *between* authored values is Unreal's, so a whole-pose numerical match is not
  a target and a failing comparison would not name a defect.

  What survives is narrower and already covered: CAP4.3 differences retail against the **decoder**,
  which is the claim that still matters — that the authored values are carried correctly. The
  composition stages keep their own numerical checks, which CAP7.2 records as complete. Visual
  acceptance moves to `docs/project/animation-roadmap.md`.
- **CAP7.5 `sp_theatre` animation acceptance — moved.** The delivered outcome is unchanged: the
  theatre act plays with skeletal pose under both composition rules, scene-driven placement, faces
  running their flex rig, lids blinking, mouths on the line audio, and the secondary-motion bones
  doing whatever CAP5.5 adjudicated — judged on one run rather than three, because the cast shows
  all three at once. It moves because it is an acceptance of the delivered stack rather than of a
  capture, and the stack is now owned elsewhere: `docs/project/animation-roadmap.md` judges the
  skeletal half and sequences it after the cinematic path migrates. The scene, camera, audio and
  subtitle halves of the same act remain `docs/project/roadmap.md`'s.

## CAP8 — Trim

- [ ] **CAP8.1 Final trim.** Delete probes, readers, fixtures, controls, and dependencies
  that no retained evidence path or active investigation uses. The community-decoder
  harnesses are a retained evidence path rather than dead machinery:
  `docs/vtmb/procedural_bones.md` and `docs/vtmb/vtmb-animation-reverse-engineering.md` both
  rest on measurements that only a run of Crowbar's and VAMPTools' own sources produces.

## Triggered capture and storage improvements

Responses to measurement, not scheduled prerequisites.

| Observed problem | Smallest allowed response |
|---|---|
| Callback time or heap allocation is measurable | Reuse fixed-size slots or a preallocated pool for the active recipe |
| Queue reaches its byte cap | Narrow spans or filter first, then tune the cap or batching from measurements |
| Immutable resource bytes dominate the trace | Store that blob once by hash and reference its byte range |
| Exact pose payloads dominate the database | Content-deduplicate offline while retaining every event row. Compression is not the response: measured over the corpus it buys 5.4% of the file after deduplication and costs an inflate on every payload read |
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
- a pre-planned ladder for scene lifecycle, save/load, ragdoll, or teardown — each is a CAP5
  case only when the difference report names it. **Secondary motion is no longer among
  them**: CAP4.3 named it with a count, a bone list and a measured signature, so CAP5.5 is
  that clause discharging rather than an exception to it;
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
| Infrastructure expands faster than evidence | The chronological order is binding *inside* a track and one task is current in each. There are two: the evidence loop (CAP4–CAP6) and the delivery (CAP7). Only a rule that has passed the evidence gate crosses between them |
| A closed rule sits unbuilt because the phase it closed in is still open | CAP7 is authorized by the evidence gate rather than by phase completion, and the critical-path table states which rules have passed it |
| A system whose format is already closed is re-derived through a capture pass | CAP6 fires on a named divergence in a built face, never as a prerequisite for building one. The same test applies to any later system whose `docs/vtmb/` topic is complete |
| An evaluator that composes every bone cannot reproduce an accumulated draw | CAP5.4 measures the divergence before it is chosen, and CAP5.6 states closure against the adjudicated baseline rather than against retail unconditionally |
