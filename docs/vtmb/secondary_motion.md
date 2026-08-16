# Secondary motion — bone chains and renderer cloth

VtMB has **two independent authored secondary-motion systems**:

- a client-side bone-chain solver for hair, ponytails, manes and breasts. It updates
  bone-to-world matrices after ordinary hierarchy composition, then ordinary StudioRender skinning
  consumes the corrected palette;
- a StudioRender particle-cloth solver for garments. Animation skins only its pinned attachment
  vertices; persistent simulated particles drive the free surface, and generated vertex routines
  substitute simulated position, normal and tangent output for selected render vertices.

Jeanette's skirt and Sheriff's trenchcoat use the second path. Their ordinary pelvis/thigh/body
weights are still present, but those weights are not the final deformation for the cloth-selected
vertices. Looking only at bones and skin weights therefore produces the exact false negative that
left Jeanette's skirt as a fixed cone.

Neither path is VPhysics. The bone path is also not later Source's `STUDIO_PROC_JIGGLE`: it is a
VtMB-specific 28-byte record table at `MDLHeader` +396/+400 and a stateful solve owned by each
`C_BaseAnimating`. The garment path is a separate VtMB extension carried by `StudioModel`,
`StudioMesh`, and an installed-header flag bit `0x400`.

Struct offsets below are the `.mdl` v2531 layout owned by `docs/vtmb/mdl_v2531.md`. Ordinary pose
composition is `docs/vtmb/animation_and_movers.md` A.4a; the separate AxisInterp stage is
`docs/vtmb/procedural_bones.md`.

## Confidence vocabulary

| Standing | Meaning in this document |
|---|---|
| **Verified** | Directly established by retail binary control flow, installed bytes, a controlled retail capture, or a source-to-generated numeric comparison. |
| **Strong evidence** | Several independent observations agree, but a shipped input does not exercise every branch or the bounded corpus does not cover every possible asset. |
| **Partial** | The stated subset is measured; extrapolation beyond it is not authorized. |
| **Unknown** | The located evidence does not name the field or behavior. No semantic label is assigned. |

## The bone-chain deformation path

The render path is closed across `client.dll`, `engine.dll`, and `StudioRender.dll`:

1. The sequence evaluator produces local positions and quaternions. Layering, transitions,
   included-model remaps and AxisInterp run on that pose.
2. `C_BaseAnimating::BuildTransformations` (`client.dll` `0x1008fd00`) composes the selected
   local transforms into bone-to-world matrices.
3. At `0x100901f7`–`0x10090235`, after the ordinary hierarchy loop, it walks the linked list at
   `C_BaseAnimating+0x724` and calls the chain update at `0x100ac880` for every table record. The
   update reads and writes the same bone-to-world matrix buffer.
4. `TStudioRender012` slot `+0x40` exposes StudioRender's bone-to-world buffer. StudioRender then
   builds `skinMatrix[i] = boneToWorld[i] * StudioBone[i].poseToBone` at `0x2c004e10`.
5. StudioRender's ordinary and flexed mesh branches pass that palette to generated CPU vertex
   routines. The inspected routines blend one to four matrices by the model's byte weights and
   transform both positions and normals before submission.

**Standing: Verified.** This proves where the bone-chain write occurs and how it reaches pixels.
Hair weighted to a driven chain uses the ordinary skinning backend. Garment cloth diverges later,
inside specialized StudioRender vertex routines described below.

Materials are likewise per surface rather than a special “hair renderer.” In the engine-resolved
install, the inspected character materials are `VertexLitGeneric`; alpha behavior is authored per
VMT. For example, `Damsel`'s lower-hair and `VV`'s `vv_hair_t` materials specify
`$translucent 1`, while their other hair surfaces do not, and `Therese`'s skirt has no such key.
`Therese`'s current patch-first hair VMT adds `$nocull 1`. **Standing: Partial** — these are
verified examples, not a claim that every hair material uses one uniform alpha policy.

## The MDL record

`MDLHeader` carries a count/index pair at **+396** and **+400** addressing an array of **28-byte**
records:

