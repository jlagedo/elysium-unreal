"""The kernel ledger's derivations, on hand-built inputs; and its regression oracles on the real corpus.

`kernel_ledger` reads the whole-body corpus, which is derived from the user's own binary and
cannot be fixtured. What it *derives* is pure — the READ/WRITE classification of a decompiled
site, the strongly connected components, the layering, the citation scanner — and those run
here on strings and dictionaries. The oracle tests at the bottom assert facts recovered by hand
during story 25a against the generated tables, and skip when the corpus is not on the machine.
"""

from __future__ import annotations

import os
import sys
import tempfile
from pathlib import Path

import pytest

# `kernel_ledger` imports `corpus`, which resolves the work root as it loads; an existing
# directory is all the import needs, and nothing here reads it unless the corpus is present.
os.environ.setdefault("ELYSIUM_WORK_ROOT", tempfile.gettempdir())

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "research" / "tooling" / "ghidra" / "driver"))

import kernel_ledger as kl  # noqa: E402

NAMES = {"m_IdealSchedule": 0x5C3C, "m_pHintNode": 0x5DDC, "m_afMemory": 0x5BC0}


def test_scan_classifies_typed_and_untyped_sites():
    code = """
    this->m_IdealSchedule = 3;
    *(undefined1 *)((int)this + 0x63e0) = 1;
    if (this->m_pHintNode != 0) { x = *(int *)((int)this + 0x5c38); }
    this->m_afMemory = this->m_afMemory | 0x40;
    *(undefined4 *)&this->field_0x1b40 = 0x1af1;
    memcpy((void *)((int)this + 0x65a8),param_2,0x4c);
    if (this->m_IdealSchedule == 4) { }
    """
    reads, writes = kl.Ledger._scan(code, NAMES)
    assert writes == {0x5C3C, 0x63E0, 0x5BC0, 0x1B40, 0x65A8}
    assert 0x5DDC in reads and 0x5C38 in reads
    # read-modify-write: in both
    assert 0x5BC0 in reads and 0x5BC0 in writes
    # `==` is a comparison, not an assignment
    assert 0x5C3C in reads


def test_scan_param1_receiver_only_when_asked():
    code = "*(undefined4 *)(param_1 + 0x5c48) = 0; x = *(int *)(param_1 + 0x98);"
    reads, writes = kl.Ledger._scan(code, {})
    assert reads == set() and writes == set()
    reads, writes = kl.Ledger._scan(code, {}, kl.PARAM1_OFFSET_RE)
    assert writes == {0x5C48} and reads == {0x98}


def test_tarjan_finds_cycles_and_keeps_singletons():
    succ = {"a": {"b"}, "b": {"c"}, "c": {"a"}, "d": {"a"}, "e": set()}
    comps = kl._tarjan(sorted(succ), succ)
    assert ["a", "b", "c"] in comps
    assert ["d"] in comps and ["e"] in comps
    assert len(comps) == 3


def test_declared_member_reads_the_next_declaration():
    lines = [
        "\t// `m_bNoAlertState` (+0x65f6), authored per NPC.",
        "\t// It skips the TROIKA layer's own damage.",
        "\tbool bNoAlertState = false;",
    ]
    assert kl.Ledger._declared_member(lines, 0) == "bNoAlertState"
    assert kl.Ledger._declared_member(["\t// +0x1234", "\tvirtual void Foo();"], 0) == ""


def test_citation_regexes_match_the_two_spellings():
    line = "// `EnterGrappleState` `0x10329760` (slot 379) writes `+0x5ce4` and +0x14b8 bit 8"
    assert [m.group(1) for m in kl.ADDRESS_RE.finditer(line)] == ["10329760"]
    assert [int(m.group(1), 16) for m in kl.OFFSET_RE.finditer(line)] == [0x5CE4, 0x14B8]
    assert kl.SEAM_RE.search("// SEAM: retail's third arm") is not None
    assert kl.SEAM_RE.search("// CHOSEN, NOT RECOVERED — the position") is not None


# --- against the real corpus -----------------------------------------------------------------

def _corpus_present() -> bool:
    try:
        return (kl.corpus._corpus_dir() / "corpus.sqlite").is_file()
    except Exception:  # noqa: BLE001 -- no work root on this machine
        return False


@pytest.fixture(scope="module")
def ledger():
    if not _corpus_present():
        pytest.skip("the Ghidra corpus is not on this machine")
    return kl.build(kl.MODULE, kl.DEFAULT_DEPTH, REPO)


def test_clear_schedule_has_twelve_callers(ledger):
    # `ClearSchedule 0x10280d30`, reached through thunk `0x10006a8c` (story 25a).
    callers = ledger.all_callers["10280d30"]
    assert callers == {
        "10084260", "101a81a0", "101a8840", "101a95d0", "10265820", "10273390", "1027be60",
        "10288780", "102ae8e0", "102b5c00", "102c6ff0", "103692c0",
    }


def test_slot_420_is_npcinit(ledger):
    bodies = ledger.slot_bodies[420]
    assert bodies["CAI_BaseNPC"] == "10273390"
    assert bodies["CAI_BaseNPCTroika"] == "1029a0b0"
    assert bodies["CNPC_VCamera"] == "103692c0"


def test_slot_379_troika_override(ledger):
    assert ledger.slot_bodies[379]["CAI_BaseNPCTroika"] == "102b5c00"
    assert ledger.slot_bodies[379]["CAI_BaseNPC"] == "1026cdc0"


def test_save_position_walk_flag(ledger):
    # `m_fSavePositionWalk +0x63e0`: read by `GetSchedule 0x102ae920`, written by the spawner's
    # helper `0x102ae8e0` (outside the closure: an untyped touch) and reset by NPCInit/TaskFail.
    assert ledger.fields[0x63E0]["name"] == "m_fSavePositionWalk"
    assert 0x63E0 in ledger.functions["102ae920"].reads
    assert 0x63E0 in ledger.functions["1029a0b0"].writes
    assert "102ae8e0" in ledger.other_touches[0x63E0]


def test_ideal_schedule_name_and_clear_schedule_writes(ledger):
    assert ledger.fields[0x5C3C]["name"] == "m_IdealSchedule"
    clear = ledger.functions["10280d30"]
    assert {0x5C38, 0x5C3C, 0x5C40, 0x5C44, 0x5C48, 0x5C4C} <= clear.writes
    assert clear.guessed  # `__fastcall` body: the receiver is `param_1`


def test_queued_burn_producer_is_a_species_offset(ledger):
    # `+0x65a8` is not a Troika datamap field; the corpus sees Troika's grapple entry and
    # `RunTask` touch it through `this`.
    assert 0x65A8 not in ledger.fields
    touchers = ledger.functions["102b5c00"].reads | ledger.functions["102b5c00"].writes
    assert 0x65A8 in touchers


def test_family_and_layers(ledger):
    assert len(ledger.family) == 77
    assert "CAI_BaseNPCTroika" in ledger.family and "CNPC_VCamera" in ledger.family
    assert ledger.bases.get("CAI_BaseNPCTroika") == "CAI_BaseNPC"
    assert len(ledger.layers) > 1
    # every closure function sits in exactly one layer
    assert sum(len(c) for layer in ledger.layers for c in layer) == len(ledger.closure)


def test_check_mode_matches_committed_tables(ledger):
    out = REPO / "docs" / "vtmb" / "npc-kernel"
    if not (out / "functions.md").is_file():
        pytest.skip("the tables are not committed yet")
    assert kl.emit(ledger.render(), out, check=True) == 0
