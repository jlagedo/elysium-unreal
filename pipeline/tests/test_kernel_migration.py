"""Story 5 identities and factory preflight. No retail listing is embedded in these tests."""
from __future__ import annotations

import copy
import json
import sys
from pathlib import Path

import pytest

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "research/tooling/ghidra/driver"))

import kernel_factory_map as kf  # noqa: E402
import kernel_migration as km  # noqa: E402


def test_declaring_classes_separate_sibling_offsets():
    cop = km.FieldIdentity("vampire.dll", "CNPC_VCop", 0x6664, "field_a")
    sibling = km.FieldIdentity("vampire.dll", "CNPC_VBach", 0x6664, "field_a")
    assert len({cop: "cop storage", sibling: "boss storage"}) == 2
    with pytest.raises(km.InvalidManifest, match="declaring class"):
        km.FieldIdentity("vampire.dll", "", 0x6664, "field_a")


def test_shared_bodies_and_branch_slots_do_not_collapse_contracts():
    # Synthetic address, deliberately shared; branch-specific slot 617 is not a global identity.
    keys = [km.BodyIdentity("vampire.dll", "20000000", family, 617, signature, receiver)
            for family, signature, receiver in [
                ("CNPCMaker", "void MakeNPC()", "CNPCMaker"),
                ("CNPCMaker", "void MakeNPC()", "CNPCMaker_Zombie"),
                ("CNPC_VBaseBoss", "int BossMethod()", "CNPC_VMingXiao")]]
    assert len(set(keys)) == 3
    with pytest.raises(km.InvalidManifest, match="signature"):
        km.BodyIdentity("vampire.dll", "20000000", "CAI_BaseNPC", 583, "", "CCineNPC")


def test_authored_scope_separates_liveness_factories_and_deferred_classes():
    manifest, classes, factories = km.load()
    assert manifest["phase"] == 0
    assert len(manifest["deferred_classes"]) == 10
    by_class = {r["retail_class"]: r for r in classes}
    assert by_class["CAI_TestHull"]["liveness"] == "live"
    assert not any(r["retail_class"] == "CAI_TestHull" for r in factories)
    assert by_class["CAI_BaseHumanoid"]["liveness"] == "dead-census-retained"
    assert sum(r["current_registry"] == "shared-npc" for r in factories) == 15
    ordinary = [r for r in factories if by_class[r["retail_class"]]["step"] == "2"]
    assert len(ordinary) == 45
    assert sum(r["current_registry"] != "shared-npc" for r in ordinary) == 30
    originals = [r for r in factories if r["classname"] not in kf.DIRECTORS
                 and not r["classname"].startswith("npc_maker")]
    assert len(originals) == 68
    assert sum(r["retail_class"] != r["prior_census_class"] for r in originals) == 9
    aliases = {r["classname"]: r["retail_class"] for r in factories}
    assert aliases["npc_VMercurio"] == aliases["npc_VProneDialog"] == "CNPC_ProneDialog"
    assert aliases["npc_VDialogPedestrian"] == aliases["npc_VPedestrian"] == "CNPC_VPedestrian"


def test_manifest_refuses_alias_ambiguity_missing_owner_and_bad_vtable():
    _, classes, factories = km.load()
    cases = []
    duplicate = copy.deepcopy(factories)
    duplicate[1]["classname"] = duplicate[0]["classname"].upper()
    cases.append(duplicate)
    for key, value in [("retail_class", "Missing"), ("final_vtable", "00000000"),
                       ("factory_listing_sha256", ""), ("current_registry", "guess")]:
        edited = copy.deepcopy(factories)
        edited[0][key] = value
        cases.append(edited)
    for rows in cases:
        with pytest.raises(km.InvalidManifest):
            km.validate(classes, rows)


def test_manifest_requires_every_pin_and_exact_deferred_set(tmp_path):
    directory = REPO / km.STORY
    for name in ("manifest.json", "classes.tsv", "factories.tsv"):
        (tmp_path / name).write_bytes((directory / name).read_bytes())
    manifest = json.loads((tmp_path / "manifest.json").read_text(encoding="utf-8-sig"))
    del manifest["inputs"]["listing"]
    (tmp_path / "manifest.json").write_text(json.dumps(manifest), encoding="utf-8")
    with pytest.raises(km.InvalidManifest, match="input pin"):
        km.load(tmp_path)
    manifest["inputs"]["listing"] = {}
    manifest["deferred_classes"].pop()
    (tmp_path / "manifest.json").write_text(json.dumps(manifest), encoding="utf-8")
    with pytest.raises(km.InvalidManifest, match="deferred"):
        km.load(tmp_path)


def test_receiver_alias_does_not_survive_word_load_or_partial_register_write():
    registers = {"ECX": 0}
    kf.move(kf.Instruction(1, "MOV", "ESI,ECX"), registers)
    kf.move(kf.Instruction(2, "LEA", "EDI,[ESI + 0x10]"), registers)
    assert registers == {"ECX": 0, "ESI": 0, "EDI": 16}
    kf.move(kf.Instruction(3, "MOV", "ECX,dword ptr [ESI]"), registers)
    assert "ECX" not in registers
    kf.move(kf.Instruction(4, "MOV", "SI,0x1"), registers)
    assert "ESI" not in registers


