"""Semantic values and stable identities for the Map GLB seam.

The map root is one of four units cut from one BSP member: the root itself and the entities,
lighting and visibility sub-units. This module owns the identities all four are named by, so a
sub-unit seam can name its sibling without importing the root's decoder.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import PurePosixPath
import re
from typing import Any

from elysium_pipeline.formats.unit_contract import asset_id as _asset_id
from elysium_pipeline.formats.unit_contract import coverage as _coverage
from elysium_pipeline.formats.unit_contract import missing_sentinel as _missing_sentinel

#: The kind this seam publishes, and the extension it declares.
KIND = "map"
KIND_TITLE = "Map"
MAP_EXTENSION = _coverage.extension_name(KIND)
SCHEMA_VERSION = "1.1.0"   # 1.1.0: the dface+96 split, primitives reachable from a face

#: The install directory every map member is resolved below, and the family directory the unit
#: is written into.
MAPS_ROOT = "maps"
FAMILY_DIR = "maps"

#: The three sub-units the root partitions the BSP with, in publication order, and the suffix
#: each one's file carries beside the root's.
SUB_UNITS = (
    ("map-entities", "entities"),
    ("map-lighting", "lighting"),
    ("map-visibility", "visibility"),
)

#: `Source inches, Z-up, right-handed` -> `glTF metres, Y-up, right-handed`.
INCH_TO_METRE = 0.0254

_KEY = re.compile(r"^[a-z0-9_\-.]+$")

#: A cubemap-patched material name: `maps/<map>/<base>_<x>_<y>_<z>`.
PATCHED_NAME = re.compile(r"^maps/(?P<map>[^/]+)/(?P<base>.+)_(?P<x>-?\d+)_(?P<y>-?\d+)_(?P<z>-?\d+)$")


class MapKeyError(ValueError):
    """A map key is not a name below `maps/` this seam can publish."""


def normalize_key(key: str) -> str:
    """The unit key: the `.bsp` file stem below `maps/`, folded to lower case.

    The singular command tolerates the root prefix and the source extension, so
    `maps/sp_tutorial_1.bsp`, `sp_tutorial_1.bsp` and `sp_tutorial_1` are one key.
    """

    normalized = str(key).replace("\\", "/").strip().strip("/").lower()
    if normalized.startswith(MAPS_ROOT + "/"):
        normalized = normalized[len(MAPS_ROOT) + 1:]
    if normalized.endswith(".bsp"):
        normalized = normalized[: -len(".bsp")]
    if not normalized or not _KEY.match(normalized):
        raise MapKeyError(f"invalid map key {key!r}")
    return normalized


def source_path(key: str) -> str:
    """The install-relative member the key resolves to."""

    return f"{MAPS_ROOT}/{normalize_key(key)}.bsp"


def asset_id(key: str) -> str:
    return _asset_id(KIND, normalize_key(key))


def sub_asset_id(kind: str, key: str) -> str:
    """The stable identity of one of the three sibling units."""

    if kind not in {name for name, _ in SUB_UNITS}:
        raise MapKeyError(f"{kind!r} is not a sub-unit of the map root")
    return _asset_id(kind, normalize_key(key))


def output_relative_path(key: str) -> PurePosixPath:
    return PurePosixPath(FAMILY_DIR) / (normalize_key(key) + ".glb")


def sub_output_relative_path(suffix: str, key: str) -> PurePosixPath:
    return PurePosixPath(FAMILY_DIR) / f"{normalize_key(key)}.{suffix}.glb"


def material_asset_id(name: str) -> str:
    """The material seam's identity for one TEXDATA name."""

    return _asset_id("material", str(name).replace("\\", "/").strip("/"))


def material_source_path(name: str) -> str:
    return "materials/" + str(name).replace("\\", "/").strip("/").lower() + ".vmt"


