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
— flex/morph data, the flex-controller and flex-rule layers, the eyeball records and the eye
system they feed, and the `.lip` phoneme files — is `docs/vtmb/facial_animation.md`.

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
| 16/20 | int | `nummovements` / `movementindex` | **Authored movement — present and load-bearing for locomotion.** 0 on dialogue clips, but **66 of `move_and_ranged`'s 722 animdescs carry it**: `walk` 23 records, `run` 9, `sneak` 1, and every weapon walk/run variant. The records are decoded as metadata while the bone clip remains in place, so the host motor applies actor translation exactly once |
| 24 | Vector | `bbmin` (3f) | per-anim bbox |
| 36 | Vector | `bbmax` (3f) | |
| 48 | int | **`animindex`** | → per-bone anim records, rel. animdesc base |
| 52/56 | int | `numikrules` / `ikruleindex` | (0 on probed) |
| 60 | int[3] | trailing | (0,0,0) |

(VtMB has **none** of modern Source's `animblockindex`/`sectionindex`/`zeroframe`
streaming fields — older HL2-Beta layout.)

`mstudiomovement_t` is **44 bytes** [SDK cross-reference, data-verified]:

| Off | Type | Field | Meaning |
|---|---|---|---|
| 0 | int | `endframe` | Last frame of this piecewise block |
| 4 | int | `motionflags` | Motion component mask |
| 8/12 | float | `v0` / `v1` | Block-start/end movement values |
| 16 | float | `angle` | Cumulative yaw at the block end |
| 20 | Vector | `vector` | Direction relative to the block's initial angle |
| 32 | Vector | `position` | Cumulative Source-space displacement at the block end |

The array starts at `animdesc + movementindex`. Source's sequence ground speed is the final
record's displacement length divided by `(numframes - 1) / fps`. The exporter writes that scalar
beside a resolved blend cell as `motion.{cycle_seconds,ground_distance_cm,ground_speed_cm_s}`;
distance is converted from Source inches to centimetres offline. The skeletal glTF remains in
place, because also translating its root would double-move a body whose route motor consumes the
same metadata. Missing or malformed movement metadata is optional and leaves the runtime's prior
gait fallback intact.

The character vocabulary and bank animation have deliberately different keys. `ACT_WALK` selects
the sequence label `walk`; that label owns the include-tree mapping to the gendered
`move_and_ranged` bank. Only after the bank is known does its neutral blend grid select the baked
animation `walk_0`. A consumer must therefore play `walk` through the vocabulary resolver while
using `walk_0`'s motion metadata; treating `walk_0` itself as a character label loses the bank owner.

Across the whole character tree, **1,386 of 6,382 animdescs** carry **23,347** movement records.
This is not confined to the original `move_and_ranged` probe. On the neutral forward `walk_0`
cells, the installed male bank reports **164.019 cm over 1.2 s = 136.683 cm/s**, and the female
bank **107.807 cm over 1.064459 s = 101.278 cm/s**.
All 6,382 animdescs have `numikrules == 0`; there is no animdesc IK payload to
recover in this corpus, although sequence-tail IK locks remain outside the first-pass exporter.
The autolayer table is decoded and censused in A.3.

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
| 612 | float[3] | transition-duration triple | seconds; the crossfade time this sequence asks for, sampled piecewise-linearly over the *outgoing* sequence's cycle. Read by `FUN_1008de30`, not by the blend evaluator; the rule that consumes it is A.4c |
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

**That pair is the whole vocabulary.** Across the merged install, 451 models carry pose
parameters, and on **all 451** index 0 is `move_yaw` and index 1 is `hit_yaw`. Exactly two
models carry more — `(move_yaw, hit_yaw, aim_yaw, aim_pitch)`. No model in the install declares
`head_yaw`, `head_pitch`, `head_roll`, or any `body_*`, `spine_*` or `neck_trans` parameter, so
the engine's spawn-time lookups for those names resolve to −1 on every character
(`docs/vtmb/facial_animation.md` → Gaze).

An axis resolves by wrapping the parameter into its loop range, normalizing it over the
descriptor's `start`..`end`, remapping through the sequence's `paramstart`..`paramend`,
clamping to 0..1, and scaling against `groupsize` to yield a cell index and a fractional
weight. An axis whose `paramindex` is `-1` yields cell 0 and weight 0.

### A `move_yaw` fan's cells are angles, and `anim[0][0]` is the 180° one [data-verified, partial corpus]

A 9×1 locomotion fan places cell *k* at `move_yaw = −180 + 45k` and names that cell's animation
`<label>_<the angle taken mod 360>`. The male `move_and_ranged` `walk` grid, with each cell's own
length and authored ground speed:

| k | `move_yaw` | animation | frames@fps | ground speed |
|---|---|---|---|---|
| 0 | −180 | `walk_180` | 46@30 | 88.6 cm/s |
| 1 | −135 | `walk_225` | 41@30 | 113.9 cm/s |
| 2 | −90 | `walk_270` | 21@30 | 97.1 cm/s |
| 3 | −45 | `walk_315` | 33@30 | 88.1 cm/s |
| 4 | 0 | `walk_0` | 37@30 | 136.7 cm/s |
| 5 | +45 | `walk_45` | 33@30 | 88.1 cm/s |
| 6 | +90 | `walk_90` | 33@30 | 60.7 cm/s |
| 7 | +135 | `walk_135` | 41@30 | 113.9 cm/s |
| 8 | +180 | `walk_180` | 46@30 | 88.6 cm/s |

**Cells 0 and 8 are the same animation.** The fan closes on itself across the wrap seam, on 107 of
107 9×1 fans in one player body's resolved vocabulary. **So `anim[0][0]`@56 is the 180° cell, not
the 0° one**: the base cell of the sequence labelled `walk` is `walk_180`, the backpedal. A
consumer taking the single-cell reading above — `seq → anim[0][0] → animdesc` — therefore gets a
walk-backwards clip for `ACT_WALK`, and reads that cell's 46 frames as the sequence's where the
forward `walk_0` has 37.

**A fan's cells share neither a length nor a rate**, so they are independently authored clips
rather than one clip sampled nine ways: `walk`'s span 21 to 46 frames, and `move_and_ranged`'s
`run` fan mixes 18.0, 22.0 and 30.0 fps across its nine cells. Anything that blends two cells has
to normalize them by cycle rather than by frame.

947 of those 107 fans' 963 cells carry the `<label>_<angle>` name. Three keep the bare label on the
0° cell (`claws_aggressive_walk`, `onehand_sneak`, `twohand_sneak`), four spell it
`<base>_run_0_alt<N>` (the `claws` and `frenzy` aggressive-run alternates), and the remaining nine
are `hit_torso`, whose cells are named by direction word (`hit_torso_back`,
`hit_torso_front_left`, …) instead of by angle. `hit_torso` is also the one fan of the 107 bound to
pose parameter 1, `hit_yaw`; the other 106 are all on `move_yaw`.

