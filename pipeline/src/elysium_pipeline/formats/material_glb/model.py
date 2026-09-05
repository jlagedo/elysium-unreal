"""Semantic records for the isolated Material GLB exporter."""

from __future__ import annotations

from dataclasses import asdict, dataclass, field, is_dataclass
from pathlib import PurePosixPath
from typing import Any


MATERIAL_EXTENSION = "ELYSIUM_vtmb_material"
SCHEMA_VERSION = "1.2.0"   # 1.2.0: materialReferences[]


@dataclass(frozen=True, slots=True)
class SourceIdentity:
    role: str
    path: str
    origin: dict[str, Any]
    byte_length: int
    sha256: str


@dataclass(frozen=True, slots=True)
class Parameter:
    """One key/value pair exactly as the source block declares it, in source order."""

    index: int
    block: str
    key: str
    source_key: str
    value: str
    value_type: str
    quoted_key: bool
    quoted_value: bool
    offset: int


@dataclass(frozen=True, slots=True)
class ProxyRecord:
    index: int
    name: str
    source_name: str
    parameters: tuple[Parameter, ...]


@dataclass(slots=True)
class MaterialModel:
    material_path: str
    asset_id: str
    sources: list[SourceIdentity]
    shader: str
    source_shader: str
    parameters: list[Parameter]
    blocks: list[dict[str, Any]]
    proxies: list[ProxyRecord]
    texture_bindings: list[dict[str, Any]]
    material_references: list[dict[str, Any]]
    patch: dict[str, Any] | None
    patch_of: dict[str, Any] | None
    surface_property: str | None
    environment: dict[str, Any] | None
    shader_resolution: dict[str, Any]
    dependencies: list[dict[str, Any]]
    comments: list[dict[str, Any]]
    anomalies: list[dict[str, Any]]
    omissions: list[dict[str, Any]]
    unresolved: list[dict[str, Any]] = field(default_factory=list)
    unsupported: list[dict[str, Any]] = field(default_factory=list)
    byte_coverage: list[dict[str, Any]] = field(default_factory=list)


def normalize_material_path(path: str) -> str:
    """The unit key: the normalized path below `materials/`, lowercased, without `.vmt`."""

    normalized = path.replace("\\", "/").strip("/").lower()
    while "//" in normalized:
        normalized = normalized.replace("//", "/")
    if normalized.startswith("materials/"):
        normalized = normalized[len("materials/"):]
    if normalized.endswith(".vmt"):
        normalized = normalized[:-len(".vmt")]
    if not normalized or normalized.startswith("../") or "/../" in normalized:
        raise ValueError(f"invalid material path {path!r}")
    return normalized


def asset_id(path: str) -> str:
    return "vtmb:material:" + normalize_material_path(path)


def texture_asset_id(path: str) -> str:
    """The texture seam's stable ID for a VMT texture value."""

    normalized = path.replace("\\", "/").strip("/").lower()
    while "//" in normalized:
        normalized = normalized.replace("//", "/")
    if normalized.startswith("materials/"):
        normalized = normalized[len("materials/"):]
    for suffix in (".tth", ".ttz", ".vtf"):
        if normalized.endswith(suffix):
            normalized = normalized[:-len(suffix)]
            break
    return "vtmb:texture:" + normalized


def material_reference_asset_id(path: str) -> str:
    """The stable ID for a VMT value that names another *material*.

    The value is authored the way `$basetexture` is -- backslashes, mixed case, sometimes a
    `.vmt` -- so it is normalized through the same rule the unit key uses, then given the
    material namespace instead of the texture one.
    """

    normalized = path.replace("\\", "/").strip("/").lower()
    while "//" in normalized:
        normalized = normalized.replace("//", "/")
    if normalized.startswith("materials/"):
        normalized = normalized[len("materials/"):]
    if normalized.endswith(".vmt"):
        normalized = normalized[:-len(".vmt")]
    return "vtmb:material:" + normalized


def surface_property_asset_id(name: str) -> str:
    return "vtmb:surface-property:" + name.strip().strip('"').lower()


def output_relative_path(path: str) -> PurePosixPath:
    return PurePosixPath(normalize_material_path(path) + ".glb")


def plain(value: Any) -> Any:
    if is_dataclass(value):
        return plain(asdict(value))
    if isinstance(value, dict):
        return {str(key): plain(item) for key, item in value.items()}
    if isinstance(value, (list, tuple)):
        return [plain(item) for item in value]
    return value
