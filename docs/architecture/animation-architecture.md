# Animation architecture — VtMB animation content on the Unreal animation system

This document owns the Unreal design of Elysium's character animation path. The engine-neutral
facts it consumes live in `docs/vtmb/animation_and_movers.md` (channel decode, the per-bone mask,
split inheritance, hierarchy composition, the root/entity transform, the autolayer binding),
`docs/vtmb/procedural_bones.md` (the axis-interpolation rule and the persistent pose array) and
`docs/vtmb/mdl_v2531.md` (the format those rules read). Status and implementation order live only
in `docs/project/animation-roadmap.md` and its roll-up in `docs/project/roadmap.md`.

The goal is not to reimplement Source's animation system inside Unreal. It is to let Unreal own
decoding, blending, skinning and LOD, and to add only the composition stages Unreal has no
equivalent for.

## 1. The two pipelines have the same shape

This is the load-bearing observation, because it is what makes a bespoke evaluator unnecessary:

| Retail | Unreal |
|---|---|
| decode locals | `UAnimSequence` evaluation |
| blend sequences, layers, transitions | blend spaces, layered blends, state machines, montages |
| compose hierarchy, split inheritance | post-process Anim Blueprint, component space |
| apply the procedural rule | post-process Anim Blueprint, component space |
| skin | skinning |

A post-process Anim Blueprint runs once per component after the anim graph and before skinning,
which is retail's slot exactly. Both engines blend **locals** and then compose, so the ordering VtMB
requires falls out of Unreal's existing pipeline rather than having to be recreated inside it.

That ordering is also why the correction cannot be baked into clips: the rule is non-linear, so
evaluating per clip and blending the results is not the same as blending first and evaluating once
(`docs/vtmb/procedural_bones.md`). Measured mid-crossfade between two of a real body's own clips,
bake-then-blend departs from blend-then-evaluate by up to 9.6°.

## 2. The asset set is baked, not built at runtime

Characters are baked into native assets on the `/ElysiumBaked` mount by an editor commandlet beside
the map bake, under the same gitignored, regenerable posture. What the bake produces:

- **One shared biped `USkeleton`** from the `Bip01` hierarchy. VtMB's shared animation banks are
  already Unreal's shared-skeleton model — the whole cast uses one bone naming convention and the
  banks carry clips authored against it — so every bank clip is playable on every character with no
  retargeting at all. Models outside the convention (animals, skeletal props, the wolf form) carry
  their own skeletons.
- **A `USkeletalMesh` per model**, morph targets preserved. A face spans several material primitives
  and glTF morph weights are mesh-level, so a target that spans two materials arrives as one
  same-named piece per primitive; those pieces are **merged**, never first-wins, or a jaw moves and
  leaves its teeth behind.
- **A compressed `UAnimSequence` per clip.** The `_delta` family — the sequences carrying the
  additive studio flags — is marked additive against the reference pose at bake. The composition
  order does **not** carry over: every `EAdditiveAnimationType` pre-multiplies the delta
  (`Delta * Base`, in `AccumulateLocalSpaceAdditivePoseInternal` and the mesh-space path alike),
  while VtMB post-multiplies it (`Base * Delta`, `docs/vtmb/animation_and_movers.md`). Composing
  one of these through `FAnimNode_ApplyAdditive` therefore reproduces the wrong order, and the
  family needs a post-multiplying applier.

  **An additive sequence's raw keys are not what ships.** Setting the additive type makes the
  compressor bake the sequence down by subtracting its base — the skeleton's reference pose —
  before compressing, so the delta has to be written *composed onto* that pose for the subtraction
  to hand it back. A bake that writes the bare delta ships every layered bone rotated by its own
  inverse bind, from a run that logs nothing wrong.
- **Blend profiles** in blend-mask mode, one per distinct per-bone mask. The mask is binary in the
  source data and there are only a handful of distinct masks per bank, so this is a small table on
  the skeleton rather than per-clip data. A profile is named for the bones it owns rather than for
  the clip or the slice that first reached it, so the same mask arriving from two banks resolves to
  one asset and a re-bake cannot rename one out from under a sequence pointing at it.

  **Which mask a clip owns is carried on the clip**, as animation metadata naming the profile. A
  sequence that states which bones it owns can be composed correctly by anything that opens it,
  including the animation editor, which is the same reason the assets are baked at all.

  A masked clip also needs one thing written that the source file states by omission: a bone the
  clip **owns and does not animate** holds its bind pose, which is an authored pose rather than an
  absence, so the bake writes it out as a constant track. Left implicit it would evaluate to the
  shared skeleton's reference pose — whichever body of the family seeded it — and the overlay would
  quietly pull those bones onto another model's bind.
