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


def test_parse_band_reads_a_range_and_a_single_layer():
    assert kl.parse_band("0-9") == (0, 9)
    assert kl.parse_band("19-99") == (19, 99)
    assert kl.parse_band("7") == (7, 7)
    with pytest.raises(SystemExit):
        kl.parse_band("low-high")


def _verdict_file(tmp_path, body: str) -> Path:
    path = tmp_path / kl.VERDICTS_TSV
    path.write_text("# a comment the loader skips\n"
                    + "\t".join(kl.VERDICT_COLUMNS) + "\n" + body, encoding="utf-8")
    return path


def test_load_verdicts_reads_the_overlay(tmp_path):
    rows = kl.load_verdicts(_verdict_file(tmp_path, (
        "0x10026530\trule\t0-4\tdefault:void\tslot 21's whole body is `return;`\n"
        "\n"
        "10430964\tmechanism\t0-4\tCRT:operator delete\tthe scalar deleting destructor\n"
        "1027db30\tdead\t5-9\t-\tno closure caller and no slot\n")))
    assert set(rows) == {"10026530", "10430964", "1027db30"}
    assert rows["10026530"].verdict == "rule"
    assert rows["10026530"].target == "default:void"      # the `0x` prefix is stripped from the key
    assert rows["1027db30"].band == "5-9"


def test_load_verdicts_refuses_a_malformed_row(tmp_path):
    # An unknown word, a duplicate address, a missing target, a missing reason and a short row are
    # each fatal: the overlay is a record, and a silently dropped row would empty a verdict.
    for body in ("10026530\tported\t0-4\tx\ty\n",
                 "10026530\trule\t0-4\tx\ty\n10026530\trule\t0-4\tx\ty\n",
                 "10026530\trule\t0-4\t\ty\n",
                 "10026530\tdead\t0-4\t-\t\n",
                 "10026530\trule\t0-4\n"):
        with pytest.raises(SystemExit):
            kl.load_verdicts(_verdict_file(tmp_path, body))


def test_load_verdicts_requires_the_header(tmp_path):
    path = tmp_path / kl.VERDICTS_TSV
    path.write_text("10026530\trule\t0-4\tx\ty\n", encoding="utf-8")
    with pytest.raises(SystemExit):
        kl.load_verdicts(path)


def test_citation_regexes_match_the_two_spellings():
    line = "// `EnterGrappleState` `0x10329760` (slot 379) writes `+0x5ce4` and +0x14b8 bit 8"
    assert [m.group(1) for m in kl.ADDRESS_RE.finditer(line)] == ["10329760"]
    assert [int(m.group(1), 16) for m in kl.OFFSET_RE.finditer(line)] == [0x5CE4, 0x14B8]
    assert kl.SEAM_RE.search("// SEAM: retail's third arm") is not None
    assert kl.SEAM_RE.search("// CHOSEN, NOT RECOVERED — the position") is not None


# --- the checklist, on a hand-built band ------------------------------------------------------


class _FixtureLedger(kl.Ledger):
    """A ledger with no corpus behind it: the checklist's own derivations, on four functions.

    `core()` is the one method that asks the database (for the offset `CBaseCombatCharacter`'s
    layout ends at), so the fixture states the core set instead of deriving it. Everything the
    checklist renders below that is arithmetic over the tables set here.
    """

    def __init__(self, functions, layers, verdicts, port=(), oracle=()):
        self.module = "vampire.dll"
        self.depth = 6
        self.meta = {"sha256": "c0ffee" * 8, "dumped_at": 0}
        self.functions = functions
        self.layer_of = layers
        self.verdicts = verdicts
        self.checklists = ("0-9",)
        self.closure = {a: 0 for a in functions}
        self.closure_edges = set()
        self.all_callers = {}
        self.family = {"CAI_BaseNPC"}
        self.helpers = ["CAISound"]
        self.interior = {}
        self.port_addr = {a: [kl.Citation("ElysiumNpc.cpp", 1, False, "")] for a in port}
        self.oracle_addr = {a: [kl.Citation("docs/vtmb/npc-ai/senses.md", 1, False, "")]
                            for a in oracle}

    def core(self):
        return sorted(self.functions)


def _band_fixture(verdicts):
    def fn(addr, name, size=8, ns="CAI_BaseNPC"):
        return kl.Function(addr, name, ns, size, False, "", "")

    functions = {
        "10000001": fn("10000001", "Empty", 3),
        "10000002": fn("10000002", "Getter"),
        "10000003": fn("10000003", "Rule", 200),
        "10000004": fn("10000004", "High", 40),
    }
    layers = {"10000001": 0, "10000002": 3, "10000003": 7, "10000004": 12}
    return _FixtureLedger(functions, layers, verdicts, port=["10000002"], oracle=["10000004"])


