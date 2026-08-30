"""Gapless byte accountability for a model unit's MDL, VTX pair and PHY.

The unit never embeds an opaque copy of a source member, so the ledger is what makes that absence
safe: every admitted byte belongs to a decoded record, a decoded string, an evidence-backed
omission or a verified zero range, and one unclaimed non-zero byte aborts publication.

The walkers below run over the *whole* model corpus rather than the character subset, so they meet
compiler-retained donor payloads the declared tables do not index. Those are classified and named,
never skipped: `seam_map_unit_contract.md` § Non-canonical storage is what forbids the shortcut.
"""

from __future__ import annotations

from bisect import bisect_right
import math
import re
import struct
from typing import Any, Iterable, Mapping

from elysium_pipeline.formats import mdl, mdl_cloth, mdl_skel
from elysium_pipeline.formats.unit_contract import (
    LEDGER_STATES,
    ZERO_STATES,
    ByteLedger,
    ByteLedgerError,
)

#: The trailer the VtMB studio compiler wrote at the end of many MDL images.
_MDL_MESH_RECORD = re.compile(r"\.meshes\[\d+\]$")

COMPILER_TRAILER_MAGIC = b"QnDbTm"
COMPILER_TRAILER_BYTES = 10


class ModelByteCoverageError(ByteLedgerError):
    """A source byte is unclaimed, multiply owned, or fails its verification."""


class ModelLedger(ByteLedger):
    """The contract's ledger with the lookups a binary format walk needs.

    `ByteLedger.claim` scans its whole range list for an overlap, which is quadratic once a
    six-megabyte body produces thirty thousand ranges -- four minutes on LaCroix alone. This
    keeps the contract's rows, states and `finish` and replaces only that scan with a binary
    search over the same list held in offset order, so a claim, a gap scan and an "is this free?"
    question are all logarithmic in the number of ranges.
    """

    __slots__ = ("_starts", "padding_owner")

    def __init__(
        self,
        source_path: str,
        data: bytes,
        *,
        span_offset: int = 0,
        padding_owner: str = "padding",
    ) -> None:
        super().__init__(source_path, data, span_offset=span_offset)
        self._starts: list[int] = []
        # Who pays for verified zero alignment in this member: `mdl.padding`, `vtx.padding` or
        # `phy.padding`, the owners the seam's owner table names for it.
        self.padding_owner = padding_owner

    def _index_before(self, offset: int) -> int:
        return bisect_right(self._starts, offset) - 1

    def _overlap(self, offset: int, length: int) -> dict[str, Any] | None:
        position = self._index_before(offset)
        if position >= 0:
            row = self.ranges[position]
            if int(row["offset"]) + int(row["length"]) > offset:
                return row
        following = position + 1
        if following < len(self.ranges):
            row = self.ranges[following]
            if int(row["offset"]) < offset + length:
                return row
        return None

    def free_spans(self, offset: int, length: int) -> list[tuple[int, int]]:
        """The still-unowned sub-intervals of `[offset, offset+length)`, in file order."""

        end = offset + length
        if offset < 0 or length <= 0 or end > len(self.data):
            raise ModelByteCoverageError(
                f"{self.source_path}: {offset}+{length} is outside the {len(self.data)}-byte member"
            )
        spans: list[tuple[int, int]] = []
        cursor = offset
        for row in self.ranges[max(self._index_before(offset), 0):]:
            start, size = int(row["offset"]), int(row["length"])
            if start >= end:
                break
            stop = start + size
            if stop <= cursor:
                continue
            if start > cursor:
                spans.append((cursor, min(start, end)))
            cursor = max(cursor, stop)
            if cursor >= end:
                break
        if cursor < end:
            spans.append((cursor, end))
        return spans

    def claim_free(self, offset: int, length: int, state: str, owner: str) -> None:
        """Claim only what no earlier range owns; an owned byte keeps the grading it has.

        The alternate VTX variant shares strip-group and index tables between the LODs the unit
        publishes and the LODs the primary variant supersedes. A byte a published record already
        paid for is `mapped` and stays so; the rest is this owner's.
        """

        for start, end in self.free_spans(offset, length):
            self.claim(start, end - start, state, owner)

    def unclaimed(self, offset: int, length: int) -> bool:
        """Whether `[offset, offset+length)` lies wholly inside the file and is unowned."""

        if offset < 0 or length <= 0 or offset + length > len(self.data):
            return False
        return self._overlap(offset, length) is None

    def claim(
        self,
        offset: int,
        length: int,
        state: str,
        owner: str,
        *,
        allow_existing: bool = False,
    ) -> None:
        offset, length = int(offset), int(length)
        if not length:
            return
        if state not in LEDGER_STATES:
            raise ModelByteCoverageError(f"{self.source_path}: invalid ledger state {state!r}")
        if not owner:
            raise ModelByteCoverageError(f"{self.source_path}: range {offset}+{length} has no owner")
        if offset < 0 or length < 0 or offset + length > len(self.data):
            raise ModelByteCoverageError(
                f"{self.source_path}: {owner} range {offset}+{length} exceeds "
                f"{len(self.data)} (file offset {self.absolute(offset)})"
            )
        existing = self._overlap(offset, length)
        if existing is not None:
            # Two VTX LODs address one strip-group table, two ledges one point cloud, and one
            # retail strip's bone-state array falls inside the material-replacement list. A claim
            # the existing range already covers in the same state is that sharing, not a double
            # count; anything else is a decode that contradicts itself.
            start, size = int(existing["offset"]), int(existing["length"])
            if (
                allow_existing
                and existing["state"] == state
                and start <= offset
                and offset + length <= start + size
            ):
                return
            raise ModelByteCoverageError(
                f"{self.source_path}: {owner} range {offset}+{length} overlaps "
                f"{existing['owner']} at {existing['offset']}+{existing['length']}"
            )
        if state in ZERO_STATES and any(self.data[offset:offset + length]):
            raise ModelByteCoverageError(
                f"{self.source_path}: {owner} labels non-zero bytes as {state}"
            )
        position = bisect_right(self._starts, offset)
        self._starts.insert(position, offset)
        self.ranges.insert(
            position, {"offset": offset, "length": length, "state": state, "owner": owner}
        )

    def array(
        self,
        offset: int,
        count: int,
        stride: int,
        owner: str,
        *,
        state: str = "mapped",
        allow_existing: bool = False,
    ) -> None:
        if count < 0:
            raise ModelByteCoverageError(f"{self.source_path}: {owner} has negative count {count}")
        if count and offset <= 0:
            raise ModelByteCoverageError(
                f"{self.source_path}: {owner} has {count} records at invalid offset {offset}"
            )
        self.claim(offset, count * stride, state, owner, allow_existing=allow_existing)

    def records(
        self,
        offset: int,
        count: int,
        stride: int,
        owner: str,
        *,
        state: str = "mapped",
    ) -> None:
        """Claim one range per record of a table, each owned by `owner[i]`.

        The seam's owner table names one owner per bone, hitbox, sequence, mesh, texture, search
        path, attachment, IK chain, include model, event, autolayer and secondary-motion record,
        so those tables are claimed a record at a time rather than as one table-wide range.
        """

        if count < 0:
            raise ModelByteCoverageError(f"{self.source_path}: {owner} has negative count {count}")
        if count and offset <= 0:
            raise ModelByteCoverageError(
                f"{self.source_path}: {owner} has {count} records at invalid offset {offset}"
            )
        for index in range(count):
            self.claim(offset + index * stride, stride, state, f"{owner}[{index}]")

    def cstring(self, offset: int, owner: str) -> None:
        if not 0 <= offset < len(self.data):
            raise ModelByteCoverageError(
                f"{self.source_path}: {owner} string offset {offset} is outside the file"
            )
        end = self.data.find(b"\0", offset)
        if end < 0:
            raise ModelByteCoverageError(
                f"{self.source_path}: {owner} string at {offset} has no terminator"
            )
        try:
            self.data[offset:end].decode("ascii", "strict")
        except UnicodeDecodeError as error:
            raise ModelByteCoverageError(
                f"{self.source_path}: {owner} is not losslessly decodable ASCII"
            ) from error
        self.claim(offset, end + 1 - offset, "mapped-string", owner, allow_existing=True)

    def gaps(self) -> list[tuple[int, int, str, str]]:
        """The unclaimed intervals, each with the owners immediately before and after it."""

        result: list[tuple[int, int, str, str]] = []
        cursor = 0
        previous = "<start>"
        for row in self.ranges:
            offset, length = int(row["offset"]), int(row["length"])
            if offset > cursor:
                result.append((cursor, offset, previous, str(row["owner"])))
            cursor = offset + length
            previous = str(row["owner"])
        if cursor < len(self.data):
            result.append((cursor, len(self.data), previous, "<end>"))
        return result

    def finish(self) -> dict[str, Any]:
        for start, end, previous, following in self.gaps():
            gap = self.data[start:end]
            if any(gap):
                at = start + next(index for index, value in enumerate(gap) if value)
                raise ModelByteCoverageError(
                    f"{self.source_path}: unclaimed non-zero byte at {at}; gap "
                    f"{start}+{end - start} between {previous} and {following}, starts "
                    f"{self.data[at:at + 16].hex()}"
                )
            self.claim(start, end - start, "padding-zero", self.padding_owner)
        try:
            return super().finish()
        except ByteLedgerError as error:
            raise ModelByteCoverageError(str(error)) from error


