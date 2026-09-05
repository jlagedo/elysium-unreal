from copy import deepcopy
import hashlib
import json
import os
from pathlib import Path
import struct

import numpy as np
import pytest

from elysium_pipeline.validation.native_geometry import read_stage, reference_skin, accessor, verify_native_geometry
from elysium_pipeline.validation.native_geometry_equivalence import audit_vertex_equivalence
from elysium_pipeline.validation.skeletal_diff import sections
from elysium_pipeline.formats.unit_contract.container import decode_glb
from pipeline.tests.test_native_geometry import fixture as geometry_fixture, container, glb, two_bone_geometry  # noqa: F401
from pipeline.tests.test_native_geometry_equivalence import aliases  # noqa: F401
from pipeline.tests.test_native_geometry_stage import stage_case as capture_stage, snapshot, publish, verify  # noqa: F401


def compare(f):
    source = glb(f["document"], f["binary"])
    return verify_native_geometry(f["payload"], f["snapshot"], body=f["body"], source_glb=source,
                                  expected_source_sha256=hashlib.sha256(source).hexdigest(),
                                  material_paths={"vtmb:material:skin": "/Materials/MI_skin"})


def set_tangent(f, value):
    """Hand-author matching source/stage/native fixture channels, without invoking writers."""
    x, y, z, sign = value
    parts = {k: bytes(v) for k, v in sections(f["payload"]).items()}
    parts["TANG"] = struct.pack("<I12f", 3, *([x, y, z, sign] * 3))
    f["payload"] = container(parts)
    primitive = f["document"]["meshes"][0]["primitives"][0]
    attr = f["document"]["accessors"][primitive["attributes"]["TANGENT"]]
    view = f["document"]["bufferViews"][attr["bufferView"]]
    data = bytearray(f["binary"])
    for i in range(3):
        struct.pack_into("<4f", data, view["byteOffset"] + 16 * i, x, z, y, -sign)
    f["binary"] = bytes(data)
    for row in [*f["snapshot"]["authoring"]["instances"], *f["snapshot"]["render"]["vertices"]]:
        row.update(tangentX=[x, y, z], tangentY=[-y * sign, x * sign, 0.], binormalSign=sign)


def test_glb_to_tang_basis_and_handedness_is_independent(geometry_fixture):
    set_tangent(geometry_fixture, [.2, .4, .3, -1.])
    result = compare(geometry_fixture)
    assert result["passed"] and result["tangentsVerified"] and result["fullChannelVerificationPassed"], result
    assert result["sourceInventory"]["tangentVectors"] == 3
    assert result["authoring"]["tangentsVerified"] and result["render"]["tangentsVerified"]
    # The native native-frame TANG bytes alone are not their own expected-answer oracle.
    parts = {k: bytes(v) for k, v in sections(geometry_fixture["payload"]).items()}
    corrupt = bytearray(parts["TANG"])
    struct.pack_into("<f", corrupt, 4, .9)
    parts["TANG"] = bytes(corrupt)
    geometry_fixture["payload"] = container(parts)
    assert "source TANGENT to TANG" in compare(geometry_fixture)["differences"][0]["reason"]


@pytest.mark.parametrize("kind", ["missing", "count", "truncated", "trailing", "nonfinite", "sign"])
def test_invalid_or_missing_tang_refuses_full_channel_acceptance(geometry_fixture, kind):
    parts = {k: bytes(v) for k, v in sections(geometry_fixture["payload"]).items()}
    if kind == "missing":
        del parts["TANG"]
    elif kind == "count":
        parts["TANG"] = struct.pack("<I", 4) + parts["TANG"][4:]
    elif kind == "truncated":
        parts["TANG"] = parts["TANG"][:3]
    elif kind == "trailing":
        parts["TANG"] += b"\0"
    else:
        data = bytearray(parts["TANG"])
        struct.pack_into("<f", data, 4 if kind == "nonfinite" else 16, float("inf") if kind == "nonfinite" else 0.)
        parts["TANG"] = bytes(data)
    geometry_fixture["payload"] = container(parts)
    result = compare(geometry_fixture)
    assert not result["passed"] and not result["tangentsVerified"] and not result["fullChannelVerificationPassed"]
    assert "TANG" in result["differences"][0]["reason"]


