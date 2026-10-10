"""Spec 0019 story 6: the ledger tooling for closed rows, on hand-built inputs.

A row is *closed* when its target says the port body is gone: a `dead` row at `-`, a `mechanism`
row at a service word. Four tools read that fact: the overlay loader (the target grammar), the
shape generator (what a closed slot emits), `kernel_lists` (the meter, and the check that a closed
row is no longer cited) and `kernel_shape --unported` (a closed row is not owed). None of these
tests needs the corpus.
"""

from __future__ import annotations

import sys
from pathlib import Path
from types import SimpleNamespace

import pytest

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "research" / "tooling"))
sys.path.insert(0, str(REPO / "research" / "tooling" / "ghidra" / "driver"))

import gen_kernel_shape as gks  # noqa: E402
import kernel_ledger as kl  # noqa: E402
import kernel_lists as kli  # noqa: E402
import kernel_shape as ks  # noqa: E402


def _overlay(tmp_path, *rows: str) -> Path:
    path = tmp_path / kl.VERDICTS_TSV
    path.write_text("\t".join(kl.VERDICT_COLUMNS) + "\n" + "".join(r + "\n" for r in rows),
                    encoding="utf-8")
    return path


# --- 1. the target grammar ----------------------------------------------------------------------


def test_every_service_word_closes_a_mechanism_row(tmp_path):
    rows = [f"1000{index:04x}\tmechanism\t0-4\t{word}\t[0019/1 seam=x] y"
            for index, word in enumerate(kl.SERVICE_TARGETS)]
    table = kl.load_verdicts(_overlay(tmp_path, *rows, "10009000\tdead\t0-4\t-\t[0019/1 dead=x]"))
    assert {v.target for v in table.values()} == {*kl.SERVICE_TARGETS, "-"}
    assert all(kl.closed_target(v.verdict, v.target) for v in table.values())


def test_open_targets_stay_accepted_and_open(tmp_path):
    table = kl.load_verdicts(_overlay(
        tmp_path,
        "10000001\tmechanism\t0-4\thand:FElysiumNpc::Spawn\ts",
        "10000002\tmechanism\t0-4\tFElysiumNpcBase::Touch\ts",
        "10000003\tmechanism\t0-4\tUStruct\ts",                 # a pre-story-6 legacy spelling
        "10000004\tdead\t0-4\thand:FElysiumNpcBase::Slot337\ts",
        "10000005\tdead\t0-4\tVisual/ElysiumNpcBody.cpp:AElysiumNpcBody::Stop\ts",
        "10000006\trule\t0-4\tFElysiumNpcNavigator\ts",
        "10000007\tunsettled\t0-4\t-\ts"))
    assert not any(kl.closed_target(v.verdict, v.target) for v in table.values())


def test_generated_literal_dead_rows_are_closed_for_the_lists_but_keep_their_body(tmp_path):
    # A `default:` / `registry:` dead row has no hand body and no test: closed for the lists and the
    # meter, but its generated literal stays (a `rule` body may read it: slot 327 by 0x1026da90).
    table = kl.load_verdicts(_overlay(
        tmp_path,
        "10000001	dead	0-4	default:1	s",
        "10000002	dead	0-4	registry:337	s"))
    assert all(kl.closed_target(v.verdict, v.target) for v in table.values())
    assert not any(kl.body_closed(v.verdict, v.target) for v in table.values())


@pytest.mark.parametrize("verdict,target", [
    ("mechanism", "Physics"),      # not in the closed set
    ("mechanism", "cmc"),          # the set is spelled exactly
    ("mechanism", "-"),            # a mechanism body gone names its service
    ("dead", "CMC"),               # a dead body gone is `-`
    ("dead", "Nothing"),
    ("dead", ""),                  # no longer implicit: `-` says the body is gone
    ("rule", "-"),                 # only dead / mechanism close
    ("present", "TraceRetail"),
])
def test_a_target_outside_the_grammar_names_the_row(tmp_path, verdict, target):
    with pytest.raises(SystemExit, match="0x1000abcd"):
        kl.load_verdicts(_overlay(tmp_path, f"1000abcd\t{verdict}\t0-4\t{target}\tsome evidence"))


