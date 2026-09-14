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

The canonical lighting GLB preserves `worldLights[].cluster`, and the visibility GLB preserves every PVS row. The staged `light_rows` projection drops the cluster, `_LightRow` has no cluster or area field, and type 3 is placed as one unrestricted Unreal `DirectionalLight`. The live fitter requires a visibility GLB to exist but does not read its PVS. [Canonical field](E:/dev/elysium-unreal/pipeline/src/elysium_pipeline/formats/map_lighting_glb/model.py:238), [staged projection](E:/dev/elysium-unreal/pipeline/src/elysium_pipeline/exporters/UE_map_sidecars.py:880), [baked row](E:/dev/elysium-unreal/pipeline/unreal/bake_map_v2.py:1986)

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

The static model-light cache (`engine.dll 0x200abd90 → 0x200aa4b0`, PVS bypass argument zero) begins with the receiver's BSP cluster:

```text
receiverCluster = cluster(leaf(receiverPosition))
eligible(receiver, light) = PVS(receiverCluster)[light.cluster]
contribution = 0 when eligible is false
```

For point, spot and texlights, distance, cone, surface angle and occlusion apply after that gate. Type 3 has no distance falloff: after the PVS gate it traces along the sun direction and contributes only when the trace reaches a sky surface. Type 5 returns zero in the direct path and supplies the first skyambient colour to the ambient gather.

This is the model-cache contract. The stealth query's sun branch (`0x200a4e90`) bypasses PVS, and world surfaces render lump 8. Applying the model-cache mask to reconstructed Unreal world lighting is a visual modernization whose surface result must be checked against the bake. Do not change the gameplay light query to follow it. [Recovered distinction](E:/dev/elysium-unreal/docs/vtmb/lighting.md)

## Visibility scope

PVS is a hard eligibility mask, not attenuation. It must not lower candela gradually or be converted into a spherical radius. The light's photometry is fitted only inside its eligible receiver set.

For `sp_tutorial_1`, patch sun source **61** is in cluster **2627**. Exactly **395** receiver clusters include 2627 in their PVS, all in detached area **13**. The other **2,256** clusters reject it. Area 13 contains 810 clusters, so assigning the whole area is a coarse approximation. Patch skyambient source 60 shares cluster 2627 but follows the separate ambient-gather rule.

A four-map ablation applied `PVS(receiverCluster)[light.cluster]` before every sampled local-light contribution:

| Map | All sample/light pairs rejected by PVS | BSP ray-visible pairs rejected by PVS | Recovered direct-fit change |
|---|---:|---:|---:|
| `sp_tutorial_1` | 90.6% | 0% | none |
| `sm_hub_1` | 85.6% | 0% | none |
| `sm_pawnshop_1` | 57.1% | 0% | none |
| `sm_asylum_1` | 30.2% | 0% | none |

PVS is a conservative visibility set: in this sample it removes no pair that already passes exact BSP-world occlusion. The tutorial, hub and pawnshop candidate gains and all four recovered-direct results are unchanged. This ablation retained invalid source-cluster columns as unknown; there was one in tutorial and eleven in Asylum. It does not establish the invalid-cluster policy. The model-cache path skips such sources.

Applying PVS to Asylum's unshadowed texlight point proxy removed 8.0% of its predicted energy and worsened median error from 1.250 to 1.366 stops. PVS is not a replacement for texlight emitting-side response and geometric occlusion.

## Sun isolation across maps

The 2026-09-13 census inspected all **108** exported lighting GLBs and verified worldlight/PVS lump hashes against the source BSP for every sun-bearing map:

| Inventory | Count |
|---|---:|
| Maps without sun records | 83 |
| Maps with sun records | 25 |
| Sun records | 33 |
| Nonzero, valid-cluster sun records with receivers | 31 |
| Maps requiring a nonempty sun mask | 24 |

`hw_cemetery_1` has a zero-intensity sun. `sp_observatory_2` has one additional nonzero sun at cluster `-1`, rejected by the model cache. Every remaining sun has PVS-excluded receivers; these are potential scope leaks, not 24 confirmed visible defects, because shadows and sky visibility can already suppress them.

