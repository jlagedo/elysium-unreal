"""Hand-authored independent fixtures; no exporter/stager/native writer as oracle."""
from copy import deepcopy
import hashlib
import json
import struct
import zlib

import numpy as np
import pytest

from elysium_pipeline.validation.native_geometry import (
    EXT, Geometry, GeometryVerificationError, check_authoring, check_render, check_weights,
    read_stage, reference_skin, unit_vector, verify_native_geometry, verify_source_geometry,
    world_matrices,
)
from elysium_pipeline.validation.geometry_inventory import morph_source_inventory


def string(value):
    raw = value.encode()
    return struct.pack("<I", len(raw)) + raw


def container(parts):
    at = 16 + len(parts) * 20
    directory = b""
    for tag, data in parts.items():
        directory += struct.pack("<4sQQ", tag.encode(), at, len(data))
        at += len(data)
    return struct.pack("<4sIII", b"ESKM", 8, len(parts), 0) + directory + b"".join(parts.values())


def glb(document, binary):
    document = {**document, "asset": {"version": "2.0"}, "buffers": [{"byteLength": len(binary)}]}
    raw = json.dumps(document, separators=(",", ":")).encode()
    raw += b" " * (-len(raw) % 4)
    binary += b"\0" * (-len(binary) % 4)
    chunks = struct.pack("<I4s", len(raw), b"JSON") + raw + struct.pack("<I4s", len(binary), b"BIN\0") + binary
    return struct.pack("<4sII", b"glTF", 2, 12 + len(chunks)) + chunks


