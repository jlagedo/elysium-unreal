# MDL v2531 Coverage and Gaps

## What the format carries, what the pipeline reads, and what Unreal could do with the difference

**Game:** *Vampire: The Masquerade — Bloodlines* (2004)
**Primary target:** Troika's modified early Source engine, MDL version `2531`
**Document role:** Exploration research brief; not a status tracker or fact owner
**Last reviewed:** 2026-08-11

Byte layouts, struct offsets and decoded values belong to `docs/vtmb/mdl_v2531.md`; this brief
cites that document rather than restating it. Skeletal-animation behaviour belongs to
`docs/vtmb/animation_and_movers.md`, flex and gaze to `docs/vtmb/facial_animation.md`, bone-chain
motion to `docs/vtmb/secondary_motion.md`, collision to `docs/vtmb/phy_vphysics.md`. Work
sequencing and status live only in `docs/project/roadmap.md`.
The impact ranking below is an assessment of what each finding would change, not a schedule.

---

## Executive conclusion

The dominant gap in this format is **not undecoded bytes. It is decoded fields that nothing
reads.**

Four pieces of authored data are fully understood and sit unused by both the offline pipeline and
the runtime: per-vertex normals, IK chains, pose parameters, and the surface property. All four
are cheap to consume, and the first is a live shading regression rather than a new feature.

Genuinely undecoded territory is now small: two header slots nothing in the shipped binaries
reads, two `StudioModel` slots that are zero everywhere, and the interiors of `StudioSeqDesc` and
`StudioAnimDesc` that no observed consumer touches. The one real hole in *method* is that the
survey has covered header and struct declarations but never a payload byte-coverage map.

---

## Confidence language

| Term | Means |
|---|---|
| **Confirmed** | Anchored by decoding what the field addresses, or by disassembly of a consuming instruction, with a stated check a wrong answer fails |
| **Probable** | Consistent across the whole corpus, but no consumer identified |
| **Open** | Recorded as unknown; the evidence that would settle it is named |

---

## 1. Coverage classes

### 1.1 Decoded and consumed

Vertex positions and UVs, `.dx80.vtx` topology, bones and the reference pose, sequences and
compressed animation tracks, skin weights, the skin table, materials and search paths,
attachments, flex/facial data, and the secondary-motion table. These carry the export today.

**Pose parameters are in this class, not the next one.** `MDLHeader`@384/388 is read by
`mdl_skel.pose_parameters`, exported into each blend sidecar as `pose_parameters`, and consumed by
the blend-space bake: the axis takes its `DisplayName` from the parameter's name, `Min`/`Max` from
the sequence's own extents, and `bWrapInput` from a non-zero `Loop`. `move_yaw` is live end to end
— the mover publishes it, the player graph blends on it, the gym records it as a channel.

The unconsumed remainder is **`hit_yaw`**: its axis and blend spaces are built like any other, and
nothing at runtime drives the parameter.

### 1.2 Decoded and **not** consumed — the actionable set

| Data | Where | Reach | Confidence |
|---|---|---|---|
| **Per-vertex normals**, SKINNED — plain `float[3]`, no table, no unpacking | `StudioVertex`+24 | 3,124 of 4,567 `StudioModel` entries; every character | **Confirmed** |
| **Per-vertex normals**, packed — byte offset / index into the `StudioRender` table | `StudioVertex2`+6, `StudioVertex3`+3 | 1,082 + 361 entries; the props | **Confirmed** |
| **IK chains** — `rhand`/`lhand`/`rfoot`/`lfoot`, 3 links each, 28B links | `MDLHeader`@368/372 | 239 models | **Confirmed** |
| **Surface property** — physical material name | `MDLHeader`@392 | 1,923 named; 2,522 genuinely unset | **Confirmed** |
| **Contents** — `CONTENTS_SOLID`, plus the per-bone field at `StudioBone`+156 | `MDLHeader`@420 | 7 models non-solid | **Confirmed** |

Two supporting facts that make the first row actionable: the authored normals agree with a
recomputation to 0.03–0.42° on clean spheres, which establishes shared frame and sign; and they
diverge by a mean of 11–15° elsewhere because they encode hard edges no averaging can reproduce.
568 of 129,870 sampled vertices carry a zero-length normal and need a fallback.

### 1.3 Undecoded

| Slot | State | What would settle it |
|---|---|---|
| `MDLHeader`@412, @416 | `0` on all 4,445; **no shipped binary reads either through a studiohdr pointer** | A Troika-side artifact (studiomdl, a QC-era tool), or a model carrying a non-zero value. The install offers neither. |
| `StudioModel`+80, +84 | `0` on all 4,567 model records | Same class of problem; no consumer identified |
| `StudioSeqDesc` interior (764B) | Much of it read by no observed consumer | Broader runtime capture, or targeted decompilation |
| `StudioAnimDesc` remainder (64 of 72B) | Same | Same |
| `.phy` vphysics | Deferred by choice, not blocked — ~2/3 of models carry one | Owned by `docs/vtmb/phy_vphysics.md` |

### 1.4 Never surveyed

**No payload byte-coverage map has been run.** The survey work so far has profiled the header and
walked declared structs. A coverage map — claim every byte range a known array owns, then inspect
what is left — is the systematic way to find undecoded regions *inside* payloads, and it is the
one method gap that could still surface something large.

---

## 2. What Unreal 5 can use

