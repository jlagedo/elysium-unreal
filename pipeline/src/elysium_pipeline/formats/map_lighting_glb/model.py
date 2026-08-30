"""Semantic values and stable identities for the Map-lighting GLB seam.

The lighting unit is one of the three sub-units the map root cuts from one BSP member: lumps 8,
15, 32 and 34 plus the `dplt` game-lump payload. Its identities are built from the root's key
rule, so `vtmb:map:sp_tutorial_1` and `vtmb:map-lighting:sp_tutorial_1` name the same map.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import PurePosixPath
from typing import Any

from elysium_pipeline.formats.map_glb import model as map_model
from elysium_pipeline.formats.unit_contract import asset_id as _asset_id
from elysium_pipeline.formats.unit_contract import coverage as _coverage

#: The kind this seam publishes, the title its generator states, and the extension it declares.
KIND = "map-lighting"
KIND_TITLE = "Map-lighting"
MAP_LIGHTING_EXTENSION = _coverage.extension_name(KIND)
SCHEMA_VERSION = "1.0.0"

#: The family directory and the suffix that tells one map's four units apart.
FAMILY_DIR = map_model.FAMILY_DIR
UNIT_SUFFIX = "lighting"

#: The lumps this unit owns, and the game-lump payload it owns inside lump 35.
LIGHTING_LUMP = 8
WORLDLIGHTS_LUMP = 15
DISP_ALPHA_LUMP = 32
DISP_SAMPLE_LUMP = 34
GAME_LUMP = 35
DETAIL_PROP_LIGHTING_ID = "dplt"
OWNED_LUMPS = (LIGHTING_LUMP, WORLDLIGHTS_LUMP, DISP_ALPHA_LUMP, DISP_SAMPLE_LUMP)

#: The root's tables this unit restates as `derived` rows and never owns a byte of.
FACE_LUMP = 7
TEXINFO_LUMP = 6
DISPINFO_LUMP = 26

FACE_BYTES = 104
TEXINFO_BYTES = 72
DISPINFO_BYTES = 176
WORLD_LIGHT_BYTES = 88
DETAIL_PROP_LIGHT_BYTES = 5

#: One `ColorRGBExp32` luxel: three `uint8` mantissas and one `int8` shared exponent.
LUXEL_BYTES = 4
#: `texinfo.flags & SURF_BUMPLIGHT` multiplies a face's samples by the bump basis.
SURF_BUMPLIGHT = 0x800
BUMP_LIGHTMAP_SETS = 4
#: A style slot the compiler left unused.
UNUSED_STYLE = 0xFF

#: What the unit represents, and the semantic state each field carries. A field read straight out
#: of a span this unit owns is `mapped`; one restated from a lump the map root owns, or counted
#: from rows rather than read, is `derived`.
MAPPED_FIELDS = (
    {"field": "map", "state": "mapped"},
    {"field": "samples", "state": "mapped"},
    {"field": "faces", "state": "derived"},
    {"field": "faces[].bumped", "state": "derived"},
    {"field": "styleCensus", "state": "derived"},
    {"field": "worldLights", "state": "mapped"},
    {"field": "lightTypeCensus", "state": "derived"},
    {"field": "dispAlphas", "state": "mapped"},
    {"field": "dispSamplePositions", "state": "mapped"},
    {"field": "displacements", "state": "derived"},
    {"field": "detailPropLighting", "state": "mapped"},
)

#: The role each owned member fills in `sourceResolution`.
MEMBER_ROLES = {
    LIGHTING_LUMP: "lighting",
    WORLDLIGHTS_LUMP: "worldlights",
    DISP_ALPHA_LUMP: "disp-lightmap-alphas",
    DISP_SAMPLE_LUMP: "disp-lightmap-sample-positions",
}
DETAIL_PROP_LIGHTING_ROLE = "detail-prop-lighting"

#: `dworldlight_t.type`, as `docs/vtmb/lighting.md` names the values.
LIGHT_TYPE_NAMES = {
    0: "emit_surface",
    1: "point",
    2: "spot",
    3: "skylight",
    4: "quakelight",
    5: "skyambient",
}

#: `Source inches, Z-up, right-handed` -> `glTF metres, Y-up, right-handed`.
INCH_TO_METRE = map_model.INCH_TO_METRE

MapKeyError = map_model.MapKeyError


def normalize_key(key: str) -> str:
    """The unit key: the map stem, folded, tolerating the root prefix and the `.bsp` suffix."""

    return map_model.normalize_key(key)


def source_path(key: str) -> str:
    """The install-relative member the four units of one map are cut from."""

    return map_model.source_path(key)


def member_path(key: str, lump: int) -> str:
    """The `sourceResolution` path of one owned lump.

    One file yields several members here and a member table is keyed by path, so each span names
    the lump it was cut from beside the file it belongs to.
    """

    return f"{source_path(key)}#lump{int(lump)}"


def game_lump_member_path(key: str, identifier: str = DETAIL_PROP_LIGHTING_ID) -> str:
    """The `sourceResolution` path of the game-lump payload this unit owns."""

    return f"{source_path(key)}#lump{GAME_LUMP}.{identifier}"


def asset_id(key: str) -> str:
    return _asset_id(KIND, normalize_key(key))


def map_asset_id(key: str) -> str:
    """The root unit, whose face, texinfo and dispinfo tables the `derived` rows restate."""

    return map_model.asset_id(key)


def map_entities_asset_id(key: str) -> str:
    """The entity unit, holding the `light*` entities the world lights were compiled from."""

    return map_model.sub_asset_id("map-entities", key)


def output_relative_path(key: str) -> PurePosixPath:
    return map_model.sub_output_relative_path(UNIT_SUFFIX, key)


def light_type_name(value: int) -> str | None:
    return LIGHT_TYPE_NAMES.get(int(value))


def to_gltf_position(point) -> tuple[float, float, float]:
    """`(x, y, z)_gltf = (x, z, -y)_source * 0.0254`."""

    x, y, z = (float(value) for value in point)
    return (x * INCH_TO_METRE, z * INCH_TO_METRE, -y * INCH_TO_METRE)


def to_gltf_direction(vector) -> tuple[float, float, float]:
    """`(x, y, z)_gltf = (x, z, -y)_source`, unscaled."""

    x, y, z = (float(value) for value in vector)
    return (x, z, -y)


@dataclass(frozen=True, slots=True)
class SampleSpan:
    """One `(set, style)` block of one face's samples, located in the `samples` accessor."""

    set_index: int
    style_index: int
    style: int
    offset: int
    length: int

    def to_json(self) -> dict[str, Any]:
        return {
            "set": self.set_index,
            "styleIndex": self.style_index,
            "style": self.style,
            "offset": self.offset,
            "length": self.length,
        }

    def owner(self, face: int) -> str:
        return f"lighting.faces[{face}].set[{self.set_index}].style[{self.style_index}]"


