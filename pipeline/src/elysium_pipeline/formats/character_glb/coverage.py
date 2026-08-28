"""Strict byte-accountability ledgers for Character GLB source members.

The exporter never embeds opaque copies of game files.  Instead, every admitted
source byte must belong to a decoded record/payload/string, an evidence-backed
omission, or a verified zero gap.  Any non-zero byte outside the declared
format walk aborts publication.
"""

from __future__ import annotations

from collections import Counter
import hashlib
import json
import math
import re
import struct
from typing import Any

from elysium_pipeline.formats import mdl, mdl_cloth, mdl_skel


class CharacterByteCoverageError(ValueError):
    """A source byte is unclaimed, multiply owned, or fails its verification."""


_ALLOWED_STATES = {
    "mapped",
    "mapped-string",
    "mapped-text",
    "derived",
    "omitted-proven",
    "padding-zero",
    "reserved-zero",
}


class ByteLedger:
    """One non-overlapping ownership map over a source member."""

    def __init__(self, path: str, data: bytes):
        self.path = path
        self.data = data
        self._owners = [-1] * len(data)
        self._ranges: list[dict[str, Any]] = []

    def claim(
        self,
        offset: int,
        length: int,
        state: str,
        owner: str,
        *,
        allow_existing: bool = False,
    ) -> None:
        if state not in _ALLOWED_STATES:
            raise CharacterByteCoverageError(
                f"{self.path}: invalid coverage state {state!r} for {owner}"
            )
        if offset < 0 or length < 0 or offset + length > len(self.data):
            raise CharacterByteCoverageError(
                f"{self.path}: {owner} range {offset}+{length} exceeds "
                f"{len(self.data)} bytes"
            )
        if length == 0:
            return
        occupied = [index for index in self._owners[offset:offset + length] if index >= 0]
        if occupied:
            if allow_existing and len(occupied) == length:
                existing = {self._ranges[index]["state"] for index in occupied}
                if existing == {state}:
                    return
            first = self._ranges[occupied[0]]
            raise CharacterByteCoverageError(
                f"{self.path}: {owner} range {offset}+{length} overlaps "
                f"{first['owner']} at {first['offset']}+{first['length']}"
            )
        if state.endswith("-zero") and any(self.data[offset:offset + length]):
            sample = self.data[offset:offset + min(length, 16)].hex()
            raise CharacterByteCoverageError(
                f"{self.path}: {owner} expected zero bytes at {offset}+{length}; "
                f"starts {sample}"
            )
        index = len(self._ranges)
        row = {"offset": offset, "length": length, "state": state, "owner": owner}
        self._ranges.append(row)
        self._owners[offset:offset + length] = [index] * length

    def array(self, offset: int, count: int, stride: int, owner: str,
              *, state: str = "mapped", allow_existing: bool = False) -> None:
        if count < 0:
            raise CharacterByteCoverageError(
                f"{self.path}: {owner} has negative count {count}"
            )
        if count and offset <= 0:
            raise CharacterByteCoverageError(
                f"{self.path}: {owner} has {count} records at invalid offset {offset}"
            )
        self.claim(
            offset, count * stride, state, owner, allow_existing=allow_existing
        )

    def cstring(self, offset: int, owner: str) -> None:
        if not 0 <= offset < len(self.data):
            raise CharacterByteCoverageError(
                f"{self.path}: {owner} string offset {offset} is outside the file"
            )
        end = self.data.find(b"\0", offset)
        if end < 0:
            raise CharacterByteCoverageError(
                f"{self.path}: {owner} string at {offset} has no terminator"
            )
        try:
            self.data[offset:end].decode("ascii", "strict")
        except UnicodeDecodeError as error:
            raise CharacterByteCoverageError(
                f"{self.path}: {owner} is not losslessly decodable ASCII"
            ) from error
        self.claim(
            offset,
            end + 1 - offset,
            "mapped-string",
            owner,
            allow_existing=True,
        )

    def gaps(self) -> list[tuple[int, int, str, str]]:
        """Current unclaimed intervals with their immediately adjacent owners."""

        result = []
        position = 0
        while position < len(self.data):
            if self._owners[position] >= 0:
                position += 1
                continue
            end = position + 1
            while end < len(self.data) and self._owners[end] < 0:
                end += 1
            previous = (
                self._ranges[self._owners[position - 1]]["owner"]
                if position > 0 and self._owners[position - 1] >= 0
                else "<start>"
            )
            following = (
                self._ranges[self._owners[end]]["owner"]
                if end < len(self.data) and self._owners[end] >= 0
                else "<end>"
            )
            result.append((position, end, previous, following))
            position = end
        return result

    def finish(self) -> dict[str, Any]:
        position = 0
        while position < len(self.data):
            if self._owners[position] >= 0:
                position += 1
                continue
            end = position + 1
            while end < len(self.data) and self._owners[end] < 0:
                end += 1
            gap = self.data[position:end]
            if any(gap):
                first = next(index for index, value in enumerate(gap) if value)
                at = position + first
                sample = self.data[at:at + 16].hex()
                previous = (
                    self._ranges[self._owners[position - 1]]["owner"]
                    if position > 0 and self._owners[position - 1] >= 0
                    else "<start>"
                )
                following = (
                    self._ranges[self._owners[end]]["owner"]
                    if end < len(self.data) and self._owners[end] >= 0
                    else "<end>"
                )
                raise CharacterByteCoverageError(
                    f"{self.path}: unclaimed non-zero byte at {at}; "
                    f"gap {position}+{end - position} between {previous} and "
                    f"{following}, starts {sample}"
                )
            self.claim(position, end - position, "padding-zero", "unreferenced-zero-gap")
            position = end

        ranges = sorted(self._ranges, key=lambda row: row["offset"])
        cursor = 0
        for row in ranges:
            if row["offset"] != cursor:
                raise CharacterByteCoverageError(
                    f"{self.path}: internal ledger discontinuity at {cursor}"
                )
            cursor += row["length"]
        if cursor != len(self.data):
            raise CharacterByteCoverageError(
                f"{self.path}: ledger ends at {cursor}/{len(self.data)}"
            )

        state_bytes = Counter()
        for row in ranges:
            state_bytes[row["state"]] += row["length"]
        canonical = json.dumps(
            {"path": self.path, "byteLength": len(self.data), "ranges": ranges},
            sort_keys=True,
            separators=(",", ":"),
        ).encode("utf-8")
        return {
            "sourcePath": self.path,
            "sourceSha256": hashlib.sha256(self.data).hexdigest(),
            "byteLength": len(self.data),
            "accountedBytes": len(self.data),
            "coveragePercent": 100.0,
            "stateBytes": dict(sorted(state_bytes.items())),
            "rangesSha256": hashlib.sha256(canonical).hexdigest(),
            "ranges": ranges,
        }


def _i32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<i", data, offset)[0]


def _u16(data: bytes, offset: int) -> int:
    return struct.unpack_from("<H", data, offset)[0]


