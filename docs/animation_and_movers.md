# VtMB animation & movement — skeletal models, brush movers, and the Godot mapping

Everything in VtMB that *moves* falls in two subsystems: **skeletal model
animation** (NPCs, animals, animated props — a rigged `.mdl` skeleton driven by
compressed keyframe clips) and **brush-entity movers** (doors, buttons, spinners,
elevators, trams — a brush model translated/rotated at runtime by parametric logic).
This documents the on-disk format and runtime behaviour of both, and how each maps
onto Godot 4. It is the companion to `mdl_v2531.md` (static-geometry half of the
`.mdl`) and `entity_io.md` (the I/O bus the movers ride).

Evidence tags: **[VtMB]** = decompiled `vampire.dll` (image base `0x10000000`);
**[data]** = shipped map/model bytes, probe-verified; **[ref]** = a VtMB-native
reference parser (VAMPTools / Crowbar); **[SDK]** = modern Source baseline
(`tools/re/source-engine/public/studio.h`); **[inferred]** = reasoned, not yet
confirmed. All counts are over the **patched** install.

---

# Part A — Skeletal model animation (`.mdl` v2531)

`mdl_v2531.md` decodes the static half (header, bodyparts, meshes, three vertex
formats, materials) and deliberately **raises** on the ~485 `models/character/**`
skeletal models. This part fills that gap: bones, sequences, the compressed
animation tracks, skinning, and attachments — enough to rig and play a clip.

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
| 136 | int | `Flags` | `BONE_USED_BY_*` / physics mask; **bit `0x2` = `BONEFLAG_ORIENTATION`** (animation stored in a permuted axis frame — see §A.4a) |
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

## A.3 Sequences and animations

**`StudioAnimDesc` — 72 bytes** [data-verified] (`NumLocalAnims`@264 /
`LocalAnimIndex`@268). The per-clip descriptor:

| Off | Type | Field | Note |
|---|---|---|---|
| 0 | int | `NameIndex` | names begin `@` (e.g. `@Jeanette_Line1_Col_E`) |
| 4 | float | `fps` | all probed = 30.0 |
| 8 | int | `flags` | loop/delta |
| 12 | int | `numframes` | 101–501 on probed clips |
| 16/20 | int | `nummovements` / `movementindex` | root motion (0 on probed) |
| 24 | Vector | `bbmin` (3f) | per-anim bbox |
| 36 | Vector | `bbmax` (3f) | |
| 48 | int | **`animindex`** | → per-bone anim records, rel. animdesc base |
| 52/56 | int | `numikrules` / `ikruleindex` | (0 on probed) |
| 60 | int[3] | trailing | (0,0,0) |

(VtMB has **none** of modern Source's `animblockindex`/`sectionindex`/`zeroframe`
streaming fields — older HL2-Beta layout.)

