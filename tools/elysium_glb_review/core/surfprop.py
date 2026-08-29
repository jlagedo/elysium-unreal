"""Surface-property inheritance.

A surface-property unit publishes only what its own table entry declares. Eighteen of
the sixty-three declare no physics at all and are meaningless read alone -- brick sets
nothing but a base of concrete. Resolving that chain spans units, so it is a consumer's
job rather than something one unit can be validated against; this is that consumer.

None of it is visually representable. Friction, elasticity, footstep and impact sounds
are gameplay and audio data, so the tool shows them and stops there.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path

from . import glb, ids, seams

#: Sections merged by key, nearest declaration winning.
MERGED_SECTIONS = ("physics", "movement", "footsteps", "impacts", "sounds")

#: Scalars taken from the nearest entry that declares them.
INHERITED_SCALARS = ("gameMaterial",)


@dataclass
class Resolved:
    """A surface property with its inheritance chain applied."""

    identity: str
    #: Identities walked, nearest first, starting with the unit itself.
    chain: tuple[str, ...] = ()
    #: Section key -> value, with the identity that supplied it.
    values: dict = field(default_factory=dict)
    #: Where each key came from, so a panel can show inherited values as inherited.
    origins: dict = field(default_factory=dict)
    #: A base that names a unit with no file. The seam treats this as a hard failure.
    broken_base: str | None = None
    #: True when the walk stopped on a cycle rather than a root.
    cyclic: bool = False

    def is_inherited(self, section: str, key: str) -> bool:
        return self.origins.get((section, key)) not in (None, self.identity)


def _merge(resolved: Resolved, identity: str, payload: dict) -> None:
    """Fold one entry into the accumulator without overwriting a nearer declaration."""
    for section in MERGED_SECTIONS:
        block = payload.get(section)
        if not isinstance(block, dict):
            continue
        target = resolved.values.setdefault(section, {})
        for key, value in block.items():
            if key not in target:
                target[key] = value
                resolved.origins[(section, key)] = identity

    for key in INHERITED_SCALARS:
        value = payload.get(key)
        if value is not None and key not in resolved.values:
            resolved.values[key] = value
            resolved.origins[(key, key)] = identity


def resolve(document: dict, root: str | Path, *, max_depth: int = 16) -> Resolved:
    """Apply the base chain of a loaded surface-property document."""
    root = Path(root)
    payload = seams.root_extension(document, seams.SURFACE_PROPERTY_EXTENSION) or {}
    identity = seams.asset_id(document) or "<unknown>"

    resolved = Resolved(identity=identity)
    chain: list[str] = []
    seen: set[str] = set()
    current_id, current = identity, payload

    for _ in range(max_depth):
        if current_id in seen:
            resolved.cyclic = True
            break
        seen.add(current_id)
        chain.append(current_id)
        _merge(resolved, current_id, current)

        base = current.get("base") or {}
        base_id = base.get("asset")
        if not base_id:
            break

        path = ids.resolve(base_id, root)
        if path is None or not path.is_file():
            resolved.broken_base = base_id
            break
        base_document = glb.read_json(path)
        current = seams.root_extension(base_document, seams.SURFACE_PROPERTY_EXTENSION) or {}
        current_id = base_id

    resolved.chain = tuple(chain)
    return resolved