def _mdl_rle_length(data: bytes, offset: int, frames: int) -> int:
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


COMPILER_TRAILER_MAGIC = b"QnDbTm"


def compiler_trailer(data: bytes) -> dict[str, Any] | None:
    """EOF ``uint32 pathOffset`` + ``QnDbTm`` compiler trailer, or None.

    No engine, Crowbar, or VAMPTools string references the magic. The pointer
    names an already-claimed in-file path (model directory / texture search
    path). The ten bytes sit inside the declared MDL image length.
    """
    if len(data) < 10 or data[-6:] != COMPILER_TRAILER_MAGIC:
        return None
    offset = len(data) - 10
    pointer = struct.unpack_from("<I", data, offset)[0]
    path = ""
    if 0 <= pointer < offset:
        end = data.find(b"\0", pointer, offset)
        if end > pointer:
            try:
                path = data[pointer:end].decode("ascii")
            except UnicodeDecodeError:
                path = ""
    return {
        "magic": "QnDbTm",
        "offset": offset,
        "pathOffset": pointer,
        "path": path,
    }


def _rle_exact_track_length(data: bytes, start: int, end: int, frames: int) -> int:
    """Bytes of one well-formed RLE track that covers exactly ``frames`` frames."""
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


def cover_mdl(path: str, data: bytes, *, vtx_data: bytes | None = None) -> dict[str, Any]:
    """Claim every declared v2531 MDL record, payload and referenced string."""
    if len(data) < 424 or data[:4] != b"IDST" or _i32(data, 4) != 2531:
        raise CharacterByteCoverageError(f"{path}: not a complete MDL v2531 image")
    ledger = ByteLedger(path, data)
    ledger.claim(0, 424, "mapped", "studiohdr")
    declared_length = _i32(data, 140)
    if not 424 <= declared_length <= len(data):
        raise CharacterByteCoverageError(
            f"{path}: declared MDL image length {declared_length} outside {len(data)} bytes"
        )
    if declared_length < len(data):
        trailing = data[declared_length:]
        if any(byte not in b"\t\n\r " for byte in trailing):
            raise CharacterByteCoverageError(
                f"{path}: non-whitespace data follows declared MDL image at {declared_length}"
            )
        ledger.claim(
            declared_length,
            len(data) - declared_length,
            "omitted-proven",
            "patchToolTrailingWhitespace",
        )

    def pair(count_at: int, offset_at: int) -> tuple[int, int]:
        return _i32(data, count_at), _i32(data, offset_at)

    def relative_string(owner: int, relative: int, label: str) -> None:
        if relative > 0:
            ledger.cstring(owner + relative, label)

    bone_count, bone_base = pair(240, 244)
    ledger.array(bone_base, bone_count, 160, "bones")
    axis_records = []
    for index in range(bone_count):
        bone = bone_base + index * 160
        relative_string(bone, _i32(data, bone), f"bone[{index}].name")
        surface = _i32(data, bone + 152)
        relative_string(bone, surface, f"bone[{index}].surfaceProperty")
        proc_type, proc_relative = _i32(data, bone + 140), _i32(data, bone + 144)
        if proc_type == 1:
            axis_records.append(bone + proc_relative)
            ledger.claim(
                bone + proc_relative, 176, "mapped", f"bone[{index}].axisInterpolation"
            )
        elif proc_type != 0:
            raise CharacterByteCoverageError(
                f"{path}: bone {index} has unsupported procedural type {proc_type}"
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
                "unindexedAxisInterpolationDuplicate",
                state="omitted-proven",
            )

    controller_count, controller_base = pair(248, 252)
    ledger.array(controller_base, controller_count, 24, "boneControllers")

    hitbox_count, hitbox_base = pair(256, 260)
    ledger.array(hitbox_base, hitbox_count, 12, "hitboxSets")
    for index in range(hitbox_count):
        record = hitbox_base + index * 12
        name = _i32(data, record)
        if name:
            ledger.cstring(record + name, f"hitboxSet[{index}].name")
        ledger.array(
            record + _i32(data, record + 8),
            _i32(data, record + 4),
            32,
            f"hitboxSet[{index}].boxes",
        )

    animation_count, animation_base = pair(264, 268)
    ledger.array(animation_base, animation_count, 72, "animationDescriptors")
    for index in range(animation_count):
        descriptor = animation_base + index * 72
        relative_string(descriptor, _i32(data, descriptor), f"animation[{index}].name")
        frames = _i32(data, descriptor + 12)
        ledger.array(
            descriptor + _i32(data, descriptor + 20),
            _i32(data, descriptor + 16),
            44,
            f"animation[{index}].movements",
        )
        records = descriptor + _i32(data, descriptor + 48)
        ledger.array(records, bone_count, 32, f"animation[{index}].boneRecords")
        for bone in range(bone_count):
            record = records + bone * 32
            for channel, relative in enumerate(struct.unpack_from("<7i", data, record + 4)):
                if not relative:
                    continue
                start = record + relative
                ledger.claim(
                    start,
                    _mdl_rle_length(data, start, frames),
                    "mapped",
                    f"animation[{index}].bone[{bone}].channel[{channel}]",
                    allow_existing=True,
                )

    sequence_count, sequence_base = pair(272, 276)
    ledger.array(sequence_base, sequence_count, 764, "sequenceDescriptors")
    for index in range(sequence_count):
        descriptor = sequence_base + index * 764
        relative_string(descriptor, _i32(data, descriptor), f"sequence[{index}].label")
        relative_string(
            descriptor, _i32(data, descriptor + 4), f"sequence[{index}].activity"
        )
        ledger.array(
            descriptor + _i32(data, descriptor + 24),
            _i32(data, descriptor + 20),
            76,
            f"sequence[{index}].events",
        )
        layer_count = _i32(data, descriptor + 660)
        if 0 < layer_count <= 16:
            ledger.array(
                descriptor + _i32(data, descriptor + 664),
                layer_count,
                4,
                f"sequence[{index}].autolayers",
            )
        envelope_count = _i32(data, descriptor + 700)
        if 0 < envelope_count <= 512:
            ledger.array(
                descriptor + _i32(data, descriptor + 704),
                envelope_count,
                24,
                f"sequence[{index}].envelopes",
            )
        swing_count = _i32(data, descriptor + 708)
        if 0 < swing_count <= 20:
            swing_base = descriptor + _i32(data, descriptor + 712)
            ledger.array(swing_base, swing_count, 188, f"sequence[{index}].swings")
            for swing in range(swing_count):
                record = swing_base + swing * 188
                for name in range(16):
                    relative = _i32(data, record + 0x78 + name * 4)
                    relative_string(
                        record,
                        relative,
                        f"sequence[{index}].swing[{swing}].name[{name}]",
                    )
        for field, label in ((732, "dodge"), (740, "blocked"), (744, "chain"),
                             (748, "alternateChain")):
            relative_string(
                descriptor,
                _i32(data, descriptor + field),
                f"sequence[{index}].{label}",
            )

    seqgroup_count, seqgroup_base = pair(284, 288)
    ledger.array(seqgroup_base, seqgroup_count, 16, "sequenceGroups")
    for index in range(seqgroup_count):
        record = seqgroup_base + index * 16
        relative_string(record, _i32(data, record), f"sequenceGroup[{index}].label")
        relative_string(record, _i32(data, record + 4), f"sequenceGroup[{index}].name")

    texture_count, texture_base = pair(292, 296)
    ledger.array(texture_base, texture_count, 20, "textures")
    for index in range(texture_count):
        record = texture_base + index * 20
        relative_string(record, _i32(data, record), f"texture[{index}].name")

    search_count, search_base = pair(300, 304)
    ledger.array(search_base, search_count, 4, "textureSearchPathOffsets")
    for index in range(search_count):
        ledger.cstring(
            _i32(data, search_base + index * 4), f"textureSearchPath[{index}]"
        )

    skin_refs, skin_base = _i32(data, 308), _i32(data, 316)
    skin_families = _i32(data, 312)
    ledger.array(skin_base, skin_refs * skin_families, 2, "skinTable")

    bodypart_count, bodypart_base = pair(320, 324)
    ledger.array(bodypart_base, bodypart_count, 16, "bodyParts")
    for bodypart_index in range(bodypart_count):
        bodypart = bodypart_base + bodypart_index * 16
        relative_string(
            bodypart, _i32(data, bodypart), f"bodyPart[{bodypart_index}].name"
        )
        model_count = _i32(data, bodypart + 4)
        model_base = bodypart + _i32(data, bodypart + 12)
        ledger.array(
            model_base, model_count, mdl_skel.MODEL_STRIDE,
            f"bodyPart[{bodypart_index}].models",
        )
        for model_index in range(model_count):
            model_record = model_base + model_index * mdl_skel.MODEL_STRIDE
            model_tag = f"bodyPart[{bodypart_index}].model[{model_index}]"
            vertex_count = _i32(data, model_record + 144)
            vertex_type = _i32(data, model_record + 156)
            vertex_stride = mdl.VSTRIDE.get(vertex_type)
            if vertex_stride is None:
                raise CharacterByteCoverageError(
                    f"{path}: {model_tag} has unknown vertex type {vertex_type}"
                )
            ledger.array(
                model_record + _i32(data, model_record + 148),
                vertex_count,
                vertex_stride,
                f"{model_tag}.vertices",
            )
            tangent_relative = _i32(data, model_record + 152)
            if tangent_relative:
                ledger.array(
                    model_record + tangent_relative,
                    vertex_count,
                    16,
                    f"{model_tag}.tangents",
                )
            mesh_count = _i32(data, model_record + 136)
            mesh_base = model_record + _i32(data, model_record + 140)
            ledger.array(mesh_base, mesh_count, mdl_skel.MESH_STRIDE, f"{model_tag}.meshes")
            for mesh_index in range(mesh_count):
                mesh = mesh_base + mesh_index * mdl_skel.MESH_STRIDE
                mesh_tag = f"{model_tag}.mesh[{mesh_index}]"
                flex_count = _i32(data, mesh + 16)
                flex_base = mesh + _i32(data, mesh + 20)
                ledger.array(flex_base, flex_count, 32, f"{mesh_tag}.flexes")
                for flex_index in range(flex_count):
                    flex = flex_base + flex_index * 32
                    kind = _i32(data, flex + 28)
                    stride = mdl_skel.VA_STRIDE.get(kind)
                    if stride is None:
                        raise CharacterByteCoverageError(
                            f"{path}: {mesh_tag}.flex[{flex_index}] type {kind}"
                        )
                    ledger.array(
                        flex + _i32(data, flex + 24),
                        _i32(data, flex + 20),
                        stride,
                        f"{mesh_tag}.flex[{flex_index}].vertices",
                    )
            eyeball_count = _i32(data, model_record + 192)
            eyeball_base = model_record + _i32(data, model_record + 196)
            ledger.array(eyeball_base, eyeball_count, 140, f"{model_tag}.eyeballs")
            for eyeball_index in range(eyeball_count):
                eyeball = eyeball_base + eyeball_index * 140
                relative_string(
                    eyeball,
                    _i32(data, eyeball),
                    f"{model_tag}.eyeball[{eyeball_index}].name",
                )
            _cover_mdl_cloth(
                ledger,
                data,
                model_record,
                mesh_base,
                mesh_count,
                model_tag,
                vtx_data=vtx_data,
                bodypart_index=bodypart_index,
                model_index=model_index,
            )

    attachment_count, attachment_base = pair(328, 332)
    ledger.array(attachment_base, attachment_count, 60, "attachments")
    for index in range(attachment_count):
        record = attachment_base + index * 60
        relative_string(record, _i32(data, record), f"attachment[{index}].name")

    transition_count, transition_base = pair(336, 340)
    ledger.array(
        transition_base, transition_count * transition_count, 1, "transitionGraph"
    )

    flexdesc_count, flexdesc_base = pair(344, 348)
    ledger.array(flexdesc_base, flexdesc_count, 4, "flexDescriptions")
    for index in range(flexdesc_count):
        record = flexdesc_base + index * 4
        relative_string(record, _i32(data, record), f"flexDescription[{index}]")

    flexctrl_count, flexctrl_base = pair(352, 356)
    ledger.array(flexctrl_base, flexctrl_count, 20, "flexControllers")
    for index in range(flexctrl_count):
        record = flexctrl_base + index * 20
        relative_string(record, _i32(data, record), f"flexController[{index}].type")
        relative_string(record, _i32(data, record + 4), f"flexController[{index}].name")

    flexrule_count, flexrule_base = pair(360, 364)
    ledger.array(flexrule_base, flexrule_count, 12, "flexRules")
    for index in range(flexrule_count):
        record = flexrule_base + index * 12
        ledger.array(
            record + _i32(data, record + 8),
            _i32(data, record + 4),
            8,
            f"flexRule[{index}].operations",
        )

    ik_count, ik_base = pair(368, 372)
    ledger.array(ik_base, ik_count, 16, "ikChains")
    for index in range(ik_count):
        record = ik_base + index * 16
        relative_string(record, _i32(data, record), f"ikChain[{index}].name")
        ledger.array(
            record + _i32(data, record + 12),
            _i32(data, record + 8),
            28,
            f"ikChain[{index}].links",
        )

    mouth_count, mouth_base = pair(376, 380)
    ledger.array(mouth_base, mouth_count, 20, "mouths")

    pose_count, pose_base = pair(384, 388)
    ledger.array(pose_base, pose_count, 20, "poseParameters")
    for index in range(pose_count):
        record = pose_base + index * 20
        relative_string(record, _i32(data, record), f"poseParameter[{index}].name")

    surface = _i32(data, 392)
    if surface:
        ledger.cstring(surface, "surfaceProperty")

    secondary_count, secondary_base = pair(396, 400)
    ledger.array(secondary_base, secondary_count, 28, "secondaryMotion")

    include_count, include_base = pair(404, 408)
    ledger.array(include_base, include_count, 116, "includeModels")
    for index in range(include_count):
        record = include_base + index * 116
        relative_string(record, _i32(data, record), f"includeModel[{index}].path")
        ledger.array(
            record + _i32(data, record + 16),
            bone_count,
            56,
            f"includeModel[{index}].boneRemap",
        )

    _cover_retained_mdl_payloads(ledger, data, bone_count)

    return ledger.finish()


