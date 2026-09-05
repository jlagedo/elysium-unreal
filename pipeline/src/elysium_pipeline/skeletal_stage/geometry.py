"""LOD0 and morph projection, retaining source vertex/slot joins for cloth and skins."""
import struct

from elysium_pipeline.skeletal_stage import payload
from elysium_pipeline.skeletal_stage.unit import MODEL_EXTENSION, SkeletalUnitError, source_position, source_direction


def geometry(unit, bone_map):
    lod = next((row for row in unit.extension["vtx"]["lods"] if row["index"] == 0), None)
    if lod is None:
        return b"", b"", [], [], []
    mesh = unit.document["meshes"][lod["mesh"]]
    surfaces, remaps, bindings = {}, [], {}
    for p, primitive in enumerate(mesh["primitives"]):
        source = primitive["extensions"][MODEL_EXTENSION]
        slot = source["skinReference"]
        name = unit.mdl["textures"][slot]["name"]
        material = primitive["extensions"]["ELYSIUM_material_reference"]["asset"]
        if name in bindings and bindings[name] != material:
            raise SkeletalUnitError(f"{unit.id}: slot {name} binds two different material units")
        bindings[name] = material
        surface = surfaces.setdefault(name, {k: [] for k in ("pos", "nrm", "uv", "joints", "weights", "tris")})
        attrs = primitive["attributes"]
        required = ("POSITION", "NORMAL", "TEXCOORD_0", "JOINTS_0", "WEIGHTS_0")
        if any(key not in attrs for key in required):
            raise SkeletalUnitError(f"{unit.id}: skeletal primitive {p} lacks a required channel")
        values = {key: unit.accessor(attrs[key]) for key in required}
        count = len(values["POSITION"])
        positions = unit.precise(source["sourcePositions"], (count, 3))
        normals = unit.precise(source["sourceNormals"], (count, 3))
        if any(len(v) != count for v in values.values()) or len(source["sourceVertices"]) != count:
            raise SkeletalUnitError(f"{unit.id}: primitive {p} channel lengths disagree")
        indices = unit.accessor(primitive["indices"]).reshape(-1)
        if len(indices) % 3 or any(i < 0 or i >= count for i in indices):
            raise SkeletalUnitError(f"{unit.id}: primitive {p} has invalid triangles")
        remap = {}
        for triangle in indices.reshape(-1, 3):
            out = []
            for raw_index in triangle:
                i = int(raw_index)
                if i not in remap:
                    remap[i] = len(surface["pos"])
                    surface["pos"].append(tuple(positions[i]))
                    surface["nrm"].append(tuple(normals[i]))
                    surface["uv"].append(tuple(values["TEXCOORD_0"][i]))
                    joints = [int(v) for v in values["JOINTS_0"][i]]
                    if any(j < 0 or j >= len(bone_map) for j in joints):
                        raise SkeletalUnitError(f"{unit.id}: vertex references a missing bone")
                    surface["joints"].append(joints)
                    surface["weights"].append(tuple(values["WEIGHTS_0"][i]))
                out.append(remap[i])
            surface["tris"].append(tuple(out))
        remaps.append((primitive, source, name, remap))
    names = list(surfaces)
    mesh_bytes, offsets = payload._mesh_section(surfaces, names, bone_map)
    targets = unit.extension["facial"]["morphTargets"]
    morph_bytes = bytearray(struct.pack("<I", len(targets))) if targets else bytearray()
    for target in targets:
        bucket = {}
        for primitive, source, name, remap in remaps:
            if "morphRecords" not in source:
                raise SkeletalUnitError(f"{unit.id}: morph source records absent; re-export this model as schema 2.1.0")
            for row in source["morphRecords"]:
                if row["target"] != target["index"] or row["vertex"] is None:
                    continue
                local = row["vertex"]
                if local not in remap:
                    raise SkeletalUnitError(f"{unit.id}: morph source names a missing drawn vertex")
                if source["sourceVertices"][local] != row["sourceVertex"]:
                    raise SkeletalUnitError(f"{unit.id}: morph source identity disagrees with its primitive")
                vertex = offsets[name] + remap[local]
                values = (*payload._conv_pos(row["position"]), *payload._conv_dir(row["normal"]))
                accumulated = bucket.setdefault(vertex, [0.] * 6)
                for axis, value in enumerate(values):
                    accumulated[axis] += float(value)
        morph_bytes += payload._string(target["name"]) + struct.pack("<I", len(bucket))
        for vertex, values in sorted(bucket.items()):
            morph_bytes += struct.pack("<I6f", vertex, *values)
    vertex_map = [{"bodyPart": source["bodyPart"], "model": source["model"],
                   "mesh": source["mesh"], "material": name,
                   "vertices": [[source["sourceVertices"][local], offsets[name] + index]
                                for local, index in remap.items()]}
                  for _, source, name, remap in remaps]
    return mesh_bytes, bytes(morph_bytes), names, [{"slot": name, "assetId": bindings[name]} for name in names], vertex_map
