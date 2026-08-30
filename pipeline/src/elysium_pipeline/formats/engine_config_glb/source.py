"""The engine-config member as the install resolves it: one file, one unit, no span."""

from __future__ import annotations

from dataclasses import dataclass
from typing import Callable

from elysium_pipeline.formats.engine_config_glb.model import (
    KNOWN_KEYS,
    asset_id,
    grammar_for,
    is_residue,
    normalize_key,
)
from elysium_pipeline.formats.unit_contract.origin import SourceMember, origin_of, read_member


class EngineConfigSourceError(RuntimeError):
    """The install carries no member for the requested engine-config key."""


@dataclass(frozen=True, slots=True)
class EngineConfigSourceClosure:
    key: str
    asset: str
    grammar: str
    residue: bool
    member: SourceMember

    def members(self) -> tuple[SourceMember, ...]:
        return (self.member,)


def source_keys(index: dict) -> list[str]:
    """Every engine-config key this seam's closed vocabulary resolves in `index`, sorted.

    There is no directory scan: the vocabulary is `model.KNOWN_KEYS`, and a key not present in
    the UP-first index (`cfg/joystick.cfg`, unshipped) is simply not enumerated.
    """

    return sorted(key for key in KNOWN_KEYS if key in index)


def load_source_closure(
    index: dict,
    key: str,
    *,
    read_bytes: Callable[[dict, str], bytes | None] | None = None,
) -> EngineConfigSourceClosure:
    normalized = normalize_key(key)
    entry = index.get(normalized)
    if entry is None:
        raise EngineConfigSourceError(f"missing required engine-config member: {normalized}")
    data = read_member(index, normalized, read_bytes=read_bytes)
    if data is None:
        raise EngineConfigSourceError(f"missing required engine-config member: {normalized}")
    origin = origin_of(entry)
    member = SourceMember(role="unit-selecting", path=normalized, data=data, origin=origin)
    return EngineConfigSourceClosure(
        key=normalized,
        asset=asset_id(normalized),
        grammar=grammar_for(normalized),
        residue=is_residue(normalized),
        member=member,
    )


def member_resolved(index: dict, source_path: str) -> bool:
    """Whether the UP-first index holds a member for one authored/derived path."""

    return normalize_key_loose(source_path) in index


def normalize_key_loose(path: str) -> str:
    return str(path).replace("\\", "/").strip().strip("/").lower()