def _unclaimed(ledger: ByteLedger, offset: int, length: int) -> bool:
    if offset < 0 or length <= 0 or offset + length > len(ledger.data):
        return False
    return all(owner < 0 for owner in ledger._owners[offset:offset + length])


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
    return {
        "offset": offset,
        "path": path,
        "remapBase": remap_base,
        "remapLength": remap_length,
    }


def _claim_unindexed_include_group(
    ledger: ByteLedger, group: dict[str, Any], owner: str
) -> None:
    ledger.claim(group["offset"], 116, "omitted-proven", owner)
    relative = _i32(ledger.data, group["offset"])
    ledger.cstring(group["offset"] + relative, f"{owner}.path")
    if _unclaimed(ledger, group["remapBase"], group["remapLength"]):
        ledger.array(
            group["remapBase"],
            group["remapLength"] // 56,
            56,
            f"{owner}.boneRemap",
            state="omitted-proven",
        )


def _cover_unindexed_include_groups(
    ledger: ByteLedger, data: bytes, bone_count: int
) -> None:
    include_count, include_base = _i32(data, 404), _i32(data, 408)
    cursor = include_base + max(include_count, 0) * 116
    if include_count == 0 and include_base > 0:
        cursor = include_base
    extra = 0
    while True:
        group = _looks_like_include_group(data, cursor, bone_count)
        if group is None or not _unclaimed(ledger, cursor, 116):
            break
        _claim_unindexed_include_group(
            ledger, group, f"unindexedIncludeModel[{extra}]"
        )
        extra += 1
        cursor += 116
    for start, end, _previous, _following in list(ledger.gaps()):
        if end - start < 116:
            continue
        group = _looks_like_include_group(data, start, bone_count)
        if group is None or not _unclaimed(ledger, start, 116):
            continue
        _claim_unindexed_include_group(
            ledger, group, f"unindexedIncludeModel@{start}"
        )


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
    values = struct.unpack_from("<5f", data, offset + 8)
    return all(math.isfinite(value) for value in values)


