# Facial animation — `.mdl` v2531 flex data, eyeballs, and the `.lip` phoneme files

VtMB animates a face with **flexes**: per-vertex morph targets stored inside the `.mdl`,
driven by a small set of named *flex controllers*, which are themselves driven by an RPN
*flex-rule* per morph. Lipsync feeds those controllers from a plain-text **`.lip`** phoneme
document shipped beside each line of audio, through a per-character **`expressions/*.vfe`**
weight table. Eyes are a **second system that meets the flex rig from outside it**: every
character model carries `StudioEyeball` records, the server runs a gaze and blink behaviour,
and the renderer's eye pass aims the iris and writes the eyelid flexdescs back into the flex
weights the rules just computed.

Struct offsets are the `.mdl` v2531 layout (`docs/vtmb/mdl_v2531.md` holds the rest of the format);
the runtime semantics come from `Bin/StudioRender.dll` (imagebase `0x2C000000`) and
`Vampire/cl_dlls/client.dll` (imagebase `0x10000000`). The data half is surveyed by
**`research/tooling/probes/probe_facial.py`**.

Related: `docs/vtmb/choreographed_scenes.md` (the `.vcd` scenes that schedule the lines and carry
`flexanimations` tracks), `docs/vtmb/animation_and_movers.md` Part A (the skeletal half of the same
`.mdl`), `docs/vtmb/audio_pipeline.md` (the line audio a `.lip` is timed against).

## Inventory

| | Count |
|---|---:|
| `.mdl` read across the merged install | **4,444** (1 unreadable) |
| …carrying flex data | **201** |
| …carrying a `mstudiomouth_t` | **199** |
| …carrying `StudioEyeball` records | **301** (two per model) |
| `StudioFlex` records | **17,960** (17,846 compressed / 114 raw) |
| vertex-animation records | **3,314,219** |
| `.lip` phoneme files | **7,136** (31 with no sibling audio) |
| `expressions/` tables | **249 `.vfe` + 249 `.txt`** |

201 models is the whole speaking cast: 193 carry the identical 65-flexdesc / 44-controller /
60-rule rig, 2 carry 66 (`mercuriodamaged`), and the rest are one-off morph rigs
(`mingxiao_transformation` 21, `creation1_full` 4, four models with a single flex).

**The player is not in that cast.** All **59** `models/character/pc/**.mdl` carry
`NumFlexDescs` 0, as do all **21** `models/hands/**.mdl` first-person viewmodels — so the 56
clan bodies `clandoc000.txt` names (roadmap PL13) have no flexdescs, no
controllers, no rules and no `StudioFlex` records at all. The PC's face is authored only in the
NPC-model portraits VtMB shows elsewhere; on the body itself there is nothing to drive. So the
lipsync and expression layers (12.3, 12.5) apply to NPCs, and any PC facial performance would be
a remaster addition under `docs/project/remaster-direction.md`'s rule, not a reproduction.

**The PC's eyes are the exception, and they cut the other way.** 57 of those 59 player models
carry a full pair of `StudioEyeball` records. Eye aiming needs no flex data — it is a
renderer-side basis built from the record and the gaze target — so the player's irises track
exactly like an NPC's. What the PC cannot do is blink or shape a lid: the eye pass delivers its
lid result into a flexdesc, and a model with `NumFlexDescs` 0 has no flexdesc to receive it and
no morph behind it. A still face with live eyes is the faithful state.

## The studiohdr facial block

**The VAMPTools field walk is 8 bytes short from `NumFlexDescs` onward** — it places the
flex block at 336 where the file puts it at 344. Two ints sit between
`LocalAttachmentIndex`@332 and the flex block, and VAMPTools has neither. The offsets below
are the ones every array closes on: each array's span equals `count × stride` exactly, and
`SurfacePropIndex`@392 resolves to `"flesh"` on every character.

| Off | Field | Array element |
|---|---|---|
| 344 / 348 | `NumFlexDescs` / `FlexDescIndex` | `mstudioflexdesc_t`, **4 B** |
| 352 / 356 | `NumFlexControllers` / `FlexControllerIndex` | `mstudioflexcontroller_t`, **20 B** |
| 360 / 364 | `NumFlexRules` / `FlexRuleIndex` | `mstudioflexrule_t`, **12 B** |
| 368 / 372 | `NumIKChains` / `IKChainIndex` | |
| 376 / 380 | `NumMouths` / `MouthIndex` | `mstudiomouth_t`, **20 B** |
| 384 / 388 | `NumLocalPoseParameters` / `LocalPoseParamIndex` | 20 B |
| 392 | `SurfacePropIndex` | string index (`"flesh"`) |
| 404 / 408 | `NumIncludeModels` / `IncludeModelIndex` | 116 B (already in `docs/vtmb/mdl_v2531.md`) |

### `mstudioflexdesc_t` — 4 bytes

One int: a string index relative to the record. The name is the **FACS** name the flex
targets. Every rigged VtMB character ships the same 65, in the same order:

```
upper_right  upper_right_lowerer  upper_right_neutral  upper_right_raiser
lower_right  lower_right_lowerer  lower_right_neutral  lower_right_raiser
upper_left   upper_left_lowerer   upper_left_neutral   upper_left_raiser
lower_left   lower_left_lowerer   lower_left_neutral   lower_left_raiser
mouth  default
AU1R AU1L AU2R AU2L AU1AU2R AU1AU2L AU4R AU4L AU6R AU6L AU9R AU9L AU38
AU10R AU10L AU12R AU12L AU15 AU17R AU17L AU25R AU25L AU18R AU18L
AU22R AU22L AU20R AU20L AU32 AU24 AU31 AU26R AU26L AU27R AU27L
AU12AU25 AU10SR AU10SL AU27Z AU42 AU16
right_open left_open right_lip_suppressor left_lip_suppressor
right_corner_suppressor left_corner_suppressor
```

The first 16 are the four eyelid ramps (upper/lower × left/right, each with a
lowerer/neutral/raiser); `mouth` is the amplitude-driven jaw (below); the rest are Ekman
action units plus six jaw/lip helpers.

### `mstudioflexcontroller_t` — 20 bytes

| Off | Type | Field |
|---|---|---|
| 0 | int | `sztypeindex` (relative to the record) |
| 4 | int | `sznameindex` (relative to the record) |
| 8 | int | `localToGlobal` — **`-1` on disk**, remapped at load |
| 12 | float | `min` (0.0 on every shipped controller) |
| 16 | float | `max` (1.0 on every shipped controller) |

Both string indices are **relative to the record base**, not to the field. The 44 shipped
controllers are grouped by `type` into five families — `eyelid` (8), `brow` (6), `nose` (4),
`mouth` (11), `phoneme` (15) — and the whole-install `type` histogram adds only `morph` and
`wholeface`, which belong to the two transformation rigs:

| type | controllers |
|---|---|
| `eyelid` | `right_lid_raiser` `left_lid_raiser` `right_lid_tightener` `left_lid_tightener` `right_lid_droop` `left_lid_droop` `blink` `half_closed` |
| `brow` | `right_inner_raiser` `left_inner_raiser` `right_outer_raiser` `left_outer_raiser` `right_lowerer` `left_lowerer` |
| `nose` | `right_cheek_raiser` `left_cheek_raiser` `wrinkler` `dilator` |
| `mouth` | `right_upper_raiser` `left_upper_raiser` `right_corner_puller` `left_corner_puller` `corner_depressor` `chin_raiser` `smile` `right_sneer` `left_sneer` `wide_open` `lower_lip` |
| `phoneme` | `right_part` `left_part` `right_puckerer` `left_puckerer` `right_funneler` `left_funneler` `right_stretcher` `left_stretcher` `bite` `presser` `tightener` `jaw_clencher` `jaw_drop` `right_mouth_drop` `left_mouth_drop` |

The `phoneme` family is exactly the key set `expressions/phonemes.txt` writes to — the
lipsync surface. The rest is expression.

### `mstudioflexrule_t` — 12 bytes + an 8-byte op array

| Off | Type | Field |
|---|---|---|
| 0 | int | `flex` — the **flexdesc** this rule computes |
| 4 | int | `numops` |
| 8 | int | `opindex` — relative to the rule record |

Each op is 8 bytes: `int op` then a union read as `int index` or `float value`. The opcodes
are Source's `StudioFlexOp_t`, confirmed by replaying the shipped rules:

