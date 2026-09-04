"""Stage the material corpus out of the published GLB units.

`uv run elysium import materials` turns every published material unit into one Unreal material
instance below `/ElysiumBaked/Materials`, parented to one of nine generated masters under
`/Game/ElysiumGenerated/Materials/V2` (`docs/architecture/seam_map_material.md` -> "Import"). This
module is the offline half of that lane and mirrors `textures.py`'s shape exactly: stage every
unit, write one provenance sidecar per unit plus one `manifest.json`, recipe-stamp, prune, and let
the editor phase (SF-4.5, not this module) do the headless import.

**No silent drop.** Every one of the corpus's 229 parameter keys (plus `include`, the patched
corpus's 230th) has a named destination in the design's parameter table; a key this module cannot
place is a **stage failure** for that unit, isolated the way a bad texture unit is: named, counted,
the run goes on, and the command exits non-zero at the end. A failed unit's asset path stays out of
the editor prune (`keep`), exactly as the texture lane protects a failed unit's asset. The same
rule covers a parameter name that resolves but is not on the selected master's exposed list
(`EXPOSED_PARAMS` below): the unit fails rather than writing a name the master does not have.

**One unit, one instance -- except a decal (R7.2 ruling 2).** A unit that draws as a PROJECTOR --
`$decal 1` (`isDecalSurface`) or the whole `decalmodulate` family -- stages a SECOND instance,
`MI_<unit>_Decal`, in the same package, parented to `M_V2_Decal` (`projector_entry`). It is an
ordinary manifest entry sharing the unit's one provenance sidecar, so the editor phase authors it
with no code of its own; the unit's entry and sidecar both name it under `decalAsset`, and the map
lane, the bake and the runtime resolve it from a `vtmb:material:` id and nothing else.

**Scope.** This module classifies keys and shapes values; it does not walk a proxy chain that
needs a `Time`/`Panner`/`If` node into material-graph nodes (SF-4.3/4.5's job) -- `texturetransform`,
`linearramp` and the shader-time arithmetic proxies are recorded in provenance with their resolved
destination name, never computed here, because their value changes every frame. It also does not
invent a naming formula the design never states: the flipbook array texture (`BaseTextureFrames`/
`NormalMapFrames`) has no stated asset-path formula (unlike a plain texture's `T_`/`TC_` rule), so
this lane leaves those two slots at the master's default and writes only the scalars
(`FrameRate`/`NormalFrameRate`, the `UseAnimated…Frames` switches) the design does state a source
for. Every ambiguity resolved this way is called out here, not silently decided.
"""

from __future__ import annotations

from dataclasses import dataclass, field
import configparser
import hashlib
import json
import math
import os
import re
from pathlib import Path, PurePosixPath

from elysium_pipeline import paths
from elysium_pipeline.asset_names import safe_name
from elysium_pipeline.formats.unit_contract.container import GlbContainerError, decode_glb
from elysium_pipeline.importers.textures import (
    PROVENANCE_SUFFIX as TEXTURE_PROVENANCE_SUFFIX,
    in_selection,
    normalize_select,
)

MATERIAL_EXTENSION = "ELYSIUM_vtmb_material"

#: The family directory this lane reads below the export_v2 root.
FAMILY = "materials"
#: The package every instance lands under.
PACKAGE_ROOT = "/ElysiumBaked/Materials"
#: The tracked package the nine hand-built masters (SF-4.3) live under.
MASTER_ROOT = "/Game/ElysiumGenerated/Materials/V2"
#: R7.2 ruling 2 (`seam_migration.md` -> "R7.2 Decals"): a `$decal` / `decalmodulate` unit stages a
#: SECOND shared instance beside its surface one -- same package, the surface instance's name plus
#: this suffix -- parented to `M_V2_Decal`, the one `MD_DeferredDecal` master a `UDecalComponent`
#: (and a mesh-decal section) actually draws. Everything that lays or bakes a decal binds this
#: twin by the unit's own `vtmb:material:` id; nothing else in the project names it.
DECAL_INSTANCE_SUFFIX = "_Decal"
#: The master that twin parents to.
DECAL_MASTER = "M_V2_Decal"
#: R7.3 (`docs/architecture/effects-architecture.md` section 5.4): the four particle material
#: children `make_v2_materials.make_particle_children` authors below this lane's package root,
#: beside the corpus instances and map-independent like them. They are not units of this lane, so
#: the manifest names them under `keep` on every run and the editor phase's prune leaves them be.
EFFECT_MATERIAL_CHILDREN = (
    f"{PACKAGE_ROOT}/particles/MI_Particle",
    f"{PACKAGE_ROOT}/particles/MI_ParticleLit",
    f"{PACKAGE_ROOT}/particles/MI_ParticleNoZ",
    f"{PACKAGE_ROOT}/particles/MI_ParticleRefract",
)
#: Bumped whenever the mapping below changes in a way that must re-stage every unit. v2: the
#: revised design (`seam_migration.md` -> "Revised after review", 2026-08-31).
SETTINGS_VERSION = "elysium-material-import-v2"
MANIFEST_SCHEMA = "1.0.0"
MANIFEST_NAME = "manifest.json"
IMPORT_REPORT_NAME = "import_report.json"
ROOT_FILES = frozenset({MANIFEST_NAME, IMPORT_REPORT_NAME})
PROVENANCE_SUFFIX = ".provenance.json"
#: `[/Script/ElysiumUE.ElysiumSurfaceSettings]` -> `ChromaThreshold` (`Config/DefaultElysium.ini`).
#: Read by :func:`_read_chroma_threshold`; never a literal anywhere the split is applied.
_CHROMA_THRESHOLD_DEFAULT = 0.02
_CHROMA_INI_SECTION = "/Script/ElysiumUE.ElysiumSurfaceSettings"
_CHROMA_INI_KEY = "ChromaThreshold"


class MaterialImportError(RuntimeError):
    """The material corpus could not be staged."""


# --- surface class table (SF-4.1 seeds its data asset from this same list) -----------------------
#
# The table itself lives in `elysium_pipeline.importers.surface_classes` (no imports, so
# `pipeline/unreal/make_surface_knobs.py` -- running inside Unreal's embedded editor Python, which
# carries no `numpy` -- can import it directly instead of a hand-mirrored copy). Re-exported here
# so every existing call site in this module (and its importers) keeps working unchanged.
from elysium_pipeline.importers.surface_classes import (  # noqa: E402
    FAMILY_DEFAULT_CLASS,
    SURFACE_CLASS_INDEX,
    SURFACE_CLASSES,
    TOP_DIRECTORY_CLASSES,
    _SURFACEPROP_NAMES,
    _TIER1_ADDITIONS,
    _TOP_DIRECTORY_ONLY,
)


def _read_chroma_threshold(root: Path | None = None) -> float:
    """`ChromaThreshold` out of `Config/DefaultElysium.ini`; `_CHROMA_THRESHOLD_DEFAULT` when the
    ini, the section or the key is absent -- never a literal at the call site."""

    ini_path = Path(root) if root is not None else paths.repo_root()
    ini_path = ini_path / "Config" / "DefaultElysium.ini"
    try:
        parser = configparser.ConfigParser(interpolation=None)
        parser.read(ini_path, encoding="utf-8")
        if parser.has_option(_CHROMA_INI_SECTION, _CHROMA_INI_KEY):
            return float(parser.get(_CHROMA_INI_SECTION, _CHROMA_INI_KEY))
    except (OSError, ValueError, configparser.Error):
        pass
    return _CHROMA_THRESHOLD_DEFAULT


# --- master inventory ------------------------------------------------------------------------------

#: Shader family (lowercased, `shaderResolution.family` or `shader`) -> master, per the "Master
#: inventory" and "The eight real unresolved families" tables. `shatteredglass` always takes the
#: translucent master (it is inherently broken/translucent glass, never opaque); every other Lit
#: family branches on the resolved blend mode at instance time.
_LIT_FAMILIES = frozenset({"lightmappedgeneric", "vertexlitgeneric", "teeth", "cable"})
_UNLIT_FAMILIES = frozenset({"unlitgeneric", "cloud"})
_TWOTEXTURE_FAMILIES = frozenset({"unlittwotexture", "worldvertextransition", "worldtwotextureblend"})
_REFRACT_FAMILIES = frozenset({"refract", "heatglow"})
#: The 36 debug/tool families ("No master, provenance only"). They still get an `MI_` instance,
#: parented to `M_V2_Unlit` with only `$basetexture` reproduced -- exactly as the design states --
#: and their unit key is *also* listed under the manifest's `provenanceOnly` array so a caller can
#: count and audit them without walking every asset's provenance sidecar.
NO_MASTER_FAMILIES = frozenset({
    "wireframe", "desaturatespotlight", "screenfeedback", "debugluxels", "shadow",
    "vertexlitgeneric_dx6", "modulate", "depthonly", "volumetricfog",
    "basetimeslightmaptimesdetail", "worlddiffusebumpmap", "basetimeslightmapwet",
    "basetimesmod2xenvmap", "bumpmappedenvmap", "burnpeel", "camo", "debugfbtexture",
    "debuglightingonly", "debugmodifyvertex", "epicoverlay", "eyeball", "gooinglass",
    "internalframesync", "lightmappedenvmappedbumpmappedtexture",
    "lightmappedmaskedenvmappedbumpmappedtranslucenttexture", "particlesphere", "redvision",
    "refractparticle", "shadowbuild", "shadowmodel", "skyfog", "translucentlightmap",
    "unlitgeneric_dx6", "vertexdiffuse", "vertexnormals", "watersurfacebottom",
})
#: The 5 `unlitgeneric` `$ignorez` world units re-routed to `M_V2_Sprite` (already Unlit,
#: two-sided, depth-test-off) rather than a tenth master -- "Master inventory", `$ignorez` bullet.
IGNOREZ_SPRITE_REROUTE_UNITS = frozenset({
    "engine/lightsprite", "engine/vertexcoloradditive", "engine/vertexcolorblend",
    "sun/overlay", "debug/debugportals",
})
#: The 1 `vertexlitgeneric` unit that keeps `M_V2_Lit` despite authoring `$ignorez`, recording the
#: flag as a named divergence rather than re-routing (no `M_V2_UnlitNoDepth`).
IGNOREZ_NAMED_DIVERGENCE_UNITS = frozenset({"models/scenery/furniture/displaytable/floating"})


#: SF-4.3-part-3 cross-cutting ruling (d): the 13 stage failures the design's "No silent drop"
#: rule surfaced, ruled per unit rather than fixed generically -- each one authors a key that
#: genuinely has no destination on *that* unit's own master, not a bug in the parameter table.
#: `unit key -> {vmt key (lower-cased) -> reason}`; every key named here is dropped from
#: classification (never bound to a parameter) and recorded in provenance as
#: `unitDivergenceProvenanceOnly` instead of failing the stage. See
#: `docs/architecture/seam_map_material.md` -> "Import" -> "Per-unit divergences" for the
#: narrative form of this table. `$vertexalpha` on the rerouted sprite unit
#: (`engine/vertexcolorblend`) is deliberately **not** here any more: `M_V2_Sprite` now exposes
#: `UseVertexAlpha`, so that key has a real destination and stages cleanly.
UNIT_DIVERGENCES: dict[str, dict[str, str]] = {
    "models/character/npc/common/raver/males/male_raver_3/eyeball": {
        "$iris": (
            "this one vertexlitgeneric unit authors $iris, an eyes-family key; M_V2_Lit has no "
            "Iris slot (Iris is M_V2_Eyes-only) -- provenance-only, named divergence"
        ),
    },
    "models/character/npc/unique/chinatown/ming-xiao/eyeball_r": {
        "$selfillum": (
            "eyes.psh has no self-illum term (every stage is a texture read; the eyes register "
            "legend carries no c-register self-illum path) and M_V2_Eyes exposes neither "
            "SelfIllumAmount nor UseSelfIllum -- provenance-only"
        ),
    },
    "models/character/npc/unique/santa_monica/ghost/eyeball_l": {
        "$selfillum": (
            "eyes.psh has no self-illum term and M_V2_Eyes exposes neither SelfIllumAmount nor "
            "UseSelfIllum -- provenance-only"
        ),
    },
    "models/character/npc/unique/santa_monica/ghost/eyeball_r": {
        "$selfillum": (
            "eyes.psh has no self-illum term and M_V2_Eyes exposes neither SelfIllumAmount nor "
            "UseSelfIllum -- provenance-only"
        ),
    },
    "stone/dincountertp": {
        "$envmapmask": (
            "worldvertextransition's InitShaderParams deletes $envmap without $bumpmap (and "
            "outright under $envmapsphere); M_V2_TwoTexture has no reflection lane at all, so a "
            "mask authored for a reflection this unit never gets is provenance-only"
        ),
        "$envmap": (
            "same InitShaderParams rule as $envmapmask on this unit ('InitShaderParams deletes "
            "$envmap without $bumpmap ... so UseEnvMap is forced off on this master', design doc "
            "'The eight real unresolved families' -> worldvertextransition): this unit has no "
            "$bumpmap, M_V2_TwoTexture has no EnvMap/UseEnvMap slot at all -- provenance-only"
        ),
    },
    "water/cheap_water": {
        "$forcecheap": (
            "non-water unit (family lightmappedgeneric, takes M_V2_LitTranslucent) authors "
            "$forcecheap (a water-only key -> CheapWater) despite not being a water surface; "
            "M_V2_LitTranslucent has no CheapWater switch -- provenance-only"
        ),
        "$fogenable": (
            "non-water unit (family lightmappedgeneric, takes M_V2_LitTranslucent) authors water "
            "fog parameters despite not being a water surface; M_V2_LitTranslucent has no fog "
            "lane at all -- provenance-only"
        ),
        "$fogcolor": "same as $fogenable on this unit -- M_V2_LitTranslucent has no FogColor slot",
        "$fogstart": "same as $fogenable on this unit -- M_V2_LitTranslucent has no FogStart slot",
        "$fogend": "same as $fogenable on this unit -- M_V2_LitTranslucent has no FogEnd slot",
    },
    "water/invisible_water": {
        "$fogenable": (
            "non-water unit (family unlitgeneric, takes M_V2_Unlit) authors water fog parameters "
            "despite not being a water surface; M_V2_Unlit has no fog lane at all -- "
            "provenance-only"
        ),
        "$fogcolor": "same as $fogenable on this unit -- M_V2_Unlit has no FogColor slot",
        "$fogstart": "same as $fogenable on this unit -- M_V2_Unlit has no FogStart slot",
        "$fogend": "same as $fogenable on this unit -- M_V2_Unlit has no FogEnd slot",
    },
}


def resolve_master(family: str, blend_mode: str) -> str | None:
    """The master an install unit's resolved shader family takes, or `None` when unrecognised."""

    family = (family or "").lower()
    if family == "shatteredglass":
        return "M_V2_LitTranslucent"
    if family in _LIT_FAMILIES:
        return "M_V2_LitTranslucent" if blend_mode in ("Translucent", "Additive") else "M_V2_Lit"
    if family in _UNLIT_FAMILIES:
        return "M_V2_Unlit"
    if family == "eyes":
        return "M_V2_Eyes"
    if family == "water":
        return "M_V2_Water"
    if family == "sprite":
        return "M_V2_Sprite"
    if family in _REFRACT_FAMILIES:
        return "M_V2_Refract"
    if family == "decalmodulate":
        # R7.2 ruling 2: the family's surface instance stays on the projector master it already
        # resolved to -- it is never a world face (0 `usemtl` hits across 108 maps), and the twin
        # every caller actually binds is `MI_<unit>_Decal` (`projector_entry`). Its `Modulate`
        # blend is the retail record; owner call A draws the family translucent, which is what the
        # twin carries (a DBuffer platform rewrites Modulate to Translucent regardless).
        return "M_V2_Decal"
    if family in _TWOTEXTURE_FAMILIES:
        return "M_V2_TwoTexture"
    if family in NO_MASTER_FAMILIES:
        return "M_V2_Unlit"
    return None


# --- exposed-parameter contract (binding contract, SF-3.1/4.3) -------------------------------------

