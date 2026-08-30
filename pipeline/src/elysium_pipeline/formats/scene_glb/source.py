"""The install's UP-first resolution of one choreographed-scene `.vcd` member.

Unlike the surface-property table, a scene is never cut from a shared file: each `.vcd` below
`sound/` is its own complete, independently resolved member, so the closure here is just that one
member, wrapped with the identity the rest of the seam needs.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Callable

from elysium_pipeline.formats.scene_glb.model import asset_id, normalize_scene_key, source_path_for
from elysium_pipeline.formats.unit_contract import SourceMember, origin_of


class SceneSourceError(RuntimeError):
    """The install carries no `.vcd` member for the requested scene key."""


@dataclass(frozen=True, slots=True)
class SceneSourceClosure:
    """The one `.vcd` member a scene unit is decoded from."""

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
) -> SceneSourceClosure:
    """Resolve one scene by key: the root prefix and `.vcd` extension are both tolerated."""

    if read_bytes is None:
        from elysium_pipeline.formats import install

        read_bytes = install.read
    normalized = normalize_scene_key(key)
    source_path = source_path_for(normalized)
    entry = index.get(source_path)
    if entry is None:
        raise SceneSourceError(f"missing required scene: {source_path}")
    data = read_bytes(index, source_path)
    if data is None:
        raise SceneSourceError(f"missing required scene: {source_path}")
    member = SourceMember(role="vcd", path=source_path, data=data, origin=origin_of(entry))
    return SceneSourceClosure(normalized, asset_id(normalized), member)
