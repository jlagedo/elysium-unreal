"""The kernel-shape census generator's derivations, and its oracles on the real corpus.

`gen_kernel_shape` transcribes the committed kernel ledger into the three files the runtime
asserts its own shape against. What it *derives* is pure — the retail-to-port type lowering, the
top-level/interior split, the story band a layer falls in, the row digest, the reserved-name scan
over the port's entity chain — and those run here on strings and small fixtures. The oracle tests
at the bottom build the real model and assert the counts story 29b landed, skipping when the
corpus is not on the machine.
"""

from __future__ import annotations

import sys
from pathlib import Path

import pytest

import _kernel_build

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "research" / "tooling"))
sys.path.insert(0, str(REPO / "research" / "tooling" / "ghidra" / "driver"))

import gen_kernel_shape as gks  # noqa: E402

FAMILY = {"CAI_BaseNPCTroika", "CNPC_VHuman", "CNPC_VWerewolf"}


def test_lower_type_keeps_what_the_port_carries():
    assert gks.lower_type("void", FAMILY) == ("void", "")
    assert gks.lower_type("bool", FAMILY) == ("bool", "")
    assert gks.lower_type("int", FAMILY) == ("int32", "")
    assert gks.lower_type("unsigned int", FAMILY) == ("uint32", "")
    assert gks.lower_type("float", FAMILY) == ("float", "")
    assert gks.lower_type("const char*", FAMILY) == ("const TCHAR*", "")
    assert gks.lower_type("const Vector&", FAMILY) == ("const FVector&", "")
    assert gks.lower_type("string_t", FAMILY) == ("FName", "")
    assert gks.lower_type("NPC_STATE", FAMILY) == ("EElysiumNpcState", "")


def test_lower_type_collapses_every_entity_to_the_ports_one_leaf():
    # This port stands one leaf for the whole `npc_V*` family, so every entity pointer is the same
    # port type; a by-value or by-reference entity says so in the note.
    assert gks.lower_type("CBaseEntity*", FAMILY) == ("FElysiumEntity*", "")
    assert gks.lower_type("CNPC_VWerewolf*", FAMILY) == ("FElysiumEntity*", "")
    assert gks.lower_type("CBasePlayer*", FAMILY) == ("FElysiumEntity*", "")
    port, note = gks.lower_type("CBaseCombatCharacter&", FAMILY)
    assert port == "FElysiumEntity*" and "by reference" in note


def test_lower_type_lowers_an_unported_type_to_one_word_and_says_what_it_was():
    # Arity is never changed: a pointer or a reference the port has no counterpart for is one word,
    # and the retail spelling travels in the note.
    assert gks.lower_type("CAI_Schedule*", FAMILY) == ("void*", "`CAI_Schedule*`")
    assert gks.lower_type("trace_t*", FAMILY) == ("void*", "`trace_t*`")
    assert gks.lower_type("const CTakeDamageInfo&", FAMILY) == ("void*", "`const CTakeDamageInfo&`")
    assert gks.lower_type("Class_T", FAMILY) == ("int32", "`Class_T`")


def test_story_bands_follow_the_specs_build_order():
    assert gks.story_for(0) == "29c"
    assert gks.story_for(9) == "29c"
    assert gks.story_for(10) == "29d"
    assert gks.story_for(18) == "29d"
    assert gks.story_for(19) == "29e"
    assert gks.story_for(26) == "29e"


def test_reserved_names_reads_methods_and_data_members_and_stops_where_told(tmp_path):
    header = tmp_path / "Fake.h"
    header.write_text(
        "class FElysiumThing\n"
        "{\n"
        "public:\n"
        "\tvirtual void DoTheThing();\n"
        "\tFVector EyeLookTarget = FVector::ZeroVector;\n"
        "\tint32 Counter;\n"
        "\tbool Inline() const { int32 Local = 0; return Local == 0; }\n"
        "};\n"
        "class FElysiumPlayer final : public FElysiumThing\n"
        "{\n"
        "\tvoid PlayerOnly();\n"
        "};\n",
        encoding="utf-8")
    saved = gks.PORT_CHAIN_HEADERS
    try:
        gks.PORT_CHAIN_HEADERS = ((header.name, "class FElysiumPlayer final"),)
        names = gks.reserved_names(tmp_path)
    finally:
        gks.PORT_CHAIN_HEADERS = saved
    # A data member is reserved as hard as a method: a member function declared on the leaf hides a
    # base's data member of the same name, silently.
    assert {"DoTheThing", "EyeLookTarget", "Counter", "Inline"} <= names
    # Past the stop marker nothing is read, and a local inside an inline body is not a member.
    assert "PlayerOnly" not in names
    assert "Local" not in names