def _i32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<i", data, offset)[0]


def _u16(data: bytes, offset: int) -> int:
    return struct.unpack_from("<H", data, offset)[0]


def compiler_trailer(data: bytes) -> dict[str, Any] | None:
    """The EOF `uint32 pathOffset` + `QnDbTm` studio-compiler trailer, or None.

    No engine, Crowbar or VAMPTools string references the magic; the pointer names an already
    claimed in-file path. The ten bytes sit inside the declared image length.
    """

    if len(data) < COMPILER_TRAILER_BYTES or data[-6:] != COMPILER_TRAILER_MAGIC:
        return None
    offset = len(data) - COMPILER_TRAILER_BYTES
    pointer = struct.unpack_from("<I", data, offset)[0]
    path = ""
    if 0 <= pointer < offset:
        end = data.find(b"\0", pointer, offset)
        if end > pointer:
            try:
                path = data[pointer:end].decode("ascii")
            except UnicodeDecodeError:
                path = ""
    return {"magic": "QnDbTm", "offset": offset, "pathOffset": pointer, "path": path}


def mdl_rle_length(data: bytes, offset: int, frames: int) -> int:
    """Bytes of the RLE animation channel that begins at `offset` and covers `frames` frames."""

    start = offset
    remaining = frames
    while remaining > 0 and offset + 2 <= len(data):
        valid, total = data[offset], data[offset + 1]
        offset += 2
        if total <= 0:
            break
        offset = min(len(data), offset + valid * 2)
        remaining -= total
    return offset - start


def rle_exact_track_length(data: bytes, start: int, end: int, frames: int) -> int:
    """Bytes of one well-formed RLE track that covers exactly `frames` frames inside `[start,end)`."""

    if frames <= 0 or start < 0 or end > len(data) or start + 2 > end:
        return 0
    offset = start
    remaining = frames
    while remaining > 0:
        if offset + 2 > end:
            return 0
        valid, total = data[offset], data[offset + 1]
        if total <= 0 or valid > total:
            return 0
        consumed = 2 + valid * 2
        if offset + consumed > end:
            return 0
        offset += consumed
        remaining -= total
    return offset - start if remaining == 0 else 0


def _rle_span_length(data: bytes, start: int, end: int) -> int:
    """Bytes of a well-formed RLE stream that exactly fills `[start, end)`."""

    offset = start
    while offset + 2 <= end:
        valid, total = data[offset], data[offset + 1]
        if total <= 0 or valid > total:
            return 0
        consumed = 2 + valid * 2
        if offset + consumed > end:
            return 0
        offset += consumed
    return offset - start if offset == end else 0


