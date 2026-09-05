"""Whole-model semantic decode: one v2531 MDL, its VTX pair and its PHY.

The decode is the same walk for every `models/**.mdl`. A shared animation bank simply has no
sequence of its own to decode, a static prop has one bone and no local animation, and a character
body fills every section; none of them is the other's special case.
"""

from __future__ import annotations

from collections import Counter
import hashlib
import math
import re
import struct
from typing import Any, Callable, Iterable

from elysium_pipeline.formats import mdl, mdl_cloth, mdl_secondary_motion, mdl_skel
from elysium_pipeline.formats.model_glb import coverage, expressions, physics, vtx
from elysium_pipeline.formats.model_glb.model import (
    PARTICLE_EVENTS,
    SOUND_EVENTS,
    SPLIT_ROTATION_FLAG,
    STATIC_PROP_FLAG,
    ModelUnit,
    family_of,
    normalize_model_key,
    plain,
    shape_of,
)
from elysium_pipeline.formats.model_glb.source import ModelSourceClosure
from elysium_pipeline.formats.unit_contract import asset_id, dependency, missing_sentinel


class ModelDecodeError(RuntimeError):
    """A source field cannot be represented by the complete model unit."""


#: The `$surfaceprop` a material may state, read from the VMT text the model's search paths found.
_SURFACE_PROPERTY = re.compile(
    rb'"?\$surfaceprop"?\s+"?([A-Za-z0-9_./\\-]+)"?', re.IGNORECASE
)

_FLEX_OP_CODES = {
    "CONST": 1, "FETCH1": 2, "FETCH2": 3, "ADD": 4, "SUB": 5, "MUL": 6, "DIV": 7,
    "NEG": 8, "EXP": 9, "OPEN": 10, "CLOSE": 11, "COMMA": 12, "MAX": 13, "MIN": 14,
}


def _i32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<i", data, offset)[0]


def _u32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<I", data, offset)[0]


def _f32(data: bytes, offset: int) -> float:
    return struct.unpack_from("<f", data, offset)[0]


def _vec3(data: bytes, offset: int) -> tuple[float, float, float]:
    return struct.unpack_from("<3f", data, offset)


def _cstr(data: bytes, offset: int, *, limit: int | None = None) -> str:
    end = data.find(b"\0", offset, limit)
    if end < 0:
        raise ModelDecodeError(f"string at {offset} has no terminator")
    return data[offset:end].decode("ascii", "replace")


#: `(name, countOffset, offsetOffset)` for every table the studio header directory declares.
_TABLE_PAIRS = (
    ("bones", 240, 244),
    ("boneControllers", 248, 252),
    ("hitBoxSets", 256, 260),
    ("localAnimations", 264, 268),
    ("localSequences", 272, 276),
    ("sequenceGroups", 284, 288),
    ("textures", 292, 296),
    ("textureSearchPaths", 300, 304),
    ("skin", 308, 316),
    ("bodyParts", 320, 324),
    ("localAttachments", 328, 332),
    ("transitions", 336, 340),
    ("flexDescriptions", 344, 348),
    ("flexControllers", 352, 356),
    ("flexRules", 360, 364),
    ("ikChains", 368, 372),
    ("mouths", 376, 380),
    ("localPoseParameters", 384, 388),
    ("secondaryMotion", 396, 400),
    ("includeModels", 404, 408),
)


def _flags(value: int) -> dict[str, Any]:
    """The header's `Flags`@228 as stored, with the one bit the format documents named."""

    return {"value": int(value), "staticProp": bool(int(value) & STATIC_PROP_FLAG)}


def decode_header(data: bytes) -> dict[str, Any]:
    """Every studio header field and the table directory, at the offsets they were read from."""

    if len(data) < 424:
        raise ModelDecodeError(f"MDL is only {len(data)} bytes; the header is 424")
    if data[:4] != b"IDST":
        raise ModelDecodeError(f"bad MDL id {data[:4]!r}")
    version = _i32(data, 4)
    if version != 2531:
        raise ModelDecodeError(f"MDL version {version}, expected 2531")
    declared_length = _i32(data, 140)
    if declared_length > len(data) or declared_length < 424:
        raise ModelDecodeError(
            f"MDL header length {declared_length} outside the {len(data)}-byte source"
        )
    trailing = data[declared_length:]
    if trailing and any(byte not in b"\t\n\r " for byte in trailing):
        raise ModelDecodeError(
            f"MDL has {len(trailing)} non-whitespace bytes after declared image length "
            f"{declared_length}"
        )
    tables = {
        name: {"count": _i32(data, count_at), "offset": _i32(data, offset_at)}
        for name, count_at, offset_at in _TABLE_PAIRS
    }
    tables["skin"]["families"] = _i32(data, 312)
    surface_offset = _i32(data, 392)
    return {
        "id": "IDST",
        "version": version,
        "checksum": _u32(data, 8),
        "name": _cstr(data, 12, limit=140),
        "length": declared_length,
        "physicalLength": len(data),
        "trailingPatchWhitespace": trailing.hex(),
        "globalScale": _vec3(data, 144),
        "eyePosition": _vec3(data, 156),
        "illumPosition": _vec3(data, 168),
        "hullMin": _vec3(data, 180),
        "hullMax": _vec3(data, 192),
        "viewBoundsMin": _vec3(data, 204),
        "viewBoundsMax": _vec3(data, 216),
        "flags": _flags(_i32(data, 228)),
        "phonemeFilter": struct.unpack_from("<2f", data, 232),
        "sequencesIndexed": _i32(data, 280),
        "surfacePropertyOffset": surface_offset,
        "surfaceProperty": _cstr(data, surface_offset) if surface_offset > 0 else "",
        "reserved412": _i32(data, 412),
        "reserved416": _i32(data, 416),
        "contents": _i32(data, 420),
        "tables": tables,
        "compilerTrailer": coverage.compiler_trailer(data),
    }


def _bones(data: bytes) -> list[dict[str, Any]]:
    rows = []
    base = _i32(data, 244)
    for bone in mdl_skel.read_bones(data):
        record = base + bone.index * mdl_skel.BONE_STRIDE
        surface_offset = _i32(data, record + 152)
        rows.append(
            {
                "index": bone.index,
                "sourceOffset": record,
                "name": bone.name,
                "parent": bone.parent,
                "boneControllers": list(struct.unpack_from("<6i", data, record + 8)),
                "position": tuple(bone.pos),
                "rotation": tuple(bone.quat),
                "positionScale": tuple(bone.posscale),
                "rotationScale": tuple(bone.rotscale),
                "poseToBone": tuple(bone.pose_to_bone),
                "flags": bone.flags,
                "proceduralType": _i32(data, record + 140),
                "proceduralIndex": _i32(data, record + 144),
                "physicsBone": _i32(data, record + 148),
                "surfacePropertyOffset": surface_offset,
                "surfaceProperty": _cstr(data, record + surface_offset) if surface_offset else "",
                "contents": _i32(data, record + 156),
            }
        )
    return rows


def split_rotation_bones(bones: Iterable[dict[str, Any]]) -> list[dict[str, Any]]:
    """The bones whose MDL flag `0x2` splits rotation and translation inheritance.

    Their animation channels are written from the source unchanged, so each listed bone's rotation
    channel states a model-space orientation while its translation stays attached to the parent.
    """

    return [
        {
            "bone": bone["index"],
            "name": bone["name"],
            "rotation": "model-space",
            "translation": "parent-attached",
        }
        for bone in bones
        if int(bone.get("flags", 0)) & SPLIT_ROTATION_FLAG
    ]