def test_comment_and_row_wrapping_hold_the_hundred_column_rule():
    prose = "word " * 60
    for line in gks._comment(prose, "\t"):
        assert len(line.expandtabs(4)) <= 100
        assert line.lstrip().startswith("//")
    cells = [f'TEXT("cell number {index} with a long spelling")' for index in range(8)]
    rows = gks._row(cells)
    assert rows[0].lstrip().startswith("{")
    assert rows[-1].rstrip().endswith("},")
    for line in rows:
        assert len(line.expandtabs(4)) <= 100
    # Every cell survives the wrap.
    joined = " ".join(part.strip() for part in rows)
    for cell in cells:
        assert cell in joined


def test_digest_changes_when_a_row_changes():
    word = gks.Word(table="CAI_BaseNPCTroika", offset=0x5CC0, member="m_NPCState", type="int",
                    field_type="FIELD_INTEGER", layer="CAI_BaseNPC", tier="datamap", size=4,
                    count=1)
    model = gks.Model(words=[word], slots=[], branch=[], classes=[], overrides=[], reserved=set(),
                      family=set(), meta={})
    before = gks.digest_of(model)
    assert before == gks.digest_of(model)
    word.member = "m_IdealNPCState"
    assert gks.digest_of(model) != before


def test_slot_declaration_is_the_retail_one_and_the_port_one_is_lowered():
    row = gks.Slot(slot=440, cls="", method="TranslateSchedule", ret="int", params="int",
                   const=False, tier="walked", words="1")
    assert row.declaration == "int TranslateSchedule(int)"
    row.port_name = "TranslateSchedule"
    row.ret_port = "int32"
    row.params_port = ["int32"]
    assert row.port_declaration() == "int32 TranslateSchedule(int32)"
    assert row.port_declaration("FElysiumNpc::") == "int32 FElysiumNpc::TranslateSchedule(int32)"


# --- the verdict overlay's effect on the emission (story 29c) ----------------------------------


def _slot(ret="int", port="int32", default="", hand="", params="", params_port=()):
    row = gks.Slot(slot=33, cls="", method="Slot33", ret=ret, params=params, const=False,
                   tier="walked", words="1", address="0x10026690")
    row.port_name = "Slot33"
    row.ret_port = port
    row.params_port = list(params_port)
    row.verdict = "rule" if (default or hand) else ""
    row.default = default
    row.hand = hand
    # A Troika-introduced slot: the one emission row is the Troika layer's own declaration.
    row.layers = [row]
    return row


def test_a_constant_body_lowers_to_the_ports_return_type():
    # The statement the definition carries, and the integer the probe asserts, per return type.
    assert gks.default_body(_slot(port="void", default="void")) == ("", "", 0)
    assert gks.default_body(_slot(port="bool", default="0"))[0] == "return false;"
    assert gks.default_body(_slot(port="bool", default="1"))[0] == "return true;"
    assert gks.default_body(_slot(port="void*", default="0"))[0] == "return nullptr;"
    # A retail `return 0xffffffff;` out of an `int` body is -1, and the probe asserts -1.
    statement, _, value = gks.default_body(_slot(port="int32", default="0xffffffff"))
    assert statement == "return static_cast<int32>(0xffffffff);" and value == -1
    # The same bits out of an `unsigned int` body are not.
    assert gks.default_body(_slot(port="uint32", default="0xffffffff"))[2] == 0xFFFFFFFF
    assert gks.default_body(_slot(port="float", default="0.0"))[0] == "return static_cast<float>(0.0);"


def test_a_constant_body_the_generator_cannot_lower_is_a_failure_not_a_guess():
    for row in (_slot(port="void", default="0"),        # void body with a value
                _slot(port="int32", default="void"),    # value body with no value
                _slot(port="bool", default="7"),        # a bool that is not 0 or 1
                _slot(port="void*", default="1"),       # a pointer constant
                _slot(port="float", default="0.5"),     # fractional: the probe compares integers
                _slot(port="FVector", default="0")):    # a type with no lowering
        with pytest.raises(SystemExit):
            gks.default_body(row)


