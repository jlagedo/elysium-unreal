"""Finalize recoverable retail trace streams into one queryable SQLite file."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import sqlite3
import struct
from typing import BinaryIO, Iterator


POSE_FILE_HEADER = struct.Struct("<8sIIQqIIIII65s11s")
POSE_RECORD_HEADER = struct.Struct("<4sIQqIIIIII5I64s3II32s")
ANIMATION_FILE_HEADER = struct.Struct("<8sIIQqIIIII65sII3s")
ANIMATION_RECORD_HEADER = struct.Struct("<4sIQqIIIIIiffi6I")
BRACKET_RECORD_HEADER = struct.Struct("<4sIQqqIIIII9I")
# Streams captured before the pose-build generation existed. Their records are
# narrower and carry no generation, so the reader picks the header by stream
# version and those columns stay NULL. V3 is the generation-bearing record from
# before the composed-pose stage kept its root/entity transform.
POSE_RECORD_HEADER_V2 = struct.Struct("<4sIQqIIIIII5I64s")
POSE_RECORD_HEADER_V3 = struct.Struct("<4sIQqIIIIII5I64s3I")
ANIMATION_RECORD_HEADER_V2 = struct.Struct("<4sIQqIIIIIiffi2I")
ANIMATION_RECORD_HEADER_V3 = struct.Struct("<4sIQqIIIIIiffi4I")
# The census stream. Its rows are a dictionary rather than events, so they land
# in their own tables and never widen `records`.
CENSUS_FILE_HEADER = struct.Struct("<8sIIQqIIIII76s")
MODEL_OBSERVATION_HEADER = struct.Struct("<4sIQq13I128s")
MODEL_IMAGE_HEADER = struct.Struct("<4sIQq7I")
# The actor stream. Same split as the census: identity per sighting in its own
# table, never widening `records`.
ACTOR_FILE_HEADER = struct.Struct("<8sIIQqIIII80s")
ACTOR_OBSERVATION_HEADER = struct.Struct("<4sIQq10I64s")
# The actor stream from before the entity handle was recorded. Its rows carry no
# index, so that column stays NULL and byte closure reads the narrower width.
ACTOR_OBSERVATION_HEADER_V2 = struct.Struct("<4sIQq9I64s")
# The scene stream. Its rows are requests made outside every pose-build bracket,
# so they are neither a dictionary nor events on the generation spine, and they
# land in their own tables rather than widening `records` with a generation no
# scene record could carry.
SCENE_FILE_HEADER = struct.Struct("<8sIIQqIIIIIII65s3s")
SCENE_REQUEST_HEADER = struct.Struct("<4sIQqIIIIIIIIIIifffII128s64s128s128s64s")
SEQUENCE_CHANGE_HEADER = struct.Struct("<4sIQqI10I")
SCENE_REQUEST_KINDS = (b"SCNE", b"SEVT", b"SBND", b"SANM")
SEQUENCE_CHANGE_KIND = b"SEQC"
# Which reasons each scene magic may carry. A record naming a reason its own kind
# never emits did not come from the hook that writes this stream.
SCENE_REASONS = {
    b"SCNE": (1, 2, 3),
    b"SEVT": (4,),
    b"SBND": (5,),
    b"SANM": (6,),
}
# The contribution stream. Unlike the census and the actor stream these rows are
# events on the same generation spine as the evaluations they nest inside, so
# they land in `records` rather than in a table of their own.
CONTRIBUTION_FILE_HEADER = struct.Struct("<8sIIQqIIIIIII65s3s")
CONTRIBUTION_RECORD_HEADER = struct.Struct("<4sIQq9I2i3Ifi2i2i2i2f3I4IifI")
# The contribution stream from before the channel decoders were witnessed. Its
# cells carry no accumulator and its file header names three frames rather than
# five, so those columns stay NULL and byte closure reads the narrower width.
CONTRIBUTION_FILE_HEADER_V1 = struct.Struct("<8sIIQqIIIII65s11s")
CONTRIBUTION_RECORD_HEADER_V1 = struct.Struct("<4sIQq9I2i3Ifi2i2i2i2f3I")
# Mirrors kPoseParameterBytes: the 24 slots the include-model remap walks.
POSE_PARAMETER_BYTES = 96
CONTRIBUTION_KINDS = (b"SEQP", b"ANIM")
# The observation reasons that carry a model identity. Construction and
# destruction name an address and a time only.
ACTOR_IDENTITY_REASONS = (1, 2, 3)
MAXIMUM_BONES = 1024
# Mirrors kCensusImageCap in the probe; a record claiming more than this did
# not come from the hook that wrote the stream.
MAXIMUM_IMAGE_BYTES = 8 * 1024 * 1024
# Mirrors kRootTransformBytes: a 3x4 root/entity transform, or nothing on an
# evaluation that never receives one.
ROOT_TRANSFORM_BYTES = 48
# Mirrors kRenderInfoBytes: the eight dwords the engine writes into the render
# info and the studio draw reads back.
RENDER_INFO_BYTES = 32


def file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while block := stream.read(8 * 1024 * 1024):
            digest.update(block)
    return digest.hexdigest()


def read_key_values(path: Path) -> dict[str, str]:
    if not path.is_file():
        return {}
    values: dict[str, str] = {}
    for line in path.read_text(encoding="ascii", errors="replace").splitlines():
        key, separator, value = line.partition("=")
        if separator:
            values[key] = value
    return values


def _read_record(
    stream: BinaryIO,
    headers: dict[bytes, struct.Struct],
) -> tuple[bytes, bytes, struct.Struct] | bytes | None:
    """Read one record, choosing its header layout by magic.

    Magic and record length sit at the same offset in every record type, so
    one stream can carry differently shaped records and still be read without
    reconstructing where the boundaries are.
    """
    prefix = stream.read(8)
    if not prefix:
        return None
    if len(prefix) != 8:
        return prefix
    magic, record_bytes = struct.unpack("<4sI", prefix)
    header = headers.get(magic)
    if header is None or record_bytes < header.size:
        return prefix + stream.read()
    remainder = stream.read(header.size - 8)
    if len(remainder) != header.size - 8:
        return prefix + remainder
    raw_header = prefix + remainder
    payload = stream.read(record_bytes - header.size)
    if len(payload) != record_bytes - header.size:
        return raw_header + payload
    return raw_header, payload, header


def _pose_records(
    stream: BinaryIO,
    *,
    generations: bool,
    render_info: bool,
) -> Iterator[tuple[dict[str, object], bytes, bytes] | bytes]:
    if render_info:
        header = POSE_RECORD_HEADER
    elif generations:
        header = POSE_RECORD_HEADER_V3
    else:
        header = POSE_RECORD_HEADER_V2
    while True:
        record = _read_record(stream, {b"POSE": header})
        if record is None:
            return
        if isinstance(record, bytes):
            yield record
            return
        raw_header, payload, _ = record
        fields = header.unpack(raw_header)
        bone_count = int(fields[8])
        expected_bytes = header.size + bone_count * 12 * 4 * 2
        if (
            bone_count < 1
            or bone_count > MAXIMUM_BONES
            or int(fields[1]) != expected_bytes
        ):
            yield raw_header + payload + stream.read()
            return
        values: dict[str, object] = {
            "kind": "POSE",
            "sequence_number": int(fields[2]),
            "qpc": int(fields[3]),
            "thread_id": int(fields[4]),
            "studio_hdr": int(fields[5]),
            "client_entity": int(fields[6]),
            "checksum": int(fields[7]),
            "bone_count": bone_count,
            "model_info": int(fields[9]),
            "draw_arguments": json.dumps([int(value) for value in fields[10:15]]),
            "model_name": fields[15].split(b"\0", 1)[0].decode("ascii", "replace"),
        }
        if generations:
            values.update(
                {
                    "generation": int(fields[16]),
                    "generation_entity": int(fields[17]),
                    "carry_generation": int(fields[18]),
                }
            )
        if render_info:
            span = int(fields[19])
            if span != RENDER_INFO_BYTES:
                yield raw_header + payload + stream.read()
                return
            # Kept as the dwords the engine wrote, not as named fields: only
            # +0x00 and +0x18 are decoded, and the rest stay evidence.
            values["render_info"] = json.dumps(
                list(struct.unpack("<8I", fields[20]))
            )
        yield values, raw_header, payload


def _bracket_values(fields: tuple[object, ...]) -> dict[str, object]:
    return {
        "kind": fields[0].decode("ascii"),
        "sequence_number": int(fields[2]),
        "qpc": int(fields[3]),
        "entry_qpc": int(fields[4]),
        "thread_id": int(fields[5]),
        "generation": int(fields[6]),
        "generation_parent": int(fields[7]),
        "generation_depth": int(fields[8]),
        "client_entity": int(fields[9]),
        "bracket_arguments": json.dumps([int(value) for value in fields[10:19]]),
    }


def _animation_records(
    stream: BinaryIO,
    *,
    selected_bones: bool,
    generations: bool,
    root_transform: bool,
) -> Iterator[tuple[dict[str, object], bytes, bytes] | bytes]:
    if root_transform:
        evaluation = ANIMATION_RECORD_HEADER
    elif generations:
        evaluation = ANIMATION_RECORD_HEADER_V3
    else:
        evaluation = ANIMATION_RECORD_HEADER_V2
    headers = {b"BASE": evaluation, b"FINL": evaluation}
    if generations:
        headers[b"PBLD"] = BRACKET_RECORD_HEADER
        headers[b"DBLD"] = BRACKET_RECORD_HEADER
        headers[b"SHDW"] = BRACKET_RECORD_HEADER
    while True:
        record = _read_record(stream, headers)
        if record is None:
            return
        if isinstance(record, bytes):
            yield record
            return
        raw_header, payload, header = record
        fields = header.unpack(raw_header)
        if header is BRACKET_RECORD_HEADER:
            if int(fields[1]) != BRACKET_RECORD_HEADER.size:
                yield raw_header + payload + stream.read()
                return
            yield _bracket_values(fields), raw_header, payload
            continue
        bone_count = int(fields[8])
        selected_bytes = ((bone_count + 31) // 32) * 4 if selected_bones else 0
        # The stored count rather than the kind: an evaluation that received no
        # root transform carries none, and a record claiming any other width did
        # not come from the hook that wrote the stream.
        root_bytes = int(fields[18]) if root_transform else 0
        expected_bytes = (
            header.size + bone_count * 7 * 4 + selected_bytes + root_bytes
        )
        if (
            bone_count < 1
            or bone_count > MAXIMUM_BONES
            or root_bytes not in (0, ROOT_TRANSFORM_BYTES)
            or int(fields[1]) != expected_bytes
        ):
            yield raw_header + payload + stream.read()
            return
        values: dict[str, object] = {
            "kind": fields[0].decode("ascii"),
            "sequence_number": int(fields[2]),
            "qpc": int(fields[3]),
            "thread_id": int(fields[4]),
            "client_entity": int(fields[5]),
            "studio_hdr": int(fields[6]),
            "checksum": int(fields[7]),
            "bone_count": bone_count,
            "studio_sequence": int(fields[9]),
            "sample_phase": float(fields[10]),
            "entity_cycle": float(fields[11]),
            "result": int(fields[12]),
            "positions": int(fields[13]),
            "quaternions": int(fields[14]),
        }
        if generations:
            values.update(
                {
                    "generation": int(fields[15]),
                    "generation_depth": int(fields[16]),
                }
            )
        if root_transform:
            values.update(
                {
                    "root_transform": int(fields[17]),
                    "root_transform_bytes": root_bytes,
                }
            )
        yield values, raw_header, payload


def _contribution_records(
    stream: BinaryIO,
    channels: bool = True,
) -> Iterator[tuple[dict[str, object], bytes, bytes] | bytes]:
    """Read one fired contribution per record.

    A sequence carries the live pose parameters and the selected-bone mask; a
    decoded cell carries neither, because every cell inside one sequence shares
    the same mask pointer. A cell instead carries the two witnessed-bone bitmaps
    the channel decoders filled in. The declared span counts rather than the kind
    decide the width, so the size stays a closed form and a record claiming
    anything else did not come from the hook that wrote the stream.
    """
    header_struct = (
        CONTRIBUTION_RECORD_HEADER if channels else CONTRIBUTION_RECORD_HEADER_V1
    )
    headers = {magic: header_struct for magic in CONTRIBUTION_KINDS}
    while True:
        record = _read_record(stream, headers)
        if record is None:
            return
        if isinstance(record, bytes):
            yield record
            return
        raw_header, payload, header = record
        fields = header.unpack(raw_header)
        kind = fields[0].decode("ascii")
        bone_count = int(fields[12])
        pose_bytes = int(fields[29])
        selected_bytes = int(fields[30])
        sequence_kind = kind == "SEQP"
        mask_words = (bone_count + 31) // 32
        expected_selected = mask_words * 4 if sequence_kind else 0
        expected_pose = POSE_PARAMETER_BYTES if sequence_kind else 0
        channel_bytes = int(fields[37]) if channels else 0
        expected_channel = 0 if sequence_kind or not channels else mask_words * 8
        if (
            bone_count < 1
            or bone_count > MAXIMUM_BONES
            or pose_bytes != expected_pose
            or selected_bytes != expected_selected
            or channel_bytes != expected_channel
            or int(fields[1])
            != header.size + pose_bytes + selected_bytes + channel_bytes
        ):
            yield raw_header + payload + stream.read()
            return
        yield (
            {
                "kind": kind,
                "sequence_number": int(fields[2]),
                "qpc": int(fields[3]),
                "thread_id": int(fields[4]),
                "generation": int(fields[5]),
                "generation_depth": int(fields[6]),
                "generation_entity": int(fields[7]),
                "contribution": int(fields[8]),
                "caller_address": int(fields[9]),
                "owner_studio_hdr": int(fields[10]),
                "owner_checksum": int(fields[11]),
                "bone_count": bone_count,
                "sequence_index": int(fields[13]),
                "animation_index": int(fields[14]),
                "sequence_descriptor": int(fields[15]),
                "animation_descriptor": int(fields[16]),
                "bone_mask": int(fields[17]),
                "cycle": float(fields[18]),
                "num_blends": int(fields[19]),
                "group_size": json.dumps([int(fields[20]), int(fields[21])]),
                "param_index": json.dumps([int(fields[22]), int(fields[23])]),
                "blend_cell": json.dumps([int(fields[24]), int(fields[25])]),
                "blend_weight": json.dumps(
                    [float(fields[26]), float(fields[27])]
                ),
                "faults": int(fields[28]),
                "pose_parameter_bytes": pose_bytes,
                "selected_bone_bytes": selected_bytes,
                "channel_record_base": int(fields[31]) if channels else None,
                "channel_bone_base": int(fields[32]) if channels else None,
                "channel_quaternion_calls": (
                    int(fields[33]) if channels else None
                ),
                "channel_position_calls": int(fields[34]) if channels else None,
                "channel_frame": int(fields[35]) if channels else None,
                "channel_fraction": float(fields[36]) if channels else None,
                "channel_bone_bytes": channel_bytes if channels else None,
            },
            raw_header,
            payload,
        )


def _census_records(
    stream: BinaryIO,
) -> Iterator[tuple[str, dict[str, object], bytes, bytes] | bytes]:
    headers = {
        b"MOBS": MODEL_OBSERVATION_HEADER,
        b"MIMG": MODEL_IMAGE_HEADER,
    }
    while True:
        record = _read_record(stream, headers)
        if record is None:
            return
        if isinstance(record, bytes):
            yield record
            return
        raw_header, payload, header = record
        fields = header.unpack(raw_header)
        if header is MODEL_OBSERVATION_HEADER:
            bone_count = int(fields[8])
            if (
                bone_count < 1
                or bone_count > MAXIMUM_BONES
                or int(fields[1]) != MODEL_OBSERVATION_HEADER.size
            ):
                yield raw_header + payload + stream.read()
                return
            values: dict[str, object] = {
                "sequence_number": int(fields[2]),
                "qpc": int(fields[3]),
                "thread_id": int(fields[4]),
                "studio_hdr": int(fields[5]),
                "checksum": int(fields[6]),
                "previous_checksum": int(fields[7]),
                "bone_count": bone_count,
                "bone_index": int(fields[9]),
                "model_length": int(fields[10]),
                "include_model_count": int(fields[11]),
                "include_model_index": int(fields[12]),
                "studio_version": int(fields[13]),
                "reason": int(fields[14]),
                "image_captured": int(fields[15]),
                "generation": int(fields[16]),
                "model_name": fields[17]
                .split(b"\0", 1)[0]
                .decode("ascii", "replace"),
            }
            yield "MOBS", values, raw_header, payload
            continue
        captured = int(fields[8])
        if (
            captured < 1
            or captured > MAXIMUM_IMAGE_BYTES
            or int(fields[1]) != MODEL_IMAGE_HEADER.size + captured
        ):
            yield raw_header + payload + stream.read()
            return
        yield (
            "MIMG",
            {
                "sequence_number": int(fields[2]),
                "qpc": int(fields[3]),
                "thread_id": int(fields[4]),
                "studio_hdr": int(fields[5]),
                "checksum": int(fields[6]),
                "model_length": int(fields[7]),
                "captured_bytes": captured,
                "capped": int(fields[9]),
            },
            raw_header,
            payload,
        )


def _actor_records(
    stream: BinaryIO, entity_index: bool
) -> Iterator[tuple[dict[str, object], bytes, bytes] | bytes]:
    layout = (
        ACTOR_OBSERVATION_HEADER if entity_index else ACTOR_OBSERVATION_HEADER_V2
    )
    while True:
        record = _read_record(stream, {b"ACTR": layout})
        if record is None:
            return
        if isinstance(record, bytes):
            yield record
            return
        raw_header, payload, header = record
        fields = header.unpack(raw_header)
        bone_count = int(fields[12])
        ref_handle = int(fields[13]) if entity_index else None
        # A lifetime record names an address and nothing else, so it carries no
        # model identity by construction; an identity record that carries none
        # did not come from the hook that writes this stream.
        identity = int(fields[7]) in ACTOR_IDENTITY_REASONS
        if (
            bone_count > MAXIMUM_BONES
            or (bone_count < 1 if identity else bone_count != 0)
            or int(fields[1]) != layout.size
        ):
            yield raw_header + payload + stream.read()
            return
        yield (
            {
                "sequence_number": int(fields[2]),
                "qpc": int(fields[3]),
                "thread_id": int(fields[4]),
                "entity": int(fields[5]),
                "renderable": int(fields[6]),
                "reason": int(fields[7]),
                "generation": int(fields[8]),
                "studio_hdr": int(fields[9]),
                "checksum": int(fields[10]),
                "previous_checksum": int(fields[11]),
                "bone_count": bone_count,
                "ref_handle": ref_handle,
                "entity_index": _handle_index(ref_handle),
                "model_name": fields[-1]
                .split(b"\0", 1)[0]
                .decode("ascii", "replace"),
            },
            raw_header,
            payload,
        )


# An entity handle carries its index in the low 13 bits and a serial above them,
# with all bits set meaning no entity. The raw handle is stored beside the index
# so the split stays checkable rather than only its result surviving.
HANDLE_INDEX_MASK = 0x1FFF
HANDLE_SERIAL_SHIFT = 13
INVALID_HANDLE = 0xFFFFFFFF
# A record whose field was never written carries a zero handle, and zero decodes
# to index zero with serial zero -- which is the world entity's slot with a
# serial no live entity holds, never a scene actor. Reading it as an index would
# turn every absent target into a spurious entity zero, so it is absent too.
ABSENT_HANDLE = 0


def _handle_index(handle: int | None) -> int | None:
    if handle is None or handle in (INVALID_HANDLE, ABSENT_HANDLE):
        return None
    return handle & HANDLE_INDEX_MASK


def _handle_serial(handle: int | None) -> int | None:
    if handle is None or handle in (INVALID_HANDLE, ABSENT_HANDLE):
        return None
    return handle >> HANDLE_SERIAL_SHIFT


def _scene_records(
    stream: BinaryIO,
) -> Iterator[tuple[dict[str, object], bytes, bytes] | bytes]:
    layouts: dict[bytes, struct.Struct] = {
        magic: SCENE_REQUEST_HEADER for magic in SCENE_REQUEST_KINDS
    }
    layouts[SEQUENCE_CHANGE_KIND] = SEQUENCE_CHANGE_HEADER
    while True:
        record = _read_record(stream, layouts)
        if record is None:
            return
        if isinstance(record, bytes):
            yield record
            return
        raw_header, payload, header = record
        fields = header.unpack(raw_header)
        magic = bytes(fields[0])[:4]
        if int(fields[1]) != header.size:
            yield raw_header + payload + stream.read()
            return
        if magic == SEQUENCE_CHANGE_KIND:
            handle = int(fields[7])
            yield (
                {
                    "kind": magic.decode("ascii"),
                    "sequence_number": int(fields[2]),
                    "qpc": int(fields[3]),
                    "thread_id": int(fields[4]),
                    "generation": int(fields[5]),
                    "client_entity": int(fields[6]),
                    "ref_handle": handle,
                    "entity_index": _handle_index(handle),
                    "entity_serial": _handle_serial(handle),
                    "renderable": int(fields[8]),
                    "studio_hdr": int(fields[9]),
                    "checksum": int(fields[10]),
                    "transitions_before": int(fields[11]),
                    "transitions_after": int(fields[12]),
                    "caller_address": int(fields[13]),
                    "faults": int(fields[14]),
                },
                raw_header,
                payload,
            )
            continue
        reason = int(fields[5])
        if reason not in SCENE_REASONS[magic]:
            yield raw_header + payload + stream.read()
            return
        yield (
            {
                "kind": magic.decode("ascii"),
                "sequence_number": int(fields[2]),
                "qpc": int(fields[3]),
                "thread_id": int(fields[4]),
                "reason": reason,
                "scope": int(fields[6]),
                "parent_scope": int(fields[7]),
                "scene_entity": int(fields[8]),
                "scene_ref_handle": int(fields[9]),
                "scene_entity_index": _handle_index(int(fields[9])),
                "scene_vftable": int(fields[10]),
                "target_entity": int(fields[11]),
                "target_ref_handle": int(fields[12]),
                "target_entity_index": _handle_index(int(fields[12])),
                "target_entity_serial": _handle_serial(int(fields[12])),
                "caller_address": int(fields[13]),
                "event_type": int(fields[14]),
                "event_start": float(fields[15]),
                "event_end": float(fields[16]),
                "scene_time": float(fields[17]),
                "playing_back": int(fields[18]),
                "faults": int(fields[19]),
                "scene_file": _text(fields[20]),
                "actor_name": _text(fields[21]),
                "text0": _text(fields[22]),
                "text1": _text(fields[23]),
                "text2": _text(fields[24]),
            },
            raw_header,
            payload,
        )


def _text(value: bytes) -> str:
    return value.split(b"\0", 1)[0].decode("ascii", "replace")


SCHEMA = """
CREATE TABLE capture_metadata (
    key TEXT PRIMARY KEY,
    value TEXT NOT NULL
);
CREATE TABLE launch_arguments (
    position INTEGER PRIMARY KEY,
    value TEXT NOT NULL
);
CREATE TABLE modules (
    name TEXT PRIMARY KEY,
    path TEXT NOT NULL,
    file_size INTEGER NOT NULL,
    sha256 TEXT NOT NULL,
    binary_profile TEXT
);
CREATE TABLE streams (
    name TEXT PRIMARY KEY,
    source_name TEXT NOT NULL,
    format TEXT NOT NULL,
    file_size INTEGER NOT NULL,
    sha256 TEXT NOT NULL,
    record_count INTEGER NOT NULL,
    incomplete_tail_bytes INTEGER NOT NULL,
    file_header BLOB NOT NULL,
    incomplete_tail BLOB NOT NULL
);
CREATE TABLE records (
    id INTEGER PRIMARY KEY,
    stream_name TEXT NOT NULL REFERENCES streams(name),
    ordinal INTEGER NOT NULL,
    kind TEXT NOT NULL,
    sequence_number INTEGER,
    qpc INTEGER,
    thread_id INTEGER,
    client_entity INTEGER,
    studio_hdr INTEGER,
    checksum INTEGER,
    bone_count INTEGER,
    studio_sequence INTEGER,
    sample_phase REAL,
    entity_cycle REAL,
    result INTEGER,
    model_info INTEGER,
    model_name TEXT,
    draw_arguments TEXT,
    positions INTEGER,
    quaternions INTEGER,
    generation INTEGER,
    generation_parent INTEGER,
    generation_depth INTEGER,
    generation_entity INTEGER,
    carry_generation INTEGER,
    entry_qpc INTEGER,
    bracket_arguments TEXT,
    root_transform INTEGER,
    root_transform_bytes INTEGER,
    render_info TEXT,
    contribution INTEGER,
    caller_address INTEGER,
    owner_studio_hdr INTEGER,
    owner_checksum INTEGER,
    sequence_index INTEGER,
    animation_index INTEGER,
    sequence_descriptor INTEGER,
    animation_descriptor INTEGER,
    bone_mask INTEGER,
    cycle REAL,
    num_blends INTEGER,
    group_size TEXT,
    param_index TEXT,
    blend_cell TEXT,
    blend_weight TEXT,
    faults INTEGER,
    pose_parameter_bytes INTEGER,
    selected_bone_bytes INTEGER,
    channel_record_base INTEGER,
    channel_bone_base INTEGER,
    channel_quaternion_calls INTEGER,
    channel_position_calls INTEGER,
    channel_frame INTEGER,
    channel_fraction REAL,
    channel_bone_bytes INTEGER,
    raw_header BLOB NOT NULL,
    raw_payload BLOB NOT NULL,
    UNIQUE(stream_name, ordinal)
);
CREATE TABLE model_headers (
    id INTEGER PRIMARY KEY,
    stream_name TEXT NOT NULL REFERENCES streams(name),
    ordinal INTEGER NOT NULL,
    sequence_number INTEGER NOT NULL,
    qpc INTEGER NOT NULL,
    thread_id INTEGER NOT NULL,
    studio_hdr INTEGER NOT NULL,
    checksum INTEGER NOT NULL,
    previous_checksum INTEGER NOT NULL,
    bone_count INTEGER NOT NULL,
    bone_index INTEGER NOT NULL,
    model_length INTEGER NOT NULL,
    include_model_count INTEGER NOT NULL,
    include_model_index INTEGER NOT NULL,
    studio_version INTEGER NOT NULL,
    reason INTEGER NOT NULL,
    image_captured INTEGER NOT NULL,
    generation INTEGER NOT NULL,
    model_name TEXT NOT NULL,
    raw_header BLOB NOT NULL,
    UNIQUE(stream_name, ordinal)
);
CREATE TABLE model_images (
    id INTEGER PRIMARY KEY,
    stream_name TEXT NOT NULL REFERENCES streams(name),
    ordinal INTEGER NOT NULL,
    sequence_number INTEGER NOT NULL,
    qpc INTEGER NOT NULL,
    thread_id INTEGER NOT NULL,
    studio_hdr INTEGER NOT NULL,
    checksum INTEGER NOT NULL,
    model_length INTEGER NOT NULL,
    captured_bytes INTEGER NOT NULL,
    capped INTEGER NOT NULL,
    raw_header BLOB NOT NULL,
    image BLOB NOT NULL,
    UNIQUE(stream_name, ordinal)
);
CREATE TABLE actor_observations (
    id INTEGER PRIMARY KEY,
    stream_name TEXT NOT NULL REFERENCES streams(name),
    ordinal INTEGER NOT NULL,
    sequence_number INTEGER NOT NULL,
    qpc INTEGER NOT NULL,
    thread_id INTEGER NOT NULL,
    entity INTEGER NOT NULL,
    renderable INTEGER NOT NULL,
    reason INTEGER NOT NULL,
    generation INTEGER NOT NULL,
    studio_hdr INTEGER NOT NULL,
    checksum INTEGER NOT NULL,
    previous_checksum INTEGER NOT NULL,
    bone_count INTEGER NOT NULL,
    ref_handle INTEGER,
    entity_index INTEGER,
    model_name TEXT NOT NULL,
    raw_header BLOB NOT NULL,
    UNIQUE(stream_name, ordinal)
);
CREATE TABLE scene_events (
    id INTEGER PRIMARY KEY,
    stream_name TEXT NOT NULL REFERENCES streams(name),
    ordinal INTEGER NOT NULL,
    kind TEXT NOT NULL,
    sequence_number INTEGER NOT NULL,
    qpc INTEGER NOT NULL,
    thread_id INTEGER NOT NULL,
    reason INTEGER NOT NULL,
    scope INTEGER NOT NULL,
    parent_scope INTEGER NOT NULL,
    scene_entity INTEGER NOT NULL,
    scene_ref_handle INTEGER NOT NULL,
    scene_entity_index INTEGER,
    scene_vftable INTEGER NOT NULL,
    target_entity INTEGER NOT NULL,
    target_ref_handle INTEGER NOT NULL,
    target_entity_index INTEGER,
    target_entity_serial INTEGER,
    caller_address INTEGER NOT NULL,
    event_type INTEGER NOT NULL,
    event_start REAL NOT NULL,
    event_end REAL NOT NULL,
    scene_time REAL NOT NULL,
    playing_back INTEGER NOT NULL,
    faults INTEGER NOT NULL,
    scene_file TEXT NOT NULL,
    actor_name TEXT NOT NULL,
    text0 TEXT NOT NULL,
    text1 TEXT NOT NULL,
    text2 TEXT NOT NULL,
    raw_header BLOB NOT NULL,
    UNIQUE(stream_name, ordinal)
);
CREATE TABLE sequence_changes (
    id INTEGER PRIMARY KEY,
    stream_name TEXT NOT NULL REFERENCES streams(name),
    ordinal INTEGER NOT NULL,
    kind TEXT NOT NULL,
    sequence_number INTEGER NOT NULL,
    qpc INTEGER NOT NULL,
    thread_id INTEGER NOT NULL,
    generation INTEGER NOT NULL,
    client_entity INTEGER NOT NULL,
    ref_handle INTEGER NOT NULL,
    entity_index INTEGER,
    entity_serial INTEGER,
    renderable INTEGER NOT NULL,
    studio_hdr INTEGER NOT NULL,
    checksum INTEGER NOT NULL,
    transitions_before INTEGER NOT NULL,
    transitions_after INTEGER NOT NULL,
    caller_address INTEGER NOT NULL,
    faults INTEGER NOT NULL,
    raw_header BLOB NOT NULL,
    UNIQUE(stream_name, ordinal)
);
CREATE TABLE failures (
    category TEXT PRIMARY KEY,
    count INTEGER NOT NULL,
    detail TEXT NOT NULL
);
CREATE TABLE artifacts (
    name TEXT PRIMARY KEY,
    contents BLOB NOT NULL
);
CREATE INDEX records_entity_time ON records(client_entity, qpc);
CREATE INDEX records_model_time ON records(checksum, qpc);
CREATE INDEX records_kind_time ON records(kind, qpc);
CREATE INDEX records_generation ON records(generation);
CREATE INDEX records_owner ON records(owner_checksum, sequence_index);
CREATE INDEX records_contribution ON records(contribution);
CREATE INDEX records_channel
    ON records(owner_checksum, animation_index, channel_frame);
