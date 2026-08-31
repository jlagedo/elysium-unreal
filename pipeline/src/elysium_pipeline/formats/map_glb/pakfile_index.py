"""Shared, per-process PAKFILE access for every seam that reads a BSP's embedded ZIP.

The texture seam, the material seam and the corpus index walk all need the same PAKFILE members
from the same 108 map BSPs. Each BSP's PAKFILE lump is parsed once per process here and shared,
so no BSP is opened and unzipped more than once no matter how many seams ask for its members.
"""

from __future__ import annotations

import logging
import struct
import threading
from types import MappingProxyType
from typing import Callable

from elysium_pipeline.formats.map_glb import pakfile as pakfile_reader
from elysium_pipeline.formats.map_glb.pakfile import PakMember
from elysium_pipeline.formats.unit_contract.origin import Origin, origin_of

#: The BSP lump whose payload is the map's embedded ZIP, and the fixed BSP header it sits behind.
PAKFILE_LUMP = 40
_BSP_HEADER_BYTES = 8 + 64 * 16 + 4

_LOG = logging.getLogger(__name__)

_MEMBERS_LOCK = threading.Lock()
#: One memo per (install index, read_bytes) identity pair. Keying on `id()` alone is unsound: once
#: an index (or an injected `read_bytes`) is garbage-collected, CPython is free to hand its address
#: to an unrelated object, and a stale cache entry would then answer for it with the wrong map's
#: members. Every entry below holds a strong reference to both the index and the `read_bytes` it
#: was built from (`_MembersEntry.index`/`.read_bytes`), so neither can be collected -- and neither
#: id can be reused -- while the entry is cached; `pakfile_members`/`pakfile_failures` still check
#: `entry.index is index and entry.read_bytes is read` before trusting a hit, as a second line of
#: defence against a cache-key collision. The stored `members` mapping is a `MappingProxyType`, so
#: no caller can mutate the shared memo through the reference it gets back.
#:
#: TODO(SF-1.5): `pakfile_failures` names every map this reader could not parse; the corpus index
#: walk and `elysium doctor` should surface it too. Left for SF-1.5/1.6 to wire up on the index
#: side -- this module only records and logs the failures.


class _MembersEntry:
    __slots__ = ("index", "read_bytes", "members", "failures")

    def __init__(self, index, read_bytes, members, failures):
        self.index = index
        self.read_bytes = read_bytes
        self.members = members
        self.failures = failures


_MEMBERS_CACHE: dict[tuple[int, int], _MembersEntry] = {}

_BSP_BYTES_LOCK = threading.Lock()
#: The single most-recently-read BSP buffer, keyed by (index, `read_bytes`, map name) identity.
#: Every plural export walks `source_keys()` in sorted order, so PAKFILE-origin keys for the same
#: map are adjacent; caching only the last buffer turns "re-read the whole BSP per member" into
#: "re-read the whole BSP once per map" without holding every map's bytes in memory at once.
#:
#: Both `index` and `read_bytes` are held by strong reference and `is`-compared, not just
#: `id()`-compared: `read_bytes` collapses to the same module-level `_default_read` for every
#: caller that does not inject one, so keying on it alone would serve one install's bytes for a
#: same-named map in a *different* install (the exact scenario two different test installs both
#: resolving `maps/tutorial.bsp` through the default reader hit). Holding the objects themselves
#: also closes the `id()`-reuse hole `_MEMBERS_CACHE` closes the same way.
_LAST_BSP: tuple[dict, Callable[[dict, str], bytes | None], str, bytes] | None = None


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


def _bsp_bytes(
    index: dict, map_name: str, read: Callable[[dict, str], bytes | None]
) -> bytes | None:
    global _LAST_BSP

    with _BSP_BYTES_LOCK:
        if (
            _LAST_BSP is not None
            and _LAST_BSP[0] is index
            and _LAST_BSP[1] is read
            and _LAST_BSP[2] == map_name
        ):
            return _LAST_BSP[3]
    data = read(index, f"maps/{map_name}.bsp")
    if data is not None:
        with _BSP_BYTES_LOCK:
            _LAST_BSP = (index, read, map_name, data)
    return data