def test_the_probe_declares_a_local_for_every_argument():
    # `const TCHAR*{}` does not parse and a reference needs an lvalue, so every parameter gets a
    # named local whatever its type.
    locals_, args = gks._probe_arguments(
        _slot(params_port=["const TCHAR*", "const FVector&", "int32"]))
    assert locals_ == ["const TCHAR* Arg0{};", "FVector Arg1{};", "int32 Arg2{};"]
    assert args == "Arg0, Arg1, Arg2"


def test_a_verdict_decides_whether_a_slot_is_stubbed():
    assert _slot().stubbed is True
    assert _slot(default="0").stubbed is False
    assert _slot(hand="FElysiumNpc::Slot33").stubbed is False
    # A `hand:` row is declared and not defined here, so the definition it claims is the linker's
    # to find; the emission carries the claim as a comment and no body.
    model = gks.Model(words=[], slots=[_slot(hand="FElysiumNpc::Slot33")], branch=[], classes=[],
                      overrides=[], reserved=set(), family=set(), meta={})
    text = gks.render_slots_cpp(model, "vampire.dll", "FElysiumNpc")
    assert "written by hand in the substrate" in text
    assert "int32 FElysiumNpc::Slot33()" not in text
    assert "FireKernelSlot" not in text.split("namespace ElysiumNpcKernelShape")[1]


def test_a_constant_body_is_emitted_with_its_probe():
    model = gks.Model(words=[], slots=[_slot(default="0xffffffff")], branch=[], classes=[],
                      overrides=[], reserved=set(), family=set(), meta={})
    text = gks.render_slots_cpp(model, "vampire.dll", "FElysiumNpc")
    assert "int32 FElysiumNpc::Slot33()" in text
    assert "return static_cast<int32>(0xffffffff);" in text
    assert "FireKernelSlot(TEXT(\"Slot33\")" not in text
    # The class's slot table carries the row, its probe typed to the class, and the compiler's
    # answer to whether the class itself declares it.
    assert 'EElysiumNpcSlotBody::Default, TEXT("0xffffffff"), -1, false' in text
    assert "[](FElysiumNpc& Receiver) -> int64 { return static_cast<int64>(Receiver.Slot33()); }" in text
    assert "TDeclaredOn<FElysiumNpc, int32()>::Test(&FElysiumNpc::Slot33)" in text
    assert "TArrayView<const TElysiumNpcSlotRow<FElysiumNpc>> NpcSlotRows()" in text
    # A stub row carries no probe: firing it would tally.
    stub = gks.Model(words=[], slots=[_slot()], branch=[], classes=[], overrides=[],
                     reserved=set(), family=set(), meta={})
    stub_text = gks.render_slots_cpp(stub, "vampire.dll", "FElysiumNpc")
    assert "EElysiumNpcSlotBody::Stub" in stub_text and "nullptr }," in stub_text
    # And a class with no generated row still compiles: a zero-length C array does not.
    empty = gks.Model(words=[], slots=[], branch=[], classes=[], overrides=[],
                      reserved=set(), family=set(), meta={})
    empty_text = gks.render_slots_cpp(empty, "vampire.dll", "FElysiumNpc")
    assert "GNpcSlotRows[]" not in empty_text
    assert "return TArrayView<const TElysiumNpcSlotRow<FElysiumNpc>>();" in empty_text


class _Bodies:
    """The one thing `apply_retail_default` asks a ledger: the decompiled body by address."""

    def __init__(self, code):
        self.functions = {addr: gks.kl.Function(addr, "F", "CBaseEntity", 5, False, "", text)
                          for addr, text in code.items()}


def test_a_retail_default_answers_a_stub_and_nothing_else():
    # L0.tooling.default-stubs: a stub whose retail body `RETAIL_DEFAULTS` reads as one constant
    # answers it; a row a verdict already settled keeps the verdict's emission.
    addr = "100266b0"
    ledger = _Bodies({addr: "int __thiscall F(void)\n{\n  return 0;\n}\n"})
    row = _slot()
    row.body, row.address = addr, f"0x{addr}"
    gks.apply_retail_default(row, ledger)
    assert row.default == "0" and "GetHighlightMaterial" in row.default_why and not row.stubbed
    text = gks.render_slots_cpp(gks.Model(words=[], slots=[row], branch=[], classes=[], overrides=[],
                                          reserved=set(), family=set(), meta={}),
                                "vampire.dll", "FElysiumNpc")
    assert "retail default (L0.tooling.default-stubs)" in text and "FireKernelSlot(TEXT(" not in text
    hand = _slot(hand="FElysiumNpc::Slot33")
    hand.body = addr
    gks.apply_retail_default(hand, ledger)
    assert hand.default == "" and hand.default_why == ""
    other = _slot()
    other.body = "10000001"
    gks.apply_retail_default(other, ledger)
    assert other.stubbed


