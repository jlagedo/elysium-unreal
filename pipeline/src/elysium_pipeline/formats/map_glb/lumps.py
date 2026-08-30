"""Fixed-width record readers for the lumps the map root owns.

Every reader takes the whole file and a `LumpSpan`, returns the lump's rows in source order --
each carrying the `sourceOffset` its bytes were read from -- and the byte claims that pay for
them. A lump longer than its record count explains keeps its surplus as a named omission rather
than silently ending early.

Struct layouts follow `docs/vtmb/bsp_format.md` and the constants `formats/bsp.py` already owns;
the VtMB-specific 104-byte `dface_t` is that module's `DFaceVTMB` layout.
"""

from __future__ import annotations

import struct
from typing import Any, Callable

from elysium_pipeline.formats import bsp as bsp_readers
from elysium_pipeline.formats.map_glb import claims as claim_tools
from elysium_pipeline.formats.map_glb.partition import LumpSpan

#: One record's width, per lump index, for every fixed-width lump the root owns.
RECORD_BYTES = {
    1: 20,    # dplane_t
    2: 32,    # dtexdata_t
    3: 12,    # Vector
    5: 32,    # dnode_t
    6: 72,    # texinfo_t
    7: bsp_readers.FACE_SIZE,     # dface_t, the 104-byte VtMB form
    10: bsp_readers.LEAF_SIZE,    # dleaf_t
    12: 4,    # dedge_t
    13: 4,    # int
    14: 48,   # dmodel_t
    16: 2,    # unsigned short
    17: 2,    # unsigned short
    18: 12,   # dbrush_t
    19: 8,    # dbrushside_t
    20: 8,    # darea_t
    21: 12,   # dareaportal_t
    26: bsp_readers.DI_SIZE,      # ddispinfo_t
    27: bsp_readers.FACE_SIZE,    # dface_t
    30: 12,   # Vector
    31: 2,    # unsigned short
    33: bsp_readers.DV_SIZE,      # CDispVert
    36: 12,   # dleafwaterdata_t: two floats, a short and its pad
    37: 10,   # dprimitive_t
    38: 12,   # Vector
    39: 2,    # unsigned short
    41: 12,   # Vector
    42: 16,   # dcubemapsample_t
    44: 4,    # int
    46: 2,    # unsigned short
    47: 2,    # unsigned short
    48: 2,    # unsigned short
}


class MapLumpError(ValueError):
    """A lump is not the fixed-width record array its census says it is."""


def counted(span: LumpSpan) -> int:
    """How many whole records the lump's length holds."""

    size = RECORD_BYTES[span.index]
    return span.length // size


def _read(
    data: bytes,
    span: LumpSpan,
    owner: str,
    build: Callable[[int, int], dict[str, Any]],
    omissions: list[dict[str, Any]],
) -> tuple[list[dict[str, Any]], list[claim_tools.Claim]]:
    if not span.populated:
        return [], []
    size = RECORD_BYTES[span.index]
    count = span.length // size
    rows = [build(index, span.offset + index * size) for index in range(count)]
    claims = claim_tools.strided(span.offset, count, size, "mapped", owner)
    claims += claim_tools.tail(
        data,
        span.offset + count * size,
        span.length - count * size,
        owner,
        "unused-lump-bytes",
        omissions,
        lump=span.index,
    )
    return rows, claims


def planes(data, span, omissions):
    def build(index: int, offset: int) -> dict[str, Any]:
        nx, ny, nz, dist, kind = struct.unpack_from("<4fi", data, offset)
        return {
            "index": index,
            "sourceOffset": offset,
            "normal": (nx, ny, nz),
            "dist": dist,
            "type": int(kind),
        }

    return _read(data, span, "planes", build, omissions)


