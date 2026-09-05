"""Unreal-native garment projection joined to the GLB stage's actual vertex order."""
from collections import defaultdict
from copy import deepcopy
import hashlib
import json
from pathlib import Path
import struct

from elysium_pipeline.asset_names import rig_bone_name
from elysium_pipeline.asset_paths import baked_unit
from elysium_pipeline.formats import eskm
from elysium_pipeline.formats.bsp import source_to_unreal


def _native_mesh(blob):
    """Read the staged geometry verbatim; its positions/winding already use the native basis."""
    where = eskm.directory(blob).get(b"MESH")
    if where is None:
        raise ValueError("cloth owner has no staged mesh")
    at, size = where
    end = at + size
    count, triangles, materials = struct.unpack_from("<3I", blob, at)
    at += 12
    sections = []
    for _ in range(materials):
        name, at = eskm._string(blob, at)
        first, length = struct.unpack_from("<2I", blob, at)
        at += 8
        if first + length > triangles:
            raise ValueError("cloth source section exceeds staged triangles")
        sections.append((name, first, length))
    vertices = [struct.unpack_from("<8f4H4f", blob, at + index * 56) for index in range(count)]
    at += count * 56
    faces = [struct.unpack_from("<3I", blob, at + index * 12) for index in range(triangles)]
    at += triangles * 12
    if at != end or any(index >= count for face in faces for index in face):
        raise ValueError("invalid staged mesh geometry for cloth")
    bones = [name for name, _ in eskm.bones(blob)]
    surfaces = {}
    for material, first, length in sections:
        selected = faces[first:first + length]
        indices = sorted({vertex for face in selected for vertex in face})
        local = {vertex: index for index, vertex in enumerate(indices)}
        surface = {"material": material, "stageVertices": indices, "positions": [], "normals": [],
                   "uvs": [], "skin": [], "triangles": [[local[v] for v in face] for face in selected]}
        for vertex in indices:
            row = vertices[vertex]
            surface["positions"].append(list(row[:3]))
            surface["normals"].append(list(row[3:6]))
            surface["uvs"].append(list(row[6:8]))
            skin = []
            for bone, weight in zip(row[8:12], row[12:16]):
                if weight > 0:
                    if bone >= len(bones):
                        raise ValueError("cloth render skin names an absent staged bone")
                    skin.append([bones[bone], weight])
            if not skin:
                raise ValueError("cloth render vertex has no source skin")
            surface["skin"].append(skin)
        if material in surfaces:
            raise ValueError("duplicate staged material section in cloth owner")
        surfaces[material] = surface
    return surfaces


def _point(value):
    return list(source_to_unreal(*value))


def _skin(rows):
    return [[rig_bone_name(name), weight] for name, weight in rows]


def _convert_simulation(garment):
    out = deepcopy(garment)
    out["rest_positions"] = [_point(position) for position in out["rest_positions"]]
    out["triangles"] = [[a, c, b] for a, b, c in out["triangles"]]
    for constraint in out["constraints"]:
        constraint["rest_length_squared"] *= 2.54 ** 2
    # These two fields are counts, not additional constraint arrays.
    if out["distance_constraints"] + out["compression_constraints"] != len(out["constraints"]):
        raise ValueError("cloth constraint counts disagree")
    out["anchor_skin"] = {key: _skin(rows) for key, rows in out["anchor_skin"].items()}
    out["particle_skin"] = [_skin(rows) for rows in out["particle_skin"]]
    for capsule in out["capsules"]:
        for field in ("bone_a_name", "bone_b_name"):
            if capsule.get(field):
                capsule[field] = rig_bone_name(capsule[field])
        for field in ("a_bind", "b_bind"):
            capsule[field] = _point(capsule[field])
        capsule["radius"] *= 2.54
        capsule.pop("a_local", None)
        capsule.pop("b_local", None)
    for sphere in out["spheres"]:
        if sphere.get("bone_name"):
            sphere["bone_name"] = rig_bone_name(sphere["bone_name"])
        sphere["centre_bind"] = _point(sphere["centre_bind"])
        sphere["radius"] *= 2.54
        sphere.pop("centre_local", None)
    out["render_maps"] = []
    return out


