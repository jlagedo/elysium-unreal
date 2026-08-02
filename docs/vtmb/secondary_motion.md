# Secondary motion — the authored per-bone angular limit table

VtMB moves hair, ponytails, manes and breasts with a stage that no animation channel drives
and no `StudioBone` field declares. The stage reads a **per-bone table in the `.mdl` header**,
undocumented in every public description of the format, and the one field of it that is
proved is an **angular limit**: the drawn bone is held at an authored maximum rotation away
from where the hierarchy would put it.

The rest of the table is almost certainly the solve that limit clamps — four further floats
per record whose value sets have the shape of a spring — but only the limit is established.
This document separates the two.

Struct offsets are the `.mdl` v2531 layout (`docs/vtmb/mdl_v2531.md` owns the header). The
composition this stage sits after is `docs/vtmb/animation_and_movers.md` A.4a, and the other
stage between locals and the drawn skeleton is `docs/vtmb/procedural_bones.md`.

## Where the table lives

`MDLHeader` carries a count/index pair at **+396** and **+400** addressing an array of
**28-byte** records:

| Off | Type | Content | Standing |
|---|---|---|---|
| +0 | int | bone index | proved — in range on 600 of 600 records |
| +4 | int | `-1` | proved — the same sentinel on 600 of 600 |
| +8 | float | `{9, 30, 60}`, 3 distinct | undecoded |
| +12 | float | `0 … 10`, 10 distinct | undecoded |
| +16 | float | `{0.05, 0.15, 0.2, 0.5, 0.8, 0.9, 0.95, 0.97}` | undecoded; damping-shaped |
| +20 | float | `0 … 7`, 11 distinct | undecoded |
| +24 | float | **the angular limit, in degrees** | proved against a running engine |

The slot is dated by its neighbours rather than by assertion. `+344`…`+388` match modern
Source's flexdesc / flexcontroller / flexrule / ikchain / mouth / localposeparameter order
exactly — on `Therese.mdl` they read 65, 44, 60, 4, 1, 2, and the two pose-parameter records
decode as a textbook `mstudioposeparamdesc_t` (`nameindex`, `flags`, `-180.0`, `180.0`,
`360.0`). `+392` is `surfacepropindex`, and `+404`/`+408` are the independently verified
`NumIncludeModels`/`IncludeModelIndex`.

**That places this pair exactly where modern Source has `keyvalueindex`/`keyvaluesize` and
`numlocalikautoplaylocks`** — and it is why the table has never been read. Every public VtMB
decoder descends from modern Source understanding, parses those bytes as a keyvalues offset,
gets nonsense, and moves on. The field is not hidden; it is mislabelled by a later revision
of the format. It is not an ik autoplay lock either: the record is 28 bytes, names a bone,
and carries degrees.

## Corpus

Over the whole engine-resolved install: **107 of 4,445 v2531 models carry a non-empty array,
600 records total, zero faults** — every bone index inside its model's bone count, every
sentinel `-1`, every array inside its image.

The driven bones are secondary motion and nothing else: `Bone01` (65), `Bone05` (46),
`Bone13` (32), `Bone09`, `Bone17`, `BoobLeft03` (18), `BoobRight01` (18), `left breast` and
`right breast` (11 each), `Mane03`, and the `Bip01 Ponytail*` chain.

The parents say the same thing, and say that it is mostly hair: **`Bip01 Head` on 320 of
600** records, `Bip01 Spine1` 108, `Bip01 HeadNub` 62, `Bip01 MouthRoot` 10, `Bip01 Neck` 6.

Limits cluster hard on round values — 30° (259 records), 10° (118), 20° (68), 90° (50),
15° (41), 60° (18), 25° (10) — and **585 of 600 are exact multiples of 5°**. The largest
arrays are `manbat` (25), `mingxiao` (23), `mingxiao_baby` (19), `andrei` (15) and `tourette`
(14).

## The limit is a ceiling, and it is reached and held

Measured against one theatre capture, over 7 models carrying the array and 28,372 samples of
94 bone-series:

| Rig | Authored limit | Samples at the ceiling |
|---|---:|---|
| `Therese` and `VV` hair chains | 10.00000° | 84–91% |
| `Damsel` hair chains | 15.00000° | 55–97% at the chain roots |
| `Therese` `left`/`right breast` | 20.00000° | rarely reached |
| `VV` `bone01`/`bone03` | 25.00000° | rarely reached |
| `Damsel` `Bone30`/`Bone32`, `Sheriff`, `Gangrel` | 30.00000° | 4–100% |

**48 of the 50 records across those models equal the observed maximum divergence exactly.**
The two that differ are both *below* the authored value on short series that never saturated
— `malk_girl` `Bone05` authored 60° against an observed 58.36°, `Gangrel` `Bone28` authored
30° against 29.87°. A ceiling not reached is not a contradiction.

Spread across the plateau samples is `1e-6` to `1e-3` degrees. A solve does not land on a
round number to six significant figures on hundreds of consecutive draws; a limit does.

**The control is what makes this a ceiling rather than a fixed offset.**
`Smiling_Jack.mdl` carries three records with a **100°** limit, its named bones are selected
by the builds that pose it, and it has 1,570 paired draws — and it never diverges at all. A
loose limit is never engaged. A static per-bone rotation would have put those bones 100° out.

## What it is not

