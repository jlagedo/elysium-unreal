"""Patch-first source closure for one VtMB texture identity."""

from __future__ import annotations

from dataclasses import dataclass
import hashlib
import os
from pathlib import Path
from typing import Callable

from elysium_pipeline.formats.texture_glb.model import SourceIdentity, asset_id, normalize_texture_path


class TextureSourceError(RuntimeError):
    """The selected texture source is absent or incoherent."""


@dataclass(frozen=True, slots=True)
class SourceMember:
    role: str
    path: str
    data: bytes
    origin: dict[str, object]

    def identity(self) -> SourceIdentity:
        return SourceIdentity(
            role=self.role,
            path=self.path,
            origin=self.origin,
            byte_length=len(self.data),
            sha256=hashlib.sha256(self.data).hexdigest(),
        )


@dataclass(frozen=True, slots=True)
class TextureSourceClosure:
    texture_path: str
    asset_id: str
    tth: SourceMember
    ttz: SourceMember | None

    def members(self) -> tuple[SourceMember, ...]:
        return (self.tth,) if self.ttz is None else (self.tth, self.ttz)


def source_keys(index: dict) -> list[str]:
    """Every texture identity the install holds, in key order.

    The engine composes `materials/<texture>.tth`, so the `.tth` members are the seam's corpus.
    This is the one selection rule: the plural export command and the corpus index's member
    dispositions both call it, so neither can drift from the other.
    """

    prefix, suffix = "materials/", ".tth"
    return sorted(
        path[len(prefix):-len(suffix)]
        for path in index
        if path.startswith(prefix) and path.endswith(suffix)
    )


def _origin(entry) -> dict[str, object]:
    kind, value = entry
    if kind == "loose":
        path = Path(value)
        lowered = [part.lower() for part in path.parts]
        root = "loose"
        for candidate in ("unofficial_patch", "vampire"):
            if candidate in lowered:
                root = path.parts[lowered.index(candidate)]
                break
        return {"kind": "loose", "root": root}
    pack, offset, size = value
    return {
        "kind": "vpk",
        "container": os.path.basename(pack),
        "offset": int(offset),
        "size": int(size),
    }


def _member(index, key, role, read_bytes, *, required=False):
    entry = index.get(key)
    data = read_bytes(index, key) if entry else None
    if data is None:
        if required:
            raise TextureSourceError(f"missing required {role}: {key}")
        return None
    return SourceMember(role, key, data, _origin(entry))


def load_source_closure(
    index: dict,
    texture_path: str,
    *,
    read_bytes: Callable[[dict, str], bytes | None] | None = None,
) -> TextureSourceClosure:
    if read_bytes is None:
        from elysium_pipeline.formats import install

        read_bytes = install.read
    normalized = normalize_texture_path(texture_path)
    base = f"materials/{normalized}"
    tth = _member(index, base + ".tth", "tth", read_bytes, required=True)
    ttz = _member(index, base + ".ttz", "ttz", read_bytes)
    return TextureSourceClosure(normalized, asset_id(normalized), tth, ttz)
