"""Resolve each fired contribution into the exact byte spans retail dereferenced.

Usage:
    uv run elysium research resolve_consumed_spans <session> [<session> ...]

Reads a finalized capture database, walks each contribution's owner image the
way the retail decoders walk it, and writes the resulting byte spans back into
the same database as a deduplicated dictionary.

This is the half of CAP2.5 that is *derived*. The probe witnesses pointers at
the hook boundary and decodes nothing; this walker predicts the same pointers
from the image alone, and `verify_consumed_spans` checks the two against each
other. That is why nothing here imports the exporter's own decoder: if the same
walker produced both sides of CAP4.2's byte-coverage difference, the difference
would be a tautology rather than a test.

Every rule below is transcribed from a confirmed finding in
`research/cases/animation-pose/specs/animation_pose.json`, cited beside it.
Where the specification pins no read, the bytes stay unclaimed: a span this
walker does not emit is reported as unknown, never as inert.

Three bounds travel with the output.

The walk is to the sampled frame, not over the clip. Both decoders read the two
header bytes of each run they skip and then only the keys bracketing the frame,
so a consumed span inside a track is a small non-contiguous set. The exporter's
own walk covers the whole track, and that difference is by construction rather
than a defect.

A zero-weight animation record ends the decode. Such a cell dereferences the
four weight bytes and nothing else -- no channel offset, no track, no bind
field.

The sequence descriptor is only partly claimed. The fields the specification
pins are emitted; the rest of the 764 bytes are left unclaimed rather than
attributed to a read nobody has witnessed.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import sqlite3
import struct
import subprocess
from typing import Any, Iterable

from research.tooling.capture.calibrate_theatre_capture import (
    DATABASE_NAME,
    resolve_session,
)

RESOLVER_VERSION = 1
REPORT_NAME = "consumed-spans.json"

SEQUENCE_KIND = "SEQP"
ANIMATION_KIND = "ANIM"

# MDLHeader displacements. Every *Index is an offset from the header base, and
# the runtime header is the file image at offset zero (docs/vtmb/mdl_v2531.md).
HEADER_NUM_BONES = 240
HEADER_BONE_INDEX = 244
HEADER_NUM_LOCAL_ANIMS = 264
HEADER_LOCAL_ANIM_INDEX = 268
HEADER_NUM_LOCAL_SEQ = 272
HEADER_LOCAL_SEQ_INDEX = 276

ANIM_DESC_STRIDE = 72
SEQ_DESC_STRIDE = 764
ANIM_RECORD_STRIDE = 32
BONE_STRIDE = 160

# StudioAnimDesc fields decode_selected_bones reads: numframes at +0xc and
# animindex at +0x30, and nothing else in the descriptor.
ANIM_DESC_NUMFRAMES = 12
ANIM_DESC_ANIMINDEX = 48

# StudioBone fields the two decoders read, per channel. The quaternion decoder
# walks a cursor from bone+0x48 and reads cursor[-7] on the unanimated branch;
# the position decoder walks one from bone+0x3c and reads cursor[-7] always.
BONE_POS = 32
BONE_QUAT = 44
BONE_POSSCALE = 60
BONE_ROTSCALE = 72

# StudioSeqDesc fields the sequence evaluator resolves a cell through.
SEQ_NUM_BLENDS = 52
SEQ_ANIM_GRID = 56
SEQ_GRID_ROW = 16
SEQ_GROUP_SIZE = 572
SEQ_PARAM_INDEX = 580

# posX, posY, posZ handled by the position decoder; rotX..rotW by the
# quaternion decoder. Offsets live at record+4, one dword per channel.
POSITION_CHANNELS = (0, 1, 2)
QUATERNION_CHANNELS = (3, 4, 5, 6)

ROLE_HEADER_FIELD = "header_field"
ROLE_SEQUENCE_DESCRIPTOR = "sequence_descriptor"
ROLE_SEQUENCE_GRID = "sequence_grid"
ROLE_ANIMATION_DESCRIPTOR = "animation_descriptor"
ROLE_ANIM_RECORD = "anim_record"
ROLE_BONE_BIND = "bone_bind"
ROLE_BONE_SCALE = "bone_scale"
ROLE_TRACK_HEADER = "track_header"
ROLE_TRACK_KEY = "track_key"

ROLES = (
    ROLE_HEADER_FIELD,
    ROLE_SEQUENCE_DESCRIPTOR,
    ROLE_SEQUENCE_GRID,
    ROLE_ANIMATION_DESCRIPTOR,
    ROLE_ANIM_RECORD,
    ROLE_BONE_BIND,
    ROLE_BONE_SCALE,
    ROLE_TRACK_HEADER,
    ROLE_TRACK_KEY,
)

# A malformed run would otherwise let the skip loop advance forever inside a
# valid image. The bound is far above any real clip, so hitting it is a fault.
MAX_TRACK_RUNS = 4096

# What is stored, and why it is not one span set per cell.
#
# A cutscene samples one clip at thousands of distinct frames -- measured, 54
# clips produce 143,610 distinct (clip, frame, mask) shapes but only 122
# distinct (clip, mask) ones. The frame changes nothing except which run headers
# a track walk steps over and which two keys it reads; the header fields, the
# descriptor fields, the per-bone records and the bind and scale fields are the
# same at every frame. Storing a set per shape therefore writes the same 90% of
# it a thousand times, which is a gigabyte of the same bytes.
#
# So three things are stored and one is left derivable:
#
#   span_sets      the 122 frame-invariant shapes, delta-encoded as a blob
#   model_coverage the per-model union of everything, tracks included -- the
#                  product CAP4.2 subtracts our decoder's coverage from
#   span_roles     that union broken down by what asked for each byte
#
# and a cell's exact spans at its own frame are regenerated from its record and
# its owner image by the same walker, in microseconds. Nothing is lost that is
# not recomputed on demand, and the store is bounded by the size of the images
# rather than by the number of cells.
SCHEMA = """
CREATE TABLE span_sets (
    id INTEGER PRIMARY KEY,
    checksum INTEGER NOT NULL,
    digest TEXT NOT NULL UNIQUE,
    kind TEXT NOT NULL,
    shape TEXT NOT NULL,
    interval_count INTEGER NOT NULL,
    byte_count INTEGER NOT NULL,
    first_offset INTEGER NOT NULL,
    last_offset INTEGER NOT NULL,
    intervals BLOB NOT NULL
);
CREATE TABLE model_coverage (
    checksum INTEGER NOT NULL,
    ordinal INTEGER NOT NULL,
    start INTEGER NOT NULL,
    "end" INTEGER NOT NULL,
    PRIMARY KEY (checksum, ordinal)
) WITHOUT ROWID;
CREATE TABLE span_roles (
    checksum INTEGER NOT NULL,
    role TEXT NOT NULL,
    intervals INTEGER NOT NULL,
    bytes INTEGER NOT NULL,
    PRIMARY KEY (checksum, role)
) WITHOUT ROWID;
CREATE TABLE record_span_sets (
    record_id INTEGER PRIMARY KEY REFERENCES {records_table}(id),
    span_set_id INTEGER REFERENCES span_sets(id),
    frame INTEGER,
    resolved INTEGER NOT NULL,
    fault TEXT
);
CREATE INDEX span_sets_checksum ON span_sets(checksum);
"""
# An indexed database reaches its event rows through a view, and a foreign key
# cannot reference one. SQLite accepts the CREATE either way and only refuses at
# the first insert -- by which point this pass has already dropped the previous
# dictionary -- so the parent is chosen from the catalogue rather than assumed.
EVENT_TABLE = "record_events"
DEFAULT_EVENT_TABLE = "records"


class Coverage:
    """One bit per byte of one owner image.

    A union over tens of millions of intervals is a set-union problem, and a bit
    per byte solves it in the space of the image rather than the space of the
    evidence: merging is an OR, double counting is impossible, and the interval
    list falls out of one pass at the end.
    """

    def __init__(self, size: int) -> None:
        self.size = size
        self.bits = bytearray((size + 7) // 8)
        self.roles: dict[str, list[int]] = {}

    def add(self, spans: Iterable[tuple[int, int, str]]) -> None:
        """Mark a run of spans consumed.

        Whole bytes are filled by slice assignment and only the two partial
        bytes at each end are touched individually, so the cost is a constant
        few operations per span rather than one per byte covered.
        """
        bits = self.bits
        for start, end, role in spans:
            tally = self.roles.setdefault(role, [0, 0])
            tally[0] += 1
            tally[1] += end - start
            end = min(end, self.size)
            if start >= end:
                continue
            first = (start + 7) >> 3
            last = end >> 3
            if first < last:
                bits[first:last] = b"\xff" * (last - first)
                for offset in range(start, first << 3):
                    bits[offset >> 3] |= 1 << (offset & 7)
                for offset in range(last << 3, end):
                    bits[offset >> 3] |= 1 << (offset & 7)
            else:
                for offset in range(start, end):
                    bits[offset >> 3] |= 1 << (offset & 7)

    def intervals(self) -> list[tuple[int, int]]:
        spans: list[tuple[int, int]] = []
        start = None
        for offset in range(self.size):
            if self.bits[offset >> 3] & (1 << (offset & 7)):
                if start is None:
                    start = offset
            elif start is not None:
                spans.append((start, offset))
                start = None
        if start is not None:
            spans.append((start, self.size))
        return spans

    def byte_count(self) -> int:
        return sum(bin(byte).count("1") for byte in self.bits)

SPAN_TABLES = (
    "span_sets",
    "model_coverage",
    "span_roles",
    "record_span_sets",
)
# Shapes this pass has had before. A database resolved by one of them carries
# tables the current schema does not name, and they hold references that would
# refuse the drop.
LEGACY_SPAN_TABLES = ("span_set_intervals",)


def encode_intervals(spans: list[tuple[int, int, str]]) -> bytes:
    """Delta-encode merged intervals as varints against the previous end."""
    blob = bytearray()
    previous = 0
    for start, end, role in spans:
        for value in (start - previous, end - start):
            # Zigzag, because a span set is sorted by start and a role change
            # can still step backwards.
            magnitude = (value << 1) if value >= 0 else ((-value) << 1) | 1
            while True:
                byte = magnitude & 0x7F
                magnitude >>= 7
                blob.append(byte | (0x80 if magnitude else 0))
                if not magnitude:
                    break
        blob.append(ROLES.index(role))
        previous = end
    return bytes(blob)


def decode_intervals(blob: bytes) -> list[tuple[int, int, str]]:
    """The inverse of `encode_intervals`, so a stored set stays readable."""
    spans: list[tuple[int, int, str]] = []
    cursor = 0
    previous = 0
    while cursor < len(blob):
        values = []
        for _ in range(2):
            magnitude = 0
            shift = 0
            while True:
                byte = blob[cursor]
                cursor += 1
                magnitude |= (byte & 0x7F) << shift
                shift += 7
                if not byte & 0x80:
                    break
            values.append(
                -(magnitude >> 1) if magnitude & 1 else magnitude >> 1
            )
        start = previous + values[0]
        end = start + values[1]
        spans.append((start, end, ROLES[blob[cursor]]))
        cursor += 1
        previous = end
    return spans


class SpanFault(Exception):
    """A walk left the image or read a structure the rules do not describe."""


class Walker:
    """One owner image, walked by the retail rules.

    Bounds are checked on every read rather than once at the end, so a fault
    names the structure that ran off the image instead of surfacing as a wrong
    span the caller cannot distinguish from a real one.
    """

    def __init__(self, image: bytes, capped: bool) -> None:
        self.image = image
        self.capped = capped

    def _check(self, offset: int, size: int, what: str) -> None:
        if offset < 0 or offset + size > len(self.image):
            raise SpanFault(f"{what} at {offset} leaves the {len(self.image)}-byte image")

    def u8(self, offset: int, what: str) -> int:
        self._check(offset, 1, what)
        return self.image[offset]

    def i32(self, offset: int, what: str) -> int:
        self._check(offset, 4, what)
        return struct.unpack_from("<i", self.image, offset)[0]

    def f32(self, offset: int, what: str) -> float:
        self._check(offset, 4, what)
        return struct.unpack_from("<f", self.image, offset)[0]


def walk_track(
    walker: Walker,
    track: int,
    frame: int,
    *,
    quaternion: bool,
) -> list[tuple[int, int, str]]:
    """The bytes one channel decoder reads inside one RLE track.

    Transcribed from the confirmed walk: total is read at track+1 and subtracted
    from the frame while it does not exceed what remains, advancing by
    valid*2+2; then the keys bracketing the frame are read out of the run that
    holds it, reaching the following run's first key when the frame after it
    leaves the run. The position decoder additionally resets the frame to zero
    on any run declaring total below its key count.
    """
    spans: list[tuple[int, int, str]] = []
    cursor = track
    valid = walker.u8(cursor, "run valid")
    total = walker.u8(cursor + 1, "run total")
    spans.append((cursor, cursor + 2, ROLE_TRACK_HEADER))
    remaining = frame
    if not quaternion and total < valid:
        remaining = 0
    runs = 0
    while total <= remaining:
        runs += 1
        if runs > MAX_TRACK_RUNS:
            raise SpanFault(f"track at {track} did not reach frame {frame}")
        remaining -= total
        cursor = cursor + valid * 2 + 2
        valid = walker.u8(cursor, "run valid")
        total = walker.u8(cursor + 1, "run total")
        spans.append((cursor, cursor + 2, ROLE_TRACK_HEADER))
        if not quaternion and total < valid:
            remaining = 0

    def key(offset: int) -> None:
        walker._check(offset, 2, "track key")
        spans.append((offset, offset + 2, ROLE_TRACK_KEY))

    if remaining < valid:
        key(cursor + 2 + remaining * 2)
        if remaining + 1 < valid:
            key(cursor + 2 + (remaining + 1) * 2)
        elif quaternion and total <= remaining + 1:
            # Only the quaternion decoder reaches into the following run from
            # inside a run it has not exhausted; the position decoder holds the
            # key it already has.
            key(cursor + valid * 2 + 4)
    else:
        key(cursor + valid * 2)
        if quaternion:
            if total <= remaining + 1:
                key(cursor + valid * 2 + 4)
        elif remaining + 1 >= total:
            key(cursor + valid * 2 + 4)
    return spans


def resolve_cell(
    walker: Walker,
    animation_index: int,
    frame: int,
    quaternion_bones: Iterable[int],
    position_bones: Iterable[int],
    *,
    tracks: bool = True,
) -> list[tuple[int, int, str]]:
    """The bytes one decoded blend cell dereferences in its owner image.

    With `tracks` false the walk stops short of every RLE track, which yields
    exactly the part of a cell that does not depend on its frame.
    """
    anims = walker.i32(HEADER_NUM_LOCAL_ANIMS, "NumLocalAnims")
    anim_base = walker.i32(HEADER_LOCAL_ANIM_INDEX, "LocalAnimIndex")
    bone_count = walker.i32(HEADER_NUM_BONES, "NumBones")
    bone_base = walker.i32(HEADER_BONE_INDEX, "BoneIndex")
    if not 0 <= animation_index < anims:
        raise SpanFault(
            f"animation index {animation_index} outside the owner's {anims}"
        )
    desc = anim_base + animation_index * ANIM_DESC_STRIDE
    # The cell frame reads the bone count and the bone array base out of the
    # header, then numframes and animindex out of the descriptor, before it
    # looks at the mask at all. A cell whose mask selects nothing reads exactly
    # this much and stops, which is a real outcome rather than a lost witness.
    spans: list[tuple[int, int, str]] = [
        (HEADER_NUM_BONES, HEADER_NUM_BONES + 4, ROLE_HEADER_FIELD),
        (HEADER_BONE_INDEX, HEADER_BONE_INDEX + 4, ROLE_HEADER_FIELD),
        (
            desc + ANIM_DESC_NUMFRAMES,
            desc + ANIM_DESC_NUMFRAMES + 4,
            ROLE_ANIMATION_DESCRIPTOR,
        ),
        (
            desc + ANIM_DESC_ANIMINDEX,
            desc + ANIM_DESC_ANIMINDEX + 4,
            ROLE_ANIMATION_DESCRIPTOR,
        ),
    ]
    block = desc + walker.i32(desc + ANIM_DESC_ANIMINDEX, "animindex")
    decoded = sorted(set(quaternion_bones) | set(position_bones))
    if not decoded:
        return spans
    quaternion_set = set(quaternion_bones)
    position_set = set(position_bones)
    for bone in decoded:
        if not 0 <= bone < bone_count:
            raise SpanFault(f"bone {bone} outside the owner's {bone_count}")
        record = block + bone * ANIM_RECORD_STRIDE
        # Both decoders read the weight first and return on zero, so a
        # zero-weight record costs four bytes and nothing else.
        spans.append((record, record + 4, ROLE_ANIM_RECORD))
        if walker.f32(record, "record weight") == 0.0:
            continue
        bone_offset = bone_base + bone * BONE_STRIDE
        for channel in range(7):
            is_quaternion = channel in QUATERNION_CHANNELS
            if is_quaternion and bone not in quaternion_set:
                continue
            if not is_quaternion and bone not in position_set:
                continue
            slot = record + 4 + channel * 4
            spans.append((slot, slot + 4, ROLE_ANIM_RECORD))
            track = walker.i32(slot, "channel offset")
            if is_quaternion:
                component = channel - QUATERNION_CHANNELS[0]
                bind = bone_offset + BONE_QUAT + component * 4
                scale = bone_offset + BONE_ROTSCALE + component * 4
            else:
                component = channel
                bind = bone_offset + BONE_POS + component * 4
                scale = bone_offset + BONE_POSSCALE + component * 4
                # The position decoder seeds every channel from the bind
                # position before it looks at the offset at all.
                spans.append((bind, bind + 4, ROLE_BONE_BIND))
            if track == 0:
                if is_quaternion:
                    spans.append((bind, bind + 4, ROLE_BONE_BIND))
                continue
            spans.append((scale, scale + 4, ROLE_BONE_SCALE))
            if tracks:
                spans.extend(
                    walk_track(
                        walker, record + track, frame, quaternion=is_quaternion
                    )
                )
    return spans


def resolve_sequence(
    walker: Walker,
    sequence_index: int,
    cells: Iterable[int],
) -> list[tuple[int, int, str]]:
    """The sequence-descriptor fields the evaluator resolves a cell through.

    Only the fields the specification pins are claimed. The remaining bytes of
    the 764 stay unclaimed, so CAP4.2 records them as unknown rather than as
    read by nobody.
    """
    count = walker.i32(HEADER_NUM_LOCAL_SEQ, "NumLocalSeq")
    base = walker.i32(HEADER_LOCAL_SEQ_INDEX, "LocalSeqIndex")
    if not 0 <= sequence_index < count:
        raise SpanFault(
            f"sequence index {sequence_index} outside the owner's {count}"
        )
    desc = base + sequence_index * SEQ_DESC_STRIDE
    spans = [
        (desc + SEQ_NUM_BLENDS, desc + SEQ_NUM_BLENDS + 4, ROLE_SEQUENCE_DESCRIPTOR),
        (desc + SEQ_GROUP_SIZE, desc + SEQ_GROUP_SIZE + 8, ROLE_SEQUENCE_DESCRIPTOR),
        (desc + SEQ_PARAM_INDEX, desc + SEQ_PARAM_INDEX + 8, ROLE_SEQUENCE_DESCRIPTOR),
    ]
    for cell in cells:
        if not 0 <= cell < SEQ_GRID_ROW * SEQ_GRID_ROW:
            raise SpanFault(f"blend cell {cell} outside the 16x16 grid")
        slot = desc + SEQ_ANIM_GRID + cell * 2
        spans.append((slot, slot + 2, ROLE_SEQUENCE_GRID))
    for start, end, _ in spans:
        walker._check(start, end - start, "sequence descriptor field")
    return spans


def merge(spans: Iterable[tuple[int, int, str]]) -> list[tuple[int, int, str]]:
    """Coalesce touching spans within a role, keeping the role tag for CAP4.2."""
    merged: list[tuple[int, int, str]] = []
    for role in ROLES:
        ordered = sorted(
            (start, end) for start, end, tag in spans if tag == role
        )
        for start, end in ordered:
            if merged and merged[-1][2] == role and start <= merged[-1][1]:
                merged[-1] = (merged[-1][0], max(merged[-1][1], end), role)
            else:
                merged.append((start, end, role))
    merged.sort(key=lambda span: (span[0], span[1], span[2]))
    return merged


def union_bytes(spans: Iterable[tuple[int, int, str]]) -> int:
    """Bytes covered at least once, counting an offset claimed twice once."""
    total = 0
    high = -1
    for start, end, _ in sorted(spans):
        if end <= high:
            continue
        total += end - max(start, high)
        high = end
    return total


def digest_of(checksum: int, spans: list[tuple[int, int, str]]) -> str:
    body = hashlib.sha256()
    body.update(struct.pack("<II", checksum & 0xFFFFFFFF, len(spans)))
    for start, end, role in spans:
        body.update(struct.pack("<II", start, end))
        body.update(role.encode("ascii") + b"\0")
    return body.hexdigest()


def bitmap_bones(payload: bytes, words: int, second: bool) -> list[int]:
    """One witnessed-bone bitmap, quaternion first then position."""
    half = words * 4
    start = half if second else 0
    chunk = payload[start : start + half]
    return [
        index
        for index in range(half * 8)
        if chunk[index // 8] & (1 << (index % 8))
    ]


def tool_commit() -> str | None:
    try:
        result = subprocess.run(
            ["git", "rev-parse", "HEAD"],
            cwd=Path(__file__).resolve().parents[3],
            capture_output=True,
            text=True,
            check=False,
        )
    except OSError:
        return None
    return result.stdout.strip() or None


def file_sha256(path: Path) -> str:
    body = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            body.update(chunk)
    return body.hexdigest()


def load_images(connection: sqlite3.Connection) -> dict[int, Walker]:
    return {
        int(checksum): Walker(bytes(image), bool(capped))
        for checksum, image, capped in connection.execute(
            "SELECT checksum, image, capped FROM model_images"
        )
    }


def resolve(
    session: Path,
    *,
    stamped_utc: str | None = None,
    rewrite: bool = False,
) -> dict[str, Any]:
    """Walk one finalized database and write its span dictionary back into it.

    The database is evidence, so the write is guarded three ways: it refuses a
    database that already carries the tables, it records the digest the
    finalizer reported before the pass so anything comparing against
    `result.json` reads that instead, and it commits once.
    """
    database = session / DATABASE_NAME
    if not database.is_file():
        raise FileNotFoundError(f"no finalized database at {database}")
    source_digest = file_sha256(database)
    connection = sqlite3.connect(database)
    connection.execute("PRAGMA foreign_keys = ON")
    try:
        existing = {
            name
            for (name,) in connection.execute(
                "SELECT name FROM sqlite_master WHERE type = 'table'"
            )
        }
        already = sorted(set(SPAN_TABLES) & existing)
        if already and not rewrite:
            raise ValueError(
                f"{database} already carries {', '.join(already)}; "
                "a finalized database is resolved once. Pass --rewrite to "
                "replace the dictionary after a walker change."
            )
        if already:
            # The dictionary is derived, so replacing it costs nothing that is
            # not regenerable; the guard exists to make a second pass
            # deliberate rather than to make one impossible. Tables an earlier
            # shape of this pass created go too, or one left holding a foreign
            # key into span_sets would refuse the drop.
            connection.execute("PRAGMA foreign_keys = OFF")
            for table in (*LEGACY_SPAN_TABLES, *reversed(SPAN_TABLES)):
                connection.execute(f"DROP TABLE IF EXISTS {table}")
            connection.execute("PRAGMA foreign_keys = ON")
        connection.executescript(
            SCHEMA.format(
                records_table=(
                    EVENT_TABLE if EVENT_TABLE in existing else DEFAULT_EVENT_TABLE
                )
            )
        )
        images = load_images(connection)
        rows = connection.execute(
            """
            SELECT id, kind, owner_checksum, bone_count, sequence_index,
                   animation_index, channel_frame, blend_cell, group_size,
                   num_blends, channel_bone_bytes, raw_payload
            FROM records
            WHERE kind IN (?, ?)
            ORDER BY id
            """,
            (SEQUENCE_KIND, ANIMATION_KIND),
        ).fetchall()

        shapes: dict[tuple, int] = {}
        digests: dict[str, int] = {}
        counts = {
            "records": 0,
            "resolved": 0,
            "faulted": 0,
            "shape_hits": 0,
            "digest_hits": 0,
            "capped_owners": 0,
            "decoded_nothing": 0,
        }
        coverage: dict[int, Coverage] = {}
        walked_frames: set[tuple] = set()
        faults: dict[str, int] = {}
        for (
            record_id,
            kind,
            checksum,
            bone_count,
            sequence_index,
            animation_index,
            channel_frame,
            blend_cell,
            group_size,
            num_blends,
            channel_bone_bytes,
            payload,
        ) in rows:
            counts["records"] += 1
            walker = images.get(int(checksum or 0))
            key: tuple
            if walker is None:
                _record_fault(
                    connection, faults, counts, record_id, "owner image absent"
                )
                continue
            if walker.capped:
                counts["capped_owners"] += 1
            if kind == ANIMATION_KIND:
                if channel_bone_bytes is None:
                    _record_fault(
                        connection, faults, counts, record_id,
                        "cell predates the channel accumulator",
                    )
                    continue
                words = int(channel_bone_bytes) // 8
                quaternion = bitmap_bones(payload or b"", words, False)
                position = bitmap_bones(payload or b"", words, True)
                if not quaternion and not position:
                    # No decoder ran, so the frame the cell would have sampled
                    # is not part of what it read. Keying it in would split one
                    # shape across every frame for no difference in bytes.
                    counts["decoded_nothing"] += 1
                    channel_frame = 0
                # The frame is deliberately absent: it changes only the track
                # spans, which this set does not carry.
                key = (
                    int(checksum),
                    int(animation_index),
                    tuple(quaternion),
                    tuple(position),
                )
            else:
                cells = json.loads(blend_cell) if blend_cell else [0, 0]
                widths = json.loads(group_size) if group_size else [1, 1]
                fired = _fired_cells(cells, widths, int(num_blends or 1))
                key = (int(checksum), int(sequence_index), tuple(fired))
            owner = coverage.setdefault(
                int(checksum), Coverage(len(walker.image))
            )
            # The union is idempotent, so a cell repeating a shape at a frame
            # already walked contributes nothing and is not walked again. This
            # is where nearly all the work goes -- a cutscene runs one clip
            # through the same frames on many actors -- so the set is the
            # difference between minutes and seconds.
            try:
                if kind == ANIMATION_KIND:
                    walked_key = (key, int(channel_frame))
                    if walked_key not in walked_frames:
                        walked_frames.add(walked_key)
                        owner.add(
                            resolve_cell(
                                walker,
                                int(animation_index),
                                int(channel_frame),
                                quaternion,
                                position,
                            )
                        )
            except SpanFault as error:
                _record_fault(connection, faults, counts, record_id, str(error))
                continue
            hit = shapes.get(key)
            if hit is not None:
                counts["shape_hits"] += 1
                counts["resolved"] += 1
                connection.execute(
                    "INSERT INTO record_span_sets VALUES (?, ?, ?, 1, NULL)",
                    (record_id, hit, channel_frame),
                )
                continue
            try:
                if kind == ANIMATION_KIND:
                    spans = resolve_cell(
                        walker,
                        int(animation_index),
                        int(channel_frame),
                        quaternion,
                        position,
                        tracks=False,
                    )
                else:
                    spans = resolve_sequence(walker, int(sequence_index), fired)
                    owner.add(spans)
            except SpanFault as error:
                _record_fault(
                    connection, faults, counts, record_id, str(error)
                )
                continue
            merged = merge(spans)
            digest = digest_of(int(checksum), merged)
            span_set = digests.get(digest)
            if span_set is None:
                cursor = connection.execute(
                    "INSERT INTO span_sets "
                    "(checksum, digest, kind, shape, interval_count, "
                    "byte_count, first_offset, last_offset, intervals) "
                    "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)",
                    (
                        int(checksum),
                        digest,
                        kind,
                        # The shape this set is the spans of, so two runs can be
                        # asked whether the same shape produced the same bytes
                        # rather than only whether their unions happen to match.
                        json.dumps(key[1:], separators=(",", ":")),
                        len(merged),
                        union_bytes(merged),
                        merged[0][0],
                        merged[-1][1],
                        encode_intervals(merged),
                    ),
                )
                span_set = int(cursor.lastrowid)
                digests[digest] = span_set
            else:
                counts["digest_hits"] += 1
            shapes[key] = span_set
            counts["resolved"] += 1
            connection.execute(
                "INSERT INTO record_span_sets VALUES (?, ?, ?, 1, NULL)",
                (record_id, span_set, channel_frame),
            )

        # The per-model union is the product CAP4.2 subtracts our decoder's
        # coverage from, so it is computed once here rather than by re-reading
        # every span set later.
        model_bytes: dict[int, int] = {}
        model_intervals = 0
        for owner, collected in sorted(coverage.items()):
            union = collected.intervals()
            model_bytes[owner] = collected.byte_count()
            model_intervals += len(union)
            connection.executemany(
                "INSERT INTO model_coverage VALUES (?, ?, ?, ?)",
                [
                    (owner, ordinal, start, end)
                    for ordinal, (start, end) in enumerate(union)
                ],
            )
            # Role totals count one occurrence per distinct shape and frame
            # rather than per cell, because a repeat adds no byte to the union
            # and is never walked. The distinct byte total is the union above.
            connection.executemany(
                "INSERT INTO span_roles VALUES (?, ?, ?, ?)",
                [
                    (owner, role, count, total)
                    for role, (count, total) in sorted(collected.roles.items())
                ],
            )

        metadata = {
            "spans_source_database_sha256": source_digest,
            "spans_resolver_version": RESOLVER_VERSION,
            "spans_tool_git": tool_commit(),
        }
        if stamped_utc:
            metadata["spans_resolved_utc"] = stamped_utc
        # Every value in this table is JSON, and every reader loads it that way;
        # a bare string here parses as a syntax error in each of them.
        connection.executemany(
            "INSERT OR REPLACE INTO capture_metadata (key, value) VALUES (?, ?)",
            [(key, json.dumps(value)) for key, value in sorted(metadata.items())],
        )
        connection.commit()
        integrity = connection.execute("PRAGMA integrity_check").fetchone()[0]
    finally:
        connection.close()

    report = {
        "session": session.name,
        "database": str(database),
        "source_database_sha256": source_digest,
        "resolver_version": RESOLVER_VERSION,
        "integrity_check": integrity,
        "counts": counts,
        "distinct_span_sets": len(digests),
        "distinct_shapes": len(shapes),
        "faults": dict(sorted(faults.items())),
        "coverage_intervals": model_intervals,
        "bytes_per_model": {
            f"0x{owner:08x}": total for owner, total in sorted(model_bytes.items())
        },
    }
    (session / REPORT_NAME).write_text(
        json.dumps(report, indent=2) + "\n", encoding="utf-8"
    )
    return report


def _fired_cells(
    cells: list[int], group_size: list[int], num_blends: int
) -> tuple[int, ...]:
    """Which grid slots the evaluator selected, from the witnessed axis cells.

    The pinned rule is one, two or four cells on the two group sizes: an axis
    whose group size is one contributes its cell alone, and a wider one
    contributes the resolved cell and the one after it. The axis cells
    themselves are witnessed from the blend resolver rather than recomputed
    from the pose parameters.

    The cell after the last on an axis is not claimed. Whether the evaluator
    wraps or clamps there is not pinned by any finding, so those bytes stay
    unclaimed rather than attributed to a read nobody has witnessed.

    **Axis 0 takes the fixed 16-short row stride and axis 1 the column**, which
    is measured rather than transcribed: for every contribution, the animation
    indices the cell records name are read off the owner's own grid, so a wrong
    address names animations the capture did not fire. The address below
    reproduces the witnessed set on **8,302 of 8,302** multi-blend contributions
    across the theatre and tutorial captures; the transposed one reproduces
    1,858 — every case where the two coincide — and misses 6,444.
    """
    if num_blends <= 1:
        return (0,)
    axes = []
    for axis in range(2):
        resolved = cells[axis] if axis < len(cells) else 0
        width = group_size[axis] if axis < len(group_size) else 1
        span = [resolved]
        if width > 1 and resolved + 1 < width:
            span.append(resolved + 1)
        axes.append(span)
    return tuple(
        first * SEQ_GRID_ROW + second for second in axes[1] for first in axes[0]
    )


def _record_fault(
    connection: sqlite3.Connection,
    faults: dict[str, int],
    counts: dict[str, int],
    record_id: int,
    reason: str,
) -> None:
    counts["faulted"] += 1
    faults[reason] = faults.get(reason, 0) + 1
    connection.execute(
        "INSERT INTO record_span_sets VALUES (?, NULL, NULL, 0, ?)",
        (record_id, reason),
    )


def summarize(report: dict[str, Any]) -> str:
    counts = report["counts"]
    lines = [
        f"session                  {report['session']}",
        f"contributions            {counts['records']}",
        f"resolved                 {counts['resolved']}",
        f"faulted                  {counts['faulted']}",
        f"distinct span sets       {report['distinct_span_sets']}",
        f"distinct shapes          {report['distinct_shapes']}",
        f"shape reuse              {counts['shape_hits']}",
        f"cells decoding nothing   {counts['decoded_nothing']}",
        f"models covered           {len(report['bytes_per_model'])}",
        f"consumed bytes           {sum(report['bytes_per_model'].values())}",
        f"integrity check          {report['integrity_check']}",
    ]
    for reason, count in report["faults"].items():
        lines.append(f"  fault {reason}: {count}")
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("sessions", nargs="+")
    parser.add_argument("--stamped-utc", default=None)
    parser.add_argument("--rewrite", action="store_true")
    args = parser.parse_args()
    failed = False
    for value in args.sessions:
        session = resolve_session(value)
        report = resolve(
            session, stamped_utc=args.stamped_utc, rewrite=args.rewrite
        )
        print(summarize(report))
        print()
        if report["counts"]["faulted"]:
            failed = True
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
