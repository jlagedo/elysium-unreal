"""Complete-LOD MDL/VTX geometry decoder for one model unit.

The walk is the same for a character body, a scenery prop and a wield model: the MDL owns the
vertex pool and the VTX owns the topology that indexes it, so a unit with no body part simply
yields no LOD.

A strip group keeps its counts, flags, bone-state changes and original-vertex table -- the
VTMB-only half -- and not its index array: those bytes become the primitive's core `indices`
accessor, and the source triangles are the core indices read back through `sourceVertices`.
"""

from __future__ import annotations

import math
import struct
from typing import Any

from elysium_pipeline.formats import mdl, mdl_skel


class ModelTopologyError(ValueError):
    """An MDL/VTX tree is malformed or describes a topology this decoder cannot name."""


def _i32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<i", data, offset)[0]


def _u16(data: bytes, offset: int) -> int:
    return struct.unpack_from("<H", data, offset)[0]


def _vec3(data: bytes, offset: int) -> tuple[float, float, float]:
    return struct.unpack_from("<3f", data, offset)


def _normal(
    data: bytes,
    vertex: int,
    vertex_list: int,
    anorms: list[tuple[float, float, float]] | None,
) -> tuple[float, float, float]:
    if vertex_list == 0:
        return _vec3(data, vertex + 24)
    if anorms is None:
        raise ModelTopologyError(
            "compact vertex normals require the installed StudioRender unit-vector table"
        )
    if vertex_list == 1:
        table_offset = _u16(data, vertex + 6)
        index = table_offset // 12
    elif vertex_list == 2:
        index = data[vertex + 3]
    else:
        raise ModelTopologyError(f"unknown embedded vertex list {vertex_list}")
    if not 0 <= index < len(anorms):
        raise ModelTopologyError(
            f"packed normal index {index} is outside the {len(anorms)}-entry table"
        )
    return tuple(float(value) for value in anorms[index])


def _unit_or_zero(value) -> tuple[float, float, float]:
    length = math.sqrt(sum(float(component) ** 2 for component in value))
    if length <= 1e-12:
        return (0.0, 0.0, 0.0)
    return tuple(float(component) / length for component in value)


def decode_header(data: bytes) -> dict[str, Any]:
    if len(data) < 36:
        raise ModelTopologyError(f"VTX is only {len(data)} bytes")
    return {
        "version": _i32(data, 0),
        "vertexCacheSize": _i32(data, 4),
        "maxBonesPerStrip": _u16(data, 8),
        "maxBonesPerTriangle": _u16(data, 10),
        "maxBonesPerVertex": _i32(data, 12),
        "checksum": struct.unpack_from("<I", data, 16)[0],
        "lodCount": _i32(data, 20),
        "materialReplacementListOffset": _i32(data, 24),
        "bodyPartCount": _i32(data, 28),
        "bodyPartOffset": _i32(data, 32),
    }


def decode_material_replacements(data: bytes) -> list[dict[str, Any]]:
    """VtMB's packed per-LOD material replacement tables."""
    header = decode_header(data)
    lod_count = header["lodCount"]
    base = header["materialReplacementListOffset"]
    if lod_count < 0 or base < 0 or base + lod_count * 8 > len(data):
        raise ModelTopologyError(
            f"material replacement lists {base}+{lod_count * 8} exceed {len(data)} bytes"
        )
    lists = []
    for lod in range(lod_count):
        record = base + lod * 8
        count, relative = struct.unpack_from("<2i", data, record)
        replacements_base = record + relative
        if count < 0 or replacements_base < 0 or replacements_base + count * 6 > len(data):
            raise ModelTopologyError(
                f"LOD {lod} material replacements run outside the VTX"
            )
        replacements = []
        for index in range(count):
            replacement = replacements_base + index * 6
            material, name_offset = struct.unpack_from("<hi", data, replacement)
            name = replacement + name_offset
            if not 0 <= name < len(data):
                raise ModelTopologyError(
                    f"LOD {lod} replacement {index} name offset is outside the VTX"
                )
            end = data.find(b"\0", name)
            if end < 0:
                raise ModelTopologyError(
                    f"LOD {lod} replacement {index} name has no terminator"
                )
            replacements.append(
                {
                    "index": index,
                    "material": material,
                    "name": data[name:end].decode("ascii", "replace"),
                }
            )
        lists.append(
            {
                "lod": lod,
                "count": count,
                "relativeOffset": relative,
                "replacements": replacements,
            }
        )
    return lists


