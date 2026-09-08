"""Semantic vocabulary for the isolated Scene GLB exporter.

A scene unit is one Faceposer `.vcd` below `sound/`: an event list keyed by actor name, with no
node, no skeleton and no sampled channel. This module owns the identity rule (the key, the asset
ID, the output path) and the format's closed and open token vocabularies; `decode.py` is the
reader that applies them.
"""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import PurePosixPath
from typing import Any

from elysium_pipeline.formats.unit_contract import asset_id as _unit_asset_id
from elysium_pipeline.formats.unit_contract import extension_name

SCENE_EXTENSION = extension_name("scene")
#: 1.1.0 added the source capsule: the `.vcd` member's exact bytes travel in the unit's BIN
#: chunk, hash-checked against `sourceResolution`.
SCHEMA_VERSION = "1.1.0"

#: The install directory a scene's key sits below, and the source extension the key drops.
SCENE_ROOT = "sound/"
SCENE_SUFFIX = ".vcd"

#: `CChoreoEvent`'s type enum: the token the grammar carries, and the numeric id the shipped
#: dispatcher (`DispatchStartEvent`) switches on.
EVENT_TYPE_IDS: dict[str, int] = {
    "section": 1,
    "expression": 2,
    "lookat": 3,
    "moveto": 4,
    "speak": 5,
    "gesture": 6,
    "sequence": 7,
    "face": 8,
    "firetrigger": 9,
    "flexanimation": 10,
    "subscene": 11,
    "loop": 12,
    "silence": 13,
    "loud": 14,
    "python": 15,
    "cameramove": 16,
    "camerashot": 17,
    "camerarestore": 18,
    "bodysound": 19,
}

#: Tokens Faceposer's parser recognises and the shipped corpus never uses. A general reader of
#: the grammar decodes these into `extras[]` on their owner, words and block verbatim, rather
#: than refusing them.
RECOGNISED_UNUSED_TOKENS = frozenset({
    "tags", "absolutetags", "relativetag", "flextimingtags", "flexanimations",
    "resumecondition", "loopcount", "yaw", "targethead", "mapname", "ramp", "range",
    "combo", "disabled", "samples_use_time",
})

#: Event types the shipped dispatcher's jump table has no case for. Decoded like any other
#: event; the record just carries `unhandled: true`.
UNHANDLED_EVENT_TYPES = frozenset({"camerashot"})


@dataclass(frozen=True, slots=True)
class SceneModel:
    """Everything one `.vcd` unit publishes, already shaped for `build_document`."""

    key: str
    asset_id: str
    source_path: str
    member: Any                                    # unit_contract.origin.SourceMember
    version: int | None
    fps: int | None
    snap: bool | None
    actors: list[dict[str, Any]]
    script_expressions: list[dict[str, Any]]
    dependencies: list[dict[str, Any]]
    comments: list[dict[str, Any]]
    anomalies: list[dict[str, Any]]
    omissions: list[dict[str, Any]]
    unresolved: list[dict[str, Any]]
    unsupported: list[dict[str, Any]]
    byte_ledger: list[dict[str, Any]]


def normalize_scene_key(path: str) -> str:
    """The unit key: install-relative below `sound/`, without `.vcd`, folded and slashed."""

    normalized = str(path).replace("\\", "/").strip("/")
    lowered = normalized.lower()
    if lowered.startswith(SCENE_ROOT):
        normalized = normalized[len(SCENE_ROOT):]
    if normalized.lower().endswith(SCENE_SUFFIX):
        normalized = normalized[: -len(SCENE_SUFFIX)]
    normalized = normalized.strip("/").lower()
    if not normalized or normalized.startswith("../") or "/../" in normalized:
        raise ValueError(f"invalid scene key {path!r}")
    return normalized


def asset_id(key: str) -> str:
    return _unit_asset_id("scene", key)


def source_path_for(key: str) -> str:
    return SCENE_ROOT + normalize_scene_key(key) + SCENE_SUFFIX


def output_relative_path(key: str) -> PurePosixPath:
    return PurePosixPath(normalize_scene_key(key) + ".glb")
