"""Adversarial channel/membership fixtures and optional comparison to the pinned scalar reader."""
from copy import deepcopy
import importlib.util
import json
from pathlib import Path
import sys

import numpy as np
import pytest

from elysium_pipeline.validation import native_geometry as current
from pipeline.tests.test_native_geometry import fixture as geometry_fixture  # noqa: F401


CASES = ["tiny-position", "tiny-normal", "lost-normal-channel", "lost-position-channel", "duplicate-delta", "out-of-range-delta",
         "nonfinite-delta", "missing-normal-attribute", "normal-over-bound", "normal-at-bound", "explicit-native-zero",
         "reordered-vertices", "sparse-instance-ids", "late-instance-corruption"]
PASS_CASES = {"normal-at-bound", "explicit-native-zero", "reordered-vertices", "sparse-instance-ids"}


def case_data(fixture, case):
    geometry = current.read_stage(fixture["payload"])
    author, render = deepcopy(fixture["snapshot"]["authoring"]), deepcopy(fixture["snapshot"]["render"])
    if case in {"tiny-position", "tiny-normal", "lost-normal-channel", "lost-position-channel"}:
        delta = {"tiny-position": [1e-7, 0, 0, 0, 0, 0], "tiny-normal": [0, 0, 0, 1e-7, 0, 0],
                 "lost-normal-channel": [.508, 0, 0, 1e-7, 0, 0], "lost-position-channel": [1e-7, 0, 0, 0, -.2, 0]}[case]
        geometry.morphs["smile"] = {0: np.asarray(delta)}
        author["morphs"][0].update(positions=[[0, *delta[:3]]], normals=[[0, *delta[3:]]])
        render["morphs"][0]["deltas"] = [[0, *delta]]
        if case.startswith("tiny"):
            author["morphs"][0].update(positions=[], normals=[])
            render["morphs"][0].update(deltas=[], sections=[])
        elif case == "lost-normal-channel":
            author["morphs"][0]["normals"] = []
            render["morphs"][0]["deltas"][0][4:] = [0, 0, 0]
        else:
            author["morphs"][0]["positions"] = []
            render["morphs"][0]["deltas"][0][1:4] = [0, 0, 0]
    elif case == "duplicate-delta":
        render["morphs"][0]["deltas"] *= 2
        author["morphs"][0]["positions"] *= 2
    elif case == "out-of-range-delta":
        render["morphs"][0]["deltas"][0][0] = 3
        author["morphs"][0]["positions"][0][0] = 3
    elif case == "nonfinite-delta":
        render["morphs"][0]["deltas"][0][4] = "Infinity"
        author["morphs"][0]["normals"][0][1] = "Infinity"
    elif case == "missing-normal-attribute":
        author["morphs"][0]["normalsPresent"] = False
        render["morphs"][0]["deltas"][0][4:] = [0, 0, 0]
    elif case in {"normal-at-bound", "normal-over-bound"}:
        value = current.NORMAL if case == "normal-at-bound" else np.nextafter(current.NORMAL, np.inf)
        render["morphs"][0]["deltas"][0][4] = value
        author["morphs"][0]["normals"][0][1] = value
    elif case == "explicit-native-zero":
        render["morphs"][0]["deltas"].append([1, 0, 0, 0, 0, 0, 0])
        author["morphs"][0]["positions"].append([1, 0, 0, 0])
    elif case == "reordered-vertices":
        author["vertices"].reverse()
        author["instances"].reverse()
        render["vertices"] = [render["vertices"][i] for i in (2, 0, 1)]
        render["indices"] = [1, 0, 2]
        render["morphs"][0]["deltas"][0][0] = 1
    elif case in {"sparse-instance-ids", "late-instance-corruption"}:
        ids = {0: 71, 1: 4, 2: 800}
        for row in author["instances"]:
            row["id"] = ids[row["id"]]
        author["triangles"][0]["instances"] = [71, 4, 800]
        author["morphs"][0]["normals"][0][0] = 71
        if case == "late-instance-corruption":
            author["morphs"][0]["normals"].append([800, .01, 0, 0])
            render["morphs"][0]["deltas"].append([2, 0, 0, 0, .01, 0, 0])
    return geometry, author, render


def outcome(function, geometry, data):
    try:
        return {"accepted": True, "result": function(geometry, data)}
    except (ValueError, KeyError, TypeError, IndexError) as exc:
        return {"accepted": False, "reason": str(exc)}


@pytest.mark.parametrize("case", CASES)
def test_vectorized_keeps_channel_membership_and_diagnostics(geometry_fixture, case):
    geometry, author, render = case_data(geometry_fixture, case)
    for function, data in ((current.check_authoring, author), (current.check_render, render)):
        result = outcome(function, geometry, data)
        assert result["accepted"] == (case in PASS_CASES), result
        if not result["accepted"]:
            assert "morph" in result["reason"]


def test_outcomes_match_the_pinned_preoptimization_scalar_reader(geometry_fixture):
    root = Path("E:/elysium-work/_r8_explore/agents/geometry/performance")
    path = root / "native_geometry_before_vectorization.py"
    if not path.exists():
        pytest.skip("local preoptimization scalar reader not pinned")
    spec = importlib.util.spec_from_file_location("r8_geometry_scalar_baseline", path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    results = []
    for case in CASES:
        geometry, author, render = case_data(geometry_fixture, case)
        for method, data in (("check_authoring", author), ("check_render", render)):
            before = outcome(getattr(module, method), geometry, data)
            after = outcome(getattr(current, method), geometry, data)
            # The pinned scalar reader predates TANG acceptance. Compare its original
            # channels only; new tangent acceptance has independent negative fixtures.
            shared = deepcopy(after)
            if shared["accepted"]:
                for key in ("tangentsVerified", "tangentVectors", "zeroTangentVectors", "tangentComponentBound"):
                    shared["result"].pop(key, None)
            assert shared == before, (case, method, before, shared)
            results.append({"case": case, "method": method, "outcome": after})
    (root / "negative_fixture_equivalence.json").write_text(json.dumps(results, indent=2), encoding="utf-8")