Which physical direction `move_yaw = 0` is, is **not established** by the cell placement alone
[inferred]. `walk`'s 0 cell is its fastest and its 90 cell its slowest, which reads like
forward against strafe — but a mirrored convention reproduces the same per-cell speeds, and
neither `run` nor `sneak` repeats the ordering (`run`'s extremes sit at ±90, `sneak`'s fastest at
−135). Reading the two yaws that the ordinary player selector differences before it writes the
parameter is what settles it.

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
`("ACT_TRANSITION", 2)`, `("ACT_FIDGET", 3)`, … — **4,460 registrations recovered from the full
function**, occupying IDs 1…`0x118c` with 32 holes. That is the map `activity`@12 is filled from at
model load, and it confirms the literal is the stable identifier rather than an artefact: the index
space is the DLL's, so it would change with a build, while the name is what the `.mdl` ships.
Landmarks: `ACT_IDLE` = 1,
`ACT_SCRIPT_CUSTOM_MOVE` = 0x18 (the activity a `scripted_sequence`'s `m_iszCustomMove` selects),
`ACT_DISPOSITION` = 0xf1. The tail of the table is a long knockback/ragdoll family
(`ACT_KNOCKBACK_FLYING_*`), which is why the count is so much larger than any one model's vocabulary.

### Player action selection is code around the model table [VtMB decompiled]

The server-side player path is located in the pinned patch `vampire.dll`. It is not a direct
button-to-clip table. `CBasePlayer::PostThink` reads the player's realized state, asks a compact
classifier for an action code, and routes that code through the current animation mode before a
base activity is translated and selected against the model:

| Address | Working role | Established behavior |
|---|---|---|
| `0x1016be10` | `CBasePlayer::PostThink` | calls the classifier, then virtual `+0x704` with its result |
| `0x1016bb50` | player action classifier | returns `-1` or a compact code from movement/ground/water/scripted state; symbolic names for the complete code set are not established |
| `0x10164240` | animation-mode router | dispatches the action code through mode-specific virtuals; the ordinary mode reaches virtual `+0x684` |
| `0x10164870` | ordinary player selector | writes `move_yaw`, `aim_yaw` and `aim_pitch`, chooses a base activity, then calls the apply path |
| `0x101644f0` | activity/sequence apply | stores ideal activity, translates/sets the activity, chooses weighted or heaviest sequence, stores `m_nSequence`, calls `ResetSequenceInfo`, and derives playback rate |
| `0x1008dc40` / `0x1008dd30` | `SelectWeightedSequence` / `SelectHeaviestSequence` | resolve one activity against the current studio header |
| `0x10090950` | `ResetSequenceInfo` | resets cycle/sequence state after a changed selection |

The classifier is driven by state after movement rather than by raw input. Its decompile tests the
current velocity components, ground flags, water level, jump/landing state, a live interaction
handle and special animation latches. Known returns in the ordinary path cover idle/moving,
air/water and contextual states. The integer is an internal dispatcher key, not the activity enum:
one compact code can select several activities from the rest of the realized state.

The ordinary selector's static compact-code map is [VtMB decompiled]:

| Compact code | Recovered policy |
|---|---|
| `0` | stationary policy: `ACT_IDLE`, `ACT_AIM`, or `ACT_CROUCH`, with weapon/form state still able to translate the result |
| `1` | moving policy: walk, run, sneak, or their relaxed variants, selected from realized speed, flags, and weapon state |
| `2` | jump/landing phase switch: `ACT_LEAP`, `ACT_HOP*`, `ACT_LEAP_ASCEND`, `ACT_LEAP_DESCEND`, `ACT_FALLING`, `ACT_LAND`, `ACT_LAND_CROUCH`, or `ACT_LAND_HARD` |
| `5` | attack policy: grounded/airborne melee activity, or a ranged attack layer when the weapon supplies that route |
| `7` | prayer state machine: `ACT_PRAYING_BEGIN` → `ACT_PRAYING_IDLE` → `ACT_PRAYING_END` → `ACT_IDLE` |
| `8` | released feeding/seduction idle attack, selected from the active interaction flags |
| `9` | `ACT_SWIM` or `ACT_TREADWATER` |
| `10` | the live interaction entity supplies the activity through its virtual policy |
| `11` | `ACT_CLIMB_UP` or `ACT_CLIMB_DOWN`, selected from vertical velocity |
| `12` | a contextual helper supplies the activity pair; its predicates and symbolic action are not yet closed |
| `13` | `ACT_PREBLOCK` |
| `14` | adds `ACT_RELOAD_LAYER` to the base policy when a weapon is present |

Codes `3`, `4`, and `6` have no distinct ordinary-selector branch and have not been observed. The
classifier itself directly returns only `0`, `1`, `2`, `7`, `8`, `9`, `10`, `11`, and `13`; a
latched action can retain other values. That split is why static reachability and controlled traces
are both required.

The ordinary selector reaches these base activity families in the recovered branches, with the
numeric identities independently fixed by the global registration table:

- `ACT_IDLE` (1), `ACT_AIM` (5), `ACT_WALK` (9), `ACT_SNEAK` (`0x12`), `ACT_RUN` (`0x13`),
  `ACT_WALK_RELAXED` (`0x16`) and `ACT_RUN_RELAXED` (`0x17`);
- `ACT_SWIM` (`0x25`) and `ACT_TREADWATER` (`0x26`);
- `ACT_HOP` through `ACT_HOP_DOWN` (`0x28`–`0x2a`), `ACT_LEAP` through
  `ACT_LEAP_DESCEND` (`0x2c`–`0x2e`), `ACT_FALLING` (`0x2f`), and the three land activities
  (`0x30`–`0x32`);
- `ACT_CLIMB_UP` / `ACT_CLIMB_DOWN` (`0x33`/`0x34`) and `ACT_CROUCH` (`0x3f`).

Presence in this selector does not prove a branch is reachable in shipped gameplay. In particular,
the activity inventory is broader than the reconstructed movement system, so a controlled trace is
what separates a live action from inherited/dead engine code.

Activity translation is a second compiled-data layer. `CBaseCombatCharacter::Weapon_TranslateActivity`
at `0x10327ec0` asks the active weapon's virtual `+0x5a4` for an override. A weapon exposes the same
table independently through virtual `+0x5a8` (pointer) and `+0x5ac` (count); the table row is three
dwords — base activity, weapon activity, and `required`. `0x1024edc0`, reached by retail's developer
ConVar `activitydump`, prints those exact rows as **Base Act / Weapon Act / Required**. This is an
extractable table per weapon class, not a set of clip-name heuristics.

The ordinary apply path's exact order is [VtMB decompiled, capture-verified]:
`SetIdealActivity` → virtual `+0x5f4` (`Weapon_TranslateActivity`) → virtual `+0x5e0`
(`CBasePlayer::NPC_TranslateActivity`) → `SetActivity` → weighted/heaviest sequence selection.
`SetIdealActivity` at `0x10324500` only stores the requested value at character `+0xff0`; its return
site is `0x10164577`, the weapon-translation return is `0x10164582`, and the player translation call
follows at `0x10164587`. The order outside this ordinary player path — special modes, forms, and the
NPC class chain — remains open.

`CBasePlayer::NPC_TranslateActivity` at `0x101647a0` is the player-specific translation after the
weapon hook: it
maps `ACT_WALK_RELAXED` to `ACT_WALK` and `ACT_RUN_RELAXED` to `ACT_RUN`, leaving other activities
unchanged. The mode-specific/form-specific translation paths remain part of the gameplay-action
investigation; the known functions and open edges are pinned in
`research/cases/animation-pose/specs/gameplay_actions.json`.

That row is what makes the unarmed case resolve at all. A player body carries **no
`ACT_WALK_RELAXED` or `ACT_RUN_RELAXED` sequence**: the only relaxed gaits in
`tremere_Male_Armor_0`'s whole resolved vocabulary are the weapon-suffixed
`<weapon>_relaxed_walk` / `<weapon>_relaxed_run` pairs [data-verified, partial corpus]. Without
the translation an unarmed request for either activity would select nothing.

The first controlled `sm_hub_1` action corpus closes the ordinary locomotion slice
[capture-verified]. Its pinned `vampire.dll` produced **72,440** `ELGACT1` records and exercised all
four operator beats; the verifier joins each classifier return to the ordinary apply call sites and
the clip-table join resolves the selected sequence against
`character/pc/male/tremere/armor0/tremere_Male_Armor_0.mdl`:

| Realized action | Base → translated activity | Exact selected answer |
|---|---|---|
| stationary idle | `ACT_IDLE` → `ACT_IDLE` | weighted `idle01` / `fidget01` / `fidget02` from `misc`, plus the duplicate `idle01` entries from `pc_idles` |
| unarmed ready | `ACT_AIM` → `ACT_READY_FISTS` | `fists_ready`, sequence 733, `fists` |
| walk | `ACT_WALK_RELAXED` → `ACT_WALK` | `walk`, sequence 5, 9×1 `move_yaw`, `move_and_ranged` |
| run | `ACT_RUN_RELAXED` → `ACT_RUN` | `run`, sequence 4, 9×1 `move_yaw`, `runotherspc_pcidles_allsequences` |
| sneak | `ACT_SNEAK` → `ACT_SNEAK` | `sneak`, sequence 7, 9×1 `move_yaw`, `move_and_ranged` |
| crouch request | `ACT_CROUCH` → `ACT_CROUCH` | `crouch`, sequence 8, `move_and_ranged`; reused while the request remained active |
| jump phase 1 | `ACT_LEAP` → `ACT_LEAP` | `leap`, sequence 1407, `misc` |
| jump phase 7 | `ACT_FALLING` → `ACT_FALLING` | `falling`, sequence 1410, `misc` |
| landing phase 8 while moving | `ACT_WALK_RELAXED` → `ACT_WALK` | `walk`, sequence 5 |
| landing phase 8 while still | `ACT_LAND` → `ACT_LAND` | `land`, sequence 1411, `misc` |
| landing phase 8 while ducked | `ACT_LAND_CROUCH` → `ACT_LAND_CROUCH` | selection returns `-1` on this body; no clip was observed |

Only compact codes `0`, `1`, and `2` occur in this controlled corpus. It therefore proves the
three-Cs locomotion path, including the relaxed-gait translation and sequence ownership, but not the
unobserved static branches above. In particular, the ordinary jump does **not** visit
`ACT_LEAP_ASCEND` or `ACT_LEAP_DESCEND`: its observed chain is `ACT_LEAP` → `ACT_FALLING` → moving
gait or land.

The model makes the final choice. `SelectWeightedSequence` enumerates sequences whose runtime
activity ID matches, then uses `actweight`; the chosen sequence still carries the label, include
owner, blend grid, flags, fade, events and autolayers described in this section. The player-body
union currently contains 3,330 exact sequence descriptors and **1,202 distinct non-empty activity
literals case-insensitively**, 1,203 as spelled. That population makes a manually authored remake
table both incomplete and unnecessary: the model inventory is the table.

**One body already carries the whole vocabulary** [data-verified]. `tremere_male_armor_0`'s include
DAG is 33 of those 122 models and 1,577 of the 3,330 descriptors, and it reaches every one of the
1,203 spellings — the other 89 models add sequences, not activities. The union buys coverage of
*which clip answers*, not of *what can be asked*.

**The gap between the two counts is one authored inconsistency, not a decode artifact.**
`ACT_MELEE_ATTACK_SLEDGEHAMMER` and `ACT_MELEE_ATTACK_sledgehammer` both occur, and both occur inside
the same two models — `shared/male/sledgehammer.mdl` and `shared/female/sledgehammer.mdl`. Whether
they name one activity or two depends on whether the DLL's name-to-enum resolution at model load
case-folds, which is **not recovered**, so a consumer that lowercases the literal merges a pair
retail may or may not merge.

The retail `player_sequence` command is the deliberate bypass. Its handler at `0x10348560` resolves
an exact label through `LookupSequence` (`0x1008f7b0`), writes `m_nSequence`, calls
`ResetSequenceInfo`, and zeros the cycle. It performs no activity or weapon translation. Scripted
and choreographed content that names an exact sequence needs the same distinct route in a remake;
ordinary gameplay does not.

The equivalent NPC policy chain is not yet closed. NPCs clearly share the same activity registry,
model vocabulary and final sequence selectors, while schedules/tasks and class overrides decide
their desired activities. The exact `schedule/task → ideal activity → translation → selection`
call graph, interrupt order, and weapon/class override order are open RE facts; they must be
recovered rather than inferred from later public Source code.

**The one activity override a map entity can author is never used** [data-verified].
`combat_start_activity` is stamped on **423** NPC and `npc_maker` entities across the 22 exported
maps — `npc_VPedestrian` 118, `npc_maker` 109, `npc_VHumanCombatant` 80, `npc_VVampire` 55 and nine
other classes — and every one of them reads `-1` or the literal `ACT_INVALID`. No shipped entity
names a spawn-time combat activity, so the whole demand goes through the spawned class's own
selection.

`numevents`@20 is non-zero on **124 of `move_and_ranged`'s 602** sequences; the event
array is located but not decoded. Whole-install scope is **1,044 event-bearing
sequences / 1,714 events**. The current exporter writes no event timeline.

### What one player body answers the selector with [data-verified, partial corpus]

`tremere_Male_Armor_0` — the body the live capture validated (A.4b) — resolves **1,462 distinct
sequence labels** through a 33-model include DAG, 28 of whose models own at least one label:
`move_and_ranged` 601, `misc` 262, then the weapon, feed and per-clan `pc/*` banks, plus 4 clips of
its own. What the ordinary selector's base activities above find on it, with `frames@fps` and
`flags`@8 read off the **base cell**, which on a fan is the 180° one:

| Activity | Label (`actweight`) | Owner bank | Shape | frames@fps | `flags` | fade | ground speed |
|---|---|---|---|---|---|---|---|
| `ACT_IDLE` | `idle01` (30), `fidget01`/`02`/`03` (1) | `misc` | clip | 61@30; `fidget03` 172@30 | `0x1`; fidgets `0x0` | 0.3 | — |
| `ACT_WALK` | `walk` | `move_and_ranged` | 9×1 on `move_yaw` | 46@30 | `0x1` | 0.2 | 60.7–136.7 cm/s per cell |
| `ACT_RUN` | `run` | `runotherspc_pcidles_allsequences` | 9×1 on `move_yaw` | 19@30 | `0x1` | 0.2 | 457.8–528.3 cm/s per cell |
| `ACT_SNEAK` | `sneak` | `move_and_ranged` | 9×1 on `move_yaw` | 66@30 | `0x1` | 0.2 | 69.7–79.3 cm/s per cell |
| `ACT_CROUCH` | `crouch` (30) | `move_and_ranged` | clip | 61@30 | `0x0` | 0.2 | — |
| `ACT_HOP` / `ACT_HOP_UP` / `ACT_HOP_DOWN` | `hop` / `hop_up` / `hop_down` | `misc` | clip | 2@30 / 15@30 / 12@30 | `0x0` | 0.2 | — |
| `ACT_LEAP` | `leap` | `misc` | clip | 46@30 | `0x0` | 0.2 | — |
| `ACT_LEAP_ASCEND` / `ACT_LEAP_DESCEND` | `leap_ascend` / `leap_descend` | `misc` | clip | 31@30 | `0x1` | **0.45** | — |
| `ACT_FALLING` | `falling` | `misc` | clip | 25@30 | `0x1` | 0.2 | — |
| `ACT_LAND` / `ACT_LAND_HARD` | `land` / `land_hard` | `misc` | clip | 20@30 / 71@30 | `0x0` | 0.2 | — |
| `ACT_SWIM` / `ACT_TREADWATER` | `swim` / `treadwater` | `misc` | clip | 39@30 | `0x1` | 0.2 | — |
| `ACT_CLIMB_UP` / `ACT_CLIMB_DOWN` | `ladder_up` / `ladder_down` | `misc` | clip | 29@30 | `0x1` | 0.2 | — |

**`ACT_RUN` does not resolve to `move_and_ranged`.** The include DAG reaches the PC-only bank
`shared/male/runotherspc_pcidles_allsequences` first, and that bank re-authors the whole fan. Its
nine cells run at a uniform 30 fps where the shared bank's mix 18.0, 22.0 and 30.0, so the player's
whole fan cycles in 0.567–0.6 s where the cast's runs 0.6–1.0 s. The cells disagree in distance
too: the two banks agree exactly on the 0° cell (478.7 cm/s on both) and diverge everywhere else,
most sharply at 180°, where the shared bank backpedals at 313.2 cm/s against the PC bank's 522.0.
All nine of the shared bank's cells are spelled `npc_run_*`. **The player and the cast do not share
a run**, and a resolver keyed on the label alone rather than on the owner the DAG names hands the
player the cast's gait.

**Nothing else in this vocabulary holds an unarmed crouch.** `crouch` is a 61-frame non-looping
one-shot — an *into* pose — and the only crouched idles the body carries are `crouch_idle` under
`ACT_CROUCH_MELEESHARED_TWOHAND` and the `<weapon>_crouch` / `<weapon>_midcrouch_idle` sets. What
retail holds a ducked unarmed player on is not readable from the model inventory. The controlled
trace shows sequence 8 being reused across 151 crouch requests, but it did not hold the input long
enough to distinguish a frozen final frame from another end-of-sequence rule.

**`ACT_HOP` is two frames** — a stub rather than the observed jump. `leap_ascend` and
`leap_descend` do carry looping 31-frame clips with a **0.45 s** crossfade (A.4c), but the controlled
ordinary jump does not select them; it selects non-looping `leap`, then looping `falling`, both at
0.2 s.

**A clip's rate is less uniform than the six-bank census above.** This body's DAG reads seven
rates — 30.0 ×1,417, 18.0 ×26, 60.0 ×5, 20.0 ×5, 35.0 ×5, 38.0 ×3 and 25.0 ×1 — so 25, 35 and 38
fps exist beside the four that census names. Every one of the 26 clips at 18.0 is a run:
`move_and_ranged`'s own `run` and its `<weapon>_aggressive_run` / `<weapon>_relaxed_run` variants.

**A player body does not include the disposition stance bank.** `shared/male/stances` is absent
from this DAG, so the plain `ACT_DISPOSITION` of the table above is unreachable on it; what the
body does carry is **8 `ACT_DISPOSITION_*` activities** — `AFRAID`, `ANGRY`, `COY`, `CRYING`,
`MESMERIZED`, `MESMERIZED_PRE_INTO`, `NERVOUS` and `STUNNED`.

### `flags`@8 is a small, nearly-closed bit set [data + VtMB decompiled]

Only five values occur across **331 loose models / 5,836 sequences**: `0x0002` ×2,642,
`0x0000` ×2,217, `0x0001` ×815, `0x0014` ×118, `0x0003` ×44. Nothing else appears.

| bit | meaning | evidence |
|---|---|---|
| `0x1` | looping | `walk`/`run`/`sneak`/every stance = 1; `crouch` = 0 |
| `0x2` | **no transition — hard cut** | the first gate of `FUN_1008de30` (A.4c): an incoming sequence carrying it yields a zero crossfade duration, and the transitioner discards the whole fading set. Attacks (`Claw1`, `Claw2`, …) carry it; `0x3` is looping + hard cut |
| `0x4` | **delta / additive** | tested at `0x10088efc` inside the pose accumulator `FUN_10088e10`, selecting a different composition branch |
| `0x10` | **selects the post-multiply additive combine** (`FUN_10088d60`) over the pre-multiply one (`FUN_10088d00`) inside the `0x4` branch — see below | read after the `0x4` gate in `FUN_10088e10`; set by every shipped `_delta` |
| `0x400` | flips the crossfade-duration combine from `max` to `min` (A.4c) | set by no shipped sequence, so unreachable and untested |

`0x1` is not a complete looping oracle: `shared/male/pc_idles`'s `idle01` reads 0 while being a
looping idle, so a consumer still applies its own policy for the clips the authors left unflagged.
The label alone does not settle it either — `shared/male/misc.mdl` carries a second `idle01`, the
copy a player body's include DAG resolves first, and that one reads `0x1`.

**`0x14` marks the additive family.** All **118** sequences carrying it are named `*_delta`
— 60 of them the male bank's — and conversely all 118 `*_delta`-named sequences across the
331 models carry exactly `0x14`, with zero exceptions in either direction. Each decodes to
near-identity local rotations (median frame-0 deviation 0.0° against 54.6° for an ordinary
partial-body layer), which is what a difference from a base pose looks like. `0x14` is
`0x4 | 0x10`, and inside the additive branch `0x10` selects the post-multiply combine. What
`0x10` would mean on a sequence that does **not** set `0x4` stays **not established**
[inferred]: it never appears alone in this corpus, and the accumulator reads it only after
the `0x4` gate.

Note that the sequence-descriptor `0x2` here is unrelated to the bone `Flags & 0x2` of
§A.4a; they are different structures that happen to share a bit position.

### The additive combine accumulates, and `0x10` picks which side the delta lands on [VtMB decompiled]

`FUN_10088e10` accumulates one evaluated layer onto the running local-space pose. `flags`@8
gates the arithmetic, with `s` the layer's weight times the animation record's per-bone
`weight`@0 mask (§A.4):

```text
if (flags & 0x4) == 0:                                          # ordinary layer
    out.quat = nlerp(out.quat, layer.quat, s)                   # complementary weights
    out.pos  = (1 - s) * out.pos + s * layer.pos
else:                                                           # delta / additive
    if (flags & 0x10) == 0:                                     # FUN_10088d00
        out.quat = normalize( scale(layer.quat, s) * out.quat )     # delta on the LEFT
    else:                                                       # FUN_10088d60
        out.quat = normalize( out.quat * scale(layer.quat, s) )     # delta on the RIGHT
    out.pos += layer.pos * s                                    # accumulates on both sides
```

The `0x4` branch carries no complementary weight — it accumulates rather than interpolating —
and position accumulates identically on both of its sides, so `0x10` changes rotation only.
`FUN_1010a450` is the plain Hamilton product `p*q`, verified component-by-component against
the identity; `FUN_1010a320` scales a rotation by a scalar.

**Every shipped additive post-multiplies.** All 118 `_delta` sequences carry `0x14`, so the
`FUN_10088d00` pre-multiply side is unreachable on shipped content.

Valve's `source-sdk-2013` carries the same control flow in `SlerpBones()`, under the names
`STUDIO_DELTA` (`0x0004`) and `STUDIO_POST` (`0x0010`), dispatching to `QuaternionSM`
(`qt = (s*p) * q`) and `QuaternionMA` (`qt = p * (s*q)`). VtMB forked Source before that
engine's public release and v2531 diverges structurally from the SDK's own format, so the
correspondence supplies vocabulary rather than authority — every statement above is read
from this binary, and the rest of the SDK's flag table is not carried across.

### The autolayer table is the base→layer binding [data-verified]

`numautolayers`@660 / `autolayerindex`@664 declare, per sequence, a list of **4-byte entries, each
a bare sequence index** the dispatcher evaluates alongside the base. VtMB's record is only that
index: unlike later Source's `mstudioautolayer_t`, it carries **no pose parameter, no flags and no
ramp**, so whatever weight a layer contributes lives in the game DLL rather than in the file.

**The combine an autolayer goes through is resolved; the weight it arrives with is not.**
`FUN_10089c40` evaluates each entry and hands the result to the accumulator `FUN_10088e10`, whose
arithmetic is given above — a complementary-weight blend for an ordinary layer, an accumulation for
a `0x4` delta. What the file cannot supply is the scalar: the accumulator takes a layer weight from
its caller and multiplies it by the record's per-bone `weight`@0, and the derivation of that
caller-side value is **not established** [inferred]. A capture hook on the accumulator's entry
would settle it; nothing readable from the shipped bytes can.

The census is clean and small. Across **4,249** v2531 models / 10,739 sequences the count histogram
is `{0: 10,278, 1: 237, 2: 224}`, and the only carriers are
`models/character/shared/{male,female}/move_and_ranged.mdl` — 232 declaring sequences and 345
entries on the male bank, 229 and 340 on the female. **0 entries are out of range, 0 are
self-referencing, and 0 of the 111 distinct targets themselves declare autolayers**, so the
recursive dispatcher never actually recurses on shipped content: depth is 1 and fan-out is at most
2. Every target is a `_layer` or a `_delta`, and **none carries an activity**, which is the same
statement the empty-activity population above makes from the other side.

The two-entry pattern is uniform: `<weapon>_aim_layer` plus `<weapon>_<action>_delta` — a masked
partial-body overlay and an unmasked additive, one of each.

**`autolayerindex`@664 is relative to the descriptor, and its entries index the declaring model's
own local sequence array** [data-verified]. Read that way, all 345 entries on the male bank and all
340 on the female resolve inside `NumLocalSeq`@272 and every one names a `_layer` or `_delta`
sequence; read as an absolute file offset, 64 and 43 resolve and none is a layer. The two readings
are distinguished by the data rather than by the SDK, and the name partition is the check.

**Entry order is authored data, not a convention** [data-verified]. The dispatcher walks the array
in index order, and on 112 of the male bank's 113 two-entry hosts — 110 of the female's 111 — the
overlay is entry 0 and the additive entry 1, which is the only order in which both survive: an
overlay blends toward its own pose with complementary weights and so overwrites an additive already
accumulated onto the bones it owns. **One host inverts it, byte-identically on both banks**:
`throwing_star_midcrouch_idle` declares `pistol_midcrouch_idle_delta` first and
`throwing_star_aim_layer` second. It also borrows a *pistol* delta under a throwing-star host, which
reads as authoring reuse rather than intent, but it is what the file states and a consumer that
hardcodes overlay-first composes this one host differently from retail. Single-entry hosts are
always an overlay — 119 male, 118 female, no additive among them.

**The running game reproduces that census exactly.** Capture of the retail pose pipeline records
**461 of 2,478** captured sequence descriptors carrying autolayers — the same 461 the static
`{1: 237, 2: 224}` histogram counts — and every one of them belongs to `move_and_ranged.mdl`, male
or female, which is the file-side "only carriers" statement reached independently. Each layer is
evaluated at the host sequence's own cycle, bit-identical to it. Measured examples: `smith_ready`
→ `smith_aim_layer`, itself a 3×3 grid on the `aim_yaw`/`aim_pitch` pose parameters;
`baseballbat_aggressive_run` → `baseballbat_bobble_layer`; `smith_aggressive_run` →
`smith_aim_layer` plus `smith_bobble_delta`.

**A pose build is nearly always one sequence.** Counting contributions per build, `sp_theatre`
measures 99.712% one, 0.281% two and 0.007% three, with **zero autolayer recursion** — the
census's depth-1 property observed live rather than inferred from the bytes. `sp_tutorial_1` is
the busier case: one renderable in one frame reaches 5 concurrent sequences, 13 contributions and
30 blend cells.

**A second binding mechanism exists and is not in the model.** 46 masked sequences on the male bank
carry an `ACT_*_LAYER_*` activity — every weapon's attack, reload and dryfire layer, the discipline
casts named in `vdata/system/disciplinetgt_000.txt`, and both `lookback_*_layer` — so the game DLL
selects them by activity and composes them as layers. No autolayer entry names them. Five sequences
per bank are neither bound nor selectable, byte-identically on male and female, and read as
abandoned authoring.

**One decode hazard.** Seven single-sequence scenery and weapon models (`projector.mdl`, three
`libcolumn_*`, `malklifetube_trims.mdl`, `g_handleclaws.mdl`, `i_handleclaws.mdl`) read
`numautolayers == 764` at sequence 0 — the descriptor tail runs past the end of the file and lands
in the string table. A reader must bound both the count and the array against the image.

**Two cautions when judging a layer by eye.** A delta over a host that does *not* declare it is
arithmetically exact and anatomically nonsense — `twohanded_crouch_attack_delta` over a standing idle
swings an arm that never raised, because the crouch and the arm-raise live in the base and the delta
carries only the swing. The pairing to judge against is the table's, not the one the two names
suggest. And **the legs and torso are not a control group**: whole-body sway under an additive is the
authored data rather than a defect, since deltas stack down the chain to roughly 37° at the skull.
A root that *translates* would be the defect.

## A.4 Animation data — the 32B/bone record + `{valid,total}` RLE [data-verified]

The core decode. At `animdesc_base + animindex` (@48): an array of **one 32-byte
record per bone** (bone order = header order), immediately followed by the
variable-length RLE blobs.

**Per-bone record (32B):** `float weight`@0 (the per-bone mask — see below), then
`int offset[7]`@4 — the 7 channel offsets **relative to this
record's start**, in order **posX, posY, posZ, rotX, rotY, rotZ, rotW**.
`offset[c]==0` ⇒ channel not animated (use bind value); `offset[c]>0` ⇒ an RLE track
at `record_base + offset[c]`.

**`weight`@0 is a binary per-bone mask, and it is load-bearing** [data-verified].
Read across the whole install it takes **exactly two values** — `{0.0, 1.0}` over
**736,208** `(animation, bone)` records on **4,508** models, with 18,346 zeros and no
fractional value anywhere. Both retail channel decoders compare it against zero before
anything else and, on zero, write a zero position and a zero quaternion and return —
reading no channel offset, no track and no bind field. So the zero set names the bones an
animation does **not** own, which is what lets a partial-body layer be composed over a base
without disturbing the bones the base owns.

The zero sets are authored masks, not physics groups: `lookback_left_layer` zeroes 59 of 60
bones and keeps only `Bip01 Head`; `katana_bobble_layer` keeps exactly the right-arm chain and
the weapon props. Only **5 distinct masks** occur over the 722 animation descriptors of the male
`move_and_ranged` bank (and 5 over the female bank's 674) — an unmasked one, one zeroing root,
pelvis, spine and both legs, one keeping arms and props only, one on the abandoned throwing-star
set, and the head-only one.

**Every cell of a blend grid carries the same mask** [data-verified, partial corpus]. A grid
(§A.3) names up to 16×16 animations behind one label, and each is its own animation record with its
own `weight`@0 — nothing in the format ties them together. They agree anyway: over the **135
multi-cell grids** of the six male shared banks (`move_and_ranged`, `misc`, `frenzy`, both
`meleeshared` sets and the PC idle set), no grid mixes masks. A 9×1 `move_yaw` locomotion fan is
unmasked on all nine cells; a 3×3 `<weapon>_aim_layer` carries one mask on all nine, the upper-body
gate rooted at `Bip01 Spine1`. The female banks are not covered, so this is a property of the
shipped content rather than one the format enforces — a consumer composing a grid as a single
masked layer has to assert it rather than assume it.

**No zero-weight record carries a non-zero channel offset** (0 of 736,208), so a decoder that
gates on the offsets rather than on the weight never reaches the zero branch on shipped content.
An earlier reading of this field as "always 1.0, ignorable" came from the theatre capture corpus,
where it does read 1.0 on all 2,254 decoded triples — because a cutscene fires no layer sequence,
not because the field is inert.

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
transition-maintenance path, not a ragdoll path or the local-channel decoder. It
maintains a `CUtlVector` of `0x4c`-byte previous-sequence records at entity
`+0x67c`/`+0x688`, evaluates each surviving previous sequence through
`FUN_100968a0`, and blends it into the current pose through `FUN_10096b30`; the
record layout, the crossfade weights and the rest of that mechanism are §A.4c.
The two live pose builders at `FUN_10091650` and `FUN_100979b0` call it
immediately after resolving the current base sequence pose and before autoplay
sequences, weighted layers, virtual hooks, and bone controllers.

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
3. the nested virtual-model remap branch inside `FUN_10089c40`, and the rule by which an
   autolayer's evaluated pose is combined with its host's — the outer remap, the sequence
   blends, and the autolayer *dispatch* are closed;
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

## A.4c Sequence transitions and the layer stack [VtMB — decompiled + capture-verified]

Two composition stages sit above the base sequence, both landing in `SlerpBones`
(`FUN_1008eb70`): a client-only **crossfade against the sequences the entity was
recently playing**, and a four-slot **layer stack** carried by combat characters.

Two independent methods reach this stage — static decompilation of the retail DLLs, and
numerical solution of pose arrays captured from the running retail game. Where they agree
it is stated below, and those agreements are the strongest evidence in this document: one
recovers the code, the other recovers the numbers the code produces, and neither is
derived from the other.

### The previous-sequence list

`FUN_10091110` maintains a `CUtlVector` at entity `+0x67c` with its element count at
`+0x688`, holding **`0x4c`-byte previous-sequence records** at that stride. The **last**
element is the sequence playing now; elements `[0 … count-2]` are previous sequences still
fading out.

| Off | Type | Field | Written at |
|---|---|---|---|
| `+0x00` | int | sequence | `0x100912d5`, from `m_nSequence`@`+0x63c` |
| `+0x04` | float | cycle at the moment it became previous | `0x100912e6` |
| `+0x08` | float | playback rate | from `m_flPlaybackRate`@`+0x640` |
| `+0x0c` | float | weight — recomputed every frame | `0x10091390` |
| `+0x10` | float | start time (`engine->Time()`) | `0x100912de` |
| `+0x14` | float | fade duration, seconds | `0x10091246` |
| `+0x18` | byte | has-saved-transform | `0x10091296` / `0x100912d1` |
| `+0x1c` | matrix3x4 | saved entity transform (48B) | `0x1009128e` (`FUN_101094b0`) |

`FUN_10091110` is `__thiscall(this, five stack dwords)`, cleaning `0x14` at its own
epilogue, the same shape as `SetupBones`, and it compares the count against zero before
anything else. A sequence change is therefore observable from outside as a growth in that
count, without decoding an entry.

### The ramp is `SimpleSpline`

Per frame, for every record except the last (`0x10091340`–`0x100913b8`):

```
f = 1.0 - (engine->Time() - rec.startTime) / rec.duration;
if (f <= 0) evict the record;
if (f < 1.0) f = 3*f*f - 2*f*f*f;      // SimpleSpline; the 3.0f constant is at 0x10225158
if (f <= 0) evict;
rec.weight = f;
```

`rec.weight` is the blend factor **toward the old pose**. Since `1 − S(1−t) = S(t)` for
`S(x) = 3x² − 2x³`, the equivalent statement is that the **new clip's weight is
`SimpleSpline(elapsed / duration)`**. `FUN_10088e10` clamps the weight to 1.0 where it
consumes it and skips the pose entirely at `<= 0`.

**Capture agrees with the decompile on every part of this.** Solving
`FINAL = P1 + w·(P2 − P1)` over recorded pose arrays returns a single scalar that explains
the whole pose — per-component spread `1.5e-6` over 197 components — and that scalar's ramp
matches `SimpleSpline` to every printed digit. Rotations are provably **slerp, not nlerp**
(median error `3.5e-8` against `1.6e-6` for the nlerp candidate); positions are a plain
lerp on the same scalar; bones outside the `SetupBones` bone mask are untouched.

### Duration is authored per sequence and combined as a `max`

`FUN_1008de30(outgoingSeqdesc, incomingSeqdesc, outgoingCycle)`:

```
if (!out || !in || (in->flags & 0x2)) return 0.0f;        // the refusal, at 0x1008de4d
cap = in->[0x264];
v   = piecewise-linear over cycle across out->[0x264], out->[0x268], out->[0x26c];
return (in->flags & 0x400) ? min(v, cap) : max(v, cap);   // 0x1008defd
```

The three floats are the `mstudioseqdesc_t` triple at 612/616/620 (§A.3). **The triple is
constant within a sequence in 100% of shipped content**, so the cycle interpolation always
collapses to one number per sequence, and no sequence sets `0x400`, so the rule is always
`max`. Across 331 loose models / 5,836 sequences the values are `(0.2,0.2,0.2)` ×5,762,
`(0.3,0.3,0.3)` ×58 and `(0.5,0.5,0.5)` ×16. The non-default clips are deliberate
authoring: the 0.3 s ones are dialogue (`nines_damagedw.mdl`, `Jeanette.mdl`,
`Therese.mdl`, `Tourette.mdl`), the 0.5 s ones lying-down and damaged-stance idles
(`hannah.mdl`, `heatherneardeath.mdl`, `lacroix.mdl`, `vv.mdl`). Capture of VPK-resident
models additionally observes **0.45** on the female `stances.mdl`, so the loose-model
survey above is a large sample rather than the complete corpus.

The VPK-resident shared banks widen it further, and the non-default values are not only dialogue.
Over one male player body's resolved vocabulary (A.3) the histogram is 0.2 ×1,401, **0.3 ×54**,
0.5 ×5 and **0.45 ×2** [data-verified, partial corpus]: the 0.3 s carriers are `idle01`, the three
`fidget` clips and the whole `bushhook`/`sledgehammer` attack sets, and the two 0.45 s carriers are
the male `misc` bank's `leap_ascend` and `leap_descend`.

**Capture agrees on the combine.** Scored against recorded transitions,
`duration = max(fade(current), fade(previous))` is right on **80 of 80** across three
theatre captures, where a current-only rule scores 20 of 26 and a previous-only rule 16
of 26.

### Concurrency, eviction, interruption

There is **no cap on concurrent transitions**: the append path is `EnsureCapacity(1)` plus
`InsertMultiple` with no bound check, and eviction is purely a record's weight reaching
zero. Survivors are blended in **descending index order — newest previous first, oldest
last** — each a slerp of the accumulated pose toward that record's pose by that record's
own weight. This is **a chain of pairwise blends, not a normalised N-way blend**, so the
oldest surviving record exerts the weakest pull on the result.

A sequence change arriving mid-transition is detected at `0x10091193` and simply
**appends**. Every in-flight record is kept untouched on its own clock; nothing is
shortened, dropped early, or refused. Capture observes the count moving `0 → 1 → 2 → 3 →
4`.

### The one refusal is a property of the incoming clip

When the duration is `<= 0` — which happens when the **incoming** sequence sets
`flags & 0x2`, or when entity flag `0x10` is set — `0x1009129c` discards the entire
previous-sequence list (`count = 0`, then `FUN_10096c30`) and appends the new record
alone. That is an unconditional hard cut, and what decides it is the clip being entered,
not whatever is already running. **2,642 of 5,836 sequences set `0x2`**, so the hard cut
is the most common authored transition behaviour in the corpus.

### Transitions are client-side only

`FUN_10091110` is called by both client pose builders — `FUN_10091650` at `0x1009176d`
and `FUN_100979b0` at `0x10097a92` — immediately after the base `CalcPose`. The server's
builder `FUN_10098eb0` in `vampire.dll` has no transitioner and no `+0x67c` vector, so
server-side bone setup sees the un-blended current sequence.

### The layer stack — `CBaseAnimatingOverlay`

The class name comes from the `DevMsg` string at `0x10552678`. `CBaseCombatCharacter`
derives from it; plain `CBaseAnimating` does not, so an ordinary animated prop carries no
layers at all.

Server-side the array base is `this + 0x73c`, stride `0x30`, **exactly four entries**:

| Off | Field | Note |
|---|---|---|
| `+0x00` | sequence | |
| `+0x04` | cycle | |
| `+0x08` | playbackrate | |
| `+0x0c` | weight | `0` means the slot is free |
| `+0x14` | blend-in | initialised `0.2` |
| `+0x18` | blend-out | initialised `0.2` |
| `+0x1c` | owner / activity id | initialised `-1` |
| `+0x20` | byte | flags |

The API around it: `AllocateLayer` `FUN_10099470`, `SetLayer` `FUN_10099020`,
`FindLayerByOwner` `FUN_100994c0`, `HasLayer` `FUN_10099540`, `RemoveLayer` `FUN_10099660`,
`RemoveLayerByOwner` `FUN_100995e0`, `AddGestureSequence` `FUN_100990f0` / `FUN_10099140`,
`AddGesture` `FUN_100991b0`.

Composition (`FUN_10098eb0`) is the base sequence, then the layers in **ascending
array-index order**, each accumulated by `SlerpBones` (`FUN_1008eb70`) with a single scalar
weight — a weighted slerp toward the layer pose, neither an additive add nor a per-bone
replace. **There is no priority field**: order is the array index and nothing sorts it. The
`+0x1c` owner id serves lookup and removal only.

**Layers do not ramp.** `blend-in` and `blend-out` are written by `SetLayer` and read
nowhere in `vampire.dll`, and they are absent from the SendTable —
`DT_BaseAnimatingOverlay` transmits only `sequenceN`/`cycleN`/`playbackrateN`/`weightN` for
N = 0..3 plus a flinch block — so no client-side ramp can be built on them either. Nothing
advances `layer.cycle` or ramps `layer.weight` per frame on the server, and a superseded
layer is killed outright (`weight = 0, sequence = 0`) with no dying state.

The client mirrors the same four entries at base `this + 0x7DC`, stride `0xE4` — larger
because each networked float carries a `CInterpolatedVar` history — and composes them
identically in `FUN_100979b0`. Its only per-layer weight logic is a linear lerp between two
received snapshot values plus a deliberate snap to 0 or 1 when the sequence index changes,
which **suppresses** smoothing across exactly the change a crossfade would want it for.

**`SetLayer` initialises weight to `0.1f`**, and the client never substitutes a default, so
a layer composes at 10% weight. That is measured from the code rather than reasoned from
behaviour, and it is surprising enough to hold as unconfirmed: a live capture of a fired
gesture, reading the composed weight back off the pose, would confirm or overturn it.

The client advances a layer's cycle with the same routine it uses for the base sequence,
`FUN_1008fa70`:

```
c = cycle + (dt / SequenceDuration(sequence)) * playbackrate
looping:      c = c - floor(c)
non-looping:  c = clamp(c, 0.0, 0.999)
```

`dt` there is `frac * 0.1`, where `frac` is the snapshot fraction from `FUN_1008f960`
clamped to `[0, 2]` — snapshot interpolation, not playback time.

**One layout inconsistency is open.** Four `0x30` entries from `0x73c` run to `0x7fc`, yet
the server's flinch array is declared at `0x7f4`; on the client, four `0xE4` entries run
past the flinch block declared at `0xb20` whichever of the two bases the evidence names for
that array (`0x7dc` from the composition loop, `0xad4` from the flinch accounting, which do
not themselves reconcile). Both strides are corroborated by the iteration code on their own
side, so the declared flinch offsets are the anomaly rather than the strides. The
discrepancy is **unexplained**; recovering the class declaration, or watching which bytes a
flinch write actually touches, would resolve it.

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

The patch-first grid contains one explicit source exception. The Night Watchman in
`sm_junkyard_1` references
`models/character/npc/doppleganger/doppleganger.mdl`, which is absent even though the
`common/doppleganger` male and female variants exist. The exporter downgrades only that
exact path to a structured warning in `npc_manifest.json` and `npc_index.json`; every
other missing model or decode failure remains fatal.

**An authored `LoopSequence` is not proof of motion.** Six models a `prop_dynamic` selects
declare exactly one **single-frame** sequence: `stage_light`, `lampfloor`, `glassa`,
`junkyardcraneb`, `bottleb` and `bottlec`. They are static dressing wearing an animation
keyvalue — there is nothing to bake, and standing a skeletal body for one would replace the
decoded static mesh with a bind pose. The animated-prop seed therefore requires at least one
sequence carrying more than one frame. The test is *any* sequence, not every one: `clamp`'s
`idle` is a single frame beside its real 45-frame `open`/`close`, and `wolf_form` carries
twelve single-frame hit poses among its clips.

Two of those six (`bottleb`, `bottlec`) and `stage_light` also use the compact vertex formats
(`StudioVertex2` 12B, `StudioVertex3` 8B) rather than the 44-byte skinned layout. Those formats
carry a quantized position, a packed normal and a UV — **no `BoneWeight` at all** — so the
skinned decode path cannot read them, and the model's whole geometry is rigidly bound to its
single bone. Only `vlist == 0` models can take the skeletal path.

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
| `mstudioeyeball_t` | 168B, authored on every speaking character | **140B**, authored two per character on 301 models — but the count/index pair sits at `StudioModel`+192/+196, not Source's slot |

---

# Part B — Brush-entity movers

Moving brush models (doors, buttons, spinners, elevators, trams), driven by the
Source `CBaseToggle` lineage compiled into `vampire.dll` and wired through the I/O
bus. All are authored at `(0,0,0)`; the entity `origin` is the spawn translation and
— for rotating doors — the **hinge** (`docs/vtmb/mdl_v2531.md`/`docs/vtmb/entity_io.md`).

## B.0 `CBaseToggle` sits under every animating entity [VtMB — decompiled]

VtMB's class chain is **`CBaseEntity → CBaseToggle → CBaseAnimating → …`**, inverted relative to
stock Source, where `CBaseToggle` is a leaf branch. Every animating entity therefore inherits the
mover — a `prop_dynamic`, an NPC and a `func_door` all carry the same `MoverData` block and the same
four move inputs. The mover keyvalues are **not** per-class, and a class that never calls
`StartMover` still parses and stores them.

Datamap `0x1059b6f0`, records `0x1059b734`, 38, builder `FUN_101c0cc0`:

| externalName | internal | offset | notes |
| --- | --- | --- | --- |
| — | `m_toggle_state` | `+0x4f8` | save |
| — | `m_flMoveDistance` | `+0x4fc` | save |
| `wait` | `m_flWait` | `+0x500` | |
| `lip` | `m_flLip` | `+0x504` | |
| `height` | `m_flHeight` | `+0x538` | |
| — | `MoverData.vecMovePos1` | `+0x450` | home position, captured at spawn |
| `move_dest` | `MoverData.vecMovePos2` | `+0x45c` | destination position |
| — | `MoverData.angMoveAngle1` | `+0x468` | home angles, captured at spawn |
| — | `MoverData.angMoveAngle2` | `+0x474` | home angles with component `[rot_axis] += rot_dist` |
| `move_speed` | `MoverData.flMoveSpeed` | `+0x480` | |
| — | `MoverData.Rot` | `+0x484` | save |
| `rot_axis` | `MoverData.Axis` | `+0x488` | index 0/1/2 into the angle triple |
| `rot_dist` | `MoverData.flRotDist` | `+0x48c` | degrees |
| `rot_speed` | `MoverData.flRotSpeed` | `+0x490` | deg/s |
| — | `MoverData.TargetPos` | `+0x494` | save |
| — | `MoverData.CurrPos` | `+0x498` | save |
| — | `MoverData.UseType` | `+0x49c` | save; copied from `use_pref` at mover init |
| `use_pref` | `m_MoverMoveType` | `+0x4a0` | **`1` linear, `2` rotational, anything else no mover** |
| — | `m_flMoveRebound{Duration,Amount,StartTime,Velocity}` | `+0x4d4`…`+0x4ec` | save |
| `MoveToDest` | `InputMoveToDest` | — | `0x101c1b60` → `StartMover(1)` |
| `MoveToHome` | `InputMoveToHome` | — | `0x101c1b40` → `StartMover(0)` |
| `RotateToDest` | `InputRotateToDest` | — | `0x101c1ba0` → `StartMover(3)` |
| `RotateToHome` | `InputRotateToHome` | — | `0x101c1b80` → `StartMover(2)` |
| `OnLinearMoveDone` | `m_OnLinearMoveDone` | `+0x4a4` | |
| `OnAngularMoveDone` | `m_OnAngularMoveDone` | `+0x4bc` | |

`CBaseToggle::StartMover` (`FUN_101c1bc0`) switches 0..3 onto LinearMove/AngularMove; mover init is
`FUN_101c1cb0` (`UseType ← use_pref`, `CurrPos ← 0` linear / `2` rotational); the toggle helper is
`FUN_101c1dc0`. In the shipped corpus only `item_container`, `item_container_animated` and
`prop_mover` drive it — `item_container*` uses it for the lid
(`docs/vtmb/entity_io.md` → "`item_container`"). `prop_switch` carries the same keys but animates
through its own `.mdl` sequences instead.

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
(485 uses), `use_override` (26 — routes `+use` to a separate button). `climbable`
(1090 uses) is authored but **dead** — the string occurs nowhere in `vampire.dll`
(`docs/vtmb/entity_io.md` → "Proven-dead Hammer/FGD keys").

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

Gameplay-action selection [VtMB — decompiled + data-verified]: hash-pinned
`research/cases/animation-pose/specs/gameplay_actions.json`; player-model descriptor inventory from
`research/tooling/capture/inventory_player_animations.py`. The specification records confirmed
addresses and preserves the unresolved player-code vocabulary and NPC policy edges as questions.
