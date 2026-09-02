"""Patch-first source closure for one VtMB texture identity."""

from __future__ import annotations

from dataclasses import dataclass
import hashlib
import os
from pathlib import Path
from typing import Callable

from elysium_pipeline.formats.texture_glb.model import (
    SPRITE_FAMILY,
    SPRITE_SUFFIX,
    SourceIdentity,
    asset_id,
    is_sprite_key,
    normalize_texture_path,
)
from elysium_pipeline.formats.unit_contract.origin import pakfile_origin


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


def source_keys(
    index: dict,
    *,
    read_bytes: Callable[[dict, str], bytes | None] | None = None,
) -> list[str]:
    """Every texture identity the install and every map's PAKFILE hold, in key order.

    The engine composes `materials/<texture>.tth`, so the `.tth` members are the seam's install
    corpus. Every map's PAKFILE lump adds baked reflection probes -- `.tth`, with a `.ttz` sibling
    only when the zip carries one -- under `maps/<map>/<stem>`; a probe stem is deduped by
    identity with an install key of the same spelling, with the install member winning (today the
    install carries no `materials/maps/**` member at all, so this is future-proofing rather than
    an observed collision). This is the one selection rule: the plural export command and the
    corpus index's member dispositions both call it, so neither can drift from the other.
    `read_bytes` is the same test injection point every seam's closure loader takes; production
    callers pass an install index and get `install.read` for free.
    """

    from elysium_pipeline.formats.map_glb.pakfile_index import pakfile_members

    prefix, suffix = "materials/", ".tth"
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


def sprite_source_keys(index: dict) -> list[str]:
    """Every particle sprite the install resolves, as texture keys (`particles/<stem>`), sorted.

    R7.3 (`seam_map_texture.md` -> "Texture unit" -> "Particle sprites"): the 318 raw
    `particles/*.tga` members are texture units too, so the particle floor draws them off
    `/ElysiumBaked/Textures/particles/T_<stem>`. Kept apart from `source_keys` on purpose: that
    selector is the corpus index's disposition rule for `materials/**.tth`, and the `.tga` members
    are already disposed under the image seam, which keeps publishing them as images.
    """

    prefix = SPRITE_FAMILY + "/"
    return sorted(
        path[:-len(SPRITE_SUFFIX)]
        for path in index
        if path.startswith(prefix) and path.endswith(SPRITE_SUFFIX) and "/" not in path[len(prefix):]
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


def _pakfile_member(
    index: dict,
    map_name: str,
    stem: str,
    extension: str,
    role: str,
    read_bytes: Callable[[dict, str], bytes | None],
    *,
    required: bool,
) -> SourceMember | None:
    from elysium_pipeline.formats.map_glb.pakfile_index import (
        bsp_origin,
        pakfile_member_bytes,
        pakfile_members,
    )

    member_name = f"materials/maps/{map_name}/{stem}{extension}"
    members = pakfile_members(index, read_bytes=read_bytes).get(map_name, ())
    member = next(
        (candidate for candidate in members if candidate.name.lower() == member_name.lower()),
        None,
    )
    if member is None:
        if required:
            raise TextureSourceError(f"missing required {role}: {member_name}")
        return None
    data = pakfile_member_bytes(index, map_name, member.name, read_bytes=read_bytes)
    origin = pakfile_origin(
        map_name, member.name, bsp_origin(index, map_name, read_bytes=read_bytes)
    ).to_json()
    return SourceMember(role, member.name, data, origin)


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
    if is_sprite_key(normalized):
        # A particle sprite: the raw `.tga` member is the whole closure (role `tga`, held in the
        # primary slot); there is no `.ttz`.
        tga = _member(index, normalized + SPRITE_SUFFIX, "tga", read_bytes, required=True)
        return TextureSourceClosure(normalized, asset_id(normalized), tga, None)
    if normalized.startswith("maps/"):
        # An install member under `materials/maps/**` wins over the PAKFILE probe of the same
        # spelling (`source_keys`'s dedup rule); today the install carries none, so this is only
        # ever exercised by a synthetic install, but a real one that started shipping one must not
        # silently keep routing to the BSP copy.
        base = f"materials/{normalized}"
        if base + ".tth" in index:
            tth = _member(index, base + ".tth", "tth", read_bytes, required=True)
            ttz = _member(index, base + ".ttz", "ttz", read_bytes)
            return TextureSourceClosure(normalized, asset_id(normalized), tth, ttz)
        map_name, _, stem = normalized[len("maps/"):].partition("/")
        if not stem:
            raise TextureSourceError(
                f"{normalized!r} names no map-relative stem to resolve in a PAKFILE"
            )
        tth = _pakfile_member(index, map_name, stem, ".tth", "tth", read_bytes, required=True)
        ttz = _pakfile_member(index, map_name, stem, ".ttz", "ttz", read_bytes, required=False)
        return TextureSourceClosure(normalized, asset_id(normalized), tth, ttz)
    base = f"materials/{normalized}"
    tth = _member(index, base + ".tth", "tth", read_bytes, required=True)
    ttz = _member(index, base + ".ttz", "ttz", read_bytes)
    return TextureSourceClosure(normalized, asset_id(normalized), tth, ttz)
