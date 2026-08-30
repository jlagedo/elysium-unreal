"""Semantic records for the isolated Expression-table GLB exporter.

One unit is one Faceposer expression or phoneme table selected by its stem below
`expressions/`. The compiled `.vfe` is the runtime authority; the readable `.txt` is authoring
evidence carried and compared, never merged into the VFE's table -- `docs/architecture/
seam_map_expression_table.md` owns the shape.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import PurePosixPath
from typing import Any

from elysium_pipeline.formats.unit_contract import SourceMember, asset_id, extension_name

KIND = "expression-table"
SCHEMA_VERSION = "1.0.0"
FAMILY = "expression-tables"
EXPRESSION_TABLE_EXTENSION = extension_name(KIND)

#: `expressions/<stem>.vfe` / `expressions/<stem>.txt`, install-relative and lower-cased.
SOURCE_DIRECTORY = "expressions"


def normalize_stem(stem: str) -> str:
    """The unit key: the file stem below `expressions/`, folded and forward-slashed."""

    normalized = str(stem).strip().replace("\\", "/").lower()
    normalized = normalized.rsplit("/", 1)[-1]
    for suffix in (".vfe", ".txt"):
        if normalized.endswith(suffix):
            normalized = normalized[: -len(suffix)]
    if not normalized or "/" in normalized:
        raise ValueError(f"invalid expression-table stem {stem!r}")
    return normalized


def stem_asset_id(stem: str) -> str:
    return asset_id(KIND, normalize_stem(stem))


def output_relative_path(stem: str) -> PurePosixPath:
    return PurePosixPath(FAMILY) / (normalize_stem(stem) + ".glb")


def identity_class(stem: str) -> str:
    """`expressions`, `phonemes` or `none`, from the stem's own suffix."""

    normalized = normalize_stem(stem)
    if normalized.endswith("_expressions"):
        return "expressions"
    if normalized.endswith("_phonemes"):
        return "phonemes"
    return "none"


@dataclass(frozen=True, slots=True)
class ExpressionTableModel:
    """The complete decode of one expression-table unit, ready for `build_document`."""

    stem: str
    asset: str
    identity_class_: str
    source_kind: str                    # "vfe+txt" | "vfe-only" | "txt-only"
    runtime_loadable: bool
    members: tuple[SourceMember, ...]
    vfe: dict[str, Any] | None
    table: dict[str, Any] | None
    txt: dict[str, Any] | None
    authoring: dict[str, Any] | None
    comparison: dict[str, Any]
    anomalies: list[dict[str, Any]] = field(default_factory=list)
    omissions: list[dict[str, Any]] = field(default_factory=list)
    typed_unidentified: list[dict[str, Any]] = field(default_factory=list)
    unresolved: list[dict[str, Any]] = field(default_factory=list)
    unsupported: list[dict[str, Any]] = field(default_factory=list)
    byte_ledger: list[dict[str, Any]] = field(default_factory=list)
