"""Semantic records for the isolated Surface-property GLB exporter."""

from __future__ import annotations

from dataclasses import asdict, dataclass, field, is_dataclass
from pathlib import PurePosixPath
from typing import Any


SURFACE_PROPERTY_EXTENSION = "ELYSIUM_vtmb_surface_property"
SCHEMA_VERSION = "1.0.0"

#: The one table every surface-property unit is cut from.
TABLE_PATH = "scripts/surfaceproperties.txt"


@dataclass(frozen=True, slots=True)
class SourceIdentity:
    """One entry's own bytes, and the table they were cut from."""

    role: str
    path: str
    origin: dict[str, Any]
    byte_length: int
    sha256: str
    table_offset: int


@dataclass(frozen=True, slots=True)
class Parameter:
    """One `key value` pair exactly as the entry declares it, in source order.

    `offset` is relative to the entry's first byte, which is what the unit's byte ledger is
    written against; the entry's own offset within the table lives in its source identity.
    """

    index: int
    key: str
    source_key: str
    value: str
    quoted_key: bool
    quoted_value: bool
    offset: int


@dataclass(slots=True)
class SurfacePropertyModel:
    name: str
    source_name: str
    asset_id: str
    sources: list[SourceIdentity]
    base: dict[str, Any] | None
    physics: dict[str, Any]
    movement: dict[str, Any]
    footsteps: dict[str, Any]
    impacts: dict[str, Any]
    sounds: dict[str, Any]
    game_material: str | None
    parameters: list[Parameter]
    dependencies: list[dict[str, Any]]
    comments: list[dict[str, Any]]
    anomalies: list[dict[str, Any]]
    omissions: list[dict[str, Any]]
    unresolved: list[dict[str, Any]] = field(default_factory=list)
    unsupported: list[dict[str, Any]] = field(default_factory=list)
    byte_coverage: list[dict[str, Any]] = field(default_factory=list)


def normalize_name(name: str) -> str:
    """The unit key: the entry name folded to lower case.

    The table spells names in mixed case (`Kitchen_Pan` beside `metalvent`) and both producers
    that name into it spell them freely, so the identity a consumer can join on is the folded one.
    """

    normalized = name.strip().strip('"').lower()
    if not normalized or "/" in normalized or "\\" in normalized:
        raise ValueError(f"invalid surface-property name {name!r}")
    return normalized


def asset_id(name: str) -> str:
    return "vtmb:surface-property:" + normalize_name(name)


def sound_asset_id(path: str) -> str:
    """The stable ID for a `.wav` a footstep or impact key names directly."""

    normalized = path.replace("\\", "/").strip().strip('"').lower()
    while "//" in normalized:
        normalized = normalized.replace("//", "/")
    return "vtmb:sound:" + normalized.strip("/")


def sound_script_asset_id(name: str) -> str:
    """The stable ID for a sound-script name the physics `impact`/`scrape` keys reference."""

    return "vtmb:sound-script:" + name.strip().strip('"').lower()


def output_relative_path(name: str) -> PurePosixPath:
    return PurePosixPath(normalize_name(name) + ".glb")


def plain(value: Any) -> Any:
    if is_dataclass(value):
        return plain(asdict(value))
    if isinstance(value, dict):
        return {str(key): plain(item) for key, item in value.items()}
    if isinstance(value, (list, tuple)):
        return [plain(item) for item in value]
    return value
