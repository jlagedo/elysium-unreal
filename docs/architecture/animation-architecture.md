# Animation architecture — VtMB pose semantics on the Unreal animation runtime

This document owns the Unreal design of Elysium's character animation path. The
engine-neutral facts it consumes live in `docs/vtmb/animation_and_movers.md` (channel
decode, split inheritance, hierarchy composition, the root/entity transform),
`docs/vtmb/procedural_bones.md` (the axis-interpolation rule and the persistent pose array)
and `docs/vtmb/mdl_v2531.md` (the format those rules read). Status and implementation order
live only in `docs/project/roadmap.md`.

The goal is not to reimplement Source's animation system inside Unreal. It is to let Unreal
own decoding, blending, skinning and LOD, and to add only the composition stages Unreal has
no equivalent for.

## 1. The two pipelines have the same shape

This is the load-bearing observation, because it is what makes a bespoke evaluator
unnecessary:

| Retail | Unreal |
|---|---|
| decode locals | `UAnimSequence` evaluation |
| blend sequences, layers, transitions | blend spaces and AnimGraph blends |
| compose hierarchy, split inheritance | post-process Anim Blueprint, component space |
| apply the procedural rule | post-process Anim Blueprint, component space |
| skin | skinning |

A post-process Anim Blueprint runs once per component after the anim graph and before
skinning, which is retail's slot exactly. Both engines blend **locals** and then compose, so
the ordering VtMB requires falls out of Unreal's existing pipeline rather than having to be
recreated inside it.

That ordering is also why the correction cannot be baked into clips: the rule is non-linear,
so evaluating per clip and blending the results is not the same as blending first and
evaluating once (`docs/vtmb/procedural_bones.md`).

## 2. Two skeletal control nodes

Both stages are nodes deriving `FAnimNode_SkeletalControlBase`; `FAnimNode_ModifyBone` is
the minimal reference shape.

1. **Split inheritance.** For each bone carrying `Flags & 0x2`, take rotation from the
   component root rather than the parent, and translation from the parent. The rule and its
   evidence are `docs/vtmb/animation_and_movers.md` A.4a.
2. **Axis interpolation.** For each bone carrying `ProcType == 1`, read the control bone's
   local rotation, evaluate the six-entry three-way blend, and replace the driven bone's
   local transform outright. The rule is `docs/vtmb/procedural_bones.md`.

**Their order in the graph is retail's**: split inheritance first, then axis interpolation,
matching the composition order the capture establishes. On the shipped corpus the two
commute — no rule names a split-inheritance bone as its driven bone *or* its control (0 of
1,535 over the models the runtime loads), and the axis rule is a pure function of a local
rotation while split inheritance changes only component-space composition. So the order is
kept because it is faithful, not because this data could catch getting it wrong. A rule whose
control was a split bone would distinguish them, and none exists.

## 3. Practical shape

- **The cost is negligible.** Twelve to twenty-one driven bones per character, each a few
  dot products and two slerps, against a skinning pass over the same mesh.
- **Resolve bone indices once** in `InitializeBoneReferences`, through `FBoneReference` and
  `FCompactPoseBoneIndex`. Never resolve by name per evaluation.
- **`LODThreshold`** drops the correction on distant characters, where limb twist is not
  resolvable.
- Skeletal controls evaluate in component space while the axis-interpolation rule reads a
  **local** rotation; deriving it as `parentComponent⁻¹ · boneComponent` is cheap, and the
  base class supplies the conversion helpers.
- Declare both nodes thread-safe so they evaluate on animation worker threads with the rest
  of the graph.

**The engine's pose-driver node is the wrong tool.** Its shape matches — a driver bone, an
evaluation space, target poses — but it interpolates with a radial basis function rather
than the sign-selected three-way slerp the rule uses. It would approximate a stage that
reproduces retail to `1e-4`.

## 4. What the export carries, and the basis hazard

Only the rule table is missing. Exported clips already store the raw decoded locals, which
are the same channels retail decodes and then overrides — the correct thing to carry, given
that something downstream does the overriding.

The hazard is basis. The rule's six entries and its **axis index** are expressed in VtMB's
basis, and a change of basis conjugates bone locals: the axis a rule names is not the same
axis after conversion, and may be negated.

So the rule table is converted by **the same exporter and the same conversion functions that
write the character's mesh and clips**, making the two consistent by construction rather
than by agreement. A separately authored native exporter reintroduces exactly the
reconciliation this avoids. One exporter emitting a companion table beside the artifact it
already writes is the arrangement that holds, and the character index already carries a
`split_bones` array as the natural place for it.

**The axis survives as a direction, not as an index.** Under the glTF conjugation Source Y
maps to −Z and Source Z to Y, so a carried-through axis index is wrong on two axes in three —
2,356 of the install's 3,123 rules. Folding the change into the entry ordering does not work
either: terms 1 and 2 are interchangeable under the inner slerp, but term 3 is distinguished
as both the `a1 + a2 == 0` fallback and the outer slerp target, and after conversion the
distinguished term reads glTF Y. The table therefore ships the axis as a converted direction
plus the three converted Source axes, and the runtime takes each term's signed weight as a
dot product. Entry order is the raw record's, and the rule body stays retail's verbatim.

**Consistency has to hold on the import side too.** The table ships in the glb's basis and in
metres, and glTFRuntime conjugates every node by its scene basis and scales translation by
100 — so the sidecar takes the *same* import transform the mesh does, read once from the same
loader configuration rather than restated as a constant. This does not breach "convert
nothing at runtime": `mdl_gltf` is the standing exemption to the `UE_` convention because
glTF is self-describing, and a table written by that exporter in that basis inherits the
exemption. Omitting it costs a factor of 100 on every driven bone's translation, silently.
The check that catches it is the driven bone's own bind position, which arrives independently
through the mesh.

Two properties of the runtime glTF loader matter. Its loader configuration pins axis
orientation and scaling rather than leaving them implicit, and its skeleton configuration
can overwrite the reference skeleton from the asset — the documented way to avoid retarget
drift when bone rotations differ from the authoring tool's. Per-bone data cannot ride inside
glTF `extras`, which the loader does not surface, though its low-level JSON access could
read them if a single-file arrangement is ever wanted.

## 5. Adjacent work

**Blend grids** are the one genuinely absent animation data: the exporter bakes cell
`[0][0]` alone, so the grid axes and the remaining cells have no representation. A 9×1 grid
maps onto a one-dimensional blend space and a 3×3 onto a two-dimensional one, driven by the
sequence's pose parameters. Independent of the composition stages above.

**Mesh construction needs no change.** Runtime skeletal mesh building is possible but buys
nothing here; the mesh path already works and the composition path is what is missing.

**The persistent partial-update behaviour is a divergence.** Retail refreshes only the bones
a mask selects, so bones legitimately carry matrices composed against older roots; the mask
bits are loader-written and cannot be recovered from the installed file
(`docs/vtmb/procedural_bones.md`). Computing every bone each frame is the divergence this
design takes, and it is recorded as one in the owning topic.

**A third stage is coming, on disjoint bones.** VtMB clamps hair, ponytail, mane and breast
bones to an authored per-bone angular limit read from a table in the `.mdl` header
(`docs/vtmb/secondary_motion.md`). It touches none of the bones the two stages above act on,
so it is a further node rather than a change to either — but the solve the limit clamps is
undecoded, so what that node evaluates below the ceiling is not yet designable.
