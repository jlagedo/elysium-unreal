# Animation Roadmap — VtMB animation data on Unreal's animation system

## Goal and ownership

This tracker drives one decision and the work that follows from it:

> **Bake VtMB's animation data into native Unreal assets and let Unreal's animation system run
> it. Write only the composition stages Unreal has no equivalent for.**

It owns the **skeletal animation stack**: the character asset bake, the shared skeleton, layer
masks and their bindings, blend spaces, the animation graph, and the locomotion that drives them —
for both the cast and the player. It also owns the two custom composition stages, because they are
the only part of retail's pose pipeline that survives into the Unreal design.

**It does not own:** facial flex, eyes, the amplitude jaw or lipsync (`docs/vtmb/facial_animation.md`
owns the specification, `docs/project/roadmap.md` owns their status); choreographed scene semantics
(`docs/vtmb/choreographed_scenes.md`); secondary motion (`docs/project/retail-capture-roadmap.md`
CAP5.5); or any VtMB format fact. Facts belong in the owning `docs/vtmb/` topic, the Unreal design
belongs in `docs/architecture/animation-architecture.md`, and project priority and roll-up status
belong in `docs/project/roadmap.md`. This file owns detailed status and task order for the
programme, and nothing else.

`docs/project/roadmap.md` and `docs/project/retail-capture-roadmap.md` are this tracker's siblings.
Retail capture remains the oracle for VtMB behaviour; it is not a gate on anything here.

## The decision

Reading VtMB's own layer machinery — an autolayer binding table, a per-bone weight mask, and a
dispatcher that evaluates layers recursively — makes it possible to reproduce Source's animation
system inside Unreal. **The owner call is not to.** That path ends in a Troika simulator: a bespoke
pose evaluator, a hand-written per-bone blender, a hand-rolled grid resolver, and a research
programme to recover blending semantics that only matter if the goal is numerical equivalence.

The goal is not numerical equivalence. It is VtMB's animation *content*, running on Unreal 5.

The mapping is close to one-to-one, which is the evidence the decision is right:

| VtMB | Unreal | Status |
|---|---|---|
| Blend grid (9×1 `move_yaw`, 3×3 `aim_yaw`/`aim_pitch`) | `UBlendSpace1D` / `UBlendSpace` | ANM3 |
| `StudioAnimRecord.weight`@0 per-bone mask | `UBlendProfile` in `BlendMask` mode + `FAnimNode_LayeredBoneBlend` | ANM2, ANM4 |
| `flags & 0x14` `_delta` clips | `AAT_LocalSpaceBase` + `ABPRT_RefPose` additive | ANM1, ANM4 |
| `autolayerindex`@664 | a binding table the graph consults — **data, not mechanism** | ANM2 |
| Shared animation banks, `Bip01` naming | one shared `USkeleton` — zero retargeting | ANM1 |
| Stance and locomotion transitions | state machine transitions | ANM4 |
| One-shot clips, gestures, disciplines | montage slots | ANM4, ANM6 |
| `Flags & 0x2` split inheritance | **no Unreal equivalent — ours stays** | done |
| `ProcType == 1` axis interpolation | **no Unreal equivalent — ours stays** | done |

The last two rows are **correctness, not feel**. Without split inheritance every character's upper
body folds about 90° forward. They are the irreducible delta between reading VtMB's rigs and not
reading them, they are already built and verified three ways, and keeping them is not a step toward
a simulator. Everything above them is Unreal's job.

**How this reads against the remaster charter.** Animation blending is the Feel layer, where
`docs/project/remaster-direction.md` calls for building the reverse-engineered original first and
polishing one delta at a time by explicit owner call. This decision is that owner call, made once
at the architecture level rather than per clip: the authored *content* — which clip, which mask,
which grid cell, which layer rides which base — is reproduced exactly, and the *interpolation
between* those authored values is Unreal's. Divergences that follow are listed at the end of this
file and each is recorded beside the faithful behaviour in the topic that owns it.

## What it supersedes

