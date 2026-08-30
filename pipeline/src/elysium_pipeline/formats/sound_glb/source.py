"""UP-first source closure for one VtMB sound identity.

The audio member selects the unit; the same-stem `.lip` resolves UP-first on its own, so the
patch's 7,121 loose caption documents sit beside whichever waveform the search path found. A
`.lip` beside a stem that ships as both `.wav` and `.mp3` is a member of both units.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Callable

from elysium_pipeline.formats.sound_glb.model import (
    AUDIO_EXTENSIONS,
    SOUND_ROOT,
    asset_id,
    companion_path,
    counterpart_key,
    key_extension,
    normalize_key,
    source_path,
)
from elysium_pipeline.formats.unit_contract.origin import (
    Origin,
    SourceMember,
    origin_of,
    read_member,
)


class SoundSourceError(RuntimeError):
    """The selected audio member is absent from the install."""


@dataclass(frozen=True, slots=True)
class SoundSourceClosure:
    """The members one sound unit is decoded from, and the mp3-first pair it belongs to.

    The counterpart member is never read: its bytes belong to its own unit, and the unit here only
    states which of the pair retail plays so a reader can see the join without following it.
    """

    key: str
    asset_id: str
    audio: SourceMember
    lip: SourceMember | None
    counterpart: str | None

    def members(self) -> tuple[SourceMember, ...]:
        return (self.audio,) if self.lip is None else (self.audio, self.lip)


def _read(
    index: dict,
    key: str,
    role: str,
    read_bytes: Callable[[dict, str], bytes | None] | None,
    *,
    required: bool,
) -> SourceMember | None:
    entry = index.get(key)
    data = read_member(index, key, read_bytes=read_bytes) if entry is not None else None
    if data is None:
        if required:
            raise SoundSourceError(f"missing required {role}: {key}")
        return None
    origin: Origin = origin_of(entry)
    return SourceMember(role=role, path=key, data=bytes(data), origin=origin)


def load_source_closure(
    index: dict,
    key: str,
    *,
    read_bytes: Callable[[dict, str], bytes | None] | None = None,
) -> SoundSourceClosure:
    """Resolve one sound identity: its audio member, its optional `.lip`, and its pair."""

    normalized = normalize_key(key)
    audio = _read(
        index,
        source_path(normalized),
        key_extension(normalized).lstrip("."),
        read_bytes,
        required=True,
    )
    lip = _read(index, companion_path(normalized), "lip", read_bytes, required=False)
    other = counterpart_key(normalized)
    counterpart = other if (SOUND_ROOT + other) in index else None
    return SoundSourceClosure(normalized, asset_id(normalized), audio, lip, counterpart)


def source_keys(index: dict) -> list[str]:
    """Every sound unit key the install index resolves, extension kept.

    `sound/**/*.sfk` and `*.pk` are Sound Forge peak caches and `*.vcd`, `*.txt` and the loose
    `.lip` orphans belong to other seams, so only the two audio spellings select a unit here.
    """

    return sorted(
        path[len(SOUND_ROOT):]
        for path in index
        if path.startswith(SOUND_ROOT) and path.endswith(AUDIO_EXTENSIONS)
    )