| code | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|---|---|---|---|---|---|---|---|
| | `CONST` | `FETCH1` | `FETCH2` | `ADD` | `SUB` | `MUL` | `DIV` |

Only those seven appear across all 201 rigs (`FETCH1` 27,523 · `MUL` 21,648 · `CONST` 17,553
· `SUB` 9,948 · `ADD` 7,800 · `FETCH2` 7,800 · `DIV` 1,755). The stack machine evaluates
left to right and its final value is the weight of the rule's flexdesc.

**The two fetches read different arrays.** `FETCH1`'s operand is a **flex-controller
index**. `FETCH2`'s is a **flexdesc index** — it reads `dest[index]`, the weight an *earlier
rule* already wrote. On `nines`, 14 of the 22 distinct `FETCH2` operands are ≥ 44 and so out
of range for the 44-entry controller array; `right_lip_suppressor` is computed by rule 29 and
read by rule 31. **Rules therefore evaluate in file order against the array they are filling**,
not against a snapshot, and a consumer that treats `FETCH2` as a second controller fetch reads
the wrong array and silently runs off the end of it.

**The `DIV` guard is required, not defensive.** `1 / right_open` is a shipped rule and
`right_open` is zero on a closed mouth, so without Source's `divisor > 0.0001 ? a / b : 0`
every downstream weight is an infinity at rest.

The shipped eyelid rules read straight back:

```
upper_right_raiser  = fc0 * (1 - fc4 * 0.8) * (1 - fc6)
upper_right_neutral = (1 - fc4 * 0.8) * (1 - fc0) * (1 - fc6)
upper_right_lowerer = fc6
```

with `fc0` = `right_lid_raiser`, `fc4` = `right_lid_droop`, `fc6` = `blink` — i.e. a raiser
suppressed by droop and cancelled by a blink, its neutral the complement, and the lowerer
driven by blink alone.

### Three rules read the wrong side of the face, and it is authored

The rig is not left-right symmetric, and the asymmetry is not per-character damage: it is
identical on **85 of 85** exported rigs, so it is a property of the one shared template.

| Rule | Reads | Effect |
|---|---|---|
| `left_open` | `right_part`, `right_puckerer`, `right_funneler` | operand-for-operand identical to `right_open` |
| `AU1AU2L` | `right_inner_raiser`, `right_outer_raiser` | operand-for-operand identical to `AU1AU2R` |
| `AU1AU2R` | `left_lowerer` | both brow-pair rules are cancelled by the **left** brow lowerer |

`left_open` is consumed by `left_lip_suppressor`, so that suppressor equals its right-hand
twin on every frame and the whole left lip chain — `AU25L`, `AU18L`, `AU22L`, and `AU17L`
through them — is scaled by how open the *right* side of the mouth is. A face driven by a
one-sided phoneme therefore moves both sides of the lip, and a brow raise is suppressed by
the opposite brow's lowerer.

**Reproduce it.** The behaviour is visible rather than inert — unlike `mouth`, every rule
here reaches a morph — so symmetrising the three rules changes what a face does and is a
Logic-layer change under `docs/project/remaster-direction.md`, not a decode fix. Nothing in
Elysium's replay special-cases them: the rules ship as data in the facial sidecar and are
evaluated as written.

**The four eyelid rules do not reach a morph by themselves — the eyeball record is the bridge,
and it is authored.** The flexdescs that carry eyelid *morphs* — `upper_right`, `lower_right`,
`upper_left`, `lower_left`, flexdescs 0/4/8/12 — carry no rule. The `_lowerer`/`_neutral`/
`_raiser` flexdescs the rules above compute carry no morph. `mstudioeyeball_t` connects the two,
and the wiring reads back exactly: on `jeanette` the right eye's record names
`upperflexdesc = {1, 2, 3}`, `lowerflexdesc = {5, 6, 7}`, `upperlidflexdesc = 0`,
`lowerlidflexdesc = 4`, and the left eye's `{9, 10, 11}` / `{13, 14, 15}` / `8` / `12`. The rule
outputs are the record's inputs and the morph-carrying flexdescs are its outputs.

The renderer closes it in `R_StudioEyeballPosition` (`StudioRender.dll` `0x2C0502C0`), per eye,
per frame, writing back into the same flex-weight array it was handed:

```
f   = Σ_{k<3} asin(uppertarget[k] / radius) × flexweight[upperflexdesc[k]]
upL = VectorIRotate(state.up,      boneToWorld)      // the lid basis, back in bone space
fwL = VectorIRotate(state.forward, boneToWorld)
v   = upL × (sin(f) × radius) + fwL × (cos(f) × radius)
flexweight[upperlidflexdesc] = dot(v, eyeball.up)    // and the same for the lower lid
```

**`uppertarget` / `lowertarget` are not angles.** They are linear offsets in eyeball units,
turned into an angle by `asin(target / radius)` — `jeanette`'s
`uppertarget = (-0.278, 0.207, 0.309)` and `lowertarget = (-0.386, -0.278, -0.139)` against
`radius = 0.5`. Reading them as radians produces a lid that moves, which is why the error is
easy to keep.

Two consequences for a consumer. The lid weight depends on the eye's *aim*, not only on the
controllers, because `state.forward` is built from the gaze target — a blink on a
looking-away eye lands differently from a blink on a centred one. And the eye pass runs
**after** the flex rules and **overwrites** flexdescs 0/4/8/12, so a port that evaluates rules
and stops has no lid motion, while one that runs them in the other order loses the rules'
contribution.

### `mstudiomouth_t` — 20 bytes

| Off | Type | Field |
|---|---|---|
| 0 | int | `bone` |
| 4 | Vector | `forward` |
| 16 | int | `flexdesc` |

One per rigged character (199 of 201), always pointing at the `mouth` flexdesc (index 16)
and at the jaw bone with `forward = (0, -1, 0)`. This is Source's audio-amplitude jaw: the
mouth flex is driven by the envelope of whatever the actor is saying, independently of the
phoneme track. In Source the order is `RunFlexRules` and then `ControlMouth`, so the amplitude
value overwrites whatever the rules computed for that flexdesc.

**But writing flexdesc 16 moves nothing in VtMB.** Across all 86 exported rigs it carries
**zero flex records** and is read by **zero `FETCH2` op**, while a rule *computes* it on 85 of
them as a weighted sum of the jaw and lip action units
(`AU27R × 0.5 + AU25R × 0.35 + AU12AU25 × 0.8 + …`). So `mouth` is a **read-out of how open the
face already is, not a driver of it** — the amplitude jaw has a slot to write into and no
geometry behind the slot.

### Divergence — Elysium bridges the jaw into the controller layer

**Faithful behaviour:** the amplitude value is written to flexdesc 16, where it moves nothing,
because no mesh carries a flex record for it.

**The divergence:** the same weight is *raised* into the `jaw_drop` controller, which the
shipped rules turn into AU26/AU27 — 85 of 86 rigs carry that controller and all 84 that deform
anything carry an AU26/AU27 morph. Raised rather than assigned, so an expression already
holding the jaw wider keeps its own value. The faithful write is kept beside it and is
inspectable; only the bridge makes a mouth move.

**A second divergence rides on it, for the theatre specifically.** Of the 30 map-placed scene
files the exported maps resolve, 16 carry a `silence`/`loud` envelope and 14 do not —
**including all eleven of `sp_theatre`'s**, whose 21 `speak` events would otherwise leave the
courtroom stone-jawed. The per-line `.vcd` beside each of those lines does carry one, cut from
the same wav by the same tool, so Elysium reads the envelope out of the line file and places it
at the event's authored start. Retail's courtroom jaw comes from `mstudiomouth_t` driven live
off the playing sample by the sound engine, which this path does not reproduce: **the outcome
matches and the source of the envelope does not.**

### The read-out pattern

`mouth` and the eyelid flexdescs look alike in the rig and are not alike in the engine, and the
contrast is the fact worth carrying. Both are slots an engine-side driver is expected to meet:
the eyelid morphs carry no rule, and `mouth` carries no morph. The eyelid slots **are** met —
`mstudioeyeball_t` is authored on every character and the eye pass writes them every frame
(above). `mouth` is not: nothing in the shipped data or in `StudioRender` puts geometry behind
flexdesc 16.

