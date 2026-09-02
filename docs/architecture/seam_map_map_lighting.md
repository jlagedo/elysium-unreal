# Map-lighting GLB seam

This document defines the lighting sub-unit of one VtMB BSP map: the baked lightmap samples,
the compiled light-source set, the displacement lighting tables and the detail-prop lighting
payload, each stated once and exactly. The map root (`seam_map_map.md`) owns the header, the
lump directory, the face table and the partition proof; shared rules are owned by
`seam_map_unit_contract.md`; the meaning of the light records and the bake is owned by
`docs/vtmb/lighting.md` and `docs/vtmb/sky-ambience.md`.

## Unit identity

```text
<VTMB>/Unofficial_Patch -> maps/<map>.bsp  (lumps 8, 15, 32, 34 and the dplt game-lump span)
  -> vtmb:map-lighting:<map>
  -> $ELYSIUM_EXPORT_V2_ROOT/maps/<map>.lighting.glb
```

The key is the map stem. The one `sourceResolution` member is the BSP with one `span` per owned
lump; the ledger is gapless over those spans and nothing else.

```text
uv run elysium export_v2 map-lighting-glb <map>
uv run elysium export_v2 map-lighting-glb --all
```

## Lump partition

| Lump | Name | Maps | Bytes (all maps) | Destination |
|---:|---|---:|---:|---|
| 8 | LIGHTING | 108 | 254.7 MB | `samples` accessor, `faces[]` spans |
| 15 | WORLDLIGHTS | 108 | 1.7 MB | `worldLights[]` |
| 32 | DISP_LIGHTMAP_ALPHAS | 48 | 1.2 MB | `dispAlphas` accessor |
| 34 | DISP_LIGHTMAP_SAMPLE_POSITIONS | 48 | 4.7 MB | `dispSamplePositions` accessor |
| 35 `dplt` | GAME_LUMP entry, version 0 | 52 | — | `detailPropLighting[]` |

The root owns the game-lump directory (`count` and the 16-byte entry rows); this unit owns the
`dplt` entry's payload span, which the root's `partition` lists under this unit's identity. On
sp_tutorial_1 the payload is 4,964 bytes: an `int` count of 992 followed by 992 five-byte
records, `ColorRGBExp32` plus a `uint8` style.

## Source closure

| Source member | Role | GLB destination |
|---|---|---|
| lump 8 | the bake | one `SCALAR` `UNSIGNED_BYTE` accessor over the whole lump |
| lump 15 | light sources | `worldLights[]` |
| lumps 32, 34 | displacement lighting | two accessors |
| `dplt` payload | detail-prop lighting | `detailPropLighting[]` |
| lump 7 (root) | per-face lightmap placement | `faces[]`, `derived` |
| lump 6 (root) | `SURF_BUMPLIGHT` per texinfo | `faces[].bumped`, `derived` |

`faces[]` restates, per face, the five root fields that locate its samples — `lightofs`,
`styles[8]`, `LightmapMins[2]`, `LightmapSize[2]` and `avgLightColor[8]` — and the texinfo flag
that multiplies them. Those bytes belong to the root's ledger; here they are `derived`, so the
unit is inspectable on its own and the export-time validator cross-checks every row against the
root's face table before either unit is written.

## Sample representation

A lightmap sample is Source's `ColorRGBExp32`: three `uint8` mantissas and one `int8` shared
exponent, four bytes, linear light. The unit stores lump 8 as **one `SCALAR` `UNSIGNED_BYTE`
accessor over the whole lump**, byte for byte, and `faces[]` rows locate each face's samples in
it. There is no per-face image, no RGB8 preview and no KTX2 wrapping: any of those would be a
second, lossy statement of the same bytes, and the exponent makes the format irreducible to an
8-bit texture. The accessor's `count` is the lump length, so the ledger range and the accessor
are the same span from two sides.

A face's samples occupy