#: On every master ("Exposed parameters, by master" -> the four shared parameters).
_SHARED_PARAMS = {"SurfaceClassIndex": "S", "SurfaceClassLUT": "T", "Alpha": "S", "Color": "V"}
#: The sine lane, shared by every master that hosts a `sine` proxy (Lit/LitTranslucent, Unlit,
#: TwoTexture).
_SINE_LANE = {
    "SineMin": "S", "SineMax": "S", "SinePeriod": "S", "SineTimeOffset": "S",
    "SineTargetMask": "V", "SineChannelMask": "V",
}
#: R7.1 ruling J (`water-architecture.md` -> "Surf sine UV translate"): the one vector a
#: `sine` -> `texturetransform` -> `$basetexturetransform` chain resolves to, `(ampU, ampV, offU,
#: offV)`, added to the base UV coordinate as `amp x wave + off`. Only the Lit pair carries it --
#: `objects/surf`, the pier's wave cards, is the whole population.
_SINE_UV_LANE = {"SineUVTranslate": "V"}
_BASE_SCROLL_LANE = {"BaseScrollRateU": "S", "BaseScrollRateV": "S"}
_BUMP_SCROLL_LANE = {"BumpScrollRateU": "S", "BumpScrollRateV": "S"}
_BASE_ANIM_LANE = {
    "FrameRate": "S", "FrameCount": "S", "BaseTextureFrames": "T", "UseAnimatedFrames": "#",
}
_NORMAL_ANIM_LANE = {
    "NormalFrameRate": "S", "NormalFrameCount": "S", "NormalMapFrames": "T",
    "UseAnimatedNormalFrames": "#",
}
#: R5.4 (seam_map_material.md -> "Scene fog on the world masters (R5.4)"): Source's per-map
#: distance fog as the Custom-Primitive-Data-driven term every world / 3D-skybox / prop primitive
#: carries (`ElysiumFog.h`, `mat_fog.fog_from_primitive`). `FogColor`/`FogStart`/`FogInvRange` are
#: read off the primitive (slots 0..3, 4, 5), never off the instance, so the stage never writes
#: them -- the bake stamps the primitive and `UElysiumMapVisuals::ApplySceneFog` re-stamps it live.
#: `FogInscatter` is the one instance value: Source fogs an additive surface to BLACK (its fog
#: colour is forced to zero under additive blending, or the haze would brighten the scene), so the
#: stage writes `0.0` for an `Additive` blend and leaves every other instance on the default `1.0`.
_SCENE_FOG_LANE = {
    "FogColor": "V", "FogStart": "S", "FogInvRange": "S", "FogInscatter": "S",
}
#: The masters that carry the scene-fog lane: every master a map's world/brush/sky face or a prop
#: slot binds. `M_V2_Water` is R7.2's (its `FogColor`/`FogStart`/`FogEnd` are the VMT's own
#: water-fog keys, a different term under the same names), `M_V2_Sprite` R7.4's, `M_V2_Eyes`
#: R6.1's, and `M_V2_Decal` carries the instance-parameter variant (R5.3).
SCENE_FOG_MASTERS = frozenset({"M_V2_Lit", "M_V2_LitTranslucent", "M_V2_Unlit", "M_V2_TwoTexture",
                               "M_V2_Refract"})


def _merged(*dicts: dict[str, str]) -> dict[str, str]:
    out: dict[str, str] = {}
    for one in dicts:
        out.update(one)
    return out


#: `master name -> {parameter name: kind}`, `kind` one of `T`/`S`/`V`/`#`, exactly the design's
#: per-master tables under "Exposed parameters, by master". The stage writes exactly these names;
#: writing anything else is a stage failure (`_validate_exposed`), never a silent default.
EXPOSED_PARAMS: dict[str, dict[str, str]] = {
    "M_V2_Lit": _merged(
        _SHARED_PARAMS, _BASE_SCROLL_LANE, _BUMP_SCROLL_LANE, _BASE_ANIM_LANE, _NORMAL_ANIM_LANE,
        _SINE_LANE, _SINE_UV_LANE, _SCENE_FOG_LANE,
        {
            "BaseTexture": "T", "NormalMap": "T", "EnvMapMask": "T", "EnvMap": "T",
            "SelfIllumAmount": "S", "EnvMapMaskScale": "S", "BumpScale": "S",
            "SelfIllumTint": "V", "EnvMapTint": "V", "TexScaleOffset": "V",
            "UseBaseTexture": "#", "UseNormalMap": "#", "UseSelfIllum": "#", "UseVertexColor": "#",
            "UseVertexAlpha": "#", "UseEnvMap": "#", "UseEnvMapMask": "#",
            "UseBaseAlphaEnvMapMask": "#", "UseNormalMapAlphaEnvMapMask": "#", "UseFixedCube": "#",
            "MetallicTint": "#",
            # R5.3 (seam_map_material.md -> "Decal fog and wetness homes"): a real per-instance
            # scalar pair, not provenance-only -- `WetnessScale` is static per unit (baked once at
            # import, same as every other instance scalar) and `WetnessDriven` gates a global,
            # live `MPC_ElysiumEnvironment` read in the graph, so no per-map material instance is
            # needed for the live half either.
            "WetnessScale": "S", "WetnessDriven": "S",
            # R6.3 (seam_map_material.md -> "Detail sway on the model masters"): declared for the
            # three-way name pin; no VMT key maps to it and the stage never sets it true -- only
            # the map bake's `MI_DetailSway_*` child of an imported instance does.
            "UseDetailSway": "#",
        },
    ),
    "M_V2_Unlit": _merged(
        _SHARED_PARAMS, _BASE_SCROLL_LANE, _BASE_ANIM_LANE, _SINE_LANE, _SCENE_FOG_LANE,
        {
            "BaseTexture": "T", "EnvMapMask": "T", "EnvMap": "T", "CloudAlphaTexture": "T",
            "EnvMapMaskScale": "S",
            "EnvMapTint": "V", "TexScaleOffset": "V", "CloudScale": "V",
            "UseBaseTexture": "#", "UseVertexColor": "#", "UseVertexAlpha": "#", "UseEnvMap": "#",
            "UseEnvMapMask": "#", "UseBaseAlphaEnvMapMask": "#", "UseFixedCube": "#",
            "MetallicTint": "#", "UseCloudAlpha": "#",
            "UseDetailSway": "#",  # R6.3, as on M_V2_Lit
        },
    ),
    "M_V2_Eyes": _merged(_SHARED_PARAMS, {
        "BaseTexture": "T", "Iris": "T", "Glint": "T", "IrisFrame": "S",
        "VampireEyes": "#", "UseGlint": "#",
    }),
    "M_V2_Water": _merged(
        _SHARED_PARAMS, _BUMP_SCROLL_LANE, _NORMAL_ANIM_LANE,
        {
            "BaseTexture": "T", "DuDvMap": "T", "NormalMap": "T", "EnvMap": "T",
            "RefractAmount": "S", "ReflectAmount": "S", "BaseReflectFract": "S", "WaterDepth": "S",
            "WaterMurkiness": "S", "WaterBaseFactor": "S", "WaterBaseMovementDist": "S",
            "WaterBaseMovementFreq": "S", "WaterSpecularMin": "S", "WaterSpecularMax": "S",
            "WaterTimeFreq1": "S", "WaterTimeFreq2": "S", "WaterWaveHeight": "S",
            "WaterWaveLength": "S", "CheapWaterStartDistance": "S", "CheapWaterEndDistance": "S",
            "FogStart": "S", "FogEnd": "S",
            "WaterColor": "V", "RefractTint": "V", "ReflectTint": "V", "FogColor": "V",
            "EnvMapTint": "V", "TexScaleOffset": "V",
            "CheapWater": "#", "UseFogEnable": "#", "UseEnvMap": "#", "UseFixedCube": "#",
            "UseBaseTexture": "#", "UseNormalMap": "#",
            # R7.1: the `$bottommaterial` faces (`dev/dev_waterbeneath2`) draw on the same SLW
            # master with the reflection stripped and no volume extinction (`Underside`).
            "Underside": "#",
        },
    ),
    "M_V2_Sprite": _merged(_SHARED_PARAMS, _BASE_ANIM_LANE, {
        "BaseTexture": "T", "UseVertexColor": "#", "UseVertexAlpha": "#",
    }),
    "M_V2_Refract": _merged(_SHARED_PARAMS, _SCENE_FOG_LANE, {
        "BaseTexture": "T", "DuDvMap": "T", "NormalMap": "T", "EnvMap": "T",
        "RefractAmount": "S", "RefractTint": "V", "EnvMapTint": "V",
        "UseBaseTexture": "#", "UseNormalMap": "#", "UseEnvMap": "#", "UseFixedCube": "#",
    }),
    # R7.2 (seam_migration.md -> "R7.2 Decals", ruling 1): re-cut MD_DeferredDecal/Translucent/
    # DefaultLit projector master. `UseVertexColor` is dropped (a projected decal has no vertex
    # colour); `Emissive`/`EmissiveScale` and the `Unlit` switch are new. Two callers bind this
    # master: `resolve_master` (the 38 `decalmodulate` units' own surface instance) and
    # `projector_entry` (the `MI_<unit>_Decal` twin of every `$decal`/`decalmodulate` unit, ruling
    # 2) -- only the second ever writes `Unlit`, and only for an unlit-family unit.
    "M_V2_Decal": _merged(_SHARED_PARAMS, {
        "BaseTexture": "T", "Emissive": "T", "EmissiveScale": "S", "Unlit": "#",
        # R5.3 (seam_map_material.md -> "Decal fog and wetness homes"), unchanged by the R7.2
        # re-cut: the world's own distance fog, as three named instance parameters -- a
        # UDecalComponent carries no Custom Primitive Data of its own. No corpus unit authors
        # these (decalmodulate ships no fog keys), so the stage never writes them; the placement
        # lane sets them per decal instance from the map's own environment, never from a per-map
        # material package.
        "FogColor": "V", "FogStart": "S", "FogInvRange": "S",
    }),
    "M_V2_TwoTexture": _merged(
        _SHARED_PARAMS, _BASE_SCROLL_LANE, _SINE_LANE, _SCENE_FOG_LANE,
        {
            "BaseTexture": "T", "BaseTexture2": "T", "NormalMap": "T",
            "AlphaBias": "S",
            "TexScaleOffset": "V", "Texture2ScaleOffset": "V",
            "UseBaseTexture2": "#", "UseNormalMap": "#", "UseBumpOnBaseTexture2": "#",
            "UseVertexColor": "#", "UseVertexAlpha": "#",
        },
    ),
}
EXPOSED_PARAMS["M_V2_LitTranslucent"] = EXPOSED_PARAMS["M_V2_Lit"]


def _switch_names(master: str) -> list[str]:
    """Every static-switch name `master` exposes, sorted -- the full-state list review finding 2
    stages per entry so the import phase can set every one of them explicitly (entry value or
    `False`) instead of writing additively onto whatever a previous run left behind."""

    return sorted(name for name, kind in EXPOSED_PARAMS[master].items() if kind == "#")


def _validate_exposed(params: "_Params", master: str) -> None:
    exposed = EXPOSED_PARAMS[master]
    for bucket_name, bucket in (
        ("texture", params.textures), ("scalar", params.scalars),
        ("vector", params.vectors), ("switch", params.switches),
    ):
        for name in bucket:
            if name not in exposed:
                raise MaterialImportError(
                    f"{name!r} ({bucket_name}) is not exposed on {master}"
                )


# --- parameter table (SF-3.2) -----------------------------------------------------------------------

#: `$bumpmap` and `$envmap` are family-dependent (the reflection contract) and handled specially;
#: `$decal`, `$selfillum`, `$ignorez`, `$spriterendermode` and `$alphatestreference` are handled
#: specially too (provenance fields / blend-table inputs, not plain parameter map entries); every
#: other texture-class key that binds a named master slot.
TEXTURE_PARAM_MAP = {
    "$basetexture": "BaseTexture",
    "$envmapmask": "EnvMapMask",
    "$iris": "Iris",
    "$basetexture2": "BaseTexture2",
    "$texture2": "BaseTexture2",  # same slot as $basetexture2, a second family spelling
    "$normalmap": "NormalMap",
    "$dudvmap": "DuDvMap",
    "$cloudalphatexture": "CloudAlphaTexture",
}
#: Data-class texture slots -- the `_linear` twin rule applies to these, never to a colour slot.
DATA_CLASS_TEXTURE_PARAMS = frozenset({"NormalMap", "DuDvMap", "EnvMapMask", "CloudAlphaTexture"})
#: Texture-class keys with no destination among the nine masters' exposed slots (none of them is
#: on any master's texture column, `docs/architecture/seam_map_material.md` -> "Exposed parameters,
#: by master"): the texture reference is still resolved and recorded in provenance, never bound to
#: an instance parameter, so a build never receives a name the master does not have. `$detail`,
#: `$detail2` and `$glassenvmap` bind nothing per "`Detail` is dropped".
TEXTURE_PROVENANCE_KEYS = frozenset({
    "%tooltexture", "$spotlightmask", "$reflecttexture", "$refracttexture", "$bottommaterial",
    "$crackmaterial", "$masktexture", "$antitexture", "$burntexture", "$detail", "$detail2",
    "$dudvtexture", "$fuzztexture", "$glassenvmap", "$modelmaterial", "$leaknoise",
})
#: Of `TEXTURE_PROVENANCE_KEYS`, the ones whose resolved dependency is a *material* reference
#: rather than a texture ("Material references": `$bottommaterial`, `$crackmaterial`,
#: `$modelmaterial`, `$leaknoise` -- "the same shape").
MATERIAL_REFERENCE_KEYS = frozenset({"$bottommaterial", "$crackmaterial", "$modelmaterial", "$leaknoise"})

SCALAR_PARAM_MAP = {
    "$alpha": "Alpha",
    "$envmapmaskscale": "EnvMapMaskScale",
    "$bumpscale": "BumpScale",
    "$refractamount": "RefractAmount",
    "$reflectamount": "ReflectAmount",
    "$fogstart": "FogStart",
    "$fogend": "FogEnd",
    "$waterbasefactor": "WaterBaseFactor",
    "$waterbasemovementdist": "WaterBaseMovementDist",
    "$waterbasemovementfreq": "WaterBaseMovementFreq",
    "$waterdepth": "WaterDepth",
    "$watermurkiness": "WaterMurkiness",
    "$waterspecularmin": "WaterSpecularMin",
    "$waterspecularmax": "WaterSpecularMax",
    "$watertimefreq1": "WaterTimeFreq1",
    "$watertimefreq2": "WaterTimeFreq2",
    "$waterwaveheight": "WaterWaveHeight",
    "$waterwavelength": "WaterWaveLength",
    "$cheapwaterstartdistance": "CheapWaterStartDistance",
    "$cheapwaterenddistance": "CheapWaterEndDistance",
    "$alpha_bias": "AlphaBias",
}
#: Scalar-shaped keys with no destination among the nine masters' exposed scalar names. `$minlight`
#: and `$maxlight` left the masters (no Lumen formula, a named divergence) and are promoted to
#: their own structured provenance fields (`_MISC_PROVENANCE_SCALAR_KEYS`), not written here.
SCALAR_PROVENANCE_KEYS = frozenset({
    # `$frame`/`$bumpframe` select which frame of a multi-frame texture Source samples when
    # nothing animates it (default 0); the static frame-0 fallback (`_apply_static_frame_fallback`)
    # reads the raw value back out of `provenance_rows` itself, so beyond not being an unmapped key
    # this is a plain provenance-only leaf like the rest of the table.
    "$frame", "$bumpframe", "$detailscale", "$detailscale2", "$contrast", "$wave",
    "$wetbrightnessfactor", "$maxbrightlevel", "$leakamount", "$leakforce",
    "$fuzzoffset", "$fuzzedgeopacity", "$fuzzfaceopacity", "$j_basescale", "$halfwidth", "$mean",
})
#: Scalar-shaped keys promoted to a named structured provenance field, read by another lane
#: ("Four parameters that left the masters", "Provenance" -> the placement/map/runtime rows).
_MISC_PROVENANCE_SCALAR_KEYS = {
    "$minlight": "minLight", "$maxlight": "maxLight", "$subdivsize": "subdivSize",
    "$curve": "curve",
}

#: `$scale`/`$texscale`/`$tex2scale`/`$texture2scale` (bare-or-pair, splat-on-bare per the
#: Value-type rules) feed the `.xy` half of the named `TexScaleOffset`/`Texture2ScaleOffset` vector.
SCALE_XY_KEYS = {
    "$scale": "TexScaleOffset", "$texscale": "TexScaleOffset",
    "$tex2scale": "Texture2ScaleOffset", "$texture2scale": "Texture2ScaleOffset",
}
#: `$bumpoffset`/`$texoffset`/`$tex2offset` (always a `[u v]` pair) feed the `.zw` half.
OFFSET_ZW_KEYS = {
    "$bumpoffset": "TexScaleOffset", "$texoffset": "TexScaleOffset",
    "$tex2offset": "Texture2ScaleOffset",
}