def _cover_unindexed_secondary_motion(
    ledger: ByteLedger, data: bytes, bone_count: int
) -> None:
    count, base = _i32(data, 396), _i32(data, 400)
    cursor = base + max(count, 0) * 28
    extra = 0
    while _unclaimed(ledger, cursor, 28) and _looks_like_secondary_record(
        data, cursor, bone_count
    ):
        ledger.claim(
            cursor, 28, "omitted-proven", f"unindexedSecondaryMotion[{extra}]"
        )
        extra += 1
        cursor += 28
    if count <= 0 or base <= 0:
        return
    indexed = [
        data[base + index * 28:base + (index + 1) * 28] for index in range(count)
    ]
    for start, end, _previous, _following in list(ledger.gaps()):
        length = end - start
        if length < 28 or length % 28:
            continue
        records = length // 28
        payload = data[start:end]
        if not all(
            payload[index * 28:(index + 1) * 28] in indexed
            for index in range(records)
        ):
            continue
        if _unclaimed(ledger, start, length):
            ledger.array(
                start,
                records,
                28,
                "unindexedSecondaryMotionDuplicate",
                state="omitted-proven",
            )


def _looks_like_texture_record(data: bytes, offset: int) -> bool:
    if offset < 0 or offset + 20 > len(data):
        return False
    name = _relative_cstring(data, offset, _i32(data, offset))
    if name is None or len(name) < 2:
        return False
    width, height, metric = struct.unpack_from("<3f", data, offset + 8)
    return all(math.isfinite(value) for value in (width, height, metric))


def _cover_duplicate_texture_table(ledger: ByteLedger, data: bytes) -> None:
    count, base = _i32(data, 292), _i32(data, 296)
    if count <= 0 or base < 20:
        return
    extra = 0
    cursor = base - 20
    while (
        extra < 32
        and cursor >= 0
        and _unclaimed(ledger, cursor, 20)
        and _looks_like_texture_record(data, cursor)
    ):
        extra += 1
        cursor -= 20
    if extra:
        start = base - extra * 20
        ledger.array(
            start, extra, 20, "unindexedTextureDuplicate", state="omitted-proven"
        )
        for index in range(extra):
            record = start + index * 20
            relative = _i32(data, record)
            if relative > 0:
                name_at = record + relative
                if not start <= name_at < start + extra * 20:
                    ledger.cstring(
                        name_at, f"unindexedTextureDuplicate[{index}].name"
                    )
    for start, end, _previous, _following in list(ledger.gaps()):
        length = end - start
        if length < 20 or length % 20 or length // 20 > 32:
            continue
        records = length // 20
        if not all(
            _looks_like_texture_record(data, start + index * 20)
            for index in range(records)
        ):
            continue
        if _unclaimed(ledger, start, length):
            ledger.array(
                start, records, 20, "unindexedTextureDuplicate", state="omitted-proven"
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
    return {
        "offset": offset,
        "name": name,
        "frames": frames,
        "movements": movements,
    }


def _looks_like_bone_records(data: bytes, offset: int, bone_count: int) -> bool:
    if bone_count <= 0 or offset < 0 or offset + bone_count * 32 > len(data):
        return False
    payload = data[offset:offset + bone_count * 32]
    if not any(payload):
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
    ledger: ByteLedger,
    data: bytes,
    records: int,
    bone_count: int,
    frames: int,
    owner: str,
) -> None:
    for bone in range(bone_count):
        record = records + bone * 32
        for channel, relative in enumerate(struct.unpack_from("<7i", data, record + 4)):
            if not relative:
                continue
            start = record + relative
            length = _mdl_rle_length(data, start, frames)
            if _unclaimed(ledger, start, length):
                ledger.claim(
                    start,
                    length,
                    "omitted-proven",
                    f"{owner}.bone[{bone}].channel[{channel}]",
                )


def _claim_unindexed_animation_descriptor(
    ledger: ByteLedger,
    data: bytes,
    descriptor: dict[str, Any],
    bone_count: int,
    owner: str,
) -> None:
    offset = descriptor["offset"]
    if not _unclaimed(ledger, offset, 72):
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
        if _unclaimed(ledger, movement_base, movements * 44):
            ledger.array(
                movement_base,
                movements,
                44,
                f"{owner}.movements",
                state="omitted-proven",
            )
    anim_relative = _i32(data, offset + 48)
    records = offset + anim_relative
    if anim_relative and _unclaimed(ledger, records, bone_count * 32):
        ledger.array(
            records, bone_count, 32, f"{owner}.boneRecords", state="omitted-proven"
        )
        _claim_animation_channels(
            ledger, data, records, bone_count, descriptor["frames"], owner
        )


