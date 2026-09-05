from copy import deepcopy
import struct

import pytest

from elysium_pipeline.importers.cloth_data import cloth_projection


def _string(value):
    encoded = value.encode()
    return struct.pack("<I", len(encoded)) + encoded


def mesh_blob():
    bones = struct.pack("<I", 1) + _string("Bone_root") + struct.pack("<i7f", -1, 0, 0, 0, 0, 0, 0, 1)
    mesh = struct.pack("<3I", 3, 1, 1) + _string("coat") + struct.pack("<2I", 0, 1)
    for index in range(3):
        mesh += struct.pack("<8f4H4f", index, -2, 3, 0, 0, 1, index / 2, .5, 0, 0, 0, 0, 1, 0, 0, 0)
    mesh += struct.pack("<3I", 0, 2, 1)
    parts = [(b"SKEL", bones), (b"MESH", mesh)]
    out = struct.pack("<4s3I", b"ESKM", 8, len(parts), 0)
    offset = 16 + len(parts) * 20
    for tag, data in parts:
        out += struct.pack("<4s2Q", tag, offset, len(data))
        offset += len(data)
    return out + b"".join(data for _, data in parts)


def body():
    garment = {"definition": 0, "particle_count": 3, "anchored_count": 1,
               "rest_positions": [[0, 0, 0], [1, 2, 3], [2, 3, 4]], "triangles": [[0, 1, 2]],
               "constraints": [{"a": 0, "b": 1, "rest_length_squared": 9}],
               "distance_constraints": 1, "compression_constraints": 0,
               "anchor_skin": {"0": [["Bone:root", 1]]}, "particle_skin": [[["Bone:root", 1]]] * 3,
               "capsules": [], "spheres": [{"bone_name": "Bone:root", "centre_bind": [1, 2, 3],
                                             "centre_local": [99, 99, 99], "radius": 2}],
               "render_maps": [{"material": "coat", "positions": [[99, 99, 99]]}]}
    model = {"bodyPart": 0, "model": 0, "columns": 1,
             "definitions": [{"lod": 0, "column": 0, "anchor_vertex_indices": [21]}],
             "meshMaps": [{"mesh": 0, "lodRows": [{"lod": 0, "selectors": [0, 255, 0],
                                                      "positionNormal": [0x8001, 0, 2], "tangent": [7, 8, 9]}]}]}
    return {"assetId": "vtmb:model:character/test/coat",
            "sourceSemantics": {"mdl": {"bodyParts": [{"models": [{"meshes": [{"vertexOffset": 20, "vertexCount": 3}]}]}]},
                                "cloth": {"garments": [garment], "sourceModels": [model]}},
            "renderVertexMap": [{"bodyPart": 0, "model": 0, "mesh": 0, "material": "coat",
                                 "vertices": [[22, 0], [20, 1], [21, 2]]}]}


def test_source_vertex_join_retains_substitutions_and_skinning_in_native_order():
    data = body()
    before = deepcopy(data)
    result = cloth_projection(data, mesh_blob())
    assert data == before
    garment = result["garments"][0]
    assert garment["assetPath"] == "/ElysiumBaked/Models/character/test/CLOTH_coat"
    render = garment["render_maps"][0]
    assert [row["particle"] for row in render["vertices"]] == [2, 1, 0]
    assert render["vertices"][1]["flip_normal"] is True
    assert render["vertices"][2]["anchor"] is True
    assert render["positions"] == [[0, -2, 3], [1, -2, 3], [2, -2, 3]]
    assert render["normals"] == [[0, 0, 1]] * 3
    assert render["triangles"] == [[0, 2, 1]]
    assert render["skin"] == [[["Bone_root", 1]]] * 3


def test_simulation_basis_changes_once_and_source_skin_names_are_canonical():
    garment = cloth_projection(body(), mesh_blob())["garments"][0]
    assert garment["rest_positions"][1] == [2.54, -5.08, 7.62]
    assert garment["triangles"] == [[0, 2, 1]]
    assert garment["constraints"][0]["rest_length_squared"] == pytest.approx(9 * 2.54 ** 2)
    assert garment["particle_skin"][0] == [["Bone_root", 1]]
    assert garment["anchor_skin"]["0"] == [["Bone_root", 1]]
    assert garment["spheres"][0]["radius"] == 5.08
    assert "centre_local" not in garment["spheres"][0]


def test_bad_identity_join_cannot_fall_back_to_a_nearby_vertex():
    data = body()
    data["renderVertexMap"][0]["vertices"][0][0] = 99
    with pytest.raises(ValueError, match="outside its mesh"):
        cloth_projection(data, mesh_blob())
    data = body()
    data["sourceSemantics"]["cloth"]["sourceModels"][0]["meshMaps"][0]["lodRows"][0]["positionNormal"][0] = 99
    with pytest.raises(ValueError, match="absent particle"):
        cloth_projection(data, mesh_blob())