#: Vector keys with an unambiguous 1:1 master vector slot. `$spriteorigin` has moved out of this
#: group (provenance, the sprite placement lane's), `$cloudscale` is a vector (`[2 2 2]`) that pads
#: to `(x, y, z, 0)`, handled via `_VECTOR_SHAPE` below.
VECTOR_PARAM_MAP = {
    "$envmaptint": "EnvMapTint",
    "$color": "Color",
    "$refracttint": "RefractTint",
    "$fogcolor": "FogColor",
    "$reflecttint": "ReflectTint",
    "$selfillumtint": "SelfIllumTint",
    "$watercolor": "WaterColor",
    "$cloudscale": "CloudScale",
}
#: Vector keys with no master destination (the merge algorithm for `$scale`/`$texoffset`/
#: `$bumpoffset` etc. is now stated -- see `SCALE_XY_KEYS`/`OFFSET_ZW_KEYS` -- so only the keys the
#: design still states no destination for remain here).
VECTOR_PROVENANCE_KEYS = frozenset({
    "$maskscale", "$clampcolor", "$maxcolor", "$leakcolor", "$glassenvmaptint",
})
#: `$color[i]`/`$envmaptint[i]` component-target spelling a `sine` proxy's `resultvar` may use.
_VECTOR_COMPONENT = re.compile(r"^(\$[a-z0-9_]+)\[(\d)\]$")

#: `$decal` is handled specially (a provenance flag, no longer a switch -- see `stage_unit`).
SWITCH_PARAM_MAP = {
    "$vertexcolor": "UseVertexColor",
    "$vertexalpha": "UseVertexAlpha",
    "$normalmapalphaenvmapmask": "UseNormalMapAlphaEnvMapMask",
    "$basealphaenvmapmask": "UseBaseAlphaEnvMapMask",
    "$vampire": "VampireEyes",
    "$forcecheap": "CheapWater",
    "$fogenable": "UseFogEnable",
    "$bumpbasetexture2withbumpmap": "UseBumpOnBaseTexture2",
    # `$normalalphaenvmapmask` is a one-off misspelling of `$normalmapalphaenvmapmask`
    # (`seam_map_material.md` -> "Static switch"); treated as the correct key, anomaly recorded.
    "$normalalphaenvmapmask": "UseNormalMapAlphaEnvMapMask",
}
#: Recorded per the reflection contract but never a switch and never a master ("no switch, no
#: master, no variant").
SWITCH_PROVENANCE_KEYS = frozenset({"$envmapsphere", "$envmapcameraspace"})

#: The blend/two-sided/opacity-clip keys, resolved together into `basePropertyOverrides`.
INSTANCE_PROPERTY_KEYS = frozenset({
    "$translucent", "$additive", "$alphatest", "$alphatested", "$nocull", "$alphatestreference",
})
#: Authored but with no effect in this lane (recorded, per the design's own notes on each).
#: `$ignorez` and `$spriterendermode` are handled specially (see `stage_unit`), not here.
INSTANCE_PROPERTY_PROVENANCE_KEYS = frozenset({"$transparent", "$translucency", "$model", "$flat"})

#: Bound by `FElysiumMaterialFactory` at runtime (SF-6.4), never by this offline lane.
RUNTIME_KEYS = frozenset({"$reflecttexture", "$refracttexture", "$clientshader"})

#: `$basetexturetransform` is the one transform key the design states an explicit parse for; its
#: riders are recorded but not resolved into a vector.
TRANSFORM_KEYS = frozenset({"$basetexturetransform"})
TRANSFORM_PROVENANCE_KEYS = frozenset({"$translate", "$noscale", "$mod2x", "$keepcolor"})

SURFACEPROP_KEY = "$surfaceprop"
PATCH_ONLY_KEY = "include"

#: Every other documented key: map-compiler hints, editor/tool keys, renderer keys VtMB never
#: implemented, misplaced proxy keys, the sprite-orientation key, the unprefixed-duplicate
#: mistakes and the one malformed key -- all "Provenance only" in the design.
PROVENANCE_ONLY_KEYS = frozenset({
    "%compilepassbullets", "%compilewater", "%compilenodraw", "%compiletrigger",
    "%compilenonsolid", "%compileclip", "%compiledetail", "%compilefog", "%compilehint",
    "%compileladder", "%compilelightpass", "%compilenovis", "%compilenpcclip",
    "%compilenpcopaque", "%compileorigin", "%compileplayercontrolclip", "%compileshadowonly",
    "%compileskip", "%compilesky", "%compilewanderclip", "%compilewet", "$compilepassbullets",
    "%keywords", "%detailtype", "%notooltexture",
    "$envmapcontrast", "$envmapmode", "$desaturate", "$modintensity", "$blur", "$soft",
    # $nooverbright (3 units): provenance only -- this lane has no per-instance opt-out of the
    # `_x2 c0` overbright doubling, so `UElysiumSurfaceSettings::Overbright` (the single global
    # knob every lit master reads) still applies to these three instances. A named deliberate
    # divergence from retail, not a silently dropped flag.
    "$multipass", "$nooverbright", "$no_fullbright", "$nofog", "$polyoffset", "$trilinear",
    "$nomip", "nomip", "$noclip", "$noztest", "$comparez", "$writez", "$decalscale",
    "$animatedtexturevar", "$animatedtextureframenumvar", "$animatedtextureframerate",
    # `$spriteorientation` moved to an explicit handler in `_classify_and_apply` (H-3): it is
    # still provenance only (no master destination), but is now actually read into
    # `misc_provenance["spriteOrientation"]` when a unit authors it, rather than silently
    # discarded despite counting as "mapped".
    "additive", "translucent", "selfillum",
    "// added by psycho-a\r\n}", "// added by psycho-a\n}",
    # Proxy scratch registers (26): declared at top level so a proxy chain has somewhere to write,
    # never read by any program -- the obfuscate noise set plus their halfwidth constants.
    "$one", "$zero", "$temp", "$temp1", "$temp2", "$tempvec", "$abstmp",
    "$a_b_noise", "$a_s_noise", "$j_b_noise", "$j_s_noise", "$xo_b_noise", "$xo_s_noise",
    "$a_threshold", "$j_threshold", "$xo_threshold", "$noisechoice",
    "$a_b_halfwidth", "$a_s_halfwidth", "$a_t_halfwidth",
    "$j_b_halfwidth", "$j_s_halfwidth", "$j_t_halfwidth",
    "$xo_b_halfwidth", "$xo_s_halfwidth", "$xo_t_halfwidth",
})

#: Every key this module recognises as a plain provenance-only leaf.
_ALL_PROVENANCE_ONLY = (
    TEXTURE_PROVENANCE_KEYS | SCALAR_PROVENANCE_KEYS | VECTOR_PROVENANCE_KEYS
    | SWITCH_PROVENANCE_KEYS | INSTANCE_PROPERTY_PROVENANCE_KEYS | TRANSFORM_PROVENANCE_KEYS
    | PROVENANCE_ONLY_KEYS
)


# --- proxy policy (SF-3.3) --------------------------------------------------------------------------

#: Kinds this lane resolves directly into a named master scalar/vector without walking a chain.
#: Every other shader-time-*capable* kind (`texturetransform`, `linearramp`, the arithmetic five) is
#: a node-graph concern for SF-4.3/4.5, which alone can tell whether a chain's inputs are
#: shader-time; this lane records them in provenance only, never guesses shader-time-ness.
PROXY_DIRECT = frozenset({"sine", "animatedtexture", "texturescroll"})
#: "runtime C++" destination in the proxy policy table.
PROXY_RUNTIME = frozenset({
    "globalwetness", "playerproximity", "playerposition", "playerspeed", "textconsole",
    "shadow", "breakablesurface",
})
#: Node-graph-capable kinds this lane does not resolve into scalars (SF-4.3/4.5's job; see above).
PROXY_GRAPH_ONLY = frozenset({"texturetransform", "linearramp", "add", "subtract", "multiply", "abs", "exponential"})
#: "not expressible" -- a chain containing one of these goes to the runtime factory whole.
PROXY_NOT_EXPRESSIBLE = frozenset({"lessorequal", "gaussiannoise", "uniformnoise"})
#: Provenance-only proxy kinds (one material each).
PROXY_PROVENANCE_ONLY = frozenset({"camo", "waterlod", "lampbeam", "lamphalo", "particlesphereproxy"})

#: `sine` `resultvar`, case-folded with a trailing `[i]` split off, -> (`SineTargetMask` component).
#: ".x Alpha, .y Color, .z SelfIllumTint, .w EnvMapTint" ("Exposed parameters" -> the sine lane).
SINE_TARGET_COMPONENT = {"$alpha": 0, "$color": 1, "$selfillumtint": 2, "$envmaptint": 3}
#: `sine` `resultvar` targets that are a *named* destination but emit nothing in the material.
SINE_PROVENANCE_TARGETS = frozenset({"$detailscale"})
#: R7.1 ruling J (`water-architecture.md`, "Surf sine UV translate"): the proxy scratch registers a
#: `sine` -> `texturetransform` chain writes its wave into before a `translatevar` reads it back as
#: a UV slide (`objects/surf`, the pier's 17 wave cards). Exactly these three spellings -- `$tempvec`
#: is a *vector* register no corpus chain translates with, and keeps the omission it has today.
SINE_TEMP_TARGETS = frozenset({"$temp", "$temp1", "$temp2"})
#: The `texturetransform` `resultvar` this lane resolves: the base texture's own matrix, the only
#: transform target `M_V2_Lit`'s UV lane has a home for (`$bumptransform` slides the normal, which
#: no corpus chain sines).
SINE_UV_TRANSFORM_TARGET = "$basetexturetransform"


# --- roots, keys, selection --------------------------------------------------------------------------


def staging_root(work_root: Path) -> Path:
    return Path(work_root) / "import" / FAMILY


def unit_root(export_v2_root: Path) -> Path:
    return Path(export_v2_root) / FAMILY


def units(export_v2_root: Path, select: str | None = None) -> list[Path]:
    """Every published material unit, in a stable order, narrowed to a directory when asked."""

    root = unit_root(export_v2_root)
    if not root.is_dir():
        return []
    found = sorted(root.rglob("*.glb"))
    if select:
        found = [unit for unit in found if in_selection(unit_key(export_v2_root, unit), select)]
    return found


def prune_scope_for(select: str | None) -> str:
    select = normalize_select(select)
    if select is None:
        return PACKAGE_ROOT + "/"
    folded = "/".join(safe_name(part) for part in PurePosixPath(select).parts)
    return f"{PACKAGE_ROOT}/{folded}/"


def _check_key(key: str) -> tuple[list[str], str]:
    parts = PurePosixPath(key).parts
    if not parts or any(part in ("", ".", "..") or ":" in part for part in parts) or "\\" in key:
        raise MaterialImportError(f"{key!r} is not a relative material key")
    return list(parts[:-1]), parts[-1]


def unit_key(export_v2_root: Path, unit: Path) -> str:
    relative = Path(unit).relative_to(unit_root(export_v2_root))
    return PurePosixPath(*relative.parts).with_suffix("").as_posix()


def asset_path_for(key: str) -> str:
    """`vtmb:material:<key>` -> `/ElysiumBaked/Materials/<dir>/MI_<safe stem>` (install and
    patched units alike -- a patched unit's `<key>` already carries its `maps/<map>/` prefix)."""

    directories, stem = _check_key(key)
    folded = "/".join(safe_name(part) for part in directories)
    name = "MI_" + safe_name(stem)
    return f"{PACKAGE_ROOT}/{folded}/{name}" if folded else f"{PACKAGE_ROOT}/{name}"


def decal_asset_path_for(key: str) -> str:
    """`vtmb:material:<key>` -> the PROJECTOR instance beside the surface one (R7.2 ruling 2):
    the same package directory, the surface instance's own name plus `_Decal`. Pure, like
    `asset_path_for` -- the bake restates this fold rather than importing this module (the
    editor's embedded Python carries no numpy, which `importers.textures` imports)."""

    return asset_path_for(key) + DECAL_INSTANCE_SUFFIX


def _base_key_from_asset_id(asset_id: str) -> str:
    if not isinstance(asset_id, str) or not asset_id.startswith("vtmb:material:"):
        raise MaterialImportError(f"not a vtmb:material: asset id: {asset_id!r}")
    return asset_id[len("vtmb:material:"):]


_PATCH_COORD_RE = re.compile(r"_(-?\d+)_(-?\d+)_(-?\d+)$")


def _source_members_sha256(document: dict) -> str:
    """sha256 over `"\\n".join(f"{role}:{sha256}")` for every `sourceResolution.members[]` row, in
    member order (the order `material_glb.build_document` writes them, itself `model.sources`'
    order -- never re-sorted): the install bytes this unit was read from, distinct from
    `unit_sha256` (the exported GLB's own bytes) and carrying the member's role, not just its
    digest, so two members that happen to share a sha256 under different roles still fingerprint
    differently. Empty when the unit carries no `sourceResolution` (a synthetic or malformed
    unit). Named `sourceMembersSha256` (review finding 9, renamed from `sourceSha256`) because a
    plain `sourceSha256` reads as "the source file's hash", singular, when it is actually a digest
    over every resolved source member."""

    members = (document.get("sourceResolution") or {}).get("members") or ()
    rows = [
        f"{member.get('role') or ''}:{member.get('sha256') or ''}"
        for member in members if isinstance(member, dict)
    ]
    if not rows:
        return ""
    return hashlib.sha256("\n".join(rows).encode("utf-8")).hexdigest()


def _resolve_patch_of(stem: str) -> dict | None:
    """`patchOf`: filename-derived, exactly when the stem ends in a `_<x>_<y>_<z>` coordinate
    suffix (the probe origin); `null` for the `cubemapdefault` and `_depth_<n>` copies -- the base
    edge is always available via `patchBase`/the manifest `parent`, per "Identity and naming"."""

    match = _PATCH_COORD_RE.search(stem)
    if not match:
        return None
    return {"x": int(match.group(1)), "y": int(match.group(2)), "z": int(match.group(3))}


# --- results -----------------------------------------------------------------------------------------


@dataclass(slots=True)
class StageResult:
    staging_root: Path
    manifest_path: Path | None = None
    staged: int = 0
    pruned: int = 0
    assets: int = 0
    provenance_only: int = 0
    patched: int = 0
    #: R7.2 ruling 2: the `MI_<unit>_Decal` projector instances staged beside a surface one. They
    #: are entries of the same manifest, so they are counted in `assets` as well.
    projectors: int = 0
    failures: list[tuple[str, str]] = field(default_factory=list)
    protected: int = 0
    #: Review finding 5: every anomaly/omission kind this run recorded, by name -> count, over
    #: every staged unit (not only the failed ones) -- "restore the rule 'never an instance
    #: written with the unknown part quietly missing'": a caller reading `summary()` alone sees
    #: `textureClassMismatch=105` and `animatedFramesArrayUnavailable=12` without walking 19,125
    #: provenance sidecars by hand.
    anomaly_counts: dict[str, int] = field(default_factory=dict)
    omission_counts: dict[str, int] = field(default_factory=dict)

    def summary(self) -> str:
        rollup = ", ".join(f"{kind}={count}" for kind, count in sorted(self.anomaly_counts.items()))
        return (
            f"material staging: {self.assets} instances ({self.patched} patched, "
            f"{self.provenance_only} provenance-only, {self.projectors} decal projectors) "
            f"from {self.staged} units, "
            f"{self.pruned} pruned, {len(self.failures)} failed "
            f"({self.protected} asset paths protected)"
            + (f" -- anomalies: {rollup}" if rollup else "")
            + f" -> {self.staging_root}"
        )


# --- value parsing (SF-3.2 "Value-type rules") ----------------------------------------------------


def _truthy(value: str) -> bool:
    try:
        return float(value.strip()) != 0.0
    except (TypeError, ValueError):
        return bool(value.strip())


def _parse_scalar(value: str) -> float:
    text = value.strip()
    if text.startswith("[") or text.startswith("{"):
        # A handful of corpus units author a scalar-shaped key with a vector value (`$cloudscale`
        # on both `cloud`-family units, `[ 2.00 2.00 2.00 ]`); every component is equal in the
        # corpus, so the first is the scalar.
        return _parse_vector(text, 1)[0]
    return float(text)