class SyntheticCorpus:
    """A made-up instruction fixture, not bytes/decompilation copied from the game."""
    tables = {0x30000000: "Base", 0x30001000: "Derived"}
    image = type("Image", (), {"read_cstring_va": lambda self, address: "npc_fixture"})()

    def __init__(self, inline: bool = False, wrong_receiver: bool = False):
        self.ctor = make_function("20001000", [
            ("MOV", "ESI,ECX"), ("MOV", "dword ptr [ESI],0x30000000"),
            ("MOV", "EAX,ESI"), ("RET", "")])
        instructions = [
            ("PUSH", "ESI"), ("PUSH", "0x2000"), ("CALL", "0x100aa720"),
            ("TEST", "EAX,EAX"), ("JZ", "0x20000080"),
            ("MOV", "ECX,EAX"), ("CALL", "0x20001000"), ("MOV", "ESI,EAX")]
        if inline:
            instructions += [("MOV", "dword ptr [ESI],0x30001000")]
        instructions += [("PUSH", "0x40000000"), ("MOV", "ECX,EBX" if wrong_receiver else "ECX,ESI"),
                         ("MOV", "EDX,dword ptr [ESI]"), ("CALL", "dword ptr [EDX + 0x1a8]"),
                         ("MOV", "EAX,ESI"), ("POP", "ESI"), ("RET", "")]
        self.fac = make_function("20000000", instructions)
        null_arm = make_function("20000080", [
            ("XOR", "ESI,ESI"), ("PUSH", "0x40000000"), ("MOV", "ECX,ESI"),
            ("MOV", "EDX,dword ptr [ESI]"), ("CALL", "dword ptr [EDX + 0x1a8]"),
            ("MOV", "EAX,ESI"), ("POP", "ESI"), ("RET", "")])
        self.fac.instructions += null_arm.instructions

    def resolve(self, address):
        if address == kf.ALLOCATOR:
            return make_function(address, [("RET", "")]), []
        return {"20000000": self.fac, "20001000": self.ctor}[address], []


def make_function(address, instructions):
    ins = [kf.Instruction(int(address, 16) + i, op, args)
           for i, (op, args) in enumerate(instructions)]
    return kf.Function(address, 256, False, ins, repr(instructions))


@pytest.mark.parametrize("inline,expected", [(False, "Base"), (True, "Derived")])
def test_factory_final_write_wins_on_the_allocated_receiver(inline, expected):
    result = kf.factory(SyntheticCorpus(inline=inline), "20000000", "npc_fixture")
    assert result["final"]["class"] == expected
    assert result["constructor"]["final"]["class"] == "Base"


def test_wrong_receiver_and_conditional_final_write_fail_closed():
    with pytest.raises(kf.Unresolved, match="allocated receiver"):
        kf.factory(SyntheticCorpus(wrong_receiver=True), "20000000", "npc_fixture")
    corpus = SyntheticCorpus()
    corpus.ctor = make_function("20001000", [
        ("MOV", "ESI,ECX"), ("JNZ", "0x20001003"),
        ("MOV", "dword ptr [ESI],0x30000000"), ("MOV", "EAX,ESI"), ("RET", "")])
    with pytest.raises(kf.Unresolved, match="branch crosses"):
        kf.constructor(corpus, "20001000")


def test_clobbered_vtable_register_and_partial_primary_store_are_not_evidence():
    corpus = SyntheticCorpus()
    call_index = next(i for i, ins in enumerate(corpus.fac.instructions)
                      if ins.op == "CALL" and "dword ptr" in ins.args)
    corpus.fac.instructions.insert(call_index, kf.Instruction(0x20000020, "XOR", "EDX,EDX"))
    with pytest.raises(kf.Unresolved, match="allocated receiver"):
        kf.factory(corpus, "20000000", "npc_fixture")
    corpus = SyntheticCorpus()
    corpus.ctor.instructions[1] = kf.Instruction(0x20001001, "MOV", "byte ptr [ESI],0x30000000")
    with pytest.raises(kf.Unresolved, match="primary vtable"):
        kf.constructor(corpus, "20001000")


def test_thunk_cycles_and_non_jump_thunks_are_rejected():
    corpus = object.__new__(kf.Corpus)
    function = make_function("20000000", [("JMP", "0x20000000")])
    function.thunk = True
    corpus.function = lambda address: function
    with pytest.raises(kf.Unresolved, match="cyclic"):
        corpus.resolve("20000000")
    function.instructions = [kf.Instruction(0x20000000, "CALL", "0x20000010")]
    with pytest.raises(kf.Unresolved, match="single direct-jump"):
        corpus.resolve("20000000")


def test_additional_factory_answers_are_not_coalesced_even_if_the_class_is_the_same():
    result = kf.factory(SyntheticCorpus(), "20000000", "npc_fixture")
    reviewed = {"classname": "npc_fixture", **km.factory_projection(result)}
    km.check_observations([reviewed], {"npc_fixture": [result]})
    with pytest.raises(km.InvalidManifest, match="multiple"):
        km.check_observations([reviewed], {"npc_fixture": [result, result]})
    with pytest.raises(km.InvalidManifest, match="coverage"):
        km.check_observations([reviewed], {})


def test_pinned_factory_replay(monkeypatch):
    # Real inputs remain outside the repository. Explicitly load the machine roots for this
    # oracle test; no synthetic fallback may accidentally skip a configured corpus.
    env = REPO / ".elysium.local.env"
    if not env.is_file():
        pytest.skip("local retail corpus is not configured")
    for line in env.read_text(encoding="utf-8-sig").splitlines():
        key, separator, value = line.partition("=")
        if separator and key in {"ELYSIUM_WORK_ROOT", "ELYSIUM_VTMB_ROOT"}:
            monkeypatch.setenv(key, value.strip())
    manifest, _, rows = km.load()
    km.check_factories(manifest, rows)