@dataclass(frozen=True, slots=True)
class FaceLighting:
    """One root face's lightmap placement, `derived` from lumps 7 and 6."""

    index: int
    source_offset: int
    tex_info: int
    flags: int
    bumped: bool
    light_offset: int
    lightmap_mins: tuple[int, int]
    lightmap_size: tuple[int, int]
    luxel_width: int
    luxel_height: int
    styles: tuple[int, ...]
    style_count: int
    lightmap_sets: int
    avg_light_color: tuple[tuple[int, int, int, int], ...]
    byte_length: int
    spans: tuple[SampleSpan, ...]

    def to_json(self) -> dict[str, Any]:
        return {
            "index": self.index,
            "sourceOffset": self.source_offset,
            "texInfo": self.tex_info,
            "flags": self.flags,
            "bumped": self.bumped,
            "lightOffset": self.light_offset,
            "lightmapMins": list(self.lightmap_mins),
            "lightmapSize": list(self.lightmap_size),
            "luxelWidth": self.luxel_width,
            "luxelHeight": self.luxel_height,
            "styles": list(self.styles),
            "styleCount": self.style_count,
            "lightmapSets": self.lightmap_sets,
            "avgLightColor": [list(sample) for sample in self.avg_light_color],
            "byteLength": self.byte_length,
            "spans": [span.to_json() for span in self.spans],
        }