def _parse_vector(value: str, n: int) -> list[float]:
    """`[r g b]` (already 0-1), `{r g b}` (0-255, divided down) or a bare number that splats.

    A handful of units separate components with commas (`[1,1,1]`) rather than whitespace; both
    are accepted.
    """

    text = value.strip()
    if text.startswith("[") and text.endswith("]"):
        parts = [float(p) for p in text[1:-1].replace(",", " ").split()]
    elif text.startswith("{") and text.endswith("}"):
        parts = [float(p) / 255.0 for p in text[1:-1].replace(",", " ").split()]
    else:
        parts = [float(text)] * n
    if not parts:
        parts = [0.0] * n
    elif len(parts) < n:
        parts = parts + [parts[-1]] * (n - len(parts))
    return [round(v, 6) for v in parts[:n]]


def _vector4_colour(value: str) -> list[float]:
    r, g, b = _parse_vector(value, 3)
    return [r, g, b, 1.0]


def _vector4_cloudscale(value: str) -> list[float]:
    x, y, z = _parse_vector(value, 3)
    return [x, y, z, 0.0]


_VECTOR_SHAPE = {
    "EnvMapTint": _vector4_colour, "Color": _vector4_colour, "RefractTint": _vector4_colour,
    "FogColor": _vector4_colour, "ReflectTint": _vector4_colour, "SelfIllumTint": _vector4_colour,
    "WaterColor": _vector4_colour, "CloudScale": _vector4_cloudscale,
}

_TRANSFORM_RE = re.compile(
    r"scale\s+([-\d.eE]+)\s+([-\d.eE]+)|translate\s+([-\d.eE]+)\s+([-\d.eE]+)"
)


def _parse_texture_transform(value: str) -> list[float]:
    """`center 0 0 scale 2 2 rotate 0 translate 0 0` -> `[scaleX, scaleY, translateX, translateY]`.

    Rotation and centre are not representable by a 2x1 `TexScaleOffset` and are dropped here (the
    stage-level fold this key alone gets an explicit parse for, per the design)."""

    scale = [1.0, 1.0]
    translate = [0.0, 0.0]
    for match in _TRANSFORM_RE.finditer(value):
        if match.group(1) is not None:
            scale = [float(match.group(1)), float(match.group(2))]
        elif match.group(3) is not None:
            translate = [float(match.group(3)), float(match.group(4))]
    return scale + translate


_TEX_SCALE_OFFSET_DEFAULT = (1.0, 1.0, 0.0, 0.0)


def _merge_vector_half(params: "_Params", name: str, half: str, values: list[float]) -> None:
    current = list(params.vectors.get(name, _TEX_SCALE_OFFSET_DEFAULT))
    if half == "xy":
        current[0], current[1] = values[0], values[1]
    elif half == "zw":
        current[2], current[3] = values[0], values[1]
    params.vectors[name] = current


# --- classifying and staging one unit ---------------------------------------------------------------


@dataclass(slots=True)
class _Params:
    """Everything one unit's parameter walk accumulates."""

    textures: dict[str, str] = field(default_factory=dict)
    scalars: dict[str, float] = field(default_factory=dict)
    vectors: dict[str, list[float]] = field(default_factory=dict)
    switches: dict[str, bool] = field(default_factory=dict)
    instance_flags: dict[str, str] = field(default_factory=dict)  # raw for blend resolution
    runtime: list[dict] = field(default_factory=list)
    anomalies: list[dict] = field(default_factory=list)
    omissions: list[dict] = field(default_factory=list)
    provenance_rows: list[dict] = field(default_factory=list)  # every parameter, in order
    envmap_value: str | None = None
    surfaceprop_value: str | None = None
    texture_deps: dict[str, str] = field(default_factory=dict)  # parameter -> resolved asset id
    material_refs: dict[str, str] = field(default_factory=dict)  # parameter -> asset id (material)
    is_decal_surface: bool = False
    ignorez_truthy: bool = False
    misc_provenance: dict = field(default_factory=dict)
    #: Required-slot parameter names (`BaseTexture`, `NormalMap`) a `textureClassMismatch` on the
    #: slot did not actually leave unbound -- `_apply_static_frame_fallback` resolved them onto the
    #: static frame-0 array binding instead, so `_check_required_slots` must not count them missing.
    static_frame_resolved: set[str] = field(default_factory=set)
    #: Required-slot parameter names whose `textureClassMismatch` was ruled a named divergence
    #: rather than a stage failure -- `_apply_cube_base_texture_divergence`'s cubemap `$basetexture`
    #: pair. Subtracted by `_check_required_slots` exactly like `static_frame_resolved`.
    cube_base_resolved: set[str] = field(default_factory=set)


def _dependency_lookup(document: dict) -> dict[str, tuple[str, bool]]:
    """`{parameter: (asset id, resolved)}` from `dependencies[]`, texture and material roles."""

    out: dict[str, tuple[str, bool]] = {}
    for row in document.get("dependencies") or ():
        if not isinstance(row, dict):
            continue
        parameter = row.get("parameter")
        asset = row.get("asset")
        role = row.get("role")
        if isinstance(parameter, str) and isinstance(asset, str) and role in ("texture", "material"):
            out[parameter] = (asset, bool(row.get("resolved")))
    return out


def _texture_asset_path(texture_key: str, *, cube: bool, twin: bool) -> str:
    """The slice-2 texture asset path for a `vtmb:texture:<key>` dependency (`textures.py`'s rule,
    reimplemented locally so this module does not import a texture-lane internal)."""

    parts = PurePosixPath(texture_key).parts
    directories, stem = list(parts[:-1]), parts[-1]
    folded = "/".join(safe_name(part) for part in directories)
    prefix = "TC_" if cube else "T_"
    name = prefix + safe_name(stem) + ("_linear" if twin else "")
    return f"/ElysiumBaked/Textures/{folded}/{name}" if folded else f"/ElysiumBaked/Textures/{name}"