@pytest.fixture
def fixture():
    positions = [[0., 0., 0.], [2.54, 0., 0.], [0., -2.54, 0.]]
    normals = [[0., 0., 1.]] * 3
    uvs = [[0., 0.], [1., 0.], [0., 1.]]
    skeleton = struct.pack("<I", 1) + string("root") + struct.pack("<i7f", -1, 0., 0., 0., 0., 0., 0., 1.)
    mesh = struct.pack("<III", 3, 1, 1) + string("skin") + struct.pack("<II", 0, 1)
    for p, n, uv in zip(positions, normals, uvs):
        mesh += struct.pack("<8f4H4f", *p, *n, *uv, 0, 0, 0, 0, 1., 0., 0., 0.)
    mesh += struct.pack("<3I", 0, 2, 1)
    morph = struct.pack("<I", 1) + string("smile") + struct.pack("<I", 2)
    morph += struct.pack("<I6f", 0, .508, 0, 0, 0, -.2, 0)
    morph += struct.pack("<I6f", 1, 0, 0, 0, 0, 0, 0)
    tangent = struct.pack("<I", 3) + struct.pack("<12f", *([1., 0, 0, 1.] * 3))
    payload = container({"SKEL": skeleton, "MESH": mesh, "MORF": morph, "TANG": tangent})
    document, binary = {"bufferViews": [], "accessors": []}, bytearray()

    def view(raw):
        binary.extend(b"\0" * (-len(binary) % 4))
        i = len(document["bufferViews"])
        document["bufferViews"].append({"buffer": 0, "byteOffset": len(binary), "byteLength": len(raw)})
        binary.extend(raw)
        return i

    def access(values, dtype, component, shape):
        index = len(document["accessors"])
        document["accessors"].append({"bufferView": view(np.asarray(values, dtype=dtype).tobytes()),
                                     "componentType": component, "type": shape, "count": len(values)})
        return index

    def precise(values):
        a = np.asarray(values, dtype="<f8")
        raw = a.tobytes()
        return {"bufferView": view(zlib.compress(raw)), "encoding": "zlib-float64-le",
                "shape": list(a.shape), "sha256": hashlib.sha256(raw).hexdigest()}

    authored = [{"flex": 0, "target": 0, "vertex": 0, "sourceVertex": 10,
                 "position": [.1, 0, 0], "normal": [0, .1, 0]}] * 2
    authored += [{"flex": 0, "target": 0, "vertex": 1, "sourceVertex": 11,
                  "position": [0, 0, 0], "normal": [0, 0, 0]},
                 {"flex": 0, "target": 0, "vertex": None, "sourceVertex": 99,
                  "position": [1, 0, 0], "normal": [0, 0, 0]}]
    source = {"bodyPart": 0, "model": 0, "mesh": 0, "skinReference": 0, "sourceVertices": [10, 11, 12],
              "sourcePositions": precise([[0., 0., 0.], [1., 0., 0.], [0., 1., 0.]]),
              "sourceNormals": precise(normals), "morphRecords": authored}
    primitive = {"indices": access([0, 1, 2], "<u4", 5125, "SCALAR"),
                 "attributes": {"TEXCOORD_0": access(uvs, "<f4", 5126, "VEC2"),
                                "TANGENT": access([[1., 0, 0, -1.]] * 3, "<f4", 5126, "VEC4"),
                                "JOINTS_0": access([[0] * 4] * 3, "<u2", 5123, "VEC4"),
                                "WEIGHTS_0": access([[1., 0, 0, 0]] * 3, "<f4", 5126, "VEC4")},
                 "extensions": {EXT: source, "ELYSIUM_material_reference": {"asset": "vtmb:material:skin"}}}
    root = {"identity": {"asset": "vtmb:model:test/body"}, "vtx": {"lods": [{"index": 0, "mesh": 0}, {"index": 1, "mesh": 1}]},
            "mdl": {"bones": [{"name": "root"}], "textures": [{"name": "skin"}],
                    "bodyParts": [{"models": [{"meshes": [{}, {"unrenderedMorphRecords": [
                        {"flex": 1, "sourceVertex": 4, "position": [0, 0, 0], "normal": [0, 0, 0]}]}]}]}]},
            "facial": {"morphTargets": [{"index": 0, "name": "smile"}]}}
    document.update({"extensions": {EXT: root}, "meshes": [{"primitives": [primitive]}]})
    body = {"assetId": root["identity"]["asset"], "wield": None, "sourceSemantics": deepcopy(root),
            "renderVertexMap": [{"bodyPart": 0, "model": 0, "mesh": 0, "material": "skin", "vertices": [[10, 0], [11, 1], [12, 2]]}]}
    author = {"vertices": [{"id": i, "position": p, "influences": [[0, 65535]]} for i, p in enumerate(positions)],
              "instances": [{"id": i, "vertex": v, "normal": normals[v], "uv": uvs[v],
                             "tangentX": [1., 0, 0], "tangentY": [0, 1., 0], "binormalSign": 1.} for i, v in enumerate([0, 2, 1])],
              "tangentYDerived": True,
              "groups": [{"id": 0, "slot": "skin"}], "triangles": [{"group": 0, "instances": [0, 1, 2]}],
              "morphs": [{"name": "smile", "normalsPresent": True, "positions": [[0, .508, 0, 0]], "normals": [[0, 0, -.2, 0]]}]}
    render = {"vertices": [{"sourceVertex": i, "section": 0, "position": p, "normal": normals[i], "uv": uvs[i],
                            "tangentX": [1., 0, 0], "tangentY": [0, 1., 0], "binormalSign": 1.,
                            "influences": [[0, 65535]]} for i, p in enumerate(positions)],
              "sections": [{"material": 0, "baseVertex": 0, "numVertices": 3, "baseIndex": 0, "numTriangles": 1, "disabled": False}],
              "indices": [0, 2, 1], "weightBits": 8, "normalBits": 8, "fullPrecisionUVs": False, "uvChannels": 1,
              "morphs": [{"name": "smile", "baseVertices": 3, "deltas": [[0, .508, 0, 0, 0, -.2, 0]], "sections": [0]}]}
    snapshot = {"schemaVersion": 1, "tangentCaptureVersion": 1, "lod": 0, "bones": [{"name": "root", "parent": -1, "position": [0, 0, 0],
                 "rotation": [0, 0, 0, 1], "scale": [1, 1, 1]}], "materials": [{"slot": "skin", "asset": "/Materials/MI_skin"}],
                "authoring": deepcopy(author), "render": deepcopy(render)}
    return {"payload": payload, "body": body, "document": document, "binary": bytes(binary), "snapshot": deepcopy(snapshot)}


def verify(f):
    source = glb(f["document"], f["binary"])
    return verify_native_geometry(f["payload"], f["snapshot"], body=f["body"], source_glb=source,
                                  expected_source_sha256=hashlib.sha256(source).hexdigest(),
                                  material_paths={"vtmb:material:skin": "/Materials/MI_skin"})