```text
luxelWidth   = LightmapSize[0] + 1
luxelHeight  = LightmapSize[1] + 1
styleCount   = number of styles[s] != 0xFF
lightmapSets = 4 if texinfo.flags & SURF_BUMPLIGHT (0x800) else 1
byteLength   = luxelWidth * luxelHeight * 4 * styleCount * lightmapSets
```

starting at `lightofs`, ordered set-major, style-minor, then rows of luxels. A bumped face carries
the flat lightmap followed by the three bump-basis lightmaps for every style. The row publishes
`lightOffset`, `luxelWidth`, `luxelHeight`, `styles[]`, `bumped`, `byteLength` and one
`spans[]` entry per `(set, style)` pair with its `offset` and `length` in the accessor.

Over the 108 maps, with that rule, no face span runs past the lump and no two spans overlap.
Faces with `lightofs == -1` (135,273 corpus-wide) have no samples and no spans. Twenty-seven
maps hold bytes that no face span claims — 610,908 bytes over 254.7 MB, from 1,468 on
`sm_smoke_1` to 52,188 on `la_museum_1`; those are `omissions[] orphan-lighting-bytes`, one row
per contiguous run with `offset`, `length` and `sha256`, claimed `omitted-proven` in the ledger.
Their origin — faces retained only through ORIGINALFACES, or compiler slack — is not established,
and the row says so in `reason`.

`styles[8]` is live; the root's `day[8]` and `night[8]` arrays are dead storage on every face of
every map and are not restated here. The map-wide style census (`styleCensus[]`) counts faces per
style value: sp_tutorial_1 uses styles 0, 1, 6, 32, 33 and 34; sm_hub_1 uses 0 and 10.

## World lights

Lump 15 is the standard 88-byte `dworldlight_t`, 396 records on sp_tutorial_1 and 687 on
sm_hub_1, and the lump length is a whole multiple of 88 on every map. One `worldLights[]` row per
record restates every field:

| Field | Offset | Published as |
|---|---:|---|
| `origin` | 0 | `{source, gltf}` |
| `intensity` | 12 | linear RGB, unbounded (values above 143,000 occur) |
| `normal` | 24 | `{source, gltf}` direction |
| `cluster` | 36 | int |
| `type` | 40 | int plus name: `0` emit_surface, `1` point, `2` spot, `3` skylight, `4` quakelight, `5` skyambient |
| `style` | 44 | int |
| `stopdot`, `stopdot2`, `exponent` | 48, 52, 56 | floats |
| `radius` | 60 | float, source inches and metres |
| `constant_attn`, `linear_attn`, `quadratic_attn` | 64, 68, 72 | floats |
| `flags` | 76 | int |
| `texinfo` | 80 | int |
| `owner` | 84 | int |

Every row keeps `sourceOffset`. `lightTypeCensus[]` counts rows per type; the corpus facts —
25 maps carry a skylight/skyambient pair and the two never separate, `sm_hub_1` carries no sky
pair — are what a consumer checks against these counts. No `KHR_lights_punctual` objects are
emitted: the punctual model has no emit-surface, skyambient, `stopdot2` or exponent, so it would
be a lossy second statement of the rows.

## Displacement lighting and detail-prop lighting

