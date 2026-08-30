"""Semantic values and stable identities for the Map-visibility GLB seam.

The unit is one of three sub-units cut from one BSP member: lump 4 -- the potentially-visible and
potentially-audible cluster sets -- plus the compiler's portal graph (lumps 22-25) on the five
maps that carry it. The map root (`formats/map_glb`) owns the identities, the source path and the
partition proof this module names its spans by, so nothing here re-derives them.

Offsets published beside a decoded record (`clusters[].sourceOffset`, `clusters[].pvs.offset` and
the row anomalies) are relative to the start of the span the record was read from -- lump 4's
first byte -- which is the same origin the unit's byte ledger uses, so a ledger range and the
record it pays for name the same place.

File-absolute positions belong to the lump table: `map.lumps[].offset` is where a lump's payload
sits and `map.lumps[].sourceOffset` is where the directory row that named it sits, both spelled
the way the map root's own partition spells them. A record decoded out of a span never restates a
file-absolute offset.

A carried portal lump is the one record this unit publishes without decoding, so the contract's
rule that a `typedUnidentified` value keeps its source-offset identity applies to it: its
`coverage.typedUnidentified` row states the lump's file offset and digest beside the member it
is. The `portals.<key>` entry itself restates neither -- the grade, the member row and the lump
table already state both, and one datum is stated once.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import PurePosixPath
from typing import Any

from elysium_pipeline.formats import bsp as bsp_readers
from elysium_pipeline.formats.map_glb import model as map_model
from elysium_pipeline.formats.map_glb import partition as map_partition
from elysium_pipeline.formats.unit_contract import coverage as _coverage

#: The kind this seam publishes, the title its generator string carries and the extension every
#: unit declares in both `extensionsUsed` and `extensionsRequired`.
KIND = "map-visibility"
KIND_TITLE = "Map-visibility"
MAP_VISIBILITY_EXTENSION = _coverage.extension_name(KIND)
SCHEMA_VERSION = "1.0.0"

#: The family directory the unit is written into, and the suffix that separates it from the root's
#: own file: `maps/<map>.visibility.glb` beside `maps/<map>.glb`.
FAMILY_DIR = map_model.FAMILY_DIR
UNIT_SUFFIX = "visibility"

#: The lumps the partition hands this unit. Lump 4 is on all 108 maps; 22-25 are on five.
VISIBILITY_LUMP = 4
PORTAL_LUMPS = (22, 23, 24, 25)
OWNED_LUMPS = (VISIBILITY_LUMP,) + PORTAL_LUMPS

#: The root's leaf table, which the `derived` leaf lists are read from. Its bytes belong to the
#: map root, so this unit reads them and never claims them, and the `dleaf_t` layout it reads
#: through is the one `formats/bsp.py` owns for the whole project rather than a copy of it.
LEAF_LUMP = 10
LEAF_RECORD_BYTES = bsp_readers.LEAF_SIZE
LEAF_CLUSTER_OFFSET = bsp_readers.LF_CLUSTER

#: `int numclusters` then `int byteofs[numclusters][2]`, both little-endian.
VIS_HEADER_BYTES = 4
BYTEOFS_ENTRY_BYTES = 8

#: The `portals` key each portal lump is published under, and the member role it fills.
PORTAL_KEYS = {22: "portals", 23: "clusters", 24: "verts", 25: "clusterPortals"}
PORTAL_ROLES = {22: "portals", 23: "portal-clusters", 24: "portal-verts", 25: "cluster-portals"}

#: The stock Source record shape each portal lump is *a candidate* for. The layouts are not
#: established against these five maps' bytes, so a candidate is only ever used to state a
#: divisibility test: a lump's length must divide by the record size before that reading could be
#: admitted at all, and no unit here admits one.
PORTAL_CANDIDATES = {
    22: ("dportal_t", 10),
    23: ("dcluster_t", 8),
    24: ("dportalvert", 12),
    25: ("dclusterportal", 2),
}

#: Why the portal lumps are carried rather than decoded.
PORTAL_REASON = (
    "the compiler's portal graph; its record layout is not established against the five maps "
    "that carry it, so the lump's bytes and identity are stated and its meaning is left open"
)

#: The role lump 4 fills in the member table.
VISIBILITY_ROLE = "visibility"

#: Every departure from the format this seam is allowed to record. A row naming anything else is a
#: tolerance the seam never agreed to, and the validator refuses it.
ANOMALY_ROLES = frozenset(
    {
        "pvs-row-overrun",
        "pvs-row-short",
        "pas-row-overrun",
        "pas-row-short",
        "byteofs-out-of-range",
        "portal-lump-not-divisible",
        # Departures the specification's table does not name but the bytes can still produce.
        "byteofs-table-truncated",
        "row-span-overlap",
        "leaf-cluster-out-of-range",
        "leaf-table-not-divisible",
    }
)

#: Every range that contributes no payload byte, and the empty-member row the contract names.
OMISSION_ROLES = frozenset({"unreferenced-vis-bytes", "empty-member"})

#: The semantic grades a `coverage.mapped` row may carry.
MAPPED_STATES = frozenset({"mapped", "equivalent", "derived"})

MapVisibilityKeyError = map_model.MapKeyError


def normalize_key(key: str) -> str:
    """The unit key: the `.bsp` stem below `maps/`, folded, as the map root spells it."""

    return map_model.normalize_key(key)


def source_path(key: str) -> str:
    """The one install-relative member all of the map's units are cut from."""

    return map_model.source_path(key)


