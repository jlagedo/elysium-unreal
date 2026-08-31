"""Shared, per-process PAKFILE access for every seam that reads a BSP's embedded ZIP.

The texture seam, the material seam and the corpus index walk all need the same PAKFILE members
from the same 108 map BSPs. Each BSP's PAKFILE lump is parsed once per process here and shared,
so no BSP is opened and unzipped more than once no matter how many seams ask for its members.
"""

from __future__ import annotations

import struct
import threading
from typing import Callable

from elysium_pipeline.formats.map_glb import pakfile as pakfile_reader
from elysium_pipeline.formats.map_glb.pakfile import PakMember
from elysium_pipeline.formats.unit_contract.origin import Origin, origin_of

#: The BSP lump whose payload is the map's embedded ZIP, and the fixed BSP header it sits behind.
PAKFILE_LUMP = 40
_BSP_HEADER_BYTES = 8 + 64 * 16 + 4

_MEMBERS_LOCK = threading.Lock()
#: One memo per install index (`id(index)` -- the index is process-lifetime and never mutated
#: after `install.build_index()` builds it, so identity is a stable cache key for that lifetime).
_MEMBERS_CACHE: dict[int, dict[str, tuple[PakMember, ...]]] = {}


class PakfileIndexError(RuntimeError):
    """A caller asked for a map or member this PAKFILE index does not hold."""


def _default_read(index: dict, key: str) -> bytes | None:
    from elysium_pipeline.formats import install

    return install.read(index, key)


def _map_names(index: dict) -> list[str]:
    return sorted(
        key[len("maps/"):-len(".bsp")]
        for key in index
        if key.startswith("maps/") and key.endswith(".bsp")
    )


def pakfile_members(
    index: dict,
    *,
    read_bytes: Callable[[dict, str], bytes | None] | None = None,
) -> dict[str, tuple[PakMember, ...]]:
    """Every map's PAKFILE member table, parsed once per process per install index.

    Best-effort per map, matching `corpus_index_glb.walk._pakfile_members`: a BSP this reader
    cannot parse -- too short, no PAKFILE lump, a malformed container -- is skipped rather than
    raising, because a map's own seam is where that failure is reported. Only the parsed
    `PakMember` bookkeeping is cached, never member bytes for every map: each member's data is
    re-sliced from its BSP on demand by `pakfile_member_bytes`.
    """

    read = read_bytes or _default_read
    cache_key = id(index)
    with _MEMBERS_LOCK:
        cached = _MEMBERS_CACHE.get(cache_key)
    if cached is not None:
        return cached
    members: dict[str, tuple[PakMember, ...]] = {}
    for map_name in _map_names(index):
        data = read(index, f"maps/{map_name}.bsp")
        if data is None or len(data) < _BSP_HEADER_BYTES:
            continue
        offset, length = struct.unpack_from("<ii", data, 8 + PAKFILE_LUMP * 16)
        if length <= 0 or offset < 0 or offset + length > len(data):
            continue
        try:
            container = pakfile_reader.parse(data, offset, length, [])
        except Exception:                                  # noqa: BLE001 - best-effort, like the
            # corpus index's own walk: a map whose PAKFILE this reader cannot parse is that map's
            # seam's failure to report, not this shared index's.
            continue
        members[map_name] = container.members
    with _MEMBERS_LOCK:
        _MEMBERS_CACHE[cache_key] = members
    return members


def pakfile_member_bytes(
    index: dict,
    map_name: str,
    member_name: str,
    *,
    read_bytes: Callable[[dict, str], bytes | None] | None = None,
) -> bytes:
    """One PAKFILE member's stored bytes: strict, unlike `pakfile_members`.

    Every member is written uncompressed (`pakfile.METHOD_STORE`), so its stored span is its
    decoded bytes; a caller that names a map or member this index does not hold gets a clear
    error rather than `None` or a `KeyError`.
    """

    read = read_bytes or _default_read
    members = pakfile_members(index, read_bytes=read_bytes).get(map_name)
    if members is None:
        raise PakfileIndexError(f"no PAKFILE members for map {map_name!r}")
    member = next(
        (candidate for candidate in members if candidate.name.lower() == member_name.lower()),
        None,
    )
    if member is None:
        raise PakfileIndexError(f"map {map_name!r} carries no PAKFILE member {member_name!r}")
    data = read(index, f"maps/{map_name}.bsp")
    if data is None:
        raise PakfileIndexError(f"map {map_name!r} bsp vanished after indexing")
    return data[member.data_offset:member.data_offset + member.compressed_size]


def bsp_origin(
    index: dict,
    map_name: str,
    *,
    read_bytes: Callable[[dict, str], bytes | None] | None = None,
) -> Origin:
    """The origin of the winning `maps/<map>.bsp` entry, nested under a PAKFILE member's own."""

    key = f"maps/{map_name}.bsp"
    entry = index.get(key)
    if entry is None:
        raise PakfileIndexError(f"no install member for map {map_name!r}")
    return origin_of(entry)