def texdata(data, span, omissions):
    def build(index: int, offset: int) -> dict[str, Any]:
        red, green, blue, name_id, width, height, view_width, view_height = struct.unpack_from(
            "<3f5i", data, offset
        )
        return {
            "index": index,
            "sourceOffset": offset,
            "reflectivity": (red, green, blue),
            "nameStringTableID": int(name_id),
            "width": int(width),
            "height": int(height),
            "viewWidth": int(view_width),
            "viewHeight": int(view_height),
        }

    return _read(data, span, "textures", build, omissions)


def texinfos(data, span, omissions):
    def build(index: int, offset: int) -> dict[str, Any]:
        values = struct.unpack_from("<16f2i", data, offset)
        return {
            "index": index,
            "sourceOffset": offset,
            "textureVecs": (values[0:4], values[4:8]),
            "lightmapVecs": (values[8:12], values[12:16]),
            "flags": int(values[16]),
            "texData": int(values[17]),
        }

    return _read(data, span, "texinfos", build, omissions)


def _face(data: bytes, index: int, offset: int, table: str) -> dict[str, Any]:
    average = [
        list(struct.unpack_from("<3Bb", data, offset + sample * 4)) for sample in range(8)
    ]
    plane, side, on_node = struct.unpack_from("<HBB", data, offset + 32)
    first_edge, num_edges, tex_info, disp_info, fog = struct.unpack_from(
        "<i3hH", data, offset + 36
    )
    styles = list(struct.unpack_from("<8B", data, offset + 48))
    day = list(struct.unpack_from("<8B", data, offset + 56))
    night = list(struct.unpack_from("<8B", data, offset + 64))
    light_offset, area = struct.unpack_from("<if", data, offset + 72)
    lightmap_mins = list(struct.unpack_from("<2i", data, offset + 80))
    lightmap_size = list(struct.unpack_from("<2i", data, offset + 88))
    original, smoothing = struct.unpack_from("<iI", data, offset + 96)
    return {
        "index": index,
        "table": table,
        "sourceOffset": offset,
        "avgLightColor": average,
        "plane": int(plane),
        "side": int(side),
        "onNode": int(on_node),
        "firstEdge": int(first_edge),
        "numEdges": int(num_edges),
        "texInfo": int(tex_info),
        "dispInfo": int(disp_info),
        "surfaceFogVolumeID": int(fog),
        "styles": styles,
        "dayStyles": day,
        "nightStyles": night,
        "lightOffset": int(light_offset),
        "area": area,
        "lightmapMins": lightmap_mins,
        "lightmapSize": lightmap_size,
        "origFace": int(original),
        "smoothingGroups": int(smoothing),
    }


def faces(data, span, omissions, table: str = "faces"):
    return _read(
        data, span, table, lambda index, offset: _face(data, index, offset, table), omissions
    )


def referenced_runs(
    offset: int, count: int, stride: int, owner: str, referenced: set[int]
) -> tuple[list[claim_tools.Claim], list[dict[str, Any]]]:
    """Split a record array into the runs some index of this map names and the runs none does.

    A record no index names reaches no accessor and no extension row, so grading its bytes
    `mapped` would state a representation the unit does not hold. Those runs are claimed
    `omitted-proven` under `<owner>.unreferenced` and the ranges are returned so the decode can
    put them on the evidence row that counts them.
    """

    claim_list: list[claim_tools.Claim] = []
    unreferenced: list[dict[str, Any]] = []
    start = 0
    while start < count:
        present = start in referenced
        end = start + 1
        while end < count and (end in referenced) == present:
            end += 1
        run_offset = offset + start * stride
        run_length = (end - start) * stride
        if present:
            claim_list.append((run_offset, run_length, "mapped", owner))
        else:
            claim_list.append((run_offset, run_length, "omitted-proven", owner + ".unreferenced"))
            unreferenced.append(
                {
                    "sourceOffset": run_offset,
                    "byteLength": run_length,
                    "firstRecord": start,
                    "recordCount": end - start,
                }
            )
        start = end
    return claim_list, unreferenced


