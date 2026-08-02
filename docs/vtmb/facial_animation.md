# Facial animation — `.mdl` v2531 flex data, eyeballs, and the `.lip` phoneme files

VtMB animates a face with **flexes**: per-vertex morph targets stored inside the `.mdl`,
driven by a small set of named *flex controllers*, which are themselves driven by an RPN
*flex-rule* per morph. Lipsync feeds those controllers from a plain-text **`.lip`** phoneme
document shipped beside each line of audio, through a per-character **`expressions/*.vfe`**
weight table. There is no separate eye system in the shipped data.

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
| …carrying eyeball data | **0** |
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
NPC-model portraits VtMB shows elsewhere; on the body itself there is nothing to drive. The
eyeball finding below is the same shape and carries the same consequence: the lipsync and
expression layers (12.3, 12.5) apply to NPCs, and any PC facial performance would be a remaster
addition under `docs/project/remaster-direction.md`'s rule, not a reproduction.

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

**The four eyelid rules drive nothing on their own, and the gap is structural.** The
flexdescs that carry eyelid *morphs* — `upper_right`, `lower_right`, `upper_left`,
`lower_left`, flexdescs 0/4/8/12 — carry no rule. The `_lowerer`/`_neutral`/`_raiser`
flexdescs the rules above compute carry no morph. Nothing in the shipped rig connects the
two, so evaluating the rules exactly as authored moves no eyelid vertex at all.

Source closes that gap inside `mstudioeyeball_t`, whose `upperlidflexdesc` takes
`Σ(lid weight × uppertarget[k])` over the three lid states. VtMB ships no eyeball record on
any model (see below), so the bridge is absent from the data rather than from our reading of
it. The rig was authored for a structure the shipped files do not contain.

**Elysium reconstructs the bridge — uncertain, and it may be an addition rather than a
reproduction.** The three lid angles are recoverable from the ramps (`lowered` is the low
ramp's `Target2`, `neutral` the hinge, `raised` the high ramp's `Target1`) and the three
sources match by name. Two things support it: without it a blink moves nothing, and the
resting lid sits at 0 rather than its 0.208 hinge, which leaves the lowered-lid morph on at
0.565 on every face in the game; with it, all 84 deforming rigs resolve to exactly zero morph
weight at rest. That last check is independent of how the bridge was constructed, which is
what makes it evidence.

What it does not establish is whether **retail** bridges it. If the original engine also
found no eyeball record, VtMB's lids may never have moved, and this reconstruction is an
improvement on the Feel layer rather than a recovery — the same footing as the eye divergence
below. Evidence that would settle it: capture retail's lid flexdesc weights across a blink
and see whether they ever leave zero.

### `mstudiomouth_t` — 20 bytes

| Off | Type | Field |
|---|---|---|
| 0 | int | `bone` |
| 4 | Vector | `forward` |
| 16 | int | `flexdesc` |

One per rigged character (199 of 201), always pointing at the `mouth` flexdesc (index 16)
and at the jaw bone with `forward = (0, -1, 0)`. This is Source's audio-amplitude jaw: the
mouth flex is driven by the envelope of whatever the actor is saying, independently of the
phoneme track.

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

## Eyeballs — the chunk exists, the data does not

`StudioModel.NumEyeballs`@80 / `EyeballIndex`@84 point at a **140-byte** `StudioEyeball`
(name index, bone, org, zoffset, radius, up, forward, texture, iris material, iris scale,
glint material, `upperflexdesc[3]`, `lowerflexdesc[3]`, `uppertarget[3]`, `lowertarget[3]`,
upper/lower lid flexdesc, pitch `[2]`, yaw `[2]`). The stride is confirmed by the `0x8C`
scaling in `StudioRender`'s eye pass.

**`NumEyeballs` is zero on every model in the install** — all 4,444, including all 201
rigged characters. VtMB ships no eyeball records, so there is no eye-posing, look-at,
procedural-lid or glint data in the models at all. What the shipped rig *does* have is:

- the eight `eyelid` flex controllers (`blink`, `half_closed`, and the
  raiser/tightener/droop left-right pairs), and
- the 16 eyelid flexdescs the four eyelid rules drive.

So eyes in VtMB are **eyelids only**: blinking and lid shaping are flexes; there is no
authored eyeball geometry orientation. Any gaze/look-at behaviour is either bone-driven or
absent. Roadmap **12.4 must be built on that basis** — it cannot decode an eye pose that was
never authored.

### Divergence — Elysium gives the cast living eyes

**Faithful behaviour:** eyes do not move. The eye surface is head texture, the only authored
eye motion is the lids, and no model carries a `StudioEyeball` record to orient, aim, or
glint. A reproduction of VtMB renders a fixed painted stare under moving eyelids.