| Off | Type | Runtime meaning | Standing |
|---|---|---|---|
| +0 | int | first moving bone | **Verified** — passed to the chain constructor; its parent must exist |
| +4 | int | explicit terminal bone, or `-1` to follow the child chain | **Strong evidence** — the two constructor branches are direct, but all 600 shipped records use `-1` |
| +8 | float | unknown authoring field; shipped values are `{9, 30, 60}` | **Unknown** — `0x10096650` does not read or pass it to the located consumer |
| +12 | float | gravity | **Verified** — direct `bc_gravity` override mapping and integration use |
| +16 | float | damping | **Verified** — direct `bc_damp` override mapping and velocity use |
| +20 | float | spring exponent; runtime coefficient is `10^(-value)` | **Verified** — direct `bc_spring` override mapping and constructor conversion |
| +24 | float | maximum angular deviation, degrees | **Verified** — radians conversion, `bc_maxangle` override, constraint clamp, and retail capture |

The field mapping does not come from value-shape guesses. `0x10096650` reads the record and calls
the constructor at `0x100ac0f0`. The call passes +24, +20, +16, +12, +4, and +0; it never reads
+8. The constructor multiplies +24 by `π/180`, evaluates `pow(10, -value)` for +20, and stores
+16 and +12 directly. Under `bc_override`, `0x100ac880` replaces those same four runtime members
from `bc_maxangle`, `bc_spring`, `bc_damp`, and `bc_gravity` respectively.

The slot is fixed by its neighbors. `+392` is `surfacepropindex`; `+404`/+408 are the independently
verified `NumIncludeModels`/`IncludeModelIndex`. Modern Source assigns other meanings around this
offset, which is why a later-format decoder can mislabel and discard the VtMB table.

## Chain construction and lifetime

On a model/data change, the client call at `0x10094bd5` rebuilds the list. `0x10096650` first
destroys the existing objects, obtains the active studio header, allocates one 0x50-byte owner per
record, constructs it, and prepends it at entity +0x724. `C_BaseEntity` destruction calls the same
cleanup at `0x1008f5e7`.

For the shipped `-1` form, the constructor starts at +0 and follows the ordered `StudioBone.parent`
chain. It rejects a rootless first moving bone with the literal diagnostic
`first moving bone %s cannot be rootless`. It allocates 100 bytes of state per moving bone and
derives the segment frames and lengths from the studio bones' bind transforms. The explicit +4
branch walks parents from the supplied terminal back toward the first bone, but no shipped record
exercises it.

**Standing: Verified** for construction, automatic chaining and lifetime; **Strong evidence** for
the intended authoring meaning of the never-shipped explicit-terminal branch.

## Per-frame solve

The update at `0x100ac880` is stateful and time-stepped:

- deltas below **0.001 s** do not advance the state;
- a first update, a backwards clock, or a gap above **0.15 s** resets the chain instead of carrying
  stale velocity across the discontinuity;
- an ordinary update chooses `N = min(10, ceil(deltaSeconds * 300))`, giving a target step rate of
  **300 Hz** with at most ten catch-up steps;
- the first update constrains the current pose, seeds previous positions from it, then runs
  `20 + 2N` warm-up integration/constraint iterations;
- subsequent substeps use the prior and current points as Verlet state. `0x100ad1c0` advances each
  point by `(current - previous) * (step / previousStep) * damping`, applies gravity on Z, and
  clamps to the optional ground height;
- `0x100ad2c0` reconstructs each segment against the current posed chain, applies the spring
  coefficient, preserves the authored segment relationship, and caps the correction at
  `maxAngle`;
- a per-record enable mask at entity +0x788 ramps the solved matrices in or out through `bc_fade`;
  `bc_ground` optionally obtains a ground height before integration;
- `bc_override` substitutes the four global `bc_*` values for the record's authored values, which
  is also the independent field-name proof above.

The solver recomposes the corrected orientations into the bone-to-world buffer and blends against
the unsolved matrices while fading. It does not author local animation channels and does not alter
the `.mdl` skin weights.

