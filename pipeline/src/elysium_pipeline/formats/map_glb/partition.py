"""The BSP v17 64-lump directory partition proof, and the spans it hands the three sub-units.

A VtMB BSP is one member cut into four `export_v2` units -- the map root and the entities,
lighting and visibility sub-units -- so before any of them is published somebody has to prove
that the four together account for every byte of the file exactly once. That proof is this
module, and it is the map root's to own (`docs/architecture/seam_map_map.md` -> "Lump partition").

Public API (stable; the three sub-unit seams import it):

    from elysium_pipeline.formats.map_glb.partition import partition

    part = partition(bsp_bytes)             # raises PartitionError if the file does not partition
    part.byte_length                        # len(bsp)
    part.ident, part.version, part.map_revision
    part.lumps                              # 64 LumpSpan rows, index-ordered, populated or not
    part.game_lumps                         # GameLumpEntry rows of lump 35's directory
    part.regions                            # every byte, offset-ordered, gapless, one owner each
    part.spans_for("map-lighting")          # -> tuple[Span, ...]  (offset, length, lump, gameLump)
    part.rows()                             # the JSON `partition` table the root publishes
    part.sha256_for("map-entities", data)   # digest of that unit's spans, concatenated in order

A sub-unit builds its `sourceResolution` from `spans_for(<its identity kind>)`: one member per
span, `span={"offset", "length"}`, and a ledger gapless over each span alone. The root's own
ledger runs over the whole member, so the sub-units' spans appear in it as `omitted-proven`
ranges naming the unit that owns them -- the same partition seen from the other side.

Ownership, from the seam specifications:

| Unit | Lumps |
|---|---|
| `map-entities` | 0 |
| `map-lighting` | 8, 15, 32, 34, and the `dplt` game-lump payload |
| `map-visibility` | 4, 22, 23, 24, 25 |
| `map` (root) | the header, the 64-row directory, `mapRevision`, the game-lump directory, the file trailer, every other populated lump, and every byte between lumps |

`Region.state` is the ledger state the owning unit claims the range under, so a reader can add
the states up without re-deriving them; `Region.unit` names the unit that owes the claim.
"""

from __future__ import annotations

from dataclasses import dataclass
import hashlib
import struct
from typing import Any, Iterable

#: The 64 directory rows, `int fileofs, filelen, version; char fourCC[4]`, from byte 8.
LUMP_COUNT = 64
LUMP_DIRECTORY_OFFSET = 8
LUMP_ENTRY_SIZE = 16
#: `int ident; int version;` then the directory, then `int mapRevision`.
HEADER_BYTES = LUMP_DIRECTORY_OFFSET + LUMP_COUNT * LUMP_ENTRY_SIZE + 4
BSP_IDENT = b"VBSP"
BSP_VERSION = 17

#: Every map ends with `int <lump 0 offset>` followed by the six ASCII bytes below. The value is
#: verified against the directory per map; what the trailer is *for* is not recoverable from the
#: file, so the record is carried in `typedUnidentified` rather than claimed as understood.
TRAILER_SIGNATURE = b"QnDbTm"
TRAILER_BYTES = 4 + len(TRAILER_SIGNATURE)

ROOT_UNIT = "map"
ENTITIES_UNIT = "map-entities"
LIGHTING_UNIT = "map-lighting"
VISIBILITY_UNIT = "map-visibility"

#: Which unit owns which lump. Every lump not named here is the root's.
ENTITIES_LUMPS = (0,)
LIGHTING_LUMPS = (8, 15, 32, 34)
VISIBILITY_LUMPS = (4, 22, 23, 24, 25)
#: The one game-lump entry the lighting unit owns the payload of; the root owns the directory.
LIGHTING_GAME_LUMPS = ("dplt",)

GAME_LUMP_INDEX = 35
PAKFILE_INDEX = 40

