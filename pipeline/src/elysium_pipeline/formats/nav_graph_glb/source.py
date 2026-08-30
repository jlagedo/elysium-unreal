"""The `.ain`/`.loc` pair as the install resolves it, for one map's nav graph."""

from __future__ import annotations

from dataclasses import dataclass
from typing import Callable

from elysium_pipeline.formats.unit_contract import Origin, SourceMember, origin_of
from elysium_pipeline.formats.nav_graph_glb.model import asset_id, normalize_key

FAMILY_DIR = "maps/graphs"


class NavGraphSourceError(RuntimeError):
    """The nav-graph's `.ain` is absent or the install carries no member for the key."""


def ain_path(key: str) -> str:
    return f"{FAMILY_DIR}/{normalize_key(key)}.ain"


def loc_path(key: str) -> str:
    return f"{FAMILY_DIR}/{normalize_key(key)}.loc"


def bsp_path(key: str) -> str:
    return f"maps/{normalize_key(key)}.bsp"


@dataclass(frozen=True, slots=True)
class NavGraphSourceClosure:
    """The one `.ain` (required) and `.loc` (optional) member this unit was decoded from.

    `map_resolved` records whether the install carries the map's own `.bsp`, which is what the
    `map` and `map-entities` dependency rows warn against rather than fail on when it is absent.
    """

    key: str
    asset_id: str
    ain: SourceMember
    loc: SourceMember | None
    map_resolved: bool

    def members(self) -> tuple[SourceMember, ...]:
        return (self.ain,) if self.loc is None else (self.ain, self.loc)


def _read(index: dict, key: str, read_bytes) -> tuple[bytes, Origin] | None:
    entry = index.get(key)
    if entry is None:
        return None
    data = read_bytes(index, key)
    if data is None:
        return None
    return data, origin_of(entry)


def load_source_closure(
    index: dict,
    key: str,
    *,
    read_bytes: Callable[[dict, str], bytes | None] | None = None,
) -> NavGraphSourceClosure:
    """Resolve one map's `.ain` and `.loc` UP-first through `index`.

    The `.ain` selects the unit and is required; a missing `.loc` is not an error here -- the
    decoder records `omissions[] missing-loc` for it -- because the seam names it an optional
    companion.
    """

    if read_bytes is None:
        from elysium_pipeline.formats import install

        read_bytes = install.read

    folded = normalize_key(key)
    ain_key = ain_path(folded)
    resolved = _read(index, ain_key, read_bytes)
    if resolved is None:
        raise NavGraphSourceError(f"missing required nav-graph member: {ain_key}")
    ain_data, ain_origin = resolved
    ain_member = SourceMember(role="ain", path=ain_key, data=ain_data, origin=ain_origin)

    loc_member: SourceMember | None = None
    loc_key = loc_path(folded)
    loc_resolved = _read(index, loc_key, read_bytes)
    if loc_resolved is not None:
        loc_data, loc_origin = loc_resolved
        loc_member = SourceMember(role="loc", path=loc_key, data=loc_data, origin=loc_origin)

    map_resolved = bsp_path(folded) in index

    return NavGraphSourceClosure(
        key=folded,
        asset_id=asset_id(folded),
        ain=ain_member,
        loc=loc_member,
        map_resolved=map_resolved,
    )
