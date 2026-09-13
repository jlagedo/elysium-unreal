"""The kernel shape's derivations on hand-built inputs, and its oracles on the real corpus.

`kernel_shape` joins the datamap records (`datamap_layout`), the decompiled bodies and the
listing. What it derives is pure — a flattened layout, the kind of an untyped member access, how a
call site uses a return value, an arity-checked parameter list — and those run here on
dictionaries and strings. The oracle tests at the bottom assert facts about the real corpus and
skip when it is not on the machine.
"""

from __future__ import annotations

import collections
import os
import sys
import tempfile
from pathlib import Path

import pytest

os.environ.setdefault("ELYSIUM_WORK_ROOT", tempfile.gettempdir())

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "research" / "tooling" / "ghidra" / "driver"))

import datamap_layout as dl  # noqa: E402
import kernel_shape as ks  # noqa: E402


def _record(name, typ, off, count=1, flags=("SAVE",), embedded=None, ops=None, key=None):
    return {"name": name, "typeName": typ, "offset": off, "count": count, "flagNames": list(flags),
            "embedded": embedded, "ops": ops, "external": key}


RECORDS = dl.Records({
    "Base": {"base": None, "records": [
        _record("m_iHealth", "int", 0x10),
        _record("m_OnDeath", "custom", 0x14, flags=("SAVE", "KEY", "OUTPUT"), key="OnDeath"),
        _record("InputKill", "void", 0, flags=("INPUT",)),
    ]},
    "Timer_t": {"base": None, "records": [
        _record("m_flNext", "time", 0x0), _record("m_vecWhere", "vector", 0x4)]},
    "Derived": {"base": "Base", "records": [
        _record("m_Timer", "embedded", 0x30, embedded="Timer_t"),
        _record("m_Seen", "custom", 0x40, ops="_08__UUnreachableEnt_t__V__CUtlMemory__UUnreachableEnt_t__V__CUtlVector____CUtlVectorDataOps"),
        _record("m_flFrame", "float", 0x58, count=3),
        _record("m_pSenses", "embedded", 0x64, flags=("SAVE", "PTR"), embedded="Timer_t"),
    ]},
}, Path("fixture.json"))


def test_flatten_orders_bases_first_and_skips_inputs():
    members = dl.flatten(RECORDS, "Derived")
    names = [m.name for m in members]
    assert "InputKill" not in names
    assert names[:2] == ["m_iHealth", "m_OnDeath"]
    by_name = {m.name: m for m in members}
    assert by_name["m_iHealth"].owner == "Base" and by_name["m_Timer"].owner == "Derived"


def test_widths_types_and_embedded_expansion():
    by_name = {m.name: m for m in dl.flatten(RECORDS, "Derived")}
    assert by_name["m_OnDeath"].width == dl.OUTPUT_WIDTH
    assert by_name["m_OnDeath"].source_type == "COutputEvent"
    # an embedded record's extent is its class's records: time (4) then a vector (12)
    assert by_name["m_Timer"].width == 0x10 and by_name["m_Timer"].source_type == "Timer_t"
    assert by_name["m_Timer.m_vecWhere"].off == 0x34 and by_name["m_Timer.m_vecWhere"].parent == "m_Timer"
    # a custom record without a fixed width is bounded by the next member
    assert by_name["m_Seen"].width == 0x18 and by_name["m_Seen"].source_type == "CUtlVector<UnreachableEnt_t>"
    assert by_name["m_flFrame"].width == 12
    # a pointer-held embedded object is one word and is not expanded in place
    assert by_name["m_pSenses"].width == 4 and by_name["m_pSenses"].source_type == "Timer_t*"
    assert "m_pSenses.m_flNext" not in by_name
    assert by_name["m_Timer.m_flNext"].source_type == "float"


def test_interior_names():
    members = dl.flatten(RECORDS, "Derived")
    by_name = {m.name: m for m in members}
    assert dl.interior_name(by_name["m_OnDeath"], 0x14 + 0x14)[0] == "m_OnDeath.m_ActionList"
    assert dl.interior_name(by_name["m_Seen"], 0x40 + 0xC)[:2] == ("m_Seen.m_Size", "int")
    assert dl.interior_name(by_name["m_flFrame"], 0x58 + 8)[0] == "m_flFrame[2]"
    vector = next(m for m in members if m.name == "m_Timer.m_vecWhere")
    assert dl.interior_name(vector, 0x34 + 8)[:2] == ("m_Timer.m_vecWhere.z", "float")
    assert dl.covering(members, 0x36)[-1].name == "m_Timer.m_vecWhere"


