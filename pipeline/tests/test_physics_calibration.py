"""PHYS1 arithmetic gates and explicit opt-in bounded installed-source evidence."""

import json
import os
import struct
from pathlib import Path
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


@pytest.mark.skipif(not os.environ.get("ELYSIUM_PHYS1_EVIDENCE"), reason="explicit read-only source probe")
def test_installed_source_evidence():
    from elysium_pipeline.validation.physics_source_evidence import collect
    destination = Path(os.environ["ELYSIUM_PHYS1_EVIDENCE"]).resolve()
    allowed = Path(r"E:\elysium-work\_r8_explore\agents\physics").resolve()
    assert destination.is_relative_to(allowed) and destination.suffix == ".json"
    report = collect(export_root=Path(r"E:\elysium-work\exports_v2"),
                     stage_root=Path(r"E:\elysium-work\import\characters"),
                     maximum_canonical=int(os.environ["ELYSIUM_PHYS1_PILOT"]) if os.environ.get("ELYSIUM_PHYS1_PILOT") else None)
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text(json.dumps(report, indent=2, allow_nan=False), encoding="utf-8")
    print(json.dumps({k: report[k] for k in ("totals", "rigShapes", "errors")}))
    print(json.dumps(report["frameRanking"][:4]))
    assert not report["errors"], f"probe errors are retained in {destination}"


@pytest.mark.skipif(not os.environ.get("ELYSIUM_PHYS1_EXISTING"), reason="explicit saved-report summary")
def test_saved_cohort_evidence_summary():
    from elysium_pipeline.validation.physics_source_evidence import summarize
    path = Path(os.environ["ELYSIUM_PHYS1_EXISTING"]).resolve()
    allowed = Path(r"E:\elysium-work\_r8_explore\agents\physics").resolve()
    assert path.is_relative_to(allowed)
    report = json.loads(path.read_text(encoding="utf-8"))
    result = summarize(report)
    path.with_suffix(".summary.json").write_text(json.dumps(result, indent=2, allow_nan=False), encoding="utf-8")
    print(json.dumps(result, indent=2))
    assert not result["projectionErrors"]
    assert not result["stageMismatches"]
    assert not result["errors"]
    assert report["totals"]["scoredCanonicalRigs"] == report["totals"]["canonicalRigs"]


@pytest.mark.skipif(not os.environ.get("ELYSIUM_PHYS1_STATIC"), reason="explicit read-only retail corpus snapshot")
def test_retail_static_evidence():
    from elysium_pipeline.validation.physics_source_evidence import digest, retail_snapshot
    destination = Path(os.environ["ELYSIUM_PHYS1_STATIC"]).resolve()
    allowed = Path(r"E:\elysium-work\_r8_explore\agents\physics").resolve()
    assert destination.is_relative_to(allowed)
    sdk = Path(r"E:\elysium-work\research\reference-source\Bloodlines SDK\sdk-src (2025.12.30)\src_main")
    result = retail_snapshot(Path(r"E:\elysium-work\research\ghidra\corpus\corpus.sqlite"),
        Path(r"E:\dev_game\Vampire The Masquerade - Bloodlines"), [
            sdk / "vphysics/vcollide_parse.cpp", sdk / "vphysics/physics_constraint.cpp",
            sdk / "ivp/havana/havok/hk_physics/constraint/ragdoll/ragdoll_constraint_bp_builder.cpp",
            sdk / "public/vphysics/constraints.h",
            Path(r"D:\Epic\UE_5.8\Engine\Source\Runtime\Experimental\Chaos\Public\Chaos\PBDJointConstraintTypes.h")])
    report = json.loads((allowed / "installed_cohort.json").read_text(encoding="utf-8"))
    manifest = Path(r"E:\elysium-work\import\characters\manifest.json").read_bytes()
    result["stageManifestStillMatches"] = digest(manifest) == report["manifestSha256"]
    destination.write_text(json.dumps(result, indent=2, allow_nan=False), encoding="utf-8")
    print(json.dumps({"binaryProvenance": result["binaryProvenance"],
                      "stageManifestStillMatches": result["stageManifestStillMatches"]}))
    # No vphysics function is cited: its empty corpus fingerprint remains an explicit
    # evidence gap. Only the client/server functions above are install-verified.
    assert all(r["matchesCorpus"] for r in result["binaryProvenance"] if r["usedForFunctions"])


