# Procedural bones and the persistent pose array — the stage between decoded locals and the drawn skeleton

A VtMB skeleton carries two kinds of bone. An ordinary bone takes its transform from the
animation channels and its parent. A **procedural** bone ignores its animation channels
entirely: its transform is recomputed every frame from the current orientation of another
bone, using a correction table authored into the model. The limb twist and roll helpers —
wrist, ulna, bicep, shoulder, shin, ankle, quadricep, knee — are all procedural.

Two consequences shape everything downstream. The correction is **not in the clip**, so no
amount of animation decoding recovers it; and the drawn skeleton is **not one pose build's
output**, because the engine's bone-to-world array is persistent and each build refreshes
only a masked subset of it.

Struct offsets are the `.mdl` v2531 layout (`docs/vtmb/mdl_v2531.md` holds the rest of the
format). The composition this stage sits inside — split inheritance, parent order, the
root/entity transform — is `docs/vtmb/animation_and_movers.md` A.4a. Method, evidence
grades, and the standing of the public decoders are
`docs/vtmb/vtmb-animation-reverse-engineering.md`.

The Unreal design that consumes these facts is
`docs/architecture/animation-architecture.md`.

## Where the rule lives in the model

Three fields of the 160-byte `StudioBone` declare it:

| Off | Type | Field | Meaning |
|---|---|---|---|
| 136 | int | `Flags` | bit `0x1` marks the bone procedural |
| 140 | int | `ProcType` | rule kind; `1` = axis interpolation |
| 144 | int | `ProcIndex` | byte offset **relative to the bone record** to the rule |

`Flags` is one of the ranges the loader rewrites in place, but bit `0x1` is authored: the
loader only ever sets `0x4`, `0x8`, and `0x20`–`0x8000`, and clears nothing
(`docs/vtmb/mdl_v2531.md`). `ProcType` and `ProcIndex` lie outside every rewritten range,
so all three read correctly from the installed file.

For `ProcType == 1` the rule is a 176-byte record:

```c
struct mstudioaxisinterpbone_t {
    int        control;   // the bone whose orientation drives this one
    int        axis;      // 0 = X, 1 = Y, 2 = Z of the control bone's transform
    Vector     pos[6];    // X+, X-, Y+, Y-, Z+, Z-
    Quaternion quat[6];
};
```

Consecutive rules sit 176 bytes apart, and every `ProcIndex` resolves inside the image to a
record naming a control bone within the model's bone count and an axis in `0..2`.

The rig shape mostly follows Valve's documented convention for helper bones, which requires
the helper and its control to be immediate children of the same parent. Across 77 rules in
five captured models the control is a **sibling** of the driven bone on 56, is the driven
bone's own parent on 19, and neither on 2. That tally is a narrow sample of the 3,123 rules
the install carries, and the corpus holds shapes it has no bucket for: on `Lacroix`,
`Bip01 L Ulna` and `Bip01 L Wrist` are driven by `Bip01 L Hand`, which is their own *child*.
The evaluation tolerates every arrangement, because it inverse-rotates by the control bone's
own parent whatever that turns out to be.

## Corpus

Over the whole engine-resolved install: **3,123 rules across 261 of 4,445 v2531 models**,
with zero faults — every `ProcIndex` resolving inside its image, every control bone inside
the model's bone count, every axis in `0..2`, and no bone carrying `Flags & 0x1` without
`ProcType` or the reverse. Restricted to the 339 models of the patch loose tree it is
**1,327 procedural bones across 110 models**. Every rule is `Flags & 0x1` with
`ProcType == 1` and a non-zero `ProcIndex`.

The named axis is not evenly spread: **X on 767 rules, Y on 1,076, Z on 1,280**. That
distribution is what makes the basis hazard below expensive rather than theoretical.

**`ProcType == 2` (quaternion interpolation) occurs nowhere**, and the cause is the
format's age rather than this corpus. The rule kinds are an enum that grew over successive
Source revisions — `AXISINTERP 1`, `QUATINTERP 2`, then `AIMATBONE 3`, `AIMATATTACH 4`,
`JIGGLE 5`, and later `TWIST_MASTER` and `TWIST_SLAVE`. VtMB uses the first entry and
nothing after it.

That has a practical edge. Valve's later authoring path — the `$proceduralbones` QC command
reading a `.vrd` helper file, with its `<helper>`, `<basepos>` and `<trigger>` records —
compiles to **quatinterp**. So the published tutorials, the trigger syntax with its
angle-of-influence and control/helper rotation pairs, and the Blender addons built around
them all describe a rule v2531 never uses. None of it transcribes into a VtMB decoder.