def cover_mdl(path: str, data: bytes) -> dict[str, Any]:
    """Claim every declared v2531 MDL record, payload and referenced string."""

    if len(data) < 424 or data[:4] != b"IDST" or _i32(data, 4) != 2531:
        raise ModelByteCoverageError(f"{path}: not a complete MDL v2531 image")
    ledger = ModelLedger(path, data, padding_owner="mdl.padding")
    # The `char[128]` internal name is its own ledger owner, so the range and the field that pays
    # for it name the same place.
    ledger.claim(0, 12, "mapped", "mdl.header")
    ledger.claim(140, 424 - 140, "mapped", "mdl.header")
    terminator = data.find(b"\0", 12, 140)
    if terminator < 0:
        raise ModelByteCoverageError(f"{path}: the 128-byte header name has no terminator")
    ledger.claim(12, terminator + 1 - 12, "mapped-string", "mdl.header.name")
    residue = data[terminator + 1:140]
    if residue:
        ledger.claim(
            terminator + 1,
            len(residue),
            "padding-zero" if not any(residue) else "omitted-proven",
            "mdl.header.name.fill",
        )

    declared_length = _i32(data, 140)
    if not 424 <= declared_length <= len(data):
        raise ModelByteCoverageError(
            f"{path}: declared MDL image length {declared_length} outside {len(data)} bytes"
        )
    if declared_length < len(data):
        trailing = data[declared_length:]
        if any(byte not in b"\t\n\r " for byte in trailing):
            raise ModelByteCoverageError(
                f"{path}: non-whitespace data follows declared MDL image at {declared_length}"
            )
        # The unit publishes these bytes in `mdl.header.trailingPatchWhitespace`, so the range
        # they came from is `mapped` under that field's own name: one range, one grading.
        ledger.claim(
            declared_length,
            len(data) - declared_length,
            "mapped",
            "mdl.header.trailingPatchWhitespace",
        )

    def pair(count_at: int, offset_at: int) -> tuple[int, int]:
        return _i32(data, count_at), _i32(data, offset_at)

    def relative_string(owner: int, relative: int, label: str) -> None:
        if relative > 0:
            ledger.cstring(owner + relative, label)

    bone_count, bone_base = pair(240, 244)
    ledger.records(bone_base, bone_count, 160, "mdl.bones")
    axis_records = []
    for index in range(bone_count):
        bone = bone_base + index * 160
        relative_string(bone, _i32(data, bone), f"mdl.bones[{index}].name")
        relative_string(bone, _i32(data, bone + 152), f"mdl.bones[{index}].surfaceProperty")
        procedural_type, procedural_relative = _i32(data, bone + 140), _i32(data, bone + 144)
        if procedural_type == 1:
            axis_records.append(bone + procedural_relative)
            ledger.claim(bone + procedural_relative, 176, "mapped", f"mdl.procedural[{index}]")
        elif procedural_type != 0:
            raise ModelByteCoverageError(
                f"{path}: bone {index} has unsupported procedural type {procedural_type}"
            )
    if axis_records:
        first_axis = min(axis_records)
        bone_end = bone_base + bone_count * 160
        duplicate_length = first_axis - bone_end
        if (
            duplicate_length > 0
            and duplicate_length % 176 == 0
            and first_axis + duplicate_length <= len(data)
            and data[bone_end:first_axis] == data[first_axis:first_axis + duplicate_length]
        ):
            ledger.array(
                bone_end,
                duplicate_length // 176,
                176,
                "mdl.unindexedAxisInterpolationDuplicate",
                state="omitted-proven",
            )

    controller_count, controller_base = pair(248, 252)
    ledger.array(controller_base, controller_count, 24, "mdl.boneControllers")

    hitbox_count, hitbox_base = pair(256, 260)
    ledger.array(hitbox_base, hitbox_count, 12, "mdl.hitboxSets")
    for index in range(hitbox_count):
        record = hitbox_base + index * 12
        relative_string(record, _i32(data, record), f"mdl.hitboxSets[{index}].name")
        ledger.records(
            record + _i32(data, record + 8), _i32(data, record + 4), 32,
            f"mdl.hitboxSets[{index}].hitboxes",
        )

    animation_count, animation_base = pair(264, 268)
    ledger.array(animation_base, animation_count, 72, "mdl.localAnimations")
    for index in range(animation_count):
        descriptor = animation_base + index * 72
        relative_string(descriptor, _i32(data, descriptor), f"mdl.localAnimations[{index}].name")
        frames = _i32(data, descriptor + 12)
        ledger.array(
            descriptor + _i32(data, descriptor + 20),
            _i32(data, descriptor + 16),
            44,
            f"mdl.localAnimations[{index}].movement",
        )
        records = descriptor + _i32(data, descriptor + 48)
        ledger.array(records, bone_count, 32, f"mdl.localAnimations[{index}].boneRecords")
        for bone in range(bone_count):
            record = records + bone * 32
            for channel, relative in enumerate(struct.unpack_from("<7i", data, record + 4)):
                if not relative:
                    continue
                start = record + relative
                ledger.claim(
                    start,
                    mdl_rle_length(data, start, frames),
                    "mapped",
                    f"mdl.localAnimations[{index}].frames.bone[{bone}].channel[{channel}]",
                    allow_existing=True,
                )

    sequence_count, sequence_base = pair(272, 276)
    ledger.records(sequence_base, sequence_count, 764, "mdl.sequences")
    for index in range(sequence_count):
        descriptor = sequence_base + index * 764
        relative_string(descriptor, _i32(data, descriptor), f"mdl.sequences[{index}].label")
        relative_string(descriptor, _i32(data, descriptor + 4), f"mdl.sequences[{index}].activity")
        ledger.records(
            descriptor + _i32(data, descriptor + 24),
            _i32(data, descriptor + 20),
            76,
            f"mdl.sequences[{index}].events",
        )
        layer_count = _i32(data, descriptor + 660)
        if 0 < layer_count <= 16:
            ledger.records(
                descriptor + _i32(data, descriptor + 664),
                layer_count,
                4,
                f"mdl.sequences[{index}].autolayers",
            )
        envelope_count = _i32(data, descriptor + 700)
        if 0 < envelope_count <= 512:
            ledger.array(
                descriptor + _i32(data, descriptor + 704),
                envelope_count,
                24,
                f"mdl.sequences[{index}].envelopes",
            )
        swing_count = _i32(data, descriptor + 708)
        if 0 < swing_count <= 20:
            swing_base = descriptor + _i32(data, descriptor + 712)
            ledger.array(swing_base, swing_count, 188, f"mdl.sequences[{index}].swings")
            for swing in range(swing_count):
                record = swing_base + swing * 188
                for name in range(16):
                    relative_string(
                        record,
                        _i32(data, record + 0x78 + name * 4),
                        f"mdl.sequences[{index}].swings[{swing}].name[{name}]",
                    )
        for field_offset, label in ((732, "dodge"), (740, "blocked"), (744, "chain"),
                                    (748, "alternateChain")):
            relative_string(
                descriptor, _i32(data, descriptor + field_offset),
                f"mdl.sequences[{index}].{label}",
            )

    group_count, group_base = pair(284, 288)
    ledger.array(group_base, group_count, 16, "mdl.sequenceGroups")
    for index in range(group_count):
        record = group_base + index * 16
        relative_string(record, _i32(data, record), f"mdl.sequenceGroups[{index}].label")
        relative_string(record, _i32(data, record + 4), f"mdl.sequenceGroups[{index}].name")

    texture_count, texture_base = pair(292, 296)
    ledger.records(texture_base, texture_count, 20, "mdl.textures")
    for index in range(texture_count):
        record = texture_base + index * 20
        relative_string(record, _i32(data, record), f"mdl.textures[{index}].name")

    search_count, search_base = pair(300, 304)
    ledger.records(search_base, search_count, 4, "mdl.searchPaths")
    for index in range(search_count):
        ledger.cstring(_i32(data, search_base + index * 4), f"mdl.searchPaths[{index}].value")

    skin_references, skin_base = _i32(data, 308), _i32(data, 316)
    ledger.array(skin_base, skin_references * _i32(data, 312), 2, "mdl.skinTable")

    bodypart_count, bodypart_base = pair(320, 324)
    ledger.array(bodypart_base, bodypart_count, 16, "mdl.bodyParts")
    for bodypart_index in range(bodypart_count):
        bodypart = bodypart_base + bodypart_index * 16
        relative_string(bodypart, _i32(data, bodypart), f"mdl.bodyParts[{bodypart_index}].name")
        model_count = _i32(data, bodypart + 4)
        model_base = bodypart + _i32(data, bodypart + 12)
        ledger.array(
            model_base, model_count, mdl_skel.MODEL_STRIDE,
            f"mdl.bodyParts[{bodypart_index}].models",
        )
        for model_index in range(model_count):
            record = model_base + model_index * mdl_skel.MODEL_STRIDE
            tag = f"mdl.bodyParts[{bodypart_index}].models[{model_index}]"
            vertex_count = _i32(data, record + 144)
            vertex_type = _i32(data, record + 156)
            vertex_stride = mdl.VSTRIDE.get(vertex_type)
            if vertex_stride is None:
                raise ModelByteCoverageError(
                    f"{path}: {tag} has unknown vertex type {vertex_type}"
                )
            ledger.array(
                record + _i32(data, record + 148), vertex_count, vertex_stride, f"{tag}.vertices"
            )
            tangent_relative = _i32(data, record + 152)
            if tangent_relative:
                ledger.array(
                    record + tangent_relative, vertex_count, 16, f"{tag}.vertices.tangents"
                )
            mesh_count = _i32(data, record + 136)
            mesh_base = record + _i32(data, record + 140)
            ledger.records(mesh_base, mesh_count, mdl_skel.MESH_STRIDE, f"{tag}.meshes")
            for mesh_index in range(mesh_count):
                mesh = mesh_base + mesh_index * mdl_skel.MESH_STRIDE
                mesh_tag = f"{tag}.meshes[{mesh_index}]"
                flex_count = _i32(data, mesh + 16)
                flex_base = mesh + _i32(data, mesh + 20)
                ledger.array(flex_base, flex_count, 32, f"{mesh_tag}.flexes")
                for flex_index in range(flex_count):
                    flex = flex_base + flex_index * 32
                    kind = _i32(data, flex + 28)
                    stride = mdl_skel.VA_STRIDE.get(kind)
                    if stride is None:
                        raise ModelByteCoverageError(
                            f"{path}: {mesh_tag}.flexes[{flex_index}] has vertex-anim type {kind}"
                        )
                    ledger.array(
                        flex + _i32(data, flex + 24),
                        _i32(data, flex + 20),
                        stride,
                        f"{mesh_tag}.flexes[{flex_index}].vertAnims",
                    )
            eyeball_count = _i32(data, record + 192)
            eyeball_base = record + _i32(data, record + 196)
            ledger.array(eyeball_base, eyeball_count, 140, f"{tag}.eyeballs")
            for eyeball_index in range(eyeball_count):
                eyeball = eyeball_base + eyeball_index * 140
                relative_string(
                    eyeball, _i32(data, eyeball), f"{tag}.eyeballs[{eyeball_index}].name"
                )
            _cover_mdl_cloth(ledger, data, record, mesh_base, mesh_count, tag)

    attachment_count, attachment_base = pair(328, 332)
    ledger.records(attachment_base, attachment_count, 60, "mdl.attachments")
    for index in range(attachment_count):
        record = attachment_base + index * 60
        relative_string(record, _i32(data, record), f"mdl.attachments[{index}].name")

    transition_count, transition_base = pair(336, 340)
    ledger.array(transition_base, transition_count * transition_count, 1, "mdl.transitionGraph")

    flexdesc_count, flexdesc_base = pair(344, 348)
    ledger.array(flexdesc_base, flexdesc_count, 4, "facial.flexDescriptions")
    for index in range(flexdesc_count):
        record = flexdesc_base + index * 4
        relative_string(record, _i32(data, record), f"facial.flexDescriptions[{index}].name")

    controller_count, controller_base = pair(352, 356)
    ledger.array(controller_base, controller_count, 20, "facial.controllers")
    for index in range(controller_count):
        record = controller_base + index * 20
        relative_string(record, _i32(data, record), f"facial.controllers[{index}].type")
        relative_string(record, _i32(data, record + 4), f"facial.controllers[{index}].name")

    rule_count, rule_base = pair(360, 364)
    ledger.array(rule_base, rule_count, 12, "facial.rules")
    for index in range(rule_count):
        record = rule_base + index * 12
        ledger.array(
            record + _i32(data, record + 8), _i32(data, record + 4), 8,
            f"facial.rules[{index}].operations",
        )

    ik_count, ik_base = pair(368, 372)
    ledger.records(ik_base, ik_count, 16, "mdl.ikChains")
    for index in range(ik_count):
        record = ik_base + index * 16
        relative_string(record, _i32(data, record), f"mdl.ikChains[{index}].name")
        ledger.array(
            record + _i32(data, record + 12), _i32(data, record + 8), 28,
            f"mdl.ikChains[{index}].links",
        )

    mouth_count, mouth_base = pair(376, 380)
    ledger.array(mouth_base, mouth_count, 20, "facial.mouths")

    pose_count, pose_base = pair(384, 388)
    ledger.array(pose_base, pose_count, 20, "mdl.poseParameters")
    for index in range(pose_count):
        record = pose_base + index * 20
        relative_string(record, _i32(data, record), f"mdl.poseParameters[{index}].name")

    surface = _i32(data, 392)
    if surface:
        ledger.cstring(surface, "mdl.header.surfaceProperty")

    secondary_count, secondary_base = pair(396, 400)
    ledger.records(secondary_base, secondary_count, 28, "mdl.secondaryMotion")

    include_count, include_base = pair(404, 408)
    ledger.records(include_base, include_count, 116, "mdl.includeModels")
    for index in range(include_count):
        record = include_base + index * 116
        relative_string(record, _i32(data, record), f"mdl.includeModels[{index}].path")
        ledger.array(
            record + _i32(data, record + 16), bone_count, 56,
            f"mdl.includeModels[{index}].boneRemap",
        )

    _cover_retained_mdl_payloads(ledger, data, bone_count)
    return ledger.finish()


