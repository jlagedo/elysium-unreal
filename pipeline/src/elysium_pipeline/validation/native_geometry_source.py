"""Bounded, independent MDL/VTX byte witnesses for staged source-vertex identities.

Offsets are the measured v2531/v107 contracts in docs/vtmb/mdl_v2531.md. This reader
does not invoke the model decoder, exporter, stage writer, or native builder.
"""
import struct


def read(data, offset, fmt):
    size = struct.calcsize("<" + fmt)
    if offset < 0 or offset + size > len(data):
        raise ValueError(f"source extent {offset}+{size} exceeds {len(data)} bytes")
    values = struct.unpack_from("<" + fmt, data, offset)
    return values[0] if len(values) == 1 else values


def mdl_vertex_witness(data, body_part, model_index, mesh_index, source_vertex):
    if data[:4] != b"IDST" or read(data, 4, "i") != 2531:
        raise ValueError("not an installed v2531 MDL")
    if not 0 <= body_part < read(data, 320, "i"):
        raise ValueError("MDL bodypart identity outside tree")
    part = read(data, 324, "i") + 16 * body_part
    if not 0 <= model_index < read(data, part + 4, "i"):
        raise ValueError("MDL model identity outside tree")
    model = part + read(data, part + 12, "i") + 224 * model_index
    if not 0 <= mesh_index < read(data, model + 136, "i"):
        raise ValueError("MDL mesh identity outside tree")
    mesh = model + read(data, model + 140, "i") + 60 * mesh_index
    count, vertex_offset, tangent_offset, layout = read(data, model + 144, "4i")
    first, mesh_count = read(data, mesh + 12, "i"), read(data, mesh + 8, "i")
    if not 0 <= source_vertex < count or not first <= source_vertex < first + mesh_count:
        raise ValueError("MDL source vertex outside model/mesh range")
    stride, uv_offset, encoding = {0: (44, 36, "2f"), 1: (12, 8, "2H"), 2: (8, 4, "2H")}[layout]
    vertex = model + vertex_offset + source_vertex * stride
    read(data, model + vertex_offset, f"{count * stride}s")  # Check complete pool extent.
    raw_uv = read(data, vertex + uv_offset, encoding)
    uv = list(raw_uv if layout == 0 else (v / 65535. for v in raw_uv))
    result = {"bodyPart": body_part, "model": model_index, "mesh": mesh_index, "sourceVertex": source_vertex,
              "partByteOffset": part, "modelByteOffset": model, "meshByteOffset": mesh,
              "modelVertexCount": count, "meshFirstVertex": first, "meshVertexCount": mesh_count,
              "vertexListType": layout, "vertexStride": stride, "vertexPoolByteOffset": model + vertex_offset,
              "vertexByteOffset": vertex, "uvRelativeOffset": uv_offset, "uvByteOffset": vertex + uv_offset,
              "uvBytesHex": data[vertex + uv_offset:vertex + uv_offset + struct.calcsize("<" + encoding)].hex(),
              "vertexRecordHex": data[vertex:vertex + stride].hex(), "uv": uv,
              "tangentPoolByteOffset": model + tangent_offset if tangent_offset > 0 else None,
              "poolStrideFromTangentBoundary": (tangent_offset - vertex_offset) / count if tangent_offset > 0 and count else None}
    if layout == 0:
        result.update(position=list(read(data, vertex + 12, "3f")), normal=list(read(data, vertex + 24, "3f")),
                      storedWeightBytes=list(data[vertex:vertex + 3]), selector=data[vertex + 3],
                      boneSlots=list(read(data, vertex + 4, "4h")))
    return result


def vtx_triangle_witness(data, mdl_witness):
    """Locate a used LOD0 VTX index, table entry and original mesh vertex for this ID."""
    if read(data, 0, "i") != 107:
        raise ValueError("not an installed v107 VTX")
    bp, mi, mesh_index = (mdl_witness[k] for k in ("bodyPart", "model", "mesh"))
    if not 0 <= bp < read(data, 28, "i"):
        raise ValueError("VTX bodypart identity outside tree")
    part = read(data, 32, "i") + bp * 8
    if not 0 <= mi < read(data, part, "i"):
        raise ValueError("VTX model identity outside tree")
    model = part + read(data, part + 4, "i") + mi * 8
    if read(data, model, "i") <= 0:
        raise ValueError("VTX has no LOD0")
    lod = model + read(data, model + 4, "i")
    if not 0 <= mesh_index < read(data, lod, "i"):
        raise ValueError("VTX mesh identity outside tree")
    mesh = lod + read(data, lod + 4, "i") + mesh_index * 8
    groups, group_base = read(data, mesh, "H"), mesh + read(data, mesh + 4, "i")
    wanted = mdl_witness["sourceVertex"] - mdl_witness["meshFirstVertex"]
    for gi in range(groups):
        group = group_base + 20 * gi
        vertices, indices, strips, flags = read(data, group, "4H")
        if flags & 0x08:
            stride, id_offset = 12, 10
        elif flags & 0x10:
            stride, id_offset = 2, 0
        else:
            raise ValueError(f"unrecognized VTX vertex-table flags {flags:#x}")
        vertex_base = group + read(data, group + 8, "i")
        index_base = group + read(data, group + 12, "i")
        strip_base = group + read(data, group + 16, "i")
        for si in range(strips):
            strip = strip_base + 16 * si
            count, first = read(data, strip, "2H")
            if count % 3 or first + count > indices:
                raise ValueError("invalid VTX triangle index range")
            for ordinal in range(count):
                at = index_base + 2 * (first + ordinal)
                vertex_index = read(data, at, "H")
                if vertex_index >= vertices:
                    raise ValueError("VTX index outside its vertex table")
                original_at = vertex_base + stride * vertex_index + id_offset
                if read(data, original_at, "H") == wanted:
                    return {"lod": 0, "meshByteOffset": mesh, "stripGroup": gi, "stripGroupByteOffset": group,
                            "strip": si, "triangle": ordinal // 3, "corner": ordinal % 3, "flags": flags,
                            "vertexTableStride": stride, "originalVertexFieldOffset": id_offset,
                            "indexByteOffset": at, "indexValue": vertex_index, "originalVertexByteOffset": original_at,
                            "originalMeshVertex": wanted, "originalVertexBytesHex": data[original_at:original_at + 2].hex()}
    raise ValueError("source vertex has no drawn VTX LOD0 witness")
