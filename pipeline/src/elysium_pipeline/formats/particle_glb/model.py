"""Semantic records for one particle-definition GLB unit.

The particle seam owns the shape; this module holds the frozen records the
decoder fills and the exporter reads back verbatim, plus the identity helpers every other module
in the package shares.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import PurePosixPath
from typing import Any

from elysium_pipeline.formats.unit_contract import asset_id as _uc_asset_id

PARTICLE_EXTENSION = "ELYSIUM_vtmb_particle"
SCHEMA_VERSION = "1.0.0"

#: The install subdirectory every particle definition and sprite lives below.
FAMILY = "particles"

#: The three sub-block keywords a particle document nests, in the vocabulary's own spelling.
BLOCK_KINDS = ("spawn", "collide", "decal")


def normalize_particle_key(raw: str) -> str:
    """The unit key: the file stem below `particles/`, lower-cased, spaces preserved.

    A `spawn`, `collide` or `decal` block names a definition bare, with the directory, or with the
    extension; all three fold onto this one key, exactly as `formats/particles.py`'s own `_name`
    resolves them.
    """

    name = str(raw).strip().replace("\\", "/").lower()
    if name.startswith("particles/"):
        name = name[len("particles/"):]
    if name.endswith(".txt"):
        name = name[: -len(".txt")]
    return name


def asset_id(key: str) -> str:
    return _uc_asset_id("particle", normalize_particle_key(key))


def sprite_asset_id(sprite_key: str) -> str:
    """The image seam's stable ID for a sprite a particle names: `vtmb:image:particles/<x>.tga`,
    the full install-relative path with its extension kept."""

    return _uc_asset_id("image", f"{FAMILY}/{normalize_particle_key(sprite_key)}.tga")


def normalize_material_path(path: str) -> str:
    """The material seam's own key: the path below `materials/`, lower-cased, without `.vmt`."""

    normalized = str(path).replace("\\", "/").strip().strip("/").lower()
    while "//" in normalized:
        normalized = normalized.replace("//", "/")
    if normalized.startswith("materials/"):
        normalized = normalized[len("materials/"):]
    if normalized.endswith(".vmt"):
        normalized = normalized[: -len(".vmt")]
    return normalized


def material_asset_id(path: str) -> str:
    return _uc_asset_id("material", normalize_material_path(path))


def source_path(key: str) -> str:
    return f"{FAMILY}/{normalize_particle_key(key)}.txt"


def sprite_source_path(sprite_key: str) -> str:
    return f"{FAMILY}/{normalize_particle_key(sprite_key)}.tga"


def output_relative_path(key: str) -> PurePosixPath:
    return PurePosixPath(normalize_particle_key(key) + ".glb")


@dataclass(frozen=True, slots=True)
class ParsedValue:
    """The value grammar: a scalar, an `a~b` range, an `a,b,...` ramp, or free text.

    `values` holds a number per element for `scalar`/`range`/`ramp`, or the one raw string for
    `text`; a ramp element that is itself a range is a nested `{"kind": "range", ...}` mapping
    rather than a bare number, so `rate "15,5~50,20,5~50,15"` keeps both forms. `positions` is one
    entry per `values` entry, `None` unless a `v(n)` keyframe stated an explicit position.
    """

    kind: str
    values: list[Any]
    positions: list[float | None]

    def to_json(self) -> dict[str, Any]:
        return {"kind": self.kind, "values": self.values, "positions": self.positions}


@dataclass(frozen=True, slots=True)
class KeyEntry:
    """One `key value` pair, in source order, wherever in the tree it was declared."""

    index: int
    block: int | None
    key: str
    source_key: str
    value: str
    parsed: ParsedValue
    offset: int
    length: int
    quoted_key: bool
    quoted_value: bool


@dataclass(frozen=True, slots=True)
class BlockEntry:
    """One `spawn`, `collide` or `decal` sub-block."""

    index: int
    kind: str
    parent: int | None
    offset: int
    length: int
    keys: list[int]


@dataclass(slots=True)
class ParticleModel:
    key: str
    asset_id: str
    sources: list[dict[str, Any]]
    root: str
    keys: list[KeyEntry]
    blocks: list[BlockEntry]
    projection: dict[str, Any]
    role: str
    precipitation: bool
    comments: list[dict[str, Any]]
    dependencies: list[dict[str, Any]]
    anomalies: list[dict[str, Any]]
    omissions: list[dict[str, Any]]
    unresolved: list[dict[str, Any]] = field(default_factory=list)
    unsupported: list[dict[str, Any]] = field(default_factory=list)
    typed_unidentified: list[dict[str, Any]] = field(default_factory=list)
    byte_ledger: list[dict[str, Any]] = field(default_factory=list)
