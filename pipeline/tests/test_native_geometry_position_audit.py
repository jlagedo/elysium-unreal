from copy import deepcopy
import hashlib
import json
import os
from pathlib import Path

import numpy as np
import pytest

from elysium_pipeline.validation.native_geometry import read_stage, reference_skin, check_authoring, check_render
from elysium_pipeline.validation.native_geometry_position_audit import writer_positions, position_precision_audit
from pipeline.tests.test_native_geometry import two_bone_geometry, fixture as geometry_fixture  # noqa: F401


def test_fvector3f_cast_is_distinguished_from_nonunit_quaternion_transform_error():
    geometry = two_bone_geometry((0, 1))
    geometry.positions *= 100
    reference = deepcopy(geometry.bones)
    reference[1]["rotation"] = [0, 0, 2 ** -.5 * (1 + 1e-7), 2 ** -.5 * (1 + 1e-7)]
    writer = writer_positions(geometry, reference)
    ideal = reference_skin(geometry, reference).positions.astype(np.float32).astype(float)
    assert np.max(np.abs(writer - ideal)) > 1e-4
    native = {"bones": reference, "authoring": {"vertices": [{"id": 0, "position": writer[0].tolist()}]}}
    result = position_precision_audit(geometry, {"wield": {"referencePose": reference}}, native)
    assert result["nativeIsFloat32"]
    assert result["idealFloat32"]["maxAbsoluteCm"] > 1e-4
    assert result["variants"]["float32/none"]["maxAbsoluteCm"] == 0
    assert result["doubleQuaternionVsIdealMatrix"]["maxAbsoluteCm"] == 0


def test_native_position_comparison_uses_declared_float32_projection_not_larger_tolerance(geometry_fixture):
    geometry = read_stage(geometry_fixture["payload"])
    geometry.positions[0, 0] = 1_000_000.03125  # Half an f32 ULP here; ideal matrix result.
    author, render = geometry_fixture["snapshot"]["authoring"], geometry_fixture["snapshot"]["render"]
    author["vertices"][0]["position"][0] = 1_000_000.
    render["vertices"][0]["position"][0] = 1_000_000.
    assert check_authoring(geometry, author)["passed"]
    assert check_render(geometry, render)["passed"]
    changed = float(np.nextafter(np.float32(1_000_000.), np.float32(np.inf)))
    author["vertices"][0]["position"][0] = changed
    render["vertices"][0]["position"][0] = changed
    for function, data in ((check_authoring, author), (check_render, render)):
        with pytest.raises(ValueError, match="position 0"):
            function(geometry, data)


@pytest.mark.skipif(os.environ.get("ELYSIUM_R8_POSITION_AUDIT") != "1", reason="explicit saved 35-failure position audit")
def test_actual_failed_authoring_positions():
    base = Path("E:/elysium-work/_r8_explore")
    stage, export = Path("E:/elysium-work/import/characters"), Path("E:/elysium-work/exports_v2")
    full_bytes = (base / "all_authored_morphs_geometry_report.json").read_bytes()
    full = json.loads(full_bytes)
    manifest_bytes = (stage / "manifest.json").read_bytes()
    manifest = json.loads(manifest_bytes)
    assets = {r["assetId"]: r for r in manifest["assets"]}
    failures = [r for r in full["products"] if any("authoring position" in d["reason"] for d in r.get("differences", []))]
    products, unavailable = [], []
    for row in failures:
        entry = assets[row["assetId"]]
        snapshot = stage / ("native_geometry/" + entry["assetId"].removeprefix("vtmb:model:") + ".json")
        data = snapshot.read_bytes()
        envelope = json.loads(data)
        # The numerical report preserves a receipt SHA even for a numeric rejection.
        if hashlib.sha256(data).hexdigest() != row["snapshotSha256"]:
            unavailable.append({"assetId": entry["assetId"], "reason": "historical snapshot replaced"})
            continue
        payload = (stage / entry["payload"]).read_bytes()
        body_data = (stage / entry["body"]).read_bytes()
        source = (export / entry["unitGlb"]).read_bytes()
        changed = [field for blob, field in ((payload, "payloadSha256"), (body_data, "bodySha256"), (source, "unitSha256"))
                   if hashlib.sha256(blob).hexdigest() != envelope[field]]
        if changed:
            unavailable.append({"assetId": entry["assetId"], "reason": "historical input replaced", "fields": changed})
            continue
        result = position_precision_audit(read_stage(payload), json.loads(body_data), envelope["native"])
        products.append({"assetId": entry["assetId"], "snapshotSha256": row["snapshotSha256"],
                         "inputHashes": {k: envelope[k] for k in ("payloadSha256", "bodySha256", "unitSha256")}, **result})
    result = {"scope": "35 saved failures only; diagnostic math, no acceptance threshold changes",
              "originalReportSha256": hashlib.sha256(full_bytes).hexdigest(), "manifestSha256": full["manifestSha256"],
              "currentManifestSha256": hashlib.sha256(manifest_bytes).hexdigest(),
              "products": products, "unavailableHistoricalInputs": unavailable}
    (base / "agents/geometry/authoring_position_precision_audit.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
    assert len(products) + len(unavailable) == 35
    assert products, unavailable