So a reproduction that writes the amplitude value into `mouth` is inert, and an implementation
that makes the jaw move is bridging a gap the data leaves open — record that as a divergence
rather than as a decode. The eyelid case carries no such caveat; it is a plain decode of an
authored record.

## `StudioModel` — two corrections the flex walk forced

Both are properties of the *containing* struct and belong in `docs/vtmb/mdl_v2531.md`:

- **`StudioModel` is 224 bytes, not ~160.** Measured off every multi-model bodypart in the
  install (`mingxiao`, `cashbox`, `flashlight`, `jewelery_box`, `doorknob_round_brass`,
  …): consecutive bodyparts' model arrays are exactly `NumModels × 224` apart. Single-model
  bodyparts — 4,423 of the 4,444 models — are unaffected, which is why the 160-byte stride
  in `mdl.py` has never produced a visible fault; a bodygroup's second and later models are
  the only thing it mis-reads.
- **The model carries its own de-quantization pair at +0xA0…+0xB4.** `StudioRender`'s vertex
  accessor reads `pos = u16 * scale + offset` with `offset` at +0xA0/0xA4/0xA8 and `scale`
  at +0xAC/0xB0/0xB4, *not* from the header hull. On disk they hold `HullMin` and
  `span/65535` for UNSKINNED models and `(0,0,0)` / `(1,1,1)` for SKINNED ones, so
  `docs/vtmb/mdl_v2531.md`'s hull-interpolation rule is numerically identical — but the per-model
  fields are the authoritative source.

## `StudioFlex` — 32 bytes, arrayed per mesh

`StudioMesh.NumFlexes`@16 / `FlexIndex`@20; **`FlexIndex` is relative to the mesh record**.

| Off | Type | Field |
|---|---|---|
| 0 | int | `FlexDesc` — the morph this flex feeds |
| 4 | float | `Target0` |
| 8 | float | `Target1` |
| 12 | float | `Target2` |
| 16 | float | `Target3` |
| 20 | int | `NumVerts` |
| 24 | int | `VertIndex` — **relative to the flex record** |
| 28 | int | `VertAnimType` — **1 = compressed (8 B), 0 = raw (20 B)** |

The four targets are the trapezoid the flexdesc's weight is remapped through, exactly as
`R_StudioFlexVerts` evaluates it (`StudioRender.dll` `0x2C0508E0`):

```
w = flexweight[FlexDesc]
if w <= Target0 or w >= Target3:  skip this flex
elif w < Target1:                 w = (w - Target0) / (Target1 - Target0)
elif w > Target2:                 w = (Target3 - w) / (Target3 - Target2)
else:                             w = 1.0
```

`(0, 1, 10, 11)` is the ordinary "ramp in over 0…1, then hold" shape. The eyelid flexes use
the sentinel pairs `(-11, -10, a, b)` and `(a, b, 10, 11)` to make one flexdesc split into a
lower half and an upper half around a hinge value.

Flex records and their vertex-animation blocks are laid out **strictly contiguously**: a
mesh's flex array is followed by each flex's vertanim block in order, and
`flexbase[i] + VertIndex[i] + NumVerts[i]*stride == flexbase[i+1] + VertIndex[i+1]` on all
17,960 records with no exceptions.

## `StudioVertAnim` — the two encodings

### Type 1 — compressed, 8 bytes (17,846 of 17,960 flexes)

| Off | Type | Field |
|---|---|---|
| 0 | u16 | vertex index, **mesh-local** (add `StudioMesh.VertexOffset`) |
| 2 | u16 | position-delta **direction** — a byte offset into the unit-vector table |
| 4 | u16 | normal-delta **direction** — same table |
| 6 | byte | position-delta magnitude |
| 7 | byte | normal-delta magnitude |

**This record stores directions, not deltas.** The two `u16` are byte offsets into a
5,314-entry table of unit vectors compiled into `StudioRender.dll` at `0x2C06E008` — the
same table `StudioVertex2`'s "packed normal" slot indexes. Both are multiples of 12 (the
`Vector` stride) on all 3.3 M records, and the largest observed value, 63,756, is exactly
entry 5,313. The magnitudes go through the shared `n/255` float table at `0x2C06C530`:

```
delta  = anorms[u16@2 / 12] * (byte@6 / 255) * 8.0     # 0x2C06C4FC
ndelta = anorms[u16@4 / 12] * (byte@7 / 255) * 2.0
pos   += w * delta          # w = the ramped flex weight above
norm  += w * ndelta
```

so a compressed position delta is capped at 8 inches and quantized to 8/255 ≈ 0.031 in per
step along a quantized direction. Measured across the whole cast: p50 **0.063 in**, p90
**0.345 in**, p99 **1.13 in**, max **5.90 in** — face-scale, as expected.

This is a **VtMB-specific encoding**. Modern Source stores `short delta[3]` + `short
ndelta[3]` in a 16-byte record, and neither VAMPTools nor Crowbar recovered VtMB's form:
Crowbar's `SourceMdlVertAnim2531` reads it as `short delta[3]` and its exporter carries a
column of commented-out scale guesses ending at `/ 327670` marked *"seems closest to being
correct"*. Reading it that way produces a direction-table byte offset scaled as a
displacement, which is why it never converged.

### Type 0 — raw, 20 bytes (114 flexes, all in `mingxiao_transformation.mdl`)

| Off | Type | Field |
|---|---|---|
| 0 | u16 | vertex index (mesh-local) |
| 2 | — | pad (Vector alignment) |
| 4 | Vector | position delta, **inches, uncompressed** |
| 16 | u16 | normal-delta direction (table byte offset) |
| 18 | byte | normal-delta magnitude |
| 19 | — | pad |

The position delta is applied as `pos += w * delta` with no scaling. Ming Xiao's
transformation is a whole-body morph across 19 frames of 2,650 vertices each, with deltas up
to 121 inches — far outside the compressed record's 8-inch ceiling, which is presumably why
the format exists at all.

## Eyes — the record, the renderer pass, and the gaze behaviour

VtMB's eyes are a complete engine-side system in three parts, all reproducible: a
`StudioEyeball` record per eye in the model, a renderer pass that turns a gaze point into iris
and glint texture coordinates, and a server-side behaviour that chooses where to look and when
to blink. The flex rig meets it only at the eyelids (above).

### `StudioEyeball` — 140 bytes, two per character

`StudioModel.NumEyeballs`@**192** / `EyeballIndex`@**196**. The offsets are confirmed by the
consumer: the eye pass indexes `model_base + *(int*)(model_base + 0xC4) + materialparam * 0x8C`.