#: The census names of the 64 directory rows, so `partition` speaks the format's vocabulary.
LUMP_NAMES = (
    "ENTITIES", "PLANES", "TEXDATA", "VERTEXES", "VISIBILITY", "NODES", "TEXINFO", "FACES",
    "LIGHTING", "OCCLUSION", "LEAFS", "FACEIDS", "EDGES", "SURFEDGES", "MODELS", "WORLDLIGHTS",
    "LEAFFACES", "LEAFBRUSHES", "BRUSHES", "BRUSHSIDES", "AREAS", "AREAPORTALS", "PORTALS",
    "CLUSTERS", "PORTALVERTS", "CLUSTERPORTALS", "DISPINFO", "ORIGINALFACES", "PHYSNODES",
    "PHYSCOLLIDE", "VERTNORMALS", "VERTNORMALINDICES", "DISP_LIGHTMAP_ALPHAS", "DISP_VERTS",
    "DISP_LIGHTMAP_SAMPLE_POSITIONS", "GAME_LUMP", "LEAFWATERDATA", "PRIMITIVES", "PRIMVERTS",
    "PRIMINDICES", "PAKFILE", "CLIPPORTALVERTS", "CUBEMAPS", "TEXDATA_STRING_DATA",
    "TEXDATA_STRING_TABLE", "OVERLAYS", "LEAFMINDISTTOWATER", "FACE_MACRO_TEXTURE_INFO",
    "DISP_TRIS", "PHYSCOLLIDESURFACE", "UNUSED_50", "UNUSED_51", "UNUSED_52", "UNUSED_53",
    "UNUSED_54", "UNUSED_55", "UNUSED_56", "UNUSED_57", "UNUSED_58", "UNUSED_59", "UNUSED_60",
    "UNUSED_61", "UNUSED_62", "UNUSED_63",
)


class PartitionError(ValueError):
    """The file does not partition into the four units' spans plus named non-unit bytes."""


@dataclass(frozen=True, slots=True)
class LumpSpan:
    """One directory row and the extent it names."""

    index: int
    name: str
    offset: int
    length: int
    version: int
    four_cc: str
    owner: str

    @property
    def populated(self) -> bool:
        return self.length > 0

    @property
    def end(self) -> int:
        return self.offset + self.length

    @property
    def row_offset(self) -> int:
        """Where this directory row itself sits in the file."""

        return LUMP_DIRECTORY_OFFSET + self.index * LUMP_ENTRY_SIZE

    def to_json(self) -> dict[str, Any]:
        return {
            "lump": self.index,
            "name": self.name,
            "offset": self.offset,
            "length": self.length,
            "version": self.version,
            "fourCC": self.four_cc,
            "sourceOffset": self.row_offset,
        }


@dataclass(frozen=True, slots=True)
class GameLumpEntry:
    """One 16-byte row of lump 35's directory, and the payload extent it names.

    `disk_id` is the four bytes as the file stores them; `id` is the conventional spelling, which
    is the byte-reversed form (`prps` on disk is `sprp`).
    """

    index: int
    id: str
    disk_id: str
    flags: int
    version: int
    offset: int
    length: int
    owner: str
    row_offset: int

    @property
    def end(self) -> int:
        return self.offset + self.length

    def to_json(self) -> dict[str, Any]:
        return {
            "index": self.index,
            "id": self.id,
            "diskId": self.disk_id,
            "flags": self.flags,
            "version": self.version,
            "offset": self.offset,
            "length": self.length,
            "sourceOffset": self.row_offset,
        }


@dataclass(frozen=True, slots=True)
class Span:
    """One contiguous extent handed to one unit."""

    offset: int
    length: int
    lump: int
    game_lump: str | None = None

    def to_json(self) -> dict[str, Any]:
        row: dict[str, Any] = {"offset": self.offset, "length": self.length, "lump": self.lump}
        if self.game_lump is not None:
            row["gameLump"] = self.game_lump
        return row


@dataclass(frozen=True, slots=True)
class Region:
    """One extent of the file, the unit that owns it and the state it is claimed under."""

    offset: int
    length: int
    state: str
    owner: str
    unit: str
    lump: int | None = None
    game_lump: str | None = None
    sha256: str | None = None

    def to_json(self) -> dict[str, Any]:
        row: dict[str, Any] = {
            "offset": self.offset,
            "length": self.length,
            "state": self.state,
            "owner": self.owner,
            "unit": self.unit,
        }
        if self.lump is not None:
            row["lump"] = self.lump
        if self.game_lump is not None:
            row["gameLump"] = self.game_lump
        if self.sha256 is not None:
            row["sha256"] = self.sha256
        return row


