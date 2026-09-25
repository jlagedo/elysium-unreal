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
TESTS = "Source/ElysiumUE/Private/Tests/ElysiumNpcKernelXTests.cpp"


def tree(root: Path, text: str, tests: str = "") -> Path:
    path = root / BODY
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")
    if tests:
        (root / TESTS).parent.mkdir(parents=True, exist_ok=True)
        (root / TESTS).write_text(tests, encoding="utf-8")
    return root


BEFORE = """
// The crow's own arm.
bool FElysiumNpc::CrowThing(int32 Value)
{
\treturn Value > 0;
}

void FElysiumNpc::Live()
{
}

void FElysiumNpc::Over(int32 A)
{
}

void FElysiumNpc::Over(float B)
{
}

static const FRow GRows[] = {
\t{ TEXT("CNPC_Crow"), 1 },
\t{ TEXT("CNPC_VCop"), 2 },   // the live row
};
"""
AFTER = """
void FElysiumNpc::Live()
{
}

void FElysiumNpc::Over(int32 A)
{
}

static const FRow GRows[] = {
\t{ TEXT("CNPC_VCop"), 2 },   // the live row
};
"""
TESTS_BEFORE = """
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrowTest, "Elysium.Substrate.X.Crow", F)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCopTest, "Elysium.Substrate.X.Cop", F)
"""
TESTS_AFTER = """
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCopTest, "Elysium.Substrate.X.Cop", F)
"""


def row(kind, fingerprint, disposition="deleted", packet="1d", file=BODY):
    return {"packet": packet, "kind": kind, "file": file, "symbol": "", "fingerprint": fingerprint,
            "retail_addresses": "", "disposition": disposition, "evidence": "synthetic"}


COMPLETE = [row("definition", "FElysiumNpc::CrowThing(int32 Value)"),
            row("definition", "FElysiumNpc::Over(float B)"),
            row("row", '{ TEXT("CNPC_Crow"), 1 },'),
            row("definition", "FElysiumNpc::Live()", "kept-live"),
            row("row", '{ TEXT("CNPC_VCop"), 2 },', "kept-live"),
            row("comment", "// The crow's own arm.", "edited"),
            row("test", "Elysium.Substrate.X.Crow", file=TESTS),
            row("test", "Elysium.Substrate.X.Cop", "edited", file=TESTS)]


def test_deleted_kept_and_edited_rows_are_held_to_both_trees(tmp_path):
    before = tree(tmp_path / "a", BEFORE, TESTS_BEFORE)
    after = tree(tmp_path / "b", AFTER, TESTS_AFTER)
    counts = s1.check_rows(COMPLETE, before, after)
    assert counts["definition:deleted"] == 2 and counts["row:kept-live"] == 1
    assert s1.check_complete(COMPLETE, before, after) == {"removed_definitions": 2, "removed_tests": 1}


def test_the_record_must_cover_every_removed_definition_and_test(tmp_path):
    before = tree(tmp_path / "a", BEFORE, TESTS_BEFORE)
    after = tree(tmp_path / "b", AFTER, TESTS_AFTER)
    # One overload removed while its sibling survives: the record must name that exact overload.
    partial = [r for r in COMPLETE if r["fingerprint"] != "FElysiumNpc::Over(float B)"]
    with pytest.raises(km.InvalidManifest, match=r"Over\(float B\)"):
        s1.check_complete(partial, before, after)
    untested = [r for r in COMPLETE if r["fingerprint"] != "Elysium.Substrate.X.Crow"]
    with pytest.raises(km.InvalidManifest, match="X.Crow"):
        s1.check_complete(untested, before, after)


def test_a_deleted_row_still_present_or_never_present_is_refused(tmp_path):
    before, after = tree(tmp_path / "a", BEFORE), tree(tmp_path / "b", BEFORE)
    with pytest.raises(km.InvalidManifest, match="still present"):
        s1.check_rows([row("definition", "FElysiumNpc::CrowThing(int32 Value)")], before, after)
    with pytest.raises(km.InvalidManifest, match="does not hold"):
        s1.check_rows([row("row", '{ TEXT("CNPC_Bat"), 3 },')], before, after)


def test_kept_rows_match_exactly_not_by_prefix(tmp_path):
    before, after = tree(tmp_path / "a", BEFORE), tree(tmp_path / "b", AFTER)
    with pytest.raises(km.InvalidManifest, match="kept row is gone"):
        s1.check_rows([row("definition", "FElysiumNpc::CrowThing(int32 Value)", "kept-contract")],
                      before, after)
    # `Over()` with no parameters names no definition: an empty list is not a wildcard.
    with pytest.raises(km.InvalidManifest, match="does not hold"):
        s1.check_rows([row("definition", "FElysiumNpc::Over()", "kept-live")], before, after)
    # A kept row's fingerprint that is only a prefix of a longer code line does not match.
    with pytest.raises(km.InvalidManifest, match="does not hold"):
        s1.check_rows([row("row", '{ TEXT("CNPC_VCop")', "kept-live")], before, after)


def test_an_edited_comment_must_actually_change(tmp_path):
    before, after = tree(tmp_path / "a", BEFORE), tree(tmp_path / "b", BEFORE)
    with pytest.raises(km.InvalidManifest, match="still reads"):
        s1.check_rows([row("comment", "// The crow's own arm.", "edited")], before, after)
    path = tmp_path / "d.tsv"
    path.write_text(f"{HEADER}\n" + "\t".join(row("row", "x", "edited").values()) + "\n",
                    encoding="utf-8")
    with pytest.raises(km.InvalidManifest, match="only tests and comments"):
        s1.read_deletions(path)