def _relative_cstring(data: bytes, owner: int, relative: int) -> str | None:
    if relative <= 0:
        return None
    offset = owner + relative
    if not 0 <= offset < len(data):
        return None
    end = data.find(b"\0", offset)
    if end <= offset:
        return None
    try:
        value = data[offset:end].decode("ascii")
    except UnicodeDecodeError:
        return None
    if not all(32 <= ord(character) < 127 for character in value):
        return None
    return value


def _looks_like_include_group(data: bytes, offset: int, bone_count: int) -> dict[str, Any] | None:
    if offset < 0 or offset + 116 > len(data) or bone_count <= 0:
        return None
    path = _relative_cstring(data, offset, _i32(data, offset))
    if path is None:
        return None
    lowered = path.replace("\\", "/").lower()
    if ".mdl" not in lowered and "models/" not in lowered:
        return None
    remap_relative = _i32(data, offset + 16)
    remap_base = offset + remap_relative
    remap_length = bone_count * 56
    if remap_relative <= 0 or remap_base < 0 or remap_base + remap_length > len(data):
        return None
    return {"offset": offset, "path": path, "remapBase": remap_base, "remapLength": remap_length}


def _claim_unindexed_include_group(
    ledger: ModelLedger, group: dict[str, Any], owner: str
) -> None:
    ledger.claim(group["offset"], 116, "omitted-proven", owner)
    relative = _i32(ledger.data, group["offset"])
    ledger.cstring(group["offset"] + relative, f"{owner}.path")
    if ledger.unclaimed(group["remapBase"], group["remapLength"]):
        ledger.array(
            group["remapBase"], group["remapLength"] // 56, 56, f"{owner}.boneRemap",
            state="omitted-proven",
        )


def _cover_unindexed_include_groups(ledger: ModelLedger, data: bytes, bone_count: int) -> None:
    include_count, include_base = _i32(data, 404), _i32(data, 408)
    cursor = include_base + max(include_count, 0) * 116
    if include_count == 0 and include_base > 0:
        cursor = include_base
    extra = 0
    while True:
        group = _looks_like_include_group(data, cursor, bone_count)
        if group is None or not ledger.unclaimed(cursor, 116):
            break
        _claim_unindexed_include_group(ledger, group, f"mdl.unindexedIncludeModel[{extra}]")
        extra += 1
        cursor += 116
    for start, _end, _previous, _following in list(ledger.gaps()):
        group = _looks_like_include_group(data, start, bone_count)
        if group is None or not ledger.unclaimed(start, 116):
            continue
        _claim_unindexed_include_group(ledger, group, f"mdl.unindexedIncludeModel@{start}")


def _looks_like_secondary_record(data: bytes, offset: int, bone_count: int) -> bool:
    if offset < 0 or offset + 28 > len(data) or bone_count <= 0:
        return False
    if not any(data[offset:offset + 28]):
        return False
    first, terminal = _i32(data, offset), _i32(data, offset + 4)
    if not 0 <= first < bone_count:
        return False
    if terminal < -1 or terminal >= bone_count:
        return False
    return all(math.isfinite(value) for value in struct.unpack_from("<5f", data, offset + 8))


def _cover_unindexed_secondary_motion(
    ledger: ModelLedger, data: bytes, bone_count: int
) -> None:
    count, base = _i32(data, 396), _i32(data, 400)
    cursor = base + max(count, 0) * 28
    extra = 0
    while ledger.unclaimed(cursor, 28) and _looks_like_secondary_record(data, cursor, bone_count):
        ledger.claim(cursor, 28, "omitted-proven", f"mdl.unindexedSecondaryMotion[{extra}]")
        extra += 1
        cursor += 28
    if count <= 0 or base <= 0:
        return
    indexed = [data[base + index * 28:base + (index + 1) * 28] for index in range(count)]
    for start, end, _previous, _following in list(ledger.gaps()):
        length = end - start
        if length < 28 or length % 28:
            continue
        records = length // 28
        payload = data[start:end]
        if not all(payload[row * 28:(row + 1) * 28] in indexed for row in range(records)):
            continue
        if ledger.unclaimed(start, length):
            ledger.array(
                start, records, 28, "mdl.unindexedSecondaryMotionDuplicate",
                state="omitted-proven",
            )


def _looks_like_texture_record(data: bytes, offset: int) -> bool:
    if offset < 0 or offset + 20 > len(data):
        return False
    name = _relative_cstring(data, offset, _i32(data, offset))
    if name is None or len(name) < 2:
        return False
    return all(
        math.isfinite(value) for value in struct.unpack_from("<3f", data, offset + 8)
    )


def _cover_duplicate_texture_table(ledger: ModelLedger, data: bytes) -> None:
    count, base = _i32(data, 292), _i32(data, 296)
    if count <= 0 or base < 20:
        return
    extra = 0
    cursor = base - 20
    while (
        extra < 32
        and cursor >= 0
        and ledger.unclaimed(cursor, 20)
        and _looks_like_texture_record(data, cursor)
    ):
        extra += 1
        cursor -= 20
    if extra:
        start = base - extra * 20
        ledger.array(
            start, extra, 20, "mdl.unindexedTextureDuplicate", state="omitted-proven"
        )
        for index in range(extra):
            record = start + index * 20
            relative = _i32(data, record)
            if relative > 0:
                name_at = record + relative
                if not start <= name_at < start + extra * 20:
                    ledger.cstring(name_at, f"mdl.unindexedTextureDuplicate[{index}].name")
    for start, end, _previous, _following in list(ledger.gaps()):
        length = end - start
        if length < 20 or length % 20 or length // 20 > 32:
            continue
        records = length // 20
        if not all(_looks_like_texture_record(data, start + row * 20) for row in range(records)):
            continue
        if ledger.unclaimed(start, length):
            ledger.array(
                start, records, 20, "mdl.unindexedTextureDuplicate", state="omitted-proven"
            )


def _animation_descriptor_at(data: bytes, offset: int) -> dict[str, Any] | None:
    if offset < 0 or offset + 72 > len(data):
        return None
    relative = _i32(data, offset)
    name_at = offset + relative
    if relative <= 0 or not 0 <= name_at < len(data):
        return None
    end = data.find(b"\0", name_at, min(len(data), name_at + 256))
    if end <= name_at:
        return None
    try:
        name = data[name_at:end].decode("ascii")
    except UnicodeDecodeError:
        return None
    if not all(32 <= ord(character) < 127 for character in name):
        return None
    fps = struct.unpack_from("<f", data, offset + 4)[0]
    frames = _i32(data, offset + 12)
    movements = _i32(data, offset + 16)
    if not math.isfinite(fps) or not 0.0 < fps <= 240.0:
        return None
    if not 0 < frames <= 1_000_000 or not 0 <= movements <= 1024:
        return None
    return {"offset": offset, "name": name, "frames": frames, "movements": movements}


def _looks_like_bone_records(data: bytes, offset: int, bone_count: int) -> bool:
    if bone_count <= 0 or offset < 0 or offset + bone_count * 32 > len(data):
        return False
    if not any(data[offset:offset + bone_count * 32]):
        return False
    has_channel = False
    for bone in range(bone_count):
        record = offset + bone * 32
        weight = struct.unpack_from("<f", data, record)[0]
        if not math.isfinite(weight) or weight < 0.0 or weight > 1.0001:
            return False
        for relative in struct.unpack_from("<7i", data, record + 4):
            if relative < 0 or (relative and record + relative >= len(data)):
                return False
            if relative:
                has_channel = True
    return has_channel


def _claim_animation_channels(
    ledger: ModelLedger, data: bytes, records: int, bone_count: int, frames: int, owner: str
) -> None:
    for bone in range(bone_count):
        record = records + bone * 32
        for channel, relative in enumerate(struct.unpack_from("<7i", data, record + 4)):
            if not relative:
                continue
            start = record + relative
            length = mdl_rle_length(data, start, frames)
            if ledger.unclaimed(start, length):
                ledger.claim(
                    start, length, "omitted-proven",
                    f"{owner}.bone[{bone}].channel[{channel}]",
                )