def test_type_codes_follow_vtmbs_enum():
    # VtMB has no FIELD_QUATERNION: 4 is an integer, 5 a boolean, 12 a handle, 15 a time
    assert [dl.type_from_code(c) for c in (1, 4, 5, 12, 15)] == ["float", "int", "bool", "ehandle", "time"]
    assert dl.type_from_code(99) == ""


def test_scan_body_reads_kinds_writes_and_consumers():
    code = """
    *(undefined4 *)((int)this + 0x5c3c) = 0;
    fVar1 = *(float *)((int)this + 0x5d5c);
    uVar2 = *(undefined2 *)(*(int *)((int)this + 0x5b90) + 0x2e);
    (**(code **)(**(int **)((int)this + 0x5d88) + 0x14))();
    cVar3 = this->field_0x5b55;
    FUN_102c66b0((int)this + 0x5ca8,param_1);
    *(int *)((int)this + iVar4 * 4 + 0x5c5c) = 1;
    *(undefined ***)&this->field_0x19b0 = &vftable_CNPC_VWerewolf_at6576;
    """
    touches = collections.defaultdict(set)
    writes = set()
    consumers = {}
    strides = {}
    for off, kind, _, write, callee, stride in ks.scan_body(code):
        touches[off].add(kind)
        if write:
            writes.add(off)
        if callee:
            consumers[off] = callee
        if stride:
            strides[off] = stride
    assert touches[0x5C3C] == {"w4"} and 0x5C3C in writes
    assert touches[0x5D5C] == {"float"} and 0x5D5C not in writes
    assert "ptr" in touches[0x5B90]
    assert "objptr" in touches[0x5D88]
    assert touches[0x5B55] == {"w1"}
    assert touches[0x5CA8] == {"addr"} and consumers[0x5CA8] == "FUN_102c66b0"
    assert strides[0x5C5C] == 4
    assert "vftable" in touches[0x19B0] and 0x19B0 in writes


def test_constructor_receiver_through_param_1():
    code = """
    undefined4 * __fastcall FUN_1028d230(undefined4 *param_1)
    {
      FUN_1027c300(param_1);
      *param_1 = &vftable_CAI_BaseNPCTroika;
      param_1[0x66c] = &vftable_CAI_BaseNPCTroika_at6576;
      param_1[0x1983] = 0;
      *(undefined1 *)((int)param_1 + 0x65f5) = 1;
      thunk_FUN_102b9780((int)param_1 + 0x6659);
    """
    touches = {(off, kind) for off, kind, *_ in ks.scan_body(code, "param_1", 4)}
    assert (0, "vftable") in touches and (0x19B0, "vftable") in touches
    assert (0x660C, "w4") in touches
    assert (0x65F5, "w1") in touches and (0x6659, "addr") in touches
    shape = ks.Shape.__new__(ks.Shape)
    shape.family = {"CAI_BaseNPCTroika"}
    shape.troika_chain = {"CAI_BaseNPC", "CAI_BaseNPCTroika"}
    shape.base_of = {"CAI_BaseNPCTroika": "CAI_BaseNPC"}
    fn = ks.kl.Function("1028d230", "FUN_1028d230", "Global", 0, False, "", code)
    assert shape.constructed(fn) == ("CAI_BaseNPCTroika", "param_1", 4)


def test_inferred_type_needs_agreement():
    ev = ks.Evidence()
    ev.kinds.update({"float": 3, "addr": 1})
    assert ev.inferred_type() == "float"
    ev.kinds.update({"w4": 1})
    assert ev.inferred_type() == ""
    ptr = ks.Evidence()
    ptr.kinds.update({"w4": 5, "ptr": 2})
    assert ptr.inferred_type() == "pointer"


def test_call_site_use_after_a_virtual_call():
    assert ks._use_after(["1028a275  TEST EAX,EAX"]) == "int"
    assert ks._use_after(["1028a275  TEST AL,AL"]) == "bool"
    assert ks._use_after(["1028a275  FSTP dword ptr [ESP + 0x10]"]) == "float"
    assert ks._use_after(["1028a275  MOV ECX,dword ptr [EAX + 0x4]"]) == "ptr"
    assert ks._use_after(["1028a275  MOV EAX,dword ptr [ESI]"]) == "unused"
    assert ks._use_after(["1028a275  ADD ESP,0x8", "1028a278  TEST AL,AL"]) == "bool"