@dataclass(frozen=True, slots=True)
class Partition:
    """The proof that one BSP's bytes partition into the four units and nothing else."""

    byte_length: int
    sha256: str
    ident: str
    version: int
    map_revision: int
    lumps: tuple[LumpSpan, ...]
    game_lumps: tuple[GameLumpEntry, ...]
    regions: tuple[Region, ...]
    trailer: dict[str, Any] | None
    anomalies: tuple[dict[str, Any], ...]

    def lump(self, index: int) -> LumpSpan:
        return self.lumps[index]

    def game_lump(self, identifier: str) -> GameLumpEntry | None:
        for entry in self.game_lumps:
            if entry.id == identifier:
                return entry
        return None

    def spans_for(self, unit: str) -> tuple[Span, ...]:
        """Every extent `unit` owns, offset-ordered. A sub-unit's `sourceResolution` is this."""

        spans = [
            Span(region.offset, region.length, region.lump, region.game_lump)
            for region in self.regions
            if region.unit == unit and region.lump is not None
        ]
        return tuple(sorted(spans, key=lambda span: span.offset))

    def rows(self) -> list[dict[str, Any]]:
        """The `partition` table the root publishes: every region, in file order."""

        return [region.to_json() for region in self.regions]

    def owners(self) -> dict[str, list[int]]:
        """Which lumps each unit owns, for a reader checking the claim without the sub-units."""

        table: dict[str, list[int]] = {}
        for lump in self.lumps:
            if lump.populated:
                table.setdefault(lump.owner, []).append(lump.index)
        return {unit: sorted(indices) for unit, indices in sorted(table.items())}

    def sha256_for(self, unit: str, data: bytes) -> str:
        """The digest of one unit's spans, concatenated in offset order."""

        digest = hashlib.sha256()
        for span in self.spans_for(unit):
            digest.update(data[span.offset:span.offset + span.length])
        return digest.hexdigest()

    def verify(self, data: bytes) -> None:
        """Re-check the partition against the bytes: gapless, non-overlapping, whole."""

        if len(data) != self.byte_length:
            raise PartitionError(f"partition covers {self.byte_length} bytes of {len(data)}")
        cursor = 0
        for region in self.regions:
            if region.offset != cursor:
                kind = "overlaps" if region.offset < cursor else "leaves a gap at"
                raise PartitionError(
                    f"{region.owner} {kind} byte {cursor} (starts at {region.offset})"
                )
            if region.length <= 0:
                raise PartitionError(f"{region.owner} claims {region.length} bytes")
            cursor += region.length
        if cursor != self.byte_length:
            raise PartitionError(f"partition accounts {cursor}/{self.byte_length} bytes")


def _owner_of(index: int) -> str:
    if index in ENTITIES_LUMPS:
        return ENTITIES_UNIT
    if index in LIGHTING_LUMPS:
        return LIGHTING_UNIT
    if index in VISIBILITY_LUMPS:
        return VISIBILITY_UNIT
    return ROOT_UNIT


def _four_cc(raw: bytes) -> str:
    return "".join(f"{byte:02x}" for byte in raw)


def _read_directory(data: bytes) -> tuple[list[LumpSpan], int, list[dict[str, Any]]]:
    lumps: list[LumpSpan] = []
    anomalies: list[dict[str, Any]] = []
    for index in range(LUMP_COUNT):
        offset, length, version, four_cc = struct.unpack_from(
            "<iii4s", data, LUMP_DIRECTORY_OFFSET + index * LUMP_ENTRY_SIZE
        )
        if length < 0 or offset < 0:
            raise PartitionError(f"lump {index} declares offset {offset} length {length}")
        if length and offset + length > len(data):
            raise PartitionError(
                f"lump {index} runs to {offset + length} past the {len(data)}-byte file"
            )
        if length and (version or four_cc != b"\0\0\0\0"):
            # The census over all 108 maps finds version 0 and a zero fourCC on every populated
            # row; a row that departs is carried rather than rejected.
            anomalies.append(
                {
                    "role": "directory-version-nonzero",
                    "lump": index,
                    "version": int(version),
                    "fourCC": _four_cc(four_cc),
                    "sourceOffset": LUMP_DIRECTORY_OFFSET + index * LUMP_ENTRY_SIZE,
                }
            )
        lumps.append(
            LumpSpan(
                index=index,
                name=LUMP_NAMES[index],
                offset=int(offset),
                length=int(length),
                version=int(version),
                four_cc=_four_cc(four_cc),
                owner=_owner_of(index),
            )
        )
    map_revision = struct.unpack_from(
        "<i", data, LUMP_DIRECTORY_OFFSET + LUMP_COUNT * LUMP_ENTRY_SIZE
    )[0]
    return lumps, int(map_revision), anomalies


