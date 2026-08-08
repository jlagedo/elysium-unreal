# SCRATCH — animation spike handoff

**Deliberately outside the project's documentation policy.** A working note, not a tracked doc:
it carries status and TODOs, which the real docs forbid. Delete it when the work lands. Where it
disagrees with `docs/vtmb/` or `docs/architecture/`, those win. `uv run elysium doctor` flags it
as a stray root file; that is expected and this is the file it means.

**§1 is the work — all of it. §2 is one paragraph on what's done.** Everything after that is a
fact, in the present tense, with no status attached. A task anywhere but §1, or a status marker
anywhere but §1 and §2, is a bug in the file.

---

## The goal

Load one character into an empty map and animate it entirely with Unreal's own systems, fed by
assets baked from VtMB's exports. Acceptance:

- `USkeletalMesh` + shared `USkeleton` + `UAnimSequence` on `/ElysiumBaked`, built offline, no
  glTFRuntime at runtime — **done**, and it is the only path
- an `UAnimBlueprint` with a real graph — state machine, blend spaces, additive and layered
  nodes — rather than the native anim instance
- locomotion driven by `UBlendSpace` assets built from VtMB's own blend grids
- correct **in the Content Browser preview and the anim editor**, not only in our runtime
- no bespoke composition rule left in the frame path except the one that genuinely cannot be
  baked (procedural / axis-interp bones)

The bar the owner set: *if we can't author Unreal bone animations correctly, the bake isn't worth
doing.* Self-describing assets are the point.

---

## 1. What's left

Eight items. Everything else in this file is a fact, not a task. Ordered; each says what done means.

**1.2 and 1.3 are the frame path emptying out.** Both are the root `CLAUDE.md` rule "Poses are baked
native" applied to the two rules still living in `Evaluate`: a clip carries a complete Unreal-native
pose, and where its meaning depends on state stored outside it, the bake resolves that state rather
than forwarding the question. Each deletes more code than it adds, and 1.4 is what deletes the
switch they leave behind.

### 1.1 Export the autolayer table, and the weight it carries

The RE is **done**; this is export work. `docs/vtmb/animation_and_movers.md` A.3 decodes the
binding: `numautolayers`@660 / `autolayerindex`@664, four bytes per entry, each a bare sequence
index, only ever on `move_and_ranged` (male 232 declaring sequences / 345 entries, female 229 /
340), 0 out of range, 0 self-referencing, depth 1, fan-out ≤ 2. The uniform pattern is one
`<weapon>_aim_layer` plus one `<weapon>_<action>_delta`. A second binding lives outside the model:
46 masked sequences the DLL selects by an `ACT_*_LAYER_*` activity. Carry the table across the
include DAG with the `numautolayers == 764` bounds hazard gated.

**The weight scalar is part of this item.** The combine is resolved; the scalar the accumulator
receives *from its caller* is not — the file carries no weight, ramp or flags, so it lives in the
DLL. A capture hook on `FUN_10088e10`'s entry settles it. Today C1 takes it as a caller argument
and the green room's slider stands in; the moment a weapon selects its own layers, that stand-in
becomes a guess shipped as behaviour.

**Done when:** a weapon and action select their own `_aim_layer` + `_delta` through the runtime's own
caller, at a weight either measured or named in code as a stand-in; the overlay-first /
additive-second order in §6 is confirmed by the table instead of assumed; and the green room shows
the declared entries, their order and the selecting authority beside what is actually running.

The green room is the instrument, not the caller. The table exports into `blends/<stem>.json` beside
the grids — same 764-byte descriptor — and the panel reads it. Census is offline against the install;
structure is a content-tier assertion; order is what the table itself states. The weight is the only
part retail has to answer, and the first move there is an analysis pass over the finalized captures,
not a new hook: the combine is closed, so base local + layer local + composed local determine the
scalar per bone.

### 1.2 Bake the additives against their named base

§4 is the evidence and the arithmetic. `_split_rotation_tracks` currently skips a `DELTA_SEQUENCE`
clip outright (`UE_mdl_skeletal.py`), and the proxy post-multiplies at evaluation. Both go: the
exporter conjugates each delta by its declared base's rotation, and the bake sets
`RefPoseType = ABPT_AnimFrame` naming that base so the compressor subtracts the same pose the
conjugation used. The pairing is the one §7 states, resolved through the include DAG rather than by
globbing a name.

**Done when:** `FElysiumNpcAnimProxy::EvaluateLayers` has no additive branch, `RequestAdditive`'s
`IsValidAdditive()` gate is gone, and the round-trip
assertion in `Elysium.Content.BakedCharacterParity` reads against the declared base rather than the
reference pose.

### 1.3 Bake the upper-body overlays as mesh-space additives