def _texture_role_conflict(texture_staging_root: Path | None, texture_key: str) -> tuple[bool, bool]:
    """`(roleConflict, verified)` for a texture unit, read from its staged colour sidecar."""

    if texture_staging_root is None:
        return False, False
    parts = PurePosixPath(texture_key).parts
    sidecar = Path(texture_staging_root).joinpath(*parts[:-1], parts[-1] + TEXTURE_PROVENANCE_SUFFIX)
    try:
        content = json.loads(sidecar.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return False, False
    if not isinstance(content, dict) or content.get("twinOf") is not None:
        return False, False
    return bool(content.get("roleConflict")), True


def _texture_array_asset_path(texture_key: str, *, twin: bool = False) -> str:
    """`/ElysiumBaked/Textures/<dir>/TA_<stem>` -- the `Texture2DArray` sibling `textures.py` stages
    for a unit whose `frames` (or KTX layer count) exceeds one (`_texture_class`'s own rule).
    Reused by the `animatedtexture`-proxy frames-array binding (`_apply_proxies` below, review
    finding 2): `BaseTextureFrames`/`NormalMapFrames` have no stated asset-path formula of their
    own in the design, so this is `_texture_asset_path`'s `TC_`/`T_` shape with the `TA_` prefix
    `textures.py::CLASS_PREFIX` actually uses for that class. `twin` names the `_linear` sibling
    the texture lane stages beside a role-conflicted colour array (R7.1: `dev/water_normal` is a
    29-frame BGR888 the lane classes as colour, sampled as a normal on every water unit)."""

    parts = PurePosixPath(texture_key).parts
    directories, stem = list(parts[:-1]), parts[-1]
    folded = "/".join(safe_name(part) for part in directories)
    name = "TA_" + safe_name(stem) + ("_linear" if twin else "")
    return f"/ElysiumBaked/Textures/{folded}/{name}" if folded else f"/ElysiumBaked/Textures/{name}"


def _frames_array_path(texture_staging_root: Path | None, param_name: str, texture_key: str) -> str:
    """The frames array a slot binds: the `_linear` twin when the slot is a data-class parameter
    (`NormalMap`) and the texture unit staged as a role-conflicted colour -- the same twin rule
    `_bind_texture` applies to the plain 2D slot, restated for the array sibling."""

    twin = False
    if param_name in DATA_CLASS_TEXTURE_PARAMS:
        twin, _verified = _texture_role_conflict(texture_staging_root, texture_key)
    return _texture_array_asset_path(texture_key, twin=twin)


def _texture_frame_count(texture_staging_root: Path | None, texture_key: str | None) -> int | None:
    """The referenced texture unit's own staged `frames` count (its sidecar's `frames` field,
    `textures.py`'s own `_texture_class`: `frames > 1` is exactly the `Texture2DArray` rule), or
    `None` when the sidecar is unavailable, unreadable, or the texture is a plain `Texture2D`
    (`frames <= 1`). Reuses the same per-unit sidecar `_texture_role_conflict` already reads."""

    if texture_staging_root is None or not texture_key:
        return None
    parts = PurePosixPath(texture_key).parts
    sidecar = Path(texture_staging_root).joinpath(*parts[:-1], parts[-1] + TEXTURE_PROVENANCE_SUFFIX)
    try:
        content = json.loads(sidecar.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return None
    if not isinstance(content, dict):
        return None
    frames = content.get("frames")
    if not isinstance(frames, int) or frames <= 1:
        return None
    return frames


def _texture_staged_class(texture_staging_root: Path | None, texture_key: str | None) -> str | None:
    """The referenced texture unit's actual staged Unreal class -- `TextureCube`, `Texture2DArray`
    or `Texture2D` -- read from its sidecar's `faces`/`frames` fields, mirroring `textures.py`'s
    own `_texture_class` rule (`faces == 6` wins over `frames`). `None` when the sidecar is
    unavailable or unreadable, so a caller with no texture-staging context falls back to the
    parameter-name guess `_texture_asset_path` makes rather than refusing every binding."""

    if texture_staging_root is None or not texture_key:
        return None
    parts = PurePosixPath(texture_key).parts
    sidecar = Path(texture_staging_root).joinpath(*parts[:-1], parts[-1] + TEXTURE_PROVENANCE_SUFFIX)
    try:
        content = json.loads(sidecar.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return None
    if not isinstance(content, dict):
        return None
    if content.get("faces") == 6:
        return "TextureCube"
    frames = content.get("frames")
    if isinstance(frames, int) and frames > 1:
        return "Texture2DArray"
    return "Texture2D"


def _bind_texture(
    params: _Params, key: str, param_name: str, document: dict, deps: dict[str, tuple[str, bool]],
    *, cube: bool, texture_staging_root: Path | None,
) -> None:
    resolution = deps.get(key)
    if resolution is None or not resolution[1]:
        return  # unresolved: recorded in provenance's textureBindings, never bound to a parameter
    asset_id, _resolved = resolution
    texture_key = asset_id[len("vtmb:texture:"):] if asset_id.startswith("vtmb:texture:") else asset_id
    # Recorded regardless of the class check below: the `animatedtexture` proxy handler
    # (`_classify_and_apply`) looks the dependency up by parameter name to find its own frames-array
    # sibling even when this function declines to bind the plain parameter to it.
    params.texture_deps[param_name] = asset_id
    # A texture parameter expects a `TextureCube` (EnvMap-family params) or a `Texture2D`; when the
    # unit it resolves to actually staged as the other -- or as a `Texture2DArray`, the `frames > 1`
    # rule leaves no plain `T_`/`TC_` sibling at all -- `_texture_asset_path`'s guess names an asset
    # that was never written (a `$envmapsphere` user's flat sphere-map bound where a cubemap is
    # wanted, the `envmap/gioint`/`skybox/hav_env` break-glass pair the other way, or any
    # `animatedtexture` unit's own static slot). Leaving the parameter unbound -- the master's
    # default -- rather than emitting a dangling reference is the same call already made for an
    # unresolved dependency just above.
    staged_class = _texture_staged_class(texture_staging_root, texture_key)
    expected_class = "TextureCube" if cube else "Texture2D"
    if staged_class is not None and staged_class != expected_class:
        params.anomalies.append({
            "kind": "textureClassMismatch", "parameter": param_name, "key": key,
            "expected": expected_class, "staged": staged_class,
        })
        return
    twin = False
    verified = True
    if param_name in DATA_CLASS_TEXTURE_PARAMS:
        conflict, verified = _texture_role_conflict(texture_staging_root, texture_key)
        twin = conflict
    path = _texture_asset_path(texture_key, cube=cube, twin=twin)
    params.textures[param_name] = path
    if param_name in DATA_CLASS_TEXTURE_PARAMS and not verified:
        params.anomalies.append({"kind": "twinUnverified", "parameter": param_name, "key": key})


def _classify_and_apply(
    params: _Params, key_lower: str, key: str, value: str, block: str, document: dict,
    deps: dict[str, tuple[str, bool]], *, family: str, texture_staging_root: Path | None,
) -> str | None:
    """Apply one top-level parameter; return an unmapped key name, or `None` when it was placed."""

    if key_lower == PATCH_ONLY_KEY:
        return None  # patch metadata, not a parameter
    if key_lower == SURFACEPROP_KEY:
        params.surfaceprop_value = value
        return None
    if key_lower == "$envmap":
        params.envmap_value = value
        return None
    if key_lower == "$bumpmap":
        param_name = "DuDvMap" if family in ("water", "refract", "heatglow") else "NormalMap"
        _bind_texture(params, key, param_name, document, deps, cube=False,
                      texture_staging_root=texture_staging_root)
        return None
    if key_lower == "$selfillum":
        if family in _UNLIT_FAMILIES:
            # "$selfillum on an unlit surface (9 units) is meaningless -- the surface is already
            # pure emissive -- and is recorded as an anomaly, not a switch." M_V2_Unlit exposes
            # neither `SelfIllumAmount` nor `UseSelfIllum`.
            params.anomalies.append({"kind": "selfIllumOnUnlitSurface", "value": value})
            return None
        try:
            amount = _parse_scalar(value)
        except ValueError:
            amount = 1.0 if _truthy(value) else 0.0
        params.scalars["SelfIllumAmount"] = amount
        params.switches["UseSelfIllum"] = amount != 0.0
        return None
    if key_lower == "$decal":
        # No longer a master switch: the provenance flag `isDecalSurface` the placement lane reads
        # (`IsDecalSurface` had no depth-offset pin to gate -- "Four parameters that left the
        # masters").
        params.is_decal_surface = _truthy(value)
        return None
    if key_lower == "$ignorez":
        params.ignorez_truthy = _truthy(value)
        return None  # no material property (`bDisableDepthTest` is material-only); see stage_unit
    if key_lower == "$spriterendermode":
        params.instance_flags[key_lower] = value  # blend-state selector, not a parameter
        return None
    if key_lower == "$alphatestreference":
        params.instance_flags[key_lower] = value  # latent: overrides the 0.5 clip value if authored
        return None
    if key_lower in ("$minlight", "$maxlight", "$subdivsize", "$curve"):
        try:
            params.misc_provenance[_MISC_PROVENANCE_SCALAR_KEYS[key_lower]] = _parse_scalar(value)
        except ValueError:
            pass
        return None
    if key_lower == "$spriteorigin":
        try:
            params.misc_provenance["spriteOrigin"] = _parse_vector(value, 2)
        except ValueError:
            pass
        return None
    if key_lower == "$spriteorientation":
        # H-3: a component-lane key ("Provenance" -> the placement/map/runtime rows) that used to
        # be a plain provenance-only leaf -- recorded as covered (mapped) but never actually read
        # into a structured field, so `spriteOrientation` was always null in the sidecar even on a
        # unit that authored it. Promoted alongside `$spriteorigin`/`$minlight`/etc: parsed when
        # authored, left absent (null) otherwise.
        try:
            params.misc_provenance["spriteOrientation"] = _parse_scalar(value)
        except ValueError:
            pass
        return None

    if key_lower in SCALE_XY_KEYS:
        name = SCALE_XY_KEYS[key_lower]
        try:
            _merge_vector_half(params, name, "xy", _parse_vector(value, 2))
        except ValueError:
            pass
        return None
    if key_lower in OFFSET_ZW_KEYS:
        name = OFFSET_ZW_KEYS[key_lower]
        try:
            _merge_vector_half(params, name, "zw", _parse_vector(value, 2))
        except ValueError:
            pass
        return None

    if key_lower in TEXTURE_PARAM_MAP:
        _bind_texture(params, key, TEXTURE_PARAM_MAP[key_lower], document, deps, cube=False,
                      texture_staging_root=texture_staging_root)
        return None
    if key_lower in TEXTURE_PROVENANCE_KEYS:
        resolution = deps.get(key)
        if resolution is not None:
            role = "material" if key_lower in MATERIAL_REFERENCE_KEYS else "texture"
            (params.material_refs if role == "material" else params.texture_deps)[key] = resolution[0]
        return None

    if key_lower in SCALAR_PARAM_MAP:
        params.scalars[SCALAR_PARAM_MAP[key_lower]] = _parse_scalar(value)
        return None
    if key_lower in SCALAR_PROVENANCE_KEYS:
        return None

    if key_lower in VECTOR_PARAM_MAP:
        name = VECTOR_PARAM_MAP[key_lower]
        params.vectors[name] = _VECTOR_SHAPE[name](value)
        return None
    if key_lower in VECTOR_PROVENANCE_KEYS:
        return None
    component = _VECTOR_COMPONENT.match(key_lower)
    if component and component.group(1) in VECTOR_PARAM_MAP:
        name = VECTOR_PARAM_MAP[component.group(1)]
        index = int(component.group(2))
        current = params.vectors.get(name, list(_VECTOR_SHAPE[name]("[0 0 0]")))
        if 0 <= index < len(current):
            current[index] = _parse_scalar(value)
        params.vectors[name] = current
        return None

    if key_lower in SWITCH_PARAM_MAP:
        params.switches[SWITCH_PARAM_MAP[key_lower]] = _truthy(value)
        if key_lower == "$normalalphaenvmapmask":
            params.anomalies.append({"kind": "misspelledKey", "key": key,
                                      "correctedTo": "$normalmapalphaenvmapmask"})
        return None
    if key_lower in SWITCH_PROVENANCE_KEYS:
        return None

    if key_lower in INSTANCE_PROPERTY_KEYS:
        params.instance_flags[key_lower] = value
        if key_lower == "$alphatested":
            params.anomalies.append({"kind": "misspelledKey", "key": key, "correctedTo": "$alphatest"})
        return None
    if key_lower in INSTANCE_PROPERTY_PROVENANCE_KEYS:
        return None

    if key_lower in RUNTIME_KEYS:
        return None  # recorded via texture_deps/material_refs already when texture-shaped

    if key_lower in TRANSFORM_KEYS:
        params.vectors["TexScaleOffset"] = _parse_texture_transform(value)
        return None
    if key_lower in TRANSFORM_PROVENANCE_KEYS:
        return None

    if key_lower in _ALL_PROVENANCE_ONLY:
        return None

    return key


def _resolve_envmap(
    params: _Params, document: dict, deps: dict[str, tuple[str, bool]], *, patched: bool,
    texture_staging_root: Path | None,
) -> dict:
    """The reflection contract. Returns the provenance `environment` block."""

    value = params.envmap_value
    if value is None:
        return {}
    if patched:
        # A patched unit's concrete `maps/<map>/c...` probe is provenance only, never sampled.
        resolution = deps.get("$envmap")
        asset_id = resolution[0] if resolution else None
        probe_path = _texture_asset_path(
            asset_id[len("vtmb:texture:"):], cube=True, twin=False,
        ) if isinstance(asset_id, str) and asset_id.startswith("vtmb:texture:") else None
        return {
            "envMapSymbol": value, "envMapAssetId": asset_id, "envMapProbePath": probe_path,
            "patchedProbe": True,
        }
    if value == "env_cubemap":
        # No texture asset, and no runtime bind either -- SF-6.2's capture actors supply the
        # image through Lumen, with nothing bound to a parameter.
        params.switches.setdefault("UseEnvMap", True)
        return {"envMapSymbol": value}
    # Authored fixed cube: envmap/<name>, shadertest/*, dev/*, lib/redenv_skyref.
    _bind_texture(params, "$envmap", "EnvMap", document, deps, cube=True,
                  texture_staging_root=texture_staging_root)
    params.switches["UseEnvMap"] = True
    if "EnvMap" in params.textures:
        params.switches["UseFixedCube"] = True
    return {"envMapSymbol": value, "envMapAsset": params.textures.get("EnvMap")}


#: Only Lit/LitTranslucent and Unlit expose `MetallicTint`; `M_V2_Refract`, `M_V2_Water` and the
#: rest have a plain `EnvMapTint` with no grey/chromatic branch at all.
_CHROMATIC_CAPABLE_FAMILIES = _LIT_FAMILIES | {"shatteredglass"} | _UNLIT_FAMILIES


def _apply_chromatic_tint(
    params: _Params, environment: dict, *, patched: bool, family: str, chroma_threshold: float,
) -> None:
    """`$envmaptint` splits grey from chromatic ("Reflection contract"). An authored fixed cube
    beats the chromatic branch -- `MetallicTint` stays off whenever `UseFixedCube` is set. The
    classification is recorded in provenance for every family; the switch is only ever set on a
    master that exposes `MetallicTint`."""

    if patched or not params.switches.get("UseEnvMap"):
        return
    tint = params.vectors.get("EnvMapTint", [1.0, 1.0, 1.0, 1.0])
    spread = max(tint[:3]) - min(tint[:3])
    chromatic = spread > chroma_threshold
    environment["envMapTintChromatic"] = chromatic
    if chromatic and not params.switches.get("UseFixedCube") and family in _CHROMATIC_CAPABLE_FAMILIES:
        params.switches["MetallicTint"] = True


def _apply_envmapmask_precedence(params: _Params) -> None:
    """`$envmapmask` beats `$normalmapalphaenvmapmask` beats `$basealphaenvmapmask`; the losers are
    recorded as anomalies rather than left both on, per the reflection contract."""

    order = ["UseEnvMapMask", "UseNormalMapAlphaEnvMapMask", "UseBaseAlphaEnvMapMask"]
    have_mask_texture = "EnvMapMask" in params.textures
    if have_mask_texture:
        params.switches["UseEnvMapMask"] = True
    active = [name for name in order if params.switches.get(name)]
    for loser in active[1:]:
        params.switches[loser] = False
        params.anomalies.append({"kind": "envMapMaskPrecedenceLoser", "switch": loser})


#: Masters whose `UseBaseTexture` this stage resolves explicitly (review finding 4): every master
#: that exposes the switch. `EnvMap`/`EnvMapMask` are deliberately excluded from the generic
#: `_TEXTURE_SWITCH_PAIRS` table below because `_resolve_envmap`/`_apply_envmapmask_precedence`
#: already give them their own precedence-aware rules; `UseBaseTexture` has no such bespoke logic
#: anywhere in this module, so it gets one here instead.
_USE_BASE_TEXTURE_MASTERS = frozenset({
    "M_V2_Lit", "M_V2_LitTranslucent", "M_V2_Unlit", "M_V2_Water", "M_V2_Refract",
})


def _resolve_use_base_texture(params: _Params, document: dict) -> bool:
    """`False` when the unit authors no `$basetexture` at all, or when its resolved default
    (`condition == ""`) pixel program is a `*_NoTexture` variant (`shader-programs/psh/*notexture*`,
    e.g. `lightmappedgeneric_notexture`) -- both cases the design's "Import" section states get
    `UseBaseTexture=False` explicitly rather than left to the master's own static-switch default.
    `True` otherwise. Review fix (finding 4): this switch was never set by the stage at all before
    this fix, on any of the five masters that expose it."""

    if "BaseTexture" not in params.textures:
        return False
    programs = (document.get("shaderResolution") or {}).get("programs") or ()
    for program in programs:
        if not isinstance(program, dict):
            continue
        pixel_shader = str(program.get("pixelShader") or "").lower()
        if "notexture" in pixel_shader:
            return False
    return True


#: `texture parameter -> gating switch parameter`, for slots whose switch is a plain "bound =>
#: True" rule with no precedence or fallback logic of its own (review finding 6's audit of every
#: texture-slot/switch pair in `EXPOSED_PARAMS`). `EnvMap`/`EnvMapMask` are handled by
#: `_resolve_envmap`/`_apply_envmapmask_precedence` instead (their switches interact with each
#: other and with `UseFixedCube`); `Iris`/`Glint`/`DuDvMap` have no paired switch on any master at
#: all (an eyes unit's Iris is always sampled when bound, and `UseGlint`/`DuDvMap` are declared and
#: gated by proxy/design rules of their own, not "bound => on").
_TEXTURE_SWITCH_PAIRS = {
    "NormalMap": "UseNormalMap",
    "BaseTexture2": "UseBaseTexture2",
    "CloudAlphaTexture": "UseCloudAlpha",
}


def _apply_texture_switch_pairs(params: _Params, master: str) -> None:
    exposed = EXPOSED_PARAMS[master]
    for texture_name, switch_name in _TEXTURE_SWITCH_PAIRS.items():
        # R7.1: the slot's frames array (`NormalMapFrames`, bound by the `animatedtexture` proxy
        # or the static-frame fallback) is the slot bound, for the gate's purposes -- before this
        # every water unit animated a normal that `UseNormalMap` then discarded.
        bound = texture_name in params.textures or (texture_name + "Frames") in params.textures
        if bound and switch_name in exposed:
            params.switches[switch_name] = True


def _apply_water_underside(params: _Params, master: str, key: str) -> None:
    """R7.1 ruling E (`water-architecture.md` section 4.2): a water unit whose `$bottommaterial`
    names *itself* is the underside material -- `dev/dev_waterbeneath2` on every water map, the
    faces VBSP emits on the inward side of every water brush face. The engine strips
    `$reflecttexture` from the material of every down-facing water face (`Mod_LoadFaces`), and
    the SLW master has no camera-under-water branch to lean on (5.8 hardcodes it off), so the
    instance says so once: `Underside` zeroes the specular and the volume extinction.

    The authored value is read straight off the unit's own provenance rows, not off
    `params.material_refs`: the GLB decoder emits a `dependencies[]` row only for a
    texture-shaped value, so `$bottommaterial` reaches no dependency on any of the 26 units that
    author it (measured on the corpus, 2026-09-04) and a `material_refs` lookup was dead. The
    value is a bare material path in its own right, compared normalised (`\\` -> `/`, an explicit
    `.vmt` stripped, case-folded) against this unit's own material key -- the same spelling the
    key already carries."""

    if master != "M_V2_Water":
        return
    bottom = next(
        (row.get("value") for row in params.provenance_rows
         if str(row.get("key") or "").lower() == "$bottommaterial" and not row.get("block")),
        None,
    )
    params.switches["Underside"] = (
        bottom is not None and _normalized_material_path(str(bottom)) == key.strip().lower()
    )


def _normalized_material_path(value: str) -> str:
    """A `materials/`-relative material path as the unit keys spell it: forward slashes, no
    `.vmt` suffix, case-folded."""

    folded = value.strip().replace("\\", "/").lower()
    return folded[:-len(".vmt")] if folded.endswith(".vmt") else folded


def _drop_unexposed_sine_uv(params: _Params, master: str) -> None:
    """R7.1 ruling J: `SineUVTranslate` is resolved by `_apply_proxies`, which runs before the
    master is known, and only the Lit pair carries the lane -- the same `sine` -> `texturetransform`
    chain on an `unlitgeneric` or `water` unit would otherwise reach `_validate_exposed` as a
    parameter the master does not have (a stage failure, since only switches are filtered by the
    full-state pass). Dropped here with the omission the chain would have taken had nothing
    consumed it."""

    if "SineUVTranslate" not in params.vectors or "SineUVTranslate" in EXPOSED_PARAMS[master]:
        return
    del params.vectors["SineUVTranslate"]
    params.omissions.append({
        "kind": "proxyTargetProvenanceOnly", "proxy": "texturetransform",
        "target": SINE_UV_TRANSFORM_TARGET,
        "reason": f"{master} exposes no SineUVTranslate lane",
    })


#: Review finding 5: per-master required texture slots. Every real master requires
#: `BaseTexture` -- it is the only slot with no other colour source on any of the nine masters
#: (`EnvMap`/`EnvMapMask`/`NormalMap`/etc. are all optional shading inputs, never the base
#: colour). A `textureClassMismatch` anomaly (`_bind_texture`) that leaves a required slot
#: unbound is a per-unit stage failure (`_check_required_slots`), not merely a recorded anomaly --
#: "never an instance written with the unknown part quietly missing" applies in full to the slot
#: that carries the surface's own colour.
#: R7.1 exempts `M_V2_Water`: on the Single Layer Water master the base texture is *coverage*
#: (`Opacity = UseBaseTexture ? Alpha x BaseTexture.a : 0`), not the surface's colour -- the look
#: is the volume, the reflection and the refraction, none of which read it. A water unit whose
#: `$basetexture` cannot bind (`dev/ocean`, `dev/oceanbeneath`: the DX6 fallback sheet, a 29-frame
#: VTF the master has no frames lane for) stages with `UseBaseTexture` off and draws as water,
#: which is what every other water unit does by default; the anomaly stays recorded.
REQUIRED_TEXTURE_SLOTS: dict[str, frozenset[str]] = {
    master: (frozenset() if master == "M_V2_Water" else frozenset({"BaseTexture"}))
    for master in EXPOSED_PARAMS
}


#: `texture parameter -> (frames-array parameter, frame-count scalar, frame-rate scalar, the
#: frames switch, the slot's own gate switch)`. Source draws frame `$frame`/`$bumpframe` (default
#: 0) of a multi-frame texture when no `animatedtexture` proxy animates it -- so a `$basetexture`/
#: `$bumpmap`(`NormalMap` lane only)/`$normalmap` that resolved but staged as a `Texture2DArray`
#: (a multi-frame VTF) is not actually unbindable, it just needs the array bound statically rather
#: than a plain `T_` sibling that was never written. Only a master exposing the matching lane
#: (`BaseTextureFrames` on Lit/LitTranslucent/Unlit/Sprite, `NormalMapFrames` on Lit/
#: LitTranslucent/Water) takes this fallback; Eyes/Decal/Refract/Water's `BaseTexture`/TwoTexture
#: expose neither lane and keep the loud failure.
_STATIC_FRAME_LANES: dict[str, tuple[str, str, str, str, str]] = {
    "BaseTexture": ("BaseTextureFrames", "FrameCount", "FrameRate", "UseAnimatedFrames", "UseBaseTexture"),
    "NormalMap": (
        "NormalMapFrames", "NormalFrameCount", "NormalFrameRate", "UseAnimatedNormalFrames",
        "UseNormalMap",
    ),
}
#: The VMT key that authors each slot's fixed frame index -- `seam_migration.md`'s ruling: a
#: non-zero authored value is a real divergence from the frame this fallback samples (always 0),
#: recorded as `staticFrameOffsetUnsupported` rather than silently honoured or silently dropped.
_STATIC_FRAME_OFFSET_KEYS = {"BaseTexture": "$frame", "NormalMap": "$bumpframe"}


def _apply_static_frame_fallback(params: _Params, master: str, texture_staging_root: Path | None) -> None:
    """Resolve a `textureClassMismatch` on `BaseTexture`/`NormalMap` onto a static frame-0 array
    binding when the master exposes the matching frames lane and no `animatedtexture` proxy has
    already claimed it (`params.switches[switch_name]` already `True`). Mutates `params` in place;
    marks the slot in `params.static_frame_resolved` so `_check_required_slots` does not also fail
    the unit over the same mismatch."""

    exposed = EXPOSED_PARAMS[master]
    mismatched = {
        anomaly["parameter"]: anomaly for anomaly in params.anomalies
        if anomaly.get("kind") == "textureClassMismatch"
    }
    for param_name, (frames_param, count_name, rate_name, switch_name, gate_switch) in (
        _STATIC_FRAME_LANES.items()
    ):
        if param_name not in mismatched or param_name in params.textures:
            continue
        if frames_param not in exposed or switch_name not in exposed:
            continue  # master has no lane for this slot -- the loud failure stands
        if params.switches.get(switch_name):
            continue  # an animatedtexture proxy already bound this frames array
        asset_id = params.texture_deps.get(param_name)
        texture_key = (
            asset_id[len("vtmb:texture:"):]
            if isinstance(asset_id, str) and asset_id.startswith("vtmb:texture:") else None
        )
        frame_count = _texture_frame_count(texture_staging_root, texture_key)
        if not frame_count:
            continue  # not actually a multi-frame array -- leave the mismatch/failure as is
        params.textures[frames_param] = _frames_array_path(
            texture_staging_root, param_name, texture_key)
        params.scalars[count_name] = float(frame_count)
        params.scalars[rate_name] = 0.0  # static: Source samples one fixed frame, never animates
        params.switches[switch_name] = True
        if gate_switch in exposed:
            params.switches[gate_switch] = True
        params.static_frame_resolved.add(param_name)

        offset_key = _STATIC_FRAME_OFFSET_KEYS.get(param_name)
        raw = next(
            (row.get("value") for row in params.provenance_rows
             if str(row.get("key") or "").lower() == offset_key and not row.get("block")),
            None,
        ) if offset_key else None
        if raw is not None:
            try:
                offset = _parse_scalar(str(raw))
            except ValueError:
                offset = 0.0
            if offset:
                params.omissions.append({
                    "kind": "staticFrameOffsetUnsupported", "parameter": param_name,
                    "key": offset_key, "value": raw,
                })


#: R7.1 follow-up: the 2 units whose `$basetexture` names a **cubemap** VTF (`faces == 6`), so the
#: binding `_bind_texture` declines leaves `M_V2_Unlit`'s required `BaseTexture` slot unbound and
#: `_check_required_slots` used to refuse the unit outright. Measured on the corpus 2026-09-04:
#:
#: * `envmap/gioint` -- 32x32 DXT1 cube; `UnlitGeneric { $baseTexture envmap/gioint }`. The
#:   Giovanni-mansion interior probe, consumed as `$envmap` by 17 `stone/gio*` units.
#: * `skybox/hav_env` -- 256x256 DXT1 cube; `UnlitGeneric { $basetexture skybox/hav_env, $nofog 1 }`.
#:   Referenced by no other unit at all.
#:
#: Neither is *drawn*: neither key appears as a face material in any exported map's `usemtl` list,
#: on any model, or in any `.env`/`.props`/`.ents` sidecar. They are the `.vmt` companions of a
#: cubemap `.vtf`, and Source could not draw them either -- `$basetexture` is a 2D sampler on
#: `UnlitGeneric`, so a cube VTF bound there samples nothing meaningful in the 2004 engine; the
#: cubemap reaches the screen only through another unit's `$envmap`, which reads the texture and
#: never this material. Named here per unit rather than keyed on the class mismatch itself,
#: exactly like `IGNOREZ_NAMED_DIVERGENCE_UNITS`: a cube `$basetexture` on a unit that *is* drawn
#: keeps the loud failure (review finding 5 stands for every unit not on this list).
CUBE_BASE_TEXTURE_DIVERGENCE_UNITS = frozenset({"envmap/gioint", "skybox/hav_env"})


def _apply_cube_base_texture_divergence(params: _Params, master: str, key: str) -> None:
    """Rule the `CUBE_BASE_TEXTURE_DIVERGENCE_UNITS` pair's unbound `BaseTexture` a named
    divergence rather than a stage failure.

    Unlike `_apply_static_frame_fallback` there is no lane to fall back onto: no master carries a
    cube base-colour sampler, and adding one would author a look the VMT does not describe. So the
    instance ships with `UseBaseTexture` off -- already resolved that way by
    `_resolve_use_base_texture`, since the slot never entered `params.textures` -- and the reason
    is written down once, in provenance, beside the anomaly that caused it."""

    if key not in CUBE_BASE_TEXTURE_DIVERGENCE_UNITS:
        return
    required = REQUIRED_TEXTURE_SLOTS.get(master, frozenset())
    for anomaly in params.anomalies:
        if anomaly.get("kind") != "textureClassMismatch":
            continue
        parameter = anomaly.get("parameter")
        if parameter not in required or anomaly.get("staged") != "TextureCube":
            continue
        if parameter in params.textures or parameter in params.static_frame_resolved:
            continue
        params.cube_base_resolved.add(parameter)
        params.omissions.append({
            "kind": "cubeBaseTextureProvenanceOnly",
            "parameter": parameter,
            "key": anomaly.get("key"),
            "reason": (
                f"{master}'s {parameter} is a Texture2D slot and this unit's authored texture "
                "staged as a TextureCube (faces == 6); Source's 2D base sampler cannot draw it "
                "either and nothing in the corpus draws this unit -- the cubemap is consumed "
                "through another unit's $envmap. Recorded, never bound."
            ),
        })


def _check_required_slots(params: _Params, master: str) -> None:
    """Raise when a required slot's only candidate texture was a `textureClassMismatch` -- the
    unit authored the key, it resolved, but the referenced texture staged as the wrong class, so
    the slot ends up unbound rather than deliberately absent (an unauthored key just leaves the
    slot at the master's own default, never a failure)."""

    required = REQUIRED_TEXTURE_SLOTS.get(master, frozenset())
    mismatched = {
        anomaly["parameter"] for anomaly in params.anomalies
        if anomaly.get("kind") == "textureClassMismatch"
    }
    missing = ((required & mismatched) - set(params.textures)
               - params.static_frame_resolved - params.cube_base_resolved)
    if missing:
        raise MaterialImportError(
            f"{master} requires {sorted(missing)}, but its referenced texture(s) staged as the "
            f"wrong class -- see the textureClassMismatch anomaly"
        )


_SPRITE_BLEND_ROWS = {
    0: "Opaque", 1: "Translucent", 2: "Translucent", 3: "Translucent", 4: "Translucent",
    9: "Translucent", 5: "Additive", 7: "Additive", 8: "Additive",
}


def _resolve_blend(params: _Params, family: str) -> dict:
    flags = params.instance_flags
    overrides: dict = {}
    if family == "sprite" and "$spriterendermode" in flags:
        raw = flags["$spriterendermode"].strip()
        try:
            mode = int(float(raw))
        except ValueError:
            mode = 0
        if mode == 6:
            raise MaterialImportError(
                "$spriterendermode=6 (kRenderEnvironmental) has no shader -- 'Unknown sprite "
                "rendermode'"
            )
        overrides["blendMode"] = _SPRITE_BLEND_ROWS.get(mode, "Opaque")
    elif _truthy(flags.get("$additive", "0")):
        overrides["blendMode"] = "Additive"
    elif _truthy(flags.get("$translucent", "0")):
        overrides["blendMode"] = "Translucent"
        if flags.get("$translucent", "").strip() not in ("0", "1"):
            params.anomalies.append({"kind": "translucentValue", "value": flags["$translucent"]})
    elif _truthy(flags.get("$alphatest", "0")) or _truthy(flags.get("$alphatested", "0")):
        overrides["blendMode"] = "Masked"
        clip = 0.5
        if "$alphatestreference" in flags:
            try:
                clip = _parse_scalar(flags["$alphatestreference"])
            except ValueError:
                pass
        overrides["opacityMaskClipValue"] = clip
    elif family == "decalmodulate":
        overrides["blendMode"] = "Modulate"
    else:
        overrides["blendMode"] = "Opaque"
    # Review finding 2: `twoSided` and `opacityMaskClipValue` are stated explicitly every time,
    # never merely omitted when off -- the importer clears a stale override by seeing an explicit
    # `False`/`None` here, not by an absent key it would have to remember to leave alone. Only
    # `blendMode` is always present already (every branch above sets it).
    overrides["twoSided"] = _truthy(flags.get("$nocull", "0"))
    overrides.setdefault("opacityMaskClipValue", None)
    return overrides


def _apply_scene_fog_inscatter(params: _Params, master: str, blend_mode: str) -> None:
    """R5.4: the one scene-fog value that is an instance fact rather than a primitive one.

    Source forces the fog colour to black under additive blending (an additive surface fades
    OUT in fog; inscattering the haze colour on top of it would brighten the scene), so an
    `Additive` instance of a scene-fog master gets `FogInscatter = 0.0`. Every other instance is
    left on the master's default `1.0`, which is `lerp(shaded, fogColour, f)` unchanged.
    """

    if master in SCENE_FOG_MASTERS and blend_mode == "Additive":
        params.scalars["FogInscatter"] = 0.0


def _resolve_surface_class(
    surfaceprop_value: str | None, directories: list[str], master: str,
) -> tuple[str, int, str]:
    if surfaceprop_value:
        folded = surfaceprop_value.strip().lower()
        if folded == "defualt":
            folded = "default"
        if folded in SURFACE_CLASS_INDEX:
            return folded, SURFACE_CLASS_INDEX[folded], "surfaceprop"
    if directories:
        top = directories[0].lower()
        if top in TOP_DIRECTORY_CLASSES:
            return top, SURFACE_CLASS_INDEX[top], "topdir"
    default_row = FAMILY_DEFAULT_CLASS.get(master, "default")
    return default_row, SURFACE_CLASS_INDEX[default_row], "familyDefault"


def _apply_proxies(
    params: _Params, document: dict, parameters: list[dict], *,
    family: str = "", texture_staging_root: Path | None = None,
) -> list[dict]:
    """Every proxy's provenance row, plus the direct scalar/vector side effects this lane places.

    Two passes (R7.1 ruling J). The loop below resolves every proxy that stands on its own; a
    `sine` writing a `$temp*` scratch register and a `texturetransform` reading one back are both
    *half* of a chain, so the loop only records them and the resolution pass afterwards emits what
    the pair means -- one `SineUVTranslate` vector. A VMT is free to author the two in either
    order (`objects/surf` puts the sine first), which is the other reason this cannot be decided
    inside the walk.
    """

    rows: list[dict] = []
    #: `$temp*` register -> `{component or None: {...}}` -- every `sine` that wrote one, in order.
    temp_sines: dict[str, dict[int | None, dict]] = {}
    #: Every `texturetransform`'s arguments, resolved after the loop.
    transforms: list[dict[str, str]] = []
    for proxy in document.get("proxies") or ():
        if not isinstance(proxy, dict):
            continue
        kind = str(proxy.get("name") or "").lower()
        indices = proxy.get("parameters") or ()
        args: dict[str, str] = {}
        for index in indices:
            if not isinstance(index, int) or not (0 <= index < len(parameters)):
                continue
            row = parameters[index]
            args[str(row.get("key") or "").lower()] = str(row.get("value") or "")

        if kind in PROXY_RUNTIME:
            destination = "runtime"
            params.runtime.append({"kind": kind, "arguments": args})
        elif kind in PROXY_NOT_EXPRESSIBLE:
            destination = "runtime"
            params.runtime.append({"kind": kind, "arguments": args, "notExpressible": True})
        elif kind in PROXY_GRAPH_ONLY:
            destination = "graph"
        elif kind in PROXY_PROVENANCE_ONLY:
            destination = "provenance"
        elif kind in PROXY_DIRECT:
            destination = "scalar"
        else:
            destination = "provenance"
            params.anomalies.append({"kind": "unknownProxy", "proxy": kind})

        if kind == "sine":
            raw_result = args.get("resultvar", "")
            folded = raw_result.strip().lower()
            match = _VECTOR_COMPONENT.match(folded)
            base, component = (match.group(1), int(match.group(2))) if match else (folded, None)
            wave = {}
            for source, target in (("sinemin", "SineMin"), ("sinemax", "SineMax"),
                                   ("sineperiod", "SinePeriod"), ("timeoffset", "SineTimeOffset")):
                if source in args:
                    try:
                        wave[target] = _parse_scalar(args[source])
                    except ValueError:
                        pass
            if base not in SINE_TEMP_TARGETS:
                # Half a chain stages nothing of its own -- not even `SinePeriod`, which on a
                # two-sine unit belongs to whichever sine actually drives a master parameter.
                params.scalars.update(wave)
            if base in SINE_TARGET_COMPONENT:
                mask_index = SINE_TARGET_COMPONENT[base]
                target_mask = list(params.vectors.get("SineTargetMask", [0.0, 0.0, 0.0, 0.0]))
                target_mask[mask_index] = 1.0
                params.vectors["SineTargetMask"] = target_mask
                if component is not None:
                    channel_mask = [0.0, 0.0, 0.0, 0.0]
                    if 0 <= component < 4:
                        channel_mask[component] = 1.0
                    params.vectors["SineChannelMask"] = channel_mask
            elif base in SINE_TEMP_TARGETS:
                # A scratch register is not a shading term: record the wave and let the resolution
                # pass below decide what (if anything) the chain it belongs to emits.
                wave["row"] = len(rows)
                wave["target"] = raw_result
                temp_sines.setdefault(base, {})[component] = wave
            elif base in SINE_PROVENANCE_TARGETS or base.startswith("$temp"):
                params.omissions.append({
                    "kind": "proxyTargetProvenanceOnly", "proxy": "sine", "target": raw_result,
                })
            elif raw_result:
                raise MaterialImportError(f"sine proxy resultvar {raw_result!r} has no destination")
        elif kind == "animatedtexture":
            var = args.get("animatedtexturevar", "").strip().lower()
            normal_lane = var in ("$bumpmap", "$normalmap")
            rate_name = "NormalFrameRate" if normal_lane else "FrameRate"
            count_name = "NormalFrameCount" if normal_lane else "FrameCount"
            frames_param = "NormalMapFrames" if normal_lane else "BaseTextureFrames"
            switch_name = "UseAnimatedNormalFrames" if normal_lane else "UseAnimatedFrames"
            # `$bumpmap` itself binds `DuDvMap` on Water/Refract/heatglow and `NormalMap`
            # elsewhere (`_classify_and_apply`'s own family-aware rule) -- the animated texture's
            # dependency has to be looked up under whichever of the two it actually landed on, or
            # a water/refract unit animating its DuDvMap would never find its own bound texture.
            bound_param = (
                ("DuDvMap" if family in ("water", "refract", "heatglow") else "NormalMap")
                if normal_lane else "BaseTexture"
            )
            # R7.1 (`water-architecture.md` section 4.4): on the water master the only frames lane
            # is the *normal* one, and `Water_Old` reads `$bumpframe` as the shared frame index of
            # both `$bumpmap` (the DUDV, `DuDvMap`, declared-not-wired on the SLW master) and
            # `$normalmap`. The proxy names `$bumpmap`, but the array that has to land in
            # `NormalMapFrames` is `$normalmap`'s own -- binding the DUDV array there (what this
            # branch did before R7.1) drew a signed offset map as the ripple normal, and left
            # `dev/water_normal` unbound on every water unit.
            if family == "water" and normal_lane and "NormalMap" in params.texture_deps:
                bound_param = "NormalMap"
            if "animatedtextureframerate" in args:
                try:
                    params.scalars[rate_name] = _parse_scalar(args["animatedtextureframerate"])
                except ValueError:
                    pass
            # Review fix (finding 2): the switch alone used to be set with neither the frames
            # array nor FrameCount ever bound -- turning it on rendered white (or broke the
            # normal) because BaseTextureFrames/NormalMapFrames stayed on the master's inert
            # `T_V2_DefaultFrames` default. Bind the unit's own `TA_` sibling (the texture lane's
            # `Texture2DArray` twin, when the referenced texture actually staged as one) and set
            # the switch only when there is a real array to sample; otherwise the switch stays off
            # and the omission is named, so a flipbook unit that lost its array does not silently
            # animate over a single frame either.
            asset_id = params.texture_deps.get(bound_param)
            texture_key = (
                asset_id[len("vtmb:texture:"):]
                if isinstance(asset_id, str) and asset_id.startswith("vtmb:texture:") else None
            )
            frame_count = _texture_frame_count(texture_staging_root, texture_key)
            if frame_count:
                params.textures[frames_param] = _frames_array_path(
                    texture_staging_root, bound_param, texture_key)
                params.scalars[count_name] = float(frame_count)
                params.switches[switch_name] = True
                # A required slot (`BaseTexture`) whose only candidate staged as a `Texture2DArray`
                # is resolved once the array itself is bound here -- `_check_required_slots` must
                # not also fail the unit over the same `textureClassMismatch` this proxy just
                # answered (same bookkeeping the static frame-0 fallback uses when no proxy claims
                # the slot at all).
                params.static_frame_resolved.add(bound_param)
            else:
                params.omissions.append({
                    "kind": "animatedFramesArrayUnavailable", "proxy": "animatedtexture",
                    "variable": var,
                    "reason": (
                        f"{bound_param} did not stage as a Texture2DArray (unresolved, or "
                        f"frames <= 1) -- {switch_name} left off rather than sampling an unbound "
                        f"flipbook array"
                    ),
                })
        elif kind == "texturescroll":
            var = args.get("texturescrollvar", "").strip().lower()
            bump_lane = var in ("$bumpoffset", "$bumptransform")
            u_name = "BumpScrollRateU" if bump_lane else "BaseScrollRateU"
            v_name = "BumpScrollRateV" if bump_lane else "BaseScrollRateV"
            try:
                rate = _parse_scalar(args.get("texturescrollrate", "0"))
                angle = _parse_scalar(args.get("texturescrollangle", "0"))
            except ValueError:
                rate, angle = 0.0, 0.0
            radians = math.radians(angle)
            # Two proxies landing on the same lane sum their rates -- exact, since two
            # translations of one UV are one panner at the sum.
            params.scalars[u_name] = round(params.scalars.get(u_name, 0.0) + rate * math.cos(radians), 6)
            params.scalars[v_name] = round(params.scalars.get(v_name, 0.0) + rate * math.sin(radians), 6)
        elif kind == "globalwetness" and "scale" in args:
            # R5.3 (seam_map_material.md -> "Decal fog and wetness homes"): a real per-instance
            # scalar pair on M_V2_Lit/M_V2_LitTranslucent, which alone declare the wetness lane
            # (`_wetness_response` in make_v2_materials.py, reading the live global
            # `MPC_ElysiumEnvironment` value the same way every unit's own `WetnessScale` scales
            # it) -- no per-map material instance needed for either half. Every other master still
            # carries the value in provenance only, since it declares no wetness lane at all.
            try:
                scale_value = _parse_scalar(args["scale"])
            except ValueError:
                scale_value = None
            if scale_value is not None:
                params.misc_provenance["wetnessScale"] = scale_value
                if family in _LIT_FAMILIES:
                    params.scalars["WetnessScale"] = scale_value
                    params.scalars["WetnessDriven"] = 1.0
        elif kind == "texturetransform":
            transforms.append(args)

        rows.append({"index": len(rows), "kind": kind, "sourceName": proxy.get("sourceName"),
                     "arguments": args, "destination": destination})
    _resolve_sine_uv_translate(params, rows, temp_sines, transforms)
    return rows


def _resolve_sine_uv_translate(
    params: _Params, rows: list[dict], temp_sines: dict[str, dict[int | None, dict]],
    transforms: list[dict[str, str]],
) -> None:
    """The second pass of `_apply_proxies` (R7.1 ruling J): join every `sine` that wrote a `$temp*`
    register to the `texturetransform` that translates the base texture by it, and emit the pair as
    one `SineUVTranslate = (ampU, ampV, offU, offV)` -- the graph adds `amp x wave + off` to the
    base UV. `objects/surf` (the pier's 17 wave cards) is `sine $temp[0]` 0 -> .5 over 15 s read by
    `translatevar $temp`, so it stages `[0.5, 0, 0, 0]`.

    What does NOT resolve keeps today's `proxyTargetProvenanceOnly` omission, named on the register
    the chain actually used: a `$temp*` sine nothing consumes; a `rotatevar`/`scalevar` rider (this
    lane has one UV translate and no matrix); a component-less `$temp` a `translatevar` reads,
    because a whole-vector sine writes both components with one number and Source's own `$temp`
    starts as `[0 0]` -- which of U and V the author meant is undecidable, so nothing is guessed.
    """

    consumed: set[int] = set()
    for args in transforms:
        if args.get("resultvar", "").strip().lower() != SINE_UV_TRANSFORM_TARGET:
            continue
        translate = args.get("translatevar", "").strip().lower()
        if translate not in temp_sines:
            continue
        amplitude, offset, used = [0.0, 0.0], [0.0, 0.0], False
        for component, wave in temp_sines[translate].items():
            if component is None or component > 1:
                continue  # undecidable, or a component the UV has no room for
            low = wave.get("SineMin", 0.0)
            amplitude[component] = wave.get("SineMax", 0.0) - low
            offset[component] = low
            consumed.add(wave["row"])
            used = True
            # The `texturetransform` row is already `graph`; say the same of the sine feeding it,
            # which reaches the graph through this vector rather than a scalar of its own.
            rows[wave["row"]]["destination"] = "graph"
            # The chain's own period is the wave the UV rides. It only reaches the instance when
            # no other sine has claimed the single `SinePeriod`/`SineTimeOffset` pair the master
            # exposes; a second sine at a *different* period is a real divergence, named rather
            # than silently overwritten (`objects/surf`'s two sines share 15 s, so it records none).
            for name in ("SinePeriod", "SineTimeOffset"):
                if name not in wave:
                    continue
                staged = params.scalars.get(name)
                if staged is None:
                    params.scalars[name] = wave[name]
                elif staged != wave[name]:
                    params.omissions.append({
                        "kind": "sineChainPeriodMismatch", "parameter": name,
                        "target": wave["target"], "staged": staged, "chain": wave[name],
                    })
        if used:
            params.vectors["SineUVTranslate"] = [amplitude[0], amplitude[1], offset[0], offset[1]]

    for waves in temp_sines.values():
        for wave in waves.values():
            if wave["row"] in consumed:
                continue
            rows[wave["row"]]["destination"] = "provenance"
            params.omissions.append({
                "kind": "proxyTargetProvenanceOnly", "proxy": "sine", "target": wave["target"],
            })


# --- provenance + manifest entry for one unit -------------------------------------------------------


def stage_unit(
    key: str, document: dict, unit_sha256: str, *, texture_staging_root: Path | None = None,
    chroma_threshold: float | None = None,
) -> tuple[dict, dict] | None:
    """`(manifest entry, provenance sidecar)` for one unit, or raise on an unmapped key.

    `document` is the unit's `ELYSIUM_vtmb_material` extension body (not the whole glTF document).
    """

    if chroma_threshold is None:
        chroma_threshold = _read_chroma_threshold()

    identity = document.get("identity") or {}
    asset_id = identity.get("asset")
    if not isinstance(asset_id, str) or not asset_id.startswith("vtmb:material:"):
        raise MaterialImportError("unit declares no material identity")

    patch = document.get("patch")
    patched = isinstance(patch, dict)
    parameters = [row for row in (document.get("parameters") or ()) if isinstance(row, dict)]
    deps = _dependency_lookup(document)
    directories, stem = _check_key(key)
    family = str((document.get("shaderResolution") or {}).get("family")
                 or document.get("shader") or "").lower()

    #: SF-4.3-part-3 ruling (d): a unit-specific allowlist of VMT keys treated as provenance-only
    #: even though the key itself resolves to a named parameter -- that parameter just is not on
    #: *this* unit's own master. See `UNIT_DIVERGENCES`.
    divergent_keys = UNIT_DIVERGENCES.get(key, {})

    params = _Params()
    for row in parameters:
        block = str(row.get("block") or "")
        if block and not block.startswith(("replace#", "insert#")):
            # A proxy-scoped row (normally `proxies#N/<kind>#M`), consumed via `proxies[]` above --
            # or, on a handful of malformed units, a block the exporter could not resolve to a
            # named proxy at all (`{#8/gaussiannoise#0`, `{#8/{#1`); either way its keys mean
            # whatever the enclosing proxy says, never the top-level VMT vocabulary, so they are
            # never checked against the parameter table.
            continue
        raw_key = str(row.get("key") or "")
        key_lower = raw_key.lower()
        value = str(row.get("value") or "")
        params.provenance_rows.append({
            "index": row.get("index"), "block": block, "key": raw_key,
            "sourceKey": row.get("sourceKey"), "value": row.get("value"),
            "valueType": row.get("valueType"),
            # H-3: `material_glb.py` writes `offset` on every unit parameter row (the VMT source
            # byte this key was authored at); wired through so `UElysiumMaterialProvenance`'s own
            # `Offset` read (already live -- `ElysiumMaterialProvenance.cpp`'s `ReadParameters`)
            # has something real to read instead of always defaulting to 0.
            "offset": row.get("offset"),
        })
        if patched and key_lower != PATCH_ONLY_KEY and not block.startswith(("replace#", "insert#")):
            continue  # a patched unit's non-override top-level rows are patch bookkeeping only
        if key_lower in divergent_keys:
            params.omissions.append({
                "kind": "unitDivergenceProvenanceOnly", "key": raw_key,
                "reason": divergent_keys[key_lower],
            })
            continue
        unmapped = _classify_and_apply(
            params, key_lower, raw_key, value, block, document, deps,
            family=family, texture_staging_root=texture_staging_root,
        )
        if unmapped is not None:
            raise MaterialImportError(f"unmapped parameter key {unmapped!r}")

    proxy_rows = _apply_proxies(params, document, parameters,
                                family=family, texture_staging_root=texture_staging_root)
    environment = _resolve_envmap(params, document, deps, patched=patched,
                                  texture_staging_root=texture_staging_root)
    _apply_chromatic_tint(params, environment, patched=patched, family=family,
                          chroma_threshold=chroma_threshold)
    _apply_envmapmask_precedence(params)

    provenance_only = False
    ignorez_named_divergence = False
    if patched:
        base_key = _base_key_from_asset_id(patch.get("asset") or "")
        parent = asset_path_for(base_key)
        surface_class = surface_class_index = None
        class_source = None
        physmat_fallback = False
        base_property_overrides = {}
        master = None
    else:
        base_property_overrides = _resolve_blend(params, family)
        blend_mode = base_property_overrides.get("blendMode", "Opaque")
        if key in IGNOREZ_SPRITE_REROUTE_UNITS and params.ignorez_truthy:
            master = "M_V2_Sprite"
        else:
            master = resolve_master(family, blend_mode)
            if key in IGNOREZ_NAMED_DIVERGENCE_UNITS and params.ignorez_truthy:
                ignorez_named_divergence = True
        if master is None:
            raise MaterialImportError(f"shader family {family!r} has no master")
        parent = f"{MASTER_ROOT}/{master}"
        provenance_only = family in NO_MASTER_FAMILIES
        if provenance_only:
            # "every parameter recorded in provenance; nothing else about them is reproduced"
            keep_textures = {k: v for k, v in params.textures.items() if k == "BaseTexture"}
            params.textures = keep_textures
            params.scalars = {}
            params.vectors = {}
            params.switches = {"UseBaseTexture": "BaseTexture" in keep_textures}
            base_property_overrides = {"blendMode": "Opaque"}
        else:
            # Review fixes (findings 4 and 6): the switch side of every plain "bound => on"
            # texture slot, resolved here rather than left to the master's own static-switch
            # default. Not run on the provenance-only path above, which already resolves its own
            # (reduced) `UseBaseTexture` a few lines up.
            if master in _USE_BASE_TEXTURE_MASTERS:
                params.switches["UseBaseTexture"] = _resolve_use_base_texture(params, document)
            _apply_texture_switch_pairs(params, master)
            _apply_water_underside(params, master, key)
            _drop_unexposed_sine_uv(params, master)
            _apply_static_frame_fallback(params, master, texture_staging_root)
            _apply_cube_base_texture_divergence(params, master, key)
            _check_required_slots(params, master)
            _apply_scene_fog_inscatter(params, master, blend_mode)
        # Review finding 2: state every switch the resolved master exposes, not only the ones a
        # rule above happened to turn on -- an entry re-imported after a corpus/design change that
        # used to turn a switch on and no longer does must land that switch's `False` explicitly,
        # or a stale `MaterialInstanceConstant` (`clear_all_material_instance_parameters` clears
        # non-static parameters only, never a static switch) keeps its old `True` forever.
        params.switches = {name: bool(params.switches.get(name, False)) for name in _switch_names(master)}
        surface_class, surface_class_index, class_source = _resolve_surface_class(
            params.surfaceprop_value, directories, master)
        physmat_fallback = surface_class not in _SURFACEPROP_NAMES
        params.scalars["SurfaceClassIndex"] = surface_class_index
        _validate_exposed(params, master)

    phys_material_path = (f"/ElysiumBaked/SurfaceProperties/PM_default"
                          if (not patched and physmat_fallback) else
                          (f"/ElysiumBaked/SurfaceProperties/PM_{safe_name(surface_class)}"
                           if not patched else None))

    # R7.2 ruling 2: the unit that draws as a PROJECTOR as well as (or instead of) a surface. Two
    # populations, one rule: a `$decal 1` unit (`isDecalSurface`, 550 of them -- 448 projector-only,
    # 15 projector and world face, 9 world face only, 78 unused) and the whole `decalmodulate`
    # family (38, the runtime impact set, never a world face -- 0 `usemtl` hits in 108 maps). Both
    # get `MI_<unit>_Decal` beside their surface instance, so everything that lays or bakes a decal
    # resolves ONE name from a `vtmb:material:` id.
    #
    # A PATCHED unit stages none of its own: it has no master and no family (it parents to another
    # instance), and its projector is its root's. The corpus does carry patched `$decal` units --
    # 10, measured 2026-09-03: `glass/libwndwf` on `hw_warrens_5`/`la_library_1` and
    # `objects/blastdoortrim` on `la_library_1` -- and inheriting the root's twin loses nothing,
    # because a patch delta can carry nothing the projector master exposes: the only non-empty
    # patched delta anywhere in the 19,709-entry manifest is `WaterDepth` (49 water units), and
    # VBSP's own patch is `$envmap`, which `M_V2_Decal` has no pin for. `map_geometry`'s
    # `MaterialBinding` reads `isDecalSurface`/`decalAsset` off the ROOT sidecar for exactly that
    # reason, so the patched face group binds the root's twin by name rather than nothing.
    decal_asset = (decal_asset_path_for(key)
                   if not patched and (params.is_decal_surface or family == "decalmodulate")
                   else None)

    entry = {
        "assetPath": asset_path_for(key),
        "unit": asset_id,
        "unitGlb": f"{FAMILY}/{key}.glb",
        "unitSha256": unit_sha256,
        "sourceMembersSha256": _source_members_sha256(document),
        "parent": parent,
        "patched": patched,
        "provenanceOnly": provenance_only,
        #: The projector instance staged beside this one (R7.2 ruling 2), or `None` when this unit
        #: never draws as a decal. `stage_materials` builds that entry from this one.
        "decalAsset": decal_asset,
        "textures": dict(sorted(params.textures.items())),
        "scalars": dict(sorted(params.scalars.items())),
        "vectors": {k: v for k, v in sorted(params.vectors.items())},
        "switches": dict(sorted(params.switches.items())),
        # Review finding 2: the full list of static-switch names the entry's master exposes
        # (empty for a patched instance, which has no master of its own and never touches a
        # switch) -- `switches` above already states every one of these explicitly, so this is
        # the audit trail for that claim, not a second source the importer must also consult.
        "allSwitches": [] if patched else _switch_names(master),
        "basePropertyOverrides": base_property_overrides,
        "physMaterial": phys_material_path,
        "physMaterialFallback": physmat_fallback if not patched else None,
        "surfaceClass": surface_class,
        "surfaceClassIndex": surface_class_index,
        "surfaceClassSource": class_source,
        "provenance": "/".join([*directories, PurePosixPath(key).name + PROVENANCE_SUFFIX]),
        "recipe": {
            "unitSha256": unit_sha256,
            "parent": parent,
            "settingsVersion": SETTINGS_VERSION,
            "chromaThreshold": chroma_threshold,
            # Review finding 4: the recipe now covers every stated fact about the instance and its
            # provenance, not just the parameter map -- the physical material path (a stale
            # `PhysMaterial` from an earlier recipe shape would otherwise survive re-import
            # unnoticed) and a digest of the sidecar bytes themselves (any provenance-only change,
            # e.g. a corrected anomaly or an added omission row, changes the sidecar without
            # changing a single bound parameter, and the policy is that *any* such change bumps
            # this recipe hash, forcing a re-import that re-stamps and re-saves the instance).
            "physMaterial": phys_material_path,
            "provenanceSha256": None,  # filled below, once `provenance` itself is built
            "params": {
                "textures": dict(sorted(params.textures.items())),
                "scalars": dict(sorted(params.scalars.items())),
                "vectors": {k: v for k, v in sorted(params.vectors.items())},
                "switches": dict(sorted(params.switches.items())),
                "allSwitches": [] if patched else _switch_names(master),
                "basePropertyOverrides": base_property_overrides,
            },
        },
    }

    provenance = {
        "assetId": asset_id,
        "materialPath": identity.get("materialPath"),
        "unitSchemaVersion": document.get("schemaVersion"),
        "unitSha256": unit_sha256,
        "settingsVersion": SETTINGS_VERSION,
        "shader": document.get("shader"),
        "sourceShader": document.get("sourceShader"),
        "shaderFamily": family,
        "shaderResolved": bool((document.get("shaderResolution") or {}).get("resolved")),
        "master": None if patched else f"{MASTER_ROOT}/{master}",
        "blendMode": base_property_overrides.get("blendMode"),
        "twoSided": base_property_overrides.get("twoSided", False),
        "surfaceClass": entry["surfaceClass"],
        "surfaceClassIndex": entry["surfaceClassIndex"],
        "surfaceClassSource": class_source,
        "physMaterialFallback": physmat_fallback if not patched else None,
        "patched": patched,
        "patchBase": (patch.get("asset") if patched else None),
        "patchOf": (_resolve_patch_of(stem) if patched else None),
        "patchKind": (patch.get("operations") if patched else None),
        "environment": environment,
        "isDecalSurface": params.is_decal_surface,
        #: R7.2 ruling 2: the projector instance this unit stages beside its surface one, or
        #: `None`. The map lane copies it onto the staged materials table (`decalAsset`), and the
        #: runtime resolves the same path from a `vtmb:material:` id.
        "decalAsset": decal_asset,
        "ignoreZ": params.ignorez_truthy,
        "ignoreZNamedDivergence": ignorez_named_divergence,
        "spriteOrigin": params.misc_provenance.get("spriteOrigin"),
        "spriteOrientation": params.misc_provenance.get("spriteOrientation"),
        "minLight": params.misc_provenance.get("minLight"),
        "maxLight": params.misc_provenance.get("maxLight"),
        "wetnessScale": params.misc_provenance.get("wetnessScale"),
        "subdivSize": params.misc_provenance.get("subdivSize"),
        "curve": params.misc_provenance.get("curve"),
        "textureBindings": [
            {"parameter": name, "asset": asset} for name, asset in sorted(params.texture_deps.items())
        ],
        "materialReferences": [
            {"parameter": name, "asset": asset} for name, asset in sorted(params.material_refs.items())
        ],
        "parameters": params.provenance_rows,
        "proxies": proxy_rows,
        "runtime": params.runtime,
        "anomalies": params.anomalies + list(document.get("anomalies") or ()),
        "omissions": params.omissions + list(document.get("omissions") or ()),
        "comments": list(document.get("comments") or ()),
        "coverage": {
            "totalKeys": len(parameters),
            # H-3: always empty by construction, not a stand-in for "not computed yet" -- every
            # top-level key this loop walks (above) either resolves through `_classify_and_apply`
            # or raises `MaterialImportError(f"unmapped parameter key {unmapped!r}")` and aborts
            # the unit entirely ("No silent drop" rule). A unit that failed to stage never reaches
            # this dict, let alone gets a sidecar written, so a staged sidecar's `unmappedKeys` can
            # only ever be `[]`; a divergent-key allowlist entry (`UNIT_DIVERGENCES`) is not an
            # exception to that -- it is recorded in `omissions`, still zero unmapped keys.
            "unmappedKeys": [],
        },
    }
    # sha256 over the exact bytes `_write_if_changed` writes for this unit's sidecar (canonical
    # JSON, sorted keys) -- review finding 4's "recipe covers the sidecar too" (see the comment on
    # `entry["recipe"]` above).
    entry["recipe"]["provenanceSha256"] = hashlib.sha256(_json_bytes(provenance)).hexdigest()
    return entry, provenance


# --- the projector twin (R7.2 ruling 2) ---------------------------------------------------------


#: The three fog parameters `M_V2_Decal` exposes that the STAGE never writes (R5.3,
#: `seam_map_material.md` -> "Decal fog and wetness homes"): a `UDecalComponent` carries no Custom
#: Primitive Data, so the world's own distance fog rides three named instance parameters that the
#: placement lane -- and, at runtime, the decal subsystem's MID -- sets per decal from the map's own
#: environment. A `$decal` surface unit whose VMT happens to author `$fogcolor`/`$fogstart` (the
#: water-fog keys, meaningless on a projector) must not leak that value into this lane's home.
_DECAL_FOG_PARAMS = frozenset({"FogColor", "FogStart", "FogInvRange"})


def projector_entry(entry: dict, provenance: dict) -> dict:
    """The `MI_<unit>_Decal` manifest entry for a unit whose `decalAsset` is set.

    Same shape as any other entry -- the editor phase (`import_materials.py`) authors it with the
    code it already has -- parented to `M_V2_Decal` and carrying only what that master exposes:

    * `BaseTexture`, `Alpha`, `Color`, `SurfaceClassIndex` copied from the surface instance (the
      shared four-parameter contract plus the one texture every master requires);
    * `Emissive` + `EmissiveScale` from `$selfillum` (3 units), derived the way the Lit master
      derives its own self-illum term -- Source's self-illum source IS the base texture, so the
      twin binds the same texture into the master's own emissive slot and `EmissiveScale` carries
      `SelfIllumAmount`;
    * the `Unlit` static switch for an unlit-family unit (28 `unlitgeneric` projectors): DefaultLit
      has no unlit shading model, so the master routes the base colour into Emissive instead;
    * blend `Translucent` always. Owner call A (`seam_migration.md` -> "R7.2 Decals"): the 38
      `decalmodulate` units draw as translucent stains, because a deferred decal may only blend
      Translucent/AlphaComposite/Modulate and DBuffer rewrites Modulate to Translucent anyway. A
      `$alphatest` projector (1 unit) takes the same route -- Masked is not a decal blend mode, and
      the alpha it would have clipped is already on Opacity.

    The physical material, surface class and provenance sidecar are the unit's own: ruling 3 binds
    this same instance as the MESH slot of an `isDecalSurface` face group, so a surface query on
    that wall must answer exactly what the surface instance would have answered.
    """

    exposed = EXPOSED_PARAMS[DECAL_MASTER]

    def carried(bucket: dict) -> dict:
        return {name: value for name, value in bucket.items()
                if name in exposed and name not in _DECAL_FOG_PARAMS}

    textures = carried(entry["textures"])
    scalars = carried(entry["scalars"])
    vectors = carried(entry["vectors"])

    self_illum = float(entry["scalars"].get("SelfIllumAmount") or 0.0)
    if self_illum and "BaseTexture" in textures:
        textures["Emissive"] = textures["BaseTexture"]
        scalars["EmissiveScale"] = self_illum

    switches = {name: False for name in _switch_names(DECAL_MASTER)}
    switches["Unlit"] = str(provenance.get("shaderFamily") or "").lower() in _UNLIT_FAMILIES

    overrides = {"blendMode": "Translucent", "twoSided": False, "opacityMaskClipValue": None}
    params = {
        "textures": dict(sorted(textures.items())),
        "scalars": dict(sorted(scalars.items())),
        "vectors": {k: v for k, v in sorted(vectors.items())},
        "switches": dict(sorted(switches.items())),
        "allSwitches": _switch_names(DECAL_MASTER),
        "basePropertyOverrides": overrides,
    }
    return {
        "assetPath": entry["decalAsset"],
        "unit": entry["unit"],
        "unitGlb": entry["unitGlb"],
        "unitSha256": entry["unitSha256"],
        "sourceMembersSha256": entry["sourceMembersSha256"],
        "parent": f"{MASTER_ROOT}/{DECAL_MASTER}",
        "patched": False,
        "provenanceOnly": entry["provenanceOnly"],
        # This IS the projector; it has no second one of its own.
        "decalAsset": None,
        "physMaterial": entry["physMaterial"],
        "physMaterialFallback": entry["physMaterialFallback"],
        "surfaceClass": entry["surfaceClass"],
        "surfaceClassIndex": entry["surfaceClassIndex"],
        "surfaceClassSource": entry["surfaceClassSource"],
        "provenance": entry["provenance"],
        "recipe": {
            "unitSha256": entry["unitSha256"],
            "parent": f"{MASTER_ROOT}/{DECAL_MASTER}",
            "settingsVersion": SETTINGS_VERSION,
            "chromaThreshold": entry["recipe"]["chromaThreshold"],
            "physMaterial": entry["physMaterial"],
            "provenanceSha256": entry["recipe"]["provenanceSha256"],
            "params": params,
        },
        **params,
    }


# --- staging the corpus ------------------------------------------------------------------------------


def _write_if_changed(path: Path, data: bytes) -> bool:
    if path.is_file() and path.stat().st_size == len(data) and path.read_bytes() == data:
        return False
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + ".tmp")
    temporary.write_bytes(data)
    os.replace(temporary, path)
    return True


def _json_bytes(content: dict) -> bytes:
    return (json.dumps(content, indent=1, sort_keys=True) + "\n").encode("utf-8")


def _unit_files(root: Path, key: str) -> set[Path]:
    directories, stem = _check_key(key)
    base = root.joinpath(*directories)
    return {base / (stem + PROVENANCE_SUFFIX)}


def _prune_stale(root: Path, produced: set[Path], select: str | None) -> int:
    if not root.is_dir():
        return 0
    select = normalize_select(select)
    scope = root
    if select:
        scope = root.joinpath(*PurePosixPath(select).parts)
        if not scope.is_dir():
            return 0
    pruned = 0
    for path in list(scope.rglob("*")):
        if path.is_dir() or path in produced:
            continue
        if path.parent == root and path.name in ROOT_FILES:
            continue
        path.unlink()
        pruned += 1
    for directory in sorted((p for p in scope.rglob("*") if p.is_dir()),
                            key=lambda p: len(p.parts), reverse=True):
        try:
            directory.rmdir()
        except OSError:
            pass
    return pruned


def stage_materials(
    export_v2_root: Path, staging_root_path: Path, *, select: str | None = None,
    texture_staging_root: Path | None = None, chroma_threshold: float | None = None,
) -> StageResult:
    """Phase 1: stage every selected unit and write the manifest. See the module docstring."""

    export_v2_root = Path(export_v2_root)
    root = Path(staging_root_path)
    result = StageResult(staging_root=root)
    found = units(export_v2_root, select)
    if not found:
        raise MaterialImportError(
            f"no material units under {unit_root(export_v2_root)}"
            + (f" matching {select!r}" if select else "")
            + "; run `uv run elysium export_v2 materials-glb` first"
        )
    if chroma_threshold is None:
        chroma_threshold = _read_chroma_threshold()

    entries: list[dict] = []
    produced: set[Path] = set()
    provenance_only_keys: list[str] = []
    failed_keys: list[str] = []
    provenance_by_path: dict[str, dict] = {}
    sidecar_path_by_path: dict[str, Path] = {}

    def failed(key: str, reason: str) -> None:
        result.failures.append((key, reason))
        failed_keys.append(key)

    for unit in found:
        key = unit_key(export_v2_root, unit)
        try:
            data = unit.read_bytes()
            unit_sha256 = hashlib.sha256(data).hexdigest()
            document, _binary = decode_glb(data, str(unit))
            extension = (document.get("extensions") or {}).get(MATERIAL_EXTENSION)
            if not isinstance(extension, dict):
                raise MaterialImportError(f"not a {MATERIAL_EXTENSION} unit")
            staged = stage_unit(key, extension, unit_sha256, texture_staging_root=texture_staging_root,
                                chroma_threshold=chroma_threshold)
            if staged is None:
                continue
            entry, provenance = staged
        except (GlbContainerError, MaterialImportError, OSError, ValueError, KeyError) as error:
            failed(key, str(error))
            continue

        result.staged += 1
        if entry["patched"]:
            result.patched += 1
        if entry["provenanceOnly"]:
            result.provenance_only += 1
            provenance_only_keys.append(key)
        # Review finding 5: the rollup counts every staged unit's anomalies/omissions, not only
        # those a caller happens to walk provenance to find.
        for row in provenance["anomalies"]:
            kind = row.get("kind") if isinstance(row, dict) else None
            if kind:
                result.anomaly_counts[kind] = result.anomaly_counts.get(kind, 0) + 1
        for row in provenance["omissions"]:
            kind = row.get("kind") if isinstance(row, dict) else None
            if kind:
                result.omission_counts[kind] = result.omission_counts.get(kind, 0) + 1
        entries.append(entry)

        directories, stem = _check_key(key)
        sidecar_path = root.joinpath(*directories, stem + PROVENANCE_SUFFIX)
        try:
            _write_if_changed(sidecar_path, _json_bytes(provenance))
        except OSError as error:
            failed(key, str(error))
            entries.pop()
            continue
        if entry["decalAsset"]:
            # R7.2 ruling 2: the projector twin is an ordinary manifest entry, so the editor phase
            # authors, prunes, fingerprints and provenance-stamps it with the code it already has.
            # It shares this unit's one provenance sidecar (`import_materials` stamps each entry's
            # own `assetPath` over it), so it is appended only once that sidecar is on disk.
            entries.append(projector_entry(entry, provenance))
            result.projectors += 1
        produced.add(sidecar_path)
        provenance_by_path[entry["assetPath"]] = provenance
        sidecar_path_by_path[entry["assetPath"]] = sidecar_path

    # Two units folding to one asset path is a defect in the fold, not a race one wins.
    owners: dict[str, list[str]] = {}
    for entry in entries:
        owners.setdefault(entry["assetPath"].lower(), []).append(entry["unitGlb"])
    collided_paths = {path for path, keys in owners.items() if len(keys) > 1}
    if collided_paths:
        kept: list[dict] = []
        for entry in entries:
            lowered = entry["assetPath"].lower()
            if lowered in collided_paths:
                unit_glb = entry["unitGlb"][len(FAMILY) + 1:-len(".glb")]
                others = sorted(set(owners[lowered]) - {entry["unitGlb"]})
                failed(unit_glb, f"asset path collides with {', '.join(others)}")
            else:
                kept.append(entry)
        entries = kept

    # A patched unit's base must have staged too, or the patch has nothing to instance-of-instance.
    known_paths = {entry["assetPath"] for entry in entries}
    based: list[dict] = []
    for entry in entries:
        if entry["patched"] and entry["parent"] not in known_paths:
            unit_glb = entry["unitGlb"][len(FAMILY) + 1:-len(".glb")]
            failed(unit_glb, f"base material {entry['parent']!r} was not staged")
        else:
            based.append(entry)
    entries = based

    # Review finding 7: the registry tag `ElysiumMaster` walks empty for a patched instance
    # because its own provenance `master` is `None` (an instance-of-instance has no master of its
    # own -- `bake_lib.make_material_instance` parents it onto another `MI_`, never a `M_V2_*`
    # asset). Walk `parent` through this run's own entries to the base unit's master and stamp
    # *that* onto the patched unit's provenance `master` field, so the Content Browser filter
    # covers every one of the corpus's 7,499 patched instances too, not only the un-patched ones.
    # A base that itself failed to stage (absent from `entries_by_path`) or a cycle (defensive;
    # `topo_order` in the editor phase would also catch it) leaves the patched provenance's
    # `master` at `None`, unchanged -- no worse than before this fix.
    entries_by_path = {entry["assetPath"]: entry for entry in entries}

    def _root_master(entry: dict) -> str | None:
        seen: set[str] = set()
        current = entry
        while current.get("patched"):
            parent_path = current["parent"]
            if parent_path in seen or parent_path not in entries_by_path:
                return None
            seen.add(parent_path)
            current = entries_by_path[parent_path]
        return provenance_by_path[current["assetPath"]].get("master")

    for entry in entries:
        if not entry["patched"]:
            continue
        root_master = _root_master(entry)
        if not root_master:
            continue
        provenance = provenance_by_path[entry["assetPath"]]
        if provenance.get("master") == root_master:
            continue
        provenance["master"] = root_master
        entry["recipe"]["provenanceSha256"] = hashlib.sha256(_json_bytes(provenance)).hexdigest()
        _write_if_changed(sidecar_path_by_path[entry["assetPath"]], _json_bytes(provenance))

    keep: set[str] = set()
    for key in failed_keys:
        try:
            keep.add(asset_path_for(key))
            # R7.2: a failed unit protects its projector twin as well -- both instances are that
            # unit's, and a run that could not re-stage it must not prune the one already imported.
            keep.add(decal_asset_path_for(key))
            produced.update(path for path in _unit_files(root, key) if path.exists())
        except MaterialImportError:
            continue
    named = {entry["assetPath"] for entry in entries}
    keep -= named
    result.protected = len(keep)
    keep.update(EFFECT_MATERIAL_CHILDREN)   # R7.3: authored beside the corpus, never pruned

    entries.sort(key=lambda entry: entry["assetPath"].lower())
    manifest = {
        "schemaVersion": MANIFEST_SCHEMA,
        "settingsVersion": SETTINGS_VERSION,
        "packageRoot": PACKAGE_ROOT,
        "masterRoot": MASTER_ROOT,
        "select": normalize_select(select),
        "pruneScope": prune_scope_for(select),
        "keep": sorted(keep),
        "stageFailures": [{"unit": key, "reason": reason} for key, reason in result.failures],
        "provenanceOnly": sorted(set(provenance_only_keys)),
        "surfaceClasses": list(SURFACE_CLASSES),
        # Review finding 5: the same rollup `StageResult.summary()` prints, carried into the
        # manifest so the editor phase can fold it into `import_report.json` without re-parsing
        # every provenance sidecar a second time.
        "anomalyCounts": dict(sorted(result.anomaly_counts.items())),
        "omissionCounts": dict(sorted(result.omission_counts.items())),
        "assets": entries,
    }
    root.mkdir(parents=True, exist_ok=True)
    manifest_path = root / MANIFEST_NAME
    _write_if_changed(manifest_path, _json_bytes(manifest))
    produced.add(manifest_path)
    result.manifest_path = manifest_path
    result.assets = len(entries)
    result.pruned = _prune_stale(root, produced, select)
    return result
