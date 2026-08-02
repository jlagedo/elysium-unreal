"""Difference the bytes retail dereferenced against the bytes our decoder reads.

Usage:
    uv run elysium research verify_byte_coverage <session> [<session> ...]

Reads a finalized, span-resolved capture database read-only and produces the
three sets CAP4.2 exists for, per owner model image and per fired identity:
what retail read and we do not (the missing-data list), what we read and retail's
animation path did not, and what neither touched. Nothing is written into the
database; our arm is a claim about this checkout's decoder at this commit, and an
evidence file must not carry a claim about a moment outside itself.

The two arms are produced by different mechanisms on purpose. Retail's spans come
from `resolve_consumed_spans`, which transcribes the confirmed decompilation and
deliberately imports nothing from the exporter. Ours comes from running
`elysium_pipeline.formats.mdl_skel` unmodified under `decoder_coverage`'s
observation. Had one walker produced both, the difference would be a tautology
rather than a test.

Four bounds travel with the report.

The frame is the animation path. Retail's spans were captured at the animation
evaluation hooks, so geometry, material, skin and flex bytes are outside the
comparison on both sides. Bytes in neither set are recorded as unknown, and a
byte no animation path reads is not thereby inert.

Ours exceeds retail by construction in four places, and each is counted with the
reason rather than reported as agreement. We walk a whole RLE track where retail
walks to the sampled frame; we read every bone where retail reads the ones a mask
selected; we read every sequence a model declares where a run fires a few dozen;
and we follow name strings retail's walker claims for nobody.

Retail exceeds ours inside a track in exactly one way. Its to-frame walk reads
two keys of the run it stops in, and our whole-track walk reads every key of
every run it enters, so the only track byte retail can reach that we do not is
the quaternion decoder's one-key look-ahead running past a track's last run.
That is measured rather than declared: a retail-only track span wider than the
two bytes of one key is a run our walk skipped, and it is a defect.

Coverage is bounded by the corpus. A byte is only in retail's set if this run
fired the identity that reads it, so an unread range means unread *by this run*.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import sqlite3
import struct
from typing import Any, Iterable

from research.tooling.capture.calibrate_theatre_capture import (
    DATABASE_NAME,
    compared_maps,
    open_database,
    resolve_session,
)
from research.tooling.capture.decoder_coverage import (
    DECODER_READ,
    Recorder,
    UnmeasuredRead,
    instrumented,
)
from research.tooling.capture.resolve_consumed_spans import (
    ROLE_TRACK_HEADER,
    ROLE_TRACK_KEY,
    Coverage,
    SpanFault,
    Walker,
    file_sha256,
    load_images,
    resolve_cell,
    resolve_sequence,
    tool_commit,
)
from research.tooling.capture.verify_model_skeleton_census import (
    _differing_spans,
    install_key,
)

REPORT_NAME = "byte-coverage.json"

SEQUENCE_KIND = "SEQP"
ANIMATION_KIND = "ANIM"

EVENT_TABLE = "record_events"
DEFAULT_EVENT_TABLE = "records"

SPAN_TABLES = ("span_sets", "model_coverage", "span_roles", "record_span_sets")

# MDLHeader displacements, from docs/vtmb/mdl_v2531.md. These are format facts
# rather than walk rules, so naming them here duplicates no decision.
HEADER_BYTES = 412
HEADER_NUM_BONES = 240
HEADER_BONE_INDEX = 244
HEADER_NUM_LOCAL_ANIMS = 264
HEADER_LOCAL_ANIM_INDEX = 268
HEADER_NUM_LOCAL_SEQ = 272
HEADER_LOCAL_SEQ_INDEX = 276
HEADER_NUM_INCLUDE_MODELS = 404
HEADER_INCLUDE_MODEL_INDEX = 408

BONE_STRIDE = 160
SEQ_DESC_STRIDE = 764
ANIM_DESC_STRIDE = 72
ANIM_RECORD_STRIDE = 32
MODEL_GROUP_STRIDE = 116
MODEL_GROUP_REMAP_OFFSET = 0x10
REMAP_RECORD_BYTES = 56

ANIM_DESC_ANIMINDEX = 48
ANIM_DESC_NUMFRAMES = 12

REGION_HEADER = "header"
REGION_BONES = "bone_array"
REGION_SEQUENCES = "seqdesc_array"
REGION_ANIMATIONS = "animdesc_array"
REGION_INCLUDES = "include_group_array"
REGION_REMAP = "bone_remap_array"
REGION_ANIM_RECORDS = "anim_records"
REGION_TRACKS = "anim_tracks"
REGION_OTHER = "unindexed"

# Field tables. Only the fields the format documents are named; a byte inside a
# structure with no named field is reported at its displacement rather than
# folded into the nearest neighbour, because an unnamed byte in a difference is
# the interesting kind.
HEADER_FIELDS = (
    (0, 4, "ID"), (4, 8, "Version"), (8, 12, "Checksum"), (12, 140, "Name"),
    (140, 144, "Length"), (144, 156, "GlobalScale"), (156, 168, "EyePosition"),
    (168, 180, "IllumPosition"), (180, 192, "HullMin"), (192, 204, "HullMax"),
    (204, 216, "ViewBBMin"), (216, 228, "ViewBBMax"), (228, 232, "Flags"),
    (240, 244, "NumBones"), (244, 248, "BoneIndex"),
    (264, 268, "NumLocalAnims"), (268, 272, "LocalAnimIndex"),
    (272, 276, "NumLocalSeq"), (276, 280, "LocalSeqIndex"),
    (280, 284, "NumLocalNodes"), (292, 296, "NumTextures"),
    (296, 300, "TextureIndex"), (320, 324, "NumBodyParts"),
    (324, 328, "BodyPartIndex"),
    (404, 408, "NumIncludeModels"), (408, 412, "IncludeModelIndex"),
)
BONE_FIELDS = (
    (0, 4, "NameIndex"), (4, 8, "ParentBone"), (8, 32, "BoneController"),
    (32, 44, "pos"), (44, 60, "quat"), (60, 72, "posscale"), (72, 88, "rotscale"),
    (88, 136, "poseToBone"), (136, 140, "Flags"), (140, 144, "ProcType"),
    (144, 148, "ProcIndex"), (148, 152, "PhysicsBone"),
    (152, 156, "SurfacePropIndex"), (156, 160, "Contents"),
)
SEQ_FIELDS = (
    (0, 4, "szlabelindex"), (4, 8, "szactivitynameindex"), (8, 12, "flags"),
    (12, 16, "activity"), (16, 20, "actweight"), (20, 24, "numevents"),
    (24, 28, "eventindex"), (28, 52, "bbox"), (52, 56, "numblends"),
    (56, 568, "anim[16][16]"), (572, 580, "groupsize"), (580, 588, "paramindex"),
    (588, 596, "paramstart"), (596, 604, "paramend"),
    (660, 664, "numautolayers"), (664, 668, "autolayerindex"),
)
ANIM_FIELDS = (
    (0, 4, "NameIndex"), (4, 8, "fps"), (8, 12, "flags"), (12, 16, "numframes"),
    (16, 20, "nummovements"), (20, 24, "movementindex"), (24, 36, "bbmin"),
    (36, 48, "bbmax"), (48, 52, "animindex"), (52, 56, "numikrules"),
    (56, 60, "ikruleindex"),
)
ANIM_RECORD_FIELDS = (
    (0, 4, "weight"),
    (4, 8, "offset[posX]"), (8, 12, "offset[posY]"), (12, 16, "offset[posZ]"),
    (16, 20, "offset[rotX]"), (20, 24, "offset[rotY]"), (24, 28, "offset[rotZ]"),
    (28, 32, "offset[rotW]"),
)
GROUP_FIELDS = (
    (0, 4, "FilenameIndex"), (4, 8, "loaded model"), (8, 12, "first virtual seq"),
    (12, 16, "virtual seq count"), (16, 20, "bone remap offset"),
    (0x44, 0x74, "pose parameter remap"),
)

STRUCT_OF_REGION = {
    REGION_HEADER: ("MDLHeader", None, HEADER_FIELDS),
    REGION_BONES: ("StudioBone", BONE_STRIDE, BONE_FIELDS),
    REGION_SEQUENCES: ("StudioSeqDesc", SEQ_DESC_STRIDE, SEQ_FIELDS),
    REGION_ANIMATIONS: ("StudioAnimDesc", ANIM_DESC_STRIDE, ANIM_FIELDS),
    REGION_INCLUDES: ("StudioModelGroup", MODEL_GROUP_STRIDE, GROUP_FIELDS),
    REGION_REMAP: ("BoneRemapRecord", REMAP_RECORD_BYTES, ()),
    REGION_ANIM_RECORDS: ("StudioAnimRecord", ANIM_RECORD_STRIDE, ANIM_RECORD_FIELDS),
    REGION_TRACKS: ("mstudioanimvalue_t", None, ()),
    REGION_OTHER: ("unindexed", None, ()),
}

# Which region each `ours_only` byte total is bucketed into. Every region has an
# entry, so the buckets are exhaustive and a surprise cannot land in a residue
# nobody declared.
OURS_ONLY_BUCKET = {
    REGION_HEADER: "ours_only_header_bytes",
    REGION_BONES: "ours_only_bone_array_bytes",
    REGION_SEQUENCES: "ours_only_sequence_descriptor_bytes",
    REGION_ANIMATIONS: "ours_only_animation_descriptor_bytes",
    REGION_INCLUDES: "ours_only_include_or_remap_bytes",
    REGION_REMAP: "ours_only_include_or_remap_bytes",
    REGION_ANIM_RECORDS: "ours_only_anim_record_bytes",
    REGION_TRACKS: "ours_only_track_bytes",
    REGION_OTHER: "ours_only_string_or_unindexed_bytes",
}

ACCOUNTED: dict[str, str] = {
    "ours_only_track_bytes": (
        "Retail walks a track to the sampled frame and reads the two keys "
        "bracketing it; the exporter materializes the whole channel. Every run "
        "header and key past the frame is ours alone by construction."
    ),
    "ours_only_bone_array_bytes": (
        "Retail reads the bind and scale dwords of the channels a mask "
        "selected; `read_bones` reads all 160 bytes of every bone once, because "
        "a clip is decoded against the whole skeleton."
    ),
    "ours_only_sequence_descriptor_bytes": (
        "`local_sequences` walks every descriptor the model declares while the "
        "run fired a few dozen, so the array beyond the fired identities is "
        "ours alone and is coverage rather than disagreement."
    ),
    "ours_only_animation_descriptor_bytes": (
        "The exporter reads a clip's own fps and frame count where retail's "
        "cell frame reads numframes and animindex alone."
    ),
    "ours_only_anim_record_bytes": (
        "Retail reads the channel offsets of the channels its mask selected; "
        "`read_anim` reads all seven of every bone."
    ),
    "ours_only_header_bytes": (
        "The exporter resolves the descriptor and include arrays out of the "
        "header, which the cell frame reaches through pointers it was handed."
    ),
    "ours_only_include_or_remap_bytes": (
        "Include-model resolution is the exporter's own bank walk; retail's "
        "captured hooks sit below the virtual-model resolver and never touch "
        "the group records."
    ),
    "ours_only_string_or_unindexed_bytes": (
        "Names are how the exporter keys a clip and a bone. Retail resolves "
        "them at load, outside every hooked frame, so no captured span claims "
        "a string byte."
    ),
    "installed_image_absent": (
        "The installed-image arm re-reads the same identities out of the "
        "patch-first install to show a loader-written byte changes no branch. "
        "It needs the user's game tree, and its absence is coverage."
    ),
    "retail_track_bytes_past_our_whole_track_walk": (
        "The quaternion decoder reads the following run's first key, and at a "
        "track's last run that key is whatever the image holds next. Those "
        "bytes are read by retail and not by us, but they are the look-ahead "
        "overrunning a track rather than a field the decoder failed to read, "
        "so they are reported apart from the missing list."
    ),
}

_TRACK_ROLES = (ROLE_TRACK_HEADER, ROLE_TRACK_KEY)
#: One `mstudioanimvalue_t`. The look-ahead reads exactly one, so a wider span
#: on the same side is a run our track walk skipped rather than a look-ahead.
_KEY_BYTES = 2

# A population that means the comparison itself would be reading the wrong bytes.
DEFECTS = (
    "owner_image_absent",
    "captured_image_capped",
    "retail_walk_faulted",
    "retail_span_outside_image",
    "rewalk_union_differs_from_stored",
    "decoder_faulted",
    "decoder_read_outside_image",
    "decoder_read_unmeasured",
    "retail_track_span_wider_than_one_key",
    "layout_regions_collided",
    "installed_image_read_set_differs",
)

MAX_REPORTED_SPANS = 24
MAX_REPORTED_FIELDS = 40
MAX_REPORTED_IDENTITIES = 120
_ZERO_BLOCK = 4096


def address(value: int | None) -> str | None:
    return None if value is None else f"0x{value:08x}"


def _i32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<i", data, offset)[0]


# --------------------------------------------------------------------------
# Interval algebra. Both arms accumulate into `Coverage`, which is one bit per
# byte of the image, so union, difference and intersection are bitwise and a
# byte cannot be counted twice however many reads claimed it.
# --------------------------------------------------------------------------


def _bits(coverage: Coverage | None, size: int) -> bytearray:
    if coverage is None:
        return bytearray((size + 7) // 8)
    return coverage.bits


def _combine(left: bytearray, right: bytearray, operation: str) -> bytearray:
    """Bitwise set algebra over two whole-image bitmaps.

    Done as one arbitrary-precision integer rather than byte by byte: an image
    of several megabytes is most of a megabit, and a Python loop over it costs
    more than every walk that produced it.
    """
    length = max(len(left), len(right))
    a = int.from_bytes(left, "little")
    b = int.from_bytes(right, "little")
    if operation == "andnot":
        value = a & ~b
    elif operation == "and":
        value = a & b
    else:
        value = a | b
    value &= (1 << (length * 8)) - 1
    return bytearray(value.to_bytes(length, "little"))


def _andnot(left: bytearray, right: bytearray) -> bytearray:
    return _combine(left, right, "andnot")


def _and(left: bytearray, right: bytearray) -> bytearray:
    return _combine(left, right, "and")


def _or(left: bytearray, right: bytearray) -> bytearray:
    return _combine(left, right, "or")


def _popcount(bits: bytearray) -> int:
    return int.from_bytes(bits, "little").bit_count()


def _intervals(bits: bytearray, size: int) -> list[tuple[int, int]]:
    """Runs of set bits, as half-open byte intervals.

    A difference bitmap over a multi-megabyte image is mostly zero, so whole
    blocks are rejected by one comparison before any bit is examined.
    """
    spans: list[tuple[int, int]] = []
    start: int | None = None
    length = len(bits)
    zero = bytes(_ZERO_BLOCK)
    base = 0
    while base < length:
        end = min(base + _ZERO_BLOCK, length)
        block = bits[base:end]
        if block == zero[: end - base]:
            if start is not None:
                spans.append((start, base * 8))
                start = None
            base = end
            continue
        for index in range(base, end):
            byte = bits[index]
            if byte == 0:
                if start is not None:
                    spans.append((start, index * 8))
                    start = None
                continue
            if byte == 0xFF and start is not None:
                continue
            offset = index * 8
            for bit in range(8):
                if byte & (1 << bit):
                    if start is None:
                        start = offset + bit
                elif start is not None:
                    spans.append((start, offset + bit))
                    start = None
        base = end
    if start is not None:
        spans.append((start, length * 8))
    return [(low, min(high, size)) for low, high in spans if low < size]


# --------------------------------------------------------------------------
# Where a byte of an image sits, and which field it belongs to.
# --------------------------------------------------------------------------


class Layout:
    """The structures one model image declares, as byte regions.

    Built from the image's own header, so a region is what the model says it is
    rather than what either walker assumed. Regions are disjoint: an overlap
    would let one byte be attributed twice, so a later region that collides with
    an earlier one is dropped and counted.
    """

    def __init__(self, image: bytes) -> None:
        self.size = len(image)
        self.collisions = 0
        self.regions: list[tuple[int, int, str, int]] = []
        self._starts: list[int] = []
        try:
            self.num_bones = _i32(image, HEADER_NUM_BONES)
            self.bone_index = _i32(image, HEADER_BONE_INDEX)
            self.num_anims = _i32(image, HEADER_NUM_LOCAL_ANIMS)
            self.anim_index = _i32(image, HEADER_LOCAL_ANIM_INDEX)
            self.num_seqs = _i32(image, HEADER_NUM_LOCAL_SEQ)
            self.seq_index = _i32(image, HEADER_LOCAL_SEQ_INDEX)
            self.num_includes = _i32(image, HEADER_NUM_INCLUDE_MODELS)
            self.include_index = _i32(image, HEADER_INCLUDE_MODEL_INDEX)
        except struct.error:
            self.num_bones = self.bone_index = 0
            self.num_anims = self.anim_index = 0
            self.num_seqs = self.seq_index = 0
            self.num_includes = self.include_index = 0
            return

        fixed = [
            (0, min(HEADER_BYTES, self.size), REGION_HEADER, -1),
            (self.bone_index, self.bone_index + BONE_STRIDE * self.num_bones,
             REGION_BONES, -1),
            (self.seq_index, self.seq_index + SEQ_DESC_STRIDE * self.num_seqs,
             REGION_SEQUENCES, -1),
            (self.anim_index, self.anim_index + ANIM_DESC_STRIDE * self.num_anims,
             REGION_ANIMATIONS, -1),
            (self.include_index,
             self.include_index + MODEL_GROUP_STRIDE * self.num_includes,
             REGION_INCLUDES, -1),
        ]
        for index in range(self.num_includes):
            entry = self.include_index + index * MODEL_GROUP_STRIDE
            try:
                offset = _i32(image, entry + MODEL_GROUP_REMAP_OFFSET)
            except struct.error:
                continue
            base = entry + offset
            fixed.append(
                (base, base + REMAP_RECORD_BYTES * self.num_bones, REGION_REMAP, index)
            )

        blocks: list[tuple[int, int]] = []
        for index in range(self.num_anims):
            descriptor = self.anim_index + index * ANIM_DESC_STRIDE
            try:
                animindex = _i32(image, descriptor + ANIM_DESC_ANIMINDEX)
            except struct.error:
                continue
            if animindex <= 0:
                continue
            blocks.append((descriptor + animindex, index))
        for start, index in blocks:
            fixed.append(
                (start, start + ANIM_RECORD_STRIDE * self.num_bones,
                 REGION_ANIM_RECORDS, index)
            )

        for start, end, kind, index in sorted(fixed):
            if start < 0 or end > self.size or end <= start:
                continue
            if self.regions and start < self.regions[-1][1]:
                self.collisions += 1
                continue
            self.regions.append((start, end, kind, index))

        # A track region is what lies between one clip's per-bone records and
        # the next structure the model declares. No header field bounds a
        # clip's RLE data, so the region is an upper bound rather than the
        # track extent: a clip's name string and any padding after its last
        # track fall inside it. Bounding it by the next declared structure is
        # what keeps a model whose blocks are followed by geometry from
        # reporting that geometry as animation data.
        tracks: list[tuple[int, int, str, int]] = []
        for position, (start, end, kind, index) in enumerate(self.regions):
            if kind != REGION_ANIM_RECORDS:
                continue
            following = (
                self.regions[position + 1][0]
                if position + 1 < len(self.regions)
                else self.size
            )
            if following > end:
                tracks.append((end, following, REGION_TRACKS, index))
        self.regions = sorted(self.regions + tracks)
        self._starts = [region[0] for region in self.regions]

    def region_at(self, offset: int) -> tuple[int, int, str, int]:
        """The region containing one offset, or the gap it falls in.

        A gap is bounded by the next declared region rather than by the end of
        the image: returning the whole remainder would swallow every structure
        above the offset into one unindexed bucket.
        """
        low, high = 0, len(self.regions)
        while low < high:
            middle = (low + high) // 2
            if self._starts[middle] <= offset:
                low = middle + 1
            else:
                high = middle
        if low:
            start, end, kind, index = self.regions[low - 1]
            if offset < end:
                return start, end, kind, index
        following = self._starts[low] if low < len(self.regions) else self.size
        return offset, max(following, offset + 1), REGION_OTHER, -1

    def attribute(self, spans: Iterable[tuple[int, int]]) -> dict[str, dict[str, Any]]:
        """Bytes per named field, over a set of intervals.

        Walked structure by structure rather than byte by byte: an interval is
        cut at the region boundaries it crosses, then at the element boundaries
        of that region's stride, and only the handful of field ranges inside one
        element are intersected.
        """
        totals: dict[str, dict[str, Any]] = {}

        def tally(name: str, region: str, count: int, low: int) -> None:
            if count <= 0:
                return
            entry = totals.setdefault(
                name, {"field": name, "region": region, "bytes": 0, "first_offset": low}
            )
            entry["bytes"] += count
            entry["first_offset"] = min(entry["first_offset"], low)

        for low, high in spans:
            cursor = low
            while cursor < high:
                start, end, kind, _ = self.region_at(cursor)
                stop = min(high, end)
                name, stride, fields = STRUCT_OF_REGION[kind]
                if stride is None or not fields:
                    tally(f"{name}:*", kind, stop - cursor, cursor)
                    cursor = stop
                    continue
                position = cursor
                while position < stop:
                    element = (position - start) // stride
                    element_start = start + element * stride
                    element_stop = min(stop, element_start + stride)
                    for field_low, field_high, field in fields:
                        overlap_low = max(position, element_start + field_low)
                        overlap_high = min(element_stop, element_start + field_high)
                        if overlap_high > overlap_low:
                            tally(
                                f"{name}.{field}",
                                kind,
                                overlap_high - overlap_low,
                                overlap_low,
                            )
                    named = sum(
                        max(
                            0,
                            min(element_stop, element_start + field_high)
                            - max(position, element_start + field_low),
                        )
                        for field_low, field_high, _ in fields
                    )
                    gap = (element_stop - position) - named
                    if gap:
                        tally(f"{name}:unnamed", kind, gap, position)
                    position = element_stop
                cursor = stop
        return totals


# --------------------------------------------------------------------------
# Support and corpus
# --------------------------------------------------------------------------


def event_table(connection: sqlite3.Connection) -> str:
    names = {
        name
        for (name,) in connection.execute(
            "SELECT name FROM sqlite_master WHERE type IN ('table', 'view')"
        )
    }
    return EVENT_TABLE if EVENT_TABLE in names else DEFAULT_EVENT_TABLE


def support(connection: sqlite3.Connection) -> dict[str, Any]:
    objects = {
        name
        for (name,) in connection.execute(
            "SELECT name FROM sqlite_master WHERE type IN ('table', 'view')"
        )
    }
    table = event_table(connection)
    columns = {row[1] for row in connection.execute(f"PRAGMA table_info({table})")}
    return {
        "event_table": table,
        "carries_contributions": {
            "owner_checksum",
            "sequence_index",
            "animation_index",
        }
        <= columns,
        "carries_spans": set(SPAN_TABLES) <= objects,
        "carries_images": "model_images" in objects,
        "carries_census": "model_headers" in objects,
    }


def prepare(connection: sqlite3.Connection, flags: dict[str, Any]) -> None:
    """One row per distinct fired identity — CAP4.1's grain, reached the same way.

    Every grouping term is the underlying expression rather than the output
    alias: an event row carries its own `checksum`, the drawn model's rather than
    the contribution owner's, so `GROUP BY checksum` would silently group
    unrelated owners together.
    """
    table = flags["event_table"]
    connection.execute(
        f"""
        CREATE TEMP TABLE fired_identity AS
        SELECT owner_checksum AS checksum,
               kind AS record_kind,
               CASE WHEN kind = '{SEQUENCE_KIND}' THEN sequence_index
                    ELSE animation_index END AS idx,
               count(*) AS records
        FROM {table}
        WHERE kind IN ('{SEQUENCE_KIND}', '{ANIMATION_KIND}')
        GROUP BY owner_checksum,
                 kind,
                 CASE WHEN kind = '{SEQUENCE_KIND}' THEN sequence_index
                      ELSE animation_index END
        """
    )


def corpus(connection: sqlite3.Connection) -> dict[str, Any]:
    rows = connection.execute(
        "SELECT record_kind, count(*), sum(records), count(DISTINCT checksum) "
        "FROM fired_identity GROUP BY record_kind"
    ).fetchall()
    by_kind = {
        kind: {"identities": identities, "records": records, "owners": owners}
        for kind, identities, records, owners in rows
    }
    return {
        "owners": connection.execute(
            "SELECT count(DISTINCT checksum) FROM fired_identity"
        ).fetchone()[0],
        "identities": connection.execute(
            "SELECT count(*) FROM fired_identity"
        ).fetchone()[0],
        "records": connection.execute(
            "SELECT sum(records) FROM fired_identity"
        ).fetchone()[0]
        or 0,
        "sequence": by_kind.get(SEQUENCE_KIND, {"identities": 0, "records": 0, "owners": 0}),
        "animation": by_kind.get(ANIMATION_KIND, {"identities": 0, "records": 0, "owners": 0}),
    }


def modules(connection: sqlite3.Connection) -> dict[str, str]:
    """The hash-gated modules the run captured.

    A difference against a decoder is only meaningful beside the build it is a
    difference from, and that build is named by the bytes the capture recorded
    rather than by the recipe that launched it.
    """
    return {
        name: digest
        for name, digest in connection.execute(
            "SELECT name, sha256 FROM modules ORDER BY name"
        )
    }


def owner_names(connection: sqlite3.Connection) -> dict[int, str]:
    return {
        int(checksum): name
        for checksum, name in connection.execute(
            "SELECT checksum, min(model_name) FROM model_headers GROUP BY checksum"
        )
    }


# --------------------------------------------------------------------------
# The retail arm
# --------------------------------------------------------------------------


def retail_arm(
    connection: sqlite3.Connection, images: dict[int, Walker]
) -> tuple[
    dict[str, Any], dict[int, Coverage], dict[tuple, Coverage], dict[int, Coverage]
]:
    """Retail's consumed bytes, per owner and per fired identity.

    The stored `model_coverage` union is the headline product, and it is enough
    for the per-owner difference. It carries no identity, though, so each
    distinct span set is re-walked at each frame it was resolved at — the same
    walk that produced it, driven from what the dictionary already stores rather
    than from the records again. Both are kept because the re-walk reproducing
    the stored union byte for byte is what says the two passes agree; a
    disagreement means one of them is wrong and no difference below it is
    trustworthy.
    """
    stored: dict[int, list[tuple[int, int]]] = {}
    for checksum, start, end in connection.execute(
        'SELECT checksum, start, "end" FROM model_coverage ORDER BY checksum, ordinal'
    ):
        stored.setdefault(int(checksum), []).append((int(start), int(end)))

    counts = {
        "owner_image_absent": 0,
        "retail_walk_faulted": 0,
        "retail_span_outside_image": 0,
        "rewalk_union_differs_from_stored": 0,
    }
    faults: dict[str, int] = {}
    per_owner: dict[int, Coverage] = {}
    per_owner_tracks: dict[int, Coverage] = {}
    per_identity: dict[tuple, Coverage] = {}
    owners_of_identity: dict[int, set[tuple]] = {}
    walks = 0

    rows = connection.execute(
        """
        SELECT DISTINCT s.checksum, s.kind, s.shape, r.frame
        FROM record_span_sets r JOIN span_sets s ON s.id = r.span_set_id
        WHERE r.resolved = 1
        ORDER BY s.checksum, s.kind, s.id, r.frame
        """
    ).fetchall()
    for checksum, kind, shape, frame in rows:
        checksum = int(checksum)
        walker = images.get(checksum)
        if walker is None:
            counts["owner_image_absent"] += 1
            continue
        payload = json.loads(shape)
        if kind == ANIMATION_KIND:
            index, quaternion, position = int(payload[0]), payload[1], payload[2]
            identity = (checksum, ANIMATION_KIND, index)
            try:
                spans = resolve_cell(
                    walker, index, int(frame or 0), quaternion, position
                )
            except SpanFault as error:
                counts["retail_walk_faulted"] += 1
                faults[str(error)] = faults.get(str(error), 0) + 1
                continue
        else:
            index, cells = int(payload[0]), payload[1]
            identity = (checksum, SEQUENCE_KIND, index)
            try:
                spans = resolve_sequence(walker, index, cells)
            except SpanFault as error:
                counts["retail_walk_faulted"] += 1
                faults[str(error)] = faults.get(str(error), 0) + 1
                continue
        walks += 1
        for start, end, _ in spans:
            if start < 0 or end > len(walker.image):
                counts["retail_span_outside_image"] += 1
        # Accumulated once, per identity. The owner union is the OR of its own
        # identities below, which is the same set for half the work -- and a
        # cutscene walks a hundred and forty thousand cells, so the half
        # matters.
        per_identity.setdefault(identity, Coverage(len(walker.image))).add(spans)
        owners_of_identity.setdefault(checksum, set()).add(identity)
        # Kept apart because a track read is the one retail read that can land
        # outside the structure it belongs to: the quaternion decoder looks one
        # key ahead, and at the last run of a track that key is whatever follows
        # the track in the image. Without the role, those bytes read as a field
        # our decoder failed to read rather than as retail reading past an end.
        per_owner_tracks.setdefault(checksum, Coverage(len(walker.image))).add(
            [span for span in spans if span[2] in _TRACK_ROLES]
        )

    for checksum, identities in owners_of_identity.items():
        size = len(images[checksum].image)
        union = Coverage(size)
        for identity in identities:
            union.bits = _or(union.bits, per_identity[identity].bits)
        per_owner[checksum] = union

    rewalk_bytes = 0
    stored_bytes = 0
    disagreeing: list[dict[str, Any]] = []
    for checksum, coverage in sorted(per_owner.items()):
        rewalk = _intervals(coverage.bits, coverage.size)
        rewalk_bytes += sum(high - low for low, high in rewalk)
        declared = stored.get(checksum, [])
        stored_bytes += sum(high - low for low, high in declared)
        if rewalk != declared:
            counts["rewalk_union_differs_from_stored"] += 1
            disagreeing.append(
                {
                    "checksum": address(checksum),
                    "stored_intervals": len(declared),
                    "rewalk_intervals": len(rewalk),
                    "stored_bytes": sum(high - low for low, high in declared),
                    "rewalk_bytes": sum(high - low for low, high in rewalk),
                }
            )
    for checksum in stored:
        if checksum not in per_owner:
            counts["rewalk_union_differs_from_stored"] += 1
            disagreeing.append(
                {"checksum": address(checksum), "reason": "the re-walk reached it not at all"}
            )

    roles = {
        role: {"intervals": intervals, "bytes": total}
        for role, intervals, total in connection.execute(
            "SELECT role, sum(intervals), sum(bytes) FROM span_roles GROUP BY role"
        )
    }
    report = {
        "available": True,
        "owners": len(per_owner),
        "span_set_frames_walked": walks,
        "identities": len(per_identity),
        "stored_bytes": stored_bytes,
        "rewalk_bytes": rewalk_bytes,
        "stored_intervals": sum(len(spans) for spans in stored.values()),
        "roles": roles,
        "counts": counts,
        "faults": dict(sorted(faults.items())),
        "disagreeing_owners": disagreeing[:MAX_REPORTED_SPANS],
    }
    return report, per_owner, per_identity, per_owner_tracks


# --------------------------------------------------------------------------
# The decoder arm
# --------------------------------------------------------------------------


def _drive(
    image: bytes, animations: list[int], *, export_frame: bool
) -> tuple[dict[Any, Recorder], list[str], list[Any]]:
    """Run the exporter's own decoder over one image and record what it read.

    Each entry point gets its own recorder so a read can be attributed to the
    identity that caused it. The two descriptor reads that reach a clip -- the
    array base and the clip's frame count -- are the ones `find_anim` and
    `local_sequences` make to reach the same descriptor, so they go through the
    decoder's own accessors and are counted as the decoder's.
    """
    from elysium_pipeline.formats import mdl_skel

    parts: dict[Any, Recorder] = {}
    faults: list[str] = []
    bones: list[Any] = []
    with instrumented(mdl_skel, image) as (data, recorder):
        try:
            bones = mdl_skel.read_bones(data)
        except UnmeasuredRead:
            raise
        except Exception as error:  # noqa: BLE001 - a decoder fault is a finding
            faults.append(f"read_bones: {type(error).__name__}: {error}")
    parts["bones"] = recorder

    sequences: list[Any] = []
    with instrumented(mdl_skel, image) as (data, recorder):
        try:
            sequences = mdl_skel.local_sequences(data)
        except UnmeasuredRead:
            raise
        except Exception as error:  # noqa: BLE001
            faults.append(f"local_sequences: {type(error).__name__}: {error}")
    parts["sequences"] = recorder

    for index in animations:
        with instrumented(mdl_skel, image) as (data, recorder):
            try:
                base = mdl_skel._i32(data, HEADER_LOCAL_ANIM_INDEX)
                descriptor = base + index * ANIM_DESC_STRIDE
                frames = mdl_skel._i32(data, descriptor + ANIM_DESC_NUMFRAMES)
                mdl_skel.read_anim(data, bones, descriptor, frames)
            except UnmeasuredRead:
                raise
            except Exception as error:  # noqa: BLE001
                faults.append(f"read_anim[{index}]: {type(error).__name__}: {error}")
        parts[(ANIMATION_KIND, index)] = recorder

    if export_frame:
        for ordinal, sequence in enumerate(sequences):
            with instrumented(mdl_skel, image) as (data, recorder):
                try:
                    mdl_skel.read_anim(data, bones, sequence.base, sequence.frames)
                except UnmeasuredRead:
                    raise
                except Exception as error:  # noqa: BLE001
                    faults.append(
                        f"read_anim[{sequence.label}]: {type(error).__name__}: {error}"
                    )
            parts[("export", ordinal)] = recorder
    return parts, faults, sequences


def _sequence_attribution(
    recorder: Recorder, layout: Layout
) -> dict[int, list[tuple[int, int]]]:
    """Split `local_sequences`' reads by the descriptor each one was reached from.

    The reads arrive in the order the decoder made them, and each descriptor it
    examines begins with a read inside that descriptor's own 764 bytes. So a
    read landing outside the array — the label string, the animation descriptor
    the base cell pointed at — belongs to whichever descriptor was last touched,
    which is causal attribution from the trace rather than a transcription of
    the loop.
    """
    array_low = layout.seq_index
    array_high = layout.seq_index + SEQ_DESC_STRIDE * layout.num_seqs
    attributed: dict[int, list[tuple[int, int]]] = {}
    current: int | None = None
    for offset, size in recorder.trace:
        if array_low <= offset < array_high:
            current = (offset - array_low) // SEQ_DESC_STRIDE
        if current is None:
            continue
        attributed.setdefault(current, []).append((offset, offset + size))
    return attributed


def decoder_arm(
    images: dict[int, Walker],
    fired: dict[int, dict[str, list[int]]],
    layouts: dict[int, Layout],
    *,
    export_frame: bool,
) -> tuple[dict[str, Any], dict[int, Coverage], dict[tuple, Coverage]]:
    """What `mdl_skel` reads when it decodes the same identities.

    Measured, not transcribed: the decoder runs unmodified under
    `decoder_coverage`'s observation, so what is recorded is what the exporter
    would read off the same image.
    """
    counts = {
        "decoder_faulted": 0,
        "decoder_read_outside_image": 0,
        "decoder_read_unmeasured": 0,
        "captured_image_capped": 0,
    }
    faults: list[dict[str, Any]] = []
    per_owner: dict[int, Coverage] = {}
    per_identity: dict[tuple, Coverage] = {}
    reads = 0
    clips = 0

    for checksum, walker in sorted(images.items()):
        wanted = fired.get(checksum)
        if wanted is None:
            continue
        if walker.capped:
            counts["captured_image_capped"] += 1
        layout = layouts[checksum]
        animations = sorted(wanted.get(ANIMATION_KIND, []))
        try:
            parts, messages, _ = _drive(
                walker.image, animations, export_frame=export_frame
            )
        except UnmeasuredRead as error:
            counts["decoder_read_unmeasured"] += 1
            faults.append({"checksum": address(checksum), "fault": str(error)})
            continue
        if messages:
            counts["decoder_faulted"] += len(messages)
            faults.extend(
                {"checksum": address(checksum), "fault": message}
                for message in messages
            )

        owner = per_owner.setdefault(checksum, Coverage(len(walker.image)))
        for key, recorder in parts.items():
            reads += recorder.reads
            counts["decoder_read_outside_image"] += recorder.outside
            owner.add(recorder.spans())
            if isinstance(key, tuple) and key[0] == ANIMATION_KIND:
                clips += 1
        skeleton = parts["bones"].spans()
        for index in animations:
            recorder = parts.get((ANIMATION_KIND, index))
            if recorder is None:
                continue
            coverage = Coverage(len(walker.image))
            coverage.add(recorder.spans())
            # A clip is decoded against the whole skeleton the exporter read, so
            # the bone array belongs to every animation identity rather than to
            # none of them.
            coverage.add(skeleton)
            per_identity[(checksum, ANIMATION_KIND, index)] = coverage
        attributed = _sequence_attribution(parts["sequences"], layout)
        for index in sorted(wanted.get(SEQUENCE_KIND, [])):
            coverage = Coverage(len(walker.image))
            coverage.add(
                [
                    (low, high, DECODER_READ)
                    for low, high in attributed.get(index, [])
                ]
            )
            per_identity[(checksum, SEQUENCE_KIND, index)] = coverage

    return (
        {
            "available": True,
            "frame": "export" if export_frame else "fired",
            "owners": len(per_owner),
            "identities": len(per_identity),
            "clips_decoded": clips,
            "reads": reads,
            "bytes": sum(coverage.byte_count() for coverage in per_owner.values()),
            "counts": counts,
            "faults": faults[:MAX_REPORTED_SPANS],
        },
        per_owner,
        per_identity,
    )


# --------------------------------------------------------------------------
# The difference
# --------------------------------------------------------------------------


def _rank(totals: dict[str, dict[str, Any]], limit: int) -> list[dict[str, Any]]:
    return sorted(totals.values(), key=lambda entry: -entry["bytes"])[:limit]


def difference(
    images: dict[int, Walker],
    layouts: dict[int, Layout],
    names: dict[int, str],
    retail_owner: dict[int, Coverage],
    ours_owner: dict[int, Coverage],
    retail_identity: dict[tuple, Coverage],
    ours_identity: dict[tuple, Coverage],
    retail_tracks: dict[int, Coverage],
) -> dict[str, Any]:
    """The three sets, per owner and per fired identity.

    The missing side is split before it is attributed. A byte retail read as
    part of a track walk and unread by us is the quaternion decoder's one-key
    look-ahead running past a track's last run, not a field the decoder failed
    to read, and reporting it by whatever structure it landed in would name the
    wrong thing.
    """
    totals = {
        "image_bytes": 0,
        "retail_bytes": 0,
        "ours_bytes": 0,
        "both_bytes": 0,
        "retail_only_bytes": 0,
        "ours_only_bytes": 0,
        "neither_bytes": 0,
    }
    counts = {name: 0 for name in OURS_ONLY_BUCKET.values()}
    counts["retail_track_bytes_past_our_whole_track_walk"] = 0
    counts["retail_track_span_wider_than_one_key"] = 0
    # A model whose declared arrays overlap has a region the layout dropped, and
    # the bytes it covered are attributed to whichever region survived. The set
    # algebra is unaffected; the field names on the product are not, so it is
    # named rather than left to be noticed in a ranking that reads plausible.
    counts["layout_regions_collided"] = 0
    missing: dict[str, dict[str, Any]] = {}
    surplus: dict[str, dict[str, Any]] = {}
    unknown: dict[str, dict[str, Any]] = {}
    lookahead: dict[str, dict[str, Any]] = {}
    owners: list[dict[str, Any]] = []

    for checksum in sorted(retail_owner):
        walker = images[checksum]
        size = len(walker.image)
        layout = layouts[checksum]
        retail = _bits(retail_owner.get(checksum), size)
        ours = _bits(ours_owner.get(checksum), size)
        missed = _andnot(retail, ours)
        # A retail track byte our whole-track walk did not reach can only be one
        # thing. Inside a run both walks entered, ours reads every key and
        # retail reads two, so ours contains retail's. Retail's look-ahead key
        # sits in the run after the one it stopped in, which our walk also
        # enters whenever frames remain -- except at the last run of a track,
        # where the look-ahead lands past the track altogether. So this
        # population is the look-ahead overrunning, and a span wider than the
        # two bytes of one key is not: that would be a run our walk skipped, and
        # it is a defect.
        past_the_end = _and(missed, _bits(retail_tracks.get(checksum), size))
        overrun = _intervals(past_the_end, size)
        retail_only = _intervals(missed, size)
        structural = _intervals(_andnot(missed, past_the_end), size)
        ours_only = _intervals(_andnot(ours, retail), size)
        both = _popcount(_and(retail, ours))
        covered = _or(retail, ours)
        neither = _intervals(
            _andnot(bytearray(b"\xff" * len(covered)), covered), size
        )
        retail_only_bytes = sum(high - low for low, high in retail_only)
        ours_only_bytes = sum(high - low for low, high in ours_only)
        neither_bytes = sum(high - low for low, high in neither)
        counts["retail_track_bytes_past_our_whole_track_walk"] += _popcount(
            past_the_end
        )
        counts["retail_track_span_wider_than_one_key"] += sum(
            1 for low, high in overrun if high - low > _KEY_BYTES
        )
        counts["layout_regions_collided"] += layout.collisions

        totals["image_bytes"] += size
        totals["retail_bytes"] += _popcount(retail)
        totals["ours_bytes"] += _popcount(ours)
        totals["both_bytes"] += both
        totals["retail_only_bytes"] += retail_only_bytes
        totals["ours_only_bytes"] += ours_only_bytes
        totals["neither_bytes"] += neither_bytes

        owner_missing = layout.attribute(structural)
        owner_surplus = layout.attribute(ours_only)
        owner_unknown = layout.attribute(neither)
        owner_overrun = layout.attribute(overrun)
        for source, sink in (
            (owner_missing, missing),
            (owner_surplus, surplus),
            (owner_unknown, unknown),
            (owner_overrun, lookahead),
        ):
            for field, entry in source.items():
                target = sink.setdefault(
                    field,
                    {
                        "field": field,
                        "region": entry["region"],
                        "bytes": 0,
                        "owners": 0,
                        "example_owner": address(checksum),
                        "example_offset": address(entry["first_offset"]),
                    },
                )
                target["bytes"] += entry["bytes"]
                target["owners"] += 1
        for entry in owner_surplus.values():
            counts[OURS_ONLY_BUCKET[entry["region"]]] += entry["bytes"]

        owners.append(
            {
                "checksum": address(checksum),
                "model_name": names.get(checksum),
                "image_bytes": size,
                "retail_bytes": _popcount(retail),
                "ours_bytes": _popcount(ours),
                "both_bytes": both,
                "retail_only_bytes": retail_only_bytes,
                "ours_only_bytes": ours_only_bytes,
                "neither_bytes": neither_bytes,
                "track_lookahead_bytes": _popcount(past_the_end),
                "retail_only_fields": _rank(owner_missing, MAX_REPORTED_FIELDS),
                # The bytes travel with the span. A range the decoder never
                # reads is a claim about a value nobody has looked at, and
                # reading it needs the image the difference was taken over --
                # so a sample of it comes along rather than staying in a
                # gitignored capture the report outlives.
                "retail_only_spans": [
                    [address(low), address(high), walker.image[low:high].hex()]
                    for low, high in retail_only[:MAX_REPORTED_SPANS]
                ],
                "retail_only_spans_truncated": len(retail_only) > MAX_REPORTED_SPANS,
            }
        )

    identities: list[dict[str, Any]] = []
    identities_with_missing = 0
    for key in sorted(retail_identity):
        checksum, kind, index = key
        size = len(images[checksum].image)
        retail = _bits(retail_identity.get(key), size)
        ours = _bits(ours_identity.get(key), size)
        only = _intervals(_andnot(retail, ours), size)
        if not only:
            continue
        identities_with_missing += 1
        if len(identities) < MAX_REPORTED_IDENTITIES:
            identities.append(
                {
                    "checksum": address(checksum),
                    "model_name": names.get(checksum),
                    "kind": kind,
                    "index": index,
                    "retail_bytes": _popcount(retail),
                    "ours_bytes": _popcount(ours),
                    "retail_only_bytes": sum(high - low for low, high in only),
                    "retail_only_fields": _rank(
                        layouts[checksum].attribute(only), MAX_REPORTED_FIELDS
                    ),
                }
            )

    return {
        "totals": totals,
        "counts": counts,
        "missing_by_field": _rank(missing, MAX_REPORTED_FIELDS),
        "ours_only_by_field": _rank(surplus, MAX_REPORTED_FIELDS),
        "unknown_by_field": _rank(unknown, MAX_REPORTED_FIELDS),
        "track_lookahead_by_field": _rank(lookahead, MAX_REPORTED_FIELDS),
        "owners": owners,
        "identities_with_missing_bytes": identities_with_missing,
        "identities": identities,
        "identities_truncated": identities_with_missing > MAX_REPORTED_IDENTITIES,
    }


# --------------------------------------------------------------------------
# The installed-image arm and the loader-written overlay
# --------------------------------------------------------------------------


def patch_first_reader() -> tuple[Any, str | None]:
    """A `key -> bytes` reader over the patch-first install, or the reason there
    is none.

    Built once and handed to both arms that need it: the index walks the VPKs
    and the loose trees, and building it twice in one pass costs more than
    everything else the install side does.
    """
    try:
        from elysium_pipeline.formats import install

        index = install.build_index(dirs=("models",), verbose=False)
    except Exception as error:  # noqa: BLE001 - an absent install is an answer
        return None, f"{type(error).__name__}: {error}"

    def read(key: str) -> bytes | None:
        return install.read(index, key)

    return read, None


def installed_arm(
    images: dict[int, Walker],
    fired: dict[int, dict[str, list[int]]],
    names: dict[int, str],
    *,
    reader: Any = None,
    reason: str | None = None,
    export_frame: bool = False,
) -> dict[str, Any]:
    """Re-read the same identities out of the installed file.

    The loader rewrites bytes in place, so a captured image and its installed
    source differ in narrow ranges. If none of those ranges reaches a branch, the
    decoder's read set is a property of the source alone and the two runs record
    the same intervals. That is a claim worth answering rather than assuming, and
    a difference would mean our arm depends on which copy it was handed.
    """
    if reader is None:
        return {
            "available": False,
            "reason": reason or "no patch-first install reader",
            "counts": {"installed_image_absent": 1},
        }

    counts = {"installed_image_read_set_differs": 0, "installed_image_absent": 0}
    compared = 0
    differing: list[dict[str, Any]] = []
    loader_bytes = 0
    for checksum, walker in sorted(images.items()):
        wanted = fired.get(checksum)
        if wanted is None:
            continue
        name = names.get(checksum)
        installed = reader(install_key(name)) if name else None
        if installed is None or len(installed) != len(walker.image):
            counts["installed_image_absent"] += 1
            continue
        rewritten, _ = _differing_spans(walker.image, installed)
        loader_bytes += rewritten
        animations = sorted(wanted.get(ANIMATION_KIND, []))
        try:
            captured_parts, _, _ = _drive(
                walker.image, animations, export_frame=export_frame
            )
            installed_parts, _, _ = _drive(
                bytes(installed), animations, export_frame=export_frame
            )
        except UnmeasuredRead as error:
            differing.append({"checksum": address(checksum), "fault": str(error)})
            counts["installed_image_read_set_differs"] += 1
            continue
        compared += 1
        left = Coverage(len(walker.image))
        right = Coverage(len(walker.image))
        for recorder in captured_parts.values():
            left.add(recorder.spans())
        for recorder in installed_parts.values():
            right.add(recorder.spans())
        if left.bits != right.bits:
            counts["installed_image_read_set_differs"] += 1
            differing.append(
                {
                    "checksum": address(checksum),
                    "model_name": name,
                    "captured_bytes": left.byte_count(),
                    "installed_bytes": right.byte_count(),
                }
            )
    return {
        "available": True,
        "owners_compared": compared,
        "loader_written_bytes": loader_bytes,
        "counts": counts,
        "differing": differing[:MAX_REPORTED_SPANS],
    }


def loader_written(
    images: dict[int, Walker],
    layouts: dict[int, Layout],
    names: dict[int, str],
    retail_owner: dict[int, Coverage],
    ours_owner: dict[int, Coverage],
    *,
    reader: Any = None,
    reason: str | None = None,
) -> dict[str, Any]:
    """Which of the missing bytes the loader wrote rather than the file carrying.

    A pointer captured at runtime is a file offset, but a byte the loader
    rewrote is not the byte on disk. A missing range inside one of those ranges
    names a value our decoder could not have read from the source even if it
    tried, so it is marked rather than counted as an ordinary shortfall.
    """
    if reader is None:
        return {
            "available": False,
            "reason": reason or "no patch-first install reader",
        }

    overlap: dict[str, dict[str, Any]] = {}
    total = 0
    resolved = 0
    for checksum in sorted(retail_owner):
        walker = images.get(checksum)
        name = names.get(checksum)
        if walker is None:
            continue
        installed = reader(install_key(name)) if name else None
        if installed is None or len(installed) != len(walker.image):
            continue
        resolved += 1
        size = len(walker.image)
        _, spans = _differing_spans(walker.image, bytes(installed))
        rewritten = Coverage(size)
        rewritten.add([(low, high, "loader") for low, high in spans])
        retail_only = _andnot(
            _bits(retail_owner.get(checksum), size), _bits(ours_owner.get(checksum), size)
        )
        marked = _intervals(_and(retail_only, rewritten.bits), size)
        if not marked:
            continue
        total += sum(high - low for low, high in marked)
        for field, entry in layouts[checksum].attribute(marked).items():
            target = overlap.setdefault(
                field, {"field": field, "region": entry["region"], "bytes": 0, "owners": 0}
            )
            target["bytes"] += entry["bytes"]
            target["owners"] += 1
    return {
        "available": True,
        "owners_resolved": resolved,
        "missing_bytes_the_loader_wrote": total,
        "by_field": _rank(overlap, MAX_REPORTED_FIELDS),
        "note": (
            "A missing range here is a runtime value rather than a source byte, "
            "so it is not a range the decoder could read off the installed file."
        ),
    }


# --------------------------------------------------------------------------
# Verdict
# --------------------------------------------------------------------------


def decide(
    flags: dict[str, Any],
    retail: dict[str, Any],
    ours: dict[str, Any],
    differences: dict[str, Any],
    installed: dict[str, Any],
) -> dict[str, Any]:
    if not flags["carries_spans"]:
        return {
            "judgeable": False,
            "byte_coverage_compared": False,
            "statement": (
                "This database carries no span dictionary; run "
                "`resolve_consumed_spans` over it before differencing it."
            ),
        }
    if not flags["carries_images"] or not flags["carries_census"]:
        return {
            "judgeable": False,
            "byte_coverage_compared": False,
            "statement": (
                "This database carries no model images or no census, so there "
                "is no owner image for either arm to read."
            ),
        }
    if not flags["carries_contributions"]:
        return {
            "judgeable": False,
            "byte_coverage_compared": False,
            "statement": (
                "This database carries no contribution stream, so no fired "
                "identity names the bytes either arm would read."
            ),
        }
    defects: dict[str, int] = {}
    accounted: dict[str, int] = {}
    unclassified: dict[str, int] = {}
    for arm in (retail, ours, differences, installed):
        for name, value in (arm.get("counts") or {}).items():
            if not value:
                continue
            if name in DEFECTS:
                defects[name] = defects.get(name, 0) + value
            elif name in ACCOUNTED:
                accounted[name] = accounted.get(name, 0) + value
            else:
                # A population no declaration covers is not silently coverage.
                unclassified[name] = unclassified.get(name, 0) + value

    compared = (
        not defects
        and not unclassified
        and bool(retail.get("available"))
        and bool(ours.get("available"))
    )
    totals = differences.get("totals") or {}
    if unclassified:
        statement = (
            f"{len(unclassified)} populations are neither declared defects nor "
            "accounted: "
            + "; ".join(f"{name} = {value:,}" for name, value in unclassified.items())
        )
    elif defects:
        statement = (
            "the two arms do not describe the same bytes: "
            + "; ".join(f"{name} = {value:,}" for name, value in defects.items())
        )
    elif not retail.get("available") or not ours.get("available"):
        statement = "one arm produced nothing, so there is no difference to report"
    else:
        overrun = (differences.get("counts") or {}).get(
            "retail_track_bytes_past_our_whole_track_walk", 0
        )
        statement = (
            f"{totals.get('retail_only_bytes', 0) - overrun:,} of "
            f"{totals.get('retail_bytes', 0):,} bytes retail dereferenced are "
            f"unread by the current decoder across {len(differences.get('owners') or ())} "
            f"owner images, over {differences.get('identities_with_missing_bytes', 0)} "
            f"fired identities, beside {overrun:,} the quaternion look-ahead read "
            f"past the end of a track; {totals.get('ours_only_bytes', 0):,} bytes "
            f"are ours alone across {len(accounted)} accounted populations, and "
            f"{totals.get('neither_bytes', 0):,} bytes are read by neither and stay "
            "unknown."
        )
    return {
        "judgeable": True,
        "byte_coverage_compared": compared,
        "defects": defects,
        "unclassified": unclassified,
        "accounted": {name: ACCOUNTED[name] for name in accounted},
        "accounted_counts": accounted,
        "arms": {
            "retail": retail.get("available", False),
            "decoder": ours.get("available", False),
            "installed": installed.get("available", False),
        },
        "statement": statement,
    }


def verify(
    session: Path,
    *,
    export_frame: bool = False,
    installed: bool = False,
    install_reader: Any = None,
) -> dict[str, Any]:
    from elysium_pipeline.formats import mdl_skel

    connection = open_database(session)
    try:
        flags = support(connection)
        metadata = {
            key: json.loads(value)
            for key, value in connection.execute(
                "SELECT key, value FROM capture_metadata WHERE key IN "
                "('map', 'created_utc', 'tool_git', 'spans_tool_git', "
                "'spans_resolver_version', 'index_version')"
            )
        }
        module_hashes = modules(connection)
        run: dict[str, Any] = {}
        retail: dict[str, Any] = {"available": False, "reason": "not reached"}
        ours: dict[str, Any] = {"available": False, "reason": "not reached"}
        differences: dict[str, Any] = {}
        installed_report: dict[str, Any] = {
            "available": False,
            "reason": "not requested",
        }
        overlay: dict[str, Any] = {"available": False, "reason": "not reached"}
        judgeable = (
            flags["carries_spans"]
            and flags["carries_images"]
            and flags["carries_census"]
            and flags["carries_contributions"]
        )
        if judgeable:
            prepare(connection, flags)
            run = corpus(connection)
            names = owner_names(connection)
            images = load_images(connection)
            layouts = {
                checksum: Layout(walker.image) for checksum, walker in images.items()
            }
            fired: dict[int, dict[str, list[int]]] = {}
            for checksum, kind, index in connection.execute(
                "SELECT checksum, record_kind, idx FROM fired_identity"
            ):
                fired.setdefault(int(checksum), {}).setdefault(kind, []).append(
                    int(index)
                )
            retail, retail_owner, retail_identity, retail_tracks = retail_arm(
                connection, images
            )
            ours, ours_owner, ours_identity = decoder_arm(
                images, fired, layouts, export_frame=export_frame
            )
            differences = difference(
                images,
                layouts,
                names,
                retail_owner,
                ours_owner,
                retail_identity,
                ours_identity,
                retail_tracks,
            )
            reader, reason = (
                (install_reader, None)
                if install_reader is not None
                else patch_first_reader()
            )
            overlay = loader_written(
                images,
                layouts,
                names,
                retail_owner,
                ours_owner,
                reader=reader,
                reason=reason,
            )
            if installed:
                installed_report = installed_arm(
                    images,
                    fired,
                    names,
                    reader=reader,
                    reason=reason,
                    export_frame=export_frame,
                )
        verdict = decide(flags, retail, ours, differences, installed_report)
    finally:
        connection.close()
    return {
        "session": session.name,
        "session_path": str(session),
        "database": str(session / DATABASE_NAME),
        "identity": {
            **metadata,
            "coverage_tool_git": tool_commit(),
            "decoder_module": "elysium_pipeline.formats.mdl_skel",
            "decoder_sha256": file_sha256(Path(mdl_skel.__file__)),
            "modules": module_hashes,
        },
        "support": flags,
        "corpus": run,
        "retail": retail,
        "decoder": ours,
        "difference": differences,
        "loader_written": overlay,
        "installed": installed_report,
        "verdict": verdict,
    }


def compare(reports: list[dict[str, Any]]) -> dict[str, Any]:
    return {
        "sessions": [report["session"] for report in reports],
        "maps": compared_maps(reports),
        "retail_bytes": [
            (report["difference"].get("totals") or {}).get("retail_bytes")
            for report in reports
        ],
        "retail_only_bytes": [
            (report["difference"].get("totals") or {}).get("retail_only_bytes")
            for report in reports
        ],
        "statement": (
            "Which bytes a run consumes depends on the identities and frames it "
            "sampled, so two honest runs of one scene differ there. What a "
            "repeat run supports is that a field named missing in one is named "
            "missing in the other; the totals are reported side by side rather "
            "than compared."
        ),
    }


def summarize(report: dict[str, Any]) -> str:
    lines = [f"{report['session']}: {report['verdict']['statement']}"]
    run = report.get("corpus") or {}
    if run:
        lines.append(
            f"  fired: {run['owners']} owners, {run['identities']} identities, "
            f"{run['records']:,} records"
        )
    retail = report.get("retail") or {}
    if retail.get("available"):
        lines.append(
            f"  retail: {retail['stored_bytes']:,} stored bytes over "
            f"{retail['owners']} owners, re-walked to {retail['rewalk_bytes']:,} "
            f"across {retail['span_set_frames_walked']:,} span-set frames"
        )
    ours = report.get("decoder") or {}
    if ours.get("available"):
        lines.append(
            f"  decoder ({ours['frame']} frame): {ours['bytes']:,} bytes over "
            f"{ours['owners']} owners, {ours['clips_decoded']} clips, "
            f"{ours['reads']:,} reads"
        )
    totals = (report.get("difference") or {}).get("totals") or {}
    if totals:
        lines.append(
            f"  difference: retail only {totals['retail_only_bytes']:,}, "
            f"both {totals['both_bytes']:,}, ours only "
            f"{totals['ours_only_bytes']:,}, neither {totals['neither_bytes']:,} "
            f"of {totals['image_bytes']:,}"
        )
    for entry in (report.get("difference") or {}).get("missing_by_field", [])[:12]:
        lines.append(
            f"    missing {entry['field']}: {entry['bytes']:,} bytes over "
            f"{entry['owners']} owners"
        )
    overrun = ((report.get("difference") or {}).get("counts") or {}).get(
        "retail_track_bytes_past_our_whole_track_walk"
    )
    if overrun:
        lines.append(
            f"    look-ahead past a track end lands in "
            + ", ".join(
                f"{entry['field']} ({entry['bytes']:,})"
                for entry in report["difference"]["track_lookahead_by_field"][:4]
            )
        )
    overlay = report.get("loader_written") or {}
    if overlay.get("available"):
        lines.append(
            f"  of those, {overlay['missing_bytes_the_loader_wrote']:,} bytes are "
            f"loader-written rather than source bytes"
        )
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("sessions", nargs="+")
    parser.add_argument("--report", type=Path)
    parser.add_argument("--export-frame", action="store_true")
    parser.add_argument("--installed-arm", action="store_true")
    parser.add_argument(
        "--no-session-reports", dest="session_reports", action="store_false"
    )
    args = parser.parse_args()

    reports = []
    for value in args.sessions:
        session = resolve_session(value)
        report = verify(
            session, export_frame=args.export_frame, installed=args.installed_arm
        )
        reports.append(report)
        if args.session_reports:
            (session / REPORT_NAME).write_text(
                json.dumps(report, indent=2) + "\n", encoding="utf-8"
            )
        print(summarize(report))
    if args.report:
        args.report.write_text(
            json.dumps(
                {
                    "sessions": [report["session"] for report in reports],
                    "reports": reports,
                    "comparison": compare(reports) if len(reports) > 1 else None,
                },
                indent=2,
            )
            + "\n",
            encoding="utf-8",
        )
    return (
        0
        if all(report["verdict"]["byte_coverage_compared"] for report in reports)
        else 1
    )


if __name__ == "__main__":
    raise SystemExit(main())
