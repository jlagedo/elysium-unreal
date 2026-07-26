# Light attribution — separating VtMB's real fixtures from its fill lights

**Status: investigation open, tooling landed.** The measurement tools, the data format, and four
hand surveys exist and are reusable. The classifier works on three of four surveyed maps and fails
on the fourth; no behavioural change has been made to the light rig, and none should be until the
open question at the bottom is closed. Nothing here is a decision — see `docs/decisions.md` for
what would have to be recorded before any light is actually removed.

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

**Save** writes `tools/out/_lights/<map>.json` — one file per map, overwritten each save:

- `counts` (sources / disabled / overridden) — the denominator, so "how many were left on" is
  answerable from the file alone
- `calibration` — the rig tuning the judgement was made under. Which lights read as redundant
  depends on how hard the rig was driving all of them, so the verdict is uninterpretable without it
- `edits[]` — only the edited sources, each with `index`, `row`, colour, position, magnitude,
  radius, style, intensity

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
- `-ElysiumProbe` / `probe.bat [map...]` — headless, one process per map; all ten exported maps in
  ~3.5 minutes. Writes `tools/out/_lights/<map>.probe.json`

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
A real light is *touching* its source object.

**Prop classification must read the prop's own materials, not its name.** A keyword list
(`lamp`/`bulb`/`sconce`/…) misses real emitters — `italian_itwndwb`, `la_wndweblit`,
`stake_labldgbs11/12` all carry emissive maps and match no keyword. Switching from name-matching to
material-emission moved `sp_giovanni_1` from 25% → 91% precision.

### Redundancy — `share`

A real light **owns** its patch; a fill light is one of many contributors. On `sm_hub_1`, median
`share` is **0.05** for hand-disabled lights against **0.65** for kept ones. Consistent in direction
on all four maps (AUC 0.18 / 0.19 / 0.04 / 0.24, low = fill).

This is the most direct statement of "standing in for bounce" available from geometry alone.

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

## Traps

- **The 3D skybox is not "up".** `sky_camera` sits *below* the level on `sp_giovanni_1` (z −9712)
  and `sp_observatory_1` (z −10063). Filtering skybox lights by altitude discards the entire map on
  those two. Identify the skybox band by **nearness to `sky_camera`**, never by height.
- **`radius == 0` means unbounded**, not smallest — the rig falls back to `FallbackRadiusCm`
  (2500 cm), the widest reach of all. Numeric comparisons invert if it is left as 0.
- **A map can hold more than one playable area.** `sp_tutorial_1` has a second one at ~170 m
  (125 lights) that the survey never covered; `sm_hub_1` has one at −148 m. Scoring must be
  restricted to the surveyed band or precision is measured against lights nobody looked at.
- **"Kept" is not the same as "judged".** The save records disabled lights only, so an unvisited
  light is indistinguishable from an examined-and-approved one. Coverage on `sp_tutorial_1` was
  verified after the fact (disabled lights span the full extent, 18 of 25 grid cells). A survey of a
  large map cannot be trusted this way without an explicit reviewed mark, which does not exist yet.

## Current classifier and its scores

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

## Where to pick up

**The one open question is `sm_hub_1`.** 249 of its lights touch nothing emissive and ~90% of them
were kept by hand. Every feature built — geometric, redundancy, ray-traced — says they look like
fill. It is the only map where the classifier and the hand verdict genuinely disagree, and it holds
104 of the 409 fill candidates.

A 15-light shortlist of the highest-confidence disagreements exists, all in the map's eastern strip
(X > 7000), several of them `mag 17476 / reach 984u / share 0.05` — numerically indistinguishable
from lights killed in the west. Resolving those 15 decides whether the rule over-flags on dense
maps or the survey was uneven.

Other threads, in rough value order:

1. Survey one more map to break the 3-of-4 tie — `hw_609_1` is the most informative (lowest
   protected share, highest abstention).
2. Add a **reviewed** mark to the save, distinct from disabled, so coverage stops being an inference.
3. Batch-level voting (classify the authored batch, not the light) — 83/85 unanimity on
   `sp_tutorial_1` suggests it would raise precision, untested elsewhere.
4. Visual adjudication: the MCP server exposes teleport + screenshot, so the disagreement cases can
   be inspected directly rather than argued from scalars.

**No change to the light rig is justified yet.** Any actual removal or attenuation of a light class
is a behavioural divergence from VtMB and needs the faithful behaviour recorded plus a dated entry
in `docs/decisions.md`, per the remaster charter.