@pytest.mark.skipif(not os.environ.get("ELYSIUM_PHYS1_PE"), reason="explicit installed PE read-only investigation")
def test_installed_vphysics_pe():
    from elysium_pipeline.validation.physics_source_evidence import PE32Evidence
    path = Path(r"E:\dev_game\Vampire The Masquerade - Bloodlines\bin\vphysics.dll")
    image = PE32Evidence(path.read_bytes())
    result = image.facts()
    result["stringReferences"] = {}
    for name in ("massCenterOverride", "origin", "angles", "xmin", "xfriction", "ragdollconstraint", "inertia", "VPhysicsCollision", "VPhysics001"):
        addresses = image.occurrences(name.encode() + b"\0", executable=False)
        result["stringReferences"][name] = [{"va": hex(va), "pushByteHits": [hex(v) for v in image.occurrences(b"\x68" + va.to_bytes(4, "little"), executable=True)]} for va in addresses]
    result["interfaceStrings"] = [{"va": hex(v), "text": image.cstring(v)} for v in image.occurrences(b"VPhysics", executable=False)]
    result["parserVtable"] = [hex(int.from_bytes(image.read(0x260ca28c+i*4, 4), "little")) for i in range(10)]
    result["parserFactoryPointerHits"] = [hex(v) for v in image.occurrences((0x26024f90).to_bytes(4,"little"))]
    result["parserFactoryBranches"] = image.relative_branch_hits(0x26024f90)
    result["interfaceReferences"] = {hex(v): [hex(at) for at in image.occurrences(v.to_bytes(4,"little"))] for v in (0x260cf17c,0x260cf22c)}
    result["pointerHits"] = {hex(v): [hex(at) for at in image.occurrences(v.to_bytes(4,"little"))] for v in (0x260e4d1c,0x260f0698,0x2600ad90)}
    result["requestedTables"] = {}
    for value in os.environ.get("ELYSIUM_PHYS1_PEEK", "").split(","):
        if value:
            address = int(value, 16)
            result["requestedTables"][hex(address)] = [hex(int.from_bytes(image.read(address+i*4,4),"little")) for i in range(32)]
    target = Path(os.environ["ELYSIUM_PHYS1_PE"]).resolve()
    assert target.is_relative_to(Path(r"E:\elysium-work\_r8_explore\agents\physics").resolve())
    target.write_text(json.dumps(result, indent=2), encoding="utf-8")
    print(json.dumps(result, indent=2))


