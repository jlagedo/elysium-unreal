"""PHYS1 arithmetic gates and explicit opt-in bounded installed-source evidence."""

import struct
from types import SimpleNamespace

import numpy as np
import pytest

from elysium_pipeline.validation.physics_calibration import (
    PhysicsEvidenceError, bind_references, constraint_bind_frames, euler_matrix, frame_candidates,
    interval, rotation_error_degrees, score_axis_cohort, score_axis_rates, score_pose_traces,
    score_solid_frames, signed_permutations, transform,
)


def test_permutations_cover_both_handednesses():
    bases = [p for _, p in signed_permutations()]
    assert len({p.tobytes() for p in bases}) == 48
    assert sum(np.linalg.det(p) > 0 for p in bases) == 24


def test_known_source_yaw_and_noncommuting_angles():
    np.testing.assert_allclose(euler_matrix([0, 90, 0]) @ [1, 0, 0], [0, 1, 0], atol=1e-12)
    assert rotation_error_degrees(euler_matrix([30, 40, 50]), euler_matrix([30, 40, 50], "xyz")) > 10
    assert rotation_error_degrees(np.eye(3), euler_matrix([0, 180, 0])) == pytest.approx(180)


def test_rotation_conjugation_cannot_identify_global_basis_sign():
    orient = euler_matrix([17, 31, 59])
    basis = np.array([[0, 1, 0], [0, 0, -1], [1, 0, 0]])
    np.testing.assert_array_equal(basis @ orient @ basis.T, (-basis) @ orient @ (-basis).T)


def test_solid_rank_can_identify_known_frame_and_exposes_missing_bone():
    basis = np.diag([1, -1, 1])
    prop = {"index": 0, "name": "Pelvis", "origin": [3, 7, 11], "angles": [13, 31, 47]}
    reference = {"pelvis": transform(basis @ prop["origin"], basis @ euler_matrix(prop["angles"]) @ basis.T)}
    rows = score_solid_frames([prop], reference)
    correct = next(r for r in rows if r["candidate"] == "+x,-y,+z/source_qangle/model/forward")
    assert correct["positionRmsCm"] == pytest.approx(0)
    assert correct["rotationRmsDegrees"] == pytest.approx(0, abs=1e-8)
    bad = score_solid_frames([prop], {}, frame_candidates()[:1])[0]
    assert bad["compared"] == 0 and bad["failures"]


def test_two_mdl_reference_representations_detect_disagreement():
    bone = SimpleNamespace(name="root", parent=-1, pos=[2, 0, 0], quat=[0, 0, 0, 1],
                           pose_to_bone=[1, 0, 0, -3, 0, 1, 0, 0, 0, 0, 1, 0])
    inverse, hierarchy = bind_references([bone])
    assert inverse["root"][0, 3] == 3
    assert hierarchy["root"][0, 3] == 2
    with pytest.raises(PhysicsEvidenceError):
        bind_references([bone, bone])


def test_retail_joint_frames_close_in_model_space_and_reject_missing_endpoint():
    parent = transform([3, 7, 11], euler_matrix([13, 29, 47]))
    child = transform([19, 23, 31], euler_matrix([53, 61, 71]))
    props = [{"index": 0, "name": "parent"}, {"index": 1, "name": "child"}]
    refs = {"parent": parent, "child": child}
    result = constraint_bind_frames(props, [{"parent": 0, "child": 1}], refs)[0]
    np.testing.assert_allclose(parent @ result["parentFrameSourceInches"], child, atol=1e-12)
    np.testing.assert_allclose(result["childFrameSourceInches"], np.eye(4))
    with pytest.raises(PhysicsEvidenceError):
        constraint_bind_frames(props, [{"parent": 0, "child": 9}], refs)


def test_asymmetric_interval_is_exact_but_zero_width_not_always_weld():
    assert interval(-25, 20, 1) == {"midpointDegrees": -2.5, "halfRangeDegrees": 22.5,
                                   "friction": 1, "zeroWidth": False}
    assert interval(7, 7, 2)["midpointDegrees"] == 7
    with pytest.raises(PhysicsEvidenceError):
        interval(20, -25, 1)
    with pytest.raises(PhysicsEvidenceError):
        interval(0, float("nan"), 0)


def test_axis_rates_require_independent_full_excitation():
    assert score_axis_rates([])["status"] == "unobserved"
    rates = [{"sourceRates": [1, 0, 0], "nativeRates": [0, 0, -1]}]
    assert score_axis_rates(rates)["status"] == "underexcited"
    rates += [{"sourceRates": [0, 2, 0], "nativeRates": [2, 0, 0]},
              {"sourceRates": [0, 0, 3], "nativeRates": [0, 3, 0]}]
    score = score_axis_rates(rates)
    assert score["scores"][0]["candidate"] == "+y,+z,-x"
    assert score["scores"][0]["rmsDegreesPerSecond"] == 0
    assert score["marginDegreesPerSecond"] > 0 and not score["accepted"]