The authoring file that *does* compile to axisinterp is **`.vhb`**, and `studiomdl` still
accepts it in later Source revisions — so the rule kind outlived the tooling that emitted it.
Modders decompiling VtMB characters report one `.vhb` whose contents do not vary between
models, which is the authoring-side statement of the shared-template measurement below,
reached from compiled models rather than from bytes. That correspondence is the useful part;
the `.vhb` text layout itself is neither decoded nor needed, since the compiled 176-byte
record is what the runtime reads and what a reproduction carries.

The driven bones are a consistent anatomical set — the most frequent are `Bip01 R Wrist`
(107 models), `Bip01 L Wrist` (106), `Bip01 L Bicep` (85), `Bip01 R Bicep` (77),
`Bip01 R Ankle` (77), `Bip01 L Shin` (76), `Bip01 L Quadricep` (76), `Bip01 L Ankle` (76).
A model carries between 12 and 21 of them; the player-character bodies carry the most.
Beyond the twist and roll helpers, four further kinds are driven: `Femoris`, `Elbow`,
`Hip`, and one `Pectoral`.

`Flags & 0x1` and `ProcType != 0` select the same bones — **295 of 295** over the 160 model
images of one theatre capture, with no bone carrying one and not the other — so either
field identifies a driven bone on its own. None of them carries `Flags & 0x2`: the
procedural set and the split-inheritance set are disjoint over the same corpus, which is
what lets A.4a's hierarchy rule and this stage be confirmed against one another's bones
without either contaminating the other.

## The correction is rotational in content, not only in effect

The rule writes a position as well as a quaternion, but the position it writes is almost
always the one the bone already has. Over the 295 rules in the captured corpus, `pos[6]`
holds **six copies of one position** at the median and deviates by `3.36e-5` at the 90th
percentile; that position **is the driven bone's bind position** on **79.3%** of rules,
within `4.71e-6` at the median. Since `a1·t + a2·t + a3·t = 1`, a constant `pos[6]` makes
the interpolation return that single value unchanged. `quat[6]`, by contrast, holds six
genuinely distinct rotations on **99.7%** of rules.

So a driven bone is pinned at its bind offset from its parent and reoriented, and a
comparison that ignores the rule diverges in orientation while the position holds. The
exceptions are real but rare — `pos[6]` spreads by up to **3.01** source units and departs
from bind by up to **3.59** on a few rules — so a reproduction must evaluate the positional
term rather than assume it inert.

Valve's authoring format names the same split. A `.vrd` helper declares one `<basepos>` —
the helper bone's rest translation in its parent's coordinate system — apart from the
rotational triggers that follow it. The measurement above is that authoring concept
recovered from compiled bytes, by a different route and for the older rule kind.

**The correction table is a shared rig template, not per-model authoring.** Across six
models drawn from both NPC and player-body trees, all ten procedural bones they have in
common carry an identical `control`, `axis`, and `quat[6]`.

## The evaluation rule

```text
v = column `axis` of boneToWorld[control]
if control has a parent: inverse-rotate v into that parent's frame
a1, a2, a3 = v.x, v.y, v.z
  sign of each component selects one of the six entries; take |a| afterwards
if a1 + a2 > 0:
    t = 1 / (a1 + a2 + a3)
    q = slerp( slerp(quat[i2], quat[i1], a1 / (a1 + a2)), quat[i3], a3 * t )
    p = a1*t*pos[i1] + a2*t*pos[i2] + a3*t*pos[i3]
else:
    q, p = entry i3
boneToWorld[i] = boneToWorld[parent(i)] * matrix(q, p)
```

**The input is the control bone's local rotation.** Pulling the axis column from the
control's bone-to-world and inverse-rotating it by the control's parent is algebraically
the same column of the control's own local matrix; measured over 199 captured frames the
two routes agree to `3.5e-4`. The rule is therefore a pure function of one local rotation,
evaluable without a component-space pass.

**It replaces the animated local rather than adjusting it.** The clip's channels for a
procedural bone are decoded and carried through composition, then discarded. Over 12,228
paired decoded-local and composed-local records for one actor, **no bone changes between
the two stages**, procedural bones included — the substitution happens while bone-to-world
is built, not while locals are prepared.

## The bone-to-world array is persistent and partially updated

A pose build refreshes only the bones its mask selects. Measured per actor over one
theatre capture:

| Model | bones | builds writing every bone | typical bones refreshed |
|---|---:|---:|---|
| `Lacroix` | 72 | 30 of 12,228 | 21 or 51 |
| `toreador_Male_Armor_0` | 81 | 59 of 11,957 | 21 or 60 |
| `Skelter` | 76 | 127 of 8,216 | 21 or 55 |
| `Isaac` | 76 | **0 of 9,992** | 21, 19 or 38 |
| `Ash` | 73 | **0 of 10,072** | 21, 9 or 28 |

