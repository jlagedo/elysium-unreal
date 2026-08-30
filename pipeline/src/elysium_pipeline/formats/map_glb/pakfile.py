"""Lump 40's embedded ZIP: the container the root owns and the members it routes away.

The root owns the ZIP's bookkeeping -- local headers, central directory, end-of-central-directory
record -- and never decodes a member. Each member's stored span is `derived`: its bytes are the
source of an ordinary material or texture unit, which the root names through a `bsp-pakfile`
origin and a `dependencies` row rather than copying here.
"""

from __future__ import annotations

from dataclasses import dataclass
import hashlib
import struct
from typing import Any

from elysium_pipeline.formats.map_glb import claims as claim_tools

#: The directory row this container is the payload of, named on the omission rows it produces.
PAKFILE_LUMP = 40
FILL_EVIDENCE = "no local header, member span, central record or EOCD addresses this range"

LOCAL_SIGNATURE = 0x04034B50
CENTRAL_SIGNATURE = 0x02014B50
EOCD_SIGNATURE = 0x06054B50
LOCAL_HEADER_BYTES = 30
CENTRAL_HEADER_BYTES = 46
EOCD_BYTES = 22

#: Stored, on every member of every map: the compiler writes the patched materials and the baked
#: reflection probes uncompressed, so a member's stored span is its bytes.
METHOD_STORE = 0

#: The only member kinds a seam claims. Anything else fails the map, because its bytes would be
#: accounted for by no unit at all.
MEMBER_EXTENSIONS = (".vmt", ".tth", ".ttz")


class PakfileError(ValueError):
    """The PAKFILE lump is not the ZIP container this seam accounts for."""


@dataclass(frozen=True, slots=True)
class PakMember:
    """One ZIP member: its two header records, its stored span and what the bytes became."""

    index: int
    name: str
    method: int
    flags: int
    crc32: int
    compressed_size: int
    uncompressed_size: int
    local_header_offset: int
    local_header_bytes: int
    data_offset: int
    central_header_offset: int
    central_header_bytes: int
    sha256: str

    @property
    def extension(self) -> str:
        _, _, suffix = self.name.rpartition(".")
        return "." + suffix.lower() if suffix else ""

    def to_json(self, unit: str | None) -> dict[str, Any]:
        return {
            "index": self.index,
            "name": self.name,
            "method": self.method,
            "flags": self.flags,
            "crc32": self.crc32,
            "compressedSize": self.compressed_size,
            "uncompressedSize": self.uncompressed_size,
            "sha256": self.sha256,
            "localHeader": {
                "sourceOffset": self.local_header_offset,
                "byteLength": self.local_header_bytes,
            },
            "centralHeader": {
                "sourceOffset": self.central_header_offset,
                "byteLength": self.central_header_bytes,
            },
            "data": {"sourceOffset": self.data_offset, "byteLength": self.compressed_size},
            "unit": unit,
        }


@dataclass(frozen=True, slots=True)
class Pakfile:
    """The whole container, in the coordinates of the file it sits in."""

    lump_offset: int
    lump_length: int
    members: tuple[PakMember, ...]
    central_offset: int
    central_bytes: int
    eocd_offset: int
    eocd_bytes: int
    comment_bytes: int
    claims: tuple[tuple[int, int, str, str], ...]

    def to_json(self, units: dict[int, str | None]) -> dict[str, Any]:
        return {
            "sourceOffset": self.lump_offset,
            "byteLength": self.lump_length,
            "entryCount": len(self.members),
            "centralDirectory": {
                "sourceOffset": self.central_offset,
                "byteLength": self.central_bytes,
            },
            "endOfCentralDirectory": {
                "sourceOffset": self.eocd_offset,
                "byteLength": self.eocd_bytes,
                "commentLength": self.comment_bytes,
            },
            "entries": [member.to_json(units.get(member.index)) for member in self.members],
        }


def _fill_claims(
    data: bytes,
    lump_offset: int,
    lump_length: int,
    claims: list[tuple[int, int, str, str]],
    omissions: list[dict[str, Any]],
) -> list[tuple[int, int, str, str]]:
    """Whatever the records did not claim, so the container is gapless over its lump."""

    filled: list[tuple[int, int, str, str]] = []
    cursor = lump_offset
    end = lump_offset + lump_length
    for offset, length, _state, _owner in sorted(claims):
        if offset > cursor:
            filled += claim_tools.tail(
                data, cursor, offset - cursor, "pakfile", "pakfile-container-fill",
                omissions, lump=PAKFILE_LUMP, evidence=FILL_EVIDENCE,
            )
        cursor = max(cursor, offset + length)
    if cursor < end:
        filled += claim_tools.tail(
            data, cursor, end - cursor, "pakfile", "pakfile-container-fill",
            omissions, lump=PAKFILE_LUMP, evidence=FILL_EVIDENCE,
        )
    return filled


def _decode_name(raw: bytes) -> str:
    return raw.decode("latin-1").replace("\\", "/")


