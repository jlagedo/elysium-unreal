"""The complete map-root decode: every lump the root owns, stated once.

The root is the unit that proves the partition, so this decode starts from
`partition.partition` and hands each region to the reader that owns it: a record array to
`lumps`, the PAKFILE container to `pakfile`, the `sprp`/`dprp` payloads to `gamelump`, lump 29 to
`physics`, and the three sub-units' spans to nobody -- they enter the ledger as `omitted-proven`
ranges naming the sibling that owns them.
"""

from __future__ import annotations

import math
from typing import Any, Callable

from elysium_pipeline.formats.map_glb import claims as claim_tools
from elysium_pipeline.formats.map_glb import gamelump, geometry, lumps, pakfile, physics
from elysium_pipeline.formats.map_glb import model as map_model
from elysium_pipeline.formats.map_glb.partition import Partition
from elysium_pipeline.formats.unit_contract import (
    MATERIAL_REFERENCE,
    MODEL_REFERENCE,
    TEXTURE_REFERENCE,
    dependency,
    reference_extension,
)

#: `texinfo.flags` bits the root marks a face by. The 3D-sky area split is the visibility unit's
#: join with the `sky_camera` entity; the root only says which faces are flagged.
SURF_SKY2D = 0x0002
SURF_SKY = 0x0004
SURF_NODRAW = 0x0080

#: `STATIC_PROP_FLAG_USE_LIGHTING_ORIGIN`: without it the record's lighting origin is storage
#: the engine never reads, which is where the uninitialized values in the lump sit.
STATIC_PROP_USE_LIGHTING_ORIGIN = 0x02

#: The lumps this decode reads a record array out of, and the reader that owns each.
TOOLS_PREFIX = "tools/"

#: `cubemapdefault` is the texture the engine substitutes for a cubemap sample it cannot find.
CUBEMAP_DEFAULT = "cubemapdefault"


class MapDecodeError(ValueError):
    """A map lump does not hold the records its census says it holds."""


def _leaf_areas(leaf_rows, leaf_faces, face_count: int) -> list[int | None]:
    areas: list[int | None] = [None] * face_count
    values = leaf_faces["values"]
    for leaf in leaf_rows:
        first, count = leaf["firstLeafFace"], leaf["numLeafFaces"]
        for step in range(count):
            position = first + step
            if position >= len(values):
                break
            face = values[position]
            if 0 <= face < face_count and areas[face] is None:
                areas[face] = leaf["area"]
    return areas


def _mopp_block(data, payloads, builder, typed_unidentified) -> dict[str, Any] | None:
    """The Havok MOPP solids: their bytes carried verbatim, and the reason they are carried.

    A MOPP solid's code is live collision data no public description covers, so it is neither
    decodable nor droppable. The bytes go into one described BIN accessor -- which is what the
    ledger's `mapped` state means by a verbatim payload -- and the solid enters
    `typedUnidentified`, where it counts against completeness until somebody decodes it.
    """

    if not payloads:
        return None
    blob = bytearray()
    entries = []
    for offset, length, owner in payloads:
        entries.append(
            {
                "owner": owner,
                "sourceOffset": offset,
                "byteLength": length,
                "firstByte": len(blob),
            }
        )
        blob.extend(data[offset:offset + length])
        typed_unidentified.append(
            {
                "field": owner + ".moppCode",
                "sourceOffset": offset,
                "byteLength": length,
                "reason": "Havok MOPP collision code; carried verbatim in the BIN payload "
                          "because no seam decodes its interior",
            }
        )
    return {
        "accessor": builder.bytes_payload(bytes(blob), "physics.moppCode"),
        "byteLength": len(blob),
        "entries": entries,
    }


def _placement(record, data, anomalies, role: str) -> dict[str, object] | None:
    """One prop record's glTF transform, or None when the record does not hold a finite one.

    A record whose origin or angles are not finite cannot be placed and cannot be written as
    JSON either, so the values are published as the bytes they were read from and the record is
    an anomaly rather than a node.
    """

    origin = tuple(record["origin"])
    angles = tuple(record["angles"])
    if all(math.isfinite(value) for value in origin + angles):
        return {
            "translation": list(geometry.position(*origin)),
            "rotation": geometry.quaternion_from_angles(*angles),
        }
    offset = int(record["sourceOffset"])
    record["placementRawHex"] = data[offset:offset + 24].hex()
    anomalies.append(
        {
            "role": f"{role}-placement-nonfinite",
            "index": record["index"],
            "sourceOffset": offset,
            "evidence": "the record's origin or angles are not finite, so the placement is "
                        "carried as placementRawHex instead of a node transform",
        }
    )
    return None