§3 and §5.3 are the evidence. One of the four masks owns the split bone, and its clips are the only
ones whose ancestors are absent from their own frames. They bake as component-space deltas against
the weapon's declared base; the 3×3 grids become `UAimOffsetBlendSpace`, which is a `UBlendSpace`,
so §5.6's sample placement is unchanged and only the sample kind differs. The family needs no blend
profile (§5.4: an additive's untouched bones are the additive identity).

**Done when:** `FAnimNode_ElysiumSplitInheritance` is deleted along with the `bSplitInheritance`
plumbing and `ElysiumNpcVisual::IsBakedClip`, `RequestGrid` no longer refuses a masked blend space,
and the green room stands an aim grid over a moving host with the torso upright.

### 1.4 Collapse the composition-stage switch

**Done.** The owner retired the glTFRuntime character path, so the mount is the only build of a
character. `FAnimNode_ElysiumSplitInheritance`, the `bSplitInheritance` plumbing,
`ElysiumNpcVisual::IsBakedClip`, `UseBakedCharacters`, the `elysium.BakedCharacters` cvar and the
baked/loader frame split on every sidecar rig are deleted; `elysium.CompositionStages` gates axis
interpolation alone. A stem the export has not covered fails by name rather than falling back.

### 1.5 A gameplay caller

`UElysiumEntityBodies::PlayNpcLayer` is the door and nothing walks through it. No NPC, weapon or
script path asks for a layer or a grid; the green room is the only caller of either.

**Done when:** a body moving with a weapon plays its layers and steers its locomotion grid with no
green room in the loop.

### 1.6 The animation graph

An `UAnimBlueprint` per body archetype: locomotion state machine, blend spaces on the pose
parameters, the additive and layered nodes, transitions timed from the authored `fade`@612
duration (already on the `Seq` namedtuple).

The load-bearing precondition is already measured: every cell of an aim grid shares one bone mask
(§5.4), so a whole grid sits behind one node. No Unreal node masks per sample, so this is the only
shape that works — for the aim family after 1.3 that node is `FAnimNode_AimOffsetBlendSpace`, and
for the three masks that do not own the split bone it is `FAnimNode_LayeredBoneBlend`.

**Done when:** `UElysiumNpcAnimInstance`'s native `Evaluate` tail is gone.

### 1.7 Post-process ABP for axis interpolation

Procedural bones are genuinely runtime — they read the control bone's live orientation, which is
why this one stage can never be baked. A post-process ABP on the skeletal mesh means the preview
applies it too. Needs `UAnimGraphNode_*` wrappers in an editor module.

**Done when:** the Content Browser preview and the anim editor show a correct rig with no game
running.

### 1.8 Empty-map harness and root motion

The green room already does most of the harness (`uv run elysium debug greenroom`, plus the Cog
window). **Done when:** it stands a body with the full graph and steps blends by hand.

Root motion is the open decision beside it.

The green room scans it; nothing consumes it. There is no decision on record either way — whether
clips drive translation, whether the motor does, or how the two reconcile. A state machine built
before this lands will bake an answer in by accident.

**Done when:** the decision is recorded in the doc that owns locomotion, and the graph honours it.

---

## 2. What's already done

Phases A, B and C, plus the blend spaces and the blend profiles. C2 was dropped on evidence — 0 of
14,004 sequences need the pre-multiply branch (§4).

**Five asset kinds ship as self-describing data:** meshes with morph targets, ordinary sequences
needing no runtime rule, additives Unreal hands back as deltas, masked overlays naming their own
blend profile, and grids as blend spaces. What each writes and why is §5; what the runtime does
with them is §6; what breaks if you undo it is §7.

How any of it got fixed is git history and is deliberately not here.

---

## 3. Quirk 1 — split inheritance (bone flag `0x2`)

One bone per biped, always `Bip01 Spine1` (373 of 433 skeletons). At pose time VtMB does:

```
rotation(world[i])    = rotation(rootToWorld) * rotation(local[i])   # parent SKIPPED
translation(world[i]) = TransformPoint(world[parent[i]], p[i])       # parent kept
```

So Spine1's animated rotation is effectively **model-space**, and the whole upper body is rooted
to the character rather than the hips.

A 2004 toolchain bug the engine was taught to tolerate — all 373 flagged bones' `poseToBone`
inverse binds are conventional hierarchy FK, so the skin bind and the live pose disagree in exactly
this one place. **Nothing was authored against the bug except the engine.** The animators posed an
ordinary hierarchy in a DCC; the exporter wrote the result in model space; the engine was then
patched to read it that way. The authored pose is conventional FK and is recoverable, which is why
this is a defect fixed at bake under `docs/project/remaster-direction.md`'s Behaviour test rather
than semantics to reproduce. The governing rule is the root `CLAUDE.md`'s "Poses are baked native".

**Baked out per clip, per frame**, with `world_rot(parent)` composed from that clip's own frame:

```
local'(Spine1) = world_rot(parent)^-1 * local(Spine1)
```

Ordinary Unreal FK then reproduces retail's pose. It does not touch the mesh — the ref skeleton is
already conventional. Children of Spine1 inherit the corrected rotation for free. Covers **1,926
of the 2,044** clips that animate the split bone.

**The remaining 118 are the one case `world_rot(parent)` is genuinely absent from the file**: a clip
whose mask excludes the split bone's ancestors carries no rotation record for them, so there is
nothing to compose and the value only ever arrives from the host at runtime. Exactly **one of the
four distinct layer masks** owns `Bip01 Spine1` — the 49-bone upper-body gate, carried by the
`*_aim_layer` and `*_bobble_layer` families; the 24-bone right arm, 45-bone left arm and 1-bone head
masks do not, so they need no correction at all.

**The bind chain is not a usable substitute, measured.** The chain is `Bip01`, `Bip01 Pelvis`,
`Bip01 Spine`, and retail declines all three — `Bip01` is an ordinary animated bone, not the entity
transform. It alone sits **63 deg** from bind on a weapon idle and **82-99 deg** on walk and run, so
a bind-normalized overlay arrives rotated by the character's own root rotation. Worse than the
value it corrects.

The base has to be a real pose, and the autolayer table names one: the host. So the family derives
per declaring host exactly as §4's additives do, and the asset carries
`AAT_RotationOffsetMeshSpace` so the aim composes in component space. §1.3 is the work.

**The residual is the blend, and only the blend.** A correction across a crossfade cannot be
baked. Measured over genuinely crossfadeable clips (`probe_b1_blend.py`, male locomotion bank,
five weights): 77.3% of bone samples within 0.5 deg, 96.7% within 2, 99.7% within 5, **0% above
10**; worst 5.17 deg / 2.24 cm, for the length of a fade. That is the whole cost of B1.

---

## 4. Quirk 2 — additive composition order (sequence flags `0x4` / `0x10`)

`FUN_10088e10` accumulates one layer onto the running local pose:

```
s = layer_weight * bone_weight            # bone_weight is the anim record's weight@0 mask
if (flags & 0x4) == 0:                    # ordinary layer
    out.quat = nlerp(out.quat, layer.quat, s)
    out.pos  = (1 - s) * out.pos + s * layer.pos
else:                                     # delta / additive
    if (flags & 0x10) == 0:               # FUN_10088d00
        out.quat = normalize( scale(layer.quat, s) * out.quat )   # delta on the LEFT
    else:                                 # FUN_10088d60
        out.quat = normalize( out.quat * scale(layer.quat, s) )   # delta on the RIGHT
    out.pos += layer.pos * s
```

All 118 `_delta` sequences carry `0x14` = `0x4 | 0x10`, so **every VtMB additive
post-multiplies**. Every Unreal `EAdditiveAnimationType` **pre-multiplies**
(`FinalAtom.Rotation = DeltaAtom.Rotation * FinalAtom.Rotation`, `TransformNonVectorized.h:1082` /
`TransformVectorized.h:1163`). Opposite.

**It bakes, because the base is named.** The correction is `Q_add' = Q_R · Q_d · Q_R^-1`,
conjugation by the base rotation — and the base is not a runtime unknown. §7 states it: a delta only
means something over its own base, and the pairing is by name, `<weapon>_<stance>_idle` for
`<weapon>_<stance>_attack_delta`. Conjugation commutes with Unreal's blend-from-identity, so a
delta conjugated against its declared base is exact at any weight over that base, and degrades away
from it exactly as any additive in any engine does. The asset names the same clip through
`RefPoseType = ABPT_AnimFrame`, so what the compressor subtracts and what the conjugation used are
one decision, not two. §1.2 is the work.

An earlier reading of this section concluded the opposite, on the grounds that the base is a
*runtime* value. It is not: the file states it, so it is a bake input.

**The audited flag set is closed** (`probe_post_flag.py`, 4,445 models / 14,004 sequences, loose
tree **plus** the VPKs):

```
selected by FLAG (0x4): 118      0x4 but NOT *_delta : 0
selected by NAME      : 118      *_delta but NOT 0x4 : 0
flag word of every 0x4 sequence:  0x14  x118   (nothing else)
0x4 WITHOUT 0x10 (would need pre-multiply): 0
0x10 WITHOUT 0x4 (bit means something else): 0
```

That is what killed C2: recording the bit would bake a constant and read a constant, and the
pre-multiply branch would be unreachable. The install is frozen — there is no stream of new clips
to defend against. **The flag-set and the name-set being identical also means every name-globbing
probe sampled the complete additive set**, so `probe_order`'s and `probe_delta_shape`'s corpora
were never partial.

**Cost of shipping the wrong order**, kept as the measure of what C1 avoided (`probe_order.py`,
56,128 bone-frames across all 118): 89.1% ≤ 0.5 deg, 4.2% ≤ 2, 4.5% ≤ 10, 1.9% ≤ 45, 0.3% above;
worst 162.9 deg. Mostly invisible, real tail.

**That 162.9 comes from the female bank, and is probably our decode rather than authored data.**
The probe globs every `.mdl` and both banks ship a `move_and_ranged.mdl`, so the two clips named
`twohanded_crouch_attack_delta` are entirely different animations — female `R Calf 117.9 /
R Thigh 83.4` (legs dominate), male `L Forearm 36.8 / R Thigh 2.9` (upper body only). A 118 deg
*delta* on a calf fits §5.2's partial-record bind contamination (p99 = 31 deg) better than an
authored value. Doesn't change C1 — the post-multiply is right either way. **Anything quoting
162.9 has to say which bank it means.**

---

## 5. What the bake writes, and why

### 5.1 Container versions

`VERSION` in `UE_mdl_skeletal.py` and `eskm.py`; `EskmVersion` in `ElysiumSkeletalSource.cpp` must
match. **v2** = split-bone normalization. **v3** = the de-duplicated `"MASK"` table plus an
`i32 mask` per clip (`-1` = owns everything). `FElysiumSkeletalSource` refuses a stale container
rather than misreading it.

### 5.2 What a `_delta` clip's decoded track actually contains

`read_anim` has no `STUDIO_DELTA` branch — it composes positions as `bind + sample*posscale` and
falls unanimated rotation components back to the **bind**. Source SDK's decoder *does* branch.
**Ours is right and v2531 has no delta branch** (`probe_delta_shape.py`, all 118): the decoded
position sits at ~zero while the bind sits 3.3 units away, so the file stores `delta - bind` and
the decoder is supposed to add the bind back. Under Source's semantics the same bytes would decode
to ≈ `-bind` and every additive would yank the skeleton toward the origin.

Three consequences C1 depends on:

- **Delta translations are essentially all zero** — p90 is 0.005 source units; the tail to 17.8 is
  real authored data, not noise.
- **An unanimated channel in a delta clip is a zero delta**, and the exporter drops channel-less
  tracks, so nothing carries a spurious bind translation.
- **A partial rotation record is bind-contaminated by construction** — 2,980 of 6,353 records
  animate only some components and take the bind for the rest (p50 exactly 0, p99 31 deg). It is
  retail's own decode either way, so it is faithful, not a defect. It is also what makes §4's
  162.9 deg outlier suspect.

### 5.3 The mask is `weight`@0, and it needed a format change

Three-state cross-tab over layer clips:

```
weight=1  channels=yes   8071    the overlay's own animation
weight=1  channels=no    1346    INSIDE the mask, holds its BIND pose  <- a real authored pose
weight=0  channels=no    3123    masked OUT, the base pose must survive
weight=0  channels=yes      0    never occurs
```

The exporter drops a channel-less track, so rows 2 and 3 both arrive as "no track". Collapsing
them either drops 1,346 authored bones or stomps 3,123 base-pose ones — hence v3.

**Only 4 distinct masks across all 209 layer clips**, so it exports as a named table plus a
per-clip reference:

```
x173   49 bones  Spine1, Spine2, Neck, Head, both clavicles/arms/all fingers, + the weapon bones
x24    24 bones  right arm + fingers
x8     45 bones  left arm + fingers
x4      1 bone   Bip01 Head          <- lookback_left_layer
```

Male `move_and_ranged` ships 4 over 826 clips, `misc` 1 which de-duplicates into those 4.
(`animation_and_movers.md` A.4's per-bank census of 5 counts the unmasked `-1` pattern no `_layer`
clip uses.) The count of 4 is male-banks-only; the female bank is not in the parity slice.

Two design facts worth keeping: the dominant mask starts at **`Bip01 Spine1`**, the split bone — so
"the upper body is rooted to the character" and "the aim layer owns the upper body" are one
decision, not two. And it includes the weapon prop bones (`Bat`, `bush hook`, `handle`, `gerber`,
`Sledgehammer`, `Cylinder01`, `tire iron`), which is how the held weapon rides the overlay.

### 5.4 Blend profiles (D2, built by C3)

`UBlendProfile` in `EBlendProfileMode::BlendMask` on the shared skeleton, named
`ElysiumLayerMask_<crc32 of the owned bone names>` — content-addressed, so the same mask from two
banks is one asset and re-baking another slice cannot rename it. The clip names its own through a
`UElysiumAnimLayerMask` (`UAnimMetaData`), which is what makes the sequence self-describing rather
than needing a side table.

**Two things the bake must write that the file states by omission.** An owned-but-unanimated bone
holds its BIND pose (1,346 records), so the bake writes those as constant tracks — otherwise they
evaluate to the *shared skeleton's* reference pose, which is whichever body seeded the family. And
an additive gets **no mask asset at all**: its masked bones already contribute nothing through the
additive identity, so a profile there would be an asset nothing reads.

**Every cell of a 3x3 `<weapon>_aim_layer` grid carries the same mask; every cell of a 9x1
locomotion fan is unmasked.** Verified over 135 grids / 6 male banks by a bake that hard-fails on
disagreement, plus `probe_grid_masks.py`. This is what makes D3's layered blend buildable.

Male banks only — the female bank is not in the parity slice. **Nothing needs writing to close
that:** the bake hard-fails on disagreement, so baking a female body *is* the measurement.

### 5.5 The additive round-trip

**Unreal's compressor rewrites an additive sequence's content, and nothing says so.** Setting
`AdditiveAnimType` makes `FCompressibleAnimData::Build` take `BakeOutAdditiveIntoRawData` instead
of `ResampleAnimationTrackData`, which subtracts the base pose (`ConvertPoseToAdditive`,
`Target * Base^-1`, against the *skeleton's* reference pose).

So a `0x4` clip's raw keys are written as `Target = Delta * Base` against the **shared skeleton's**
reference pose, with untouched channels filled with a zero delta rather than a bind value. The base
is not a choice: it is whatever the compressor subtracts. Both evaluation paths then hand the delta
straight back — raw goes through `GetBonePose_Additive`, compressed was subtracted at bake time.

### 5.6 Blend space sample placement

Cell k on axis a sits at `ParamStart[a] + k*(ParamEnd[a]-ParamStart[a])/(GroupSize[a]-1)`. The pose
parameter's own `start`/`end` **cancel exactly** out of the normalize-then-remap pair — `Alpha`
reduces to `(value - ParamStart)/(ParamEnd - ParamStart)` — so only the wrap ever consults the
descriptor. Axis config is `Min/Max = ParamStart/ParamEnd`, `GridNum = GroupSize-1`, `DisplayName`
= the parameter's name.

**`bWrapInput` stays FALSE, including on `move_yaw`.** The fan duplicates its clip at both ends
(`anaconda_aggressive_run` cells `[0,0]` and `[8,0]` are both `run`), so clamped interpolation
already reproduces retail across the seam. Turning wrap on makes -180 and +180 one point with two
samples on it, and `IsTooCloseToExistingSamplePoint` rejects the second — **silently**.

Cell addressing is `anim[i0][i1]`. The transposed reading reproduced 1,858/8,302 against
8,302/8,302 for the correct one — **do not re-derive this**.

The bake reads the sidecar with `FElysiumBlendTable`, the runtime's own reader, so bake and game
cannot disagree about what a grid says.

### 5.7 Two normalization details that are not obvious

- **36 clips carry no split-bone track at all.** `read_anim` returns a **zero quaternion**, not the
  bind rotation, for a bone with no animation record. Composing through one produced degenerate
  keys (289 corrupted clips on the first attempt). The fix is a pre-filter: correct a split bone
  only if every frame of that clip has a readable rotation for it.
- **"clip animates nothing → track absent" had to be preserved explicitly**, via a `channels`
  pre-pass in `_clip_payload`, or normalization would have synthesised tracks.

### 5.8 The A1 guard

`BuildAnimSequencesFromSource` checks **every** bone of a container that carries geometry — the
whole bone list, not just the animated ones — against the skeleton before writing a single clip,
and fails the whole owner naming the missing bones. A body's own clips can therefore never bake
short. A bank's unresolved bones are still dropped, because a bank recorded on another clan's rig
legitimately names hair chains this family never had; they are counted into `OutDroppedTracks`.

The guard deliberately does **not** name a cause: the mesh build for the same stem may have failed
earlier in the same run, in which case the skeleton never *saw* those bones.

---

## 6. What the runtime does, and why

`Evaluate` is `EvaluateBody` → `EvaluateLayers` → `EvaluateComposition` → facial curves.

**`FElysiumNpcAnimProxy::EvaluateLayers(FPoseContext&)`** operates on already-evaluated
`FPoseContext`s rather than as an `FAnimNode_Base` with `FPoseLink`s, because there is no graph to
plug them into. It branches on `IsValidAdditive()`:

- **overlay:** `nlerp(out, layer, s)` + lerp on translation
- **additive:** `out.quat = normalize(out.quat * scale(delta, s))`, `out.pos += delta.pos * s`

with `s = layer_weight * mask`. `scale(q,s)` is `FQuat::Slerp(Identity, q, s)` — retail's
`QuaternionScale` is a true power, not the nlerp `FTransform::BlendFromIdentityAndAccumulate` uses,
and slerp-from-identity reproduces it as a rotation. Scale is left alone: VtMB has no scale
channel. Layers accumulate in LOCAL space **under** the composition stages, not over them.

**Two gates, mirror-imaged, and both load-bearing:**

- `RequestAdditive` refuses a sequence that is not `IsValidAdditive()`. Only an additive evaluates
  to a delta — it starts from the additive identity, so an untouched bone comes back as *no
  change*. An ordinary sequence starts from the **reference pose**, and accumulating that
  post-multiplies every untouched bone by its own bind. So **layering requires
  the mount**, which is now the only source a clip can have.
- `RequestLayer` refuses a non-additive carrying no mask — an unmasked ordinary layer would pull
  every bone it does not own onto the reference pose.
- `RequestGrid` refuses a blend space whose samples carry `UElysiumAnimLayerMask`, which is why an
  aim grid bakes but cannot be stood. **Deliberately no layered blend-space path:** in a graph the
  mask is a property of the *blend node*, not of the pose feeding it, so that seam would be built
  wrong and then deleted by D3.

**`s = layer_weight` alone in the additive branch, and the reason does not generalise.**
`bone_weight` is a binary mask and is *not* 1.0 everywhere — 5,972 records install-wide are zero.
The product reduces only here, only for `0x4` additives: a zero-weight record never carries a
channel offset (0 of 736,208), the exporter drops a channel-less track, and a missing track on an
additive evaluates to the additive identity. **That equivalence belongs to the additive identity,
not to the mask**, so it does not carry over to ordinary layers.

**Slot allocation.** Layers get their **own** `LayerPlayers[2]` array, not a share of
`Players[MaxPlayers]`. That array belongs to the crossfade and `TakeFreeSlot` evicts from it by
age; an autolayer's lifetime is the weapon's, not the transition's. Two, because retail's pattern
is one `_aim_layer` plus one `_delta` and never more; a third request replaces the weakest. Layers
update *ahead of* the `bInitialized` gate, like cloth, so one rides over the ref pose on a body
with no clip yet.

**The order within the walk is an assumption** — overlay-first, additive-second, the only order in
which both contributions survive. Stated in the code at the loop; §1.1 is what settles it.

**The split-inheritance gate is PER CLIP.** `UElysiumNpcAnimInstance::PlayClip` asks
`ElysiumNpcVisual::IsBakedClip(Sequence)` and flips the proxy's rig through `SetCompositionRig`
only when the answer changes. Not per body and not on the cvar: a baked body still reaches for
loader-sourced bank clips, which carry VtMB's rotations unchanged and *do* need the stage.
`SetCompositionRig`'s bool is the **starting state** only. Stage 2 (axis interp) is unaffected and
inherently un-bakeable — a driven bone needs a *live* control orientation.

**Blend spaces: a BASE slot only.** `FAnimNode_BlendSpacePlayer_Standalone GridPlayer` in the
proxy; when set it IS the body pose, and `Request` clears it. A grid row falls back to standing the
resolved cell when there is no baked blend space — that is D1's A/B, not an error.

**Callers today:** `UElysiumNpcAnimInstance::PlayLayer/StopLayer/StopAllLayers/GetActiveLayers`,
`UElysiumEntityBodies::PlayNpcLayer/StopNpcLayers`, and the green room. **Nothing in gameplay
selects a layer or a grid** — §1.5.

---

## 7. Traps

- **A stem the character export missed cannot stand at all.** There is no second build of a
  character, so `LoadMesh` fails by name rather than substituting one. `uv run elysium export
  characters` with no arguments bakes the whole cast, which is what the game needs; naming models
  is for iterating.
- **`UElysiumEntityBodies::NpcMeshCache` pins a stem for the map epoch.** A re-export is invisible
  until `ForgetNpcVisuals()` drops it, which the green room's Restand calls. The asset and clip
  caches are keyed off the same map.
- **The Cog green-room readout reports the MESH, not the clip.** A body standing green still says
  nothing about which sequence is playing; `LoadBakedClip` returns null silently.
- **The Content Browser preview has no anim graph**, so it applies no axis interpolation — a rig
  with driven bones previews with untwisted forearms. Expected, not a bug; §1.7 is what fixes it.
- **A missing bone track evaluates to identity**, not to the ref pose. If a parity failure's
  magnitude equals a bone's full bind transform, the track is missing — don't hunt a rotation bug.
  **Corollary:** that holds for an *additive* (`ResetToAdditiveIdentity`) and is exactly backwards
  for an ordinary one (`ResetToRefPose`). The same "error equals the bind" signature means *two
  different things* depending on the stamp.
- **An additive parity failure of exactly one bind is ambiguous between three causes** — a dropped
  track, the additive round-trip, and the compression race below. Check `IsCompressedDataValid()`
  before reading anything into the number.
- **`GetAnimationPose` falls back to the raw data model whenever compressed data for the platform
  is not resident**, and compression runs asynchronously after a bake. The same binary against the
  same mount both passed and failed across runs. `EvaluateAdditiveFrame` now calls
  `WaitOnExistingCompression()` and warns if `IsCompressedDataValid()` is still false.
- **`UAnimSequence::GetBoneTransform` never does the additive conversion.** A plain track read: on
  a raw evaluation it hands back the keys as written. For a baked `_delta` that is the reference
  pose, so a test built on it reports a correct asset as broken by exactly one bind — and would
  pass just as happily if the subtraction never ran. `GetAnimationPose` is the door the runtime
  uses (`EvaluateAdditiveFrame` in `ElysiumBakedCharacterTests.cpp` is the worked example).
- **`GetImportedModel()->LODModels` must grow in parallel with `AddLODInfo()`** or `PostLoad`
  asserts in whichever process opens the package next. Nothing needs it at bake time, so the bake
  runs clean and the asset saves.
- **`USkeleton::AddCurveMetaData` defaults `bTransact = true`**, which dereferences a null `GEditor`
  under `-game`.
- **A delta only means something over ITS OWN base.** `twohanded_crouch_attack_delta` over a
  standing idle is arithmetically exact and anatomically nonsense — the crouch and the arm-raise
  live in the base, the delta carries only the swing. Pairing is by name:
  `<weapon>_<stance>_idle` is the base for `<weapon>_<stance>_attack_delta`. Same trap for an
  overlay: judge it over a host that actually declares it.
- **The legs and torso are not a control group.** Whole-body sway is the data, not a defect — deltas
  stack down the chain to ~37 deg at the skull. What *would* be a defect is the root translating.

**Watch item (A1's unfound root cause).** Re-running the clip build against the finished on-disk
skeleton produced all 58 tracks, so the bone was absent when the clips baked and present
afterwards — no more was established. The guard in §5.8 is a fence, not a fix. **If it ever fires
on a body whose mesh built cleanly, that question is live again.**

---

## 8. Harness

### Cvars

| cvar | default | note |
|---|---|---|
| `elysium.CompositionStages` | 1 | axis interpolation over the blended pose — the only stage left |
| `elysium.AnimLayers` | 1 | 0 ignores every autolayer, `_delta` and masked `_layer` alike (C1/C3's A/B) |
| `elysium.BlendSpaces` | 1 | 0 declines every grid, so a grid label plays the single resolved cell (D1's A/B) |
| `elysium.Cloth` | **0** | the garment spike, off. Turning it on takes that stem off the baked path — see the `IsStemBaked` trap |
| `elysium.NpcAnim` | 1 | 0 drops to the single-node instance (and refuses layers) |

`elysium.LayerDump` is a **command**: one-shot, logs the pose the next layer evaluation reads, per
bone, largest first, with each bone's mask weight and the sequence's `AdditiveAnimType`. It is the
instrument that settled C1 — the numbers are directly comparable against the container, so it
separates "the asset or the read is wrong" from "the accumulate is wrong" in one line.

### Green-room recipes

Not guessable, and each was expensive to find.

```
# additive over its own base
uv run elysium gr smiling_jack        # any stem now; cloth is off, so nothing is excluded from baked
tick elysium.BakedCharacters          # restands automatically; line must go green `baked: SK_...`
clip filter `twohanded_crouch_idle`   # the BASE. A pose, not a delta.
pick `twohanded_crouch_attack_delta`  # the layer, over it, at the weight slider's value
elysium.LayerDump                     # one-shot per-bone log of the pose being read

# overlay + additive, over a host that declares both (animation_and_movers.md A.3)
clip filter `smith_aggressive_run`    # -> smith_aim_layer PLUS smith_bobble_delta
pick the [overlay layer] row          # legs keep walking; spine-up and both arms take the overlay
pick the [additive layer] row         # the delta rides on top of that

# blend space
clip filter `aggressive_run`          # rows marked `->` are grids
pick one                              # stands the whole fan, not the cell
drag `move_yaw`                       # 0 = forward, 90 = strafe; between cells both contribute
```

### Commands

```
uv run elysium build
uv run elysium export characters smiling_jack tremere_male_armor_0     # the parity slice
uv run elysium test Elysium.Content.BakedCharacterParity
uv run elysium gr smiling_jack        # the interactive lab; `debug greenroom` is the one-shot
```

A bake change is only visible after a re-export — the mount is not rebuilt by `build`. And
`export characters` rewrites the `.eskm` for the named stems **and their owner banks only**, so
banks outside the slice stay at whatever container version they were last written at.

Test results land as JSON + HTML under `$ELYSIUM_EXPORT_ROOT/_tests/`.

### What the parity test asserts, and what it cannot

Equality against **retail's own `.mdl`** cannot live in the repo — the install is the one thing
bring-your-own-game forbids committing. So it is offline and measured once, not a per-change check.

In-repo, `Elysium.Content.BakedCharacterParity` carries:

- the **composed-pose** half, both sides driven through the hierarchy and compared in component
  space, so an error at any bone arrives amplified at every bone below it. **Both sides read the
  `.eskm`**, so this asserts transport and structure — not that the container is faithful to VtMB;
- the rest pose against the container, the bone set and morph set against the loader;
- three assertions that are **genuine** because each compares the bake against something the
  container *declares* rather than against its own arithmetic: the `_delta` additive round-trip,
  the layer masks and their bind-held bones, and the blend-grid axis configuration and sample
  placement.

C1's was proven non-vacuous by disabling the composition, re-baking, and watching it go red with
bind-sized errors.

---

## 9. Evidence

### Probes

`E:\elysium-work\scratch\animation-spike-probes\` — pure Python, no editor, no Unreal. They read
the MDLs from the install and the `.eskm` files from the export root by absolute path: `cd` there
and `python <probe>.py`. They insert `E:\dev\elysium-unreal\pipeline\src` on `sys.path`, so they do
**not** need `uv run elysium` and do not read `.elysium.local.env` — if either root moves, edit the
constants at the top of each file.

**Every probe but `probe_post_flag.py` globs `Unofficial_Patch\models` only** — 339 models, the
patch's own overrides, against the install's 4,445. Legitimate for anything scoped to the character
banks, and it is what every split-inheritance and delta-shape number was measured over. It is *not*
the install. A whole-corpus claim has to merge the VPKs the way `probe_post_flag.py` does (loose
overrides, then `vpk.index_all`) or it is sampling.

| probe | proved |
|---|---|
| `probe_b1_exact.py` | the bake reproduces retail FK — worst 7.245e-06 deg / 3.516e-13 cm, 24 models, 14,127 frame-poses. The offline half of A2. |
| `probe_normalize.py` | normalization is exact — worst 7.2e-06 deg / 3.6e-13 cm, ~14,400 frame-poses, every bone |
| `probe_b1_blend.py` | B1's residual across a crossfade — 0% above 10 deg, worst 5.17 |
| `probe_order.py` | cost of the pre/post additive mismatch — 89.1% ≤0.5 deg, 1.9% >10, worst 162.9 (female bank — see §4) |
| `probe_post_flag.py` | the additive set is identical selected by flag or by name, every member carries `0x14`, `0x10` never appears without `0x4` — 4,445 models / 14,004 sequences, loose tree **plus** the VPKs. Killed C2. |
| `probe_delta_shape.py` | a `_delta` track decodes to a TRUE delta, so v2531 has no `STUDIO_DELTA` branch and `read_anim` is right to add the bind |
| `probe_layer_mask.py` | `weight@0` is a real binary mask, not a flat 1.0 — 5,972 zeros install-wide; masked-out and bind-holding are distinct states v2 could not tell apart; only 4 distinct masks exist |
| `probe_grid_masks.py` | every cell of a 3x3 aim grid carries ONE mask and every cell of a 9x1 fan is unmasked. Male banks only. |
| `probe_split.py` | applying the split rule drops the lean 60.8 → 10.8 deg |
| `probe_delta_ancestors.py` | 41 of 118 additives leave the split bone's ancestor chain alone |
| `probe_bind_vs_clip.py` | the bind pose is upright; the naive-FK clip pose is not |
| `probe_lean.py` | the bend is systematic across models and clip families |
| `probe_source_fk.py` | the bend is already present in Source space, before either exporter |
| `probe_spine_quats.py` | isolates it to `Bip01 Spine1` — bind vs clip quaternion per spine bone |
| `probe_channels.py` | which rotation channels each spine bone animates, with weights and scales |
| `probe_diff.py` | source clip track list vs the baked track list (found the 2 dropped tracks) |

### RE addresses (client.dll, image base `0x10000000`)

| address | what | confidence |
|---|---|---|
| `FUN_10088e10` | layer accumulator; `0x4` tested at `0x10088efc` | confirmed |
| `FUN_10088d00` | pre-multiply combine (`scale(layer,s) * base`) | confirmed |
| `FUN_10088d60` | post-multiply combine (`base * scale(layer,s)`) — the one all `_delta` use | confirmed |
| `FUN_1010a450` | Hamilton product `p*q`, verified component-wise | confirmed |
| `FUN_1010a320` | quaternion scale (`q^s`) | inferred from use sites |
| `FUN_1008fd00` | `C_BaseAnimating::BuildTransformations` (split inheritance) | confirmed by VProf string |
| `FUN_100919c0` | `C_BaseAnimating::SetupBones` | confirmed by VProf string |
| `FUN_10089c40` | dispatch_model_pose; walks autolayers, calls the accumulator | confirmed |
| `FUN_10091110` | sequence transitions; `SimpleSpline` crossfade ramp | confirmed |
| `FUN_1010a020` / `a2b0` / `a140` / `a180` | probably QuaternionAlign / Normalize / Blend / BlendNoAlign | **unverified**, and nothing depends on resolving them |

Decompile one more:

```
uv run elysium research ghidra_context research/cases/animation-pose/specs/animation_pose.json \
    --address <hex> --kinds funcs,asm
```

The address must already be an entry in the spec's `functions` array or it is rejected. Runs are
paced `lock_delay_seconds` apart because the headless JVM holds the project lock briefly after
printing.

### The Valve correspondence — vocabulary only, not authority

VtMB forked Source **before** that engine's public release, and v2531 diverges structurally (no
`RadianEuler rot`, no `qAlignment`, 160-byte bone vs 216, `rotscale` a 4-float quaternion rather
than a 3-axis euler). SDK 2013 is a *descendant* of what Troika forked, not the same code.

Confirmed correspondence: `SlerpBones()`, `STUDIO_DELTA = 0x0004`, `STUDIO_POST = 0x0010`,
`QuaternionSM(s,p,q,qt) = (s*p)*q`, `QuaternionMA(p,s,q,qt) = p*(s*q)`. Valve's own header leaves
`STUDIO_POST`'s comment **empty** — undocumented everywhere except the consuming code.

**Do not carry the rest of the SDK flag table across.** `0x20`, `0x80`, `0x100`, `0x200` mean
whatever v2531 says they mean. The repo already records one trap of this shape: sequence flag `0x2`
and bone flag `0x2` are unrelated structures sharing a bit position.

### Where things live

```
Ghidra dumps      E:\elysium-work\research\ghidra\out\animation_pose\
                  (FUN_10088e10's body is inline in 10089c40_dispatch_model_pose.decomp.txt)
Source reference  E:\elysium-work\research\reference-source\source-engine\public\
                  bone_setup.cpp (SlerpBones ~L1373-1476), studio.h (flags ~L3066)
RE spec           research/cases/animation-pose/specs/animation_pose.json
Ghidra project    E:\elysium-work\research\ghidra\project  (ProjName `vtmb`, the default)
Exports           E:\elysium-work\exports\npc\  → *.eskm, *.glb, banks/, blends/, cloth/, tex/
Baked mount       Plugins/ElysiumBaked/Content/Characters/{Skeletons,Meshes,Materials,Textures,Anims}
Anim asset path   Anims/<family>/<owner>/A_<clip>          ← note the TWO levels
Exporter          pipeline/src/elysium_pipeline/exporters/UE_mdl_skeletal.py
Bake              Source/ElysiumUE/Private/Editor/ElysiumSkeletalBuild.cpp
Runtime           Source/ElysiumUE/Private/Visual/ElysiumNpcAnimInstance.{h,cpp}
```