def member_path(key: str, lump: int) -> str:
    """One owned lump's member path.

    A span member needs an identity of its own -- the contract's ledger table is keyed by member
    path and two members may not share one -- so each span names the file it was cut from and the
    directory row that cut it.
    """

    return f"{source_path(key)}#lump{int(lump)}"


def asset_id(key: str) -> str:
    return map_model.sub_asset_id(KIND, key)


def map_asset_id(key: str) -> str:
    """The root unit whose leaf table this one's `derived` rows restate."""

    return map_model.asset_id(key)


def output_relative_path(key: str) -> PurePosixPath:
    return map_model.sub_output_relative_path(UNIT_SUFFIX, key)


def lump_name(lump: int) -> str:
    return map_partition.LUMP_NAMES[int(lump)]


def row_byte_length(num_clusters: int) -> int:
    """`ceil(numClusters / 8)`: one bit per cluster, rounded up to whole bytes."""

    return (max(0, int(num_clusters)) + 7) // 8


def set_bits(row: bytes, limit: int) -> int:
    """How many of the row's first `limit` bits are set.

    Bits at and above `numClusters` are the last byte's slack rather than clusters, so they are
    not counted: the answer is the number of clusters the row makes visible (audible).
    """

    if limit <= 0 or not row:
        return 0
    value = int.from_bytes(bytes(row), "little")
    return (value & ((1 << int(limit)) - 1)).bit_count()


@dataclass(frozen=True, slots=True)
class RowRef:
    """One cluster's PVS or PAS entry: the offset it declares, and the row that offset names.

    An entry that decodes to no byte at all publishes `emptyRow` and no accessor: glTF gives
    `accessor.count` a minimum of one, so a row that produced nothing is stated by the entry and
    its `*-row-short` anomaly rather than by a degenerate accessor.
    """

    offset: int                                  # the `byteofs` value, relative to the lump
    accessor: int | None = None
    compressed_length: int | None = None
    shared_with: int | None = None               # the cluster that records the shared row
    shared_set: str | None = None                # which of that cluster's two rows it is
    out_of_range: bool = False
    empty_row: bool = False

    def to_json(self) -> dict[str, Any]:
        if self.out_of_range:
            return {"offset": self.offset, "outOfRange": True}
        if self.shared_with is not None:
            return {
                "offset": self.offset,
                "sharedWith": self.shared_with,
                "sharedSet": self.shared_set,
            }
        if self.empty_row:
            return {
                "offset": self.offset,
                "compressedLength": self.compressed_length,
                "emptyRow": True,
            }
        return {
            "accessor": self.accessor,
            "offset": self.offset,
            "compressedLength": self.compressed_length,
        }


@dataclass(frozen=True, slots=True)
class ClusterRow:
    """One cluster: its two rows, the leaves that resolve to it and its two set-bit counts.

    A count is `null` where the cluster's `byteofs` entry names no row at all, because a zero
    would state that nothing is visible from the cluster rather than that the source declined to
    say; the matching `byteofs-out-of-range` anomaly is what carries the reason.
    """

    index: int
    source_offset: int                           # where the cluster's two offsets sit in the span
    pvs: RowRef
    pas: RowRef
    leaves: tuple[int, ...]
    visible_count: int | None
    audible_count: int | None

    def to_json(self) -> dict[str, Any]:
        return {
            "index": self.index,
            "sourceOffset": self.source_offset,
            "pvs": self.pvs.to_json(),
            "pas": self.pas.to_json(),
            "leaves": list(self.leaves),
            "visibleCount": self.visible_count,
            "audibleCount": self.audible_count,
        }