# --- 2. generation ------------------------------------------------------------------------------


def _layer(owner, retail, body, verdict="", target="", override=False, ret_port="int32"):
    row = gks.Slot(slot=77, cls="", method="Probe", ret="int", params="", const=False,
                   tier="walked", words="1", body=body, address=f"0x{body}")
    row.port_name, row.ret_port = "Probe", ret_port
    row.owner, row.retail, row.override = owner, retail, override
    row.verdict, row.verdict_target = verdict, target
    return row


def _merged(*layers):
    row = gks.Slot(slot=77, cls="", method="Probe", ret="int", params="", const=False,
                   tier="walked", words="1", body=layers[-1].body)
    row.port_name, row.ret_port = "Probe", layers[-1].ret_port
    row.verdict, row.verdict_target = layers[-1].verdict, layers[-1].verdict_target
    row.layers = list(layers)
    return row


def _ledger(dispatch=0, species=None, verdicts=None, code=None):
    functions = {a: SimpleNamespace(code=c) for a, c in (code or {}).items()}
    return SimpleNamespace(slot_dispatch_sites={77: dispatch} if dispatch else {},
                           slot_bodies={77: dict(species or {})}, verdicts=dict(verdicts or {}),
                           functions=functions)


def _model(*rows):
    return gks.Model(words=[], slots=list(rows), branch=[], classes=[], overrides=[],
                     reserved=set(), family=set(), meta={})


@pytest.mark.parametrize("verdict,target", [("dead", "-"), ("mechanism", "Chaos")])
def test_an_unreached_closed_slot_emits_nothing(verdict, target):
    row = _merged(_layer("FElysiumNpc", "CAI_BaseNPCTroika", "10000077", verdict, target))
    gks.close_layers(row, _ledger())
    assert row.port_kind == gks.CLOSED and not row.generated and row.layers == []
    assert row.port_name == "" and "no dispatch site" in row.port_why
    text = gks.render_slots_cpp(_model(row), "vampire.dll", "FElysiumNpc")
    assert "Probe" not in text
    assert "closed — " in gks.render_slots_inl(_model(row), "vampire.dll", "FElysiumNpc")


def test_a_dispatched_closed_slot_answers_the_default_and_never_tallies():
    row = _merged(_layer("FElysiumNpc", "CAI_BaseNPCTroika", "10000077", "dead", "-"))
    gks.close_layers(row, _ledger(dispatch=3))
    [layer] = row.layers
    assert row.port_kind == "" and layer.closed and not layer.stubbed and not layer.default
    text = gks.render_slots_cpp(_model(row), "vampire.dll", "FElysiumNpc")
    body = text.split("int32 FElysiumNpc::Probe()")[1].split("\n}\n")[0]
    assert "return {};" in body and "FireKernelSlot" not in body
    assert "EElysiumNpcSlotBody::Closed" in text and "EElysiumNpcSlotBody::Stub" not in text


def test_a_dispatched_closed_constant_body_answers_retails_literal():
    row = _merged(_layer("FElysiumNpc", "CAI_BaseNPCTroika", "10000077", "dead", "-",
                         ret_port="bool"))
    gks.close_layers(row, _ledger(dispatch=1, code={"10000077": "bool f(void)\n{\n  return 1;\n}\n"}))
    assert row.layers[0].default == "1"
    text = gks.render_slots_cpp(_model(row), "vampire.dll", "FElysiumNpc")
    assert "return true;" in text and 'EElysiumNpcSlotBody::Default, TEXT("1"), 1' in text


def test_a_closed_override_is_dropped_and_the_class_inherits():
    base = _layer("FElysiumNpcBase", "CAI_BaseNPC", "10000001", "rule", "hand:FElysiumNpcBase::Probe")
    base.hand = "FElysiumNpcBase::Probe"
    troika = _layer("FElysiumNpc", "CAI_BaseNPCTroika", "10000002", "mechanism", "CMC", override=True)
    row = _merged(base, troika)
    gks.close_layers(row, _ledger())
    assert row.layers == [base] and row.port_kind == ""
    assert "Probe" not in gks.render_slots_inl(_model(row), "vampire.dll", "FElysiumNpc")