def test_a_retail_default_that_disagrees_with_its_body_is_a_failure():
    addr = "100266b0"
    row = _slot()
    row.body, row.address = addr, f"0x{addr}"
    with pytest.raises(SystemExit, match="RETAIL_DEFAULTS"):
        gks.apply_retail_default(row, _Bodies({addr: "int F(void)\n{\n  return 1;\n}\n"}))


def test_each_class_emits_its_own_typed_slot_table():
    # A chain class's own constant body is probed on a receiver of that class, not through a
    # qualified call on an NPC (plan step 6c).
    row = _slot(default="0")
    row.owner, row.retail = "FElysiumEntity", "CBaseEntity"
    model = gks.Model(words=[], slots=[row], branch=[], classes=[], overrides=[],
                      reserved=set(), family=set(), meta={})
    text = gks.render_slots_cpp(model, "vampire.dll", "FElysiumEntity")
    assert "const TElysiumNpcSlotRow<FElysiumEntity> GEntitySlotRows[] =" in text
    assert 'TEXT("CBaseEntity"), TEXT("Slot33")' in text
    assert "[](FElysiumEntity& Receiver) -> int64" in text
    assert "TArrayView<const TElysiumNpcSlotRow<FElysiumEntity>> EntitySlotRows()" in text
    assert "FElysiumNpc& Npc" not in text and "::Slot33(" not in text.split("GEntitySlotRows")[1]
    assert gks.slot_rows_accessor("FElysiumCombatCharacter") == "CombatCharacterSlotRows"


def test_a_default_matches_its_body_as_a_32_bit_word():
    assert gks.same_word("-1", "0xffffffff")
    assert gks.same_word("0x17", "23")
    assert not gks.same_word("-1", "0xfffffffe")
    assert not gks.same_word("0", "1")


def test_constant_return_reads_only_a_one_statement_body():
    assert gks.constant_return("void f(void)\n{\n  return;\n}\n") == "void"
    assert gks.constant_return("int f(void)\n{\n  return 0x17;\n}\n") == "0x17"
    assert gks.constant_return("int f(void)\n{\n  return -1;\n}\n") == "-1"
    # Anything with a second statement, a global, or a member read is not a value.
    assert gks.constant_return("int f(void)\n{\n  return DAT_1072b360;\n}\n") == ""
    assert gks.constant_return("int f(void)\n{\n  x = 1;\n  return 0;\n}\n") == ""
    assert gks.constant_return("int f(void)\n{\n  return this->m_x;\n}\n") == ""
    assert gks.constant_return("") == ""


# --- against the real corpus (opt in: `pytest -m corpus`) ------------------------------------

@pytest.fixture
def model():
    return _kernel_build.model()   # one build per process, on the shape tests' shape


@pytest.mark.corpus
def test_the_census_carries_the_shape_29b_landed(model):
    troika = [w for w in model.words if w.table == gks.BASE_TABLE]
    assert len(troika) == 756                      # top-level words of the flattened layout
    assert len([w for w in troika if w.npc]) == 388  # the NPC's own, past `CBaseCombatCharacter`
    assert len(model.slots) == gks.TROIKA_SLOTS    # 617, one row per Troika-line virtual
    assert len(model.classes) == 77
    # Every Troika-line slot index, once.
    assert sorted(row.slot for row in model.slots) == list(range(gks.TROIKA_SLOTS))


@pytest.mark.corpus
def test_every_slot_has_a_port_callable_and_no_two_share_a_name(model):
    names: dict[str, int] = {}
    for row in model.slots:
        if row.port_kind == gks.DELETED:
            assert row.verdict == "dead", f"deleted slot {row.slot} is not dead"
            continue   # a dead, uncalled, un-overridden stub deleted in 0019/5 step 6
        if row.port_kind == gks.CLOSED:
            assert row.closed and not row.layers, f"closed slot {row.slot} still emits"
            continue   # every body closed at 0019/6 and nothing reaches it
        assert row.port_name, f"slot {row.slot} has no port callable"
        if row.port_kind == gks.PORT:
            continue   # a mapped slot names an existing method, which many slots may not do twice
        key = (row.port_name, row.ret_port, tuple(row.params_port), row.const)
        assert key not in names, f"slot {row.slot} redeclares slot {names.get(key)}"
        names[key] = row.slot