- **A bespoke layer stage as the destination.** The animation proxy composes both kinds of layer
  itself because there is no graph to hold a layered blend or an additive node yet. What is
  superseded is treating that stage as the design rather than as the stand-in; ANM4 says what
  replaces it and what that costs.
- **A hand-rolled grid resolver.** `ElysiumBlendGrids`'s nearest-cell pick computes interpolation
  fractions and discards them. Replaced by baked blend spaces; the parse survives for provenance
  and for the per-cell ground speed the motor consumes.
- **A capture and decompilation phase.** An earlier design gated runtime work on recovering
  retail's autolayer weighting, evaluation order and delta reference frame. Those questions only
  need retail's answer if the blending is being matched numerically. **Dropped.** Capture returns
  as an oracle when a *visible* divergence needs explaining, never as a gate.
- **CAP7.3, CAP7.4 and CAP7.5** in `docs/project/retail-capture-roadmap.md`. CAP7.1 (the rule
  table) and CAP7.2 (the two composition stages) are complete and survive unchanged.

## Evidence

Measured read-only against the owner's install and the current export. Each row is reproducible and
names what it decides. **Rows marked ▶ are VtMB facts owed to `docs/vtmb/animation_and_movers.md`**,
which owns them; they appear here as the evidence for a project decision, not as their own record.

| Measurement | Value | What it decides |
|---|---|---|
| ▶ `StudioAnimRecord.weight`@0 value set | `{0.0, 1.0}` only, over **736,208** `(anim, bone)` records across **4,508** models | The field is a binary per-bone mask, so it is a blend mask rather than a weight curve |
| Distinct masks per bank | **5** over 722 animdescs — unmasked 396, root+pelvis+spine+legs 308, arms+props 12, orphans 4, `Bip01 Head` alone 2 | Five blend profiles per skeleton, not a per-clip table |
| Zero-weight records carrying a channel offset | **0 of 736,208** | The current export *drops* masked bones rather than corrupting them |
| ▶ `numautolayers`@660 histogram | `{0: 10,278, 1: 237, 2: 224}` over 4,249 models; the only carriers are `character/shared/{male,female}/move_and_ranged.mdl`; **0** out of range, **0** self-referencing, depth **1**, fan-out **≤ 2** | A fixed two-slot binding; retail's recursive dispatcher never recurses on shipped content |
| ▶ Autolayer targets | 111 distinct, every one a `_layer` or `_delta` sequence, **none carrying an activity** | Layers are composed, never selected as a base |
| ▶ A second binding mechanism | **46** masked sequences are selected by the game DLL through `ACT_*_LAYER_*` activities — weapon attack/reload/dryfire, the discipline casts, and both `lookback_*_layer` | A masked sequence reaching the base clip path is a defect no autolayer work fixes |
| Orphan layers | exactly **5 per bank**, byte-identical on male and female | Abandoned authoring — but orphanhood must be *measured* across the include DAG, not assumed |
| Blend grids in the export | **275** grids over 15 sidecars: 222 are 9×1 on `move_yaw`, 49 are 3×3 on `aim_yaw`/`aim_pitch`, 4 on `hit_yaw` | Directly `UBlendSpace1D` and `UBlendSpace` |
| Split-inheritance forward-kinematics check | with the rule applied, `walk` head-rise `+0.428` (upright); a masked layer standing alone `+0.000` rise / `+0.431` forward (flat) | The fold is a reference-pose fallback on the split bone, which a layered blend removes by construction |
| Morph target names across the cast | **54 distinct**; 86 of 166 models carry a flex rig, each with the same 53 FACS action units | One shared skeleton carries one 54-name curve set — the face costs the bake nothing |
| ▶ `numautolayers` bounds hazard | 7 single-sequence scenery and weapon models read **764**, the descriptor tail running past the file into the string table | A bounds gate is mandatory in the exporter, and its population is named |

## Phases

Numbered for dependency, not for date. ANM1 and ANM2 are independent and start together.