def _body_parts(
    data: bytes, anorms, referenced: dict[tuple[int, int], set[int]]
) -> list[dict[str, Any]]:
    """Every MDL body part, model and mesh, and the VTMB-only half of the vertex pool.

    A drawn vertex's position, normal, tangent, UV, joints and weights are the core
    attributes of the primitive that reaches it, so the record here keeps only what core
    glTF cannot say: the pool index, the stored weight bytes, the bone slots and the
    influence selector. A pool entry no strip group reaches has no core vertex at all, so
    it is listed once, in full, under `unreferencedVertices`.
    """

    rows = []
    bodypart_count = _i32(data, 320)
    bodypart_base = _i32(data, 324)
    for bodypart_index in range(bodypart_count):
        bodypart = bodypart_base + bodypart_index * 16
        name_offset, model_count, base_value, model_offset = struct.unpack_from(
            "<4i", data, bodypart
        )
        models = []
        for model_index in range(model_count):
            model = bodypart + model_offset + model_index * mdl_skel.MODEL_STRIDE
            mesh_count = _i32(data, model + 136)
            mesh_offset = _i32(data, model + 140)
            vertex_count = _i32(data, model + 144)
            vertex_offset = _i32(data, model + 148)
            tangent_offset = _i32(data, model + 152)
            vertex_list = _i32(data, model + 156)
            stride = mdl.VSTRIDE.get(vertex_list)
            if stride is None:
                raise ModelDecodeError(
                    f"body part {bodypart_index} model {model_index} has vertex list {vertex_list}"
                )
            position_offset = _vec3(data, model + 160)
            position_scale = _vec3(data, model + 172)
            skins = mdl_skel.read_skin(data, model, vertex_offset, vertex_count, vertex_list)
            tangent_base = model + tangent_offset if tangent_offset > 0 else None
            if tangent_base is not None and tangent_base + vertex_count * 16 > len(data):
                raise ModelDecodeError(
                    f"body part {bodypart_index} model {model_index} tangents overrun the MDL"
                )
            drawn = referenced.get((bodypart_index, model_index), frozenset())
            vertices = []
            unreferenced = []
            for vertex_index in range(vertex_count):
                vertex = model + vertex_offset + vertex_index * stride
                vertices.append(
                    {
                        "index": vertex_index,
                        "storedWeights": (
                            list(data[vertex:vertex + 3]) if vertex_list == 0 else []
                        ),
                        "boneSlots": (
                            list(struct.unpack_from("<4h", data, vertex + 4))
                            if vertex_list == 0
                            else [0]
                        ),
                        "influenceSelector": data[vertex + 3] if vertex_list == 0 else None,
                    }
                )
                if vertex_index in drawn:
                    continue
                x, y, z, u, texture_v = mdl._read_vertex(
                    data, vertex, vertex_list, position_offset, position_scale
                )
                bone_indices, bone_weights = skins[vertex_index]
                unreferenced.append(
                    {
                        "index": vertex_index,
                        "position": (x, y, z),
                        "normal": vtx._normal(data, vertex, vertex_list, anorms),
                        "tangent": (
                            struct.unpack_from("<4f", data, tangent_base + vertex_index * 16)
                            if tangent_base is not None
                            else None
                        ),
                        "uv": (u, texture_v),
                        "bones": bone_indices,
                        "weights": bone_weights,
                    }
                )
            meshes = []
            for mesh_index in range(mesh_count):
                mesh = model + mesh_offset + mesh_index * mdl_skel.MESH_STRIDE
                meshes.append(
                    {
                        "index": mesh_index,
                        "sourceOffset": mesh,
                        "material": _i32(data, mesh),
                        "modelOffset": _i32(data, mesh + 4),
                        "vertexCount": _i32(data, mesh + 8),
                        "vertexOffset": _i32(data, mesh + 12),
                        "flexes": plain(mdl_skel.mesh_flexes(data, model, mesh_index)),
                        "materialType": _i32(data, mesh + 24),
                        "materialParam": _i32(data, mesh + 28),
                        "meshId": _i32(data, mesh + 32),
                        "center": _vec3(data, mesh + 36),
                        "clothSelectorOffset": _i32(data, mesh + 48),
                        "clothPositionNormalOffset": _i32(data, mesh + 52),
                        "clothTangentOffset": _i32(data, mesh + 56),
                    }
                )
            eyeballs = plain(mdl_skel.eyeballs(data, model))
            eyeball_base = model + _i32(data, model + 196)
            for eyeball_index, eyeball in enumerate(eyeballs):
                record = eyeball_base + eyeball_index * mdl_skel.EYEBALL_STRIDE
                eyeball_name_offset = _i32(data, record)
                eyeball["nameOffset"] = eyeball_name_offset
                eyeball["name"] = (
                    _cstr(data, record + eyeball_name_offset) if eyeball_name_offset else ""
                )
                eyeball["reserved124"] = list(struct.unpack_from("<4i", data, record + 124))
            models.append(
                {
                    "index": model_index,
                    "sourceOffset": model,
                    "name": _cstr(data, model, limit=model + 128),
                    "type": _i32(data, model + 128),
                    "boundingRadius": _f32(data, model + 132),
                    "meshCount": mesh_count,
                    "meshOffset": mesh_offset,
                    "vertexCount": vertex_count,
                    "vertexOffset": vertex_offset,
                    "tangentsOffset": tangent_offset,
                    "vertexListType": vertex_list,
                    "positionOffset": position_offset,
                    "positionScale": position_scale,
                    "reserved184": list(struct.unpack_from("<2i", data, model + 184)),
                    "eyeballs": eyeballs,
                    "clothDefinitionCount": _i32(data, model + 200),
                    "clothDefinitionOffset": _i32(data, model + 204),
                    "clothCapsuleCount": _i32(data, model + 208),
                    "clothCapsuleOffset": _i32(data, model + 212),
                    "clothSphereCount": _i32(data, model + 216),
                    "clothSphereOffset": _i32(data, model + 220),
                    "meshes": meshes,
                    "vertices": vertices,
                    "unreferencedVertices": unreferenced,
                }
            )
        rows.append(
            {
                "index": bodypart_index,
                "sourceOffset": bodypart,
                "name": _cstr(data, bodypart + name_offset),
                "base": base_value,
                "models": models,
            }
        )
    return rows


def _texture_records(data: bytes) -> list[dict[str, Any]]:
    count, base = _i32(data, 292), _i32(data, 296)
    if count < 0 or base < 0 or base + count * 20 > len(data):
        raise ModelDecodeError(f"invalid texture declaration count={count}, offset={base}")
    rows = []
    for index in range(count):
        record = base + index * 20
        name_offset, flags = struct.unpack_from("<2i", data, record)
        width, height, max_world_units_per_texel = struct.unpack_from("<3f", data, record + 8)
        rows.append(
            {
                "index": index,
                "sourceOffset": record,
                "name": _cstr(data, record + name_offset),
                "flags": flags,
                "width": width,
                "height": height,
                "maxWorldUnitsPerTexel": max_world_units_per_texel,
            }
        )
    return rows


def _sequence_groups(data: bytes) -> list[dict[str, Any]]:
    count, base = _i32(data, 284), _i32(data, 288)
    if count < 0 or base < 0 or base + count * 16 > len(data):
        raise ModelDecodeError(f"invalid sequence-group declaration count={count}, offset={base}")
    rows = []
    for index in range(count):
        record = base + index * 16
        label_offset, name_offset, cache, data_pointer = struct.unpack_from("<4i", data, record)
        rows.append(
            {
                "index": index,
                "sourceOffset": record,
                "label": _cstr(data, record + label_offset) if label_offset else "",
                "name": _cstr(data, record + name_offset) if name_offset else "",
                "cachePointer": cache,
                "dataPointer": data_pointer,
            }
        )
    return rows


def _bone_controllers(data: bytes) -> list[dict[str, Any]]:
    count, base = _i32(data, 248), _i32(data, 252)
    if count < 0 or base < 0 or base + count * 24 > len(data):
        raise ModelDecodeError(f"invalid bone-controller declaration count={count}, offset={base}")
    return [
        {
            "index": index,
            "sourceOffset": base + index * 24,
            "bone": _i32(data, base + index * 24),
            "type": _i32(data, base + index * 24 + 4),
            "start": _f32(data, base + index * 24 + 8),
            "end": _f32(data, base + index * 24 + 12),
            "rest": _i32(data, base + index * 24 + 16),
            "inputField": _i32(data, base + index * 24 + 20),
        }
        for index in range(count)
    ]


def _transition_graph(data: bytes) -> dict[str, Any]:
    count, base = _i32(data, 336), _i32(data, 340)
    if count < 0 or base < 0 or base + count * count > len(data):
        raise ModelDecodeError(f"invalid transition graph count={count}, offset={base}")
    return {
        "nodeCount": count,
        "sourceOffset": base,
        "matrix": [
            list(data[base + row * count:base + (row + 1) * count]) for row in range(count)
        ],
    }


def _include_models(data: bytes, bones: list[dict[str, Any]]) -> list[dict[str, Any]]:
    count, base = _i32(data, 404), _i32(data, 408)
    stride = 116
    if count < 0 or base < 0 or base + count * stride > len(data):
        raise ModelDecodeError(f"invalid include-model declaration count={count}, offset={base}")
    rows = []
    for index in range(count):
        record = base + index * stride
        filename_offset = _i32(data, record)
        remap_offset = _i32(data, record + 16)
        remap_base = record + remap_offset
        remap_length = len(bones) * 56
        if remap_offset <= 0 or remap_base < 0 or remap_base + remap_length > len(data):
            raise ModelDecodeError(f"include model {index} remap runs outside the MDL")
        remap = []
        for bone_index in range(len(bones)):
            item = remap_base + bone_index * 56
            source_bone = struct.unpack_from("<h", data, item)[0]
            parent_bone, common_ancestor = struct.unpack_from("<2h", data, item + 4)
            remap.append(
                {
                    "bone": bone_index,
                    "boneName": bones[bone_index]["name"],
                    "sourceBone": source_bone,
                    "branchSelector": data[item + 2],
                    "positionTransform": data[item + 3],
                    "parentBone": parent_bone,
                    "commonAncestor": common_ancestor,
                    "matrix": list(struct.unpack_from("<12f", data, item + 8)),
                }
            )
        path = _cstr(data, record + filename_offset).replace("\\", "/")
        if path and not path.lower().startswith("models/"):
            path = "models/" + path
        rows.append(
            {
                "index": index,
                "includeIndex": index,
                "sourceOffset": record,
                "path": path,
                "loadedModelPointer": _i32(data, record + 4),
                "virtualSequenceBase": _i32(data, record + 8),
                "virtualSequenceCount": _i32(data, record + 12),
                "remapOffset": remap_offset,
                "globalToLocalPoseParameters": list(struct.unpack_from("<24h", data, record + 20)),
                "localToGlobalPoseParameters": list(struct.unpack_from("<24h", data, record + 68)),
                "boneRemap": remap,
            }
        )
    return rows