def _cover_donor_animation_payloads(
    ledger: ByteLedger, data: bytes, bone_count: int
) -> None:
    extra = 0
    for start, end, _previous, _following in list(ledger.gaps()):
        offset = start
        while offset + 72 <= end:
            descriptor = _animation_descriptor_at(data, offset)
            if descriptor is not None and _unclaimed(ledger, offset, 72):
                _claim_unindexed_animation_descriptor(
                    ledger,
                    data,
                    descriptor,
                    bone_count,
                    f"unindexedAnimation[{extra}]",
                )
                extra += 1
                offset += 72
                continue
            offset += 4
        if (
            _unclaimed(ledger, start, bone_count * 32)
            and _looks_like_bone_records(data, start, bone_count)
        ):
            owner = f"unindexedAnimationBoneRecords@{start}"
            ledger.array(
                start,
                bone_count,
                32,
                owner,
                state="omitted-proven",
            )
            _claim_animation_channels(ledger, data, start, bone_count, 1, owner)
    for start, end, previous, following in list(ledger.gaps()):
        length = end - start
        if length <= 0 or not _unclaimed(ledger, start, length):
            continue
        if "unindexedAnimation" not in previous or ".boneRecords" not in following:
            continue
        ledger.claim(
            start,
            length,
            "omitted-proven",
            f"{previous}.preBoneRecordPayload",
        )
    for start, end, previous, following in list(ledger.gaps()):
        length = end - start
        if not 2 <= length <= 8 or not _unclaimed(ledger, start, length):
            continue
        neighbours = previous + " " + following
        if "unindexedAnimation" not in neighbours and "sequenceDescriptors" not in neighbours:
            continue
        ledger.claim(
            start, length, "omitted-proven", f"unindexedAnimationAlignmentResidue@{start}"
        )
    for start, end, previous, following in list(ledger.gaps()):
        length = end - start
        if length <= 0 or not _unclaimed(ledger, start, length):
            continue
        if not any(ledger.data[start:end]):
            continue
        adjacent = previous.startswith("unindexedAnimation") or following.startswith(
            "unindexedAnimation"
        )
        if not adjacent:
            continue
        ledger.claim(
            start,
            length,
            "omitted-proven",
            f"{previous}.adjacentRetainedPayload",
        )


def _rle_span_length(data: bytes, start: int, end: int) -> int:
    """Bytes of a well-formed RLE stream that exactly fills ``[start, end)``."""
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


def _animation_frame_count(data: bytes, index: int) -> int | None:
    if len(data) < 272 or data[:4] != b"IDST":
        return None
    count = _i32(data, 264)
    base = _i32(data, 268)
    if not 0 <= index < count or base < 0 or base + (index + 1) * 72 > len(data):
        return None
    frames = _i32(data, base + index * 72 + 12)
    if not 0 < frames <= 1_000_000:
        return None
    return frames


def _cover_compiler_trailer(ledger: ByteLedger) -> None:
    trailer = compiler_trailer(ledger.data)
    if trailer is None:
        return
    if _unclaimed(ledger, trailer["offset"], 10):
        ledger.claim(
            trailer["offset"],
            10,
            "mapped",
            "compilerTrailerQnDbTm",
        )


def _cover_extra_animation_tracks(ledger: ByteLedger) -> None:
    data = ledger.data
    for start, end, previous, following in list(ledger.gaps()):
        match = re.fullmatch(
            r"animation\[(\d+)\]\.bone\[\d+\]\.channel\[\d+\]",
            previous,
        )
        if match is None or not _unclaimed(ledger, start, end - start):
            continue
        frames = _animation_frame_count(data, int(match.group(1)))
        if frames is None:
            continue
        offset = start
        extra = 0
        while offset < end:
            length = _rle_exact_track_length(data, offset, end, frames)
            if length <= 0:
                break
            ledger.claim(
                offset,
                length,
                "mapped",
                f"{previous}.unindexedTrack[{extra}]",
            )
            extra += 1
            offset += length
        leftover = end - offset
        if leftover <= 0 or not _unclaimed(ledger, offset, leftover):
            continue
        if leftover == 4 and data[offset:end] == b"ULDD":
            ledger.claim(
                offset,
                4,
                "omitted-proven",
                f"{previous}.compilerPackingULDD",
            )


def _cover_orphan_rle_samples(ledger: ByteLedger) -> None:
    for start, end, previous, following in list(ledger.gaps()):
        length = end - start
        if length < 4 or not _unclaimed(ledger, start, length):
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
        ledger.claim(
            start, length, "omitted-proven", f"orphanAnimationSample@{start}"
        )
    _cover_extra_animation_tracks(ledger)
    for start, end, previous, following in list(ledger.gaps()):
        length = end - start
        if length <= 0 or not _unclaimed(ledger, start, length):
            continue
        if not any(ledger.data[start:end]):
            continue
        if "channel" in previous and (
            following == "<end>" or following.startswith("textureSearchPath")
        ):
            ledger.claim(
                start, length, "omitted-proven", f"{previous}.trailingPayload"
            )


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
    if not 1 <= vertex_count <= 20000 or not 0 <= flex_count <= 256:
        return False
    return True


def _cover_unindexed_meshes(ledger: ByteLedger, data: bytes) -> None:
    for start, end, _previous, _following in list(ledger.gaps()):
        if end - start < mdl_skel.MESH_STRIDE:
            continue
        if not _looks_like_studio_mesh(data, start):
            continue
        if not _unclaimed(ledger, start, mdl_skel.MESH_STRIDE):
            continue
        vertex_count = _i32(data, start + 8)
        flex_count = _i32(data, start + 16)
        owner = f"unindexedMesh@{start}"
        ledger.claim(start, mdl_skel.MESH_STRIDE, "omitted-proven", owner)
        cursor = start + mdl_skel.MESH_STRIDE
        for stride, name in ((44, "vertices"), (16, "tangents")):
            length = vertex_count * stride
            if cursor + length <= end and _unclaimed(ledger, cursor, length):
                ledger.array(
                    cursor,
                    vertex_count,
                    stride,
                    f"{owner}.{name}",
                    state="omitted-proven",
                )
                cursor += length
        flex_length = flex_count * 32
        if flex_count and cursor + flex_length <= end and _unclaimed(
            ledger, cursor, flex_length
        ):
            ledger.array(
                cursor, flex_count, 32, f"{owner}.flexes", state="omitted-proven"
            )
            cursor += flex_length
        leftover = end - cursor
        if vertex_count >= 300 and leftover > 0 and any(data[cursor:end]):
            if _unclaimed(ledger, cursor, leftover):
                ledger.claim(
                    cursor, leftover, "omitted-proven", f"{owner}.retainedPayload"
                )