| Off | Type | Field | Note |
|---|---|---|---|
| 0 | int | `nameindex` | 0 on every shipped record |
| 4 | int | `bone` | the head bone |
| 8 | Vector | `org` | eye centre, bone-local inches; the pair mirrors in Z |
| 20 | float | `zoffset` | sideways shift, applied as `forward += right × 2·zoffset` |
| 24 | float | `radius` | `0.5` on every shipped record |
| 28 | Vector | `up` | bone-local, orthonormal with `forward` |
| 40 | Vector | `forward` | the authored resting aim |
| 52 | int | `texture` | **never read by the renderer** (Source's `texture`) |
| 56 | int | `iris_material` | **never read** (Source's `unused1`) |
| 60 | float | `iris_scale` | per character; enters as `1/(1/iris_scale + fEyeSize)` |
| 64 | int | `glint_material` | **never read** (Source's `unused2`) |
| 68 | int[3] | `upperflexdesc` | the upper lid's three rule outputs |
| 80 | int[3] | `lowerflexdesc` | the lower lid's three |
| 92 | float[3] | `uppertarget` | linear offsets, **not radians** — see the eyelid bridge above |
| 104 | float[3] | `lowertarget` | |
| 116 | int | `upperlidflexdesc` | the morph-carrying flexdesc the pass writes |
| 120 | int | `lowerlidflexdesc` | |
| 124 | — | 16 bytes | **never read** (Source's `unused[4]`) |

The three material-index fields hold real texture-table indices — `jeanette`'s resolve to
`Eyeball_l` / `Eyeball_r` and `glint`, LaCroix's and Nines' to `Pupil_r` / `Pupil_l` — and
`StudioRender` reads none of them. The material actually bound is the eye mesh's ordinary
skinref material, and the iris and glint textures come from the `.vmt`'s `$iris` and `$glint`.
The 16 bytes at +124 are read by nothing either, so **there is no data-driven gaze limit**: the
only shaping in the data is `zoffset`, and the only switch is the renderer config's `bEyeMove`.

**Census: 301 models carry records, two each.** 298 sit under `models/character/` (221 `npc`,
57 `pc`, 15 `shared`, 5 `monster`); the other three are a cinematic `sewer_guard` and the two
`handleclaws` wield models. Models without them are gibs, props and scenery.

The eyes are also authored as their own geometry and material, independently of the record:
**400 `eyeball_l.vmt` / `eyeball_r.vmt` across 199 character directories**, 394 on the `Eyes`
shader and 4 on `VertexLitGeneric`, over a shared iris palette at
`materials/models/character/eyes/`.

### The eye meshes are flagged for the pass

`StudioMesh.materialtype`@24 is **1** on an eyeball mesh, and `materialparam`@28 selects which
eyeball — **598 such meshes over 299 models**, `materialparam` ∈ {0, 1}. `StudioRender`'s
hardware draw loop dispatches on that field alone: `materialtype == 1` takes the eye path,
everything else the ordinary mesh path.

### The renderer pass

`R_StudioEyeballPosition` (`0x2C0502C0`) runs once per eyeball per frame, *before* the mesh
loop, into an `eyeballstate_t` (112 B at `CStudioRender + 0x288 + i × 0x70`):

```
o = eb.org;  o += sign(o) × cfg.fEyeShift          // per component
state.org = VectorTransform(o, boneToWorld[eb.bone])
state.up  = VectorRotate(eb.up, boneToWorld)

state.forward = normalize(cfg.m_ViewTarget − state.org)      // the gaze point
if (!cfg.bEyeMove) state.forward = −VectorRotate(eb.forward, boneToWorld)

state.right   = normalize(cross(state.forward, state.up))
state.forward = normalize(state.forward + state.right × (2 × eb.zoffset))
state.right   = normalize(cross(state.forward, state.up))
state.up      = normalize(cross(state.right, state.forward))

s = 1/eb.iris_scale + cfg.fEyeSize;  if (s > 0) s = 1/s;  s = −s
state.irisU.xyz = s × state.right;  state.irisU.w = 0.5 − dot(state.irisU.xyz, state.org)
state.irisV.xyz = s × state.up;     state.irisV.w = 0.5 − dot(state.irisV.xyz, state.org)
```

The planes are consumed as `u = dot(worldPos, irisU.xyz) + irisU.w`. Two details a port has to
keep: `$eyeorigin` is recomputed **without** the eye shift while `state.org` (which the plane
`w` terms use) includes it, and `$eyeup` is the **bone-local** `eb.up` halved, not `state.up`.
The same function writes the eyelid flexdescs back (the eyelid bridge above).

### The `Eyes` shader

`stdshader_dx8.dll` only — `stdshader_dx9.dll` has no eye shader. Params: `$IRIS`,
`$IRISFRAME`, `$GLINT`, `$EYEORIGIN`, `$EYEUP`, `$IRISU`, `$IRISV`, `$GLINTU`, `$GLINTV`, and
`$VAMPIRE` (*"Turn on to get whatever vampire-eye effect we use"*). The compiled programs live
in the VPKs at `shaders/vsh/*.vcs` and `shaders/psh/*.vcs`, resolved by name as
`shaders\<vsh|psh|fxc>\<name>.vcs`; `materials/dxshaders/*.psh` ships the readable ps1.1
sources for all but the two vampire variants.

The vertex program is always `Eyes`; the pixel program is chosen per material:

| `$vampire` | config overbright | pixel program |
|---|---|---|
| 0 | ≠ 2.0 | `Eyes` |
| 1 | ≠ 2.0 | `Eyes_Vampire` |
| 0 | 2.0 | `Eyes_Overbright2` |
| 1 | 2.0 | `Eyes_Vampire_Overbright2` |

**The vampire variant reorders the lighting modulate against the iris composite.** Stock is
`((sclera lerp iris) × illumination) + glint`; vampire is
`((sclera × illumination) lerp iris) + glint`, so the iris texel is never multiplied by scene
lighting and renders at full texture value however dark the room is — a self-illuminated iris
over a normally lit sclera, blended by the iris texture's own alpha. Alpha comes from the
sclera in both. **12 shipped materials set `$vampire 1`**, the Sheriff among them. No shipped
material sets `$glint` or `$irisframe`.

In the vertex program, `c44`–`c47` are the iris and glint planes, applied as
`oT1.xy = dot4(c44/c45, worldPos)` and `oT2.xy = dot4(c46/c47, worldPos)`; `oT0` is the raw
mesh UV for the sclera. `c42`/`c43` (`$EYEORIGIN`/`$EYEUP`) build the shading normal instead,
`n = normalize((P − eyeOrigin) − 0.5 × dot(P − eyeOrigin, eyeUp) × eyeUp)` — half the up
component is removed, flattening the sphere normal so the eyeball does not go dark top and
bottom.

### The glint

`$GLINT` is a **32×32 BGRA procedural texture** regenerated per eye per frame, never a shipped
file. Its planes come from `2 × eb.radius` and the *view* right/up rather than the eye basis.
The generator takes at most the **first two lights**, and per light splats up to two points: a
cornea highlight at `(2/3)·radius` along the half-angle vector, and a sclera one at `radius`,
the latter only when the light is more than 30° off the eye's axis. Each splat is a **1-to-4
texel bilinear dot** with a `≥ 0.25` energy gate, written through a 1024-entry linear→texture
gamma LUT. The softness is bilinear magnification of 32×32 across the eyeball, not a falloff
kernel.

Two properties are as-compiled rather than evidently intended, and a port that "fixes" either
will not match: the second sample reuses the first sample's half-angle vector, and the
accumulator is allocated `w·h·16` bytes, indexed as `w·h·12`, and cleared only `w·h·4` — so two
thirds of it carries over between regenerations. `u`/`v` are world-space lengths consumed as
normalized `[-0.5, +0.5]` texture offsets, which makes the eyeball radius the effective
lobe-size constant.

#### Divergence — Elysium has no glint node

**Owner call, Presentation layer.** The procedural splat is not reproduced. `M_Eyes` carries no
glint; the highlight comes from UE specular off the flattened eye normal, lit by the same HWRT
Lumen and MegaLights the rest of the scene uses.

The faithful behaviour is above. The call rests on *what the splat was for*: a 32×32 per-eye
per-frame texture that samples at most two lights is a fix for a renderer with no per-pixel
specular, not an artist's decision about how an eye should read. No shipped material sets
`$glint`, so nothing in the corpus depends on it either. Reproducing it would mean carrying the
two as-compiled defects above — the reused half-angle vector and the two-thirds-stale
accumulator — into a renderer that does not need the technique at all.

### Gaze — the server behaviour

State lives on `CBaseCombatCharacter` (`vampire.dll`): `m_vEyeLookTarget`@0x0E44 (commanded),
`m_vCurEyeTarget`@0x0E50 (smoothed), `m_hEyeLookTarget`@0x0E64, `m_flEyeIntegRate`@0x0E3C,
plus a scripted-mode int at **0x0E68 that the datamap does not carry** — so a scripted look-at
does not survive a save.

**Every timing and rate constant is content**, read from `vdata/System/DispositionTable.txt`
(the compiled-in fallbacks sit in BSS and are zero); the `Neutral` block seeds the global
defaults, so every other disposition inherits from it. Retail values:

| Disposition | Fidget points | Hold min/max | Eye turn rate |
|---|---|---|---|
| Neutral | `[-1,-1,-1]` | .15 / .25 | 0.3 |
| Anger, PrinceSitting | `[0,2,0]` | .25 / 1 | 0.95 |
| Disgust | `[0,2,3]` | .15 / .45 | 0.6 |
| Apathy | `[7,8,9]` | 1 / 2 | 0.2 |
| Confused | `[7,5,9]` | .15 / .25 | 0.2 |

Disposition-level and therefore global: blink interval **2.5–6.0 s**, eye turn rate **0.9**,
fidget interval **5–8 s**.

**Selection** is a priority cascade — dialogue partner, target entity, enemy, navigation goal,
heard sound, then an autonomous scan — with every candidate gated by
`dot(headForward, normalize(p − headPos)) > 0.866`, a **±30° cone** off the live head bone. The
navigation-goal and sound arms apply the target directly and bypass smoothing. The autonomous
scan sweeps a 300-unit sphere centred 300 units ahead of the eyes, keeps the nearest survivor,
and re-picks after `RandomInt(1, 5)` seconds; finding nothing it looks straight ahead at
`eyePos + BodyDirection2D() × 500` and retries in 0.5 s. The candidate filter is
`entity->+0x94 != 0 || (GetFlags() & FL_CLIENT)`, so **the player always qualifies**.

**`+0x94` is not recovered.** It is a `CBaseEntity` field the filter tests as a boolean and
nothing else in the decompilation names, so what makes a *non-player* entity worth looking at is
unknown. The `FL_CLIENT` half stands on its own and is exact.

> **Divergence — Elysium's scan admits the player and combat characters.** With `+0x94` unknown,
> the runtime treats the recovered half as the whole rule and adds characters, which is the
> smallest set that makes the behaviour observable at all. Widening it is a content decision, not
> a maths one, and it is the one place in the cascade where the shipped candidate set cannot be
> matched until the field is identified. Everything else in the cascade is reproduced.

In dialogue the NPC looks at the partner's `EyePosition()` — eye height on the entity, not an
attachment or a head bone. A dialogue **camera shot can redirect it**: when the shot's flags
carry bit `0x10`, gaze swaps to a third entity, which couples eye direction to the cinematic
camera.

**Fidget** is the saccade layer. Once the eyes converge to within a unit of the target, the
character holds for the disposition's interval, then walks a **three-step sequence of
head-relative keypad cells** — Troika's own comment: *"eye targets are head relative, with the
numbers being like the numbers on a keypad; 0 will have the NPC fall back to normal look
behavior."* Cell 5 is centre, each step is ±20° of pitch and yaw projected 25 units out, held
for the disposition's hold range, and a `Fidget Points` triple of `[-1,-1,-1]` means pick a
random cell 1–9 instead. Exhausting the sequence restores the default direction.

**Integration** is a fixed-timestep lerp, not a rate: per 0.1 s of accumulated interval,
`m_vCurEyeTarget += rate × (m_vEyeLookTarget − m_vCurEyeTarget)`. The head is given the
*commanded* target and the eyes the *smoothed* one.

**Head turn is inert in retail.** `m_flHeadYaw`/`m_flHeadPitch` are integrated every think
through a `0.8 / 0.2` filter and applied with `CBaseAnimating::SetBoneController(0, …)` and
`(1, …)` — **bone controllers, not pose parameters**. No shipped model declares a single bone
controller (`docs/vtmb/mdl_v2531.md`), so the lookup fails, the value never reaches the
skeleton, and the filter runs unclamped apart from a `> 360 → 0` guard. Visible head movement
in VtMB dialogue is animation and choreography, not this path. The *input* side is live: the
gaze cone and the fidget grid are computed in the real animated head-bone frame, with a
fallback to `EyePosition()`/`EyeAngles()` when a model has no head bone.

Blink is likewise server-side only as a cadence: `Blink()` is a single networked toggle, and
the envelope that animates it is client-side (below).

### The scripted look-at inputs

Four `CBaseCombatCharacter` inputs resolve a named entity and store it with a mode:

| Input | Mode pushed | Aim point |
|---|---|---|
| `LookAtEntityEye` | 1 | `EyePosition()` |
| `LookAtEntityCenter` | **1** | `EyePosition()` — the handler pushes the Eye constant |
| `LookAtEntityOrigin` | 3 | `GetAbsOrigin()` |
| `LookAtEntityDefault` | 0 | clears the target, restoring autonomous behaviour |

**`LookAtEntityCenter` is a shipped defect.** Mode 2 resolves `WorldSpaceCenter()`, and no
handler ever passes 2, so all 10 authored `LookAtEntityCenter` firings behave exactly as
`LookAtEntityEye`. A scripted gaze still passes the ±30° cone test and falls back to straight
ahead outside it, and it yields back to autonomous automatically if the target entity goes
away.

Shipped maps fire these 40 times across 15 of the 101 maps. `sp_theatre` wires the intro both
ways: on one keyframe the player is aimed at `prince1` while `Prince1` is released to
autonomous, and on a later keyframe `Prince1` is aimed at `!playercontroller`.

### The networked hop

Two values leave the server: `m_viewtarget` (the smoothed gaze point) and `m_blinktoggle`, both
on `CBaseEntity` and both in the `DT_BaseFlex` table. The client interpolates `m_viewtarget`
against its previous value and hands it to `IStudioRender::SetEyeViewTarget`, which parks it at
`CStudioRender+0x78` — the `m_ViewTarget` the eye pass reads. `m_blinktoggle` drives the blink
envelope in `C_BaseFlex::SetupWeights` (below).

`GetEyeballs` exists server-side and walks the records correctly, but has no callers and sits
in no vftable; the server reasons about eye *positions* only through `EyePosition()`.
`sv_draw_eye_orientation` is registered with no reader — the visualiser is compiled out of the
retail build. The live debug surface is `npc_disposition`, `dump_disposition_table`, and an NPC
overlay printing the current, default and step eye targets.

## The unit-vector table

`0x2C06E008` in `StudioRender.dll`: 5,314 `Vector` entries, 99.8 % unit length, beginning
`(0,0,-1)`, `(0.04907,0,-0.99880)`, `(0.03470,0.03470,-0.99880)` … — a spherical
quantization in the Quake/Source `anorms` tradition, but far finer than Quake's 162.
Indexed by **byte offset**, not by index. It is used by three things:

| Consumer | Slot |
|---|---|
| `StudioVertex2` (UNSKINNED, 12 B) | the u16 at +6 |
| `StudioVertAnim` type 1 | the u16 at +2 and +4 |
| `StudioVertAnim` type 0 | the u16 at +16 |

`StudioVertex3` (COMPRESSED, 8 B) uses a **different** table — `0x2C06D358`, indexed by
`byte × 16`. This corrects `docs/vtmb/mdl_v2531.md`, which records both packed-normal slots as
"unused; regenerate from geometry": they are exact, they simply live in the renderer rather
than the model. `research/tooling/probes/probe_facial.py --anorms <out>` extracts the table from the user's
own DLL (nothing table-derived is committed — same bring-your-own posture as every other
game-sourced byte).

## `.lip` — the phoneme document

A `.lip` sits beside its line audio with the extension swapped (`line431_col_e.mp3` →
`line431_col_e.lip`); 7,105 of the 7,136 have a sibling `.wav`/`.mp3`. It is **plain text**,
CRLF, and the whole corpus parses on one grammar:

```
VERSION 1.2
PLAINTEXT
{
Out, devil!
}
WORDS
{
WORD Out 0.140 0.578
{
593 aw 0.140 0.437 1.000 0
116 t 0.437 0.578 1.000 0
}
WORD devil 0.702 1.155
{ … }
}
EMPHASIS
{
}
CLOSECAPTION
{
english
{
PHRASE char 13 "Out, devil!" 0.140 1.155
}
}
OPTIONS
{
voice_duck 1
speaker_name Bach
}
```

| Element | Shape | Corpus |
|---|---|---|
| `VERSION` | `1.0` / `1.1` / `1.2` | 157 / 501 / 6,478 |
| line 2 | always `PLAINTEXT` | 7,136 |
| `WORD` | `WORD <text> <start> <end>` (seconds) | 122,672 rows |
| phoneme row | `<code> <phoneme> <start> <end> <volume> [<flag>]` | 383,597 six-field / 11,331 five-field |
| `EMPHASIS` | present on every file, **always empty** | 0 rows |
| `CLOSECAPTION` | one language block, always `english` | 7,055 files |
| `PHRASE` | `PHRASE char <n> "<text>" <start> <end>` | |
| `OPTIONS` | `voice_duck <0\|1>` (all), `speaker_name <name>` (7,074) | |

Times are seconds from the start of the audio. Words are ordered, phonemes are ordered
within a word, and gaps between words are silence. Lines run p50 **4.13 s** / p90 14.2 s /
max 445 s, with p50 11 words.

**59 distinct phoneme strings** appear; the head is the ordinary ARPABET-ish set (`ax` `t`
`n` `s` `ih` `iy` `d` `r` `l` `m` `ah` `k` `eh` `ay` `uw` `ae` `dh` `w` `ao` `z` …) plus
`<sil>` (481) and a small tail of authoring junk (`???`, `AH`, `L`, `W`, `D`, and `aa2`-style
duplicates).

**The leading integer is the key, and the phoneme string is not.** The integer is the code point
of the phoneme's IPA form — ASCII for the single-letter ones (`t`=116, `n`=110, `s`=115,
`d`=100, `l`=108, `m`=109, `k`=107, `w`=119, `z`=122, `b`=98, `f`=102, `p`=112, `v`=118,
`g`=103) and the Unicode scalar for the rest (`0x0259`=601, `0x026a`=618, `0x028c`=652,
`0x025b`=603, `0x0251`=593, `0x00e6`=230, `0x00f0`=240, `0x0254`=596, `0x025a`=602). That is
exactly the `expressions/` table's **class** column, which is how a code resolves to a row.

`client.dll` settles it: `FUN_100c4940` takes the code as an integer, bounds-checks it against
the table's count at `+0x98`, and indexes the table's own code→row array at `+0x9c`. **No string
is involved at any point.**

The corpus agrees and the string does not survive contact with it. Across all 7,136 files there
are **48 distinct codes** but **555 distinct (code, string) pairs**:

| `.lip` string | code | row it actually names | uses |
|---|---|---|---|
| `k` | 107 | `c` | 12,444 |
| `ay` | 593 | `aa` | 10,221 |
| `ng` | 331 | `nx` | 4,073 |
| `h` | 104 | `hh` | 3,174 |
| `ax` | 618 / 603 / 652 / 106 / 117 / 105 / 111 / 601 / 593 | `ih` / `eh` / `ah` / `y` / `uw` / `iy` / `ow` / `ax` / `aa` | 4,000+ |

So `ax` alone appears under nine codes naming nine different rows, and the four commonest
strings in the corpus name a row of a different name every time. **Match on the code.** Every
one of the 48 resolves against 122 of the 126 shipped phoneme tables; the four that fall short
(`larry`, `shu`, `phonemes_strong`, `phonemes_weak`) carry a single row each and are degenerate.

**A word's text may begin or end with a quotation mark.** Faceposer writes the caption's own
punctuation into it — `WORD "I'll 0.130 0.190`, `WORD elevator." 0.429 1.028` — so a tokenizer
that treats `"` as an opening quote swallows the line and drops that word's phonemes. It happens
in **1,159 files**, one word each, and the file otherwise parses. The only quoted content in the
format is the `PHRASE` line. Split on whitespace alone.

A word's text may also contain the CP-1252 ellipsis `0x85` (`WORD man…" 10.000 10.336`, seven
files). It is part of the word, not a separator — a splitter that treats U+0085 as whitespace
breaks those rows in two and reads the wrong field as a time.

Parsed that way the corpus yields **394,923 phoneme rows** over 7,136 files, with exactly **one**
malformed row — `WORD Come on 0.048 0.400`, whose word text carries a real space — and 92 files
carrying a placeholder word with no phonemes at all.

The trailing `volume` is 1.000 on effectively every row and the sixth field is 0; version 1.0
and 1.1 files omit the sixth field. **Neither is consumed.** The accumulate call takes only the
phoneme code, the envelope's `scale` and the two tables (`FUN_100c4940`'s argument list), so a
row's volume never reaches a weight.

## `expressions/` — phoneme → flex-controller weights

`expressions/` holds **249 `.vfe`** (Faceposer's compiled form) and the identical 249
`.txt` sources. The `.txt` is readable and is the useful one:

```
$keys right_cheek_raiser left_cheek_raiser wrinkler right_upper_raiser … lower_lip
$hasweighting
"b" "b" 0.000 0.000 … 0.350 1.000 … 0.330 1.000 … "Big : voiced alveolar stop"
"m" "m" 0.000 0.000 … 0.340 1.000 … 0.590 1.000 … "Mat : voiced bilabial nasal"
```

The first quoted word is the row's name — what an `expression` event's `param2` selects. The
second is its **class**, and on a phoneme table that is the row's key: a single character or a
`0x….` scalar whose code point is what a `.lip` row's leading integer holds (`"c" "k"` is
reached by 107, `"nx" "0x014b"` by 331). On an expression table the class is `_` on every row
and only the name is usable.

`$keys` names the flex controllers the table writes; `$hasweighting` (set on all 249) means
each row carries **two floats per key** — the value and its weight — so a row is
`2 × len($keys)` numbers between the two quoted names and the trailing description. Key sets
run 22–33 entries (the modal 30), rows 32 or 48 per table; 48 distinct key names appear
across the set, all drawn from the `phoneme` and `mouth` controller families.

**The file is chosen by the actor's model.** `client.dll` `FUN_100C4210` formats
`"expressions/%s_%s.vfe"` from the basename of the entity's model path (stripped of
directory and extension by `FUN_100C4270`) and a class string — `"phonemes"` at
`FUN_100C42F0`. So `models/…/lacroix.mdl` loads `expressions/lacroix_phonemes.vfe`, and the
same mechanism yields `<stem>_expressions.vfe`. Of the 249 files, 121 are `_phonemes`, 121
`_expressions` and 7 unsuffixed; **238 of the 242 suffixed stems match a shipped model
stem**. `expressions/phonemes.vfe` and `expressions/phonemes_male.vfe` are the fallbacks
`client.dll` names literally.