Unrefreshed slots are not empty: they hold valid unit quaternions carried over from earlier
builds. So a bone keeps the world matrix it was composed with when it was **last**
refreshed — against the root transform and parent matrix in force at that moment, not the
current one. A drawn skeleton is an accumulation across many builds, and two models in the
theatre corpus never refresh a complete skeleton at any point in the run.

This is observable behaviour, not an invisible optimization: recomposing every bone from
the current build yields a different result from the one retail draws whenever the actor
moved since a bone was last refreshed.

**The mask is the bone flags.** Source's pose builder takes a bone mask and skips every
bone where `!(boneFlags(i) & boneMask)`, the flags carrying usage bits its loader computes
from hitboxes, attachments and per-LOD skinned vertices. v2531 does the same at different
bit positions: over one actor's builds the 21-bone mask is exactly `Flags & 0x4`, the
72-bone mask is exactly `Flags & 0x10`, and the 51-bone mask is exactly the difference
between the two sets. The partial updates are three call sites asking for different masks,
not an anomaly.

Those usage bits are loader-written — they fall inside the same rewrite that sets `0x4`,
`0x8` and `0x20`–`0x8000` and clears nothing — so they read as unset on disk. **An offline
reader cannot recover the masks from the installed file.** Obtaining them means recomputing
the usage flags from hitboxes, attachments and vertex LODs the way the loader does, or
capturing them from a running process.

## Verification

Replaying the accumulation — one persistent array per renderable, each build refreshing
only its masked bones, draws and builds merged in the capture's global sequence order —
reproduces the captured bone-to-world exactly:

| Model | draws judged | without the rule | with the rule | median error |
|---|---:|---:|---:|---:|
| `Lacroix` | 16,371 | 0.0% | **100.0%** | 6.3e-05 |
| `toreador_Male_Armor_0` | 14,320 | 0.0% | **100.0%** | 5.8e-05 |
| `Skelter` | 6,785 | 0.0% | **100.0%** | 3.0e-05 |
| `Isaac` | 8,218 | 0.0% | **100.0%** | 3.7e-05 |
| `Ash` | 9,048 | 0.0% | **100.0%** | 2.5e-05 |

**54,742 of 54,742 draws reproduce with the rule applied and none without it.** The
residual is float32 accumulation and is the same size on ordinary and procedural bones.
685 draws at the start of the run are excluded because the array is not yet fully written.

Two properties make this evidence rather than a fit. Draws are tied to pose builds by
identity — the renderable a draw records against the same renderable a build's pose group
names — and ordered by the capture's dense global sequence counter, never by wall clock.
And the same pass with the procedural rule switched off reproduces nothing at all, so the
rule is doing the work rather than absorbing an error.

### A second, independent route to the same rule

`uv run elysium research verify_transform_difference` reaches it from the other end. It
differences a composed bone-to-world against the captured one per bone, knowing nothing of
this rule, and asks only which bones disagree. Over 110,082 paired records of the same
capture the answer is `ProcType` alone: **748,666 of 968,910** procedural bone observations
fall outside the rotation band against **37,712 of 5,702,073** ordinary ones, and every one
of the 32 clusters it ranks is a driven bone. Evaluating this rule as a third composition
candidate then takes that to **0 of 968,910** on translation, model-space translation and
rotation alike, and lifts whole-record agreement from **46,972** to **107,763** of 110,082.

The two routes share the corpus and nothing else: that one replays the persistent array and
this one seeds unrefreshed slots from the draw's own matrices, so neither inherits the
other's accumulation model. A second transcription of the rule, written from the pseudocode
above and checked against the first bone for bone, is a regression in
`pipeline/tests/test_retail_capture.py`.

**What the rule does not explain is a different stage, and it is located.** **2,428**
records stay outside the band with this rule applied, on **90 bones over 7 models** carrying
neither `ProcType` nor `Flags & 0x2` — hair chains, `left`/`right breast`, ponytails. They
are the authored per-bone angular limit `docs/vtmb/secondary_motion.md` owns: 47 of the 90
are named by that table and the remaining 43 descend from a named bone. Nothing about it
bears on this rule, and the two stages act on disjoint bone sets.

`Sheriff Sword` was previously counted in that residual and does not belong there. Composed
against retail's own captured `Bip01 R Hand` matrix it reproduces to a median **3.42e-06°**
over 4,036 draws, inside the excellent band on every one. Its local sits a median 98.5° from
bind, so unlike the secondary-motion bones it is animated; its residual flag came from
composing its own model's `Bip01` chain upstream of it.

Neither bone controllers nor IK rules participate: all five models declare
`NumBoneControllers == 0`, no bone names a controller, and their local animations declare
zero IK rules.

## What the public decoders do with these bits

Both VtMB-native decoders act on the procedural flags and neither reproduces the rule.
General tool standing is `docs/vtmb/vtmb-animation-reverse-engineering.md`.