def texture_asset_id(name: str) -> str:
    """The texture seam's identity for one cubemap or PAKFILE texture member."""

    normalized = str(name).replace("\\", "/").strip("/").lower()
    if normalized.startswith("materials/"):
        normalized = normalized[len("materials/"):]
    for suffix in (".tth", ".ttz", ".vtf"):
        if normalized.endswith(suffix):
            normalized = normalized[: -len(suffix)]
            break
    return _asset_id("texture", normalized)


def model_key(path: str) -> str:
    """The model seam's key for one static- or detail-prop dictionary entry.

    Both prop kinds name the same models the same way, so both the resolved identity and the
    missing-model sentinel are built from this one spelling: one absent model has one identity.
    """

    normalized = str(path).replace("\\", "/").strip("/").lower()
    if normalized.startswith("models/"):
        normalized = normalized[len("models/"):]
    if normalized.endswith(".mdl"):
        normalized = normalized[: -len(".mdl")]
    return normalized


def model_asset_id(path: str) -> str:
    """The model seam's identity for one static- or detail-prop dictionary entry."""

    return _asset_id("model", model_key(path))


def missing_model_asset_id(path: str) -> str:
    """The sentinel a prop keeps when the install holds no member for its dictionary entry."""

    return _missing_sentinel("model", model_key(path))


def surface_property_asset_id(name: str) -> str:
    return _asset_id("surface-property", str(name).strip().strip('"'))


def split_patched_name(name: str) -> tuple[str, tuple[int, int, int]] | None:
    """`maps/<map>/<base>_<x>_<y>_<z>` -> (`<base>`, cubemap origin), or None.

    The compiler writes one patched copy of a material per cubemap it is lit by; the patched name
    and its base are distinct units, and the root states the join rather than the substitution.
    """

    match = PATCHED_NAME.match(str(name).replace("\\", "/").strip("/").lower())
    if not match:
        return None
    return match["base"], (int(match["x"]), int(match["y"]), int(match["z"]))


@dataclass(slots=True)
class MapModel:
    """Everything one map root publishes, decoded once and written twice (core and extension)."""

    key: str
    asset_id: str
    source_path: str
    source: Any                              # the SourceMember the unit was decoded from
    partition: Any                           # partition.Partition
    header: dict[str, Any]
    trailer: dict[str, Any] | None
    sub_units: list[dict[str, Any]]
    planes: list[dict[str, Any]]
    textures: list[dict[str, Any]]
    texture_strings: dict[str, Any]
    texinfos: list[dict[str, Any]]
    faces: list[dict[str, Any]]
    original_faces: list[dict[str, Any]]
    edges: dict[str, Any]
    surf_edges: dict[str, Any]
    normal_indices: dict[str, Any]           # lump 31, the table faces[].normalIndexStart cuts
    displacement_triangle_tags: dict[str, Any]   # lump 48, cut by displacements[].dispTriStart
    models: list[dict[str, Any]]
    bsp: dict[str, Any]
    collision: dict[str, Any]
    displacements: list[dict[str, Any]]
    primitives: dict[str, Any]
    physics: dict[str, Any]
    water: dict[str, Any]
    cubemaps: list[dict[str, Any]]
    occluders: dict[str, Any]
    static_props: dict[str, Any]
    detail_props: dict[str, Any]
    pakfile: dict[str, Any]
    scenes: dict[str, Any]                   # the built core glTF arrays and the BIN payload
    binary: bytes
    dependencies: list[dict[str, Any]]
    anomalies: list[dict[str, Any]]
    omissions: list[dict[str, Any]]
    census: dict[str, Any]
    claims: list[tuple[int, int, str, str]] = field(default_factory=list)
    mapped: list[str] = field(default_factory=list)
    typed_unidentified: list[dict[str, Any]] = field(default_factory=list)
    omitted_proven: list[dict[str, Any]] = field(default_factory=list)
    unresolved: list[dict[str, Any]] = field(default_factory=list)
    unsupported: list[dict[str, Any]] = field(default_factory=list)