def test_missing_core_tangent_cannot_pass_from_native_or_stage_only(geometry_fixture):
    del geometry_fixture["document"]["meshes"][0]["primitives"][0]["attributes"]["TANGENT"]
    assert "missing required TANGENT" in compare(geometry_fixture)["differences"][0]["reason"]


@pytest.mark.parametrize("domain", ["authoring", "render"])
@pytest.mark.parametrize("channel,mode", [("tangentX", "missing"), ("tangentY", "missing"), ("binormalSign", "missing"),
                                         ("tangentX", "altered"), ("tangentY", "altered"), ("binormalSign", "altered"),
                                         ("tangentX", "nonfinite"), ("tangentY", "nonfinite"), ("binormalSign", "nonfinite")])
def test_saved_tangent_xyz_y_and_sign_are_each_required(geometry_fixture, domain, channel, mode):
    rows = geometry_fixture["snapshot"][domain]["instances" if domain == "authoring" else "vertices"]
    if mode == "missing":
        del rows[0][channel]
    elif channel == "binormalSign":
        rows[0][channel] = -1. if mode == "altered" else "NaN"
    else:
        rows[0][channel][0] += .1 if mode == "altered" else 0
        if mode == "nonfinite":
            rows[0][channel][0] = "Infinity"
    result = compare(geometry_fixture)
    assert not result["passed"] and not result["tangentsVerified"]
    assert domain + " " + channel in result["differences"][0]["reason"]


def test_old_capture_without_tangents_remains_explicitly_unaccepted(geometry_fixture):
    del geometry_fixture["snapshot"]["tangentCaptureVersion"]
    result = compare(geometry_fixture)
    assert not result["passed"] and not result["fullChannelVerificationPassed"]
    assert "lacks tangent capture" in result["differences"][0]["reason"]


def test_zero_tangents_and_their_negative_sign_survive_exactly(geometry_fixture):
    set_tangent(geometry_fixture, [0., 0., 0., -1.])
    assert compare(geometry_fixture)["passed"]
    geometry_fixture["snapshot"]["render"]["vertices"][0]["binormalSign"] = 1.
    assert "render binormalSign" in compare(geometry_fixture)["differences"][0]["reason"]
    geometry_fixture["snapshot"]["render"]["vertices"][0]["binormalSign"] = -1.
    geometry_fixture["snapshot"]["render"]["vertices"][0]["tangentX"][0] = 1e-9
    assert "source zero changed" in compare(geometry_fixture)["differences"][0]["reason"]


def test_native_y_must_match_captured_axes_not_only_the_looser_packing_bound(geometry_fixture):
    geometry_fixture["snapshot"]["render"]["vertices"][0]["tangentY"][0] = 1e-4
    assert "tangentY native basis" in compare(geometry_fixture)["differences"][0]["reason"]


def test_nonzero_small_tangent_is_not_silently_dropped(geometry_fixture):
    set_tangent(geometry_fixture, [1e-7, 0, 0, 1.])
    geometry_fixture["snapshot"]["authoring"]["instances"][0]["tangentX"] = [0., 0., 0.]
    assert "dropped nonzero tangent" in compare(geometry_fixture)["differences"][0]["reason"]