**Standing: Verified by static control/data flow.** The full solve does not yet have a
game-independent numeric replay, so bit-for-bit standalone equivalence remains open even though
the retail algorithm and parameters are located.

## Hair: how geometry reaches the solver

Hair is ordinary skinned mesh geometry whose weights include the extra chain bones. Representative
source-to-generated comparisons show:

| Model / surface | Vertices | Weight evidence | Table evidence |
|---|---:|---|---|
| `Therese` / `therese_hair` | 577 | 319 effectively single-weight; remainder blends `Bip01 Head`, neck, and `Bone05`…`Bone34` chains | 11 hair-chain records plus 2 breast records |
| `Damsel` / `damselupperhair` + `damsaellowerhair` | 304 | head/neck plus `Bone01`…`Bone19` chain weights | 7 records; five 15° hair chains and two 30° roots |
| `VV` / `vv_hair` + `vv_hair_t` | 599 | head/neck plus the same numbered hair-chain family | 11 ten-degree hair records plus two 25° body records |

The two bodies selected for the bounded Unreal hair proof carry these exact head-parented records:

| Body | First-child route | gravity | damping | spring exponent | maximum |
|---|---|---:|---:|---:|---:|
| `malkavian_female_armor_0` | `Bone05` → `Bone06` → `Bone07` → `Bone08` → `Bone09` | 0.9 | 0.9 | 0.3 | 60° |
| `jeanette` | `Bone01` → `Bone03` → `Bone05` → `Bone06` → `Bone07` | 1.1 | 0.9 | 0.0 | 30° |
| `jeanette` | `Bone09` → `Bone10` → `Bone11` → `Bone12` → `Bone13` | 1.1 | 0.9 | 0.0 | 30° |

Every first moving bone above is parented to `Bip01 Head`. The same two model files also carry
breast records, which are independent records and are not hair. Jeanette's skirt is independent
again: it uses the renderer-cloth carrier, not either bone route. The other three Malkavian female
armour bodies are different model inputs and are not implied by the armour-0 record. **Standing:
Verified** from the installed v2531 records and bone trees.

The animation clips leave the affected chain locals at bind in the captured cases. The
table-driven stage supplies the dynamic orientation; StudioRender then applies it only to vertices
whose skin weights reference those bones. A head-rigid hair cap and a dynamic strand can therefore
share one character and even one material family without any special-case renderer.

The controlled `sp_theatre` capture confirms that the authored maximum is a real ceiling. Across
seven table-bearing models, 48 of 50 observed record maxima equal the authored angle; saturated
series sit on 10°, 15°, 20°, 25°, or 30° to float precision. A `Smiling_Jack` control with a loose
100° limit never reaches it. **Standing: Verified.** Flat ceiling-heavy series do not reveal the
underlying spring by correlation; the static solver does.

## Garment cloth: authored carrier and gate

The installed `MDLHeader.Flags` bit **`0x400`** is the garment-cloth gate. It agrees exactly with
the payload census: **60 of 4,445** v2531 models have the bit and a non-empty cloth payload; there
are zero flagged models without the payload and zero payload carriers without the flag. At
runtime, VEngineModel006's instance creation reads this bit at header +228 and asks
TStudioRender012 to create a per-model cloth-instance handle only when it is set.

The payload extends the 224-byte `StudioModel` and 60-byte `StudioMesh` records:

| Owner / off | Type | Runtime meaning |
|---|---|---|
| `StudioModel` +200 | int | cloth-definition count |
| `StudioModel` +204 | int | relative offset to a table of definition-record offsets |
| `StudioModel` +208/+212 | int/int | authored capsule count / model-relative 36-byte record array |
| `StudioModel` +216/+220 | int/int | authored sphere count / model-relative 20-byte record array |
| `StudioMesh` +48 | int | mesh-relative `uint8[NumVertices]` cloth-definition selector; `0xFF` means ordinary skinning |
| `StudioMesh` +52 | int | mesh-relative `uint16[NumVertices]` simulated position/normal index; high bit reverses the normal |
| `StudioMesh` +56 | int | mesh-relative `uint16[NumVertices]` simulated tangent-output index |

