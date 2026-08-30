"""One particle definition as the install resolves it.

Unlike the surface-property table, a particle definition is already one file per unit: there is
no larger container to cut a span from, so the source closure is the whole member the install
holds for `particles/<key>.txt`.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Callable

from elysium_pipeline.formats.unit_contract import Origin, SourceMember, origin_of, read_member
from elysium_pipeline.formats.particle_glb.model import (
    asset_id,
    normalize_particle_key,
    source_path,
)


class ParticleSourceError(RuntimeError):
    """The install carries no member for the requested particle definition."""


@dataclass(frozen=True, slots=True)
class ParticleSourceClosure:
    """The one member a particle unit is decoded from."""

    key: str
    asset_id: str
    member: SourceMember

    def members(self) -> tuple[SourceMember, ...]:
        return (self.member,)


def load_source_closure(
    index: dict,
    key: str,
    *,
    read_bytes: Callable[[dict, str], bytes | None] | None = None,
) -> ParticleSourceClosure:
    """The source closure for one particle definition, UP-first through `index`.

    An empty (zero-byte) member is a valid, if degenerate, source: the contract records it with an
    `empty-member` omission rather than refusing it. A member the install does not carry at all is
    the one case this raises, because there is then no bytes for the unit to be cut from.
    """

    folded = normalize_particle_key(key)
    path = source_path(folded)
    entry = index.get(path)
    if entry is None:
        raise ParticleSourceError(f"missing particle definition: {path}")
    data = read_member(index, path, read_bytes=read_bytes)
    if data is None:
        raise ParticleSourceError(f"missing particle definition: {path}")
    origin: Origin = origin_of(entry)
    member = SourceMember(role="definition", path=path, data=data, origin=origin)
    return ParticleSourceClosure(key=folded, asset_id=asset_id(folded), member=member)
