"""Semantic records for the isolated Image GLB exporter.

An image is a raw `.tga` or `.bmp` file below the install -- particle sprites, loose art below
`materials/`, the shipped `screenshots/` and the Faceposer tool icons below `gfx/hlfaceposer/` --
as distinct from a VTF-wrapped texture. The image seam owns the format
rules; this module states only the identity and the semantic model the decoders fill in.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import PurePosixPath
from typing import Any

from elysium_pipeline.formats.unit_contract import asset_id as _contract_asset_id
from elysium_pipeline.formats.unit_contract import extension_name
from elysium_pipeline.formats.unit_contract.origin import SourceMember

IMAGE_KIND = "image"
IMAGE_EXTENSION = extension_name(IMAGE_KIND)
SCHEMA_VERSION = "1.0.0"

#: The two containers one image key may spell, kept in the key per the seam's own rule.
IMAGE_EXTENSIONS = (".tga", ".bmp")


class ImageModelError(ValueError):
    """A path or record does not carry the shape the image seam publishes."""


def normalize_image_path(path: str) -> str:
    """The unit key: the whole install-relative path, lower-cased, forward-slashed.

    Unlike a texture, an image's identity is keyed above `materials/` -- particle sprites and
    `screenshots/` sit outside it entirely -- so the key keeps the whole path and its extension
    rather than stripping a shared root.
    """

    normalized = str(path).replace("\\", "/").strip("/").lower()
    if not normalized or normalized.startswith("../") or "/../" in normalized:
        raise ImageModelError(f"invalid image path {path!r}")
    if not normalized.endswith(IMAGE_EXTENSIONS):
        raise ImageModelError(f"invalid image path {path!r}: not a .tga or .bmp member")
    return normalized


def container_of(path: str) -> str:
    """`tga` or `bmp`, from the key's own extension."""

    normalized = normalize_image_path(path)
    return normalized.rsplit(".", 1)[-1]


def asset_id(path: str) -> str:
    return _contract_asset_id(IMAGE_KIND, normalize_image_path(path))


def output_relative_path(path: str) -> PurePosixPath:
    return PurePosixPath(normalize_image_path(path) + ".glb")


@dataclass(frozen=True, slots=True)
class ImageSourceClosure:
    """The one member an image unit is decoded from."""

    image_path: str
    asset_id: str
    container: str
    member: SourceMember

    def members(self) -> tuple[SourceMember, ...]:
        return (self.member,)


@dataclass(slots=True)
class ImageModel:
    """The complete decode of one TGA or BMP member, ready for KTX2 wrapping."""

    image_path: str
    asset_id: str
    container: str
    member: SourceMember
    width: int
    height: int
    bits_per_pixel: int
    vk_format_name: str
    vk_format_value: int
    pixel_data: bytes                      # oriented top-down, left-to-right, verbatim channels
    source_format: dict[str, Any]
    palette: dict[str, Any] | None
    footer: dict[str, Any] | None
    orientation: dict[str, Any]
    omissions: list[dict[str, Any]] = field(default_factory=list)
    anomalies: list[dict[str, Any]] = field(default_factory=list)
    typed_unidentified: list[dict[str, Any]] = field(default_factory=list)
    unresolved: list[dict[str, Any]] = field(default_factory=list)
    ledger_row: dict[str, Any] = field(default_factory=dict)
