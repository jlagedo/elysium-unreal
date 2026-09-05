"""Opt-in two-pilot profile on pinned evidence, never the running shared corpus job."""
import cProfile
import hashlib
import io
import importlib.util
import json
import os
from pathlib import Path
import pstats
import sys
import time

import pytest

from elysium_pipeline.validation import native_geometry


ROOT = Path("E:/elysium-work/_r8_explore/agents/geometry/performance")
PILOTS = ("character/npc/unique/chinatown/ming_xiao/mingxiao",
          "character/npc/unique/santa_monica/jeanette/jeanette")


@pytest.mark.skipif(os.environ.get("ELYSIUM_R8_GEOMETRY_PROFILE") not in ("baseline", "optimized", "timing"), reason="explicit pinned pilot profile")
def test_pinned_pilot_profile():
    mode = os.environ["ELYSIUM_R8_GEOMETRY_PROFILE"]
    ROOT.mkdir(parents=True, exist_ok=True)
    if mode == "baseline":
        stage, export = Path("E:/elysium-work/import/characters"), Path("E:/elysium-work/exports_v2")
        manifest_bytes = (stage / "manifest.json").read_bytes()
        editor_bytes = (stage / "native_verify_report.json").read_bytes()
        manifest, editor = json.loads(manifest_bytes), json.loads(editor_bytes)
        assert hashlib.sha256(manifest_bytes).hexdigest() == editor["manifestSha256"]
        assets = {e["assetId"]: e for e in manifest["assets"]}
        receipts = {r["assetId"]: r for r in editor["geometrySnapshots"]}
        pinned = []
        for key in PILOTS:
            identity, name = "vtmb:model:" + key, key.rsplit("/", 1)[1]
            entry, receipt = assets[identity], receipts[identity]
            inputs = {"payload": ((stage / entry["payload"]).read_bytes(), entry["recipe"]["payloadSha256"]),
                      "body": ((stage / entry["body"]).read_bytes(), entry["recipe"]["bodySha256"]),
                      "glb": ((export / entry["unitGlb"]).read_bytes(), entry["recipe"]["unitSha256"]),
                      "snapshot": ((stage / receipt["file"]).read_bytes(), receipt["sha256"])}
            paths, hashes = {}, {}
            for kind, (data, digest) in inputs.items():
                assert hashlib.sha256(data).hexdigest() == digest
                path = ROOT / (name + "." + kind)
                path.write_bytes(data)
                paths[kind], hashes[kind] = path.name, digest
            pinned.append({"assetId": identity, "files": paths, "sha256": hashes})
        (ROOT / "inputs.json").write_text(json.dumps({"manifestSha256": hashlib.sha256(manifest_bytes).hexdigest(),
            "editorReportSha256": hashlib.sha256(editor_bytes).hexdigest(), "products": pinned}, indent=2), encoding="utf-8")
        (ROOT / "native_geometry_before_vectorization.py").write_bytes(Path(native_geometry.__file__).read_bytes())
    pinned = json.loads((ROOT / "inputs.json").read_bytes())
    if mode == "timing":
        spec = importlib.util.spec_from_file_location("r8_geometry_timing_baseline", ROOT / "native_geometry_before_vectorization.py")
        original = importlib.util.module_from_spec(spec)
        sys.modules[spec.name] = original
        spec.loader.exec_module(original)
        timings = []
        for entry in pinned["products"]:
            inputs = {kind: (ROOT / path).read_bytes() for kind, path in entry["files"].items()}
            for kind, data in inputs.items():
                assert hashlib.sha256(data).hexdigest() == entry["sha256"][kind]
            envelope, body = json.loads(inputs["snapshot"]), json.loads(inputs["body"])
            outputs, elapsed = [], []
            for function in (original.verify_native_geometry, native_geometry.verify_native_geometry):
                start = time.perf_counter()
                outputs.append(function(inputs["payload"], envelope["native"], body=body, source_glb=inputs["glb"],
                                        expected_source_sha256=entry["sha256"]["glb"], material_paths=envelope["materialPaths"]))
                elapsed.append(time.perf_counter() - start)
            assert outputs[0] == outputs[1], "unprofiled before/after outcomes changed"
            timings.append({"assetId": entry["assetId"], "beforeSeconds": elapsed[0], "afterSeconds": elapsed[1],
                            "identicalOutcomes": True, "passed": outputs[1]["passed"]})
        (ROOT / "unprofiled_comparison.json").write_text(json.dumps({"scope": "same pinned inputs, no profiler; excludes input I/O/envelope parse",
            "beforeSeconds": sum(t["beforeSeconds"] for t in timings), "afterSeconds": sum(t["afterSeconds"] for t in timings),
            "moduleSha256": hashlib.sha256(Path(native_geometry.__file__).read_bytes()).hexdigest(), "products": timings}, indent=2), encoding="utf-8")
        return
    profile, results, timings = cProfile.Profile(), [], []
    for entry in pinned["products"]:
        inputs = {kind: (ROOT / path).read_bytes() for kind, path in entry["files"].items()}
        for kind, data in inputs.items():
            assert hashlib.sha256(data).hexdigest() == entry["sha256"][kind]
        envelope, body = json.loads(inputs["snapshot"]), json.loads(inputs["body"])
        assert envelope["assetId"] == body["assetId"] == entry["assetId"]
        for kind, field in (("payload", "payloadSha256"), ("body", "bodySha256"), ("glb", "unitSha256")):
            assert entry["sha256"][kind] == envelope[field]
        start = time.perf_counter()
        result = profile.runcall(native_geometry.verify_native_geometry, inputs["payload"], envelope["native"], body=body,
                                source_glb=inputs["glb"], expected_source_sha256=entry["sha256"]["glb"], material_paths=envelope["materialPaths"])
        timings.append({"assetId": entry["assetId"], "seconds": time.perf_counter() - start})
        results.append({"assetId": entry["assetId"], "result": result})
    profile.dump_stats(str(ROOT / (mode + ".prof")))
    stream = io.StringIO()
    pstats.Stats(profile, stream=stream).sort_stats("cumulative").print_stats(35)
    (ROOT / (mode + "_profile.txt")).write_text(stream.getvalue(), encoding="utf-8")
    output = {"scope": "two pinned pilot numerical comparisons; cProfile timing excludes input I/O and envelope parsing",
              "moduleSha256": hashlib.sha256(Path(native_geometry.__file__).read_bytes()).hexdigest(),
              "seconds": sum(t["seconds"] for t in timings), "timings": timings, "products": results}
    (ROOT / (mode + "_results.json")).write_text(json.dumps(output, indent=2), encoding="utf-8")
    if mode == "optimized":
        before = json.loads((ROOT / "baseline_results.json").read_bytes())
        assert output["products"] == before["products"], "pinned numeric outcomes/counts/aliases/diagnostics changed"
        assert output["seconds"] < before["seconds"]