def _claim_unindexed_animation_descriptor(
    ledger: ModelLedger, data: bytes, descriptor: dict[str, Any], bone_count: int, owner: str
) -> None:
    offset = descriptor["offset"]
    if not ledger.unclaimed(offset, 72):
        return
    ledger.claim(offset, 72, "omitted-proven", owner)
    relative = _i32(data, offset)
    name_at = offset + relative
    if relative > 0 and not offset <= name_at < offset + 72:
        ledger.cstring(name_at, f"{owner}.name")
    movements = descriptor["movements"]
    movement_relative = _i32(data, offset + 20)
    if movements > 0 and movement_relative:
        movement_base = offset + movement_relative
        if ledger.unclaimed(movement_base, movements * 44):
            ledger.array(
                movement_base, movements, 44, f"{owner}.movements", state="omitted-proven"
            )
    animation_relative = _i32(data, offset + 48)
    records = offset + animation_relative
    if animation_relative and ledger.unclaimed(records, bone_count * 32):
        ledger.array(records, bone_count, 32, f"{owner}.boneRecords", state="omitted-proven")
        _claim_animation_channels(ledger, data, records, bone_count, descriptor["frames"], owner)


def _cover_donor_animation_payloads(ledger: ModelLedger, data: bytes, bone_count: int) -> None:
    extra = 0
    for start, end, _previous, _following in list(ledger.gaps()):
        offset = start
        while offset + 72 <= end:
            descriptor = _animation_descriptor_at(data, offset)
            if descriptor is not None and ledger.unclaimed(offset, 72):
                _claim_unindexed_animation_descriptor(
                    ledger, data, descriptor, bone_count, f"mdl.unindexedAnimation[{extra}]"
                )
                extra += 1
                offset += 72
                continue
            offset += 4
        if ledger.unclaimed(start, bone_count * 32) and _looks_like_bone_records(
            data, start, bone_count
        ):
            owner = f"mdl.unindexedAnimationBoneRecords@{start}"
            ledger.array(start, bone_count, 32, owner, state="omitted-proven")
            _claim_animation_channels(ledger, data, start, bone_count, 1, owner)
    for start, end, previous, following in list(ledger.gaps()):
        length = end - start
        if length <= 0 or not ledger.unclaimed(start, length):
            continue
        if "unindexedAnimation" not in previous or ".boneRecords" not in following:
            continue
        ledger.claim(start, length, "omitted-proven", f"{previous}.preBoneRecordPayload")
    for start, end, previous, following in list(ledger.gaps()):
        length = end - start
        if not 2 <= length <= 8 or not ledger.unclaimed(start, length):
            continue
        neighbours = previous + " " + following
        if "unindexedAnimation" not in neighbours and "mdl.sequences" not in neighbours:
            continue
        ledger.claim(
            start, length, "omitted-proven", f"mdl.unindexedAnimationAlignmentResidue@{start}"
        )
    for start, end, previous, following in list(ledger.gaps()):
        length = end - start
        if length <= 0 or not ledger.unclaimed(start, length):
            continue
        if not any(data[start:end]):
            continue
        if not (
            previous.startswith("mdl.unindexedAnimation")
            or following.startswith("mdl.unindexedAnimation")
        ):
            continue
        ledger.claim(start, length, "omitted-proven", f"{previous}.adjacentRetainedPayload")


def _animation_frame_count(data: bytes, index: int) -> int | None:
    if len(data) < 272 or data[:4] != b"IDST":
        return None
    count, base = _i32(data, 264), _i32(data, 268)
    if not 0 <= index < count or base < 0 or base + (index + 1) * 72 > len(data):
        return None
    frames = _i32(data, base + index * 72 + 12)
    return frames if 0 < frames <= 1_000_000 else None


def _cover_compiler_trailer(ledger: ModelLedger) -> None:
    trailer = compiler_trailer(ledger.data)
    if trailer is None:
        return
    if ledger.unclaimed(trailer["offset"], COMPILER_TRAILER_BYTES):
        ledger.claim(
            trailer["offset"], COMPILER_TRAILER_BYTES, "mapped", "mdl.compilerTrailerQnDbTm"
        )


_CHANNEL_OWNER = re.compile(
    r"mdl\.localAnimations\[(\d+)\]\.frames\.bone\[\d+\]\.channel\[\d+\]"
)


def _cover_extra_animation_tracks(ledger: ModelLedger) -> None:
    data = ledger.data
    for start, end, previous, _following in list(ledger.gaps()):
        match = _CHANNEL_OWNER.fullmatch(previous)
        if match is None or not ledger.unclaimed(start, end - start):
            continue
        frames = _animation_frame_count(data, int(match.group(1)))
        if frames is None:
            continue
        offset = start
        extra = 0
        while offset < end:
            length = rle_exact_track_length(data, offset, end, frames)
            if length <= 0:
                break
            ledger.claim(offset, length, "mapped", f"{previous}.unindexedTrack[{extra}]")
            extra += 1
            offset += length
        leftover = end - offset
        if leftover <= 0 or not ledger.unclaimed(offset, leftover):
            continue
        if leftover == 4 and data[offset:end] == b"ULDD":
            ledger.claim(offset, 4, "omitted-proven", f"{previous}.compilerPackingULDD")


def _cover_orphan_rle_samples(ledger: ModelLedger) -> None:
    for start, end, previous, following in list(ledger.gaps()):
        length = end - start
        if length < 4 or not ledger.unclaimed(start, length):
            continue
        neighbours = previous + " " + following
        if (
            "channel" not in neighbours
            and "boneRecords" not in neighbours
            and "unindexedAnimation" not in neighbours
        ):
            continue
        if _rle_span_length(ledger.data, start, end) != length:
            continue
        ledger.claim(start, length, "omitted-proven", f"mdl.orphanAnimationSample@{start}")
    _cover_extra_animation_tracks(ledger)
    for start, end, previous, following in list(ledger.gaps()):
        length = end - start
        if length <= 0 or not ledger.unclaimed(start, length):
            continue
        if not any(ledger.data[start:end]):
            continue
        if "channel" in previous and (
            following == "<end>" or following.startswith("mdl.searchPaths")
        ):
            ledger.claim(start, length, "omitted-proven", f"{previous}.trailingPayload")


def _looks_like_studio_mesh(data: bytes, offset: int) -> bool:
    if offset < 0 or offset + mdl_skel.MESH_STRIDE > len(data):
        return False
    if not any(data[offset:offset + mdl_skel.MESH_STRIDE]):
        return False
    material = _i32(data, offset)
    vertex_count = _i32(data, offset + 8)
    flex_count = _i32(data, offset + 16)
    if not -1 <= material <= 4096:
        return False
    return 1 <= vertex_count <= 20000 and 0 <= flex_count <= 256


def _cover_unindexed_meshes(ledger: ModelLedger, data: bytes) -> None:
    for start, end, _previous, _following in list(ledger.gaps()):
        if end - start < mdl_skel.MESH_STRIDE:
            continue
        if not _looks_like_studio_mesh(data, start):
            continue
        if not ledger.unclaimed(start, mdl_skel.MESH_STRIDE):
            continue
        vertex_count = _i32(data, start + 8)
        flex_count = _i32(data, start + 16)
        owner = f"mdl.unindexedMesh@{start}"
        ledger.claim(start, mdl_skel.MESH_STRIDE, "omitted-proven", owner)
        cursor = start + mdl_skel.MESH_STRIDE
        for stride, name in ((44, "vertices"), (16, "tangents")):
            length = vertex_count * stride
            if cursor + length <= end and ledger.unclaimed(cursor, length):
                ledger.array(
                    cursor, vertex_count, stride, f"{owner}.{name}", state="omitted-proven"
                )
                cursor += length
        flex_length = flex_count * 32
        if flex_count and cursor + flex_length <= end and ledger.unclaimed(cursor, flex_length):
            ledger.array(cursor, flex_count, 32, f"{owner}.flexes", state="omitted-proven")
            cursor += flex_length
        leftover = end - cursor
        if vertex_count >= 300 and leftover > 0 and any(data[cursor:end]):
            if ledger.unclaimed(cursor, leftover):
                ledger.claim(cursor, leftover, "omitted-proven", f"{owner}.retainedPayload")


_STRING_TOKENS = ("models", "materials", "anim", "idle", "bone", ".mdl", ".vmt")


def _cover_unreferenced_strings(ledger: ModelLedger, data: bytes) -> None:
    for start, end, _previous, _following in list(ledger.gaps()):
        if end - start > 512:
            continue
        cursor = start
        while cursor < end and ledger.unclaimed(cursor, 1):
            if data[cursor] == 0:
                # Alignment fill between the previous table and the first string. It is left for
                # `finish` to claim as verified padding rather than ending the walk.
                cursor += 1
                continue
            terminator = data.find(b"\0", cursor, end)
            if terminator < 0:
                break
            try:
                text = data[cursor:terminator].decode("ascii")
            except UnicodeDecodeError:
                break
            if len(text) < 3 or not all(32 <= ord(character) < 127 for character in text):
                break
            lowered = text.lower()
            named = (
                "/" in text
                or "\\" in text
                or any(token in lowered for token in _STRING_TOKENS)
                or (len(text) >= 4 and text.replace("_", "").isalnum())
            )
            if not named:
                break
            ledger.claim(
                cursor, terminator + 1 - cursor, "omitted-proven",
                f"mdl.unreferencedString@{cursor}",
            )
            cursor = terminator + 1


