"""Patch-first source closure for one VtMB material identity."""

from __future__ import annotations

from dataclasses import dataclass
import hashlib
import os
from pathlib import Path
from typing import Callable

from elysium_pipeline.formats.material_glb.model import (
    SourceIdentity,
    asset_id,
    normalize_material_path,
)


class MaterialSourceError(RuntimeError):
    """The selected material source is absent or incoherent."""


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
class MaterialSourceClosure:
    """The one VMT the unit owns.

    An included VMT stays a hashed dependency rather than a member: its bytes belong to its own
    material product, and duplicating them here would make two units authoritative for one file.
    """

    material_path: str
    asset_id: str
    vmt: SourceMember

    def members(self) -> tuple[SourceMember, ...]:
        return (self.vmt,)


def origin_of(entry) -> dict[str, object]:
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


def load_source_closure(
    index: dict,
    material_path: str,
    *,
    read_bytes: Callable[[dict, str], bytes | None] | None = None,
) -> MaterialSourceClosure:
    if read_bytes is None:
        from elysium_pipeline.formats import install

        read_bytes = install.read
    normalized = normalize_material_path(material_path)
    key = f"materials/{normalized}.vmt"
    entry = index.get(key)
    data = read_bytes(index, key) if entry else None
    if data is None:
        raise MaterialSourceError(f"missing required vmt: {key}")
    member = SourceMember("vmt", key, data, origin_of(entry))
    return MaterialSourceClosure(normalized, asset_id(normalized), member)
