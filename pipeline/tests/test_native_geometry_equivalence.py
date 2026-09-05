from copy import deepcopy
import hashlib
import json
import os
from pathlib import Path

import numpy as np
import pytest

from elysium_pipeline.validation.native_geometry import read_stage, reference_skin, check_render, verify_native_geometry
from elysium_pipeline.validation.native_geometry_equivalence import audit_vertex_equivalence
from pipeline.tests.test_native_geometry import fixture as geometry_fixture  # noqa: F401


@pytest.fixture
def aliases(geometry_fixture):
    geometry = read_stage(geometry_fixture["payload"])
    for name in ("positions", "normals", "uvs", "joints", "weights", "tangents"):
        values = getattr(geometry, name)
        setattr(geometry, name, np.concatenate([values, values[:1]], axis=0))
    geometry.morphs["smile"][3] = geometry.morphs["smile"][0].copy()
    geometry.triangles.append((0, 3, 2, 1))
    render = deepcopy(geometry_fixture["snapshot"]["render"])
    render["indices"] += [0, 2, 1]
    render["sections"][0]["numTriangles"] += 1
    return geometry, render


def test_exact_source_alias_preserves_all_channels_and_triangle_multiplicity(aliases):
    geometry, render = aliases
    classes, proof = audit_vertex_equivalence(geometry, render)
    assert proof["passed"] and classes == [0, 1, 2, 0]
    assert proof["sourceAliases"] == [[3, 0]]
    assert proof["originalSourceVertices"] == 4 and proof["representedSourceIds"] == 3
    native = check_render(geometry, render)
    assert native["passed"] and native["sourceVertices"] == 4 and native["sourceAliasCount"] == 1
    assert native["sourceAliases"] == [[3, 0]]


@pytest.mark.parametrize("channel", ["positions", "normals", "uvs", "weights", "joints", "morphPosition", "morphNormal", "morphMembership"])
def test_alias_requires_every_meaningful_channel_to_be_exact(aliases, channel):
    geometry, render = aliases
    if channel == "morphMembership":
        geometry.morphs["extraZero"] = {3: np.zeros(6)}
    elif channel in ("morphPosition", "morphNormal"):
        geometry.morphs["smile"][3][0 if channel == "morphPosition" else 3] += 1e-12
    else:
        values = getattr(geometry, channel)
        values[3, 0] += 1 if channel == "joints" else 1e-12
    _, proof = audit_vertex_equivalence(geometry, render)
    assert not proof["passed"] and proof["unprovenSourceIds"] == 1
    with pytest.raises(ValueError, match="source vertex omission"):
        check_render(geometry, render)


@pytest.mark.parametrize("mode", ["multiplicity", "winding", "material"])
def test_exact_attributes_do_not_excuse_different_triangles(aliases, mode):
    geometry, render = aliases
    if mode == "multiplicity":
        render["indices"] = render["indices"][:3]
        render["sections"][0]["numTriangles"] = 1
    elif mode == "winding":
        render["indices"][3:] = [0, 1, 2]
    else:
        render["sections"][0]["material"] = 1
    _, proof = audit_vertex_equivalence(geometry, render)
    assert not proof["passed"] and proof["missingEquivalentTriangles"] > 0


@pytest.mark.skipif(os.environ.get("ELYSIUM_R8_GEOMETRY_PILOTS") != "1", reason="explicit read-only pilot snapshot audit")
def test_pilot_snapshot_alias_evidence():
    stage = Path("E:/elysium-work/import/characters")
    report = json.loads((stage / "native_verify_report.json").read_bytes())
    manifest_bytes = (stage / "manifest.json").read_bytes()
    assert hashlib.sha256(manifest_bytes).hexdigest() == report["manifestSha256"]
    manifest = json.loads(manifest_bytes)
    assets = {e["assetId"]: e for e in manifest["assets"]}
    results = []
    numeric = []
    for receipt in report["geometrySnapshots"]:
        data = (stage / receipt["file"]).read_bytes()
        assert hashlib.sha256(data).hexdigest() == receipt["sha256"]
        envelope = json.loads(data)
        entry = assets[receipt["assetId"]]
        payload = (stage / entry["payload"]).read_bytes()
        body_data = (stage / entry["body"]).read_bytes()
        assert hashlib.sha256(payload).hexdigest() == entry["recipe"]["payloadSha256"] == envelope["payloadSha256"]
        assert hashlib.sha256(body_data).hexdigest() == entry["recipe"]["bodySha256"] == envelope["bodySha256"]
        body = json.loads(body_data)
        geometry = reference_skin(read_stage(payload), body["wield"]["referencePose"] if body.get("wield") else None)
        _, proof = audit_vertex_equivalence(geometry, envelope["native"]["render"])
        results.append({"assetId": entry["assetId"], "snapshotSha256": receipt["sha256"], **proof})
        source = (Path("E:/elysium-work/exports_v2") / entry["unitGlb"]).read_bytes()
        result = verify_native_geometry(payload, envelope["native"], body=body, source_glb=source,
                                        expected_source_sha256=entry["recipe"]["unitSha256"], material_paths=envelope["materialPaths"])
        numeric.append({"assetId": entry["assetId"], "passed": result["passed"], "differences": result["differences"],
                        "sourceInventoryPassed": result["sourceInventory"]["passed"], "authoringPassed": result["authoring"]["passed"]})
    output = Path("E:/elysium-work/_r8_explore/agents/geometry/pilot_vertex_equivalence.json")
    output.write_text(json.dumps({"scope": "exact source equivalence investigation", "products": results}, indent=2), encoding="utf-8")
    output.with_name("pilot_numeric_after_equivalence.json").write_text(json.dumps(numeric, indent=2), encoding="utf-8")
    assert len(results) == 2