def test_full_boundary_reports_inventory_without_claiming_rendered_acceptance(fixture):
    result = verify(fixture)
    assert result["passed"], result
    assert result["authoring"]["passed"] and result["render"]["passed"]
    inventory = result["sourceInventory"]
    assert inventory["primitiveRecords"] == 4
    assert inventory["explicitZeroRecords"] == 2
    assert inventory["repeatedContributions"] == 1
    assert inventory["unrenderedVertexRecords"] == inventory["unrenderedMeshRecords"] == 1
    assert inventory["stagedMorphRecords"] == 2
    assert inventory["higherLodsRetainedInGlb"] == [1]
    assert not inventory["nativeDenseMorphsProveSourceInventory"]
    assert not result["evaluatedSkinningVerified"] and not result["renderedAcceptance"]


@pytest.mark.parametrize("path,value,reason", [
    (("authoring", "vertices", 1, "position", 0), 5., "authoring position"),
    (("authoring", "instances", 0, "normal", 1), .5, "authoring source normal"),
    (("authoring", "instances", 0, "uv", 0), .01, "authoring UV"),
    (("authoring", "vertices", 0, "influences"), [[0, 50000]], "weight exceeds"),
    (("authoring", "triangles", 0, "instances"), [0, 2, 1], "authoring topology"),
    (("authoring", "morphs", 0, "positions", 0, 1), .6, "authoring morph position"),
    (("authoring", "morphs", 0, "normals", 0, 2), -.3, "authoring morph normal"),
    (("authoring", "morphs", 0, "normalsPresent"), False, "normal attribute missing"),
    (("render", "vertices", 0, "sourceVertex"), 2, "render position"),
    (("render", "vertices", 1, "position", 0), 3., "render position"),
    (("render", "vertices", 0, "normal", 0), .1, "render source normal"),
    (("render", "vertices", 0, "uv", 0), .01, "render UV"),
    (("render", "vertices", 0, "influences"), [[0, 50000]], "invalid native raw weight"),
    (("render", "indices"), [0, 1, 2], "render topology"),
    (("render", "sections", 0, "material"), 1, "render material index"),
    (("render", "sections", 0, "disabled"), True, "render section disabled"),
    (("render", "morphs", 0, "deltas", 0, 1), .7, "render morph position/normal"),
    (("render", "morphs", 0, "deltas", 0, 5), -.2001, "render morph position/normal"),
    (("render", "morphs", 0, "sections"), [], "render morph section coverage"),
    (("render", "morphs", 0, "baseVertices"), 9, "render morph base vertex"),
    (("bones", 0, "position", 0), 2., "native reference position"),
    (("materials", 0, "asset"), "/Materials/wrong", "native material package"),
])
def test_native_corruption_fails_at_owning_boundary(fixture, path, value, reason):
    target = fixture["snapshot"]
    for step in path[:-1]:
        target = target[step]
    target[path[-1]] = value
    result = verify(fixture)
    assert not result["passed"]
    assert reason in result["differences"][0]["reason"], result


def test_source_join_corruption_even_with_unchanged_native_data(fixture):
    fixture["body"]["renderVertexMap"][0]["vertices"][0][0] = 99
    assert "join order/coverage" in verify(fixture)["differences"][0]["reason"]


def test_source_uv_and_weight_corruption_cannot_hide_behind_staged_payload(fixture):
    primitive = fixture["document"]["meshes"][0]["primitives"][0]
    for channel in ("TEXCOORD_0", "WEIGHTS_0"):
        f = deepcopy(fixture)
        accessor = f["document"]["accessors"][primitive["attributes"][channel]]
        view = f["document"]["bufferViews"][accessor["bufferView"]]
        data = bytearray(f["binary"])
        struct.pack_into("<f", data, view["byteOffset"], .2)
        f["binary"] = bytes(data)
        assert not verify(f)["passed"]


def test_explicit_zero_and_repeated_records_have_distinct_inventory_hash(fixture):
    before = verify(fixture)["sourceInventory"]
    source = fixture["document"]["meshes"][0]["primitives"][0]["extensions"][EXT]
    # Merge the two repeated contributions: dense result unchanged; source identity changed.
    source["morphRecords"] = deepcopy(source["morphRecords"])
    source["morphRecords"][0]["position"] = [.2, 0, 0]
    source["morphRecords"][0]["normal"] = [0, .2, 0]
    del source["morphRecords"][1]
    after = verify(fixture)
    assert after["passed"]
    assert before["orderedRecordSha256"] != after["sourceInventory"]["orderedRecordSha256"]
    assert after["sourceInventory"]["repeatedContributions"] == 0


