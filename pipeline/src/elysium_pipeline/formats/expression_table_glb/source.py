"""Patch-first source closure for one VtMB expression-table identity.

Both members resolve UP-first, independently of each other: a stem may ship only the compiled
`.vfe`, only the readable `.txt`, or both. Neither member is required for the closure to exist --
the closure itself refuses only when the install holds neither.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Callable

from elysium_pipeline.formats.expression_table_glb.model import (
    SOURCE_DIRECTORY,
    normalize_stem,
    stem_asset_id,
)
from elysium_pipeline.formats.unit_contract import SourceMember, origin_of


class ExpressionTableSourceError(RuntimeError):
    """The selected expression-table source is absent or incoherent."""


@dataclass(frozen=True, slots=True)
class ExpressionTableSourceClosure:
    """The one stem's members the unit owns: the compiled VFE, the readable TXT, or both."""

    stem: str
    asset: str
    vfe: SourceMember | None
    txt: SourceMember | None

    def members(self) -> tuple[SourceMember, ...]:
        result: list[SourceMember] = []
        if self.vfe is not None:
            result.append(self.vfe)
        if self.txt is not None:
            result.append(self.txt)
        return tuple(result)


def _member(index: dict, key: str, role: str, read_bytes) -> SourceMember | None:
    entry = index.get(key)
    data = read_bytes(index, key) if entry else None
    if data is None:
        return None
    return SourceMember(role=role, path=key, data=data, origin=origin_of(entry))


def load_source_closure(
    index: dict,
    stem: str,
    *,
    read_bytes: Callable[[dict, str], bytes | None] | None = None,
) -> ExpressionTableSourceClosure:
    if read_bytes is None:
        from elysium_pipeline.formats import install

        read_bytes = install.read
    normalized = normalize_stem(stem)
    base = f"{SOURCE_DIRECTORY}/{normalized}"
    vfe = _member(index, base + ".vfe", "vfe", read_bytes)
    txt = _member(index, base + ".txt", "txt", read_bytes)
    if vfe is None and txt is None:
        raise ExpressionTableSourceError(f"no expression table for stem {normalized!r}")
    return ExpressionTableSourceClosure(normalized, stem_asset_id(normalized), vfe, txt)


def source_keys(index: dict) -> list[str]:
    """Every stem the UP-first install resolves under `expressions/`, VFE or TXT or both."""

    stems: set[str] = set()
    prefix = f"{SOURCE_DIRECTORY}/"
    for key in index:
        lowered = str(key).replace("\\", "/").lower()
        if not lowered.startswith(prefix):
            continue
        if lowered.endswith(".vfe") or lowered.endswith(".txt"):
            stems.add(normalize_stem(lowered))
    return sorted(stems)