def cloth_projection(body, mesh_blob):
    """Re-key every substitution/anchor using source identity, never a position-nearest join."""
    id = body["assetId"]
    semantics = body["sourceSemantics"]
    source = semantics["cloth"]
    garments = source.get("garments", [])
    if not garments:
        return {"schemaVersion": "1.0.0", "assetId": id, "garments": []}
    surfaces = _native_mesh(mesh_blob)
    owners = [(model, column) for model in source["sourceModels"] for column in range(model["columns"])]
    if len(owners) != len(garments):
        raise ValueError("cloth garment inventory does not match the source model definitions")
    joins = defaultdict(list)
    for mapping in body["renderVertexMap"]:
        joins[mapping["bodyPart"], mapping["model"], mapping["mesh"]].append(mapping)
    projected = []
    for ordinal, (garment, (owner, column)) in enumerate(zip(garments, owners)):
        if garment["definition"] != column:
            raise ValueError("cloth definition order changed")
        definitions = [row for row in owner["definitions"] if row["lod"] == 0 and row["column"] == column]
        if len(definitions) != 1:
            raise ValueError("cloth garment has no unique LOD0 source definition")
        definition = definitions[0]
        out = _convert_simulation(garment)
        out["bodyPart"], out["model"] = owner["bodyPart"], owner["model"]
        out["assetPath"] = baked_unit(id, "CLOTH", role=str(ordinal) if len(garments) > 1 else None)
        bindings, anchor_joins = defaultdict(dict), defaultdict(list)
        model = semantics["mdl"]["bodyParts"][owner["bodyPart"]]["models"][owner["model"]]
        for raw_map in owner["meshMaps"]:
            mesh_index = raw_map["mesh"]
            lods = [row for row in raw_map["lodRows"] if row["lod"] == 0]
            if len(lods) != 1:
                raise ValueError("cloth mesh map has no unique LOD0 row")
            raw = lods[0]
            source_mesh = model["meshes"][mesh_index]
            for mapping in joins[owner["bodyPart"], owner["model"], mesh_index]:
                material = mapping["material"]
                for source_vertex, stage_vertex in mapping["vertices"]:
                    offset = source_vertex - source_mesh["vertexOffset"]
                    if not 0 <= offset < source_mesh["vertexCount"] or offset >= len(raw["selectors"]):
                        raise ValueError("cloth source vertex is outside its mesh selector row")
                    anchor_joins[material].append((source_vertex, stage_vertex))
                    if raw["selectors"][offset] != column:
                        continue
                    if offset >= len(raw["positionNormal"]) or offset >= len(raw["tangent"]):
                        raise ValueError("drawn cloth vertex lacks position/normal/tangent mapping")
                    position = raw["positionNormal"][offset]
                    row = {"particle": position & 0x7fff, "tangent": raw["tangent"][offset],
                           "flip_normal": bool(position & 0x8000)}
                    if row["particle"] >= garment["particle_count"]:
                        raise ValueError("cloth substitution names an absent particle")
                    previous = bindings[material].get(stage_vertex)
                    if previous is not None and previous != row:
                        raise ValueError("two source records disagree on one staged cloth vertex")
                    bindings[material][stage_vertex] = row
        for material, rows in bindings.items():
            if not rows:
                continue
            anchors = defaultdict(list)
            for particle, vertex in enumerate(definition["anchor_vertex_indices"]):
                anchors[vertex].append(particle)
            for source_vertex, stage_vertex in anchor_joins[material]:
                if stage_vertex not in rows and source_vertex in anchors:
                    rows[stage_vertex] = {"particle": anchors[source_vertex][0], "tangent": None,
                                          "flip_normal": False, "anchor": True}
            if material not in surfaces:
                raise ValueError("cloth map material is absent from staged geometry")
            render = deepcopy(surfaces[material])
            if not set(rows).issubset(render["stageVertices"]):
                raise ValueError("cloth binding names a vertex outside its material section")
            render["vertices"] = [rows.get(vertex) for vertex in render["stageVertices"]]
            render["claimed"] = sum(row is not None for row in render["vertices"])
            render["total"] = len(render["vertices"])
            out["render_maps"].append(render)
        if not out["render_maps"]:
            raise ValueError("cloth garment has no drawn staged substitution map")
        projected.append(out)
    return {"schemaVersion": "1.0.0", "assetId": id, "model": "models/" + id.removeprefix("vtmb:model:") + ".mdl",
            "stem": id.removeprefix("vtmb:model:").rsplit("/", 1)[-1], "garments": projected}


def stage_cloth_data(stage_root, manifest, log=print):
    from elysium_pipeline.importers.characters import _write, _json_bytes

    root = Path(stage_root)
    selected = set(manifest["selectedUnits"])
    count = garments = 0
    for entry in manifest["assets"]:
        if entry["assetId"] not in selected:
            continue
        try:
            encoded = (root / entry["body"]).read_bytes()
            if hashlib.sha256(encoded).hexdigest() != entry["recipe"]["bodySha256"]:
                raise ValueError("cloth body semantics changed after staging")
            body = json.loads(encoded)
            entry["clothData"] = None
            entry["clothAssets"] = []
            entry["recipe"]["clothDataSha256"] = None
            if not (body["sourceSemantics"].get("cloth") or {}).get("garments"):
                continue
            if not entry["meshAsset"]:
                raise ValueError("cloth owner has no skeletal projection")
            blob = (root / entry["payload"]).read_bytes()
            if hashlib.sha256(blob).hexdigest() != entry["recipe"]["payloadSha256"]:
                raise ValueError("cloth owner geometry changed after staging")
            projection = cloth_projection(body, blob)
            encoded = _json_bytes(projection)
            relative = entry["key"] + ".cloth.json"
            _write(root / relative, encoded)
            entry["clothData"] = relative
            entry["clothAssets"] = [row["assetPath"] for row in projection["garments"]]
            entry["recipe"]["clothDataSha256"] = hashlib.sha256(encoded).hexdigest()
            count += 1
            garments += len(projection["garments"])
        except (ValueError, KeyError, OSError, IndexError, struct.error) as error:
            manifest["stageFailures"].append({"assetId": entry["assetId"], "reason": "cloth data: " + str(error)})
    _write(root / "manifest.json", _json_bytes(manifest))
    log(f"character cloth: {count} owners, {garments} garments; {len(manifest['stageFailures'])} stage failures")
    return manifest