def test_a_closed_introducer_stays_declared_while_another_body_needs_it():
    closed = dict(verdict="dead", target="-")
    # A live override on the chain.
    row = _merged(_layer("FElysiumNpcBase", "CAI_BaseNPC", "10000001", **closed),
                  _layer("FElysiumNpc", "CAI_BaseNPCTroika", "10000002", "rule", "FElysiumNpc::Probe",
                         override=True))
    gks.close_layers(row, _ledger())
    assert len(row.layers) == 2 and row.layers[0].closed and not row.layers[0].stubbed
    # A species own body the overlay has not closed; a closed one does not hold the slot.
    for species, held in (({"CNPC_VHuman": "10000009"}, True),
                          ({"CNPC_VHuman": "10000001"}, False)):   # the inherited pointer
        row = _merged(_layer("FElysiumNpc", "CAI_BaseNPCTroika", "10000001", **closed))
        gks.close_layers(row, _ledger(species=species))
        assert (row.port_kind != gks.CLOSED) is held
    row = _merged(_layer("FElysiumNpc", "CAI_BaseNPCTroika", "10000001", **closed))
    gks.close_layers(row, _ledger(species={"CNPC_VHuman": "10000009"},
                                  verdicts={"10000009": kl.Verdict("10000009", "dead", "0-4", "-", "x")}))
    assert row.port_kind == gks.CLOSED
    # A class below the NPC line overriding the slot.
    row = _merged(_layer("FElysiumEntity", "CBaseEntity", "10000001", **closed))
    row.port_kind = gks.OVERRIDDEN_BELOW
    gks.close_layers(row, _ledger())
    assert row.layers and row.port_kind == gks.OVERRIDDEN_BELOW


def test_open_rows_emit_as_before():
    # A dead or mechanism row that still names its port body keeps today's emission: the stub.
    for verdict, target in (("dead", "FElysiumNpc::Probe"), ("mechanism", "FElysiumNpcBase::Probe"),
                            ("rule", "FElysiumNpc::Probe")):
        row = _merged(_layer("FElysiumNpc", "CAI_BaseNPCTroika", "10000077", verdict, target))
        gks.close_layers(row, _ledger())
        assert row.layers[0].stubbed and not row.layers[0].closed


def test_a_chain_hand_body_over_a_closed_row_is_a_failure():
    layer = _layer("FElysiumEntity", "CBaseEntity", "10000077", "dead", "-")
    layer.hand = "FElysiumEntity::Probe"          # what `CHAIN_HAND` sets
    with pytest.raises(SystemExit, match="CHAIN_HAND"):
        gks.close_layers(_merged(layer), _ledger(dispatch=1))


# --- 3. the meter -------------------------------------------------------------------------------


class _Cited(kl.Ledger):
    """The verdict half of a ledger, with no corpus behind it."""

    def __init__(self, verdicts):
        self.verdicts = verdicts


def _port(cites):
    """`kernel_lists.port_cites`'s answer, by hand: address -> its port lines."""
    return {a: [kl.Citation(p, n, t) for p, n, t in rows] for a, rows in cites.items()}


def _v(addr, verdict, target):
    return kl.Verdict(addr, verdict, "0-4", target, "[0019/1 x=y] z")