Within each map, all usable suns have **identical direction, RGB intensity and style**. The multiple-source cases therefore reduce to one directional contribution using the union of their PVS eligibility masks. Retail accepts the first successful sun per model lighting state; adding the duplicate intensities would overlight overlap regions.

| Map with multiple sun records | Usable sources | Union of eligible clusters |
|---|---|---:|
| `ch_temple_1` | 2, 4, 12 | 631 |
| `sm_warehouse_1` | 20, 22, 24, 58 | 1,024 |
| `sp_taxiride` | 33, 35, 37 | 231 |
| `sp_observatory_2` | 5; source 7 is invalid | 273 |

**Recommended projection: one PVS-masked directional light per active map using an Unreal Light Function.**

```text
allowed[c] = any(PVS(c)[sun.cluster] for valid equivalent suns)
mask(x)    = allowed[cluster(leaf(sourceSpace(x)))]
sun(x)     = one sun intensity × mask(x) × sky eligibility × shadow visibility
```

The live-tuning skill generates a pruned BSP decision tree in an uncompressed TGA/RGBA8 data texture and evaluates it at the shaded world position in a Light Function. The original float32 planes and child indices are preserved. A recorded 0.5-Source-inch bias toward the sun places surface-boundary queries on the open side. Leaf bounding boxes are not exact leaf volumes. Normal shadows remain active; a PVS bit alone does not prove a sky hit. Source-index mapping is retained when consolidating records.

