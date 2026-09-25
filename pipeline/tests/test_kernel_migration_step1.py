"""Story 5 step 1's deletion record and live-definition guard, on synthetic trees."""
from __future__ import annotations

import sys
from pathlib import Path

import pytest

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "research/tooling/ghidra/driver"))
sys.path.insert(0, str(REPO / "research/tooling"))

import kernel_migration as km  # noqa: E402
import kernel_migration_step1 as s1  # noqa: E402

HEADER = "\t".join(s1.DELETION_COLUMNS)
BODY = "Source/ElysiumUE/Private/Substrate/ElysiumNpcKernelX.cpp"


def tree(root: Path, text: str) -> Path:
    path = root / BODY
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")
    return root


BEFORE = """
bool FElysiumNpc::CrowThing(int32 Value)
{
\treturn Value > 0;
}

void FElysiumNpc::Live()
{
}

static const FRow GRows[] = {
\t{ TEXT("CNPC_Crow"), 1 },
\t{ TEXT("CNPC_VCop"), 2 },
};
"""
AFTER = """
void FElysiumNpc::Live()
{
}

static const FRow GRows[] = {
\t{ TEXT("CNPC_VCop"), 2 },
};
"""


def row(kind, fingerprint, disposition="deleted", packet="1d"):
    return {"packet": packet, "kind": kind, "file": BODY, "symbol": "", "fingerprint": fingerprint,
            "retail_addresses": "", "disposition": disposition, "evidence": "synthetic"}


def test_deleted_and_kept_rows_are_held_to_both_trees(tmp_path):
    before, after = tree(tmp_path / "a", BEFORE), tree(tmp_path / "b", AFTER)
    rows = [row("definition", "FElysiumNpc::CrowThing(int32 Value)"),
            row("row", '{ TEXT("CNPC_Crow"), 1 },'),
            row("definition", "FElysiumNpc::Live()", "kept-live"),
            row("row", '{ TEXT("CNPC_VCop"), 2 },', "kept-live")]
    counts = s1.check_rows(rows, before, after)
    assert counts == {"definition:deleted": 1, "definition:kept-live": 1, "row:deleted": 1,
                      "row:kept-live": 1}


def test_a_deleted_row_still_present_or_never_present_is_refused(tmp_path):
    before, after = tree(tmp_path / "a", BEFORE), tree(tmp_path / "b", BEFORE)
    with pytest.raises(km.InvalidManifest, match="still present"):
        s1.check_rows([row("definition", "FElysiumNpc::CrowThing(int32 Value)")], before, after)
    with pytest.raises(km.InvalidManifest, match="does not hold"):
        s1.check_rows([row("row", '{ TEXT("CNPC_Bat"), 3 },')], before, after)


def test_a_kept_row_that_disappeared_is_refused(tmp_path):
    before, after = tree(tmp_path / "a", BEFORE), tree(tmp_path / "b", AFTER)
    with pytest.raises(km.InvalidManifest, match="kept row is gone"):
        s1.check_rows([row("definition", "FElysiumNpc::CrowThing(int32 Value)", "kept-contract")],
                      before, after)


def test_live_definitions_are_guarded_whatever_the_record_says(tmp_path):
    after = tree(tmp_path / "b", AFTER)
    definition = lambda symbol: {"symbol": symbol}
    inventory = {"bodies": [
        {"verdict": "dead", "live_receivers": [], "definitions": [definition("FElysiumNpc::CrowThing")]},
        {"verdict": "rule", "live_receivers": ["CNPC_VCop"], "definitions": [definition("FElysiumNpc::Live")]}]}
    assert s1.check_live_definitions(inventory, after) == 1
    # A dead verdict with a live receiver is still guarded: a shared body is not a dead body.
    inventory["bodies"][0]["live_receivers"] = ["CNPC_VWerewolf"]
    with pytest.raises(km.InvalidManifest, match="CrowThing"):
        s1.check_live_definitions(inventory, after)


def test_a_guard_exemption_is_reverified_not_trusted(tmp_path):
    after = tree(tmp_path / "b", AFTER)
    inventory = {"bodies": [{"verdict": "unsettled", "live_receivers": [],
                             "definitions": [{"symbol": "FElysiumNpc::CrowThing"}]}]}
    with pytest.raises(km.InvalidManifest, match="CrowThing"):
        s1.check_live_definitions(inventory, after)
    home = {"FElysiumNpc::CrowThing": {"live_home": "FElysiumNpc::Live"}}
    assert s1.check_live_definitions(inventory, after, home) == 1
    gone = {"FElysiumNpc::CrowThing": {"live_home": "FElysiumNpc::Missing"}}
    with pytest.raises(km.InvalidManifest, match="live home"):
        s1.check_live_definitions(inventory, after, gone)
    callers = {"FElysiumNpc::CrowThing": {"retail_callers": ["10000001"]}}
    assert s1.check_live_definitions(inventory, after, callers, {"10000001": "dead"}) == 1
    with pytest.raises(km.InvalidManifest, match="retail caller"):
        s1.check_live_definitions(inventory, after, callers, {"10000001": "rule"})


def test_record_refuses_unknown_kinds_and_duplicates(tmp_path):
    path = tmp_path / "deletions.tsv"
    good = "\t".join(row("row", "x").values())
    path.write_text(f"{HEADER}\n{good}\n{good}\n", encoding="utf-8")
    with pytest.raises(km.InvalidManifest, match="duplicate"):
        s1.read_deletions(path)
    path.write_text(f"{HEADER}\n" + good.replace("\trow\t", "\tguess\t") + "\n", encoding="utf-8")
    with pytest.raises(km.InvalidManifest, match="unknown"):
        s1.read_deletions(path)


def test_dead_census_class_rows_must_survive(tmp_path):
    shape = tmp_path / s1.SHAPE
    shape.parent.mkdir(parents=True)
    shape.write_text('\t\t{ TEXT("CNPC_Crow"), TEXT("CAI_BaseNPC"), }\n', encoding="utf-8")
    classes = [{"retail_class": "CNPC_Crow", "liveness": "dead-census-retained"},
               {"retail_class": "CNPC_VCop", "liveness": "live"}]
    assert s1.check_census(classes, tmp_path) == 1
    classes.append({"retail_class": "CNPC_VTest", "liveness": "dead-census-retained"})
    with pytest.raises(km.InvalidManifest, match="CNPC_VTest"):
        s1.check_census(classes, tmp_path)


def test_historical_source_needs_a_full_commit_hash(tmp_path):
    with pytest.raises(km.InvalidManifest, match="full hash"):
        km.historical_source("aa1c3c86", tmp_path)