def test_a_closed_row_still_cited_fails_the_check_and_names_its_cites():
    verdicts = {"10000001": _v("10000001", "dead", "-"),
                "10000002": _v("10000002", "mechanism", "TraceRetail"),
                "10000003": _v("10000003", "dead", "FElysiumNpc::Probe"),      # open: cites fine
                "10000004": _v("10000004", "dead", "-"),
                "10000005": _v("10000005", "mechanism", "hand:FElysiumNpc::X")}
    tests = "Source/ElysiumUE/Private/Tests/ElysiumNpcKernelTests.cpp"
    port = {
        "10000001": [("Source/ElysiumUE/Private/Substrate/ElysiumNpc.cpp", 12, "Probe(); // 0x10000001")],
        "10000002": [(tests, 40, "TestEqual(Npc.Probe(), 1); // 0x10000002")],
        "10000003": [("Source/ElysiumUE/Private/Substrate/ElysiumNpc.cpp", 9, "// 0x10000003")],
        # A test comment recording the removal is a deprecation note; the same words in a
        # non-test source are still a port site.
        "10000004": [(tests, 7, "// 0x10000004: dead, deleted in 0019/6")],
        "10000005": [("Source/ElysiumUE/Private/Substrate/ElysiumNpc.cpp", 3, "// 0x10000005")],
    }
    problems = kli.closed_problems(_Cited(verdicts), _port(port))
    assert [p.split(":")[0] for p in problems] == ["0x10000001", "0x10000002"]
    assert "ElysiumNpc.cpp:12" in problems[0] and "ElysiumNpcKernelTests.cpp:40" in problems[1]
    port["10000004"].append(("Source/ElysiumUE/Private/Substrate/ElysiumNpc.cpp", 5,
                             "// 0x10000004: dead, deleted in 0019/6"))
    assert [p.split(":")[0] for p in kli.closed_problems(_Cited(verdicts), _port(port))] == [
        "0x10000001", "0x10000002", "0x10000004"]


def test_the_pinned_rows_are_exempt_and_the_meter_counts_closed_over_total():
    pinned = sorted(kli.CITED_WHEN_CLOSED)[0]
    verdicts = {pinned: _v(pinned, "dead", "-"),
                "10000002": _v("10000002", "dead", "FElysiumNpc::Probe"),
                "10000003": _v("10000003", "mechanism", "CRT:operator delete"),
                "10000004": _v("10000004", "mechanism", "UClass"),            # legacy: open
                "10000005": _v("10000005", "rule", "FElysiumNpc::Rule")}
    ledger = _Cited(verdicts)
    port = _port({pinned: [("Source/ElysiumUE/Private/Substrate/X.cpp", 1, "x")]})
    assert kli.closed_problems(ledger, port) == []
    assert kli.closed_meter(ledger) == {"dead": (1, 2), "mechanism": (1, 2)}


# --- 4. the unported list -----------------------------------------------------------------------


def test_a_closed_row_is_never_unported(tmp_path):
    driver = tmp_path / "research" / "tooling" / "ghidra" / "driver"
    driver.mkdir(parents=True)
    (driver / "kernel_classes.tsv").write_text(
        "retail_class\tretail_base\tvtable\tport_class\tport_base\tstep\tliveness\tevidence\n"
        "CNPC_VHuman\tCAI_BaseNPCTroika\t0\tFElysiumNpcHuman\tFElysiumNpc\t5\tlive\tx\n",
        encoding="utf-8")
    (tmp_path / "Source" / "ElysiumUE" / "Private" / "Substrate").mkdir(parents=True)

    def layer(body, verdict, target):
        row = _layer("FElysiumNpc", "CAI_BaseNPCTroika", body, verdict, target)
        return _merged(row)

    slots = [layer("10000001", "mechanism", "FElysiumNpc::Probe"),    # open: still a stub
             layer("10000002", "mechanism", "UNavigationSystem"),     # closed
             layer("10000003", "dead", "-")]                          # dead never enters
    for index, row in enumerate(slots):
        row.slot = row.layers[0].slot = 100 + index
    overrides = [gks.OverrideRow("CNPC_VHuman", 100, "0x10000011", "A", "mechanism"),
                 gks.OverrideRow("CNPC_VHuman", 101, "0x10000012", "B", "mechanism", closed=True),
                 gks.OverrideRow("CNPC_VHuman", 102, "0x10000013", "C", "dead", closed=True)]
    model = gks.Model(words=[], slots=slots, branch=[], classes=[], overrides=overrides,
                      reserved=set(), family=set(), meta={})
    ported, unported = ks.override_rows(tmp_path, model)
    assert ported == []
    assert [(r[1], r[2], r[4]) for r in unported] == [("100", "0x10000001", "stub"),
                                                      ("100", "0x10000011", "no-override")]