Four mechanisms are eliminated by measurement rather than by argument.

- **Not driven by the actor's motion.** Spearman correlation of the divergence against the
  actor's own root translation, root rotation, parent translation and parent rotation, over
  93 series, gives medians of `+0.227`, `+0.030`, `+0.225` and `+0.183`, spanning both signs
  from `-0.66` to `+0.89`; only 13 of 93 exceed `|ρ| = 0.5` and all 13 are 26-sample series.
  `Damsel` is rooted on 673 of 683 samples with the divergence pinned at exactly 15.000°
  through all of them. A solve that goes quiet when the actor stops is refuted.
- **Not a first-order lag on the parent.** Fitting one gives `α` spread `0.002`–`0.847` across
  93 bones (median `0.506`, median R² `0.585`). One lag would give one `α`.
- **Not a visible spring.** Lag-1 autocorrelation of the signed error components has median
  `+0.728` with a sign-change rate of `0.08`. No oscillation, no settling. Retail's drawn
  orientation is off the geodesic between the previous draw and the current target on 25,176
  of 39,512 samples carrying signal.
- **Not VPhysics.** `Therese.mdl`, `VV.mdl` and `Damsel.mdl` ship **byte-identical** `.phy`
  files — 25,489 bytes, the same tail, the same solids — so one file cannot carry a 10° limit
  for one and 15° for another. Its solids are ragdoll parts (`Bip01 Pelvis`, `Spine1`,
  `R UpperArm`, `L Forearm`); no hair, breast, ponytail or cloth bone appears in it at all,
  and its `ragdollconstraint` blocks index those solids. `docs/vtmb/phy_vphysics.md` owns
  that format.

Nor is it a declared procedural rule: none of these bones carries `ProcType`, `Flags & 0x1`
or `Flags & 0x2`, and VtMB's `ProcType` enum stops at `AXISINTERP` and never reaches the
later `JIGGLE` (`docs/vtmb/procedural_bones.md`).

## Three properties the corrected bones share

- **No clip animates them.** The captured composed local is the bind pose on every sample of
  every affected bone — `0.0` in position and `≤0.00175°` in rotation.
- **The correction is rotational.** Translation composes exactly against retail's own parent,
  worst `1.7e-4` units corpus-wide.
- **Direction is model-specific, not global.** `Therese` and `VV` draw the bone higher on
  92–96% of samples; `Damsel`, `Sheriff` and `Gangrel` lower on 92–100%. Consistent within a
  model, inconsistent across the corpus.

## The correction propagates down the chain, and the arithmetic closes

A residual of 90 bones over 7 models resolves into **47 bones the array names** and **43
descendants of a named bone**. Every array record in those models produces a divergence, and
every other divergent bone descends from one.

Descendants inherit it because retail composes a child off the **pre-correction** parent and
then overwrites the parent's own rotation in place. That predicts a descendant's *translation*
error as the chord of the parent's clamped rotation at the descendant's own radius,
`2r·sin(θ/2)`:

| Bone | Predicted | Measured |
|---|---|---|
| `Therese` depth-8 hair, 10° limit | `2 × 2.181 × sin 5° = 0.380` | 0.38 |
| `Damsel` depth-9 hair, 30° limit | `0.82 + 2 × 2.152 × sin 15° = 1.934` | 1.94 |

Nothing is fitted: the radius comes from the bind pose and the angle from the authored limit.
Two analyses reached those numbers from opposite ends — one differencing transforms with no
knowledge of the table, one predicting from the table with no knowledge of the residual scan.

The depth trend is a consequence rather than a separate finding. Divergence growing
monotonically with depth (`Sheriff` 1.55 → 3.66 → 8.24 at depths 8/9/10) is what a fixed
angular limit at a chain root produces as the lever arm lengthens; it is not evidence of an
accumulating solve.

## Open questions

- **Four of the five floats are undecoded.** `+8`, `+12`, `+16` and `+20` are almost certainly
  the solve the limit clamps — `+16`'s value set is damping-shaped and the others are small
  positive scalars — but nothing establishes that. Evidence that would settle it: a capture of
  the stage that writes these bone-to-world slots, or a model whose limit is loose enough
  (like `Smiling_Jack`'s 100°) that the underlying solve is visible unclamped across a run
  with real motion.
- **What drives the bone below the ceiling is unknown.** The corpus saturates most frames, so
  the sub-ceiling behaviour is barely sampled, and the three obvious candidates are refuted
  above. The samples that fall short of the limit are the evidence to work from.
- **The writing stage is not located.** The composed locals carry the bind pose and the drawn
  matrix carries the correction, so the write happens between the pose build and the studio
  draw. Which function performs it is unestablished.
- **Nothing outside this corpus is measured.** Seven models carrying the array appear in one
  theatre capture; the other 100 are decoded from the installed file and unobserved at runtime.

## Provenance

- The array is located, decoded and censused offline from the installed images; the ceiling is
  measured against one finalized `sp_theatre` capture. Method and evidence grades are
  `docs/vtmb/vtmb-animation-reverse-engineering.md`.
- The bytes are read by neither retail's hooked animation path nor the offline decoder: over
  `Therese`'s 165 and `VV`'s 300 consumed spans, zero overlap the array and zero touch header
  bytes 396–404. They fall inside the 35,858,024 bytes the byte-coverage difference records as
  read by neither side.
