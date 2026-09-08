# VtMB-to-Unreal light conversion

## Current conversion

[derive_light](E:/dev/elysium-unreal/pipeline/unreal/bake_map_v2.py:1839) derives playable local lights as:

```text
colour    = rgb / max(rgb)
intensity = min(max(rgb) * 0.003, 8)
reach     = positive authored radius, otherwise 2500 cm
falloff   = inverse-square OFF, exponent 1
indirect / specular / volumetric scattering = 1 / 1 / 1
```

Point/spot shadows are enabled. Intensity units are not explicitly preserved through placement and the light store. These are derivation values; baked actors and calibration overrides can differ.

Measurements from the patch-first `sp_tutorial_1` BSP and matching exported/staged rows:

| Quantity | Value |
|---|---:|
| Playable point/spot lights | **336**: 184 points, 152 spots |
| Miniature-skybox locals | **58** |
| Sun / skyambient | 1 / 1 |
| Playable attenuation | All `(c,l,q) = (0,0,1)` |
| Zero-radius playable lights | 49 |
| Median positive radius | 650.24 cm |
| Hammer brightness min / median / max | 1 / 50 / 3500 |
| Derived intensity min / median / max | 0.05939 / 3.33418 / 8 |
| Lights clipped at 8 | **96/336 = 28.6%** |

UE's non-inverse-square kernel is `max(0, 1-(d/R)^2)^p`. At exponent 1 and radius 25 m, it retains **84% at 10 m** and **36% at 20 m**. This broad response increases overlapping illumination. Exponent 8 remains a radius-relative polynomial. [UE attenuation](D:/Epic/UE_5.8/Engine/Shaders/Private/DynamicLightingCommon.ush:20)

## Retail response

For point/spot lights, before cone, receiver-normal, visibility and style terms:

```text
D(d) = c + l*d + q*d^2                       # d in Source inches
D100 = c + 100*l + 10000*q
I    = (Hammer_RGB/255)^2.2 * (brightness/255) * D100
S(d) = I / D(d)
S100 = I / D100
```

`I` is the compiled RGB numerator. Recover the source contribution at 100 units by dividing by `D100`. Its absolute scale has no established SI calibration. The equation matches all 336 tutorial sources within float32 error. [Transfer evidence](E:/dev/elysium-unreal/docs/vtmb/sky-ambience.md:1285)

Retail `engine.dll`:

- **`0x200b6830` — loader:** zero point/spot attenuation becomes `q=1`; zero spot exponent becomes 1; radius below one Source unit becomes zero.
- **`0x200a5620` — distance:** reciprocal `D(d)`; positive radius provides a hard cutoff unless bypassed by the caller. Zero radius leaves attenuation active.
- **`0x200a9ee0` — eligibility:** style scaling, contribution threshold and visibility checks.
- **`0x200a57e0` — angle:** receiver cosine, spotlight cone/exponent and texlight emitting-side response.

World surfaces use baked lump-8 lighting. Lump-15 lights feed the model light cache: normally two direct slots, with other eligible contributions folded into ambient (`0x200aa4b0`). The Unreal world rig reconstructs the baked appearance using live lights and Lumen. Authored fills and radiosity coexist in the bake. [Retail lighting chain](E:/dev/elysium-unreal/docs/vtmb/sky-ambience.md:675)

## Conversion recommendations

**Use explicit candela and inverse-square for pure-quadratic playable locals; preserve authored intensity ratios.** Calibrate material response and exposure before selecting the final gain.

For `c=l=0, q>0`:

```text
candidate_cd = K * max(I) * 0.00064516 / q
```

`0.00064516 = 0.0254^2` converts the distance scale. **K remains a measured/artistic calibration**, expressed as UE lux per unit of Source contribution at a reference receiver. For `q=1`, a multiplier of `0.003` corresponds to `K approximately 4.65`; neither value is established as the correct gain.