- [ ] **ANM1 Bake the character assets.** One shared biped `USkeleton` built from the `Bip01`
  hierarchy, with the outliers — animals, skeletal props, `wolf_form` — on their own; a
  `USkeletalMesh` per model with morph targets intact; a compressed `UAnimSequence` per clip, with
  the `_delta` family flagged `AAT_LocalSpaceBase` / `ABPRT_RefPose` at bake time.

  A new editor commandlet beside `pipeline/unreal/bake_map.py`, on the same `/ElysiumBaked` mount
  and the same gitignored, regenerable posture. **glTFRuntime stays** as the bake-time reader rather
  than being replaced: the vendored multi-primitive morph-target patch is load-bearing and fails
  silently when lost, and keeping the same reader keeps it earning.

  *Retires:* the per-map-epoch retarget cache, the `RemoveTracks` bank filtering, and the constraint
  that resolved sequences cache on the map actor rather than the subsystem.
  *Acceptance:* a character loads from the baked mount with no glTFRuntime call at runtime, and the
  facial morph-target contract still holds.

- [ ] **ANM2 Carry the two discarded MDL fields.** `autolayerindex`, unioned across the include DAG
  so orphanhood is measured rather than assumed, and the per-bone weight mask, deduped. The masks
  bake into blend profiles on the shared skeleton; the base→layer binding stays data the graph
  reads. Needs the bounds gate the evidence table names.
  *Acceptance:* every autolayer target resolves to a label, the orphan census reproduces, and the
  mask count per bank is 5.

  **The mask half is delivered.** The character container carries a de-duplicated mask table and a
  per-clip index into it, and the character bake turns every mask a non-additive clip references
  into a blend profile on the shared skeleton, named for the bones it owns, with the clip carrying
  the profile's name as its own metadata. The male `move_and_ranged` bank ships **4** masks over its
  826 exported clips plus the unmasked state, which is the evidence table's five; `misc` ships one
  more. Because a profile is named for the bones it owns *after* they resolve against the skeleton,
  masks that differ only in bones a family does not have become one asset: the baked male family
  skeletons carry **4** profiles and the female **5**.
  `Elysium.Content.BakedCharacterParity` asserts each baked layer's profile against the container's
  mask bone for bone, and asserts that a bone the mask owns and the clip does not animate holds the
  container's bind pose rather than the skeleton's reference pose.

  **The binding half is not**, and its blocker is gone rather than open: `numautolayers`@660 /
  `autolayerindex`@664 are decoded and censused in `docs/vtmb/animation_and_movers.md` A.3, so what
  remains is exporting the table across the include DAG, not reverse-engineering it. Until it ships,
  a layer is played by explicit request and nothing in gameplay selects one.

- [ ] **ANM3 Bake the blend spaces.** A `UBlendSpace1D` per 9×1 `move_yaw` fan and a `UBlendSpace`
  per 3×3 aim grid, samples placed at the axis values the grid declares. The −180/+180 endpoint
  cells are already duplicates in the authored data, which is how a wrapping axis is authored.
  The per-cell ground speed stays available to the motor and is re-read when the cell changes.
  *Acceptance:* a walk grid blends across its cells instead of playing the base cell.

- [ ] **ANM4 The animation graph.** An Animation Blueprint per body archetype, replacing the
  hand-rolled proxy: a locomotion state machine, layered blend per bone over the ANM2 profiles,
  additive nodes for the `_delta` family, montage slots for one-shots, and a post-process graph
  carrying split inheritance then axis interpolation in retail's order.

  Two defects close here. A masked sequence stops being reachable as a base clip, which is what
  currently lets an `ACT_LOOKBACK` pick flatten a character — a defect fix rather than a divergence,
  since retail composes those as layers and never as bases. And a layered blend takes masked bones
  from the base pose, so the split bone no longer falls back to the reference pose.

  Montage slots also un-collapse a stated simplification: gesture and sequence currently share one
  clip slot, so a scene's gesture overwrites its sequence instead of layering over it.

  **This is also what retires the proxy's own layer composition.** Both combines run there today —
  a `_delta` accumulated post-multiplied and a masked `*_layer` blended under its profile, in two
  slots of their own, under `elysium.AnimLayers` — because there is no graph to plug a layered blend
  or an additive node into. The blend profiles they read are the assets `FAnimNode_LayeredBoneBlend`
  consumes unchanged, so the graph replaces the mechanism without re-baking anything.

