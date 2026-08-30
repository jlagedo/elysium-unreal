"""The spans one map-lighting unit is cut from, resolved UP-first through the map root.

The BSP resolves as one member; the partition proof (`formats/map_glb/partition.py`) says which
of its bytes this unit owns -- lumps 8, 15, 32 and 34, and the `dplt` game-lump payload. Each
owned span becomes one `SourceMember`, so the unit's ledger is gapless over each span alone and
never states a byte the root or a sibling sub-unit is authoritative for.

The root's face (lump 7), texinfo (lump 6) and dispinfo (lump 26) tables are read here too and
are deliberately *not* members: their bytes belong to the map root, and this unit only restates
them as `derived` rows under a `dependencies` row naming the root.
"""

from __future__ import annotations

from dataclasses import dataclass
import hashlib
from typing import Callable

import elysium_pipeline.formats.map_glb.partition as map_partition
from elysium_pipeline.formats.map_glb import source as map_source
from elysium_pipeline.formats.map_lighting_glb import model as lighting_model
from elysium_pipeline.formats.unit_contract import (
    Origin,
    SourceMember,
    origin_of,
    read_member,
)


class MapLightingSourceError(RuntimeError):
    """The requested map member is absent, or is not a BSP this seam can partition."""


@dataclass(frozen=True, slots=True)
class MapLightingSourceClosure:
    """One map's lighting spans, the file they were cut from, and the root tables they need."""

    key: str
    asset_id: str
    source_path: str
    data: bytes                                   # the whole BSP: the spans and the root tables
    origin: Origin
    partition: map_partition.Partition
    lighting: SourceMember
    worldlights: SourceMember
    disp_alphas: SourceMember | None
    disp_sample_positions: SourceMember | None
    detail_prop_lighting: SourceMember | None
    game_lump: map_partition.GameLumpEntry | None

    def members(self) -> tuple[SourceMember, ...]:
        """Every member this unit publishes a ledger for, in lump order then the game lump."""

        ordered = [self.lighting, self.worldlights, self.disp_alphas, self.disp_sample_positions,
                   self.detail_prop_lighting]
        return tuple(member for member in ordered if member is not None)

    def lump(self, index: int) -> map_partition.LumpSpan:
        return self.partition.lump(index)

    def lump_bytes(self, index: int) -> bytes:
        """One root lump's bytes, for a table this unit restates without owning it."""

        span = self.partition.lump(index)
        if not span.populated:
            return b""
        return self.data[span.offset:span.end]

    @property
    def source_sha256(self) -> str:
        return hashlib.sha256(self.data).hexdigest()

    def spans_agree_with_root(self) -> None:
        """The members are exactly the spans the root's partition hands this unit.

        The root publishes the same partition from the other side, so a member the partition does
        not name -- or a span no member covers -- would leave a byte with two owners or none.
        """

        published = {
            (member.span[0], member.span[1])
            for member in self.members()
            if member.span is not None and member.span[1]
        }
        owned = {
            (span.offset, span.length)
            for span in self.partition.spans_for(map_partition.LIGHTING_UNIT)
        }
        if published != owned:
            raise MapLightingSourceError(
                f"{self.source_path}: lighting members {sorted(published)} are not the "
                f"partition's lighting spans {sorted(owned)}"
            )


def source_keys(index: dict) -> list[str]:
    """Every map key the UP-first install index resolves, sorted.

    One map is one lighting unit: lump 8 is populated on all 108 retail maps, and a map that
    carried none would still publish -- with an empty selecting member and a warning -- rather
    than disappear from the family.
    """

    return map_source.source_keys(index)


def _lump_member(
    key: str, data: bytes, span: map_partition.LumpSpan, origin: Origin
) -> SourceMember:
    payload = data[span.offset:span.end] if span.populated else b""
    return SourceMember(
        role=lighting_model.MEMBER_ROLES[span.index],
        path=lighting_model.member_path(key, span.index),
        data=payload,
        origin=origin,
        span=(span.offset, len(payload)),
    )


def load_source_closure(
    index: dict,
    key: str,
    *,
    read_bytes: Callable[[dict, str], bytes | None] | None = None,
) -> MapLightingSourceClosure:
    """Resolve one map UP-first and cut the five lighting spans out of its partition."""

    normalized = lighting_model.normalize_key(key)
    path = lighting_model.source_path(normalized)
    entry = index.get(path)
    data = read_member(index, path, read_bytes=read_bytes) if entry else None
    if data is None:
        raise MapLightingSourceError(f"missing required map: {path}")
    try:
        proof = map_partition.partition(data)
    except map_partition.PartitionError as error:
        raise MapLightingSourceError(f"{path}: {error}") from error
    origin = origin_of(entry)
    lighting = _lump_member(normalized, data, proof.lump(lighting_model.LIGHTING_LUMP), origin)
    worldlights = _lump_member(
        normalized, data, proof.lump(lighting_model.WORLDLIGHTS_LUMP), origin
    )
    disp_alphas = disp_samples = None
    if proof.lump(lighting_model.DISP_ALPHA_LUMP).populated:
        disp_alphas = _lump_member(
            normalized, data, proof.lump(lighting_model.DISP_ALPHA_LUMP), origin
        )
    if proof.lump(lighting_model.DISP_SAMPLE_LUMP).populated:
        disp_samples = _lump_member(
            normalized, data, proof.lump(lighting_model.DISP_SAMPLE_LUMP), origin
        )
    entry_row = proof.game_lump(lighting_model.DETAIL_PROP_LIGHTING_ID)
    detail = None
    if entry_row is not None:
        detail = SourceMember(
            role=lighting_model.DETAIL_PROP_LIGHTING_ROLE,
            path=lighting_model.game_lump_member_path(normalized, entry_row.id),
            data=data[entry_row.offset:entry_row.end],
            origin=origin,
            span=(entry_row.offset, entry_row.length),
        )
    closure = MapLightingSourceClosure(
        key=normalized,
        asset_id=lighting_model.asset_id(normalized),
        source_path=path,
        data=data,
        origin=origin,
        partition=proof,
        lighting=lighting,
        worldlights=worldlights,
        disp_alphas=disp_alphas,
        disp_sample_positions=disp_samples,
        detail_prop_lighting=detail,
        game_lump=entry_row,
    )
    closure.spans_agree_with_root()
    return closure