def _cover_unreferenced_strings(ledger: ByteLedger, data: bytes) -> None:
    tokens = ("models", "materials", "anim", "idle", "bone", ".mdl", ".vmt")
    for start, end, _previous, _following in list(ledger.gaps()):
        if end - start > 512:
            continue
        cursor = start
        while cursor < end and _unclaimed(ledger, cursor, 1):
            if data[cursor] == 0:
                break
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
                or any(token in lowered for token in tokens)
                or (len(text) >= 4 and text.replace("_", "").isalnum())
            )
            if not named:
                break
            ledger.claim(
                cursor,
                terminator + 1 - cursor,
                "omitted-proven",
                f"unreferencedString@{cursor}",
            )
            cursor = terminator + 1


def _cover_retained_mdl_payloads(
    ledger: ByteLedger, data: bytes, bone_count: int
) -> None:
    """Classify compiler-retained donor/cache bytes the declared tables do not index."""
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
        if length <= 0 or length > 64 or not _unclaimed(ledger, start, length):
            continue
        if not any(data[start:end]):
            continue
        neighbours = previous + " " + following
        if "textureSearchPath" not in neighbours and "skinTable" not in neighbours:
            continue
        ledger.claim(
            start,
            length,
            "omitted-proven",
            f"{previous}.unindexedSearchOrSkinResidue",
        )
    for start, end, previous, following in list(ledger.gaps()):
        length = end - start
        if length <= 0 or length > 32 or not _unclaimed(ledger, start, length):
            continue
        if ".name" not in previous or ".name" not in following:
            continue
        if any(ledger.data[start:end]):
            ledger.claim(
                start, length, "omitted-proven", f"unreferencedStringResidue@{start}"
            )
    for start, end, previous, following in list(ledger.gaps()):
        length = end - start
        if length <= 0 or not _unclaimed(ledger, start, length):
            continue
        if not any(data[start:end]):
            continue
        string_owners = (".name", ".path", ".label")
        if following == "<end>" and any(token in previous for token in string_owners) and length <= 16:
            ledger.claim(
                start, length, "omitted-proven", f"{previous}.trailingCompilerResidue"
            )
            continue
        if length <= 8 and any(token in previous for token in string_owners) and any(
            token in following for token in string_owners
        ):
            ledger.claim(
                start, length, "omitted-proven", f"stringAlignmentResidue@{start}"
            )
            continue
        if "cloth.selectors" in previous and "cloth.positions" in following:
            if all(byte == 0xFF for byte in data[start:end]):
                ledger.claim(
                    start,
                    length,
                    "omitted-proven",
                    f"{previous}.unselectedSuffix",
                )


_CLOTH_BLOCKS = (
    (0x10, (0x04,), 2, "particleVertices", "mapped"),
    (0x20, (0x18, 0x1C), 16, "constraints", "mapped"),
    (0x2C, (0x24, 0x28), 1, "packedSimdPayload", "omitted-proven"),
    (0x34, (0x30,), 6, "collisionTriangles", "mapped"),
    (0x40, (0x38,), 4, "edgePairs", "mapped"),
    (0x48, (0x44,), 10, "normalContributions", "omitted-proven"),
    (0x50, (0x4C,), 12, "tangentInterpolation", "omitted-proven"),
    (0x54, (0x04,), 16, "optionalSeedA", "mapped"),
    (0x58, (0x04,), 16, "optionalSeedB", "mapped"),
)


def _vtx_model_lod_count(vtx_data: bytes, bodypart_index: int, model_index: int) -> int:
    if len(vtx_data) < 36:
        raise CharacterByteCoverageError("VTX is too short to size cloth maps")
    bodypart_count = _i32(vtx_data, 28)
    if not 0 <= bodypart_index < bodypart_count:
        raise CharacterByteCoverageError(
            f"VTX has no bodypart {bodypart_index} for cloth map"
        )
    bodypart = _i32(vtx_data, 32) + bodypart_index * 8
    model_count = _i32(vtx_data, bodypart)
    if not 0 <= model_index < model_count:
        raise CharacterByteCoverageError(
            f"VTX bodypart {bodypart_index} has no model {model_index} for cloth map"
        )
    model = bodypart + _i32(vtx_data, bodypart + 4) + model_index * 8
    lod_count = _i32(vtx_data, model)
    if lod_count <= 0:
        raise CharacterByteCoverageError(
            f"VTX bodypart {bodypart_index} model {model_index} has {lod_count} LODs"
        )
    return lod_count


def _cover_mdl_cloth(
    ledger: ByteLedger,
    data: bytes,
    model: int,
    mesh_base: int,
    mesh_count: int,
    tag: str,
    *,
    vtx_data: bytes | None,
    bodypart_index: int,
    model_index: int,
) -> None:
    del vtx_data, bodypart_index, model_index
    _table, lod_rows, definition_count, records = mdl_cloth.definition_table(data, model)
    capsule_count = _i32(data, model + 208)
    sphere_count = _i32(data, model + 216)
    if capsule_count:
        ledger.array(model + _i32(data, model + 212), capsule_count, 36, f"{tag}.capsules")
    if sphere_count:
        ledger.array(model + _i32(data, model + 220), sphere_count, 20, f"{tag}.spheres")
    if not definition_count:
        return
    table = model + _i32(data, model + 204)
    ledger.array(
        table,
        lod_rows * definition_count,
        4,
        f"{tag}.clothDefinitionOffsets",
    )
    for index, record in enumerate(records):
        owner = f"{tag}.cloth[{index}]"
        ledger.claim(record, 92, "mapped", owner)
        for offset_field, count_fields, stride, name, state in _CLOTH_BLOCKS:
            relative = _i32(data, record + offset_field)
            count = sum(_i32(data, record + field) for field in count_fields)
            if name == "packedSimdPayload":
                count = (count + 3) // 4 * 4
            if relative and count:
                ledger.array(record + relative, count, stride, f"{owner}.{name}", state=state)
        triangle_count = _i32(data, record + 0x30)
        triangle_relative = _i32(data, record + 0x34)
        edge_relative = _i32(data, record + 0x40)
        if triangle_count > 0 and triangle_relative > 0 and edge_relative > 0:
            triangle_end = record + triangle_relative + triangle_count * 6
            edge_start = record + edge_relative
            if edge_start - triangle_end == 2:
                ledger.claim(
                    triangle_end,
                    2,
                    "omitted-proven",
                    f"{owner}.triangleAlignmentResidue",
                )
    for mesh_index in range(mesh_count):
        mesh = mesh_base + mesh_index * mdl_skel.MESH_STRIDE
        try:
            layout = mdl_cloth.map_layout(data, mesh)
        except ValueError as error:
            raise CharacterByteCoverageError(f"{ledger.path}: {error}") from error
        if layout is None:
            continue
        mesh_tag = f"{tag}.mesh[{mesh_index}].cloth"
        selector_count = layout["rows"] * layout["vertex_count"]
        ledger.array(
            layout["selector_base"],
            selector_count,
            1,
            f"{mesh_tag}.selectors",
        )
        if layout["selector_align"]:
            ledger.claim(
                layout["selector_base"] + selector_count,
                layout["selector_align"],
                "omitted-proven",
                f"{mesh_tag}.selectors.alignment",
            )
        position_count = layout["position_rows"] * layout["vertex_count"]
        ledger.array(
            layout["position_base"],
            position_count,
            2,
            f"{mesh_tag}.positions",
        )
        if layout["position_align"]:
            ledger.claim(
                layout["position_base"] + position_count * 2,
                layout["position_align"],
                "omitted-proven",
                f"{mesh_tag}.positions.alignment",
            )
        count = selector_count
        start = layout["tangent_base"]
        expected_end = start + count * 2
        secondary_count = _i32(data, 396)
        secondary_base = _i32(data, 400)
        if secondary_count > 0 and start < secondary_base < expected_end:
            available_bytes = secondary_base - start
            if available_bytes % 2:
                raise CharacterByteCoverageError(
                    f"{ledger.path}: {tag}.mesh[{mesh_index}] truncated tangent "
                    "payload is not uint16-aligned"
                )
            count = available_bytes // 2
            if any(
                value != 0xFF
                for value in data[
                    layout["selector_base"] + count:
                    layout["selector_base"] + selector_count
                ]
            ):
                raise CharacterByteCoverageError(
                    f"{ledger.path}: {tag}.mesh[{mesh_index}] tangent payload "
                    "omits a selected vertex"
                )
        ledger.array(start, count, 2, f"{mesh_tag}.tangents")