def _cover_retained_mdl_payloads(ledger: ModelLedger, data: bytes, bone_count: int) -> None:
    """Classify the compiler-retained donor and cache bytes the declared tables do not index."""

    _cover_compiler_trailer(ledger)
    if len(data) >= 412 and data[:4] == b"IDST":
        _cover_unindexed_include_groups(ledger, data, bone_count)
        _cover_unindexed_secondary_motion(ledger, data, bone_count)
        _cover_duplicate_texture_table(ledger, data)
    _cover_donor_animation_payloads(ledger, data, bone_count)
    _cover_orphan_rle_samples(ledger)
    _cover_unindexed_meshes(ledger, data)
    _cover_unreferenced_strings(ledger, data)
    for start, end, previous, following in list(ledger.gaps()):
        length = end - start
        if length <= 0 or not ledger.unclaimed(start, length) or not any(data[start:end]):
            continue
        # A body-part model whose declared `vertexindex`/`tangentindex` skip a block inside its
        # own extent: the compiler retained a donor pool no runtime accessor addresses.
        mesh_owner = _MDL_MESH_RECORD.search(previous)
        if mesh_owner is not None and ".vertices" in following:
            ledger.claim(
                start, length, "omitted-proven",
                f"{previous[: mesh_owner.start()]}.retainedVertexPayload",
            )
    for start, end, previous, following in list(ledger.gaps()):
        length = end - start
        if length <= 0 or length > 64 or not ledger.unclaimed(start, length):
            continue
        if not any(data[start:end]):
            continue
        neighbours = previous + " " + following
        if "mdl.searchPaths" not in neighbours and "mdl.skinTable" not in neighbours:
            continue
        ledger.claim(start, length, "omitted-proven", f"{previous}.unindexedSearchOrSkinResidue")
    for start, end, previous, following in list(ledger.gaps()):
        length = end - start
        if length <= 0 or length > 32 or not ledger.unclaimed(start, length):
            continue
        if ".name" not in previous or ".name" not in following:
            continue
        if any(ledger.data[start:end]):
            ledger.claim(
                start, length, "omitted-proven", f"mdl.unreferencedStringResidue@{start}"
            )
    string_owners = (".name", ".path", ".label", ".value")
    for start, end, previous, following in list(ledger.gaps()):
        length = end - start
        if length <= 0 or not ledger.unclaimed(start, length):
            continue
        if not any(data[start:end]):
            continue
        if following == "<end>" and length <= 16 and any(
            token in previous for token in string_owners
        ):
            ledger.claim(start, length, "omitted-proven", f"{previous}.trailingCompilerResidue")
            continue
        if (
            length <= 8
            and any(token in previous for token in string_owners)
            and any(token in following for token in string_owners)
        ):
            ledger.claim(start, length, "omitted-proven", f"mdl.stringAlignmentResidue@{start}")
            continue
        if "cloth.selectors" in previous and "cloth.positions" in following:
            if all(byte == 0xFF for byte in data[start:end]):
                ledger.claim(start, length, "omitted-proven", f"{previous}.unselectedSuffix")


#: `(offsetField, countFields, stride, name)` for every payload a cloth definition addresses.
_CLOTH_BLOCKS = (
    (0x10, (0x04,), 2, "particleVertices"),
    (0x20, (0x18, 0x1C), 16, "constraints"),
    (0x2C, (0x24, 0x28), 1, "packedSimdPayload"),
    (0x34, (0x30,), 6, "collisionTriangles"),
    (0x40, (0x38,), 4, "edgePairs"),
    (0x48, (0x44,), 10, "normalContributions"),
    (0x50, (0x4C,), 12, "tangentInterpolation"),
    (0x54, (0x04,), 16, "optionalSeedA"),
    (0x58, (0x04,), 16, "optionalSeedB"),
)


def _cover_mdl_cloth(
    ledger: ModelLedger, data: bytes, model: int, mesh_base: int, mesh_count: int, tag: str
) -> None:
    _table, lod_rows, definition_count, records = mdl_cloth.definition_table(data, model)
    capsule_count = _i32(data, model + 208)
    sphere_count = _i32(data, model + 216)
    if capsule_count:
        ledger.array(model + _i32(data, model + 212), capsule_count, 36, f"{tag}.cloth.capsules")
    if sphere_count:
        ledger.array(model + _i32(data, model + 220), sphere_count, 20, f"{tag}.cloth.spheres")
    if definition_count:
        ledger.array(
            model + _i32(data, model + 204), lod_rows * definition_count, 4,
            f"{tag}.cloth.definitionOffsets",
        )
        for index, record in enumerate(records):
            owner = f"{tag}.cloth[{index}]"
            ledger.claim(record, 92, "mapped", owner)
            for offset_field, count_fields, stride, name in _CLOTH_BLOCKS:
                relative = _i32(data, record + offset_field)
                count = sum(_i32(data, record + field) for field in count_fields)
                if not (relative and count):
                    continue
                ledger.array(record + relative, count, stride, f"{owner}.{name}")
                if name == "packedSimdPayload":
                    # One lane-count byte per SIMD block, rounded up to four; the pad carries
                    # uninitialised compiler bytes rather than zeros, so it is residue.
                    residue = -count % 4
                    if residue:
                        ledger.claim(
                            record + relative + count, residue, "omitted-proven",
                            f"{owner}.{name}.alignmentResidue",
                        )
            triangle_count = _i32(data, record + 0x30)
            triangle_relative = _i32(data, record + 0x34)
            edge_relative = _i32(data, record + 0x40)
            if triangle_count > 0 and triangle_relative > 0 and edge_relative > 0:
                triangle_end = record + triangle_relative + triangle_count * 6
                if record + edge_relative - triangle_end == 2:
                    ledger.claim(
                        triangle_end, 2, "omitted-proven", f"{owner}.triangleAlignmentResidue"
                    )
    for mesh_index in range(mesh_count):
        mesh = mesh_base + mesh_index * mdl_skel.MESH_STRIDE
        try:
            layout = mdl_cloth.map_layout(data, mesh)
        except ValueError as error:
            raise ModelByteCoverageError(f"{ledger.source_path}: {error}") from error
        if layout is None:
            continue
        mesh_tag = f"{tag}.meshes[{mesh_index}].cloth"
        selector_count = layout["rows"] * layout["vertex_count"]
        ledger.array(layout["selector_base"], selector_count, 1, f"{mesh_tag}.selectors")
        if layout["selector_align"]:
            ledger.claim(
                layout["selector_base"] + selector_count, layout["selector_align"],
                "omitted-proven", f"{mesh_tag}.selectors.alignment",
            )
        position_count = layout["position_rows"] * layout["vertex_count"]
        ledger.array(layout["position_base"], position_count, 2, f"{mesh_tag}.positions")
        if layout["position_align"]:
            ledger.claim(
                layout["position_base"] + position_count * 2, layout["position_align"],
                "omitted-proven", f"{mesh_tag}.positions.alignment",
            )
        count = selector_count
        start = layout["tangent_base"]
        expected_end = start + count * 2
        secondary_count, secondary_base = _i32(data, 396), _i32(data, 400)
        if secondary_count > 0 and start < secondary_base < expected_end:
            available = secondary_base - start
            if available % 2:
                raise ModelByteCoverageError(
                    f"{ledger.source_path}: {mesh_tag} truncated tangent payload is not "
                    "uint16-aligned"
                )
            count = available // 2
            if any(
                value != 0xFF
                for value in data[
                    layout["selector_base"] + count:layout["selector_base"] + selector_count
                ]
            ):
                raise ModelByteCoverageError(
                    f"{ledger.source_path}: {mesh_tag} tangent payload omits a selected vertex"
                )
        ledger.array(start, count, 2, f"{mesh_tag}.tangents")