The counts govern all four model-level relative offsets. Empty capsule/sphere sets can retain a
nonzero offset value, so a decoder must not dereference one when its count is zero. Across all
4,567 `StudioModel` records, 60 carry definitions, 55 carry capsules, and 25 carry spheres. Across
9,741 meshes, **84** carry all three per-vertex maps and none carries only part of the triple.
Fifty-nine carrying model records have one definition and one has two.

This is why the earlier bone/weight audit was insufficient. `jeanette_skirt` really does carry
pelvis and thigh weights, but its selector map replaces the skinned result on 364 of 417 render
vertices. Sheriff's coat is integrated into the generically named `sheriffbody2`, so a
garment-material-name search misses it before even reaching the maps.

**Standing: Verified** by the installed corpus, the engine flag consumer, and the generated
StudioRender vertex routines that read all three maps.

### Cloth definition record

Each +204 table entry resolves from the `StudioModel` base to an authored definition of at least
92 bytes. Relative offsets inside the definition resolve from the definition record itself:

| Off | Type | Runtime meaning |
|---|---|---|
| +0x00 | float | gravity scale |
| +0x04 | int | total particle count |
| +0x08 | int | pinned/anchored particle count |
| +0x0c | int | dynamic particle count; pinned + dynamic = total |
| +0x10 | int | relative `uint16[]` source-vertex indices for the pinned particles |
| +0x14 | int | total constraint count |
| +0x18 | int | general distance-constraint count |
| +0x1c | int | compression-only constraint count |
| +0x20 | int | relative array of 16-byte constraint records |
| +0x24/+0x28 | int/int | packed SIMD block counts for the two constraint sets |
| +0x2c | int | relative packed SIMD constraint payload |
| +0x30 | int | collision-triangle count |
| +0x34 | int | relative `uint16[3]` collision-triangle array |
| +0x38/+0x3c | int/int | tangent-edge and normal-edge counts |
| +0x40 | int | relative edge-pair table |
| +0x44/+0x48 | int/int | normal-contribution count / relative contribution table |
| +0x4c/+0x50 | int/int | extra tangent-output count / relative interpolation table |
| +0x54/+0x58 | int/int | optional relative seed tables; their exact authoring names remain unknown |

A 16-byte constraint is `uint16 particleA`, `uint16 particleB`, two float movement weights,
and float rest-length-squared. The first set is solved unconditionally. The second is solved only
when current length squared is below rest length squared, so it resists compression rather than
enforcing distance in both directions.

### Authored collision records and current-pose frame

The `StudioModel` collision arrays have these exact on-disk layouts:

| Record / off | Type | Runtime meaning |
|---|---|---|
| capsule +0x00 | int | first endpoint bone |
| capsule +0x04 | int | second endpoint bone |
| capsule +0x08 | float | radius |
| capsule +0x0c | float3 | first endpoint in model bind space |
| capsule +0x18 | float3 | second endpoint in model bind space |
| sphere +0x00 | int | centre bone |
| sphere +0x04 | float | radius |
| sphere +0x08 | float3 | centre in model bind space |

The points are not stored in bone-local space. For every `DrawModel`, StudioRender first builds
`skinMatrix[bone] = currentBoneToWorld[bone] * poseToBone[bone]`, then transforms each authored
bind-space point through the named bone's current skin matrix. Capsule endpoints can therefore
follow different bones. Radius is copied without matrix scaling. The renderer builds this cache
once per draw; each cloth substep linearly interpolates capsule endpoints and sphere centres from
the previous cache to the current one.

**Standing: Verified.** `0x2c014a00` reads the 36-byte and 20-byte records with these strides and
field offsets, and uses the same current 0x30-byte matrix palette that the generated vertex
routines consume. `0x2c004e10` establishes that palette as bone-to-world times inverse bind.

### Lifetime, solve, and final vertex substitution