def cover_vtx(path: str, data: bytes) -> dict[str, Any]:
    """Claim the complete VtMB compact VTX hierarchy and replacement strings."""
    if len(data) < 36:
        raise CharacterByteCoverageError(f"{path}: VTX is only {len(data)} bytes")
    ledger = ByteLedger(path, data)
    ledger.claim(0, 36, "mapped", "vtxHeader")
    lod_count = _i32(data, 20)
    replacement_base = _i32(data, 24)
    ledger.array(replacement_base, lod_count, 8, "materialReplacementLists")
    for lod in range(lod_count):
        record = replacement_base + lod * 8
        count, relative = struct.unpack_from("<2i", data, record)
        base = record + relative
        ledger.array(base, count, 6, f"materialReplacementList[{lod}]")
        for index in range(count):
            replacement = base + index * 6
            ledger.cstring(
                replacement + _i32(data, replacement + 2),
                f"materialReplacementList[{lod}][{index}].name",
            )

    bodypart_count, bodypart_base = _i32(data, 28), _i32(data, 32)
    ledger.array(bodypart_base, bodypart_count, 8, "bodyParts")
    for bodypart_index in range(bodypart_count):
        bodypart = bodypart_base + bodypart_index * 8
        model_count, model_relative = struct.unpack_from("<2i", data, bodypart)
        model_base = bodypart + model_relative
        ledger.array(model_base, model_count, 8, f"bodyPart[{bodypart_index}].models")
        for model_index in range(model_count):
            model = model_base + model_index * 8
            model_tag = f"bodyPart[{bodypart_index}].model[{model_index}]"
            model_lods, lod_relative = struct.unpack_from("<2i", data, model)
            lod_base = model + lod_relative
            ledger.array(lod_base, model_lods, 12, f"{model_tag}.lods")
            for lod_index in range(model_lods):
                lod = lod_base + lod_index * 12
                mesh_count, mesh_relative = struct.unpack_from("<2i", data, lod)
                mesh_base = lod + mesh_relative
                lod_tag = f"{model_tag}.lod[{lod_index}]"
                ledger.array(mesh_base, mesh_count, 8, f"{lod_tag}.meshes")
                for mesh_index in range(mesh_count):
                    mesh = mesh_base + mesh_index * 8
                    stripgroup_count = _u16(data, mesh)
                    stripgroup_base = mesh + _i32(data, mesh + 4)
                    mesh_tag = f"{lod_tag}.mesh[{mesh_index}]"
                    ledger.array(
                        stripgroup_base,
                        stripgroup_count,
                        mdl.STRIPGROUP_STRIDE,
                        f"{mesh_tag}.stripGroups",
                        allow_existing=True,
                    )
                    for group_index in range(stripgroup_count):
                        group = stripgroup_base + group_index * mdl.STRIPGROUP_STRIDE
                        group_tag = f"{mesh_tag}.stripGroup[{group_index}]"
                        vertex_count = _u16(data, group)
                        index_count = _u16(data, group + 2)
                        strip_count = _u16(data, group + 4)
                        vertex_stride, _vertex_offset = mdl._vtable_form(data, group)
                        ledger.array(
                            group + _i32(data, group + 8),
                            vertex_count,
                            vertex_stride,
                            f"{group_tag}.vertices",
                            allow_existing=True,
                        )
                        ledger.array(
                            group + _i32(data, group + 12),
                            index_count,
                            2,
                            f"{group_tag}.indices",
                            allow_existing=True,
                        )
                        strip_base = group + _i32(data, group + 16)
                        ledger.array(
                            strip_base,
                            strip_count,
                            16,
                            f"{group_tag}.strips",
                            allow_existing=True,
                        )
                        for strip_index in range(strip_count):
                            strip = strip_base + strip_index * 16
                            state_count = _u16(data, strip + 10)
                            ledger.array(
                                strip + _i32(data, strip + 12),
                                state_count,
                                4,
                                f"{group_tag}.strip[{strip_index}].boneStateChanges",
                                allow_existing=True,
                            )
    _cover_unindexed_vtx_records(ledger, data)
    return ledger.finish()


def _cover_unindexed_vtx_records(ledger: ByteLedger, data: bytes) -> None:
    for start, end, previous, following in list(ledger.gaps()):
        length = end - start
        if length <= 0 or not _unclaimed(ledger, start, length) or not any(data[start:end]):
            continue
        if length % 12 == 0 and "lods" in previous and "meshes" in following:
            ledger.array(
                start,
                length // 12,
                12,
                f"{previous}.unindexedLods",
                state="omitted-proven",
            )
            continue
        if length % 8 == 0 and "meshes" in previous and "stripGroups" in following:
            ledger.array(
                start,
                length // 8,
                8,
                f"{previous}.unindexedMeshes",
                state="omitted-proven",
            )
            continue
        stride = mdl.STRIPGROUP_STRIDE
        if length % stride == 0 and "stripGroups" in previous and "strips" in following:
            ledger.array(
                start,
                length // stride,
                stride,
                f"{previous}.unindexedStripGroups",
                state="omitted-proven",
            )
            continue
        if length % 16 == 0 and "strips" in previous and "vertices" in following:
            ledger.array(
                start,
                length // 16,
                16,
                f"{previous}.unindexedStrips",
                state="omitted-proven",
            )
            continue
        if length % 2 == 0 and "indices" in previous:
            ledger.array(
                start,
                length // 2,
                2,
                f"{previous}.unindexedIndices",
                state="omitted-proven",
            )
            continue
        ledger.claim(
            start,
            length,
            "omitted-proven",
            f"{previous}.unindexedVtxPayload",
        )