Select candela explicitly across derivation, placement, harvest, persistence and calibration. Under UE inverse-square, the same numeric value in candela produces 625 times the legacy-unitless response. Spot lumens also change on-axis illumination with cone angle. [UE physical units](https://dev.epicgames.com/documentation/unreal-engine/using-physical-lighting-units-in-unreal-engine)

| Property | Recommendation |
|---|---|
| Intensity ceiling | Remove the artistic clamp at 8 while retaining finite-value validation. The extended ceiling of 512 avoids tutorial clipping at gain 0.003, but remains a ceiling. |
| Positive radius | Start with the authored radius; measure edge darkening. UE's smooth window retains approximately 88% at half-radius and 35% at 80%, unlike retail's hard clip. |
| Zero radius | Derive finite reach from a contribution threshold and budget. `R_metres = sqrt(candidate_cd / E_cut_lux)` is an ideal point-light bound; check aggregate omitted energy. |
| Spotlight cone | Preserve both cosine boundaries and exponent. Tutorial retail spots interpolate linearly between boundaries; UE squares its cone mask. Account for the resulting edge-energy difference. |
| Source data | Carry attenuation coefficients through staging; `light_rows` currently drops them. Preserve linear RGB without another Hammer gamma expansion. |
| Non-quadratic sources | Use an explicit exception or a documented approximation over relevant distances. |
| Texlights | Preserve emitting-side response and group energy; account for material emissive contributions. |
| Sun / skyambient | Retain separate directional and sky-cube mappings. Calibrate independently of the point-light gain. |

The 108-map census contains **406 non-quadratic locals**, four quadratic sources with `q=8`, and **2,429 texlights**. The tutorial rule needs those separate treatments before generalization. [Staged projection](E:/dev/elysium-unreal/pipeline/src/elysium_pipeline/exporters/UE_map_sidecars.py:889)

## Materials, bounce and sky

- **Test `Overbright=1` before fitting light gain.** V2 applies this scalar to BaseColor, changing reflectance and Lumen bounce; UE saturates the resulting BaseColor. Retail's gamma-space `base * lightmap * 2` is a different operation. [Material graph](E:/dev/elysium-unreal/pipeline/unreal/make_v2_materials.py:1133), [retail combine, `0x200718d0`](E:/dev/elysium-unreal/docs/vtmb/color_gamma.md:29)
- **Inspect actual surface-class shading.** `ClassInfluence=1` selects the class table, so global roughness/specular defaults do not describe every material. Keep specular and fog fixed during distance calibration. [Class lookup](E:/dev/elysium-unreal/pipeline/unreal/make_v2_materials.py:888)
- **Keep fixture indirect contribution at 1 for the candidate final rig.** Zero suppresses its Lumen surface-cache injection; use it for isolation only. Test selective fill reductions with both screen traces and surface-cache results. [UE injection](D:/Epic/UE_5.8/Engine/Source/Runtime/Renderer/Private/Lumen/LumenSceneDirectLighting.cpp:2103), [screen-trace limitations](https://dev.epicgames.com/documentation/en-us/unreal-engine/lumen-technical-details-in-unreal-engine)
- **Evaluate miniature-sky lighting separately.** V2 transforms positions by `16 * (position - sky_camera)` and gives these lights at least 50 m reach. At tutorial `info_player_start` `(-35.56,-19032.22,-416.56)` cm, **35 sky-light bounds overlap; 24 pass point/outer-cone checks**. At `houselights`, the counts are 10/8. These are candidates before shadows, normals and renderer culling. The `Sky/Lights` folder provides no isolation. Establish separation from playable direct lighting and GI before changing sky intensity or scale.

## Calibration pass

1. **Pin the baseline:** source map/version, final actor units and values, material collection/class table, effective exposure, tone curve, local exposure and rendering tier. `AutoExposure=False` is a default that cameras, volumes and viewport settings can override. [Exposure settings](https://dev.epicgames.com/documentation/unreal-engine/auto-exposure-in-unreal-engine?lang=en-US)
2. **Verify bake ownership:** use the existing `--no-light-store` option to bypass harvest and application for a controlled derivation; preserve authored stores and account for calibration assets. `AdoptBaked` retains actor values. An ordinary rebake can harvest and restore the previous set. [Store behavior](E:/dev/elysium-unreal/pipeline/unreal/light_store.py:1)
3. **Isolate contributions:** compare direct diffuse, specular, indirect, sky locals, SkyLight, emissive and fog at fixed cameras. Inspect Lumen coverage and shadow visibility; allow temporal lighting to settle.
4. **Measure direct response:** use a neutral matte receiver with GI absent. Match illumination at one distance inside both radii, then compare other distances and cone angles. Keeping the same numeric intensity across falloff modes does not isolate shape.
5. **Evaluate the full rig:** restore fixture GI, preserve unclipped source ratios, then test suspected fills regionally. Compare both tutorial playable regions and transfer to `sm_hub_1`, an interior, and maps with texlights/non-quadratic sources before manual curation.

Record pre-tonemap values where available, shadow-to-lit ratios, highlight clipping, fixture readability and fixed-camera images. Raw luxels and displayed screenshots require consistent color/exposure transforms.

Automated acceptance should cover coefficient/unit preservation, store round-trips, radius/cone behavior, sky transforms and source/style identity. Preserve entity state, lightstyle timing and Source I/O. The fill classifier's measured precision on `sm_hub_1` is only 20%; use it to rank review candidates. [Attribution evidence](E:/dev/elysium-unreal/docs/vtmb/light-attribution.md)

**Rendered acceptance:** determine final gains and each component's contribution to washout through owner-piloted comparison.

## Offline experiment: four maps

63,533 area-weighted observations from playable planar surfaces were compared against the static baked layer. Each model received one fitted global scale; different faces were held out. The model includes BSP-world occlusion, with no Lumen, material shading or prop shadows.

| Model / median error in stops | Tutorial | Hub | Pawnshop | Asylum |
|---|---:|---:|---:|---:|
| Current broad, ceiling 8 | 0.995 | 2.060 | 0.794 | 1.741 |
| Broad, unclipped | 0.977 | 1.559 | 0.657 | 1.688 |
| Exponent 8, ceiling 8 | 1.246 | 2.223 | 0.532 | 1.572 |
| Inverse-square, clipped | 0.545 | 1.108 | 0.431 | 1.393 |
| Inverse-square, unclipped | 0.453 | 0.779 | 0.268 | 1.250 |
| Recovered Source direct | 0.337 | 0.540 | 0.245 | 0.498 |

Errors use log2((prediction + 0.25)/(baked + 0.25)) to retain dark samples. The inverse-square advantage over broad falloff survives different dark floors and ray-start offsets on all four maps.

**Use inverse-square and preserve intensity ratios for point/spot lights. Treat texlights separately.** Asylum has 376 texlights among 448 sources. Adding emitting-side response and world occlusion to its inverse-square texlight proxy reduces median error from **1.250 to 0.500 stops**, close to the Source-direct result.

**Use face-plane normals in calibration.** Existing probes' winding-derived normals oppose them on every sampled face in all four maps.

Under the hypothetical convention UE lux = baked Y / 255, tutorial/hub/pawnshop yield candela coefficients of 0.0008695 / 0.0009025 / 0.0008896 times compiled RGB magnitude. Asylum yields 0.0001419 with the current texlight point representation and 0.0007030 after the directional/occlusion correction. A universal gain is premature until source types are handled correctly.

Group fitting adds limited value after inverse-square on tutorial/hub/pawnshop. In Asylum, correcting texlight response improves the fit more than group adjustment. All gains remain conditional on this direct-only model.

[Four-map experiment, heatmaps, regional estimates and reproducible arithmetic](E:/elysium-work/research/lighting-math-2026-09-07/results.md).

## Whole-corpus experiment

All **108 map sets / 432 V2 map GLBs** are present and readable; the lighting/entity source hashes match the BSP corpus. The offline run scored **108/108 maps** using **1,596,396 retained observations**.

With one global scale fitted per model/map, inverse-square plus texlight direction/occlusion improves held-out median error on **95 maps**, ties on **2**, and worsens **11**. The median of per-map errors falls from **1.119 to 0.586 stops**.

**Keep per-map exceptions visible.** 34 maps contain non-quadratic source records; 31 have them active in this comparison. The current inverse-square hypothesis cannot preserve that attenuation. The run produces research results and complete source inventories, not authoritative bake parameters.

[Corpus report, per-map CSV, heatmaps and source inventories](E:/elysium-work/research/lighting-math-2026-09-07/corpus/results.md).

## Editor property pilot

Candidate files are ready for sp_tutorial_1, sm_pawnshop_1 and sm_hub_1: **1,244 sources listed, 1,093 local-light edits**. They use the tested inverse-square/unclipped profile with provisional candela scaling. Hub texlights remain point approximations in this pass.

Apply to existing editor-world lights before Play, with no build/import. The scratch helper previews changes, captures a baseline, applies an undo transaction and verifies property readback. It does not save the level. Materials, exposure, indirect/specular/fog multipliers and sky/environment sources retain their existing values.

[Files, preview/apply/restore commands and validation](E:/elysium-work/research/lighting-math-2026-09-07/live-pilot/README.md).