def raw_vectors(data, span, omissions, owner: str, *, referenced: set[int] | None = None):
    """A lump of bare `Vector`s whose values reach the product as accessor elements.

    `referenced` is the set of record indices some index lump of this map names; the records
    outside it are claimed `omitted-proven`, because nothing carries them into the product.
    """

    if not span.populated:
        return [], [], []
    stride = RECORD_BYTES[span.index]
    count = span.length // stride
    values = list(struct.unpack_from(f"<{count * 3}f", data, span.offset)) if count else []
    points = [tuple(values[index * 3:index * 3 + 3]) for index in range(count)]
    if referenced is None:
        claim_list = claim_tools.strided(span.offset, count, stride, "mapped", owner)
        unreferenced: list[dict[str, Any]] = []
    else:
        claim_list, unreferenced = referenced_runs(
            span.offset, count, stride, owner, referenced
        )
    claim_list += claim_tools.tail(
        data,
        span.offset + count * stride,
        span.length - count * stride,
        owner,
        "unused-lump-bytes",
        omissions,
        lump=span.index,
    )
    return points, claim_list, unreferenced


def nodes(data, span, omissions):
    def build(index: int, offset: int) -> dict[str, Any]:
        plane, child_front, child_back = struct.unpack_from("<3i", data, offset)
        mins = list(struct.unpack_from("<3h", data, offset + 12))
        maxs = list(struct.unpack_from("<3h", data, offset + 18))
        first_face, num_faces, area, padding = struct.unpack_from("<2H2h", data, offset + 24)
        return {
            "index": index,
            "sourceOffset": offset,
            "plane": int(plane),
            "children": [int(child_front), int(child_back)],
            "mins": mins,
            "maxs": maxs,
            "firstFace": int(first_face),
            "numFaces": int(num_faces),
            "area": int(area),
            "padding": int(padding),
        }

    return _read(data, span, "bsp.nodes", build, omissions)


def leafs(data, span, omissions):
    def build(index: int, offset: int) -> dict[str, Any]:
        contents, cluster, packed = struct.unpack_from("<ihH", data, offset)
        mins = list(struct.unpack_from("<3h", data, offset + 8))
        maxs = list(struct.unpack_from("<3h", data, offset + 14))
        first_face, num_faces, first_brush, num_brushes = struct.unpack_from(
            "<4H", data, offset + 20
        )
        water, padding = struct.unpack_from("<2h", data, offset + 28)
        return {
            "index": index,
            "sourceOffset": offset,
            "contents": int(contents),
            "cluster": int(cluster),
            "area": int(packed) & 0x1FF,
            "flags": (int(packed) >> 9) & 0x7F,
            "mins": mins,
            "maxs": maxs,
            "firstLeafFace": int(first_face),
            "numLeafFaces": int(num_faces),
            "firstLeafBrush": int(first_brush),
            "numLeafBrushes": int(num_brushes),
            "leafWaterDataID": int(water),
            "padding": int(padding),
        }

    return _read(data, span, "bsp.leafs", build, omissions)


def edges(data, span, omissions):
    if not span.populated:
        return {"sourceOffset": span.offset, "stride": 4, "count": 0, "values": []}, []
    count = counted(span)
    raw = struct.unpack_from(f"<{count * 2}H", data, span.offset) if count else ()
    table = {
        "sourceOffset": span.offset,
        "stride": 4,
        "count": count,
        "values": [[raw[index * 2], raw[index * 2 + 1]] for index in range(count)],
    }
    claim_list = claim_tools.strided(span.offset, count, 4, "mapped", "edges")
    claim_list += claim_tools.tail(
        data,
        span.offset + count * 4,
        span.length - count * 4,
        "edges",
        "unused-lump-bytes",
        omissions,
        lump=span.index,
    )
    return table, claim_list