def test_pinned_glb_digest_rejects_changed_source(fixture):
    source = glb(fixture["document"], fixture["binary"])
    result = verify_native_geometry(fixture["payload"], fixture["snapshot"], body=fixture["body"], source_glb=source,
                                    expected_source_sha256="0" * 64, material_paths=[])
    assert "digest changed" in result["differences"][0]["reason"]


def test_unrendered_mesh_records_must_survive_body_source_inventory(fixture):
    fixture["body"]["sourceSemantics"]["mdl"]["bodyParts"][0]["models"][0]["meshes"][1]["unrenderedMorphRecords"] = []
    assert "source semantics changed" in verify(fixture)["differences"][0]["reason"]


def test_tiny_and_normal_only_morph_drops_are_failures(fixture):
    geometry = read_stage(fixture["payload"])
    for delta in ([1e-7, 0, 0, 0, 0, 0], [0, 0, 0, 1e-7, 0, 0]):
        geometry.morphs["smile"] = {0: np.asarray(delta)}
        built = deepcopy(fixture["snapshot"]["render"])
        built["morphs"][0]["deltas"] = []
        built["morphs"][0]["sections"] = []
        with pytest.raises(GeometryVerificationError, match="dropped nonzero"):
            check_render(geometry, built)
        author = deepcopy(fixture["snapshot"]["authoring"])
        author["morphs"][0].update(positions=[], normals=[])
        with pytest.raises(GeometryVerificationError, match="dropped nonzero"):
            check_authoring(geometry, author)


def test_render_vertex_reordering_uses_native_source_map(fixture):
    render = fixture["snapshot"]["render"]
    render["vertices"] = [render["vertices"][i] for i in [2, 0, 1]]
    render["indices"] = [1, 0, 2]
    render["morphs"][0]["deltas"][0][0] = 1
    assert verify(fixture)["passed"]
    render["vertices"][1]["sourceVertex"] = 1
    assert not verify(fixture)["passed"]


def test_duplicate_triangle_is_not_accepted_as_topology_set(fixture):
    render = fixture["snapshot"]["render"]
    render["indices"] *= 2
    render["sections"][0]["numTriangles"] = 2
    assert "render topology" in verify(fixture)["differences"][0]["reason"]


def two_bone_geometry(weights=(.5, .5)):
    bones = [{"name": "root", "parent": -1, "position": [2., 0, 0], "rotation": [0, 0, 0, 1]},
             {"name": "child", "parent": 0, "position": [0, 3., 0], "rotation": [0, 0, 0, 1]}]
    return Geometry(bones, np.array([[4., 5., 1.]]), np.array([[1., 0, 0]]), np.array([[0., 0]]),
                    np.array([[0, 1, 0, 0]]), np.array([[*weights, 0, 0]]), [], [],
                    {"bend": {0: np.array([.2, -.1, .3, 0, .4, .1])}})


def test_independent_wield_affine_skin_and_morph_normal_length():
    geometry = two_bone_geometry()
    pose = deepcopy(geometry.bones)
    pose[0]["position"] = [10., 0, 0]
    pose[1]["rotation"] = [0, 0, 2 ** -.5, 2 ** -.5]
    result = reference_skin(geometry, pose)
    # Root: (12,5,1). Child rotates (p-(2,3,0)) 90 degrees, then adds (10,3,0).
    np.testing.assert_allclose(result.positions, [[10., 5., 1.]])
    np.testing.assert_allclose(result.normals, [[2 ** -.5, 2 ** -.5, 0]])
    a = np.array([[.5, -.5, 0], [.5, .5, 0], [0, 0, 1]])
    np.testing.assert_allclose(result.morphs["bend"][0][:3], [.15, .05, .3])
    for weight in [-2., -.5, 0., .25, .5, 1., 2.]:
        expected = unit_vector(a @ (geometry.normals[0] + weight * geometry.morphs["bend"][0][3:]))
        actual = unit_vector(result.normals[0] + weight * result.morphs["bend"][0][3:])
        np.testing.assert_allclose(actual, expected, atol=1e-12)
    wrong = unit_vector(result.normals[0] + a @ geometry.morphs["bend"][0][3:])
    assert np.linalg.norm(wrong - unit_vector(a @ (geometry.normals[0] + geometry.morphs["bend"][0][3:]))) > .05


