"""The spans one map-visibility unit is cut from, resolved UP-first through the map root.

The BSP resolves as one member; the partition proof (`formats/map_glb/partition.py`) says which
of its bytes this unit owns. Each owned lump becomes one `SourceMember` carrying a `span`, so the
unit's ledger is gapless over each span alone and never states a byte the root or a sibling
sub-unit is authoritative for.

The root's leaf table (lump 10) is read here too and is deliberately *not* a member: its bytes
belong to the map root, and this unit only restates them as `derived` leaf lists under a
`dependencies` row naming the root.
"""

from __future__ import annotations

from dataclasses import dataclass
import hashlib
from typing import Callable

import elysium_pipeline.formats.map_glb.partition as map_partition
from elysium_pipeline.formats.map_glb import source as map_source
from elysium_pipeline.formats.map_visibility_glb import model as vis_model
from elysium_pipeline.formats.unit_contract import (
    Origin,
    SourceMember,
    origin_of,
    read_member,
)


class MapVisibilitySourceError(RuntimeError):
    """The requested map member is absent, or is not a BSP this seam can partition."""


@dataclass(frozen=True, slots=True)
class MapVisibilitySourceClosure:
    """One map's visibility spans, the file they were cut from and the root's leaf table."""

    key: str
    asset_id: str
    source_path: str
    data: bytes                                   # the whole BSP, for the spans and lump 10
    origin: Origin
    partition: map_partition.Partition
    visibility: SourceMember
    portals: tuple[SourceMember, ...]

    def members(self) -> tuple[SourceMember, ...]:
        """Every member this unit publishes a ledger for: lump 4 first, then 22-25."""

        return (self.visibility,) + self.portals

    def portal_member(self, lump: int) -> SourceMember | None:
        wanted = vis_model.member_path(self.key, lump)
        for member in self.portals:
            if member.path == wanted:
                return member
        return None

    @property
    def byte_length(self) -> int:
        return len(self.data)

    @property
    def sha256(self) -> str:
        """The whole BSP's digest, which pins the root unit this one depends on."""

        return hashlib.sha256(self.data).hexdigest()

    @property
    def leaf_span(self) -> map_partition.LumpSpan:
        return self.partition.lump(vis_model.LEAF_LUMP)

    def leaf_bytes(self) -> bytes:
        """The root's leaf table, the one thing this unit derives from without owning it."""

        span = self.leaf_span
        if not span.populated:
            return b""
        return self.data[span.offset:span.end]


def source_keys(index: dict) -> list[str]:
    """Every map key the UP-first install index resolves, sorted.

    One map is one visibility unit: lump 4 is populated on all 108 retail maps, and a map that
    carried none would still publish -- with an empty selecting member and a warning -- rather
    than disappear from the family.
    """

    return map_source.source_keys(index)


def _member(
    key: str, data: bytes, origin: Origin, span: map_partition.LumpSpan, role: str
) -> SourceMember:
    return SourceMember(
        role=role,
        path=vis_model.member_path(key, span.index),
        data=data[span.offset:span.end] if span.populated else b"",
        origin=origin,
        span=(span.offset, span.length if span.populated else 0),
    )


def load_source_closure(
    index: dict,
    key: str,
    *,
    read_bytes: Callable[[dict, str], bytes | None] | None = None,
) -> MapVisibilitySourceClosure:
    """Resolve one map UP-first and cut the five lumps this unit owns out of it."""

    normalized = vis_model.normalize_key(key)
    path = vis_model.source_path(normalized)
    entry = index.get(path)
    data = read_member(index, path, read_bytes=read_bytes) if entry else None
    if data is None:
        raise MapVisibilitySourceError(f"missing required map: {path}")
    try:
        proof = map_partition.partition(data)
    except map_partition.PartitionError as error:
        raise MapVisibilitySourceError(f"{path}: {error}") from error
    origin = origin_of(entry)
    visibility = _member(
        normalized, data, origin, proof.lump(vis_model.VISIBILITY_LUMP), vis_model.VISIBILITY_ROLE
    )
    portals = tuple(
        _member(normalized, data, origin, proof.lump(lump), vis_model.PORTAL_ROLES[lump])
        for lump in vis_model.PORTAL_LUMPS
        if proof.lump(lump).populated
    )
    return MapVisibilitySourceClosure(
        key=normalized,
        asset_id=vis_model.asset_id(normalized),
        source_path=path,
        data=data,
        origin=origin,
        partition=proof,
        visibility=visibility,
        portals=portals,
    )


__all__: list[str] = [
    "MapVisibilitySourceClosure",
    "MapVisibilitySourceError",
    "load_source_closure",
    "source_keys",
]