def scalars(data, span, omissions, owner: str, code: str):
    """One flat table for a lump whose record is a single scalar.

    The row's own offset is `sourceOffset + index * stride`, which the table states once instead
    of repeating it on every element -- the same datum, said once.
    """

    stride = RECORD_BYTES[span.index]
    if not span.populated:
        return {"sourceOffset": span.offset, "stride": stride, "count": 0, "values": []}, []
    count = counted(span)
    values = list(struct.unpack_from(f"<{count}{code}", data, span.offset)) if count else []
    table = {
        "sourceOffset": span.offset,
        "stride": stride,
        "count": count,
        "values": values,
    }
    claim_list = claim_tools.strided(span.offset, count, stride, "mapped", owner)
    claim_list += claim_tools.tail(
        data,
        span.offset + count * stride,
        span.length - count * stride,
        owner,
        "unused-lump-bytes",
        omissions,
        lump=span.index,
    )
    return table, claim_list


def models(data, span, omissions):
    def build(index: int, offset: int) -> dict[str, Any]:
        values = struct.unpack_from("<9f3i", data, offset)
        return {
            "index": index,
            "sourceOffset": offset,
            "mins": values[0:3],
            "maxs": values[3:6],
            "origin": values[6:9],
            "headNode": int(values[9]),
            "firstFace": int(values[10]),
            "numFaces": int(values[11]),
        }

    return _read(data, span, "models", build, omissions)


def brushes(data, span, omissions):
    def build(index: int, offset: int) -> dict[str, Any]:
        first_side, num_sides, contents = struct.unpack_from("<3i", data, offset)
        return {
            "index": index,
            "sourceOffset": offset,
            "firstSide": int(first_side),
            "numSides": int(num_sides),
            "contents": int(contents),
        }

    return _read(data, span, "collision.brushes", build, omissions)


def brush_sides(data, span, omissions):
    def build(index: int, offset: int) -> dict[str, Any]:
        plane, tex_info, disp_info, bevel = struct.unpack_from("<H3h", data, offset)
        return {
            "index": index,
            "sourceOffset": offset,
            "plane": int(plane),
            "texInfo": int(tex_info),
            "dispInfo": int(disp_info),
            "bevel": int(bevel),
        }

    return _read(data, span, "collision.brushSides", build, omissions)


def areas(data, span, omissions):
    def build(index: int, offset: int) -> dict[str, Any]:
        num_portals, first_portal = struct.unpack_from("<2i", data, offset)
        return {
            "index": index,
            "sourceOffset": offset,
            "numAreaPortals": int(num_portals),
            "firstAreaPortal": int(first_portal),
        }

    return _read(data, span, "bsp.areas", build, omissions)


def area_portals(data, span, omissions):
    def build(index: int, offset: int) -> dict[str, Any]:
        key, other, first_vert, num_verts, plane = struct.unpack_from("<4Hi", data, offset)
        return {
            "index": index,
            "sourceOffset": offset,
            "portalKey": int(key),
            "otherArea": int(other),
            "firstClipPortalVert": int(first_vert),
            "clipPortalVerts": int(num_verts),
            "plane": int(plane),
        }

    return _read(data, span, "bsp.areaPortals", build, omissions)


def vectors(data, span, omissions, owner: str):
    def build(index: int, offset: int) -> dict[str, Any]:
        x, y, z = struct.unpack_from("<3f", data, offset)
        return {"index": index, "sourceOffset": offset, "point": (x, y, z)}

    return _read(data, span, owner, build, omissions)