def _parse_members(
    index: dict, read: Callable[[dict, str], bytes | None]
) -> tuple[dict[str, tuple[PakMember, ...]], dict[str, str]]:
    members: dict[str, tuple[PakMember, ...]] = {}
    failures: dict[str, str] = {}
    for map_name in _map_names(index):
        data = _bsp_bytes(index, map_name, read)
        if data is None:
            failures[map_name] = "bsp bytes unavailable"
            continue
        if len(data) < _BSP_HEADER_BYTES:
            failures[map_name] = f"bsp is only {len(data)} bytes, shorter than the fixed header"
            continue
        offset, length = struct.unpack_from("<ii", data, 8 + PAKFILE_LUMP * 16)
        if length <= 0 or offset < 0 or offset + length > len(data):
            failures[map_name] = f"PAKFILE lump range {offset}+{length} is invalid for {len(data)} bytes"
            continue
        try:
            container = pakfile_reader.parse(data, offset, length, [])
        except Exception as error:                          # noqa: BLE001 - best-effort, like the
            # corpus index's own walk: a map whose PAKFILE this reader cannot parse is that map's
            # seam's failure to report, not this shared index's.
            failures[map_name] = f"{type(error).__name__}: {error}"
            continue
        # A member packed flat (`materials/<name>.<ext>`, no `maps/` component at all) names no
        # map and is fine as-is -- it is a retail material or texture this BSP's own PAKFILE
        # happens to carry, addressed the ordinary non-map way by `source_keys`. Only a member
        # that *does* claim `maps/` routing but names a map other than the one whose BSP it was
        # read from is the mismatch: a corrupt or hand-edited PAKFILE claiming another map's
        # identity, which every map-routed lookup (`_pakfile_member`, `_pakfile_vmt`) assumes
        # cannot happen.
        maps_prefix = "materials/maps/"
        own_prefix = f"materials/maps/{map_name}/"
        verified = []
        for member in container.members:
            name = member.name.replace("\\", "/").lower()
            if name.startswith(maps_prefix) and not name.startswith(own_prefix):
                _LOG.warning(
                    "pakfile_index: map %r's PAKFILE carries member %r outside its own %r "
                    "prefix; skipped",
                    map_name, member.name, own_prefix,
                )
                continue
            verified.append(member)
        members[map_name] = tuple(verified)
    for map_name, reason in failures.items():
        _LOG.warning("pakfile_index: could not parse PAKFILE for map %r: %s", map_name, reason)
    return members, failures


def pakfile_members(
    index: dict,
    *,
    read_bytes: Callable[[dict, str], bytes | None] | None = None,
) -> "MappingProxyType[str, tuple[PakMember, ...]]":
    """Every map's PAKFILE member table, parsed once per process per (index, read_bytes) pair.

    Best-effort per map, matching `corpus_index_glb.walk._pakfile_members`: a BSP this reader
    cannot parse -- too short, no PAKFILE lump, a malformed container -- is skipped rather than
    raising, because a map's own seam is where that failure is reported; `pakfile_failures` names
    every map skipped this way. Only the parsed `PakMember` bookkeeping is cached, never member
    bytes for every map: each member's data is re-sliced from its BSP on demand by
    `pakfile_member_bytes`. The returned mapping is a read-only view onto the shared memo.
    """

    read = read_bytes or _default_read
    key = (id(index), id(read))
    with _MEMBERS_LOCK:
        cached = _MEMBERS_CACHE.get(key)
        if cached is not None and cached.index is index and cached.read_bytes is read:
            return cached.members
        # Parsed while still holding the lock: a concurrent caller for the same index would
        # otherwise race to parse the same BSPs and each memoize its own duplicate table.
        members, failures = _parse_members(index, read)
        entry = _MembersEntry(
            index, read, MappingProxyType(dict(members)), MappingProxyType(dict(failures))
        )
        _MEMBERS_CACHE[key] = entry
        return entry.members


def pakfile_failures(
    index: dict,
    *,
    read_bytes: Callable[[dict, str], bytes | None] | None = None,
) -> "MappingProxyType[str, str]":
    """`{map_name: reason}` for every map `pakfile_members` could not parse for this index."""

    read = read_bytes or _default_read
    pakfile_members(index, read_bytes=read)
    key = (id(index), id(read))
    with _MEMBERS_LOCK:
        entry = _MEMBERS_CACHE.get(key)
    if entry is None or entry.index is not index or entry.read_bytes is not read:
        return MappingProxyType({})
    return entry.failures


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
    members = pakfile_members(index, read_bytes=read).get(map_name)
    if members is None:
        raise PakfileIndexError(f"no PAKFILE members for map {map_name!r}")
    member = next(
        (candidate for candidate in members if candidate.name.lower() == member_name.lower()),
        None,
    )
    if member is None:
        raise PakfileIndexError(f"map {map_name!r} carries no PAKFILE member {member_name!r}")
    data = _bsp_bytes(index, map_name, read)
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