def test_params_are_held_to_the_images_word_count():
    seen = [collections.Counter({"void*": 3}), collections.Counter({"float": 2, "int": 1})]
    assert ks.Shape._params_from(seen, 2) == ["void*", "float"]
    assert ks.Shape._params_from(seen, 3) == ["void*", "float", "int"]
    assert ks.Shape._params_from([collections.Counter({"double": 1})], 1) is None
    assert ks.Shape._params_from([], 0) == []


def test_return_prefers_call_sites_then_prototypes():
    assert ks.Shape._return_from(collections.Counter({"bool": 5, "unused": 1}), collections.Counter()) == "bool"
    assert ks.Shape._return_from(collections.Counter({"unused": 4}), collections.Counter({"int": 3})) == "void"
    assert ks.Shape._return_from(collections.Counter(), collections.Counter({"float": 2})) == "float"
    assert ks.Shape._return_from(collections.Counter(), collections.Counter({"int": 1, "void": 1})) is None


def test_overlay_rows_parse(tmp_path, monkeypatch):
    fields = tmp_path / "kernel_fields.tsv"
    fields.write_text("# header\nvampire.dll\tCAI_BaseNPCTroika\t0x5c38\tm_pSchedule\tCAI_Schedule*\t1\t"
                      "doc\tClearSchedule 0x10280d30 zeroes it\n", encoding="utf-8")
    sigs = tmp_path / "kernel_signatures.tsv"
    sigs.write_text("vampire.dll\t432\tRunAI\tvoid\tbool\t\twalked\tpops one word\n"
                    "vampire.dll\t617\tMakeNPC\tvoid\tbool\t\twalked\tbranch\tCNPCMaker\n", encoding="utf-8")
    monkeypatch.setattr(ks, "FIELDS_OVERLAY", fields)
    monkeypatch.setattr(ks, "SIGNATURES_OVERLAY", sigs)
    shape = ks.Shape.__new__(ks.Shape)
    shape.module = "vampire.dll"
    shape.troika_chain = {"CAI_BaseNPC", "CAI_BaseNPCTroika"}
    shape.fields_overlay, shape.signatures_overlay = collections.defaultdict(list), {}
    shape.load_overlays()
    assert shape.fields_overlay[("CAI_BaseNPCTroika", 0x5C38)][0]["name"] == "m_pSchedule"
    assert shape.signatures_overlay[(432, "")]["params"] == "bool"
    # a per-branch virtual past a base's table is keyed by the class that introduces it
    assert shape.signatures_overlay[(617, "CNPCMaker")]["method"] == "MakeNPC"


# --- against the real corpus -----------------------------------------------------------------

def _corpus_present() -> bool:
    try:
        from elysium_pipeline.paths import research_root
        return (ks.kl.corpus._corpus_dir() / "corpus.sqlite").is_file() and \
            dl.load(research_root(), ks.kl.MODULE) is not None
    except Exception:  # noqa: BLE001 -- no work root on this machine
        return False


@pytest.fixture(scope="module")
def built():
    if not _corpus_present():
        pytest.skip("the Ghidra corpus or the datamap records are not on this machine")
    return ks.build(ks.kl.MODULE, ks.kl.DEFAULT_DEPTH, REPO)


def test_npc_state_and_schedule_state_types(built):
    shape, rows, _ = built
    by_off = {(r.table, r.off): r for r in rows if r.tier == "datamap" and "." not in r.member}
    assert by_off[(ks.TROIKA, 0x5CC0)].member == "m_NPCState"
    assert by_off[(ks.TROIKA, 0x5CC0)].type == "int"
    assert by_off[(ks.TROIKA, 0x5C40)].type == "AIScheduleState_t"
    assert by_off[(ks.TROIKA, 0x5C40)].size == 0x14
    assert by_off[(ks.TROIKA, 0x5E0C)].type == "COutputEvent"


def test_secondary_vtable_word_is_seen(built):
    shape, _, _ = built
    assert "vftable" in shape.evidence[ks.TROIKA][0x19B0].kinds


def test_start_task_signature_is_the_sdks(built):
    _, _, sigs = built
    row = next(s for s in sigs if s["slot"] == 442)
    assert row["words"] == [1]
    assert row["tier"] in ("sdk", "walked")


def test_check_mode_matches_committed_tables(built):
    out = REPO / "docs" / "vtmb" / "npc-kernel"
    if not (out / "layout.md").is_file():
        pytest.skip("the shape tables are not committed yet")
    shape, rows, sigs = built
    assert ks.kl.emit(ks.render(shape, rows, sigs), out, check=True) == 0
