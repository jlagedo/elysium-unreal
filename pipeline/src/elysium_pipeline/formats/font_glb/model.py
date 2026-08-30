"""Semantic records for the isolated Font GLB exporter.

Two identities share this package: `vtmb:font:<stem>` for one `.fnt` glyph table below
`materials/fonts/`, and the single `vtmb:font-list:fontlist` registry unit cut from
`materials/fonts/fontlist.txt`. Each declares its own extension, `ELYSIUM_vtmb_font` and
`ELYSIUM_vtmb_font_list` respectively, per `seam_map_unit_contract.md`'s "every unit declares its
own extension" rule -- mirroring the sibling two-identity seam, `shader_program_glb`.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import PurePosixPath
import re
from typing import Any

from elysium_pipeline.formats.unit_contract import SourceMember
from elysium_pipeline.formats.unit_contract import asset_id as _contract_asset_id
from elysium_pipeline.formats.unit_contract import extension_name

#: The font unit's own extension.
FONT_EXTENSION = extension_name("font")
#: The font-list unit's own extension, distinct from the font unit's (see the module docstring).
FONT_LIST_EXTENSION = extension_name("font-list")
SCHEMA_VERSION = "1.0.0"

#: The `asset.generator` title each kind signs its product with (`seam_map_unit_contract.md`:
#: `Elysium <Kind> GLB Exporter`, hyphenating a multi-word kind to match its kind slug, e.g.
#: `Expression-table`, `Nav-graph`, `Shader-source`). `font-list`'s own slug hyphenates the same
#: way, so its title is `Font-list`, not `Font List`.
FONT_GENERATOR_TITLE = "Font"
FONT_LIST_GENERATOR_TITLE = "Font-list"

FONTS_DIR = "materials/fonts"
FONT_LIST_KEY = "fontlist"
FONT_LIST_PATH = f"{FONTS_DIR}/fontlist.txt"

#: `<face>_<size>_<weight>_<flags>`; `face` may itself carry underscores, so the three trailing
#: numeric groups are what anchors the split.
NAME_PATTERN = re.compile(r"^(?P<face>.+)_(?P<size>\d+)_(?P<weight>\d+)_(?P<flags>\d+)$")


class FontModelError(ValueError):
    """A font identity or a registry row does not have the shape this seam expects."""


def normalize_font_key(key: str) -> str:
    """The unit key: the file stem below `materials/fonts/`, tolerant of the root/extension."""

    normalized = str(key).replace("\\", "/").strip().strip("/").lower()
    prefix = FONTS_DIR + "/"
    if normalized.startswith(prefix):
        normalized = normalized[len(prefix):]
    if normalized.endswith(".fnt"):
        normalized = normalized[: -len(".fnt")]
    if not normalized or "/" in normalized:
        raise FontModelError(f"invalid font key {key!r}")
    return normalized


def font_path(key: str) -> str:
    return f"{FONTS_DIR}/{normalize_font_key(key)}.fnt"


def page_texture_path(key: str, page: int) -> str:
    return f"{FONTS_DIR}/{normalize_font_key(key)}-page{page}.tth"


def page_material_path(key: str, page: int) -> str:
    return f"{FONTS_DIR}/{normalize_font_key(key)}-page{page}.vmt"


def asset_id(key: str) -> str:
    return _contract_asset_id("font", normalize_font_key(key))


def font_list_asset_id() -> str:
    return _contract_asset_id("font-list", FONT_LIST_KEY)


def output_relative_path(key: str) -> PurePosixPath:
    return PurePosixPath(normalize_font_key(key) + ".glb")


def font_list_output_relative_path() -> PurePosixPath:
    return PurePosixPath(FONT_LIST_KEY + ".glb")


@dataclass(frozen=True, slots=True)
class NameParts:
    face: str
    size: int
    weight: int
    flags: int


def parse_name(key: str) -> NameParts | None:
    """The `<face>_<size>_<weight>_<flags>` parse of a normalized key, or `None` on a mismatch."""

    match = NAME_PATTERN.match(normalize_font_key(key))
    if match is None:
        return None
    return NameParts(
        face=match.group("face"),
        size=int(match.group("size")),
        weight=int(match.group("weight")),
        flags=int(match.group("flags")),
    )


def registry_key(face: str, size: int, weight: int, flags: int) -> str:
    """The `vtmb:font:` key one `fontlist.txt` row composes.

    The shipped stems zero-pad `size` to at least two digits and `weight`/`flags` to at least
    three; a value with more digits than that is left alone, which is exactly what `zfill` does.
    """

    folded_face = face.strip().lower().replace(" ", "_")
    return f"{folded_face}_{str(size).zfill(2)}_{str(weight).zfill(3)}_{str(flags).zfill(3)}"


@dataclass(slots=True)
class FontModel:
    """The complete decode of one `.fnt` glyph table."""

    key: str
    asset_id: str
    members: tuple[SourceMember, ...]
    header: dict[str, Any]
    char_map: list[int]
    glyphs: list[dict[str, Any]]
    trailer: dict[str, Any]
    pages: list[dict[str, Any]]
    font_list: dict[str, Any] | None
    dependencies: list[dict[str, Any]]
    anomalies: list[dict[str, Any]]
    omissions: list[dict[str, Any]]
    unresolved: list[dict[str, Any]]
    unsupported: list[dict[str, Any]]
    typed_unidentified: list[dict[str, Any]]
    byte_ledger: list[dict[str, Any]]
    name: NameParts | None = None


@dataclass(slots=True)
class FontListModel:
    """The complete decode of `materials/fonts/fontlist.txt`."""

    members: tuple[SourceMember, ...]
    rows: list[dict[str, Any]]
    comments: list[dict[str, Any]]
    dependencies: list[dict[str, Any]]
    anomalies: list[dict[str, Any]]
    omissions: list[dict[str, Any]]
    unresolved: list[dict[str, Any]]
    unsupported: list[dict[str, Any]]
    byte_ledger: list[dict[str, Any]] = field(default_factory=list)
    omitted_proven: list[dict[str, Any]] = field(default_factory=list)