@pytest.mark.corpus
def test_no_generated_slot_shadows_the_port_chain(model):
    for row in model.slots:
        if row.port_kind != gks.PORT:
            assert row.port_name not in model.reserved, \
                f"slot {row.slot} would shadow `{row.port_name}` in the port's entity chain"


@pytest.mark.corpus
def test_unsettled_rows_carry_over_as_recorded(model):
    # 29b-0 left seven top-level words and seven Troika-line slots unsettled; they land as their
    # recorded type and arity rather than being dropped.
    assert len([w for w in model.words if w.tier == "unsettled"]) == 7
    unsettled = [row for row in model.slots if row.tier == "unsettled"]
    assert len(unsettled) == 7
    for row in unsettled:
        if row.verdict_target == "-" and not row.port_name:
            # 0019/6: an unsettled-signature slot whose verdict row is closed (`dead` at `-`) and
            # that nothing reaches emits no port callable at all; the arity is still recorded.
            continue
        assert row.port_name
        assert len(row.params_port) == len([p for p in row.params.split(",") if p.strip()])


@pytest.mark.corpus
def test_check_mode_matches_the_committed_files(model):
    if not REPO.joinpath(*gks.CENSUS_OUTPUT).is_file():
        pytest.skip("the census is not committed yet")
    assert gks._emit(REPO.joinpath(*gks.CENSUS_OUTPUT),
                     gks.render_census(model, gks.kl.MODULE), True) == 0
    for owner, surface in gks.SLOT_SURFACES.items():
        assert gks._emit(REPO.joinpath(*surface.inl),
                         gks.render_slots_inl(model, gks.kl.MODULE, owner), True) == 0
        assert gks._emit(REPO.joinpath(*surface.cpp),
                         gks.render_slots_cpp(model, gks.kl.MODULE, owner), True) == 0


def test_the_slot_surface_splits_by_layer():
    # Story 0019/5 step 5: a slot the CAI_BaseNPC table holds is the base's declaration with the
    # base body; the Troika overrides it when its table holds another body, and keeps the base's
    # other overloads of that name visible.
    from dataclasses import replace

    row = _slot(hand="FElysiumNpcBase::Slot33")
    base = replace(row, owner="FElysiumNpcBase", retail="CAI_BaseNPC", override=False, layers=[])
    troika = replace(_slot(), owner="FElysiumNpc", override=True, layers=[], address="0x102b0000")
    row.layers = [base, troika]
    overload = _slot(params="int", params_port=("int32",))
    overload.layers = [replace(overload, owner="FElysiumNpcBase", retail="CAI_BaseNPC", override=False,
                               layers=[])]
    model = gks.Model(words=[], slots=[row, overload], branch=[], classes=[], overrides=[],
                      reserved=set(), family=set(), meta={})
    base_inl = gks.render_slots_inl(model, "vampire.dll", "FElysiumNpcBase")
    troika_inl = gks.render_slots_inl(model, "vampire.dll", "FElysiumNpc")
    assert "\tvirtual int32 Slot33();" in base_inl
    assert "\tvirtual int32 Slot33(int32);" in base_inl
    assert "\tint32 Slot33() override;" in troika_inl
    assert "\tusing FElysiumNpcBase::Slot33;" in troika_inl
    base_cpp = gks.render_slots_cpp(model, "vampire.dll", "FElysiumNpcBase")
    troika_cpp = gks.render_slots_cpp(model, "vampire.dll", "FElysiumNpc")
    # The base's hand body is declared, not defined here; its overload is a base stub.
    assert "int32 FElysiumNpcBase::Slot33()\n" not in base_cpp
    assert "int32 FElysiumNpcBase::Slot33(int32)" in base_cpp
    assert "FireKernelBaseSlot(TEXT(\"CAI_BaseNPC::Slot33\")" in base_cpp
    # The Troika override is its own stub, firing its own address under its owner's name (step 6).
    assert "int32 FElysiumNpc::Slot33()" in troika_cpp
    assert 'TEXT("0x102b0000")' in troika_cpp
    assert 'FireKernelSlot(TEXT("CAI_BaseNPCTroika::Slot33")' in troika_cpp


