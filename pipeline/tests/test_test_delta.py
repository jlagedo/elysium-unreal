"""Exact differences, expectation consumption and failed-report rejection."""
from __future__ import annotations

import json
import sys
from pathlib import Path

import pytest

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "research/tooling"))
import test_delta as td  # noqa: E402


def policy(*changes, ordered=()):
    return {"schema_version": 1, "ordered_tests": list(ordered), "changes": list(changes)}


def change(kind, test="A", **kwargs):
    return {"id": f"{kind}-{test}", "kind": kind, "test": test,
            "packet": "synthetic-test", "evidence": "synthetic observable", **kwargs}


def test_exact_additions_removals_and_renames():
    before = {"Old": [("Warning", "kept")], "Removed": []}
    after = {"New": [("Warning", "kept")], "Added": []}
    result = td.compare(before, after, policy(
        change("rename", "Old", to="New"), change("addition", "Added"),
        change("removal", "Removed", coverage_disposition="assertions carried by New")))
    assert result["comparison_passed"]
    assert len(result["applied_expectations"]) == 3


def test_unmatched_change_and_unreviewed_removal_fail():
    assert not td.compare({"A": []}, {"B": []}, policy())["comparison_passed"]
    with pytest.raises(td.InvalidReport, match="coverage disposition"):
        td.compare({"A": []}, {}, policy(change("removal")))
    with pytest.raises(td.InvalidReport, match="unused"):
        td.compare({"A": []}, {"A": []}, policy(change("addition", "Absent")))


def stub(label="OldOwner::Call", address="0x10000001", receiver="npc_example"):
    return ("Warning", "LogElysiumStub: Stub fired!!!!! [slot] " + label
            + f" | {address} (29c) | on #4 example({receiver}) | owner: the NPC kernel")


def test_diagnostic_remap_preserves_retail_identity_receiver_and_count():
    old, new = stub(), stub(label="NewOwner::Call")
    expectation = change("diagnostic_remap", before=list(old), after=list(new))
    assert td.compare({"A": [old, old]}, {"A": [new, new]}, policy(expectation))["comparison_passed"]
    assert not td.compare({"A": [old, old]}, {"A": [new]}, policy(expectation))["comparison_passed"]
    for changed in (stub(receiver="npc_other"), stub(address="0x10000002")):
        with pytest.raises(td.InvalidReport, match="identity"):
            td.compare({"A": [old]}, {"A": [changed]}, policy(
                change("diagnostic_remap", before=list(old), after=list(changed))))


def test_order_is_explicit_for_deterministic_tests_and_counts_always_matter():
    a, b = ("Warning", "first"), ("Warning", "second")
    before, after = {"A": [a, b, a]}, {"A": [b, a, a]}
    assert td.compare(before, after, policy())["comparison_passed"]
    assert not td.compare(before, after, policy(ordered=["A"]))["comparison_passed"]
    expected = change("diagnostics", before=[list(a), list(b), list(a)],
                      after=[list(b), list(a), list(a)])
    assert td.compare(before, after, policy(expected, ordered=["A"]))["comparison_passed"]
    assert not td.compare(before, {"A": [a, b]}, policy())["comparison_passed"]


def test_expectations_are_not_wildcards_and_cannot_be_unused():
    a, b = ("Warning", "a"), ("Warning", "b")
    expected = change("diagnostics", before=[["Warning", "a", 1]], after=[["Warning", "b", 1]])
    assert td.compare({"A": [a]}, {"A": [b]}, policy(expected))["comparison_passed"]
    with pytest.raises(td.InvalidReport, match="exactly match"):
        td.compare({"A": [a]}, {"A": [b, b]}, policy(expected))
    with pytest.raises(td.InvalidReport, match="unused"):
        td.compare({"A": [a]}, {"A": [a]}, policy(expected))
    with pytest.raises(td.InvalidReport, match="exact tests"):
        td.compare({"A": [a]}, {"A": [a]}, policy(ordered=["*"]))


def write_run(tmp_path, **overrides):
    paths = {}
    for suite in td.SUITES:
        path = tmp_path / (suite + ".json")
        report = {"failed": 0, "notRun": 0, "inProcess": 0,
                  "succeeded": 1, "succeededWithWarnings": 0,
                  "tests": [{"fullTestPath": suite + ".Example", "state": "Success", "entries": []}]}
        report.update(overrides)
        path.write_text(json.dumps(report), encoding="utf-8")
        paths[suite] = str(path)
    descriptor = tmp_path / "run.json"
    descriptor.write_text(json.dumps({"suites": paths, "provenance": {"synthetic": True}}), encoding="utf-8")
    return descriptor


@pytest.mark.parametrize("bad", [{"failed": 1}, {"notRun": 1}, {"inProcess": 1},
                                {"tests": []}, {"succeeded": 2}])
def test_failed_incomplete_empty_and_inconsistent_reports_cannot_pass(tmp_path, bad):
    with pytest.raises(td.InvalidReport):
        td.load_run(write_run(tmp_path, **bad))


def test_all_suites_required_and_hashes_recorded(tmp_path):
    path = write_run(tmp_path)
    tests, meta = td.load_run(path)
    assert len(tests) == 3 and set(meta["reports"]) == set(td.SUITES)
    assert all(len(report["sha256"]) == 64 for report in meta["reports"].values())
    run = json.loads(path.read_text())
    del run["suites"][td.SUITES[0]]
    path.write_text(json.dumps(run))
    with pytest.raises(td.InvalidReport, match="three suites"):
        td.load_run(path)


def test_warning_order_survives_report_parsing(tmp_path):
    path = write_run(tmp_path)
    report_path = tmp_path / (td.SUITES[0] + ".json")
    report = json.loads(report_path.read_text())
    report["tests"][0]["entries"] = [
        {"event": {"type": "Warning", "message": message}, "timestamp": timestamp}
        for message, timestamp in [("b", "ignored"), ("a", "also ignored"), ("b", "different")]]
    report_path.write_text(json.dumps(report))
    tests, _ = td.load_run(path)
    assert tests[td.SUITES[0] + ".Example"] == [("Warning", "b"), ("Warning", "a"), ("Warning", "b")]