### The compiled `.vfe` header

Every `.vfe` has a `.txt` twin, so the pipeline reads the text and nothing needs the binary
form. The header is recorded because it dates the toolchain and because a reader ported from
modern Source mis-walks it silently:

| Off | Type | Field | Value |
|---|---|---|---|
| 0 | char[4] | `id` | `EFV\0` on disk — Source's `('V'<<16)+('F'<<8)+'E'` little-endian |
| 4 | int | `version` | `0` on all 249 |
| 8 | char[128] | `name` | the file's own `expressions/…` path |
| 136 | int | `length` | file size — **exact on 247 of 249** |
| 140 | int | `numflexsettings` | 4–48, matching the `.txt`'s row count |

**`name` is `char[128]` where modern Source's `flexsettinghdr_t` has `char[64]`** — the same
widening `MDLHeader.Name` carries (`docs/vtmb/mdl_v2531.md`), so it is a property of Troika's
toolchain rather than of one format. A modern walk reads `length` at 72, which is `0` in
every shipped file.

`phonemes_strong.vfe` and `phonemes_weak.vfe` are the two exceptions: both are 1,260 bytes
with `0x3F800000` — float `1.0` — where `length` and `numflexsettings` belong, so they are a
different layout rather than a damaged one. They are two of the four degenerate tables the
`.lip` coverage above already sets aside.