- **Blend spaces** from the exported grids: one-dimensional for a `move_yaw` fan, two-dimensional
  for an aim grid. A sample sits at the axis value its cell declares —
  `paramstart + k·(paramend − paramstart)/(groupsize − 1)` — and the axis spans the grid's own
  range with one grid division per gap between cells, so every cell lands on a division. The pose
  parameter's own range does not enter: it cancels out of retail's axis resolution exactly, leaving
  the grid's range alone, and is consulted only for the wrap.

  **The wrapping axis is authored as duplicate endpoints, and that is how it is baked.** A
  `move_yaw` fan runs −180..180 with the same clip at both ends, so ordinary clamped interpolation
  reproduces retail across the seam. Marking the axis as cyclic instead would make the two ends one
  point carrying two samples, which the engine rejects — silently, by declining the second. Wrapping
  the parameter into range is the caller's job, which is what the parameter's own `loop` states.

  **A grid composes as one thing, so its cells must agree about what they are.** The cells of a
  partial-body aim grid carry the same per-bone mask, which is what lets the whole grid sit behind a
  single layered blend — no blend node masks per sample. The bake refuses to write a grid whose
  cells disagree, because such a grid could not be layered at all and the failure would otherwise
  surface only when the graph was built over it.
- **Morph-target curve metadata**, authored at bake. Registering it at runtime transacts by default
  and reaches an editor transaction buffer that does not exist under the editor executable in game
  mode; authoring it at bake removes both that hazard and its workaround.

The glTF reader stays the same one on both sides of the seam — it runs inside the bake commandlet
rather than at runtime. Its vendored morph-target vertex-base patch is load-bearing and fails
silently when lost, so keeping the same reader keeps that fix in the path.

## 3. The animation graph

An Animation Blueprint per body archetype — biped, animal, skeletal prop — rather than a
hand-written instance:

- **A locomotion state machine** over idle, walk, run, sneak, crouch and air, with real transition
  rules. VtMB's stance banks ship almost no authored transitions (of the gendered disposition
  banks, one carries a single transition clip), so a short engine blend is what reproduces the
  original's feel rather than inventing polish — but it belongs in a transition rule, not in a
  hand-integrated scalar.
- **Layered blend per bone** over the baked blend profiles. This is where VtMB's partial-body layers
  land, and it is what makes a masked bone come from the base pose rather than from the reference
  pose. The binding — which layer rides which base — is exported data the graph consults, not a
  mechanism the graph implements.

  The arithmetic underneath is a **complementary-weight** blend gated per bone by the mask —
  `nlerp(base, layer, s)` on rotation and the matching lerp on translation, `s` the layer's weight
  times the bone's mask bit. An overlay therefore *replaces* the bones it owns rather than adding to
  them, which is what separates it from the additive below and why the two are never the same node.
- **Additive nodes** for the `_delta` family.
- **Montage slots** for one-shots: scripted-sequence clips, scene gestures, disciplines, and the
  cinematic playback path. A slot is also what keeps a gesture layered over a sequence instead of
  replacing it.
- **A post-process graph** carrying the two custom stages below, in retail's order.

Two properties the graph must preserve. A **masked sequence is never selectable as a base clip** —
retail composes those as layers and never selects one, so a base-clip path that can reach one is a
defect. That extends to a whole grid: an aim grid's cells are masked overlays, so it is a layer's
blend space and never a body pose. And **pose parameters drive the blend spaces**, so a nine-cell
fan resolves off its neutral cell only when something writes the parameter.

## 4. Two custom stages, and only two

Both derive `FAnimNode_SkeletalControlBase` and run in the post-process graph.

1. **Split inheritance.** For each bone carrying the split flag, take rotation from the component
   root rather than the parent, and translation from the parent. The rule and its evidence are
   `docs/vtmb/animation_and_movers.md`.
2. **Axis interpolation.** For each bone the model declares as procedurally driven, read the control
   bone's local rotation, evaluate the six-entry three-way blend, and replace the driven bone's
   local transform outright. The rule is `docs/vtmb/procedural_bones.md`.

**Their order in the graph is retail's**: split inheritance first, then axis interpolation, matching
the composition order the capture establishes. On the shipped corpus the two commute — no rule names
a split-inheritance bone as its driven bone *or* its control — and the axis rule is a pure function
of a local rotation while split inheritance changes only component-space composition. So the order
is kept because it is faithful, not because this data could catch getting it wrong.

These two are **correctness, not feel**. With split inheritance disabled a character's whole upper
body folds about 90° forward. They are the irreducible delta between reading VtMB's rigs and not
reading them.

**The engine's pose-driver node is the wrong tool** for the second. Its shape matches — a driver
bone, an evaluation space, target poses — but it interpolates with a radial basis function rather
than the sign-selected three-way slerp the rule uses. It would approximate a stage that reproduces
retail to `1e-4`.

Practical shape: twelve to twenty-one driven bones per character, each a few dot products and two
slerps, against a skinning pass over the same mesh — the cost is negligible. Resolve bone indices
once in `InitializeBoneReferences`, never by name per evaluation. `LODThreshold` drops the
correction on distant characters, where limb twist is not resolvable. Skeletal controls evaluate in
component space while the axis rule reads a **local** rotation; deriving it as
`parentComponent⁻¹ · boneComponent` is cheap and the base class supplies the conversion. Declare
both thread-safe so they evaluate on animation worker threads with the rest of the graph.