@dataclass(frozen=True, slots=True)
class WorldLight:
    """One 88-byte `dworldlight_t`, every field restated."""

    index: int
    source_offset: int
    origin: tuple[float, float, float]
    intensity: tuple[float, float, float]
    normal: tuple[float, float, float]
    cluster: int
    type: int
    style: int
    stopdot: float
    stopdot2: float
    exponent: float
    radius: float
    constant_attn: float
    linear_attn: float
    quadratic_attn: float
    flags: int
    texinfo: int
    owner: int

    def to_json(self) -> dict[str, Any]:
        return {
            "index": self.index,
            "sourceOffset": self.source_offset,
            "origin": {
                "source": list(self.origin),
                "gltf": list(to_gltf_position(self.origin)),
            },
            "intensity": list(self.intensity),
            "normal": {
                "source": list(self.normal),
                "gltf": list(to_gltf_direction(self.normal)),
            },
            "cluster": self.cluster,
            "type": self.type,
            "typeName": light_type_name(self.type),
            "style": self.style,
            "stopdot": self.stopdot,
            "stopdot2": self.stopdot2,
            "exponent": self.exponent,
            "radius": {"source": self.radius, "metres": self.radius * INCH_TO_METRE},
            "constantAttn": self.constant_attn,
            "linearAttn": self.linear_attn,
            "quadraticAttn": self.quadratic_attn,
            "flags": self.flags,
            "texinfo": self.texinfo,
            "owner": self.owner,
        }


@dataclass(frozen=True, slots=True)
class DisplacementLighting:
    """One dispinfo's runs into lumps 32 and 34, `derived` from the root's lump 26."""

    index: int
    source_offset: int
    power: int
    map_face: int
    alpha_start: int
    alpha_length: int
    sample_position_start: int
    sample_position_length: int

    def to_json(self) -> dict[str, Any]:
        return {
            "index": self.index,
            "sourceOffset": self.source_offset,
            "power": self.power,
            "mapFace": self.map_face,
            "alphaStart": self.alpha_start,
            "alphaLength": self.alpha_length,
            "samplePositionStart": self.sample_position_start,
            "samplePositionLength": self.sample_position_length,
        }


@dataclass(frozen=True, slots=True)
class DetailPropLight:
    """One five-byte `dplt` record: `ColorRGBExp32` plus a `uint8` style."""

    index: int
    source_offset: int
    r: int
    g: int
    b: int
    exponent: int
    style: int

    def to_json(self) -> dict[str, Any]:
        return {
            "index": self.index,
            "sourceOffset": self.source_offset,
            "r": self.r,
            "g": self.g,
            "b": self.b,
            "exponent": self.exponent,
            "style": self.style,
        }


@dataclass(slots=True)
class MapLightingModel:
    """Everything one lighting unit publishes, decoded once."""

    key: str
    asset_id: str
    source_path: str
    source: Any                                  # the MapLightingSourceClosure it came from
    map: dict[str, Any]
    samples: dict[str, Any] | None
    faces: list[FaceLighting]
    style_census: list[dict[str, Any]]
    world_lights: list[WorldLight]
    light_type_census: list[dict[str, Any]]
    disp_alphas: dict[str, Any] | None
    disp_sample_positions: dict[str, Any] | None
    displacements: list[DisplacementLighting]
    detail_prop_lighting: list[DetailPropLight]
    binary: bytes
    buffer_views: list[dict[str, Any]]
    accessors: list[dict[str, Any]]
    dependencies: list[dict[str, Any]]
    anomalies: list[dict[str, Any]]
    omissions: list[dict[str, Any]]
    claims: dict[str, list[tuple[int, int, str, str]]] = field(default_factory=dict)
    mapped: list[str] = field(default_factory=list)
    typed_unidentified: list[dict[str, Any]] = field(default_factory=list)
    omitted_proven: list[dict[str, Any]] = field(default_factory=list)
    unresolved: list[dict[str, Any]] = field(default_factory=list)
    unsupported: list[dict[str, Any]] = field(default_factory=list)