def dispinfos(data, span, omissions):
    def build(index: int, offset: int) -> dict[str, Any]:
        start = struct.unpack_from("<3f", data, offset)
        vert_start, tri_start, power, min_tess = struct.unpack_from("<4i", data, offset + 12)
        smoothing = struct.unpack_from("<f", data, offset + 28)[0]
        contents = struct.unpack_from("<i", data, offset + 32)[0]
        map_face = struct.unpack_from("<H", data, offset + 36)[0]
        alpha_start, sample_start = struct.unpack_from("<2i", data, offset + 40)
        neighbours = []
        for edge in range(4):
            base = offset + 48 + edge * 12
            sub = []
            for half in range(2):
                neighbour, orientation, span_flag, neighbour_span = struct.unpack_from(
                    "<H3B", data, base + half * 6
                )
                sub.append(
                    {
                        "neighbour": int(neighbour),
                        "orientation": int(orientation),
                        "span": int(span_flag),
                        "neighbourSpan": int(neighbour_span),
                    }
                )
            neighbours.append(sub)
        corners = []
        for corner in range(4):
            base = offset + 96 + corner * 10
            values = list(struct.unpack_from("<4H", data, base))
            corners.append(
                {"neighbours": values, "count": int(data[base + 8]), "padding": int(data[base + 9])}
            )
        allowed = list(struct.unpack_from("<10I", data, offset + 136))
        return {
            "index": index,
            "sourceOffset": offset,
            "startPosition": start,
            "dispVertStart": int(vert_start),
            "dispTriStart": int(tri_start),
            "power": int(power),
            "minTess": int(min_tess),
            "smoothingAngle": smoothing,
            "contents": int(contents),
            "mapFace": int(map_face),
            "lightmapAlphaStart": int(alpha_start),
            "lightmapSamplePositionStart": int(sample_start),
            "edgeNeighbours": neighbours,
            "cornerNeighbours": corners,
            "allowedVerts": allowed,
        }

    return _read(data, span, "displacements", build, omissions)


def disp_verts(data, span, omissions):
    def build(index: int, offset: int) -> dict[str, Any]:
        vx, vy, vz, dist, alpha = struct.unpack_from("<5f", data, offset)
        return {
            "index": index,
            "sourceOffset": offset,
            "vector": (vx, vy, vz),
            "dist": dist,
            "alpha": alpha,
        }

    return _read(data, span, "displacements.verts", build, omissions)


def leaf_water(data, span, omissions):
    def build(index: int, offset: int) -> dict[str, Any]:
        surface_z, min_z, tex_info, padding = struct.unpack_from("<2f2h", data, offset)
        return {
            "index": index,
            "sourceOffset": offset,
            "surfaceZ": surface_z,
            "minZ": min_z,
            "surfaceTexInfoID": int(tex_info),
            "padding": int(padding),
        }

    return _read(data, span, "water.leafData", build, omissions)


def primitives(data, span, omissions):
    def build(index: int, offset: int) -> dict[str, Any]:
        kind, first_index, index_count, first_vert, vert_count = struct.unpack_from(
            "<5H", data, offset
        )
        return {
            "index": index,
            "sourceOffset": offset,
            "type": int(kind),
            "firstIndex": int(first_index),
            "indexCount": int(index_count),
            "firstVert": int(first_vert),
            "vertCount": int(vert_count),
        }

    return _read(data, span, "primitives.primitives", build, omissions)


def cubemaps(data, span, omissions):
    def build(index: int, offset: int) -> dict[str, Any]:
        x, y, z, size = struct.unpack_from("<4i", data, offset)
        return {
            "index": index,
            "sourceOffset": offset,
            "origin": [int(x), int(y), int(z)],
            "size": int(size),
        }

    return _read(data, span, "cubemaps", build, omissions)


