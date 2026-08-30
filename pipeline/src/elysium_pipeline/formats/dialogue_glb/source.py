"""The `.dlg` member as the UP-first install resolves it, plus the audio-join predicate."""

from __future__ import annotations

from dataclasses import dataclass
from typing import Callable

from elysium_pipeline.formats.dialogue_glb.model import asset_id, normalize_dlg_key, source_path_for
from elysium_pipeline.formats.unit_contract import SourceMember, origin_of, read_member


class DialogueSourceError(RuntimeError):
    """The selected `.dlg` member is absent."""


@dataclass(frozen=True, slots=True)
class DialogueSourceClosure:
    """The one `.dlg` member the unit owns, plus a resolver for its sound/scene join."""

    key: str
    asset: str
    member: SourceMember
    resolved: Callable[[str], bool]

    def members(self) -> tuple[SourceMember, ...]:
        return (self.member,)


def load_source_closure(
    index: dict,
    key: str,
    *,
    read_bytes: Callable[[dict, str], bytes | None] | None = None,
) -> DialogueSourceClosure:
    """Resolve one dialogue unit's `.dlg` member UP-first and bind its audio-join predicate.

    `resolved` answers whether the install (the same index this closure was built from) holds a
    member for an install-relative path -- a plain membership test, since the audio join only
    needs to know a candidate exists, never its bytes.
    """

    folded = normalize_dlg_key(key)
    path = source_path_for(folded)
    entry = index.get(path)
    if entry is None:
        raise DialogueSourceError(f"missing required dialogue file: {path}")
    data = read_member(index, path, read_bytes=read_bytes)
    if data is None:
        raise DialogueSourceError(f"missing required dialogue file: {path}")
    member = SourceMember(role="dlg", path=path, data=data, origin=origin_of(entry))

    def resolved(candidate_path: str) -> bool:
        return candidate_path.lower().replace("\\", "/") in index

    return DialogueSourceClosure(key=folded, asset=asset_id(folded), member=member, resolved=resolved)


def source_keys(index: dict) -> list[str]:
    """Every dialogue unit key the UP-first install index resolves, sorted."""

    keys = set()
    for path in index:
        if path.startswith("dlg/") and path.endswith(".dlg"):
            keys.add(normalize_dlg_key(path))
    return sorted(keys)