TStudioRender012 owns a 16-bit cloth handle per engine model instance and persistent cloth objects
keyed by that handle, the model variant, and the definition. Each object has double-buffered
particle positions. The relevant interface slots are +0x98 create handle, +0xa0 reset handle,
+0xa4 set wind/collision inputs, and +0xa8 advance and retire stale objects. The public
`cloth_reset` command walks every live engine model instance; StudioRender marks each object for
lazy reinitialization on its next draw. Retail also registers `r_cloth`, but the registration's
static destructor was initially mistaken for a change callback. Static xrefs do not establish the
ConVar's exact runtime gating semantics, so that remains a live-control question rather than a
claimed toggle rule.

On a cloth draw:

1. Source vertices named by +0x10 are skinned through the current bone palette and copied into the
   pinned particle prefix. Animation therefore drives the waist/shoulder attachment boundary.
2. The free particle suffix advances from the double-buffered positions. The integrator applies
   inertia/damping, the definition's gravity scale on Z, and the current wind force. Elapsed time
   is subdivided with a cap of 30 simulation steps.
3. Each substep integrates, solves the general distance set, solves the compression-only set,
   solves the general set again, then projects collision triangles against the model's authored
   capsules and spheres and any dynamic client collision records.
4. StudioRender regenerates normals and tangents from the simulated surface.
5. One of 72 generated vertex specializations reads `StudioMesh` +48/+52/+56. `0xFF` vertices keep
   ordinary skeletal output; selected vertices take the simulated position/normal/tangent output.

The +0x10 anchor-source list and the per-render-vertex output maps are independent. A source
vertex used to refresh a pinned particle can retain selector `0xFF` and render by ordinary
skinning, while another selected render vertex can read that same pinned particle. Jeanette's
selected position indices start at 28 although particles 0..55 are pinned; Sheriff's start at 50
although particles 0..90 are pinned. This is one pinning mechanism, not a second anchor path: a
selected vertex that names the pinned prefix receives a position already overwritten from the
skinned source vertex.

#### Gravity, integration and simulation clock

The definition's gravity field is a physical multiplier, not a raw Source gameplay-gravity value.
For a no-wind dynamic particle, `0x2c002a30` performs the equivalent of:

```text
ratio = 0.97 * currentStep / previousStep
next = current + ratio * (current - previous)
next.z -= gravityScale * 384 * currentStep * currentStep
```

The hard-coded acceleration is **384 model units/s²**. VtMB model units are inches, so this is
32 ft/s² or 975.36 cm/s²: approximately one physical g. The authored values 5 for Jeanette and 3
for Sheriff therefore mean approximately five and three g. Because acceleration is multiplied by
the squared substep duration, subdivision does not multiply or attenuate the total gravity over a
fixed elapsed interval.

There is no separate authored inertia coefficient. The previous displacement is retained by 0.97
per substep and corrected by the current-to-previous step-duration ratio. At the target cadence,
the equivalent retention over 1/60 s is `0.97^5 = 0.858734`. When the submitted wind vector is
nonzero, the previous displacement uses only the step-duration ratio and the wind displacement is
scaled by approximately `0.97 - 0.027 * abs(dot(windDirection, particleNormal))`; gravity remains
the same.

Elapsed time uses a 300 Hz target rather than a strict fixed-step accumulator:

```text
stepCount = min(30, round(elapsedSeconds * 300))
step      = elapsedSeconds / stepCount
```

At 60 and 30 Hz this selects five and ten substeps. The 30-step cap begins to matter above 0.1 s,
and an accumulated gap above 0.25 s marks the object for reinitialization instead of carrying stale
state. Initialization seeds the double buffers and performs 15
distance -> compression -> distance -> collision settling passes before ordinary advancement.

#### No animated-position leash

Only the pinned prefix is refreshed from the skinned pose. After initialization, a dynamic
particle's per-step path reads its two persistent positions, gravity, wind, the authored pair
constraints and collision inputs; it never reads that particle's skinned or rest-pose position.
There is no per-particle maximum displacement, animation-drive sphere or long-range tether in the
retail solve. The general distance graph, including edges from pinned to dynamic particles, is what
holds the free surface to the attachment boundary.