def _ik_chains(data: bytes, bones: list[dict[str, Any]]) -> list[dict[str, Any]]:
    count, base = _i32(data, 368), _i32(data, 372)
    if count < 0 or base < 0 or base + count * 16 > len(data):
        raise ModelDecodeError(f"invalid IK chain declaration count={count}, offset={base}")
    chains = []
    for index in range(count):
        record = base + index * 16
        name_offset, link_type, link_count, link_offset = struct.unpack_from("<4i", data, record)
        links_base = record + link_offset
        if link_count < 0 or links_base < 0 or links_base + link_count * 28 > len(data):
            raise ModelDecodeError(f"IK chain {index} link array runs outside the MDL")
        links = []
        for link_index in range(link_count):
            link = links_base + link_index * 28
            bone = _i32(data, link)
            if not 0 <= bone < len(bones):
                raise ModelDecodeError(
                    f"IK chain {index} link {link_index} names bone {bone} of {len(bones)}"
                )
            links.append(
                {
                    "bone": bone,
                    "boneName": bones[bone]["name"],
                    "kneeDirection": _vec3(data, link + 4),
                    "reserved": _vec3(data, link + 16),
                }
            )
        chains.append(
            {
                "index": index,
                "sourceOffset": record,
                "name": _cstr(data, record + name_offset),
                "linkType": link_type,
                "links": links,
            }
        )
    return chains


def _hitbox_sets(data: bytes, bones: list[dict[str, Any]]) -> list[dict[str, Any]]:
    """VtMB's compact 12-byte set headers and 32-byte bone-local boxes."""

    count, base = _i32(data, 256), _i32(data, 260)
    if count < 0 or base < 0 or base + count * 12 > len(data):
        raise ModelDecodeError(f"invalid hitbox-set declaration count={count}, offset={base}")
    sets = []
    for set_index in range(count):
        record = base + set_index * 12
        name_offset, box_count, box_offset = struct.unpack_from("<3i", data, record)
        boxes_base = record + box_offset
        if box_count < 0 or boxes_base < 0 or boxes_base + box_count * 32 > len(data):
            raise ModelDecodeError(f"hitbox set {set_index} box array runs outside the MDL")
        boxes = []
        for box_index in range(box_count):
            box = boxes_base + box_index * 32
            bone, group = struct.unpack_from("<2i", data, box)
            if not 0 <= bone < len(bones):
                raise ModelDecodeError(
                    f"hitbox set {set_index} box {box_index} names bone {bone}"
                )
            boxes.append(
                {
                    "index": box_index,
                    "sourceOffset": box,
                    "bone": bone,
                    "boneName": bones[bone]["name"],
                    "group": group,
                    "boundsMin": _vec3(data, box + 8),
                    "boundsMax": _vec3(data, box + 20),
                }
            )
        sets.append(
            {
                "index": set_index,
                "sourceOffset": record,
                "name": _cstr(data, record + name_offset) if name_offset > 0 else "",
                "hitboxes": boxes,
            }
        )
    return sets


def _payload_bound_after(data: bytes, after: int) -> int:
    bounds = [len(data)]
    trailer = coverage.compiler_trailer(data)
    if trailer is not None and trailer["offset"] > after:
        bounds.append(trailer["offset"])
    for offset_at in (268, 276, 288, 296, 304, 316, 324, 332, 340, 348, 356, 364,
                      372, 380, 388, 400, 408):
        offset = _i32(data, offset_at)
        if offset > after:
            bounds.append(offset)
    search_count, search_base = _i32(data, 300), _i32(data, 304)
    if search_count > 0 and 0 <= search_base < len(data):
        for index in range(search_count):
            pointer = _i32(data, search_base + index * 4)
            if pointer > after:
                bounds.append(pointer)
    return min(bounds)


def _unindexed_animation_tracks(
    data: bytes, records: int, bone_count: int, frames: int
) -> list[dict[str, Any]]:
    """RLE tracks that follow the declared channels and no bone record points at."""

    last_end = records + bone_count * 32
    for bone in range(bone_count):
        record = records + bone * 32
        for relative in struct.unpack_from("<7i", data, record + 4):
            if not relative:
                continue
            start = record + relative
            last_end = max(last_end, start + coverage.mdl_rle_length(data, start, frames))
    limit = _payload_bound_after(data, last_end)
    tracks = []
    offset = last_end
    extra = 0
    while offset < limit:
        length = coverage.rle_exact_track_length(data, offset, limit, frames)
        if length <= 0:
            break
        tracks.append(
            {
                "index": extra,
                "sourceOffset": offset,
                "byteLength": length,
                "samples": mdl_skel._rle_channel(data, offset, frames),
            }
        )
        extra += 1
        offset += length
    return tracks


def _local_animations(data: bytes) -> list[dict[str, Any]]:
    animations = []
    bone_count = _i32(data, 240)
    for index in range(_i32(data, 264)):
        found = mdl_skel.local_animation(data, index)
        if found is None:
            raise ModelDecodeError(f"local animation {index} did not resolve")
        name, base, frames, fps = found
        records = base + _i32(data, base + 48)
        bone_weights = []
        channel_offsets = []
        for bone in range(bone_count):
            record = records + bone * 32
            bone_weights.append(_f32(data, record))
            channel_offsets.append(list(struct.unpack_from("<7i", data, record + 4)))
        animations.append(
            {
                "index": index,
                "name": name,
                "sourceOffset": base,
                "frameCount": frames,
                "fps": fps,
                "flags": _i32(data, base + 8),
                "boundsMin": _vec3(data, base + 24),
                "boundsMax": _vec3(data, base + 36),
                "animationDataOffset": _i32(data, base + 48),
                "ikRuleCount": _i32(data, base + 52),
                "ikRuleOffset": _i32(data, base + 56),
                "trailing": list(struct.unpack_from("<3i", data, base + 60)),
                "boneWeights": bone_weights,
                "channelOffsets": channel_offsets,
                "unindexedTracks": _unindexed_animation_tracks(
                    data, records, bone_count, frames
                ),
                "movement": plain(mdl_skel.read_movements(data, base)),
            }
        )
    return animations


def _swing_records(data: bytes, descriptor: int, bone_names: list[str]) -> list[dict[str, Any]]:
    rows = plain(mdl_skel.read_swing_records(data, descriptor, bone_names))
    count = _i32(data, descriptor + 708)
    relative = _i32(data, descriptor + 712)
    if not rows or count != len(rows) or relative <= 0:
        return rows
    base = descriptor + relative
    for index, row in enumerate(rows):
        record = base + index * 188
        unidentified = row.pop("unidentified")
        row["sourceOffset"] = record
        row["kickOnlyMarker"] = unidentified[0]
        row["candidateCounts"] = unidentified[1:5]
        row["resolvedKnockbackActivities"] = unidentified[5:21]
        row["knockbackNameOffsets"] = list(struct.unpack_from("<16i", data, record + 0x78))
        row["bucket0LowHeightMarker"] = data[record + 0xB9]
        row["reservedBB"] = data[record + 0xBB]
    return rows