**`StudioSeqDesc` (`mstudioseqdesc_t`) — 764 bytes** [data-verified — stride
confirmed] (`NumLocalSeq`@272 / `LocalSeqIndex`@276). The game-facing entries; each
references anims through a blend grid: `szlabelindex`@0, `szactivitynameindex`@4
(e.g. `ACT_DIALOG_SCRIPTED_SEQUENCE`), `flags`@8, `activity`@12 (`-1` until the game
DLL maps it), `numevents`/`eventindex`@20/24 (anim events), bbox@28, `numblends`@52,
then **`short anim[16][16]`@56** (512B **inline** blend grid; `MAXSTUDIOBLENDS=16`, a
v2531 fixed-size divergence from modern Source's variable `blend[]` pointer), then a
**196-byte trailing region** (568..763: `paramindex[2]`, `fadein/out`, autolayers, IK
locks, keyvalues). The **fixed stride is 764B**, established by data: it is the unique
stride that resolves every sequence name across models of every size — jeanette
(22: `Jeanette_Line1_Col_E`…`ragdoll`), mingxiao (29: `run`…`mingXiao_death`),
`move_and_ranged` (602: `walk`…`holy_light_idle`); no other stride in [200,1400]
resolves the names on any model. On these NPCs the mapping is **1 seq ↔ 1 anim,
blends=1** (`anim[0][0]` → local anim), so a first pass reads
`seq → anim[0][0] → animdesc` and skips blend interpolation.

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
bones with **partial** rotation animation — e.g. a clavicle with only the `w`
channel animated must keep its bind `x/y/z`; adding a delta, or zeroing the
un-animated axes, snaps the shoulder and tears the mesh. Adding the bind quaternion
to *every* channel (as a naive "delta" reading suggests) double-counts the bind
orientation and throws the whole skeleton off.

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
the correct rest shoulder. The Godot exporter (`tools/mdl_gltf.py`) consumes this
decode verbatim.

## A.4a `BONEFLAG_ORIENTATION` (bone flag `0x2`) — the axis-permuted bone [VtMB — decompiled + data-verified]

One bone per biped — always **`Bip01 Spine1`** — carries `Flags & 0x2`
(`BONEFLAG_ORIENTATION`, @136). Its **animation** rotation is stored with a constant
120° axis-permutation post-composed into every keyframe: the decoded quaternion is
`q_real · Q_ORIENT`, where `Q_ORIENT = (-0.5,-0.5,-0.5,0.5)` (the X→Y→Z→X cycle). Its
**bind** quaternion carries no such permutation. Recover the real local rotation by
right-multiplying each animation-frame quaternion (after the §A.4 decode) by
`Q_ORIENT⁻¹ = (0.5,0.5,0.5,0.5)`:

```
q = normalize(decode §A.4)
if bone.flags & 0x2:  q = qmul(q, (0.5, 0.5, 0.5, 0.5))    # Hamilton product, strip the permutation
```

Left un-stripped, Spine1 sits ~110° off, and because it **parents the whole upper
body** (Spine1 → Spine2 → Neck → Head, and both clavicles), it throws the torso, head,
and arms while the legs — which branch off the pelvis *below* Spine1 — stay correct.
The constant is universal: verified on every character model that has an ORIENT bone
(brujah/gangrel/gangmember male+female), the same `(0.5,0.5,0.5,0.5)` recovers Spine1
from ~85–118° down to ~13–20° from bind.

**Game confirmation** (`client.dll`, Ghidra): the bone-matrix pass at `FUN_10091110`
iterates the bone array (stride `0xa0`=160, array at `studiohdr+0xf4`) and branches on
`(bone.parent < 0) || (*(byte*)(bone + 0x88) & 2)` — i.e. root bones and
`BONEFLAG_ORIENTATION` bones — into an angle re-decomposition (`AngleMatrix`
`FUN_1010a540` → `MatrixAngles` `FUN_10107eb0`), the runtime form of the axis swap.
VtMB's model tools (VAMPTools `Animation.cpp`) apply the equivalent fixed swap on FBX
export; the quaternion post-multiply above is the direct glTF-space equivalent.

## A.5 Skinning [data-verified]

All character models are `VertexListType==0` (SKINNED, 44B `StudioVertex`); the
per-vertex `BoneWeight` (`mdl_v2531.md`) *is* the skin: `byte Weight[3]`@0,
`short Bone[3]`@4, `byte NumBones`@10. **`NumBones` reads 0 on VtMB data — derive
the influence count from nonzero weights** (probe: jeanette 6389/6393 verts have
`NumBones==0`). Max **3 influences**, fits Godot's 4-weight skin directly:

```
for i in 0..2:
    if Weight[i] > 0:  influence(bone=Bone[i], weight=Weight[i]/255.0)
```

`Bone[i]` indexes the same `StudioBone` array; `poseToBone`@88 is the inverse-bind
matrix (or compose it from the parent-relative `pos`/`quat`). jeanette: 4132 verts
1-bone, 1761 2-bone, 500 3-bone; weights sum to 255.

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
602 seqs). So the decoder **must resolve include-models transitively and merge their
sequences** onto the NPC's own skeleton — an NPC's playable set is *own anims ∪
(recursively) all included banks*, keyed by bone name against a shared Biped skeleton.

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

---

# Part B — Brush-entity movers

Moving brush models (doors, buttons, spinners, elevators, trams), driven by the
Source `CBaseToggle` lineage compiled into `vampire.dll` and wired through the I/O
bus. All are authored at `(0,0,0)`; the entity `origin` is the spawn translation and
— for rotating doors — the **hinge** (`mdl_v2531.md`/`entity_io.md`).

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
through the audio layer (`audio_pipeline.md`), plus explicit `locked_sound`/
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
(`entity_io.md`), e.g. `"OnPressed" ",,,0,-1,FindPlayer().ClearActiveDisciplines(),"`.

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
- **Button — `CBaseButton`**: press-in (`speed`, `lip`-adjusted) → `TriggerAndWait`
  (fire `OnPressed`, hold `wait`s) → `ButtonReturn`/`ButtonBackHome` spring-back, or
  latch when `wait -1`.
- **Spinner — `CFuncRotating`**: continuous rotation (not a fixed arc);
  `SpinUp`/`SpinDown` accel/decel toward `maxspeed` gated by `fanfriction`;
  `HurtTouch` applies `dmg`; driven by `Start`/`Stop`/`SetSpeed`/`Reverse`.
- **Keyframe mover**: interpolate along the `mover_keyframe` `NextKey` linked-list at
  `speed`, firing `OnReached`/`OnReachedKeyframe` per node. Elevator: move to the
  requested `floorN` Z at `speed`, `OnReachFloorAny`/`OnReachFloorN` on arrival.

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

# Part C — Mapping to Godot 4

