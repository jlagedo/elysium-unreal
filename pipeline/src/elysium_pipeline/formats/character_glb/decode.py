"""Whole-character semantic decode for the isolated Character GLB Exporter."""

from __future__ import annotations

from collections import Counter
import hashlib
import struct
from typing import Any, Callable

from elysium_pipeline.formats import mdl, mdl_cloth, mdl_secondary_motion, mdl_skel
from elysium_pipeline.formats.character_glb import coverage, expressions, physics, vtx
from elysium_pipeline.formats.character_glb.model import CharacterModel, plain
from elysium_pipeline.formats.character_glb.source import CharacterSourceClosure


class CharacterDecodeError(RuntimeError):
    """A source field cannot be represented by the complete character model."""


def _i32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<i", data, offset)[0]


def _u32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<I", data, offset)[0]


def _vec3(data: bytes, offset: int) -> tuple[float, float, float]:
    return struct.unpack_from("<3f", data, offset)


def _cstr(data: bytes, offset: int, *, limit: int | None = None) -> str:
    end = data.find(b"\0", offset, limit)
    if end < 0:
        raise CharacterDecodeError(f"string at {offset} has no terminator")
    return data[offset:end].decode("ascii", "replace")


def _header(data: bytes) -> dict[str, Any]:
    if len(data) < 424:
        raise CharacterDecodeError(f"MDL is only {len(data)} bytes; header is 424")
    if data[:4] != b"IDST":
        raise CharacterDecodeError(f"bad MDL id {data[:4]!r}")
    version = _i32(data, 4)
    if version != 2531:
        raise CharacterDecodeError(f"MDL version {version}, expected 2531")
    declared_length = _i32(data, 140)
    if declared_length > len(data) or declared_length < 424:
        raise CharacterDecodeError(
            f"MDL header length {declared_length} != source length {len(data)}"
        )
    trailing = data[declared_length:]
    if trailing and any(byte not in b"\t\n\r " for byte in trailing):
        raise CharacterDecodeError(
            f"MDL has {len(trailing)} non-whitespace bytes after declared image length "
            f"{declared_length}"
        )
    pairs = {
        "bones": (240, 244),
        "boneControllers": (248, 252),
        "hitBoxSets": (256, 260),
        "localAnimations": (264, 268),
        "localSequences": (272, 276),
        "sequenceGroups": (284, 288),
        "textures": (292, 296),
        "textureSearchPaths": (300, 304),
        "skin": (308, 316),
        "bodyParts": (320, 324),
        "localAttachments": (328, 332),
        "transitions": (336, 340),
        "flexDescriptions": (344, 348),
        "flexControllers": (352, 356),
        "flexRules": (360, 364),
        "ikChains": (368, 372),
        "mouths": (376, 380),
        "localPoseParameters": (384, 388),
        "secondaryMotion": (396, 400),
        "includeModels": (404, 408),
    }
    tables = {
        name: {"count": _i32(data, count), "offset": _i32(data, offset)}
        for name, (count, offset) in pairs.items()
    }
    tables["skin"]["families"] = _i32(data, 312)
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
        "flags": _i32(data, 228),
        "phonemeFilter": struct.unpack_from("<2f", data, 232),
        "sequencesIndexed": _i32(data, 280),
        "surfaceProperty": (
            _cstr(data, _i32(data, 392)) if _i32(data, 392) > 0 else ""
        ),
        "reserved412": _i32(data, 412),
        "reserved416": _i32(data, 416),
        "contents": _i32(data, 420),
        "tables": tables,
        "compilerTrailer": coverage.compiler_trailer(data),
    }


def _bones(data: bytes) -> list[dict[str, Any]]:
    result = []
    for bone in mdl_skel.read_bones(data):
        record = _i32(data, 244) + bone.index * mdl_skel.BONE_STRIDE
        surface_offset = _i32(data, record + 152)
        result.append(
            {
                "index": bone.index,
                "name": bone.name,
                "parent": bone.parent,
                "boneControllers": list(
                    struct.unpack_from("<6i", data, record + 8)
                ),
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
                "surfaceProperty": (
                    _cstr(data, record + surface_offset) if surface_offset else ""
                ),
                "contents": _i32(data, record + 156),
            }
        )
    return result