def _read_game_lumps(data: bytes, directory: LumpSpan) -> tuple[list[GameLumpEntry], int]:
    """The rows of lump 35's directory, and the byte the directory itself ends at."""

    if not directory.populated:
        return [], directory.offset
    if directory.length < 4:
        raise PartitionError(f"game lump directory is {directory.length} bytes")
    count = struct.unpack_from("<i", data, directory.offset)[0]
    if count < 0 or directory.offset + 4 + count * 16 > directory.end:
        raise PartitionError(f"game lump directory declares {count} entries")
    entries: list[GameLumpEntry] = []
    for index in range(count):
        row = directory.offset + 4 + index * 16
        disk, flags, version, offset, length = struct.unpack_from("<4sHHii", data, row)
        identifier = disk[::-1].decode("ascii", "replace")
        if length < 0 or offset < 0 or offset + length > len(data):
            raise PartitionError(f"game lump {identifier} names {offset}+{length}")
        if length and not (directory.offset <= offset and offset + length <= directory.end):
            raise PartitionError(
                f"game lump {identifier} payload {offset}+{length} leaves lump 35"
            )
        entries.append(
            GameLumpEntry(
                index=index,
                id=identifier,
                disk_id=disk.decode("ascii", "replace"),
                flags=int(flags),
                version=int(version),
                offset=int(offset),
                length=int(length),
                owner=LIGHTING_UNIT if identifier in LIGHTING_GAME_LUMPS else ROOT_UNIT,
                row_offset=row,
            )
        )
    return entries, directory.offset + 4 + count * 16


def _trailer(data: bytes, claimed_end: int) -> dict[str, Any] | None:
    """The 10-byte tail every map carries, when the lumps leave room for exactly it."""

    if len(data) - claimed_end != TRAILER_BYTES:
        return None
    if data[-len(TRAILER_SIGNATURE):] != TRAILER_SIGNATURE:
        return None
    value = struct.unpack_from("<I", data, len(data) - TRAILER_BYTES)[0]
    return {
        "sourceOffset": len(data) - TRAILER_BYTES,
        "byteLength": TRAILER_BYTES,
        "value": int(value),
        "signature": TRAILER_SIGNATURE.decode("ascii"),
    }


def _fill(data: bytes, start: int, end: int, scope: str, lump: int | None) -> Region:
    """The bytes between two claimed extents: zero fill, or evidence-backed dead storage."""

    chunk = data[start:end]
    if any(chunk):
        return Region(
            start,
            end - start,
            "omitted-proven",
            f"{scope}.inter-lump-fill",
            ROOT_UNIT,
            lump=lump,
            sha256=hashlib.sha256(chunk).hexdigest(),
        )
    return Region(start, end - start, "padding-zero", f"{scope}.padding", ROOT_UNIT, lump=lump)


def _reject_overlaps(regions: Iterable[Region]) -> None:
    cursor = 0
    previous = "header"
    for region in regions:
        if region.offset < cursor:
            raise PartitionError(
                f"{region.owner} at {region.offset} overlaps {previous}, which ends at {cursor}"
            )
        cursor = region.offset + region.length
        previous = region.owner


