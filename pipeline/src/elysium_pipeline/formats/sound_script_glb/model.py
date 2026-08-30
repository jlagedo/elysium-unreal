"""Semantic records for the sound-script GLB exporter's five unit kinds."""

from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import PurePosixPath
from typing import Any

from elysium_pipeline.formats.unit_contract import asset_id, extension_name

SCHEMA_VERSION = "1.0.0"
SOUND_SCRIPT_EXTENSION = extension_name("sound-script")

#: The one identity kind title every unit publishes as `asset.generator`; the five kinds share
#: one extension and one exporter, so they share one generator title too.
GENERATOR_TITLE = "Sound-script"

#: The six tables this seam cuts, keyed the way `source.py` addresses them.
TABLE_PATHS = {
    "game-sound-live": "scripts/game_sounds_surfaceproperties.txt",
    "game-sound-dormant": "scripts/sounds.txt",
    "manifest": "scripts/game_sounds_manifest.txt",
    "soundscape": "scripts/soundscapes.txt",
    "sentence": "scripts/sentences.txt",
    "dsp-preset": "scripts/dsp_presets.txt",
}

MANIFEST_KEY = "game_sounds_manifest"


class SoundScriptModelError(ValueError):
    """A name or identity does not fit the seam's rules."""


def normalize_name(name: str) -> str:
    """The unit key: the entry name folded to lower case.

    Every table this seam cuts spells names in mixed case and both the manifest and the
    soundscape `dsp` key reference them freely, so the identity a consumer can join on is folded.
    """

    normalized = str(name).strip().lower()
    if not normalized or "/" in normalized or "\\" in normalized:
        raise SoundScriptModelError(f"invalid sound-script name {name!r}")
    return normalized


def game_sound_asset_id(name: str) -> str:
    return asset_id("sound-script", normalize_name(name))


def manifest_asset_id() -> str:
    return asset_id("sound-script-manifest", MANIFEST_KEY)


def soundscape_asset_id(name: str) -> str:
    return asset_id("soundscape", normalize_name(name))


def sentence_asset_id(name: str) -> str:
    return asset_id("sentence", normalize_name(name))


def dsp_preset_asset_id(preset_id: int | str) -> str:
    return asset_id("dsp-preset", str(int(preset_id)))


#: `role | Produced by` names `sound-script-table` for a manifest `precache_file` row without
#: naming a kind that owns that identity anywhere in the contract; this seam mints one in the
#: role's own namespace so the row still carries a stable, joinable asset id. See
#: `specDeviations`.
def sound_script_table_asset_id(path: str) -> str:
    return asset_id("sound-script-table", path)


def sound_asset_id(path: str) -> str:
    """The stable ID for a `.wav`/`.mp3` a `wave` value names directly."""

    normalized = path.replace("\\", "/").strip().strip('"').lower()
    while "//" in normalized:
        normalized = normalized.replace("//", "/")
    return "vtmb:sound:" + normalized.strip("/")


def game_sound_output_path(name: str) -> PurePosixPath:
    return PurePosixPath("sound-scripts") / (normalize_name(name) + ".glb")


def manifest_output_path() -> PurePosixPath:
    return PurePosixPath("sound-scripts") / (MANIFEST_KEY + ".glb")


def soundscape_output_path(name: str) -> PurePosixPath:
    return PurePosixPath("soundscapes") / (normalize_name(name) + ".glb")


def sentence_output_path(name: str) -> PurePosixPath:
    return PurePosixPath("sentences") / (normalize_name(name) + ".glb")


def dsp_preset_output_path(preset_id: int | str) -> PurePosixPath:
    return PurePosixPath("dsp-presets") / (str(int(preset_id)) + ".glb")


@dataclass(frozen=True, slots=True)
class Parameter:
    """One `key value` pair, or one positional DSP token, exactly as the entry declares it.

    `offset` is relative to the unit's own span, which is what its byte ledger is written
    against; the span's offset within the table lives in the source member's `span`.
    """

    index: int
    key: str
    source_key: str
    value: str
    quoted_key: bool
    quoted_value: bool
    offset: int


@dataclass(slots=True)
class SoundScriptModel:
    """The one shape every kind's decode returns; `record` carries the kind-specific projection."""

    kind: str
    name: str
    source_name: str
    asset_id: str
    dormant: bool
    dormant_evidence: str | None
    sources: list[Any]
    parameters: list[Parameter]
    record: dict[str, Any]
    dependencies: list[dict[str, Any]]
    comments: list[dict[str, Any]]
    anomalies: list[dict[str, Any]]
    omissions: list[dict[str, Any]]
    unresolved: list[dict[str, Any]] = field(default_factory=list)
    unsupported: list[dict[str, Any]] = field(default_factory=list)
    typed_unidentified: list[dict[str, Any]] = field(default_factory=list)
    byte_coverage: list[dict[str, Any]] = field(default_factory=list)
