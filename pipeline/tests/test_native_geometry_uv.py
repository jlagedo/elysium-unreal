import hashlib
import json
import os
from pathlib import Path
import struct

import numpy as np
import pytest

from elysium_pipeline.validation.native_geometry import check_render, read_stage, verify_native_geometry
from elysium_pipeline.validation.native_geometry_uv import uv_precision_inventory
from elysium_pipeline.validation.skeletal_diff import sections
from pipeline.tests.test_native_geometry import container, fixture as geometry_fixture, glb  # noqa: F401
from pipeline.tests.test_native_geometry_stage import stage_case as capture_stage, snapshot, publish, verify  # noqa: F401


def compare(fixture):
    source = glb(fixture["document"], fixture["binary"])
    return verify_native_geometry(fixture["payload"], fixture["snapshot"], body=fixture["body"], source_glb=source,
                                  expected_source_sha256=hashlib.sha256(source).hexdigest(),
                                  material_paths={"vtmb:material:skin": "/Materials/MI_skin"})


@pytest.mark.parametrize("token", ["NaN", "Infinity", "-Infinity"])
@pytest.mark.parametrize("path,context,component", [
    (("render", "vertices", 1, "uv", 0), "render UV 1 (source 1)", "(0,)"),
    (("render", "vertices", 2, "uv", 1), "render UV 2 (source 2)", "(1,)"),
    (("render", "vertices", 0, "position", 2), "render position 0", "(2,)"),
    (("render", "vertices", 0, "normal", 1), "render source normal 0", "(1,)"),
    (("authoring", "vertices", 1, "position", 0), "authoring position 1", "(0,)"),
    (("render", "morphs", 0, "deltas", 0, 4), "render morph: smile id 0", "(3,)"),
    (("authoring", "morphs", 0, "normals", 0, 2), "authoring morph normal: smile id 0", "(1,)"),
    (("bones", 0, "rotation", 0), "native reference rotation: root", "(0,)"),
])
def test_diagnostic_tokens_reject_with_vertex_channel_context(geometry_fixture, token, path, context, component):
    row = geometry_fixture["snapshot"]
    for key in path[:-1]:
        row = row[key]
    row[path[-1]] = token
    result = compare(geometry_fixture)
    assert not result["passed"]
    reason = result["differences"][0]["reason"]
    assert context in reason and "nonfinite component " + component in reason and token in reason


def test_native_token_report_precedes_source_half_overflow(geometry_fixture):
    geometry = read_stage(geometry_fixture["payload"])
    geometry.uvs[0, 1] = np.float32(-1.097e24)
    render = geometry_fixture["snapshot"]["render"]
    render["vertices"][0]["uv"][1] = "-Infinity"
    with pytest.raises(ValueError, match=r"render UV 0 \(source 0\) actual: nonfinite component \(1,\): -Infinity"):
        check_render(geometry, render)


@pytest.mark.parametrize("token", ["NaN", "Infinity", "-Infinity"])
def test_quoted_diagnostic_survives_envelope_hash_gate_and_fails_with_context(capture_stage, token):
    capture_stage["units"][0]["envelope"]["native"]["render"]["vertices"][1]["uv"][0] = token
    snapshot(capture_stage)
    publish(capture_stage)
    report = verify(capture_stage)
    assert report["editorNativePassed"] and not report["geometryPassed"] and not report["passed"]
    reason = report["products"][0]["differences"][0]["reason"]
    assert "render UV 1 (source 1) actual: nonfinite component (0,): " + token in reason


def test_finite_huge_uv_is_not_clamped_and_passes_only_with_full_precision(geometry_fixture):
    geometry = read_stage(geometry_fixture["payload"])
    geometry.uvs[0, 0] = np.float32(1.45e21)
    render = geometry_fixture["snapshot"]["render"]
    render["vertices"][0]["uv"][0] = float(geometry.uvs[0, 0])
    with pytest.raises(ValueError, match=r"render UV 0 .* source half UV projection: nonfinite component \(0,\)"):
        check_render(geometry, render)
    render["fullPrecisionUVs"] = True
    assert check_render(geometry, render)["passed"]
    render["vertices"][0]["uv"][0] = 65504.
    with pytest.raises(ValueError, match="render UV 0"):
        check_render(geometry, render)


def test_uv_census_distinguishes_finite_half_range_and_rounding_overflow(geometry_fixture):
    parts = {k: bytes(v) for k, v in sections(geometry_fixture["payload"]).items()}
    mesh = bytearray(parts["MESH"])
    # This hand-authored fixture has one four-byte slot name, then three 56-byte vertices.
    vertex_start = 12 + 4 + 4 + 8
    values = [65504., 65519., 65520., -1.097e24, -6.78e20, 1.45e21]
    for vertex in range(3):
        struct.pack_into("<2f", mesh, vertex_start + 56 * vertex + 24, *values[vertex * 2:vertex * 2 + 2])
    parts["MESH"] = bytes(mesh)
    result = uv_precision_inventory(container(parts))
    assert result["outsideFiniteHalfRangeComponents"] == 5
    assert result["roundedHalfNonfiniteComponents"] == 4
    assert result["sourceFinite"] and result["affectedVertices"] == 3
    assert result["examples"][0]["component"] == "V"


@pytest.mark.skipif(os.environ.get("ELYSIUM_R8_UV_CENSUS") != "1", reason="explicit read-only staged UV census")
def test_staged_uv_precision_census():
    stage = Path("E:/elysium-work/import/characters")
    data = (stage / "manifest.json").read_bytes()
    manifest = json.loads(data)
    selected = set(manifest["selectedUnits"])
    products, failures, vertices, components, meshes = [], [], 0, 0, 0
    for entry in manifest["assets"]:
        if entry["assetId"] not in selected or not entry["meshAsset"]:
            continue
        meshes += 1
        try:
            payload = (stage / entry["payload"]).read_bytes()
            assert hashlib.sha256(payload).hexdigest() == entry["recipe"]["payloadSha256"]
            row = uv_precision_inventory(payload)
            vertices += row["vertices"]
            components += row["components"]
            if row["outsideFiniteHalfRangeComponents"]:
                products.append({"assetId": entry["assetId"], "payloadSha256": entry["recipe"]["payloadSha256"], **row})
        except (ValueError, KeyError, AssertionError) as exc:
            failures.append({"assetId": entry["assetId"], "reason": str(exc)})
    assert (stage / "manifest.json").read_bytes() == data, "manifest changed during UV census"
    result = {"scope": "read-only-staged-UV-precision-census; no native snapshot reads or changes",
              "manifestSha256": hashlib.sha256(data).hexdigest(), "meshes": meshes, "vertices": vertices,
              "components": components, "affectedMeshes": len(products),
              "outsideFiniteHalfRangeComponents": sum(p["outsideFiniteHalfRangeComponents"] for p in products),
              "roundedHalfNonfiniteComponents": sum(p["roundedHalfNonfiniteComponents"] for p in products),
              "failures": failures, "products": products, "passed": not failures}
    Path("E:/elysium-work/_r8_explore/agents/geometry/stage_uv_precision_census.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
    assert not failures, failures[:4]