- **Crowbar** reads `Flags & 0x1` as modern Source's `STUDIO_PROC_AXISINTERP` and zeroes the
  bone's exported euler outright. Measured against a decode of the same file, that costs up
  to **44.9°** on `Bip01 L/R Shoulder`, 26.9° on the biceps, 6.4° on the wrists. It also
  reads `Flags & 0x2` as `STUDIO_PROC_QUATINTERP` and substitutes the bind quaternion for
  all four components, freezing the split-inheritance bone at bind on every frame of every
  clip — up to **180°**, on one bone per ordinary biped.
- **VAMPTools** does not test `0x1`, so it decodes procedural bones verbatim and leaves the
  correction unapplied. It tests `0x2` under the name `BONEFLAG_ORIENTATION` and applies a
  fixed inverse-and-axis-permutation, which A.4a shows no fixed transform can reproduce.

Crowbar's two defects have one cause. Modern Source keeps the rule kind and the bone flags
apart: `proctype` is an enum, while the flags carry `BONE_PHYSICS_PROCEDURAL 0x02` and
`BONE_ALWAYS_PROCEDURAL 0x04` beneath `BONE_CALCULATE_MASK 0x1F`, and the procedural
dispatcher gates on the flag before switching on the enum. Crowbar tests the **enum values
as flag bits**. In v2531 bit `0x1` does coincide with `ProcType != 0`, so its first test
selects exactly the right bones and then does the wrong thing to them; its second test
lands on `0x2`, which here is split inheritance and has nothing to do with procedural
rules.

## Consequences for reproduction

The correction cannot be baked into exported clips. For a clip played alone a bake is
exact, because the rule is deterministic in the control bone's local rotation and the clip
fully determines it. Under blending it is not: the rule is non-linear — sign-selected among
six entries, two slerps, and a `1/(a1+a2+a3)` normalisation — so evaluating per clip and
blending the results is not the same as blending first and evaluating once. Measured on one
model at a 50/50 blend of nearby captured control poses, the two disagree by a median of
0.004°–0.54° and up to **5°** on the shoulders and biceps; poses further apart diverge
further. Measured again in a host animation graph, mid-crossfade between two of a real
body's own clips, bake-then-blend departs from blend-then-evaluate by up to **9.6°**. Blend
grids, sequence transitions, and layered sequences all occur in the corpus.

The faithful arrangement is therefore to carry the rule table per model as data — driven
bone, control bone, axis, `pos[6]`, `quat[6]`, 176 bytes each — and to evaluate it in
retail's own order: after the locals are decoded **and blended**, after the hierarchy is
composed, and before skinning. The ordering is the load-bearing part; a host animation
system that blends locals and then exposes a pose-modification stage satisfies it without
any change to how the clips themselves are stored.

Exported tracks for driven bones are neither authoritative nor wrong: they are the same
channels retail decodes and then overrides, so they are the correct thing to carry provided
something downstream does the overriding. What must not happen is baking the rule's
*output* into a clip, for the blending reason above.

## Where the Unreal side lives

The host-engine design that consumes this stage — where the two composition rules sit in
Unreal's animation pipeline, what the exporter has to carry and why the basis forces a
single exporter — is `docs/architecture/animation-architecture.md`. The constraint it hangs
off is the ordering above: decode, blend, compose, then apply this rule, then skin.

## Open questions

- **`ProcType == 2` is out of scope rather than unverified.** It postdates this format
  revision and no shipped model declares it, so its layout and semantics stay undecoded and
  nothing in VtMB exercises them. Evidence that would change that: a shipped model declaring
  it, or a decompilation of a v2531 consumer that reads `mstudioquatinterpbone_t`.
- **The partial-update behaviour is faithful but not yet adjudicated for the rebuild.**
  Reproducing it means reproducing the mask, and the mask bits are loader-written, so the
  cost is recomputing the usage flags from hitboxes, attachments and vertex LODs rather than
  reading them. Recomputing every bone each frame is a divergence and needs an explicit
  owner call recorded beside this behaviour.
- **A game-independent regression covers the rule's arithmetic, not its truth.** Two
  independent transcriptions are checked against one another over a synthetic model in
  `pipeline/tests/test_retail_capture.py`, which fixes the evaluation against drift. That
  the rule is *retail's* rule still rests on one capture corpus and the installed models it
  names, reached by two methods.
- **This rule and split inheritance commute on the shipped corpus.** No rule anywhere names a
  split-inheritance bone as its *control* either — 0 of 1,535 over the models one host runtime
  loads — and since the rule is a pure function of a local rotation while split inheritance
  changes only component-space composition, the two stages touch disjoint data. Retail's order
  remains the order to reproduce, but the corpus contains no case that distinguishes it, so an
  implementation running them the other way could not be caught by this evidence.
