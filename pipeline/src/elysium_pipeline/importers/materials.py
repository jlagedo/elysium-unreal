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

#: The 63 `scripts/surfaceproperties.txt` entry names this corpus's `$surfaceprop` values resolve
#: against (`uv run elysium import surface-properties`, 2026-08-31 run), `default` first. Every
#: name in this tuple gets a real `PM_<name>` physical-material asset (SF-2); everything else in
#: `SURFACE_CLASSES` below falls back to `PM_default` (`physMaterialFallback`).
_SURFACEPROP_NAMES = (
    "default", "armorflesh", "bottle", "boulder", "brick", "can_pop", "can_pop_crushed",
    "canister", "cardboard", "carpet", "computer", "concrete", "default_silent", "dirt",
    "fish_fresh", "fish_frozen", "flesh", "gargoyle", "glass", "glass_shard", "glassbottle",
    "grass", "gravel", "grenade", "gunship", "ice", "kitchen_pan", "kitchen_pot",
    "kitchen_utensils", "ladder", "metal", "metal_barrel", "metalgrate", "metalpanel",
    "metalvent", "ming_xiao", "ming_xiao_tentacle", "mud", "paper", "papercup", "plaster",
    "plastic", "player", "player_control_clip", "popcan", "quiet", "ring", "rivet", "rock",
    "roller", "rubber", "sand", "snow", "stone", "strider", "tile", "tin", "wade", "water",
    "watermelon", "weapon", "wood", "woodpanel",
)
#: VMT top-directory names the design's tier-2 fallback names (`seam_map_material.md` -> "Identity
#: and naming") that are not already a `$surfaceprop` entry name.
_TOP_DIRECTORY_ONLY = ("asphalt", "blends", "cable", "drapery", "grates", "ground")
#: The four `$surfaceprop` values the 63-entry table does not define, each getting a class row of
#: its own; `asphalt` is already counted via `_TOP_DIRECTORY_ONLY` above, so only three are new
#: here -- the revision's own accounting (`seam_migration.md` -> "Revised after review").
_TIER1_ADDITIONS = ("bone", "cloth", "leather")
#: The ordered class table: `default` at index 0 (the class LUT's row 0), everything else sorted so
#: the table is stable across regeneration. 72 rows: 63 (`_SURFACEPROP_NAMES`, `default` included)
#: + 6 (`_TOP_DIRECTORY_ONLY`) + 3 (`_TIER1_ADDITIONS`). SF-4.1's `UElysiumSurfaceCalibration` seeds
#: its rows from this same list, so the index this lane writes as `SurfaceClassIndex` is the row
#: that class asset defines.
SURFACE_CLASSES: tuple[str, ...] = ("default",) + tuple(
    sorted(set(_SURFACEPROP_NAMES[1:]) | set(_TOP_DIRECTORY_ONLY) | set(_TIER1_ADDITIONS))
)
SURFACE_CLASS_INDEX = {name: index for index, name in enumerate(SURFACE_CLASSES)}
#: The curated 16-name top-directory allowlist (tier 2), stated verbatim rather than derived, per
#: "Identity and naming": `plaster`, `wood`, `stone`, `blends`, `brick`, `ground`, `metal`, `tile`,
#: `drapery`, `carpet`, `cable`, `glass`, `grates`, `asphalt`, `water`, `grass`.
TOP_DIRECTORY_CLASSES: frozenset[str] = frozenset({
    "plaster", "wood", "stone", "blends", "brick", "ground", "metal", "tile", "drapery",
    "carpet", "cable", "glass", "grates", "asphalt", "water", "grass",
})
#: Tier-3 per-family default class row, keyed by the resolved master.
FAMILY_DEFAULT_CLASS: dict[str, str] = {
    "M_V2_Lit": "default", "M_V2_LitTranslucent": "default", "M_V2_Unlit": "default",
    "M_V2_Eyes": "flesh", "M_V2_Water": "water", "M_V2_Sprite": "default",
    "M_V2_Decal": "default", "M_V2_TwoTexture": "default", "M_V2_Refract": "glass",
}


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
_BASE_SCROLL_LANE = {"BaseScrollRateU": "S", "BaseScrollRateV": "S"}
_BUMP_SCROLL_LANE = {"BumpScrollRateU": "S", "BumpScrollRateV": "S"}
_BASE_ANIM_LANE = {
    "FrameRate": "S", "FrameCount": "S", "BaseTextureFrames": "T", "UseAnimatedFrames": "#",
}
_NORMAL_ANIM_LANE = {
    "NormalFrameRate": "S", "NormalFrameCount": "S", "NormalMapFrames": "T",
    "UseAnimatedNormalFrames": "#",
}


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
        _SINE_LANE,
        {
            "BaseTexture": "T", "NormalMap": "T", "EnvMapMask": "T", "EnvMap": "T",
            "SelfIllumAmount": "S", "EnvMapMaskScale": "S", "BumpScale": "S",
            "SelfIllumTint": "V", "EnvMapTint": "V", "TexScaleOffset": "V",
            "UseBaseTexture": "#", "UseNormalMap": "#", "UseSelfIllum": "#", "UseVertexColor": "#",
            "UseVertexAlpha": "#", "UseEnvMap": "#", "UseEnvMapMask": "#",
            "UseBaseAlphaEnvMapMask": "#", "UseNormalMapAlphaEnvMapMask": "#", "UseFixedCube": "#",
            "MetallicTint": "#",
        },
    ),
    "M_V2_Unlit": _merged(
        _SHARED_PARAMS, _BASE_SCROLL_LANE, _BASE_ANIM_LANE, _SINE_LANE,
        {
            "BaseTexture": "T", "EnvMapMask": "T", "EnvMap": "T", "CloudAlphaTexture": "T",
            "EnvMapMaskScale": "S",
            "EnvMapTint": "V", "TexScaleOffset": "V", "CloudScale": "V",
            "UseBaseTexture": "#", "UseVertexColor": "#", "UseVertexAlpha": "#", "UseEnvMap": "#",
            "UseEnvMapMask": "#", "UseBaseAlphaEnvMapMask": "#", "UseFixedCube": "#",
            "MetallicTint": "#", "UseCloudAlpha": "#",
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
        },
    ),
    "M_V2_Sprite": _merged(_SHARED_PARAMS, _BASE_ANIM_LANE, {
        "BaseTexture": "T", "UseVertexColor": "#",
    }),
    "M_V2_Refract": _merged(_SHARED_PARAMS, {
        "BaseTexture": "T", "DuDvMap": "T", "NormalMap": "T", "EnvMap": "T",
        "RefractAmount": "S", "RefractTint": "V", "EnvMapTint": "V",
        "UseBaseTexture": "#", "UseNormalMap": "#", "UseEnvMap": "#", "UseFixedCube": "#",
    }),
    "M_V2_Decal": _merged(_SHARED_PARAMS, {"BaseTexture": "T", "UseVertexColor": "#"}),
    "M_V2_TwoTexture": _merged(
        _SHARED_PARAMS, _BASE_SCROLL_LANE, _SINE_LANE,
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
    "$bumpframe", "$detailscale", "$detailscale2", "$contrast", "$wave",
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
    "$multipass", "$nooverbright", "$no_fullbright", "$nofog", "$polyoffset", "$trilinear",
    "$nomip", "nomip", "$noclip", "$noztest", "$comparez", "$writez", "$decalscale",
    "$animatedtexturevar", "$animatedtextureframenumvar", "$animatedtextureframerate",
    "$spriteorientation",
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


def _base_key_from_asset_id(asset_id: str) -> str:
    if not isinstance(asset_id, str) or not asset_id.startswith("vtmb:material:"):
        raise MaterialImportError(f"not a vtmb:material: asset id: {asset_id!r}")
    return asset_id[len("vtmb:material:"):]


_PATCH_COORD_RE = re.compile(r"_(-?\d+)_(-?\d+)_(-?\d+)$")


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
    failures: list[tuple[str, str]] = field(default_factory=list)
    protected: int = 0

    def summary(self) -> str:
        return (
            f"material staging: {self.assets} instances ({self.patched} patched, "
            f"{self.provenance_only} provenance-only) from {self.staged} units, "
            f"{self.pruned} pruned, {len(self.failures)} failed "
            f"({self.protected} asset paths protected) -> {self.staging_root}"
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


def _bind_texture(
    params: _Params, key: str, param_name: str, document: dict, deps: dict[str, tuple[str, bool]],
    *, cube: bool, texture_staging_root: Path | None,
) -> None:
    resolution = deps.get(key)
    if resolution is None or not resolution[1]:
        return  # unresolved: recorded in provenance's textureBindings, never bound to a parameter
    asset_id, _resolved = resolution
    texture_key = asset_id[len("vtmb:texture:"):] if asset_id.startswith("vtmb:texture:") else asset_id
    params.texture_deps[param_name] = asset_id
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
    if _truthy(flags.get("$nocull", "0")):
        overrides["twoSided"] = True
    return overrides


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


def _apply_proxies(params: _Params, document: dict, parameters: list[dict]) -> list[dict]:
    """Every proxy's provenance row, plus the direct scalar/vector side effects this lane places."""

    rows: list[dict] = []
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
            for source, target in (("sinemin", "SineMin"), ("sinemax", "SineMax"),
                                   ("sineperiod", "SinePeriod"), ("timeoffset", "SineTimeOffset")):
                if source in args:
                    try:
                        params.scalars[target] = _parse_scalar(args[source])
                    except ValueError:
                        pass
            raw_result = args.get("resultvar", "")
            folded = raw_result.strip().lower()
            match = _VECTOR_COMPONENT.match(folded)
            base, component = (match.group(1), int(match.group(2))) if match else (folded, None)
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
            switch_name = "UseAnimatedNormalFrames" if normal_lane else "UseAnimatedFrames"
            if "animatedtextureframerate" in args:
                try:
                    params.scalars[rate_name] = _parse_scalar(args["animatedtextureframerate"])
                except ValueError:
                    pass
            params.switches[switch_name] = True
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
            # WetnessScale left the masters: it is provenance only, read by
            # `FElysiumMaterialFactory` at runtime, never sampled in the graph.
            try:
                params.misc_provenance["wetnessScale"] = _parse_scalar(args["scale"])
            except ValueError:
                pass

        rows.append({"index": len(rows), "kind": kind, "sourceName": proxy.get("sourceName"),
                     "arguments": args, "destination": destination})
    return rows


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
        })
        if patched and key_lower != PATCH_ONLY_KEY and not block.startswith(("replace#", "insert#")):
            continue  # a patched unit's non-override top-level rows are patch bookkeeping only
        unmapped = _classify_and_apply(
            params, key_lower, raw_key, value, block, document, deps,
            family=family, texture_staging_root=texture_staging_root,
        )
        if unmapped is not None:
            raise MaterialImportError(f"unmapped parameter key {unmapped!r}")

    proxy_rows = _apply_proxies(params, document, parameters)
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
        surface_class, surface_class_index, class_source = _resolve_surface_class(
            params.surfaceprop_value, directories, master)
        physmat_fallback = surface_class not in _SURFACEPROP_NAMES
        params.scalars["SurfaceClassIndex"] = surface_class_index
        _validate_exposed(params, master)

    entry = {
        "assetPath": asset_path_for(key),
        "unit": asset_id,
        "unitGlb": f"{FAMILY}/{key}.glb",
        "unitSha256": unit_sha256,
        "parent": parent,
        "patched": patched,
        "provenanceOnly": provenance_only,
        "textures": dict(sorted(params.textures.items())),
        "scalars": dict(sorted(params.scalars.items())),
        "vectors": {k: v for k, v in sorted(params.vectors.items())},
        "switches": dict(sorted(params.switches.items())),
        "basePropertyOverrides": base_property_overrides,
        "physMaterial": (f"/ElysiumBaked/SurfaceProperties/PM_default"
                         if (not patched and physmat_fallback) else
                         (f"/ElysiumBaked/SurfaceProperties/PM_{safe_name(surface_class)}"
                          if not patched else None)),
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
            "params": {
                "textures": dict(sorted(params.textures.items())),
                "scalars": dict(sorted(params.scalars.items())),
                "vectors": {k: v for k, v in sorted(params.vectors.items())},
                "switches": dict(sorted(params.switches.items())),
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
            "unmappedKeys": [],
        },
    }
    return entry, provenance


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
        entries.append(entry)

        directories, stem = _check_key(key)
        sidecar_path = root.joinpath(*directories, stem + PROVENANCE_SUFFIX)
        try:
            _write_if_changed(sidecar_path, _json_bytes(provenance))
        except OSError as error:
            failed(key, str(error))
            entries.pop()
            continue
        produced.add(sidecar_path)

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

    keep: set[str] = set()
    for key in failed_keys:
        try:
            keep.add(asset_path_for(key))
            produced.update(path for path in _unit_files(root, key) if path.exists())
        except MaterialImportError:
            continue
    named = {entry["assetPath"] for entry in entries}
    keep -= named
    result.protected = len(keep)

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
