"""Semantic records for the isolated Nav-graph GLB exporter."""

from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import PurePosixPath
from typing import Any

from elysium_pipeline.formats.unit_contract import asset_id as _asset_id
from elysium_pipeline.formats.unit_contract import extension_name

#: The unit kind this package publishes: `vtmb:nav-graph:<map>`.
KIND = "nav-graph"

#: The extension every unit of this kind declares: `ELYSIUM_vtmb_nav_graph`.
NAV_GRAPH_EXTENSION = extension_name(KIND)

SCHEMA_VERSION = "1.0.0"

#: Every retail `.ain` declares this; any other value is `anomalies[] version-not-30`.
EXPECTED_VERSION = "30"


def normalize_key(key: str) -> str:
    """The map stem: lower-cased, forward-slashed, `maps/graphs/` and `.ain`/`.loc` stripped.

    The unit-contract command surface tolerates the root prefix and the source extension on the
    caller's argument, so this seam accepts `sp_tutorial_1`, `maps/graphs/sp_tutorial_1.ain` and
    every spelling between the two.
    """

    normalized = str(key).strip().replace("\\", "/").lower()
    if normalized.startswith("maps/graphs/"):
        normalized = normalized[len("maps/graphs/"):]
    if normalized.startswith("nav-graphs/"):
        normalized = normalized[len("nav-graphs/"):]
    for suffix in (".ain", ".loc", ".glb"):
        if normalized.endswith(suffix):
            normalized = normalized[: -len(suffix)]
    if not normalized or "/" in normalized:
        raise ValueError(f"invalid nav-graph map key {key!r}")
    return normalized


def asset_id(key: str) -> str:
    return _asset_id(KIND, normalize_key(key))


def output_relative_path(key: str) -> PurePosixPath:
    return PurePosixPath(normalize_key(key) + ".glb")


@dataclass(frozen=True, slots=True)
class HeaderField:
    """One labelled header line: its parsed value and the byte range of label plus value."""

    value: int
    offset: int
    length: int


@dataclass(frozen=True, slots=True)
class Header:
    version: str
    version_field: HeaderField
    num_hulls: HeaderField
    used_hull_bits: HeaderField
    zone_count: HeaderField
    num_nodes: HeaderField
    total_num_links: HeaderField


@dataclass(frozen=True, slots=True)
class Zones:
    values: tuple[int, ...]
    offset: int
    length: int


@dataclass(frozen=True, slots=True)
class NodeLabel:
    """One `Nodes:` token inside the node stream, tracked so the ledger can claim it apart from
    the node data around it."""

    index: int
    offset: int
    length: int


@dataclass(frozen=True, slots=True)
class Node:
    """One node record: 2 fixed tokens (origin, yaw), `numHulls` hull offsets, then whatever the
    file's own per-node width leaves over before the trailing 2-token `lead` pair.

    `tail` is carried as the corpus's node width actually is: every shipped `.ain` outside
    `sp_tutorial_1` widens it well past 6 ints (`specDeviations` in the exporter's manifest), so
    it is a variable-length int list here, never a fixed 6-tuple.
    """

    index: int
    origin_source: tuple[float, float, float]
    yaw: float
    hull_offsets: tuple[float, ...]
    tail: tuple[int, ...]
    lead: tuple[int, ...]
    wc_id: int | None
    source_line: int
    source_offset: int
    offset: int
    length: int


@dataclass(frozen=True, slots=True)
class Link:
    index: int
    src: int
    dst: int
    fields: tuple[int, ...]
    source_line: int
    source_offset: int
    offset: int
    length: int


@dataclass(frozen=True, slots=True)
class WCLookup:
    values: tuple[int, ...]
    offset: int
    length: int


@dataclass(frozen=True, slots=True)
class Stamp:
    """The `.loc` companion: the raw digit text, its integer value, and the file's own extent.

    `value` is `None` when the companion resolved but does not parse as a decimal stamp plus
    CRLF -- `raw` is then also `None` (never the file's own bytes: a unit never embeds an opaque
    copy of its source member) and `anomalies[] malformed-loc` names the departure, alongside the
    member's own `byteLength`/`sha256`, rather than the unit failing to publish over an optional
    companion.
    """

    raw: str | None
    value: int | None
    byte_length: int
    sha256: str
    offset: int
    length: int
    line_end_offset: int
    line_end_length: int


@dataclass(slots=True)
class NavGraphModel:
    key: str
    asset_id: str
    ain_path: str
    loc_path: str | None
    members: tuple[Any, ...]
    header: Header
    zones: Zones
    nodes: list[Node]
    node_labels: list[NodeLabel]
    links: list[Link]
    wc_lookup: WCLookup
    stamp: Stamp | None
    dependencies: list[dict[str, Any]]
    anomalies: list[dict[str, Any]] = field(default_factory=list)
    omissions: list[dict[str, Any]] = field(default_factory=list)
    typed_unidentified: list[dict[str, Any]] = field(default_factory=list)
    mapped: list[str] = field(default_factory=list)
    byte_ledger: list[dict[str, Any]] = field(default_factory=list)