def _local_sequences(data: bytes) -> list[dict[str, Any]]:
    """Every descriptor positionally, including duplicate labels and invalid base cells."""

    count = _i32(data, 272)
    base = _i32(data, 276)
    animation_count = _i32(data, 264)
    animation_base = _i32(data, 268)
    labels = mdl_skel.local_sequence_labels(data)
    activities = mdl_skel.local_sequence_activities(data)
    bone_names = mdl_skel.bone_names(data)
    rows = []
    for index in range(count):
        descriptor = base + index * mdl_skel._SEQDESC_STRIDE
        grid = mdl_skel.read_grid(data, descriptor)
        base_animation = grid.cells[0].anim if grid.cells else -1
        animation_descriptor = (
            animation_base + base_animation * 72
            if 0 <= base_animation < animation_count
            else None
        )
        rows.append(
            {
                "index": index,
                "sourceOffset": descriptor,
                "labelOffset": _i32(data, descriptor),
                "label": labels[index],
                "activityNameOffset": _i32(data, descriptor + 4),
                "activity": activities[index],
                "flags": _i32(data, descriptor + 8),
                "resolvedActivity": _i32(data, descriptor + 12),
                "activityWeight": _i32(data, descriptor + 16),
                "eventCount": _i32(data, descriptor + 20),
                "eventOffset": _i32(data, descriptor + 24),
                "boundsMin": _vec3(data, descriptor + 28),
                "boundsMax": _vec3(data, descriptor + 40),
                "blendCount": _i32(data, descriptor + 52),
                "animationTable": list(struct.unpack_from("<256h", data, descriptor + 56)),
                "movementIndex": _i32(data, descriptor + 568),
                "grid": plain(grid),
                "baseAnimation": base_animation,
                "groupSize": list(struct.unpack_from("<2i", data, descriptor + 572)),
                "parameterIndexes": list(struct.unpack_from("<2i", data, descriptor + 580)),
                "parameterStarts": list(struct.unpack_from("<2f", data, descriptor + 588)),
                "parameterEnds": list(struct.unpack_from("<2f", data, descriptor + 596)),
                "parameterParent": _i32(data, descriptor + 604),
                "sequenceGroup": _i32(data, descriptor + 608),
                "fade": list(struct.unpack_from("<3f", data, descriptor + 612)),
                "entryNode": _i32(data, descriptor + 624),
                "exitNode": _i32(data, descriptor + 628),
                "nodeFlags": _i32(data, descriptor + 632),
                "entryPhase": _f32(data, descriptor + 636),
                "exitPhase": _f32(data, descriptor + 640),
                "lastFrame": _f32(data, descriptor + 644),
                "nextSequence": _i32(data, descriptor + 648),
                "pose": _i32(data, descriptor + 652),
                "ikRuleCount": _i32(data, descriptor + 656),
                "autolayerCount": _i32(data, descriptor + 660),
                "autolayerOffset": _i32(data, descriptor + 664),
                "weightListOffset": _i32(data, descriptor + 668),
                "secondaryBoundsMin": _vec3(data, descriptor + 672),
                "secondaryBoundsMax": _vec3(data, descriptor + 684),
                "statGate": _i32(data, descriptor + 696),
                "envelopeCount": _i32(data, descriptor + 700),
                "envelopeOffset": _i32(data, descriptor + 704),
                "swingCount": _i32(data, descriptor + 708),
                "swingOffset": _i32(data, descriptor + 712),
                "lowReachRaw": _f32(data, descriptor + 716),
                "reachRaw": _f32(data, descriptor + 720),
                "attackButtonMask": _i32(data, descriptor + 724),
                "resolvedDodgeActivity": _i32(data, descriptor + 728),
                "dodgeActivityNameOffset": _i32(data, descriptor + 732),
                "resolvedBlockedActivity": _i32(data, descriptor + 736),
                "blockedActivityNameOffset": _i32(data, descriptor + 740),
                "chainNameOffset": _i32(data, descriptor + 744),
                "alternateChainNameOffset": _i32(data, descriptor + 748),
                "comboWindow": list(struct.unpack_from("<3f", data, descriptor + 752)),
                "autolayers": [
                    {"index": layer, "label": labels[layer]}
                    for layer in mdl_skel.read_autolayers(data, descriptor, count)
                ],
                "events": plain(mdl_skel.read_events(data, descriptor)),
                "reach": mdl_skel.read_reach(data, descriptor),
                "lowReach": mdl_skel.read_low_reach(data, descriptor),
                "envelopes": plain(mdl_skel.read_envelopes(data, descriptor)),
                "blockedReaction": mdl_skel.read_blocked_reaction(data, descriptor),
                "swings": _swing_records(data, descriptor, bone_names),
                "combo": plain(mdl_skel.read_combo_chain(data, descriptor)),
                "movement": (
                    plain(mdl_skel.read_movements(data, animation_descriptor))
                    if animation_descriptor is not None
                    else None
                ),
            }
        )
    return rows


def flex_operation(operation: int | str, integer: int, floating: float) -> dict[str, Any]:
    """Project only the opcode-selected union arm while retaining its exact bits."""

    if isinstance(operation, str):
        if operation in _FLEX_OP_CODES:
            code, name = _FLEX_OP_CODES[operation], operation
        elif operation.startswith("op"):
            code, name = int(operation[2:]), operation
        else:
            raise ModelDecodeError(f"unsupported flex operation {operation}")
    else:
        code = int(operation)
        name = next(
            (label for label, value in _FLEX_OP_CODES.items() if value == code), f"op{code}"
        )
    row: dict[str, Any] = {
        "operation": name,
        "operationCode": code,
        "rawOperandBits": integer & 0xFFFFFFFF,
    }
    if code == 1:
        row.update(operandKind="constant", value=floating)
    elif code == 2:
        row.update(operandKind="flexControllerIndex", index=integer)
    elif code == 3:
        row.update(operandKind="flexDescriptionIndex", index=integer)
    elif code in _FLEX_OP_CODES.values():
        row["operandKind"] = "unused"
    else:
        raise ModelDecodeError(f"unsupported flex operation {operation}")
    return row


def _with_source_offsets(
    rows: list[dict[str, Any]], offsets: list[int]
) -> list[dict[str, Any]]:
    """Put each record's own file offset first in the record the writer publishes."""

    if len(rows) != len(offsets):
        raise ModelDecodeError(
            f"{len(rows)} decoded record(s) against {len(offsets)} declared offsets"
        )
    return [{"sourceOffset": offset, **row} for offset, row in zip(offsets, rows)]


def _table_offsets(data: bytes, count_at: int, offset_at: int, stride: int) -> list[int]:
    """Where each record of one header-declared table begins, in file coordinates.

    A record read from an offset keeps that offset, so the ledger range that paid for it and the
    record it paid for name the same place.
    """

    count, base = _i32(data, count_at), _i32(data, offset_at)
    return [base + index * stride for index in range(max(count, 0))]


def _facial(data: bytes) -> dict[str, Any]:
    controllers = _table_offsets(data, 352, 356, 20)
    rules = _table_offsets(data, 360, 364, 12)
    mouths = _table_offsets(data, 376, 380, 20)
    return {
        "flexDescriptions": mdl_skel.flex_descs(data),
        "controllers": [
            {
                "sourceOffset": controllers[index],
                "type": row[0],
                "name": row[1],
                "min": row[2],
                "max": row[3],
            }
            for index, row in enumerate(mdl_skel.flex_controllers(data))
        ],
        "rules": [
            {
                "sourceOffset": rules[index],
                "flexDescription": flex,
                "operations": [
                    flex_operation(operation, integer, floating)
                    for operation, integer, floating in operations
                ],
            }
            for index, (flex, operations) in enumerate(mdl_skel.flex_rules(data))
        ],
        "mouths": [
            {
                "sourceOffset": mouths[index],
                "bone": bone,
                "forward": forward,
                "flexDescription": flex,
            }
            for index, (bone, forward, flex) in enumerate(mdl_skel.mouths(data))
        ],
        # The phoneme cross-fade pair at `+232` is `mdl.header.phonemeFilter`; restating it here
        # would state one datum twice, and would make a prop with no rig look as though it
        # carried facial data.
    }


def _unrendered_morph_records(data, body_parts, lods, anorms):
    """The flex records on a source mesh with no LOD0 primitive still belong to the unit."""
    rendered = {(p["bodyPart"], p["model"], p["mesh"])
                for lod in lods if lod["index"] == 0 for p in lod["primitives"]}
    for part in body_parts:
        for model in part["models"]:
            for mesh in model["meshes"]:
                if (part["index"], model["index"], mesh["index"]) in rendered:
                    continue
                rows = []
                for i, flex in enumerate(mesh["flexes"]):
                    if flex["numverts"] and anorms is None:
                        raise ModelDecodeError("unrendered facial records require the StudioRender unit-vector table")
                    for local, position, normal in mdl_skel.vert_anims(data, flex, anorms):
                        rows.append({"flex": i, "sourceVertex": mesh["vertexOffset"] + local,
                                     "position": tuple(position), "normal": tuple(normal)})
                if rows:
                    mesh["unrenderedMorphRecords"] = rows


