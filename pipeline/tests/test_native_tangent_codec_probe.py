from collections import Counter
import hashlib
import json
import os
from pathlib import Path

import numpy as np
import pytest

from elysium_pipeline.validation.native_tangent_codec_probe import determinant_sign, exact_codec_input, pack16


def test_zero_bin_is_exact_and_does_not_change_packed_source_zero():
    assert np.float32(32767) * np.float32(2 ** -17) < np.float32(.5)
    assert np.array_equal(pack16([2 ** -17, -2 ** -17, 0]), [0, 0, 0])
    for normal in np.eye(3):
        result = exact_codec_input([0, 0, 0], normal, -1)
        assert result["packedX"] == [0, 0, 0]
        assert result["packedW"] == -32767
        assert determinant_sign(result["codecX"], result["codecY"], result["codecZ"]) == -1


def test_lantern_float32_cross_cancellation_can_use_double_cross_without_changing_x():
    x = np.array([.9070675969, .4209849238, 0], dtype=np.float32)
    z = -np.array([.9070675373, .4209848940, 0], dtype=np.float32)
    assert not np.any(np.cross(z, x))
    assert np.any(np.cross(z.astype(float), x.astype(float)))
    result = exact_codec_input(x, z, -1)
    assert result["mode"] == "original-x-double-cross"
    assert np.array_equal(result["codecX"], x)
    assert result["packedW"] == -32767


def test_codec_probe_preserves_packed_axes_for_parallel_and_general_bases():
    rng = np.random.default_rng(827)
    for i in range(600):
        n = rng.normal(size=3)
        n = (n / np.linalg.norm(n)).astype(np.float32)
        x = np.zeros(3) if i % 3 == 0 else n.copy() if i % 3 == 1 else -n.copy()
        sign = -1 if i % 2 else 1
        result = exact_codec_input(x, n, sign)
        assert np.array_equal(pack16(result["codecX"]), pack16(x))
        assert np.array_equal(pack16(result["codecZ"]), pack16(n))
        assert determinant_sign(result["codecX"], result["codecY"], result["codecZ"]) == sign


@pytest.mark.skipif(os.environ.get("ELYSIUM_R8_CODEC_PROBE") != "1", reason="explicit four-snapshot codec-input proof")
def test_current_four_snapshot_authored_bases_have_exact_codec_inputs():
    stage = Path("E:/elysium-work/import/characters")
    report = json.loads((stage / "native_verify_report.json").read_bytes())
    keys = {"character/npc/unique/santa_monica/jeanette/jeanette", "character/npc/unique/downtown/bomb_guy/bomb_guy",
            "weapons/handleclaws/wield/w_f_handleclaws", "scenery/structural/chinese/lanternskins"}
    products = []
    for receipt in report["geometrySnapshots"]:
        if receipt["assetId"].removeprefix("vtmb:model:") not in keys:
            continue
        data = (stage / receipt["file"]).read_bytes()
        assert hashlib.sha256(data).hexdigest() == receipt["sha256"]
        envelope = json.loads(data)
        native = envelope["native"]
        assert native["render"]["normalBits"] == 16, "probe is deliberately limited to the actual SNORM16 build"
        by_source = {}
        for instance in native["authoring"]["instances"]:
            basis = (instance["tangentX"], instance["normal"], instance["binormalSign"])
            if instance["vertex"] in by_source:
                assert by_source[instance["vertex"]] == basis
            by_source[instance["vertex"]] = basis
        modes, examples, mismatches = Counter(), [], 0
        for vertex, row in enumerate(native["render"]["vertices"]):
            x, n, sign = by_source[row["sourceVertex"]]
            result = exact_codec_input(x, n, sign)
            assert result["packedW"] == int(sign * 32767)
            assert np.array_equal(pack16(result["codecX"]), pack16(x))
            assert np.array_equal(pack16(result["codecZ"]), pack16(n))
            assert np.array_equal(pack16(row["tangentX"]), pack16(x)), "current packed X differs from authored quantization"
            assert np.array_equal(pack16(row["normal"]), pack16(n)), "current packed N differs from authored quantization"
            modes[result["mode"]] += 1
            if row["binormalSign"] != sign:
                mismatches += 1
                if len(examples) < 5:
                    examples.append({"renderVertex": vertex, "sourceVertex": row["sourceVertex"], "authoredX": x,
                                     "authoredNormal": n, "authoredW": sign, "currentPackedW": row["binormalSign"], **result})
        products.append({"assetId": receipt["assetId"], "snapshotSha256": receipt["sha256"],
                         "vertices": len(native["render"]["vertices"]), "currentSignMismatches": mismatches,
                         "codecModes": dict(modes), "allProposedPackedXYZAndSignsExact": True, "examples": examples})
    output = {"scope": "pure 16-bit stock-codec-input proof; no asset/engine writes, no native execution",
              "nativeAcceptance": False, "products": products}
    Path("E:/elysium-work/_r8_explore/agents/geometry/tangent_codec_input_probe.json").write_text(json.dumps(output, indent=2), encoding="utf-8")
    assert len(products) == 4
