"""Patch-first direct-source closure for one character GLB."""

from __future__ import annotations

from dataclasses import dataclass
import hashlib
import os
from pathlib import Path
import struct
from typing import Callable

from elysium_pipeline.formats.character_glb.model import SourceIdentity, asset_id


class CharacterSourceError(RuntimeError):
    """The requested character source closure is absent or internally inconsistent."""


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
class CharacterSourceClosure:
    model_path: str
    asset_id: str
    mdl: SourceMember
    primary_vtx: SourceMember
    alternate_vtx: SourceMember | None
    phy: SourceMember | None
    facial: tuple[SourceMember, ...]

    def members(self) -> tuple[SourceMember, ...]:
        optional = tuple(member for member in (self.alternate_vtx, self.phy) if member)
        return (self.mdl, self.primary_vtx, *optional, *self.facial)


def normalize_model_path(model_path: str) -> str:
    normalized = model_path.replace("\\", "/").strip("/").lower()
    if not normalized.endswith(".mdl"):
        normalized += ".mdl"
    asset_id(normalized)
    return normalized


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


def _member(
    index: dict,
    path: str,
    role: str,
    read_bytes: Callable[[dict, str], bytes | None],
    *,
    required: bool = False,
) -> SourceMember | None:
    key = path.lower().replace("\\", "/")
    entry = index.get(key)
    data = read_bytes(index, key) if entry else None
    if data is None:
        if required:
            raise CharacterSourceError(f"missing required {role}: {key}")
        return None
    return SourceMember(role=role, path=key, data=data, origin=_origin(entry))


def _checksum(data: bytes, offset: int, label: str) -> int:
    if len(data) < offset + 4:
        raise CharacterSourceError(f"{label} is shorter than checksum field at {offset}")
    return struct.unpack_from("<I", data, offset)[0]


def load_source_closure(
    index: dict,
    model_path: str,
    *,
    read_bytes: Callable[[dict, str], bytes | None] | None = None,
) -> CharacterSourceClosure:
    """Resolve every direct member independently through the supplied patch-first index."""
    if read_bytes is None:
        from elysium_pipeline.formats import install

        read_bytes = install.read

    normalized = normalize_model_path(model_path)
    stem = normalized[:-4]
    mdl = _member(index, normalized, "mdl", read_bytes, required=True)
    dx80 = _member(index, stem + ".dx80.vtx", "vtx-dx80", read_bytes)
    dx7 = _member(index, stem + ".dx7_2bone.vtx", "vtx-dx7-2bone", read_bytes)
    if dx80 is None and dx7 is None:
        raise CharacterSourceError(
            f"missing required topology: {stem}.dx80.vtx or {stem}.dx7_2bone.vtx"
        )
    primary = dx80 or dx7
    alternate = dx7 if dx80 is not None else None
    phy = _member(index, stem + ".phy", "phy", read_bytes)

    basename = stem.rsplit("/", 1)[-1]
    facial = []
    for family in ("expressions", "phonemes"):
        family_members = []
        for suffix in ("txt", "vfe"):
            found = _member(
                index,
                f"expressions/{basename}_{family}.{suffix}",
                f"facial-{family}-{suffix}",
                read_bytes,
            )
            if found:
                family_members.append(found)
        facial.extend(family_members)

    mdl_checksum = _checksum(mdl.data, 8, mdl.path)
    for member in (primary, alternate):
        if member is None:
            continue
        value = _checksum(member.data, 16, member.path)
        if value != mdl_checksum:
            raise CharacterSourceError(
                f"checksum mismatch: {mdl.path}=0x{mdl_checksum:08x}, "
                f"{member.path}=0x{value:08x}"
            )
    if phy is not None:
        # The legacy PHY header retains its compiler checksum as provenance, but the
        # retail VCollideLoad seam receives only solidCount and the bytes after this
        # header.  Keep the companion and export both stored values; unlike VTX, a
        # PHY mismatch is not a runtime admission failure.
        _checksum(phy.data, 12, phy.path)

    return CharacterSourceClosure(
        model_path=normalized,
        asset_id=asset_id(normalized),
        mdl=mdl,
        primary_vtx=primary,
        alternate_vtx=alternate,
        phy=phy,
        facial=tuple(facial),
    )
