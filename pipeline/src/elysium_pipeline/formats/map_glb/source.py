"""The one member a map root is decoded from, resolved UP-first.

A map's four units share one member: `maps/<map>.bsp`. The root's ledger runs over the whole
file, because the root is the unit that proves the partition -- the three sub-units' spans are
`omitted-proven` ranges in it, naming the unit that owns them.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Any, Callable

import elysium_pipeline.formats.map_glb.partition as map_partition
from elysium_pipeline.formats.map_glb import model as map_model
from elysium_pipeline.formats.unit_contract import (
    Origin,
    SourceMember,
    origin_of,
    read_member,
)


class MapSourceError(RuntimeError):
    """The requested map member is absent, or is not a BSP this seam decodes."""


@dataclass(frozen=True, slots=True)
class MapSourceClosure:
    """One map's BSP, its partition proof and the identities the four units are named by."""

    key: str
    asset_id: str
    bsp: SourceMember
    partition: map_partition.Partition

    def members(self) -> tuple[SourceMember, ...]:
        return (self.bsp,)

    @property
    def origin(self) -> Origin:
        return self.bsp.origin

    def sub_unit_rows(self) -> list[dict[str, Any]]:
        """The three siblings: identity, published path, owned spans and their digest.

        A sibling's own file does not exist when the root is written, so what the root pins is
        what it can prove -- the source spans the sibling is cut from and their SHA-256.
        """

        rows: list[dict[str, Any]] = []
        for kind, suffix in map_model.SUB_UNITS:
            spans = self.partition.spans_for(kind)
            rows.append(
                {
                    "asset": map_model.sub_asset_id(kind, self.key),
                    "path": map_model.sub_output_relative_path(suffix, self.key).as_posix(),
                    "lumps": sorted({span.lump for span in spans}),
                    "spans": [span.to_json() for span in spans],
                    "byteLength": sum(span.length for span in spans),
                    "sourceSha256": self.partition.sha256_for(kind, self.bsp.data),
                }
            )
        return rows


def source_keys(index: dict) -> list[str]:
    """Every map key the UP-first install index resolves, sorted.

    Only the maps directly below `maps/` are units: the engine writes its AI node graphs and
    sound caches into subdirectories there while it runs, and nothing in this seam reads them.
    """

    keys: set[str] = set()
    for entry in index:
        if not entry.startswith(map_model.MAPS_ROOT + "/") or not entry.endswith(".bsp"):
            continue
        stem = entry[len(map_model.MAPS_ROOT) + 1:]
        if "/" in stem:
            continue
        try:
            keys.add(map_model.normalize_key(stem))
        except map_model.MapKeyError:
            continue
    return sorted(keys)


def load_source_closure(
    index: dict,
    key: str,
    *,
    read_bytes: Callable[[dict, str], bytes | None] | None = None,
) -> MapSourceClosure:
    """Resolve one map member UP-first and prove its 64-lump directory partitions the file."""

    normalized = map_model.normalize_key(key)
    path = map_model.source_path(normalized)
    entry = index.get(path)
    data = read_member(index, path, read_bytes=read_bytes) if entry else None
    if data is None:
        raise MapSourceError(f"missing required map: {path}")
    try:
        proof = map_partition.partition(data)
    except map_partition.PartitionError as error:
        raise MapSourceError(f"{path}: {error}") from error
    member = SourceMember(role="bsp", path=path, data=data, origin=origin_of(entry))
    return MapSourceClosure(
        key=normalized,
        asset_id=map_model.asset_id(normalized),
        bsp=member,
        partition=proof,
    )