def decode_map(
    closure,
    *,
    member_exists: Callable[[str], bool] | None = None,
) -> map_model.MapModel:
    """Decode one map root from its source closure.

    `member_exists` answers whether the UP-first index holds an install-relative member, which is
    what a `dependencies` row's `resolved` states. Without it every reference outside the map's
    own PAKFILE publishes `resolved: false`, because this decode never looked.
    """

    data = closure.bsp.data
    part: Partition = closure.partition
    anomalies: list[dict[str, Any]] = [dict(row) for row in part.anomalies]
    omissions: list[dict[str, Any]] = []
    typed_unidentified: list[dict[str, Any]] = []
    omitted_proven: list[dict[str, Any]] = []
    unsupported: list[dict[str, Any]] = []
    unresolved: list[dict[str, Any]] = []
    claim_list: list[claim_tools.Claim] = []
    # A merged ledger range never leaves the partition region its owner names, so the offsets the
    # regions start at are where fusion stops.
    region_boundaries = {region.offset for region in part.regions} | {len(data)}

    def span(index: int):
        return part.lump(index)

    # --- the regions no record reader owns -------------------------------------------------
    decoded_lumps = {
        1, 2, 3, 5, 6, 7, 9, 10, 12, 13, 14, 16, 17, 18, 19, 20, 21, 26, 27, 29, 30, 31, 33,
        36, 37, 38, 39, 40, 41, 42, 43, 44, 46, 47, 48,
    }
    game_lump_payloads = {"sprp", "dprp"}
    for region in part.regions:
        if region.unit != "map":
            claim_list.append((region.offset, region.length, region.state, region.owner))
            omitted_proven.append(
                {
                    "role": "sub-unit-span",
                    "unit": region.unit,
                    "lump": region.lump,
                    "gameLump": region.game_lump,
                    "sourceOffset": region.offset,
                    "byteLength": region.length,
                    "sha256": region.sha256,
                    "evidence": f"{region.unit} publishes these bytes as its own span",
                }
            )
            continue
        if region.owner in {"header", "trailer"}:
            claim_list.append((region.offset, region.length, region.state, region.owner))
            continue
        if region.owner.endswith(".padding") or region.owner.endswith(".inter-lump-fill"):
            claim_list.append((region.offset, region.length, region.state, region.owner))
            if region.state == "omitted-proven":
                omissions.append(
                    {
                        "role": "inter-lump-fill",
                        "lump": region.lump,
                        "sourceOffset": region.offset,
                        "byteLength": region.length,
                        "sha256": region.sha256,
                        "evidence": "no directory row and no record addresses this range",
                    }
                )
            continue
        if region.lump is None or region.owner == "header.gameLumpDirectory":
            claim_list.append((region.offset, region.length, region.state, region.owner))
            continue
        if region.game_lump in game_lump_payloads:
            continue                       # decoded below, which claims the payload itself
        if region.lump not in decoded_lumps:
            claim_list.append((region.offset, region.length, "omitted-proven", "lumps.unsupported"))
            unsupported.append(
                {
                    "role": "unsupported-lump",
                    "lump": region.lump,
                    "name": part.lump(region.lump).name,
                    "sourceOffset": region.offset,
                    "byteLength": region.length,
                }
            )

    # --- record arrays ---------------------------------------------------------------------
    plane_rows, plane_claims = lumps.planes(data, span(1), omissions)
    texdata_rows, texdata_claims = lumps.texdata(data, span(2), omissions)
    node_rows, node_claims = lumps.nodes(data, span(5), omissions)
    texinfo_rows, texinfo_claims = lumps.texinfos(data, span(6), omissions)
    face_rows, face_claims = lumps.faces(data, span(7), omissions, "faces")
    occluder_block, occluder_claims = lumps.occluders(data, span(9), omissions)
    leaf_rows, leaf_claims = lumps.leafs(data, span(10), omissions)
    edge_table, edge_claims = lumps.edges(data, span(12), omissions)
    surf_edge_table, surf_edge_claims = lumps.scalars(
        data, span(13), omissions, "surfEdges", "i"
    )
    model_rows, model_claims = lumps.models(data, span(14), omissions)
    leaf_face_table, leaf_face_claims = lumps.scalars(
        data, span(16), omissions, "bsp.leafFaces", "H"
    )
    leaf_brush_table, leaf_brush_claims = lumps.scalars(
        data, span(17), omissions, "bsp.leafBrushes", "H"
    )
    brush_rows, brush_claims = lumps.brushes(data, span(18), omissions)
    brush_side_rows, brush_side_claims = lumps.brush_sides(data, span(19), omissions)
    area_rows, area_claims = lumps.areas(data, span(20), omissions)
    area_portal_rows, area_portal_claims = lumps.area_portals(data, span(21), omissions)
    disp_rows, disp_claims = lumps.dispinfos(data, span(26), omissions)
    original_face_rows, original_face_claims = lumps.faces(
        data, span(27), omissions, "originalFaces"
    )
    normal_index_table, normal_index_claims = lumps.scalars(
        data, span(31), omissions, "normalIndices", "H"
    )
    disp_vert_rows, disp_vert_claims = lumps.disp_verts(data, span(33), omissions)
    water_rows, water_claims = lumps.leaf_water(data, span(36), omissions)
    primitive_rows, primitive_claims = lumps.primitives(data, span(37), omissions)
    prim_vert_rows, prim_vert_claims = lumps.vectors(
        data, span(38), omissions, "primitives.verts"
    )
    prim_index_table, prim_index_claims = lumps.scalars(
        data, span(39), omissions, "primitives.indices", "H"
    )
    clip_portal_rows, clip_portal_claims = lumps.vectors(
        data, span(41), omissions, "bsp.clipPortalVerts"
    )
    cubemap_rows, cubemap_claims = lumps.cubemaps(data, span(42), omissions)
    texture_names, string_table, string_claims = lumps.texdata_strings(
        data, span(44), span(43), omissions
    )
    water_min_table, water_min_claims = lumps.scalars(
        data, span(46), omissions, "water.leafMinDist", "H"
    )
    macro_table, macro_claims = lumps.scalars(
        data, span(47), omissions, "faces.macroTexture", "h"
    )
    disp_tri_table, disp_tri_claims = lumps.scalars(
        data, span(48), omissions, "displacementTriangleTags", "H"
    )
    # Lumps 3 and 30 are read last of the record arrays, because what pays for their bytes is
    # which of their records an index lump names: a vertex or a vertex normal no edge and no
    # normal index addresses reaches no accessor, so its run is `omitted-proven`, not `mapped`.
    referenced_vertices = {value for edge in edge_table["values"] for value in edge}
    referenced_normals = set(normal_index_table["values"])
    vertex_points, vertex_claims, unreferenced_vertex_runs = lumps.raw_vectors(
        data, span(3), omissions, "meshes.position", referenced=referenced_vertices
    )
    normal_points, normal_claims, unreferenced_normal_runs = lumps.raw_vectors(
        data, span(30), omissions, "meshes.normal", referenced=referenced_normals
    )
    claim_list += (
        plane_claims + texdata_claims + vertex_claims + node_claims + texinfo_claims
        + face_claims + occluder_claims + leaf_claims + edge_claims + surf_edge_claims
        + model_claims + leaf_face_claims + leaf_brush_claims + brush_claims
        + brush_side_claims + area_claims + area_portal_claims + disp_claims
        + original_face_claims + normal_claims + normal_index_claims + disp_vert_claims
        + water_claims + primitive_claims + prim_vert_claims + prim_index_claims
        + clip_portal_claims + cubemap_claims + string_claims + water_min_claims
        + macro_claims + disp_tri_claims
    )

    # --- the census facts the root verifies per map -----------------------------------------
    day_night_zero = all(
        not any(face["dayStyles"]) and not any(face["nightStyles"])
        for face in face_rows + original_face_rows
    )
    # `dayStyles` and `nightStyles` are decoded and published per face, and their bytes are
    # claimed `mapped` inside the faces record array, so the fact that they are zero everywhere is
    # a census fact (`census.faceDayNightStylesZero`) and not an omission -- one datum carries one
    # coverage grade.
    if face_rows and not day_night_zero:
        anomalies.append(
            {
                "role": "face-daynight-nonzero",
                "evidence": "a face carries a non-zero day[8] or night[8] light-style array",
            }
        )

    # --- textures and their material identities ----------------------------------------------
    map_name = closure.key
    pak = pakfile.parse(data, span(40).offset, span(40).length, omissions)
    claim_list += list(pak.claims)
    pak_names = {member.name.lower(): member for member in pak.members}

    dependencies: list[dict[str, Any]] = []
    seen_dependencies: set[tuple[str, str]] = set()
    missing_models: set[str] = set()

    def add_dependency(role: str, asset: str, source: str, resolved: bool, **optional) -> None:
        key = (role, asset)
        if key in seen_dependencies:
            return
        seen_dependencies.add(key)
        dependencies.append(dependency(role, asset, source, resolved, **optional))

    def resolves(path: str) -> bool:
        if path.lower() in pak_names:
            return True
        return bool(member_exists(path)) if member_exists is not None else False

    materials: list[dict[str, Any]] = []
    material_index: dict[str, int] = {}
    texture_rows: list[dict[str, Any]] = []
    for row in texdata_rows:
        name_id = row["nameStringTableID"]
        if not 0 <= name_id < len(texture_names):
            raise MapDecodeError(
                f"texdata {row['index']} names string {name_id} of {len(texture_names)}"
            )
        name = texture_names[name_id]
        folded = name.replace("\\", "/").lower()
        if folded not in material_index:
            material_index[folded] = len(materials)
            materials.append({"name": name, "asset": map_model.material_asset_id(folded)})
        patched = map_model.split_patched_name(folded)
        source = map_model.material_source_path(folded)
        add_dependency(
            "material",
            map_model.material_asset_id(folded),
            source,
            resolves(source),
            **(
                {"sha256": pak_names[source].sha256, "byteLength": pak_names[source].uncompressed_size}
                if source in pak_names
                else {}
            ),
        )
        base_asset = None
        if patched is not None:
            base_name, origin = patched
            base_asset = map_model.material_asset_id(base_name)
            add_dependency(
                "material",
                base_asset,
                map_model.material_source_path(base_name),
                resolves(map_model.material_source_path(base_name)),
            )
        if (row["width"], row["height"]) != (row["viewWidth"], row["viewHeight"]):
            anomalies.append(
                {
                    "role": "texdata-view-size-mismatch",
                    "texdata": row["index"],
                    "sourceOffset": row["sourceOffset"],
                    "size": [row["width"], row["height"]],
                    "viewSize": [row["viewWidth"], row["viewHeight"]],
                    "evidence": "the TEXDATA record states two sizes for one texture",
                }
            )
        # The name is the string table's, stated there once: this row names the string by its
        # `nameStringTableID` and the unit it resolves to by `asset`.
        texture_rows.append(
            {
                **row,
                "material": material_index[folded],
                "asset": map_model.material_asset_id(folded),
                "patched": patched is not None,
                "baseAsset": base_asset,
                "cubemapOrigin": list(patched[1]) if patched is not None else None,
                "inPakfile": source in pak_names,
            }
        )

    # --- the PAKFILE members routed to the material and texture seams -------------------------
    pak_units: dict[int, str | None] = {}
    for member in pak.members:
        extension = member.extension
        if extension == ".vmt":
            asset = map_model.material_asset_id(
                member.name[len("materials/"):-len(".vmt")]
                if member.name.lower().startswith("materials/")
                else member.name
            )
            role = "material"
        elif extension in (".tth", ".ttz"):
            asset = map_model.texture_asset_id(member.name)
            role = "texture"
        else:
            pak_units[member.index] = None
            unsupported.append(
                {
                    "role": "pakfile-member",
                    "name": member.name,
                    "sourceOffset": member.data_offset,
                    "byteLength": member.compressed_size,
                    "evidence": "no seam claims this member kind",
                }
            )
            continue
        pak_units[member.index] = asset
        add_dependency(role, asset, member.name, True, sha256=member.sha256,
                       byteLength=member.uncompressed_size)

    # --- geometry ------------------------------------------------------------------------
    builder = geometry.Builder()
    for material in materials:
        builder.materials.append(
            {
                "name": material["name"],
                **reference_extension(MATERIAL_REFERENCE, material["asset"]),
            }
        )

    face_areas = _leaf_areas(leaf_rows, leaf_face_table, len(face_rows))
    normal_indices = normal_index_table["values"]
    running = 0
    face_normal_span: list[tuple[int, int]] = []
    for face in face_rows:
        face_normal_span.append((running, face["numEdges"]))
        running += face["numEdges"]
    if running != len(normal_indices) and normal_indices:
        anomalies.append(
            {
                "role": "vertnormalindices-count",
                "evidence": f"faces consume {running} normal indices; lump 31 holds "
                            f"{len(normal_indices)}",
            }
        )

    face_geometry: list[geometry.FaceGeometry | None] = [None] * len(face_rows)
    scene_nodes: dict[str, list[int]] = {
        "world": [], "brushModels": [], "displacements": [], "placements": []
    }

    def build_model_mesh(model_row, faces_of_model, name: str) -> int | None:
        groups: dict[int, dict[str, list]] = {}
        for face_index in faces_of_model:
            face = face_rows[face_index]
            if face["dispInfo"] >= 0:
                continue                    # the displacement mesh states this face's geometry
            tex_info = face["texInfo"]
            if not 0 <= tex_info < len(texinfo_rows):
                anomalies.append(
                    {"role": "face-texinfo-range", "face": face_index, "texInfo": tex_info}
                )
                continue
            texinfo = texinfo_rows[tex_info]
            tex_data = texinfo["texData"]
            if not 0 <= tex_data < len(texture_rows):
                anomalies.append(
                    {"role": "texinfo-texdata-range", "face": face_index, "texData": tex_data}
                )
                continue
            material = texture_rows[tex_data]["material"]
            group = groups.setdefault(
                material,
                {"position": [], "normal": [], "uv0": [], "uv1": [], "indices": []},
            )
            corners = geometry.face_vertices(face, surf_edge_table["values"], edge_table["values"])
            if len(corners) < 3:
                anomalies.append({"role": "degenerate-face", "face": face_index,
                                  "numEdges": face["numEdges"],
                                  "reason": "fewer than three edges"})
                continue
            if all(0 <= vertex < len(vertex_points) for vertex in corners) and not (
                geometry.winding_area([vertex_points[vertex] for vertex in corners]) > 0.0
            ):
                # The winding closes on itself -- collinear corners, or a corner stated twice.
                # The face still triangulates, so its bytes and its fan stay published and the
                # finding is stated rather than the geometry dropped.
                anomalies.append({"role": "degenerate-face", "face": face_index,
                                  "numEdges": face["numEdges"],
                                  "reason": "the winding encloses no area"})
            first_vertex = len(group["position"])
            first_index = len(group["indices"])
            start, count = face_normal_span[face_index]
            for step, vertex in enumerate(corners):
                if not 0 <= vertex < len(vertex_points):
                    raise MapDecodeError(f"face {face_index} names vertex {vertex}")
                point = vertex_points[vertex]
                group["position"].append(geometry.position(*point))
                normal_slot = start + step
                normal_id = (
                    normal_indices[normal_slot] if normal_slot < len(normal_indices) else -1
                )
                if 0 <= normal_id < len(normal_points):
                    group["normal"].append(geometry.direction(*normal_points[normal_id]))
                else:
                    # Lumps 30 and 31 hold a normal for every face vertex on all 108 maps. A
                    # map where they do not gets the substitution stated rather than hidden.
                    anomalies.append(
                        {
                            "role": "missing-vertex-normal",
                            "face": face_index,
                            "faceVertex": step,
                            "normalIndex": normal_id,
                            "evidence": "lump 31 names no vertex normal for this face vertex; "
                                        "the NORMAL accessor carries (0, 1, 0) in its place",
                        }
                    )
                    group["normal"].append((0.0, 1.0, 0.0))
                group["uv0"].append(
                    geometry.texture_coordinates(
                        point,
                        texinfo["textureVecs"],
                        texture_rows[tex_data]["width"],
                        texture_rows[tex_data]["height"],
                    )
                )
                group["uv1"].append(
                    geometry.luxel_coordinates(
                        point, texinfo["lightmapVecs"], face["lightmapMins"]
                    )
                )
            for step in range(1, len(corners) - 1):
                group["indices"].extend(
                    [first_vertex, first_vertex + step, first_vertex + step + 1]
                )
            face_geometry[face_index] = geometry.FaceGeometry(
                primitive=material,
                first_index=first_index,
                index_count=(len(corners) - 2) * 3,
                first_vertex=first_vertex,
                vertex_count=len(corners),
            )
        if not groups:
            return None
        primitives = []
        order = sorted(groups)
        slots = {material: position for position, material in enumerate(order)}
        for material in order:
            group = groups[material]
            primitives.append(
                {
                    "attributes": {
                        "POSITION": builder.vectors(group["position"], 3, f"{name}.position"),
                        "NORMAL": builder.vectors(group["normal"], 3, f"{name}.normal"),
                        "TEXCOORD_0": builder.vectors(group["uv0"], 2, f"{name}.uv"),
                        "TEXCOORD_1": builder.vectors(group["uv1"], 2, f"{name}.luxel"),
                    },
                    "indices": builder.indices(group["indices"], f"{name}.indices"),
                    "material": material,
                    "mode": 4,
                }
            )
        for face_index in faces_of_model:
            record = face_geometry[face_index]
            if record is not None:
                face_geometry[face_index] = geometry.FaceGeometry(
                    primitive=slots[record.primitive],
                    first_index=record.first_index,
                    index_count=record.index_count,
                    first_vertex=record.first_vertex,
                    vertex_count=record.vertex_count,
                )
        return builder.mesh(name, primitives)

    model_nodes: list[int | None] = []
    for model_row in model_rows:
        first, count = model_row["firstFace"], model_row["numFaces"]
        indices = [
            index for index in range(first, first + count) if 0 <= index < len(face_rows)
        ]
        if len(indices) != count:
            anomalies.append(
                {"role": "face-outside-model", "model": model_row["index"],
                 "firstFace": first, "numFaces": count}
            )
        name = "world" if model_row["index"] == 0 else f"brushModel[{model_row['index']}]"
        mesh = build_model_mesh(model_row, indices, name)
        if mesh is None:
            model_nodes.append(None)
            continue
        node = {"name": name, "mesh": mesh}
        origin = model_row["origin"]
        if any(origin):
            node["translation"] = list(geometry.position(*origin))
        node_index = builder.node(node)
        model_nodes.append(node_index)
        scene_nodes["world" if model_row["index"] == 0 else "brushModels"].append(node_index)

    covered = [False] * len(face_rows)
    for model_row in model_rows:
        for index in range(model_row["firstFace"], model_row["firstFace"] + model_row["numFaces"]):
            if 0 <= index < len(covered):
                covered[index] = True
    orphan_faces = [index for index, seen in enumerate(covered) if not seen]
    if orphan_faces:
        anomalies.append(
            {"role": "face-outside-model", "faces": orphan_faces[:32],
             "count": len(orphan_faces),
             "evidence": "no dmodel_t face range covers these faces"}
        )

    # --- displacements ---------------------------------------------------------------------
    displacement_rows: list[dict[str, Any]] = []
    disp_face = {}
    for face_index, face in enumerate(face_rows):
        if face["dispInfo"] >= 0:
            disp_face.setdefault(face["dispInfo"], face_index)
    for record in disp_rows:
        face_index = disp_face.get(record["index"])
        row = dict(record)
        side = (1 << record["power"]) + 1
        vertex_count = side * side
        triangle_count = 2 * (side - 1) * (side - 1)
        row["vertexCount"] = vertex_count
        row["triangleCount"] = triangle_count
        row["face"] = face_index
        row["mesh"] = None
        row["node"] = None
        if face_index is None:
            anomalies.append(
                {"role": "displacement-without-face", "displacement": record["index"]}
            )
            displacement_rows.append(row)
            continue
        face = face_rows[face_index]
        corners = [
            vertex_points[vertex]
            for vertex in geometry.face_vertices(
                face, surf_edge_table["values"], edge_table["values"]
            )
        ]
        start = record["dispVertStart"]
        grid = disp_vert_rows[start:start + vertex_count]
        if len(grid) != vertex_count:
            anomalies.append(
                {"role": "displacement-vertex-range", "displacement": record["index"]}
            )
            displacement_rows.append(row)
            continue
        points, alphas, triangles = geometry.displacement_grid(
            corners, record["startPosition"], record["power"], grid
        )
        normals = geometry.smooth_normals(points, triangles)
        name = f"displacement[{record['index']}]"
        tex_info = face["texInfo"]
        material = None
        if 0 <= tex_info < len(texinfo_rows):
            tex_data = texinfo_rows[tex_info]["texData"]
            if 0 <= tex_data < len(texture_rows):
                material = texture_rows[tex_data]["material"]
        primitive = {
            "attributes": {
                "POSITION": builder.vectors(points, 3, f"{name}.position"),
                "NORMAL": builder.vectors(normals, 3, f"{name}.normal"),
                "_ALPHA": builder.scalars(alphas, f"{name}.alpha"),
            },
            "indices": builder.indices(triangles, f"{name}.indices"),
            "mode": 4,
        }
        if material is not None:
            primitive["material"] = material
        mesh = builder.mesh(name, [primitive])
        node = builder.node({"name": name, "mesh": mesh})
        scene_nodes["displacements"].append(node)
        row["mesh"] = mesh
        row["node"] = node
        displacement_rows.append(row)

    # --- game lumps ------------------------------------------------------------------------
    static_props: dict[str, Any] = {"version": None, "dictionary": [], "leaves": {}, "props": []}
    detail_props: dict[str, Any] = {"version": None, "dictionary": [], "sprites": [], "records": []}
    uninitialized_lighting_origins: list[int] = []
    sprp = part.game_lump("sprp")
    if sprp is not None and sprp.length:
        decoded = gamelump.decode_static_props(data, sprp.offset, sprp.length, sprp.version)
        claim_list += list(decoded.claims)
        static_props = decoded.block
        if static_props["trailingBytes"]:
            claim_list += claim_tools.tail(
                data,
                sprp.end - static_props["trailingBytes"],
                static_props["trailingBytes"],
                "staticProps",
                "unused-lump-bytes",
                omissions,
                lump=35,
            )
        for prop in static_props["props"]:
            name = (
                decoded.dictionary[prop["propType"]]
                if 0 <= prop["propType"] < len(decoded.dictionary)
                else None
            )
            asset = None
            if name is None:
                # The record still states a placement and a lighting origin, so it goes through
                # the same normalisation as every other prop: one row shape, one coordinate
                # system, whatever the dictionary index says.
                anomalies.append(
                    {"role": "static-prop-dictionary-range", "prop": prop["index"],
                     "propType": prop["propType"]}
                )
            else:
                path = name.replace("\\", "/").lower()
                present = resolves(path)
                asset = (
                    map_model.model_asset_id(path)
                    if present
                    else map_model.missing_model_asset_id(path)
                )
                if present:
                    add_dependency("model", asset, path, True)
                elif asset not in missing_models:
                    missing_models.add(asset)
                    omitted_proven.append(
                        {
                            "role": "missing-model",
                            "asset": asset,
                            "sourcePath": path,
                            "evidence": "the UP-first index holds no member for this "
                                        "dictionary entry",
                        }
                    )
            placement = _placement(prop, data, anomalies, "static-prop")
            node = None
            if placement is not None:
                node_record: dict[str, Any] = {
                    "name": f"staticProp[{prop['index']}]",
                    **placement,
                }
                if asset is not None:
                    node_record.update(reference_extension(MODEL_REFERENCE, asset))
                node = builder.node(node_record)
                scene_nodes["placements"].append(node)
            prop["node"] = node
            prop["asset"] = asset
            prop["usesLightingOrigin"] = bool(prop["flags"] & STATIC_PROP_USE_LIGHTING_ORIGIN)
            if all(math.isfinite(value) for value in prop["lightingOrigin"]):
                prop["lightingOrigin"] = list(geometry.position(*prop["lightingOrigin"]))
            else:
                # `lightingOrigin` is dead storage unless the prop asks the engine to read it;
                # the census over all 108 maps finds every non-finite value on a prop whose
                # STATIC_PROP_USE_LIGHTING_ORIGIN flag is clear.
                raw = data[prop["sourceOffset"] + 44:prop["sourceOffset"] + 56]
                prop["lightingOrigin"] = None
                prop["lightingOriginRawHex"] = raw.hex()
                uninitialized_lighting_origins.append(prop["index"])
                if prop["usesLightingOrigin"]:
                    anomalies.append(
                        {
                            "role": "static-prop-lighting-origin-nonfinite",
                            "prop": prop["index"],
                            "sourceOffset": prop["sourceOffset"],
                            "evidence": "the prop asks the engine to read a lighting origin the "
                                        "record does not hold a finite value for",
                        }
                    )
            del prop["origin"]
            del prop["angles"]
        if uninitialized_lighting_origins:
            omitted_proven.append(
                {
                    "role": "static-prop-lighting-origin-uninitialized",
                    "props": uninitialized_lighting_origins[:32],
                    "count": len(uninitialized_lighting_origins),
                    "evidence": "every prop with a non-finite lighting origin carries "
                                "STATIC_PROP_FLAG_USE_LIGHTING_ORIGIN clear, so the engine "
                                "never reads the field; the bytes are kept as "
                                "staticProps.props[].lightingOriginRawHex",
                }
            )
    dprp = part.game_lump("dprp")
    if dprp is not None and dprp.length:
        decoded = gamelump.decode_detail_props(data, dprp.offset, dprp.length, dprp.version)
        claim_list += list(decoded.claims)
        detail_props = decoded.block
        if detail_props["trailingBytes"]:
            claim_list += claim_tools.tail(
                data,
                dprp.end - detail_props["trailingBytes"],
                detail_props["trailingBytes"],
                "detailProps",
                "detail-prop-trailing-bytes",
                omissions,
                lump=35,
            )
        detail_props["spritesOmitted"] = {
            "reason": "dprp version 2 carries no sprite dictionary",
            "evidence": "the dictionary is followed directly by the object count on all 108 maps",
        }
        for record in detail_props["records"]:
            name = (
                decoded.dictionary[record["detailModel"]]
                if 0 <= record["detailModel"] < len(decoded.dictionary)
                else None
            )
            asset = None
            if name is None:
                anomalies.append(
                    {"role": "detail-prop-dictionary-range", "detailProp": record["index"],
                     "detailModel": record["detailModel"]}
                )
            else:
                path = name.replace("\\", "/").lower()
                present = resolves(path)
                asset = (
                    map_model.model_asset_id(path)
                    if present
                    else map_model.missing_model_asset_id(path)
                )
                if present:
                    add_dependency("model", asset, path, True)
                elif asset not in missing_models:
                    # A sentinel names no unit, so it produces no dependency row; the reference
                    # is graded here instead, once per absent model.
                    missing_models.add(asset)
                    omitted_proven.append(
                        {
                            "role": "missing-model",
                            "asset": asset,
                            "sourcePath": path,
                            "evidence": "the UP-first index holds no member for this "
                                        "dictionary entry",
                        }
                    )
            placement = _placement(record, data, anomalies, "detail-prop")
            node = None
            if placement is not None:
                node_record: dict[str, Any] = {
                    "name": f"detailProp[{record['index']}]",
                    **placement,
                }
                if asset is not None:
                    node_record.update(reference_extension(MODEL_REFERENCE, asset))
                node = builder.node(node_record)
                scene_nodes["placements"].append(node)
            record["node"] = node
            record["asset"] = asset
            del record["origin"]
            del record["angles"]

    # --- cubemaps ---------------------------------------------------------------------------
    cubemap_out: list[dict[str, Any]] = []
    for record in cubemap_rows:
        x, y, z = record["origin"]
        member = f"materials/maps/{map_name}/c{x}_{y}_{z}.tth"
        asset = map_model.texture_asset_id(member)
        present = resolves(member)
        add_dependency("texture", asset, member, present)
        if not present:
            # Another seam's data that merely fails to resolve warns rather than failing the
            # unit, so the reference keeps its dependency row and says so here.
            anomalies.append(
                {
                    "role": "unresolved-cubemap-probe",
                    "reason": f"the install holds no reflection probe for {asset}",
                    "asset": asset,
                    "sourcePath": member,
                    "sourceOffset": record["sourceOffset"],
                    "evidence": "neither the map's PAKFILE nor the UP-first index holds the "
                                "baked reflection probe this sample names",
                }
            )
        node = builder.node(
            {
                "name": f"cubemap[{record['index']}]",
                "translation": list(geometry.position(x, y, z)),
                **reference_extension(TEXTURE_REFERENCE, asset),
            }
        )
        scene_nodes["placements"].append(node)
        cubemap_out.append({**record, "asset": asset, "resolved": present, "node": node})
    default_member = f"materials/maps/{map_name}/{CUBEMAP_DEFAULT}.tth"
    if cubemap_rows:
        add_dependency(
            "texture",
            map_model.texture_asset_id(default_member),
            default_member,
            resolves(default_member),
        )

    # --- VPhysics ---------------------------------------------------------------------------
    physics_span = span(29)
    physics_block: dict[str, Any] = {"models": [], "coordinateSystem": "IVP metres, axis-only"}
    if physics_span.populated:
        decoded_physics = physics.decode(data, physics_span.offset, physics_span.length)
        claim_list += claim_tools.merge(decoded_physics.claims)
        anomalies.extend(decoded_physics.anomalies)
        omissions.extend(decoded_physics.omissions)
        physics_block = {
            "sourceOffset": physics_span.offset,
            "byteLength": physics_span.length,
            "coordinateSystem": "IVP metres, axis-only",
            "positionAccessor": builder.vectors(
                decoded_physics.vertices, 3, "physics.hullPositions"
            )
            if decoded_physics.vertices
            else None,
            "indexAccessor": builder.indices(decoded_physics.indices, "physics.hullIndices")
            if decoded_physics.indices
            else None,
            "models": decoded_physics.models,
            "moppCode": _mopp_block(data, decoded_physics.mopp, builder, typed_unidentified),
            "triangleRecords": {
                "recordBytes": physics.TRIANGLE_BYTES,
                "note": "each IVP_Compact_Triangle states three start points, which are the "
                        "index accessor, and three opposite-edge links, which are that same "
                        "topology's adjacency",
            },
        }
        for name in dict.fromkeys(decoded_physics.surface_properties):
            add_dependency(
                "surface-property",
                map_model.surface_property_asset_id(name),
                "scripts/surfaceproperties.txt",
                member_exists("scripts/surfaceproperties.txt") if member_exists else False,
            )

    # --- the sibling units ---------------------------------------------------------------
    sub_units = closure.sub_unit_rows()
    for kind, _suffix in map_model.SUB_UNITS:
        add_dependency(
            kind,
            map_model.sub_asset_id(kind, map_name),
            closure.bsp.path,
            True,
        )

    # --- scenes ---------------------------------------------------------------------------
    scene_order = ["world", "brushModels", "displacements", "placements"]
    for name in scene_order:
        builder.scene(name, scene_nodes[name])

    faces_out: list[dict[str, Any]] = []
    for index, face in enumerate(face_rows):
        record = face_geometry[index]
        start, count = face_normal_span[index]
        texinfo = texinfo_rows[face["texInfo"]] if 0 <= face["texInfo"] < len(texinfo_rows) else None
        flags = texinfo["flags"] if texinfo else 0
        tex_data = texinfo["texData"] if texinfo else -1
        name = (
            texture_names[texture_rows[tex_data]["nameStringTableID"]]
            if 0 <= tex_data < len(texture_rows)
            else None
        )
        faces_out.append(
            {
                **face,
                "bspArea": face_areas[index],
                "material": texture_rows[tex_data]["material"] if 0 <= tex_data < len(texture_rows) else None,
                "tool": bool(name and name.replace("\\", "/").lower().startswith(TOOLS_PREFIX)),
                "sky": bool(flags & (SURF_SKY | SURF_SKY2D)),
                "noDraw": bool(flags & SURF_NODRAW),
                "normalIndexStart": start,
                "normalIndexCount": count,
                "primitive": record.primitive if record else None,
                "firstIndex": record.first_index if record else None,
                "indexCount": record.index_count if record else None,
                "firstVertex": record.first_vertex if record else None,
                "vertexCount": record.vertex_count if record else None,
                "macroTexture": macro_table["values"][index]
                if index < len(macro_table["values"])
                else None,
            }
        )

    if part.trailer is not None:
        typed_unidentified.append(
            {
                "field": "trailer",
                "sourceOffset": part.trailer["sourceOffset"],
                "byteLength": part.trailer["byteLength"],
                "value": part.trailer["value"],
                "signature": part.trailer["signature"],
                "matchesEntitiesLumpOffset": part.trailer["value"] == part.lump(0).offset,
                "reason": "a 10-byte tail every map carries; its value is lump 0's offset on all "
                          "108 maps and nothing in the format declares what reads it",
            }
        )

    header = {
        "ident": part.ident,
        "version": part.version,
        "mapRevision": part.map_revision,
        "lumpCount": len(part.lumps),
        "lumps": [row.to_json() for row in part.lumps],
        "gameLumpDirectory": {
            "sourceOffset": span(35).offset,
            "count": len(part.game_lumps),
            "entries": [entry.to_json() for entry in part.game_lumps],
        },
    }

    # --- the spatial lumps, restated in glTF space --------------------------------------------
    # Every positional record is transformed once, here, after the geometry that reads the source
    # values has been built; the axis map turns a bounding pair inside out on two axes, so the
    # corners are re-sorted rather than mapped in place.
    def _bounds(low, high):
        first, second = geometry.position(*low), geometry.position(*high)
        return (
            [min(first[axis], second[axis]) for axis in range(3)],
            [max(first[axis], second[axis]) for axis in range(3)],
        )

    for row in plane_rows:
        row["normal"] = list(geometry.direction(*row["normal"]))
        row["dist"] = row["dist"] * map_model.INCH_TO_METRE
    for row in model_rows:
        row["mins"], row["maxs"] = _bounds(row["mins"], row["maxs"])
        row["origin"] = list(geometry.position(*row["origin"]))
    for row in displacement_rows:
        row["startPosition"] = list(geometry.position(*row["startPosition"]))
    for row in disp_vert_rows:
        row["vector"] = list(geometry.direction(*row["vector"]))
        row["dist"] = row["dist"] * map_model.INCH_TO_METRE
    for row in prim_vert_rows + clip_portal_rows:
        row["point"] = list(geometry.position(*row["point"]))
    for row in water_rows:
        row["surfaceZ"] = row["surfaceZ"] * map_model.INCH_TO_METRE
        row["minZ"] = row["minZ"] * map_model.INCH_TO_METRE
    for row in occluder_block.get("data") or []:
        row["mins"], row["maxs"] = _bounds(row["mins"], row["maxs"])

    # A vertex or a vertex normal no record addresses reaches no accessor, so its bytes were
    # claimed `omitted-proven` above rather than `mapped`; the runs that were claimed that way
    # are the evidence this row carries.
    vertex_count = len(vertex_points)
    normal_count = len(normal_points)
    referenced_vertex_count = len(referenced_vertices & set(range(vertex_count)))
    referenced_normal_count = len(referenced_normals & set(range(normal_count)))
    for label, total, referenced, runs, owner in (
        ("vertexes", vertex_count, referenced_vertex_count, unreferenced_vertex_runs,
         "meshes.position.unreferenced"),
        ("vertexNormals", normal_count, referenced_normal_count, unreferenced_normal_runs,
         "meshes.normal.unreferenced"),
    ):
        if total > referenced:
            omitted_proven.append(
                {
                    "role": f"unreferenced-{label}",
                    "count": total - referenced,
                    "of": total,
                    "owner": owner,
                    "ranges": runs,
                    "evidence": "no edge or normal index of this map names these records, so "
                                "they reach no accessor; their bytes are claimed omitted-proven "
                                "under this owner",
                }
            )

    census = {
        "referencedVertexes": referenced_vertex_count,
        "referencedVertexNormals": referenced_normal_count,
        "faces": len(face_rows),
        "originalFaces": len(original_face_rows),
        "planes": len(plane_rows),
        "vertexes": len(vertex_points),
        "vertexNormals": len(normal_points),
        "texinfos": len(texinfo_rows),
        "texdatas": len(texture_rows),
        "brushModels": len(model_rows),
        "displacements": len(displacement_rows),
        "staticProps": len(static_props.get("props") or []),
        "detailProps": len(detail_props.get("records") or []),
        "cubemaps": len(cubemap_out),
        "pakfileMembers": len(pak.members),
        "physicsModels": len(physics_block.get("models") or []),
        "directoryVersionsZero": not any(
            row["role"] == "directory-version-nonzero" for row in anomalies
        ),
        "faceDayNightStylesZero": day_night_zero,
    }

    document_scenes = {
        "scenes": builder.scenes,
        "nodes": builder.nodes,
        "meshes": builder.meshes,
        "materials": builder.materials,
        "accessors": builder.accessors,
        "bufferViews": builder.buffer_views,
    }

    return map_model.MapModel(
        key=map_name,
        asset_id=closure.asset_id,
        source_path=closure.bsp.path,
        source=closure.bsp,
        partition=part,
        header=header,
        trailer=part.trailer,
        sub_units=sub_units,
        planes=plane_rows,
        textures=texture_rows,
        texture_strings={
            "table": string_table,
            "names": texture_names,
            "blobSourceOffset": span(43).offset,
            "blobByteLength": span(43).length,
        },
        texinfos=texinfo_rows,
        faces=faces_out,
        original_faces=original_face_rows,
        edges=edge_table,
        surf_edges=surf_edge_table,
        normal_indices=normal_index_table,
        displacement_triangle_tags=disp_tri_table,
        models=[
            {**row, "node": model_nodes[row["index"]] if row["index"] < len(model_nodes) else None}
            for row in model_rows
        ],
        bsp={
            "nodes": node_rows,
            "leafs": leaf_rows,
            "leafFaces": leaf_face_table,
            "leafBrushes": leaf_brush_table,
            "areas": area_rows,
            "areaPortals": area_portal_rows,
            "clipPortalVerts": clip_portal_rows,
        },
        collision={"brushes": brush_rows, "brushSides": brush_side_rows},
        displacements=displacement_rows,
        primitives={
            "primitives": primitive_rows,
            "verts": prim_vert_rows,
            "indices": prim_index_table,
        },
        physics=physics_block,
        water={"leafData": water_rows, "leafMinDist": water_min_table},
        cubemaps=cubemap_out,
        occluders=occluder_block,
        static_props=static_props,
        detail_props=detail_props,
        pakfile={
            **pak.to_json(pak_units),
            # Every member's `bsp-pakfile` origin is this one with the entry's `name` as its
            # `member`, so the shared part is stated once instead of per entry.
            "memberOrigin": {
                "kind": "bsp-pakfile",
                "map": map_name,
                "member": None,
                "origin": closure.bsp.origin.to_json(),
            },
        },
        scenes=document_scenes,
        binary=bytes(builder.payload),
        dependencies=dependencies,
        anomalies=anomalies,
        omissions=omissions,
        census=census,
        claims=claim_tools.merge(claim_list, boundaries=region_boundaries),
        typed_unidentified=typed_unidentified,
        omitted_proven=omitted_proven,
        unresolved=unresolved,
        unsupported=unsupported,
        mapped=[
            "header", "partition", "subUnits", "planes", "textures", "textureStrings",
            "texinfos", "faces", "originalFaces", "edges", "surfEdges", "normalIndices",
            "models", "bsp", "collision", "displacements", "displacementTriangleTags",
            "primitives", "physics", "water", "cubemaps", "occluders", "staticProps",
            "detailProps", "pakfile",
        ],
    )
