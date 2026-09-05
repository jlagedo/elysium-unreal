"""Receipt/selection/hash integration with hand-authored geometry, no producer oracle."""
from copy import deepcopy
import hashlib
import json

import pytest

from elysium_pipeline.validation import native_geometry_stage as gate
from elysium_pipeline.validation.skeletal_diff import sections
from pipeline.tests.test_native_geometry import container, fixture as geometry_fixture, glb  # noqa: F401


def write(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    if not isinstance(data, bytes):
        data = json.dumps(data, separators=(",", ":")).encode()
    path.write_bytes(data)
    return hashlib.sha256(data).hexdigest()


def publish(case):
    case["editor"]["manifestSha256"] = write(case["stage"] / "manifest.json", case["manifest"])
    write(case["stage"] / "native_verify_report.json", case["editor"])


def snapshot(case, index=0):
    unit = case["units"][index]
    unit["receipt"]["sha256"] = write(unit["snapshotPath"], unit["envelope"])


def add_unit(case, suffix, *, mesh=True, selected=True):
    f = case["fixture"]
    identity = "vtmb:model:" + suffix
    document, body = deepcopy(f["document"]), deepcopy(f["body"])
    root = document["extensions"]["ELYSIUM_vtmb_model"]
    root["identity"]["asset"] = identity
    body["assetId"] = identity
    payload = f["payload"]
    if not mesh:
        payload = container({"SKEL": bytes(sections(payload)["SKEL"])})
        root["vtx"]["lods"] = []
        body["renderVertexMap"] = []
    body["sourceSemantics"] = deepcopy(root)
    entry = {"assetId": identity, "key": suffix, "payload": suffix + ".skel", "body": suffix + ".body.json",
             "unitGlb": "models/" + suffix + ".glb", "meshAsset": "/ElysiumBaked/Models/" + suffix.rsplit("/", 1)[0] + "/SK_" + suffix.rsplit("/", 1)[1] if mesh else None,
             "materials": [{"slot": "skin", "assetId": "vtmb:material:skin"}] if mesh else []}
    entry["recipe"] = {
        "payloadSha256": write(case["stage"] / entry["payload"], payload),
        "bodySha256": write(case["stage"] / entry["body"], body),
        "unitSha256": write(case["export"] / entry["unitGlb"], glb(document, f["binary"])),
    }
    unit = {"entry": entry, "body": body, "document": document}
    case["manifest"]["assets"].append(entry)
    if selected:
        case["manifest"]["selectedUnits"].append(identity)
    if mesh:
        envelope = {"snapshotVersion": 1, "assetId": identity, "meshAsset": entry["meshAsset"], **entry["recipe"],
                    "native": deepcopy(f["snapshot"]), "materialPaths": {"vtmb:material:skin": "/Materials/MI_skin"}}
        path = "native_geometry/" + suffix + ".json"
        receipt = {"assetId": identity, "file": path, "sha256": write(case["stage"] / path, envelope)}
        unit.update(envelope=envelope, receipt=receipt, snapshotPath=case["stage"] / path)
        if selected:
            case["editor"]["geometrySnapshots"].append(receipt)
    case["units"].append(unit)
    return unit


@pytest.fixture
def stage_case(tmp_path, geometry_fixture):
    case = {"stage": tmp_path / "stage", "export": tmp_path / "export", "fixture": geometry_fixture, "units": [],
            "manifest": {"schemaVersion": "1.0.0", "producer": "characters", "stageFailures": [], "selectedUnits": [], "assets": []},
            "editor": {"geometrySnapshots": [], "failed": []}}
    add_unit(case, "test/body")
    publish(case)
    return case


def verify(case):
    result = gate.verify_geometry_stage(case["stage"], case["export"])
    assert json.loads((case["stage"] / "native_geometry_report.json").read_text()) == result
    assert not list(case["stage"].glob("native_geometry_report.*.tmp"))
    return result


def reasons(report):
    return "\n".join(d["reason"] for d in report["differences"] + [d for p in report["products"] for d in p.get("differences", [])])


def test_valid_native_and_source_only_inputs_are_pinned_and_reported_compactly(stage_case):
    add_unit(stage_case, "test/source_only", mesh=False)
    publish(stage_case)
    result = verify(stage_case)
    assert result["passed"] and result["geometryPassed"] and result["editorNativePassed"]
    assert result["counts"]["expectedMeshes"] == result["counts"]["meshesCompared"] == 1
    assert result["counts"]["expectedSourceOnly"] == result["counts"]["sourceOnlyCompared"] == 1
    assert result["counts"]["unrenderedMeshRecords"] == 2
    assert result["counts"]["explicitZeroRecords"] == 3
    assert not result["renderedAcceptance"] and not result["evaluatedSkinningVerified"]
    assert result["tangentsVerified"] and result["fullSourceGeometryPreservationVerified"]
    assert result["unverifiedChannels"] == []
    mesh, source = result["products"]
    assert mesh["sourceInventory"]["vertices"] == mesh["authoring"]["vertices"] == mesh["render"]["vertices"] == 3
    assert source["kind"] == "source-only" and source["sourceInventory"]["unrenderedMeshRecords"] == 1
    assert "positions" not in json.dumps(result)
    assert "sourceNormalFallbacks" not in json.dumps(result)
    assert "materialPaths" not in json.dumps(result)


def test_scoped_selection_ignores_retained_unselected_assets_and_snapshot_files(stage_case):
    unselected = add_unit(stage_case, "test/retained", selected=False)
    # Retained manifest rows need no files for a scoped run. The unreceipted snapshot
    # also has no authority and is neither read nor removed.
    for root, field in (("stage", "payload"), ("stage", "body"), ("export", "unitGlb")):
        (stage_case[root] / unselected["entry"][field]).unlink()
    publish(stage_case)
    result = verify(stage_case)
    assert result["passed"] and result["counts"]["selectedUnits"] == 1
    assert result["counts"]["unselectedManifestEntries"] == 1
    assert unselected["snapshotPath"].exists()


@pytest.mark.parametrize("mode", ["missing", "duplicate", "extra", "source-only", "unselected"])
def test_receipts_require_exact_selected_mesh_coverage(stage_case, mode):
    receipts = stage_case["editor"]["geometrySnapshots"]
    if mode == "missing":
        receipts.clear()
    elif mode == "duplicate":
        receipts.append(deepcopy(receipts[0]))
    else:
        if mode == "source-only":
            add_unit(stage_case, "test/other", mesh=False)
        elif mode == "unselected":
            add_unit(stage_case, "test/other", selected=False)
        extra = deepcopy(receipts[0])
        extra.update(assetId="vtmb:model:test/other", file="native_geometry/test/other.json")
        receipts.append(extra)
    publish(stage_case)
    result = verify(stage_case)
    assert not result["passed"] and not result["geometryPassed"]
    assert "receipt" in reasons(result)
    if mode in ("extra", "source-only", "unselected"):
        assert result["products"][0]["passed"]


@pytest.mark.parametrize("key,value", [
    ("assetId", "vtmb:model:test/other"), ("meshAsset", "/Other/SK_body"),
    ("payloadSha256", "0" * 64), ("bodySha256", "0" * 64), ("unitSha256", "0" * 64),
    ("snapshotVersion", 2), ("snapshotVersion", True), ("native", None), ("materialPaths", []),
])
def test_valid_receipt_hash_cannot_hide_stale_or_invalid_envelope(stage_case, key, value):
    stage_case["units"][0]["envelope"][key] = value
    snapshot(stage_case)
    publish(stage_case)
    result = verify(stage_case)
    assert not result["passed"]
    assert "envelope" in reasons(result) or "snapshotVersion" in reasons(result)


@pytest.mark.parametrize("kind,field", [("stage", "payload"), ("stage", "body"), ("export", "unitGlb")])
@pytest.mark.parametrize("mesh", [True, False])
def test_all_selected_input_hashes_including_source_only_are_required(stage_case, kind, field, mesh):
    unit = stage_case["units"][0] if mesh else add_unit(stage_case, "test/source_only", mesh=False)
    path = stage_case[kind] / unit["entry"][field]
    path.write_bytes(path.read_bytes() + b" ")
    publish(stage_case)
    result = verify(stage_case)
    assert not result["passed"] and "Sha256 mismatch" in reasons(result)


def test_snapshot_bytes_must_match_receipt_hash(stage_case):
    path = stage_case["units"][0]["snapshotPath"]
    path.write_bytes(path.read_bytes() + b" ")
    result = verify(stage_case)
    assert not result["passed"] and "snapshot receipt sha256 mismatch" in reasons(result)


def test_report_manifest_hash_must_match_exact_bytes(stage_case):
    path = stage_case["stage"] / "manifest.json"
    path.write_bytes(path.read_bytes() + b"\n")
    result = verify(stage_case)
    assert not result["passed"] and "stale editor report manifestSha256" in reasons(result)
    assert not result["products"]


@pytest.mark.parametrize("path", ["../outside.json", "/outside.json", "C:/outside.json", "native_geometry/test/../body.json", "native_geometry/test/other.json"])
def test_receipt_paths_are_canonical_and_cannot_escape_stage(stage_case, path):
    stage_case["units"][0]["receipt"]["file"] = path
    publish(stage_case)
    result = verify(stage_case)
    assert not result["passed"] and "file" in reasons(result)


def test_source_input_path_cannot_escape_export(stage_case):
    stage_case["units"][0]["entry"]["unitGlb"] = "../outside.glb"
    publish(stage_case)
    result = verify(stage_case)
    assert not result["passed"] and "relative file path" in reasons(result)


@pytest.mark.parametrize("mode", ["duplicate-asset", "duplicate-selected", "missing-selected", "empty-selected", "failed-stage"])
def test_manifest_selection_is_validated(stage_case, mode):
    manifest = stage_case["manifest"]
    if mode == "duplicate-asset":
        manifest["assets"].append(deepcopy(manifest["assets"][0]))
    elif mode == "duplicate-selected":
        manifest["selectedUnits"] *= 2
    elif mode == "missing-selected":
        manifest["selectedUnits"].append("vtmb:model:test/missing")
    elif mode == "empty-selected":
        manifest["selectedUnits"] = []
    else:
        manifest["stageFailures"] = [{"reason": "stage failed"}]
    publish(stage_case)
    assert not verify(stage_case)["geometryPassed"]


def test_unrelated_editor_failures_do_not_suppress_geometry_diagnostics(stage_case):
    stage_case["editor"]["failed"] = [{"assetId": "unrelated-clip", "reason": "native clip data mismatch"}]
    publish(stage_case)
    result = verify(stage_case)
    assert result["geometryPassed"] and result["products"][0]["passed"]
    assert not result["passed"] and not result["editorNativePassed"]
    assert result["nativeFailures"] == stage_case["editor"]["failed"]


def test_numeric_corruption_fails_even_with_correct_envelope_and_receipt_hashes(stage_case):
    stage_case["units"][0]["envelope"]["native"]["authoring"]["vertices"][0]["position"][0] += 1
    snapshot(stage_case)
    publish(stage_case)
    result = verify(stage_case)
    assert not result["passed"] and "authoring position" in reasons(result)
    assert result["products"][0]["sourceInventory"]["passed"]


def test_comparator_runs_sequentially_in_selected_order_and_continues_after_failure(stage_case, monkeypatch):
    add_unit(stage_case, "test/second")
    stage_case["manifest"]["selectedUnits"].reverse()
    publish(stage_case)
    original, calls = gate.verify_native_geometry, []

    def compare(*args, **kwargs):
        calls.append(kwargs["body"]["assetId"])
        if len(calls) == 1:
            raise ValueError("one bad numeric unit")
        return original(*args, **kwargs)

    monkeypatch.setattr(gate, "verify_native_geometry", compare)
    result = verify(stage_case)
    assert calls == stage_case["manifest"]["selectedUnits"]
    assert not result["passed"] and result["products"][1]["passed"]


@pytest.mark.parametrize("which", ["manifest.json", "native_verify_report.json"])
def test_rewritten_checkpoint_or_manifest_during_comparison_invalidates_run(stage_case, monkeypatch, which):
    original = gate.verify_native_geometry

    def compare(*args, **kwargs):
        path = stage_case["stage"] / which
        path.write_bytes(path.read_bytes() + b"\n")
        return original(*args, **kwargs)

    monkeypatch.setattr(gate, "verify_native_geometry", compare)
    result = verify(stage_case)
    assert result["products"][0]["passed"] and not result["geometryPassed"]
    assert "changed during" in reasons(result)


def test_source_only_semantics_cannot_change_even_when_body_digest_is_updated(stage_case):
    unit = add_unit(stage_case, "test/source_only", mesh=False)
    unit["body"]["sourceSemantics"]["mdl"]["bodyParts"][0]["models"][0]["meshes"][1]["unrenderedMorphRecords"] = []
    unit["entry"]["recipe"]["bodySha256"] = write(stage_case["stage"] / unit["entry"]["body"], unit["body"])
    publish(stage_case)
    result = verify(stage_case)
    assert not result["passed"] and "source-only body/source semantics" in reasons(result)


def test_source_only_glb_identity_is_independent_of_matching_digest(stage_case):
    unit = add_unit(stage_case, "test/source_only", mesh=False)
    unit["document"]["extensions"]["ELYSIUM_vtmb_model"]["identity"]["asset"] = "vtmb:model:wrong/unit"
    unit["entry"]["recipe"]["unitSha256"] = write(stage_case["export"] / unit["entry"]["unitGlb"], glb(unit["document"], stage_case["fixture"]["binary"]))
    publish(stage_case)
    result = verify(stage_case)
    assert not result["passed"] and "source-only GLB identity" in reasons(result)


def test_duplicate_json_keys_and_nonfinite_snapshot_values_are_not_silently_accepted(stage_case):
    unit = stage_case["units"][0]
    data = json.dumps(unit["envelope"]).encode()
    for invalid in (b'{"snapshotVersion":1,' + data[1:], data.replace(b'"snapshotVersion": 1', b'"snapshotVersion": NaN')):
        unit["receipt"]["sha256"] = write(unit["snapshotPath"], invalid)
        publish(stage_case)
        result = verify(stage_case)
        assert not result["passed"]
        assert "JSON" in reasons(result)


def test_report_keeps_counts_instead_of_per_vertex_inventory_arrays(stage_case, monkeypatch):
    original = gate.verify_native_geometry

    def compare(*args, **kwargs):
        result = original(*args, **kwargs)
        result["sourceInventory"]["sourceNormalFallbacks"] = [{"secretVertexArray": [1, 2, 3]}] * 1000
        result["authoring"]["quantizedInfluenceLosses"] = [{"secretVertexArray": [4, 5, 6]}] * 10
        return result

    monkeypatch.setattr(gate, "verify_native_geometry", compare)
    result = verify(stage_case)
    assert "secretVertexArray" not in json.dumps(result)
    assert result["products"][0]["sourceInventory"]["normalFallbackVertices"] == 1000
    assert result["products"][0]["authoring"]["quantizedInfluenceLossCount"] == 10
    assert len(json.dumps(result)) < 6000


@pytest.mark.parametrize("file", ["manifest.json", "native_verify_report.json", "native_geometry/test/body.json", "test/body.skel", "test/body.body.json"])
def test_missing_required_file_produces_failure_evidence(stage_case, file):
    (stage_case["stage"] / file).unlink()
    assert not verify(stage_case)["passed"]


def test_selected_source_only_run_needs_no_mesh_receipts(stage_case):
    source = add_unit(stage_case, "test/source_only", mesh=False)
    stage_case["manifest"]["selectedUnits"] = [source["entry"]["assetId"]]
    stage_case["editor"]["geometrySnapshots"] = []
    publish(stage_case)
    result = verify(stage_case)
    assert result["passed"] and result["counts"]["expectedMeshes"] == 0
    assert result["counts"]["sourceOnlyCompared"] == 1


def test_source_only_classification_cannot_hide_staged_mesh(stage_case):
    source = add_unit(stage_case, "test/source_only", mesh=False)
    source["entry"]["recipe"]["payloadSha256"] = write(stage_case["stage"] / source["entry"]["payload"], stage_case["fixture"]["payload"])
    publish(stage_case)
    result = verify(stage_case)
    assert not result["passed"] and "source-only manifest entry contains staged mesh" in reasons(result)


def test_hexadecimal_digest_case_is_consistent_across_hash_layers(stage_case):
    unit = stage_case["units"][0]
    for key, digest in unit["entry"]["recipe"].items():
        unit["entry"]["recipe"][key] = digest.upper()
        unit["envelope"][key] = digest.upper()
    snapshot(stage_case)
    unit["receipt"]["sha256"] = unit["receipt"]["sha256"].upper()
    publish(stage_case)
    assert verify(stage_case)["passed"]


@pytest.mark.parametrize("field", ["manifestSha256", "geometrySnapshots", "failed"])
def test_missing_editor_integrity_fields_fail_closed(stage_case, field):
    del stage_case["editor"][field]
    write(stage_case["stage"] / "native_verify_report.json", stage_case["editor"])
    assert not verify(stage_case)["passed"]


def test_receipt_hash_must_be_a_real_sha256_digest(stage_case):
    stage_case["units"][0]["receipt"]["sha256"] = ""
    publish(stage_case)
    result = verify(stage_case)
    assert not result["passed"] and "sha256" in reasons(result)


def test_manifest_material_identity_join_is_checked_independently(stage_case):
    stage_case["units"][0]["entry"]["materials"][0]["assetId"] = "vtmb:material:wrong"
    publish(stage_case)
    result = verify(stage_case)
    assert not result["passed"] and "manifest material bindings" in reasons(result)


def test_malformed_extra_receipt_still_allows_valid_geometry_diagnostics(stage_case):
    stage_case["editor"]["geometrySnapshots"].append({"assetId": ["not", "an", "identity"]})
    publish(stage_case)
    result = verify(stage_case)
    assert not result["passed"] and result["products"][0]["passed"]
    assert "invalid model assetId" in reasons(result)


def test_numeric_json_overflow_is_rejected_before_comparison(stage_case):
    unit = stage_case["units"][0]
    encoded = json.dumps(unit["envelope"]).encode()
    encoded = b'{"extraMetadata":1e999,' + encoded[1:]
    unit["receipt"]["sha256"] = write(unit["snapshotPath"], encoded)
    publish(stage_case)
    result = verify(stage_case)
    assert not result["passed"] and "nonfinite JSON" in reasons(result)