@pytest.mark.skipif(not os.environ.get("ELYSIUM_PHYS1_INSTALLED_RE"), reason="explicit bounded installed disassembly")
def test_installed_vphysics_semantics():
    from elysium_pipeline.validation.physics_source_evidence import vphysics_installed_receipt
    output = Path(os.environ["ELYSIUM_PHYS1_INSTALLED_RE"]).resolve()
    assert output.is_relative_to(Path(r"E:\elysium-work\_r8_explore\agents\physics").resolve())
    result = vphysics_installed_receipt(
        Path(r"E:\dev_game\Vampire The Masquerade - Bloodlines\bin\vphysics.dll"),
        Path(r"C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\Llvm\x64\bin\llvm-objdump.exe"),
        caller_binaries={
            "client.dll": Path(r"E:\dev_game\Vampire The Masquerade - Bloodlines\Vampire\cl_dlls\client.dll"),
            "vampire.dll": Path(r"E:\dev_game\Vampire The Masquerade - Bloodlines\Vampire\dlls\vampire.dll")})
    output.write_text(json.dumps(result, indent=2), encoding="utf-8")
    solid = [r["key"] for r in result["parserKeys"]["parse_solid"]]
    ragdoll = [r["key"] for r in result["parserKeys"]["parse_ragdoll"]]
    assert solid == ["index", "name", "parent", "surfaceprop", "mass", "massCenterOverride",
                     "inertia", "damping", "rotdamping", "volume", "drag", "rollingdrag"]
    assert ragdoll == ["parent", "child", "xmin", "xmax", "xfriction", "ymin", "ymax",
                       "yfriction", "zmin", "zmax", "zfriction"]
    assert result["tables"]["parserVtable"]["values"][3] == 0x260238f0
    assert result["tables"]["parserVtable"]["values"][5] == 0x26023c40
    assert result["tables"]["environmentVtable"]["values"][12] == 0x260107a0
    assert result["tables"]["collisionVtable"]["values"][26] == 0x2600ad90
    assert result["tables"]["motorAxisMap"]["values"] == [0, 2, 1]
    assert result["tables"]["limitAxisMap"]["values"] == [0, 2, 1]
    assert result["tables"]["objectVtable"]["values"][12] == 0x260173f0
    assert result["tables"]["objectVtable"]["values"][36] == 0x260182d0
    # Claims below are pinned to this observed image. A different install must be
    # reviewed as new evidence, not silently blessed by these address-specific gates.
    assert result["pe"]["sha256"] == "b8bbbf306b939055ce49548ace707cabc719783026d4044950b204de73d3ae10"
    instructions = {r["va"]: " ".join(r["instruction"].split()) for s in result["spans"].values() for r in s["rows"]}
    for address, expected in {
        "0x26023bba": "je 0x26023bce",  # absent callback skips unknown key
        "0x26023c85": "mov byte ptr [esi + 0x9a], 0x1",  # parsed clockwise flag
        "0x26023d92": "mov dword ptr [esi + 0x70], edi",  # x motor target = 0
        "0x2600ba4d": "mov al, byte ptr [ebp + 0x9a]",
        "0x2600bb02": "call 0x2609d850",
        "0x2600bb4b": "call dword ptr [edx + 0x30]",  # mass virtual
        "0x2600bb4e": "fmul dword ptr [edi + 0x4]",  # mass * friction
        "0x2609d927": "mov dword ptr [ebx + 0x9c], 0xbdcccccd",
        "0x2609d931": "mov dword ptr [ebx + 0xa0], 0x3dcccccd",
    }.items():
        assert instructions[address] == expected
    for module, address in (("client.dll", "0x10126daa"), ("vampire.dll", "0x1019c3da")):
        caller = {r["va"]: " ".join(r["instruction"].split()) for r in result["callers"][module]["span"]["rows"]}
        assert caller[address] == "push 0x0"  # both pass NULL unknown-key handler
    print(json.dumps({"sha256": result["pe"]["sha256"], "spanCount": len(result["spans"]),
                      "parserKeys": result["parserKeys"], "constants": result["constants"]}))


@pytest.mark.skipif(not os.environ.get("ELYSIUM_PHYS1_BRANCH_CENSUS"), reason="explicit bounded rig-only branch census")
def test_installed_ragdoll_branch_coverage():
    from elysium_pipeline.validation.physics_source_evidence import ragdoll_branch_census
    lane = Path(r"E:\elysium-work\_r8_explore\agents\physics").resolve()
    receipt = json.loads((lane / "installed_vphysics_semantics.json").read_text(encoding="utf-8"))
    result = ragdoll_branch_census(lane / "installed_cohort.json", receipt)
    (lane / "ragdoll_branch_census.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
    print(json.dumps({k: v for k, v in result.items() if k != "zeroFreedomJoints"}))
    assert not result["sourceHashesChanged"] and not result["incompleteFields"]
    assert result["rigs"] == 324