**The internal name is authoring provenance, not a path.** On **13 of 249** it names a
different file than the one it ships as — `shu_phonemes.vfe` and `larry_phonemes.vfe` both
carry `larry_e3_phonemes.vfe`, `kiki_expressions.vfe` carries `mingxiao_expressions.vfe` —
so a table copied during authoring keeps the source's name. Nothing at runtime reads it: the
file is selected by the actor's model stem (above).

## The chain, end to end

```
.lip phoneme row  ──►  expressions/<model stem>_phonemes.vfe  ─┐  (accumulated, +=)
networked m_flexWeight[128]  ─────────────────────────────────┤  (lerped, then min/max remap)
Blink() toggle  ──►  the 0.3 s blink envelope  ───────────────┤  (assigned, no remap)
                                                              ▼
                       global flex-controller weights — g_flexweight[128]
                                                              │  mstudioflexrule_t RPN
                                                              ▼
                                        flexdesc weights (65)
                                                              │  SetFlexWeights
                                                              ▼
                    R_StudioEyeballPosition  ──►  overwrites flexdescs 0/4/8/12 (the lids)
                                                              │  StudioFlex target ramp
                                                              ▼
              per-mesh StudioVertAnim  ──►  pos += w·delta,  norm += w·ndelta

line audio envelope  ──►  mstudiomouth_t  ──►  the `mouth` flexdesc  (no geometry behind it)
```

`.vcd` scenes drive the same controllers directly through their `flexanimations` tracks
(`docs/vtmb/choreographed_scenes.md`), so expression and lipsync meet at the controller layer, not at
the vertex layer.

### `C_BaseFlex::SetupWeights` — the client-side order

The whole client half runs in one function, called by the engine **per drawn model per frame**,
so the face is evaluated at render time rather than on a think tick. The order is load-bearing:

1. zero `g_flexweight[128]` — a **global** controller index space, distinct from the model's own
   controller order; names are interned by `AddGlobalFlexController` into a 256-entry table;
2. `dt = (curtime − m_flAnimTime) × 10.0`, **unclamped in both directions**;
3. lazily fill each controller's `localToGlobal`, **mutating the shared loaded `studiohdr` in
   place** — per model file, not per instance;
4. per controller: lerp previous↔current by `dt` (or snap, when the entity's effects carry
   `0x10`), then remap through the controller's own `min`/`max`;
5. **blink** (below) — assigned *after* the loop, so it overwrites any networked `blink` value,
   and written raw, bypassing the `min`/`max` remap every other controller receives;
6. visemes accumulate additively from the `.vfe`: `g_flexweight[map[i]] += scale × value_i`, so
   lipsync, expression and blink share one surface. `scale` is the phoneme envelope below, and
   `value_i` is the row's **value** column — the `$hasweighting` influence beside it is **not
   applied** (`FUN_100c4940` walks the row at stride 12 as `{int key; float value; float
   influence}` and reads only the first two);
7. `RunFlexRules` → the flexdesc array — gated by the `flex_rules` ConVar, which when 0 returns
   **without zeroing the destination**, sending uninitialized stack to the renderer;
8. the view target is interpolated and pushed to the renderer;
9. `SetFlexWeights(count, weights)` — flexdesc index space, confirmed by the debug overlay,
   which walks `numflexdesc` printing each flexdesc name against the same array.

The blink envelope, with no ConVar behind it — the duration is a literal `0.3` (stock Source
uses 0.2):

```
on m_blinktoggle changing:  m_blinktime = curtime + 0.3
a = (m_blinktime − curtime) × 5.235987755982989       // (π/2) / 0.3
w = 0                                                  // outside the window
if (a > 0 and cos(a) > 0):
    w = 2 × sqrt(cos(a));  if (w > 1) w = 2 − w        // folded about 1
g_flexweight[blink] = w
```

With `u = (m_blinktime − curtime)/0.3` running 1 → 0, the lid is **fully closed 48.3 ms after
the toggle** and reopens over the remaining **251.7 ms** — a fast close and a slow open, 300 ms
in total. The cadence that drives the toggle is the server's, `RandomFloat(2.5, 6.0)` from the
disposition table.

The `eyes_updown` / `eyes_rightleft` names are interned as **flex controllers**, not pose
parameters, and nothing indexes the weight array by them — gaze reaches the renderer only
through the view target. A model that declared them would still have them driven by the
ordinary networked controller path.

### The phoneme envelope — step 6's `scale`

The viseme block is `0x100c463b–0x100c4730`. It walks the emitting entity's sentences, and each
phoneme row contributes over a window derived from its own duration:

```
S = clamp(end − start, studiohdr+232, studiohdr+236)     // FUN_100c3be0
t = (curtime − the speak event's start) − phoneme_delay

A = (start − t) / S ;  skip the phoneme when A ≥ 1 ;  A = max(A, 0)
B = (end   − t) / S ;  skip the phoneme when B ≤ 0 ;  B = min(B, 1)

scale = B − A
```

So a phoneme's contribution is a **trapezoid whose ramp-in ends at its authored start and whose
ramp-out ends at its authored end**, both of duration `S`. Two consequences that a symmetric
in-span reading would get wrong:

- the phoneme **leads in before it is spoken** — the window opens at `start − S` — and always
  decays to exactly zero at `end`;
- a span shorter than `S` **never reaches full weight**. Its peak is `span / S`, attained
  exactly at the authored start. Since spans abut within a word, one phoneme's decay overlaps
  the next one's lead-in, and the additive accumulate is what sums them.

`S` is the span **clamped to a per-model pair**, so the blend width is authored per character
rather than global. `FUN_100c3be0` prefers the `phonemefilter_min` / `phonemefilter_max`
ConVars, but only when **both** are away from their defaults — each reads as `0.0` when
`IsDefault()` answers true, and either zero sends it to the model pair. Shipped behaviour is
therefore entirely the model's own numbers.

Those numbers are the two floats at **studiohdr +232/+236** (`mdl_v2531.md` recorded them as an
`int[2] Unknown`). Across the 339 loose models:

| `+232`, `+236` | Rigged | Models |
|---|---|---|
| **0.065, 0.100** | yes | **57** — including `lacroix`, `nines` and `skelter` |
| 0.080, 0.100 | yes | 32 |
| 0.080, 0.105 | yes | `Jeanette` alone |
| 0.065, 0.100 | no | 112 |
| 0.080, 0.100 | no | 24 |
| 0.0, 0.0 | no | 113, all `NumFlexDescs 0` |

**The pair varies across the rigged cast**, so it is a per-model input rather than a constant: the
two shipped minima differ by 23 %, and the majority of speaking characters — every one of
sp_theatre's — blends over `clamp(span, 0.065, 0.100)` seconds. The `(0,0)` pair would
make `S` zero and `1/S` infinite; nothing in the clamp guards it, and only models that never
reach the viseme path carry it.

`phoneme_delay` contributes **0 at its default** — the call site takes a literal `0.0` when the
ConVar answers `IsDefault()`. The clock is therefore the authored event's, not the mixer's
position.

## The offline export (PL10)

`pipeline/src/elysium_pipeline/exporters/npc_export.py` bakes each rigged NPC's flexes into its own `.glb` as glTF **morph
targets** and writes everything above them to `$ELYSIUM_EXPORT_ROOT/npc/facial/<stem>.json`. The split follows
what a morph target is: a vertex displacement. Everything between a controller value and that
displacement — the rules, the ramps — is arithmetic, so it ships as data and 12.3 replays it.
The decoders are `mdl_skel.py`'s (`flex_descs`, `flex_controllers`, `flex_rules`, `mouths`,
`mesh_flexes`, `vert_anims`, `read_anorms`); `probe_facial.py` is the survey over the same
code, so a layout fixed for one is fixed for both.

### The morph target is a flex *record*, not a flexdesc

One target per distinct `(flexdesc, target ramp)` over the whole model. A flexdesc can carry
two flexes on one mesh under different ramps — the eyelid hinge above — and the ramp decides
which half a given weight drives, so they are two morphs. The shipped 65-flexdesc rig yields **53**
of them: 45 flexdescs that a mesh actually deforms, plus eight second ramps.

Names are the flexdesc's FACS name, with a `#k` suffix on the second and later ramp of the
same flexdesc (`AU12R`, `AU12R#1`). Uniqueness is load-bearing — glTFRuntime keys a
`UMorphTarget` by name.

### A morph spans materials

VtMB's face is not one mesh. `AU27Z` (the wide jaw drop) on `nines` moves 382 head vertices,
145 molar, 265 lower-teeth, 27 tongue and one neck seam vertex on the body — five of the
model's eight material primitives.

glTF weights are mesh-level, so the target list is **unified across every primitive** and
written in the same order on each; that is what lets `mesh.extras.targetNames` name them
positionally. A primitive a target does not touch still carries it, as a one-entry zero
sparse accessor (glTFRuntime applies the names first and drops the empty ones after, under
`bIgnoreEmptyMorphTargets`). A target that spans two materials therefore arrives as one
same-named piece per primitive, so the consumer **must** load with
`MorphTargetsDuplicateStrategy::Merge` — the default, `Ignore`, keeps the first piece and
silently drops the rest, which would move a jaw and leave its teeth behind.

### The encoding

Each target attribute is a **sparse accessor with no `bufferView`** — base implicitly zero,
only the moved vertices stored — carrying `POSITION` and `NORMAL` deltas in glTF space
(`(x,y,z)→(x,z,−y)`, ×0.0254 for the position, unscaled for the normal). The `POSITION` and
`NORMAL` accessors of one target share their sparse *indices* view, since a flex moves the
same vertex set in both; the *values* views are never shared, because glTFRuntime reads
`sparse.values.byteOffset` and then never applies it.

The mesh's base normals are regenerated from face normals at export (the `.mdl`'s own vertex
normals are not carried), so a normal delta lands on a recomputed base rather than the
authored one.

### What ships beside it

`$ELYSIUM_EXPORT_ROOT/npc/facial/<stem>.json`, index-aligned with the glb, named by
`npc_index.json`'s `npcs[stem].facial`:

| Field | Holds |
|---|---|
| `flexdescs` | the 65 FACS names, index = flexdesc id |
| `controllers` | the 44 `{name, type, min, max}` rows, index = the `FETCH1` operand |
| `rules` | 60 × `{flexdesc, ops}`; an op is `[name]`, or `[name, operand]` for `CONST` (float), `FETCH1` (controller index) and `FETCH2` (**flexdesc** index — see the opcode table above) |
| `mouths` | `{bone, forward, flexdesc}` — the amplitude jaw, for 12.5 |
| `morphs` | `{name, flexdesc, targets}` per glTF morph target, in order; `targets` is the four-value ramp |

Not exported: the **`StudioEyeball` records** — which the eyes section above shows every
character carries, and which a face needs for its lids as well as its irises — and the `.vfe`
expression tables, where `UE_extract_scenes.py` mirrors the readable `.txt` twins to
`$ELYSIUM_EXPORT_ROOT/expressions/` instead.

**A sidecar can be complete and still drive nothing**, so a consumer decides on morph targets
rather than on the manifest parsing. `shovelhead` carries the full 65/44/60 rig with **zero**
morph targets and `female_raver_1` a single flexdesc; both are real export states rather than
faults, and a loader that treats a parsed sidecar as a working face moves nothing and reports
success.

Across the NPC set the exported maps place, **78 of 101 models are rigged**, carrying **4,015
morph targets** (53 each on 68 of them; `mercurio`'s family 51–52) and 1.3 MB of manifests.
The morph data costs **0.33–0.53 MB per rigged NPC** — near-constant, since every face runs
the same rig over comparable geometry — so it is a rounding error against a glb whose size is
set by its dialogue clips (`heather` +2.4 %) and the whole of a glb that has none
(`copper` +95 %).

## What this settles for the rebuild

- The controller → rule → flexdesc evaluation has to run at load time, not as a flat morph
  list: 44 controllers drive 65 morphs through 60 RPN rules, and the rules are where the
  eyelid interaction lives.
- Eyes are a **plain reproduction end to end** — record, renderer math, gaze behaviour and
  tuning data are all recovered (eyes section above). Two pieces of it are inert in retail and
  are therefore owner calls rather than decisions: head turn drives bone controllers no model
  declares, and `LookAtEntityCenter` aims at the eye rather than the centre.
- **Three of the 60 rules are cross-wired left to right on every rig** (rules section above).
  They are authored and they move geometry, so they are reproduced; symmetrising them is a
  content change, not a repair.
- The flex evaluation has an **order dependency that spans the seam**: rules first, then the
  eyeball pass, which overwrites the four eyelid flexdescs. A morph pipeline that ends at the
  rules has no lid motion.
- The eyelid `uppertarget`/`lowertarget` values are **linear offsets through `asin(t/radius)`**,
  not radians.
- Lip sync is a three-file join per line — `.lip` for timing, `expressions/<stem>_phonemes`
  for the weights, `mstudiomouth_t` for the amplitude jaw — with the phoneme *code* as the key,
  resolved through the table's class column. All three are on disk: `$ELYSIUM_EXPORT_ROOT/lip/`, `$ELYSIUM_EXPORT_ROOT/expressions/`, and `mouths` in the facial
  manifest. The join needs a **fourth** input the manifest does not carry: the two floats at
  studiohdr +232/+236 that set the phoneme blend width (envelope section above).
- The phoneme envelope is **not symmetric inside the span**. It ramps in before the phoneme's
  authored start and decays to zero at its end, and a span shorter than the blend width peaks
  below 1. A reconstruction that ramps within the span moves the mouth early-to-late by up to
  the blend width and over-articulates every short phoneme.
- The viseme accumulate applies the expression row's **value** and ignores its influence
  column. `lacroix_phonemes`'s `r2` row carries `0.050` under influence `0.000`, so applying
  the influence is a silent divergence rather than a no-op.
- No player body carries a flex rig (inventory above), so the PC has no morph targets to
  drive and no `facial/` sidecar — but 57 of the 59 carry eyeball records, so the faithful
  player face is a **still face with live, aiming eyes**.

Implementation roll-up status for this system is in `docs/project/roadmap.md`; the open retail
facial/lip capture work is specified in `docs/project/plans/capture.md`.

## Provenance

- Struct layout: validated by `research/tooling/probes/probe_facial.py` over the whole merged install —
  0/17,960 out-of-range flexdesc indices, 0/3,314,219 out-of-mesh vertex indices,
  0/17,960 non-contiguous vertex-animation blocks.
- `StudioRender.dll` (imagebase `0x2C000000`): `R_StudioFlexVerts` at `0x2C050860` — the
  target ramp at `0x2C0508E0`, the `VertAnimType` branch at `0x2C050982`, the 20-byte loop
  at `0x2C0509A8`, the 8-byte loop at `0x2C050AF0`; the vertex accessor at `0x2C0181B0`; the
  tables at `0x2C06E008` (unit vectors), `0x2C06D358` (byte-indexed normals), `0x2C06C530`
  (`n/255`), and the scale constant `8.0` at `0x2C06C4FC`.
- `client.dll` (imagebase `0x10000000`): the expression-file name at `0x100C4210` /
  `0x100C4270` / `0x100C42F0`; the flex convars `flex_rules`, `print_flex_weights`,
  `print_flex_rules`, `phoneme_delay`, `phonemefilter_min`, `phonemefilter_max`.
- The eye system, by half. Model: `StudioModel+192/+196` and `StudioMesh+24/+28`, censused over
  the merged install. Renderer (`StudioRender.dll`): the eye pass `0x2C0502C0`, the per-mesh
  eye path `0x2C01CB60` off the draw loop `0x2C01D9F0`, the material-var setter `0x2C01CA20`,
  the glint planes `0x2C0513F0`, the glint texture `0x2C051350` and its regenerator
  `0x2C051690`, the splat `0x2C050D70`, the light query `0x2C051F70`, and the view-target
  setter `0x2C050270` (`IStudioRender` vtable `+0x1C`; flex weights at `+0x34`). Shader
  (`stdshader_dx8.dll`): the `Eyes` object `0x100250F4`, vtable `0x1001E358`, `DrawElements`
  `0x100053F0`; the compiled programs in the VPKs under `shaders/vsh/` and `shaders/psh/`.
  Server (`vampire.dll`): `SetScriptedEyeTarget` `0x10325CB0` and the four input handlers
  `0x10325940`/`…A30`/`…B20`/`…C10`, the scripted maintainer `0x10325620`, the autonomous
  maintainer `0x1026B810`, the fidget `0x102C0010`, `SetHeadDirection` `0x1026AF70`, the
  cone test `0x10325DA0`, `CalcLookData` `0x10331DA0`, `Blink` `0x100B5CE0`. Client
  (`client.dll`): `SetupWeights` `0x100C42F0`, `RunFlexRules` `0x100C3CD0`, the view-target
  interpolation `0x100C4110`, `AddGlobalFlexController` `0x100C4880`, the weight array
  `0x104A4A90`.
- The flex rules have a **second, independent recovery**. A community decompiler recovered the
  60 rules of the shipped rig as QC `%flexcontroller` expressions and published them on Planet
  Vampire (thread *"BloodThirstyVamp's decompiled VTMB models"*), naming the `FETCH2` targets as
  QC `localvar`s. Replayed against this document's opcode table over `heather.mdl`, the two
  agree **operand for operand on all 60**, including the three cross-wired rules above and the
  `1 / right_open` divide. Neither reading informed the other, and the published set carries no
  scaling or ordering assumption that ours could have inherited.
- The `.vfe` header is read from the shipped files themselves — all 249 in the merged install,
  `id` and `version` uniform, `length` exact on 247 and the internal name resolving as described.
- Reference parsers: `$ELYSIUM_WORK_ROOT/research/reference-source/VAMPTools` `studio.h` (flex block 8 bytes short; eyeball and
  vertanim marked *UNVERIFIED ALIGNMENT*) and `$ELYSIUM_WORK_ROOT/research/reference-source/Crowbar` `SourceModel2531`
  (`StudioFlex` 32 B with field 0x1C as *"unknown"*, `StudioVertAnim` read as
  `short delta[3]`, eyeball 140 B). Neither recovered the compressed vertex-animation
  encoding.