def test_axis_cohort_does_not_substitute_one_joint_for_another():
    report = score_axis_cohort([("m", 0, 1), ("m", 0, 2)], [
        {"model": "m", "parentSolid": 0, "childSolid": 1,
         "sourceRates": [1, 0, 0], "nativeRates": [1, 0, 0]}])
    assert report["unobservedJoints"] == 1 and report["underexcitedJoints"] == 1
    assert report["rankedJoints"] == 0
    with pytest.raises(PhysicsEvidenceError):
        score_axis_cohort([], [{"model": "m", "parentSolid": 0, "childSolid": 1}])


def test_pose_scoring_never_fits_or_drops_samples():
    row = {"model": "m", "repeat": 0, "time": 1, "solid": 2,
           "positionCm": [0, 0, 0], "rotation": np.eye(3).tolist()}
    actual = dict(row, positionCm=[3, 4, 0])
    scores = score_pose_traces([row], {"offset": [actual], "missing": []})
    assert scores[0]["positionRmsCm"] == 5
    assert scores[1]["status"] == "coverage-mismatch"
    with pytest.raises(PhysicsEvidenceError):
        score_pose_traces([row, row], {})


def test_projection_audit_detects_lost_metadata_and_changed_hull(monkeypatch):
    from elysium_pipeline.validation import physics_source_evidence as evidence
    vertices = [[0., 0., 0.], [1., 0., 0.], [0., 1., 0.], [0., 0., 1.]]
    triangles = [[0, 2, 1], [0, 1, 3], [0, 3, 2], [1, 2, 3]]
    raw = {"solids": [{"properties": {"massbias": 2.0},
                       "hulls": [{"vertices": vertices, "triangles": triangles}]}]}
    projected = {"solids": [{"properties": {}, "hulls": [{"positions": 0, "indices": 1}]}]}
    changed = np.array(vertices)
    changed[1, 0] = 2
    monkeypatch.setattr(evidence, "read_accessor", lambda *args:
                        changed if args[-1] == 0 else np.array(triangles).reshape(-1, 1))
    failures, counts = evidence.projection_errors(raw, projected, None, {}, 0, 0)
    assert failures == ["physics.solids[0].properties.massbias: missing",
                        "physics.solids[0].hulls[0].vertices"]
    assert counts == {"vertices": 4, "triangles": 4}


def _synthetic_pe():
    data = bytearray(0x400)
    data[:2] = b"MZ"
    struct.pack_into("<I", data, 0x3c, 0x80)
    data[0x80:0x84] = b"PE\0\0"
    struct.pack_into("<HHIIIHH", data, 0x84, 0x14c, 1, 0, 0, 0, 224, 0)
    struct.pack_into("<H", data, 0x98, 0x10b)
    struct.pack_into("<I", data, 0x98+28, 0x400000)
    data[0x178:0x180] = b".text\0\0\0"
    struct.pack_into("<IIII", data, 0x180, 0x200, 0x1000, 0x100, 0x200)
    struct.pack_into("<I", data, 0x178+36, 0x60000020)
    data[0x200:0x202] = b"\x90\xc3"
    return bytes(data)


def test_pe_reads_use_file_sections_and_reject_virtual_only_or_wrong_architecture():
    from elysium_pipeline.validation.physics_source_evidence import PE32Evidence
    image = PE32Evidence(_synthetic_pe())
    assert image.read(0x401000, 2) == b"\x90\xc3"
    with pytest.raises(PhysicsEvidenceError):
        image.read(0x401100, 1)
    bad = bytearray(_synthetic_pe())
    struct.pack_into("<H", bad, 0x84, 0x8664)
    with pytest.raises(PhysicsEvidenceError):
        PE32Evidence(bytes(bad))


def test_static_listing_cannot_claim_different_instruction_bytes():
    from elysium_pipeline.validation.physics_source_evidence import PE32Evidence, installed_listing_rows
    image = PE32Evidence(_synthetic_pe())
    rows = installed_listing_rows("401000: 90  nop\n401001: c3  ret", image)
    assert len(rows) == 2
    with pytest.raises(PhysicsEvidenceError):
        installed_listing_rows("401000: c3  ret", image)


def test_retail_input_keeps_axis_signs_and_mass_scaled_zero_speed_motor():
    from elysium_pipeline.validation.physics_calibration import retail_ragdoll_input
    joint = {"xmin": -25, "xmax": 20, "xfriction": 1, "ymin": -40, "ymax": 20,
             "yfriction": 2, "zmin": -37, "zmax": 63, "zfriction": 3}
    result = retail_ragdoll_input(joint, 7, degrees_to_radians=1, solver_axis_order=[2, 0, 1])
    assert [r["limitsRadians"] for r in result] == [[-20, 25], [-20, 40], [-37, 63]]
    assert [r["intermediateAxis"] for r in result] == [0, 2, 1]
    assert [r["solverSlot"] for r in result] == [1, 0, 2]
    assert [r["motorStrength"] for r in result] == [7, 14, 21]
    assert all(r["targetAngularSpeed"] == 0 for r in result)
    assert all(r["solverSlot"] is None for r in retail_ragdoll_input(joint, 7, degrees_to_radians=1))
    with pytest.raises(PhysicsEvidenceError):
        retail_ragdoll_input(joint, 7, degrees_to_radians=1, solver_axis_order=[0, 0, 1])