CREATE INDEX model_headers_identity ON model_headers(studio_hdr, checksum);
CREATE INDEX model_images_checksum ON model_images(checksum);
CREATE INDEX actor_observations_identity
    ON actor_observations(entity, checksum);
CREATE INDEX actor_observations_index
    ON actor_observations(entity_index, qpc);
CREATE INDEX scene_events_target ON scene_events(target_entity_index, qpc);
CREATE INDEX scene_events_scope ON scene_events(scope);
CREATE INDEX scene_events_scene ON scene_events(scene_entity, qpc);
CREATE INDEX sequence_changes_entity ON sequence_changes(entity_index, qpc);
CREATE INDEX sequence_changes_generation ON sequence_changes(generation);
"""


INSERT_MODEL_HEADER = """
INSERT INTO model_headers (
    stream_name, ordinal, sequence_number, qpc, thread_id, studio_hdr,
    checksum, previous_checksum, bone_count, bone_index, model_length,
    include_model_count, include_model_index, studio_version, reason,
    image_captured, generation, model_name, raw_header
) VALUES (
    :stream_name, :ordinal, :sequence_number, :qpc, :thread_id, :studio_hdr,
    :checksum, :previous_checksum, :bone_count, :bone_index, :model_length,
    :include_model_count, :include_model_index, :studio_version, :reason,
    :image_captured, :generation, :model_name, :raw_header
)
"""

INSERT_MODEL_IMAGE = """
INSERT INTO model_images (
    stream_name, ordinal, sequence_number, qpc, thread_id, studio_hdr,
    checksum, model_length, captured_bytes, capped, raw_header, image
) VALUES (
    :stream_name, :ordinal, :sequence_number, :qpc, :thread_id, :studio_hdr,
    :checksum, :model_length, :captured_bytes, :capped, :raw_header, :image
)
"""


INSERT_ACTOR_OBSERVATION = """
INSERT INTO actor_observations (
    stream_name, ordinal, sequence_number, qpc, thread_id, entity, renderable,
    reason, generation, studio_hdr, checksum, previous_checksum, bone_count,
    ref_handle, entity_index, model_name, raw_header
) VALUES (
    :stream_name, :ordinal, :sequence_number, :qpc, :thread_id, :entity,
    :renderable, :reason, :generation, :studio_hdr, :checksum,
    :previous_checksum, :bone_count, :ref_handle, :entity_index, :model_name,
    :raw_header
)
"""


INSERT_SCENE_EVENT = """
INSERT INTO scene_events (
    stream_name, ordinal, kind, sequence_number, qpc, thread_id, reason, scope,
    parent_scope, scene_entity, scene_ref_handle, scene_entity_index,
    scene_vftable, target_entity, target_ref_handle, target_entity_index,
    target_entity_serial, caller_address, event_type, event_start, event_end,
    scene_time, playing_back, faults, scene_file, actor_name, text0, text1,
    text2, raw_header
) VALUES (
    :stream_name, :ordinal, :kind, :sequence_number, :qpc, :thread_id, :reason,
    :scope, :parent_scope, :scene_entity, :scene_ref_handle,
    :scene_entity_index, :scene_vftable, :target_entity, :target_ref_handle,
    :target_entity_index, :target_entity_serial, :caller_address, :event_type,
    :event_start, :event_end, :scene_time, :playing_back, :faults, :scene_file,
    :actor_name, :text0, :text1, :text2, :raw_header
)
"""


INSERT_SEQUENCE_CHANGE = """
INSERT INTO sequence_changes (
    stream_name, ordinal, kind, sequence_number, qpc, thread_id, generation,
    client_entity, ref_handle, entity_index, entity_serial, renderable,
    studio_hdr, checksum, transitions_before, transitions_after,
    caller_address, faults, raw_header
) VALUES (
    :stream_name, :ordinal, :kind, :sequence_number, :qpc, :thread_id,
    :generation, :client_entity, :ref_handle, :entity_index, :entity_serial,
    :renderable, :studio_hdr, :checksum, :transitions_before,
    :transitions_after, :caller_address, :faults, :raw_header
)
"""


INSERT_RECORD = """
INSERT INTO records (
    stream_name, ordinal, kind, sequence_number, qpc, thread_id,
    client_entity, studio_hdr, checksum, bone_count, studio_sequence,
    sample_phase, entity_cycle, result, model_info, model_name,
    draw_arguments, positions, quaternions, generation, generation_parent,
    generation_depth, generation_entity, carry_generation, entry_qpc,
    bracket_arguments, root_transform, root_transform_bytes, render_info,
    contribution, caller_address, owner_studio_hdr, owner_checksum,
    sequence_index, animation_index, sequence_descriptor, animation_descriptor,
    bone_mask, cycle, num_blends, group_size, param_index, blend_cell,
    blend_weight, faults, pose_parameter_bytes, selected_bone_bytes,
    channel_record_base, channel_bone_base, channel_quaternion_calls,
    channel_position_calls, channel_frame, channel_fraction,
    channel_bone_bytes, raw_header, raw_payload
) VALUES (
    :stream_name, :ordinal, :kind, :sequence_number, :qpc, :thread_id,
    :client_entity, :studio_hdr, :checksum, :bone_count, :studio_sequence,
    :sample_phase, :entity_cycle, :result, :model_info, :model_name,
    :draw_arguments, :positions, :quaternions, :generation, :generation_parent,
    :generation_depth, :generation_entity, :carry_generation, :entry_qpc,
    :bracket_arguments, :root_transform, :root_transform_bytes, :render_info,
    :contribution, :caller_address, :owner_studio_hdr, :owner_checksum,
    :sequence_index, :animation_index, :sequence_descriptor,
    :animation_descriptor, :bone_mask, :cycle, :num_blends, :group_size,
    :param_index, :blend_cell, :blend_weight, :faults, :pose_parameter_bytes,
    :selected_bone_bytes, :channel_record_base, :channel_bone_base,
    :channel_quaternion_calls, :channel_position_calls, :channel_frame,
    :channel_fraction, :channel_bone_bytes, :raw_header, :raw_payload
)
"""

RECORD_DEFAULTS: dict[str, object] = {
    # A contribution names no client entity of its own: it is scoped by the
    # generation whose bracket already named one, and the frames it hooks
    # receive a studio header rather than an entity.
    "client_entity": None,
    "studio_sequence": None,
    "sample_phase": None,
    "entity_cycle": None,
    "result": None,
    "model_info": None,
    "model_name": None,
    "draw_arguments": None,
    "positions": None,
    "quaternions": None,
    "generation": None,
    "generation_parent": None,
    "generation_depth": None,
    "generation_entity": None,
    "carry_generation": None,
    "entry_qpc": None,
    "bracket_arguments": None,
    "studio_hdr": None,
    "checksum": None,
    "bone_count": None,
    "root_transform": None,
    "root_transform_bytes": None,
    "render_info": None,
    "contribution": None,
    "caller_address": None,
    "owner_studio_hdr": None,
    "owner_checksum": None,
    "sequence_index": None,
    "animation_index": None,
    "sequence_descriptor": None,
    "animation_descriptor": None,
    "bone_mask": None,
    "cycle": None,
    "num_blends": None,
    "group_size": None,
    "param_index": None,
    "blend_cell": None,
    "blend_weight": None,
    "faults": None,
    "pose_parameter_bytes": None,
    "selected_bone_bytes": None,
    "channel_record_base": None,
    "channel_bone_base": None,
    "channel_quaternion_calls": None,
    "channel_position_calls": None,
    "channel_frame": None,
    "channel_fraction": None,
    "channel_bone_bytes": None,
}


def _insert_stream(
    connection: sqlite3.Connection,
    path: Path,
    name: str,
) -> tuple[int, int]:
    file_header_struct = (
        POSE_FILE_HEADER
        if name == "pose"
        else CENSUS_FILE_HEADER
        if name == "census"
        else ACTOR_FILE_HEADER
        if name == "actor"
        else CONTRIBUTION_FILE_HEADER
        if name == "contribution"
        else SCENE_FILE_HEADER
        if name == "scene"
        else ANIMATION_FILE_HEADER
    )
    # Both contribution file headers are 128 bytes and agree on magic and
    # version, which is all this reader takes from one; the frame RVAs that
    # differ between them are read back from the stored header instead.
    with path.open("rb") as stream:
        file_header = stream.read(file_header_struct.size)
        if len(file_header) != file_header_struct.size:
            raise ValueError(f"{path} has no complete file header")
        fields = file_header_struct.unpack(file_header)
        magic = fields[0].rstrip(b"\0")
        version = int(fields[1])
        if name == "pose":
            if (magic, version) not in {
                (b"ELPOSE2", 2),
                (b"ELPOSE3", 3),
                (b"ELPOSE4", 4),
            }:
                raise ValueError(f"{path} is not a supported ELPOSE stream")
            records = _pose_records(
                stream,
                generations=version >= 3,
                render_info=version >= 4,
            )
        elif name == "census":
            if (magic, version) != (b"ELMDL1", 1):
                raise ValueError(f"{path} is not a supported ELMDL stream")
            records = _census_records(stream)
        elif name == "actor":
            if (magic, version) not in {
                (b"ELACT1", 1),
                (b"ELACT2", 2),
                (b"ELACT3", 3),
            }:
                raise ValueError(f"{path} is not a supported ELACT stream")
            records = _actor_records(stream, entity_index=version >= 3)
        elif name == "contribution":
            if (magic, version) not in {(b"ELCON1", 1), (b"ELCON2", 2)}:
                raise ValueError(f"{path} is not a supported ELCON stream")
            records = _contribution_records(stream, channels=version >= 2)
        elif name == "scene":
            if (magic, version) != (b"ELSCN1", 1):
                raise ValueError(f"{path} is not a supported ELSCN stream")
            records = _scene_records(stream)
        else:
            if (magic, version) not in {
                (b"ELANIM1", 1),
                (b"ELANIM2", 2),
                (b"ELANIM3", 3),
                (b"ELANIM4", 4),
            }:
                raise ValueError(f"{path} is not a supported ELANIM stream")
            records = _animation_records(
                stream,
                selected_bones=version >= 2,
                generations=version >= 3,
                root_transform=version >= 4,
            )

        connection.execute(
            """
            INSERT INTO streams (
                name, source_name, format, file_size, sha256, record_count,
                incomplete_tail_bytes, file_header, incomplete_tail
            ) VALUES (?, ?, ?, ?, ?, 0, 0, ?, ?)
            """,
            (
                name,
                path.name,
                magic.decode("ascii"),
                path.stat().st_size,
                file_sha256(path),
                file_header,
                b"",
            ),
        )

        record_count = 0
        incomplete_tail = b""
        for ordinal, record in enumerate(records):
            if isinstance(record, bytes):
                incomplete_tail = record
                break
            if name == "census":
                kind, values, raw_header, raw_payload = record
                statement = (
                    INSERT_MODEL_HEADER
                    if kind == "MOBS"
                    else INSERT_MODEL_IMAGE
                )
                row = {
                    "stream_name": name,
                    "ordinal": ordinal,
                    "raw_header": raw_header,
                    **values,
                }
                if kind == "MIMG":
                    row["image"] = raw_payload
                connection.execute(statement, row)
                record_count += 1
                continue
            if name == "actor":
                values, raw_header, _ = record
                connection.execute(
                    INSERT_ACTOR_OBSERVATION,
                    {
                        "stream_name": name,
                        "ordinal": ordinal,
                        "raw_header": raw_header,
                        "ref_handle": None,
                        "entity_index": None,
                        **values,
                    },
                )
                record_count += 1
                continue
            if name == "scene":
                values, raw_header, _ = record
                statement = (
                    INSERT_SEQUENCE_CHANGE
                    if values["kind"] == "SEQC"
                    else INSERT_SCENE_EVENT
                )
                connection.execute(
                    statement,
                    {
                        "stream_name": name,
                        "ordinal": ordinal,
                        "raw_header": raw_header,
                        **values,
                    },
                )
                record_count += 1
                continue
            values, raw_header, raw_payload = record
            connection.execute(
                INSERT_RECORD,
                {
                    "stream_name": name,
                    "ordinal": ordinal,
                    **RECORD_DEFAULTS,
                    "raw_header": raw_header,
                    "raw_payload": raw_payload,
                    **values,
                },
            )
            record_count += 1

    connection.execute(
        """
        UPDATE streams
        SET record_count = ?, incomplete_tail_bytes = ?, incomplete_tail = ?
        WHERE name = ?
        """,
        (
            record_count,
            len(incomplete_tail),
            incomplete_tail,
            name,
        ),
    )
    return record_count, len(incomplete_tail)


def _json_value(value: object) -> str:
    return json.dumps(value, sort_keys=True, separators=(",", ":"))


def finalize(
    session: Path,
    database: Path | None = None,
    *,
    retain_temporary_streams: bool = False,
) -> dict[str, object]:
    session = session.resolve()
    database = (database or session / "capture.sqlite").resolve()
    if database.exists():
        raise FileExistsError(database)
    launch_path = session / "launch.json"
    if not launch_path.is_file():
        raise FileNotFoundError(launch_path)
    launch = json.loads(launch_path.read_text(encoding="utf-8"))
    stream_paths = [
        ("pose", session / "scene.elpose"),
        ("animation", session / "animation.elanim"),
        ("census", session / "model.elmdl"),
        ("actor", session / "actor.elact"),
        ("contribution", session / "contribution.elcon"),
        ("scene", session / "scene.elscn"),
    ]
    stream_paths = [(name, path) for name, path in stream_paths if path.is_file()]
    if not stream_paths:
        raise FileNotFoundError(f"no raw capture streams below {session}")

    temporary = database.with_name(database.name + ".tmp")
    if temporary.exists():
        temporary.unlink()
    database.parent.mkdir(parents=True, exist_ok=True)
    connection = sqlite3.connect(temporary)
    counts: dict[str, int] = {}
    tails: dict[str, int] = {}
    try:
        connection.execute("PRAGMA journal_mode=DELETE")
        connection.execute("PRAGMA synchronous=FULL")
        connection.execute("PRAGMA foreign_keys=ON")
        connection.executescript(SCHEMA)
        connection.execute("BEGIN IMMEDIATE")
        for key, value in {
            "created_utc": launch.get("created_utc"),
            "tool_git": launch.get("tool_git"),
            "map": launch.get("map"),
            "recipe": launch.get("recipe"),
            # The offline analyzers pick their run-zero rule from this rather
            # than from the map name, so a database carries the rule its own
            # recipe declared instead of one inferred from what it captured.
            "run_zero_rule": launch.get("run_zero_rule"),
            "entry_command": launch.get("entry_command"),
            "save": launch.get("save"),
            "capture_duration_seconds": launch.get("capture_duration_seconds"),
            "retail_exit_code": launch.get("retail_exit_code"),
        }.items():
            connection.execute(
                "INSERT INTO capture_metadata(key, value) VALUES (?, ?)",
                (key, _json_value(value)),
            )
        for position, argument in enumerate(launch.get("launch_arguments", [])):
            connection.execute(
                "INSERT INTO launch_arguments(position, value) VALUES (?, ?)",
                (position, str(argument)),
            )
        for module in launch.get("modules", []):
            connection.execute(
                """
                INSERT INTO modules(name, path, file_size, sha256, binary_profile)
                VALUES (?, ?, ?, ?, ?)
                """,
                (
                    module["name"],
                    module["path"],
                    int(module["file_size"]),
                    module["sha256"],
                    module.get("binary_profile"),
                ),
            )
        for artifact_name in (
            "launch.json",
            "recipe.cfg",
            "supervision.txt",
            "done.txt",
            "ready.txt",
            "live_pose_hook.ini",
            "boundary.json",
            "console.log",
        ):
            artifact = session / artifact_name
            if artifact.is_file():
                connection.execute(
                    "INSERT INTO artifacts(name, contents) VALUES (?, ?)",
                    (artifact_name, artifact.read_bytes()),
                )
        for stream_name, stream_path in stream_paths:
            count, tail = _insert_stream(connection, stream_path, stream_name)
            counts[stream_name] = count
            tails[stream_name] = tail

        done = read_key_values(session / "done.txt")
        supervision = read_key_values(session / "supervision.txt")
        failures = {
            "hook_dropped": (int(done.get("dropped", "0")), "hook writer drops"),
            "hook_skipped": (
                int(done.get("skipped", "0")),
                "callbacks that reached no readable header or pose buffer",
            ),
            "hook_filtered": (
                int(done.get("filtered", "0")),
                "callbacks excluded by the configured target checksum",
            ),
            "hook_unbracketed": (
                int(done.get("unbracketed", "0")),
                "records emitted outside every pose-build bracket",
            ),
            "hook_bracket_overflow": (
                int(done.get("bracket_overflow", "0")),
                "brackets refused because the nesting depth cap was reached",
            ),
            "census_overflow": (
                int(done.get("census_overflow", "0")),
                "studio headers refused because a census table was full",
            ),
            "census_faults": (
                int(done.get("census_faults", "0")),
                "census reads or allocations that failed and released a claim",
            ),
            "census_capped": (
                int(done.get("census_capped", "0")),
                "model images truncated at the configured capture cap",
            ),
            "census_vanished": (
                int(done.get("census_vanished", "0")),
                "recorded headers that no longer read as one at capture stop",
            ),
            "actor_overflow": (
                int(done.get("actor_overflow", "0")),
                "skeletal entities refused because the actor table was full",
            ),
            "actor_faults": (
                int(done.get("actor_faults", "0")),
                "actor reads or allocations that failed and released a claim",
            ),
            "actor_vanished": (
                int(done.get("actor_vanished", "0")),
                "recorded actors whose model header no longer reads at stop",
            ),
            "contribution_faults": (
                int(done.get("contribution_faults", "0")),
                "contributions that could not name one of their sources",
            ),
            "contribution_overflow": (
                int(done.get("contribution_overflow", "0")),
                "contribution scopes refused because the nesting cap was hit",
            ),
            "contribution_unscoped": (
                int(done.get("contribution_unscoped", "0")),
                "decoded cells emitted outside every contribution scope",
            ),
            "channel_unwitnessed": (
                int(done.get("channel_unwitnessed", "0")),
                "decoded cells whose channel decoders left no accumulator",
            ),
            "channel_stride_faults": (
                int(done.get("channel_stride_faults", "0")),
                "channel decodes whose record pointer was off the 32-byte stride",
            ),
            "channel_nested": (
                int(done.get("channel_nested", "0")),
                "selected-bone loops re-entered on one thread",
            ),
            "scene_faults": (
                int(done.get("scene_faults", "0")),
                "scene records that could not name something they should have",
            ),
            "scene_overflow": (
                int(done.get("scene_overflow", "0")),
                "scene scopes refused because the nesting cap was hit",
            ),
            "scene_unscoped": (
                int(done.get("scene_unscoped", "0")),
                "scene records emitted outside every scene scope",
            ),
            "scene_truncated": (
                int(done.get("scene_truncated", "0")),
                "scene records whose source string was longer than its field",
            ),
            "scene_resolutions_unscoped": (
                int(done.get("scene_resolutions_unscoped", "0")),
                "actor resolutions outside every scene scope, which are the "
                "per-frame re-pin a scene with position_start performs rather "
                "than requests, counted here and recorded nowhere",
            ),
            "incomplete_streams": (
                sum(1 for value in tails.values() if value),
                _json_value(tails),
            ),
            "probe_profile_misses": (
                int(supervision.get("binary_profile_misses", "0")),
                "hash-gated module profile misses",
            ),
            "probe_diagnostics": (
                int(supervision.get("probe_diagnostic_writes", "0")),
                "hash or hook validation diagnostics",
            ),
        }
        for category, (count, detail) in failures.items():
            connection.execute(
                "INSERT INTO failures(category, count, detail) VALUES (?, ?, ?)",
                (category, count, detail),
            )
        connection.commit()
        integrity = connection.execute("PRAGMA integrity_check").fetchone()[0]
        # Census and actor rows land in their own tables, so the count that must
        # match is the total across every one of them rather than `records`
        # alone.
        stored_records = sum(
            connection.execute(f"SELECT count(*) FROM {table}").fetchone()[0]
            for table in (
                "records",
                "model_headers",
                "model_images",
                "actor_observations",
                "scene_events",
                "sequence_changes",
            )
        )
        if integrity != "ok" or stored_records != sum(counts.values()):
            raise RuntimeError(
                f"SQLite verification failed: integrity={integrity!r} "
                f"records={stored_records}/{sum(counts.values())}"
            )
    except BaseException:
        connection.close()
        if temporary.exists():
            temporary.unlink()
        raise
    connection.close()
    os.replace(temporary, database)

    if not retain_temporary_streams:
        for _, stream_path in stream_paths:
            stream_path.unlink()

    return {
        "database": str(database),
        "database_bytes": database.stat().st_size,
        "database_sha256": file_sha256(database),
        "records": counts,
        "incomplete_tail_bytes": tails,
        "temporary_streams_retained": retain_temporary_streams,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("session", type=Path)
    parser.add_argument("--database", type=Path)
    parser.add_argument("--retain-temporary-streams", action="store_true")
    args = parser.parse_args()
    report = finalize(
        args.session,
        args.database,
        retain_temporary_streams=args.retain_temporary_streams,
    )
    print(json.dumps(report, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