def _bodyparts(
    data: bytes,
    anorms: list[tuple[float, float, float]] | None,
) -> list[dict[str, Any]]:
    """Every MDL bodypart/model/mesh and the complete embedded vertex pools."""
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
                raise CharacterDecodeError(
                    f"bodypart {bodypart_index} model {model_index} has vertex list {vertex_list}"
                )
            position_offset = _vec3(data, model + 160)
            position_scale = _vec3(data, model + 172)
            skins = mdl_skel.read_skin(
                data, model, vertex_offset, vertex_count, vertex_list
            )
            tangent_base = model + tangent_offset if tangent_offset > 0 else None
            if tangent_base is not None and tangent_base + vertex_count * 16 > len(data):
                raise CharacterDecodeError(
                    f"bodypart {bodypart_index} model {model_index} tangent array runs outside MDL"
                )
            vertices = []
            for vertex_index in range(vertex_count):
                vertex = model + vertex_offset + vertex_index * stride
                x, y, z, u, texture_v = mdl._read_vertex(
                    data,
                    vertex,
                    vertex_list,
                    position_offset,
                    position_scale,
                )
                bone_indices, bone_weights = skins[vertex_index]
                vertices.append(
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
                        "storedWeights": (
                            list(data[vertex:vertex + 3]) if vertex_list == 0 else []
                        ),
                        "boneSlots": (
                            list(struct.unpack_from("<4h", data, vertex + 4))
                            if vertex_list == 0
                            else [0]
                        ),
                        "influenceSelector": (
                            data[vertex + 3] if vertex_list == 0 else None
                        ),
                    }
                )
            meshes = []
            for mesh_index in range(mesh_count):
                mesh = model + mesh_offset + mesh_index * mdl_skel.MESH_STRIDE
                meshes.append(
                    {
                        "index": mesh_index,
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
                eyeball_record = eyeball_base + eyeball_index * mdl_skel.EYEBALL_STRIDE
                name_offset = _i32(data, eyeball_record)
                eyeball["nameOffset"] = name_offset
                eyeball["name"] = (
                    _cstr(data, eyeball_record + name_offset) if name_offset else ""
                )
                eyeball["reserved124"] = list(
                    struct.unpack_from("<4i", data, eyeball_record + 124)
                )
            models.append(
                {
                    "index": model_index,
                    "sourceOffset": model,
                    "name": _cstr(data, model, limit=model + 128),
                    "type": _i32(data, model + 128),
                    "boundingRadius": struct.unpack_from("<f", data, model + 132)[0],
                    "meshCount": mesh_count,
                    "meshOffset": mesh_offset,
                    "vertexCount": vertex_count,
                    "vertexOffset": vertex_offset,
                    "tangentsOffset": tangent_offset,
                    "vertexListType": vertex_list,
                    "positionOffset": position_offset,
                    "positionScale": position_scale,
                    "reserved184": list(
                        struct.unpack_from("<2i", data, model + 184)
                    ),
                    "eyeballs": eyeballs,
                    "clothDefinitionCount": _i32(data, model + 200),
                    "clothDefinitionOffset": _i32(data, model + 204),
                    "clothCapsuleCount": _i32(data, model + 208),
                    "clothCapsuleOffset": _i32(data, model + 212),
                    "clothSphereCount": _i32(data, model + 216),
                    "clothSphereOffset": _i32(data, model + 220),
                    "meshes": meshes,
                    "vertices": vertices,
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
        raise CharacterDecodeError(
            f"invalid texture declaration count={count}, offset={base}"
        )
    rows = []
    for index in range(count):
        record = base + index * 20
        name_offset, flags = struct.unpack_from("<2i", data, record)
        width, height, max_world_units_per_texel = struct.unpack_from(
            "<3f", data, record + 8
        )
        rows.append(
            {
                "index": index,
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
        raise CharacterDecodeError(
            f"invalid sequence-group declaration count={count}, offset={base}"
        )
    rows = []
    for index in range(count):
        record = base + index * 16
        label_offset, name_offset, cache, data_pointer = struct.unpack_from(
            "<4i", data, record
        )
        rows.append(
            {
                "index": index,
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
        raise CharacterDecodeError(
            f"invalid bone-controller declaration count={count}, offset={base}"
        )
    return [
        {
            "index": index,
            "bone": _i32(data, base + index * 24),
            "type": _i32(data, base + index * 24 + 4),
            "start": struct.unpack_from("<f", data, base + index * 24 + 8)[0],
            "end": struct.unpack_from("<f", data, base + index * 24 + 12)[0],
            "rest": _i32(data, base + index * 24 + 16),
            "inputField": _i32(data, base + index * 24 + 20),
        }
        for index in range(count)
    ]


def _transition_graph(data: bytes) -> dict[str, Any]:
    count, base = _i32(data, 336), _i32(data, 340)
    length = count * count
    if count < 0 or base < 0 or base + length > len(data):
        raise CharacterDecodeError(
            f"invalid transition graph count={count}, offset={base}"
        )
    return {
        "nodeCount": count,
        "matrix": [list(data[base + row * count:base + (row + 1) * count])
                   for row in range(count)],
    }


def _include_models(data: bytes, bones: list[dict[str, Any]]) -> list[dict[str, Any]]:
    count, base = _i32(data, 404), _i32(data, 408)
    stride = 116
    if count < 0 or base < 0 or base + count * stride > len(data):
        raise CharacterDecodeError(
            f"invalid include-model declaration count={count}, offset={base}"
        )
    rows = []
    for index in range(count):
        record = base + index * stride
        filename_offset = _i32(data, record)
        remap_offset = _i32(data, record + 16)
        remap_base = record + remap_offset
        remap_length = len(bones) * 56
        if remap_offset <= 0 or remap_base < 0 or remap_base + remap_length > len(data):
            raise CharacterDecodeError(
                f"include model {index} remap runs outside the MDL"
            )
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
                "path": path,
                "loadedModelPointer": _i32(data, record + 4),
                "virtualSequenceBase": _i32(data, record + 8),
                "virtualSequenceCount": _i32(data, record + 12),
                "remapOffset": remap_offset,
                "globalToLocalPoseParameters": list(
                    struct.unpack_from("<24h", data, record + 20)
                ),
                "localToGlobalPoseParameters": list(
                    struct.unpack_from("<24h", data, record + 68)
                ),
                "boneRemap": remap,
            }
        )
    return rows


def _ik_chains(data: bytes, bones: list[dict[str, Any]]) -> list[dict[str, Any]]:
    count, base = _i32(data, 368), _i32(data, 372)
    if count < 0 or base < 0 or base + count * 16 > len(data):
        raise CharacterDecodeError(
            f"invalid IK chain declaration count={count}, offset={base}"
        )
    chains = []
    for index in range(count):
        record = base + index * 16
        name_offset, link_type, link_count, link_offset = struct.unpack_from(
            "<4i", data, record
        )
        links_base = record + link_offset
        if link_count < 0 or links_base < 0 or links_base + link_count * 28 > len(data):
            raise CharacterDecodeError(f"IK chain {index} link array runs outside the MDL")
        links = []
        for link_index in range(link_count):
            link = links_base + link_index * 28
            bone, = struct.unpack_from("<i", data, link)
            if not 0 <= bone < len(bones):
                raise CharacterDecodeError(
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
        raise CharacterDecodeError(
            f"invalid hitbox-set declaration count={count}, offset={base}"
        )
    sets = []
    for set_index in range(count):
        record = base + set_index * 12
        name_offset, box_count, box_offset = struct.unpack_from("<3i", data, record)
        boxes_base = record + box_offset
        if box_count < 0 or boxes_base < 0 or boxes_base + box_count * 32 > len(data):
            raise CharacterDecodeError(
                f"hitbox set {set_index} box array runs outside the MDL"
            )
        boxes = []
        for box_index in range(box_count):
            box = boxes_base + box_index * 32
            bone, group = struct.unpack_from("<2i", data, box)
            if not 0 <= bone < len(bones):
                raise CharacterDecodeError(
                    f"hitbox set {set_index} box {box_index} names bone {bone}"
                )
            boxes.append(
                {
                    "index": box_index,
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
                "name": _cstr(data, record + name_offset) if name_offset > 0 else "",
                "boxes": boxes,
            }
        )
    return sets


def _material_rows(
    data: bytes,
    index: dict,
    read_bytes: Callable[[dict, str], bytes | None],
) -> tuple[list[dict[str, Any]], list[list[str]], list[dict[str, Any]]]:
    names = mdl.materials(data)
    search = mdl.search_paths(data)
    rows = []
    dependencies = []

    def read(path: str) -> bytes | None:
        return read_bytes(index, path)

    for slot, name in enumerate(names):
        resolved, _ = mdl.resolve_vmt(name, search, read)
        if resolved is None:
            missing_id = f"vtmb:missing-material:{slot}:{mdl.sanitize(name)}"
            rows.append(
                {
                    "slot": slot,
                    "sourceName": name,
                    "sourcePath": None,
                    "material": missing_id,
                    "resolved": False,
                    "candidates": [
                        mdl._norm(f"materials/{path}/{name}.vmt")
                        for path in search
                    ],
                }
            )
            continue
        material_id = "vtmb:material:" + resolved.lower().replace("\\", "/")
        source_path = "materials/" + resolved + ".vmt"
        source_bytes = read(source_path)
        if source_bytes is None:
            raise CharacterDecodeError(
                f"resolved material {resolved!r} disappeared during dependency hashing"
            )
        rows.append(
            {
                "slot": slot,
                "sourceName": name,
                "sourcePath": source_path,
                "material": material_id,
                "resolved": True,
            }
        )
        dependencies.append(
            {
                "role": "material",
                "asset": material_id,
                "sourcePath": source_path,
                "byteLength": len(source_bytes),
                "sha256": hashlib.sha256(source_bytes).hexdigest(),
            }
        )
    families = []
    for family in mdl.skin_table(data):
        ids = []
        for texture in family:
            ids.append(rows[texture]["material"] if 0 <= texture < len(rows) else "")
        families.append(ids)
    return rows, families, dependencies


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
    last_end = records + bone_count * 32
    for bone in range(bone_count):
        record = records + bone * 32
        for relative in struct.unpack_from("<7i", data, record + 4):
            if not relative:
                continue
            start = record + relative
            last_end = max(last_end, start + coverage._mdl_rle_length(data, start, frames))
    limit = _payload_bound_after(data, last_end)
    tracks = []
    offset = last_end
    extra = 0
    while offset < limit:
        length = coverage._rle_exact_track_length(data, offset, limit, frames)
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
            raise CharacterDecodeError(f"local animation {index} did not resolve")
        name, base, frames, fps = found
        animation_records = base + _i32(data, base + 48)
        bone_weights = []
        channel_offsets = []
        for bone in range(bone_count):
            record = animation_records + bone * 32
            bone_weights.append(struct.unpack_from("<f", data, record)[0])
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
                    data, animation_records, bone_count, frames
                ),
                "movement": plain(mdl_skel.read_movements(data, base)),
            }
        )
    return animations


def _swing_records_complete(
    data: bytes,
    descriptor: int,
    bone_names: list[str],
) -> list[dict[str, Any]]:
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
        row["knockbackNameOffsets"] = list(
            struct.unpack_from("<16i", data, record + 0x78)
        )
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
        autolayer_indices = mdl_skel.read_autolayers(data, descriptor, count)
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
                "animationTable": list(
                    struct.unpack_from("<256h", data, descriptor + 56)
                ),
                "movementIndex": _i32(data, descriptor + 568),
                "grid": plain(grid),
                "baseAnimation": base_animation,
                "groupSize": list(struct.unpack_from("<2i", data, descriptor + 572)),
                "parameterIndexes": list(
                    struct.unpack_from("<2i", data, descriptor + 580)
                ),
                "parameterStarts": list(
                    struct.unpack_from("<2f", data, descriptor + 588)
                ),
                "parameterEnds": list(
                    struct.unpack_from("<2f", data, descriptor + 596)
                ),
                "parameterParent": _i32(data, descriptor + 604),
                "sequenceGroup": _i32(data, descriptor + 608),
                "fade": list(struct.unpack_from("<3f", data, descriptor + 612)),
                "entryNode": _i32(data, descriptor + 624),
                "exitNode": _i32(data, descriptor + 628),
                "nodeFlags": _i32(data, descriptor + 632),
                "entryPhase": struct.unpack_from("<f", data, descriptor + 636)[0],
                "exitPhase": struct.unpack_from("<f", data, descriptor + 640)[0],
                "lastFrame": struct.unpack_from("<f", data, descriptor + 644)[0],
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
                "lowReachRaw": struct.unpack_from("<f", data, descriptor + 716)[0],
                "reachRaw": struct.unpack_from("<f", data, descriptor + 720)[0],
                "attackButtonMask": _i32(data, descriptor + 724),
                "resolvedDodgeActivity": _i32(data, descriptor + 728),
                "dodgeActivityNameOffset": _i32(data, descriptor + 732),
                "resolvedBlockedActivity": _i32(data, descriptor + 736),
                "blockedActivityNameOffset": _i32(data, descriptor + 740),
                "chainNameOffset": _i32(data, descriptor + 744),
                "alternateChainNameOffset": _i32(data, descriptor + 748),
                "comboWindow": list(
                    struct.unpack_from("<3f", data, descriptor + 752)
                ),
                "autolayers": [
                    {"index": layer, "label": labels[layer]}
                    for layer in autolayer_indices
                ],
                "events": plain(mdl_skel.read_events(data, descriptor)),
                "reach": mdl_skel.read_reach(data, descriptor),
                "lowReach": mdl_skel.read_low_reach(data, descriptor),
                "envelopes": plain(mdl_skel.read_envelopes(data, descriptor)),
                "blockedReaction": mdl_skel.read_blocked_reaction(data, descriptor),
                "swings": _swing_records_complete(data, descriptor, bone_names),
                "combo": plain(mdl_skel.read_combo_chain(data, descriptor)),
                "movement": (
                    plain(mdl_skel.read_movements(data, animation_descriptor))
                    if animation_descriptor is not None
                    else None
                ),
            }
        )
    return rows


_SOUND_EVENTS = {1004, 1005, 1008, 2005, 4020, 5004, 5005, *range(4150, 4156)}
_EFFECT_EVENTS = {5103, *range(5111, 5120)}


def _event_dependencies(sequences: list[dict[str, Any]]) -> list[dict[str, Any]]:
    """Stable sound/effect references named by character-local animation events."""
    rows = []
    seen: set[tuple[str, str]] = set()
    for sequence in sequences:
        for event in sequence.get("events") or []:
            code = event.get("event")
            options = str(event.get("options") or "").strip()
            if not options:
                continue
            if code in _SOUND_EVENTS:
                role, prefix = "sound", "vtmb:sound:"
            elif code in _EFFECT_EVENTS:
                role, prefix = "effect", "vtmb:effect:"
            else:
                continue
            key = (role, options.lower().replace("\\", "/"))
            if key in seen:
                continue
            seen.add(key)
            rows.append(
                {
                    "role": role,
                    "asset": prefix + key[1],
                    "sourcePath": options,
                    "event": code,
                }
            )
    return rows


_FLEX_OP_CODES = {
    "CONST": 1,
    "FETCH1": 2,
    "FETCH2": 3,
    "ADD": 4,
    "SUB": 5,
    "MUL": 6,
    "DIV": 7,
    "NEG": 8,
    "EXP": 9,
    "OPEN": 10,
    "CLOSE": 11,
    "COMMA": 12,
    "MAX": 13,
    "MIN": 14,
}


def _flex_operation(operation: int | str, integer: int, floating: float) -> dict[str, Any]:
    """Project only the opcode-selected union arm while retaining its exact bits."""

    if isinstance(operation, str):
        if operation in _FLEX_OP_CODES:
            code = _FLEX_OP_CODES[operation]
            name = operation
        elif operation.startswith("op"):
            code = int(operation[2:])
            name = operation
        else:
            raise CharacterDecodeError(f"unsupported flex operation {operation}")
    else:
        code = int(operation)
        name = next(
            (label for label, value in _FLEX_OP_CODES.items() if value == code),
            f"op{code}",
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
        raise CharacterDecodeError(f"unsupported flex operation {operation}")
    return row


def _facial(data: bytes, selected) -> dict[str, Any]:
    return {
        "flexDescriptions": mdl_skel.flex_descs(data),
        "controllers": [
            {"type": row[0], "name": row[1], "min": row[2], "max": row[3]}
            for row in mdl_skel.flex_controllers(data)
        ],
        "rules": [
            {
                "flexDescription": flex,
                "operations": [
                    _flex_operation(operation, integer, floating)
                    for operation, integer, floating in operations
                ],
            }
            for flex, operations in mdl_skel.flex_rules(data)
        ],
        "mouths": [
            {"bone": bone, "forward": forward, "flexDescription": flex}
            for bone, forward, flex in mdl_skel.mouths(data)
        ],
        "phonemeFilter": mdl_skel.phoneme_filter(data),
        "selectedTables": expressions.decode_selected(selected),
    }


def _morph_targets(
    data: bytes,
    lods: list[dict[str, Any]],
    anorms: list[tuple[float, float, float]] | None,
) -> list[dict[str, Any]]:
    """Attach every MDL flex record to its LOD0 primitive by source vertex identity."""
    flex_descriptions = mdl_skel.flex_descs(data)
    if not flex_descriptions:
        return []
    if anorms is None:
        raise CharacterDecodeError(
            "facial morphs require the installed StudioRender unit-vector table"
        )
    if not lods:
        raise CharacterDecodeError("facial model has no decoded LOD")

    slots: list[tuple[int, tuple[float, ...]]] = []
    slot_of: dict[tuple[int, tuple[float, ...]], int] = {}
    per_primitive: list[dict[int, dict[int, tuple[tuple[float, ...], tuple[float, ...]]]]] = []
    for primitive in lods[0]["primitives"]:
        source_to_local = {
            source: index for index, source in enumerate(primitive["sourceVertices"])
        }
        targets: dict[int, dict[int, tuple[tuple[float, ...], tuple[float, ...]]]] = {}
        for flex in mdl_skel.mesh_flexes(
            data, primitive["modelBase"], primitive["mesh"]
        ):
            key = (flex["flexdesc"], tuple(float(value) for value in flex["targets"]))
            if key not in slot_of:
                slot_of[key] = len(slots)
                slots.append(key)
            slot = slot_of[key]
            bucket = targets.setdefault(slot, {})
            for local, position, normal in mdl_skel.vert_anims(data, flex, anorms):
                source = primitive["vertexOffset"] + local
                vertex = source_to_local.get(source)
                if vertex is None:
                    continue
                bucket[vertex] = (
                    tuple(float(value) for value in position or (0.0, 0.0, 0.0)),
                    tuple(float(value) for value in normal or (0.0, 0.0, 0.0)),
                )
        per_primitive.append(targets)

    seen: Counter[str] = Counter()
    rows = []
    for slot, (flex, ramp) in enumerate(slots):
        base_name = (
            flex_descriptions[flex]
            if 0 <= flex < len(flex_descriptions)
            else f"flex{flex}"
        )
        name = base_name if seen[base_name] == 0 else f"{base_name}#{seen[base_name]}"
        seen[base_name] += 1
        rows.append(
            {
                "index": slot,
                "name": name,
                "flexDescription": flex,
                "targets": list(ramp),
            }
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


def _cloth_source_records(
    data: bytes, vtx_data: bytes | None = None
) -> list[dict[str, Any]]:
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
            _table, lod_rows, columns, definition_offsets = mdl_cloth.definition_table(
                data, model
            )
            if columns <= 0:
                continue
            definitions = []
            for lod in range(lod_rows):
                for definition in range(columns):
                    record = definition_offsets[lod * columns + definition]
                    decoded = plain(
                        mdl_cloth.read_definition(data, model, definition, lod=lod)
                    )
                    tangent_edges = _i32(data, record + 56)
                    normal_edges = _i32(data, record + 60)
                    edge_base = record + _i32(data, record + 64)
                    edge_pairs = [
                        list(struct.unpack_from("<2H", data, edge_base + edge * 4))
                        for edge in range(tangent_edges + normal_edges)
                    ] if tangent_edges + normal_edges else []
                    particles = _i32(data, record + 4)

                    def seeds(field: int) -> list[list[int]]:
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
                            "header": {
                                "distanceSimdBlockCount": _i32(data, record + 36),
                                "compressionSimdBlockCount": _i32(data, record + 40),
                                "packedSimdPayloadOffset": _i32(data, record + 44),
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
                            "optionalSeedA": seeds(84),
                            "optionalSeedB": seeds(88),
                        }
                    )

            runtime_lods = None
            if vtx_data is not None:
                try:
                    runtime_lods = coverage._vtx_model_lod_count(
                        vtx_data, bodypart_index, model_index
                    )
                except coverage.CharacterByteCoverageError as error:
                    raise CharacterDecodeError(str(error)) from error
            mesh_count = _i32(data, model + 136)
            mesh_base = model + _i32(data, model + 140)
            mesh_maps = []
            physical_rows = lod_rows
            for mesh_index in range(mesh_count):
                mesh = mesh_base + mesh_index * mdl_skel.MESH_STRIDE
                try:
                    layout = mdl_cloth.map_layout(data, mesh)
                except ValueError as error:
                    raise CharacterDecodeError(str(error)) from error
                if layout is None:
                    continue
                physical_rows = max(physical_rows, layout["rows"], layout["position_rows"])
                vertex_count = layout["vertex_count"]
                tangent_entries = layout["rows"] * vertex_count
                tangent_end = layout["tangent_base"] + tangent_entries * 2
                if (
                    secondary_count > 0
                    and layout["tangent_base"] < secondary_base < tangent_end
                ):
                    tangent_entries = (secondary_base - layout["tangent_base"]) // 2
                lod_maps = []
                for lod in range(layout["rows"]):
                    selector_start = layout["selector_base"] + lod * vertex_count
                    position_start = layout["position_base"] + lod * vertex_count * 2
                    tangent_start = lod * vertex_count
                    tangent_count = max(
                        0, min(vertex_count, tangent_entries - tangent_start)
                    )
                    position_count = vertex_count if lod < layout["position_rows"] else 0
                    lod_maps.append(
                        {
                            "lod": lod,
                            "runtimeIndexed": (
                                runtime_lods is not None and lod < runtime_lods
                            ),
                            "selectors": list(
                                data[selector_start:selector_start + vertex_count]
                            ),
                            "positionNormal": list(struct.unpack_from(
                                f"<{position_count}H",
                                data,
                                position_start,
                            )) if position_count else [],
                            "tangent": list(struct.unpack_from(
                                f"<{tangent_count}H",
                                data,
                                layout["tangent_base"] + tangent_start * 2,
                            )) if tangent_count else [],
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


def _secondary_motion_records(data: bytes) -> list[dict[str, Any]]:
    return [
        {
            "firstBone": record.first_bone,
            "terminalBone": record.terminal_bone,
            "unusedAuthoredPreset": record.unnamed_field_8,
            "gravity": record.gravity,
            "damping": record.damping,
            "springExponent": record.spring_exponent,
            "maxAngleDegrees": record.max_angle_degrees,
        }
        for record in mdl_secondary_motion.read_chain_records(data)
    ]


def decode_character(
    closure: CharacterSourceClosure,
    index: dict,
    *,
    read_bytes: Callable[[dict, str], bytes | None] | None = None,
    anorms: list[tuple[float, float, float]] | None = None,
) -> CharacterModel:
    """Decode the complete direct source closure into one exporter-owned semantic model."""
    if read_bytes is None:
        from elysium_pipeline.formats import install

        read_bytes = install.read

    data = closure.mdl.data
    header = _header(data)
    header["bodyParts"] = _bodyparts(data, anorms)
    bones = _bones(data)
    header["textures"] = _texture_records(data)
    header["sequenceGroups"] = _sequence_groups(data)
    header["boneControllers"] = _bone_controllers(data)
    header["transitionGraph"] = _transition_graph(data)
    include_models = _include_models(data, bones)
    header["includeModels"] = include_models
    primary_lods = vtx.decode_lods(
        data,
        closure.primary_vtx.data,
        variant=closure.primary_vtx.role,
        anorms=anorms,
    )
    comparison = {
        "primary": closure.primary_vtx.role,
        "primaryHeader": vtx.decode_header(closure.primary_vtx.data),
        "primaryMaterialReplacements": vtx.decode_material_replacements(
            closure.primary_vtx.data
        ),
        "alternate": None,
        "alternateHeader": None,
        "alternateMaterialReplacements": None,
        "equivalent": None,
    }
    if closure.alternate_vtx is not None:
        alternate_lods = vtx.decode_lods(
            data,
            closure.alternate_vtx.data,
            variant=closure.alternate_vtx.role,
            anorms=anorms,
        )
        primary_by_lod = {lod["index"]: lod for lod in primary_lods}
        alternate_by_lod = {lod["index"]: lod for lod in alternate_lods}
        overlapping_lods = sorted(primary_by_lod.keys() & alternate_by_lod.keys())
        overlap_equivalent = all(
            vtx.topology_signature([primary_by_lod[index]])
            == vtx.topology_signature([alternate_by_lod[index]])
            for index in overlapping_lods
        )
        equivalent = vtx.topology_signature(primary_lods) == vtx.topology_signature(alternate_lods)
        comparison = {
            "primary": closure.primary_vtx.role,
            "primaryHeader": vtx.decode_header(closure.primary_vtx.data),
            "primaryMaterialReplacements": vtx.decode_material_replacements(
                closure.primary_vtx.data
            ),
            "alternate": closure.alternate_vtx.role,
            "alternateHeader": vtx.decode_header(closure.alternate_vtx.data),
            "alternateMaterialReplacements": vtx.decode_material_replacements(
                closure.alternate_vtx.data
            ),
            "equivalent": equivalent,
            "overlapEquivalent": overlap_equivalent,
            "overlappingLods": overlapping_lods,
            "primaryOnlyLods": sorted(primary_by_lod.keys() - alternate_by_lod.keys()),
            "alternateOnlyLods": sorted(alternate_by_lod.keys() - primary_by_lod.keys()),
        }
        if not overlap_equivalent:
            raise CharacterDecodeError(
                f"{closure.primary_vtx.path} and {closure.alternate_vtx.path} "
                "decode to different topology on an overlapping LOD"
            )
        primary_lods = [
            primary_by_lod[index] if index in primary_by_lod else alternate_by_lod[index]
            for index in sorted(primary_by_lod.keys() | alternate_by_lod.keys())
        ]

    morph_targets = _morph_targets(data, primary_lods, anorms)

    material_rows, skin_families, dependencies = _material_rows(
        data, index, read_bytes
    )
    include_dependencies = []
    for include in include_models:
        path = include["path"]
        normalized = path.lower().replace("\\", "/")
        if not normalized.endswith(".mdl"):
            normalized += ".mdl"
        source_bytes = read_bytes(index, normalized)
        if source_bytes is None:
            raise CharacterDecodeError(f"missing included animation bank {normalized}")
        prefix = "models/character/"
        identifier = normalized[len(prefix):] if normalized.startswith(prefix) else normalized[7:]
        if identifier.endswith(".mdl"):
            identifier = identifier[:-4]
        include_dependencies.append(
            {
                "role": "animation-bank",
                "asset": "vtmb:animation-bank:" + identifier,
                "sourcePath": normalized,
                "byteLength": len(source_bytes),
                "sha256": hashlib.sha256(source_bytes).hexdigest(),
            }
        )

    rules, rule_faults = mdl_skel.axis_interp_records(data, mdl_skel.read_bones(data))
    if rule_faults:
        raise CharacterDecodeError("; ".join(rule_faults))
    try:
        garments = mdl_cloth.build(data, closure.primary_vtx.data)
    except (IndexError, struct.error, ValueError) as error:
        raise CharacterDecodeError(f"renderer cloth: {error}") from error
    cloth_records = _cloth_source_records(data, closure.primary_vtx.data)

    physics_model = physics.decode(closure.phy.data) if closure.phy else None
    physics_dependencies = []
    if physics_model:
        surface_properties = sorted(
            {
                str(solid["properties"].get("surfaceprop", "")).strip().lower()
                for solid in physics_model["solids"]
                if str(solid["properties"].get("surfaceprop", "")).strip()
            }
        )
        physics_dependencies = [
            {
                "role": "surface-property",
                "asset": "vtmb:surface-property:" + name,
                "sourcePath": "scripts/surfaceproperties.txt#" + name,
            }
            for name in surface_properties
        ]
    omitted_proven = []
    unresolved: list[dict[str, Any]] = []
    unsupported: list[dict[str, Any]] = []
    if header.get("compilerTrailer"):
        omitted_proven.append(
            {
                "path": "mdl.compilerTrailerQnDbTm",
                "reason": "compiler-eof-trailer-no-engine-consumer",
                "magic": header["compilerTrailer"]["magic"],
                "pathOffset": header["compilerTrailer"]["pathOffset"],
                "path": header["compilerTrailer"]["path"],
            }
        )
    local_animations = _local_animations(data)
    if any(animation["unindexedTracks"] for animation in local_animations):
        omitted_proven.append(
            {
                "path": "mdl.localAnimations[].unindexedTracks",
                "reason": "rle-after-declared-channels-no-bone-offset",
            }
        )
    if any(
        (row.get("runtimeLodCount") or 0) < (row.get("lodRows") or 0)
        for row in cloth_records
    ):
        omitted_proven.append(
            {
                "path": "mdl.cloth[].meshMaps[].lodRows[runtimeLodCount:]",
                "reason": "physical-map-rows-beyond-vtx-numlods",
            }
        )
    if closure.alternate_vtx is not None and comparison.get("overlapEquivalent", False):
        omitted_proven.append(
            {
                "path": "vtx.alternateTopology[].overlap",
                "reason": "semantic-equivalence-compared",
            }
        )
    if cloth_records:
        omitted_proven.extend(
            [
                {"path": "mdl.cloth[].packedSimdPayload", "reason": "renderer-cache"},
                {"path": "mdl.cloth[].normalTangentContributionPayloads",
                 "reason": "renderer-cache"},
            ]
        )
    for row in material_rows:
        if row.get("resolved", True):
            continue
        omitted_proven.append(
            {
                "path": f"mdl.textures[{row['slot']}]",
                "reason": "studio-texture-name-has-no-vmt",
                "sourceName": row["sourceName"],
                "candidates": row.get("candidates", []),
            }
        )
    sequences = _local_sequences(data)
    facial = {**_facial(data, closure.facial), "morphTargets": morph_targets}
    for family, entry in (facial.get("selectedTables") or {}).items():
        if entry.get("semanticAgreement") is not False:
            continue
        omitted_proven.append(
            {
                "path": f"facial.selectedTables.{family}.txt",
                "reason": "faceposer-text-not-runtime-authority",
                "difference": entry.get("semanticDifference"),
            }
        )
    event_dependencies = _event_dependencies(sequences)
    return CharacterModel(
        model_path=closure.model_path,
        asset_id=closure.asset_id,
        sources=[member.identity() for member in closure.members()],
        header=header,
        bones=bones,
        lods=primary_lods,
        materials=material_rows,
        skin_families=skin_families,
        local_animations=local_animations,
        sequences=sequences,
        pose_parameters=plain(mdl_skel.pose_parameters(data)),
        attachments=plain(mdl_skel.attachments(data)),
        hitbox_sets=_hitbox_sets(data, bones),
        ik_chains=_ik_chains(data, bones),
        facial=facial,
        procedural={"axisInterpolation": plain(rules)},
        secondary_motion=_secondary_motion_records(data),
        cloth={"garments": plain(garments), "sourceModels": cloth_records},
        physics=plain(physics_model),
        dependencies=(
            dependencies
            + include_dependencies
            + physics_dependencies
            + event_dependencies
        ),
        variant_comparison=comparison,
        byte_coverage=coverage.cover_closure(closure.members()),
        typed_unidentified=[
            {
                "path": "mdl.secondaryMotion[].unusedAuthoredPreset",
                "recordOffset": 8,
                "type": "float32",
                "standing": "runtime-unused-authoring-name-unrecovered",
            },
        ],
        omitted_proven=omitted_proven,
        unresolved=unresolved,
        unsupported=unsupported,
    )
