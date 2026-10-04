from copy import deepcopy

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