| Finding | Engine mechanism | What changes |
|---|---|---|
| Authored normals | `FMeshDescription` vertex-instance normals with `bRecomputeNormals = false` | Correct shading and authored hard edges; MikkTSpace tangents derive from them; facial morph normal deltas finally land on a matching base; Lumen reads them |
| IK chains | `UIKRigDefinition` solver chains, Two Bone IK | Foot placement on stairs and slopes; weapon grip and prop interaction; optionally the engine's supported retargeting path |
| `hit_yaw` | An existing blend-space axis, driven by damage direction | Directional hit reactions across the cast — the axis is already built, only the driver is missing |
| Surface property + `scripts/surfaceproperties.txt` | `UPhysicalMaterial`, `EPhysicalSurface`, a data-driven impact table | Physics (density/elasticity/friction), footsteps, and a 5×3 impact-sound matrix |
| `Contents` | Collision channel setup on the baked body | The 7 non-solid models stop colliding |

**On the surface-property table.** `scripts/surfaceproperties.txt` ships 63 property definitions
carrying far more than names — physics constants, footsteps, and an impact matrix of
`{bullet, metal, wood, blade, fist} × {soak, norm, crit}`, VtMB's own weapon classes crossed with
its damage-resolution outcomes. Owned by `docs/vtmb/surface_properties.md`, which also records the
two decode traps: `base` chains must be resolved transitively, and a repeated key is a random
variation pool rather than an override.

**A distinction worth keeping.** A model's surface property is what the model is *made of*, so it
drives impacts against it. Footstep sounds come from the *world* surface being walked on, declared
as `$surfaceprop` on 4,606 of the install's `.vmt` files. Both resolve through the same table.

---

## 3. Priority by impact

Ranked by visible improvement against cost. Ranking only — sequencing belongs to the roadmap.

| # | Item | Impact | Cost | Note |
|---|---|---|---|---|
| 1 | **Authored normals** | High — fixes a live regression | Low | One exporter field, one build flag, one fallback for zero-length normals |
| 2 | **Surface properties** | Medium-high, very broad | Low-medium | A decoder against a shipped table; lands physics, footsteps and combat audio at once |
| 3 | **Foot IK** | Highest visible feel gain | High | IK Rig per family, graph nodes, ground trace |
| 4 | **`hit_yaw` reactions** | High for combat feel | Medium | The axis exists; needs a direction through the damage path |
| 5 | **`Contents`** | Low, but a correctness fix | Very low | 7 models |
| 6 | **IK Rig retargeting** | Potentially structural | High | Evaluate before committing; the current compatible-skeleton path works |

Item 1 is first because it is the only entry that repairs something currently wrong rather than
adding something absent. Item 3 has the largest payoff of any item here and is ranked below
cheaper work only on cost.

---

## 4. Caveats on the numbers

- **Cast coverage is unverified.** IK chains reach 239 models and pose parameters 451. Whether
  those sets cover the playable cast or a subset has not been checked against the NPC manifest.
- **`SurfaceFriction` is hardcoded to 1.0 on the mover** (`ElysiumMovementComponent.h`), justified
  by a comment stating that 1 of the install's 11,624 `.vmt` files carries a `$surfaceprop`.
  **4,606 of 11,627 do.** The conclusion very nearly survives the correction — resolved through the
  `base` chain and VtMB's ×1.25-clamped-to-1.0 rule, every common world surface lands on exactly
  1.0 because `default` friction is 0.8 — but `glass` resolves to 0.5, giving 0.625 on 334
  materials. So the constant is a simplification with one real exception, not the derivation the
  comment claims.
- **`kneeDir` is zero on all 2,928 IK links**, so no pole vectors are authored. Any IK solver must
  derive its own.
- **Consuming packed normals makes `StudioRender.dll` a hard dependency for the prop path**, where
  it is currently optional. A recompute fallback is needed when the DLL is absent.

---

## 5. Method notes — the traps this format sets

Recorded because each one produced a wrong published answer before it was caught.

**An in-range value is not evidence of an offset.** Header slots past 420 were profiled as
"resolves inside the file on all 4,445 models" and named as indices. They were `StudioBone[0]`
fields — a float triple at hip height reads as a large in-range integer. The header ends at 424,
which `BoneIndex`@244 states on every model. Anchor a field by decoding what it addresses.

**A wrong stride can validate.** The IK link array appeared correct at strides 16, 20 and 28,
because at the wrong strides every link read bone 0 — the root, trivially in range. Only 28 yields
parent→child chains. Choose a check that a wrong answer fails, not one every answer passes.

**Both community reference headers are wrong at the tail, in different directions.** VAMPTools
omits a field pair and drifts 8 bytes from `NumFlexDescs` onward; Crowbar has correct widths but
placeholder names, and at @420 it struck out the correct field and substituted a placeholder.
Neither is a Troika artifact. Past `SurfacePropIndex`@392, settle against the shipped binaries.

**Check whether the codebase already reads the field, before calling it a gap.** Twice in one pass:
the secondary-motion pair at 396/400 was profiled as unknown while `docs/vtmb/secondary_motion.md`
already documented it, and pose parameters at 384/388 were called unconsumed while
`mdl_skel.pose_parameters` read them, the exporter shipped them, and the blend-space bake set
`bWrapInput` from them. A byte profile says what a field contains, never whether something already
uses it — that question is answered by grepping the repository, and it costs one command.

**An unauthored array's index is a write cursor, not a null.** `TransitionIndex` equals
`BodyPartIndex` and `BoneControllerIndex` equals `AttachmentIndex`, both on all 4,445 models. Gate
on the count; the index alone never reads as absent.
