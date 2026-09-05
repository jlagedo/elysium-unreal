"""Opt-in read-only R8 corpus check; outputs only the geometry lane's investigation report.

Set ELYSIUM_R8_GEOMETRY_CORPUS=1 for the current supplied work-root corpus. This is
an offline source/stage gate; it does not fabricate a native snapshot or accept a mesh.
"""
from collections import Counter
import hashlib
import json
import os
from pathlib import Path

import numpy as np
import pytest

from elysium_pipeline.formats.unit_contract.container import decode_glb
from elysium_pipeline.validation.native_geometry import read_stage, reference_skin, verify_source_geometry
from elysium_pipeline.validation.geometry_inventory import morph_source_inventory


@pytest.mark.skipif(os.environ.get("ELYSIUM_R8_GEOMETRY_CORPUS") not in ("1", "failures"), reason="explicit local R8 corpus opt-in")
def test_current_staged_mesh_source_coverage():
    stage = Path("E:/elysium-work/import/characters")
    export = Path("E:/elysium-work/exports_v2")
    report_dir = Path("E:/elysium-work/_r8_explore/agents/geometry")
    manifest_bytes = (stage / "manifest.json").read_bytes()
    manifest = json.loads(manifest_bytes)
    totals, failures, products, risks, non_mesh_inventory = Counter(), [], [], [], []
    previous = (json.loads((report_dir / "source_geometry_corpus.json").read_text())["failures"]
                if os.environ.get("ELYSIUM_R8_GEOMETRY_CORPUS") == "failures" else None)
    selected = {r["assetId"] for r in previous} if previous is not None else None
    for entry in manifest["assets"]:
        if selected is not None and entry["assetId"] not in selected:
            continue
        totals["meshEntries" if entry["meshAsset"] else "nonMeshEntries"] += 1
        try:
            payload = (stage / entry["payload"]).read_bytes()
            body_bytes = (stage / entry["body"]).read_bytes()
            raw = (export / entry["unitGlb"]).read_bytes()
            for data, key in ((payload, "payloadSha256"), (body_bytes, "bodySha256"), (raw, "unitSha256")):
                assert hashlib.sha256(data).hexdigest() == entry["recipe"][key], key + " changed"
            body = json.loads(body_bytes)
            document, binary = decode_glb(raw)
            if not entry["meshAsset"]:
                assert body["sourceSemantics"]["mdl"] == document["extensions"]["ELYSIUM_vtmb_model"]["mdl"]
                inventory = morph_source_inventory(document)
                totals["nonMeshPrimitiveMorphRecords"] += inventory["primitiveRecords"]
                totals["nonMeshUnrenderedMorphRecords"] += inventory["unrenderedMeshRecords"]
                non_mesh_inventory.append({"assetId": entry["assetId"], **inventory})
                continue
            geometry = read_stage(payload)
            inventory = verify_source_geometry(geometry, body, document, binary)
            posed = reference_skin(geometry, body["wield"]["referencePose"] if body.get("wield") else None)
            for key in ("vertices", "triangles", "primitiveRecords", "explicitZeroRecords", "repeatedContributions",
                        "unrenderedVertexRecords", "unrenderedMeshRecords", "stagedMorphRecords"):
                totals[key] += inventory[key]
            totals["morphTargets"] += len(geometry.morphs)
            totals["sourceNormalFallbacks"] += len(inventory["sourceNormalFallbacks"])
            totals["boneNameProjections"] += len(inventory["boneNameProjection"])
            totals["wieldMeshes"] += int(bool(body.get("wield")))
            totals["wieldMorphTargets"] += len(geometry.morphs) if body.get("wield") else 0
            normal_only = [(name, v) for name, values in posed.morphs.items() for v, d in values.items()
                           if not np.any(d[:3]) and np.any(d[3:])]
            totals["normalOnlyMorphRecords"] += len(normal_only)
            # UE constructs target positions in float32, then subtracts the base. Detect
            # nonzero source displacements that disappear even with threshold set to zero.
            cancelled = [(name, v) for name, values in posed.morphs.items() for v, d in values.items()
                         if np.any(d[:3]) and not np.any((posed.positions[v].astype(np.float32) + d[:3].astype(np.float32))
                                                        - posed.positions[v].astype(np.float32))]
            totals["float32CancelledPositionRecords"] += len(cancelled)
            if normal_only or cancelled:
                risks.append({"assetId": entry["assetId"], "normalOnlyRecords": len(normal_only),
                              "cancelledPositionRecords": len(cancelled), "examples": (normal_only + cancelled)[:5]})
            products.append({"assetId": entry["assetId"], **inventory, "wield": bool(body.get("wield")),
                             "morphTargets": len(geometry.morphs)})
        except (ValueError, KeyError, IndexError, TypeError, AssertionError) as exc:
            failures.append({"assetId": entry["assetId"], "reason": str(exc)})
    report_dir.mkdir(parents=True, exist_ok=True)
    report = {"scope": "read-only-GLB-stage-and-independent-wield-math; no native execution",
              "manifestSha256": hashlib.sha256(manifest_bytes).hexdigest(), "totals": dict(totals),
              "stageFailures": manifest["stageFailures"], "failures": failures, "nativeBuildRisks": risks,
              "products": products, "nonMeshInventory": non_mesh_inventory, "passed": not failures, "nativeAcceptance": False}
    filename = "source_geometry_recheck.json" if selected is not None else "source_geometry_corpus.json"
    (report_dir / filename).write_text(json.dumps(report, indent=2), encoding="utf-8")
    assert not failures, json.dumps({"count": len(failures), "first": failures[:8]}, indent=2)