**The divergence, on an explicit owner call:** the cast has living eyes. Faces carry the
theatre act in close-up and the slice's fidelity bar names eyes explicitly, so the painted
stare is rejected even though it is what retail draws. This is an addition on
`docs/project/remaster-direction.md`'s Feel layer — there is no VtMB behaviour to be faithful
to here, so the usual "reproduce by default" resolution has nothing to resolve to.

The intent is settled and the mechanism is not. Gaze targeting, saccades, an oriented iris
and a glint are the candidates; each is built one at a time and stays A/B-able against the
painted baseline, per the Feel-layer rule that a delta is polished by explicit owner call
rather than in a batch. Whatever is built is **new data beside the model**, since the
140-byte `StudioEyeball` layout above is decoded but never populated by any shipped file —
it documents a chunk VtMB's own tools never wrote.

Roadmap 12.4 owns the build; this section owns the call.

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

**The leading integer is not a reliable key.** It is Faceposer's numeric phoneme code —
ASCII for the single-letter phonemes (`t`=116, `n`=110, `s`=115, `d`=100, `l`=108, `m`=109,
`k`=107, `w`=119, `z`=122, `b`=98, `f`=102, `p`=112, `v`=118, `g`=103) and an extended code
for the rest (`ax`=601, `ih`=618, `ah`=652, `eh`=603, `ay`=593, `ae`=230, `dh`=240,
`ao`=596, `er`=602) — but the same phoneme string carries several different codes across the
corpus (`ax` appears under 20 distinct codes, `ih` under 12). **Match on the phoneme string.**

The trailing `volume` is 1.000 on effectively every row and the sixth field is 0; version 1.0
and 1.1 files omit the sixth field.

## `expressions/` — phoneme → flex-controller weights

`expressions/` holds **249 `.vfe`** (Faceposer's compiled form) and the identical 249
`.txt` sources. The `.txt` is readable and is the useful one:

```
$keys right_cheek_raiser left_cheek_raiser wrinkler right_upper_raiser … lower_lip
$hasweighting
"b" "b" 0.000 0.000 … 0.350 1.000 … 0.330 1.000 … "Big : voiced alveolar stop"
"m" "m" 0.000 0.000 … 0.340 1.000 … 0.590 1.000 … "Mat : voiced bilabial nasal"
```

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

## The chain, end to end

```
.lip phoneme row  ──►  expressions/<model stem>_phonemes.vfe   (phoneme → controller weights)
                                     │
line audio envelope ──►  mstudiomouth_t  ──►  the `mouth` flexdesc
                                     ▼
                         flex-controller values (44)
                                     │  mstudioflexrule_t RPN
                                     ▼
                          flexdesc weights (65)
                                     │  StudioFlex target ramp
                                     ▼
              per-mesh StudioVertAnim  ──►  pos += w·delta,  norm += w·ndelta
```

`.vcd` scenes drive the same controllers directly through their `flexanimations` tracks
(`docs/vtmb/choreographed_scenes.md`), so expression and lipsync meet at the controller layer, not at
the vertex layer.

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

Not exported: **eyeballs** (none authored anywhere in the install) and the `.vfe` expression
tables — `UE_extract_scenes.py` mirrors the readable `.txt` twins to `$ELYSIUM_EXPORT_ROOT/expressions/`
instead.

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
- There is no eyeball data to consume (eyeballs section above), so nothing decodes an eye
  pose. Elysium adds living eyes anyway, on the owner call recorded beside that section as a
  divergence; the lids stay a reproduction.
- Lip sync is a three-file join per line — `.lip` for timing, `expressions/<stem>_phonemes`
  for the weights, `mstudiomouth_t` for the amplitude jaw — with the phoneme *string* as the
  key. All three are on disk: `$ELYSIUM_EXPORT_ROOT/lip/`, `$ELYSIUM_EXPORT_ROOT/expressions/`, and `mouths` in the facial
  manifest.
- No player body carries a flex rig (inventory above), so the PC has no morph targets to
  drive and no `facial/` sidecar — it animates with a still face.

Implementation roll-up status for this system is in `docs/project/roadmap.md`; detailed retail
facial/lip capture status is in `docs/project/retail-capture-roadmap.md`.

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
- Reference parsers: `$ELYSIUM_WORK_ROOT/research/reference-source/VAMPTools` `studio.h` (flex block 8 bytes short; eyeball and
  vertanim marked *UNVERIFIED ALIGNMENT*) and `$ELYSIUM_WORK_ROOT/research/reference-source/Crowbar` `SourceModel2531`
  (`StudioFlex` 32 B with field 0x1C as *"unknown"*, `StudioVertAnim` read as
  `short delta[3]`, eyeball 140 B). Neither recovered the compressed vertex-animation
  encoding.