def _morph_targets(data: bytes, lods: list[dict[str, Any]], anorms) -> list[dict[str, Any]]:
    """Attach every MDL flex record to its LOD 0 primitive by source vertex identity."""

    flex_descriptions = mdl_skel.flex_descs(data)
    if not flex_descriptions or not lods:
        return []
    if anorms is None:
        raise ModelDecodeError(
            "facial morphs require the installed StudioRender unit-vector table"
        )

    slots: list[tuple[int, tuple[float, ...]]] = []
    slot_of: dict[tuple[int, tuple[float, ...]], int] = {}
    per_primitive: list[dict[int, dict[int, tuple[tuple[float, ...], tuple[float, ...]]]]] = []
    for primitive in lods[0]["primitives"]:
        morph_records = []
        source_to_local = {
            source: index for index, source in enumerate(primitive["sourceVertices"])
        }
        targets: dict[int, dict[int, tuple[tuple[float, ...], tuple[float, ...]]]] = {}
        for flex_index, flex in enumerate(mdl_skel.mesh_flexes(data, primitive["modelBase"], primitive["mesh"])):
            key = (flex["flexdesc"], tuple(float(value) for value in flex["targets"]))
            if key not in slot_of:
                slot_of[key] = len(slots)
                slots.append(key)
            bucket = targets.setdefault(slot_of[key], {})
            for local, position, normal in mdl_skel.vert_anims(data, flex, anorms):
                position = tuple(float(value) for value in position or (0.0, 0.0, 0.0))
                normal = tuple(float(value) for value in normal or (0.0, 0.0, 0.0))
                vertex = source_to_local.get(primitive["vertexOffset"] + local)
                # Dense glTF cannot distinguish an authored zero from an absent record, or
                # represent a flex on an undrawn vertex. Preserve the ordered source records
                # beside the render projection, including repeated contributions to one target.
                morph_records.append({
                    "flex": flex_index, "target": slot_of[key], "sourceVertex": primitive["vertexOffset"] + local,
                    "vertex": vertex, "position": position, "normal": normal})
                if vertex is None:
                    continue
                prior_position, prior_normal = bucket.get(vertex, ((0., 0., 0.), (0., 0., 0.)))
                bucket[vertex] = (tuple(a + b for a, b in zip(prior_position, position)),
                                  tuple(a + b for a, b in zip(prior_normal, normal)))
        primitive["morphRecords"] = morph_records
        per_primitive.append(targets)

    seen: Counter[str] = Counter()
    rows = []
    for slot, (flex, ramp) in enumerate(slots):
        base_name = (
            flex_descriptions[flex] if 0 <= flex < len(flex_descriptions) else f"flex{flex}"
        )
        name = base_name if seen[base_name] == 0 else f"{base_name}#{seen[base_name]}"
        seen[base_name] += 1
        rows.append(
            {"index": slot, "name": name, "flexDescription": flex, "targets": list(ramp)}
        )
    for primitive, targets in zip(lods[0]["primitives"], per_primitive):
        vertex_count = len(primitive["positions"])
        dense = []
        for row in rows:
            position = [(0.0, 0.0, 0.0)] * vertex_count
            normal = [(0.0, 0.0, 0.0)] * vertex_count
            for vertex, values in targets.get(row["index"], {}).items():
                position[vertex], normal[vertex] = values
            dense.append({"position": position, "normal": normal})
        primitive["morphTargets"] = dense
    return rows


def _cloth_source_records(data: bytes, vtx_data: bytes | None) -> list[dict[str, Any]]:
    records = []
    bones = mdl_skel.read_bones(data)
    bodypart_count = _i32(data, 320)
    bodypart_base = _i32(data, 324)
    secondary_count = _i32(data, 396)
    secondary_base = _i32(data, 400)
    for bodypart_index in range(bodypart_count):
        bodypart = bodypart_base + bodypart_index * 16
        model_count = _i32(data, bodypart + 4)
        model_offset = _i32(data, bodypart + 12)
        for model_index in range(model_count):
            model = bodypart + model_offset + model_index * mdl_skel.MODEL_STRIDE
            _table, lod_rows, columns, definition_offsets = mdl_cloth.definition_table(data, model)
            if columns <= 0:
                continue
            definitions = []
            for lod in range(lod_rows):
                for definition in range(columns):
                    record = definition_offsets[lod * columns + definition]
                    decoded = plain(mdl_cloth.read_definition(data, model, definition, lod=lod))
                    tangent_edges = _i32(data, record + 56)
                    normal_edges = _i32(data, record + 60)
                    edge_base = record + _i32(data, record + 64)
                    # One table of `tangent_edges` pairs whose first `normal_edges` entries are
                    # the normal set; the tangent pass extends the same table.
                    edge_pairs = [
                        list(struct.unpack_from("<2H", data, edge_base + edge * 4))
                        for edge in range(tangent_edges)
                    ]
                    particles = _i32(data, record + 4)

                    def table(count_field: int, offset_field: int, layout: str,
                              stride: int, record=record) -> list[tuple]:
                        count = _i32(data, record + count_field)
                        relative = _i32(data, record + offset_field)
                        if not (count and relative):
                            return []
                        base = record + relative
                        return [
                            struct.unpack_from(layout, data, base + row * stride)
                            for row in range(count)
                        ]

                    normal_contributions = [
                        {"edgeA": row[0], "edgeB": row[1], "vertices": list(row[2:])}
                        for row in table(68, 72, "<5H", 10)
                    ]
                    tangent_interpolation = [
                        {"edgeA": row[0], "edgeB": row[1], "weightA": row[2], "weightB": row[3]}
                        for row in table(76, 80, "<2H2f", 12)
                    ]
                    distance_blocks = _i32(data, record + 36)
                    compression_blocks = _i32(data, record + 40)
                    simd_relative = _i32(data, record + 44)
                    lanes = (
                        list(
                            data[
                                record + simd_relative:
                                record + simd_relative + distance_blocks + compression_blocks
                            ]
                        )
                        if simd_relative
                        else []
                    )

                    def seeds(field: int, record=record, particles=particles) -> list[list[int]]:
                        relative = _i32(data, record + field)
                        if not relative:
                            return []
                        return [
                            list(struct.unpack_from("<4I", data, record + relative + row * 16))
                            for row in range(particles)
                        ]

                    definitions.append(
                        {
                            **decoded,
                            "sourceOffset": record,
                            "header": {
                                "distanceSimdBlockCount": distance_blocks,
                                "compressionSimdBlockCount": compression_blocks,
                                "packedSimdPayloadOffset": simd_relative,
                                "tangentEdgeCount": tangent_edges,
                                "normalEdgeCount": normal_edges,
                                "edgePairOffset": _i32(data, record + 64),
                                "normalContributionCount": _i32(data, record + 68),
                                "normalContributionOffset": _i32(data, record + 72),
                                "extraTangentOutputCount": _i32(data, record + 76),
                                "tangentInterpolationOffset": _i32(data, record + 80),
                                "optionalSeedAOffset": _i32(data, record + 84),
                                "optionalSeedBOffset": _i32(data, record + 88),
                            },
                            "edgePairs": edge_pairs,
                            "normalContributions": normal_contributions,
                            "tangentInterpolation": tangent_interpolation,
                            "simdBlockLanes": {
                                "distance": lanes[:distance_blocks],
                                "compression": lanes[distance_blocks:],
                            },
                            "optionalSeedA": seeds(84),
                            "optionalSeedB": seeds(88),
                        }
                    )

            runtime_lods = None
            if vtx_data is not None:
                runtime_lods = _vtx_model_lod_count(vtx_data, bodypart_index, model_index)
            mesh_count = _i32(data, model + 136)
            mesh_base = model + _i32(data, model + 140)
            mesh_maps = []
            physical_rows = lod_rows
            for mesh_index in range(mesh_count):
                mesh = mesh_base + mesh_index * mdl_skel.MESH_STRIDE
                try:
                    layout = mdl_cloth.map_layout(data, mesh)
                except ValueError as error:
                    raise ModelDecodeError(str(error)) from error
                if layout is None:
                    continue
                physical_rows = max(physical_rows, layout["rows"], layout["position_rows"])
                vertex_count = layout["vertex_count"]
                tangent_entries = layout["rows"] * vertex_count
                tangent_end = layout["tangent_base"] + tangent_entries * 2
                if secondary_count > 0 and layout["tangent_base"] < secondary_base < tangent_end:
                    tangent_entries = (secondary_base - layout["tangent_base"]) // 2
                lod_maps = []
                for lod in range(layout["rows"]):
                    selector_start = layout["selector_base"] + lod * vertex_count
                    position_start = layout["position_base"] + lod * vertex_count * 2
                    tangent_start = lod * vertex_count
                    tangent_count = max(0, min(vertex_count, tangent_entries - tangent_start))
                    position_count = vertex_count if lod < layout["position_rows"] else 0
                    lod_maps.append(
                        {
                            "lod": lod,
                            "runtimeIndexed": runtime_lods is not None and lod < runtime_lods,
                            "selectors": list(
                                data[selector_start:selector_start + vertex_count]
                            ),
                            "positionNormal": (
                                list(
                                    struct.unpack_from(
                                        f"<{position_count}H", data, position_start
                                    )
                                )
                                if position_count
                                else []
                            ),
                            "tangent": (
                                list(
                                    struct.unpack_from(
                                        f"<{tangent_count}H",
                                        data,
                                        layout["tangent_base"] + tangent_start * 2,
                                    )
                                )
                                if tangent_count
                                else []
                            ),
                        }
                    )
                mesh_maps.append(
                    {
                        "mesh": mesh_index,
                        "physicalLodRows": layout["rows"],
                        "positionLodRows": layout["position_rows"],
                        "runtimeLodCount": runtime_lods,
                        "lodRows": lod_maps,
                    }
                )
            capsules, spheres = mdl_cloth.read_colliders(data, model, bones)
            records.append(
                {
                    "bodyPart": bodypart_index,
                    "model": model_index,
                    "lodRows": physical_rows,
                    "definitionLodRows": lod_rows,
                    "runtimeLodCount": runtime_lods,
                    "columns": columns,
                    "definitions": definitions,
                    "meshMaps": mesh_maps,
                    "capsules": plain(capsules),
                    "spheres": plain(spheres),
                }
            )
    return records