def body(verdict, symbol, signature, scope=False, live=()):
    return {"verdict": verdict, "step1_scope": scope, "live_receivers": list(live),
            "definitions": [{"symbol": symbol, "signature": signature}]}


def test_only_the_no_instance_rows_are_unguarded(tmp_path):
    after = tree(tmp_path / "b", AFTER)
    crow = "bool FElysiumNpc::CrowThing(int32 Value)"
    inventory = {"bodies": [body("dead", "FElysiumNpc::CrowThing", crow, scope=True),
                            body("rule", "FElysiumNpc::Live", "void FElysiumNpc::Live()",
                                 live=["CNPC_VCop"])]}
    assert s1.check_live_definitions(inventory, after) == 1
    # Dead for a reason other than "no instance" is still guarded.
    inventory["bodies"][0]["step1_scope"] = False
    with pytest.raises(km.InvalidManifest, match="CrowThing"):
        s1.check_live_definitions(inventory, after)
    # A no-instance row with a live receiver is guarded: a shared body is not a dead body.
    inventory["bodies"][0].update(step1_scope=True, live_receivers=["CNPC_VWerewolf"])
    with pytest.raises(km.InvalidManifest, match="CrowThing"):
        s1.check_live_definitions(inventory, after)


def test_the_guard_matches_overloads_by_parameter_list(tmp_path):
    after = tree(tmp_path / "b", AFTER)
    inventory = {"bodies": [body("rule", "FElysiumNpc::Over", "void FElysiumNpc::Over(float B)")]}
    with pytest.raises(km.InvalidManifest, match=r"Over\(float B\)"):
        s1.check_live_definitions(inventory, after)


def test_a_guard_exemption_is_rederived_not_trusted(tmp_path):
    after = tree(tmp_path / "b", AFTER)
    inventory = {"bodies": [body("unsettled", "FElysiumNpc::CrowThing",
                                 "bool FElysiumNpc::CrowThing(int32 Value)")]}
    home = {"FElysiumNpc::CrowThing": {"live_home": "FElysiumNpc::Live"}}
    assert s1.check_live_definitions(inventory, after, home) == 1
    gone = {"FElysiumNpc::CrowThing": {"live_home": "FElysiumNpc::Missing"}}
    with pytest.raises(km.InvalidManifest, match="live home"):
        s1.check_live_definitions(inventory, after, gone)
    callers = {"FElysiumNpc::CrowThing": {"address": "10000000", "retail_callers": ["10000001"]}}
    ledger = lambda address: {"10000001"}
    assert s1.check_live_definitions(inventory, after, callers, {"10000001": "dead"}, ledger) == 1
    # A caller the list omits refuses the exemption, even when every listed caller is dead.
    wider = lambda address: {"10000001", "10000002"}
    with pytest.raises(km.InvalidManifest, match="ledger"):
        s1.check_live_definitions(inventory, after, callers, {"10000001": "dead"}, wider)
    with pytest.raises(km.InvalidManifest, match="neither a dead row nor explained"):
        s1.check_live_definitions(inventory, after, callers, {"10000001": "rule"}, ledger)
    callers["FElysiumNpc::CrowThing"]["caller_notes"] = {"10000001": "only a dead class reaches it"}
    assert s1.check_live_definitions(inventory, after, callers, {"10000001": "rule"}, ledger) == 1


def test_an_overlay_target_may_not_name_a_removed_symbol(tmp_path):
    before, after = tree(tmp_path / "a", BEFORE), tree(tmp_path / "b", AFTER)
    overlay = after / "research/tooling/ghidra/driver/kernel_verdicts.tsv"
    overlay.parent.mkdir(parents=True)
    overlay.write_text("10000000\tdead\t0-4\t-\tgone\n"
                       "10000001\trule\t0-4\thand:FElysiumNpc::Live\tlive\n", encoding="utf-8")
    # One symbol removed: `Over` loses an overload but the name survives.
    assert s1.check_overlay_targets(before, after) == 1
    overlay.write_text("10000000\tdead\t0-4\tFElysiumNpc::CrowThing\tstale\n", encoding="utf-8")
    with pytest.raises(km.InvalidManifest, match="10000000 FElysiumNpc::CrowThing"):
        s1.check_overlay_targets(before, after)


def rules(*addresses):
    return {"live_rule_inventory": [{"module": "vampire.dll", "address": a, "receiver": "CNPC_VVampireBoss",
                                     "slot": 525, "family": "CAI_BaseNPC"} for a in addresses]}


def test_the_rule_identity_changes_only_by_the_listed_delta():
    step0, current = rules("10000000"), rules("10000000", "10000001")
    entry = ["vampire.dll", "10000001", "CNPC_VVampireBoss", 525, "CAI_BaseNPC"]
    assert s1.check_rule_delta(step0, step0, {}) == {"added": 0, "removed": 0}
    with pytest.raises(km.InvalidManifest, match="unlisted additions"):
        s1.check_rule_delta(step0, current, {})
    with pytest.raises(km.InvalidManifest, match="reason"):
        s1.check_rule_delta(step0, current, {"added": [entry]})
    assert s1.check_rule_delta(step0, current, {"added": [entry], "reason": "re-judged"}) == {"added": 1, "removed": 0}
    # An entry the inventory does not show is refused, and a deletion may not remove a rule.
    with pytest.raises(km.InvalidManifest, match="unused entries"):
        s1.check_rule_delta(step0, step0, {"added": [entry], "reason": "re-judged"})
    with pytest.raises(km.InvalidManifest, match="unlisted removals"):
        s1.check_rule_delta(current, step0, {})


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