def test_chain_bodies_split_by_owner_and_collapse_the_toggle():
    # Story 0019/5 step 6: a slot is declared by its introducer's port class with that class's own
    # body and overridden where a table refills it. CBaseToggle has no port node: its body stands on
    # FElysiumAnimating, and CBaseAnimating's own body wins there when it has one.
    tables = {cls: {} for cls in gks.CHAIN}
    for cls in gks.CHAIN:
        tables[cls][10] = "e0"            # every table holds CBaseEntity's body: one row
        tables[cls][11] = "e1"
    for cls in gks.CHAIN[1:]:
        tables[cls][11] = "t1"            # CBaseToggle refills 11, everyone below inherits it
    for cls in gks.CHAIN[5:]:
        tables[cls][12] = "c2"            # introduced by CBaseCombatCharacter
    tables["CAI_BaseNPC"][12] = "n2"      # the NPC base refills it
    tables["CAI_BaseNPCTroika"][12] = "n2"
    assert gks.chain_bodies(10, tables) == [("FElysiumEntity", "CBaseEntity", "e0")]
    assert gks.chain_bodies(11, tables) == [("FElysiumEntity", "CBaseEntity", "e1"),
                                            ("FElysiumAnimating", "CBaseToggle", "t1")]
    assert gks.chain_bodies(12, tables) == [("FElysiumCombatCharacter", "CBaseCombatCharacter", "c2"),
                                            ("FElysiumNpcBase", "CAI_BaseNPC", "n2")]


def test_a_subclass_member_is_a_collision_for_the_chain(tmp_path):
    # A class below the NPC line that declares a chain slot's name overrides or hides the virtual
    # every entity now inherits; the scan names it, and a local inside a method body is not a member.
    root = tmp_path / "Source" / "ElysiumUE" / "Private"
    root.mkdir(parents=True)
    (root / "Door.h").write_text(
        "class FElysiumDoor final : public FElysiumEntity\n{\npublic:\n\tvirtual void MoveDone();\n\tvoid Think()\n\t{\n\t\tstatic const FName Local(TEXT(\"x\"));\n\t}\n};\nclass FElysiumNpcThing final : public FElysiumNpc\n{\npublic:\n\tvoid Precache() override;\n};\n",
        encoding="utf-8")
    names = gks.chain_subclass_names(tmp_path)
    assert names["MoveDone"] == {"FElysiumDoor"}
    assert "Local" not in names
    assert "Precache" not in names       # the NPC line overrides on purpose



def test_a_subclass_override_is_told_from_a_hide(tmp_path):
    # 0019 story 5 commit B: an `OVERRIDDEN_BELOW` slot needs every declaring class below the NPC
    # line to say `override`; the scan collects which ones do.
    root = tmp_path / "Source" / "ElysiumUE" / "Private"
    root.mkdir(parents=True)
    (root / "Weapon.h").write_text(
        "class FElysiumWeapon final : public FElysiumEntity\n{\npublic:\n\tvirtual void Hide() override;\n"
        "\tvoid Unhide(FElysiumCombatCharacter* Wearer);   // not an override\n};\n",
        encoding="utf-8")
    overrides: dict[str, set[str]] = {}
    names = gks.chain_subclass_names(tmp_path, overrides)
    assert names["Hide"] == {"FElysiumWeapon"} and overrides.get("Hide") == {"FElysiumWeapon"}
    assert names["Unhide"] == {"FElysiumWeapon"} and "Unhide" not in overrides


def test_no_slot_is_an_accepted_hide():
    # The accepted-hide kind is gone with commit B: every slot a class below the NPC line declares
    # is a real override of it.
    kinds = {kind for kind, _, _ in gks.SLOT_PORT_MAP.values()}
    assert "accepted" not in kinds
    # Slot 158 left the set: retail's CBasePlayer and CHL2_Player fill slot 158 with the base
    # CBaseEntity::IsAlive 0x100b4dc0 (vtmb_slot 158), so the player's override is gone and the one
    # base body reads m_lifeState (+0x200) for every class.
    assert {s for s, (k, _, _) in gks.SLOT_PORT_MAP.items() if k == gks.OVERRIDDEN_BELOW} == {66, 67, 86, 123, 133, 153}
