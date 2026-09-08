# MDL v2531 Coverage and Gaps

## What the format carries, what the pipeline reads, and what Unreal could do with the difference

**Game:** *Vampire: The Masquerade — Bloodlines* (2004)
**Primary target:** Troika's modified early Source engine, MDL version `2531`
**Document role:** Exploration research brief; not a status tracker or fact owner
**Last reviewed:** 2026-08-11

Byte layouts, struct offsets and decoded values belong to `docs/vtmb/mdl_v2531.md`; this brief
cites that document rather than restating it. Skeletal-animation behaviour belongs to
`docs/vtmb/animation_and_movers.md`, flex and gaze to `docs/vtmb/facial_animation.md`, bone-chain
motion to `docs/vtmb/secondary_motion.md`, collision to `docs/vtmb/phy_vphysics.md`.
The impact ranking below is an assessment of what each finding would change, not a schedule.

---

## Executive conclusion

The dominant gap in this format is **not undecoded bytes. It is decoded fields that nothing
reads.**

Five pieces of authored data are understood but do not drive shipped runtime behavior here:
per-vertex normals, IK chains, pose parameters, the surface property, and the per-material maximum
world-units-per-texel metric. All five
are retained by the Character GLB exporter. The first four can drive host features; the texel
metric remains metadata because the shipped runtime does not read it.

Genuinely unidentified semantics are bounded but not closed. Secondary-motion record `+8` is an
unused authored preset whose physical authoring name is unavailable, and the full character byte
ledger has exposed compiler-retained donor/cache payloads whose runtime exclusion is settled but
whose last internal byte boundaries are not all implemented in the ledger (§1.5).
`StudioTexture+16` is the compiler's conservative
maximum world-units-per-texel metric; only its exact compiler-side reducer remains unavailable.
The two header slots are reserved zero storage, and every *indexed* `StudioSeqDesc` and
`StudioAnimDesc` is carried as a complete typed record.

---

## Confidence language

| Term | Means |
|---|---|
| **Confirmed** | Anchored by decoding what the field addresses, or by disassembly of a consuming instruction, with a stated check a wrong answer fails |
| **Strong evidence** | Independent corpus tests select one semantic family, but the original compiler reducer or authoring name is unavailable |
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
| **Maximum world units per texel** — larger representative U/V material scale | `StudioTexture`+16 | 11,817 typed values; no runtime reader | **Strong corpus evidence** |

Two supporting facts that make the first row actionable: the authored normals agree with a
recomputation to 0.03–0.42° on clean spheres, which establishes shared frame and sign; and they
diverge by a mean of 11–15° elsewhere because they encode hard edges no averaging can reproduce.
568 of 129,870 sampled vertices carry a zero-length normal and need a fallback.

### 1.3 Undecoded

| Slot | State | What would settle it |
|---|---|---|
| `MDLHeader`@412, @416 | reserved zero storage: `0` on all 4,445 and no shipped reader | No authored value population remains; compiler-side names are unrecoverable |
| secondary-motion record +8 | unused authored preset `{9,30,60}` over 600 records | A compiler-side name; every recovered runtime walk demonstrably omits it |
| compiler-retained donor/cache blocks | Disabled include groups, animation payloads and mesh donors in §1.5; declared counts make them unreachable | Complete the typed subrange walk so the ledger can classify every retained byte without an opaque catch-all |
| isolated compiler residue | Cloth alignment, string remnants and one orphan RLE sample in §1.5 | Preserve each bounded shape under its proven runtime-unused classification |
| `.phy` solid-transform frame | Hulls and KeyValues are decoded; the ragdoll solid `origin`/`angles` mapping remains open | Owned by `docs/vtmb/phy_vphysics.md` |

### 1.4 Payload byte coverage

The Character GLB exporter now runs a strict per-file byte ledger over every direct source member.
Independent MDL, VTX, PHY, VFE and TXT walkers claim decoded records, payloads and referenced
strings; remaining zero runs are retained positionally as verified zero storage. Any unclaimed
non-zero byte, overlap, out-of-range declaration, source hash mismatch, or non-zero byte inside a
zero-classified range aborts publication. The ledger is emitted in the required
`ELYSIUM_vtmb_character` extension and revalidated before the GLB is written.

This closes the previous method gap for the Character GLB slice. It does not make dependency
payloads part of the character: VMTs and included animation-bank MDLs are hashed references owned
by their separate export products.

### 1.5 Full character-export census

The strict `export_v2 characters-glb` pass admits **484** installed character models. The first
corpus run published **280** and refused **204**. These are first-stop categories: correcting one
gate can expose a later problem in the same model, so they describe why that run stopped rather
than disjoint properties of the source files.