def decode_lods(
    mdl_data: bytes,
    vtx_data: bytes,
    *,
    variant: str,
    anorms: list[tuple[float, float, float]] | None = None,
) -> list[dict[str, Any]]:
    """Decode every VTX LOD into MDL-identified primitives in Source space."""
    if mdl_data[:4] != b"IDST":
        raise ModelTopologyError(f"bad MDL ident {mdl_data[:4]!r}")
    if len(vtx_data) < 36:
        raise ModelTopologyError(f"{variant} VTX is only {len(vtx_data)} bytes")

    material_names = mdl.materials(mdl_data)
    skin_table = mdl.skin_table(mdl_data)
    family_zero = skin_table[0]
    bodypart_count = _i32(mdl_data, 320)
    bodypart_base = _i32(mdl_data, 324)
    vtx_bodypart_count = _i32(vtx_data, 28)
    vtx_bodypart_base = _i32(vtx_data, 32)
    if vtx_bodypart_count != bodypart_count:
        raise ModelTopologyError(
            f"{variant} has {vtx_bodypart_count} bodyparts; MDL declares {bodypart_count}"
        )

    lods: dict[int, dict[str, Any]] = {}
    for bodypart_index in range(bodypart_count):
        mdl_bodypart = bodypart_base + bodypart_index * 16
        vtx_bodypart = vtx_bodypart_base + bodypart_index * 8
        model_count = _i32(mdl_data, mdl_bodypart + 4)
        mdl_model_offset = _i32(mdl_data, mdl_bodypart + 12)
        vtx_model_count = _i32(vtx_data, vtx_bodypart)
        vtx_model_offset = _i32(vtx_data, vtx_bodypart + 4)
        if vtx_model_count != model_count:
            raise ModelTopologyError(
                f"{variant} bodypart {bodypart_index} has {vtx_model_count} models; "
                f"MDL declares {model_count}"
            )

        for model_index in range(model_count):
            model_base = mdl_bodypart + mdl_model_offset + model_index * mdl_skel.MODEL_STRIDE
            vtx_model = vtx_bodypart + vtx_model_offset + model_index * 8
            mesh_count = _i32(mdl_data, model_base + 136)
            mesh_base = model_base + _i32(mdl_data, model_base + 140)
            vertex_count = _i32(mdl_data, model_base + 144)
            vertex_base = _i32(mdl_data, model_base + 148)
            tangent_offset = _i32(mdl_data, model_base + 152)
            tangent_base = model_base + tangent_offset if tangent_offset > 0 else None
            if tangent_base is not None and tangent_base + vertex_count * 16 > len(mdl_data):
                raise ModelTopologyError(
                    f"bodypart {bodypart_index} model {model_index} tangents overrun MDL"
                )
            vertex_list = _i32(mdl_data, model_base + 156)
            vertex_stride = mdl.VSTRIDE.get(vertex_list)
            if vertex_stride is None:
                raise ModelTopologyError(
                    f"bodypart {bodypart_index} model {model_index} names vertex list "
                    f"{vertex_list}"
                )
            quant_offset = _vec3(mdl_data, model_base + 160)
            quant_scale = _vec3(mdl_data, model_base + 172)
            skin = mdl_skel.read_skin(
                mdl_data, model_base, vertex_base, vertex_count, vertex_list
            )

            lod_count = _i32(vtx_data, vtx_model)
            lod_base = vtx_model + _i32(vtx_data, vtx_model + 4)
            for lod_index in range(lod_count):
                vtx_lod = lod_base + lod_index * 12
                vtx_mesh_count = _i32(vtx_data, vtx_lod)
                vtx_mesh_base = vtx_lod + _i32(vtx_data, vtx_lod + 4)
                switch_point = struct.unpack_from("<f", vtx_data, vtx_lod + 8)[0]
                if vtx_mesh_count != mesh_count:
                    raise ModelTopologyError(
                        f"{variant} bodypart {bodypart_index} model {model_index} LOD "
                        f"{lod_index} has {vtx_mesh_count} meshes; MDL declares {mesh_count}"
                    )
                lod = lods.setdefault(
                    lod_index,
                    {
                        "index": lod_index,
                        "switchPoints": [],
                        "primitives": [],
                        "staleSections": [],
                    },
                )
                lod["switchPoints"].append(float(switch_point))

                for mesh_index in range(mesh_count):
                    mdl_mesh = mesh_base + mesh_index * mdl_skel.MESH_STRIDE
                    vtx_mesh = vtx_mesh_base + mesh_index * 8
                    skin_reference = _i32(mdl_data, mdl_mesh)
                    texture_index = (
                        family_zero[skin_reference]
                        if 0 <= skin_reference < len(family_zero)
                        else skin_reference
                    )
                    material = (
                        material_names[texture_index]
                        if 0 <= texture_index < len(material_names)
                        else f"mat{texture_index}"
                    )
                    vertex_offset = _i32(mdl_data, mdl_mesh + 12)
                    stripgroup_count = _u16(vtx_data, vtx_mesh)
                    stripgroup_base = vtx_mesh + _i32(vtx_data, vtx_mesh + 4)
                    positions: list[tuple[float, float, float]] = []
                    normals: list[tuple[float, float, float]] = []
                    tangents: list[tuple[float, float, float, float]] = []
                    uvs: list[tuple[float, float]] = []
                    joints: list[list[int]] = []
                    weights: list[list[float]] = []
                    source_vertices: list[int] = []
                    triangles: list[tuple[int, int, int]] = []
                    remap: dict[int, int] = {}
                    stripgroups = []
                    stale: set[int] = set()
                    dropped = 0

                    for stripgroup_index in range(stripgroup_count):
                        stripgroup = (
                            stripgroup_base
                            + stripgroup_index * mdl.STRIPGROUP_STRIDE
                        )
                        table_vertex_count = _u16(vtx_data, stripgroup)
                        table_index_count = _u16(vtx_data, stripgroup + 2)
                        strip_count = _u16(vtx_data, stripgroup + 4)
                        stripgroup_flags = _u16(vtx_data, stripgroup + 6)
                        vertex_table = stripgroup + _i32(vtx_data, stripgroup + 8)
                        index_table = stripgroup + _i32(vtx_data, stripgroup + 12)
                        strip_base = stripgroup + _i32(vtx_data, stripgroup + 16)
                        table_stride, table_vertex = mdl._vtable_form(vtx_data, stripgroup)
                        vertex_records = []
                        for vertex_record_index in range(table_vertex_count):
                            record = vertex_table + vertex_record_index * table_stride
                            if table_stride == 12:
                                vertex_records.append(
                                    {
                                        "boneWeightIndex": _u16(vtx_data, record),
                                        "boneIds": list(
                                            struct.unpack_from("<4h", vtx_data, record + 2)
                                        ),
                                        "originalMeshVertex": _u16(vtx_data, record + 10),
                                    }
                                )
                            else:
                                vertex_records.append(
                                    {"originalMeshVertex": _u16(vtx_data, record)}
                                )
                        strip_rows = []
                        for strip_index in range(strip_count):
                            strip = strip_base + strip_index * 16
                            index_count = _u16(vtx_data, strip)
                            first_index = _u16(vtx_data, strip + 2)
                            strip_vertex_count = _u16(vtx_data, strip + 4)
                            first_vertex = _u16(vtx_data, strip + 6)
                            bone_count = vtx_data[strip + 8]
                            strip_flags = vtx_data[strip + 9]
                            state_count = _u16(vtx_data, strip + 10)
                            state_base = strip + _i32(vtx_data, strip + 12)
                            state_changes = [
                                {
                                    "hardwareId": _u16(vtx_data, state_base + state * 4),
                                    "newBoneId": _u16(vtx_data, state_base + state * 4 + 2),
                                }
                                for state in range(state_count)
                            ]
                            if index_count % 3:
                                raise ModelTopologyError(
                                    f"{variant} LOD {lod_index} strip has {index_count} indices"
                                )
                            strip_rows.append(
                                {
                                    "indexCount": index_count,
                                    "firstIndex": first_index,
                                    "vertexCount": strip_vertex_count,
                                    "firstVertex": first_vertex,
                                    "boneCount": bone_count,
                                    "flags": strip_flags,
                                    "boneStateChanges": state_changes,
                                }
                            )
                            for corner in range(0, index_count, 3):
                                corners = []
                                for ordinal in range(3):
                                    local = _u16(
                                        vtx_data,
                                        index_table
                                        + (first_index + corner + ordinal) * 2,
                                    )
                                    corners.append(
                                        vertex_offset
                                        + _u16(
                                            vtx_data,
                                            vertex_table
                                            + local * table_stride
                                            + table_vertex,
                                        )
                                    )
                                outside = [
                                    value
                                    for value in corners
                                    if not 0 <= value < vertex_count
                                ]
                                if outside:
                                    # A VTX built against a wider vertex block than the MDL
                                    # ships. The triangle has no source vertex to read, so it is
                                    # dropped and named rather than invented; the unit publishes
                                    # the sections that do resolve and carries this one in
                                    # `anomalies[]`.
                                    stale.update(outside)
                                    dropped += 1
                                    continue
                                triangle = []
                                for global_vertex in corners:
                                    if global_vertex not in remap:
                                        remap[global_vertex] = len(positions)
                                        source_vertices.append(global_vertex)
                                        source_vertex = (
                                            model_base
                                            + vertex_base
                                            + global_vertex * vertex_stride
                                        )
                                        x, y, z, u, v = mdl._read_vertex(
                                            mdl_data,
                                            source_vertex,
                                            vertex_list,
                                            quant_offset,
                                            quant_scale,
                                        )
                                        positions.append((x, y, z))
                                        normals.append(
                                            _unit_or_zero(
                                                _normal(
                                                    mdl_data,
                                                    source_vertex,
                                                    vertex_list,
                                                    anorms,
                                                )
                                            )
                                        )
                                        if tangent_base is not None:
                                            tangents.append(
                                                struct.unpack_from(
                                                    "<4f",
                                                    mdl_data,
                                                    tangent_base + global_vertex * 16,
                                                )
                                            )
                                        uvs.append((u, v))
                                        bone_indices, bone_weights = skin[global_vertex]
                                        joints.append(
                                            (list(bone_indices) + [0, 0, 0, 0])[:4]
                                        )
                                        weights.append(
                                            (list(bone_weights) + [0.0, 0.0, 0.0, 0.0])[:4]
                                        )
                                    triangle.append(remap[global_vertex])
                                triangles.append(tuple(triangle))
                        stripgroups.append(
                            {
                                "index": stripgroup_index,
                                "flags": stripgroup_flags,
                                "vertexForm": "boned" if table_stride == 12 else "flat",
                                "indexCount": table_index_count,
                                "vertices": vertex_records,
                                "strips": strip_rows,
                            }
                        )

                    if dropped:
                        # Named whether or not a triangle of this section survived, so a section
                        # the stale VTX empties entirely is still visible in the unit.
                        lod["staleSections"].append(
                            {
                                "variant": variant,
                                "bodyPart": bodypart_index,
                                "model": model_index,
                                "mesh": mesh_index,
                                "vertexCount": vertex_count,
                                "staleVertices": sorted(stale),
                                "droppedTriangles": dropped,
                            }
                        )
                    if triangles:
                        lod["primitives"].append(
                            {
                                "bodyPart": bodypart_index,
                                "model": model_index,
                                "modelBase": model_base,
                                "mesh": mesh_index,
                                "skinReference": skin_reference,
                                "material": material,
                                "materialType": _i32(mdl_data, mdl_mesh + 24),
                                "materialParam": _i32(mdl_data, mdl_mesh + 28),
                                "vertexOffset": vertex_offset,
                                "sourceVertices": source_vertices,
                                "positions": positions,
                                "normals": normals,
                                "tangents": tangents,
                                "uvs": uvs,
                                "joints": joints,
                                "weights": weights,
                                "triangles": triangles,
                                "stripGroups": stripgroups,
                            }
                        )
    return [lods[index] for index in sorted(lods)]


def topology_signature(lods: list[dict[str, Any]]) -> tuple:
    """Semantic topology identity independent of local output-vertex numbering."""
    signature = []
    for lod in lods:
        primitives = []
        for primitive in lod["primitives"]:
            source = primitive["sourceVertices"]
            triangles = []
            for triangle in primitive["triangles"]:
                wound = tuple(source[corner] for corner in triangle)
                rotations = (
                    wound,
                    (wound[1], wound[2], wound[0]),
                    (wound[2], wound[0], wound[1]),
                )
                triangles.append(min(rotations))
            primitives.append(
                (
                    primitive["bodyPart"],
                    primitive["model"],
                    primitive["mesh"],
                    primitive["skinReference"],
                    tuple(sorted(triangles)),
                )
            )
        signature.append((lod["index"], tuple(sorted(primitives))))
    return tuple(signature)