def test_noncommuting_reference_chain_and_bind_inverse():
    bones = [{"name": "root", "parent": -1, "position": [3, 4, 5], "rotation": [0, 0, 2 ** -.5, 2 ** -.5]},
             {"name": "child", "parent": 0, "position": [2, 0, 0], "rotation": [2 ** -.5, 0, 0, 2 ** -.5]}]
    worlds = world_matrices(bones)
    # X quarter-turn then Z quarter-turn; origin translates through rotated parent.
    np.testing.assert_allclose(worlds[1] @ [0, 1, 0, 1], [3, 6, 6, 1], atol=1e-12)
    geometry = two_bone_geometry((0, 1))
    geometry.bones = bones
    same = reference_skin(geometry, bones)
    np.testing.assert_allclose(same.positions, geometry.positions, atol=1e-12)


def test_cancelled_reference_normal_refuses_all_vertices_even_without_morphs():
    geometry = two_bone_geometry()
    geometry.morphs = {}
    pose = deepcopy(geometry.bones)
    pose[1]["rotation"] = [0, 0, 1, 0]
    with pytest.raises(GeometryVerificationError, match="cancelled"):
        reference_skin(geometry, pose)


@pytest.mark.parametrize("bits,raw", [(16, [32767, 32768]), (8, [32639, 32639])])
def test_native_weight_precision_bound_and_bone_identity(bits, raw):
    geometry = two_bone_geometry()
    assert check_weights([[0, raw[0]], [1, raw[1]]], geometry, 0, bits=bits, field="skin") == []
    with pytest.raises(GeometryVerificationError, match="missing influence"):
        check_weights([[0, raw[0]]], geometry, 0, bits=bits, field="skin")
    with pytest.raises(GeometryVerificationError, match="duplicate bone"):
        check_weights([[0, raw[0]], [0, raw[1]]], geometry, 0, bits=bits, field="skin")
    with pytest.raises(GeometryVerificationError):
        check_weights([[0, 25700], [1, 39835]], geometry, 0, bits=bits, field="skin")


def test_quantized_zero_influence_is_explicit_and_bounded():
    geometry = two_bone_geometry((1 - 1e-7, 1e-7))
    losses = check_weights([[0, 65535]], geometry, 0, bits=16, field="skin")
    assert losses == [{"vertex": 0, "bone": "child", "weight": 1e-7, "bits": 16}]


@pytest.mark.parametrize("invalid", [float("nan"), float("inf")])
def test_nonfinite_native_geometry_fails_closed(fixture, invalid):
    fixture["snapshot"]["authoring"]["vertices"][0]["position"][0] = invalid
    assert not verify(fixture)["passed"]


def test_missing_native_evidence_is_not_an_empty_success(fixture):
    del fixture["snapshot"]["render"]
    result = verify(fixture)
    assert result["authoring"]["passed"] and not result["render"]["passed"] and not result["passed"]


def test_wield_end_to_end_requires_reskinned_positions_and_morphs(fixture):
    snapshot = fixture["snapshot"]
    pose = deepcopy(snapshot["bones"])
    pose[0].update(position=[10., 20., 30.], rotation=[0., 0., 2 ** -.5, 2 ** -.5])
    fixture["body"]["wield"] = {"referencePose": pose}
    snapshot["bones"] = deepcopy(pose)
    assert not verify(fixture)["passed"]  # Installing only a new bind leaves the mesh behind.
    for data in (snapshot["authoring"], snapshot["render"]):
        for row in data["vertices"]:
            x, y, z = row["position"]
            row["position"] = [10. - y, 20. + x, 30. + z]
    for row in [*snapshot["authoring"]["instances"], *snapshot["render"]["vertices"]]:
        row["tangentX"], row["tangentY"] = [0, 1., 0], [-1., 0, 0]
    snapshot["authoring"]["morphs"][0].update(positions=[[0, 0, .508, 0]], normals=[[0, .2, 0, 0]])
    snapshot["render"]["morphs"][0]["deltas"] = [[0, 0, .508, 0, .2, 0, 0]]
    result = verify(fixture)
    assert result["passed"], result


def test_split_render_vertices_receive_every_morph_copy(fixture):
    geometry = read_stage(fixture["payload"])
    geometry.triangles *= 2
    render = fixture["snapshot"]["render"]
    render["vertices"].append(deepcopy(render["vertices"][0]))
    render["sections"][0].update(numVertices=4, numTriangles=2)
    render["indices"] += [3, 2, 1]
    render["morphs"][0]["baseVertices"] = 2
    render["morphs"][0]["deltas"].append([3, .508, 0, 0, 0, -.2, 0])
    assert check_render(geometry, render)["splitCopies"] == 1
    render["morphs"][0]["deltas"].pop()
    with pytest.raises(GeometryVerificationError, match="morph"):
        check_render(geometry, render)