def _vtx_model_lod_count(vtx_data: bytes, bodypart_index: int, model_index: int) -> int:
    if len(vtx_data) < 36:
        raise ModelDecodeError("VTX is too short to size cloth maps")
    bodypart_count = _i32(vtx_data, 28)
    if not 0 <= bodypart_index < bodypart_count:
        raise ModelDecodeError(f"VTX has no body part {bodypart_index} for the cloth map")
    bodypart = _i32(vtx_data, 32) + bodypart_index * 8
    model_count = _i32(vtx_data, bodypart)
    if not 0 <= model_index < model_count:
        raise ModelDecodeError(
            f"VTX body part {bodypart_index} has no model {model_index} for the cloth map"
        )
    model = bodypart + _i32(vtx_data, bodypart + 4) + model_index * 8
    lod_count = _i32(vtx_data, model)
    if lod_count <= 0:
        raise ModelDecodeError(
            f"VTX body part {bodypart_index} model {model_index} declares {lod_count} LODs"
        )
    return lod_count


def _secondary_motion_records(data: bytes) -> list[dict[str, Any]]:
    offsets = _table_offsets(data, 396, 400, 28)
    return [
        {
            "sourceOffset": offsets[index],
            "firstBone": record.first_bone,
            "terminalBone": record.terminal_bone,
            "unusedAuthoredPreset": record.unnamed_field_8,
            "gravity": record.gravity,
            "damping": record.damping,
            "springExponent": record.spring_exponent,
            "maxAngleDegrees": record.max_angle_degrees,
        }
        for index, record in enumerate(mdl_secondary_motion.read_chain_records(data))
    ]


def _axis_interpolation_rows(
    data: bytes, records: list[dict[str, Any]]
) -> list[dict[str, Any]]:
    """The procedural correction tables, keyed the way every other extension record is.

    `sourceOffset` is the record the `mdl.procedural[<bone>]` ledger range paid for, so the owner
    and the row name the same place even though the rows are indexed by array position.
    """

    bone_base = _i32(data, 244)
    rows = []
    for record in records:
        bone_record = bone_base + int(record["bone_index"]) * 160
        rows.append(
            {
                "sourceOffset": bone_record + _i32(data, bone_record + 144),
                "bone": record["bone"],
                "boneIndex": int(record["bone_index"]),
                "control": record["control"],
                "controlIndex": int(record["control_index"]),
                "axisIndex": int(record["axis_index"]),
                "pos": [list(value) for value in record["pos"]],
                "quat": [list(value) for value in record["quat"]],
            }
        )
    return rows


def _geometric_normal(positions, triangles, vertex_index: int):
    """The area-weighted normal of the faces that touch one vertex, or None."""

    accumulated = [0.0, 0.0, 0.0]
    for triangle in triangles:
        if vertex_index not in triangle:
            continue
        a, b, c = (positions[corner] for corner in triangle)
        edge_one = (b[0] - a[0], b[1] - a[1], b[2] - a[2])
        edge_two = (c[0] - a[0], c[1] - a[1], c[2] - a[2])
        accumulated[0] += edge_one[1] * edge_two[2] - edge_one[2] * edge_two[1]
        accumulated[1] += edge_one[2] * edge_two[0] - edge_one[0] * edge_two[2]
        accumulated[2] += edge_one[0] * edge_two[1] - edge_one[1] * edge_two[0]
    length = math.sqrt(sum(value * value for value in accumulated))
    if not math.isfinite(length) or length <= 1e-12:
        return None
    return tuple(value / length for value in accumulated)


def _repair_degenerate_normals(lods: list[dict[str, Any]]) -> list[dict[str, Any]]:
    """Replace a degenerate authored normal by the geometric one and name the vertex.

    `vtx.decode_lods` answers `(0, 0, 0)` where the source stored a normal that does not normalise,
    which no consumer can shade; the replacement is recorded rather than applied silently.
    """

    anomalies: list[dict[str, Any]] = []
    for lod in lods:
        for primitive in lod["primitives"]:
            normals = primitive["normals"]
            for index, normal in enumerate(normals):
                if any(abs(component) > 1e-9 for component in normal):
                    continue
                replacement = _geometric_normal(
                    primitive["positions"], primitive["triangles"], index
                )
                anomalies.append(
                    {
                        "row": "degenerate-normal",
                        "lod": lod["index"],
                        "bodyPart": primitive["bodyPart"],
                        "model": primitive["model"],
                        "mesh": primitive["mesh"],
                        "vertex": index,
                        "sourceVertex": primitive["sourceVertices"][index],
                        "replacement": "area-weighted-geometric" if replacement else "none",
                    }
                )
                if replacement is not None:
                    normals[index] = replacement
    return anomalies


def _degenerate_binds(bones: list[dict[str, Any]]) -> list[dict[str, Any]]:
    rows = []
    for bone in bones:
        quaternion = bone["rotation"]
        length = math.sqrt(sum(float(value) ** 2 for value in quaternion))
        if math.isfinite(length) and length > 1e-12:
            continue
        rows.append(
            {
                "row": "degenerate-bind",
                "bone": bone["index"],
                "name": bone["name"],
                "rotation": list(quaternion),
                "replacement": "identity",
            }
        )
    return rows


def _material_rows(
    data: bytes, index: dict, read_bytes: Callable[[dict, str], bytes | None]
) -> tuple[list[dict[str, Any]], list[list[str]], list[dict[str, Any]], set[str]]:
    """The model's material slots, its skin families, its material dependencies and surfaces."""

    names = mdl.materials(data)
    search = mdl.search_paths(data)
    rows: list[dict[str, Any]] = []
    dependencies: list[dict[str, Any]] = []
    declared: set[str] = set()
    surfaces: set[str] = set()

    def read(path: str) -> bytes | None:
        return read_bytes(index, path)

    for slot, name in enumerate(names):
        resolved, _parsed = mdl.resolve_vmt(name, search, read)
        if resolved is None:
            rows.append(
                {
                    "slot": slot,
                    "sourceName": name,
                    "sourcePath": None,
                    "material": missing_sentinel("material", f"{slot}:{mdl.sanitize(name)}"),
                    "resolved": False,
                    "candidates": [
                        mdl._norm(f"materials/{path}/{name}.vmt") for path in search
                    ],
                }
            )
            continue
        material_id = asset_id("material", resolved)
        source = "materials/" + resolved + ".vmt"
        payload = read(source)
        if payload is None:
            raise ModelDecodeError(
                f"resolved material {resolved!r} disappeared during dependency hashing"
            )
        match = _SURFACE_PROPERTY.search(payload)
        surface = match.group(1).decode("ascii", "replace").strip().lower() if match else ""
        if surface:
            surfaces.add(surface)
        rows.append(
            {
                "slot": slot,
                "sourceName": name,
                "sourcePath": source,
                "material": material_id,
                "resolved": True,
                "surfaceProperty": surface,
            }
        )
        # Two texture slots often name one VMT through different search paths; the reference is
        # declared once whatever produced it.
        if material_id not in declared:
            declared.add(material_id)
            dependencies.append(
                dependency(
                    "material",
                    material_id,
                    source,
                    True,
                    byteLength=len(payload),
                    sha256=hashlib.sha256(payload).hexdigest(),
                )
            )
    families = []
    for family in mdl.skin_table(data):
        families.append(
            [rows[texture]["material"] if 0 <= texture < len(rows) else "" for texture in family]
        )
    return rows, families, dependencies, surfaces


def _sound_reference(options: str, index: dict) -> tuple[str, str, bool]:
    """The `(key, install path, resolved)` a sound-naming event option answers to."""

    normalized = options.replace("\\", "/").strip().strip('"').lower().lstrip("/")
    if normalized.endswith((".wav", ".mp3")):
        twin = normalized[:-4] + (".mp3" if normalized.endswith(".wav") else ".wav")
        candidates = [normalized, twin]
    else:
        candidates = [normalized + ".wav", normalized + ".mp3"]
    for candidate in candidates:
        if "sound/" + candidate in index:
            return candidate, "sound/" + candidate, True
    return candidates[0], "sound/" + candidates[0], False