def test_wield_tangent_uses_weighted_linear_map_normalizes_and_keeps_sign():
    geometry = two_bone_geometry()
    geometry.tangents = np.array([[1., 0, 0, -1.]])
    reference = deepcopy(geometry.bones)
    reference[0]["position"] = [1000., -2000., 3000.]
    reference[1]["rotation"] = [0, 0, 2 ** -.5, 2 ** -.5]
    actual = reference_skin(geometry, reference)
    np.testing.assert_allclose(actual.tangents, [[2 ** -.5, 2 ** -.5, 0., -1.]], atol=1e-12)
    geometry.tangents[0, :3] = 0
    np.testing.assert_array_equal(reference_skin(geometry, reference).tangents, [[0, 0, 0, -1]])
    geometry.tangents[0, 0] = 1e-7
    np.testing.assert_allclose(reference_skin(geometry, reference).tangents, actual.tangents, atol=1e-12)


@pytest.mark.parametrize("axis", [0, 1, 2, 3])
def test_exact_aliases_include_tangent_direction_and_sign(aliases, axis):
    geometry, render = aliases
    geometry.tangents[3, axis] = -1. if axis == 3 else np.nextafter(geometry.tangents[3, axis], np.inf)
    _, proof = audit_vertex_equivalence(geometry, render)
    assert not proof["passed"] and proof["unprovenSourceIds"] == 1
    assert "tangentXYZ/sign" in proof["examples"][0]["differentChannels"]


def test_stage_report_full_channel_booleans_stay_false_on_any_tangent_failure(capture_stage):
    capture_stage["units"][0]["envelope"]["native"]["render"]["vertices"][0]["binormalSign"] = -1.
    snapshot(capture_stage)
    publish(capture_stage)
    result = verify(capture_stage)
    assert not result["passed"] and not result["tangentsVerified"] and not result["fullSourceGeometryPreservationVerified"]
    assert result["unverifiedChannels"] == ["TANGENT"]


@pytest.mark.skipif(os.environ.get("ELYSIUM_R8_OLD_TANGENT_ALIASES") != "1", reason="explicit pinned old-alias rejection audit")
def test_actual_old_aliases_are_rejected_with_source_tangents():
    base = Path("E:/elysium-work/_r8_explore/agents/geometry")
    pinned = json.loads((base / "performance/inputs.json").read_bytes())
    products = []
    for entry in pinned["products"]:
        inputs = {kind: (base / "performance" / name).read_bytes() for kind, name in entry["files"].items()}
        for kind, data in inputs.items():
            assert hashlib.sha256(data).hexdigest() == entry["sha256"][kind]
        body, envelope = json.loads(inputs["body"]), json.loads(inputs["snapshot"])
        document, binary = decode_glb(inputs["glb"])
        geometry = read_stage(inputs["payload"])
        root = document["extensions"]["ELYSIUM_vtmb_model"]
        lod = next(r for r in root["vtx"]["lods"] if r["index"] == 0)
        primitives = {tuple(p["extensions"]["ELYSIUM_vtmb_model"][k] for k in ("bodyPart", "model", "mesh")): p
                      for p in document["meshes"][lod["mesh"]]["primitives"]}
        tangent = np.zeros((len(geometry.positions), 4))
        seen = set()
        for join in body["renderVertexMap"]:
            primitive = primitives[tuple(join[k] for k in ("bodyPart", "model", "mesh"))]
            source = primitive["extensions"]["ELYSIUM_vtmb_model"]
            lookup = {v: i for i, v in enumerate(source["sourceVertices"])}
            values = accessor(document, binary, primitive["attributes"]["TANGENT"])
            for original, staged in join["vertices"]:
                x, y, z, sign = values[lookup[original]]
                tangent[staged] = [x, z, y, -sign]
                seen.add(staged)
        assert len(seen) == len(tangent)
        geometry.tangents = tangent  # Independent source expectation; no pinned bytes are rewritten.
        _, proof = audit_vertex_equivalence(geometry, envelope["native"]["render"])
        assert not proof["passed"] and proof["unprovenSourceIds"] > 0
        products.append({"assetId": entry["assetId"], "snapshotSha256": entry["sha256"]["snapshot"], **proof})
    (base / "old_aliases_with_tangents_rejected.json").write_text(json.dumps(products, indent=2), encoding="utf-8")
