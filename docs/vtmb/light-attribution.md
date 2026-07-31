# Light attribution — separating VtMB's real fixtures from its fill lights

The per-map light set is curated **by hand** in the Lights Cog window — VtMB's lights are authored like a painting, for the baked result rather than as
physical fixtures, so no automatic rule ships — and the saved survey auto-applies at map load.
The classifier below is a candidate-ranker/advisor for that hand pass. Per-task status:
`docs/project/roadmap.md`.

## The question

VtMB has no global illumination. To keep interiors readable, its level authors spray soft wide
lights around a map that stand in for bounce — they light a volume rather than representing any
visible source. Alongside them sit the real lights: a lamp, a candle, a neon sign, a lit window.

Both kinds are the same `dworldlight_t` record in WORLDLIGHTS and the runtime treats them
identically, so `UElysiumLightRig` spawns a real Unreal light for each. Under Lumen the bounce is
now computed for real, so the fill lights are **doubled** contribution — they accumulate on top of
GI, wash out contrast, and (for the wide detached ones) throw long shadows from nothing.

The task is to tell the two apart from data, so the fill class can be dropped, dimmed, or converted
while the fixtures stay untouched.

**Scale of the problem (measured, 10 maps, 2,001 playable lights):** ~60% of lights sit on a
visibly emitting surface, ~20% are confidently fill, ~18% are undecided. The 20% figure is an
**upper bound** — it includes `sm_hub_1`, where the classifier's precision is 20%.

## Tooling

### The hand survey — the Lights Cog window

Each source carries an **Enabled** switch, orthogonal to the per-light attribute override: it
changes no value, so a light comes back exactly as it was, and it outranks both the rig's master
toggle and the window's isolate pass. Walking a map flipping that switch is how a labelled set is
produced.

Each source also carries a **Reviewed** mark — the "judged" bit, distinct from disabled: switching
a light off marks it reviewed by itself (a kill is a verdict), a kept light is marked by hand, and
re-enabling clears nothing. Copy-pasted lights (an identical colour/mag/radius/type/style tuple)
form a **batch** the selected light's panel acts on in one go — off, on, reviewed, or cycling the
members — matching the authored-batches finding below. **Next unreviewed** selects the nearest
unjudged light.