def _particle_reference(options: str, index: dict) -> tuple[str, str, bool]:
    stem = options.replace("\\", "/").strip().strip('"').lower().lstrip("/")
    if stem.startswith("particles/"):
        stem = stem[len("particles/"):]
    if stem.endswith(".txt"):
        stem = stem[:-4]
    path = f"particles/{stem}.txt"
    return stem, path, path in index


def _event_dependencies(sequences: list[dict[str, Any]], index: dict) -> list[dict[str, Any]]:
    """The sound and particle references the model's own animation events name.

    A reference that fails to resolve is another seam's data, so the row states `resolved: false`
    and the unit warns; it does not fail.
    """

    rows: list[dict[str, Any]] = []
    seen: set[tuple[str, str]] = set()
    for sequence in sequences:
        for event in sequence.get("events") or []:
            code = event.get("event")
            options = str(event.get("options") or "").strip()
            if not options:
                continue
            if code in SOUND_EVENTS:
                role = "sound"
                key, path, resolved = _sound_reference(options, index)
            elif code in PARTICLE_EVENTS:
                role = "particle"
                key, path, resolved = _particle_reference(options, index)
            else:
                continue
            if (role, key) in seen:
                continue
            seen.add((role, key))
            rows.append(dependency(role, asset_id(role, key), path, resolved))
    return rows


def _surface_dependencies(
    names: Iterable[str], declared: frozenset[str]
) -> list[dict[str, Any]]:
    """One row per surface the header, the bones, the PHY solids or a bound VMT name.

    `declared` empty means the install shipped no table to check against, so the reference is
    reported as resolved rather than as a miss this seam cannot substantiate.
    """

    return [
        dependency(
            "surface-property",
            asset_id("surface-property", name),
            "scripts/surfaceproperties.txt#" + name,
            not declared or name in declared,
        )
        for name in sorted({str(value).strip().lower() for value in names if str(value).strip()})
    ]


def _vtx_variant_rows(closure: ModelSourceClosure) -> list[dict[str, Any]]:
    rows = []
    for member in closure.vtx_members:
        rows.append(
            {
                "role": member.role,
                "path": member.path,
                "byteLength": member.byte_length,
                "sha256": member.sha256,
                "header": vtx.decode_header(member.data),
                "materialReplacements": vtx.decode_material_replacements(member.data),
            }
        )
    return rows


