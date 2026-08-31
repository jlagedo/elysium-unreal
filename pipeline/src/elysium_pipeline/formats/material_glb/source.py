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
from elysium_pipeline.formats.unit_contract.origin import pakfile_origin


class MaterialSourceError(RuntimeError):
    """The selected material source is absent or incoherent."""


def source_keys(
    index: dict,
    *,
    read_bytes: Callable[[dict, str], bytes | None] | None = None,
) -> list[str]:
    """Every VMT identity the engine can address, in key order.

    The engine composes `materials/<search path><name>.vmt`, so a VMT packed outside `materials/`
    names no material and is not a unit. This is the one selection rule: the plural export
    command and the corpus index's member dispositions both call it, so neither can drift from
    the other. Every map's PAKFILE lump adds its own patched-material copies -- cubemap-patched
    duplicates of a base material, one per baked probe that lit it -- under
    `maps/<map>/<mat>_<x>_<y>_<z>`; the install carries no `materials/maps/**` member today, so
    these keys are never install duplicates. `read_bytes` is the same test injection point every
    seam's closure loader takes.
    """

    from elysium_pipeline.formats.map_glb.pakfile_index import pakfile_members

    prefix, suffix = "materials/", ".vmt"
    keys = {
        path[len(prefix):-len(suffix)]
        for path in index
        if path.startswith(prefix) and path.endswith(suffix)
    }
    for members in pakfile_members(index, read_bytes=read_bytes).values():
        for member in members:
            name = member.name.replace("\\", "/").lower()
            if not name.endswith(suffix):
                continue
            stem = name[len(prefix):] if name.startswith(prefix) else name
            keys.add(stem[:-len(suffix)])
    return sorted(keys)


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


def _pakfile_vmt(
    index: dict,
    map_name: str,
    stem: str,
    read_bytes: Callable[[dict, str], bytes | None] | None,
) -> SourceMember:
    from elysium_pipeline.formats.map_glb.pakfile_index import (
        bsp_origin,
        pakfile_member_bytes,
        pakfile_members,
    )

    member_name = f"materials/maps/{map_name}/{stem}.vmt"
    members = pakfile_members(index, read_bytes=read_bytes).get(map_name, ())
    member = next(
        (candidate for candidate in members if candidate.name.lower() == member_name.lower()),
        None,
    )
    if member is None:
        raise MaterialSourceError(f"missing required vmt: {member_name}")
    data = pakfile_member_bytes(index, map_name, member.name, read_bytes=read_bytes)
    origin = pakfile_origin(
        map_name, member.name, bsp_origin(index, map_name, read_bytes=read_bytes)
    ).to_json()
    return SourceMember("vmt", member.name, data, origin)


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
    if normalized.startswith("maps/"):
        # An install member under `materials/maps/**` wins over the PAKFILE patched copy of the
        # same spelling (`source_keys`'s dedup rule); today the install carries none, so this is
        # only ever exercised by a synthetic install, but a real one that started shipping one
        # must not silently keep routing to the BSP copy.
        install_key = f"materials/{normalized}.vmt"
        entry = index.get(install_key)
        if entry is not None:
            data = read_bytes(index, install_key)
            if data is not None:
                member = SourceMember("vmt", install_key, data, origin_of(entry))
                return MaterialSourceClosure(normalized, asset_id(normalized), member)
        map_name, _, stem = normalized[len("maps/"):].partition("/")
        if not stem:
            raise MaterialSourceError(
                f"{normalized!r} names no map-relative stem to resolve in a PAKFILE"
            )
        member = _pakfile_vmt(index, map_name, stem, read_bytes)
        return MaterialSourceClosure(normalized, asset_id(normalized), member)
    key = f"materials/{normalized}.vmt"
    entry = index.get(key)
    data = read_bytes(index, key) if entry else None
    if data is None:
        raise MaterialSourceError(f"missing required vmt: {key}")
    member = SourceMember("vmt", key, data, origin_of(entry))
    return MaterialSourceClosure(normalized, asset_id(normalized), member)