The save is the map's **standing hand-authored light state**: map load auto-applies its disabled +
reviewed sets (`UElysiumLightRig::LoadSurvey`; `elysium.LightSurvey 0` turns that off and loads
the full faithful rig — the A/B back to VtMB's as-authored source set), and the window's **Load**
button runs the same pass mid-session. The apply is additive — it sets marks, never clears them —
and attribute overrides are not restored either way.

**Save** writes `$ELYSIUM_EXPORT_ROOT/_lights/<map>.json` — one file per map, overwritten each save:

- `counts` (sources / disabled / overridden / reviewed) — the denominator, so "how many were left
  on" and coverage are answerable from the file alone
- `calibration` — the rig tuning the judgement was made under. Which lights read as redundant
  depends on how hard the rig was driving all of them, so the verdict is uninterpretable without it
- `edits[]` — only the edited sources, each with `index`, `row`, colour, position, magnitude,
  radius, style, intensity, and the disabled/overridden/reviewed flags
- `reviewed[]` — every reviewed source's `.lights` line index, disabled ones included. The
  coverage record: a source absent here was never judged, so scoring restricts to this list
  instead of inferring coverage from where the disabled lights sit

`index` is the source's **`<map>.lights` line**; `row` is its position in the window's list. They
are not the same — the skyambient row is skipped and sources are appended in the order the baked
level offers its actors up. Anything joining back to the sidecar keys on `index`.

### The probe — `ElysiumLightProbe`

Runs **in-engine**, against the real built scene, because the engine holds answers the exported
sidecars only approximate: Chaos traces against actual placed geometry (props included, already
transformed), `GetMaterialFromCollisionFaceIndex` names the surface hit, and the bound MID's
`EmissiveScale` says whether that surface actually glows — ground truth, not an inference from
`map_Ke` in the `.mtl`.

64 rays per light on `ELYSIUM_PICK_CHANNEL` (`bTraceComplex` + `bReturnFaceIndex`). Per light it
records:

| field | meaning |
|---|---|
| `near_dist`, `near_emissive`, `near_is_prop`, `near_surface` | **the nearest thing the light touches**, and whether it emits |
| `enc256`, `enc512`, `hit_frac`, `hit_med`, `hit_mean` | how boxed-in the light is, and the scale of the volume it occupies |
| `emissive_frac`, `prop_frac` | what the whole ray fan lands on |
| `share` | the fraction of the illumination arriving at the points this light reaches that comes from **this** light |

- `elysium.lightprobe [rays]` — live, in-session
- `-ElysiumProbe` / `dev/elysium.ps1 probe [map...]` — headless, one process per map; all ten exported maps in
  ~3.5 minutes. Writes `$ELYSIUM_EXPORT_ROOT/_lights/<map>.probe.json`

`<map>.proposal.json` holds the classifier's protected/fill/review index lists, ready to load as a
pre-selection.

## What the data supports

Measured against four hand surveys: `sp_tutorial_1` (72 of 210 disabled, lower area only),
`sm_oceanhouse_1` (30/163), `sp_giovanni_1` (12/153), `sm_hub_1` (36/627).

### The strongest single signal: what the light is touching

**If the nearest thing a light touches emits, it is a fixture.** Kill rates for that class:
`sm_oceanhouse_1` 0%, `sp_giovanni_1` 1%, `sm_hub_1` 3%, `sp_tutorial_1` 10%.

The asymmetry is the useful part: the test is reliable when it says *"there is a source here"* and
weak when it says *"there is not"*. It is a **protection** rule, not a fill detector. It is a strong
prior rather than a guarantee — it held 11 lights on `sp_tutorial_1` and 12 on `sm_hub_1` that were
killed by hand, so it must not be wired as an un-overridable lock.

Distances are the tell: fixtures and windows are hit at **3–21 cm**, plain brush at ~93 cm.

**Prop classification must read the prop's own materials, not its name.** A keyword list
(`lamp`/`bulb`/`sconce`/…) misses real emitters — `italian_itwndwb`, `la_wndweblit`,
`stake_labldgbs11/12` all carry emissive maps and match no keyword. Switching from name-matching to
material-emission moved `sp_giovanni_1` from 25% → 91% precision.

### Redundancy — `share`

A real light **owns** its patch; a fill light is one of many contributors. On `sm_hub_1`, median
`share` is **0.05** for hand-disabled lights against **0.65** for kept ones. Consistent in direction
on all four maps (AUC 0.18 / 0.19 / 0.04 / 0.24, low = fill).

### Detachment

A fill light floats; a fixture light is mounted. Median distance to the nearest anything:

| map | disabled | kept |
|---|---|---|
| sp_tutorial_1 | 142 cm | 20 cm |
| sm_hub_1 | 187 cm | 39 cm |
| sp_giovanni_1 | 1293 cm | 8 cm |

Direction holds on every map including interiors, where it was expected to vanish.

### Windows split in two, in opposite directions

Both halves are categorical on `sp_tutorial_1`:

- **translucent** glass (`blend 1`) — 16 lights nearest one, **all 16 disabled**. Real light should
  pass through, so the fake spill is redundant
- **opaque/lit** window (emissive, no blend) — 19 lights nearest one, **all 19 kept**. The light
  fakes an interior the player cannot see into

On `sm_hub_1` the lit-window class rescues 77 lights that would otherwise read as unmotivated — it
is a shopfront street, and that clause carries it.

### Authored batches

Level authors copy-paste lights, so an exact (colour, magnitude, radius, type) tuple identifies a
batch. On `sp_tutorial_1`, **83 of 85 batches are unanimous** in the hand verdict — the judgement is
effectively made per batch, not per light. Batch signatures do **not** transfer between maps (exact
colour tuples are per-map; zero cross-map matches on nine maps).

### The worldlight `type` and `style` columns

The sidecar's `type` (0 emit_surface, 1 point, 2 spot) and `style` fields carry signal the
geometric features do not; the current classifier reads neither.

- **`emit_surface` (type 0) is a texlight** — VRAD derives it from an emissive face, so it is
  attached to a visible source *by construction*. `sm_hub_1` has 20; 0 disabled. Auto-protect,
  no geometry test needed.
- **Spotlights are almost never fill on the failing map.** `sm_hub_1` kill rates: 1% of spots
  (2 of 367) vs 11% of points. 32 of its 104 fill candidates are spots and 31 of those were
  hand-kept — restricting fill to `type == point` alone lifts hub precision 20% → 28%. The
  semantics back it: a spot has an authored aim direction; fill-for-bounce is omnidirectional.
  A per-map **prior**, not a rule — spots are killed at 13–16% on `sp_giovanni_1` /
  `sm_oceanhouse_1`.
- **Styled lights are never fill in the surveys** — 11 across the four maps (10 on
  `sp_tutorial_1`, 1 on `sm_hub_1`), 0 disabled. Small sample, strong prior: a flicker pattern
  is an authored effect on a specific source.

### Per-map normalisation is mandatory

Absolute centimetre thresholds do not transfer — a 256 cm rule fitted on `sp_tutorial_1` scores 85%
precision there and 47% on `sm_oceanhouse_1`. The same rule expressed as *"top decile of detachment
for this map"* holds. Every threshold in the classifier is a within-map percentile.

## What the data does **not** support

Recorded so these are not re-derived:

- **Colour alone.** 84% of disabled lights are cool, but so are 52% of kept ones — 26% precision.
  Cool is a real term *in combination* (dropping it collapses `sm_oceanhouse_1` from 100% → 28%
  precision) but is worthless by itself.
- **`%cool` predicts `%fill` per map.** Holds on three maps (61%→34%, 18%→18%, 9%→7%), breaks on
  `sm_hub_1` (30%→5%). Three points was never a line.
- **Huge radius alone.** `radius ≥ 1024u` (26 m) is fill on `sm_hub_1` (11 of 13 disabled) and
  entirely legitimate on `sp_giovanni_1` (33 lights, **0** disabled). The separator is detachment:
  those 33 sit a median 28 cm from geometry, the hub's 13 sit at 252 cm. Reach is only meaningful
  together with detachment.
- **"Worst offenders only" as an explanation for `sm_hub_1`.** Tested and rejected — its disabled
  lights sit at the *median* of the fill-looking population (percentile 38–65), and the lights kept
  there have wider reach and more peers than the ones killed.
- **A separate unvisited area on `sm_hub_1`.** Tested and rejected — single-link clustering (1500 cm)
  finds one connected 531-light region plus a 96-light area at z ≈ −148 m.
- **Per-light NNLS deconvolution of the bake.** Solving per-light weights so the rig's direct
  irradiance reproduces the baked lightmap (lump 8) collapses: direct kernels are decorrelated
  from a bounce-dominated bake, so the solver dumps everything into a flat ambient and rails the
  weights to the clamp. Per-**light** attribution against the bake is ill-posed; per-**area**
  balancing is well-posed and is what `probe_light_attribution.py` ships as (the `.lightfit`
  per-cell rebalance). Full method and caveats in that tool's docstring.
- **Per-light `emissive_frac` as a killed-vs-kept separator inside the fill class.** The
  load-bearing-fill hypothesis — hand-kept fill sits where there is no emissive surface for
  Lumen to bounce — does not show at per-light scale: on `sm_hub_1`, killed-fill median 0.040 vs
  kept-fill 0.031, AUC 0.39 (slightly inverted). The **regional** form is untested and still
  plausible — the eastern strip (X > 7000) has half the west's median emissive surroundings
  (0.135 vs 0.313) — but a light's own ray fan does not measure it.

## Traps

- **The 3D skybox is not "up".** `sky_camera` sits *below* the level on `sp_giovanni_1` (z −9712)
  and `sp_observatory_1` (z −10063). Filtering skybox lights by altitude discards the entire map on
  those two. Identify the skybox band by **nearness to `sky_camera`**, never by height.
- **`radius == 0` means unbounded**, not smallest — the rig falls back to `FallbackRadiusCm`
  (2500 cm), the widest reach of all. Numeric comparisons invert if it is left as 0.
- **A map can hold more than one playable area.** `sp_tutorial_1` has a second one at ~170 m
  (125 lights) that the survey never covered; `sm_hub_1` has one at −148 m. Scoring must be
  restricted to the surveyed band or precision is measured against lights nobody looked at.
- **"Kept" is not the same as "judged" in the four existing surveys.** They predate the reviewed
  mark, so in their saves an unvisited light is indistinguishable from an examined-and-approved
  one. Coverage on `sp_tutorial_1` was verified after the fact (disabled lights span the full
  extent, 18 of 25 grid cells); the other three carry no such check. A survey made with the
  reviewed mark records coverage as data — score it against `reviewed[]` only.

## Classifier and measured scores

```
PROTECTED  the nearest surface emits                          -> keep
FILL       not emissive AND detached (>p60) AND redundant (share <p40)
REVIEW     everything else
```

| map | fill precision | recall | protected class wrongly held |
|---|---|---|---|
| sp_giovanni_1 | 100% | 92% | 1 |
| sm_oceanhouse_1 | 90% | 96% | 0 |
| sp_tutorial_1 | 75% | 55% | 11 |
| **sm_hub_1** | **20%** | 58% | 12 |

Across ten maps: 1,220 protected (60%), 409 fill (20%), 372 review (18%).

`hw_609_1` is the least trustworthy unlabelled result — 45% of its lights land in review, meaning
the rule largely abstains there.

**The unresolved disagreement is `sm_hub_1`.** Its hand survey retains many non-emissive lights
that the classifier ranks as fill. The map's authored sky term is documented in
`docs/vtmb/sky-ambience.md`; the classifier question and next work remain in `docs/project/roadmap.md`.

**No classifier-driven change to the light rig is justified yet.** Hand-authored curation is
decided and running; an *automatic*
removal or attenuation of a light class would be a further divergence needing the `sm_hub_1`
question closed and its own dated decision, per the remaster charter.
