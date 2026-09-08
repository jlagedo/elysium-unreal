"""The ui-resource seam's members as the install resolves them: one file, one unit.

A ui-resource unit is never cut from a shared table and never reads another member: the source
closure is always the whole file, carries no `span`, and the member's role is `unit-selecting`.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Callable

from elysium_pipeline.formats.unit_contract import SourceMember, origin_of, read_member
from elysium_pipeline.formats.ui_resource_glb.model import (
    asset_id,
    classify,
    dormant_evidence,
    encoding_of,
    grammar_of,
    is_dormant,
    normalize_key,
)


class UiResourceSourceError(RuntimeError):
    """The install carries no ui-resource member for the requested key."""


@dataclass(frozen=True, slots=True)
class UiResourceSourceClosure:
    """The one file the unit owns: no span, no sibling members."""

    key: str
    asset: str
    category: str
    grammar: str
    encoding: str
    member: SourceMember
    dormant: bool = False
    dormant_evidence: str | None = None

    def members(self) -> tuple[SourceMember, ...]:
        return (self.member,)


def source_keys(index: dict) -> list[str]:
    """Every ui-resource key the UP-first install index resolves, folded and sorted."""

    keys: set[str] = set()
    for entry in index:
        if classify(entry) is not None:
            keys.add(entry)
    return sorted(keys)


def load_source_closure(
    index: dict,
    key: str,
    *,
    read_bytes: Callable[[dict, str], bytes | None] | None = None,
) -> UiResourceSourceClosure:
    normalized = normalize_key(key)
    category = classify(normalized)
    if category is None:
        raise UiResourceSourceError(f"{normalized} is not a ui-resource member")
    entry = index.get(normalized)
    if entry is None:
        raise UiResourceSourceError(f"missing required ui-resource member: {normalized}")
    data = read_member(index, normalized, read_bytes=read_bytes)
    if data is None:
        raise UiResourceSourceError(f"missing required ui-resource member: {normalized}")
    origin = origin_of(entry)
    member = SourceMember(role="unit-selecting", path=normalized, data=data, origin=origin)
    grammar = grammar_of(normalized)
    assert grammar is not None                           # `classify` already proved membership
    return UiResourceSourceClosure(
        key=normalized,
        asset=asset_id(normalized),
        category=category,
        grammar=grammar,
        encoding=encoding_of(normalized),
        member=member,
        dormant=is_dormant(normalized),
        dormant_evidence=dormant_evidence(normalized),
    )


__all__ = [
    "UiResourceSourceClosure",
    "UiResourceSourceError",
    "load_source_closure",
    "source_keys",
]
