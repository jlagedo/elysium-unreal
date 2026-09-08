"""Identity and stable-name rules for the sound-scheme GLB seam.

A unit is
one `sound/schemes/*.txt` KeyValues file -- one VtMB map SoundScheme -- so every name here folds
to the lower-cased stem below `sound/schemes/`, which is the seam's identity key.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import PurePosixPath
from typing import Any

from elysium_pipeline.formats.unit_contract.origin import SourceMember
from elysium_pipeline.formats.unit_contract.references import asset_id as _contract_asset_id

KIND = "sound-scheme"
KIND_TITLE = "Sound-scheme"
SCHEMA_VERSION = "1.0.0"

#: The install directory every unit is cut from, and the extension every one of its files carries.
SOURCE_ROOT = "sound/schemes/"
SOURCE_SUFFIX = ".txt"

#: The table `SchemeParams.RoomDSP` and every `RandomSound[].Filename`/`Music.Filename`/etc. name.
DSP_PRESET_PATH = "scripts/dsp_presets.txt"


class SoundSchemeModelError(ValueError):
    """A name or identity does not fit the seam's rules."""


def normalize_stem(raw: str) -> str:
    """The unit key: lower case, forward-slashed, the `sound/schemes/` prefix and `.txt` suffix
    dropped whether or not the caller supplied them.

    The singular export command "tolerates the root prefix and the source extension on its
    argument", so a bare stem and a full install path both resolve
    to the same key.
    """

    if not raw:
        raise SoundSchemeModelError("a sound scheme name is empty")
    normalized = str(raw).strip().strip('"').replace("\\", "/").lower()
    if normalized.startswith(SOURCE_ROOT):
        normalized = normalized[len(SOURCE_ROOT):]
    if normalized.endswith(SOURCE_SUFFIX):
        normalized = normalized[: -len(SOURCE_SUFFIX)]
    normalized = normalized.strip("/")
    if not normalized:
        raise SoundSchemeModelError(f"invalid sound-scheme name {raw!r}")
    return normalized


def source_path(stem: str) -> str:
    """The install-relative path one stem resolves against."""

    return f"{SOURCE_ROOT}{stem}{SOURCE_SUFFIX}"


def asset_id(stem: str) -> str:
    return _contract_asset_id(KIND, normalize_stem(stem))


def output_relative_path(stem: str) -> PurePosixPath:
    return PurePosixPath(normalize_stem(stem) + ".glb")


def sound_asset_id(path: str) -> str:
    """The stable ID for a `.wav`/`.mp3` a scheme names directly.

    Filenames are relative to `sound/`, in either slash direction and any case (the same rule
    the surface-property seam states for a footstep or impact `Filename`).
    """

    normalized = str(path).replace("\\", "/").strip().strip('"').lower()
    while "//" in normalized:
        normalized = normalized.replace("//", "/")
    normalized = normalized.strip("/")
    return _contract_asset_id("sound", normalized)


def sound_source_path(path: str) -> str:
    """The install-relative path one authored `Filename` resolves against.

    Case-folded, for membership tests against the (lower-cased) install index -- not the
    spelling-preserved path a `dependencies` row publishes; see `sound_dependency_source_path`.
    """

    normalized = str(path).replace("\\", "/").strip().strip('"').lower()
    while "//" in normalized:
        normalized = normalized.replace("//", "/")
    return "sound/" + normalized.strip("/")


def sound_dependency_source_path(path: str) -> str:
    """The install-relative path one authored `Filename` names, spelling preserved.

    The unit contract's "References between units" rule defines a dependency's `sourcePath` as
    "the install-relative path the referrer authored (spelling preserved)"; only the `sound/` root,
    slash direction and surrounding quotes/whitespace are normalized here, the way
    `sound_source_path` normalizes them for the case-folded form used for install lookups.
    """

    normalized = str(path).replace("\\", "/").strip().strip('"')
    while "//" in normalized:
        normalized = normalized.replace("//", "/")
    return "sound/" + normalized.strip("/")


def dsp_preset_asset_id(preset_id: str) -> str:
    return _contract_asset_id("dsp-preset", str(preset_id).strip().strip('"'))


@dataclass(frozen=True, slots=True)
class Parameter:
    """One `key value` pair exactly as the file declares it, in source order.

    `block` is the block-type path (`"RandomSound[3]"`) the pair was read from; `offset` is the
    key's byte offset in the file, which is what the unit's byte ledger is written against.
    """

    index: int
    block: str
    key: str
    source_key: str
    value: str
    quoted_key: bool
    quoted_value: bool
    offset: int


@dataclass(slots=True)
class SoundSchemeModel:
    """The complete decode of one `sound/schemes/<stem>.txt` file."""

    stem: str
    asset_id: str
    member: SourceMember
    parameters: list[Parameter]
    params: dict[str, Any] | None
    music: dict[str, Any] | None
    combat: dict[str, Any] | None
    alert: dict[str, Any] | None
    ambient: dict[str, Any] | None
    random_sounds: list[dict[str, Any]]
    dependencies: list[dict[str, Any]]
    comments: list[dict[str, Any]]
    anomalies: list[dict[str, Any]]
    omissions: list[dict[str, Any]]
    unresolved: list[dict[str, Any]] = field(default_factory=list)
    unsupported: list[dict[str, Any]] = field(default_factory=list)
    ledger_row: dict[str, Any] | None = None