def occluders(data, span, omissions):
    """Lump 9's three counted tables. One map populates it, with all three counts zero."""

    if not span.populated:
        return {"count": 0, "data": [], "polyData": [], "vertexIndices": []}, []
    offset, end = span.offset, span.end
    if span.length < 12:
        raise MapLumpError(f"OCCLUSION is {span.length} bytes")
    count = struct.unpack_from("<i", data, offset)[0]
    if count < 0 or offset + 4 + count * 40 > end:
        raise MapLumpError(f"OCCLUSION declares {count} occluders")
    occluder_rows = []
    for index in range(count):
        row = offset + 4 + index * 40
        flags, first_poly, poly_count = struct.unpack_from("<3i", data, row)
        mins = struct.unpack_from("<3f", data, row + 12)
        maxs = struct.unpack_from("<3f", data, row + 24)
        area = struct.unpack_from("<i", data, row + 36)[0]
        occluder_rows.append(
            {
                "index": index,
                "sourceOffset": row,
                "flags": int(flags),
                "firstPoly": int(first_poly),
                "polyCount": int(poly_count),
                "mins": mins,
                "maxs": maxs,
                "area": int(area),
            }
        )
    cursor = offset + 4 + count * 40
    poly_count = struct.unpack_from("<i", data, cursor)[0]
    if poly_count < 0 or cursor + 4 + poly_count * 12 > end:
        raise MapLumpError(f"OCCLUSION declares {poly_count} polygons")
    polys = []
    for index in range(poly_count):
        row = cursor + 4 + index * 12
        first_vertex, vertex_count, plane = struct.unpack_from("<3i", data, row)
        polys.append(
            {
                "index": index,
                "sourceOffset": row,
                "firstVertexIndex": int(first_vertex),
                "vertexCount": int(vertex_count),
                "plane": int(plane),
            }
        )
    cursor += 4 + poly_count * 12
    vertex_count = struct.unpack_from("<i", data, cursor)[0]
    if vertex_count < 0 or cursor + 4 + vertex_count * 4 > end:
        raise MapLumpError(f"OCCLUSION declares {vertex_count} vertex indices")
    vertex_indices = (
        list(struct.unpack_from(f"<{vertex_count}i", data, cursor + 4)) if vertex_count else []
    )
    cursor += 4 + vertex_count * 4
    claim_list = [(offset, cursor - offset, "mapped", "occluders")]
    claim_list += claim_tools.tail(
        data, cursor, end - cursor, "occluders", "unused-lump-bytes", omissions, lump=span.index
    )
    return (
        {
            "sourceOffset": offset,
            "count": count,
            "data": occluder_rows,
            "polyData": polys,
            "vertexIndices": vertex_indices,
        },
        claim_list,
    )


def texdata_strings(data, table_span: LumpSpan, blob_span: LumpSpan, omissions):
    """Lump 44's offsets into lump 43's NUL-terminated names, and the bytes neither addresses."""

    table, table_claims = scalars(data, table_span, omissions, "textures.stringTable", "i")
    blob = data[blob_span.offset:blob_span.end]
    names: list[str] = []
    spans: set[tuple[int, int]] = set()
    for offset in table["values"]:
        if offset < 0 or offset >= len(blob):
            raise MapLumpError(f"TEXDATA_STRING_TABLE names byte {offset} of a {len(blob)}-byte blob")
        end = blob.find(b"\0", offset)
        if end < 0:
            raise MapLumpError(f"TEXDATA_STRING_DATA name at {offset} is unterminated")
        names.append(blob[offset:end].decode("latin-1"))
        spans.add((offset, end + 1))
    claim_list = list(table_claims)
    cursor = 0
    run_start: int | None = None
    for start, end in sorted(spans):
        if start > cursor:
            if run_start is not None:
                claim_list.append(
                    (
                        blob_span.offset + run_start,
                        cursor - run_start,
                        "mapped-string",
                        "textures.stringData",
                    )
                )
                run_start = None
            claim_list += claim_tools.tail(
                data,
                blob_span.offset + cursor,
                start - cursor,
                "textures.stringData",
                "unused-lump-bytes",
                omissions,
                lump=blob_span.index,
            )
        if run_start is None:
            run_start = start
        cursor = max(cursor, end)
    if run_start is not None:
        claim_list.append(
            (
                blob_span.offset + run_start,
                cursor - run_start,
                "mapped-string",
                "textures.stringData",
            )
        )
    if cursor < len(blob):
        claim_list += claim_tools.tail(
            data,
            blob_span.offset + cursor,
            len(blob) - cursor,
            "textures.stringData",
            "unused-lump-bytes",
            omissions,
            lump=blob_span.index,
        )
    return names, table, claim_list