def test_section_chunking_preserves_triangle_material_and_global_bone_identity(fixture):
    geometry = read_stage(fixture["payload"])
    geometry.slots.append("eyes")
    geometry.triangles.append((1, 0, 2, 1))
    render = fixture["snapshot"]["render"]
    copies = deepcopy(render["vertices"])
    for row in copies:
        row["section"] = 1
    render["vertices"] += copies
    render["sections"].append({"material": 1, "baseVertex": 3, "numVertices": 3, "baseIndex": 3,
                               "numTriangles": 1, "disabled": False})
    render["indices"] += [3, 5, 4]
    render["morphs"][0].update(baseVertices=2, sections=[0, 1])
    render["morphs"][0]["deltas"].append([3, .508, 0, 0, 0, -.2, 0])
    assert check_render(geometry, render)["sections"] == 2
    render["sections"][1]["material"] = 0
    with pytest.raises(GeometryVerificationError, match="topology/winding/material"):
        check_render(geometry, render)


def test_source_zero_normal_fallback_is_explicit_and_cannot_mask_authored_normals(fixture):
    source = fixture["document"]["meshes"][0]["primitives"][0]["extensions"][EXT]
    # Replace the precise source normal stream with authored zeros, leaving the
    # staged triangle positions untouched. Its declared fallback faces down in UE.
    raw = np.zeros((3, 3), dtype="<f8").tobytes()
    compressed = zlib.compress(raw)
    view = len(fixture["document"]["bufferViews"])
    fixture["document"]["bufferViews"].append({"buffer": 0, "byteOffset": len(fixture["binary"]), "byteLength": len(compressed)})
    fixture["binary"] += compressed
    source["sourceNormals"] = {"bufferView": view, "encoding": "zlib-float64-le", "shape": [3, 3], "sha256": hashlib.sha256(raw).hexdigest()}
    geometry = read_stage(fixture["payload"])
    geometry.normals[:] = [0, 0, -1]
    result = verify_source_geometry(geometry, fixture["body"], fixture["document"], fixture["binary"])
    assert len(result["sourceNormalFallbacks"]) == 3
    geometry.normals[1] = [1, 0, 0]
    with pytest.raises(GeometryVerificationError, match="source normals"):
        verify_source_geometry(geometry, fixture["body"], fixture["document"], fixture["binary"])


def test_folded_source_bone_name_keeps_original_identity(fixture):
    root = fixture["document"]["extensions"][EXT]
    root["mdl"]["bones"][0]["name"] = "root\t"
    fixture["body"]["sourceSemantics"]["mdl"] = deepcopy(root["mdl"])
    geometry = read_stage(fixture["payload"])
    geometry.bones[0]["name"] = "root_"
    result = verify_source_geometry(geometry, fixture["body"], fixture["document"], fixture["binary"])
    assert result["boneNameProjection"] == [{"source": "root\t", "native": "root_", "nativeIndex": 0}]


def test_morph_section_presence_not_order_is_semantic(fixture):
    # Native sections may enumerate deltas in different processing order; identity
    # and duplicates still matter. A single section here must occur exactly once.
    fixture["snapshot"]["render"]["morphs"][0]["sections"] = [0, 0]
    assert not verify(fixture)["passed"]


def test_material_expected_path_cannot_hide_changed_source_material_id(fixture):
    primitive = fixture["document"]["meshes"][0]["primitives"][0]
    primitive["extensions"]["ELYSIUM_material_reference"]["asset"] = "vtmb:material:wrong"
    assert "material identity inventory" in verify(fixture)["differences"][0]["reason"]


def test_entirely_unrendered_unit_retains_ordered_zero_and_repeated_records(fixture):
    document = fixture["document"]
    root = document["extensions"][EXT]
    root["vtx"]["lods"] = []
    rows = root["mdl"]["bodyParts"][0]["models"][0]["meshes"][1]["unrenderedMorphRecords"]
    rows.append(deepcopy(rows[0]))
    inventory = morph_source_inventory(document)
    assert inventory["primitiveRecords"] == 0
    assert inventory["unrenderedMeshRecords"] == inventory["explicitZeroRecords"] == 2
    assert inventory["repeatedContributions"] == 1
    before = inventory["orderedRecordSha256"]
    rows.pop()
    assert morph_source_inventory(document)["orderedRecordSha256"] != before
