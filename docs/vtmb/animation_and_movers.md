# VtMB animation & movement — skeletal models and brush movers

Everything in VtMB that *moves* falls in two subsystems: **skeletal model
animation** (NPCs, animals, animated props — a rigged `.mdl` skeleton driven by
compressed keyframe clips) and **brush-entity movers** (doors, buttons, spinners,
elevators, trams — a brush model translated/rotated at runtime by parametric logic).
This documents the on-disk format and runtime behaviour of both. It is the
companion to `docs/vtmb/mdl_v2531.md` (static-geometry half of the `.mdl`) and
`docs/vtmb/entity_io.md` (the I/O bus the movers ride).

Evidence tags: **[VtMB]** = decompiled `vampire.dll` (image base `0x10000000`);
**[data]** = shipped map/model bytes, probe-verified; **[ref]** = a VtMB-native
reference parser (VAMPTools / Crowbar); **[SDK]** = modern Source baseline
(`$ELYSIUM_WORK_ROOT/research/reference-source/source-engine/public/studio.h`); **[inferred]** = reasoned, not yet
confirmed. All counts are over the **patched** install.

---

# Part A — Skeletal model animation (`.mdl` v2531)

`docs/vtmb/mdl_v2531.md` decodes the static half (header, bodyparts, meshes, three vertex
formats, materials) and deliberately **raises** on the ~485 `models/character/**`
skeletal models. This part fills that gap: bones, sequences, the compressed
animation tracks, skinning, and attachments — enough to rig and play a clip. The **face**
— flex/morph data, the flex-controller and flex-rule layers, the (absent) eyeball chunks and
the `.lip` phoneme files — is `docs/vtmb/facial_animation.md`.

**Scale** [data]: **427** `character/**` `.mdl` in the VPKs (485 merged with the
patch); **377 are true skeletons** (>5 bones), median **67 bones**, up to 270
(`rat_crowd`), on the 3ds Max **Biped** rig (`Bip01`/`Bip01 Pelvis`/`Bip01 Spine`…).
Sequence counts run 1→**602** (`character/shared/male/move_and_ranged.mdl`).

## A.1 The headline divergence

**VtMB v2531 animation is *not* modern Source's format** [ref, both parsers agree;
data-verified]. The modern `mstudioanim_t` linked-list with `Quaternion48/64`,
`Vector48`, and `STUDIO_ANIM_RAWPOS/RAWROT/DELTA` flag compression **does not exist
here** — those types appear in VAMPTools' copied-forward `studio.h` but the VtMB
parser never uses them. The real scheme is the older **HL2-Beta-2003** layout: a
**fixed 32-byte record per bone** holding **7 channel offsets** (posXYZ + quat XYZW),
each pointing at a classic `{valid,total}` RLE short-value track. Rotation is
animated as a **4-component quaternion**, not 3 euler DOF.

## A.2 `StudioBone` — 160 bytes [data-verified]

The skeleton: bind pose + hierarchy + the per-DOF scales the animation deltas
multiply against. Array of `NumBones` (@240) at `BoneIndex` (@244).

| Off | Type | Field | Note |
|---|---|---|---|
| 0 | int | `NameIndex` | rel. to bone base → `"Bip01 Spine"` |
| 4 | int | `ParentBone` | bone index; `-1` = root |
| 8 | int[6] | `BoneController` | per-DOF controller index, `-1` none |
| 32 | Vector | `pos` (3f) | bind position (Source inches), **parent-relative** |
| 44 | Quaternion | `quat` (4f) | bind rotation (x,y,z,w), normalized |
| 60 | Vector | `posscale` (3f) | position-delta scale (anim short × this, added to bind) |
| 72 | Quaternion | `rotscale` (4f) | **quaternion**-component scale (anim short × this = the component itself, per §A.4 — not a delta) |
| 88 | matrix3x4 | `poseToBone` | 12f row-major — inverse-bind (world→bone) |
| 136 | int | `Flags` | `BONE_USED_BY_*` / physics mask; bit `0x2` selects split rotation/translation inheritance during `BuildTransformations` — see §A.4a |
| 140 | int | `ProcType` | procedural rule (1=axisinterp, 2=quatinterp, jiggle); 0=none |
| 144 | int | `ProcIndex` | → procedural-rule struct (rel. bone base) |
| 148 | int | `PhysicsBone` | ragdoll bone index |
| 152 | int | `SurfacePropIndex` | → surface-property name |
| 156 | int | `Contents` | BSP contents flags |

**v2531 signature** vs modern Source's 216-byte `mstudiobone_t`: there is **no
`RadianEuler rot`, no `qAlignment`, no `unused[8]`**; `rotscale` is a **quaternion**
(4f), not a 3-axis euler vector. Probed on `jeanette.mdl`: root `Bip01`
parent=−1 `pos=(0,0,39.67)`, uniform `posscale=(1/256,…)`, `rotscale ~1e-5`; stride
160 verified end-to-end (bone[70] name `"tire iron"` = a weapon-carry bone).

A patch-first whole-install audit (`pipeline/src/elysium_pipeline/validation/validate_skeletal_pipeline.py`) reads
**433 character skeletons / 30,530 bones**. Every bind quaternion is normalized
within `9.24e-8`, every parent precedes its child, and every one of the **373**
`Flags & 0x2` bones is `Bip01 Spine1`. All 373 split bones' `poseToBone`
matrices are conventional hierarchy-FK inverse binds (worst identity error
`7.36e-6`), not inverse binds made from split-inheritance FK. Split inheritance
is therefore a live-pose application rule only; the skin bind remains ordinary.
The only material whole-cast inverse-bind exceptions are the two weighted tooth
bones in `doppleganger_female.mdl`, whose source `poseToBone` matrices do not
invert their declared hierarchy bind. That model is not in the current generated
cast and remains a source-data exception rather than a reason to rewrite all
inverse binds.

The same audit finds **3,104 bones with `ProcType != 0`**. Their rule payloads
and retail application are not decoded by `mdl_skel.py`, so the generated
skeleton preserves their ordinary bind/animation channels but omits the
procedural contribution.

`Flags & 0x1` marks the same bones: over the 160 model images of one theatre
capture, **295 of 295** bones with `ProcType != 0` carry it and no other bone
does. Those bones never carry `Flags & 0x2` — the two sets are disjoint across
the same corpus — so split inheritance and the procedural stage are independent
rules on independent bones.

The rule these fields declare, what omitting it costs a composed pose, and the
persistent bone-to-world array it is evaluated into are owned by
`docs/vtmb/procedural_bones.md`.

## A.3 Sequences and animations

**`StudioAnimDesc` — 72 bytes** [data-verified] (`NumLocalAnims`@264 /
`LocalAnimIndex`@268). The per-clip descriptor:

| Off | Type | Field | Note |
|---|---|---|---|
| 0 | int | `NameIndex` | names begin `@` (e.g. `@Jeanette_Line1_Col_E`) |
| 4 | float | `fps` | **not uniform** — 30.0 on 1,436 of 1,502 sequences across six banks, but 18.0 ×54 (incl. `run`), 60.0 ×8, 20.0 ×4. A clip's own rate, so a consumer must read it rather than assume 30 |
| 8 | int | `flags` | loop/delta |
| 12 | int | `numframes` | 101–501 on probed clips |
| 16/20 | int | `nummovements` / `movementindex` | **root motion — present, and load-bearing for locomotion.** 0 on the dialogue clips (jeanette et al.), but **66 of `move_and_ranged`'s 722 animdescs carry it**: `walk` 23 records, `run` 9, `sneak` 1, every weapon walk/run variant. The records are located but **not decoded**, so a locomotion clip bakes in place. The native route motor moves the actor at retail walk speed, but its feet can slide until these records drive or calibrate the visual stride |
| 24 | Vector | `bbmin` (3f) | per-anim bbox |
| 36 | Vector | `bbmax` (3f) | |
| 48 | int | **`animindex`** | → per-bone anim records, rel. animdesc base |
| 52/56 | int | `numikrules` / `ikruleindex` | (0 on probed) |
| 60 | int[3] | trailing | (0,0,0) |

(VtMB has **none** of modern Source's `animblockindex`/`sectionindex`/`zeroframe`
streaming fields — older HL2-Beta layout.)

Across the whole character tree, **1,386 of 6,382 animdescs** carry **23,347**
movement records. The exporter does not decode them. This is not confined to the
original `move_and_ranged` probe: any consumer that moves an actor from the
ordinary local bone tracks alone omits authored root displacement.
All 6,382 animdescs have `numikrules == 0`; there is no animdesc IK payload to
recover in this corpus, although sequence-tail IK locks and autolayers remain
outside the first-pass exporter.

