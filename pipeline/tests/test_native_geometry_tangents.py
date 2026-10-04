
import pytest

from elysium_pipeline.validation.native_geometry_tangents import tangent_inventory


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