**Standing: Verified by complete static data flow through the advance, integration, constraint and
collision functions.** The optional definition seed tables' exact authoring roles remain unknown,
but neither is read as a per-frame dynamic-particle target in the located solve.

The client contributes horizontal wind in `C_BaseAnimating::InternalDrawModel` when the model has
flag `0x400`: angle selects XY direction, a 256-entry random table and changes-per-second vary the
speed between the replicated minimum and maximum, and Z is zero. The same call can contribute one
32-byte dynamic collision record derived from the local player's position and orientation; the
record carries radius-like value 18 and identifier 1. Server `CBaseAnimating::SetWind` and
`RemoveWind` own the replicated source, angle, change rate, minimum speed and maximum speed.

**Standing: Verified by static control/data flow.** The retained retail capture ends at bone setup
and draw attribution, before these particle buffers and substituted vertices. A post-skin retail
particle series and game-independent numerical replay remain the final equivalence test.

### Jeanette and Sheriff payloads

| Model / mesh | Header flags | Cloth selection | Definition | Collision |
|---|---:|---|---|---|
| `Jeanette.mdl` / `jeanette_skirt` | `0x500` | 364 of 417 vertices; 53 ordinary | gravity scale 5; 386 particles = 56 pinned + 330 dynamic; 934 distance + 849 compression-only constraints; 330 collision triangles | 3 capsules, 2 spheres |
| `Sheriff.mdl` / `sheriffbody2` | `0x440` | 2,028 of 8,310 vertices; 6,282 ordinary | gravity scale 3; 374 particles = 91 pinned + 283 dynamic; 833 distance + 760 compression-only constraints; 256 collision triangles | 6 capsules, 0 spheres |
| `Sheriff.mdl` / `sheriffhead` | `0x440` | 20 of 2,858 vertices; 2,838 ordinary | shares the same definition and output space | shares the same model primitives |

Jeanette's selected position indices span 28..385 and her tangent outputs 386..749. Sheriff's two
mapped meshes jointly select 2,048 vertices; their tangent outputs span 374..2,421, exactly the
definition's particle prefix followed by 2,048 extra tangent outputs. Those cross-record matches
are enforced by `research/tooling/probes/cloth_payload_audit.py` rather than copied by inspection.

The finalized `sp_theatre` capture proves Sheriff's exact model image is resident and drawn in
retail. It does **not** capture post-skin cloth state. The smallest closing capture hooks the
StudioRender step/output boundary for this one model, records particle positions, frame delta,
wind and collision inputs around `cloth_reset`, and compares them with a standalone replay. A
Jeanette standing/walking series then provides the clearer visual gravity and inertia acceptance.

## Bone-chain corpus and controls

Over the whole engine-resolved install, **107 of 4,445 v2531 models** carry the array: **600
records, zero structural faults**. Every +0 bone index is in range, every +4 value is `-1`, and
every array fits its model image. The first-bone names and parents are dominated by hair, mane,
ponytail and breast rigs; `Bip01 Head` is the parent on 320 of 600 records.

VPhysics is excluded independently. `Therese`, `VV`, and `Damsel` ship byte-identical `.phy`
files even though their table limits differ, and those solids name ragdoll body parts rather than
hair or garment bones. No affected bone declares `ProcType`, `Flags & 0x1`, or `Flags & 0x2`, and
VtMB's decoded procedural enum stops at AxisInterp rather than later Source's JIGGLE value.

## Confidence ledger and remaining unknowns