| First refusal | Models | Finding |
|---|---:|---|
| non-finite semantic value | 105 | Exporter representation defect. A flex-op operand is a union; projecting every operand as both int and float turns integer bit patterns such as `0xffffffff` into NaN. Retail reads float only for `CONST`, int only for `FETCH1`/`FETCH2`, and ignores it for arithmetic ops. |
| byte ledger | 53 | Real coverage refusal. The detailed families are below; some are decoder extent errors, while the rest are bounded dead duplicates or compiler-retained donor/cache data that still need explicit typed ledger ranges. |
| TXT/VFE facial pair | 27 | Over-strict provenance gate. Twenty-one differ only because TXT rounds to three decimals; four have authored row-name differences, Pisha's compiled table has five additional controller keys, and `scrubs_female_phonemes.txt` has no same-stem VFE. Retail loads VFE, not TXT. |
| PHY checksum mismatch | 14 | Over-strict source-closure gate. The PHY checksum is compiler provenance, not a retail admission test; `VCollideLoad` receives the solid count and bytes after the 16-byte header, not the MDL checksum. |
| PHY KeyValues | 2 | Valid inline `break { ... }` blocks rejected by a parser that only accepts multiline blocks. |
| zero-key facial table | 1 | Valid `crooked_cop_expressions` table: 32 labelled settings and an empty controller/value vector. |
| MDL declared length | 1 | The patch's `taine.mdl` has one trailing `0x0a` after its declared MDL image. |
| VTX variant disagreement | 1 | Barabus has LOD0 in DX80 and LOD0..6 in DX7; their shared LOD0 is identical. Whole-variant equality is the wrong check. |

The 53 byte-ledger stops divide as follows:

| Shape | Models | Current interpretation |
|---|---:|---|
| disabled PC include payload | 15 | Exactly two unindexed 116-byte `StudioModelGroup` records plus two `56 × NumBones` remap arrays per model. Their paths name PC-idle/frenzy banks, but `NumIncludeModels == 0`; all retail walkers gate on that count. |
| donor animation payload | 7 | Six unindexed bone-record/RLE payloads plus Animalism's complete named `wolf_morph` descriptor and payload. The local animation/sequence counts and grids exclude them. |
| cloth map extent/alignment | 10 | Nine use more VTX LOD rows than the definition matrix. Doppleganger stores a shortened tangent prefix whose omitted 69-vertex suffix is entirely selector `0xFF`; StudioRender reads the tangent map only on the selected branch. |
| duplicate AxisInterp table before the referenced table | 5 | Exact byte-for-byte duplicate records; `ProcIndex` selects the second copy, making the first compiler residue. |
| unreferenced string fragment | 5 | Recognisable material/include/animation string residue not reached by any declared string index. |
| collision-triangle alignment residue | 2 | One non-zero u16 after a packed `u16[3]` triangle array. StudioRender advances exactly 6 bytes per triangle, proving the word is not a fourth index. |
| full-body donor mesh | 2 | The same 596,392-byte retained body block in `walkie_talki1` and `sabbat_hand`: it begins with an unindexed 4,187-vertex/6-flex `StudioMesh` and matches the `sabbat_henchman` body layout at the same offsets, while each file's model header selects a later small active mesh. |
| recursive include cache/donor records | 2 | Eight female and nine male `StudioModelGroup` records plus remap pointers retained after the declared `allsequences` group array. They name the tail of the recursively included `alsequences` DAG; the declared count excludes them and recursion loads the real child records. |
| unindexed Taxida mesh payload | 1 | A 348-vertex mesh record followed by packed vertex and tangent-shaped arrays; the model header selects a later 568-vertex mesh. The patch-only compiler retained both. |
| duplicate texture table | 1 | Twelve complete 20-byte `StudioTexture` records before the indexed 13-record table in `sabbat_henchman`; names and texel metrics match the indexed records (apart from one case-only spelling). |
| extra secondary-motion records | 1 | Venus retains two additional valid 28-byte records after its declared five; the count excludes them. |
| duplicate secondary-motion subset | 1 | Tremere female armor 1 retains five valid records, all byte/semantic duplicates of records in its indexed ten-record table. |
| orphan animation sample | 1 | One complete four-byte, one-frame RLE sample immediately before the first referenced channel of `move_and_ranged` animation 100. No channel offset names it. |

Therefore the present evidence boundary is exact: **280/484 products account for every direct
source byte under the ledger; the other 204 are refused, never silently published.** Fixing the
151 known false refusals does not by itself prove full-corpus byte closure; the 53 ledger cases
remain the gate until each bounded donor/residue shape has explicit ranges and a regression.

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