## 5. What the export carries, and the basis hazard

Exported clips store the raw decoded locals, which are the same channels retail decodes and then
overrides — the correct thing to carry, given that something downstream does the overriding. Beside
them the export must carry the procedural rule table, the per-bone mask inventory, the layer
binding, and the blend grids.

The hazard is basis. The rule's six entries and its **axis index** are expressed in VtMB's basis,
and a change of basis conjugates bone locals: the axis a rule names is not the same axis after
conversion, and may be negated.

So the rule table is converted by **the same exporter and the same conversion functions that write
the character's mesh and clips**, making the two consistent by construction rather than by
agreement. A separately authored native exporter reintroduces exactly the reconciliation this
avoids. One exporter emitting a companion table beside the artifact it already writes is the
arrangement that holds.

**The axis survives as a direction, not as an index.** Under the glTF conjugation Source Y maps to
−Z and Source Z to Y, so a carried-through axis index is wrong on two axes in three — 2,356 of the
install's 3,123 rules. Folding the change into the entry ordering does not work either: terms 1 and
2 are interchangeable under the inner slerp, but term 3 is distinguished as both the `a1 + a2 == 0`
fallback and the outer slerp target, and after conversion the distinguished term reads glTF Y. The
table therefore ships the axis as a converted direction plus the three converted Source axes, and
the runtime takes each term's signed weight as a dot product. Entry order is the raw record's, and
the rule body stays retail's verbatim.

**Consistency has to hold on the import side too.** The table ships in the glb's basis and in
metres, and the glTF loader conjugates every node by its scene basis and scales translation by 100 —
so the sidecar takes the *same* import transform the mesh does, read once from the same loader
configuration rather than restated as a constant. This does not breach "convert nothing at
runtime": the glTF exporter is the standing exemption to the `UE_` convention because glTF is
self-describing, and a table written by that exporter in that basis inherits the exemption. Omitting
it costs a factor of 100 on every driven bone's translation, silently. The check that catches it is
the driven bone's own bind position, which arrives independently through the mesh.

The mask ships **inside the character container**, as a de-duplicated table the clips index into,
rather than beside it: it is per animation record, so a sidecar would restate the clip list to say
which row belongs to which clip. It is carried as bone **names** rather than indices by the time it
reaches an asset, for the same reason retargeting works by name: a bank's bone order is not a
character's.

**It cannot be reconstructed from the tracks, which is why it has to ship.** A bone the clip does
not own animates nothing, and a bone the clip owns and leaves at its bind pose animates nothing
either, so both reach the exporter with no channel and would arrive as the same absence. The two are
opposite results when the clip is composed as a layer — the first keeps the base pose, the second
replaces it with a bind pose the overlay authored.

## 6. The face is a curve interface

The facial rig writes named float curves flagged as morph-target curves over whatever pose the body
produced; it writes no bones. The eyes read an eyeball bone's transform to build an aim basis and
then write shader parameters, so they write no bones either. The body and the face therefore never
interact — a crossfade between two stances does not touch the face, and a blink does not touch the
pose — and nothing in the graph above changes that.

What the bake owes the face is only this: the morph targets present, the duplicate-target pieces
merged, and the curve metadata authored. `docs/vtmb/facial_animation.md` owns everything else.

## 7. Adjacent stages

**The garment simulation** (`pipeline/src/elysium_pipeline/enhancement/cloth.py` writes its rig) runs
after both composition stages, so the simulation sees the finished skeleton. It is an enhancement
rather than a reproduction — VtMB simulates no cloth — and it is independent of both: a model may
carry either, both or neither.

**Secondary motion is a third stage, on disjoint bones.** VtMB clamps hair, ponytail, mane and
breast bones to an authored per-bone angular limit read from a table in the model header
(`docs/vtmb/secondary_motion.md`). It touches none of the bones the two stages above act on, so it
is a further node rather than a change to either — but the solve the limit clamps is undecoded, so
what that node evaluates below the ceiling is not yet designable.

**The persistent partial-update behaviour is a divergence.** Retail refreshes only the bones a mask
selects, so bones legitimately carry matrices composed against older roots; those mask bits are
loader-written and cannot be recovered from the installed file
(`docs/vtmb/procedural_bones.md`). Computing every bone each frame is the divergence this design
takes, and it is recorded as one in the owning topic.

**Blend-space interpolation is a divergence.** The authored grid states its cells and the parameter
range each axis spans; Unreal interpolates between samples by its own scheme rather than by retail's
cell selection. The authored content is reproduced; the interpolation between authored values is
Unreal's. Recorded in `docs/project/animation-roadmap.md` with the rest of the programme's
divergences.