def test_band_stats_counts_a_verdict_as_a_citation():
    verdicts = {"10000001": kl.Verdict("10000001", "rule", "0-4", "default:void", "slot 21")}
    ledger = _band_fixture(verdicts)

    low = ledger.band_stats(0, 4)
    assert low["core"] == 2                 # layers 0 and 3
    assert low["rule"] == 1 and low["empty"] == 1
    assert low["port"] == 1 and low["oracle"] == 0
    # 10000001 is verdicted and 10000002 is port-cited, so nothing in the band is uncited.
    assert low["neither"] == 0

    high = ledger.band_stats(5, 9)
    assert high["core"] == 1 and high["empty"] == 1
    assert high["neither"] == 1             # unverdicted and cited by nobody

    # The band bounds are the `order.md` layer, so layer 12 is in neither of the two.
    assert ledger.band_stats(10, 18)["core"] == 1


def test_band_stats_counts_unsettled_apart_from_the_four_verdicts():
    ledger = _band_fixture(
        {"10000003": kl.Verdict("10000003", "unsettled", "5-9", "-", "the jump table is lost")})
    stats = ledger.band_stats(5, 9)
    assert stats["unsettled"] == 1
    assert stats["verdicted"] == 0          # a recorded failure is not a verdict
    assert stats["rule"] == stats["mechanism"] == stats["present"] == stats["dead"] == 0
    assert stats["empty"] == 0              # but the row is not empty either
    assert stats["neither"] == 0            # and it counts as read


def test_render_checklist_joins_the_overlay_and_survives_regeneration():
    verdicts = {
        "10000001": kl.Verdict("10000001", "rule", "0-4", "default:void", "slot 21 does nothing"),
        "10000002": kl.Verdict("10000002", "present", "0-4", "FElysiumNpc::Sleeping",
                               "+0x5bb4 | the bound word"),
    }
    text = _band_fixture(verdicts)._render_checklist("0-9")
    assert kl.GENERATED_BANNER in text
    assert "| Core functions, layers 0–9 | 3 |" in text
    assert "`0x10000001` | CAI_BaseNPC::Empty | 3 | 0 | — | — | — | 0d/0v/0c | `rule` | default:void" in text
    # A pipe inside an overlay cell would end the table row early.
    assert "FElysiumNpc::Sleeping | +0x5bb4 ¦ the bound word |" in text
    # A row with no overlay entry renders an empty verdict rather than being dropped.
    assert "`0x10000003` | CAI_BaseNPC::Rule | 200 | 7 | — | — | — | 0d/0v/0c |  |  |  |" in text
    # Out of band.
    assert "0x10000004" not in text


def test_bodies_packs_split_on_rows_and_on_size():
    ledger = _band_fixture({})
    ledger.functions["10000003"].code = "x" * (kl.PACK_BUDGET + 10)
    packs = ledger.bodies("0-9", per_pack=120)
    # The third row alone passes the character budget, so the fourth would open a new pack; with
    # only three rows in band the split is after it.
    assert list(packs) == ["band-0-9-pack-01.md"]
    assert "## 0x10000001  CAI_BaseNPC::Empty" in packs["band-0-9-pack-01.md"]

    packs = ledger.bodies("0-9", per_pack=1)
    assert list(packs) == [f"band-0-9-pack-{n:02d}.md" for n in (1, 2, 3)]


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


def test_vtable_tail_past_the_dump_bound(ledger):
    # `DumpVtables.java` used to stop at 600; Troika's primary table holds 617 and
    # CNPC_VTzimisce's 628. The tail entries are JMP thunks the dump resolves to their bodies.
    assert ledger.slot_count["CAI_BaseNPCTroika"] == 617
    assert ledger.slot_count["CNPC_VTzimisce"] == 628
    assert ledger.slot_bodies[614]["CAI_BaseNPCTroika"] == "102c23f0"
    assert ledger.slot_bodies[616]["CAI_BaseNPCTroika"] == "102ad110"
    assert ("CAI_BaseNPCTroika", 614) in ledger.functions["102c23f0"].slots


def test_field_types_come_from_the_datamap(ledger):
    if not ledger.field_types:
        pytest.skip("the datamap records are not on this machine")
    assert ledger.field_types[0x5CC0] == "int"          # m_NPCState: VtMB code 4 is FIELD_INTEGER
    assert ledger.field_types[0x5C40] == "AIScheduleState_t"
    assert ledger.field_types[0x159C] == "int"          # an image record the builder replay lacks


def test_check_mode_matches_committed_tables(ledger):
    out = REPO / "docs" / "vtmb" / "npc-kernel"
    if not (out / "functions.md").is_file():
        pytest.skip("the tables are not committed yet")
    assert kl.emit(ledger.render(), out, check=True) == 0
