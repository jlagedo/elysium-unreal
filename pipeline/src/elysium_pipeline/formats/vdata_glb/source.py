"""The vdata rulebook as the install resolves it: one `.txt` file, one unit.

A vdata unit is never cut from a shared table and never reads another vdata file (`seam_map_vdata.md`
Source closure), so the source closure is always the whole member, carries no `span`, and the
member's role is `unit-selecting` -- the one row the seam's own source-closure table names.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Callable

from elysium_pipeline.formats.unit_contract import SourceMember, origin_of, read_member
from elysium_pipeline.formats.vdata_glb.model import (
    SOURCE_ROOT,
    SOURCE_SUFFIX,
    asset_id,
    normalize_key,
    source_path,
    subtree_of,
    variant_of,
)


class VdataSourceError(RuntimeError):
    """The install carries no vdata member for the requested key."""


@dataclass(frozen=True, slots=True)
class VdataSourceClosure:
    """The one file the unit owns: no span, no sibling members."""

    key: str
    asset: str
    subtree: str
    variant: str
    member: SourceMember

    def members(self) -> tuple[SourceMember, ...]:
        return (self.member,)


def source_keys(index: dict) -> list[str]:
    """Every vdata key the UP-first install index resolves, folded and sorted.

    743 units resolve in the merged install across six subtrees (`seam_map_vdata.md`);
    `vdata/system/stealth.xls` is a design-source spreadsheet the engine never opens and is
    excluded by the `.txt` filter alone.
    """

    keys: set[str] = set()
    for entry in index:
        if entry.startswith(SOURCE_ROOT) and entry.endswith(SOURCE_SUFFIX):
            keys.add(entry[len(SOURCE_ROOT):-len(SOURCE_SUFFIX)])
    return sorted(keys)


def load_source_closure(
    index: dict,
    key: str,
    *,
    read_bytes: Callable[[dict, str], bytes | None] | None = None,
) -> VdataSourceClosure:
    normalized = normalize_key(key)
    path = source_path(normalized)
    entry = index.get(path)
    if entry is None:
        raise VdataSourceError(f"missing required vdata table: {path}")
    data = read_member(index, path, read_bytes=read_bytes)
    if data is None:
        raise VdataSourceError(f"missing required vdata table: {path}")
    origin = origin_of(entry)
    member = SourceMember(role="unit-selecting", path=path, data=data, origin=origin)
    return VdataSourceClosure(
        key=normalized,
        asset=asset_id(normalized),
        subtree=subtree_of(normalized),
        variant=variant_of(normalized),
        member=member,
    )


__all__ = [
    "VdataSourceClosure",
    "VdataSourceError",
    "load_source_closure",
    "source_keys",
]