def parse(
    data: bytes,
    lump_offset: int,
    lump_length: int,
    omissions: list[dict[str, Any]] | None = None,
) -> Pakfile:
    """Parse the ZIP in file coordinates and claim every one of its container bytes.

    The returned claims are `(offset, length, state, owner)` in file coordinates and are gapless
    over the lump: the two header records and the trailing directory are `mapped`, each member's
    stored span is `derived`, and anything between them is proven fill -- zero fill is
    `padding-zero`, and a non-zero run becomes an evidence-backed `omissions[]` row, so a ZIP
    with bytes no record addresses is named rather than surfacing as a bare ledger gap.
    """

    if lump_length <= 0:
        return Pakfile(lump_offset, 0, (), lump_offset, 0, lump_offset, 0, 0, ())
    fill_rows: list[dict[str, Any]] = omissions if omissions is not None else []
    raw = data[lump_offset:lump_offset + lump_length]
    eocd = raw.rfind(struct.pack("<I", EOCD_SIGNATURE))
    if eocd < 0:
        raise PakfileError("PAKFILE carries no end-of-central-directory record")
    (
        disk,
        central_disk,
        entries_on_disk,
        entries,
        central_size,
        central_offset,
        comment_length,
    ) = struct.unpack_from("<HHHHIIH", raw, eocd + 4)
    if disk or central_disk or entries_on_disk != entries:
        raise PakfileError("PAKFILE spans more than one disk")
    if eocd + EOCD_BYTES + comment_length != len(raw):
        raise PakfileError(
            f"PAKFILE holds {len(raw) - (eocd + EOCD_BYTES + comment_length)} bytes past its EOCD"
        )
    if central_offset + central_size > len(raw):
        raise PakfileError("PAKFILE central directory runs past the lump")

    members: list[PakMember] = []
    claims: list[tuple[int, int, str, str]] = []
    cursor = central_offset
    for index in range(entries):
        if struct.unpack_from("<I", raw, cursor)[0] != CENTRAL_SIGNATURE:
            raise PakfileError(f"PAKFILE central record {index} has no signature")
        (
            _made_by,
            _needed,
            flags,
            method,
            _time,
            _date,
            crc32,
            compressed,
            uncompressed,
            name_length,
            extra_length,
            comment,
            _disk_start,
            _internal,
            _external,
            local_offset,
        ) = struct.unpack_from("<HHHHHHIIIHHHHHII", raw, cursor + 4)
        name = _decode_name(raw[cursor + 46:cursor + 46 + name_length])
        central_bytes = CENTRAL_HEADER_BYTES + name_length + extra_length + comment
        if cursor + central_bytes > central_offset + central_size:
            raise PakfileError(f"PAKFILE central record {index} runs past the directory")
        if struct.unpack_from("<I", raw, local_offset)[0] != LOCAL_SIGNATURE:
            raise PakfileError(f"PAKFILE member {name!r} has no local header")
        (
            _local_needed,
            local_flags,
            local_method,
            _local_time,
            _local_date,
            local_crc,
            local_compressed,
            local_uncompressed,
            local_name_length,
            local_extra_length,
        ) = struct.unpack_from("<HHHHHIIIHH", raw, local_offset + 4)
        if (local_crc, local_compressed, local_uncompressed) != (crc32, compressed, uncompressed):
            raise PakfileError(f"PAKFILE member {name!r} disagrees with its central record")
        if local_flags & 0x08:
            raise PakfileError(f"PAKFILE member {name!r} defers its sizes to a data descriptor")
        if method != METHOD_STORE or local_method != METHOD_STORE:
            raise PakfileError(f"PAKFILE member {name!r} uses compression method {method}")
        local_bytes = LOCAL_HEADER_BYTES + local_name_length + local_extra_length
        data_offset = local_offset + local_bytes
        if data_offset + compressed > len(raw):
            raise PakfileError(f"PAKFILE member {name!r} runs past the lump")
        member = PakMember(
            index=index,
            name=name,
            method=method,
            flags=flags,
            crc32=crc32,
            compressed_size=compressed,
            uncompressed_size=uncompressed,
            local_header_offset=lump_offset + local_offset,
            local_header_bytes=local_bytes,
            data_offset=lump_offset + data_offset,
            central_header_offset=lump_offset + cursor,
            central_header_bytes=central_bytes,
            sha256=hashlib.sha256(raw[data_offset:data_offset + compressed]).hexdigest(),
        )
        members.append(member)
        claims.append(
            (
                member.local_header_offset,
                local_bytes,
                "mapped",
                f"pakfile.entries[{index}].localHeader",
            )
        )
        if compressed:
            claims.append(
                (member.data_offset, compressed, "derived", f"pakfile.entries[{index}].data")
            )
        claims.append(
            (
                member.central_header_offset,
                central_bytes,
                "mapped",
                f"pakfile.entries[{index}].centralHeader",
            )
        )
        cursor += central_bytes
    if cursor != central_offset + central_size:
        raise PakfileError("PAKFILE central directory size disagrees with its records")
    claims.append(
        (
            lump_offset + eocd,
            EOCD_BYTES + comment_length,
            "mapped",
            "pakfile.endOfCentralDirectory",
        )
    )
    claims.extend(
        _fill_claims(data, lump_offset, lump_length, claims, fill_rows)
    )
    return Pakfile(
        lump_offset=lump_offset,
        lump_length=lump_length,
        members=tuple(members),
        central_offset=lump_offset + central_offset,
        central_bytes=central_size,
        eocd_offset=lump_offset + eocd,
        eocd_bytes=EOCD_BYTES + comment_length,
        comment_bytes=comment_length,
        claims=tuple(claims),
    )