@dataclass(frozen=True, slots=True)
class PortalLump:
    """One portal-graph lump, carried with its identity named and its meaning left open.

    The `portals.<key>` entry names the member its bytes are, and the member row plus
    `map.lumps[]` are where that member's position and digest are stated; the entry restates
    neither. The `coverage.typedUnidentified` grade does carry both, because a carried record
    keeps its source-offset identity there, next to the candidate layout it was not read as.
    """

    lump: int
    key: str
    member: str
    source_offset: int                           # where the lump sits in the BSP
    byte_length: int
    sha256: str
    candidate: str
    record_bytes: int

    @property
    def remainder(self) -> int:
        return self.byte_length % self.record_bytes if self.record_bytes else self.byte_length

    @property
    def whole_records(self) -> int:
        return self.byte_length // self.record_bytes if self.record_bytes else 0

    @property
    def divides(self) -> bool:
        return self.record_bytes > 0 and not self.remainder

    def _sentence(self) -> str:
        return (
            f"lump {self.lump} ({lump_name(self.lump)}) is carried whole: {self.byte_length} "
            f"bytes {'divide' if self.divides else 'do not divide'} by the candidate "
            f"{self.candidate} record of {self.record_bytes} bytes"
        )

    def to_json(self) -> dict[str, Any]:
        return {
            "lump": self.lump,
            "name": lump_name(self.lump),
            "member": self.member,
            "byteLength": self.byte_length,
            "candidate": {"struct": self.candidate, "recordBytes": self.record_bytes},
            "divides": self.divides,
            "wholeRecords": self.whole_records,
            "remainder": self.remainder,
            "reason": PORTAL_REASON,
        }

    def coverage_row(self) -> dict[str, Any]:
        return {
            "field": f"portals.{self.key}",
            "lump": self.lump,
            "member": self.member,
            "sourceOffset": self.source_offset,
            "byteLength": self.byte_length,
            "sha256": self.sha256,
            "candidate": self.candidate,
            "divides": self.divides,
            "reason": self._sentence(),
        }

    def anomaly_row(self) -> dict[str, Any]:
        return {
            "role": "portal-lump-not-divisible",
            "lump": self.lump,
            "name": lump_name(self.lump),
            "byteLength": self.byte_length,
            "candidate": self.candidate,
            "recordBytes": self.record_bytes,
            "remainder": self.remainder,
            # `reason` is the key the contract's operator phrasing speaks, so a warning line
            # names the lump it is about rather than repeating the role four times.
            "reason": (
                f"portal-lump-not-divisible: lump {self.lump} ({lump_name(self.lump)}) carries "
                f"{self.byte_length} bytes, which leave {self.remainder} over the candidate "
                f"{self.candidate} record of {self.record_bytes} bytes"
            ),
        }


@dataclass(slots=True)
class MapVisibilityModel:
    """Everything one map-visibility unit publishes, decoded once."""

    key: str
    asset_id: str
    source_path: str
    members: tuple[Any, ...]                     # the SourceMembers, lump 4 first
    map_block: dict[str, Any]
    num_clusters: int
    row_byte_length: int
    clusters: list[ClusterRow]
    unclustered_leaves: list[int]
    portals: dict[str, dict[str, Any]]
    accessors: list[dict[str, Any]]
    buffer_views: list[dict[str, Any]]
    binary: bytes
    dependencies: list[dict[str, Any]]
    anomalies: list[dict[str, Any]]
    omissions: list[dict[str, Any]]
    claims: dict[str, list[tuple[int, int, str, str]]] = field(default_factory=dict)
    mapped: list[dict[str, Any]] = field(default_factory=list)
    typed_unidentified: list[dict[str, Any]] = field(default_factory=list)
    omitted_proven: list[dict[str, Any]] = field(default_factory=list)
    unresolved: list[dict[str, Any]] = field(default_factory=list)
    unsupported: list[dict[str, Any]] = field(default_factory=list)