- [ ] **ANM5 Drive it.** Locomotion state and `move_yaw` for the cast, from the motor's own
  velocity and facing; and the player's gait from the movement component's reported state. The
  player is the cleaner demonstration of the `move_yaw` fan — its facing is the camera yaw and its
  velocity is the mover's, so the angle between them is unambiguous. Depends on ANM4, and on the
  Source movement port for the player half.

- [ ] **ANM6 Migrate the cinematic path.** Choreographed scene playback from the sequence player's
  absolute-time seek to a montage position. **Deliberately last.** The theatre is the project's
  proven ground and its seek path is verified; there is no reason to put it at risk before the
  stack underneath it is established.

## Interface notes

Bounded, because this tracker does not own these systems.

### The face

**The face and the body meet at a curve interface, not a pose interface.** The facial rig's entire
output is named float curves flagged as morph-target curves on the output pose; it writes no bones.
The eyes read an eyeball bone's transform to build an aim basis and then write shader parameters,
so they write no bones either. Nothing in the bake changes either path, and the two systems keep
the property that a crossfade does not touch the face and a blink does not touch the pose.

The bake contract is therefore narrow: morph targets preserved, the duplicate-target merge strategy
retained — a face spans several material primitives and glTF weights are mesh-level, so the pieces
must be stitched rather than the first kept — and morph-target curve metadata authored at bake.

**This deletes a gotcha.** Curve metadata registration transacts by default and reaches the editor
transaction buffer, which does not exist when the game runs under the editor executable in game
mode; the runtime path works around it. Metadata authored at bake and serialised removes both the
hazard and the workaround.

### Lipsync

Phoneme tracks are authored *timed* data and nothing consumes them yet, so there is nothing to
port. Designing them onto **montage curve tracks** rather than per-frame hand-driven weights lands
them on the same mechanism ANM4 introduces, at no migration cost, and hands timing and blending to
Unreal instead of to the substrate clock. **Recorded here as a forward note only** — the decision
belongs to `docs/vtmb/facial_animation.md`'s owner and to `docs/project/roadmap.md`'s status row.

### Secondary motion

A third post-process stage, beside split inheritance and axis interpolation, on disjoint bones. It
is undesignable until the solve beneath its proved angular limit is decoded;
`docs/project/retail-capture-roadmap.md` CAP5.5 owns that.

## Risks

- **The theatre is the thing to protect.** Choreographed playback shares the animation instance
  with everything ANM4 replaces. ANM6 is last for this reason, and the theatre stays on the
  verified seek path until the stack under it is established.
- **Keep an A/B per stage.** Every composition and blending stage carries a console toggle. This is
  not ceremony: the split-inheritance fold that started this programme was diagnosed by turning the
  stage off and watching the pose change.
- **The animation iteration loop grows a bake step.** Export already requires an editor build, so
  the prerequisite is not new, but the edit-to-see-it cycle lengthens for character work.
- **Unreal's blending is not Source's.** Where a shipped pose looks wrong, the first question is
  whether the authored data was carried correctly, and only then whether the interpolation differs.
  The A/B toggles and the retail capture database are what separate those two.

## Divergences to record when settled

Each is recorded beside the faithful behaviour in the topic that owns it, and marked as a
divergence, per the house rules.

| Divergence | Owning document |
|---|---|
| Layer blend-in and blend-out times are ours — VtMB's four-byte autolayer record carries no ramp, so whatever weight the dispatcher passes lives in the game DLL and not in the file | `docs/vtmb/animation_and_movers.md` |
| Unreal blend-space interpolation replaces the authored grid's own cell selection | `docs/architecture/animation-architecture.md` |
| Any change to the cast's movement-orientation and strafing settings made so that `move_yaw` resolves off the neutral cell — **an open owner call, not yet made** | `docs/architecture/animation-architecture.md` |

**Not divergences**, and recorded as defect fixes with their numbers rather than as choices: keeping
masked sequences out of the base clip path, and the autonomous walk paths using a hardcoded ground
speed where the authored clip cell states a different one.