def partition(bsp: bytes) -> Partition:
    """Partition one BSP v17 member into the four units' regions and the bytes nobody decodes.

    Every byte lands in exactly one `Region`: the header and directory, one region per populated
    lump (the game lump split into its directory and its per-entry payloads), the trailer, and
    the inter-lump bytes no directory row claims -- `padding-zero` where the source stores zeros
    and `omitted-proven inter-lump-fill` where it does not. An overlap between two lumps fails
    the map, because a byte with two owners has no ledger.
    """

    data = bytes(bsp)
    if len(data) < HEADER_BYTES:
        raise PartitionError(f"a BSP is at least {HEADER_BYTES} bytes; this one is {len(data)}")
    ident, version = struct.unpack_from("<4si", data, 0)
    if ident != BSP_IDENT:
        raise PartitionError(f"file opens with {ident!r}, not {BSP_IDENT!r}")
    if version != BSP_VERSION:
        raise PartitionError(f"BSP version {version}; this seam decodes v{BSP_VERSION}")

    lumps, map_revision, anomalies = _read_directory(data)
    game_lumps, directory_end = _read_game_lumps(data, lumps[GAME_LUMP_INDEX])

    regions: list[Region] = [Region(0, HEADER_BYTES, "mapped", "header", ROOT_UNIT)]
    for lump in lumps:
        if not lump.populated:
            continue
        if lump.index == GAME_LUMP_INDEX:
            regions.append(
                Region(
                    lump.offset,
                    directory_end - lump.offset,
                    "mapped",
                    "header.gameLumpDirectory",
                    ROOT_UNIT,
                    lump=lump.index,
                )
            )
            cursor = directory_end
            for entry in sorted(game_lumps, key=lambda row: row.offset):
                if not entry.length:
                    continue
                if entry.offset < cursor:
                    raise PartitionError(f"game lump {entry.id} payload overlaps byte {cursor}")
                if entry.offset > cursor:
                    regions.append(_fill(data, cursor, entry.offset, "gameLump", lump.index))
                owner = (
                    f"gameLump[{entry.id}]"
                    if entry.owner == ROOT_UNIT
                    else f"subUnits[{entry.owner}].gameLump[{entry.id}]"
                )
                regions.append(
                    Region(
                        entry.offset,
                        entry.length,
                        "mapped" if entry.owner == ROOT_UNIT else "omitted-proven",
                        owner,
                        entry.owner,
                        lump=lump.index,
                        game_lump=entry.id,
                        sha256=hashlib.sha256(data[entry.offset:entry.end]).hexdigest(),
                    )
                )
                cursor = entry.end
            if cursor < lump.end:
                regions.append(_fill(data, cursor, lump.end, "gameLump", lump.index))
            continue
        owner = (
            f"lumps[{lump.index}]"
            if lump.owner == ROOT_UNIT
            else f"subUnits[{lump.owner}].lump[{lump.index}]"
        )
        regions.append(
            Region(
                lump.offset,
                lump.length,
                "mapped" if lump.owner == ROOT_UNIT else "omitted-proven",
                owner,
                lump.owner,
                lump=lump.index,
                sha256=(
                    hashlib.sha256(data[lump.offset:lump.end]).hexdigest()
                    if lump.owner != ROOT_UNIT
                    else None
                ),
            )
        )

    regions.sort(key=lambda region: region.offset)
    _reject_overlaps(regions)

    claimed_end = max((region.offset + region.length for region in regions), default=0)
    trailer = _trailer(data, claimed_end)

    filled: list[Region] = []
    cursor = 0
    for region in regions:
        if region.offset > cursor:
            filled.append(_fill(data, cursor, region.offset, "file", None))
        filled.append(region)
        cursor = region.offset + region.length
    if trailer is not None:
        filled.append(
            Region(len(data) - TRAILER_BYTES, TRAILER_BYTES, "mapped", "trailer", ROOT_UNIT)
        )
    elif cursor < len(data):
        filled.append(_fill(data, cursor, len(data), "file", None))

    result = Partition(
        byte_length=len(data),
        sha256=hashlib.sha256(data).hexdigest(),
        ident=ident.decode("ascii"),
        version=int(version),
        map_revision=map_revision,
        lumps=tuple(lumps),
        game_lumps=tuple(game_lumps),
        regions=tuple(filled),
        trailer=trailer,
        anomalies=tuple(anomalies),
    )
    result.verify(data)
    return result

