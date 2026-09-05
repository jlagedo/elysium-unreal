from collections import Counter
import hashlib
import json
import os
from pathlib import Path
import struct

import numpy as np
import pytest

from elysium_pipeline.validation.native_geometry_tangents import tangent_inventory
from elysium_pipeline.formats.unit_contract.container import decode_glb
from elysium_pipeline.validation.native_geometry import accessor


def document():
    return {"accessors": [{"componentType": 5126, "type": "VEC4", "count": 3}],
            "meshes": [{"primitives": [{"attributes": {"TANGENT": 0}, "extensions": {"ELYSIUM_vtmb_model": {
                "bodyPart": 0, "model": 0, "sourceVertices": [0, 1, 2]}}}]}],
            "extensions": {"ELYSIUM_vtmb_model": {"vtx": {"lods": [{"index": 0, "mesh": 0}]},
                "mdl": {"bodyParts": [{"models": [{"tangentsOffset": 500, "vertexCount": 4,
                                                   "unreferencedVertices": [{"tangent": [1, 0, 0, -1]}]}]}]},
                "cloth": {"maps": [{"tangent": [0, 1, 2]}]}}}}


def test_authored_core_and_extension_tangent_inventory_does_not_claim_derivation():
    result = tangent_inventory(document())
    assert result["lod0TangentVectors"] == 3
    assert result["authoredModelTangentRecords"] == 4
    assert result["unreferencedExtensionTangentVectors"] == 1
    assert result["clothTangentFields"]["cloth.maps[].tangent:items"] == 3
    assert not result["nativeTangentDerivationVerified"] and not result["nativeTangentBuffersCompared"]


def test_tangent_record_count_corruption_fails():
    doc = document()
    doc["accessors"][0]["count"] = 2
    with pytest.raises(ValueError, match="TANGENT/source"):
        tangent_inventory(doc)


@pytest.mark.skipif(os.environ.get("ELYSIUM_R8_TANGENT_CENSUS") != "1", reason="explicit staged GLB tangent metadata census")
def test_staged_glb_tangent_inventory():
    stage, export = Path("E:/elysium-work/import/characters"), Path("E:/elysium-work/exports_v2")
    manifest_bytes = (stage / "manifest.json").read_bytes()
    manifest = json.loads(manifest_bytes)
    selected = set(manifest["selectedUnits"])
    products, failures, totals, cloth = [], [], Counter(), Counter()
    for entry in manifest["assets"]:
        if entry["assetId"] not in selected:
            continue
        try:
            with (export / entry["unitGlb"]).open("rb") as stream:
                header = stream.read(20)
                magic, version, size, length, kind = struct.unpack("<5I", header)
                assert magic == 0x46546C67 and version == 2 and kind == 0x4E4F534A
                metadata = stream.read(length)
                digest = hashlib.sha256(header + metadata)
                total_bytes = 20 + len(metadata)
                while chunk := stream.read(1024 * 1024):
                    digest.update(chunk)
                    total_bytes += len(chunk)
            assert size == total_bytes and digest.hexdigest() == entry["recipe"]["unitSha256"]
            doc = json.loads(metadata)
            row = tangent_inventory(doc)
            totals["selectedUnits"] += 1
            totals["meshUnits" if entry["meshAsset"] else "sourceOnlyUnits"] += 1
            if row.get("lod0TangentPrimitives"):
                totals["unitsWithCoreLod0Tangents"] += 1
                totals["meshUnitsWithCoreLod0Tangents" if entry["meshAsset"] else "sourceOnlyUnitsWithCoreLod0Tangents"] += 1
            if row.get("unreferencedExtensionTangentVectors"):
                totals["unitsWithUnreferencedExtensionTangents"] += 1
            for key, value in row.items():
                if type(value) is int:
                    totals[key] += value
            cloth.update(row["clothTangentFields"])
            products.append({"assetId": entry["assetId"], "unitSha256": digest.hexdigest(), "meshAsset": entry["meshAsset"], **row})
        except (ValueError, KeyError, AssertionError) as exc:
            failures.append({"assetId": entry["assetId"], "reason": str(exc)})
    result = {"scope": "staged-GLB-metadata-tangent-inventory; no repeated skin/morph numeric comparison",
              "manifestSha256": hashlib.sha256(manifest_bytes).hexdigest(), "totals": dict(totals),
              "clothTangentFields": dict(cloth), "failures": failures, "products": products, "passed": not failures}
    Path("E:/elysium-work/_r8_explore/agents/geometry/tangent_source_inventory.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
    assert not failures, failures[:4]


@pytest.mark.skipif(os.environ.get("ELYSIUM_R8_TANGENT_PILOTS") != "1", reason="explicit pinned pilot tangent/alias audit")
def test_pilot_authored_tangents_across_previously_proved_aliases():
    base = Path("E:/elysium-work/_r8_explore/agents/geometry")
    pinned = json.loads((base / "performance/inputs.json").read_bytes())
    proofs = {r["assetId"]: r for r in json.loads((base / "pilot_vertex_equivalence.json").read_bytes())["products"]}
    products = []
    for entry in pinned["products"]:
        glb_data = (base / "performance" / entry["files"]["glb"]).read_bytes()
        body_data = (base / "performance" / entry["files"]["body"]).read_bytes()
        assert hashlib.sha256(glb_data).hexdigest() == entry["sha256"]["glb"]
        assert hashlib.sha256(body_data).hexdigest() == entry["sha256"]["body"]
        document, binary = decode_glb(glb_data)
        body = json.loads(body_data)
        root = document["extensions"]["ELYSIUM_vtmb_model"]
        lod = next(r for r in root["vtx"]["lods"] if r["index"] == 0)
        primitives = {tuple(p["extensions"]["ELYSIUM_vtmb_model"][k] for k in ("bodyPart", "model", "mesh")): p
                      for p in document["meshes"][lod["mesh"]]["primitives"]}
        tangent = {}
        for join in body["renderVertexMap"]:
            primitive = primitives[tuple(join[k] for k in ("bodyPart", "model", "mesh"))]
            source = primitive["extensions"]["ELYSIUM_vtmb_model"]
            lookup = {v: i for i, v in enumerate(source["sourceVertices"])}
            values = accessor(document, binary, primitive["attributes"]["TANGENT"])
            tangent.update({staged: values[lookup[original]] for original, staged in join["vertices"]})
        aliases = proofs[entry["assetId"]]["sourceAliases"]
        different = [{"sourceVertex": a, "aliasSourceVertex": b, "sourceTangent": tangent[a].tolist(), "aliasTangent": tangent[b].tolist()}
                     for a, b in aliases if not np.array_equal(tangent[a], tangent[b])]
        products.append({"assetId": entry["assetId"], "sourceSha256": entry["sha256"]["glb"], "aliases": len(aliases),
                         "differentAuthoredTangents": len(different), "examples": different[:8], "nativeTangentValuesVerified": False})
    (base / "pilot_alias_authored_tangents.json").write_text(json.dumps(products, indent=2), encoding="utf-8")
