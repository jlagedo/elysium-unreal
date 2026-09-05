"""Tangent-channel inventory only; no claim that MikkTSpace reproduces authored tangents."""
from collections import Counter


def tangent_inventory(document):
    root = document["extensions"]["ELYSIUM_vtmb_model"]
    counts, joined = Counter(), set()
    for lod in root["vtx"]["lods"]:
        level = "lod0" if lod["index"] == 0 else "higherLod"
        for primitive in document["meshes"][lod["mesh"]]["primitives"]:
            counts[level + "Primitives"] += 1
            if "TANGENT" not in primitive["attributes"]:
                continue
            attr = document["accessors"][primitive["attributes"]["TANGENT"]]
            if attr["componentType"] != 5126 or attr["type"] != "VEC4":
                raise ValueError("authored core TANGENT is not FLOAT VEC4")
            source = primitive["extensions"]["ELYSIUM_vtmb_model"]
            if attr["count"] != len(source["sourceVertices"]):
                raise ValueError("TANGENT/source vertex count mismatch")
            counts[level + "TangentPrimitives"] += 1
            counts[level + "TangentVectors"] += attr["count"]
            joined.update((source["bodyPart"], source["model"], v) for v in source["sourceVertices"])
    for part in root["mdl"]["bodyParts"]:
        for model in part["models"]:
            if model["tangentsOffset"] > 0:
                counts["modelsWithAuthoredTangentPool"] += 1
                counts["authoredModelTangentRecords"] += model["vertexCount"]
            counts["unreferencedExtensionTangentVectors"] += sum(v.get("tangent") is not None for v in model["unreferencedVertices"])
    # Cloth tangent indices/interpolation are distinct source semantics, owned by the
    # cloth lane. Inventory their carriers without repeating simulation derivation.
    cloth_fields = Counter()

    def walk(value, path):
        if isinstance(value, dict):
            for key, item in value.items():
                child = path + "." + key
                if "tangent" in key.lower():
                    cloth_fields[child + ":fields"] += 1
                    if isinstance(item, list):
                        cloth_fields[child + ":items"] += len(item)
                if isinstance(item, (dict, list)):
                    walk(item, child)
        elif isinstance(value, list):
            for item in value:
                if isinstance(item, (dict, list)):
                    walk(item, path + "[]")

    walk(root.get("cloth", {}), "cloth")
    return {**dict(counts), "uniqueCoreSourceTangentVerticesAcrossLods": len(joined),
            "clothTangentFields": dict(cloth_fields), "sourceTangentValuesValidated": False,
            "stagedMeshCarriesTangents": False, "nativeTangentDerivationVerified": False,
            "nativeTangentBuffersCompared": False}
