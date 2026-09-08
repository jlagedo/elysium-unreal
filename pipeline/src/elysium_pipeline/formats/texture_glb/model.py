"""Semantic records for the isolated Texture GLB exporter."""

from __future__ import annotations

from dataclasses import asdict, dataclass, is_dataclass
from pathlib import PurePosixPath
from typing import Any


TEXTURE_EXTENSION = "ELYSIUM_vtmb_texture"
SCHEMA_VERSION = "1.1.0"


@dataclass(frozen=True, slots=True)
class SourceIdentity:
    role: str
    path: str
    origin: dict[str, Any]
    byte_length: int
    sha256: str


@dataclass(frozen=True, slots=True)
class FormatInfo:
    source_enum: int
    source_name: str
    vk_format: int
    vk_name: str
    block_width: int
    block_height: int
    block_bytes: int
    channels: tuple[str, ...]
    compressed: bool


@dataclass(frozen=True, slots=True)
class TextureLevel:
    ktx_level: int
    source_mip: int
    width: int
    height: int
    images: tuple[bytes, ...]


@dataclass(slots=True)
class TextureModel:
    texture_path: str
    asset_id: str
    sources: list[SourceIdentity]
    format: FormatInfo
    width: int
    height: int
    declared_width: int
    declared_height: int
    frames: int
    cubemap: bool
    mip_count: int
    levels: list[TextureLevel]
    header: dict[str, Any]
    sampling: dict[str, bool]
    faces: list[dict[str, Any]]
    omissions: list[dict[str, Any]]
    byte_coverage: list[dict[str, Any]]


#: R7.3: the one install directory whose raw `.tga` members are admitted as texture units beside
#: the `materials/**.tth` corpus -- the 318 particle sprites. The key keeps the directory
#: (`particles/<stem>`), so the asset
#: lands under `/ElysiumBaked/Textures/particles/`.
SPRITE_FAMILY = "particles"
SPRITE_SUFFIX = ".tga"


def is_sprite_key(normalized: str) -> bool:
    return normalized.startswith(SPRITE_FAMILY + "/")


def normalize_texture_path(path: str) -> str:
    normalized = path.replace("\\", "/").strip("/").lower()
    if normalized.startswith("materials/"):
        normalized = normalized[len("materials/"):]
    for suffix in (".tth", ".ttz", SPRITE_SUFFIX):
        if normalized.endswith(suffix):
            normalized = normalized[:-len(suffix)]
            break
    if not normalized or normalized.startswith("../") or "/../" in normalized:
        raise ValueError(f"invalid texture path {path!r}")
    return normalized


def asset_id(path: str) -> str:
    return "vtmb:texture:" + normalize_texture_path(path)


def output_relative_path(path: str) -> PurePosixPath:
    return PurePosixPath(normalize_texture_path(path) + ".glb")


def plain(value: Any) -> Any:
    if is_dataclass(value):
        return plain(asdict(value))
    if isinstance(value, dict):
        return {str(key): plain(item) for key, item in value.items()}
    if isinstance(value, (list, tuple)):
        return [plain(item) for item in value]
    return value