| Claim | Standing | What remains |
|---|---|---|
| +12/+16/+20/+24 are gravity/damping/spring exponent/max angle | **Verified** | A standalone regression should reproduce retail numerically, not rename the fields again. |
| The solve runs after hierarchy composition and writes render bone matrices | **Verified** | None for stage ownership; numeric replay remains separate. |
| Hair vertices move through normal CPU skinning of the corrected palette | **Verified** | Broader material-policy census is separate from deformation. |
| Installed flag `0x400` and `StudioModel`/`StudioMesh` extensions select renderer cloth | **Verified** | None for carrier identity or static gate. |
| Jeanette's skirt and Sheriff's coat substitute simulated vertices after ordinary skinning | **Verified** | Capture the post-skin particle/output series for numerical retail acceptance. |
| Cloth uses pinned skinned anchors, Verlet-like integration, distance/compression constraints, collision, wind, and regenerated tangent space | **Verified** | Implement and compare a standalone replay. |
| Cloth gravity is `gravityScale * 384 in/s^2`, integrated with squared substep time | **Verified** | A standalone replay should confirm numeric equivalence under controlled frame deltas. |
| Cloth targets 300 Hz with rounded subdivision, a 30-step cap, 0.97 no-wind retention and reset above 0.25 s | **Verified** | Host solvers still need an explicit parameter mapping and retail-series comparison. |
| Dynamic cloth particles have no animated-position leash or long-range tether | **Verified** | None for retail mechanism; any host leash is a deliberate approximation. |
| Capsule/sphere records are bind-space points transformed through the current skin palette per draw | **Verified** | None for layout or frame; capture remains useful for numerical output acceptance. |
| `r_cloth` exact runtime gating semantics | **Unknown** | Establish with a controlled live toggle or locate its indirect consumer; the apparent callback is its static destructor. |
| +4 is an explicit chain terminal when nonnegative | **Strong evidence** | No installed record exercises that branch. |
| +8's authoring meaning | **Unknown** | The located retail constructor does not consume it; values alone do not justify a name. |
| Exact `bc_ground` query semantics and the single-body `money` scale special case | **Unknown** | Trace the called interface and identify shipped chains that reach the special case. |
| Bit-for-bit game-independent solver replays | **Partial** | Compare both the bone-chain and renderer-cloth paths with controlled retail series. |

## Provenance

- Retail `client.dll`: 3,428,425 bytes, SHA-256
  `e88beae0dd03af06493c71c5e8d87a6993b54e590cb6ad37cd3513c588582870`, image base
  `0x10000000`.
- Retail `StudioRender.dll`: SHA-256
  `13d56ce90de2c5faedc0df26d36b24e90f30eded5055625f616cd97e301e124b`.
- Retail `engine.dll`: SHA-256
  `9d00b2c1e5edbad052fb0b514634ba54574666e12d1b408c46ce141d51ed2313`.
- Retail server `vampire.dll`: SHA-256
  `c546f4de2003624d72f54d03805e0dbe1d8157231adcc62368ff53fe6e48a76f`.
- Static anchors: table owner `0x10096650`, cleanup `0x100966e0`, chain constructor
  `0x100ac0f0`, chain update `0x100ac880`, integration `0x100ad1c0`, constraint/recomposition
  `0x100ad2c0`, and `BuildTransformations` call site `0x1009022c`.
- Cloth anchors: VEngineModel006 instance create/reset `0x200a7be0`/`0x200a8480`, client wind
  submitter `0x10092970`, TStudioRender012 vtable `0x2c06c150`, cloth get/create `0x2c001310`,
  simulation `0x2c001e50`, integration `0x2c002a30`, distance/compression solves
  `0x2c002d90`/`0x2c0030a0`, collision projection `0x2c0031a0`, tangent-space regeneration
  `0x2c002500`, and representative mapped-vertex routines `0x2c023590`/`0x2c03a2e0`.
- Runtime ceiling and transform evidence comes from the finalized `sp_theatre` capture, banked
  under `$ELYSIUM_WORK_ROOT/research`; evidence grades and capture method are defined in
  `docs/vtmb/vtmb-animation-reverse-engineering.md`.
- Corpus counts and cloth payloads use the engine-resolved patch-first install. The reproducible
  decoder is `research/tooling/probes/cloth_payload_audit.py`; generated reports remain below
  `$ELYSIUM_WORK_ROOT/research` and model outputs below `$ELYSIUM_EXPORT_ROOT`.