Lump 32 is one `uint8` alpha per displacement lightmap luxel and lump 34 one `uint8` sample
position per luxel, in dispinfo order; each is stored as one `SCALAR` `UNSIGNED_BYTE` accessor
and `displacements[]` rows (`derived` from the root's `dispinfo` `DispVertStart`-style offsets)
locate each displacement's run. `detailPropLighting[]` is one row per five-byte record:
`{r, g, b, exponent, style}` with `sourceOffset`. A `dplt` payload of exactly four bytes (count 0,
as on sm_hub_1) is a complete, empty table.

## GLB structure

```text
map.lighting.glb
|- JSON chunk
|  `- extensions.ELYSIUM_vtmb_map_lighting
`- BIN chunk
   |- samples          SCALAR UNSIGNED_BYTE, lump 8 verbatim
   |- dispAlphas       SCALAR UNSIGNED_BYTE, lump 32 verbatim
   `- dispSamplePositions  SCALAR UNSIGNED_BYTE, lump 34 verbatim
```

The unit is scene-less; the accessors are reached through the extension.

```json
{
  "asset": {"version": "2.0", "generator": "Elysium Map-lighting GLB Exporter"},
  "extensionsUsed": ["ELYSIUM_vtmb_map_lighting"],
  "extensionsRequired": ["ELYSIUM_vtmb_map_lighting"],
  "extensions": {
    "ELYSIUM_vtmb_map_lighting": {
      "schemaVersion": "1.0.0",
      "identity": {},
      "sourceResolution": {},
      "coordinateTransform": {},
      "map": {},
      "samples": {},
      "faces": [],
      "styleCensus": [],
      "worldLights": [],
      "lightTypeCensus": [],
      "dispAlphas": {},
      "dispSamplePositions": {},
      "displacements": [],
      "detailPropLighting": [],
      "dependencies": [],
      "anomalies": [],
      "omissions": [],
      "coverage": {}
    }
  }
}
```

## Extension reference

| Key | Contents |
|---|---|
| `map` | `stem`, `mapRevision`, the owned lumps' offsets and lengths |
| `coordinateTransform` | the unit contract rule for `worldLights` origins and normals; luxels and samples stay in source units |
| `samples` | `accessor`, `byteLength`, `sha256`, `format: "ColorRGBExp32"` |
| `faces` | one row per root face, `derived` |
| `styleCensus`, `lightTypeCensus` | counts |
| `worldLights` | the lump 15 rows |
| `dispAlphas`, `dispSamplePositions` | accessor references with length and digest |
| `displacements` | per-dispinfo runs into both accessors |
| `detailPropLighting` | the `dplt` rows |
| `anomalies` | `lightofs-out-of-range`, `face-without-lightmap`, `span-overlap`, `worldlight-length-not-multiple`, `dplt-count-mismatch` |
| `omissions` | `orphan-lighting-bytes` |

`face-without-lightmap` is informational — a `lightofs` of `-1` is authored and common — and is
emitted as a count with the face index list, not as a failure.

## Dependencies

| Role | Produced by |
|---|---|
| `map` | the root unit, whose face and texinfo tables the `derived` rows restate |
| `map-entities` | the `light*` entities the world lights were compiled from; the join is by origin and is a consumer computation |

## Byte ledger owners

| Owner | Range |
|---|---|
| `lighting.faces[i].set[k].style[s]` | one face's samples for one set and style |
| `lighting.orphan[n]` | one unclaimed run, `omitted-proven` |
| `worldLights[i]` | one 88-byte record |
| `dispAlphas`, `dispSamplePositions` | the whole lump |
| `dplt.count`, `dplt.records[i]` | the header int and one 5-byte record |

Every claimed range is `mapped`; lump 8's claimed bytes are `mapped` because they are copied
verbatim into an accessor whose every byte `faces[]` describes.

## Coverage and validation

A complete unit has zero `unresolved` and zero `unsupported` rows. Export-time validation
re-reads the root's face and texinfo tables, recomputes every face span, checks that spans are
disjoint, in range and together with the orphan runs cover the lump exactly, compares the
`samples` accessor's bytes and digest with the lump, re-decodes every world light and `dplt`
record, and verifies each `derived` face row against the root. The standalone validator checks
the accessors' lengths and digests, the ledger, and that every `faces[].spans[]` entry lies inside
the `samples` accessor.

## Import

R4.3 of `docs/project/seam_migration.md` -> "Roadmap — one pipeline" moves the real-time light
rig's **calibration**, not its geometry, off cvars/C++ literals and a per-map JSON survey file and
onto two editor surfaces: `UElysiumLightingSettings` (global, Project Settings -> Elysium ->
Lighting) and `UElysiumLightCalibration` (per-map, a data asset). `worldLights[]` above is
unaffected -- this section is about what a light's *raw* row is turned into at runtime, not about
the row itself, and the runtime still reads `<map>.lights` (`UE_bsp_to_scene`'s restatement of
`worldLights[]`) to adopt and derive every source. R5.6 is the task that bakes the derived values
and deletes that reader; this one is scoped to where the human-tunable numbers live.

### `UElysiumLightingSettings` — the global half

`Config = Elysium`, `DefaultConfig`, Project Settings -> Elysium -> Lighting, tracked at
`Config/DefaultElysium.ini` — the same pattern `UElysiumSurfaceSettings` established
(`seam_map_material.md` -> "Import" -> "Knob contract"). Every field is `UElysiumLightRig`'s own
former hardcoded default (`ElysiumLightRig.h`, historically lines ~157-188) or one of three retired
console variables, carried over at the same faithful value -- this task moves *where* the numbers
live, never what they are (`docs/project/seam_migration.md`, "Wire first, tune later"):

| Settings field | Was |
|---|---|
| `PointSpotScale`, `MaxBrightness`, `ExtendedMaxBrightness`, `FalloffExponent`, `RadiusScale`, `FallbackRadiusCm`, `IndirectLightingScale`, `VolumetricScatteringScale`, `SunScaleLux`, `SunSourceAngleDegrees`, `SunSoftSourceAngleDegrees`, `MinSkyReachCm`, `bPointShadows`, `bSpotShadows`, `bSunShadows` | `UElysiumLightRig`'s own `UPROPERTY` defaults |
| `bUseExtendedBrightnessCeiling` | `elysium.LightCurve` (0/1) |
| `bApplyLightFit` | `elysium.LightFit` (0/1) |
| — (retired outright) | `elysium.LightScale` — redundant once `PointSpotScale` is itself the editable value; no override concept survives it |
| — (deleted, R5.5) | `SpecularScale` — carried here at the legacy `0.0` by R4.3, then superseded: the light rig's specular scale is `UElysiumSurfaceSettings::LightSpecularScale` (one global knob, default 1.0; `seam_map_map.md` → "Import — reflection captures (R5.5)" → "`LightSpecularScale` rides along"), and a second field for the same number would be a second writer |

`SkyReachScale` stays a per-instance rig field, not a settings field: it is set from the map's own
`<map>.sky` scale at `Adopt`, not a human calibration.

`UElysiumLightRig::ApplySettings(const UElysiumLightingSettings&, const UElysiumSurfaceSettings&)`
copies every field above — plus `LightSpecularScale` from the surfaces page (R5.5) — into
the rig's own like-named mirrors (kept as separate fields, not a pointer to the settings singleton,
so a per-instance PIE edit in the component's own Details panel still works and `ApplyToSource`
keeps one cheap, uniform read path). `Adopt` calls it first, from the two `GetDefault<>` objects,
so a fresh map load always starts from the current Project Settings pages.

**Push timing diverges from `UElysiumSurfaceSettings` on purpose.** The surfaces page follows an
interactive slider drag live because it only has to touch one parameter collection; a light rig is
a per-world scene component with real per-light state (shadows, MegaLights, source shape), and
re-deriving 400+ lights on every tick of a drag is not what a slider needs to pay for.
`PostEditChangeProperty` therefore drops the interactive branch entirely — nothing pushes until
`EPropertyChangeType::Interactive` is *not* set in the change event, i.e. the drag's terminal
`ValueSet` — and only then does `PushToWorlds` walk every world context, find its
`UElysiumMapSubsystem`'s current map, call `ApplySettings` + `ApplyLiveTuning` on its light rig if
it has one, and move on.

### `UElysiumLightCalibration` — the per-map half

```text
/ElysiumBaked/<map>/DA_<map>_LightCalibration
```

One optional asset per map, beside `DA_<map>_Entities`/`DA_<map>_Collision`
(`FElysiumContentPaths::BakedMapLightCalibration`) — but unlike those two, **no producer writes
it**. It replaces the Lights Cog window's JSON survey (`UElysiumLightRig::LoadSurvey`,
`$ELYSIUM_EXPORT_ROOT/_lights/<map>.json`, deleted this task), which likewise had no generator and
existed only once a human ran a hand pass and pressed Save. `FElysiumLightCalibrationRow` restates
that survey's per-light shape as reflected `UPROPERTY` fields instead of JSON keys, authored
directly in the Content Browser's property panel:

| Field | Semantics |
|---|---|
| `SourceIndex` | the `.lights` line this row overrides (`worldLights[]`'s `sourceOffset` order) — stable across a re-export, unlike the rig's live array position, which drops the skyambient row and any row with no matching baked actor |
| `bDisabled` | switches the source off; independent of every override below |
| `bOverrideIntensity` / `Intensity` | |
| `bOverrideReach` / `ReachCm` | local/spot attenuation radius; no effect on the sun |
| `bOverrideColor` / `Color` | |

**A row is a merge, not a replacement.** Each override is its own on/off switch plus a value, not
one struct-wide flag — a hand pass can move one light's reach without restating its colour,
intensity and every other attribute the calibrated baseline already got right. A source with no
row keeps exactly the value `ApplyToSource` derives for it.

`UElysiumLightRig::ApplyCalibrationAsset` joins `Rows` onto the rig's live sources by `SourceIndex`
(the same join `LoadSurvey` used to do against its own JSON `edits[]`) and applies each override
through the ordinary per-source setters (`SetSourceDisabled`, `SetSourceIntensity`,
`SetSourceReach`, `SetSourceColor` — the last two new this task, `SetSourceReach` matching
`SetSourceIntensity`'s existing shape and `SetSourceColor` deliberately leaving `FLightSource::Color`
— the calibrated baseline `RevertSource` restores to — untouched). Every setter marks its source
overridden, which is what takes it out of both `ApplyLiveTuning` (so a later settings push does not
write over it) and the per-frame lightstyle tick, exactly as a hand edit through the old Cog editor
did. A row whose `SourceIndex` matches no live source (a stale row after a re-export) is silently
skipped, counted but not asserted on.

`Adopt` resolves the asset itself, quietly (`LoadObject<UElysiumLightCalibration>` with
`LOAD_NoWarn | LOAD_Quiet` at `BakedMapLightCalibration(MapName)`, `MapName` being the `.lights`
file's own base name) after the base derivation pass (`ApplyLiveTuning`) has run, so calibration
rows apply on top of the calibrated baseline rather than instead of it. **The asset's presence is
the cutover flag**, exactly as R4.1/R4.2: a map with one gets its rows applied, a map without one —
every map today — runs exactly as R4.2 left it. No remapper exists or is planned: no `_lights/*.json`
survey was ever saved to disk on this corpus (confirmed empty at the time this task landed), so
there is nothing to migrate, and the owner re-tunes each map fresh in the editor.

**R4.6's explicit `UElysiumMapTransportSettings` list does not gate this path**
(`seam_map_map.md` -> "## Import" -> "The explicit per-map cutover flag (R4.6)"), unlike the three
whole-swap transports (entities, collision, environment). Those three replace a legacy read
outright, so an unlisted map needs a flag telling its resolver to keep ignoring an asset that may
already exist; calibration never replaces anything — it merges on top of the derivation this file
states above, which keeps running unconditionally — so there is no legacy behavior for an unlisted
map to fall back to, and the asset's own presence already is the only fact that matters. R5.6's
bake, which retires the `.lights` derivation outright, is what eventually gives this path a real
legacy-vs-new split to gate.

### Sky baked (R5.2)

R5.2 finishes the SkyLight actor `pipeline/unreal/bake_map.py::_place_sky` has always placed
half-empty: it authors the actor (`SLS_SpecifiedCubemap`, `LowerHemisphereIsBlack`) with a null
`Cubemap` and the raw `emit_skyambient` magnitude as `Intensity`, and says so in its own comment —
"handed its real cubemap at load" — because the cube is assembled from six loose face PNGs, which
until this task only `ElysiumEnvironment::BuildSkyCubeFrom` could do, at runtime, every load. R5.2
does not replace that join; it asks the SAME function to build a PERSISTENT asset instead, once, at
bake, and gives the placeholder actor its real values.

**Cube identity: one per sky NAME, not per map.** The game shares six skies between 108 maps
(`ApplyEnvironment`'s comment on the runtime path already says so); the bake follows the same rule
rather than duplicating six images per converted map:

```text
/ElysiumBaked/Sky/Textures/TC_Sky_<SkyName>          UTextureCube, one per distinct .env skyname
/ElysiumBaked/Sky/Meshes/SM_SkyDome                  UStaticMesh, ONE — the backdrop box is the
                                                      same geometry (BuildSkyBox's own half-extent,
                                                      500000 cm) whatever sky samples it
/ElysiumBaked/Sky/Materials/MI_Sky_<SkyName>          UMaterialInstanceConstant off M_Sky,
                                                      SkyCube = the sky's cube, Brightness = 1
```

**The faithful face set only, never `tex_hi`.** `elysium.EnhancedTextures` is a per-user runtime
toggle with no baked-asset equivalent; a bake is asked once, so it has to pick one set, and the
faithful decode is VtMB's own data while the enhanced set is an opt-in visual substitution (B5) —
"wire first, tune later" reads that as content selection, not taste, and picks the faithful one.
Converted maps therefore stop honouring `elysium.EnhancedTextures` for their sky specifically (every
other reader of that cvar is unaffected); R6.4's "sky cube faces" deferred-reader entry is the face
PNGs' own eventual promotion to first-class imported textures (provenance, corpus dedup), a
different question from which set this bake samples today.

**The join, computed once instead of every load.** `UElysiumSkyBakeLibrary::BakeSkyCubeAsset`
(`Source/ElysiumUE/Public/ElysiumSkyBakeLibrary.h`) is a thin `UFUNCTION` face onto
`ElysiumEnvironment::BuildSkyCubeFrom` — literally the runtime's own function, now given a real
`Outer`/`Name` instead of `GetTransientPackage()`/`NAME_None` — so the pixel decode, the K1 x K2
face/rotation table and the solid-angle-weighted upper-hemisphere mean (`CubeUpperMean`) are one
computation asked from two call sites, not two computations that merely claim to agree. `_place_sky`
calls it once per distinct sky name the three-map corpus uses, then applies the exact policy
`UElysiumMapVisuals::SkyAmbientIntensity` states in C++: no pair or a pair reading zero -> intensity
0; a black cube (`CubeUpperMean` at or below `KINDA_SMALL_NUMBER`) with a nonzero pair -> intensity
0, logged, rather than a divide that would ship an infinity; otherwise `Mag / CubeUpperMean`. `Mag`
is the same `(color, mag)` `_load_lights` already parses off `<map>.lights`' first type-5 row for
the placeholder path — no new sidecar reader, only a later use of the same tuple.

**The dome.** `SM_SkyDome` is `ElysiumMapVisuals::BuildSkyBox`'s own vertex/triangle table
(`bake_lib.build_dynamic_mesh` + `create_static_mesh`, no collision, no Nanite — the box is meant to
enclose the whole scene and is excluded from ray tracing exactly as the runtime backdrop was), placed
as a `StaticMeshActor` at the origin, tagged `elysium.skydome` (`ElysiumBakedTags::SkyDome`) rather
than `elysium.sky` — the 3D-skybox miniature's own tag — so `ApplySceneFog`'s sky-fog stamping, which
walks `SkyActors`, never touches it. `AdoptBakedLevel` captures it into `BakedSkyDomeActor`, and
`elysium.togglesky` (`ToggleSkybox`) hides/shows it alongside the miniature, the same as the
runtime-built `SkyDomeMesh` it stands in for.

**Cutover: `MapsOnV2Models`, not a new list.** R5.1's own entry already scoped the dome here ("the
sky *dome* is R5.2's — this lane authors only the miniature's own geometry"); the cube and the
SkyLight's real values ride the same flag because neither means anything without the geometry that
displays them, and because a converted map's bake already runs the `_place_sky` stage this section
changes on every pass. `ElysiumMapVisuals::ApplyEnvironment` gained a `MapName` parameter for
exactly this test: on a `MapsOnV2Models` map it runs `ApplySceneFog` (unaffected — R5.3's) and
returns, never touching `SkyLight`, `SkyDomeMesh` or `SkyMid`; every other map runs precisely the
runtime path this file described before R5.2, unchanged. Deleting that runtime path outright is
R8's, matching every other legacy-path retirement in this roadmap.

Each map's own bake re-authors its sky's `TC_Sky_<name>`/`SM_SkyDome`/`MI_Sky_<name>` package rather
than skipping a found asset: the six source PNGs never change between bakes, so a second map that
shares a sky reproduces byte-identical content into the same package — idempotent by construction,
not by an existence check.

**Measured (2026-09-01, the three working maps).** Two distinct skies across the three:
`sp_tutorial_1` is `la` (cube upper-hemisphere mean 0.00335), `sm_pawnshop_1` and `sm_hub_1` both
`pier` (0.01120, identical to five significant figures on both bakes — the same bytes, reproduced,
not cached). Numbers, boot and shot-diff results are in `docs/project/seam_migration.md` under this
task's Settled entry.

### Cog Lights window: viewer, not editor

The window (`ElysiumCogWindow_Lights`) is now **read-only**. Deleted outright: the "Rig tuning" tab
(every global calibration slider — `UElysiumLightingSettings` is the surface now), the "Sky & fog"
tab (sky light intensity/colour/cubemap, height fog, the skylight-leaking A/B — these have no live
tuning surface until R4.4's per-map environment asset lands, which is a known, accepted gap: their
*derived* values still apply at load, only the interactive override is gone), the per-light editor
and its 3D gizmo, batch enable/disable, and the JSON survey's Save/Reload. Kept: the visibility
toggle, the per-source list/table, world-marker click-to-select, Isolate (a display-only hide pass,
touches no light's value) and a read-only "Selected light" readout (identity, override/disabled
state, resolved values) — the viewing surface the roadmap line names explicitly. `RenderSelectedSource`
retained its `SameBatch` authored-batch identification as read-only text (count and off-count only,
no batch on/off buttons), since it is exactly the information `docs/vtmb/light-attribution.md`'s
hand survey used to identify a copy-pasted decision.

### Test re-homing

`Elysium.Substrate.LightRig`'s JSON-survey assertions (`LoadSurvey`) were deleted with the code
they exercised and replaced with coverage of `ApplySettings` and `ApplyCalibrationAsset` (a
synthetic `UElysiumLightingSettings`/`UElysiumLightCalibration`, `NewObject`-constructed in the
test, exercising the merge-row join and per-source setters directly — no baked asset, no scratch
content root needed, since nothing under test reads through `FElysiumContentPaths`). The
derivation-math assertions the same test already carried (non-inverse-square falloff, MegaLights,
shadows-from-calibration, spot cone from `stopdot`/`stopdot2`) are **not yet moved**: the roadmap
line's "re-homed to bake verification" describes where they belong once R5.6 bakes final light
values and gives them something to be verified against — there is no baked light asset to compare
before that task lands, so re-homing them now would mean deleting coverage of `ApplyToSource`'s
formulas with nothing to replace it. They stay in `Elysium.Substrate.LightRig` until R5.6 gives them
a destination.