This uses **no Lighting Channels** and needs no camera-based light switching. Unreal's ordinary Light Function path supplies absolute world position, and UE 5.8's Lumen surface-cache and stochastic lighting shaders evaluate that material as part of light attenuation. The world-position mask cannot use the 2D Light Function Atlas; Epic also excludes directional lights from that atlas. This is a supported rendering path to prototype, not a tested Elysium result. [Epic Light Functions](https://dev.epicgames.com/documentation/unreal-engine/using-light-functions-in-unreal-engine?lang=en-US), [world-position input](D:/Epic/UE_5.8/Engine/Shaders/Private/LightFunctionCommon.ush:72), [Lumen mask](D:/Epic/UE_5.8/Engine/Shaders/Private/Lumen/LumenSceneDirectLightingShadowMask.usf:197)

For a quick direct-light comparison, channels remain viable: keep channel 0 on every receiver for ordinary local lights, add channel 1 to sun-eligible receiver components, and put the sun on channel 1 only. Channel numbers can be reused per map. Mixed-scope meshes need splitting, moving actors need membership updates, and channels do not promise Lumen indirect isolation. Giving all of area 13 channel 1 would protect the tutorial but admit 415 extra Hunter clusters.

Pilot the Light Function on tutorial first, with source intensity fixed. Verify zero direct contribution in the main tutorial, preserved sunlight in eligible Hunter locations, and masked Lumen injection after settling. Test mesh and mask boundaries, skylit openings, reflections, fog/translucency, and distant cameras. Set the mask's fade distance beyond the supported scene and disabled brightness to zero so distance fading does not restore global sunlight. These checks are required before corpus application. Preserve SkyLight and gameplay-query semantics separately.

[Per-map census](E:/elysium-work/research/lighting-math-2026-09-07/sun-scope-census/maps.csv), [complete cluster sets and source hashes](E:/elysium-work/research/lighting-math-2026-09-07/sun-scope-census/census.json), [reproducible census](E:/elysium-work/research/lighting-math-2026-09-07/sun_scope_census.py).

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
| Visibility gate | Preserve `sourceCluster`; derive each receiver cluster from its BSP leaf and apply `PVS(receiverCluster)[sourceCluster]` before photometric terms. Preserve invalid-cluster cases explicitly. |
| Scope output | Record `sourceCluster`, `sourceArea`, eligible-cluster count/set, eligible areas and the visibility-unit hash in every candidate. Scope and intensity are independent outputs. |
| Unreal projection | Prefer a PVS-derived world-space Light Function for suns. Use channels for direct-light comparisons when receiver components can express the mask. A property-only pass must report an unresolved scope. |
| Spotlight cone | Preserve both cosine boundaries and exponent. Tutorial retail spots interpolate linearly between boundaries; UE squares its cone mask. Account for the resulting edge-energy difference. |
| Source data | Carry cluster and attenuation coefficients through staging; `light_rows` currently drops both. Preserve linear RGB without another Hammer gamma expansion. |
| Non-quadratic sources | Use an explicit exception or a documented approximation over relevant distances. |
| Texlights | Preserve emitting-side response, BSP occlusion and group energy; account for material emissive contributions. PVS alone cannot make the point proxy safe. |
| Sun | Apply receiver PVS and sky-hit eligibility. A scoped sun has no inferred radius. Block an unrestricted Unreal directional conversion when the source scope is smaller than the loaded world. |
| Skyambient | Retain a separate sky-cube mapping and the first-source rule. Calibrate independently of the point-light gain and sun scope. |

The 108-map census contains **406 non-quadratic locals**, four quadratic sources with `q=8`, and **2,429 texlights**. The tutorial rule needs those separate treatments before generalization. [Staged projection](E:/dev/elysium-unreal/pipeline/src/elysium_pipeline/exporters/UE_map_sidecars.py:889)

## Materials, bounce and sky

- **Test `Overbright=1` before fitting light gain.** V2 applies this scalar to BaseColor, changing reflectance and Lumen bounce; UE saturates the resulting BaseColor. Retail's gamma-space `base * lightmap * 2` is a different operation. [Material graph](E:/dev/elysium-unreal/pipeline/unreal/make_v2_materials.py:1133), [retail combine, `0x200718d0`](E:/dev/elysium-unreal/docs/vtmb/color_gamma.md:29)
- **Inspect actual surface-class shading.** `ClassInfluence=1` selects the class table, so global roughness/specular defaults do not describe every material. Keep specular and fog fixed during distance calibration. [Class lookup](E:/dev/elysium-unreal/pipeline/unreal/make_v2_materials.py:888)
- **Keep fixture indirect contribution at 1 for the candidate final rig.** Zero suppresses its Lumen surface-cache injection; use it for isolation only. Test selective fill reductions with both screen traces and surface-cache results. [UE injection](D:/Epic/UE_5.8/Engine/Source/Runtime/Renderer/Private/Lumen/LumenSceneDirectLighting.cpp:2103), [screen-trace limitations](https://dev.epicgames.com/documentation/en-us/unreal-engine/lumen-technical-details-in-unreal-engine)
- **Evaluate miniature-sky lighting separately.** V2 transforms positions by `16 * (position - sky_camera)` and gives these lights at least 50 m reach. At tutorial `info_player_start` `(-35.56,-19032.22,-416.56)` cm, **35 sky-light bounds overlap; 24 pass point/outer-cone checks**. At `houselights`, the counts are 10/8. These are candidates before shadows, normals and renderer culling. The `Sky/Lights` folder provides no isolation. Establish separation from playable direct lighting and GI before changing sky intensity or scale.

## Calibration pass

1. **Pin the baseline:** source map/version, final actor units and values, material collection/class table, effective exposure, tone curve, local exposure and rendering tier. `AutoExposure=False` is a default that cameras, volumes and viewport settings can override. [Exposure settings](https://dev.epicgames.com/documentation/unreal-engine/auto-exposure-in-unreal-engine?lang=en-US)
2. **Verify bake ownership:** use the existing `--no-light-store` option to bypass harvest and application for a controlled derivation; preserve authored stores and account for calibration assets. `AdoptBaked` retains actor values. An ordinary rebake can harvest and restore the previous set. [Store behavior](E:/dev/elysium-unreal/pipeline/unreal/light_store.py:1)
3. **Resolve visibility scope:** calculate source cluster/area and eligible receiver clusters before fitting. Fail directional conversion when that set cannot be represented by the selected Unreal partition. Do not use intensity to compensate for scope leakage.
4. **Isolate contributions:** compare direct diffuse, specular, indirect, sky locals, SkyLight, emissive and fog at fixed cameras. Inspect Lumen coverage and shadow visibility; allow temporal lighting to settle.
5. **Measure direct response:** use a neutral matte receiver with GI absent. Match illumination at one distance inside both radii, then compare other distances and cone angles. Keeping the same numeric intensity across falloff modes does not isolate shape.
6. **Evaluate the full rig:** restore fixture GI, preserve unclipped source ratios, then test suspected fills regionally. Compare both tutorial playable regions and transfer to `sm_hub_1`, an interior, and maps with texlights/non-quadratic sources before manual curation.

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

The live-tuning skill now emits a separate sun candidate with the local fit. `sun_scope.py` can also calculate it alone; `sun_editor.py` supports load/inspect/prepare/apply/restore. It saves only generated support assets, preserves the keeper's current intensity, suppresses equivalent duplicates and invalid-cluster suns, and leaves the level unsaved. The sun pass has its own transaction, disk baseline, property readback and rollback.

Offline validation processed all 108 maps and generated 24 masks. Packed-tree decisions matched original BSP/PVS decisions at **54,582 positions with zero mismatches**. The Custom-node HLSL compiled with UE's DXC. Tutorial's material was subsequently created and applied successfully in the live editor, with component readback verified. [Skill](<C:/Users/João Amaro/.codex/skills/elysium-live-light-tuning/SKILL.md>), [corpus verification](E:/elysium-work/research/live-light-tuning/skill-validation/corpus/validation.json).

## Forward-only live numerical correction

**Initialize from patch-first V2, then solve each region once.** Editor light values are neither model inputs nor a backup source. The source candidate supplies local candela, colour, radius, cones and falloff; indirect intensity starts at 1. Sun intensity/colour and the PVS mask are source-derived. SkyLight intensity is joined from V2 skyambient and the source cube's conserved upper-hemisphere mean.

Regions use BSP areas, bounded spatial/height groups, fixture triangulation and geometry-derived receiver probes. Strong broad groups run first in deterministic source-derived order. Other groups remain fixed while the active region is measured.

Each region measures its baseline, its isolated intensity response, a combined radius/indirect response, and one candidate. Baseline and candidate include training and validation views; response probes use training views only. The local search has fixed bounds: gain 1/16–64, radius up to 1.57 times source reach, and indirect strength up to 2.83. A region that is already readable skips fitting. A failed local candidate leaves that region at its V2 values.

**Completed regions are locked.** There are no neighbor-repair loops, global profile combinations, final rendering rescans or recursive attempts. Later spill can affect an earlier region; this order dependence is accepted for the one-pass experiment. Recorded image statistics describe each region when processed, not a globally validated final image. Final property readback does not change or solve regions again.

The pass reads floating-point render-target arrays with Lumen enabled, manual capture exposure matching the project default bias, and material/depth masks. It does not inspect screenshots, move the user camera, create permanent fill sources, save old editor values, or save the map. Cancellation returns only the unfinished region to V2 and retains completed regions. An explicit reset regenerates values from V2 without tuning.

This is a visual readability modernization. Gameplay lighting queries, source/style identity and actor transforms remain unchanged. The offline photometric scale and numerical thresholds remain model policy; they do not establish a retail fidelity percentage.

The implementation passes 50 automated tests. The patch-tutorial run completed in **8 minutes 35 seconds**, with **612 retained view measurements**. It initialized and verified **336 local lights**, processed **98 regions exactly once**, and retained changed parameters in **67 regions / 247 lights**. The remaining lights kept V2-derived initialization. Source 145 has no selected probe region and remains explicitly untuned. One region lacked stable usable probe coverage. The temporary capture was removed; the level remains unsaved. No old editor-value backups were created.

Local averages over the 97 regions with scorable views changed from **59.8% to 36.6% dark coverage** and **0.56% to 0.79% overbright coverage**. These measurements compare each region immediately before/after its own solve; they are not an audit of the final combined map. Completed regions were not revisited.

[Skill](<C:/Users/João Amaro/.codex/skills/elysium-live-light-tuning/SKILL.md>), [forward plan](E:/elysium-work/research/readability/sp_tutorial_1/20260914T012945555543Z/plan.json), [execution result](E:/elysium-work/research/readability/sp_tutorial_1/20260914T012945555543Z/result.json), [calculated light values](E:/elysium-work/research/readability/sp_tutorial_1/20260914T012945555543Z/corrected-lights.json).