> **Godot-target mapping (reference).** Part C maps Parts A/B onto the read-only Godot prototype
> (`E:\dev\elysium`: `mdl_skel.py`/`mdl_gltf.py` glTF bake, `Skeleton3D`, `AnimatableBody3D`, the
> `GameScene` playback — the "shipped" statuses are the prototype's). It is porting reference, not
> the Unreal target — see `docs/rebuild-strategy.md` (§B3 movers, §B5 NPCs via glTFRuntime). The
> glTF-bake decision and the coordinate/skin/attachment facts carry over; only the runtime host
> changes. Parts A and B above are engine-neutral.

## C.1 Skeletal models → `Skeleton3D` + `Skin` + `AnimationPlayer`

Skeletal models need a **richer container than the world's runtime-OBJ path** — OBJ
carries no skeleton, skin, or animation. Two routes were considered; **route 1
shipped**:

1. **Offline-bake to glTF 2.0 (shipped).** `tools/mdl_skel.py` decodes the skeletal
   half (bones, skin, RLE animation tracks; §A) and `tools/mdl_gltf.py` writes one
   `.glb` per model: mesh + skin + `Skeleton3D` node hierarchy + inverse-bind matrices
   + **one named animation** (the clip named on the CLI — no sequence merge or
   shared-library resolution yet). Godot's glTF importer builds `Skeleton3D` + `Skin` +
   `AnimationPlayer` natively and handles the coordinate conversion — the least custom
   runtime code. The viewer loads it in `GameScene.SpawnGltfCharacter`
   (`GltfDocument.AppendFromFile` → `GenerateScene`), plays the clip, and lets the
   `LightRig` light the mesh (exported to `tools/out/npc/`, launched via `--gltf` or an
   animated-prop swap). `mdl_gltf.py` imports `mdl` for the shared material pipeline —
   it is a sibling module, not an extension of `mdl.py`.
2. **Runtime builder (not taken).** Python writes a skeletal sidecar (bones + skin +
   decoded animation tracks); C# builds `Skeleton3D.AddBone`/`SetBoneParent`/
   `SetBoneRest`, an `ArrayMesh` with `SurfaceTool.SetBones`/`SetWeights`, a `Skin`, and
   `Animation` resources under an `AnimationPlayer`. More code; the glTF route made it
   unnecessary.

**Coordinate care**: bone `pos`/`quat` are parent-relative in **Source** space —
apply the same `M: (x,y,z)→(x,z,−y)` basis change and `×0.0254` the props use
(`basis' = M·basis·M⁻¹`, `origin' = M·origin·0.0254`), which transforms the bind
quaternions consistently. Route 1 offloads this to the glTF importer.

**Skin**: `Weight[i]/255` (§A.5), 3 influences into Godot's 4-slot skin; inverse-bind
from `poseToBone`. **Attachments** (§A.6) → `BoneAttachment3D` for weapons/props.

## C.2 Movers → `AnimatableBody3D` + a parametric mover component

Brush geometry already exports (world/brush-entity meshes offset by `origin`;
`.ents`/`.hulls` carry the per-entity convex hulls). Per mover, spawn an
**`AnimatableBody3D`** (moves by transform and pushes the `CharacterBody3D` player
correctly) at `origin`, driven by a small mover component that reads the exported
keyvalues:

- **`func_door_rotating`** → rotate `distance°` about the `angles` axis around the
  hinge (`origin`) at `speed` °/s. **`func_door`** → translate `(bbox_extent − lip)`
  inches along `angles` at `speed` in/s. **`func_button`** → press-in + spring/latch.
  **`func_rotating`** → continuous spin (`AnimationPlayer` loop or `_Process`).
  **keyframe mover** → interpolate the `NextKey` path.
- The primitive is a **constant-velocity `LinearMove`/`AngularMove` toward a target
  that fires a `MoveDone` callback** (§B.4) — no easing. A door is one arc +
  `wait`-timer autoclose + blocked/crush; a button is press + `TriggerAndWait` +
  return/latch.

**Movers depend on the not-yet-built I/O runtime** (`entity_io.md`): `Open`/`Close`/
`Lock`/`Unlock` arrive as inputs, `OnFullyOpen`/`OnPressed`/`OnIn`/`OnOut` fire as
outputs (some with field-6 Python), and `+use` targets doors/buttons. So movers land
naturally **with the interaction/`+use` milestone**, not before it. VtMB plumbing the
component must honour: `soundgroup` → the open/close/move/stop sounds
(`audio_pipeline.md`); `linked_door` → drive the paired leaf; `use_override` → route
`+use` to a button; `StartHidden` → spawn `SOLID_NONE` + inert until `ScriptUnhide`;
`use_icon`/`locked_icon` → the reticle armed by `OnIn`/`OnOut`.

## C.3 Scope for Elysium

1. **Skeletal decode + glTF export** (§A) — **shipped** (`mdl_skel.py` + `mdl_gltf.py`,
   played back in `GameScene`); unblocks NPCs, animated props, and animals. The
   remaining engineering is include-model resolution (§A.7 — an NPC that idles from a
   shared library) and merging multiple sequences into one `.glb`; the self-contained
   clips work today.
2. **Movers** (§B) — geometry + parametric spec export now; runtime lands with the
   I/O/`+use` milestone (doors/buttons are the first thing `+use` acts on).
3. **Deferred**: procedural bones (jiggle/IK — `ProcType`≠0), root-motion
   `mstudiomovement_t`, blend spaces (`numblends`>1), anim events, `.phy` ragdoll
   (vphysics lump undecoded), and NPC AI/spawning (a separate subsystem).

## C.4 Provenance

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
