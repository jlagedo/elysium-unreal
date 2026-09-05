"""Ordered GLB morph records, independent of whether a native mesh is produced.

Dense morph arrays cannot establish this boundary. The caller pins the entire GLB's
SHA-256 and retains the GLB; this inventory identifies explicit zeros, repeated
contributions, undrawn vertices and entire source meshes with no LOD0 primitive.
"""
from collections import Counter
import hashlib
import json
import math


EXT = "ELYSIUM_vtmb_model"


def morph_source_inventory(document):
    root = document["extensions"][EXT]
    lods = [r for r in root["vtx"]["lods"] if r["index"] == 0]
    if len(lods) > 1:
        raise ValueError("duplicate source LOD0")
    primitives = document["meshes"][lods[0]["mesh"]]["primitives"] if lods else []
    inventory, drawn_meshes, counts = [], set(), Counter()

    def count_rows(rows, prefix, kind):
        repeated = Counter()
        for row in rows:
            values = [*row["position"], *row["normal"]]
            if len(row["position"]) != 3 or len(row["normal"]) != 3 or not all(math.isfinite(v) for v in values):
                raise ValueError("invalid source morph position/normal")
            counts[kind] += 1
            counts["explicitZeroRecords"] += int(not any(values))
            key = (row["target"] if kind == "primitiveRecords" else row["flex"], row["sourceVertex"])
            counts["repeatedContributions"] += int(repeated[key] > 0)
            repeated[key] += 1
            if kind == "primitiveRecords":
                counts["unrenderedVertexRecords"] += int(row["vertex"] is None)
            inventory.append([*prefix, row])

    for i, primitive in enumerate(primitives):
        source = primitive["extensions"][EXT]
        key = tuple(source[k] for k in ("bodyPart", "model", "mesh"))
        if key in drawn_meshes:
            raise ValueError("duplicate source LOD0 mesh")
        drawn_meshes.add(key)
        count_rows(source["morphRecords"], ["primitive", i], "primitiveRecords")
    for bp, part in enumerate(root["mdl"]["bodyParts"]):
        for model_index, model in enumerate(part["models"]):
            for mesh_index, mesh in enumerate(model["meshes"]):
                rows = mesh.get("unrenderedMorphRecords", [])
                if rows and (bp, model_index, mesh_index) in drawn_meshes:
                    raise ValueError("whole unrendered source mesh has LOD0 geometry")
                count_rows(rows, ["unrenderedMesh", bp, model_index, mesh_index], "unrenderedMeshRecords")
    return {"passed": True, "scope": "GLB-ordered-morph-source-inventory",
            "nativeDenseMorphsProveSourceInventory": False,
            "orderedRecordSha256": hashlib.sha256(json.dumps(inventory, sort_keys=True, separators=(",", ":")).encode()).hexdigest(),
            **{k: counts[k] for k in ("primitiveRecords", "explicitZeroRecords", "repeatedContributions",
                                      "unrenderedVertexRecords", "unrenderedMeshRecords")}}