def decode_model(
    closure: ModelSourceClosure,
    index: dict,
    *,
    read_bytes: Callable[[dict, str], bytes | None] | None = None,
    anorms: list[tuple[float, float, float]] | None = None,
    surface_properties: frozenset[str] | None = None,
) -> ModelUnit:
    """Decode one complete model closure into the exporter-owned semantic unit."""

    if read_bytes is None:
        from elysium_pipeline.formats import install

        read_bytes = install.read

    data = closure.mdl.data
    header = decode_header(data)
    bones = _bones(data)
    include_models = _include_models(data, bones)
    sequences = _local_sequences(data)
    local_animations = _local_animations(data)

    anomalies: list[dict[str, Any]] = []
    omissions: list[dict[str, Any]] = []
    omitted_proven: list[dict[str, Any]] = []
    unresolved: list[dict[str, Any]] = []
    unsupported: list[dict[str, Any]] = []

    if header["physicalLength"] != header["length"]:
        anomalies.append(
            {
                "row": "header-length-mismatch",
                "declaredLength": header["length"],
                "physicalLength": header["physicalLength"],
                # The bytes themselves are stated once, by `mdl.header.trailingPatchWhitespace`,
                # which is also the ledger owner that pays for them.
                "field": "mdl.header.trailingPatchWhitespace",
                "evidence": "the bytes past the declared image are whitespace a patch tool wrote",
            }
        )

    # Topology. A variant the install lacks is an omission; a variant it ships that disagrees with
    # the MDL checksum is storage the engine refuses, which is an anomaly, not a member.
    for role in closure.absent_variants:
        omissions.append(
            {
                "row": "missing-vtx-variant",
                "variant": role,
                "path": closure.mdl.path[:-4] + (
                    ".dx80.vtx" if role.endswith("dx80") else ".dx7_2bone.vtx"
                ),
                "reason": "install-ships-no-such-variant",
            }
        )
    for variant in closure.disagreeing_variants:
        anomalies.append(
            {
                "row": "vtx-variant-disagreement",
                "variant": variant.role,
                "path": variant.path,
                "reason": variant.reason,
                "admitted": variant.admitted,
                "mdlChecksum": variant.mdl_checksum,
                "variantChecksum": variant.variant_checksum,
                "byteLength": variant.byte_length,
            }
        )

    lods: list[dict[str, Any]] = []
    comparison: dict[str, Any] = {
        "primary": closure.primary_vtx.role if closure.primary_vtx else None,
        "alternate": closure.alternate_vtx.role if closure.alternate_vtx else None,
        "equivalent": None,
    }
    if closure.primary_vtx is not None:
        lods = vtx.decode_lods(
            data, closure.primary_vtx.data, variant=closure.primary_vtx.role, anorms=anorms
        )
        if closure.alternate_vtx is not None:
            alternate = vtx.decode_lods(
                data, closure.alternate_vtx.data, variant=closure.alternate_vtx.role,
                anorms=anorms,
            )
            primary_by_lod = {lod["index"]: lod for lod in lods}
            alternate_by_lod = {lod["index"]: lod for lod in alternate}
            overlapping = sorted(primary_by_lod.keys() & alternate_by_lod.keys())
            overlap_equivalent = all(
                vtx.topology_signature([primary_by_lod[value]])
                == vtx.topology_signature([alternate_by_lod[value]])
                for value in overlapping
            )
            comparison.update(
                equivalent=(
                    vtx.topology_signature(lods) == vtx.topology_signature(alternate)
                ),
                overlapEquivalent=overlap_equivalent,
                overlappingLods=overlapping,
                primaryOnlyLods=sorted(primary_by_lod.keys() - alternate_by_lod.keys()),
                alternateOnlyLods=sorted(alternate_by_lod.keys() - primary_by_lod.keys()),
            )
            if not overlap_equivalent:
                anomalies.append(
                    {
                        "row": "vtx-variant-disagreement",
                        "variant": closure.alternate_vtx.role,
                        "path": closure.alternate_vtx.path,
                        "reason": "overlapping-lod-decodes-to-different-topology",
                    }
                )
            else:
                omissions.append(
                    {
                        "row": "legacy-vtx-equivalent",
                        "variant": closure.alternate_vtx.role,
                        "path": closure.alternate_vtx.path,
                        "reason": "semantic-equivalence-compared",
                        "overlappingLods": overlapping,
                    }
                )
                omitted_proven.append(
                    {
                        "path": "vtx.comparison.overlap",
                        "reason": "legacy-vtx-equivalent",
                        "variant": closure.alternate_vtx.role,
                    }
                )
            lods = [
                primary_by_lod.get(value) or alternate_by_lod[value]
                for value in sorted(primary_by_lod.keys() | alternate_by_lod.keys())
            ]
    elif _i32(data, 320) > 0:
        # Topology is the unit's own structure, so an MDL with body parts and no admitted VTX is
        # unresolved rather than an omission.
        unresolved.append(
            {
                "path": "vtx",
                "reason": "no-admitted-vtx-variant",
                "bodyParts": _i32(data, 320),
            }
        )

    anomalies.extend(_repair_degenerate_normals(lods))
    anomalies.extend(_degenerate_binds(bones))
    # A VTX variant compiled against a wider vertex block than the MDL ships. The sections that
    # resolve are published; the triangles whose source vertex the MDL does not hold are named
    # here and stay unresolved, because no byte of this install can supply them.
    for lod in lods:
        for section in lod.get("staleSections") or []:
            anomalies.append(
                {
                    "row": "vtx-vertex-outside-model",
                    "lod": lod["index"],
                    **section,
                    "resolved": False,
                    "evidence": (
                        "the VTX addresses vertices the MDL's own vertex block does not hold; "
                        "the triangles that name them carry no source vertex and are dropped"
                    ),
                }
            )

    referenced: dict[tuple[int, int], set[int]] = {}
    for lod in lods:
        for primitive in lod["primitives"]:
            referenced.setdefault(
                (primitive["bodyPart"], primitive["model"]), set()
            ).update(primitive["sourceVertices"])
    body_parts = _body_parts(data, anorms, referenced)
    _unrendered_morph_records(data, body_parts, lods, anorms)
    morph_targets = _morph_targets(data, lods, anorms)

    materials, skin_families, material_dependencies, material_surfaces = _material_rows(
        data, index, read_bytes
    )
    for row in materials:
        if row["resolved"]:
            continue
        omitted_proven.append(
            {
                "path": f"materialBindings.slots[{row['slot']}]",
                "reason": "studio-texture-name-has-no-vmt",
                "sourceName": row["sourceName"],
                "asset": row["material"],
                "candidates": row["candidates"],
            }
        )

    include_dependencies: list[dict[str, Any]] = []
    for include in include_models:
        normalized = include["path"].lower()
        if not normalized.endswith(".mdl"):
            normalized += ".mdl"
        payload = read_bytes(index, normalized)
        include["sourcePath"] = normalized
        if payload is None:
            # An include model is the unit's own structure, so a path the install cannot reach is
            # a `coverage.unresolved` row with the reference kept under its real identity. The
            # sentinel namespace is for a reference the referenced kind's own rules make
            # unreachable, and it produces no dependency row at all.
            include["asset"] = asset_id("model", normalize_model_key(normalized))
            include["resolved"] = False
            anomalies.append(
                {
                    "row": "stale-include-path",
                    "includeIndex": include["includeIndex"],
                    "path": normalized,
                    "asset": include["asset"],
                }
            )
            unresolved.append(
                {
                    "path": f"mdl.includeModels[{include['includeIndex']}]",
                    "reason": "include-model-is-not-installed",
                    "sourcePath": normalized,
                }
            )
            include_dependencies.append(
                dependency("model", include["asset"], normalized, False)
            )
            continue
        include["asset"] = asset_id("model", normalize_model_key(normalized))
        include["resolved"] = True
        # The rows keep include order, and `mdl.includeModels[i].asset` carries the index-to-bank
        # join; a dependency row may hold only the keys the unit contract names.
        include_dependencies.append(
            dependency(
                "model",
                include["asset"],
                normalized,
                True,
                byteLength=len(payload),
                sha256=hashlib.sha256(payload).hexdigest(),
            )
        )

    facial = _facial(data)
    selected_tables = (
        expressions.selected_tables(closure.key, index) if facial["controllers"] else []
    )
    facial["selectedTables"] = selected_tables
    facial["morphTargets"] = morph_targets
    omitted_proven.extend(expressions.omitted_selections(selected_tables))

    rules, rule_faults = mdl_skel.axis_interp_records(data, mdl_skel.read_bones(data))
    if rule_faults:
        raise ModelDecodeError("; ".join(rule_faults))
    rules = _axis_interpolation_rows(data, rules)
    try:
        garments = mdl_cloth.build(data, closure.primary_vtx.data) if closure.primary_vtx else []
    except (IndexError, struct.error, ValueError) as error:
        raise ModelDecodeError(f"renderer cloth: {error}") from error
    cloth_records = _cloth_source_records(
        data, closure.primary_vtx.data if closure.primary_vtx else None
    )

    physics_model = physics.decode(closure.phy.data) if closure.phy is not None else None
    surfaces: set[str] = set(material_surfaces)
    if header["surfaceProperty"]:
        surfaces.add(header["surfaceProperty"])
    surfaces.update(bone["surfaceProperty"] for bone in bones if bone["surfaceProperty"])
    if physics_model is not None:
        # The PHY text's retail spellings are the unit's anomalies, not a second `physics` key.
        anomalies.extend(physics_model.pop("textAnomalies", []))
        if physics_model["header"].get("solidBlockCount") != physics_model["header"]["solidCount"]:
            anomalies.append(
                {
                    "row": "phy-solid-count-mismatch",
                    "declaredSolids": physics_model["header"]["solidCount"],
                    "keyValueSolidBlocks": physics_model["header"].get("solidBlockCount"),
                }
            )
        for solid in physics_model["solids"]:
            value = str(solid.get("properties", {}).get("surfaceprop", "")).strip()
            if value:
                surfaces.add(value.lower())

    if surface_properties is None:
        from elysium_pipeline.formats.model_glb.source import surface_property_names

        surface_properties = surface_property_names(index, read_bytes=read_bytes)
    surface_dependencies = _surface_dependencies(surfaces, surface_properties)
    event_dependencies = _event_dependencies(sequences, index)

    sequence_groups = _sequence_groups(data)
    referenced_groups = {int(row["sequenceGroup"]) for row in sequences}
    for group in sequence_groups:
        if group["index"] in referenced_groups:
            continue
        omissions.append(
            {
                "row": "unused-sequence-group",
                "index": group["index"],
                "label": group["label"],
                "reason": "no-local-sequence-names-this-group",
            }
        )
    omissions.append(
        {
            "row": "reserved-field",
            "fields": ["header@396/@400"],
            "field": "mdl.keyValues",
            "reason": "v2531-has-no-header-keyvalues-region",
            "evidence": (
                "the count/index pair at +396/+400 addresses the 28-byte secondary-motion "
                "records this unit carries, not modern Source's keyvalueindex/keyvaluesize"
            ),
        }
    )
    if not header["reserved412"] and not header["reserved416"]:
        omissions.append(
            {
                "row": "reserved-field",
                "fields": ["header@412", "header@416"],
                "reason": "reserved-zero-storage-no-shipped-consumer",
            }
        )
    # The EOF trailer and the tracks that follow an animation's declared channels are both
    # decoded into the extension and both claimed `mapped`, so neither is an omission: one range
    # carries one grading.
    if any(
        (row.get("runtimeLodCount") or 0) < (row.get("lodRows") or 0) for row in cloth_records
    ):
        omitted_proven.append(
            {
                "path": "cloth.sourceModels[].meshMaps[].lodRows[runtimeLodCount:]",
                "reason": "physical-map-rows-beyond-vtx-numlods",
            }
        )

    # Every `omitted-proven` byte range names the evidence that justified it, which the unit
    # contract requires to live in `omissions` or `coverage.omittedProven`.
    # The legacy twin publishes only the LODs the primary variant lacks; the rest of its topology
    # is the same triangles a second time, and the ledger grades it as the omission it is.
    published_lods: dict[str, frozenset[int]] = {}
    if closure.alternate_vtx is not None:
        published_lods[closure.alternate_vtx.path] = frozenset(
            comparison.get("alternateOnlyLods") or ()
        )
    byte_ledger = coverage.cover_closure(closure.members(), published_lods=published_lods)
    omitted_proven.extend(coverage.omitted_proven_rows(byte_ledger))

    dependencies = (
        material_dependencies
        + include_dependencies
        + expressions.table_dependencies(selected_tables)
        + surface_dependencies
        + event_dependencies
    )

    return ModelUnit(
        key=closure.key,
        asset=closure.asset,
        family=family_of(closure.key),
        shape=shape_of(
            body_part_count=_i32(data, 320),
            bone_count=len(bones),
            local_animation_count=len(local_animations),
            flags=header["flags"]["value"],
        ),
        roles=[],
        members=closure.members(),
        mdl_data=data,
        header=header,
        bones=bones,
        split_rotation_bones=split_rotation_bones(bones),
        local_animations=local_animations,
        sequences=sequences,
        pose_parameters=_with_source_offsets(
            plain(mdl_skel.pose_parameters(data)), _table_offsets(data, 384, 388, 20)
        ),
        attachments=_with_source_offsets(
            plain(mdl_skel.attachments(data)), _table_offsets(data, 328, 332, 60)
        ),
        hitbox_sets=_hitbox_sets(data, bones),
        ik_chains=_ik_chains(data, bones),
        bone_controllers=_bone_controllers(data),
        transition_graph=_transition_graph(data),
        include_models=include_models,
        sequence_groups=sequence_groups,
        textures=_texture_records(data),
        search_paths=list(mdl.search_paths(data)),
        skin_table=[list(family) for family in mdl.skin_table(data)],
        body_parts=body_parts,
        # v2531 has no header KeyValues region: `+396`/`+400` is the secondary-motion count/index
        # pair, not `keyvalueindex`/`keyvaluesize` (`docs/vtmb/mdl_v2531.md` § `+396`/`+400`). The
        # `$staticprop` compile switch survives as `header.flags.staticProp`; `prop_data` lives in
        # the PHY KeyValues tail this unit already carries.
        key_values=None,
        lods=lods,
        vtx_variants=_vtx_variant_rows(closure),
        vtx_comparison=comparison,
        materials=materials,
        skin_families=skin_families,
        facial=facial,
        procedural={"axisInterpolation": plain(rules)},
        secondary_motion=_secondary_motion_records(data),
        cloth={"garments": plain(garments), "sourceModels": cloth_records},
        physics=plain(physics_model),
        dependencies=dependencies,
        anomalies=anomalies,
        omissions=omissions,
        byte_ledger=byte_ledger,
        typed_unidentified=[
            {
                "path": "secondaryMotion[].unusedAuthoredPreset",
                "recordOffset": 8,
                "type": "float32",
                "standing": "runtime-unused-authoring-name-unrecovered",
            }
        ] if _i32(data, 396) else [],
        omitted_proven=omitted_proven,
        unresolved=unresolved,
        unsupported=unsupported,
    )
