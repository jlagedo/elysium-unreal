"""The span one map-entities unit is cut from, resolved UP-first through the map root.

The BSP resolves as one member; the partition proof (`formats/map_glb/partition.py`) says which
of its bytes this unit owns -- lump 0, and nothing else. That lump becomes one `SourceMember`
carrying a `span`, so the unit's ledger is gapless over the span alone and never states a byte
the root or a sibling sub-unit is authoritative for.

The MODELS lump (14) is read here too and is deliberately *not* a member: its records belong to
the map root, and this unit reads only how many of them there are, so a `*N` brush reference can
be proven to name a model that exists. What the unit publishes about it is the index space -- the
directory row, the record count and the lump's digest -- under a `dependencies` row naming the
root, never the records themselves.
"""

from __future__ import annotations

from dataclasses import dataclass
import hashlib
from typing import Any, Callable

import elysium_pipeline.formats.map_glb.partition as map_partition
from elysium_pipeline.formats.map_glb import source as map_source
from elysium_pipeline.formats.map_entities_glb import model as entity_model
from elysium_pipeline.formats.unit_contract import (
    Origin,
    SourceMember,
    origin_of,
    read_member,
)


class MapEntitiesSourceError(RuntimeError):
    """The requested map member is absent, or is not a BSP this seam can partition."""


@dataclass(frozen=True, slots=True)
class MapEntitiesSourceClosure:
    """One map's ENTITIES lump, the file it was cut from, and the brush-model index space."""

    key: str
    asset_id: str
    source_path: str
    data: bytes                                   # the whole BSP, for the span and lump 14
    origin: Origin
    partition: map_partition.Partition
    entities: SourceMember

    def members(self) -> tuple[SourceMember, ...]:
        return (self.entities,)

    @property
    def byte_length(self) -> int:
        return len(self.data)

    @property
    def sha256(self) -> str:
        """The whole BSP's digest, which pins the root unit this one shares a member with."""

        return hashlib.sha256(self.data).hexdigest()

    @property
    def map_revision(self) -> int:
        return self.partition.map_revision

    @property
    def lump_span(self) -> map_partition.LumpSpan:
        return self.partition.lump(entity_model.ENTITIES_LUMP)

    @property
    def models_span(self) -> map_partition.LumpSpan:
        return self.partition.lump(entity_model.MODELS_LUMP)

    def brush_models(self) -> dict[str, Any]:
        """The `*N` index space: the directory row, the record count and the lump's digest.

        The count is `filelen / 48`; a length the stride does not divide is reported as the
        remainder so a `*N` that names a partial record is still checkable.
        """

        span = self.models_span
        payload = self.data[span.offset:span.end] if span.populated else b""
        return {
            "lump": span.index,
            "offset": span.offset,
            "length": span.length,
            "stride": entity_model.MODEL_STRIDE,
            "count": span.length // entity_model.MODEL_STRIDE,
            "remainder": span.length % entity_model.MODEL_STRIDE,
            "sha256": hashlib.sha256(payload).hexdigest(),
        }


def source_keys(index: dict) -> list[str]:
    """Every map key the UP-first install index resolves, sorted.

    One map is one entities unit: lump 0 is populated on every map the engine loads, and a map
    whose lump was empty would still publish -- with an empty selecting member and a warning --
    rather than disappear from the family.
    """

    return map_source.source_keys(index)


def load_source_closure(
    index: dict,
    key: str,
    *,
    read_bytes: Callable[[dict, str], bytes | None] | None = None,
) -> MapEntitiesSourceClosure:
    """Resolve one map UP-first and cut lump 0 out of it."""

    normalized = entity_model.normalize_key(key)
    path = entity_model.source_path(normalized)
    entry = index.get(path)
    data = read_member(index, path, read_bytes=read_bytes) if entry else None
    if data is None:
        raise MapEntitiesSourceError(f"missing required map: {path}")
    try:
        proof = map_partition.partition(data)
    except map_partition.PartitionError as error:
        raise MapEntitiesSourceError(f"{path}: {error}") from error
    origin = origin_of(entry)
    span = proof.lump(entity_model.ENTITIES_LUMP)
    member = SourceMember(
        role="entities-lump",
        path=entity_model.member_path(normalized),
        data=data[span.offset:span.end] if span.populated else b"",
        origin=origin,
        span=(span.offset, span.length if span.populated else 0),
    )
    return MapEntitiesSourceClosure(
        key=normalized,
        asset_id=entity_model.asset_id(normalized),
        source_path=path,
        data=data,
        origin=origin,
        partition=proof,
        entities=member,
    )


__all__ = [
    "MapEntitiesSourceClosure",
    "MapEntitiesSourceError",
    "load_source_closure",
    "source_keys",
]