def cover_vtx(
    path: str,
    data: bytes,
    *,
    variant: str,
    published_lods: frozenset[int] | None = None,
) -> dict[str, Any]:
    """Claim the complete VtMB compact VTX hierarchy and its replacement strings.

    `published_lods` is the set of LOD indices this variant's own topology reaches the product
    through. `None` means every LOD does, which is the primary variant and any model that ships
    one variant alone. The legacy twin of a model that ships both publishes only the LODs the
    primary lacks: its remaining topology is the same triangles read a second time, so those bytes
    are `omitted-proven` rather than `mapped`, which is what `seam_map_model.md` § Source closure
    asks for and what keeps `mapped` meaning "this range reached the product".
    """

    if len(data) < 36:
        raise ModelByteCoverageError(f"{path}: VTX is only {len(data)} bytes")
    ledger = ModelLedger(path, data, padding_owner="vtx.padding")
    ledger.claim(0, 36, "mapped", f"vtx.{variant}.header")
    lod_count = _i32(data, 20)
    replacement_base = _i32(data, 24)
    ledger.array(replacement_base, lod_count, 8, f"vtx.{variant}.materialReplacements")
    for lod in range(lod_count):
        record = replacement_base + lod * 8
        count, relative = struct.unpack_from("<2i", data, record)
        base = record + relative
        ledger.array(base, count, 6, f"vtx.{variant}.materialReplacements[{lod}].entries")
        for index in range(count):
            replacement = base + index * 6
            ledger.cstring(
                replacement + _i32(data, replacement + 2),
                f"vtx.{variant}.materialReplacements[{lod}].entries[{index}].name",
            )

    # The published records first, so a table two LODs share is owned by the LOD that reached the
    # product; the superseded pass then claims only what is left.
    _cover_vtx_topology(ledger, data, variant, published_lods, "mapped")
    if published_lods is not None:
        _cover_vtx_topology(ledger, data, variant, published_lods, "omitted-proven")
    _cover_unindexed_vtx_records(ledger, data, variant)
    return ledger.finish()


def _lod_state(published: frozenset[int] | None, lods: Iterable[int]) -> str:
    """`mapped` when this record is on the path to a LOD the unit publishes from this variant."""

    if published is None:
        return "mapped"
    return "mapped" if any(index in published for index in lods) else "omitted-proven"


def _cover_vtx_topology(
    ledger: ModelLedger,
    data: bytes,
    variant: str,
    published: frozenset[int] | None,
    pass_state: str,
) -> None:
    """Claim the body-part/model/LOD/mesh hierarchy whose grading is `pass_state`."""

    def take(offset: int, count: int, stride: int, owner: str, state: str) -> None:
        if state != pass_state or count <= 0:
            return
        if count and offset <= 0:
            raise ModelByteCoverageError(
                f"{ledger.source_path}: {owner} has {count} records at invalid offset {offset}"
            )
        if state == "mapped":
            ledger.array(offset, count, stride, owner, allow_existing=True)
        else:
            ledger.claim_free(offset, count * stride, state, owner)

    bodypart_count, bodypart_base = _i32(data, 28), _i32(data, 32)
    for bodypart_index in range(bodypart_count):
        bodypart = bodypart_base + bodypart_index * 8
        model_count, model_relative = struct.unpack_from("<2i", data, bodypart)
        model_base = bodypart + model_relative
        bodypart_tag = f"vtx.{variant}.bodyParts[{bodypart_index}]"
        part_lods: set[int] = set()
        for model_index in range(model_count):
            part_lods.update(range(_i32(data, model_base + model_index * 8)))
        take(bodypart, 1, 8, bodypart_tag, _lod_state(published, part_lods))
        for model_index in range(model_count):
            model = model_base + model_index * 8
            model_tag = f"{bodypart_tag}.models[{model_index}]"
            model_lods, lod_relative = struct.unpack_from("<2i", data, model)
            take(model, 1, 8, model_tag, _lod_state(published, range(model_lods)))
            lod_base = model + lod_relative
            for lod_index in range(model_lods):
                state = _lod_state(published, (lod_index,))
                lod = lod_base + lod_index * 12
                lod_tag = f"{model_tag}.lods[{lod_index}]"
                take(lod, 1, 12, lod_tag, state)
                if state != pass_state:
                    continue
                mesh_count, mesh_relative = struct.unpack_from("<2i", data, lod)
                mesh_base = lod + mesh_relative
                take(mesh_base, mesh_count, 8, f"{lod_tag}.meshes", state)
                for mesh_index in range(mesh_count):
                    mesh = mesh_base + mesh_index * 8
                    group_count = _u16(data, mesh)
                    group_base = mesh + _i32(data, mesh + 4)
                    mesh_tag = f"{lod_tag}.meshes[{mesh_index}]"
                    take(
                        group_base, group_count, mdl.STRIPGROUP_STRIDE,
                        f"{mesh_tag}.stripGroups", state,
                    )
                    for group_index in range(group_count):
                        group = group_base + group_index * mdl.STRIPGROUP_STRIDE
                        group_tag = f"{mesh_tag}.stripGroups[{group_index}]"
                        vertex_count = _u16(data, group)
                        index_count = _u16(data, group + 2)
                        strip_count = _u16(data, group + 4)
                        vertex_stride, _offset = mdl._vtable_form(data, group)
                        take(
                            group + _i32(data, group + 8), vertex_count, vertex_stride,
                            f"{group_tag}.vertices", state,
                        )
                        take(
                            group + _i32(data, group + 12), index_count, 2,
                            f"{group_tag}.indices", state,
                        )
                        strip_base = group + _i32(data, group + 16)
                        take(strip_base, strip_count, 16, f"{group_tag}.strips", state)
                        for strip_index in range(strip_count):
                            strip = strip_base + strip_index * 16
                            take(
                                strip + _i32(data, strip + 12), _u16(data, strip + 10), 4,
                                f"{group_tag}.strips[{strip_index}].boneStateChanges", state,
                            )


def _cover_unindexed_vtx_records(ledger: ModelLedger, data: bytes, variant: str) -> None:
    for start, end, previous, following in list(ledger.gaps()):
        length = end - start
        if length <= 0 or not ledger.unclaimed(start, length) or not any(data[start:end]):
            continue
        for stride, before, after, name in (
            (12, "lods", "meshes", "unindexedLods"),
            (8, "meshes", "stripGroups", "unindexedMeshes"),
            (mdl.STRIPGROUP_STRIDE, "stripGroups", "strips", "unindexedStripGroups"),
            (16, "strips", "vertices", "unindexedStrips"),
        ):
            if length % stride == 0 and before in previous and after in following:
                ledger.array(
                    start, length // stride, stride, f"{previous}.{name}", state="omitted-proven"
                )
                break
        else:
            if length % 2 == 0 and "indices" in previous:
                ledger.array(
                    start, length // 2, 2, f"{previous}.unindexedIndices", state="omitted-proven"
                )
                continue
            ledger.claim(
                start, length, "omitted-proven", f"vtx.{variant}.unindexedPayload@{start}"
            )


def cover_phy(path: str, data: bytes) -> dict[str, Any]:
    """Claim the legacy VPhysics header, every solid's ledge tree, and the KeyValues tail."""

    if len(data) < 16:
        raise ModelByteCoverageError(f"{path}: PHY is only {len(data)} bytes")
    ledger = ModelLedger(path, data, padding_owner="phy.padding")
    header_size, _ident, solid_count, _checksum = struct.unpack_from("<4i", data)
    ledger.claim(0, 16, "mapped", "phy.header")
    if header_size > 16:
        ledger.claim(16, header_size - 16, "reserved-zero", "phy.header.extension")
    position = header_size
    for solid_index in range(solid_count):
        solid_size = _i32(data, position)
        body = position + 4
        tag = f"phy.solids[{solid_index}]"
        ledger.claim(position, 4, "mapped", f"{tag}.size")
        ledger.claim(body, 48, "mapped", f"{tag}.header")
        root = _i32(data, body + 32)
        ledges: list[int] = []
        _cover_phy_node(ledger, data, body + root, tag, set(), ledges)
        # Ledges of one solid share a point cloud: each names the same base and reaches a
        # different highest index. The extent is the maximum over the ledges that address it, so
        # the array is claimed once for the whole set rather than once per ledge.
        point_extents: dict[int, int] = {}
        for ledge_index, ledge in enumerate(ledges):
            highest = _cover_phy_ledge(ledger, data, ledge, f"{tag}.ledges[{ledge_index}]")
            if highest < 0:
                continue
            base = ledge + _i32(data, ledge)
            point_extents[base] = max(point_extents.get(base, -1), highest)
        for base, highest in sorted(point_extents.items()):
            ledger.array(base, highest + 1, 16, f"{tag}.points@{base}")
        end = body + solid_size
        for start, gap_end, _previous, _following in list(ledger.gaps()):
            if gap_end <= body or start >= end:
                continue
            start, gap_end = max(start, body), min(gap_end, end)
            if any(data[start:gap_end]):
                raise ModelByteCoverageError(
                    f"{path}: {tag} has unclaimed non-zero bytes at {start}+{gap_end - start}"
                )
            ledger.claim(start, gap_end - start, "padding-zero", f"{tag}.zeroGap")
        position = end
    try:
        data[position:].decode("ascii", "strict")
    except UnicodeDecodeError as error:
        raise ModelByteCoverageError(
            f"{path}: the PHY KeyValues tail is not losslessly decodable ASCII"
        ) from error
    ledger.claim(position, len(data) - position, "mapped-text", "phy.keyValues")
    return ledger.finish()