def cover_vfe(path: str, data: bytes) -> dict[str, Any]:
    if len(data) < 172 or data[:4] != b"EFV\0":
        raise CharacterByteCoverageError(f"{path}: not a complete VFE image")
    ledger = ByteLedger(path, data)
    ledger.claim(0, 172, "mapped", "vfeHeader")
    (
        _length,
        setting_count,
        setting_offset,
        table_name_offset,
        index_count,
        index_offset,
        key_count,
        key_name_offset,
        key_mapping_offset,
    ) = struct.unpack_from("<9i", data, 136)
    ledger.array(setting_offset, setting_count, 24, "settings")
    ledger.array(index_offset, index_count, 4, "indexes")
    ledger.array(key_name_offset, key_count, 4, "keyNameOffsets")
    ledger.array(key_mapping_offset, key_count, 4, "keyMappings")
    if table_name_offset:
        ledger.cstring(table_name_offset, "tableName")
    for index in range(key_count):
        ledger.cstring(_i32(data, key_name_offset + index * 4), f"key[{index}].name")
    for index in range(setting_count):
        record = setting_offset + index * 24
        name_offset, kind, count = struct.unpack_from("<3i", data, record)
        ledger.cstring(record + name_offset, f"setting[{index}].name")
        stride = 12 if kind == 0 else 8 if kind == 1 else 0
        if not stride:
            raise CharacterByteCoverageError(f"{path}: setting {index} type {kind}")
        ledger.array(
            record + _i32(data, record + 20),
            count,
            stride,
            f"setting[{index}].values",
        )
    return ledger.finish()


def cover_txt(path: str, data: bytes) -> dict[str, Any]:
    data.decode("utf-8-sig", "strict")
    ledger = ByteLedger(path, data)
    ledger.claim(0, len(data), "mapped-text", "faceposerText")
    return ledger.finish()


def cover_phy(path: str, data: bytes) -> dict[str, Any]:
    if len(data) < 16:
        raise CharacterByteCoverageError(f"{path}: PHY is only {len(data)} bytes")
    ledger = ByteLedger(path, data)
    header_size, _ident, solid_count, _checksum = struct.unpack_from("<4i", data)
    ledger.claim(0, 16, "mapped", "phyHeader")
    if header_size > 16:
        ledger.claim(16, header_size - 16, "reserved-zero", "phyHeaderExtension")
    position = header_size
    for solid_index in range(solid_count):
        solid_size = _i32(data, position)
        body = position + 4
        tag = f"solid[{solid_index}]"
        ledger.claim(position, 4, "mapped", f"{tag}.size")
        ledger.claim(body, 48, "mapped", f"{tag}.header")
        root = _i32(data, body + 32)
        ledges: list[int] = []
        _cover_phy_node(ledger, data, body + root, tag, set(), ledges)
        for ledge_index, ledge in enumerate(ledges):
            _cover_phy_ledge(ledger, data, ledge, f"{tag}.ledge[{ledge_index}]")
        end = body + solid_size
        cursor = body
        while cursor < end:
            if ledger._owners[cursor] >= 0:
                cursor += 1
                continue
            gap_end = cursor + 1
            while gap_end < end and ledger._owners[gap_end] < 0:
                gap_end += 1
            if any(data[cursor:gap_end]):
                raise CharacterByteCoverageError(
                    f"{path}: {tag} has unclaimed non-zero bytes at "
                    f"{cursor}+{gap_end - cursor}"
                )
            ledger.claim(cursor, gap_end - cursor, "padding-zero", f"{tag}.zeroGap")
            cursor = gap_end
        position = end
    try:
        data[position:].decode("ascii", "strict")
    except UnicodeDecodeError as error:
        raise CharacterByteCoverageError(
            f"{path}: PHY KeyValues tail is not losslessly decodable ASCII"
        ) from error
    ledger.claim(position, len(data) - position, "mapped-text", "phyKeyValuesTail")
    return ledger.finish()


def _cover_phy_node(
    ledger: ByteLedger,
    data: bytes,
    node: int,
    tag: str,
    visited: set[int],
    ledges: list[int],
) -> None:
    if node in visited:
        raise CharacterByteCoverageError(f"{ledger.path}: {tag} revisits node {node}")
    visited.add(node)
    ledger.claim(node, 28, "mapped", f"{tag}.node[{node}]")
    right, ledge = struct.unpack_from("<2i", data, node)
    if right == 0:
        ledges.append(node + ledge)
        return
    _cover_phy_node(ledger, data, node + 28, tag, visited, ledges)
    _cover_phy_node(ledger, data, node + right, tag, visited, ledges)


def _cover_phy_ledge(ledger: ByteLedger, data: bytes, offset: int, tag: str) -> None:
    point_offset, _node_offset, _packed, triangle_count, _padding = struct.unpack_from(
        "<iiIhh", data, offset
    )
    ledger.claim(offset, 16, "mapped", f"{tag}.header")
    triangles = offset + 16
    ledger.array(triangles, triangle_count, 16, f"{tag}.triangles")
    highest = -1
    for triangle in range(triangle_count):
        record = triangles + triangle * 16
        for edge in range(3):
            highest = max(highest, _i32(data, record + 4 + edge * 4) & 0xFFFF)
    if highest >= 0:
        ledger.array(
            offset + point_offset,
            highest + 1,
            16,
            f"{tag}.points",
        )


def cover_member(
    role: str,
    path: str,
    data: bytes,
    *,
    vtx_data: bytes | None = None,
) -> dict[str, Any]:
    if role == "mdl":
        return cover_mdl(path, data, vtx_data=vtx_data)
    if role.startswith("vtx-"):
        return cover_vtx(path, data)
    if role == "phy":
        return cover_phy(path, data)
    if role.endswith("-vfe"):
        return cover_vfe(path, data)
    if role.endswith("-txt"):
        return cover_txt(path, data)
    raise CharacterByteCoverageError(f"{path}: no byte-ledger walker for role {role}")


def cover_closure(members) -> list[dict[str, Any]]:
    vtx_data = None
    for member in members:
        if member.role == "vtx-dx80":
            vtx_data = member.data
            break
        if member.role.startswith("vtx-") and vtx_data is None:
            vtx_data = member.data
    return [
        cover_member(
            member.role,
            member.path,
            member.data,
            vtx_data=vtx_data if member.role == "mdl" else None,
        )
        for member in members
    ]