**`StudioSeqDesc` (`mstudioseqdesc_t`) — 764 bytes** [data-verified — stride
confirmed] (`NumLocalSeq`@272 / `LocalSeqIndex`@276). The game-facing entries; each
references anims through a blend grid: `szlabelindex`@0, `szactivitynameindex`@4,
`flags`@8, `activity`@12, `actweight`@16, `numevents`/`eventindex`@20/24 (anim events),
bbox@28, `numblends`@52,
then **`short anim[16][16]`@56** (512B **inline** blend grid; `MAXSTUDIOBLENDS=16`, a
v2531 fixed-size divergence from modern Source's variable `blend[]` pointer), then a
**196-byte trailing region** (568..763) whose blend and autolayer fields are decoded
below. The **fixed stride is 764B**, established by data: it is the unique
stride that resolves every sequence name across models of every size — jeanette
(22: `Jeanette_Line1_Col_E`…`ragdoll`), mingxiao (29: `run`…`mingXiao_death`),
`move_and_ranged` (602: `walk`…`holy_light_idle`); no other stride in [200,1400]
resolves the names on any model. On these NPCs the mapping is **1 seq ↔ 1 anim,
blends=1** (`anim[0][0]` → local anim), so a first pass reads
`seq → anim[0][0] → animdesc` and skips blend interpolation.

That statement is deliberately limited to the original probes. The whole
character tree contains **6,074 sequences**, of which **294** across 22 models
have `numblends > 1` (maximum 9). They include ordinary shared-bank locomotion
(`walk`, `run`, weapon movement/aim layers), frenzy runs, monster movement, and
hit/combat moves. `mdl_skel.local_sequences` and the GLB exporter still select
only `anim[0][0]`; the generated clip is a valid base cell, not a faithful
evaluation of those 294 blend grids.

The patch-first player-body union is a narrower, capture-oriented view of that
corpus. `clandoc000.txt` contains 216 indexed body references across 18 clan
table blocks, resolving to 56 distinct player MDLs. Their transitive include
graphs contain 122 owner MDLs with no missing includes, 3,330 raw sequence
descriptors, 3,615 raw animation descriptors, and 5,482 active blend cells.
There are 270 multi-blend sequences: 219 use a 9×1 grid, 49 use 3×3, and two use
5×1. Of the animations, 3,388 are referenced by an active cell and 227 are not.

Names cannot define coverage for this union. The 3,330 raw sequences collapse
to only 1,484 case-insensitive labels, and 1,430 of those label groups contain
more than one owner/sequence identity. The capture inventory therefore keys an
entry by exact owner model and raw index, retains the complete 764-byte sequence
descriptor and 16×16 grid, and records target-model compatibility separately.
The same rule applies to all 3,615 raw 72-byte animation descriptors.

### The blend grid is two axes, not a raw 16×16 [data-verified + VtMB decompiled]

The trailing region carries the blend space. Read from the runtime evaluator
(`FUN_10089500`, A.4b) and then checked against the installed corpus:

| Off | Type | Field | Note |
|---|---|---|---|
| 572 | int[2] | `groupsize` | extent of each axis; the grid is `groupsize[0] × groupsize[1]`, not 16×16 |
| 580 | int[2] | `paramindex` | pose parameter driving each axis, `-1` when the axis is unused |
| 588 | float[2] | `paramstart` | axis range, in the parameter's own units |
| 596 | float[2] | `paramend` | |
| 660/664 | int | `numautolayers` / `autolayerindex` | four bytes per entry, each a sequence index the dispatcher evaluates recursively |

A cell is `anim[i0][i1]` at `56 + (i0 * 16 + i1) * 2`: **axis 0 takes the row stride**, a
fixed 16 `short`s regardless of `groupsize`, so the authored grid is a sub-rectangle of the
inline 16×16 array.

**The order is settled by retail's own records, not by inspection.** Reading each captured
contribution's witnessed animation indices off its owner's grid, this address reproduces the
fired set on **8,302 of 8,302** multi-blend contributions across the theatre and tutorial
captures. The transposed address — `anim[i1][i0]`, which this document previously stated —
reproduces **1,858**, exactly the cases where the two coincide, and misses **6,444**. A 9×1
grid cannot tell them apart; a 3×3 names six of its nine cells wrongly.

**The extents-disagree fallback never fires on shipped content.** `groupsize[0] * groupsize[1]
== numblends` holds on **0 exceptions over 297 multi-blend grids across all 4,445 v2531 models**
— 9×1 (235), 3×3 (49), 2×1 (9), 5×1 (4) — so a reader's disagreement branch is defensive rather
than a path that silently drops cells.

`groupsize[0] * groupsize[1] == numblends`@52 holds for **294 of 294** multi-blend
sequences across the installed character tree, which is what establishes these as the
axis extents rather than nearby integers. Their distribution is 9×1 (235), 3×3 (49),
2×1 (6) and 5×1 (4).

**Pose parameter descriptors — 20 bytes** (`NumLocalPoseParameters`@384 /
`LocalPoseParamIndex`@388): `nameindex`@0, `flags`@4, `start`@8, `end`@12, `loop`@16.
A non-zero `loop` is a wrap modulus. The player-body corpus reads `move_yaw` and
`hit_yaw` with `flags` 1, `start` −180, `end` 180 and `loop` 360 — matching the
`paramstart`/`paramend` of every 9×1 sequence that selects them.

An axis resolves by wrapping the parameter into its loop range, normalizing it over the
descriptor's `start`..`end`, remapping through the sequence's `paramstart`..`paramend`,
clamping to 0..1, and scaling against `groupsize` to yield a cell index and a fractional
weight. An axis whose `paramindex` is `-1` yields cell 0 and weight 0.

### The activity name is the selection key [data-verified]

`activity`@12 reads **`-1` on disk for every sequence** — the game DLL resolves the *name*
to its enum at model load, so the durable key is the literal at `szactivitynameindex`@4,
which is present in the shipped bytes and decodes cleanly (`mdl_skel.local_sequences`):

| Model | Sequences | Carrying an activity | What they carry |
|---|---|---|---|
| `shared/male/pc_idles` | 1 | 1 | `idle01` = **`ACT_IDLE`** |
| `shared/male/stances` | 67 | 67 | all **`ACT_DISPOSITION`** — the disposition stance set |
| `shared/male/move_and_ranged` | 602 | 475 | 461 distinct (`ACT_WALK`, `ACT_RUN`, `ACT_IDLE_GLOCK`, …) |

The 127 sequences in `move_and_ranged` with an **empty** activity are the layer/plumbing
entries (`baseballbat_bobble_layer`, `claws_aggressive_walk_layer`) — additive helpers the
engine composes, not clips it selects.

`actweight`@16 is the weighted-random share among the sequences sharing one activity:
`claws_aggressive_run` carries 7 against its two `_alt` variants at 3 each, so the base run
plays ~54% of the time. The observed values are 1 (×279), 100 (×177), 0 (×127 — exactly the
activity-less layers), 20, 30, 7 and 3.

Together these are what lets a consumer ask for *an* `ACT_IDLE` rather than pattern-match a
label: `regular_cop` resolves 229 clips with "idle" in the name, of which `Stance_Dead_Idle_1`
and `Bed_Left_Idle` are not idles in any useful sense.

**The enum the names resolve into is in the binary** [decompile-verified]. `FUN_104126e0`
(`vampire.dll`) registers the whole activity table one name at a time — `("ACT_IDLE", 1)`,
`("ACT_TRANSITION", 2)`, `("ACT_FIDGET", 3)`, … — **3,045 names recovered from the dump**, indices
running 1…0xbfe. That is the map `activity`@12 is filled from at model load, and it confirms the
literal is the stable identifier rather than an artefact: the index space is the DLL's, so it would
change with a build, while the name is what the `.mdl` ships. Landmarks: `ACT_IDLE` = 1,
`ACT_SCRIPT_CUSTOM_MOVE` = 0x18 (the activity a `scripted_sequence`'s `m_iszCustomMove` selects),
`ACT_DISPOSITION` = 0xf1. The tail of the table is a long knockback/ragdoll family
(`ACT_KNOCKBACK_FLYING_*`), which is why the count is so much larger than any one model's vocabulary.

`flags`@8 correlates with looping (`walk`/`run`/`sneak`/every stance = 1; `crouch` = 0), but
`idle01` reads 0 while being a looping idle, and the values 2 and 0x14 are unmapped — the bit
meanings are **not** established, so looping is a consumer policy rather than a read of this
field. `numevents`@20 is non-zero on **124 of `move_and_ranged`'s 602** sequences; the event
array is located but not decoded. Whole-install scope is **1,044 event-bearing
sequences / 1,714 events**. The current exporter writes no event timeline.

## A.4 Animation data — the 32B/bone record + `{valid,total}` RLE [data-verified]

The core decode. At `animdesc_base + animindex` (@48): an array of **one 32-byte
record per bone** (bone order = header order), immediately followed by the
variable-length RLE blobs.

**Per-bone record (32B):** `float weight`@0 (always 1.0 — per-bone anim weight,
ignorable), then `int offset[7]`@4 — the 7 channel offsets **relative to this
record's start**, in order **posX, posY, posZ, rotX, rotY, rotZ, rotW**.
`offset[c]==0` ⇒ channel not animated (use bind value); `offset[c]>0` ⇒ an RLE track
at `record_base + offset[c]`.

**RLE track (`mstudioanimvalue_t`, 2B each — the one piece carried over unchanged
from Source):**

```
union { struct { byte valid; byte total; }; short value; }   // 2 bytes

# decode one channel over numframes:
remaining = numframes
while remaining > 0:
    valid = u8[p]; total = u8[p+1]; p += 2          # run header
    shorts = int16[p : p + 2*valid]; p += 2*valid   # 'valid' explicit keys
    # frames [0..valid) → shorts[i]; frames [valid..total) → shorts[valid-1] (clamp)
    remaining -= total
# frame sample = shorts[min(frame_in_run, valid-1)]
```

**The runtime does not walk the clip — it walks to the sampled frame**
[capture-verified]. The loop above is what an offline decoder does to materialise a
whole channel. `FUN_100889f0` and `FUN_10088ba0` instead subtract each run's `total`
from the frame while it does not exceed what remains, advancing by `valid*2+2`, and
stop at the run holding the frame. So a run the walk steps over costs **only its two
header bytes** — its keys are never read — and the run that holds the frame costs its
header plus the two keys bracketing the frame. When the frame after it leaves the run,
the quaternion decoder reads the **following run's first key** at `run + valid*2 + 4`;
the position decoder does not, and holds the key it already has. That asymmetry is the
one place the two decoders disagree about which bytes they touch.

**That look-ahead runs past the end of a track** [capture-verified]. Nothing conditions
the offset on there being a following run: whenever the frame sits on the run's last key
and the next frame leaves it, `run + valid*2 + 4` is read — and at the *last* run of a
track that names two bytes the track does not contain, so the decoder reads whatever the
image holds there. Measured on one `sp_theatre` capture differenced against a whole-track
walk: two such bytes, on `scenery/structural/la/LAmanhole.mdl`, whose one-run rotation
track is followed by the next clip's animation records, so the look-ahead lands in that
clip's `weight`@0. Whether the value it reads reaches the output is not established; what
is established is that the read leaves the track.

`FUN_10088ba0` additionally resets the frame to zero on any run declaring
`total < valid`, which no authored track in the corpus does.

**A zero weight ends the decode** [capture-verified]. `weight`@0 is documented above as
always 1.0 across the installed corpus, and both decoders compare it against zero
before anything else: on zero they write an identity-free zero output and return, having
read those four bytes and nothing else — no channel offset, no track, no bind field.

**Which bone fields each decoder reads** [capture-verified], per channel rather than
per bone: the quaternion decoder reads `quat`@44 for an unanimated channel and
`rotscale`@72 for an animated one; the position decoder reads `pos`@32 as the base of
every channel whether animated or not, and `posscale`@60 only for an animated one.
Neither reads `Flags`@136, and neither reads the `studiohdr` it is passed.

**Sample → bone transform** [data-verified, matches Crowbar `CalcBoneRotation`
(`SourceModel2531`)]. **Position and rotation are handled differently — this is the
one place to get exactly right, or animated limbs fling out.** Both are decoded
**per channel**, and an un-animated channel (`offset==0`) falls back to the **bind
value**, never 0:

```
# position: sample*scale is a DELTA on the bind position
pos[j] = bone.pos[j] + sample(posCh[j]) · bone.posscale[j]   if posCh[j] animated
       = bone.pos[j]                                          otherwise            # j = 0,1,2

# rotation: sample*scale is the ABSOLUTE quaternion component (NOT a delta on bind)
q[j]   = sample(rotCh[j]) · bone.rotscale[j]                  if rotCh[j] animated
       = bone.quat[j]                                         otherwise            # j = 0,1,2,3 (x,y,z,w)
q = normalize(q)
```

The rotation is **not** `bind + delta`: an animated component *replaces* the bind
component with `sample × rotscale`; an un-animated component *keeps* the bind
component. The difference only shows under animation (at the bind pose every bone
matrix is identity, so any decode reproduces the rest mesh) and bites hardest on
bones with **partial** rotation animation — adding a delta, or zeroing the
un-animated axes, tears the mesh at the joint (the clavicles, below). Adding the
bind quaternion to *every* channel, as a naive "delta" reading suggests,
double-counts the bind orientation and throws the whole skeleton off.

This mirrors Source's `CalcBonePosition` (delta on bind) and `CalcBoneQuaternion`,
adapted to v2531's 4-channel quaternion, where the modern euler+`AngleQuaternion`
path is replaced by direct per-component quaternion values. Probe proof (`jeanette`
anim0, `Bip01 Spine` rotZ): runs `(38,42),(40,44),(48,52)…` sum to `numframes=501`;
`rotscale.z=6.05e-6`. Decodes clean across all probed bones; verified in-engine by
`gangmember_male_2 walking_suitcase` reconstructing a correct upright walk cycle.

Zeroing an un-animated rotation axis (instead of keeping bind) survives most bones —
a limb bone's bind `x/y/z` are ~0, so 0 and bind coincide — but collapses any bone
whose bind carries a **large rotation**. The clavicles are the canonical victims:
an idle clip animates only their `w` channel (`......1`), so a 0-fill gives
`(0,0,0,w)` = identity, snapping the shoulder ~150° off; keeping bind `x/y/z` gives
the correct rest shoulder. The exporter (`pipeline/src/elysium_pipeline/formats/mdl_gltf.py`) consumes this
decode verbatim.

## A.4a Bone flag `0x2`: split rotation/translation inheritance

**Status: application verified through bone-to-world construction.** One bone per ordinary biped — normally
**`Bip01 Spine1`** — carries `Flags & 0x2` at bone offset 136. The local animation
channel decoder does not branch on it: `client.dll` `FUN_10089b20` walks the
selected bones and calls `FUN_100889f0` for rotation plus `FUN_10088ba0` for
position. `FUN_100889f0` reads all four rotation offsets, substitutes the bind
quaternion component for an absent channel, decodes `RLE_short * rotscale` for a
present channel, and interpolates adjacent frames. It receives the 160-byte bone
record but never reads `flags@136`. This verifies §A.4's **local-channel** formula;
the flag is a post-decode hierarchy rule rather than an alternate channel format.

The live rule is in `client.dll` `FUN_1008fd00`, identified by its VProf string as
`C_BaseAnimating::BuildTransformations`. `C_BaseAnimating::SetupBones`
(`FUN_100919c0`) calls the standard pose builder through vtable slot `+0x208`, then
calls this function through slot `+0x1ec`. Raw stack arguments prove its compact
contract is `(positions, quaternions, rootToWorld, selectedBones)`.

For every selected bone, the function builds a local 3×4 matrix `L` from decoded
quaternion `q[i]` and position `p[i]`. With Source's
`ConcatTransforms(A, B) = A * B`, it then executes:

```text
if parent[i] == -1:
    boneToWorld[i] = rootToWorld * L
else if (flags[i] & 0x2) == 0:
    boneToWorld[i] = boneToWorld[parent[i]] * L
else:
    rotation(boneToWorld[i]) = rotation(rootToWorld) * rotation(L)
    translation(boneToWorld[i]) =
        TransformPoint(boneToWorld[parent[i]], p[i])
```

The flag test is at `0x1008ffc5`; the split-inheritance branch is
`0x1008fff3–0x100901b5`. Thus a flagged spine keeps its positional attachment to
its parent but does **not** inherit that parent's rotation. Its orientation is
rooted directly in the entity transform. This is steady live pose construction,
not a courtroom special case.

**The rule reproduces the captured matrices.** Composing a capture's own composed
locals under it and placing them by the captured root frame returns the
bone-to-world the same draw recorded: over 110,082 paired records of one theatre
cutscene, **0 of 84,202** `Flags & 0x2` bone observations leave the excellent band
of `docs/vtmb/vtmb-animation-reverse-engineering.md` §11.3 on translation,
model-space translation or rotation. The ordinary hierarchy leaves it on **8,142
of the same 84,202** — and on **rotation only**, with translation identical under
both rules, which is the pseudocode's own shape measured rather than assumed: the
branch changes where a flagged bone's orientation comes from and leaves its
position attached to its parent either way. That the ordinary rule agrees on the
other 90% is expected rather than a weakening, since the two coincide whenever a
flagged bone's parent chain carries no rotation relative to the entity transform.

The two frames are related exactly — `boneToWorld = rootToWorld · modelSpace`
holds through the split branch as well as the ordinary one, since
`rotation(rootToWorld) · rotation(L)` is the rotation of
`rootToWorld · modelSpace[i]` — so entity placement divides out of a comparison
rather than being estimated.

**A slot outside the selected mask is not written, and a root is not always a
frame.** `BuildTransformations` writes only the bones its fourth argument selects,
so an unselected slot holds whatever the bone cache last left there — frequently
zeros — and reads as a matrix without being one. Separately, the `rootToWorld` the
stage receives is not always a rotation and a translation: **4,415 of 238,421**
composed poses in one cutscene carry a **singular** root, and they are only **two
distinct matrices**, on `Sheriff.mdl`, `Ash.mdl`, `Skelter.mdl` and `Isaac.mdl` —
scratch the caller never filled in rather than a transform. Both are properties of
the runtime; what they cost a reader is that a comparison must exclude such a slot
instead of scoring it, because two zero matrices agree exactly on translation and
disagree by `acos(-0.5)` = 120° on rotation, and neither number means anything.

A separate client path at `FUN_10091110` does read the flag. It is the sequence
transition-maintenance path, not a ragdoll path or the local-channel decoder. The
two live pose builders at `FUN_10091650` and `FUN_100979b0` call it
immediately after resolving the current base sequence pose and before autoplay
sequences, weighted layers, virtual hooks, and bone controllers.

`FUN_10091110` maintains a variable array at entity offsets `+0x67c/+0x688` whose
entries are 0x4c-byte previous-sequence records. It detects a sequence change,
records sequence/cycle/time, computes transition weights, evaluates each surviving
previous sequence through `FUN_100968a0`, and blends it into the current pose through
`FUN_10096b30`. A record whose byte `+0x18` is set also carries a saved 3×4 entity
transform at `+0x1c`.

The array is a `CUtlVector` at `+0x67c` — the frame takes its address as a `this`
pointer and reads the buffer through it — with the **element count at `+0x688`**, which
the function compares against zero before anything else. `FUN_10091110` is
`__thiscall(this, five stack dwords)`, cleaning `0x14` at its own epilogue, the same shape
as `SetupBones`. So a sequence change is observable from outside as a growth in that
count, without decoding an entry: what the 0x4c bytes hold beyond the fields above is not
established, and the sequence now playing is already named by the evaluations the same
pose build fires.

Only while rebuilding one of those saved previous poses does it special-case every
root bone or bone with `Flags & 0x2`. Raw stack accounting establishes the full
frame conversion:

```text
savedEntity = AngleMatrix(savedAngles, savedOrigin)
currentEntity = AngleMatrix(currentRenderAngles, currentRenderOrigin)
delta = inverse(currentEntity) * savedEntity
adjusted = delta * Matrix(decodedQuaternion[i], decodedPosition[i])

decodedQuaternion[i] = Quaternion(MatrixAngles(adjusted))
if parent[i] == -1:
    decodedPosition[i] = TranslationColumn(adjusted)
```

The saved matrix is captured at record `+0x1c`. Matrix inverse
`FUN_10108450`, concat `FUN_10108a50`, matrix-to-angles `FUN_10107eb0`,
angles-to-quaternion `FUN_1010a650`, and column extraction `FUN_10108510`
agree on this order. A flagged non-root bone receives the adjusted rotation but
keeps its decoded position; a root receives both. The branch therefore preserves
root/special-bone orientation, plus root position, when a previous pose sampled in
the saved entity frame is blended under the current render frame. It is not
evidence for baking a fixed multiplier into every exported animation key.

VAMPTools confirms that `0x2` is semantically significant, but not how retail
applies it. Its `Animation.cpp::ConvertToEuler` path inverts the quaternion,
re-decomposes it, and permutes axes while producing FBX Euler tracks. That is a
reference-export operation, not proof that a fixed quaternion should be pre- or
post-multiplied into direct glTF keys.

Three simple exporter candidates are all contradicted by at least one pose:

- a fixed left multiplication keeps a shared neutral stance upright but makes the
  courtroom seated torso unnaturally straight;
- no flag-specific operation makes that seated lean plausible but folds the
  ordinary neutral stance sideways;
- the VAMPTools-like/right-multiply candidate can put the seated head below the
  pelvis.

The retail rule explains why none is an oracle. A conventional glTF hierarchy
always inherits the parent's rotation. To reproduce one sampled retail pose with
such a hierarchy, the equivalent flagged-bone local rotation would have to be:

```text
inverse(rotation(parentBoneToWorld))
    * rotation(rootToWorld)
    * rotation(decodedLocal)
```

That correction changes as the parent pose changes. A fixed left or right
multiplier cannot reproduce it, and independently correcting each source clip
does not automatically preserve the rule after runtime sequence/layer blending.
The faithful representation therefore needs either split-inheritance evaluation
at runtime or fully evaluated model-space animation data with explicit blending
semantics.

`npc_index.json` v3/v4 accepts an optional `split_bones` array on each target
mesh; the exporter fills it directly from that model's
`StudioBone.Flags & 0x2`. This is retained as diagnostic metadata only. Shared
and cinematic banks remain raw local channels.

The source-to-output validator found that the local generated corpus predates
that raw-key policy: **286 GLBs / 4,276 flagged-bone rotation tracks / 611,191
samples** contain the exact discarded fixed rewrite
`q_generated = (0.5,0.5,-0.5,0.5) * q_raw` (xyzw). All non-flagged tracks match
the current exporter equation. The live experiment that subsequently replaced
the flagged component rotation after crossfading therefore operated on already
rewritten keys. Its quarter-turn discontinuity is evidence of stale output plus
double/misapplied correction, not evidence against the decompiled retail branch.

The coordinate seam itself is closed below. After regenerating raw keys, the
equivalent Unreal local rotation for a flagged bone is the parent component
rotation inverse multiplied by the raw decoded local rotation, evaluated
**after** sequence/layer blending; the fixed skeletal-component yaw remains
outside the pose. The current runtime still evaluates a conventional hierarchy
and does not consume `split_bones`, so the post-blend correction remains
implementation work. The rendered retail invariant below pins the behavior it
must reproduce.

## A.4b Retail pose pipeline — durable Ghidra proof path

Detailed capture and experiment status is tracked in
`docs/project/retail-capture-roadmap.md`. Three tracked investigation specifications divide the
retail path at its DLL boundaries: `animation_pose.json` (`client.dll`),
`animation_skinning.json` (`engine.dll`), and `animation_studiorender.json`
(`StudioRender.dll`) under `research/cases/animation-pose/specs/`. They preserve pinned binary
hashes, seed addresses, working aliases, direct call edges, confidence,
eliminated leads, and open questions independently of the gitignored Ghidra
project. The tracked driver serializes Ghidra runs with the required project-lock
delay and rebuilds a local context pack:

```powershell
uv run elysium research animation-pose research/cases/animation-pose/specs/animation_pose.json --dry-run
uv run elysium research animation-pose research/cases/animation-pose/specs/animation_pose.json --binary <client.dll>
uv run elysium research animation-pose research/cases/animation-pose/specs/animation_pose.json --address 10091110
```

Binary-derived decompilation, assembly, xrefs, a run log, and an index land under
`$ELYSIUM_WORK_ROOT/research/ghidra/$ELYSIUM_EXPORT_ROOT/animation_pose/` and remain uncommitted. A Ghidra re-import does
not discard the investigation entry points because the specification recreates
the pack.

The evidence labels in this section mean:

- **confirmed:** direct listing/decompile behavior whose arguments and offsets
  agree, or a byte-level fixture independently reproduces it;
- **partial:** the local behavior is known but its caller, consumer, or coordinate
  frame is not;
- **hypothesis:** a semantic working name used to direct the next xref/caller
  pass; it is not an implementation contract.

The confirmed local decode chain is:

```
FUN_100968a0  virtual-model/base-model pose selection
  -> FUN_10089c40  local/include pose dispatch
     -> FUN_10089740  sequence/blend evaluation
        -> FUN_10089b20  selected-bone loop
           -> FUN_100889f0  quaternion channels
           -> FUN_10088ba0  position channels
```

**The four frames share one seven-dword `__cdecl` contract**
`(studiohdr, positions, quaternions, sequence, cycle, poseParameters, boneMask)`, except
`FUN_10089b20`, which takes the animation descriptor of one cell in place of the sequence
index and receives no pose parameters. `FUN_100968a0` is the only `__thiscall` of the
group: it obtains the entity's `studiohdr` from `C_BaseAnimating::GetStudioHdr(-1)` and
forwards its own six stack arguments unchanged. Every exit below it is a bare `RET` and
every caller cleans, so the owning studio header is argument zero at each stage rather
than something a consumer has to infer.

That is what makes source attribution possible: a capture on these frames reads the
*owner* — which for a character is usually a shared bank that is nobody's entity model.
Two complete `sp_theatre` runs record 238,529 and 238,793 sequence contributions naming
34 and 35 owner identities, of which 17 and 18 are owners no actor animates under.

**Frame selection is `floor((numframes - 1) * cycle)`**, and the remainder is the
interpolation fraction handed to both channel decoders. `FUN_10089b20` reads
`numframes`@12 of the descriptor, decrements it, multiplies by the cycle argument and
truncates. Clip duration therefore divides by `frames - 1`, not `frames`.

**A cell is addressed from the owner, not the target.** The captured descriptor pointers
satisfy `pointer - studiohdr == LocalSeqIndex@276 + index * 764` and
`LocalAnimIndex@268 + index * 72` on **961,518 of 961,518** contributions across the two
runs, with every index inside the owner's own `NumLocalSeq`/`NumLocalAnims`. Combined
with the loaded image being the file unrelocated (`docs/vtmb/mdl_v2531.md`), a captured
pointer minus the header base is a file offset.

**Blend cells fire per axis pair.** `FUN_10089740` resolves both axes through
`FUN_10089500`, then evaluates one, two or four cells according to `groupsize[1]` and
`groupsize[0]` (A.3). Across the two runs 3,440 and 3,434 sequence contributions are
multi-blend, every one of them a 9×1 grid — the shared-bank `move_yaw` locomotion
blends — and each fires exactly two cells from adjacent rows.

**The selected-bone mask reaches the cell unchanged.** `FUN_10089b20` gates each bone
on `mask[4 + (i >> 5) * 4] & (1 << (i & 31))` in the *owner's* bone index space, and the
owner is frequently a shared bank the dispatcher resolved to rather than the entity's
own model — so whether the mask survives that resolution is not a question the code
answers by inspection. Two complete `sp_theatre` runs record **231,747 of 231,747** and
**231,649 of 231,649** decoded-bone sets equal to the mask of their enclosing sequence,
with no cell decoding a bone its mask does not select, over 34 and 35 owner identities
of which 17 and 18 are banks no actor animates under. For this corpus the mask is
therefore the same object at both ends of the dispatch.

**That is a measurement, not a structural property.** `FUN_1008e370` **rebuilds** the mask in
the include model's own index space before handing it down, so the two ends agreeing is a fact
about the rigs this corpus fires rather than about the dispatcher. A model whose include
mapping permutes bone indices differently would break the equality without breaking the
engine. Read the counts above as coverage, and do not carry the claim to a rig this corpus
does not reach.

**A cell whose mask selects no bone still reads sixteen bytes.** `FUN_10089b20` reads
`NumBones`@240 and `BoneIndex`@244 from the header and `numframes`@12 and `animindex`@48
from the descriptor before it examines the mask, then walks the bones calling nothing.
Two runs record **10,311** such cells and a third records **10,353**, so the behaviour is
authored and the count is not: two runs agreeing exactly is what a narrow sample looks like,
and the third is 0.4% away. What reproduces is that the cell reads sixteen bytes and decodes
nothing, not how many times a given cutscene reaches one.

`FUN_100968a0`'s base-model path calls `FUN_10089c40`. Its include-model path
evaluates into temporary position/quaternion arrays, then consumes 0x3c-byte
mapping records containing source bone, target bone, a position-transform byte,
and a 3×4 matrix at record `+0x0c`. A clear byte copies the source position; a set
byte applies `TransformPoint(sourcePosition, mappingMatrix)` through
`FUN_10107f80`. The source quaternion is copied verbatim to the target in both
cases. Thus this outer virtual-model remap changes position frame when authored
but does not transform local rotation.

**The 56-byte group remap record, read from its consumer:**

| Off | Type | Field |
|---|---|---|
| +0x00 | short | source bone — **negative means write the including model's own bind pose** |
| +0x02 | byte | branch selector |
| +0x03 | byte | position transform — clear copies, set `TransformPoint`s |
| +0x04 | short | chain short A |
| +0x06 | short | chain short B |
| +0x08 | matrix3x4 | the mapping matrix |

This corrects two earlier readings. The `+0x04` dword recorded as loader-written is the two
chain shorts, and the matrix sits at `+0x08` rather than `+0x0c`.

**The array is walked only on the include-group path.** A sequence index below
`NumLocalSeq`@272 jumps to the evaluator and returns having read no group at all, so the
remap is reachable only after a group's four ownership tests pass. Which path an evaluation
took therefore decides whether the transform applies, and it is a property of the
contribution rather than of the model: one captured actor reads *transformed* under one
owner bank and *copied* under another, with the same remap array in both.

**One branch of the record would hang the engine.** The chain-rebase branch's second loop
never advances its induction variable and terminates only when its two shorts are equal, so
a record carrying unequal shorts loops forever. **0 of 3,095 records in the shipped corpus
enter it**, so the branch is unreachable on shipped content — a latent hang, not a live one,
and its semantics stay undecoded because nothing exercises them.

`FUN_10089c40` is where an owner is resolved. A sequence index below `NumLocalSeq`@272
takes the local path straight to `FUN_10089740`; otherwise it walks the include groups at
`NumIncludeModels`@404 / `IncludeModelIndex`@408 (116-byte stride) for the group whose
sequence range contains the index, remaps 24 pose-parameter slots through the `short`
array at group `+0x44`, recurses with that group's `studiohdr` and its owner-local index,
and then walks the group's bone remap array at group `+0x10` — 56 bytes per bone of the
including model — to map the result back. Afterwards it evaluates `numautolayers`@660
further sequences recursively, which is why one pose build can carry several sequence
contributions: two runs show 659 and a comparable count of layered generations, up to
three sequences deep. The group's runtime field layout is owned by
`docs/vtmb/mdl_v2531.md`.

The two callers place the transitioner in the live pose order:

```
FUN_10091650  base pose -> transitions -> autoplay -> virtual hook -> controllers
FUN_100979b0  base pose -> transitions -> weighted layers -> virtual hook -> controllers
```

`FUN_10091650` has 228 data references across client vtables, so this is a broadly
shared render-pose path rather than a courtroom-only or physics-only behavior. The
exact retail class/method names remain working aliases until RTTI/vtable ownership
is recovered.

`FUN_100919c0` is confirmed as `C_BaseAnimating::SetupBones` by its VProf string.
Its raw call sites establish the next two stages and their arguments:

```text
SetupBones
  -> vtable +0x208 (FUN_10091650)
       (selectedBones, positions, quaternions)
  -> vtable +0x1ec (FUN_1008fd00, C_BaseAnimating::BuildTransformations)
       (positions, quaternions, rootToWorld, selectedBones)
```

`BuildTransformations` performs ordinary parent-local concatenation, root/entity
composition, and the verified `Flags & 0x2` split-inheritance branch above. The
renderer continuation crosses two interfaces and three retail DLLs:

```text
client.dll C_BaseAnimating::InternalDrawModel (0x10092970)
  -> VEngineModel006 +0x08
engine.dll CModelRender::DrawModel (0x200a6640)
  -> TStudioRender012 +0x40: obtain CStudioRender bone buffer
  -> IClientRenderable +0x3c: SetupBones writes boneToWorld[]
  -> CModelRender::RenderModel (0x200a5f00)
     -> TStudioRender012 +0x58
StudioRender.dll CStudioRender::DrawModel (0x2c004f00)
  -> build skin palette (0x2c004e10)
  -> mesh branch and specialized vertex dispatch
```

#### Exactly two engine frames submit a studio draw

The chain above is one of them. The other is the shadow pass, entered from the
client shadow manager rather than from `C_BaseAnimating`:

```text
client.dll shadow manager (call site 0x100d7bc5)
  -> VEngineModel006 +0x44
engine.dll CModelRender::DrawModelShadow (0x200a6990)
  -> VEngineModel006 +0x34 -> TStudioRender012 +0x40: obtain bone buffer
  -> IClientRenderable +0x3c: SetupBones, fifth argument NULL
  -> TStudioRender012 +0x58            (0x200a6ce4, no RenderModel)
StudioRender.dll CStudioRender::DrawModel (0x2c004f00)
```

The two frames are siblings: `CModelRender::RenderModel` has one direct caller,
`0x200a6945` inside `CModelRender::DrawModel`, and no vtable slot holds it, so
the shadow frame cannot reach the studio draw through it.

**The set of routes is closed, not merely enumerated.** Only `engine.dll` and
`StudioRender.dll` contain the string `TStudioRender012`, so no other module
holds the interface. Inside `engine.dll` the pointer lives in one global,
`0x20d63ef0`, written only by its acquisition at `0x200a5a40` and cleared at
`0x200a5aa1`; following every one of the 72 reads of that global to the vtable
register it feeds yields slot `+0x58` at `0x200a6221` and `0x200a6ce4` and
nowhere else. A studio draw that neither frame encloses would therefore be a
missing hook rather than an unknown path.

The shadow frame builds its own pose through the same renderable slot `+0x3c`,
staging the bone buffer, boneMask `0x15e` and an LOD-shifted maxBones exactly as
the ordinary frame does. It differs in one argument: where
`CModelRender::DrawModel` passes the address of a stack local as the fifth
argument (`0x200a68a7`), the shadow frame passes `NULL` (`0x200a6c5b`). That
argument is therefore an optional caller-supplied pointer; what the callee writes
through it is still unknown.

`CModelRender::DrawModelShadow` is `__thiscall` with four callee-cleaned dword
arguments and no return value — both exits, `0x200a6e1f` and `0x200a6e52`, are
`RET 0x10` with `EAX` untouched.

Which of the two frames a given draw belongs to, and how the capture proves it,
are owned by `vtmb-animation-reverse-engineering.md` → "Grouping records by pose
build".

#### The client entity hierarchy is built and torn down through one pair

`C_BaseEntity::C_BaseEntity` is `0x1008f3c0` and `C_BaseEntity::~C_BaseEntity` is
`0x1008f5c0`. Both are `__thiscall` with no stack arguments; the constructor has
12 callers and the destructor 15, so every client entity passes through the pair
whether or not it is skeletal. The constructor's relocatable prologue is
`53 55 56 57 8b f1` and the destructor's is `53 56 57 8b f9`, each complete
instructions with no relative operand, and nothing branches into either. The
destructor leaves through a single exit at `0x1008f6e5` that restores `this` into
`ECX` and jumps to `0x1009ddc0` rather than returning.

Both touch five subobject vtables at `+0x0`, `+0x4`, `+0x8`, `+0xc` and `+0x10` —
the constructor installing `0x1022d6d4` and its four siblings, the destructor
restoring exactly those five. **The second subobject sits at `this+0x4`.** That is
the declared multiple-inheritance layout behind the render-info relation: the
interface the draw stream names, and the one `SetupBones` receives, is the second
base four bytes into the instance, and the constructors say so without any
capture.

**A vtable carrying the pose slots does not identify a shared construction path.**
`0x101e3cfc` holds `BuildTransformations` at `+0x1ec` and the standard pose
builder at `+0x208`, yet exactly one instruction references it as data and its
writer `0x10002d80` has exactly one caller. It belongs to one concrete class;
every other class derived from `C_BaseAnimating` carries its own vtable with the
same inherited slots. A capture hooked on `0x10002d80` records no construction at
all while skeletal actors are being posed.

#### A studio model's bytes live in a cache slot with no free to hook

A `model_t` holds its type at `+0x88` — `2` for brush, `3` for studio — its
reference flags at `+0x84`, and for a studio model a Quake `cache_user_t` at
`+0xb0`. **The `studiohdr` is `*(model + 0xb0)`**, and the model owns no other
copy of it.

`CModelLoader::GetExtraData` (`0x200bb6f0`) is the sole route from a model to its
header. For a studio model it calls `Cache_Check` (`0x20114780`) on the cache
user and, when that returns zero, calls `CModelLoader::LoadModel`
(`0x200b8960`) to re-cache before returning the word again. So a header address
stops being valid exactly when that one word is nulled, and an evicted model
comes back from a later `GetExtraData` **at a different address** rather than the
old one.

`CModelLoader::UnloadModel` frees nothing. It is `0x200b8e60`, the model-loader
interface's slot `+0x18` reached from `CEngineClient::UnloadModel`
(`0x2001a7b0`), and it is two instructions: `model->flags &= ~flags`. It drops a
reference.

The free itself is a store without a call boundary. `Cache_Free` (`0x201146f0`)
takes one cache user, unlinks it and nulls it — but the same body is **inlined**
into `Cache_Alloc` (`0x201147f0`), whose eviction loop nulls the LRU victim
whenever `Cache_TryAlloc` (`0x20114200`) fails, and into `Cache_Flush`
(`0x201143a0`), which walks the whole LRU list doing the same. Three allocator
variants (`0x201139c0`, `0x201134e0`, `0x201136e0`) carry the body too. Every
copy is identifiable by the `Cache_Free: not allocated` assert guarding it.

Those two inlined paths are the ones a map change takes, so hooking `Cache_Free`
would miss the case that matters. Observing an unload means bracketing
`Cache_Alloc` and `Cache_Flush` as well and re-reading the recorded headers
afterwards, and the result is still "these headers no longer read as their own"
rather than a per-model free event.

#### The render info a studio draw receives

`CModelRender::RenderModel` builds the struct `CStudioRender::DrawModel` takes as
its first argument, at `0x200a6191`–`0x200a61d5`, and passes it at `0x200a6210`:

| Offset | Contents |
|---|---|
| `+0x00` | the `studiohdr` |
| `+0x04` | the renderable plus `0xc0` |
| `+0x08` | 16-bit; `0xffff` unless a per-instance record supplies it |
| `+0x0a` | 16-bit; same default and same source |
| `+0x0c`, `+0x10`, `+0x14` | passed on to the mesh dispatch |
| `+0x18` | the entity, which is the instance plus four |
| `+0x1c` | taken from the caller's model record |

The two 16-bit fields default to `0xffff` and are otherwise loaded from a
24-byte-stride table indexed by a 16-bit id; the studio draw zero-extends both at
`0x2c005150` and `0x2c005110`. What they select is not established. The consumer
reads nothing past `+0x1c`, so 32 bytes is the whole struct as both sides use it.

The `TStudioRender012` object uses vtable `0x2c06c150`. Slot `+0x40`
(`0x2c004a20`) returns its `this+0x5c` buffer, which engine passes as the output
argument to client `SetupBones`. Before drawing, `0x2c004e10` loops all studio
bones and computes a separate palette at `this+0x60`:

```text
skinMatrix[i] = boneToWorld[i] * StudioBone[i].poseToBone
```

The second operand is read at `StudioBone + 0x58`, exactly `poseToBone@88` in the
160-byte v2531 bone. Matrix-concat routine `0x2c00eba0` proves the operand order;
there is no shader-layout transpose or reversed inverse bind hidden at this seam.

Both ordinary StudioRender mesh branches call `0x2c01aaf0`, which selects a
generated specialization and passes the `this+0x60` palette. The observed
specializations apply skinning on the CPU:

- `0x2c01aff0` selects one to four palette matrices by the vertex bone indices
  and forms their byte-weighted matrix blend;
- table entry `0x2c01e4e0` transforms position with the affine matrix, transforms
  normal with its 3×3 rotation, copies UV, and advances the dynamic-mesh streams;
- direct flex path `0x2c01cb60` performs the equivalent one-to-four influence
  weighted position/normal calculation;
- packed-vertex table entry `0x2c04cdb0` decodes its compact source fields and
  applies the supplied matrix before writing the same streams.

The specialization tables begin at `0x2c07f9c8`, `0x2c07fc08`, and
`0x2c07fc28`. Their complete selector-field meanings and whether every generated
entry is CPU-skinned remain open, but the observed main/flex/packed paths all
consume the proven palette and submit already-transformed dynamic vertices.
`0x2c0054f0`, initially found as another `this+0x60` reader, is only the
`CStudioRender` destructor and is retained as an eliminated lead in the tracked
specification.

### Live rendered retail invariant

A hash-gated, read-only probe (`research/tooling/capture/capture_live_pose.py`) polls the two
`CStudioRender` buffers with `ReadProcessMemory`; it does not attach a debugger,
suspend a thread, or write into the game process. The probe refuses an
unrecognized `StudioRender.dll`; the validated binary SHA-256 is
`13d56ce90de2c5faedc0df26d36b24e90f30eded5055625f616cd97e301e124b`.
It derives the requested target's checksum and bone count from the merged,
patch-first install. The first validated target was
`models/character/pc/male/tremere/armor0/tremere_Male_Armor_0.mdl`,
checksum `0x40e9c200`, with 80 bones. Each captured frame contains both 80-entry
row-major 3×4 arrays: `boneToWorld` and the final skin palette. Longer scene
passes may accept a partial result because `CStudioRender` exposes only the
actor currently being drawn; an off-camera actor is not evidence of an
unchanged pose.

The reproducible protocol reloads `sp_tutorial_1` to clear the forced player
sequence, uses third person, and fixes simulation/render timing:

```text
sv_cheats 1
thirdperson
fps_max 30
host_timescale 1
host_framerate 0.033333333
pausable 0
```

For unattended `+exec` playback from a loaded save, the player controller must
be settled before selection. The working stimulus keeps simulation at normal
time, waits for the map and third-person player model, pulses `+forward` for one
command frame, releases it, allows 45 fixed wait frames for locomotion to settle,
and then runs `player_sequence howl`. This resets the player's idle timer without
carrying the movement transition into the selected sequence. A recipe that
freezes the world and restores `host_timescale 1` adjacent to selection, or fires
selection immediately after the movement pulse, is not a clean capture stimulus:
normal idle/locomotion can visibly contaminate the forced clip.

The unattended recipe arms the recorder when the target is first rendered and
keeps a bounded pre-roll instead of guessing how long save loading takes. It
issues `player_sequence howl` twice: issuing the same command again restarts the
forced clip, so offline alignment can reject the controller transition around
the first selection and select one uninterrupted pass. A completed
`player_sequence` remains the selected forced player sequence; `unpause` does
not clear it, while reloading the map does. The fixed `host_framerate` value is
seconds per frame on this build, not frames per second.

The accepted unattended seed run recorded all 238 requested palettes with no
drops or incomplete records, exited retail naturally, and left no process. Its
longest clean live-to-authored span is 69 consecutive pairs covering authored
`howl` frames 9–77. Patch-first resolution identifies `howl` in
`models/character/shared/male/misc.mdl` as an 81-frame, 30-FPS, non-looping
clip. `research/tooling/capture/validate_live_pose_capture.py` compares the live
buffers to the source MDLs, and
`research/tooling/capture/compare_pose_captures.py` compares two sessions after
removing the entity/root transform.

The live data independently verifies both load-bearing matrix rules:

- Across all 81 frames,
  `skinPalette[i] = boneToWorld[i] * poseToBone[i]` has median frame RMS
  `4.0481e-5`; the worst matrix-element error is `2.44073e-4`.
- After the entry transition, live frames 8–77 align monotonically with authored
  frames 9–78 over the 53 name-shared target/donor bones. Applying the retail
  split-inheritance hierarchy gives median frame RMS `7.92464e-4` and maximum
  `0.0176376`. A conventional hierarchy gives median `7.48408` and maximum
  `8.55726`. On `Bip01 Spine1` itself, the split rule gives median `1.51184e-4`
  against `0.681111` for conventional inheritance. This is a rendered
  discrimination of the `Flags & 0x2` branch, not only a decompile inference.

Raw palette hashes are deliberately not the acceptance metric: map reloads may
place the player under a different entity transform, and the first/last live
frames include transition history. Root-relative matrices and authored-frame
alignment isolate the skeletal evaluation. The 27 target-only helper/attachment
bones also expose a remaining contract: propagating their target bind locals
under the donor-driven parents gives median frame RMS `0.143857`, so simple
target-bind fallback does not reproduce their live result. The capture proves
the mismatch but does not yet attribute it among the outer virtual-model map,
controllers, or another post-base-pose stage.

The polling probe is intentionally target-specific. A companion native probe
captures the whole visible scene instead:

```powershell
uv run elysium research build_live_pose_capture
uv run elysium research capture_live_scene start --duration 0
```

`capture_live_scene.py` injects a 32-bit, hash-gated hook into the user's retail
process and replaces only `CStudioRender::DrawModel`'s vtable slot for the
capture interval. The hook calls the original function, then queues the studio
header, client entity, checksum, model name, `boneToWorld`, and final skin
palette for every draw. Stop restores the slot before unloading. The `ELPOSE2`
reader rejects an incomplete record tail and reports queued, written, and
dropped counts. This is not the polling probe's read-only process contract; it
is an explicit temporary mutation of the running retail renderer.

One complete `sp_theatre` retry trace contains **229,201 draw records**, **69
models**, and **915,533,348 bytes** over **215.2831204 seconds**, with zero
dropped records and zero incomplete tail bytes. The trace SHA-256 is
`db35b31fcad43bce5854bea6d21290a034810b68e965b16fc56775c8a4213c8d`.
It includes 11,657 Lacroix draws, 7,750 Malkavian-male draws, 6,382 Ash draws,
5,633 Vampire4/Ventrue-female draws, 4,432 Jack draws, 3,593 Nines draws, and
the courtroom stake, sword, and cigar. Render capture is necessarily
visibility-gated; absence while culled is not an unchanged pose.

`archive_courtroom_poses.py` closes that coverage gap on the authored side. Its
seven NPZs retain **32,907 frame samples** (4,701 frames for each scene),
**1,955 cinematic bones**, and all local positions, quaternions, binds,
parents, flags, and inverse binds. The manifest binds all 23 VCD actor slots
to their source `BipNN` root and target model. For the broken seat,
`Vampire4/Bip01` resolves to
`ventrue_female_Armor_1.mdl`; extracting consecutive changes yields 1,261 live
poses over 132.105408 seconds.

The whole-scene join rejects direct cinematic name-fold/copy as a complete
retail model. A second hook at `FUN_100968a0` records the resolver's normalized
phase, BASE and final local arrays, selected-bone bitset, and pose-buffer
identity. Its clean Vampire4 interval contains **4,684 resolver records**
(2,342 BASE + 2,342 final), all correctly paired, with zero capture drops. The
trace SHA-256 is
`a9fb0ba9fa66df45a712bdddeb882cfba73616eea509fd0d1111218695a2c293`.
The two selected masks cover the ordinary body and the detail/finger pass. Every
paired final pose equals BASE exactly, so no transition, gesture, controller,
or later local-pose layer changes the seated hold after external base
resolution.

Joining those exact BASE arrays to the held actor locals and the authored
`Courtroom_bip5/Bip01` archive makes the following candidate nearly exact:

```text
livePosition = heldActorPosition + authoredPosition - cinematicBindPosition
liveRotation = heldActorRotation * inverse(cinematicBindRotation) * authoredRotation
```

Across every selected ordinary non-root bone, the position RMS is `2.54121e-6`
Source inches and the rotation matrix RMS is `1.91195e-5`. This does **not**
close the composition law. The captured interval is a bind-like static hold:
authored local approximately equals cinematic bind, so subtraction cancels and
all three plausible non-commuting rotation orders collapse to the held pose.
The small residual therefore proves the join and the static cancellation case,
not the moving rotation order or a general donor-delta rule.

A runtime experiment that promoted this candidate to the cinematic-bank
contract keyed both channels for every donor bone and composed
held-entry/authored/donor-bind locals for every actor. Live acceptance of the
full courtroom scene failed: almost all NPCs disappeared, floated, or bent.
That result rejects the generalized implementation. It was removed; cinematic
banks remain sparse raw channels and the runtime remains on the preceding
absolute playback path. No donor-bind manifest contract is accepted as a
retail fact.

The next decisive sample must be a phase-pinned **moving** resolver interval.
It must record at least one actor whose selected non-root bones have a
non-identity authored delta, then compare the candidate multiplication orders
against the resolver BASE locals. `Flags & 0x2` split inheritance remains a
separate later-stage requirement.

The whole-scene draw capture is still useful as an identity/final-output
archive. It contains 229,201 records over 215.283 seconds with zero drops:
24 rendered character model instances with more than one draw, plus the
animated `Cin_Stake`, `Cin_Sheriff_Sword`, and `Cin_Cigar` props. The seven
courtroom VCDs deterministically assign 23 actor slots to
`Courtroom_bip1`...`Courtroom_bip7`, a `BipNN` source root, and the
`entire_scene` sequence. Model-path matching joins 17 of those slots to 16
distinct rendered model records. It is not a complete actor join: the same
Malkavian male model is authored for two slots, the player model is selected at
runtime, several expected variants were not drawn, and the render hook records
neither targetname nor entity index. Consequently the draw dump alone
deterministically identifies final matrices by client pointer/model checksum,
but not every VCD actor name or source sequence.

**The entity identity that closes it is the client entity's own handle.**
`C_BaseEntity` stores an `EHANDLE` at `+0xe4`, and two independent accessors on its
primary vtable `0x1022d6d4` name that displacement: slot `+0` is `0x1009f050`,
`MOV [ECX+0xe4],EDX`, and slot `+4` is `0x1009f060`, `LEA EAX,[ECX+0xe4]; RET`. The
handle packs the entity index in its low **13** bits and a serial above them, with all
bits set meaning no entity — the same encoding the server uses at `CBaseEntity+0x448`, so
one index space spans the two modules. A capture that records the handle beside the
address therefore joins a drawn actor to the scene that asked for its animation, bounded
by the serial and by the interval the address was one actor.

The constructor at `FUN_1008f3c0` installs five vtables — the primary at `0x1022d6d4` and
subobject vtables at `+4`, `+8`, `+0xc` and `+0x10`. The one at `+4` is the renderable the
draw stream records, which is where this document's fixed four-byte offset between the two
comes from.

The phase/mask/buffer-identity join is reproducible with:

```powershell
uv run elysium research analyze_live_animation_stages `
  $ELYSIUM_EXPORT_ROOT/_live_pose/<resolver-session> `
  --archive $ELYSIUM_EXPORT_ROOT/_live_pose/courtroom_authored/courtroom_bip5.npz `
  --held-pose $ELYSIUM_EXPORT_ROOT/_live_pose/<whole-scene-session>/vampire4_pose_changes `
  --report $ELYSIUM_EXPORT_ROOT/_live_pose/<resolver-session>/vampire4_animation_stages.json
```

Re-run the source/capture check with:

```powershell
uv run elysium research validate_live_pose_capture `
  $ELYSIUM_EXPORT_ROOT/_live_pose/<session> `
  --report $ELYSIUM_EXPORT_ROOT/_live_pose/live_pose_validation.json
```

The end-to-end trace is complete only when Ghidra evidence identifies and verifies
all of these seams:

1. ~~studio header/sequence/animation record selection;~~
2. RLE local position and quaternion evaluation;
3. the nested virtual-model remap branch inside `FUN_10089c40` — the outer remap,
   sequence blends, and autolayer recursion are closed;
4. ~~the complete `Flags & 0x2` post-decode path and its coordinate frame;~~
5. ~~parent-local hierarchy concatenation into model-space bone matrices;~~
6. ~~`Bip01` root composition with entity origin/angles and cinematic placement;~~
7. ~~`poseToBone` use in CPU/GPU skinning and final render submission.~~

The independent source/output audit is implemented by the internal
`elysium_pipeline.validation.validate_skeletal_pipeline` library module. It is not a
public project-tooling entrypoint.

It reads the patch-first retail files rather than trusting exporter output, then
compares generated node binds, inverse binds, skin joints/weights, timelines, and
every emitted animation sample. A structurally valid/loadable GLB is not proof of
retail semantics: the report separately inventories procedural bones, blend
grids, movement, events, split inheritance, and included-model bind fallback
that the current exporter/runtime does not apply.

Each newly confirmed format or behavior fact is corrected into this document in
the same pass. The context specification keeps unresolved addresses and working
aliases; it does not promote a hypothesis into a VtMB fact.

## A.5 Skinning [data-verified + VtMB decompiled]

All character models are `VertexListType==0` (SKINNED, 44B `StudioVertex`); the
per-vertex `BoneWeight` (layout: `docs/vtmb/mdl_v2531.md`) *is* the skin. **`NumBones` reads 0
on VtMB data — derive the influence count from nonzero weights** (probe: jeanette
6389/6393 verts have `NumBones==0`). Max **3 influences**, well within a standard
4-weight skin:

```
for i in 0..2:
    if Weight[i] > 0:  influence(bone=Bone[i], weight=Weight[i]/255.0)
```

`Bone[i]` indexes the same `StudioBone` array. Retail application proves that
`poseToBone`@88 is the inverse-bind matrix: StudioRender computes
`boneToWorld * poseToBone`, then selects those skin matrices by `Bone[i]` and
blends them using the stored byte weights. jeanette: 4132 verts 1-bone, 1761
2-bone, 500 3-bone; weights sum to 255.

**The palette relation is measured, not only decompiled.** Multiplying a draw's
own captured bone-to-world by the stored `poseToBone` returns the skin palette the
same draw recorded on **397,796 of 397,796** draws across 141 models of one theatre
cutscene — worst translation **3.46e-4** source units and worst rotation
**2.96e-06°**, with no bone outside the excellent band of
`docs/vtmb/vtmb-animation-reverse-engineering.md` §11.3 on either metric. The
same corpus establishes that the stored bind is the ordinary one: `poseToBone`
equals the conventional hierarchy-FK inverse on **all 5,517** captured bones, none
over the band, which is §A.2's whole-install finding reached over the models a run
actually drew.

A patch-first whole-character decode validates **1,621,270 weighted vertices /
2,116,520 triangles**. Every non-zero influence names a source bone, normalized
weights sum to one, and no `.mdl`/`.dx80.vtx` skin decode fails.

## A.6 Attachments & hitboxes [data-verified]

**`StudioAttachment` (60B):** `NameIndex`@0, `type/flags`@4, `bone`@8,
`matrix3x4 local`@12 — where to parent weapons/props/muzzle effects. jeanette:
`mouth`@bone12, `eyes`@bone12, weapon-mount@bone47 (paired with the `tire iron`
bone). **Hitboxes:** set (12B: `NameIndex`, `NumHitBoxes`, `HitBoxIndex`) →
box (32B: `Bone`, `Group`, `bbmin`, `bbmax`) — combat damage zones (jeanette: 1
"default" set, 19 boxes). Both are trimmed from modern Source's 92B/68B forms.

## A.7 The shared animation library [data-verified]

**Per-NPC models carry only their own clips (mostly dialogue); locomotion, combat,
and idle come from a shared library pulled in by the studiohdr include-model
mechanism — and the includes form a recursive tree** [data-verified].
`jeanette.mdl` (22 own dialogue seqs) → includes
`shared/female/npc_allsequences.mdl`, which is a **0-anim/0-seq aggregator** that
itself includes the leaf banks `shared/female/{stances,conversations,allsequences}.mdl`;
those leaves hold the real animation banks (`male/move_and_ranged.mdl` = 722 anims /
602 seqs). So the decoder **resolves include-models transitively** — an NPC's playable set
is *own anims ∪ (recursively) all included banks*, keyed by bone name against a shared Biped
skeleton.

**On-disk** [data-verified]: `NumIncludeModels`@404 / `IncludeModelIndex`@408 →
`StudioModelGroup[]` (layout: `docs/vtmb/mdl_v2531.md`). The tree is a DAG — `frenzy`/`pc_idles` reappear
via several parents — so resolution dedups by path. The shared Biped deformation
core is addressed by bone name. A bank can also animate optional bones absent
from a particular target — weapon/attachment bones, anatomy helpers, and toes on
a few reduced skeletons. Those tracks have no target bone or weighted vertices
and are skipped. This is distinct from a broken skin: every non-zero vertex
influence must still name a valid joint in the target GLB, and every joint must
be reachable from the skin root.
`m_iszPlay` and friends name a **sequence label** (`StudioSeqDesc.szlabel`@0 → `anim[0][0]`@56 →
local anim); the pipeline keys clips by that label.

**Pipeline** (`elysium_pipeline.formats.mdl_skel.resolve_tree`/`local_sequences`, `elysium_pipeline.formats.mdl_gltf.export_npc`/
`export_bank`, `pipeline/src/elysium_pipeline/exporters/npc_export.py`; roadmap PL4): rather than merge every bank into each NPC
(a ~94 MB / ~90k-accessor monolith × the cast ≈ 4.2 GB), each model's own clips bake **once** —
the NPC's dialogue into `$ELYSIUM_EXPORT_ROOT/npc/<npc>.glb`, each shared bank into `$ELYSIUM_EXPORT_ROOT/npc/banks/<bank>.glb`
(skeleton + clips, no mesh) — and `npc_manifest.json` records the per-NPC `clip → owning-stem`
resolution plus the target mesh's diagnostic `split_bones` inventory. The runtime loads a bank
glb once and applies its clips to any NPC skeletal mesh by bone name (glTFRuntime
`LoadSkeletalAnimation(mesh, …)`), which is VtMB's own virtualmodel bank-sharing.

The patch-first grid contains four explicit source exceptions. The Night Watchman in
`sm_junkyard_1` references
`models/character/npc/doppleganger/doppleganger.mdl`, which is absent even though the
`common/doppleganger` male and female variants exist. Three animated props —
`bottleb.mdl`, `bottlec.mdl`, and `stage_light.mdl` — carry truncated skeletal mesh
records. Their per-map static `model_mesh` geometry decodes successfully, so they remain
visible through the static path while skeletal animation is unavailable. The exporter
downgrades only these exact paths to structured warnings in `npc_manifest.json` and
`npc_index.json`; every other missing model or decode failure remains fatal.

Retail first evaluates the included model's complete pose. Its outer mapping then
copies the donor quaternion verbatim and either copies its position or transforms
that position by the authored 3×4 mapping matrix. A channel absent from the donor
animation therefore falls back to the **donor bind**, not the target bind.

The runtime instead binds the bank's sparse local tracks to the target reference
skeleton by **bone name** and deliberately leaves glTFRuntime's generic
rest-pose retargeter disabled. Source-node tracks with no target bone are removed
before animation construction. Across **4,515** ordinary target/bank pairs,
**3,158** have at least one donor/target bind difference, **154** differ in parent
topology, and **2,692** actually use a source clip whose absent channel selects a
different donor versus target bind fallback. This is a live virtual-model
fidelity gap. Generic proportion retargeting is still not the answer: a faithful
resolver must evaluate donor fallback and apply the recovered per-record outer
position mapping, while the nested remap record semantics remain open. A
whole-cast cinematic bank also encodes authored stage placement in its `Bip01`
root, so applying an unrelated rest-frame transform can double that placement.

`mdl_gltf.py` writes Source position `(x,y,z)` as right-handed Y-up glTF
`(x,z,-y) * 0.0254`. glTFRuntime's default basis and `SceneScale=100` map that to
skeletal-component position `(y,x,z) * 2.54` centimetres. The component's fixed
**−90° yaw** then maps it to world `(x,-y,z) * 2.54`, exactly this project's
Source→Unreal position convention. Quaternion conversion at both steps is basis
conjugation, so it preserves multiplication order; the component yaw is outside
the local pose. Ordinary NPCs, scene understudies, controller NPCs, and the
pawn-attached player body use that same basis. NPC and player-material skeletal
permutations keep separate mesh, asset, and clip cache identities; rebuilding the
player body cannot replace an ordinary NPC's materials.

## A.8 Deviations from modern Source (v44–49) [ref/SDK]

| Area | Modern Source | VtMB v2531 |
|---|---|---|
| `mstudiobone_t` | 216B; `pos,quat,rot(euler),posscale,rotscale,qAlignment,unused[8]` | **160B**; no euler `rot`, no `qAlignment`; `rotscale` is a **quaternion** |
| Anim storage | `mstudioanim_t` linked list, variable size, per-bone `flags`+`nextoffset` | **fixed 32B record/bone**: `weight` + `offset[7]` |
| Anim channels | 2 valueptrs (pos+rot), rot = 3 euler DOF | **7 flat offsets**: posXYZ + **rot XYZW** |
| Constant compression | `Quat48/64`, `Vector48` + `RAW*`/`DELTA` flags | **none** — every channel is a `{valid,total}` RLE track |
| `mstudioanimdesc_t` | + animblock/section/zeroframe streaming | **72B**, none of it |
| seqdesc blends | variable `blend[]` | fixed `short anim[16][16]` (512B) |
| attachment / hitbox | 92B / 68B | **60B / 32B** |
| skin `NumBones` byte | reliable | **unused (0)** — derive from weights |
| `mstudioflex_t` / vertanim | 60B / 16B `short delta[3]` + `short ndelta[3]` | **32B / 8B**, and the 8B record stores *directions* into a renderer-side unit-vector table (`docs/vtmb/facial_animation.md`) |
| `mstudioeyeball_t` | 168B, authored on every speaking character | **140B, and never authored** — `NumEyeballs` is 0 on all 4,444 models |

---

# Part B — Brush-entity movers

Moving brush models (doors, buttons, spinners, elevators, trams), driven by the
Source `CBaseToggle` lineage compiled into `vampire.dll` and wired through the I/O
bus. All are authored at `(0,0,0)`; the entity `origin` is the spawn translation and
— for rotating doors — the **hinge** (`docs/vtmb/mdl_v2531.md`/`docs/vtmb/entity_io.md`).

## B.1 Inventory [data — exact counts, 108 maps]

| classname | count | maps | role |
|---|---:|---:|---|
| `func_door_rotating` | **1236** | 102 | swinging door (the workhorse) |
| `func_door` | 214 | 40 | sliding door / drawer / cabinet |
| `func_button` | 162 | 41 | pressable switch |
| `func_rotating` | 119 | 51 | continuous spinner (fans, gears) |
| `func_movelinear` | 33 | 12 | linear piston / platform |
| `mover_keyframe` | 122 | 5 | path node (`NextKey` linked-list) |
| `func_keyframed_mover` | 55 | 5 | brush riding a keyframe path (trams, cars) |
| `func_elevator` | 15 | 11 | multi-floor lift (`GotoFloor`) |
| `func_physbox` / `func_pushable` | 179 / 11 | | vphysics-driven (not scripted lerp) |

**VtMB replaces Source's train/platform classes with the keyframe-mover family.**
`func_tracktrain`, `func_train`, `path_track`, `path_corner`, `func_plat*`,
`momentary_rot_button`, `momentary_door`, `func_conveyor`, `func_water`,
`func_pendulum` are **compiled in `vampire.dll` (RTTI recovers the classes) but
instantiated in zero maps** — skip them until a map needs one.

## B.2 Keyvalue schema [data — real observed values]

- **`func_door_rotating`**: `speed` (deg/s, mode 100), `distance` (arc in **degrees**,
  mode 90/20), `wait` (autoclose delay; **`-1` = stay open**), `angles` (rotation
  axis, default yaw/Z), `spawnflags` (256 = Use-Opens dominates), `soundgroup`,
  `use_icon 10`/`locked_icon 3`, `linked_door` (the paired leaf).
- **`func_door`**: `speed` (in/s), `lip` (inches subtracted from travel), `angles`
  (**slide direction**), `wait`. Many are `soundgroup metal_file_cabinet` drawers.
- **`func_button`**: `speed` (press-in in/s), `wait` (`-1` = latch), `spawnflags`
  (1056 = Use+Toggle), `locked_sound`/`unlocked_sound` (explicit WAVs),
  `use_icon`/`locked_icon` (19 distinct icons). **47% spawn `StartHidden 1`**
  (armed later by `ScriptUnhide`).
- **`func_rotating`**: `maxspeed` (deg/s), `fanfriction` (decel %), `volume`,
  `dmg` (crush), `angles` (spin axis), `spawnflags` (513 = Start-ON + large sound).
- **`func_movelinear`**: `speed`, `movedistance` (inches), `startposition` (0..1),
  `angles` (direction); mostly `spawnflags 8` = non-solid (laser tracks).
- **`func_elevator`**: `numfloors` + `floor1..8` (absolute Z), `speed`,
  `startsound`/`stopsound`. **`func_keyframed_mover`**: `speed`, `start_key`
  (first `mover_keyframe` targetname), riding a `NextKey` linked-list of nodes.

**VtMB schema deviations** [data]: **`noise1/noise2/noise_moving/noise_arrived` do
not exist** (0 across all maps) — VtMB uses a named **`soundgroup`** token resolved
through the audio layer (`docs/vtmb/audio_pipeline.md`), plus explicit `locked_sound`/
`unlocked_sound` on buttons. **`movedir` does not exist** — direction is `angles`.
`func_movelinear` uses `movedistance`/`startposition`, not Source's `movedir`.
VtMB-added: `use_icon`/`locked_icon`, `StartHidden`, `soundgroup`, `linked_door`
(485 uses), `use_override` (26 — routes `+use` to a separate button), `climbable`
(1090).

## B.3 The I/O surface [data — wire counts]

**Inputs**: `Open`/`Close`/`Toggle`/`Lock`/`Unlock` (doors); `SetSpeed`/`Start`/
`Stop`/`Reverse` (`func_rotating`); `StartMoving`/`StopMoving` (`func_movelinear`);
**`GotoFloor`** (66, `func_elevator`); **`MoveForwards`/`MoveBackwards`** (55,
`func_keyframed_mover`); plus base `ScriptHide`/`ScriptUnhide`/`Kill`.

**Outputs**: `OnOpen`/`OnClose`/`OnFullyOpen`/`OnFullyClosed` (doors — 833 on
`func_door_rotating` alone), `OnPressed` + **`OnIn`/`OnOut`** (buttons — the latter
arm the `+use` reticle as the look-cursor enters/leaves; not a stock Source output),
`OnLockedUse`, `OnBlockedClosing`, `OnReachFloorAny`/`OnReachFloorN`,
`OnReached`/`OnReachedKeyframe`. **~96 mover outputs carry a field-6 Python payload**
(`docs/vtmb/entity_io.md`), e.g. `"OnPressed" ",,,0,-1,FindPlayer().ClearActiveDisciplines(),"`.

## B.4 The movement logic [VtMB — decompiled]

`vampire.dll` is the Source `CBaseToggle`/`CBaseDoor`/`CBaseButton`/`CFuncRotating`
lineage (RTTI-confirmed: `CBaseDoor→CRotDoor`, `CBaseButton→CRotButton`,
`CFuncRotating`, `CMomentaryRotButton`).

- **Lerp core — `CBaseToggle`**: `LinearMove(dest,speed)`/`AngularMove(destAngle,
  speed)` set a **constant velocity** toward the target, schedule `m_flMoveDoneTime`,
  and snap + fire `MoveDone` on arrival — **no easing**. Fields `m_flMoveDoneTime`
  @`0x10554988`, `m_OnLinearMoveDone`@`0x1059bf70`, `m_OnAngularMoveDone`@`0x1059c42c`.
- **Door cycle — `CBaseDoor`** (`Spawn` `FUN_100ef260`): 4-state machine
  `m_toggle_state` `{AT_TOP=0, AT_BOTTOM=1, GOING_UP=2, GOING_DOWN=3}` (dword
  @`0x4f8`). `CBaseDoor::Use` (`FUN_100efc90`) reverses in-flight motion, or for a
  resting door runs a locked check (byte @`0x55C`) then `DoorGoUp`; the
  `NO_AUTO_RETURN` bit (`m_spawnflags & 0x20`, which the decompiler renders as
  `byte[this+0x81]` since `0x81×4 = 0x204`) both keeps the door open and permits
  re-use mid-motion. Player `+use` opening is gated by the `PUSE` bit `0x100`. Cycle:
  **closed → GoUp (Linear/AngularMove) → HitTop → wait(`wait`s) → GoDown → HitBottom**;
  `wait -1` disables autoclose. The **locked path** plays the locked/unlocked sound
  and fires `OnLockedUse`. Blocked-while-closing deals `dmg`, reverses, and fires
  `OnBlockedClosing` (`CBaseDoor::Blocked` `FUN_100f14a0`).
  `CBaseDoor::Use` checks `use_override` first: a resolved override receives its normal
  `Use(activator)` with the door as caller exactly once, then the door returns without
  toggling. A missing/unusable target therefore fails closed. PASSABLE (`0x8`) changes
  solidity, not usability: player/physics collision is disabled while use/debug traces
  can still address the door.
- **Button — `CBaseButton`**: press-in (`speed`, `lip`-adjusted) → `TriggerAndWait`
  (fire `OnPressed`, hold `wait`s) → `ButtonReturn`/`ButtonBackHome` spring-back, or
  latch when `wait -1`.
- **Spinner — `CFuncRotating`**: continuous rotation (not a fixed arc);
  `SpinUp`/`SpinDown` accel/decel toward `maxspeed` gated by `fanfriction`;
  `HurtTouch` applies `dmg`; driven by `Start`/`Stop`/`SetSpeed`/`Reverse`.
- **Keyframe mover**: interpolate along the `mover_keyframe` `NextKey` linked-list at
  `speed`, firing `OnReached`/`OnReachedKeyframe` per node. Elevator: move to the
  requested `floorN` Z at `speed`, `OnReachFloorAny`/`OnReachFloorN` on arrival.

### B.4.1 `func_elevator` [VtMB — decompiled]

`CFuncElevator` is factory `0x1020e740`; its 37-row datamap is `0x105aadf0`
(builder `0x1020e8d0`). The absolute Source-Z floor table is `+0x608`, `numfloors`
`+0x62c`, current floor `+0x630`, target floor `+0x634`, original position `+0x800`,
and lock byte `+0x80c`.

- `GotoFloor` (`0x1020f3f0`) converts its one-based input to an index. A locked
  elevator and an elevator whose current floor is `-1` (moving) ignore the request,
  so there is no mid-move retarget.
- A request for the current floor completes **synchronously** without
  `OnMoveStart`: it fires `OnReachFloorAny`, then `OnReachFloorN`, then writes the
  current-floor field. The tutorial's initial floor-one request relies on these
  outputs; no invented deferred completion is needed.
- A valid new floor moves vertically from `(original X, original Y)` to the
  absolute `floorN` Z at constant `speed`, sets target/current to moving state,
  starts the authored sound, then fires `OnMoveStart`.
- Crossing an intermediate floor fires `OnPassFloorAny` then its
  `OnPassFloor2..7`; arrival (`0x1020f6c0`/`0x1020f700`) stops the sound, fires
  `OnReachFloorAny` then `OnReachFloorN`, and only then records the resting floor.
- `Lock`, `Unlock`, `SnapToFloor` and `CallCurrentFloorOutputs` are dedicated
  inputs. Persistence resolves an in-progress move at its requested destination
  in a resting state, matching the mover save policy.

## B.5 Spawnflag bits [VtMB — decompiled, per-bit confirmed]

Each mover class's `Spawn`/handlers were decompiled and every `m_spawnflags & <mask>`
test read out; **every spawnflag value observed in the maps reconciles exactly**.
Bits are VtMB's own (they happen to match early-Source values). Only two bits are
*not* tested in these entities' own code — door `0x10` and rotating `0x10` — because
they are consumed elsewhere (NPC door-AI / the spin accel-decel ramp); those stay
`[inferred]`.

**`func_door` / `func_door_rotating`** (`CBaseDoor::Spawn` `FUN_100ef260`,
`CRotDoor::Spawn` `FUN_100f1c60`):

| bit | name | effect |
|---|---|---|
| `0x1` | START_OPEN | spawn at open position (`AT_TOP`) |
| `0x2` | REVERSE | negate movedir (rotating) |
| `0x8` | PASSABLE | non-solid |
| `0x10` | ONEWAY | NPC-nav one-way *(inferred — not tested here)* |
| `0x20` | NO_AUTO_RETURN | stay open; allow re-use mid-motion |
| `0x100` | PUSE | **player `+use` opens** (the dominant door bit) |
| `0x200` | NONPCS | blocks NPC activators (returns "locked") |
| `0x400` | PTOUCH | touch opens (obsolete; cleared when `0x100` is set) |
| `0x800` | LOCKED | starts locked (byte @`0x55C`) |
| `0x1000` | SILENT | suppress move sounds |
| `0x2000` | USE_CLOSES | select auto-close think (CloseWhenUnblocked) |

**`func_button`** (`CBaseButton::Spawn` `FUN_100c8d60`):

| bit | name | effect |
|---|---|---|
| `0x1` | DONTMOVE | button doesn't physically move |
| `0x20` | TOGGLE | stays pressed until re-used |
| `0x40` | SPARK | repeating spark think |
| `0x100` | TOUCH_ACTIVATES | arms touch |
| `0x200` | DAMAGE_ACTIVATES | shootable |
| `0x400` | USE_ACTIVATES | arms `+use` |
| `0x800` | LOCKED | starts disabled (byte @`0x5C4`) |
| `0x2000` | **SILENT** | **suppress activation/return sound — the one non-stock bit** |

(`CRotButton::Spawn` `FUN_100c9a20` reassigns the low bits: `0x1`=non-solid,
`0x2`=reverse, `0x100`=touch; `+use` is always armed.)

**`func_rotating`** (`CFuncRotating::Spawn` `FUN_100bfaa0`): `0x1` START_ON,
`0x2` REVERSE, `0x4` Z-axis, `0x8` X-axis (else Y), `0x10` ACC/DEC *(inferred)*,
`0x20` HURT (crush-touch), `0x40` NOT_SOLID, `0x80`/`0x100`/`0x200` = small/medium/
large sound radius. **`func_movelinear`** (`CFuncMoveLinear::Spawn` `FUN_10116030`):
only `0x8` = NOT_SOLID is tested.

---

# Provenance

Skeletal format [ref, data-verified]: VAMPTools `MDLConverter/inc/external/studio.h`
+ `src/{Animation,Skeleton}.cpp`; Crowbar `Core/GameModel/SourceModel2531/*2531.vb`
(definitive byte order, cites the leaked Bloodlines SDK studio.h). Probe models:
`character/npc/unique/santa_monica/jeanette/jeanette.mdl` (71 bones, 22 anims/501
frames, 3 attachments, 19 hitboxes, includes `shared/female/npc_allsequences.mdl`),
`character/npc/common/skeleton/skeleton_male.mdl`, `character/shared/male/
move_and_ranged.mdl` (602 seq). Mover logic [VtMB]: `vampire.dll` Spawn functions
`CBaseDoor` `FUN_100ef260` / `CRotDoor` `FUN_100f1c60` / `CBaseButton` `FUN_100c8d60`
/ `CRotButton` `FUN_100c9a20` / `CFuncRotating` `FUN_100bfaa0` / `CFuncMoveLinear`
`FUN_10116030`; `CBaseDoor::Use` `FUN_100efc90`, `CBaseDoor::Blocked` `FUN_100f14a0`;
`m_spawnflags` dword @entity+`0x204`, `m_toggle_state` @`0x4f8`, `m_flMoveDoneTime`
@`0x10554988`; RTTI class recovery for the full lineage.