def _cover_phy_node(
    ledger: ModelLedger, data: bytes, node: int, tag: str, visited: set[int], ledges: list[int]
) -> None:
    if node in visited:
        raise ModelByteCoverageError(f"{ledger.source_path}: {tag} revisits node {node}")
    visited.add(node)
    ledger.claim(node, 28, "mapped", f"{tag}.ledgeTree.node@{node}")
    right, ledge = struct.unpack_from("<2i", data, node)
    if right == 0:
        ledges.append(node + ledge)
        return
    _cover_phy_node(ledger, data, node + 28, tag, visited, ledges)
    _cover_phy_node(ledger, data, node + right, tag, visited, ledges)


def _cover_phy_ledge(ledger: ModelLedger, data: bytes, offset: int, tag: str) -> int:
    """Claim one ledge header and its triangles; answer the highest point index it reaches."""

    _point_offset, _node_offset, _packed, triangle_count, _padding = struct.unpack_from(
        "<iiIhh", data, offset
    )
    ledger.claim(offset, 16, "mapped", f"{tag}.header", allow_existing=True)
    triangles = offset + 16
    ledger.array(triangles, triangle_count, 16, f"{tag}.triangles", allow_existing=True)
    highest = -1
    for triangle in range(triangle_count):
        record = triangles + triangle * 16
        for edge in range(3):
            highest = max(highest, _i32(data, record + 4 + edge * 4) & 0xFFFF)
    return highest


def cover_member(member, *, published_lods: frozenset[int] | None = None) -> dict[str, Any]:
    """The ledger row for one closure member, by the role it fills.

    `published_lods` names the LOD indices a VTX member's topology reaches the product through;
    `None` says every LOD does. It is the only thing about a member the walk cannot read from the
    member itself, because which twin publishes a shared LOD is the unit's choice.
    """

    if member.role == "mdl":
        return cover_mdl(member.path, member.data)
    if member.role.startswith("vtx-"):
        return cover_vtx(
            member.path, member.data, variant=member.role, published_lods=published_lods
        )
    if member.role == "phy":
        return cover_phy(member.path, member.data)
    raise ModelByteCoverageError(f"{member.path}: no byte-ledger walker for role {member.role}")


def cover_closure(
    members: Iterable[Any], *, published_lods: Mapping[str, frozenset[int]] | None = None
) -> list[dict[str, Any]]:
    """One ledger row per member, keyed for the VTX pair by source path."""

    published = published_lods or {}
    return [
        cover_member(member, published_lods=published.get(member.path))
        for member in members
    ]


#: Why each `omitted-proven` owner family contributes no payload byte. The unit contract requires
#: every `omitted-proven` range to carry its reason in `omissions` or `coverage.omittedProven`, so
#: the owner a claim site chose is joined here to the evidence that justified the claim. An owner
#: no pattern names is a classification the seam never made, and it aborts publication.
OMISSION_REASONS: tuple[tuple[str, str], ...] = (
    (r"^mdl\.header\.name\.fill$", "non-zero-fill-past-header-name-terminator"),
    (
        r"^mdl\.unindexedAxisInterpolationDuplicate$",
        "duplicate-axis-interpolation-table-no-bone-addresses",
    ),
    (
        r"^mdl\.unindexedIncludeModel(\[\d+\]|@\d+)(\.path|\.boneRemap)?$",
        "include-group-past-declared-count-no-header-offset-addresses",
    ),
    (
        r"^mdl\.unindexedSecondaryMotion\[\d+\]$",
        "secondary-motion-record-past-declared-count",
    ),
    (
        r"^mdl\.unindexedSecondaryMotionDuplicate$",
        "byte-identical-copy-of-the-declared-secondary-motion-records",
    ),
    (
        r"^mdl\.unindexedTextureDuplicate(\[\d+\]\.name)?$",
        "duplicate-texture-table-no-header-offset-addresses",
    ),
    (
        r"^mdl\.unindexedAnimation\[\d+\](\..+)?$",
        "donor-animation-descriptor-no-declared-index-addresses",
    ),
    (
        r"^mdl\.unindexedAnimationBoneRecords@\d+(\..+)?$",
        "animation-bone-records-no-declared-descriptor-addresses",
    ),
    (
        r"^mdl\.unindexedAnimationAlignmentResidue@\d+$",
        "compiler-alignment-fill-between-animation-payloads",
    ),
    (r"\.preBoneRecordPayload$", "retained-payload-between-donor-descriptor-and-its-records"),
    (r"\.adjacentRetainedPayload$", "retained-payload-adjacent-to-a-donor-animation"),
    (r"^mdl\.orphanAnimationSample@\d+$", "well-formed-rle-run-no-bone-record-addresses"),
    (r"\.compilerPackingULDD$", "compiler-packing-marker-after-the-declared-channels"),
    (r"\.trailingPayload$", "retained-animation-payload-after-the-last-declared-channel"),
    (r"^mdl\.unindexedMesh@\d+(\..+)?$", "retained-mesh-header-and-pool-no-model-record-addresses"),
    (r"\.retainedVertexPayload$", "donor-vertex-pool-inside-the-model-extent-no-accessor-reaches"),
    (r"^mdl\.unreferencedString@\d+$", "ascii-string-no-declared-offset-addresses"),
    (r"^mdl\.unreferencedStringResidue@\d+$", "non-zero-fill-between-two-declared-strings"),
    (r"^mdl\.stringAlignmentResidue@\d+$", "non-zero-alignment-fill-between-declared-strings"),
    (r"\.trailingCompilerResidue$", "non-zero-compiler-residue-at-the-end-of-the-image"),
    (r"\.unindexedSearchOrSkinResidue$", "retained-search-path-or-skin-table-residue"),
    (r"\.unselectedSuffix$", "cloth-selector-suffix-that-selects-no-vertex"),
    (r"\.packedSimdPayload\.alignmentResidue$", "simd-lane-count-pad-the-compiler-left-uninitialised"),
    (r"\.triangleAlignmentResidue$", "cloth-triangle-table-alignment-fill"),
    (r"\.selectors\.alignment$", "cloth-selector-table-alignment-fill"),
    (r"\.positions\.alignment$", "cloth-position-table-alignment-fill"),
    (r"\.unindexedLods$", "vtx-lod-records-no-model-record-addresses"),
    (r"\.unindexedMeshes$", "vtx-mesh-records-no-lod-record-addresses"),
    (r"\.unindexedStripGroups$", "vtx-strip-group-records-no-mesh-record-addresses"),
    (r"\.unindexedStrips$", "vtx-strip-records-no-strip-group-addresses"),
    (r"\.unindexedIndices$", "vtx-index-payload-no-strip-group-addresses"),
    (r"^vtx\..+\.unindexedPayload@\d+$", "vtx-payload-no-declared-record-addresses"),
    (
        r"^vtx\.vtx-dx7-2bone\.bodyParts(\[|$)",
        "legacy-vtx-equivalent-lod-the-primary-variant-publishes",
    ),
)

_OMISSION_PATTERNS = tuple((re.compile(pattern), reason) for pattern, reason in OMISSION_REASONS)


def omission_reason(owner: str) -> str:
    """The evidence an `omitted-proven` range's owner stands for."""

    for pattern, reason in _OMISSION_PATTERNS:
        if pattern.search(owner):
            return reason
    raise ModelByteCoverageError(
        f"omitted-proven range owner {owner!r} carries no recorded reason"
    )


def omitted_proven_rows(ledger_rows: Iterable[dict[str, Any]]) -> list[dict[str, Any]]:
    """One `coverage.omittedProven` row per distinct `omitted-proven` owner in the ledger.

    The row names the owner and the member it belongs to; the bytes themselves are stated once, by
    the ledger range the row joins to.
    """

    rows: list[dict[str, Any]] = []
    seen: set[tuple[str, str]] = set()
    for ledger_row in ledger_rows:
        source_path = str(ledger_row["sourcePath"])
        for entry in ledger_row["ranges"]:
            if entry["state"] != "omitted-proven":
                continue
            owner = str(entry["owner"])
            if (source_path, owner) in seen:
                continue
            seen.add((source_path, owner))
            rows.append(
                {
                    "path": owner,
                    "sourcePath": source_path,
                    "reason": omission_reason(owner),
                }
            )
    return rows
